pragma Singleton

import QtQuick

/*
 * Border — the `qs.Commons` singleton that describes a control's edge.
 *
 * A border SPEC is a plain object, and that shape is the contract:
 *
 *     { color: <color>, widths: { top, right, bottom, left } }
 *
 * Widgets build one with `flat()` or ask for a named control state with
 * `controlSpec()`, then hand it to a BorderSurface. They also read the four
 * side widths back to inset their own content, which is why the accessors
 * exist and why they have to tolerate being handed nothing.
 *
 * ⚠ THEIRS IS 242 LINES AND THIS IS NOT, AND THAT IS NOT A SHORTCUT. Most of
 * Omarchy's file is resolving a user token file — per-state colours, alphas,
 * gradients and per-side widths a person can override on their own desktop.
 * synui has no such file. Reimplementing the READER for a file that does not
 * exist would be code that can only ever return its own defaults, so what is
 * here is the defaults, taken off synui's Style, behind exactly their function
 * names.
 *
 * ⚠ GRADIENTS COLLAPSE TO A FLAT COLOUR. Their spec can carry one; nothing on
 * this desktop produces one, and a widget that passes one gets its base colour
 * rather than an error. Said here because a silent downgrade should be written
 * down somewhere.
 */
QtObject {
    id: root

    function widths(top, right, bottom, left) {
        return { top: top || 0, right: right || 0, bottom: bottom || 0, left: left || 0 }
    }

    function none() {
        return { color: "transparent", widths: root.widths(0, 0, 0, 0) }
    }

    /* One colour, one width, all four sides — what nearly every caller wants. */
    function flat(color, width) {
        const w = (width === undefined || width === null) ? Style.normalBorderWidth : width
        return { color: color ? color : "transparent", widths: root.widths(w, w, w, w) }
    }

    /*
     * The border for a named control state.
     *
     * ⚠ THE STATE NAMES ARE THEIRS AND ARE HYPHENATED — `hover-cursor`, not
     * `hover`. A widget passes the string through from its own state machine, so
     * a name spelt differently here matches nothing and returns an invisible
     * border rather than an error.
     */
    function controlSpec(state, foreground, accent, urgent) {
        const f = foreground ? foreground : Color.foreground
        const a = accent ? accent : Color.accent
        switch (state) {
        case "hover-cursor":
        case "hover":
            return { color: Style.hoverBorderFor(f, a, urgent),
                     widths: root.widths(Style.hoverBorderWidth, Style.hoverBorderWidth,
                                         Style.hoverBorderWidth, Style.hoverBorderWidth) }
        case "selected":
            return { color: Style.selectedBorderFor(f, a, urgent),
                     widths: root.widths(Style.selectedBorderWidth, Style.selectedBorderWidth,
                                         Style.selectedBorderWidth, Style.selectedBorderWidth) }
        case "focus":
            return { color: Style.focusBorderFor(f, a, urgent),
                     widths: root.widths(Style.focusBorderWidth, Style.focusBorderWidth,
                                         Style.focusBorderWidth, Style.focusBorderWidth) }
        case "normal":
            return { color: Style.normalBorderFor(f, a, urgent),
                     widths: root.widths(Style.normalBorderWidth, Style.normalBorderWidth,
                                         Style.normalBorderWidth, Style.normalBorderWidth) }
        }
        return root.none()
    }

    /* A surface's own edge — a panel, a popup, a card. On Omarchy each surface
     * can be themed separately; here they all take the caller's fallback, which
     * is the colour the widget already decided suits its own background. */
    function surfaceSpec(section, token, fallbackColor, fallbackWidth, alphaKey) {
        return root.flat(fallbackColor ? fallbackColor : Color.popups.border,
                         fallbackWidth === undefined ? Style.normalBorderWidth : fallbackWidth)
    }

    function localOrSurfaceSpec(section, token, localColor, defaultColor, fallbackWidth, alphaKey) {
        return root.flat(localColor ? localColor : defaultColor, fallbackWidth)
    }

    /* Whether a named state paints a border at all. `selected` is width 0 by
     * default — a selected row is shown by its FILL — and a caller asks this
     * rather than assuming, so a theme that opts into a selected border gets
     * one without the caller changing. */
    function controlHasWidth(state) {
        return root.maxWidth(root.controlSpec(state, Color.foreground, Color.accent, null)) > 0
    }

    function withWidth(spec, width) {
        return { color: root.color(spec), widths: root.widths(width, width, width, width) }
    }

    /* ── Reading a spec back ─────────────────────────────────────────────────
     *
     * ⚠ EVERY ONE OF THESE IS HANDED `undefined` IN PRACTICE. A widget reads
     * `Border.top(someSpec)` before the spec is assigned, on the first binding
     * pass — so a version that dereferenced blindly would throw on load rather
     * than on use. Guarded, and 0 is the right answer for "no border yet". */
    function top(spec)    { return spec && spec.widths ? spec.widths.top    : 0 }
    function right(spec)  { return spec && spec.widths ? spec.widths.right  : 0 }
    function bottom(spec) { return spec && spec.widths ? spec.widths.bottom : 0 }
    function left(spec)   { return spec && spec.widths ? spec.widths.left   : 0 }
    function color(spec)  { return spec && spec.color ? spec.color : "transparent" }

    function maxWidth(spec) {
        return Math.max(root.top(spec), root.right(spec), root.bottom(spec), root.left(spec))
    }
    function isNone(spec) { return root.maxWidth(spec) <= 0 }

    /* Rectangle.border draws one width on all four sides. Anything else needs
     * the overlay, which is four rectangles rather than one property. */
    function canUseNative(spec) {
        if (!spec || !spec.widths) return true
        const w = spec.widths
        return w.top === w.right && w.right === w.bottom && w.bottom === w.left
    }
    function needsOverlay(spec) { return !root.canUseNative(spec) && !root.isNone(spec) }
    function uniformWidth(spec) { return root.top(spec) }
}
