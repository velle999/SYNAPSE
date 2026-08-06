import Quickshell
import QtQuick
import QtQuick.Controls.Basic

import ".."

Button {
    id: root

    property bool isToggled: false
    property string iconFontValue: "\ue5c3"
    property string toggledIconFontValue: "\ue5c5"

    implicitWidth: 18
    implicitHeight: 18

    background: Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: parent.height
        border.width: 0
        border.color: root.isToggled ? Config.colors.accent : Config.colors.outline
        color: "transparent"
        opacity: mouse.hovered ? 0.4 : 1
        radius: 0

        Text {
            font.family: iconFont.name
            horizontalAlignment: Text.AlignHCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.horizontalCenter: parent.horizontalCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.isToggled ? 30 : 18
            text: root.isToggled ? root.toggledIconFontValue : root.iconFontValue
            color: root.isToggled ? Config.colors.accent : Config.colors.outline
        }
    }
    HoverHandler {
        id: mouse
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        cursorShape: Qt.PointingHandCursor
    }
}
