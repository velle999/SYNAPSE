import QtQuick
import qs.Commons

/*
 * PanelSeparator — the one-pixel rule between panel sections.
 *
 * ⚠ AN ALPHA OF THE FOREGROUND, NOT A GREY. A fixed grey rule is invisible on
 * one theme and a hard line on another; taking the panel's own ink at 12%
 * lands correctly on both, and is the same trick every fill in Style uses.
 */
Rectangle {
    id: root

    property color foreground: Color.foreground
    property real  strength: 0.12

    /* Stretches to its parent when it has one — a separator is always the full
     * width of the section it divides — and keeps an implicit width for the
     * layouts that measure before placing. */
    width: parent ? parent.width : root.implicitWidth
    implicitWidth: 100
    implicitHeight: 1
    height: 1
    color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, root.strength)
}
