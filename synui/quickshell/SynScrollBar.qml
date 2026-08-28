import QtQuick
import QtQuick.Controls

/*
 * SynScrollBar — the scrollbar every view in this tree uses.
 *
 * ⛔ A VIEW THAT SCROLLS SHOWS THAT IT SCROLLS. A wheel is not a substitute:
 * without a bar there is nothing on screen saying there is anything past the
 * edge of the view, nothing saying how much, nothing saying where in it you
 * are, and no way to cross a long list in one gesture. velle, 2026-08-28:
 * "you keep making windows without scrollbars and thats dumb."
 *
 * ⚠ VISIBLE AT REST, which is the whole reason this type exists rather than a
 * bare ScrollBar. Qt's default fades the handle out unless `active` — true
 * while the view is moving or the bar is hovered, and false in exactly the
 * state where somebody is deciding whether there is more to see. A bar nobody
 * can see is the bug, not the fix.
 *
 * ⚠ AsNeeded, so a view shorter than its window draws no furniture. The rule
 * is that scrolling is visible, not that every view wears a bar.
 *
 * Used as `ScrollBar.vertical: SynScrollBar {}` — which needs
 * `import QtQuick.Controls` in the consumer too, for the attached property.
 * Pinned by preflight's `scrollbar` gate.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
ScrollBar {
    id: sb

    // ⚠ ORIENTATION-AWARE. Attached as `ScrollBar.horizontal` this is a bar
    // that must be SHORT and WIDE; the vertical-only version drew a 7px-wide
    // handle lying on its side across a game shelf.
    readonly property bool vert: sb.orientation === Qt.Vertical

    policy: ScrollBar.AsNeeded
    /*
     * ⛔ NOTHING TO SCROLL MEANS NO SCROLLBAR AT ALL. AsNeeded hides the
     * handle by fading its OPACITY, and a custom contentItem replaces the
     * binding that does it — so a bar styled to be visible at rest became
     * visible at rest everywhere, a full-length handle that cannot move
     * sitting on every short list on the desktop. velle, 2026-08-28:
     * "if there's nothing to scroll the scrollbar should autohide. i don't
     * need the fucking scrollbars literally everywhere when they can't even
     * do anything."
     *
     * `size` is the fraction of the content the view can show: 1.0 means it
     * all fits. Visible at rest is for the case where there IS more — that
     * is the whole point of it — and is clutter in every other case.
     */
    readonly property bool needed:
        sb.policy === ScrollBar.AlwaysOn ||
        (sb.policy === ScrollBar.AsNeeded && sb.size < 1.0)
    visible: sb.needed
    padding: 2
    implicitWidth:  sb.vert ? 11 : 48
    implicitHeight: sb.vert ? 48 : 11

    contentItem: Rectangle {
        implicitWidth:  sb.vert ? 7 : 32
        implicitHeight: sb.vert ? 32 : 7
        radius: Math.min(width, height) / 2
        color: sb.pressed ? Theme.magenta : sb.hovered ? Theme.fg : Theme.fgDim
        opacity: sb.pressed || sb.hovered ? 1.0 : 0.5
        Behavior on color   { ColorAnimation  { duration: 90 } }
        Behavior on opacity { NumberAnimation { duration: 90 } }
    }

    background: Rectangle {
        radius: Math.min(width, height) / 2
        color: Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.08)
        opacity: sb.hovered || sb.pressed ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
