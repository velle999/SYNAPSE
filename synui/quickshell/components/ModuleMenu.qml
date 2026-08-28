import QtQuick
import Quickshell
import ".."

/*
 * ModuleMenu — a small right-click menu for ONE bar module.
 *
 * BarMenu is the bar's own menu: per-monitor furniture switches, checkboxes,
 * the plugin section. This is the other shape — a short list of ACTIONS
 * belonging to whatever module was right-clicked, the way right-clicking a
 * thing in any desktop offers that thing's options.
 *
 * ⚠ IT IS A COMPONENT RATHER THAN A SECOND POPUP WRITTEN IN A MODULE, and
 * BarMenu's header is the argument. Everything about making a popup dismiss
 * correctly here was learned the hard way and none of it is obvious:
 *
 *   - `grabFocus` is what dismisses it. The COMPOSITOR notices the click
 *     outside, so there is no screen-covering input region to get wrong and the
 *     dismissing click does not also land on what was underneath.
 *   - The leave timer is a BACKSTOP for a refused grab, not the mechanism, and
 *     it has to be long. At 1200ms it WAS the mechanism: open the menu, glance
 *     away, gone.
 *   - ⚠ A HoverHandler, never a MouseArea filling the panel. Qt hands hover to
 *     exactly ONE item, so a panel-filling MouseArea is `exited` the moment the
 *     pointer reaches a row inside the menu — and the enter of the next item
 *     arrives BEFORE the exit of the previous, so rows stopping the timer
 *     themselves cannot save it.
 *   - ⚠ `barWindow` cannot be discovered from in here. A PopupWindow is not an
 *     Item: it has no `parent`, and the QsWindow attached property only
 *     resolves on an Item. A popup with no anchor window never maps, silently.
 *     `required`, so forgetting fails at load instead.
 *
 * A second copy of all that would be a menu that dismisses correctly until the
 * day it does not.
 *
 * Rows are `{ label, detail, key, enabled }`; picking one emits triggered(key)
 * and closes — unlike BarMenu, which stays open, because these are actions and
 * an action you have taken is a menu you are done with.
 */
PopupWindow {
    id: menu

    required property var barWindow
    property int    anchorX: 0
    property string title: ""
    // Each: { label: string, detail: string (optional), key: string,
    //         enabled: bool (optional, default true) }
    property var rows: []

    signal triggered(string key)

    implicitWidth: 236
    implicitHeight: col.implicitHeight + 16
    color: "transparent"
    visible: false
    grabFocus: true

    // The compositor broke the grab. The window is already down; this puts the
    // QML property back in step, or the next openAt() would set `visible` to a
    // value it already held and map nothing.
    onClosed: menu.visible = false

    function openAt(x) {
        menu.anchorX = x
        menu.visible = true
        closeTimer.stop()
    }

    anchor {
        window: menu.barWindow
        rect.x: Math.max(4, menu.anchorX - menu.implicitWidth / 2)
        // BarConfig owns the arithmetic so the popup sites cannot drift apart —
        // below a top bar, above a bottom one.
        rect.y: BarConfig.popupY(menu.implicitHeight)
    }

    // Guarded on where the pointer is NOW, not only on the restarts: a restart
    // is a guess made up to 8 seconds ago, and one stale guess is all it takes
    // to shut the menu under a hand resting on it.
    Timer {
        id: closeTimer
        interval: 8000
        onTriggered: if (!menuHover.hovered) menu.visible = false
    }

    /* What the wallpaper is doing under this menu. The anchor rect is relative
     * to the BAR window, which layer-shell has pinned to a screen edge spanning
     * its full width — so the rect's coordinates are the screen's and no
     * translation is needed. */
    readonly property var backdrop: menu.barWindow
        ? Theme.backdropFor(menu.barWindow.screen,
                            menu.anchor.rect.x, menu.anchor.rect.y,
                            menu.implicitWidth, menu.implicitHeight)
        : null

    Rectangle {
        anchors.fill: parent
        radius: Theme.panelRadius
        color: Theme.popupBgOn(menu.backdrop)
        border.color: Theme.magenta
        border.width: 1

        // Focus is taken when the menu MAPS rather than declared once: a hidden
        // window has no focus to hold, so an unconditional `focus: true` would
        // be claimed at load and never re-claimed on the second open.
        focus: true
        Keys.onEscapePressed: menu.visible = false
        onVisibleChanged: if (visible) forceActiveFocus()

        HoverHandler { id: menuHover }

        Column {
            id: col
            anchors { fill: parent; margins: 8 }
            spacing: 1

            Text {
                text: menu.title
                color: Theme.magenta
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.letterSpacing: 1
                bottomPadding: 4
                visible: menu.title !== ""
            }

            Repeater {
                model: menu.rows

                delegate: Rectangle {
                    id: row
                    required property var modelData

                    // Absent means yes: a row that has to declare itself
                    // enabled is a row somebody will forget to declare.
                    readonly property bool on: modelData.enabled !== false

                    width: col.width
                    height: 22
                    radius: Theme.radius
                    color: (row.on && rowMouse.containsMouse) ? Theme.hoverBg
                                                              : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    Text {
                        id: label
                        anchors {
                            left: parent.left; leftMargin: 8
                            verticalCenter: parent.verticalCenter
                        }
                        text: row.modelData.label
                        color: row.on ? Theme.popupFgOn(menu.backdrop)
                                      : Theme.popupFgDimOn(menu.backdrop)
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }

                    // The right-hand half of a row: a count, a version, "6h".
                    // Dim, and never the reason to click — a row whose label
                    // does not say what it does is a row that needs a better
                    // label, not a longer one.
                    Text {
                        anchors {
                            left: label.right; leftMargin: 8
                            right: parent.right; rightMargin: 8
                            verticalCenter: parent.verticalCenter
                        }
                        horizontalAlignment: Text.AlignRight
                        text: row.modelData.detail || ""
                        color: Theme.popupFgDimOn(menu.backdrop)
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        elide: Text.ElideLeft
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: row.on
                        onClicked: {
                            menu.visible = false
                            menu.triggered(row.modelData.key)
                        }
                    }
                }
            }
        }
    }
}
