import QtQuick
import qs.Commons

/*
 * BorderSurface — a Rectangle that takes a border SPEC instead of a colour and
 * a width, and tells its content how far in to sit.
 *
 * The insets are the reason it is not just a Rectangle. A control with a border
 * and padding has to place its label inside both, and doing that arithmetic at
 * every call site is how a panel ends up with rows whose text is a pixel out
 * from each other. Ask the surface.
 *
 * Native `Rectangle.border` for the uniform case, which is nearly all of them;
 * BorderOverlay only when the four sides differ.
 */
Rectangle {
    id: root

    property var  borderSpec: Border.none()
    property real padding: 0
    property real topPadding:    root.padding
    property real rightPadding:  root.padding
    property real bottomPadding: root.padding
    property real leftPadding:   root.padding

    readonly property real borderTop:    Border.top(root.borderSpec)
    readonly property real borderRight:  Border.right(root.borderSpec)
    readonly property real borderBottom: Border.bottom(root.borderSpec)
    readonly property real borderLeft:   Border.left(root.borderSpec)

    readonly property real contentTopInset:    root.borderTop + root.topPadding
    readonly property real contentRightInset:  root.borderRight + root.rightPadding
    readonly property real contentBottomInset: root.borderBottom + root.bottomPadding
    readonly property real contentLeftInset:   root.borderLeft + root.leftPadding

    readonly property bool usesOverlayBorder: Border.needsOverlay(root.borderSpec)

    border.color: Border.canUseNative(root.borderSpec) ? Border.color(root.borderSpec)
                                                       : "transparent"
    border.width: Border.canUseNative(root.borderSpec) ? Border.uniformWidth(root.borderSpec) : 0

    Loader {
        anchors.fill: parent
        active: root.usesOverlayBorder
        sourceComponent: BorderOverlay {
            anchors.fill: parent
            radius: root.radius
            borderSpec: root.borderSpec
        }
    }
}
