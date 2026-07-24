import QtQuick
import Quickshell
import ".."

/*
 * The bar's right-click menu — per-monitor feature toggles.
 *
 * Dismissal is a pointer-leave timer plus an explicit Done row, NOT a
 * click-anywhere grab. A menu that grabs the pointer globally would have to
 * cover the screen with an input region, and this one sits on a bar that is
 * often right next to whatever the user actually meant to click. Leaving it
 * open while the pointer is on it also means several toggles can be flipped in
 * one visit, which is the common case when fitting a narrow monitor.
 *
 * It does not take keyboard focus. The bar is pointer-driven and stealing keys
 * from the focused window to run a menu nobody navigates with arrows would be
 * a regression for no gain.
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

    function openAt(x) {
        menu.anchorX = x
        menu.visible = true
        closeTimer.stop()
    }

    anchor {
        window: menu.barWindow
        rect.x: Math.max(4, menu.anchorX - menu.implicitWidth / 2)
        rect.y: Theme.barHeight + 2
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
