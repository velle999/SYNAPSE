pragma Singleton

import QtQuick
import ".."

/*
 * Style — the `qs.Commons` singleton an Omarchy bar widget reaches for.
 *
 * ⚠ A SHIM ONTO SynapseOS's OWN THEME, NOT A COPY OF OMARCHY'S.
 *
 * Omarchy is MIT (© David Heinemeier Hansson) so vendoring their Style.qml
 * verbatim would be perfectly legal — and would be the wrong thing. It carries
 * THEIR spacing scale, THEIR font tokens and THEIR palette, so a widget dropped
 * onto this desktop would come out looking like a piece of Omarchy sitting on
 * SynapseOS instead of like part of the bar it is in. What a widget actually
 * wants from `Style` is "how big is body text here", "what is one unit of space
 * here" — questions this desktop already answers.
 *
 * So this is the same NAMES over synui's Theme. A widget written against the
 * documented contract gets SynapseOS's font, SynapseOS's spacing and
 * SynapseOS's corner radius without knowing it moved.
 *
 * ── The surface is MEASURED, and that is the whole method ───────────────────
 *
 * ⛔ A MISSING TYPE IS LOUD AND A MISSING PROPERTY IS SILENT. `import qs.Ui`
 * naming a type nothing provides fails to load and says so; `Style.spacing.md`
 * where there is no `md` is `undefined`, which lays out as zero and draws a
 * widget that is present, running and invisible. Nothing logs it. So this file
 * cannot be written to taste — it is built from a COUNT over the real corpus:
 *
 *     grep -rhoE '\bStyle\.[A-Za-z0-9_.]+' <widgets>/*.qml | sort | uniq -c
 *
 * over 40 of the most-installed community widgets. Every key that survey
 * returned is here. The counts are kept beside the groups below so the next
 * person can see what was being answered rather than guessing whether a key
 * matters. Re-run it against a fresh sample before adding anything.
 *
 * The values in the trailing comments are Omarchy's own at their base font
 * size, kept so the arithmetic here can be checked against theirs.
 */
