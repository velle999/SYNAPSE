import Quickshell
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import ".."
import "../utils/" as Utils

RowLayout {
    id: workspaces
    spacing: 3
    anchors.left: parent.left
    anchors.verticalCenter: parent.verticalCenter

    // Every desktop, not this screen's — synui's workspaces are global, so
    // there is no per-monitor set to filter down to. See SynWorkspaces.qml.
    property var currentWorkspaces: SynWorkspaces.list
    Repeater {
        model: parent.currentWorkspaces
        Button {
            id: control
            anchors.centerIn: parent.centerIn
            contentItem: Text {
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: Utils.TextUtils.toRomanNumeral(modelData.id)

                font.family: fontBoska.name
                font.weight: 650
                width: 10
                height: 10
                font.pixelSize: 13
                color: control.getColor()
            }
            onPressed: event => {
                SynWorkspaces.switchTo(modelData.id);
                event.accepted = true;
            }

            // Upstream also painted `modelData.urgent` with Config.colors.urgent.
            // synui has no urgency concept — not in the IPC, not in the
            // compositor — so that branch could only ever be dead code here.
            // Dropped rather than left in to look supported. The `urgent` key
            // stays in all five palettes — it costs nothing, keeps the theme
            // schema uniform, and is the one thing to bind if synui ever grows
            // an urgency hint.
            function getColor() {
                if (modelData.id == SynWorkspaces.focusedId || mouse.hovered) {
                    return Config.colors.accent;
                }
                // textLight, not text: these numerals sit on the translucent
                // bar, not on an accent chip. See taskbar/ClockWidget.qml.
                return Config.colors.textLight;
            }
            background: Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.horizontalCenter: parent.horizontalCenter
                width: 18
                height: 18
                border.width: 0
                color: "transparent"
            }

            HoverHandler {
                id: mouse
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                cursorShape: Qt.PointingHandCursor
            }
        }
    }
}
