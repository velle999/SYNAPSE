pragma Singleton

import QtQuick
import Quickshell

/*
 * Util — the pure helpers `qs.Commons` promises. No state; anything stateful
 * belongs on Color, on Style, or on a service.
 *
 * ⚠ MEASURED, LIKE Style. Across 40 of the most-installed community widgets the
 * whole `Util.*` surface is four names — `alpha` (51 calls), `execDetached` (9),
 * `shellQuote` (8), `wheelSteps` (1) — plus the two this bar already used. They
 * are all here, with the handful their own widgets need beside them. A name
 * that is not here is `undefined`, and calling it throws where the widget stood
 * rather than anywhere a person would look.
 */
QtObject {
    id: root

    function clamp(value, min, max) {
        const n = Number(value)
        if (!isFinite(n)) return min
        return Math.max(min, Math.min(max, n))
    }

    function clampAlpha(value) { return root.clamp(value, 0, 1) }

    /*
     * A base colour composed with an opacity — the single most-called helper in
     * the whole API, because every hover fill, border and selection in every
     * panel is one of these.
     *
     * ⚠ TAKES A STRING OR A COLOR. A widget passes `Color.accent` (a color) in
     * one line and `"#00000000"` (a string) in the next, and a version that only
     * handled one of them would fail on half its call sites.
     */
    function alpha(c, opacity) {
        const a = root.clampAlpha(opacity)
        if (!c) return Qt.rgba(0, 0, 0, a)
        if (typeof c === "string") c = Qt.color(c)
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    /*
     * One wheel notch is one step.
     *
     * ⚠ CLAMPED TO ±120 BEFORE ACCUMULATING, which is the whole point of the
     * function. Some pointer and compositor combinations report a single notch
     * as several hundred units, and a volume control that took the raw delta
     * would jump from muted to full on one flick — while a touchpad, which
     * emits many small deltas, needs the remainder carried. Both cases, one
     * accumulator.
     */
    function wheelSteps(accumulator, delta) {
        delta = Math.max(-120, Math.min(120, delta))
        if (accumulator * delta < 0) accumulator = 0
        const total = accumulator + delta
        const steps = total < 0 ? Math.ceil(total / 120) : Math.floor(total / 120)
        return { steps: steps, remainder: total - steps * 120 }
    }

    /* file:// with each segment percent-encoded, so a space or a `#` in a user's
     * path does not truncate an Image.source. */
    function fileUrl(path) {
        if (!path) return ""
        return "file://" + String(path).split("/").map(encodeURIComponent).join("/")
    }

    /* Single-quote for a shell. The replace closes the literal, escapes the
     * quote and reopens — the only way to put a `'` inside `'…'`. */
    function shellQuote(s) {
        return "'" + String(s || "").replace(/'/g, "'\\''") + "'"
    }

    function execDetached(command) {
        Quickshell.execDetached(["bash", "-lc", command])
    }

    /*
     * An argv vector, without a shell re-reading it.
     *
     * ⛔ THE `exec "$@"` IS A CONSTANT AND THAT IS THE SECURITY OF IT. The
     * arguments only ever land in positional parameters, which bash expands
     * without tokenising again — so a filename containing `$(id)` stays a
     * filename. Anything built out of input goes through here and never through
     * execDetached.
     */
    function execArgv(argv) {
        Quickshell.execDetached(["bash", "-lc", 'exec "$@"', "bash"].concat(argv))
    }

    function isPlainObject(v) {
        return v !== null && typeof v === "object" && !Array.isArray(v)
    }

    /* A widget id with the punctuation a settings key cannot carry folded out,
     * which is how a manifest id becomes a lookup key. */
    function canonicalWidgetId(id) {
        return String(id || "").replace(/[^A-Za-z0-9_.-]/g, "-").toLowerCase()
    }

    function cloneJson(value) {
        try { return JSON.parse(JSON.stringify(value)) } catch (e) { return value }
    }
}
