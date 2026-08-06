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
import "../smallicons/weatherWidget" as Widgets

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
        left: positionX + 20
        top: positionY + height / 3 + 20
        bottom: 0
        right: 0
    }

    implicitWidth: 500
    implicitHeight: 280
    visible: popupState == Config.SidebarPopup.ThemingMenu ? true : false
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
                    x: 0
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
                        text: "Themes"
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
                    x: -450
                    y: -0
                    property real scale: 0.3
                    width: implicitWidth * scale
                    height: implicitHeight * scale
                }
                GridView {
                    id: iconGrid
                    property int columnAmount: 4
                    property int margin: 4
                    bottomMargin: 15
                    anchors.fill: parent
                    cellWidth: parent.width / columnAmount
                    cellHeight: parent.width / columnAmount
                    clip: true
                    model: Object.keys(Config.themes)

                    delegate: Rectangle {

                        width: parent.width / iconGrid.columnAmount
                        height: parent.width / iconGrid.columnAmount
                        color: "transparent"
                        RectangularShadow {
                            anchors.centerIn: parent
                            width: 90
                            height: 90
                            color: Qt.rgba(0, 0, 0, 0.5)
                            radius: 4
                            blur: 14
                        }
                        Rectangle {
                            id: buttonOutline
                            anchors.centerIn: parent
                            width: 90
                            height: 90
                            radius: 4
                            color: Config.themes[modelData].base
                            border.width: 1
                            border.color: favoritesHovered.hovered || Config.settings.currentTheme == modelData ? Config.colors.accent : Config.colors.highlight

                            Behavior on border.color {
                                ColorAnimation {
                                    duration: 64
                                }
                            }
                            Widgets.Squares {
                                anchors.centerIn: parent
                                width: 87
                                height: 87
                                leftAndRightColor: Config.themes[modelData].humorCold
                                bottomColor: Config.themes[modelData].humorDry
                                topColor: Config.themes[modelData].accent
                                lineColor: Config.themes[modelData].outline
                            }
                            Widgets.Sun {
                                anchors.centerIn: parent
                                width: 70
                                height: 70
                                bodyColor: Config.themes[modelData].cbodyBackground
                                lineColor: Config.themes[modelData].cbodyStroke
                            }
                            Rectangle {
                                width: 26
                                height: 26
                                radius: width / 2
                                anchors.centerIn: parent
                                border.width: 1
                                border.color: Config.themes[modelData].outline
                                color: Config.themes[modelData].accent
                            }
                            Text {

                                anchors.centerIn: parent
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: "T"
                                font.family: fontRecia.name
                                font.pixelSize: 15
                                font.weight: 600
                                color: Config.themes[modelData].text
                            }
                        }
                        Text {
                            width: parent.width
                            anchors.top: buttonOutline.bottom
                            topPadding: 3
                            font.pixelSize: 13
                            //font.family: fontRecia.name
                            text: modelData
                            color: Config.colors.accent
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        /*
                         * Upstream had an Image here sourcing `parent.iconPath`
                         * — a property nothing declares. It logged "Unable to
                         * assign [undefined] to QUrl" on every one of the five
                         * swatches at startup and drew nothing, because the
                         * swatch is built out of the Squares/Sun drawings and
                         * the accent dot above; there is no icon to show. Gone
                         * rather than given a path.
                         */
                        MouseArea {

                            anchors.fill: buttonOutline
                            HoverHandler {
                                id: favoritesHovered
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                cursorShape: Qt.PointingHandCursor
                            }
                            onReleased: {
                                Config.applyTheme(modelData);
                                root.closeCallback();
                            }
                        }
                    }
                }
            }
        }
    }
}
