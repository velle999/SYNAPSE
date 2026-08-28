import QtQuick
import QtQuick.Controls.Basic

/*
 * SynScrollBar — the scrollbar every view in the Antiquity shell uses.
 *
 * ⛔ A VIEW THAT SCROLLS SHOWS THAT IT SCROLLS. Without a bar there is nothing
 * on screen saying there is anything past the edge of the view, nothing saying
 * how much, and no way to cross a long list in one gesture. velle, 2026-08-28:
 * "you keep making windows without scrollbars and thats dumb."
 *
 * ⚠ VISIBLE AT REST. Qt's default fades the handle out unless `active` — true
 * while the view is moving or the bar is hovered, and false in exactly the
 * state where somebody is deciding whether there is more to see.
 *
 * ⚠ A SECOND COPY, AND DELIBERATELY. This tree draws from Config.colors and
 * synui/quickshell draws from Theme; they are two shells with two palettes and
 * no shared module between them, so a component here cannot read that one's
 * colours. The palettes are already duplicated this way — the bar follows the
 * palette it is drawn against.
 *
 * ⚠ Controls.Basic, matching every consumer in this tree: mixing the default
 * style in would pull a second Controls style into the same process.
 *
 * Used as `ScrollBar.vertical: SynScrollBar {}`.
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
    padding: 2
    implicitWidth:  sb.vert ? 11 : 48
    implicitHeight: sb.vert ? 48 : 11

    contentItem: Rectangle {
        implicitWidth:  sb.vert ? 7 : 32
        implicitHeight: sb.vert ? 32 : 7
        radius: Math.min(width, height) / 2
        color: sb.pressed ? Config.colors.accent
             : sb.hovered ? Config.colors.textLight
                          : Config.colors.outline
        opacity: sb.pressed || sb.hovered ? 1.0 : 0.6
        Behavior on color   { ColorAnimation  { duration: 90 } }
        Behavior on opacity { NumberAnimation { duration: 90 } }
    }

    background: Rectangle {
        radius: Math.min(width, height) / 2
        color: Config.colors.highlight
        opacity: sb.hovered || sb.pressed ? 0.35 : 0.0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
