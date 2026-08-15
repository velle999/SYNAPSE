import QtQuick
import Quickshell
import Quickshell.Io
import ".."

/*
 * Virtual desktops.
 *
 * synui's workspaces span ALL monitors — switching moves every screen at once —
 * so this is one row of pills, not a per-output set. `synctl workspaces` gives
 * the whole picture in a single call (id, name, window count, which is visible),
 * so there is no second poll for the active one.
 *
 * Clicking dispatches through synctl rather than reimplementing anything:
 * `synctl dispatch ws N` runs the exact keybind action Super+N does.
 */
Item {
    id: root

    // This used to reserve the launcher's width at its left edge, because the
    // compositor drew the "◢ SYNAPSE" button over the bar's top-left corner and
    // hit-tested it there — anything placed under it was invisible AND
    // unclickable, so this mirrored launcher.c's own width formula off the same
    // state file. The button is a bar module now (modules/Launcher.qml) and sits
    // in the same Row as this, so the corner is ordinary bar again and the whole
    // mirror is gone.

    property var workspaces: []

    implicitWidth: row.implicitWidth
    implicitHeight: Theme.barHeight

    Row {
        id: row
        height: parent.height
        spacing: 3

        Repeater {
            model: root.workspaces

            delegate: Rectangle {
                id: pill
                required property var modelData

                readonly property bool active: modelData.visible === true
                readonly property bool occupied: (modelData.windows || 0) > 0

                width: 22
                height: 20
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.radius

                // Three states worth telling apart at a glance: the one you are
                // on, ones holding windows you can go back to, and empty ones.
                color: pill.active ? Theme.barActiveBg
                                   : (mouse.containsMouse ? Theme.barHoverBg : "transparent")
                border.width: pill.active ? 1 : 0
                border.color: Theme.barAccent
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    anchors.centerIn: parent
                    text: pill.modelData.id
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: pill.active ? Theme.barFg
                                       : (pill.occupied ? Theme.barGlyph : Theme.barDim)
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        switcher.command = ["synctl", "dispatch", "ws",
                                            String(pill.modelData.id)]
                        switcher.running = true
                    }
                }
            }
        }
    }

    Process { id: switcher }

    Process {
        id: poll
        command: ["synctl", "workspaces"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const ws = JSON.parse(this.text)
                    if (Array.isArray(ws)) root.workspaces = ws
                } catch (e) {
                    // Leave the last good list up. Blanking the row because one
                    // poll raced a compositor restart would look like the
                    // desktops vanished.
                }
            }
        }
    }

    Timer {
        interval: 400
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: poll.running = true
    }
}
