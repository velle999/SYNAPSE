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
 * "tooltip": "Friday, July 24 2026\n\n…", "date": "Friday, July 24 2026"}.
 * The time is the part of `text` before the double space. The long date comes
 * from `date` — a key that exists FOR this widget, because the tooltip always
 * carries a date on purpose and so cannot express the "No date" layout.
 */
WidgetFrame {
    id: root

    widgetId: "clock"
    shown: WidgetState.clock
    label: I18n.tr("CLOCK")
    /* The clock's own colour, not the glyph one — see Theme.qml's `clock`. On a
     * desktop taking its palette off the wallpaper this is the SECOND measured
     * hue, which is what keeps a clock the size of this one from being the same
     * violet as every icon on the bar above it. */
    accent: Theme.clock

    /*
     * The date goes on whatever this card is sitting on. Same opt-in as the
     * note and the system monitor.
     *
     * ⚠ THE TIME ALREADY SOLVED THIS ONE ELEMENT UP, WHICH IS WHY IT WAS EASY
     * TO MISS. It is 42px in the theme's clock colour with a black
     * `Text.Outline` behind it, so it survives any wallpaper — and it sits
     * directly above a 12px date in the dim ink with no outline and no card
     * under it. On a bright wallpaper the time is perfectly readable and the
     * date beneath it is gone, which reads as the date being broken rather
     * than as the pair being inked for two different surfaces.
     *
     * The TIME keeps its own colour: `Theme.clock` is the theme's statement
     * about what a clock looks like, and the outline already carries it. Only
     * the date, which had neither, is handed to the backdrop.
     */
    inkOnBackdrop: true

    homeEdgeH: "right"; homeEdgeV: "bottom"
    homeMarginX: 22
    // Clear of the visualiser when both are on — it is 110 tall at the bottom.
    // Only while this is at its home corner; a clock somebody has placed
    // deliberately does not get shuffled by another widget appearing.
    homeMarginY: WidgetState.visualizer ? 124 : 24

    // Both strings are intrinsically sized — neither is wrapped or given a
    // width — so measuring the card from them is not the loop it looks like.
    cardWidth: Math.max(180, time.implicitWidth + 22, date.implicitWidth + 22)
    // The date line is not always there — "No date" is one of the layouts the
    // Date & Time settings offer. An empty Text still reports a full line of
    // implicitHeight, so measuring it unconditionally left the card carrying a
    // blank strip under the time for a line that is not drawn.
    bodyHeight: time.implicitHeight + (date.visible ? date.implicitHeight + 2 : 0)

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
        visible: text !== ""
        color: root.inkDim
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
                    // `date` is the widget's own key: the long form when the
                    // chosen layout has a date, and EMPTY when it does not.
                    // Reading the tooltip instead — as this did — meant "No
                    // date" was a setting this widget silently ignored, since
                    // the tooltip always carries one on purpose.
                    root.dateText = String(j.date !== undefined
                                           ? j.date
                                           : (parts.length > 1 ? parts[1] : ""))
                } catch (e) {
                    root.timeText = "--:--:--"
                    root.dateText = I18n.tr("clock unavailable")
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
