import Quickshell
import QtQuick
import Qt5Compat.GraphicalEffects
import ".."

Rectangle {
    id: root

    property bool widgetBackground: false
    width: 460
    height: 220

    color: Qt.rgba(0, 0, 0, 0.0)

    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }

    Rectangle {
        anchors.fill: parent
        radius: Config.settings.defaultWindowRadius
        color: Config.colors.base
        border.width: 1
        border.color: Config.colors.highlight
        opacity: 0.75
        visible: root.widgetBackground
    }

    Column {
        anchors.centerIn: parent
        spacing: 0

        Text {
            id: timeText
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDateTime(clock.date, Config.settings.militaryTimeClockFormat ? "hh:mm" : "h:mm AP")
            font.pixelSize: 104
            font.family: fontBoska.name
            font.weight: 500
            color: Config.colors.accent
            DropShadow {
                anchors.fill: parent
                color: "Black"
                opacity: 0.3
                source: parent
                radius: 5
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDateTime(clock.date, "dddd, MMMM d")
            font.pixelSize: 21
            font.family: fontRecia.name
            color: Config.colors.textLight
            DropShadow {
                anchors.fill: parent
                color: "Black"
                opacity: 0.3
                source: parent
                radius: 5
            }
        }
    }
}
