import QtQuick
import Quickshell
import Quickshell.Wayland
import "modules"
import "components"

/*
 * The SYNAPSE bar.
 *
 * Layout: start button and virtual desktops left, clock centre, status right.
 *
 * The left side used to be shared, not free: the compositor drew the "◢ SYNAPSE"
 * launcher over the bar's top-left corner itself and hit-tested it there, so
 * anything under it was invisible AND unclickable, and Workspaces had to mirror
 * launcher.c's width formula to keep out of the way. The button is a module here
 * now (modules/Launcher.qml), which is what lets it slide with the bar instead
 * of hanging over the desktop after an auto-hide.
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

    // Wanted up when it is not hiding at all, while the pointer is anywhere over
    // it, or while its menu is open — closing the bar out from under its own
    // menu would be a fine way to make the menu unusable. The mixer counts for
    // the same reason: adjusting a slider means the pointer is on the popup and
    // not on the bar, which is exactly when auto-hide would pull it away.
    readonly property bool wantsReveal: !bar.autohide || edgeHover.hovered
                                        || menu.visible || volume.mixerOpen

    // …but `revealed` is a latch, not that binding. Going up is immediate; going
    // down waits, so a pointer that only crosses the top edge on its way to a
    // window's titlebar does not drag the bar down and back up behind it.
    property bool revealed: true

    onWantsRevealChanged: {
        if (bar.wantsReveal) { hideDelay.stop(); bar.revealed = true }
        else                   hideDelay.restart()
    }
    Component.onCompleted: bar.revealed = bar.wantsReveal

    Timer {
        id: hideDelay
        interval: Theme.barHideDelay
        // Re-read rather than assigning false: the pointer may have come back
        // during the wait, and this fires either way.
        onTriggered: bar.revealed = bar.wantsReveal
    }

    // Which edge, global (BarConfig.edge). Left and right are always anchored:
    // the bar spans the monitor either way, only the vertical side changes.
    anchors {
        top:    !BarConfig.atBottom
        bottom:  BarConfig.atBottom
        left: true
        right: true
    }
    implicitHeight: Theme.barHeight

    // Reserve the strip so maximized windows stop below it — but an auto-hiding
    // bar must reserve NOTHING, or it hides and leaves its own empty gap behind,
    // which is the whole thing it was asked not to do.
    //
    // This value has to be RIGHT AT CREATION: quickshell sends
    // set_exclusive_zone once when it makes the layer surface, and a change
    // arriving before that surface's first configure is dropped on the floor —
    // silently, with the QML property reading the new value. That is why
    // BarConfig reads bar.json synchronously (blockLoading); an async read
    // landed inside exactly that window and left every auto-hiding bar
    // reserving 28px forever. Do not make `autohide` depend on anything that
    // resolves later than construction.
    exclusiveZone: bar.autohide ? 0 : Theme.barHeight

    // Not focusable: the bar is pointer-driven, and taking keyboard focus here
    // would steal keys from the focused window for no benefit. (synui grants
    // layer-shell keyboard focus correctly — verified 2026-07-22 — so this is a
    // choice, not a limitation.)
    focusable: false

    color: "transparent"

    // While hidden, only a strip at the screen edge takes pointer input. The
    // window keeps its full height, so without this a hidden bar would keep
    // swallowing every click along the top of the screen — invisibly, which is
    // the worst version of that bug. The strip is what the pointer runs into to
    // bring the bar back.
    //
    // The region FOLLOWS THE SLIDE rather than snapping with `revealed`. Snapped,
    // the two disagree for the length of the animation in both directions: a bar
    // still visibly on screen stops taking clicks on the way out (they land in
    // the window behind it), and the pointer, now outside the region, cannot
    // catch it on the way back either — so it looks like the bar ignored you.
    //
    // On a bottom bar the content slides DOWN (content.y goes positive), so the
    // strip that stays live is the one at the bottom of the window, and the
    // arithmetic mirrors: on top, `barHeight + content.y` is how much is still
    // on screen with content.y negative; at the bottom it is
    // `barHeight - content.y`, and the region has to start where that begins
    // rather than at y=0.
    readonly property int liveHeight:
        Math.max(Theme.barEdgeStrip,
                 Theme.barHeight + (BarConfig.atBottom ? -content.y : content.y))

    mask: Region {
        x: 0
        y: BarConfig.atBottom ? Theme.barHeight - bar.liveHeight : 0
        width: bar.width
        height: bar.liveHeight
    }

    // The hover item is the content's PARENT, not its sibling.
    //
    // It cannot be the content itself: once that is translated off the top the
    // pointer is no longer over it and it could never be hovered back into view.
    // But as a sibling underneath, it is starved — Qt delivers hover front to
    // back and stops at the first item that takes it, so every module MouseArea
    // (hoverEnabled, for its own wash) shadowed this handler. Hovering the
    // background revealed the bar; sliding onto the tray, a workspace pill or
    // any module reported "not hovered" and hid it out from under the pointer,
    // which then re-armed the edge strip and revealed it again. That flapping is
    // the auto-hide glitch. Nesting fixes it: an ancestor gets hover alongside
    // its children, so the handler stays hovered anywhere over the bar and the
    // modules keep their own hover (verified against Qt 6.11 hover delivery).
    // Stacking the handler on TOP is not the fix — it starves the modules
    // instead, `blocking: false` or not.
    Item {
        anchors.fill: parent
        HoverHandler { id: edgeHover }

        Item {
            id: content
            width: bar.width
            height: Theme.barHeight
            // Hides off the edge it lives on: up from the top, down from the
            // bottom. Sliding a bottom bar upwards would take it across the
            // desktop instead of off it.
            y: bar.revealed ? 0
                            : (BarConfig.atBottom ? Theme.barHeight
                                                  : -Theme.barHeight)
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
                // It marks the edge FACING THE DESKTOP, so on a bottom bar it
                // is an overline — anchored to the bottom there it would be
                // drawn along the screen edge, where half of it is off-screen
                // and the rest reads as a stray line rather than a rule.
                //
                // WHICH EDGE IS A `y`, NOT AN ANCHOR SWAP, and that is not a
                // style preference.
                //
                // It used to be `top: atBottom ? parent.top : undefined` with a
                // matching `bottom`, and those are two INDEPENDENT bindings. On
                // a live move they do not re-evaluate atomically: `top` takes
                // parent.top while `bottom` is still parent.bottom, and for that
                // instant the rule is anchored top AND bottom — which is Qt's
                // signal to size the item from its anchors and throw the
                // `height` binding away. Clearing `bottom` a moment later
                // removes the anchor but does NOT restore the height, so the
                // 2px rule stayed 28px tall: the whole bar painted solid accent,
                // Theme.bg nowhere, until the bar was restarted.
                //
                // It only ever bit a move, never a login — at startup the
                // ternaries resolve once and only one of the two is ever set,
                // which is why a bottom bar comes up correct and turns magenta
                // the moment you touch Desktop ▸ Bar edge.
                //
                // With no vertical anchor at all there is nothing that can
                // override `height`, and `y` is a single binding that cannot be
                // observed half-applied. Verified with the pattern in
                // isolation: rule.height across the flip is 2 → 2 here and was
                // 2 → 28 before.
                Rectangle {
                    anchors { left: parent.left; right: parent.right }
                    y: BarConfig.atBottom ? 0 : parent.height - height
                    height: Theme.accentHeight
                    color: Theme.magenta
                }

                // ── Left: start button, then virtual desktops ────
                Row {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: 0

                    Launcher {
                        anchors.verticalCenter: parent.verticalCenter
                        // Which monitor's menu a click opens. The bar knows; the
                        // button must not guess.
                        output: bar.outName
                    }

                    Workspaces {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: BarConfig.get(bar.outName, "workspaces")
                    }
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

                    // First, nearest the centre, and on EVERY monitor: it is
                    // an alert, not a readout, and it is invisible unless
                    // something is actually being captured. No BarConfig key
                    // for the same reason GameMode has none — see Recording.qml.
                    Recording {}

                    Tray      { anchors.verticalCenter: parent.verticalCenter
                                visible: BarConfig.get(bar.outName, "tray") }
                    Media     { barVisible: BarConfig.get(bar.outName, "media") }
                    GameMode  {}
                    Battery   {}
                    Volume    { id: volume
                                barVisible: BarConfig.get(bar.outName, "volume") }
                    Cpu       { barVisible: BarConfig.get(bar.outName, "sysinfo") }
                    Memory    { barVisible: BarConfig.get(bar.outName, "sysinfo") }
                    Bluetooth { barVisible: BarConfig.get(bar.outName, "netbt") }
                    Network   { barVisible: BarConfig.get(bar.outName, "netbt") }
                }
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
