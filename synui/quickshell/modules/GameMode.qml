import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * Game mode — reports only, exactly like the waybar module.
 *
 * synui writes $XDG_RUNTIME_DIR/synui/game on every enter/leave and
 * synui-game-status turns that into JSON. Super+G is what toggles it; a click
 * here does NOT, on purpose — game mode stops synapd, and a stray bar click
 * should not be able to do that.
 */
BarModule {
    id: root

    property bool on: false

    moduleVisible: on              // invisible when off, like waybar's empty text
    icon: Icons.game
    iconColor: root.pal.clock
    // ⛔ NOT TRANSLATED, AND THAT IS THE DECISION. This is a four-letter badge
    // in a fixed-width slot beside the clock, not a sentence: "SPIELMODUS" and
    // "ゲームモード" do not fit, and the tooltip below carries the meaning in
    // the user's language. Same rule the compositor applies to its own badges.
    text: "GAME"
    textColor: root.pal.clock
    active: on
    tooltipText: tip

    property string tip: ""

    Process {
        id: poll
        command: ["synui-game-status"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const j = JSON.parse(this.text)
                    root.on  = (j.alt === "on") || (j.class === "active")
                    root.tip = j.tooltip || ""
                } catch (e) {
                    root.on = false
                }
            }
        }
    }

    Timer {
        interval: 2000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: poll.running = true
    }
}
