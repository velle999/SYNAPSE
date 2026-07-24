import QtQuick
import Quickshell
import Quickshell.Widgets
import Quickshell.Services.SystemTray
import ".."

/*
 * System tray (StatusNotifierItem).
 *
 * This module is load-bearing, and the waybar config says why: nothing else on
 * SynapseOS runs an SNI watcher, so without it org.kde.StatusNotifierWatcher is
 * simply absent from the bus. An app that "closes to tray" (Steam does by
 * default) then unmaps its window and hands its icon to nobody — the window is
 * unreachable, with no tray, no taskbar, and Steam will not raise it again.
 * Losing this on the way from waybar to quickshell would strand Steam.
 *
 * Passive items are shown deliberately. Steam registers its item and only
 * later marks it Active; hiding Passive items means the icon can be missing
 * exactly when it is needed, which is indistinguishable from a broken tray.
 * quickshell exposes every item, so this is simply the absence of a filter —
 * but it is a decision, not an oversight.
 */
Row {
    id: root
    spacing: Theme.moduleGap

    Repeater {
        model: SystemTray.items

        delegate: Rectangle {
            id: item
            required property var modelData

            width: Theme.iconSize + 12
            height: Theme.barHeight
            radius: Theme.radius
            color: mouse.containsMouse ? Theme.hoverBg : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            IconImage {
                anchors.centerIn: parent
                implicitSize: Theme.iconSize + 2
                source: item.modelData.icon
                // Some SNI apps ship only a themed name with no pixmap; the
                // blank space is better than a broken-image glyph.
                visible: source !== ""
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton

                onClicked: (m) => {
                    const d = item.modelData
                    if (m.button === Qt.LeftButton) {
                        // onlyMenu items have no activate action; showing the
                        // menu is the whole interaction for them.
                        if (d.onlyMenu) menuAnchor.open()
                        else d.activate()
                    } else if (m.button === Qt.RightButton) {
                        menuAnchor.open()
                    } else if (m.button === Qt.MiddleButton) {
                        d.secondaryActivate()
                    }
                }
            }

            QsMenuAnchor {
                id: menuAnchor
                menu: item.modelData.menu
                anchor {
                    window: item.QsWindow.window
                    rect.x: item.mapToItem(null, 0, 0).x
                    rect.y: Theme.barHeight
                }
            }
        }
    }
}
