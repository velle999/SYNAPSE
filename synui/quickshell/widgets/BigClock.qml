import QtQuick
import Quickshell
import Quickshell.Io
import ".."

/*
 * The big desktop clock.
 *
 * Driven by synui-clock, exactly like the bar's clock module and for the same
 * reason: synui's Date & Time panel writes 12/24-hour, seconds and world-clock
 * settings to ~/.config/synui/clock.state, and synui-clock is what reads them.
 * Formatting a date here in QML would give a desktop with two clocks that
 * disagree the moment a toggle is flipped.
 *
 * synui-clock returns one line: {"text": "12:51:35 PM  2026-07-24",
 * "tooltip": "Friday, July 24 2026\n\n…"}. The time is the part before the
 * double space; the tooltip's first line is the long date, which is nicer here
 * than the ISO one and costs nothing since it is already in the payload.
 */
WidgetFrame {
    id: root

    widgetId: "clock"
    shown: WidgetState.clock
    label: "CLOCK"
    accent: Theme.cyan

    homeEdgeH: "right"; homeEdgeV: "bottom"
    homeMarginX: 22
    // Clear of the visualiser when both are on — it is 110 tall at the bottom.
    // Only while this is at its home corner; a clock somebody has placed
    // deliberately does not get shuffled by another widget appearing.
    homeMarginY: WidgetState.visualizer ? 124 : 24

    // Both strings are intrinsically sized — neither is wrapped or given a
    // width — so measuring the card from them is not the loop it looks like.
    cardWidth: Math.max(180, time.implicitWidth + 22, date.implicitWidth + 22)
    bodyHeight: time.implicitHeight + date.implicitHeight + 2

    property string timeText: "--:--:--"
    property string dateText: ""

    // The shadow is Qt's own text outline, NOT a second copy of the string
    // behind the first.
    //
    // It was that: another Text at the same size, 35% opaque, anchored 5px left
    // and 1px up. At 42px a 5px horizontal offset is about one stroke width, so
    // the ghost landed BESIDE each stroke rather than under it — every digit
    // came with a detached smear on its left and a visible gap between the two.
    // velle, 2026-08-07: "it shouldn't have space between the shadow and the
    // number on top."
    //
    // An outline cannot gap: Qt draws it around the glyph's own path, so there
    // is no offset to get wrong and nothing to keep in step when the font or the
    // size changes. It is also one Text item instead of two, on the widget most
    // meant to be read from across the room and redrawn every second.
    //
    // Same trick, same colour as the Pizza widget's destination label — whose
    // comment already claimed this is what the clock did.
    Text {
        id: time
        anchors { right: parent.right; top: parent.top }
        text: root.timeText
        color: root.accent
        style: Text.Outline
        styleColor: Qt.rgba(0, 0, 0, 0.55)
        font.family: Theme.fontFamily
        font.pixelSize: 42
    }

    Text {
        id: date
        anchors { right: parent.right; top: time.bottom; topMargin: 2 }
        text: root.dateText
        color: Theme.fgDim
        font.family: Theme.fontFamily
        font.pixelSize: 12
        font.letterSpacing: 0.8
    }

    Process {
        id: tick
        command: ["synui-clock"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const j = JSON.parse(this.text)
                    const t = String(j.text || "")
                    // Split on the run of spaces the format puts between time
                    // and date; if the user's format has no date part this
                    // leaves the whole string as the time, which is correct.
                    const parts = t.split(/\s{2,}/)
                    root.timeText = parts[0] || "--:--:--"
                    const tip = String(j.tooltip || "").split("\n")[0].trim()
                    root.dateText = tip || (parts.length > 1 ? parts[1] : "")
                } catch (e) {
                    root.timeText = "--:--:--"
                    root.dateText = "clock unavailable"
                }
            }
        }
    }

    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: tick.running = true
    }
}
