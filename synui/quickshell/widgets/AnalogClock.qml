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
    label: "CLOCK"
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
        return ["minimal", "classic", "roman", "neon"].indexOf(f) >= 0
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
            "|" + root.seconds + "|" + root.ink + "|" + root.accent +
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

            const cx = w / 2, cy = h / 2
            const r  = Math.min(w, h) / 2 - 6
            if (r <= 4) return

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
            const k = r / rBase
            function px(v) { return Math.max(1, v * k) }

            const ink    = root.ink
            const dim    = root.inkDim
            const accent = root.accent
            const neon   = root.face === "neon"

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

            /* ── The bezel ── */
            if (root.face !== "minimal") {
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
                ctx.fillStyle = Qt.rgba(ink.r, ink.g, ink.b, 0.85)
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
            stroke(at(ha + Math.PI, r * 0.12), at(ha, r * 0.52),
                   rBase * 0.075, neon ? accent : ink, 0.95)
            stroke(at(ma + Math.PI, r * 0.14), at(ma, r * minReach),
                   rBase * 0.048, neon ? accent : ink, 0.95)

            if (root.seconds) {
                const sa = minutes(root.ss)
                stroke(at(sa + Math.PI, r * 0.20),
                       at(sa, root.face === "roman" ? r * 0.62 : r * 0.88),
                       1.8, accent, 0.95)
                // The counterweight: a real second hand has one, and at this
                // size it is the difference between a hand and a scratch.
                const tail = at(sa + Math.PI, r * 0.20)
                ctx.beginPath()
                ctx.fillStyle = Qt.rgba(accent.r, accent.g, accent.b, 0.95)
                ctx.arc(tail.x, tail.y, px(2.6), 0, Math.PI * 2)
                ctx.fill()
            }

            /* ── The pin ── */
            ctx.beginPath()
            ctx.fillStyle = root.seconds
                ? Qt.rgba(accent.r, accent.g, accent.b, 1)
                : Qt.rgba(ink.r, ink.g, ink.b, 0.95)
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
