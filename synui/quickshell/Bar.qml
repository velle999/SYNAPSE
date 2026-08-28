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

    /*
     * ── How the compositor knows this is the bar ─────────────────────────────
     *
     * synui keys the backdrop blur off the layer surface's namespace: the shell
     * marks the surfaces it wants frosted "synui-glass", and until the glass
     * presets stopped asking for a CLEAR bar the strip deliberately kept the
     * plain one. It could not join that set, because blur is masked by what the
     * client actually painted and a bar with no background paints nothing but
     * glyphs — which would have put a little frosted halo behind each letter of
     * the clock instead of a sheet behind the strip.
     *
     * ⚠ SO THE BAR HAS ITS OWN NAMESPACE RATHER THAN THE GLASS ONE, and the
     * decision moves to the compositor, which is the only side that can make it:
     * layer.c's layer_wants_glass() asks syn_bar_has_background(), so the frost
     * appears and disappears with bar_opacity while this string never changes.
     * A namespace is fixed at map time; a bar's opacity is not.
     *
     * It also has to keep reading as one of the shell's own surfaces, because
     * the bar's right-click menu and the mixer are xdg_popups parented to it and
     * inherit their glass through layer_is_shell(). Both spellings live in
     * layer.c; changing this means changing SYN_BAR_NAMESPACE there.
     */
    WlrLayershell.namespace: "synui-bar"

    readonly property string outName: modelData.name

    /*
     * ── The half of the plugin contract the HOST owes ───────────────────────
     *
     * BarWidget promises a widget three things about the bar it is in (Ui/
     * BarWidget.qml). Two are facts and the third is a service, and without
     * them the base type's guards would simply hand every plugin its fallback
     * for ever — a contract that compiles and is not kept.
     *
     * `vertical` is false and stated rather than omitted: synui's bar is a
     * horizontal strip and has no column form (see the note on bar_edge in
     * config.c, which offers top and bottom only where the dock offers four).
     * A widget branching on it gets the right answer today and will get the
     * right answer if that ever changes.
     */
    readonly property bool vertical: false
    readonly property int  barSize: bar.height

    /* Which edge this strip is on, spelt the way a plugin's panel expects —
     * "top" | "bottom" | "left" | "right". A KeyboardPanel reads it to decide
     * which side of the bar to open on, and a panel handed nothing would open
     * downwards off the bottom of a bottom bar. synui offers two of the four;
     * the other two are answered honestly by never being returned. */
    readonly property string position: BarConfig.atBottom ? "bottom" : "top"

    /* The rest of what an interactive plugin widget reads off its host
     * (Ui/WidgetButton.qml). Every one of these is guarded on the widget's side,
     * so a bar that offered none of them would still work — they are here so a
     * plugin picks up THIS desktop's font and ink rather than falling back to
     * the shim's desktop-wide answer, which cannot know which monitor it is on.
     *
     * ⚠ bar.pal IS PER-STRIP AND PER-MODULE, which is the whole reason to hand
     * it over rather than let the widget ask Color: a clear bar takes its ink
     * off the wallpaper under THIS bar, and the singleton has one value for the
     * whole desktop. */
    readonly property string fontFamily:    Theme.fontFamily
    readonly property color  urgent:        Theme.red

    /*
     * ⚠ TWO NAMES, ONE VALUE, AND ONLY ONE OF THEM IS OURS TO CHOOSE.
     * `foreground` is what Omarchy's bar is called by every widget written
     * against it — snake, tetris, calendar and flappy all read
     * `bar ? bar.foreground : Color.foreground` — and this bar answered only to
     * `barForeground`. The ternary took its fallback branch, `Color.foreground`
     * is a Theme colour that resolves fine, and nothing looked wrong…
     *
     * ⛔ …EXCEPT IN THE PANELS, WHERE IT WAS NOT A TERNARY. tetris and calendar
     * read `bar.foreground` with `bar` non-null, got undefined, and the log on
     * tty1 filled with "Unable to assign [undefined] to QColor" while their
     * panels drew text in the default colour. That is the whole failure: an
     * assignment of undefined to a colour is a warning nobody sees and a
     * component that keeps running.
     *
     * `barForeground` is derived rather than duplicated, so there is one
     * expression to be wrong: two `bar.pal.fg` bindings would be two owners of
     * one value, and the second to be edited would silently disagree.
     */
    readonly property color  foreground:    bar.pal.fg
    readonly property color  barForeground: bar.foreground

    /* The strip's own backdrop, which a plugin's panel uses to sit its controls
     * on something that matches the bar they were summoned from — tetris's four
     * dropdowns read it three times each. Same undefined-to-QColor silence as
     * `foreground` had, in the same panel. `pal.bg` and not Theme.bg because a
     * clear bar's backdrop is a scrim over THIS screen's wallpaper. */
    readonly property color  background:    bar.pal.bg

    /*
     * ── The rest of the host, which is NOT the bar ──────────────────────────
     *
     * `shell` is the session — the panels and services this bar's widgets ask
     * to open, which are mounted once for the desktop and not once per strip.
     * A widget reaches it as `bar.shell` because that is where Omarchy puts it;
     * what it points at is PluginHost, and the reasons live there.
     *
     * ⚠ IT WAS UNDEFINED, AND EVERY CALLER GUARDS. flappy's click is
     * `if (bar.shell && typeof bar.shell.toggle === "function")` — careful code
     * whose reward on a host missing the member was a button that did nothing
     * and said nothing. Two of the five installed plugins failed exactly that
     * way.
     */
    readonly property var shell: PluginHost

    /* Launch a command line. Omarchy's `bar.run`, and the one call of the three
     * that was loud when it was missing — tetris's Start threw a TypeError
     * rather than failing a guard. PluginHost owns the launching; this is the
     * name a widget knows it by. */
    function run(cmd) { PluginHost.run(cmd) }

    /*
     * Every live instance of one plugin, across every monitor.
     *
     * ⚠ THIS IS WHAT MAKES broadcast() MEAN ANYTHING. A bar surface exists per
     * screen, so a widget that has just learned something — a click, a file
     * change — has peers on the other monitors still holding the old answer.
     * Omarchy's base falls back to refreshing itself alone when the host cannot
     * enumerate, which is honest and is also a widget that updates on one
     * screen and not the others.
     *
     * ⚠ THROUGH THE SINGLETON, because a Bar cannot see its siblings: the
     * Variants that creates them is in shell.qml, so there is no id to reach
     * and no parent to walk. Plugins is where they all meet, for the same
     * reason Theme and BarConfig are singletons.
     */
    function moduleWidgets(name) { return Plugins.widgetsFor(name) }

    /* This bar's own instances of one plugin — what the singleton folds. */
    function pluginWidgets(name) {
        const out = []
        for (let i = 0; i < pluginRow.count; i++) {
            const slot = pluginRow.itemAt(i)
            if (slot && slot.item && slot.pid === name) out.push(slot.item)
        }
        return out
    }

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
    /*
     * ⚠ ONE Component.onCompleted, AND THAT IS NOT A STYLE PREFERENCE. QML
     * refuses a property assigned twice — "Property value set multiple times" —
     * and it refuses it by failing to load the WHOLE type, so a second handler
     * here does not lose its own line, it takes the entire bar off the screen.
     *
     * The plugin registration below arrived as its own `Component.onCompleted`
     * and did exactly that: `Type Bar unavailable`, no bar, no start menu, from
     * a package that built and installed cleanly. bar_shape.sh caught it by
     * loading the real tree; nothing else in the suite loads QML at all.
     */
    Component.onCompleted: {
        bar.revealed = bar.wantsReveal
        /* Register with the singleton so a plugin on THIS bar can reach its
         * instances on the others — a Bar cannot see its siblings, the Variants
         * that makes them is in shell.qml. */
        Plugins.registerBar(bar)
    }
    /* Unregistered when the surface goes: a monitor unplugged must not leave a
     * dead bar in the list for broadcast() to walk. */
    Component.onDestruction: Plugins.unregisterBar(bar)

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

    /*
     * Is there a bar on this desktop at all — Control panel ▸ Desktop ▸ Bar.
     *
     * UNMAPPING THE WINDOW is the whole implementation, and it is why the switch
     * no longer kills anything. This process is the bar AND the desktop widgets,
     * the OSD, the start menu, the mixer and the notes (shell.qml); the old
     * `bar_stop_cmd = pkill -x quickshell` could only turn the bar off by taking
     * all of them with it. Here the layer surface is destroyed and the rest of
     * the shell never notices.
     *
     * Destroying it is also what gives the exclusive zone back: the reservation
     * belongs to the surface, so a bar switched off stops holding a strip of the
     * screen and maximized windows grow into it. Coming back builds a NEW
     * surface, which is the only moment set_exclusive_zone is honoured (see
     * below) — and since BarConfig reads settings.state with blockLoading, the
     * autohide answer is already there when that happens.
     */
    visible: BarConfig.barEnabled

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
                    /*
                     * ── Plugin widgets ───────────────────────────────
                     *
                     * Third-party bar widgets, in Omarchy's shell-plugin
                     * format (see synui-plugins). First in the right-hand run,
                     * nearest the centre, so a plugin never displaces the
                     * modules somebody relies on being at the end of the bar —
                     * the clock, the tray and the volume keep their place
                     * whatever is installed.
                     *
                     * ⚠ EACH ONE IN A Loader, AND THAT IS THE POINT. A plugin
                     * is somebody else's QML in this process: a syntax error, a
                     * missing import or a type that does not resolve must cost
                     * that one widget and not the bar. A Loader isolates the
                     * failure and reports it as `status`, where instantiating
                     * the component inline would take the whole surface down
                     * with it — and the bar is the thing you would use to fix
                     * it.
                     *
                     * Plugins.activeModel is already filtered to enabled AND
                     * hostable, so nothing here has to re-ask.
                     *
                     * ⚠ A ListModel OF IDS, NOT THE `active` ARRAY, and the
                     * difference is whether a reorder RELOADS every plugin
                     * here or moves one item. See Plugins.syncModel for the
                     * whole argument. What it costs on this side is one
                     * indirection: the row is looked up by id rather than
                     * handed over, as a BINDING so a rescan that changes a
                     * plugin's directory still reaches its slot.
                     */
                    Repeater {
                        id: pluginRow
                        model: Plugins.activeModel

                        delegate: Loader {
                            id: pluginSlot
                            required property string pid
                            readonly property var row: Plugins.rowFor(pluginSlot.pid)

                            anchors.verticalCenter: parent ? parent.verticalCenter
                                                           : undefined
                            asynchronous: true
                            source: Plugins.entryUrl(pluginSlot.row)

                            /* A widget that failed to load takes no width: a
                             * broken plugin leaving a gap in the bar would read
                             * as a bar layout bug rather than as one plugin
                             * being wrong. `synui-plugins list` is where the
                             * reason lives. */
                            visible: pluginSlot.status === Loader.Ready
                            width: visible && item ? item.implicitWidth : 0

                            onLoaded: {
                                /*
                                 * The two the host injects. Assigned and not
                                 * bound: a plugin is free to write to its own
                                 * properties, and a binding would fight it and
                                 * win — which reads as the widget's own code
                                 * being ignored.
                                 *
                                 * ⚠ `settings` NOW COMES FROM A FILE, WHICH
                                 * THIS NOTE USED TO SAY IT DELIBERATELY DID
                                 * NOT. It was left at its empty default on the
                                 * grounds that Omarchy fills it from shell.json,
                                 * synui has no equivalent, and inventing one
                                 * would be a second settings format for a
                                 * feature nothing had asked for. Three
                                 * installed plugins then asked at once —
                                 * flappy's best score, chess's `defaults`,
                                 * tetris's sound and theme — so PluginConfig is
                                 * that file, and its header carries the
                                 * argument. An absent key still returns the
                                 * widget's own fallback, which is what the
                                 * contract says it should.
                                 */
                                pluginSlot.item.bar = bar
                                pluginSlot.item.moduleName = pluginSlot.pid
                                pluginSlot.item.settings =
                                    PluginConfig.settingsFor(pluginSlot.pid)
                            }

                            onStatusChanged: {
                                if (pluginSlot.status === Loader.Error)
                                    console.warn("synui-bar: plugin",
                                                 pluginSlot.pid,
                                                 "failed to load from",
                                                 pluginSlot.source)
                            }

                            /*
                             * ⚠ ASSIGNED SETTINGS GO STALE, and this is what
                             * stops them. The assignment above happens once, at
                             * load — it has to, for the reason given there —
                             * and a plugin's own panel writes settings back
                             * while both are up: flappy's best score is written
                             * by the panel and read by the widget in the bar. A
                             * widget still holding the value it was handed at
                             * startup would show the previous record for the
                             * rest of the session.
                             *
                             * Re-assigned rather than bound, so a plugin that
                             * writes its own `settings` is not fought over the
                             * frames in between.
                             */
                            Connections {
                                target: PluginConfig
                                function onRevisionChanged() {
                                    if (!pluginSlot.item) return
                                    pluginSlot.item.settings =
                                        PluginConfig.settingsFor(pluginSlot.pid)
                                }
                            }
                        }
                    }

                    Recording { barScreen: bar.screen }

                    // Immediately left of the tray, which is where an
                    // indicator of this kind is looked for — and it is hidden
                    // whenever there is nothing pending, so on a current
                    // machine the tray is still the leftmost thing here.
                    Updates   { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "updates") }

                    /* Left of the update badge and the tray, with the rest of
                     * the readouts rather than with the indicators: a
                     * temperature is a number you read, not a thing that wants
                     * you. Like Updates it is invisible when there is nothing
                     * to say, so a bar with the weather off is unchanged.
                     *
                     * ⚠ This is modules/Weather.qml. There is a
                     * widgets/Weather.qml too — the desktop card — and the two
                     * are only unambiguous because this file imports "modules"
                     * and shell.qml imports "widgets", neither importing both.
                     */
                    Weather   { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "weather") }

                    /* The assistant, left of the tray and the readouts: it
                     * is a thing you press rather than a thing you read, and
                     * the two kinds do not interleave well. It hides itself
                     * where vibe is not installed. */
                    Assistant { barScreen: bar.screen
                                barVisible: BarConfig.get(bar.outName, "assistant") }

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
