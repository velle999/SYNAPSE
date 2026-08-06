import Quickshell
import Quickshell.Wayland
import Quickshell.Io
import QtQuick

import "../popups" as Popups
import ".."

Scope {
    // Taskbar variants, we have one taskber per screen.
    Variants {
        model: Quickshell.screens
        Item {
            id: root
            required property var modelData
            property int currentPopup: Config.SystemPopup.None

            PanelWindow {
                id: taskbar
                screen: root.modelData
                WlrLayershell.layer: WlrLayer.Bottom
                WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
                WlrLayershell.namespace: "diinki_celestialantiquity:bars"

                anchors {
                    top: true
                    //left: true
                    //right: true
                }
                width: screen.width / 2
                implicitHeight: 18
                color: "transparent"
                /*=== Taskbar Background (colors & shading) ===*/
                Rectangle {
                    anchors.fill: parent
                    property color glassColor: Config.colors.glassTintColor
                    color: Qt.rgba(glassColor.r, glassColor.g, glassColor.b, 0.2)
                    border.width: 1
                    bottomLeftRadius: 6
                    bottomRightRadius: 6
                    border.color: Config.colors.outline
                }

                /*=== ===================================== ===*/

                /*=== Workspaces & Background for it ===*/
                Item {
                    id: test2
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    height: parent.height - 8

                    // The margins are weird due to the additional outlines added to each button
                    // that add depth, which is 1 pixel; thus we expand the width by 5 and not 4.
                    anchors.leftMargin: 11
                    width: workspaces.width + 5

                    Workspaces {
                        id: workspaces
                        anchors.leftMargin: 2
                        anchors.rightMargin: 0
                    }
                }
                /*=== ============================== ===*/
                Popups.AppLauncher {
                    id: appLauncher
                    closeCallback: taskbar.closeAllPopups
                    menuWidth: 0
                    popupWidth: 500
                    screenHeight: modelData.height
                    currentPopup: root.currentPopup
                }
                function closeAllPopups() {
                    root.currentPopup = Config.SystemPopup.None;
                }
                TaskbarButton {
                    id: appLauncherButton
                    isToggled: root.currentPopup == Config.SystemPopup.AppLauncher ? true : false
                    iconFontValue: "\ue8b6"
                    anchors.centerIn: parent
                    onClicked: {
                        if (root.currentPopup == Config.SystemPopup.None) {
                            //appLauncher.openAppLauncher();
                            root.currentPopup = Config.SystemPopup.AppLauncher;
                        } else {
                            //taskbar.closeAllPopups();
                            root.currentPopup = Config.SystemPopup.None;
                        }
                        root.currentPopup = Config.SystemPopup.AppLauncher;
                    }
                }
                Scope {
                    id: appLauncherIpc
                    property string screenName: taskbar.screen.name
                    IpcHandler {
                        target: "appLauncher_" + appLauncherIpc.screenName
                        function toggleAppLauncher() {
                            if (root.currentPopup == Config.SystemPopup.None) {
                                root.currentPopup = Config.SystemPopup.AppLauncher;
                            } else {
                                root.currentPopup = Config.SystemPopup.None;
                            }
                        }
                    }
                }

                /*=== ============================= ===*/

                /*=== System Tray & Background for it ===*/
                Item {
                    id: test
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    height: parent.height - 8
                    width: sysTray.width + 18

                    SysTray {
                        id: sysTray
                    }
                }
                /*=== =============================== ===*/
            }

            /*=== POPUP CLOSING PANEL ===*/
            // This panel is strictly for detecting clicks
            // outside of popups in order to close them.
            PanelWindow {
                id: overlay
                screen: root.modelData
                color: "transparent"

                implicitHeight: screen.height

                // Better UX to not have it close on hotbar press? idk. TODO: Figure this out
                //implicitHeight: screen.height - taskbar.implicitHeight

                anchors {
                    bottom: true
                    left: true
                    right: true
                }

                visible: root.currentPopup != Config.SystemPopup.None ? true : false

                exclusionMode: ExclusionMode.Ignore

                MouseArea {
                    id: popupArea
                    width: Screen.width
                    height: Screen.height
                    visible: root.currentPopup != Config.SystemPopup.None ? true : false
                    onClicked: {
                        root.currentPopup = Config.SystemPopup.None;
                    }
                }
            }
            /*=== =================== ===*/
        }
    }

    enum SystemPopups {
        Startmenu,
        ThemePicker,
        None
    }
}
