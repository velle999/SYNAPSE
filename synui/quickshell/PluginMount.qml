import QtQuick
import Quickshell

/*
 * PluginMount — one plugin's session-scoped entry points.
 *
 * ⚠ ONCE PER SESSION, NOT ONCE PER MONITOR, and that is the whole reason this
 * is not in Bar.qml. Bar.qml's Repeater instantiates the `bar-widget` entry
 * point per screen, which is right for something that appears on every bar and
 * wrong for everything here: flappy's `panel` is a game loop, chess's `service`
 * is a board with a clock on it. Three copies of either would be three
 * simulations racing over one saved state. Both plugins say so in their own
 * headers — "the shell hands a plugin that declares both to the panel loader
 * rather than to the bar, so this is mounted exactly ONCE no matter how many
 * monitors are attached".
 *
 * So this is instantiated from shell.qml, where there is one of everything, and
 * the bar reaches what it creates through PluginHost.
 *
 * ⛔ THROUGH A Loader, FOR THE REASON Bar.qml GIVES ONE DIRECTORY UP: this is
 * somebody else's QML in this process. A syntax error, a missing import or a
 * type that does not resolve has to cost that one plugin and not the shell —
 * and the shell is the thing you would use to fix it.
 */
Item {
    id: mount

    /* One row out of Plugins.active: { id, name, description, dir, entry,
     * panelEntry, serviceEntry, … }. */
    required property var modelData

    /* Never rendered — the entry points here own their own windows (flappy a
     * WlrLayershell PanelWindow, chess a FloatingWindow) and nothing in this
     * item is ever put on a screen. It exists to be a parent for the two
     * Loaders and for nothing else. */
    visible: false

    /*
     * What a plugin is handed as `manifest`.
     *
     * ⚠ BUILT FROM THE SCAN, NOT PARSED HERE. synui-plugins is the one program
     * that reads a manifest.json — that split is what stops the command line
     * and the bar coming to different conclusions about what is installed — and
     * a JSON.parse in this file would be a second parser for the same file.
     * What the installed corpus actually reads off `manifest` is `.id`; the
     * rest is what the scan already carries.
     */
    readonly property var manifest: ({
        id: mount.modelData.id,
        name: mount.modelData.name,
        description: mount.modelData.description,
        dir: mount.modelData.dir
    })

    function entryUrl(file) {
        return (file && mount.modelData.dir)
               ? "file://" + mount.modelData.dir + "/" + file : ""
    }

    /*
     * Hand over the four things the host injects, skipping any the plugin did
     * not declare.
     *
     * ⛔ AN UNCONDITIONAL ASSIGNMENT ABORTS THE REST OF THE BLOCK. "Cannot
     * assign to non-existent property" is a JS exception, not a warning: it
     * throws out of onLoaded, so the assignments AFTER it never run and the
     * registration at the end never happens. flappy's panel declares `shell`
     * and nothing else — no `omarchyPath` — so injecting that first left it
     * registered nowhere, and the widget went on doing nothing exactly as it
     * had before any of this existed. The failure looked identical to the bug
     * it was meant to fix.
     *
     * Only `shell` is required of a panel, and only because there is nothing
     * for it to be part of without one.
     */
    function inject(item, service) {
        if (!item) return
        if (item.omarchyPath !== undefined) item.omarchyPath = mount.modelData.dir
        if (item.manifest    !== undefined) item.manifest    = mount.manifest
        if (item.service     !== undefined) item.service     = service
        item.shell = PluginHost
    }

    /*
     * ── The service ─────────────────────────────────────────────────────────
     *
     * Loaded EAGERLY and never unloaded while the plugin is on, because the bar
     * widget reads it before anything is opened: chess's summaryText() asks
     * `service.snapshot` on its very first paint, and a service that appeared
     * only when a panel did would leave "♞ Chess" in the bar for a game already
     * in progress.
     */
    Loader {
        id: serviceLoader
        asynchronous: true
        source: mount.entryUrl(mount.modelData.serviceEntry)

        onLoaded: {
            mount.inject(item, null)
            PluginHost.registerService(mount.modelData.id, item)
        }

        /*
         * ⚠ A FAILED LOAD MUST SAY SO. The bar's own plugin Loader can afford
         * silence — a widget that does not load takes no width and the gap is
         * visible — but a service that fails leaves a widget that draws
         * perfectly and answers nothing, which reads as the plugin being idle
         * rather than broken. That is the exact failure this whole file exists
         * to stop happening quietly.
         */
        onStatusChanged: if (status === Loader.Error)
            console.warn("plugin service failed to load:",
                         mount.modelData.id, source)
    }

    /*
     * ── The panel ───────────────────────────────────────────────────────────
     *
     * Also eager, and that is a decision rather than symmetry: a lazy panel
     * cannot answer `opened`, so PluginHost.toggle() would have to create one
     * to ask whether it was already up. Both installed panels cost nothing
     * while closed — flappy's loop is a FrameAnimation that runs only while
     * open, chess's window is simply not visible — so eager buys a correct
     * toggle for no frames.
     *
     * ⚠ asynchronous, so a heavy panel cannot stall the bar's first frame —
     * which means a click CAN land before the panel exists. PluginHost holds
     * the intent and registerPanel() replays it.
     */
    Loader {
        id: panelLoader
        asynchronous: true
        source: mount.entryUrl(mount.modelData.panelEntry)

        onLoaded: {
            /* Chess's panel takes the service as well, and takes it from the
             * host rather than reaching for a sibling: the two entry points are
             * separate files and only the host knows both are up. */
            mount.inject(item, PluginHost.serviceFor(mount.modelData.id))
            PluginHost.registerPanel(mount.modelData.id, item)
        }

        onStatusChanged: if (status === Loader.Error)
            console.warn("plugin panel failed to load:",
                         mount.modelData.id, source)
    }

    /* The service can finish loading after the panel does — two asynchronous
     * Loaders in one item have no order — so the panel's `service` is set again
     * when the service arrives. Assigning the same value twice is free; leaving
     * a panel holding null is a chess board that never draws a piece. */
    Connections {
        target: PluginHost
        function onServicesChanged() {
            if (panelLoader.item && panelLoader.item.service !== undefined)
                panelLoader.item.service = PluginHost.serviceFor(mount.modelData.id)
        }
    }

    /* Declared BEFORE the Loader resolves, so a click landing in between is
     * held rather than discarded — and so a plugin with no panel at all is
     * never reported as having one open. See PluginHost.expected. */
    Component.onCompleted:
        PluginHost.expectPanel(mount.modelData.id, mount.modelData.panelEntry !== "")

    Component.onDestruction: {
        PluginHost.registerService(mount.modelData.id, null)
        PluginHost.registerPanel(mount.modelData.id, null)
        PluginHost.expectPanel(mount.modelData.id, false)
    }
}
