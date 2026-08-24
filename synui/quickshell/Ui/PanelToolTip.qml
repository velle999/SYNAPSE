import QtQuick
import QtQuick.Controls
import qs.Commons

/*
 * PanelToolTip — a themed ToolTip, declared inside the thing it describes.
 *
 * ⚠ THE PROPERTY NAMES ARE PREFIXED `panel*` AND THAT IS NOT A STYLE CHOICE.
 * ToolTip already has `background` and `font`; a property called `background`
 * here would shadow the one the control itself uses to draw, and the tooltip
 * would come out unstyled while every binding looked correct.
 */
ToolTip {
    id: root

    property color  panelForeground: Color.tooltip.text
    property color  panelBackground: Color.tooltip.background
    property color  panelBorder:     Color.tooltip.border
    property string fontFamily: Style.font.family
    property real   fontSize: Style.font.bodySmall

    readonly property var panelBorderSpec:
        Border.localOrSurfaceSpec("tooltip", "border", root.panelBorder,
                                  Color.tooltip.border, Style.normalBorderWidth)

    delay: 400
    /* Zero here and paid on the text instead, so the border sits tight against
     * the label rather than around a padded box inside a padded box. */
    padding: 0

    background: BorderSurface {
        color: root.panelBackground
        borderSpec: root.panelBorderSpec
        radius: Style.cornerRadius
    }

    contentItem: Text {
        text: root.text
        color: root.panelForeground
        font.family: root.fontFamily
        font.pixelSize: root.fontSize
        leftPadding:   Border.left(root.panelBorderSpec)   + Style.spacing.controlPaddingX
        rightPadding:  Border.right(root.panelBorderSpec)  + Style.spacing.controlPaddingX
        topPadding:    Border.top(root.panelBorderSpec)    + Style.spacing.controlPaddingY
        bottomPadding: Border.bottom(root.panelBorderSpec) + Style.spacing.controlPaddingY
    }
}
