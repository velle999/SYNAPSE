import QtQuick
import Quickshell
import ".."

/*
 * The quick-launch strip.
 *
 * SynapseOS already has three ways to start something — the native start menu
 * (Super tap), the dock, and the desktop right-click menu — so this one earns
 * its place by being always-visible rather than by being another menu. Where a
 * row duplicates something synui already draws, it DISPATCHES to it
 * (`synctl dispatch menu`, `… wallpaper`) instead of reimplementing it; two
 * wallpaper pickers that disagree would be worse than none.
 *
 * `interactive`, and the price of it: while this is on, clicks in the strip go
 * to it and not to the desktop underneath, so the desktop right-click menu is
 * unreachable there. Unavoidable for anything meant to be clicked, and why it
 * is off by default. The reporting widgets avoid it by taking nothing but their
 * grip — see WidgetFrame.
 */
WidgetFrame {
    id: root

    widgetId: "launcher"
    shown: WidgetState.launcher
    label: "LAUNCH"
    accent: Theme.cyan
    interactive: true

    homeEdgeH: "left"; homeEdgeV: "top"
    homeMarginX: 20
    homeMarginY: Theme.barHeight + 60

    cardWidth: 224
    bodyHeight: col.implicitHeight

    // Rows dispatch to synui's own panels where one exists, and spawn only
    // where it does not. `synctl dispatch <action>` runs the exact keybind
    // action, so these can never drift from what the keyboard does.
    readonly property var entries: [
        { name: "terminal",     sub: "syntty",            exec: ["syntty"] },
        // rofi, the same launcher Super+Space runs. Spawned rather than
        // dispatched precisely because of the rule above: dispatch is for
        // things synui DRAWS, and rofi is an external program. This row used
        // to run `synctl dispatch menu`, which opens synui's own menu panel —
        // a different thing from the app launcher the row is labelled as.
        { name: "app launcher", sub: "rofi",              exec: ["rofi", "-show", "drun"] },
        { name: "files",        sub: "file manager",      exec: ["synui-open-folder"] },
        { name: "browser",      sub: "firefox",           exec: ["firefox"] },
        { name: "select wall",  sub: "wallpaper library", exec: ["synctl", "dispatch", "wallpaper"] }
    ]

    Column {
        id: col
        width: parent.width
        spacing: 4

        Repeater {
            model: root.entries

            delegate: Item {
                id: row
                required property var modelData
                required property int index

                width: parent.width
                height: 36

                // The nudge on hover is the whole character of this widget in
                // the rice it came from; keep it small enough to read as
                // feedback rather than motion.
                x: mouse.containsMouse ? 7 : 0
                Behavior on x { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutQuad } }

                Rectangle {
                    anchors.fill: parent
                    color: root.accent
                    opacity: mouse.containsMouse ? 0.13 : 0.0
                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                }

                // The row's own left edge, lit only under the pointer. A whole
                // border around every row would turn the strip into a stack of
                // boxes; one lit edge says "this one" and nothing else.
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 2
                    color: mouse.containsMouse ? root.accent : Theme.fg
                    opacity: mouse.containsMouse ? 1.0 : 0.13
                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                }

                Text {
                    id: num
                    anchors { left: parent.left; leftMargin: 11; verticalCenter: parent.verticalCenter }
                    text: String(row.index + 1).padStart(2, "0")
                    color: mouse.containsMouse ? root.accent : Theme.fgDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.letterSpacing: 1
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }

                Column {
                    anchors {
                        left: num.right; leftMargin: 11
                        right: parent.right; rightMargin: 8
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: 0
                    Text {
                        text: row.modelData.name
                        color: Theme.fg
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: "› " + row.modelData.sub
                        color: Theme.fgDim
                        font.family: Theme.fontFamily
                        font.pixelSize: 9
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    // execDetached, not a shared Process: a Process runs one
                    // command at a time and QUEUES the next one until the
                    // running child exits, so clicking a second tile while the
                    // first application was still open did nothing until that
                    // application was closed. The start menu had the identical
                    // bug — a widget whose whole purpose is launching things
                    // cannot launch only one at a time.
                    onClicked: Quickshell.execDetached(row.modelData.exec)
                }
            }
        }
    }
}
