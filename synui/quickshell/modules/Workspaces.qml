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

    // The bar's top-left corner is NOT ours. synui draws the "◢ SYNAPSE"
    // launcher there itself (src/launcher.c) and hit-tests it in the
    // compositor, so anything placed under it is invisible AND unclickable.
    //
    // Its width is computed at runtime from the text extents, not fixed, and it
    // changes when the style flips between text and logo — so the same formula
    // is mirrored here off the same state file rather than guessing a margin
    // that goes wrong the moment the style is toggled.
    //   text: PAD(12) + advance("◢ SYNAPSE") + PAD(12)
    //   logo: PAD(12) + advance("◢") + GAP(6) + EMBLEM(23) + PAD(12)
    property bool logoStyle: true

    property FileView launcherState: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/launcher.state"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.logoStyle = this.text().indexOf("style=logo") >= 0
        onLoadFailed: root.logoStyle = true
    }

    TextMetrics {
        id: caretMetrics
        font.family: "monospace"
        font.pixelSize: 13          // LAUNCHER_FONT
        text: "◢"
    }
    TextMetrics {
        id: wordmarkMetrics
        font.family: "monospace"
        font.pixelSize: 13
        text: "◢ SYNAPSE"
    }

    readonly property int launcherWidth:
        root.logoStyle ? (12 + Math.round(caretMetrics.advanceWidth) + 6 + 23 + 12)
                       : (12 + Math.round(wordmarkMetrics.advanceWidth) + 12)

    property var workspaces: []

    implicitWidth: launcherWidth + row.implicitWidth
    implicitHeight: Theme.barHeight

    Row {
        id: row
        x: root.launcherWidth
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
                color: pill.active ? Theme.activeBg
                                   : (mouse.containsMouse ? Theme.hoverBg : "transparent")
                border.width: pill.active ? 1 : 0
                border.color: Theme.magenta
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    anchors.centerIn: parent
                    text: pill.modelData.id
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: pill.active ? Theme.fg
                                       : (pill.occupied ? Theme.cyan : Theme.fgDim)
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
