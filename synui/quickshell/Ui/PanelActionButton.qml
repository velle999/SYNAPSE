import QtQuick
import qs.Commons

/*
 * PanelActionButton — the small icon button at the right-hand end of a panel
 * row. Forget this network, unpair this device, confirm this passphrase.
 *
 * ⛔ IT OWNS ITS OWN HOVER, AND THAT IS THE EXCEPTION TO CursorSurface'S RULE.
 * Everything else in a panel derives its look from the panel's single cursor;
 * an action button does not, because it is not a cursor target — the ROW it
 * sits in is. Two things would light up otherwise: the row under the pointer
 * and the button under the pointer, and neither would tell you what Enter does.
 *
 * `hasCursor` is the way back in for the case where the button IS the target,
 * and `hovered(bool)` lets the panel move its own cursor to match the pointer.
 *
 * ⚠ `hoverColor` IS THE WHOLE VISUAL API. Left at the foreground it hovers in
 * the panel's own ink; set to the urgent colour it hovers red, which is how a
 * destructive action announces itself before it is pressed rather than after.
 */
BorderSurface {
    id: root

    property string iconText: ""
    property string tooltipText: ""
    property color  foreground: Color.foreground
    property color  hoverColor: root.foreground
    property string fontFamily: Style.font.family
    property real   fontSize: Style.font.icon
    /* Never smaller than the glyph plus a little air, whatever the caller asks
     * for — a 22px default that a large text scale would otherwise clip. */
    property real   size: Math.max(Style.space(22), root.fontSize + Style.spacing.sm * 2)

    property bool focusable: false
    property bool hasCursor: false
    property bool bordered: false

    signal clicked()
    signal hovered(bool isHovered)

    /* Tabbable only where a form wants it. On a panel row, Tab belongs to the
     * list, not to each row's buttons. */
    activeFocusOnTab: root.focusable
    Keys.onReturnPressed: if (root.focusable) root.clicked()
    Keys.onEnterPressed:  if (root.focusable) root.clicked()
    Keys.onSpacePressed:  if (root.focusable) root.clicked()

    implicitWidth: root.size
    implicitHeight: root.size
    radius: Style.cornerRadius

    readonly property bool showFocusRing: root.focusable && root.activeFocus
    readonly property bool hot: (mouse.containsMouse || root.hasCursor) && root.enabled

    color: root.showFocusRing ? Style.focusFillFor(root.hoverColor, root.hoverColor, null)
         : (root.hot         ? Style.hoverFillFor(root.hoverColor, root.hoverColor, null)
                             : "transparent")

    borderSpec: root.showFocusRing
        ? Border.controlSpec("focus", root.hoverColor, root.hoverColor, null)
        : ((root.hot && root.bordered)
           ? Border.controlSpec("hover-cursor", root.hoverColor, root.hoverColor, null)
           : (root.bordered
              ? Border.controlSpec("normal", root.foreground, Color.accent, null)
              : Border.none()))

    Behavior on color { ColorAnimation { duration: 60 } }

    Text {
        anchors.centerIn: parent
        text: root.iconText
        /* Disabled is a DARKENED foreground rather than an opacity, so the
         * button keeps its shape in the row instead of half-vanishing. */
        color: root.enabled ? (root.hot ? root.hoverColor : root.foreground)
                            : Qt.darker(root.foreground, 2.0)
        font.family: root.fontFamily
        font.pixelSize: root.fontSize
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled: root.enabled
        onContainsMouseChanged: root.hovered(mouse.containsMouse)
        onClicked: {
            if (root.focusable) root.forceActiveFocus()
            root.clicked()
        }
    }

    PanelToolTip {
        visible: root.tooltipText !== "" && mouse.containsMouse
        text: root.tooltipText
        fontFamily: root.fontFamily
    }
}
