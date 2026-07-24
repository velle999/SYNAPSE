import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
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
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    visible: WidgetState.clock && modelData.name === WidgetState.primaryOutput

    WlrLayershell.layer: WlrLayer.Bottom
    anchors { bottom: true; right: true }
    // Clear of the visualiser when both are on — it is 110 tall at the bottom.
    margins { bottom: WidgetState.visualizer ? 124 : 24; right: 22 }
    Behavior on margins.bottom { NumberAnimation { duration: Theme.animNormal } }

    implicitWidth: box.implicitWidth + 34
    implicitHeight: box.implicitHeight + 20

    exclusiveZone: 0
    focusable: false
    mask: Region {}
    color: "transparent"

    property string timeText: "--:--:--"
    property string dateText: ""

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Theme.popupBg
        border.color: Theme.magenta
        border.width: 1

        Column {
            id: box
            anchors.centerIn: parent
            spacing: 2

            Text {
                anchors.right: parent.right
                text: root.timeText
                color: Theme.cyan
                font.family: Theme.fontFamily
                font.pixelSize: 40
            }

            Text {
                anchors.right: parent.right
                text: root.dateText
                color: Theme.fgDim
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }
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
