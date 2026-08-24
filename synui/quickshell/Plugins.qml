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
     *   { id, name, description, dir, entry, enabled, unsupported } */
    property var all: []

    /* The ones the bar should actually instantiate: enabled, and hostable.
     * Filtered HERE rather than in the delegate so a refused plugin cannot be
     * loaded by a Repeater that forgot to check. */
    readonly property var active:
        root.all.filter(p => p.enabled && p.unsupported === "")

    property bool scanned: false

    function rescan() { scanProc.running = true }

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
                    out.push({ id: f[0], name: f[1], description: f[2],
                               dir: f[3], entry: f[4],
                               enabled: f[5] === "on", unsupported: f[6] })
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
}
