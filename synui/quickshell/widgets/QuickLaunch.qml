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

    /*
     * The strip goes on whatever this card is sitting on — the same opt-in the
     * note, the monitor and the clock's date take.
     *
     * ⚠ THE HOVER HID THIS, WHICH IS WHY IT LOOKED FINE TO ANYONE USING IT.
     * The row under the pointer lights its edge and its number in the accent and
     * washes itself at 0.13, so the row you are ABOUT TO CLICK is legible on
     * anything — and every other row is 12px and 9px of the theme's ink on a
     * card that at a low glass level is not there. You find that out by LOOKING
     * at the strip rather than by using it, and looking at it is what it is for:
     * this is the widget whose whole claim over the start menu is being
     * always-visible.
     *
     * The accents stay accents. A lit edge and a lit number mean THIS ONE, the
     * way red at 90% means hot on the monitor and the note keeps its yellow.
     */
    inkOnBackdrop: true

    /* The weather card shares this corner and sits above, so this hops below
     * it when both are on — the same arrangement the pet has with the monitor,
     * and only while nobody has dragged either: WidgetFrame stops applying home
     * margins the moment there is a stored position. */
    readonly property int weatherClearance: 118

    homeEdgeH: "left"; homeEdgeV: "top"
    homeMarginX: 20
    homeMarginY: Theme.barHeight + 60
               + (WidgetState.weather ? weatherClearance : 0)

    cardWidth: 224
    bodyHeight: col.implicitHeight

    // Rows dispatch to synui's own panels where one exists, and spawn only
    // where it does not. `synctl dispatch <action>` runs the exact keybind
    // action, so these can never drift from what the keyboard does.
    readonly property var entries: [
        { name: "terminal",     sub: "syntty",            exec: ["syntty"] },
        // rofi, the same launcher Super+= runs. Spawned rather than
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
                    color: mouse.containsMouse ? root.accent : root.ink
                    opacity: mouse.containsMouse ? 1.0 : 0.13
                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                }

                Text {
                    id: num
                    anchors { left: parent.left; leftMargin: 11; verticalCenter: parent.verticalCenter }
                    text: String(row.index + 1).padStart(2, "0")
                    color: mouse.containsMouse ? root.accent : root.inkDim
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
                        color: root.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: "› " + row.modelData.sub
                        color: root.inkDim
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
