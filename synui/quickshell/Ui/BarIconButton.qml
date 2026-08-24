import QtQuick
import qs.Commons

/*
 * BarIconButton — a WidgetButton that shows an ICON rather than a label.
 *
 * ⛔ IT IS THE SINGLE MOST-NEEDED TYPE IN THE WHOLE MODULE. Probed across 40 of
 * the most-installed community bar widgets, 21 of them name it — more than half
 * — and the failure it causes is the worst-looking one there is. A widget
 * typically writes
 *
 *     implicitWidth: button.implicitWidth
 *
 * against its BarIconButton, so when the type is missing the binding resolves to
 * `undefined`, the widget's implicit width is 0, and the bar lays out something
 * with no size. It is loaded. It is running. It is on. And there is nothing on
 * screen — which is exactly what "I installed a few and they all fail to work"
 * looks like from the outside.
 *
 * ── What it is ──────────────────────────────────────────────────────────────
 *
 * A square slot with a glyph optically centred in it — see OpticalGlyph for why
 * that is not the same as centring the text. The label WidgetButton would have
 * drawn is switched off and the glyph takes its place, so everything else about
 * the button (the press and wheel signals, the hover cursor, the tooltip hook,
 * dimming, concealment) is inherited unchanged.
 *
 * `iconComponent` is the escape hatch for a widget that wants to draw its own
 * icon — an Image, a Canvas, a shape — instead of a font glyph. When it is set
 * the glyph hides and the component fills the same canvas, so a widget mixing
 * the two still gets one consistent row.
 */
WidgetButton {
    id: root

    /* Draw this instead of the glyph. Null means "use `text` as a glyph". */
    property Component iconComponent: null
    /* The square the button occupies in the bar, and the smaller square the
     * icon is drawn inside it. Two sizes rather than one: the slot is the
     * click target and the rhythm of the row, the canvas is how big the icon
     * looks, and tying them together makes every icon either cramped or huge. */
    property real slotSize:    Style.bar.iconSlot
    property real opticalSize: Style.bar.iconCanvas

    /* What the optical centring worked out, for chrome that wants to line up
     * with the ink — an underline under the glyph rather than under the slot. */
    readonly property real opticalCenterErrorX:
        glyph.visible ? glyph.paintedCenterX - canvas.width / 2 : 0
    readonly property real glyphPaintedWidth: glyph.visible ? glyph.tightWidth : 0
    readonly property real glyphBaselineY:    glyph.visible ? glyph.baselineY : 0
    readonly property int  glyphFontSize:     glyph.visible ? glyph.renderedFontSize : 0

    labelVisible: false
    /* ⚠ AN ICON BUTTON IS "EMPTY" ONLY IF IT HAS NEITHER. The inherited default
     * asks about `text` alone, which would hide a button drawing a component. */
    hasVisualContent: root.text !== "" || root.iconComponent !== null
    fontSize: Style.bar.iconFont
    /* Square along the bar's long axis and full-thickness across it, whichever
     * way the bar runs. -1 is WidgetButton's "derive it from the content". */
    fixedWidth:  root.vertical ? -1 : root.slotSize
    fixedHeight: root.vertical ? root.slotSize : -1

    Item {
        id: canvas
        anchors.centerIn: parent
        width: root.opticalSize
        height: root.opticalSize

        OpticalGlyph {
            id: glyph
            anchors.fill: parent
            visible: root.iconComponent === null
            text: root.text
            fontFamily: root.fontFamily
            fontSize: root.fontSize
            color: root.active && root.useActiveColor ? root.activeColor : root.foreground
            rotation: root.textRotation
        }

        Loader {
            anchors.fill: parent
            visible: root.iconComponent !== null
            sourceComponent: root.iconComponent
        }
    }
}
