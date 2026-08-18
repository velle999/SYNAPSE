import QtQuick
import Quickshell
import ".."

/*
 * TUXAGOTCHI — a virtual pet on the desktop, and the only widget here that
 * needs something from you.
 *
 * Everything else on this desktop reports (the clock, the meters, the
 * spectrum), holds what you wrote (the note), or opens something (the pizza).
 * This one is looked after: it gets hungry on a clock you are not watching, it
 * makes a mess, it gets ill if the mess is left, and it beeps until somebody
 * does something about it.
 *
 * This file is only the join. Three pieces do the work, and the split is not
 * tidiness — it is what makes a widget that is almost entirely a PICTURE
 * something anyone can look at before shipping it:
 *
 *   TuxState   the pet. A singleton, because there is one pet however many
 *              monitors are plugged in, and Variants builds one of these
 *              windows per screen.
 *   TuxShell   the toy — the screen and the eight icons round it. Plain
 *              QtQuick: no Theme, no Quickshell, no singleton.
 *   TuxScreen  the LCD inside it, on the same terms.
 *
 * So tests/tux_screen.sh renders every mood the pet has, buttons and all, with
 * the `qml` tool and no compositor at all. What is left here is what genuinely
 * belongs to the desktop: where the card sits, what colour the theme is, and
 * the fact that the pet is a singleton.
 *
 * `interactive`, with the price QuickLaunch, PostIt and Pizza already pay: the
 * desktop's right-click menu cannot be reached through the card.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
WidgetFrame {
    id: root

    widgetId: "tux"
    shown: WidgetState.tux
    label: "TUXAGOTCHI"
    accent: Theme.cyan
    interactive: true

    /*
     * Under the system monitor on the right-hand edge, on the same terms the
     * pizza sits above the clock: a widget's home is expressed relative to
     * whatever else already claims that corner, so switching two on does not
     * stack them. Only until somebody drags it — WidgetFrame stops applying
     * home margins the moment there is a stored position.
     */
    readonly property int sysmonClearance: 124

    homeEdgeH: "right"; homeEdgeV: "top"
    homeMarginX: 18
    homeMarginY: Theme.barHeight + 18 + (WidgetState.sysmon ? sysmonClearance : 0)

    cardWidth: 232
    bodyHeight: toy.implicitHeight

    /*
     * The glass the pet lives behind. A real one is a grey-green film that owes
     * its colour to the polariser, and copying that exactly would put a fixed
     * olive rectangle on every theme SynapseOS has. So the LCD is a dark pane
     * on a dark desktop and a pale one on a light desktop — and Tux stays
     * black, white and orange on top of it, because he is a logo and not an
     * accent (see tuxart.js).
     */
    readonly property color lcdBg: Theme.isLight
        ? Qt.rgba(0.79, 0.84, 0.75, 1.0)
        : Qt.rgba(0.09, 0.13, 0.12, 1.0)
    readonly property color lcdInk: Theme.isLight ? "#2b3a2f" : "#d6e6da"

    TuxShell {
        id: toy
        anchors.fill: parent

        pet: TuxState

        // The timer stops on every screen but the one showing this. `visible`
        // is WidgetFrame's own "on the primary output" answer, so the copies
        // Variants builds for the other monitors cost nothing.
        animate: root.visible

        lcdBg: root.lcdBg
        lcdInk: root.lcdInk
        accent: root.accent
        warn: Theme.red
        label: Theme.fgDim
        labelBright: Theme.fg
        isLight: Theme.isLight
        fontFamily: Theme.fontFamily
        iconFamily: Theme.iconFamily
        soundGlyph: Icons.volHigh
        muteGlyph: Icons.volMuted
    }
}
