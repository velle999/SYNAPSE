import QtQuick
import QtQuick.Shapes
import Quickshell
import Quickshell.Wayland
import ".."

/*
 * WidgetFrame — the panel every desktop widget is drawn in, and the thing that
 * lets you drag it somewhere else.
 *
 * Before this existed each widget was its own PanelWindow with its own copy of
 * the anchors, the margins, the rounded rectangle and the magenta hairline, and
 * "make them draggable" would have meant five copies of the drag as well. The
 * six widgets now describe only what they are — a size, a home corner, a name,
 * some content — and everything about being a widget is here.
 *
 *
 * WHY A DRAG EXPANDS THE SURFACE
 *
 * The obvious way to move a layer-shell widget is to add the pointer's delta to
 * its margins as it moves. That is broken, and subtly enough to look like it
 * works: the pointer position in every motion event is relative to the SURFACE,
 * and the surface is the thing being moved. Push the margin by 10 and the
 * compositor sends a motion event saying the pointer is 10px back the way it
 * came — which is true, and indistinguishable from the user actually moving it
 * back. Accumulate those and the widget crawls away from the pointer or snaps
 * home, depending on which way you resolve it, and it does so at whatever rate
 * motion events outrun frames.
 *
 * So the surface does not move during a drag. On press it is re-anchored to all
 * four edges — the usable area, since exclusiveZone stays 0, which is exactly
 * the space the margins are measured in — and the CARD moves inside it as an
 * ordinary Item under Qt's own drag.target. Qt's drag already accounts for a
 * target that moves under the pointer; the surface underneath it is nailed
 * down; the arithmetic is exact and there is none of it here. On release the
 * card's position becomes margins again and the surface shrinks back.
 *
 * The cost is one full-usable-area buffer for the length of the gesture, for
 * one widget. That is why the expansion is per-drag and not simply how these
 * windows are built: five permanently fullscreen transparent surfaces is over
 * 150MB of VRAM on a 4K desktop to hold five small cards.
 *
 *
 * WHY THE GRIP IS THE ONLY THING YOU CAN CLICK
 *
 * `mask: Region { item: grip }`. Widgets that report something — the monitor,
 * the clock, the visualiser — must let clicks through to the desktop beneath
 * them, or they are invisible rectangles that eat the right-click menu; that
 * rule is why they carried an empty mask before, and dragging cannot be allowed
 * to cost it. The grip is 18px, and it is the whole of what those widgets take.
 * PostIt and QuickLaunch set `interactive` and take the card, because they are
 * things to click and already paid that price on purpose.
 *
 *
 * WHY THERE IS NO QtQuick.Effects HERE
 *
 * The shadow and the neon bloom are both stacks of the same outline, stroked
 * wider and fainter. A MultiEffect would be the obvious way to get a real blur,
 * and it costs a layer — an offscreen texture — per widget, which means every
 * label on the desktop is text resampled from a texture instead of text. These
 * are static geometries the renderer batches and never touches again, and at
 * this size the difference between a stacked stroke and a gaussian is not one
 * anybody can see on a wallpaper.
 */
