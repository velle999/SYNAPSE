import QtQuick
import Quickshell
import Quickshell.Io
import ".."

/*
 * The analog desktop clock.
 *
 * A dial, in one of four faces. It exists beside BigClock rather than inside it
 * because they are not the same widget wearing a different skin: BigClock is a
 * 42px readout meant to be legible from across the room and this is an object on
 * the desktop, and a person who wants both should be able to have both.
 *
 * ⚠ IT DOES NOT PARSE A TIME, AND THAT IS DELIBERATE. Every other clock on this
 * desktop goes through synui-clock so the Date & Time panel's 12/24-hour and
 * seconds toggles reach all of them from one place — BigClock's own header says
 * why. A DIAL HAS NO 12/24-HOUR SETTING to honour: a face is twelve hours by
 * construction, and reading a formatted string only to throw the format away and
 * recover the numbers from it would be a parser standing between this widget and
 * a fact it can simply ask the system for.
 *
 * The one setting a dial CAN honour is the second hand, and that is exactly the
 * setting it honours — read out of the same clock.state the panel writes, so
 * turning seconds off stops this from repainting once a second as well.
 *
 * ⚠ AND THE FACE IS NOT CHOSEN HERE. It is a control-panel row (Desktop ▸
 * "Analog clock face") written to settings.state and read back through
 * BarConfig, which is the route widget_glass already takes. The alternative was
 * click-to-cycle, and that costs `interactive` — a widget that takes clicks is
 * one the desktop right-click menu cannot be opened through, which WidgetFrame
 * charges QuickLaunch and the note for on purpose and which a clock has no
 * business paying.
 */
