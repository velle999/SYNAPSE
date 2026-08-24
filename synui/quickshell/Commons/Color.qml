pragma Singleton

import QtQuick
import ".."

/*
 * Color — the palette half of `qs.Commons`.
 *
 * ⚠ synui's, NOT Omarchy's, for the reason Style.qml gives at length: a widget
 * should look like part of the bar it is in.
 *
 * ⛔ AND THE INK IS PER-MONITOR AND PER-MODULE ON THIS DESKTOP, which theirs is
 * not. A clear bar takes its ink off the wallpaper underneath, so the right
 * answer differs between two screens and between two ends of one screen — see
 * Theme.barPaletteSpanOn. A singleton cannot express that: it has one value for
 * the whole desktop.
 *
 * So this is the FOLDED, desktop-wide answer, which is what Theme hands back
 * for a caller with no window to ask about. It is right everywhere the bar has
 * a background and approximate on a fully clear one — the honest trade for an
 * API whose shape has no room for the question. A widget that wants the exact
 * ink for its own strip should root at BarWidget and read `bar` instead.
 */
QtObject {
    /* ⚠ THE bar* PAIR, NOT fg/magenta. Theme.barFg and Theme.barAccent are the
     * FOLDED answers — they already resolve the clear-bar case to the ink
     * measured off the wallpaper, and fall back to the theme's own colours when
     * the strip has a background. Reading `fg` directly would hand a widget the
     * palette's colour on a clear bar, which is exactly the unreadable text the
     * ink measurement exists to prevent. */
    readonly property color foreground: Theme.barFg
    readonly property color accent:     Theme.barAccent
    readonly property color background: Theme.bg
    /* What a widget in an alarming state colours itself. synui's own modules
     * use `red` for exactly that — a CPU over 90%, a battery about to die — so a
     * plugin saying "urgent" lands on the same colour the bar already means it
     * with. */
    readonly property color urgent:     Theme.red

    readonly property QtObject popups: QtObject {
        /* A popup is its own surface rather than part of the strip, so it takes
         * the popup background and the theme's own ink — the clear-bar
         * substitution above is about the bar and does not apply to a card
         * floating over the desktop. */
        readonly property color background: Theme.popupBg
        readonly property color text:       Theme.fg
        readonly property color border:     Theme.magenta
    }
}
