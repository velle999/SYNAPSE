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

            PanelWindow {
                id: widgetScreen
                screen: root.modelData
                WlrLayershell.layer: WlrLayer.Bottom
                color: "Transparent"
                anchors {
                    top: true
                    left: true
                    right: true
                    bottom: true
                }
                Rectangle {
                    anchors.fill: parent
                    color: "Transparent"

                    Repeater {
                        anchors.fill: parent
                        model: Object.values(Config.widgets[widgetScreen.screen.name])

                        delegate: Loader {
                            //anchors.fill: parent
                            required property var modelData
                            source: Config.widgetPaths[modelData.widgetName]
                            onLoaded: {
                                item.x = modelData.x;
                                item.y = modelData.y;
                                item.widgetBackground = modelData.enableBackground;
                            }
                        }
                    }
                }
            }
        }
    }
}
