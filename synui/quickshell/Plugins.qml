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

    /*
     * ── Turning a plugin on or off, and reordering it ───────────────────────
     *
     * execDetached, never a shared Process: PostItState.qml and PluginHost.qml
     * both carry the same note — a Process object runs ONE child at a time, so
     * clicking two checkboxes in the bar menu before the first exits would
     * silently drop the second (Quickshell's own `running = true` on an
     * already-running Process is a no-op, not a queue). There is nothing here
     * to read back either: `synui-plugins` writes plugins.state / plugins-
     * order.state, and the FileView watches below bring the change back with
     * no IPC, exactly like every toggle already on this menu.
     */
    function setEnabled(id, on) {
        Quickshell.execDetached(["synui-plugins", id, on ? "on" : "off"])
    }
    function moveUp(id)   { Quickshell.execDetached(["synui-plugins", id, "up"]) }
    function moveDown(id) { Quickshell.execDetached(["synui-plugins", id, "down"]) }

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

    /* Same arrangement, for `synui-plugins <id> up|down`'s file: a rescan
     * brings the reordered `all` (and so `active`) back with no IPC. `scan`
     * itself does the reordering — this file only says WHEN to ask again. */
    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/plugins-order.state"
        watchChanges: true
        onFileChanged: root.rescan()
        // No file is the ordinary case: nobody has moved a plugin yet, and the
        // scan's own order (install order) stands.
        onLoadFailed: {}
    }
}
