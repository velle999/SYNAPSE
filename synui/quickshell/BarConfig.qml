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

    property FileView configFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/bar.json"
        watchChanges: true
        // The bar is both reader and writer here, so a torn read is a real
        // possibility rather than a theoretical one.
        atomicWrites: true
        onFileChanged: reload()
        onLoaded: {
            try {
                const j = JSON.parse(this.text())
                if (j && typeof j === "object") root.perOutput = j
            } catch (e) {
                // Keep what is in memory. Blanking every monitor's layout
                // because one write raced a read would be far worse than
                // ignoring a bad parse.
            }
        }
        // Absent file is the normal case: nobody has changed anything yet.
        onLoadFailed: root.perOutput = ({})
    }
}