PanelWindow {
    id: win

    // Widget content goes in the body, below the header rule. Everything this
    // file declares for itself is assigned to `data` explicitly further down,
    // because redefining the default property redefines it for THIS file too —
    // an anonymous child here would become a child of the body it is drawn in.
    default property alias content: body.data

    required property var modelData

    // Which screen this instance belongs to. Normally the model entry itself,
    // because these are instantiated one per screen — PostIt overrides it,
    // because its model has one entry per NOTE per screen and the screen is a
    // field of it.
    property var screenData: modelData
    screen: screenData

    // ── What a widget declares ───────────────────────────
    property string widgetId: ""
    property bool   shown:    false
    property string label:    ""
    property color  accent:   Theme.magenta

    property int  cardWidth:  200

    // A widget states how tall its CONTENT is and the frame adds its own
    // chrome, so nobody has to keep a hand-added constant in step with the
    // header. A widget with a fixed size sets cardHeight over the top instead.
    property int  bodyHeight: 60
    property int  cardHeight: bodyHeight + chromeTop + chromeBottom

    readonly property bool hasHeader: chrome && label !== ""
    readonly property int  chromeTop:    hasHeader ? 32 : (chrome ? 12 : 0)
    readonly property int  chromeBottom: chrome ? 11 : 0

    // Home corner: where this widget sits until somebody drags it. Kept as an
    // edge and a margin rather than a coordinate so the designed layout still
    // means the same thing on any screen.
    property string homeEdgeH:   "left"
    property string homeEdgeV:   "top"
    property int    homeMarginX: 20
    property int    homeMarginY: 20

    // The visualiser spans the screen; there is nowhere to drag it sideways and
    // no card to put a shadow under.
    property bool fillWidth: false
    property bool chrome:    true
    property bool dragX:     true
    property bool dragY:     true

    /* ── Glass ───────────────────────────────────────────────────────────────
     *
     * The other card these can be drawn as: a rounded translucent pane that
     * matches the dock, for a desktop whose chrome is glass (macOS 26) or whose
     * owner has asked for it. Theme.widgetsGlass is the whole decision — see
     * there and BarConfig for where the three positions come from.
     *
     * A MODE and not a replacement, deliberately. The chamfered HUD panel with
     * its scanlines and corner ticks is what SYNAPSE looks like, and a Gruvbox
     * or Win95 desktop wearing frosted Mac cards would be no better matched than
     * a Tahoe one wearing the HUD. So both exist and the theme picks.
     *
     * `chrome` still gates it: the visualiser has no card, so it has no card to
     * make glass either.
     *
     * THE BLUR IS REAL, AND IT IS THE COMPOSITOR'S
     *
     * It did not use to be: this said "nothing here can ask for one", because
     * synui applied no effects to layer surfaces at all and what a glass widget
     * drew was the rest of the recipe without the frosting. Since -379 the
     * surface asks by name (WlrLayershell.namespace below) and layer.c puts a
     * scenefx blur node behind it, the same one the dock has. What this draws is
     * the falloff, the specular hairline and the rim ON TOP of that.
     *
     * Theme.widgetAlpha still carries the lift it was given for the unblurred
     * case. Deliberately left alone here: it is the dock's own opacity plus a
     * constant, so moving it is a change to how the widgets and the dock relate
     * to each other, and that is a separate decision from this one.
     */
    readonly property bool glass: win.chrome && Theme.widgetsGlass

    // Follows the desktop's radius where there is one, with a floor: at 4px a
    // "rounded" glass card just looks like a rectangle that failed.
    readonly property int glassRadius: Theme.panelRadius > 0
                                       ? Math.max(12, Theme.panelRadius) : 14

    // base lifted `t` of the way to white, at alpha `a`. The gradient, the
    // specular and the rim are all this function with different numbers, so
    // there is one place the surface's colour is decided.
    function lift(base, t, a) {
        return Qt.rgba(base.r + (1 - base.r) * t,
                       base.g + (1 - base.g) * t,
                       base.b + (1 - base.b) * t, a)
    }

    // Whether the body takes pointer input. False is the default because a
    // widget that takes clicks is a widget the desktop menu cannot be opened
    // through.
    property bool interactive: false

    // ── Position ─────────────────────────────────────────
    // A stored position wins over the home one, and only for a widget that has
    // one — so a widget nobody has touched keeps following its designed corner,
    // including any later change to where that corner is.
    readonly property bool placed: WidgetLayout.has(widgetId)
    readonly property string edgeH: WidgetLayout.edgeH(widgetId, homeEdgeH)
    readonly property string edgeV: WidgetLayout.edgeV(widgetId, homeEdgeV)
    readonly property int    mx:    WidgetLayout.x(widgetId, homeMarginX)
    readonly property int    my:    WidgetLayout.y(widgetId, homeMarginY)

    /* ── Does this card cast its own shadow? ─────────────────────────────────
     *
     * ⚠ NOT WHILE THE COMPOSITOR IS FROSTING BEHIND IT, AND THE REASON IS THE
     * SHAPE OF THE BLUR'S MASK RATHER THAN TASTE.
     *
     * layer.c limits the backdrop blur to where this client actually paints, so
     * that a full-screen surface with a small card in it frosts the card and not
     * the screen. That mask is a STENCIL, and the test it is built with is
     * `discard_transparent && gl_FragColor.a == 0.0` (scenefx tex.frag) — so the
     * stencil keeps every pixel whose alpha is merely NON-ZERO, and the blur
     * inside it is drawn at full strength.
     *
     * A drop shadow is faint, not transparent. These rings are black at 0.045 to
     * 0.09 and spread nine pixels past the card, so on a frosted desktop they
     * stencil in a nine-pixel band that the compositor then fills with the same
     * blur that is under the card. What was a shadow you had to look for became
     * a wide frosted halo around every widget — and nothing on the control panel
     * moves it, because the shadow rows there are uifx.c's and reach window
     * frames only. These rings are literals in this file.
     *
     * So the two mechanisms do not stack: where there is a blur, the blur IS the
     * lift, and the card carries the rim and the specular and nothing else.
     *
     * Keyed on Theme.glassSurfaces — is the compositor frosting — and NOT on
     * `win.glass`, which is whether this card PAINTS itself as glass. They come
     * apart: `widget_glass = off` on a glass desktop draws the chamfered HUD, and
     * its shadow strokes are twenty pixels wide, so that combination is the worst
     * halo of the two rather than an exception to it. The namespace is claimed
     * unconditionally below, so the frosting is not the card's to opt out of.
     */
    readonly property bool ownShadow: !Theme.glassSurfaces

    // Room around the card for that shadow to spread into. The card's visible
    // edge still sits `mx` from the screen edge, so the pad is invisible in the
    // layout and the home margins mean what they say. No shadow, no pad — which
    // is also what shrinks the frosted patch back to the card itself, since the
    // surface is what the blur node is sized from.
    readonly property int pad: (chrome && win.ownShadow) ? 16 : 0

    property bool dragging: false

    // The surface grows on press, but not instantly — the layer surface has to
    // be reconfigured and that is a round trip. Until it lands the card stays
    // where it was rather than jumping to the corner of a window that is still
    // card-sized.
    readonly property bool expanded: dragging && height > cardHeight + pad * 2 + 8

    visible: shown && screenData && screenData.name === WidgetState.primaryOutput

    WlrLayershell.layer: WlrLayer.Bottom

    // Frosted on a glass theme — see StartMenu.qml. `widget_glass` already
    // decides whether the CARD paints itself as glass; this is the other half,
    // the blur behind it, which no client can do for itself.
    WlrLayershell.namespace: "synui-glass"

    anchors {
        left:   win.dragging || win.fillWidth || win.edgeH === "left"
        right:  win.dragging || win.fillWidth || win.edgeH === "right"
        top:    win.dragging || win.edgeV === "top"
        bottom: win.dragging || win.edgeV === "bottom"
    }

    margins {
        left:   (win.dragging || win.fillWidth) ? 0 : Math.max(0, win.mx - win.pad)
        right:  (win.dragging || win.fillWidth) ? 0 : Math.max(0, win.mx - win.pad)
        top:    win.dragging ? 0 : Math.max(0, win.my - win.pad)
        bottom: win.dragging ? 0 : Math.max(0, win.my - win.pad)
    }

    // Only for the shuffle a widget does when the visualiser appears under it —
    // and only while it is at home, since a widget that was put somewhere on
    // purpose should stay there. Never while dragging: an animated margin
    // fighting a pointer is the one case this must not smooth over.
    Behavior on margins.bottom {
        enabled: !win.dragging && !win.placed
        NumberAnimation { duration: Theme.animNormal }
    }
    Behavior on margins.top {
        enabled: !win.dragging && !win.placed
        NumberAnimation { duration: Theme.animNormal }
    }

    implicitWidth:  cardWidth + pad * 2
    implicitHeight: cardHeight + pad * 2

    exclusiveZone: 0
    // Never focusable. layer.c hands a layer surface the keyboard at map time
    // and nowhere else, so this could not become true usefully anyway — but
    // more to the point, focusable means EXCLUSIVE here, and these are up for
    // as long as the desktop is. See PostIt.qml.
    focusable: false
    color: "transparent"

    // The grip is the whole input surface of a reporting widget. An interactive
    // one takes its card, which it was already doing.
    mask: Region { item: win.interactive ? card : grip }

    data: [
        /* ── The card ──────────────────────────────────────────────────────
         *
         * x and y are set imperatively rather than bound, because Qt's drag
         * writes them directly and would break a binding the first time
         * anybody moved anything — leaving the widget stuck wherever it was
         * dropped even after the window went back to being anchored.
         */
        Item {
            id: card

            x: win.pad
            y: win.pad
            width:  win.fillWidth ? win.width - win.pad * 2 : win.cardWidth
            height: win.cardHeight

            /* ── Frame: glass ────────────────────────────
             *
             * Declared before the HUD frame so that when both are somehow
             * visible the HUD wins, which is the safer way round: the HUD is
             * opaque and legible on anything.
             */
            Item {
                id: glassFrame
                anchors.fill: parent
                visible: win.glass

                // Shadow, spread OUTSIDE the card rather than under it — a
                // translucent pane with a shadow inside its own edge looks
                // dirty rather than lifted. Negative margins grow each ring
                // past the card; the radius grows with them so the corners stay
                // concentric instead of tightening as the rings widen.
                Item {
                    id: glassShadow
                    anchors.fill: parent
                    visible: win.ownShadow

                    // The drop, as a plain property with the Behavior on IT
                    // rather than on each ring's anchors.topMargin: a Behavior
                    // on a member of a grouped property is not something to
                    // rely on, and one animated number driving three bindings
                    // is what keeps the rings moving together anyway.
                    property real drop: win.dragging ? 7 : 3
                    Behavior on drop { NumberAnimation { duration: Theme.animNormal } }

                    Repeater {
                        model: [ { m: 9, a: 0.045 }, { m: 5, a: 0.065 }, { m: 2, a: 0.09 } ]
                        delegate: Rectangle {
                            required property var modelData
                            anchors.fill: parent
                            anchors.margins: -modelData.m
                            anchors.topMargin: -modelData.m + glassShadow.drop
                            radius: win.glassRadius + modelData.m
                            color: Qt.rgba(0, 0, 0,
                                           modelData.a * (win.dragging ? 1.6 : 1))
                        }
                    }
                }

                Rectangle {
                    id: pane
                    anchors.fill: parent
                    radius: win.glassRadius

                    // Thinnest at the top, where the light is: the falloff is
                    // what stops a flat alpha reading as a sheet of plastic.
                    // Same rule the dock's body follows (dock_paint_body).
                    gradient: Gradient {
                        GradientStop {
                            position: 0.0
                            color: win.lift(Theme.popupBg, Theme.isLight ? 0.35 : 0.13,
                                            Theme.widgetAlpha * 0.88)
                        }
                        GradientStop {
                            position: 1.0
                            color: win.lift(Theme.popupBg, 0.0, Theme.widgetAlpha)
                        }
                    }

                    // The rim takes whichever of black/white shows on this
                    // scheme's surface — a white hairline is invisible on
                    // Tahoe's near-white pane and a black one on SYNAPSE's
                    // navy. It goes yellow while dragging, which is the same
                    // signal the HUD outline gives.
                    border.width: 1
                    border.color: win.dragging ? Theme.yellow
                                : Theme.isLight ? Qt.rgba(0, 0, 0, 0.14)
                                                : Qt.rgba(1, 1, 1, 0.20)
                    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
                }

                // The specular. Inset past the corner arcs for the reason the
                // dock's is: carried into them it would just be a second rim,
                // and a highlight is where the light hits and nowhere else.
                Rectangle {
                    anchors {
                        left: parent.left; right: parent.right; top: parent.top
                        leftMargin: win.glassRadius; rightMargin: win.glassRadius
                        topMargin: 1
                    }
                    height: 1
                    color: Qt.rgba(1, 1, 1, Theme.isLight ? 0.80 : 0.28)
                }
            }

            // ── Frame ────────────────────────────────────
            Item {
                id: frame
                anchors.fill: parent
                visible: win.chrome && !win.glass

                LinearGradient {
                    id: panelFill
                    x1: 0; y1: 0
                    x2: 0; y2: card.height
                    // Top-lit rather than flat: the surface these sit on is a
                    // wallpaper, and a panel with no falloff at all looks
                    // pasted onto it.
                    GradientStop { position: 0.0;  color: Qt.lighter(Theme.popupBg, 1.35) }
                    GradientStop { position: 0.55; color: Theme.popupBg }
                    GradientStop { position: 1.0;  color: Qt.darker(Theme.popupBg, 1.18) }
                }

                // Shadow. Offset down so what survives being covered by the
                // panel is a spread below and to the sides, which is what a
                // shadow is; a centred halo would just look like a second glow.
                Item {
                    anchors.fill: parent
                    visible: win.ownShadow
                    y: win.dragging ? 9 : 4
                    Behavior on y { NumberAnimation { duration: Theme.animNormal } }
                    Repeater {
                        model: [ { w: 20, a: 0.045 }, { w: 12, a: 0.07 }, { w: 6, a: 0.10 } ]
                        delegate: Chamfered {
                            required property var modelData
                            line: Qt.rgba(0, 0, 0, modelData.a * (win.dragging ? 1.5 : 1))
                            lineWidth: modelData.w
                        }
                    }
                }

                // Two fainter, wider strokes of the same outline under the
                // crisp one. A neon edge is a bloom, not a line.
                Repeater {
                    model: [ { w: 9, a: 0.055 }, { w: 4, a: 0.10 } ]
                    delegate: Chamfered {
                        required property var modelData
                        line: Qt.rgba(win.accent.r, win.accent.g, win.accent.b,
                                      modelData.a * (win.dragging ? 2.4 : 1))
                        lineWidth: modelData.w
                        Behavior on line { ColorAnimation { duration: Theme.animFast } }
                    }
                }

                Chamfered {
                    id: outline
                    grad: panelFill
                    line: win.dragging ? Theme.yellow : win.accent
                    lineWidth: 1
                    Behavior on line { ColorAnimation { duration: Theme.animFast } }
                }

                // Scanlines. Inset by the chamfer so none of them show in the
                // cut corners, where there is no panel behind them to be a
                // texture on.
                Item {
                    anchors { fill: parent; margins: outline.cut }
                    clip: true
                    Repeater {
                        model: Math.ceil(card.height / 3)
                        delegate: Rectangle {
                            required property int index
                            y: index * 3
                            width: parent.width
                            height: 1
                            color: Theme.fg
                            opacity: 0.035
                        }
                    }
                }

                // Ticks in the two square corners. Reads as a bracketed HUD
                // panel rather than a box with a corner missing.
                Repeater {
                    model: [
                        { x: 0,   y: 0,   w: 11, h: 2 }, { x: 0,   y: 0,   w: 2,  h: 11 },
                        { x: -11, y: -2,  w: 11, h: 2 }, { x: -2,  y: -11, w: 2,  h: 11 }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData.x < 0 ? card.width + modelData.x : modelData.x
                        y: modelData.y < 0 ? card.height + modelData.y : modelData.y
                        width:  modelData.w
                        height: modelData.h
                        color: win.accent
                        opacity: 0.9
                    }
                }
            }

            // ── Header ───────────────────────────────────
            Item {
                id: header
                visible: win.hasHeader
                anchors {
                    top: parent.top; left: parent.left; right: parent.right
                    topMargin: 9; leftMargin: 11; rightMargin: 9
                }
                height: visible ? 15 : 0

                Rectangle {
                    id: tag
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: 3; height: 9
                    color: win.accent
                }

                Text {
                    anchors { left: tag.right; leftMargin: 6; verticalCenter: parent.verticalCenter }
                    text: win.label
                    color: win.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.letterSpacing: 1.6
                }

                // Fades out towards the grip so the rule reads as an edge of
                // the header rather than a line boxing it in.
                Rectangle {
                    anchors {
                        left: parent.left; right: parent.right
                        bottom: parent.bottom; rightMargin: 26
                    }
                    height: 1
                    opacity: 0.25
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0;  color: "transparent" }
                        GradientStop { position: 0.6;  color: "transparent" }
                        GradientStop { position: 1.0;  color: win.accent }
                    }
                }
            }

            // ── Grip ─────────────────────────────────────
            DragGrip {
                id: grip
                frame: win
                // Above the body, which is declared after it and would
                // otherwise take the press — an interactive widget fills its
                // body with a MouseArea, and the grip has to win.
                z: 5
                anchors {
                    top: parent.top; right: parent.right
                    topMargin: win.chrome ? 5 : 4
                    rightMargin: win.chrome ? 6 : 12
                }
            }

            // ── Body ─────────────────────────────────────
            // Anchored to the card and inset by the frame's own metrics rather
            // than to the header, so the number a widget adds to bodyHeight and
            // the space it actually gets are the same number.
            Item {
                id: body
                anchors {
                    top: parent.top; left: parent.left
                    right: parent.right; bottom: parent.bottom
                    topMargin: win.chromeTop
                    bottomMargin: win.chromeBottom
                    leftMargin: win.chrome ? 11 : 0
                    rightMargin: win.chrome ? 11 : 0
                }
            }
        },

        /* ── Drag guides ────────────────────────────────────────────────────
         *
         * Only exist while the surface is expanded, which is the only time
         * there is anything to draw them on. Four hairlines along the card's
         * edges, so lining a widget up with another one is a thing you can
         * actually do rather than a thing you eyeball.
         */
        Repeater {
            model: win.expanded ? 4 : 0
            delegate: Rectangle {
                required property int index
                readonly property bool vertical: index < 2
                color: Theme.yellow
                opacity: 0.22
                width:  vertical ? 1 : win.width
                height: vertical ? win.height : 1
                x: vertical ? (index === 0 ? card.x : card.x + card.width - 1) : 0
                y: vertical ? 0 : (index === 2 ? card.y : card.y + card.height - 1)
            }
        },

        // The card slides into its snapped position while the surface is still
        // big enough to show it doing so, and only then is the position
        // committed and the surface shrunk. Committing first would collapse the
        // window mid-slide and the snap would never be seen.
        ParallelAnimation {
            id: settle
            NumberAnimation {
                id: settleX
                target: card; property: "x"
                duration: Theme.animNormal; easing.type: Easing.OutCubic
            }
            NumberAnimation {
                id: settleY
                target: card; property: "y"
                duration: Theme.animNormal; easing.type: Easing.OutCubic
            }
            onFinished: win.commitDrag()
        }
    ]

    /* ── Drag ──────────────────────────────────────────────────────────────
     *
     * The window is re-anchored on press and the card is handed absolute
     * coordinates. `expanded` is what says the surface has actually grown; the
     * card is only moved into the big window's coordinate space once it has,
     * because before that there is no big window to put it in.
     */
    onExpandedChanged: {
        if (win.expanded) {
            card.x = win.fillWidth ? win.pad
                   : win.edgeH === "left" ? win.mx
                                          : win.width - card.width - win.mx
            card.y = win.edgeV === "top" ? win.my
                                         : win.height - card.height - win.my
        } else {
            card.x = win.pad
            card.y = win.pad
        }
    }

    // Edges first, then the centre. Snapping to a centre that happens to sit a
    // few pixels from an edge snap would make the edge unreachable.
    function snap(v, size, extent) {
        if (Math.abs(v - win.pad) <= 26) return win.pad
        const far = extent - size - win.pad
        if (Math.abs(v - far) <= 26) return far
        const mid = Math.round((extent - size) / 2)
        if (Math.abs(v - mid) <= 18) return mid
        return v
    }

    function beginDrag() {
        WidgetLayout.dragging = true
        win.dragging = true
    }

    // Clamped to the usable area with the shadow's pad to spare, so a widget
    // can be parked against an edge but never off one.
    function moveCard(dx, dy) {
        if (win.dragX)
            card.x = Math.max(win.pad, Math.min(win.width - card.width - win.pad, card.x + dx))
        if (win.dragY)
            card.y = Math.max(win.pad, Math.min(win.height - card.height - win.pad, card.y + dy))
    }

    // Pressed and let go without going anywhere, or the grab was taken away by
    // a VT switch. Either way nothing moved, so nothing is written.
    function cancelDrag() {
        settle.stop()
        win.dragging = false
        WidgetLayout.dragging = false
    }

    function endDrag() {
        // Released before the surface ever grew — a click, not a drag. Nothing
        // moved, so there is nothing to save.
        if (!win.expanded) {
            win.dragging = false
            WidgetLayout.dragging = false
            return
        }
        settle.stop()
        settleX.to = win.dragX ? win.snap(card.x, card.width,  win.width)  : card.x
        settleY.to = win.dragY ? win.snap(card.y, card.height, win.height) : card.y
        settle.start()
    }

    // Which edges the widget is anchored to is re-derived from where it was
    // dropped: the nearer edge on each axis wins. Drag the clock to the top
    // left and it becomes a top-left widget, and stays in that corner when the
    // resolution changes — which keeping the original anchor would not do.
    function commitDrag() {
        const left   = Math.round(card.x)
        const right  = Math.round(win.width - card.width - card.x)
        const top    = Math.round(card.y)
        const bottom = Math.round(win.height - card.height - card.y)

        WidgetLayout.place(win.widgetId,
                           win.fillWidth ? win.homeEdgeH : (left <= right ? "left" : "right"),
                           top <= bottom ? "top" : "bottom",
                           win.fillWidth ? win.homeMarginX
                                         : Math.max(win.pad, Math.min(left, right)),
                           Math.max(win.pad, Math.min(top, bottom)))
        WidgetLayout.save()
        win.dragging = false
        WidgetLayout.dragging = false
    }

    // Back to the designed corner, for when a widget has been dropped somewhere
    // unhelpful. Double-click the grip.
    function resetPosition() {
        settle.stop()
        win.dragging = false
        WidgetLayout.dragging = false
        WidgetLayout.clear(win.widgetId)
    }

    /* ── The panel outline ─────────────────────────────────────────────────
     *
     * Two corners cut instead of four rounded. The diagonals are what stop this
     * reading as a settings dialog dropped on the wallpaper — and they are why
     * the curve renderer is asked for by name: on the default renderer an
     * unantialiased 45° edge at this size is a staircase.
     *
     * Self-contained on purpose. An inline component is its own type and must
     * not reach for ids in the file around it, so it sizes itself from the
     * parent it fills and takes its gradient as a property.
     */
    component Chamfered: Shape {
        id: sh
        property color line: "transparent"
        property real  lineWidth: 1
        property var   grad: null
        readonly property real cut: 13

        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: sh.line
            strokeWidth: sh.lineWidth
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.MiterJoin
            fillColor: "transparent"
            fillGradient: sh.grad

            startX: 0; startY: 0
            PathLine { x: sh.width - sh.cut; y: 0 }
            PathLine { x: sh.width;          y: sh.cut }
            PathLine { x: sh.width;          y: sh.height }
            PathLine { x: sh.cut;            y: sh.height }
            PathLine { x: 0;                 y: sh.height - sh.cut }
            PathLine { x: 0;                 y: 0 }
        }
    }
}
