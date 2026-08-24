import QtQuick
import qs.Commons

/*
 * PanelSectionHeader — the small bold label that introduces a panel section.
 *
 * ⚠ THE TOP PADDING IS NOT DECORATION. A Nerd Font's outlines run past the
 * ascent the font declares — JetBrainsMono's by about a tenth of an em, a
 * patched or user-chosen family by more. In normal flow that sliver overlaps
 * whatever is above and nobody notices; at the top of a CLIPPING list it is
 * outside the clip, and the header renders with its tops shaved off. Reserving
 * the overshoot here covers every panel at once instead of each one finding out.
 */
Text {
    id: root

    property color  foreground: Color.foreground
    property string fontFamily: Style.font.family
    property real   fontSize: Style.font.caption

    color: Qt.darker(root.foreground, 1.4)
    font.family: root.fontFamily
    font.pixelSize: root.fontSize
    font.bold: true

    topPadding: Math.ceil(root.fontSize * 0.15)
}
