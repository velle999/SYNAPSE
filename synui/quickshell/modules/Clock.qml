import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * Clock — driven by synui-clock, exactly as the waybar module was.
 *
 * It would be trivial to format a date in QML, and that would be the wrong
 * move: synui's Date & Time panel writes ~/.config/synui/clock.state (12/24h,
 * seconds, world-clock zones) and synui-clock is what reads it. Formatting
 * here would silently strand those toggles, which is the exact bug the waybar
 * config's own comment says custom/clock exists to avoid.
 *
 * Clicking pops synui's compositor-drawn calendar. This is the ONLY way in by
 * default since 2026-07-31 — Super+Shift+T became `retile` — so this handler is
 * load-bearing rather than a convenience.
 */
BarModule {
    id: root

    property string clockText: "--:--"
    property string clockTip: ""

    text: clockText
    // waybar colours #custom-clock with the theme's clock_fg, not the glyph
    // colour — and that value exists precisely because #ffd319 is illegible on
    // a light bar. Using the glyph colour here would have left clockFg unread.
    textColor: root.pal.clock
    tooltipText: clockTip

    onClicked: calendar.running = true

    Process {
        id: calendar
        command: ["synctl", "dispatch", "calendar"]
    }

    Process {
        id: tick
        command: ["synui-clock"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const j = JSON.parse(this.text)
                    root.clockText = j.text || "--:--"
                    root.clockTip  = j.tooltip || ""
                } catch (e) {
                    // A malformed line must not wedge the clock at its last
                    // value with no explanation.
                    root.clockText = I18n.tr("clock?")
                    root.clockTip  = I18n.tr("synui-clock returned unparseable output")
                }
            }
        }
    }

    // 1s, matching waybar's interval — clock.state changes (a toggle in the
    // settings panel) show up within a tick.
    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: tick.running = true
    }
}
