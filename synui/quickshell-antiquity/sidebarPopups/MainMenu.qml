import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Controls
//import Quickshell.Widgets
import Quickshell.Wayland
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import ".."

import "../smallicons/" as Smallicons
import "../utils" as Utils

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

    // Wider than the 500 upstream used, because that was the width of a panel
    // whose left two thirds were empty and which now has a clock, a track title
    // and a transport to fit side by side.
    implicitWidth: 560

    /*
     * The HEIGHT follows the pane rather than being a number, because the pane
     * grows and shrinks: the battery row, the now-playing block and the
     * brightness stepper each appear only where they mean something, so a fixed
     * height that suits a laptop playing music leaves a desktop looking at the
     * empty box this panel was already accused of being.
     *
     * 74 is the chrome around it — the 40px title bar, the 6/12 margins on the
     * body and the 8s inside it. The floor is what the three buttons and the
     * volume slider in the right-hand strip need; below that the pane would be
     * the shorter of the two and the strip would start clipping.
     *
     * No layout loop: a ColumnLayout's implicitHeight is the sum of its
     * children's, which does not depend on the height it is given.
     */
    implicitHeight: Math.max(240, 74 + statusPane.implicitHeight)
    visible: popupState == Config.SidebarPopup.MainMenu ? true : false
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
                    x: -450
                    y: -500
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
                        text: "Control Panel"
                        font.family: fontQuilon.name
                        font.pixelSize: 22
                        font.weight: 600
                        color: Config.colors.accent
                    }

                    Item {
                        Layout.fillWidth: true
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
                    x: -300
                    y: -320
                    property real scale: 0.3
                    width: implicitWidth * scale
                    height: implicitHeight * scale
                }
                RowLayout {
                    anchors.fill: parent
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        Layout.margins: 8
                        Layout.rightMargin: 0
                        radius: 4
                        color: Config.colors.shadow
                        border.width: 1
                        border.color: Config.colors.highlight
                        clip: true

                        // Upstream left this Rectangle empty — a bordered void
                        // taking two thirds of a panel called "Control Panel".
                        // See ControlPanelPane.qml for what belongs in it and
                        // why it is a file of its own rather than another
                        // hundred lines inline here.
                        ControlPanelPane {
                            id: statusPane
                            anchors.fill: parent
                            active: root.visible
                        }
                    }
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.margins: 8
                        Layout.leftMargin: 4
                        Layout.rightMargin: 4
                        radius: 4
                        color: Config.colors.shadow
                        border.width: 1
                        border.color: Config.colors.highlight
                        width: 50

                        ColumnLayout {
                            anchors.fill: parent
                            Item {
                                Layout.fillHeight: true
                                Layout.alignment: Qt.AlignHCenter

                                Layout.topMargin: 10
                                width: 20
                                Slider {
                                    id: volumeControl
                                    orientation: Qt.Vertical
                                    from: 0.0
                                    to: 1.0
                                    stepSize: 0.01
                                    value: AudioSystem.volume
                                    onMoved: AudioSystem.volume = value
                                    implicitWidth: 20
                                    implicitHeight: parent.height

                                    background: Rectangle {
                                        x: volumeControl.leftPadding
                                        y: volumeControl.topPadding + volumeControl.availableHeight / 2 - height / 2
                                        implicitWidth: 4
                                        implicitHeight: volumeControl.height
                                        width: volumeControl.availableWidth
                                        height: implicitHeight
                                        radius: width / 2
                                        color: Config.colors.accent

                                        Rectangle {
                                            width: parent.width
                                            height: volumeControl.visualPosition * parent.height
                                            color: Config.colors.highlight
                                            radius: width / 2
                                        }
                                    }

                                    handle: Rectangle {
                                        // Don't ask, I don't even know why this shit is required, its the only way I could make the
                                        // padding look good...
                                        y: volumeControl.visualPosition * (volumeControl.availableHeight - height / 2 + 3)
                                        x: volumeControl.leftPadding + volumeControl.availableWidth / 2 - width / 2

                                        implicitWidth: 20
                                        implicitHeight: 20
                                        radius: width / 2
                                        color: volumeControl.pressed ? "#f0f0f0" : "#f6f6f6"
                                        border.color: "#bdbebf"
                                    }
                                }
                            }

                            Text {

                                Layout.alignment: Qt.AlignHCenter
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: ""
                                font.family: iconFont.name
                                font.pixelSize: 28
                                font.weight: 600
                                color: Config.colors.accent
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillHeight: true
                        width: 50
                        Layout.margins: 8
                        Layout.leftMargin: 0
                        Layout.topMargin: 0
                        Layout.bottomMargin: 0
                        radius: 4
                        color: "transparent"
                        border.width: 0

                        ColumnLayout {
                            anchors.fill: parent
                            Button {
                                Layout.alignment: Qt.AlignHCenter
                                implicitWidth: parent.width
                                implicitHeight: width

                                background: Rectangle {
                                    width: parent.width
                                    height: parent.height
                                    color: Config.colors.shadow
                                    border.width: 1
                                    border.color: networkButtonHovered.hovered ? Config.colors.accent : Config.colors.highlight
                                    radius: 4
                                    Behavior on border.color {
                                        ColorAnimation {
                                            duration: 64
                                        }
                                    }
                                    Text {

                                        anchors.fill: parent
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        text: ""
                                        font.family: iconFont.name
                                        font.pixelSize: 28
                                        font.weight: 600
                                        color: Config.colors.accent
                                    }
                                }
                                MouseArea {

                                    anchors.fill: parent
                                    HoverHandler {
                                        id: networkButtonHovered
                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    onReleased: {
                                        Quickshell.execDetached(["nm-connection-editor"]);
                                        root.closeCallback();
                                    }
                                }
                            }
                            Button {
                                Layout.alignment: Qt.AlignHCenter
                                implicitWidth: parent.width
                                implicitHeight: width

                                background: Rectangle {
                                    width: parent.width
                                    height: parent.height
                                    color: Config.colors.shadow
                                    border.width: 1
                                    border.color: settingsButtonHovered.hovered ? Config.colors.accent : Config.colors.highlight
                                    radius: 4
                                    Behavior on border.color {
                                        ColorAnimation {
                                            duration: 64
                                        }
                                    }
                                    Text {

                                        anchors.fill: parent
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        text: ""
                                        font.family: iconFont.name
                                        font.pixelSize: 28
                                        font.weight: 600
                                        color: Config.colors.accent
                                    }
                                }
                                MouseArea {

                                    anchors.fill: parent
                                    HoverHandler {
                                        id: settingsButtonHovered
                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    onReleased: {
                                        Config.openSettingsWindow = !Config.openSettingsWindow;
                                        root.closeCallback();
                                    }
                                }
                            }
                            Button {
                                Layout.alignment: Qt.AlignHCenter
                                implicitWidth: parent.width
                                implicitHeight: width

                                background: Rectangle {
                                    width: parent.width
                                    height: parent.height
                                    color: Config.colors.shadow
                                    border.width: 1
                                    border.color: volumeButtonHovered.hovered ? Config.colors.accent : Config.colors.highlight
                                    radius: 4
                                    Behavior on border.color {
                                        ColorAnimation {
                                            duration: 64
                                        }
                                    }
                                    Text {

                                        anchors.fill: parent
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        text: ""
                                        font.family: iconFont.name
                                        font.pixelSize: 28
                                        font.weight: 600
                                        color: Config.colors.accent
                                    }
                                }
                                MouseArea {

                                    anchors.fill: parent
                                    HoverHandler {
                                        id: volumeButtonHovered
                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    onReleased: {
                                        Quickshell.execDetached(["pavucontrol"]);
                                        root.closeCallback();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
