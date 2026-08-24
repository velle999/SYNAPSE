import QtQuick
import qs.Commons

/*
 * Toggle — a labelled row with a switch at the end of it.
 *
 * ⚠ THE ROW OWNS THE CLICK, NOT THE SWITCH. Its ToggleSwitch is
 * `interactive: false` and is presentation only — otherwise a click landing on
 * the switch and a click landing on the label would take two different paths
 * and only one of them would be the one the caller wired up.
 *
 * Stateless about the value, like the switch: bind `checked`, flip it on
 * `clicked()`. That is what lets it sit in a model-driven list without the
 * delegate and the model disagreeing about what is on.
 */
BorderSurface {
    id: root

    property string label: ""
    property string description: ""
    property bool checked: false
    property bool hasCursor: false

    /* Pill on a rounded theme, rectangle on a sharp one — forwarded to the
     * switch so the whole row follows the desktop. */
    property bool rounded: Style.cornerRadius > 0

    property color foreground: Color.foreground
    property color accent: Color.accent
    property string fontFamily: Style.font.family
    property real titleSize: Style.font.subtitle
    property real descriptionSize: Style.font.caption

    signal clicked()
    signal hovered(bool isHovered)

    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed:  root.clicked()
    Keys.onSpacePressed:  root.clicked()

    implicitHeight: Math.max(54, content.implicitHeight + Style.spacing.huge)
    implicitWidth: Style.space(240)
    radius: Style.cornerRadius

    readonly property bool hot: root.hasCursor || mouse.containsMouse
    readonly property var rowBorderSpec:
        Border.controlSpec(root.activeFocus ? "focus" : (root.hot ? "hover-cursor" : "normal"),
                           root.foreground, root.accent, null)

    color: Style.controlFill(root.activeFocus, root.hot, root.foreground, root.accent)
    borderSpec: root.rowBorderSpec

    Behavior on color { ColorAnimation { duration: 100 } }

    Row {
        id: content
        anchors {
            left: parent.left;   leftMargin:  root.borderLeft  + Style.spacing.rowPaddingX
            right: parent.right; rightMargin: root.borderRight + Style.spacing.rowPaddingX
            verticalCenter: parent.verticalCenter
        }
        spacing: Style.spacing.rowPaddingX

        Column {
            width: parent.width - track.width - parent.spacing
            spacing: Style.spacing.xs
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: root.label
                color: root.foreground
                font { family: root.fontFamily; pixelSize: root.titleSize; bold: true }
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: root.description !== ""
                text: root.description
                color: Qt.darker(root.foreground, 1.5)
                font { family: root.fontFamily; pixelSize: root.descriptionSize }
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }

        ToggleSwitch {
            id: track
            checked: root.checked
            rounded: root.rounded
            foreground: root.foreground
            accent: root.accent
            interactive: false
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    HoverHandler {
        id: hoverHandler
        onHoveredChanged: root.hovered(hoverHandler.hovered)
    }
}
