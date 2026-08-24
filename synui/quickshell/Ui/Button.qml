import QtQuick
import QtQuick.Controls
import qs.Commons

/*
 * Button — one component for every clickable thing in a panel.
 *
 * The states compose independently and paint in a fixed priority, which is the
 * part worth knowing:
 *
 *     pressed              pressed fill
 *     activeFocus          focus fill + focus border
 *     hasCursor || hover   hover-cursor fill (+ border when `bordered`)
 *     selected             selected fill
 *     active               selected fill
 *     idle                 `background`, or a normal border when `bordered`
 *
 * ⛔ THE BORDER IS RESERVED IN THE IMPLICIT SIZE EVEN WHEN NOTHING IS DRAWN,
 * and that is the one subtle thing in this file. A button that is borderless at
 * rest and gains a one-pixel border on hover grows by two pixels — so every
 * control beside it in the row shifts as the pointer crosses it, and a row of
 * buttons ripples. Reserving the largest border any state COULD paint costs a
 * pixel of padding and buys a row that holds still.
 *
 * `hovered(bool)` is emitted so a panel with its own keyboard cursor can move
 * that cursor to follow the pointer — see CursorSurface for why the row must
 * not simply colour itself.
 */
BorderSurface {
    id: root

    property string text: ""
    property string iconText: ""
    property string tooltipText: ""

    property bool selected: false
    property bool active: false
    property bool hasCursor: false
    property bool focusable: false
    property bool bordered: false

    property color foreground: Color.foreground
    property color background: "transparent"
    property color accent: Color.accent

    property string fontFamily: Style.font.family
    property real fontSize: Style.font.body
    property real iconSize: Style.font.icon
    property real iconRotation: 0
    property bool iconSpinning: false
    property real horizontalPadding: Style.spacing.controlPaddingX
    property real verticalPadding: Style.spacing.controlPaddingY
    property bool leftAlign: false

    property color tooltipBackground: Color.tooltip.background
    property color tooltipForeground: Color.tooltip.text
    property color tooltipBorder: Color.tooltip.border

    signal clicked()
    signal rightClicked()
    signal hovered(bool isHovered)

    leftPadding:   root.horizontalPadding
    rightPadding:  root.horizontalPadding
    topPadding:    root.verticalPadding
    bottomPadding: root.verticalPadding

    activeFocusOnTab: root.focusable
    Keys.onReturnPressed: if (root.focusable) root.clicked()
    Keys.onEnterPressed:  if (root.focusable) root.clicked()
    Keys.onSpacePressed:  if (root.focusable) root.clicked()

    readonly property bool hot: mouseArea.containsMouse || root.hasCursor
    readonly property bool showFocusRing: root.focusable && root.activeFocus
    readonly property color selectedColor:
        Style.selectedStateColor(root.foreground, root.accent, null)

    readonly property var focusBorderSpec:    Border.controlSpec("focus", root.foreground, root.accent, null)
    readonly property var hoverBorderSpec:    Border.controlSpec("hover-cursor", root.foreground, root.accent, null)
    readonly property var selectedBorderSpec: Border.controlSpec("selected", root.foreground, root.accent, null)
    readonly property var normalBorderSpec:   Border.controlSpec("normal", root.foreground, root.accent, null)
    readonly property var tooltipBorderSpec:
        Border.localOrSurfaceSpec("tooltip", "border", root.tooltipBorder,
                                  Color.tooltip.border, Math.max(1, Style.normalBorderWidth))

    /* See the note at the top: the widest border any state can paint, reserved
     * so none of them changes the button's size. */
    readonly property real reservedBorder: Math.max(
        root.focusable ? Border.maxWidth(root.focusBorderSpec) : 0,
        Border.maxWidth(root.hoverBorderSpec),
        Border.maxWidth(root.selectedBorderSpec),
        root.bordered ? Border.maxWidth(root.normalBorderSpec) : 0)

    implicitWidth:  row.implicitWidth  + root.horizontalPadding * 2 + root.reservedBorder * 2
    implicitHeight: row.implicitHeight + root.verticalPadding * 2   + root.reservedBorder * 2
    radius: Style.cornerRadius

    borderSpec: root.showFocusRing ? root.focusBorderSpec
              : root.hot           ? root.hoverBorderSpec
              : root.selected      ? (Border.controlHasWidth("selected")
                                      ? root.selectedBorderSpec
                                      : (root.bordered ? root.normalBorderSpec : Border.none()))
              : root.bordered      ? root.normalBorderSpec
                                   : Border.none()

    color: mouseArea.pressed  ? Style.pressedFillFor(root.foreground, root.accent, null)
         : root.showFocusRing ? Style.focusFillFor(root.foreground, root.accent, null)
         : root.hot           ? Style.hoverFillFor(root.foreground, root.accent, null)
         : root.selected      ? Style.selectedFillFor(root.foreground, root.accent, null)
         : root.active        ? Style.selectedFillFor(root.foreground, root.accent, null)
                              : root.background

    Behavior on color { ColorAnimation { duration: 120 } }

    ToolTip {
        visible: root.tooltipText !== "" && mouseArea.containsMouse
        text: root.tooltipText
        delay: 400
        padding: 0
        background: BorderSurface {
            color: root.tooltipBackground
            borderSpec: root.tooltipBorderSpec
            radius: 0
        }
        contentItem: Text {
            text: root.tooltipText
            color: root.tooltipForeground
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            leftPadding:   Border.left(root.tooltipBorderSpec)   + Style.spacing.controlPaddingX
            rightPadding:  Border.right(root.tooltipBorderSpec)  + Style.spacing.controlPaddingX
            topPadding:    Border.top(root.tooltipBorderSpec)    + Style.spacing.controlPaddingY
            bottomPadding: Border.bottom(root.tooltipBorderSpec) + Style.spacing.controlPaddingY
        }
    }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        /* Centred, unless the caller wants a column of buttons whose labels
         * line up — a menu rather than a toolbar. */
        anchors.left: root.leftAlign ? parent.left : undefined
        anchors.leftMargin: root.leftAlign ? root.reservedBorder + root.leftPadding : 0
        anchors.horizontalCenter: root.leftAlign ? undefined : parent.horizontalCenter
        spacing: Style.spacing.controlGap

        Text {
            visible: root.iconText !== ""
            text: root.iconText
            color: root.selected ? root.selectedColor : root.foreground
            font.family: root.fontFamily
            font.pixelSize: root.iconSize
            /* ⚠ THE STATIC ROTATION IS DROPPED WHILE SPINNING. Both applied at
             * once and the animation would start from the caller's angle and
             * end 360° later at the same place, which looks like a stutter. */
            rotation: root.iconSpinning ? 0 : root.iconRotation
            transformOrigin: Item.Center
            anchors.verticalCenter: parent.verticalCenter

            RotationAnimation on rotation {
                from: 0; to: 360
                duration: 900
                loops: Animation.Infinite
                running: root.iconSpinning
            }
        }

        Text {
            visible: root.text !== ""
            text: root.text
            color: root.selected ? root.selectedColor : root.foreground
            font.family: root.fontFamily
            font.pixelSize: root.fontSize
            font.bold: root.selected
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (root.focusable) root.forceActiveFocus()
            if (mouse.button === Qt.RightButton) root.rightClicked()
            else root.clicked()
        }
    }

    /* ⚠ A HoverHandler AND a hoverEnabled MouseArea, both. The MouseArea's
     * containsMouse drives the paint; the handler is what emits the signal,
     * because a MouseArea's entered/exited do not fire while a press is being
     * held elsewhere and a panel cursor would stick. */
    HoverHandler {
        id: hoverHandler
        onHoveredChanged: root.hovered(hoverHandler.hovered)
    }
}
