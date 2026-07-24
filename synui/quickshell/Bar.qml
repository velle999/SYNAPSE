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
 */
PanelWindow {
    id: bar

    required property var modelData
    screen: modelData

    anchors { top: true; left: true; right: true }
    implicitHeight: Theme.barHeight

    // Reserve the strip so maximized windows stop below it. Auto would infer
    // this from the anchors, but stating it means a future layout change
    // cannot silently un-reserve the bar and let windows slide underneath.
    exclusiveZone: Theme.barHeight

    // Not focusable: the bar is pointer-driven, and taking keyboard focus here
    // would steal keys from the focused window for no benefit. (synui grants
    // layer-shell keyboard focus correctly — verified 2026-07-22 — so this is a
    // choice, not a limitation.)
    focusable: false

    color: "transparent"

    Rectangle {
        anchors.fill: parent
        color: Theme.bg

        // The magenta underline the waybar CSS drew with border-bottom.
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
        }

        // ── Centre: clock ────────────────────────────────
        Clock {
            anchors.centerIn: parent
        }

        // ── Right: status modules ────────────────────────
        Row {
            anchors {
                right: parent.right
                rightMargin: 6
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.moduleGap

            Tray     { anchors.verticalCenter: parent.verticalCenter }
            Media    {}
            GameMode {}
            Battery  {}
            Volume   {}
            Cpu      {}
            Memory   {}
            Bluetooth{}
            Network  {}
        }
    }
}
