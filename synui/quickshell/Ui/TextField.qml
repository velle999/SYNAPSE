import QtQuick
import QtQuick.Controls
import qs.Commons

/*
 * TextField — a single-line input wearing the panel kit's focus and selection.
 *
 * ⚠ IT INHERITS FROM Qt Quick Controls' TextField RATHER THAN WRAPPING ONE, so
 * the whole underlying API — text, placeholderText, accepted, editingFinished,
 * validator, selectAll — is available without this file re-exposing each name.
 * A wrapper would have to forward all of it and would silently lack whatever it
 * forgot.
 *
 * ⚠ THERE IS NO `hovered` SIGNAL HERE ON PURPOSE. QQC's TextField already has a
 * `hovered` PROPERTY; adding a signal of the same name would shadow it, and a
 * caller reading either would get the other. Panels watch `onHoveredChanged`.
 *
 * ⛔ A PANEL WITH ONE OF THESE MUST SET ITS PanelKeyCatcher's `blocked` TO THIS
 * FIELD'S activeFocus, or every letter typed in here is read as a panel
 * shortcut and the passphrase box stays empty while the panel scrolls.
 */
TextField {
    id: root

    property color foreground: Color.foreground
    property color accent: Color.accent
    property color selectionTint: Style.selectionFillFor(root.foreground, root.accent, null)
    property bool  password: false
    property real  horizontalPadding: Style.spacing.controlPaddingX
    property real  verticalPadding: Style.spacing.inputPaddingY

    /* The panel cursor landing on this field without focusing it — the field
     * paints the shared cursor state so the highlight is where the arrow keys
     * say it is. */
    property bool hasCursor: false

    readonly property bool focused: root.activeFocus
    readonly property bool hot: root.hovered || root.hasCursor
    readonly property var borderSpec:
        Border.controlSpec(root.focused ? "focus" : (root.hot ? "hover-cursor" : "normal"),
                           root.foreground, root.accent, null)

    echoMode: root.password ? TextInput.Password : TextInput.Normal
    font.family: Style.font.family
    font.pixelSize: Style.font.body
    color: root.foreground
    selectionColor: root.selectionTint
    selectedTextColor: root.foreground
    placeholderTextColor: Qt.darker(root.foreground, 1.6)

    /* The padding carries the border, so the text does not shift by a pixel
     * when focus thickens the edge. */
    leftPadding:   root.horizontalPadding + Border.left(root.borderSpec)
    rightPadding:  root.horizontalPadding + Border.right(root.borderSpec)
    topPadding:    root.verticalPadding   + Border.top(root.borderSpec)
    bottomPadding: root.verticalPadding   + Border.bottom(root.borderSpec)

    background: BorderSurface {
        color: Style.controlFill(root.focused, root.hot, root.foreground, root.accent)
        borderSpec: root.borderSpec
        radius: Style.cornerRadius
    }
}
