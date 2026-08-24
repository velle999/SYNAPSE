import QtQuick
import QtQuick.Controls as QQC
import qs.Commons

/*
 * NumberField — a labelled spin box wearing the kit's chrome.
 *
 * ⚠ `onValueModified`, NOT `onValueChanged`. The first fires only when a PERSON
 * changed the number; the second fires when the binding does too, so a field
 * bound to live state would report every external update back as a user edit
 * and loop.
 *
 * ⚠ THE HOVER COMES OFF A HoverHandler IN THE BACKGROUND, not off the SpinBox.
 * QQC's SpinBox has its own `hovered` covering the whole control including its
 * up and down buttons, which lights the field while the pointer is on a button
 * that is not part of it.
 */
Column {
    id: root

    property string label: ""
    property int value: 0
    property int from: 0
    property int to: 100
    property int stepSize: 1
    property color foreground: Color.foreground
    property color accent: Color.accent
    property string fontFamily: Style.font.family
    property real fontSize: Style.font.body
    property real fieldWidth: Style.spacing.numberFieldWidth
    property bool hasCursor: false
    property bool hovering: false
    property alias field: spin

    signal modified(int value)
    signal hovered(bool on)

    spacing: Style.spacing.md

    Text {
        visible: root.label !== ""
        text: root.label
        color: Qt.darker(root.foreground, 1.4)
        font { family: root.fontFamily; pixelSize: Style.font.bodySmall }
    }

    QQC.SpinBox {
        id: spin
        width: root.fieldWidth
        implicitHeight: Math.max(Style.spacing.controlHeight,
                                 root.fontSize + Style.spacing.controlPaddingY * 2)
        from: root.from
        to: root.to
        stepSize: root.stepSize
        value: root.value
        editable: true
        font { family: root.fontFamily; pixelSize: root.fontSize }

        readonly property bool focused: spin.activeFocus
        readonly property bool hot: root.hovering || root.hasCursor
        readonly property var fieldBorderSpec:
            Border.controlSpec(spin.focused ? "focus" : (spin.hot ? "hover-cursor" : "normal"),
                               root.foreground, root.accent, null)

        leftPadding:   Border.left(spin.fieldBorderSpec)  + Style.spacing.controlPaddingX
        rightPadding:  Border.right(spin.fieldBorderSpec) + Style.spacing.controlPaddingX
        topPadding:    Border.top(spin.fieldBorderSpec)
        bottomPadding: Border.bottom(spin.fieldBorderSpec)

        onValueModified: root.modified(spin.value)

        background: BorderSurface {
            color: Style.controlFill(spin.focused, spin.hot, root.foreground, root.accent)
            borderSpec: spin.fieldBorderSpec
            radius: Style.cornerRadius

            HoverHandler {
                id: fieldHover
                onHoveredChanged: {
                    root.hovering = fieldHover.hovered
                    root.hovered(fieldHover.hovered)
                }
            }
        }

        contentItem: TextInput {
            text: spin.displayText
            font: spin.font
            color: root.foreground
            selectionColor: Style.selectionFillFor(root.foreground, root.accent, null)
            selectedTextColor: root.foreground
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !spin.editable
            validator: spin.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }
    }
}
