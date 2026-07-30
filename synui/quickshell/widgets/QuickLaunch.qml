import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
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
 * This is the one widget that takes pointer input, which has a consequence
 * worth knowing: while it is on, clicks in this strip go to it and not to the
 * desktop underneath, so the desktop right-click menu is unreachable there.
 * That is unavoidable for anything clickable and is why it is off by default.
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    visible: WidgetState.launcher && modelData.name === WidgetState.primaryOutput

    WlrLayershell.layer: WlrLayer.Bottom
    anchors { left: true; top: true }
    margins { left: 20; top: Theme.barHeight + 60 }

    implicitWidth: 210
    implicitHeight: col.implicitHeight

    exclusiveZone: 0
    // Not focusable: these are pointer targets. Taking keyboard focus would
    // pull keys away from the focused window for a widget that needs none.
    focusable: false
    color: "transparent"

    // Rows dispatch to synui's own panels where one exists, and spawn only
    // where it does not. `synctl dispatch <action>` runs the exact keybind
    // action, so these can never drift from what the keyboard does.
    readonly property var entries: [
        { name: "terminal",     sub: "kitty",             exec: ["kitty"] },
        { name: "app launcher", sub: "all applications",  exec: ["synctl", "dispatch", "menu"] },
        { name: "files",        sub: "file manager",      exec: ["synui-open-folder"] },
        { name: "browser",      sub: "firefox",           exec: ["firefox"] },
        { name: "select wall",  sub: "wallpaper library", exec: ["synctl", "dispatch", "wallpaper"] }
    ]

    Column {
        id: col
        width: parent.width
        spacing: 6

        Repeater {
            model: root.entries

            delegate: Rectangle {
                id: row
                required property var modelData

                width: parent.width
                height: 38
                radius: 6
                color: mouse.containsMouse ? Theme.activeBg : Theme.popupBg
                border.color: mouse.containsMouse ? Theme.magenta : Theme.hoverBg
                border.width: 1
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                // The nudge on hover is the whole character of this widget in
                // the rice it came from; keep it small enough to read as
                // feedback rather than motion.
                x: mouse.containsMouse ? 8 : 0
                Behavior on x { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutQuad } }

                Rectangle {
                    id: chip
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    width: 22; height: 22; radius: 4
                    color: Theme.hoverBg
                    Text {
                        anchors.centerIn: parent
                        text: row.modelData.name.charAt(0).toUpperCase()
                        color: Theme.cyan
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }

                Column {
                    anchors {
                        left: chip.right; leftMargin: 9
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
                    onClicked: {
                        launch.command = row.modelData.exec
                        launch.running = true
                    }
                }
            }
        }
    }

    Process { id: launch }
}
