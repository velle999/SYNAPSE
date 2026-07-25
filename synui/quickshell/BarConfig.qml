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

    // Forces the blocking read above to happen NOW — while this singleton is
    // being constructed, which is before the first `get()` call that referenced
    // it can return, and therefore before any PanelWindow exists. Without a
    // reader the FileView would sit idle until something asked for its text,
    // and `blockLoading` would have nothing to block on.
    Component.onCompleted: root.parse(configFile.text())
}
