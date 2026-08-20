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

    /*
     * This screen's strip palette — the ink, the washes and the background, all
     * resolved against the wallpaper under THIS bar rather than under the desk.
     *
     * A clear bar's ink comes off the wallpaper (Theme.barInks), and a wallpaper
     * is a different picture on every monitor. The desktop-wide fold vetoes when
     * two screens disagree, which is how one letterboxed television put an opaque
     * strip back on all three bars — see Theme.barInks for the measurement.
     */
    readonly property var pal: Theme.barPalette(bar.screen)

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
    // The strip, plus the gap a floating pill lifts off the edge by (0 for every
    // other shape, so this is barHeight unchanged unless the pill is on).
    implicitHeight: Theme.barSpan

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
    //
    // Theme.barSpan and not barHeight: a floating pill reserves the gap it
    // floats by as well, or a maximized window comes up under the gap with its
    // titlebar against the pill's underside. This is also why BarConfig's
    // uifxFile and Theme's chromeFile are blockLoading — the shape, and so this
    // number, is now decided by the corner radius and the chrome, and BOTH have
    // to be known here rather than one configure later. See BarConfig.uifxFile.
    exclusiveZone: bar.autohide ? 0 : Theme.barSpan

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
                 Theme.barSpan + (BarConfig.atBottom ? -content.y : content.y))

    // Where the strip sits inside a window that may be taller than it. A
    // floating pill lifts off the edge it lives on, so the gap is ABOVE it on a
    // top bar and BELOW it on a bottom one; every other shape has no gap and
    // this is 0 both ways.
    readonly property int stripY: BarConfig.atBottom ? 0 : Theme.barGap

    // A floating pill's gap is transparent. Left in the input region it makes
    // the bar swallow clicks in a band along the screen edge and at both ends
    // with nothing drawn there — the invisible click-eater the comment above is
    // about, in its other form. So the region becomes the pill's own rect.
    //
    // NOT WHILE AUTO-HIDING, and that is the same reasoning inverted. There the
    // region is also what the pointer runs into to bring the bar back, and the
    // strip it runs into is at the screen edge — which, for a pill, is inside
    // the gap. Trim that away and a revealed pill has a dead band above it:
    // crossing it drops the hover, the bar hides, the strip re-arms and reveals
    // it again. That is the flapping the HoverHandler below exists to stop, so
    // an auto-hiding pill keeps the full-width region: generous, not wrong.
    readonly property bool maskPill: Theme.barPill && !bar.autohide

    mask: Region {
        x: bar.maskPill ? Theme.barGap : 0
        y: bar.maskPill ? bar.stripY
                        : (BarConfig.atBottom ? Theme.barSpan - bar.liveHeight : 0)
        width:  bar.width - (bar.maskPill ? 2 * Theme.barGap : 0)
        height: bar.maskPill ? Theme.barHeight : bar.liveHeight
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
            height: Theme.barSpan
            // Hides off the edge it lives on: up from the top, down from the
            // bottom. Sliding a bottom bar upwards would take it across the
            // desktop instead of off it.
            //
            // By barSpan, not barHeight: a pill travelling only its own height
            // would stop with the gap's worth of itself still on screen.
            y: bar.revealed ? 0
                            : (BarConfig.atBottom ? Theme.barSpan
                                                  : -Theme.barSpan)
            Behavior on y {
                NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
            }

            // The strip itself. Inset from the window rather than filling it,
            // which is what leaves the pill's gap as desktop; at every other
            // shape barGap is 0 and this is the old anchors.fill exactly.
            Rectangle {
                x: Theme.barGap
                y: bar.stripY
                width: parent.width - 2 * Theme.barGap
                height: Theme.barHeight
                color: bar.pal.bg

                /*
                 * `ends` still touches the screen edge, so only the pair of
                 * corners FACING THE DESKTOP curves. Rounding the pair at the
                 * edge would cut two notches out of the screen's own corners
                 * with the desktop showing through them — the same reason
                 * render.c leaves mission control's full-screen dim square.
                 *
                 * The pill floats free of the edge and keeps all four, which is
                 * what makes it a capsule rather than a tab.
                 *
                 * Per-corner radii are Qt 6.7+; `radius` is the value they fall
                 * back to, so a shape that wants all four sets only that. Four
                 * INDEPENDENT scalar bindings — unlike the anchor pair below,
                 * there is no half-applied state where Qt reinterprets them.
                 */
                radius: Theme.barRadius
                topLeftRadius:     (Theme.barEnds && !BarConfig.atBottom) ? 0 : Theme.barRadius
                topRightRadius:    (Theme.barEnds && !BarConfig.atBottom) ? 0 : Theme.barRadius
                bottomLeftRadius:  (Theme.barEnds &&  BarConfig.atBottom) ? 0 : Theme.barRadius
                bottomRightRadius: (Theme.barEnds &&  BarConfig.atBottom) ? 0 : Theme.barRadius

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
                //
                // INSET TO THE CURVE when the bar is rounded. The rule runs
                // along the edge facing the desktop, which is exactly the edge
                // whose corners curve, so at full width its last few pixels
                // stick out past the background as two little tabs. render.c hit
                // the identical thing on the compositor's panels and solved it
                // by rounding the accent's own top corners; here the strip is a
                // child of a rounded parent with no clip, so the margins are the
                // version of "let the curve eat its ends" that costs nothing.
                // barRadius is 0 at full width, where this is the old rule.
                //
                // Gone entirely on a clear bar. The rule is what gives a strip
                // its edge, and a strip with no background has no edge to give:
                // a 2px line across the screen with nothing above it is not a
                // bar, it is a line. (A single boolean binding, deliberately —
                // never a second ternary on this item, see the anchors above.)
                Rectangle {
                    visible: !bar.pal.clear
                    anchors { left: parent.left; right: parent.right
                              leftMargin: Theme.barRadius
                              rightMargin: Theme.barRadius }
                    y: BarConfig.atBottom ? 0 : parent.height - height
                    height: Theme.accentHeight
                    color: bar.pal.accent
                }

                // ── Left: start button, then virtual desktops ────
                // The margins keep the first and last modules off the curve. A
                // capsule takes a full half-height out of each end, and the
                // start button sat right in it — clipped on a shape with no
                // clip means drawn OVER the corner, hanging off the pill.
                Row {
                    anchors { left: parent.left
                              leftMargin: Theme.barRadius
                              verticalCenter: parent.verticalCenter }
                    spacing: 0

                    Launcher {

                        barScreen: bar.screen
                        anchors.verticalCenter: parent.verticalCenter
                        // Which monitor's menu a click opens. The bar knows; the
                        // button must not guess.
                        output: bar.outName
                    }

                    Workspaces {

                        barScreen: bar.screen
                        anchors.verticalCenter: parent.verticalCenter
                        visible: BarConfig.get(bar.outName, "workspaces")
                    }
                }

                // ── Centre: clock ────────────────────────────────
                Clock {
                    barScreen: bar.screen
                    anchors.centerIn: parent
                    visible: BarConfig.get(bar.outName, "clock")
                }

                // ── Right: status modules ────────────────────────
                Row {
                    anchors {
                        right: parent.right
                        // 6 is the square bar's breathing room and stays the
                        // floor; a curve wider than that pushes the tray in
                        // past it. Not additive — 6 + 14 would leave the right
                        // side visibly slacker than the left at every radius.
                        rightMargin: Math.max(6, Theme.barRadius)
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: Theme.moduleGap

                    // First, nearest the centre, and on EVERY monitor: it is
                    // an alert, not a readout, and it is invisible unless
                    // something is actually being captured. No BarConfig key
                    // for the same reason GameMode has none — see Recording.qml.
                    Recording { barScreen: bar.screen }

                    Tray      { barScreen: bar.screen
                                anchors.verticalCenter: parent.verticalCenter
                                visible: BarConfig.get(bar.outName, "tray") }
                    Media     { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "media") }
                    GameMode  { barScreen: bar.screen }
                    Battery   { barScreen: bar.screen }
                    Volume    { id: volume
                                barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "volume") }
                    Cpu       { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "sysinfo") }
                    Memory    { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "sysinfo") }
                    Bluetooth { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "netbt") }
                    Network   { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "netbt") }
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
