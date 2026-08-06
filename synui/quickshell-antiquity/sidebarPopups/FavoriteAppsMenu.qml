import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Quickshell.Wayland
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import ".."

import "../utils" as Utils
import "../smallicons/" as Smallicons

/* NOTE:
*  This entire module is quite a mess, and is likely going to get a complete re-write.
*  I'm experimenting with creating the entire window frame/designs with SVG in order to
*  skip the need of creating everything out of rectangles and borders.
*/
PanelWindow {
    id: root
    property QtObject anchorWindow
    property double positionY: 0
    property double positionX: 0
    property int popupState: Config.SidebarPopup.None
    property var closeCallback: function () {}
    WlrLayershell.layer: WlrLayer.Overlay

    WlrLayershell.namespace: "diinki_celestialantiquity:bars"

    anchors {
        left: true
        top: true
    }
    margins {
        left: positionX
        top: positionY + height / 3 + 20
        bottom: 0
        right: 0
    }

    implicitWidth: 500
    implicitHeight: 280
    visible: popupState == Config.SidebarPopup.FavoriteAppsMenu ? true : false
    color: "transparent"
    SidebarPopupWindow {

        anchorWindow: parent
        ColumnLayout {
            anchors.fill: parent
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                width: parent.width
                color: Config.colors.base
                radius: 12
                bottomLeftRadius: 0
                bottomRightRadius: 0
                height: 40
                border.width: 1
                border.color: Config.colors.outline
                clip: true
                Smallicons.BackgroundPatternOne {
                    lineColor: Config.colors.patternLineColor
                    x: -350
                    y: -55
                    property real scale: 0.37
                    width: implicitWidth * scale
                    height: implicitHeight * scale
                }
                Item {
                    anchors.fill: parent
                    Text {
                        id: windowTitle
                        anchors.centerIn: parent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: "Favorite Apps"
                        font.family: fontQuilon.name
                        font.pixelSize: 22
                        font.weight: 600
                        color: Config.colors.accent
                    }
                }
            }
            Rectangle {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.margins: 12
                Layout.topMargin: 6
                radius: 7
                color: Config.colors.base
                border.width: 1
                border.color: Config.colors.outline
                clip: true

                Smallicons.BackgroundPatternOne {
                    lineColor: Config.colors.patternLineColor
                    x: -150
                    y: -40
                    property real scale: 0.3
                    width: implicitWidth * scale
                    height: implicitHeight * scale
                }
                GridView {
                    id: iconGrid
                    property int columnAmount: 5
                    anchors.fill: parent
                    cellWidth: parent.width / columnAmount
                    cellHeight: parent.width / columnAmount
                    clip: true
                    model: Object.values(Config.favoriteApps)

                    delegate: Rectangle {

                        width: parent.width / iconGrid.columnAmount
                        height: parent.width / iconGrid.columnAmount
                        property var iconPath: Utils.AppSearch.getIcon(modelData.icon)
                        color: "transparent"
                        RectangularShadow {
                            anchors.centerIn: parent
                            width: 74
                            height: 74
                            color: Qt.rgba(0, 0, 0, 0.5)
                            radius: width / 2
                            blur: 14
                        }
                        Rectangle {
                            id: buttonOutline
                            anchors.centerIn: parent
                            width: 74
                            height: 74
                            radius: width / 2
                            color: Config.colors.shadow
                            border.width: 1
                            border.color: favoritesHovered.hovered ? Config.colors.accent : Config.colors.highlight

                            Behavior on border.color {
                                ColorAnimation {
                                    duration: 64
                                }
                            }
                        }
                        Image {
                            anchors.centerIn: parent
                            width: 50
                            height: 50
                            asynchronous: true
                            antialiasing: true
                            source: parent.iconPath
                            Item {
                                width: 50
                                height: 50
                                y: 48
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: Config.colors.accent
                                    opacity: favoritesHovered.hovered ? 1 : 0

                                    Behavior on opacity {
                                        NumberAnimation {
                                            duration: 64
                                        }
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: buttonOutline
                            HoverHandler {
                                id: favoritesHovered
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                cursorShape: Qt.PointingHandCursor
                            }
                            onReleased: {
                                Quickshell.execDetached(modelData.execCommand);
                                root.closeCallback();
                            }
                        }
                    }
                }
            }
        }
    }
}
