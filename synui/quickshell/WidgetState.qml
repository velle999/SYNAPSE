pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * WidgetState — which desktop widgets are switched on, and where they go.
 *
 * Everything here is OFF by default. These are decoration: a fresh install, the
 * live ISO, and everyone upgrading gets exactly the desktop they had until they
 * ask for one. `synui-widgets` writes the file, synui's Super+Shift+A bind and
 * the control panel row both run that same command, and this watches the result
 * — so there is one writer and one format, however the toggle was reached.
 *
 * Same idiom as Theme.qml's theme.json, and for the same reason: watching a
 * file means a toggle repaints instantly with no restart and no IPC.
 */
QtObject {
    id: root

    property bool visualizer: false
    property bool sysmon:     false
    property bool clock:      false
    property bool launcher:   false
    property bool postit:     false
    property bool pizza:      false
    property bool tux:        false
    property bool analog:     false
    property bool music:      false
    property bool weather:    false

    // Desktop widgets live on ONE screen. A clock and a system monitor
    // duplicated onto three monitors is noise, not a rice — so they follow the
    // primary output, which is the one synui itself calls primary.
    property string primaryOutput: ""

    property FileView stateFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/widgets.state"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.parse(this.text())
        // No file at all is the normal case, not an error: it means nobody has
        // turned a widget on yet.
        onLoadFailed: root.reset()
    }

    function reset() {
        root.visualizer = false
        root.sysmon     = false
        root.clock      = false
        root.launcher   = false
        root.postit     = false
        root.pizza      = false
        root.tux        = false
        root.analog     = false
        root.music      = false
        root.weather    = false
    }

    function parse(text) {
        root.reset()
        for (const raw of String(text).split("\n")) {
            const line = raw.trim()
            if (line === "" || line.startsWith("#")) continue
            const eq = line.indexOf("=")
            if (eq < 0) continue
            const key = line.slice(0, eq).trim()
            const on  = line.slice(eq + 1).trim() === "on"
            switch (key) {
            case "visualizer": root.visualizer = on; break
            case "sysmon":     root.sysmon     = on; break
            case "clock":      root.clock      = on; break
            case "launcher":   root.launcher   = on; break
            case "postit":     root.postit     = on; break
            case "pizza":      root.pizza      = on; break
            case "tux":        root.tux        = on; break
            case "analog":     root.analog     = on; break
            case "music":      root.music      = on; break
            case "weather":    root.weather    = on; break
            }
        }
    }

    property Process outputProbe: Process {
        running: true
        command: ["synctl", "outputs"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    for (const o of JSON.parse(this.text)) {
                        if (o.primary) { root.primaryOutput = o.name; return }
                    }
                } catch (e) { /* keep whatever we had */ }
            }
        }
    }

    // Re-probe when the monitor set changes: unplugging the primary makes synui
    // promote another, and a widget pinned to a screen that no longer exists is
    // a widget nobody can see.
    property Connections screenConn: Connections {
        target: Quickshell
        function onScreensChanged() { outputProbe.running = true }
    }

    // cava is an optdepend. Without it the visualizer must stay dark rather
    // than spawn a failing process 60 times a second.
    property bool haveCava: false
    property Process cavaProbe: Process {
        running: true
        command: ["sh", "-c", "command -v cava >/dev/null 2>&1 && echo yes"]
        stdout: StdioCollector {
            onStreamFinished: root.haveCava = this.text.trim() === "yes"
        }
    }
}
