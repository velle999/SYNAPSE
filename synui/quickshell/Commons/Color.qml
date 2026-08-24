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

    /*
     * ── The rest of the surface, MEASURED ───────────────────────────────────
     *
     * ⛔ A COLOUR THAT IS NOT HERE IS `undefined`, AND `undefined` PAINTS BLACK.
     * That is worse than a missing type, which at least fails loudly: a panel
     * whose text colour resolved to nothing is a panel of black text on a black
     * card, and nothing in the log says so. So, as with Style, these are counted
     * off the real corpus rather than chosen —
     *
     *     grep -rhoE '\bColor\.[A-Za-z0-9_.]+' <widgets>/*.qml | sort | uniq -c
     *
     * over 40 of the most-installed community widgets. `accent` leads at 192
     * uses, then popups.text, urgent, foreground, muted, background.
     */

    /* Text that is present but secondary — a timestamp beside a title, a unit
     * after a number. synui's own dimmed bar ink, which is the same intent. */
    readonly property color muted: Theme.barDim

    /* ⚠ `text` IS NOT `foreground`. A widget uses `foreground` for ink ON THE
     * BAR and `text` for ink on a surface of its own, and on a clear bar those
     * are genuinely different colours — foreground is measured off the
     * wallpaper, text is the theme's. Aliasing them would put wallpaper-derived
     * ink on a popup that has its own background. */
    readonly property color text:   Theme.fg
    readonly property color border: Theme.barDim

    /*
     * The bar as a widget sees it from outside.
     *
     * ⚠ THE FOLDED ANSWER AGAIN — see the note at the top. A widget drawing its
     * own strip-coloured chrome gets the desktop-wide background here; the exact
     * per-monitor one is on `bar` for anything rooted at BarWidget.
     */
    readonly property QtObject bar: QtObject {
        readonly property color background: Theme.bg
        readonly property color foreground: Theme.barFg
        readonly property color accent:     Theme.barAccent
    }

    readonly property QtObject popups: QtObject {
        /* A popup is its own surface rather than part of the strip, so it takes
         * the popup background and the theme's own ink — the clear-bar
         * substitution above is about the bar and does not apply to a card
         * floating over the desktop. */
        readonly property color background: Theme.popupBg
        readonly property color text:       Theme.fg
        readonly property color border:     Theme.magenta
    }

    /* A tooltip is a popup that has to stay legible over anything, so it takes
     * the popup surface rather than the bar's — the same card, smaller. */
    readonly property QtObject tooltip: QtObject {
        readonly property color background: Theme.popupBg
        readonly property color text:       Theme.fg
        readonly property color border:     Theme.magenta
    }

    /* Omarchy's summoned command menu is its own surface with its own tokens.
     * synui has no such menu, so these are the popup answers — the same kind of
     * surface, and a widget asking is asking "what does a floating card look
     * like here". */
    readonly property QtObject menu: QtObject {
        readonly property color background: Theme.popupBg
        readonly property color text:       Theme.fg
        readonly property color border:     Theme.magenta

        /*
         * What a full-screen surface dims the desktop with behind itself.
         *
         * ⚠ MISSING IS NOT A FALLBACK, IT IS BLACK. flappy-pipes reads
         * `Color.menu.scrim` for the wash behind its playfield; an absent
         * member is `undefined`, assigning undefined to a `color` leaves the
         * default, and the default is opaque black — so the one member nobody
         * had asked for yet turned a translucent dim into a blackout, with a
         * "Unable to assign [undefined] to QColor" line in a log on tty1 as
         * the only notice. A missing PROPERTY is silent where a missing TYPE is
         * loud, which is the whole reason this module is built from a count
         * over the real corpus rather than to taste (tools/plugin-compat.sh).
         *
         * Derived from the popup background rather than named as a colour of
         * its own: a scrim is the surface beneath, held back, and a theme that
         * moves its cards moves this with them.
         */
        readonly property color scrim: Qt.rgba(Theme.popupBg.r, Theme.popupBg.g,
                                               Theme.popupBg.b, 0.62)
    }

    /*
     * A colour laid OVER another and flattened.
     *
     * ⚠ IT HAS TO RETURN AN OPAQUE COLOUR. Widgets use it to work out what a
     * translucent fill will actually look like on the surface beneath — for
     * picking readable text over it — so returning something with alpha in it
     * would defeat the one thing it is for.
     */
    function composed(over, under, amount) {
        const a = Util.clampAlpha(amount === undefined ? Qt.color(over).a : amount)
        const o = Qt.color(over)
        const u = Qt.color(under)
        return Qt.rgba(o.r * a + u.r * (1 - a),
                       o.g * a + u.g * (1 - a),
                       o.b * a + u.b * (1 - a), 1)
    }
}
