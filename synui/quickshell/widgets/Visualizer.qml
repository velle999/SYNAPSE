import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import ".."

/*
 * Audio visualiser — a cava-driven spectrum across the bottom of the screen.
 *
 * cava does the work because there is no spectrum to be had otherwise:
 * quickshell's PipeWire binding exposes a single `peak` level, not bands, and
 * an FFT in QML over raw audio is not a thing this desktop should grow. cava
 * already knows how to capture the sink monitor and band it.
 *
 * The config is written at spawn instead of shipped as a file. cava only takes
 * a config PATH, and a second installed file is one more thing that can go
 * missing or fall out of step with the tree it belongs to — this way the
 * visualiser carries its own settings and works identically from the packaged
 * tree or a user copy.
 *
 * COST, stated plainly: this repaints at 60fps for as long as it is on, silence
 * included (cava emits zeros, not nothing). That is why it is opt-in and why it
 * is the one widget not recommended on by default.
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    visible: WidgetState.visualizer
             && WidgetState.haveCava
             && modelData.name === WidgetState.primaryOutput

    // Bottom, not Background: it should sit above the wallpaper but let windows
    // cover it, which is what Bottom means. On Background a maximised window
    // would still be over it, but so would nothing else — and the wallpaper
    // picker draws there.
    WlrLayershell.layer: WlrLayer.Bottom

    anchors { bottom: true; left: true; right: true }
    implicitHeight: 110

    // Decoration must never reserve space or eat a click. Without the empty
    // mask this is a full-width invisible strip that swallows every click along
    // the bottom of the screen.
    exclusiveZone: 0
    focusable: false
    mask: Region {}
    color: "transparent"

    property var levels: []
    readonly property int barCount: 48

    Row {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: parent.height
        spacing: 2

        Repeater {
            model: root.barCount

            delegate: Rectangle {
                required property int index

                readonly property real level:
                    index < root.levels.length ? root.levels[index] / 100 : 0

                width: (root.width - (root.barCount - 1) * 2) / root.barCount
                height: Math.max(2, root.height * level)
                anchors.bottom: parent.bottom
                radius: 2
                opacity: 0.75

                // Bars take the accent at the top of their travel and the glyph
                // colour at rest, so the whole thing moves through the theme
                // rather than sitting on one flat colour.
                color: Qt.rgba(
                    Theme.magenta.r * level + Theme.cyan.r * (1 - level),
                    Theme.magenta.g * level + Theme.cyan.g * (1 - level),
                    Theme.magenta.b * level + Theme.cyan.b * (1 - level),
                    1)

                // Short, or the bars lag the music and it looks wrong rather
                // than smooth. cava already does its own smoothing.
                Behavior on height { NumberAnimation { duration: 60 } }
            }
        }
    }

    Process {
        id: cava
        // Only while visible: a hidden visualiser must not keep cava running,
        // capturing audio and waking the CPU 60 times a second for nothing.
        running: root.visible
        command: ["sh", "-c",
            "conf=\"${XDG_RUNTIME_DIR:-/tmp}/synui-cava.conf\"; " +
            "printf '%s\\n' " +
            "'[general]' 'mode = normal' 'framerate = 60' 'bars = 48' " +
            "'[input]' 'method = pipewire' 'source = auto' " +
            "'[output]' 'method = raw' 'raw_target = /dev/stdout' " +
            "'data_format = ascii' 'ascii_max_range = 100' 'channels = mono' " +
            "> \"$conf\"; exec cava -p \"$conf\""]

        stdout: SplitParser {
            splitMarker: "\n"
            onRead: (line) => {
                const s = line.trim()
                if (s === "") return
                // "v;v;v;…;" — the trailing separator leaves an empty last
                // field, which parses to NaN and would draw a bar of height 0
                // at the end forever.
                const parts = s.split(";")
                const out = []
                for (const p of parts) {
                    if (p === "") continue
                    const n = parseInt(p, 10)
                    out.push(isNaN(n) ? 0 : n)
                }
                if (out.length > 0) root.levels = out
            }
        }
    }
}
