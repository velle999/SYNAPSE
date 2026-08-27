import QtQuick
import Quickshell
import Quickshell.Io
import ".."

/*
 * The weather, on the desktop.
 *
 * The bar module says the temperature and nothing else, because a bar is for a
 * glance down and the width of a word costs something there. This is the same
 * reading with room around it: the condition, the place, and how long ago it
 * was true. Both draw from WeatherState, so the desktop and the bar cannot ever
 * disagree about the temperature — which is the failure a second fetcher would
 * eventually produce, and the reason neither of them fetches.
 *
 * ⚠ THERE IS A modules/Weather.qml TOO — the bar's temperature. The two names
 * are only unambiguous because shell.qml imports "widgets" and Bar.qml imports
 * "modules", and neither imports both.
 *
 * ⚠ EMPTY IS A STATE THIS WIDGET HAS TO DRAW, unlike the bar module. A module
 * that hides itself costs a few pixels of bar; a card the user has explicitly
 * switched on and dragged somewhere cannot vanish because the network went
 * away — that reads as the widget being broken. So the card stays and says why
 * there is nothing in it, and the one case it can actually fix (the weather has
 * never been turned on) it says how to fix.
 */
WidgetFrame {
    id: root

    widgetId: "weather"
    shown: WidgetState.weather
    label: "WEATHER"
    accent: Theme.cyan

    /*
     * Every line here is text on the card, and at dock_opacity 0.00 that is
     * 11px of a colour chosen for a dark pane painted onto the wallpaper.
     * Same call SysMonitor and the note make, and for the same reason.
     *
     * The TEMPERATURE is the exception the accent always is: it is the one
     * number this widget exists to show, and it stays the accent so it reads as
     * the headline rather than as another line of body text.
     */
    inkOnBackdrop: true

    /*
     * Top-left, under the bar. The other corners are taken — the monitor, the
     * dial and the pet stack down the right, the clock and the player along the
     * bottom — and this is a small card that wants to be near the top of the
     * eye's path rather than at the end of it.
     *
     * QuickLaunch shares this corner and moves down when this is on, exactly as
     * the pet moves for the monitor. Only until somebody drags one: WidgetFrame
     * stops applying home margins the moment there is a stored position.
     */
    homeEdgeH: "left"; homeEdgeV: "top"
    homeMarginX: 20
    homeMarginY: Theme.barHeight + 18

    cardWidth: 216
    bodyHeight: col.implicitHeight

    // Nothing has ever been fetched. Distinguished below from "switched off",
    // because only one of the two has an instruction worth printing.
    readonly property bool empty: !WeatherState.have

    Column {
        id: col
        width: parent.width
        spacing: 6

        // ── The reading ──────────────────────────────────
        Item {
            width: col.width
            height: root.empty ? 0 : 46
            visible: !root.empty

            Text {
                id: glyph
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                // The same glyph the bar shows, from the same name the
                // compositor published — Icons.weatherFor is the only place a
                // name becomes a picture.
                text: Icons.weatherFor(WeatherState.icon)
                color: WeatherState.stale ? root.inkDim : root.accentInk
                font.family: Theme.iconFamily
                font.pixelSize: 30
            }

            Text {
                anchors { left: glyph.right; leftMargin: 12
                          verticalCenter: parent.verticalCenter }
                text: WeatherState.label
                // Dimmed when old, never hidden and never drawn as current:
                // the lock screen and the bar module make the identical call,
                // and a plausible wrong temperature is the worst outcome here.
                color: WeatherState.stale ? root.inkDim : root.accentInk
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
            }
        }

        // ── What it is doing, and where ──────────────────
        Text {
            width: col.width
            visible: !root.empty && WeatherState.cond !== ""
            height: visible ? implicitHeight : 0
            text: WeatherState.cond
            color: root.ink
            font.family: Theme.fontFamily
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Text {
            width: col.width
            visible: !root.empty && WeatherState.place !== ""
            height: visible ? implicitHeight : 0
            text: WeatherState.place
            color: root.inkDim
            font.family: Theme.fontFamily
            font.pixelSize: 10
            font.letterSpacing: 0.8
            elide: Text.ElideRight
        }

        /* HOW OLD, always, not only when stale. A widget read at a glance from
         * a chair has no other way to say whether it is looking at now — the
         * bar module can dim a number and be understood because it is two
         * characters wide, but a card with a condition and a place on it looks
         * authoritative whatever colour it is drawn in. */
        Text {
            width: col.width
            visible: !root.empty
            height: visible ? implicitHeight : 0
            text: WeatherState.stale ? "last reading " + WeatherState.ageText
                                     : "updated " + WeatherState.ageText
            color: root.inkDim
            font.family: Theme.fontFamily
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        // ── Nothing to show ──────────────────────────────
        //
        // One line, and it names the command rather than describing the state,
        // because the state is not the user's problem and the command is the
        // whole of the fix. `synctl weather on` is the same switch the Super+Z
        // row and the bar's menu reach.
        Text {
            width: col.width
            visible: root.empty
            height: visible ? implicitHeight : 0
            text: "No reading yet\nsynctl weather on"
            color: root.inkDim
            font.family: Theme.fontFamily
            font.pixelSize: 11
            lineHeight: 1.25
            wrapMode: Text.NoWrap
        }
    }
}