WidgetFrame {
    id: root

    widgetId: "analog"
    shown: WidgetState.analog
    label: I18n.tr("CLOCK")
    accent: Theme.clock

    // The hour markings and the hands are the theme's ink on whatever this card
    // is sitting on — the same opt-in the monitor and the note take, and it
    // matters more here than on a text widget: a dial at dock_opacity 0.00 is
    // hairlines straight onto the wallpaper with nothing behind them.
    inkOnBackdrop: true

    /*
     * ── Frameless, and resizable by its corner ──────────────────────────────
     *
     * `chrome: false` drops the card, the header rule and the "CLOCK" label, so
     * what is on the desktop is the dial and nothing else. A clock face is
     * already a bounded, self-evident object — it is round, it is obviously a
     * clock, and it has no reading that needs a caption. The frame was doing
     * what a frame is for on the widgets that DO need it (a column of numbers
     * has to be told where it ends) and nothing at all here.
     *
     * `inkOnBackdrop` above matters more with the card gone, not less: the
     * hands are now hairlines straight onto the wallpaper with nothing behind
     * them, so they have to be inked against what is actually there.
     *
     * ⚠ THE DIAL IS THE ONLY WIDGET THAT CAN HONESTLY RESIZE. Every part of it
     * is a fraction of its own box — the rim, the ticks, the hand lengths and
     * widths are all computed from `dial` below — so a bigger one is the same
     * drawing at a bigger size. The reporting widgets have a font size rather
     * than a dimension, and stretching their cards would put the same glyphs in
     * more space, which is why `resizable` is opt-in and this is the only
     * widget that opts in.
     *
     * The floor is where the hour ticks stop being distinguishable from the
     * minute ones; the ceiling is a dial that still fits a 1080p desktop beside
     * something else.
     */
    chrome: false

    resizable: true
    baseSize: 148
    minSize: 96
    maxSize: 420

    homeEdgeH: "right"; homeEdgeV: "top"
    homeMarginX: 22
    homeMarginY: 24

    /* What the whole face is drawn from. It was a constant; it is the frame's
     * live size now, so a drag on the corner reaches every fraction below with
     * no second number to keep in step. */
    readonly property int dial: root.userSize
    cardWidth: dial
    bodyHeight: dial

    // Which face. BarConfig resolves settings.state over synuirc; an unknown
    // name falls back rather than drawing nothing, because a typo in a config
    // file must not leave a blank card with no way to tell why.
    readonly property string face: {
        const f = String(Theme.analogClockFace || "")
        return ["minimal", "classic", "roman", "neon", "monster"].indexOf(f) >= 0
               ? f : "minimal"
    }

    // Whether the second hand exists at all. Same key the digits used, and the
    // same default: synui-clock's seed is seconds="0", so a desktop that has
    // never opened the Date & Time panel gets a dial that moves once a minute.
    property bool seconds: false

    property int  hh: 0
    property int  mm: 0
    property int  ss: 0

    Canvas {
        id: dialCanvas
        anchors.fill: parent
        // Repainted every tick, so an unlayered canvas is redrawn from its own
        // paint handler rather than resampled from a texture — the same reason
        // WidgetFrame refuses QtQuick.Effects.
        renderStrategy: Canvas.Immediate

        // Everything the picture depends on, named so a change repaints it. A
        // Canvas has no bindings of its own: without these it would draw once
        // and then only on the timer, so switching face or theme would do
        // nothing until the next second ticked over.
        readonly property string repaintKey:
            root.face + "|" + root.hh + ":" + root.mm + ":" + root.ss +
            "|" + root.seconds + "|" + root.ink + "|" + root.accentInk +
            /* ⚠ THE SIZE IS ONE OF THEM NOW. It was a constant when this list
             * was written; the corner grip changes it many times a second, and
             * every stroke width in onPaint is scaled from it. A Canvas does
             * repaint on resize, but this list's own rule is that anything the
             * picture depends on is named here — and relying on the implicit
             * one would mean the widths lagged the geometry by a frame during a
             * drag, which is exactly when somebody is looking at it. */
            "|" + root.dial
        onRepaintKeyChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            const w = width, h = height
            ctx.reset()
            ctx.clearRect(0, 0, w, h)

            const cx = w / 2
            // ⚠ NOT const: the monster face moves the dial down into the
            // creature's belly once, below, and every mark and hand is
            // placed through at() so they all follow.
            let cy = h / 2
            const half = Math.min(w, h) / 2
            const neon = root.face === "neon"
            const monster = root.face === "monster"

            /*
             * ⚠ EVERY STROKE WIDTH BELOW IS AN ABSOLUTE PIXEL COUNT, and the
             * dial is resizable now — so they all go through `k`.
             *
             * The radii were always fractions of `r` and scaled for free. The
             * WIDTHS were not: a 3px hour mark and a 10px neon glow are exactly
             * right on the designed 148px face, hairlines on a 420px one and a
             * blob on a 96px one. Left alone, dragging the corner would have
             * produced a bigger clock that looked progressively worse, which is
             * the kind of thing that reads as the resize being broken rather
             * than as line widths being fixed.
             *
             * `k` is the ratio to the radius at the designed size, so every
             * literal keeps meaning exactly what it does today and nothing had
             * to be re-tuned. Floored at 1 device pixel: a mark thinner than
             * that does not get thinner, it disappears.
             */
            const rBase = root.baseSize / 2 - 6

            /*
             * ⚠ THE RIM IS 6 PIXELS AND THE NEON GLOW IS NOT.
             *
             * The margin between the dial and the edge of its box was a flat 6,
             * from when the dial was a flat 148. Neon draws its outermost glow
             * pass AT r with lineWidth px(10), so it reaches 5k BEYOND r — five
             * pixels at the designed size, which is what the 6 was chosen to
             * clear, and fifteen at the 420 ceiling, which it is not.
             *
             * What that looks like is the bug as reported: the circle comes out
             * FLAT ON FOUR SIDES. Those are the only places a circle touches its
             * bounding box, so they are the only places the overflow has
             * anywhere to be cut off — the ring is whole in the diagonals and
             * sheared at twelve, three, six and nine.
             *
             * Neon alone, which is why the other three faces are fine at every
             * size: their bezel is px(2)/px(2.5), so it reaches at most 1.25k
             * past r and is still inside the 6 at the ceiling.
             *
             * Two passes because the rim depends on k and k depends on r: the
             * first sizes k off the untrimmed radius, which OVER-estimates the
             * rim, so the second pass always leaves the glow room to spare. At
             * the designed size both passes give r = 68 and k = 1, so the face
             * that was drawn before is drawn to the pixel now.
             */
            let r = half - 6
            let k = r / rBase
            if (neon) {
                r = half - Math.max(6, 5 * k + 1)
                k = r / rBase
            }
            /*
             * ⚠ THE MONSTER'S DIAL IS SMALLER THAN ITS BOX, because the dial is
             * not the whole picture on this face — it is what the creature is
             * holding. 0.60 leaves the ears their height above it and the paws
             * theirs below, at every size, because everything else on the face
             * is a fraction of `half` too.
             */
            if (monster) {
                r = half * 0.60
                k = r / rBase
            }
            if (r <= 4) return
            function px(v) { return Math.max(1, v * k) }

            const ink    = root.ink
            const dim    = root.inkDim
            /* ⚠ accentInk, NOT accent. On this face the accent IS the
             * drawing — bezel, glow, every tick, both hands, the pin — so it
             * is the one colour that has to survive the wallpaper behind it.
             * WidgetFrame corrects it against that backdrop the way it already
             * corrected `ink`, which the neon face never draws in. */
            const accent = root.accentInk
            /* ⛔ FIXED, ON THE MONSTER, AND ONLY THERE. `ink` is light on a
             * dark theme, and the monster's dial is a white belly the creature
             * carries with it — so theme ink there is light on light, which is
             * a clock nobody can read on half the desktops that have one. Every
             * other face draws straight onto the wallpaper and wants the
             * corrected ink; this one brings its own background. */
            const dialInk = monster ? Qt.rgba(0.16, 0.13, 0.22, 1) : ink

            function stroke(a, b, width, colour, alpha, cap) {
                ctx.beginPath()
                ctx.lineWidth   = px(width)
                ctx.lineCap     = cap || "round"
                ctx.strokeStyle = Qt.rgba(colour.r, colour.g, colour.b,
                                          alpha === undefined ? 1 : alpha)
                ctx.moveTo(a.x, a.y)
                ctx.lineTo(b.x, b.y)
                ctx.stroke()
            }
            function at(angle, dist) {
                return { x: cx + Math.cos(angle) * dist,
                         y: cy + Math.sin(angle) * dist }
            }
            // Twelve o'clock is straight up, and canvas angles run clockwise
            // from three. Every hand and mark goes through here so the offset
            // is written once.
            function hours(n)   { return n * Math.PI / 6 - Math.PI / 2 }
            function minutes(n) { return n * Math.PI / 30 - Math.PI / 2 }

            /* ── The creature ──
             *
             * ⛔ DRAWN HERE, NOT COPIED. The thing this is modelled on is a
             * Windows Vista/7 sidebar gadget somebody drew, and their drawing
             * is theirs — the same rule syn-arcade's icon set states at length
             * about application logos, and the same reason: a mark or an
             * artwork somebody else owns has no business inside a GPL package.
             * So this is an original animal in the same spirit — a round pink
             * thing with ears, eyes and paws holding a clock — and every curve
             * of it is arithmetic in this file.
             *
             * ⚠ IT IS PINK ON EVERY THEME, and that is deliberate rather than
             * an oversight. The other four faces are the theme's ink because
             * they are chrome; this one is a CHARACTER, and a character whose
             * colour follows the accent is a different character on every
             * desktop. Tux is black and white wherever he is drawn, for the
             * same reason.
             *
             * ⚠ WHICH IS WHY THE DIAL IS ITS OWN LIGHT FACE. Ink is light on a
             * dark theme, and light ink on the white belly of a pink animal is
             * a clock nobody can read — so the marks and the two hands below
             * are a fixed dark on this face, and the accent is kept for the
             * second hand where it lands on white either way.
             */
            if (monster) {
                /*
                 * ⚠ 0.78, NOT 0.86 — THE EARS SPEND THE DIFFERENCE. Everything
                 * on this creature is a fraction of bodyR, so the number is
                 * really a decision about how much of the box is animal and how
                 * much is the space its ears stand up into. At 0.86 there was no
                 * such space: the ears could reach 1.14 * bodyR before they hit
                 * the edge, which is a pair of horns poking out of a circle
                 * rather than the tall pointed ears the thing is supposed to
                 * have. At 0.78 they reach 1.22 and still clear the box.
                 *
                 * The dial does not move with it — r stays 0.60 * half — so the
                 * belly now fills 0.77 of the body's width instead of 0.70,
                 * which is the other half of what makes this read as a small
                 * animal holding a big clock.
                 */
                const bodyR   = half * 0.78
                /*
                 * ⚠ 0.19, AND IT IS WRITTEN TWICE — once here for the belly and
                 * again below for `cy`, because the marks and hands are placed
                 * long after this block has gone out of scope. They have to
                 * agree: the two numbers ARE the same dial.
                 *
                 * It was 0.10, which put the clock in the middle of the chest
                 * instead of down in the belly, and the cost was the eyes — the
                 * belly's rim rose far enough to cut them in half, so the
                 * creature looked like it was peering over the top of the clock
                 * rather than holding one below its face.
                 */
                const dialDrop = 0.19
                const fur     = Qt.rgba(0.98, 0.55, 0.82, 1)     // the coat
                const furDark = Qt.rgba(0.85, 0.36, 0.66, 1)     // its edge
                const furLit  = Qt.rgba(1.00, 0.74, 0.90, 1)     // the light on it

                function blob(x, y, rx, ry, fill) {
                    ctx.beginPath()
                    ctx.ellipse(x - rx, y - ry, rx * 2, ry * 2)
                    ctx.fillStyle = fill
                    ctx.fill()
                    ctx.lineWidth = px(2)
                    ctx.strokeStyle = Qt.rgba(furDark.r, furDark.g, furDark.b, 0.9)
                    ctx.stroke()
                }

                /* Ears first, so the head is drawn over their roots and they
                 * come out of it rather than sitting on it.
                 *
                 * ⚠ THE BASE SITS ON THE HEAD'S TOP CORNER, NOT INSIDE IT. The
                 * first pair were narrow spikes rooted at 0.4 * bodyR — halfway
                 * down the skull — and a triangle that starts that deep and ends
                 * barely clear of the outline is a horn. An ear is broad where
                 * it meets the head and comes to a point well above it: the base
                 * spans 0.54 * bodyR along the top-right of the curve, and the
                 * tip is 1.22 up and slightly OUTBOARD of the base's centre,
                 * which is the lean that makes a pair of ears look alert rather
                 * than like two identical cones. */
                for (const side of [-1, 1]) {
                    const inner = { x: cx + side * bodyR * 0.46,
                                    y: cy - bodyR * 0.72 }
                    const outer = { x: cx + side * bodyR * 1.00,
                                    y: cy - bodyR * 0.50 }
                    const tip   = { x: cx + side * bodyR * 0.89,
                                    y: cy - bodyR * 1.22 }
                    ctx.beginPath()
                    ctx.moveTo(inner.x, inner.y)
                    ctx.lineTo(tip.x, tip.y)
                    ctx.lineTo(outer.x, outer.y)
                    ctx.closePath()
                    ctx.fillStyle = Qt.rgba(fur.r, fur.g, fur.b, 1)
                    ctx.fill()
                    ctx.lineWidth = px(2)
                    ctx.lineJoin = "round"
                    ctx.strokeStyle = Qt.rgba(furDark.r, furDark.g, furDark.b, 0.9)
                    ctx.stroke()

                    /* The inner ear: the same triangle shrunk towards its own
                     * centre, in the lit fur. Without it the ear is a flat pink
                     * shape and the head reads as a cut-out. */
                    const mx = (inner.x + outer.x + tip.x) / 3
                    const my = (inner.y + outer.y + tip.y) / 3
                    function inset(p) {
                        return { x: mx + (p.x - mx) * 0.34,
                                 y: my + (p.y - my) * 0.40 }
                    }
                    const i2 = inset(inner), o2 = inset(outer), t2 = inset(tip)
                    ctx.beginPath()
                    ctx.moveTo(i2.x, i2.y)
                    ctx.lineTo(t2.x, t2.y)
                    ctx.lineTo(o2.x, o2.y)
                    ctx.closePath()
                    ctx.fillStyle = Qt.rgba(furLit.r, furLit.g, furLit.b, 0.38)
                    ctx.fill()
                }

                /* Feet, under the body for the same reason. ⚠ THESE ARE NOT
                 * THE HANDS. The creature's hands are on the dial's two sides,
                 * drawn last so they land on top of it; these are the haunches
                 * it is sitting on, and they stay behind everything. */
                for (const side of [-1, 1])
                    blob(cx + side * bodyR * 0.74, cy + bodyR * 0.70,
                         bodyR * 0.26, bodyR * 0.22,
                         Qt.rgba(fur.r, fur.g, fur.b, 1))

                /* The body. Wider than tall, so the dial sits in a belly rather
                 * than in a head. */
                blob(cx, cy, bodyR, bodyR * 0.86, Qt.rgba(fur.r, fur.g, fur.b, 1))

                /* A highlight along the top left, which is the whole difference
                 * between a drawing of an animal and a pink circle. */
                ctx.beginPath()
                ctx.ellipse(cx - bodyR * 0.72, cy - bodyR * 0.74,
                            bodyR * 0.62, bodyR * 0.38)
                ctx.fillStyle = Qt.rgba(furLit.r, furLit.g, furLit.b, 0.55)
                ctx.fill()

                /* Eyes, above the dial and inside the body.
                 *
                 * ⚠ SMALL, CLOSE TOGETHER, AND LOOKING STRAIGHT AT YOU. The
                 * first draft drew two big white ovals set wide apart with a
                 * small pupil pushed up towards twelve, on the theory that eyes
                 * dead centre read as a doll. They do not — what they read as
                 * is a creature that has noticed you, and a pupil rolled up at
                 * the clock it is holding reads as one that has not. So each eye
                 * is one dark bead, taller than wide, with the whole of the iris
                 * facing forward and a catchlight up on the left doing the work
                 * the sclera used to: 0.14 of bodyR across against the 0.60 the
                 * pair of ovals took, which is what puts them on the forehead
                 * instead of across the whole face.
                 *
                 * ⚠ HIGH ENOUGH TO CLEAR THE BELLY. The dial's rim reaches
                 * r * 1.06 from a centre pushed 0.19 * half down, so an eye any
                 * lower than about 0.6 * bodyR has its bottom behind the clock,
                 * which reads as a drawing that does not fit together rather
                 * than as a creature holding something. */
                for (const side of [-1, 1]) {
                    const ex  = cx + side * bodyR * 0.23
                    const ey  = cy - bodyR * 0.70
                    const erx = bodyR * 0.085
                    const ery = bodyR * 0.120

                    ctx.beginPath()
                    ctx.ellipse(ex - erx, ey - ery, erx * 2, ery * 2)
                    ctx.fillStyle = Qt.rgba(0.13, 0.09, 0.14, 1)
                    ctx.fill()
                    ctx.lineWidth = px(1.2)
                    ctx.strokeStyle = Qt.rgba(furDark.r, furDark.g, furDark.b, 0.75)
                    ctx.stroke()

                    /* The catchlight, up on the left of BOTH eyes rather than
                     * mirrored — a highlight is where the light is, and the
                     * light on this face comes from the top left, the same
                     * corner the coat is lit from. Mirroring it is the tell of
                     * a drawing with no light in it. */
                    ctx.beginPath()
                    ctx.ellipse(ex - erx * 0.72, ey - ery * 0.70,
                                erx * 0.80, ery * 0.72)
                    ctx.fillStyle = Qt.rgba(1, 1, 1, 0.92)
                    ctx.fill()
                }

                /* The belly the dial sits on, and its rim. */
                const dcy = cy + half * dialDrop
                ctx.beginPath()
                ctx.arc(cx, dcy, r * 1.06, 0, Math.PI * 2)
                ctx.fillStyle = Qt.rgba(1, 1, 1, 0.97)
                ctx.fill()
                ctx.lineWidth = px(2.5)
                ctx.strokeStyle = Qt.rgba(furDark.r, furDark.g, furDark.b, 0.8)
                ctx.stroke()

                /* ⚠ EVERYTHING BELOW IS DRAWN ON TOP OF THE BELLY, and that is
                 * the whole point of it being down here rather than up with the
                 * ears. The teeth and the hands are the two things that touch
                 * the clock, and a tooth or a paw painted before the white disc
                 * is a tooth or a paw the disc then covers up. `at()` is no use
                 * to them either: it still answers around the body's centre for
                 * a few lines yet, so these place themselves around `dcy`. */
                function onDial(hour, dist) {
                    const a = hours(hour)
                    return { x: cx  + Math.cos(a) * r * dist,
                             y: dcy + Math.sin(a) * r * dist }
                }

                /* ── Teeth ──
                 *
                 * Two of them, at the dial's upper corners, pointing down out
                 * of a jaw the clock is otherwise hiding. ⚠ THE MOUTH IS NEVER
                 * DRAWN: the creature is holding the dial across its face, so
                 * all that shows of a mouth is the pair of points either side
                 * of it, and a mouth line added behind them only reads as a
                 * crack in the belly. */
                for (const side of [-1, 1]) {
                    /* ⚠ 1.08, NOT 1.16 — A TOOTH HAS TO TOUCH THE THING IT IS
                     * BITING. The rim is at 1.06 r, so a tooth centred at 1.16
                     * sits entirely out in the fur with a gap under it, and at
                     * this size a small white triangle floating in pink reads as
                     * a speck of dirt rather than as a fang. Centred at 1.08 its
                     * point crosses the rim and the jaw closes on the dial. */
                    const tip = onDial(12 + side * 1.38, 1.08)
                    const tw  = r * 0.085
                    const th  = r * 0.20
                    ctx.beginPath()
                    ctx.moveTo(tip.x - tw, tip.y - th * 0.42)
                    ctx.lineTo(tip.x + tw, tip.y - th * 0.42)
                    // Tilted outward, so the pair splays the way teeth in a jaw
                    // do rather than hanging like two icicles.
                    ctx.lineTo(tip.x + side * tw * 0.55, tip.y + th * 0.58)
                    ctx.closePath()
                    ctx.fillStyle = Qt.rgba(1, 1, 1, 0.98)
                    ctx.fill()
                    ctx.lineWidth = px(1.2)
                    ctx.lineJoin  = "round"
                    ctx.strokeStyle = Qt.rgba(furDark.r, furDark.g, furDark.b, 0.6)
                    ctx.stroke()
                }

                /* ── The hands holding it ──
                 *
                 * ⚠ ON THE DIAL'S SIDES, NOT UNDER IT. A paw parked at the foot
                 * of the clock is a creature standing behind one; a paw at nine
                 * and another at three, each with its inner edge over the rim,
                 * is a creature carrying one. So the distance is 1.16 r against
                 * a paw 0.16 r across — the inner edge lands at 1.00 r, inside
                 * the 1.06 r rim, and the overlap is what does the holding.
                 *
                 * They sit a little above the middle (9:14 and 2:46 rather than
                 * 9:00 and 3:00) because arms come down to a thing held against
                 * the chest, and level paws read as a clock resting on a shelf. */
                for (const side of [-1, 1]) {
                    const paw = onDial(6 - side * 3.23, 1.10)
                    blob(paw.x, paw.y, r * 0.21, r * 0.27,
                         Qt.rgba(fur.r, fur.g, fur.b, 1))

                    /* ⚠ TWO CREASES ON THE INNER EDGE, NOT ONE ACROSS THE
                     * MIDDLE. A single line through the centre of the lump
                     * reads as a seam — the lump looks split rather than
                     * jointed. Fingers are short, they are stacked, and they
                     * are on the side of the paw that meets the clock, which is
                     * where a hand's fingers are when it is holding one. */
                    for (const j of [-1, 1]) {
                        const fy = paw.y + j * r * 0.10
                        ctx.beginPath()
                        ctx.moveTo(paw.x - side * r * 0.16, fy)
                        ctx.lineTo(paw.x - side * r * 0.02, fy + j * r * 0.03)
                        ctx.lineWidth   = px(1.4)
                        ctx.lineCap     = "round"
                        ctx.strokeStyle = Qt.rgba(furDark.r, furDark.g,
                                                  furDark.b, 0.65)
                        ctx.stroke()
                    }
                }
            }

            /* ⚠ THE DIAL SITS LOW ON THE MONSTER, in the belly rather than in
             * the middle of the box — the ears take the top. Every mark and
             * hand below is placed through at(), so moving the centre once here
             * moves all of them. */
            if (monster) cy += half * 0.19

            /* ── The bezel ── */
            // Not on the monster: the belly's own rim, in the creature's fur
            // colour, is that face's bezel and a second ring inside it reads as
            // a mistake.
            if (root.face !== "minimal" && !monster) {
                ctx.beginPath()
                ctx.lineWidth   = px(neon ? 2.5 : 2)
                ctx.strokeStyle = neon
                    ? Qt.rgba(accent.r, accent.g, accent.b, 0.85)
                    : Qt.rgba(ink.r, ink.g, ink.b, 0.30)
                ctx.arc(cx, cy, r, 0, Math.PI * 2)
                ctx.stroke()
            }

            /* ── The hour marks ──
             *
             * Four faces, and they differ ONLY here and in the hands. A face is
             * a set of marks; making each one its own file would be four copies
             * of the geometry above for a switch statement's worth of
             * difference.
             */
            if (root.face === "minimal") {
                // Four strokes at the quarters. Nothing else — this is the face
                // for a desktop that wants a clock without an ornament on it.
                for (let i = 0; i < 12; i += 3)
                    stroke(at(hours(i), r * 0.82), at(hours(i), r * 0.96),
                           3, ink, 0.75)
                for (let i = 0; i < 12; i++) {
                    if (i % 3 === 0) continue
                    const p = at(hours(i), r * 0.92)
                    ctx.beginPath()
                    ctx.fillStyle = Qt.rgba(dim.r, dim.g, dim.b, 0.55)
                    ctx.arc(p.x, p.y, px(1.6), 0, Math.PI * 2)
                    ctx.fill()
                }
            } else if (root.face === "classic") {
                // A full railway dial: a mark every minute, longer at the hours.
                for (let i = 0; i < 60; i++) {
                    const hour = i % 5 === 0
                    stroke(at(minutes(i), r * (hour ? 0.78 : 0.88)),
                           at(minutes(i), r * 0.95),
                           hour ? 3 : 1, ink, hour ? 0.8 : 0.35, "butt")
                }
            } else if (root.face === "roman") {
                const numerals = ["XII", "I", "II", "III", "IIII", "V", "VI",
                                  "VII", "VIII", "IX", "X", "XI"]
                ctx.font = "600 " + Math.round(r * 0.15) + "px " + Theme.fontFamily
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                /*
                 * The numerals carry the accent. This is the only face with
                 * numbers on it, so it is the only place the change applies —
                 * the other three mark their hours with ticks, and those are
                 * already ink on purpose so the hands can be read against them.
                 *
                 * ⚠ accentInk, NOT accent, for the reason given where it is
                 * bound above: this is drawn over the wallpaper, and accentInk
                 * is the accent already corrected against that backdrop.
                 *
                 * ⚠ AND 0.95, NOT THE 0.85 THE INK USED. accentOn() corrects
                 * the colour to a contrast ratio AT FULL ALPHA; drawing it
                 * thinner blends it back toward the background and gives away
                 * some of what the correction just bought. These are the
                 * smallest marks on the dial — r * 0.15 — and small text needs
                 * more of that ratio, not less.
                 */
                ctx.fillStyle = Qt.rgba(accent.r, accent.g, accent.b, 0.95)
                /*
                 * ⚠ 0.70, NOT 0.80. The numerals are CENTRED on this radius and
                 * they are not all the same width: "IIII" and "VIII" are four
                 * and five times the width of "I", so a ring that fits the thin
                 * ones puts half of the wide ones outside the bezel — and at
                 * three and nine o'clock, outside the CARD. It was 0.80, and
                 * IIII was clipped by the card's own edge.
                 */
                for (let i = 0; i < 12; i++) {
                    const p = at(hours(i), r * 0.70)
                    // IIII rather than IV at four o'clock: it is what almost
                    // every real dial has done since clocks had faces, and it
                    // balances the VIII opposite it.
                    ctx.fillText(numerals[i], p.x, p.y)
                }
            } else if (monster) {
                /*
                 * A tick every hour, longer at the quarters, and the 12 written
                 * out — which is the one numeral a face like this needs, because
                 * a dial with no numbers at all reads as a decoration and the
                 * top mark is what tells the eye which way up it is.
                 *
                 * ⚠ THE TICKS ARE BLUE, NOT THE ACCENT. They are on the
                 * creature's own white belly, which never changes colour, so
                 * there is nothing here for an accent to adapt TO — and a dial
                 * that recoloured with the theme while the animal holding it
                 * stayed pink would look like two drawings.
                 */
                const tick = Qt.rgba(0.25, 0.47, 0.85, 1)
                for (let i = 0; i < 12; i++) {
                    const quarter = i % 3 === 0
                    if (i === 0) continue        // the 12 is written, not ticked
                    stroke(at(hours(i), r * (quarter ? 0.68 : 0.76)),
                           at(hours(i), r * 0.90),
                           quarter ? 3.5 : 2.5, tick, quarter ? 0.95 : 0.7, "butt")
                }
                ctx.font = "700 " + Math.round(r * 0.30) + "px " + Theme.fontFamily
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillStyle = Qt.rgba(tick.r, tick.g, tick.b, 0.95)
                ctx.fillText("12", cx, cy - r * 0.72)
            } else {
                // neon: a glow ring and a tick at each hour, in the accent.
                for (let pass = 0; pass < 3; pass++) {
                    ctx.beginPath()
                    ctx.lineWidth   = px(10 - pass * 3)
                    ctx.strokeStyle = Qt.rgba(accent.r, accent.g, accent.b,
                                              0.05 + pass * 0.06)
                    ctx.arc(cx, cy, r, 0, Math.PI * 2)
                    ctx.stroke()
                }
                for (let i = 0; i < 12; i++)
                    stroke(at(hours(i), r * 0.74), at(hours(i), r * 0.88),
                           i % 3 === 0 ? 3.5 : 2, accent, i % 3 === 0 ? 0.9 : 0.45)
            }

            /* ── The hands ──
             *
             * The fractional parts are not a nicety. An hour hand that sits on
             * the 3 until it snaps to the 4 is a clock that reads wrong for 58
             * of every 60 minutes, and the position of the hour hand BETWEEN two
             * numerals is most of what makes a dial readable at a glance.
             */
            const minf  = root.mm + root.ss / 60
            const hourf = (root.hh % 12) + minf / 60

            const ha = hours(hourf)
            const ma = minutes(minf)

            if (neon) {
                // Bloom under both hands: the same stroke, wider and fainter,
                // which is how WidgetFrame draws its own glow.
                stroke(at(ha + Math.PI, r * 0.12), at(ha, r * 0.52),
                       9, accent, 0.16)
                stroke(at(ma + Math.PI, r * 0.14), at(ma, r * 0.80),
                       7, accent, 0.16)
            }
            // The minute hand stops short of the numerals on the roman face:
            // their ring is at 0.70, and a hand crossing it reads as a strike
            // through whichever numeral it happens to be over.
            const minReach = root.face === "roman" ? 0.60 : 0.80
            /*
             * ⚠ rBase, NOT r — THE ONLY TWO WIDTHS ON THIS FACE THAT ARE A
             * FRACTION OF THE RADIUS.
             *
             * Everything else handed to stroke() is an absolute count of pixels
             * at the designed size, which is exactly what px() is for. These two
             * were written as `r * 0.075` and `r * 0.048`, which is already the
             * right width at any radius — and then px() multiplied them by k a
             * second time. The hands therefore grew with the SQUARE of the dial:
             * correct at 148px where k is 1, and at the 420px ceiling a 46px
             * slab where 15px was meant.
             *
             * It reads worst on neon, which is the only face that draws a bloom
             * under its hands. The bloom is an absolute 9 and scales linearly,
             * so past about twice the designed size the hand became WIDER than
             * the glow that is supposed to surround it — the glow vanished under
             * the slab and the face stopped looking like neon at all.
             *
             * px(rBase * 0.075) is r * 0.075 at every size, so the fractions
             * still mean what they say and the designed face is unchanged.
             */
            /*
             * ⚠ THE MONSTER'S TWO HANDS ARE COLOURED, and it is the one face
             * where they are. The other four draw hands in ink because ink is
             * what stands out against their marks; here the marks are already
             * blue on white, so an ink hand would be the third dark thing in a
             * small circle. Orange and blue is the pair that reads fastest at
             * this size, and telling the hour hand from the minute hand at a
             * glance is most of what a dial is for.
             */
            const hourInk = monster ? Qt.rgba(0.95, 0.60, 0.12, 1)
                                    : (neon ? accent : ink)
            const minInk  = monster ? Qt.rgba(0.25, 0.47, 0.85, 1)
                                    : (neon ? accent : ink)
            stroke(at(ha + Math.PI, r * 0.12), at(ha, r * 0.52),
                   rBase * 0.075, hourInk, 0.95)
            stroke(at(ma + Math.PI, r * 0.14), at(ma, r * minReach),
                   rBase * 0.048, minInk, 0.95)

            // Red, on the monster, because that is what a second hand is on a
            // face like this — and the accent is already spoken for by neither
            // of the other two hands here.
            const secInk = monster ? Qt.rgba(0.90, 0.16, 0.28, 1) : accent
            if (root.seconds) {
                const sa = minutes(root.ss)
                stroke(at(sa + Math.PI, r * 0.20),
                       at(sa, root.face === "roman" ? r * 0.62 : r * 0.88),
                       1.8, secInk, 0.95)
                // The counterweight: a real second hand has one, and at this
                // size it is the difference between a hand and a scratch.
                const tail = at(sa + Math.PI, r * 0.20)
                ctx.beginPath()
                ctx.fillStyle = Qt.rgba(secInk.r, secInk.g, secInk.b, 0.95)
                ctx.arc(tail.x, tail.y, px(2.6), 0, Math.PI * 2)
                ctx.fill()
            }

            /* ── The pin ── */
            ctx.beginPath()
            ctx.fillStyle = root.seconds
                ? Qt.rgba(secInk.r, secInk.g, secInk.b, 1)
                : Qt.rgba(dialInk.r, dialInk.g, dialInk.b, 0.95)
            ctx.arc(cx, cy, px(root.seconds ? 3.4 : 3.0), 0, Math.PI * 2)
            ctx.fill()
            if (!neon) {
                ctx.beginPath()
                ctx.fillStyle = Qt.rgba(0, 0, 0, 0.55)
                ctx.arc(cx, cy, px(1.3), 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }

    /*
     * The time, from JavaScript's own Date.
     *
     * ⚠ NOT synui-clock, and the header says why: there is no format to honour
     * on a dial, and spawning a process once a second to print a string this
     * widget would then have to un-format is a worse answer than asking the
     * clock the system already has. The one setting that DOES apply — seconds —
     * is read below, from the file the Date & Time panel writes.
     */
    function tick() {
        const d = new Date()
        root.hh = d.getHours()
        root.mm = d.getMinutes()
        root.ss = d.getSeconds()
    }

    Timer {
        // A dial with no second hand moves once a minute, and a widget that
        // wakes 60 times more often than it changes is the cost Pizza.qml's
        // comment is about. Still one second when the hand is there.
        interval: root.seconds ? 1000 : 15000
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: root.tick()
    }

    // Clock & Time's "Show seconds", out of the file that panel writes. Watched,
    // so the toggle reaches this dial without a restart — the same idiom
    // WidgetState uses for widgets.state.
    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/clock.state"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: {
            let on = false
            for (const raw of String(this.text()).split("\n")) {
                const line = raw.trim()
                if (!line || line.startsWith("#")) continue
                const eq = line.indexOf("=")
                if (eq < 0) continue
                if (line.slice(0, eq).trim() !== "seconds") continue
                // ⚠ `seconds=1`, NOT `seconds=on`. clock.c writes an INT here
                // while widgets.state next door writes on/off, and reading this
                // file with that file's rule would leave the second hand off on
                // every desktop that had asked for it.
                on = line.slice(eq + 1).trim() === "1"
            }
            root.seconds = on
        }
        // No file at all is the normal case, not an error: it means the Date &
        // Time panel has never been saved, and synui-clock's own default is off.
        onLoadFailed: root.seconds = false
    }
}
