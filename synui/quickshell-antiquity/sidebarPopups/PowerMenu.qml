import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Quickshell.Wayland
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import ".."

PanelWindow {
    id: root
    property QtObject anchorWindow
    property Item buttonItem
    property double positionY: 0
    property double positionX: 0
    property int popupState: Config.SidebarPopup.None
    WlrLayershell.layer: WlrLayer.Overlay

    visible: popupState == Config.SidebarPopup.PowerMenu ? true : false
    WlrLayershell.namespace: "diinki_celestialantiquity:bars"

    color: Qt.rgba(0, 0, 0, 0)
    anchors {
        left: true
        top: true
        //right: true
        //bottom: true
    }

    margins {
        left: positionX + 20
        top: positionY + height / 3 + 20
        bottom: 0
        right: 0
    }

    implicitWidth: 533
    implicitHeight: 328

    Rectangle {
        property color glassColor: Config.colors.glassTintColor
        color: Qt.rgba(glassColor.r, glassColor.g, glassColor.b, 0.2)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 297
        border.width: 1
        border.color: Config.colors.outline
        radius: 20
        clip: true
    }

    ColumnLayout {
        anchors.fill: parent
        Rectangle {
            Layout.alignment: Qt.AlignBottom
            width: parent.width
            height: 235
            color: "transparent"
            clip: false

            RowLayout {
                width: parent.width
                height: parent.height
                Rectangle {
                    id: hibernateCard
                    height: 235
                    color: "transparent"
                    Layout.leftMargin: 10
                    Image {
                        height: parent.height
                        fillMode: Image.PreserveAspectFit
                        source: "../assets/tarot_card_hibernate.svg"
                        y: hibernateHover.hovered ? -20 : 0
                        Behavior on y {
                            NumberAnimation {
                                duration: 180
                                easing.type: Easing.OutCubic
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            HoverHandler {
                                id: hibernateHover
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                cursorShape: Qt.PointingHandCursor
                            }
                            onPressed: {
                                Quickshell.execDetached(["systemctl", "suspend-then-hibernate"]);
                            }
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    id: reboot
                    height: 235
                    color: "transparent"
                    Image {
                        height: parent.height
                        fillMode: Image.PreserveAspectFit
                        source: "../assets/tarot_card_reboot.svg"
                        y: rebootHover.hovered ? -20 : 0
                        Behavior on y {
                            NumberAnimation {
                                duration: 180
                                easing.type: Easing.OutCubic
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            HoverHandler {
                                id: rebootHover
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                cursorShape: Qt.PointingHandCursor
                            }
                            onPressed: {
                                Quickshell.execDetached(["systemctl", "reboot"]);
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                Rectangle {
                    id: poweroff
                    height: 235
                    width: 164
                    color: "transparent"

                    Layout.rightMargin: 10
                    y: poweroffHover.hovered ? -20 : 0
                    Behavior on y {
                        NumberAnimation {
                            duration: 180
                            easing.type: Easing.OutCubic
                        }
                    }
                    Image {
                        height: parent.height
                        fillMode: Image.PreserveAspectFit
                        source: "../assets/tarot_card_poweroff.svg"
                        y: 0

                        MouseArea {
                            anchors.fill: parent
                            HoverHandler {
                                id: poweroffHover
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                cursorShape: Qt.PointingHandCursor
                            }
                            onPressed: {
                                Quickshell.execDetached(["systemctl", "poweroff"]);
                            }
                        }
                    }
                }
            }
        }

        Rectangle {

            Layout.alignment: Qt.AlignBottom
            width: parent.width
            color: Config.colors.base
            radius: 9
            topLeftRadius: 0
            topRightRadius: 0
            height: 40
            border.width: 1
            border.color: Config.colors.outline
            clip: true

            RowLayout {
                anchors.fill: parent

                Rectangle {
                    Layout.leftMargin: 11
                    width: 162
                    Text {
                        anchors.fill: parent
                        text: ""
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: iconFont.name
                        font.pixelSize: 26
                        color: Config.colors.textLight
                        font.weight: 650
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    Text {
                        anchors.fill: parent
                        text: ""
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: iconFont.name
                        font.pixelSize: 32
                        color: Config.colors.warning
                        font.weight: 800
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    Layout.rightMargin: 11
                    width: 162
                    Text {
                        anchors.fill: parent
                        text: ""
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: iconFont.name
                        font.pixelSize: 27
                        color: Config.colors.danger
                        font.weight: 800
                    }
                }
            }
        }
    }
}
