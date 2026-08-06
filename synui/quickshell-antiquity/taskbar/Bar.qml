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

            /*
             * DERIVED, not assigned. Upstream held the launcher's open state
             * here, one copy per screen, and every opener wrote into its own
             * copy — which is fine under Hyprland, where the only openers were
             * this bar's own button and a bind that named the monitor. It
             * cannot answer `synui-bar ipc call menu toggle <output>`, which
             * arrives at the shell rather than at any one screen's bar.
             *
             * LauncherState is the single owner now; this binding is just "is
             * it open, and is it open HERE". `output === ""` counts as here so
             * that a caller who named no monitor, and whose fallback probe has
             * not answered yet, still gets a launcher somewhere rather than an
             * invisible one everywhere.
             */
            readonly property int currentPopup: LauncherState.open
                                                && (LauncherState.output === modelData.name
                                                    || LauncherState.output === "")
                                                ? Config.SystemPopup.AppLauncher
                                                : Config.SystemPopup.None

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
                    LauncherState.close();
                }
                TaskbarButton {
                    id: appLauncherButton
                    isToggled: root.currentPopup == Config.SystemPopup.AppLauncher ? true : false
                    iconFontValue: "\ue8b6"
                    anchors.centerIn: parent
                    // Upstream's body ended with an unconditional
                    // `currentPopup = AppLauncher` after the if/else, so the
                    // else branch was dead and the button could only ever open
                    // \u2014 clicking it again did nothing. A toggle that names this
                    // screen is what both branches were reaching for.
                    onClicked: LauncherState.toggle(root.modelData.name)
                }
                /*
                 * The per-screen `appLauncher_<name> toggleAppLauncher` handler
                 * that stood here was Hyprland's half of a bind that no longer
                 * exists \u2014 nothing in SYNAPSE ever called it. The Super tap's
                 * target lives in shell.qml now, once for the whole shell, on
                 * the same `menu` contract the SYNAPSE bar answers to. See
                 * LauncherState.qml.
                 */

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
                        LauncherState.close();
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
