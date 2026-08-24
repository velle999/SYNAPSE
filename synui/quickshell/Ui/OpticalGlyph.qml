import QtQuick
import qs.Commons

/*
 * OpticalGlyph — one icon glyph, centred on what is PAINTED rather than on the
 * box the font reserves for it.
 *
 * ⚠ THIS IS NOT A WRAPPER AROUND Text AND THE DIFFERENCE IS THE WHOLE POINT. A
 * Nerd Font icon carries wildly uneven side bearings — the same nominal advance
 * width around glyphs whose ink sits anywhere inside it — so a row of icons
 * anchored by their text boxes comes out visibly ragged even though every box
 * is perfectly aligned. Correcting by the tight bounding rect is what makes a
 * bar of mixed icons read as a row.
 *
 * ⚠ HORIZONTALLY ONLY, WHICH IS DELIBERATE. Nudging the vertical too would move
 * each glyph off the shared baseline and every icon would sit at its own
 * height — the ragged edge, rotated ninety degrees. The line box and the
 * baseline stay exactly where the font put them.
 *
 * Reimplemented from Omarchy's behaviour, like everything else in this module.
 */
Item {
    id: root

    property string text: ""
    property string fontFamily: Style.font.family
    property real   fontSize: Style.font.body
    property color  color: Color.foreground
    /* Their debug overlay, kept because a widget can switch it on and would
     * otherwise fail on the property. It draws nothing unless asked. */
    property bool   debugBounds: false

    readonly property int  renderedFontSize: Math.max(1, Math.round(root.fontSize))
    readonly property real tightWidth: Math.max(1, metrics.tightBoundingRect.width)
    /* The offset that moves the painted ink to the middle of this item: half the
     * reserved box, minus the middle of what is actually drawn. */
    readonly property real horizontalCorrection:
        glyph.implicitWidth / 2 - (metrics.tightBoundingRect.x + root.tightWidth / 2)
    readonly property real paintedCenterX:
        glyph.x + metrics.tightBoundingRect.x + root.tightWidth / 2
    readonly property real baselineY: glyph.y + glyph.baselineOffset

    TextMetrics {
        id: metrics
        font.family: root.fontFamily
        font.pixelSize: root.renderedFontSize
        text: root.text
    }

    Text {
        id: glyph
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: root.horizontalCorrection
        text: root.text
        color: root.color
        font.family: root.fontFamily
        font.pixelSize: root.renderedFontSize
        /* NativeRendering: these are one- and two-pixel stems at bar sizes,
         * where the distance-field renderer smears them. */
        renderType: Text.NativeRendering
    }

    Rectangle {
        visible: root.debugBounds
        anchors.fill: parent
        color: "transparent"
        border { width: 1; color: "#4488ff" }
    }
    Rectangle {
        visible: root.debugBounds
        x: 0
        y: Math.round(root.baselineY)
        width: parent.width
        height: 1
        color: "#44ff88"
    }
}
