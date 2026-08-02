import QtQuick
import Quickshell
import Quickshell.Io
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
 * is the one widget not recommended on by default. It is also why it carries no
 * chrome: a panel, a shadow and a scanline field behind sixty-per-second
 * geometry is the one place on this desktop where that would actually cost
 * something.
 *
 * It spans the screen, so it drags vertically and no further — there is nowhere
 * sideways for a full-width strip to go.
 */
WidgetFrame {
    id: root

    widgetId: "visualizer"
    shown: WidgetState.visualizer && WidgetState.haveCava
    accent: Theme.magenta

    // Bottom, not Background: it should sit above the wallpaper but let windows
    // cover it, which is what Bottom means. On Background a maximised window
    // would still be over it, but so would nothing else — and the wallpaper
    // picker draws there.
    fillWidth: true
    chrome: false
    dragX: false

    homeEdgeV: "bottom"
    homeMarginX: 0
    homeMarginY: 0

    bodyHeight: 110

    property var levels: []
    // Peak hold, decayed on every frame cava sends rather than by a timer of
    // its own: the caps are only interesting while something is playing, and
    // that is exactly when frames are arriving.
    property var peaks: []
    readonly property int barCount: 48

    Row {
        id: strip
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: parent.height
        spacing: 2

        Repeater {
            model: root.barCount

            delegate: Item {
                id: bar
                required property int index

                readonly property real level:
                    index < root.levels.length ? root.levels[index] / 100 : 0
                readonly property real peak:
                    index < root.peaks.length ? root.peaks[index] / 100 : 0

                width: (strip.width - (root.barCount - 1) * strip.spacing) / root.barCount
                height: parent.height
                anchors.bottom: parent.bottom

                // Cyan at rest and magenta at the top of its travel, as a
                // gradient up the bar rather than one flat colour for the whole
                // thing — so the spectrum reads as a spectrum even when every
                // band happens to be at the same height.
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: Math.max(2, bar.height * bar.level)
                    opacity: 0.55 + 0.45 * bar.level
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.magenta }
                        GradientStop { position: 1.0; color: Theme.cyan }
                    }
                    // Short, or the bars lag the music and it looks wrong
                    // rather than smooth. cava already does its own smoothing.
                    Behavior on height { NumberAnimation { duration: 60 } }
                }

                // The cap is what makes a transient visible at all: the bar
                // itself is back down before the eye has finished with it.
                Rectangle {
                    anchors { left: parent.left; right: parent.right }
                    y: bar.height - Math.max(2, bar.height * bar.peak) - 2
                    height: 2
                    color: Theme.fg
                    opacity: bar.peak > 0.02 ? 0.5 : 0
                    Behavior on y { NumberAnimation { duration: 90 } }
                }
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
                if (out.length === 0) return

                const prev = root.peaks
                const held = []
                for (let i = 0; i < out.length; i++) {
                    const was = i < prev.length ? prev[i] : 0
                    held.push(Math.max(out[i], was - 1.6))
                }
                root.levels = out
                root.peaks = held
            }
        }
    }
}
