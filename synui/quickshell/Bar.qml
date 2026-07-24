import QtQuick
import Quickshell
import Quickshell.Wayland
import "modules"
import "components"

/*
 * The SYNAPSE bar.
 *
 * Layout: virtual desktops left, clock centre, status right.
 *
 * The left side is shared, not free. synui draws the "◢ SYNAPSE" launcher over
 * the bar's top-left corner itself (src/launcher.c) and a click there calls
 * menu_toggle() in the compositor, so anything drawn under it is both invisible
 * and unclickable. Workspaces starts past it by mirroring launcher.c's own
 * width formula.
 *
 * WHAT IS SHOWN IS PER MONITOR (BarConfig, right-click the bar). A 1080-wide
 * portrait panel cannot hold what a 2560-wide landscape one can: the centred
 * clock ends up drawn straight through the tray and the media title. Making it
 * a global setting would mean losing the workspace pills on the wide monitors
 * to fix the narrow one.
 */
PanelWindow {
    id: bar

    required property var modelData
    screen: modelData

    readonly property string outName: modelData.name

    readonly property bool autohide: BarConfig.get(bar.outName, "autohide")

    // Revealed when it is not hiding at all, while the pointer is anywhere over
    // it, or while its menu is open — closing the bar out from under its own
    // menu would be a fine way to make the menu unusable.
    readonly property bool revealed: !bar.autohide || edgeHover.hovered || menu.visible

    anchors { top: true; left: true; right: true }
    implicitHeight: Theme.barHeight

    // Reserve the strip so maximized windows stop below it — but an auto-hiding
    // bar must reserve NOTHING, or it hides and leaves its own empty gap behind,
    // which is the whole thing it was asked not to do.
    exclusiveZone: bar.autohide ? 0 : Theme.barHeight

    // Not focusable: the bar is pointer-driven, and taking keyboard focus here
    // would steal keys from the focused window for no benefit. (synui grants
    // layer-shell keyboard focus correctly — verified 2026-07-22 — so this is a
    // choice, not a limitation.)
    focusable: false

    color: "transparent"

    // While hidden, only a sliver at the screen edge takes pointer input. The
    // window is still full height, so without this a hidden bar would keep
    // swallowing every click along the top of the screen — invisibly, which is
    // the worst version of that bug. The sliver is what the pointer runs into
    // to bring the bar back.
    mask: Region {
        x: 0
        y: 0
        width: bar.width
        height: bar.revealed ? Theme.barHeight : 3
    }

    // Hover lives on its own item, NOT on the sliding content: once the content
    // is translated off the top, the pointer is no longer over it and it could
    // never be hovered back into view. A HoverHandler rather than a MouseArea
    // because handlers do not consume events — the modules' own MouseAreas keep
    // working, and hovering one still counts as hovering the bar.
    Item {
        anchors.fill: parent
        HoverHandler { id: edgeHover }
    }

    Item {
        id: content
        width: bar.width
        height: Theme.barHeight
        y: bar.revealed ? 0 : -Theme.barHeight
        Behavior on y {
            NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.bg

            // Right-click anywhere the modules do not claim opens the per-monitor
            // menu. It sits UNDER the modules, so a module that uses right-click
            // for its own thing (volume → mixer, network → nmtui) still wins;
            // modules with no use for it decline the button so it falls through
            // to here rather than being swallowed. See BarModule.acceptsRight.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: (m) => menu.openAt(m.x)
            }

            // The accent underline the waybar CSS drew with border-bottom.
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.accentHeight
                color: Theme.magenta
            }

            // ── Left: virtual desktops ───────────────────────
            // Workspaces reserves the launcher's width itself rather than being
            // offset from here — see the note in Workspaces.qml.
            Workspaces {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                visible: BarConfig.get(bar.outName, "workspaces")
            }

            // ── Centre: clock ────────────────────────────────
            Clock {
                anchors.centerIn: parent
                visible: BarConfig.get(bar.outName, "clock")
            }

            // ── Right: status modules ────────────────────────
            Row {
                anchors {
                    right: parent.right
                    rightMargin: 6
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.moduleGap

                Tray      { anchors.verticalCenter: parent.verticalCenter
                            visible: BarConfig.get(bar.outName, "tray") }
                Media     { barVisible: BarConfig.get(bar.outName, "media") }
                GameMode  {}
                Battery   {}
                Volume    { barVisible: BarConfig.get(bar.outName, "volume") }
                Cpu       { barVisible: BarConfig.get(bar.outName, "sysinfo") }
                Memory    { barVisible: BarConfig.get(bar.outName, "sysinfo") }
                Bluetooth { barVisible: BarConfig.get(bar.outName, "netbt") }
                Network   { barVisible: BarConfig.get(bar.outName, "netbt") }
            }
        }
    }

    BarMenu {
        id: menu
        output: bar.outName
        // A PopupWindow cannot find its own window — see BarMenu.barWindow.
        barWindow: bar
    }
}
