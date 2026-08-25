pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * Plugins — the bar's third-party widgets, in Omarchy's shell-plugin format.
 *
 * ⚠ THE SCAN IS synui-plugins' AND NOT THIS FILE'S, and that is the same split
 * every other list in this bar uses: the shell script walks the plugin
 * directories, reads each manifest and decides what can be hosted, and this
 * reads its TSV. One place parses a manifest, so the command line and the bar
 * cannot come to different conclusions about what is installed — which for a
 * plugin system is the difference between "it is off" and "it was refused".
 *
 * The refusals matter as much as the list. An Omarchy widget that imports
 * qs.Ui, qs.Commons or Quickshell.Hyprland cannot run here — those are their
 * own singletons and a compositor socket synui does not have — and a plugin
 * system whose answer to that is an empty space on the bar is a plugin system
 * nobody can debug. `unsupported` carries the reason and the settings pane
 * shows it.
 */
Singleton {
    id: root

    /* One entry per installed bar-widget plugin:
     *   { id, name, description, dir, entry, enabled, unsupported,
     *     panelEntry, serviceEntry } */
    property var all: []

    /* The ones the bar should actually instantiate: enabled, and hostable.
     * Filtered HERE rather than in the delegate so a refused plugin cannot be
     * loaded by a Repeater that forgot to check. */
    readonly property var active:
        root.all.filter(p => p.enabled && p.unsupported === "")

    /*
     * The ones with something to mount ONCE PER SESSION rather than once per
     * monitor — a `panel` or a `service` entry point. shell.qml's Variants
     * model; see PluginMount for why they cannot live on the bar.
     *
     * ⚠ A SEPARATE LIST RATHER THAN A CHECK IN THE DELEGATE, for the same
     * reason `active` is one: a mount instantiated for a plugin with neither
     * entry point would be an Item and two idle Loaders per installed plugin,
     * created and destroyed on every rescan.
     */
    readonly property var sessionScoped:
        root.active.filter(p => p.panelEntry !== "" || p.serviceEntry !== "")

    property bool scanned: false

    function rescan() { scanProc.running = true }

    /* One row out of `all`, by id — what a bar slot resolves its plugin
     * through now that the model below carries ids rather than rows. Reads
     * `all`, so a binding on it re-evaluates when a scan lands. */
    function rowFor(id) {
        for (let i = 0; i < root.all.length; i++)
            if (root.all[i].id === id) return root.all[i]
        return null
    }

    /*
     * ── The bar's model, and why it is not just `active` ────────────────────
     *
     * ⛔ A Repeater OVER A JS ARRAY REBUILDS EVERY DELEGATE WHEN THE ARRAY IS
     * REASSIGNED. It has no way not to: an array carries no identity per entry,
     * so "the same widget, one place to the left" and "thirteen different
     * widgets" look exactly alike to it. Every scan therefore tore down and
     * reloaded every plugin on every bar — the weather widget refetched, the
     * vitals timer restarted, tetris forgot its board — and a scan is what
     * lands after any reorder, any checkbox, and anything at all touching
     * plugins.state.
     *
     * A ListModel of ids has identity. `move()` on it moves the ITEM, so
     * reordering costs a re-layout and no loads at all, and a scan that
     * changes nothing (much the commonest kind — the file watches ask for one
     * whenever anything writes) produces no model operations and so no churn.
     *
     * ONE model for every bar, because they all draw the same list: a Repeater
     * per monitor sharing one model is ordinary QML, and the alternative is
     * each bar diffing the same list against its own copy.
     */
    ListModel { id: activeIds }
    readonly property ListModel activeModel: activeIds

    onActiveChanged: root.syncModel()
    Component.onCompleted: root.syncModel()

    function syncModel() {
        const want = root.active

        /* Gone first, so the insert pass below can trust that anything still
         * in the model is still wanted and only its POSITION is in question. */
        for (let i = activeIds.count - 1; i >= 0; i--) {
            const have = activeIds.get(i).pid
            if (!want.some(p => p.id === have)) activeIds.remove(i)
        }

        for (let j = 0; j < want.length; j++) {
            const id = want[j].id
            let at = -1
            /* From j, not from 0: everything before it is already in place. */
            for (let k = j; k < activeIds.count; k++)
                if (activeIds.get(k).pid === id) { at = k; break }
            if (at < 0)        activeIds.insert(j, { pid: id })
            else if (at !== j) activeIds.move(at, j, 1)
        }
    }

    /*
     * ── Turning a plugin on or off ──────────────────────────────────────────
     *
     * execDetached, never a shared Process: PostItState.qml and PluginHost.qml
     * both carry the same note — a Process object runs ONE child at a time, so
     * clicking two checkboxes in the bar menu before the first exits would
     * silently drop the second (Quickshell's own `running = true` on an
     * already-running Process is a no-op, not a queue). There is nothing here
     * to read back either: `synui-plugins` writes plugins.state, and the
     * FileView watch below brings the change back with no IPC, exactly like
     * every toggle already on this menu.
     *
     * ⚠ THIS ONE STILL WAITS FOR THE SCRIPT, and reordering (below) no longer
     * does. The difference is what the answer depends on: a checkbox changes
     * which plugins are MOUNTED, and enabling one can be the first time its
     * directory is looked at — the rescan is doing real work. A move only
     * changes the sequence of a list already in hand.
     */
    function setEnabled(id, on) {
        Quickshell.execDetached(["synui-plugins", id, on ? "on" : "off"])
    }

    /*
     * ── Moving a plugin along the bar ───────────────────────────────────────
     *
     * ⚠ THE ROW MOVES HERE, NOT WHEN THE SCRIPT ANSWERS. `synui-plugins <id>
     * up` did the whole job and the bar waited for it to come back through the
     * file watch: a scan to work out the current order (a walk of every plugin
     * directory, a read of every manifest, a grep of every entry point), a
     * write, an inotify wakeup, and then a SECOND scan to find out what the
     * write had done. About half a second of it, per click on an arrow, for a
     * swap of two rows this file was already holding — which is what
     * "reordering is slow" meant.
     *
     * So the list is reordered right here, in the frame of the click, and the
     * script is told the finished order rather than asked to work one out.
     * `all` is the only thing that decides what the menu draws and what order
     * the bar mounts widgets in, so moving it IS the move; the write below is
     * how it survives a restart, not how it happens.
     *
     * The neighbour is the one the MENU shows. `all` carries the plugins this
     * bar cannot host as well (BarMenu filters them out, and the bar never
     * draws them), so a plain swap with `i - 1` could trade places with a row
     * nobody can see — one click, nothing moves. Unsupported rows are stepped
     * OVER, and keep their own place in the file.
     */
    function move(id, delta) {
        const rows = root.all
        const from = rows.findIndex(p => p.id === id)
        /* Same gate `synui-plugins <id> up` applies: a plugin the bar will not
         * host has no place in the row to move within. */
        if (from < 0 || rows[from].unsupported !== "") return

        let to = from + delta
        while (to >= 0 && to < rows.length && rows[to].unsupported !== "")
            to += delta
        if (to < 0 || to >= rows.length) return  // the ends, where ▴/▾ are dim

        /* A NEW array, never a swap in place — reassigning the same object
         * notifies nothing. The same trap `bars` below carries. */
        const next = rows.slice()
        next.splice(to, 0, next.splice(from, 1)[0])
        root.all = next
        orderWrite.restart()
    }

    function moveUp(id)   { root.move(id, -1) }
    function moveDown(id) { root.move(id, +1) }

    /*
     * ⚠ COALESCED, because two clicks on ▴ are one intention. Each click states
     * the WHOLE order (`synui-plugins order`), so the last writer wins by
     * design — but two detached children racing to write one file can land in
     * the wrong sequence, and the loser's order is the one that survives.
     * Waiting a frame or two collapses a run of clicks into a single write of
     * where the plugin actually ended up.
     */
    Timer {
        id: orderWrite
        interval: 80
        onTriggered: root.writeOrder()
    }

    function writeOrder() {
        /* Never before the first scan: an empty list here would be read as
         * "no plugin has a place any more" and wipe the file. */
        if (!root.scanned || root.all.length === 0) return
        Quickshell.execDetached(
            ["synui-plugins", "order"].concat(root.all.map(p => p.id)))
    }

    /*
     * ── Every bar on the desk, so a widget can reach its peers ──────────────
     *
     * A bar surface exists per monitor and the Variants that makes them lives
     * in shell.qml, so one Bar cannot see its siblings — there is no id to
     * reach and no parent to walk. A singleton is the only place they all meet,
     * which is the same reason Theme and BarConfig are ones.
     *
     * This is what makes BarWidget.broadcast() mean anything: without it a
     * widget refreshing itself updates one screen and leaves the others
     * holding the previous answer.
     *
     * ⚠ A NEW ARRAY, NEVER push(). Reassigning the SAME object does not notify
     * a QML binding — the property has not changed as far as the engine is
     * concerned — so a `bars.push(b)` would register the bar and update nothing
     * that watches. It is the same trap as every other `var` in this tree.
     */
    property var bars: []

    function registerBar(b) {
        if (!b || root.bars.indexOf(b) >= 0) return
        root.bars = root.bars.concat([b])
    }

    function unregisterBar(b) {
        root.bars = root.bars.filter(x => x !== b)
    }

    /* Every live instance of one plugin, across every monitor. Each bar answers
     * for its own slots; this only folds them. */
    function widgetsFor(name) {
        let out = []
        for (let i = 0; i < root.bars.length; i++) {
            const b = root.bars[i]
            if (b && typeof b.pluginWidgets === "function")
                out = out.concat(b.pluginWidgets(name))
        }
        return out
    }

    /* Absolute path to a plugin's entry point, for a Loader's `source`.
     * file:// because the QML engine treats a bare absolute path as relative to
     * the importing document, which would look inside the bar's own tree. */
    function entryUrl(p) {
        return (p && p.dir && p.entry) ? "file://" + p.dir + "/" + p.entry : ""
    }

    Process {
        id: scanProc
        command: ["synui-plugins", "scan"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                const lines = this.text.split("\n")
                /* Row 1 is the header. Read by POSITION and not by name: the
                 * producer is one script in this same package, so the columns
                 * are a contract rather than a guess — and a header-keyed read
                 * would silently produce empty rows if a column were renamed,
                 * where this produces a build-time-obvious break. */
                for (let i = 1; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 7 || !f[0]) continue
                    /* Columns 8 and 9 are the session-scoped entry points, and
                     * they are read with a fallback rather than by widening the
                     * length check above: a scan from an older synui-plugins on
                     * $PATH still produces every bar widget it always did, and
                     * loses only the panels it never knew about. */
                    out.push({ id: f[0], name: f[1], description: f[2],
                               dir: f[3], entry: f[4],
                               enabled: f[5] === "on", unsupported: f[6],
                               panelEntry: f[7] || "", serviceEntry: f[8] || "" })
                }
                root.all = out
                root.scanned = true
            }
        }
    }

    /*
     * plugins.state is synui-plugins' file and this only watches it, exactly as
     * WidgetState watches widgets.state: a toggle from the command line or the
     * control panel reaches the bar with no IPC and no reload.
     *
     * A re-SCAN rather than a re-read of the file, because enabling a plugin is
     * not the only thing that can have happened — `omarchy plugin add` drops a
     * new directory in and the state file is what changes next.
     */
    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/plugins.state"
        watchChanges: true
        onFileChanged: root.rescan()
        /* No file is the ordinary case, not an error: it means nobody has
         * turned a plugin on. The scan above still ran and still listed what is
         * installed. */
        onLoadFailed: {}
    }

    /*
     * Same arrangement, for the order file: `synui-plugins <id> up|down` from
     * a terminal reaches the bar with no IPC, because a rescan brings the
     * reordered `all` (and so `active`) back. `scan` itself does the
     * reordering — this file only says WHEN to ask again.
     *
     * ⚠ AND NOT WHEN THE FILE ALREADY SAYS WHAT THIS BAR IS SHOWING. move()
     * has put the row where it belongs before the write ever went out, so
     * rescanning on our own write would be a quarter of a second of
     * manifest-walking to arrive back at the list already on screen — and it
     * would reassign `all`, which tears down and reloads every plugin widget
     * on the bar a second time: the weather widget refetches, the vitals timer
     * restarts, all of it a beat after the arrow was clicked.
     *
     * ⚠ THE TEST IS THE FILE AGAINST `all`, NOT A FLAG SAYING "THAT WAS MINE".
     * A flag is only ever right about who wrote it, and that is the wrong
     * question: what matters is whether this bar is already showing what the
     * file now says. An `up` typed in a terminal, a hand-edited file, or our
     * own write that landed after something else had already reordered `all`
     * all disagree with it — and all of them rescan.
     */
    property bool orderDirty: false

    FileView {
        id: orderFile
        path: Quickshell.env("HOME") + "/.config/synui/plugins-order.state"
        watchChanges: true

        onFileChanged: { root.orderDirty = true; this.reload() }
        onLoaded: {
            /* The load the path itself triggers at startup, which is not a
             * change and has nothing to bring back: `scan` already applied
             * this exact file. */
            if (!root.orderDirty) return
            root.orderDirty = false
            if (this.text().trim() === root.all.map(p => p.id).join("\n"))
                return
            root.rescan()
        }
        // No file is the ordinary case: nobody has moved a plugin yet, and the
        // scan's own order (install order) stands.
        onLoadFailed: { root.orderDirty = false }
    }
}
