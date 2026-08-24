pragma Singleton

import QtQuick
import ".."

/*
 * Style — the `qs.Commons` singleton an Omarchy bar widget reaches for.
 *
 * ⚠ A SHIM ONTO SynapseOS's OWN THEME, NOT A COPY OF OMARCHY'S.
 *
 * Omarchy is MIT (© David Heinemeier Hansson) so vendoring their Style.qml
 * verbatim would be perfectly legal — and would be the wrong thing. It is 23 KB
 * that carries THEIR spacing scale, THEIR font tokens and THEIR palette, so a
 * widget dropped onto this desktop would come out looking like a piece of
 * Omarchy sitting on SynapseOS instead of like part of the bar it is in. What
 * a widget actually wants from `Style` is "how big is body text here", "what is
 * one unit of space here" — questions this desktop already answers.
 *
 * So this is the same NAMES over synui's Theme. A widget written against the
 * documented contract gets SynapseOS's font, SynapseOS's spacing and
 * SynapseOS's corner radius without knowing it moved.
 *
 * ── What is here, and why it is this and not more ───────────────────────────
 *
 * Measured rather than guessed: across the eight bar widgets Omarchy ships,
 * the whole `Style.*` surface is eleven properties. They are all here. Their
 * real file has many more — a widget that reaches one of those fails to load
 * and says so in the bar's log, which is the honest outcome for an API this
 * side never claimed to have.
 *
 * The defaults in the comments are Omarchy's own values at their base font
 * size, kept so the arithmetic below can be checked against theirs.
 */
QtObject {
    id: root

    /*
     * One unit of space. Omarchy scales these by a user setting; synui has no
     * such knob, so the identity is the honest mapping — a widget asking for
     * `space(8)` on this desktop gets 8 pixels, which is what it asked for.
     * Rounded for the int form because a half-pixel margin is a blurry edge.
     */
    function spaceReal(px) { return px }
    function space(px)     { return Math.round(px) }

    /* Mirrors the compositor's window rounding on Omarchy; here it is the bar's
     * own, which is the surface the widget is sitting in. */
    readonly property int cornerRadius: Theme.barRadius

    readonly property QtObject font: QtObject {
        readonly property string family:    Theme.fontFamily
        readonly property int    baseSize:  Theme.fontSize
        /* Their ratios off the base size — 0.833 / 0.917 / 1.0 — so a caption
         * stays a caption relative to whatever this desktop's text size is,
         * including after Control panel ▸ Appearance ▸ Text scale. */
        readonly property int    caption:   Math.max(8, Math.round(Theme.fontSize * 0.833))
        readonly property int    bodySmall: Math.max(9, Math.round(Theme.fontSize * 0.917))
        readonly property int    body:      Theme.fontSize
    }

    readonly property QtObject bar: QtObject {
        /* The two slot sizes a widget lays itself out in. Fractions of the bar
         * rather than Omarchy's literals (27 and 21 at their 12px base), so a
         * widget is the right size on a bar this desktop can resize. */
        readonly property int iconSlot:   Math.round(Theme.barHeight * 0.96)
        readonly property int statusSlot: Math.round(Theme.barHeight * 0.75)
    }

    readonly property QtObject spacing: QtObject {
        readonly property int controlPaddingX: 10
    }

    /*
     * The wash under a hovered control.
     *
     * ⚠ TAKES THE FOREGROUND AND RETURNS AN ALPHA OF IT, which is Omarchy's
     * signature and matters: a hover fill picked from the theme's own ink sits
     * correctly on a light desktop and a dark one alike, where a fixed grey
     * only works on one. `urgent` wins where it is set, exactly as theirs does.
     */
    function hoverFillFor(foreground, accent, urgent) {
        const c = urgent ? urgent : (accent ? accent : foreground)
        return Qt.rgba(Qt.color(c).r, Qt.color(c).g, Qt.color(c).b, 0.16)
    }
}