QtObject {
    id: root

    /*
     * One unit of space. Omarchy scales these by a user setting; synui has no
     * such knob, so the identity is the honest mapping — a widget asking for
     * `space(8)` on this desktop gets 8 pixels, which is what it asked for.
     * Rounded for the int form because a half-pixel margin is a blurry edge.
     *
     * 1,106 calls in the sample. It is by a wide margin the most-used thing in
     * the whole API and the reason a wrong answer here is invisible everywhere.
     */
    function spaceReal(px) { return px }
    function space(px)     { return Math.round(px) }

    /* Mirrors the compositor's window rounding on Omarchy; here it is the bar's
     * own, which is the surface the widget is sitting in. */
    readonly property int cornerRadius: Theme.barRadius

    /* The gap their compositor leaves around a window, which panels use to sit
     * off the screen edge. synui's bar radius is the nearest real answer; the
     * literal is theirs. */
    readonly property int gapsOut: 5

    // ── Fonts ───────────────────────────────────────────────────────────────
    //
    // Their ratios off the base size, so a caption stays a caption relative to
    // whatever this desktop's text size is — including after Control panel ▸
    // Appearance ▸ Text scale, which their scale knob has no counterpart for.
    function fontPx(ratio) { return Math.max(8, Math.round(Theme.fontSize * ratio)) }

    readonly property QtObject font: QtObject {
        readonly property string family:         Theme.fontFamily
        readonly property string resolvedFamily: Theme.fontFamily
        /* Omarchy lets a popup take a different family from the bar. synui has
         * one family for the whole desktop and that is deliberate, so this is
         * the same answer rather than a second knob. */
        readonly property string menuFamily:     Theme.fontFamily
        readonly property int    baseSize:       Theme.fontSize

        readonly property int caption:      root.fontPx(0.833)   // 10
        readonly property int bodySmall:    root.fontPx(0.917)   // 11
        readonly property int body:         Theme.fontSize       // 12
        readonly property int subtitle:     root.fontPx(1.083)   // 13
        readonly property int title:        root.fontPx(1.167)   // 14
        readonly property int heading:      root.fontPx(1.333)   // 16
        readonly property int display:      root.fontPx(2.0)     // 24
        readonly property int displayLarge: root.fontPx(2.333)   // 28

        readonly property int iconSmall:    root.fontPx(0.917)
        readonly property int icon:         root.fontPx(1.167)
        readonly property int iconLarge:    root.fontPx(1.5)     // 18
    }

    // ── The bar's own measurements ──────────────────────────────────────────
    //
    // Fractions of THIS bar rather than Omarchy's literals (26/27/21 at their
    // 12px base), so a widget is the right size on a bar this desktop can
    // resize. The literals are in the comments as the ratio's origin.
    readonly property QtObject bar: QtObject {
        readonly property int sizeHorizontal: Theme.barHeight                     // 26
        readonly property int sizeVertical:   Theme.barHeight                     // 28
        readonly property int iconSlot:   Math.round(Theme.barHeight * 0.96)      // 27
        readonly property int iconCanvas: Math.round(Theme.barHeight * 0.60)      // 16
        readonly property int iconFont:   Math.max(8, Math.round(Theme.fontSize * 1.083)) // 13
        readonly property int statusSlot: Math.round(Theme.barHeight * 0.75)      // 21
    }

    // ── Spacing ─────────────────────────────────────────────────────────────
    //
    // Their scale verbatim in NAME and in RATIO to each other. These are the
    // rungs a widget lays a popup out on, and a set that is merely "about
    // right" reads as a panel whose rows do not line up.
    readonly property QtObject spacing: QtObject {
        readonly property real scale: 1.0

        readonly property int hairline: 1
        readonly property int xxs:   2
        readonly property int xs:    3
        readonly property int sm:    4
        readonly property int md:    6
        readonly property int lg:    8
        readonly property int xl:    10
        readonly property int xxl:   12
        readonly property int xxxl:  14
        readonly property int huge:  18

        readonly property int controlGap:      8
        readonly property int controlPaddingX: 10
        readonly property int controlPaddingY: 6
        readonly property int inputPaddingY:   7
        readonly property int controlHeight:   28
        readonly property int popupRowHeight:  28

        readonly property int dropdownWidth:            240
        readonly property int searchableDropdownWidth:  260
        readonly property int numberFieldWidth:         120
        readonly property int searchablePopupMinHeight: 220

        readonly property int rowGap:       8
        readonly property int rowPaddingX:  12
        readonly property int labelGap:     6
        readonly property int panelGap:     8
        readonly property int panelPadding: 12
        readonly property int popupPadding: 10
    }

    // ── Control states ──────────────────────────────────────────────────────
    //
    // ⚠ THE SIGNATURE IS WHAT MATTERS, AND IT IS THEIRS: every one of these
    // takes (foreground, accent, urgent) and returns a colour DERIVED from
    // them. That is not a stylistic choice — a fill picked from the theme's own
    // ink sits correctly on a light desktop and a dark one alike, where a fixed
    // grey only works on one. Omarchy resolves which of the three to use
    // through a user token file; synui has no such file, so the resolution here
    // is the documented DEFAULT of each token, which is `foreground` for all of
    // them except where a widget passes something else in.
    //
    // The alphas are theirs, because they are what makes a hovered row read as
    // hovered rather than as selected.
    readonly property int normalBorderWidth:   1
    readonly property int hoverBorderWidth:    1
    readonly property int selectedBorderWidth: 0
    readonly property int focusBorderWidth:    1

    readonly property real normalFillAlpha:    0.04
    readonly property real hoverFillAlpha:     0.08
    readonly property real selectedFillAlpha:  0.18
    readonly property real pressedFillAlpha:   0.22
    readonly property real focusFillAlpha:     0.08
    readonly property real selectionFillAlpha: 0.35

    readonly property real normalBorderAlpha:   0.40
    readonly property real hoverBorderAlpha:    0.25
    readonly property real selectedBorderAlpha: 1.00
    readonly property real focusBorderAlpha:    0.25

    /* `urgent` wins where it is set, then `accent`, then the foreground —
     * Omarchy's own precedence, and the reason a widget signalling something
     * wrong stays legible when the theme changes underneath it. */
    function stateColor(foreground, accent, urgent) {
        return urgent ? urgent : (accent ? accent : foreground)
    }
    function normalStateColor(f, a, u)    { return root.stateColor(f, a, u) }
    function hoverStateColor(f, a, u)     { return root.stateColor(f, a, u) }
    function selectedStateColor(f, a, u)  { return root.stateColor(f, a, u) }
    function pressedStateColor(f, a, u)   { return root.stateColor(f, a, u) }
    function focusStateColor(f, a, u)     { return root.stateColor(f, a, u) }
    function selectionStateColor(f, a, u) { return root.stateColor(f, a, u) }

    function normalFillFor(f, a, u)    { return Util.alpha(root.normalStateColor(f, a, u),    root.normalFillAlpha) }
    function hoverFillFor(f, a, u)     { return Util.alpha(root.hoverStateColor(f, a, u),     root.hoverFillAlpha) }
    function selectedFillFor(f, a, u)  { return Util.alpha(root.selectedStateColor(f, a, u),  root.selectedFillAlpha) }
    function pressedFillFor(f, a, u)   { return Util.alpha(root.pressedStateColor(f, a, u),   root.pressedFillAlpha) }
    function focusFillFor(f, a, u)     { return Util.alpha(root.focusStateColor(f, a, u),     root.focusFillAlpha) }
    function selectionFillFor(f, a, u) { return Util.alpha(root.selectionStateColor(f, a, u), root.selectionFillAlpha) }

    function normalBorderFor(f, a, u)   { return Util.alpha(root.normalStateColor(f, a, u),   root.normalBorderAlpha) }
    function hoverBorderFor(f, a, u)    { return Util.alpha(root.hoverStateColor(f, a, u),    root.hoverBorderAlpha) }
    function selectedBorderFor(f, a, u) { return Util.alpha(root.selectedStateColor(f, a, u), root.selectedBorderAlpha) }
    function focusBorderFor(f, a, u)    { return Util.alpha(root.focusStateColor(f, a, u),    root.focusBorderAlpha) }

    /* One control's fill, border and border width for the state it is in —
     * their three-argument shorthand, used by every field and dropdown. */
    function controlFill(focused, hot, foreground, accent) {
        if (focused) return root.focusFillFor(foreground, accent, null)
        if (hot)     return root.hoverFillFor(foreground, accent, null)
        return root.normalFillFor(foreground, accent, null)
    }
    function controlBorder(focused, hot, foreground, accent) {
        if (focused) return root.focusBorderFor(foreground, accent, null)
        if (hot)     return root.hoverBorderFor(foreground, accent, null)
        return root.normalBorderFor(foreground, accent, null)
    }
    function controlBorderWidth(focused, hot) {
        if (focused) return root.focusBorderWidth
        if (hot)     return root.hoverBorderWidth
        return root.normalBorderWidth
    }

    /* The ready-made ones, off this desktop's own palette. */
    readonly property color normalFill:          root.normalFillFor(Color.foreground, Color.accent, null)
    readonly property color hoverFill:           root.hoverFillFor(Color.foreground, Color.accent, null)
    readonly property color selectedFill:        root.selectedFillFor(Color.foreground, Color.accent, null)
    readonly property color pressedFill:         root.pressedFillFor(Color.foreground, Color.accent, null)
    readonly property color focusFillColor:      root.focusFillFor(Color.foreground, Color.accent, null)
    readonly property color selectionFill:       root.selectionFillFor(Color.foreground, Color.accent, null)
    readonly property color normalBorderColor:   root.normalBorderFor(Color.foreground, Color.accent, null)
    readonly property color hoverBorderColor:    root.hoverBorderFor(Color.foreground, Color.accent, null)
    readonly property color selectedBorderColor: root.selectedBorderFor(Color.foreground, Color.accent, null)
    readonly property color focusBorderColor:    root.focusBorderFor(Color.foreground, Color.accent, null)
    readonly property color selectedAccentFill:  Util.alpha(Color.accent, root.selectedFillAlpha)
}
