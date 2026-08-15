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
            color: mouse.containsMouse ? Theme.barHoverBg : "transparent"
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

            /*
             * Anchor to the ITEM, not to a hand-computed rect.
             *
             * This used to be `window:` + `rect.x: item.mapToItem(null,0,0).x`,
             * and the menus opened offset from their icon — sometimes left,
             * sometimes right. mapToItem() is a function call, so QML records no
             * dependency on the geometry it reads: the binding is evaluated once
             * when the delegate is created, which is BEFORE the Row has laid the
             * delegate out, and never again. Every icon therefore anchored to a
             * position it no longer occupied, and the error grew with the item's
             * index and changed whenever an app added or removed its icon.
             *
             * `rect.y: Theme.barHeight` was wrong for a second reason: the bar
             * autohides by sliding `content.y`, so the icon's offset inside the
             * window is not a constant.
             *
             * PopupAnchor tracks an item's real position and size, so the menu
             * follows the icon through layout changes and the autohide slide,
             * and `gravity` lets it size itself against a rect with actual
             * width instead of the zero-width one a bare x/y pair produced.
             */
            QsMenuAnchor {
                id: menuAnchor
                menu: item.modelData.menu
                anchor {
                    item: item
                    // Away from the edge the bar is on, so a bottom bar's tray
                    // menus grow upward instead of off the screen.
                    edges:   BarConfig.atBottom ? Edges.Top : Edges.Bottom
                    gravity: BarConfig.atBottom ? Edges.Top : Edges.Bottom
                }
            }
        }
    }
}
