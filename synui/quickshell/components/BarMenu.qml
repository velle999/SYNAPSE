import QtQuick
import Quickshell
import ".."

/*
 * The bar's right-click menu — per-monitor feature toggles.
 *
 * DISMISSAL IS A POPUP GRAB. `grabFocus` makes this an xdg_popup with a real
 * grab, which is the mechanism every menu on every desktop uses and the one the
 * tray menus here (QsMenuAnchor) have always had: the COMPOSITOR notices a click
 * outside and dismisses, so there is no screen-covering input region to get
 * wrong, and the click that dismissed it does not also land on whatever was
 * underneath. It brings keyboard focus with it, which is how Escape can close
 * the menu — and it is given back the moment the menu goes away, so the "don't
 * steal keys from the focused window" objection this file used to carry does
 * not apply: the menu only holds them while it is up and you opened it.
 *
 * The pointer-leave timer is kept as a BACKSTOP, not as the mechanism. It is
 * what closes the menu if the grab is ever refused, which would otherwise leave
 * a menu with no way out but the Done row — the complaint that got this fixed.
 * Toggles still leave the menu open on purpose: fitting a narrow monitor
 * usually means turning off two or three things in one visit.
 */
PopupWindow {
    id: menu

    // Which monitor this menu is editing. The bar passes its own screen; every
    // toggle here writes only that output's settings.
    property string output: ""
    property int    anchorX: 0

    // The window this menu hangs off, passed in by the bar.
    //
    // It CANNOT be discovered from here. A PopupWindow is not an Item, so it has
    // no `parent` — inside a PanelWindow `menu.parent` is *undefined*, not a
    // window — and the `QsWindow` attached property only resolves on an Item
    // (which is why BarModule and Tray can say `root.QsWindow.window` and this
    // cannot). The defensive `parent ? parent.QsWindow.window : null` this used
    // to read therefore took the null branch every single time, leaving the
    // popup with no anchor window, and a PopupWindow with no anchor window never
    // maps: right-clicking the bar did nothing at all, silently.
    //
    // `required` so a caller that forgets fails loudly at load instead of
    // quietly reintroducing a dead menu.
    required property var barWindow

    implicitWidth: 232
    implicitHeight: col.implicitHeight + 16
    color: "transparent"
    visible: false

    // See the header: this is what makes a click anywhere else close the menu.
    grabFocus: true

    // The compositor broke the grab (a click outside, a Super+key, a window
    // taking focus). The window is already down; this is what puts the QML
    // property back in step with it, or the next openAt() would set visible to
    // a value it already held and map nothing.
    onClosed: menu.visible = false

    function openAt(x) {
        menu.anchorX = x
        menu.visible = true
        closeTimer.stop()
    }

    anchor {
        window: menu.barWindow
        rect.x: Math.max(4, menu.anchorX - menu.implicitWidth / 2)
        // Below a top bar, above a bottom one. BarConfig owns the arithmetic
        // so the four popup sites cannot drift apart.
        rect.y: BarConfig.popupY(menu.implicitHeight)
    }

    Timer {
        id: closeTimer
        interval: 1200
        onTriggered: menu.visible = false
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.popupBg
        border.color: Theme.magenta
        border.width: 1

        // Escape closes, now that the grab brings keyboard focus with it. Focus
        // is taken when the menu maps rather than declared once: a hidden window
        // has no focus to hold, so an unconditional `focus: true` would be
        // claimed at load and never re-claimed on the second open.
        focus: true
        Keys.onEscapePressed: menu.visible = false
        onVisibleChanged: if (visible) forceActiveFocus()

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onEntered: closeTimer.stop()
            onExited: closeTimer.restart()
        }

        Column {
            id: col
            anchors { fill: parent; margins: 8 }
            spacing: 1

            Text {
                text: "Bar · " + menu.output
                color: Theme.magenta
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.letterSpacing: 1
                bottomPadding: 4
            }

            Repeater {
                model: BarConfig.rows

                delegate: Rectangle {
                    id: row
                    required property var modelData

                    readonly property bool on: BarConfig.get(menu.output, modelData.key)

                    width: col.width
                    height: 22
                    radius: Theme.radius
                    color: rowMouse.containsMouse ? Theme.hoverBg : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    // A checkbox, not just colour: on a light theme a "dim vs
                    // bright" distinction is nearly invisible, and this row is
                    // the only thing telling you what state you just set.
                    Text {
                        id: box
                        anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                        text: row.on ? "[x]" : "[ ]"
                        color: row.on ? Theme.cyan : Theme.fgDim
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }

                    Text {
                        anchors {
                            left: box.right; leftMargin: 8
                            right: parent.right; rightMargin: 6
                            verticalCenter: parent.verticalCenter
                        }
                        text: row.modelData.label
                        color: row.on ? Theme.fg : Theme.fgDim
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: closeTimer.stop()
                        onExited: closeTimer.restart()
                        // Stays open on purpose — fitting a narrow monitor
                        // usually means turning off two or three things.
                        onClicked: BarConfig.toggle(menu.output, row.modelData.key)
                    }
                }
            }

            Rectangle {
                width: col.width; height: 1
                color: Theme.hoverBg
            }

            Rectangle {
                width: col.width
                height: 20
                radius: Theme.radius
                color: doneMouse.containsMouse ? Theme.activeBg : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "Done"
                    color: Theme.fg
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }

                MouseArea {
                    id: doneMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: closeTimer.stop()
                    onClicked: menu.visible = false
                }
            }
        }
    }
}
