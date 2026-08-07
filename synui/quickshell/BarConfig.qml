pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * BarConfig — what the bar shows, PER MONITOR.
 *
 * Per monitor because a 1080-wide portrait panel cannot hold what a 2560-wide
 * landscape one can: with the desktop switcher on the left, the clock centred
 * and the status group on the right, the clock ends up drawn straight through
 * the tray and the media title. One global setting would mean giving up the
 * workspace pills on the wide monitors to fix the narrow one.
 *
 * Written by the bar itself (the right-click menu) rather than by a helper —
 * unlike widgets.state, where synui's keybind and the control panel also need a
 * way in. Nothing outside the bar sets these, so a CLI would be a second writer
 * with nobody to use it.
 *
 * Unknown monitors simply are not in the file and fall back to the defaults
 * below, so plugging in a new screen gives a working bar with no setup.
 */
QtObject {
    id: root

    // Everything on, nothing hidden: the bar a fresh install has always had.
    readonly property var defaults: ({
        "autohide":   false,
        "workspaces": true,
        "media":      true,
        "clock":      true,
        "tray":       true,
        "sysinfo":    true,     // cpu + memory
        "volume":     true,
        "netbt":      true      // network + bluetooth
    })

    // Human labels for the menu, in menu order.
    readonly property var rows: [
        { key: "autohide",   label: "Auto-hide bar" },
        { key: "workspaces", label: "Desktop switcher" },
        { key: "media",      label: "Media preview" },
        { key: "clock",      label: "Clock" },
        { key: "tray",       label: "System tray" },
        { key: "sysinfo",    label: "CPU + memory" },
        { key: "volume",     label: "Volume" },
        { key: "netbt",      label: "Network + Bluetooth" }
    ]

    property var perOutput: ({})

    /* ── Which edge the bar sits on ──────────────────────────────────────────
     *
     * GLOBAL, unlike everything above it, and read out of synui's config rather
     * than bar.json. Both of those are deliberate.
     *
     * Global because it is the bar's answer to the dock's edge, and that is one
     * setting for the whole desktop: furniture on the bottom of one monitor and
     * the top of the next is a layout nobody asks for by accident, and the
     * control panel row that sets it has no monitor to scope itself to.
     *
     * Out of synui's config because the control panel is the writer. bar.json is
     * the bar's OWN file — the right-click menu writes it and nothing else does,
     * which is exactly why it can be written back without a second writer to
     * race. A control-panel row writing bar.json would introduce one.
     *
     * So the flow is the same as every other setting on that panel: the row
     * writes `bar_edge` into settings.state through settings.c, and this reads
     * it back. synuirc first, then settings.state on top, because that is
     * synui's own precedence (synui_config_load) — a value someone changed in a
     * panel a minute ago beats the line they wrote in synuirc last year.
     */
    readonly property bool atBottom: root.edge === "bottom"
    property string edge: "top"

    // Where a popup hanging off the bar starts, given its height. One place
    // rather than a `Theme.barHeight + 2` at each anchor site: a bottom bar's
    // popups have to go UP, and every one of those sites would otherwise be a
    // separate chance to forget.
    function popupY(h) {
        return root.atBottom ? -h - 2 : Theme.barHeight + 2
    }

    // `key = value`, synuirc's language, which is also settings.state's. Only
    // ever asked for keys the bar cares about, so this does not need to be a
    // parser — it needs to find one line and ignore everything it does not
    // understand, including the repeated keys (bind, autostart) that a real
    // parser would have to model.
    function readKey(text, key) {
        if (!text) return ""
        for (const raw of text.split("\n")) {
            const line = raw.trim()
            if (!line || line.startsWith("#")) continue
            const eq = line.indexOf("=")
            if (eq < 0 || line.slice(0, eq).trim() !== key) continue
            let val = line.slice(eq + 1).trim()
            // Inline comment, whitespace-preceded — config.c's rule, and the
            // reason it is not simply "cut at the first #" is that a value can
            // legitimately BEGIN with one (`border_color_focus = #ff296d`).
            const hash = val.search(/\s#/)
            if (hash >= 0) val = val.slice(0, hash).trim()
            return val
        }
        return ""
    }

    function applyEdge() {
        // settings.state wins where it has the key, synuirc where it does not.
        const v = root.readKey(settingsFile.text(), "bar_edge")
                  || root.readKey(synuircFile.text(), "bar_edge")
        root.edge = (v === "bottom") ? "bottom" : "top"
    }

    property FileView synuircFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/synuirc"
        watchChanges: true
        // Same reason as bar.json below: the edge decides which side the
        // exclusive zone is reserved on, and that has to be right at the
        // surface's first configure.
        blockLoading: true
        onFileChanged: reload()
        onLoaded: root.applyEdge()
        onLoadFailed: root.applyEdge()
    }

    property FileView settingsFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/settings.state"
        watchChanges: true
        blockLoading: true
        onFileChanged: reload()
        onLoaded: root.applyEdge()
        // Absent is the normal case — settings.state only exists once something
        // on the control panel has been changed.
        onLoadFailed: root.applyEdge()
    }

    function get(output, key) {
        const o = root.perOutput[output]
        if (o && o[key] !== undefined) return o[key] === true
        return root.defaults[key] === true
    }

    function set(output, key, value) {
        // Rebuild rather than mutate: assigning a NEW object is what makes every
        // binding on perOutput re-evaluate. Mutating in place changes the data
        // and repaints nothing.
        const next = {}
        for (const k in root.perOutput) next[k] = Object.assign({}, root.perOutput[k])
        if (!next[output]) next[output] = {}
        next[output][key] = value === true
        root.perOutput = next

        // Persist. The reload this triggers parses back the same values, so the
        // watch is harmless rather than a write loop.
        configFile.setText(JSON.stringify(next, null, 2) + "\n")
    }

    function toggle(output, key) { root.set(output, key, !root.get(output, key)) }

    function parse(text) {
        try {
            const j = JSON.parse(text)
            if (j && typeof j === "object") root.perOutput = j
        } catch (e) {
            // Keep what is in memory. Blanking every monitor's layout because
            // one write raced a read would be far worse than ignoring a bad
            // parse.
        }
    }

    property FileView configFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/bar.json"
        watchChanges: true
        // The bar is both reader and writer here, so a torn read is a real
        // possibility rather than a theoretical one.
        atomicWrites: true

        /*
         * THE FIRST READ MUST BE SYNCHRONOUS, and this is not a preference.
         *
         * `autohide` decides the bar's exclusive zone, and quickshell only ever
         * sends set_exclusive_zone ONCE per surface unless the value changes
         * AFTER that surface's first configure. An async read landed inside
         * exactly that window — traced: get_layer_surface,
         * set_exclusive_zone(28), then this file loading and flipping the
         * property to 0, and only THEN the first configure. Wayland never saw
         * the 0. So an auto-hiding bar reserved its 28px forever: it slid out of
         * sight and left the strip of bare desktop above every maximized window
         * that auto-hide exists to avoid, with the QML property reading 0 the
         * whole time.
         *
         * Loading before any window exists also means the per-monitor module
         * switches are right on the first frame instead of popping in.
         */
        blockLoading: true

        onFileChanged: reload()
        onLoaded: root.parse(this.text())
        // Absent file is the normal case: nobody has changed anything yet.
        onLoadFailed: root.perOutput = ({})
    }

    // Forces the three blocking reads above to happen NOW, for the reason given
    // on configFile: without a reader a FileView sits idle and `blockLoading`
    // has nothing to block on. The edge is read here for the same reason
    // `autohide` is — it decides which side the exclusive zone is reserved on,
    // and quickshell sends that once per surface.
    Component.onCompleted: {
        root.parse(configFile.text())
        root.applyEdge()
    }
}
