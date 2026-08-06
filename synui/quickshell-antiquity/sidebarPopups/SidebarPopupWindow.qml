import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Quickshell.Wayland
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import ".."

Rectangle {
    id: root
    radius: Config.settings.defaultWindowRadius

    property QtObject anchorWindow
    property color glassColor: Config.colors.glassTintColor
    anchors.fill: anchorWindow
    //color: Config.colors.base
    border.width: 1
    border.color: Config.colors.outline
    color: Qt.rgba(glassColor.r, glassColor.g, glassColor.b, 0.2)
}
