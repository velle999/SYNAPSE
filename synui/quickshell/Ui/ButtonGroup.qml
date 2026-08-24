import QtQuick
import qs.Commons

/*
 * ButtonGroup — a mutually exclusive row of chips. Pick one of N.
 *
 * `options` is either a plain array of strings (the label is the value) or an
 * array of `{ value, label, icon?, tooltip? }`. Mixing the two is fine, which
 * is why every read goes through the accessors below rather than touching the
 * fields directly.
 *
 * ⚠ THE GROUP IS ONE TAB STOP, NOT ONE PER CHIP. A form walked with Tab should
 * treat "bar position" as a single control; landing on each of four chips in
 * turn makes a short form feel enormous. Once the group has focus, h/l and the
 * arrows walk between chips and Enter activates. Focus arrives on the SELECTED
 * chip so the existing choice is what the eye lands on.
 *
 * ⚠ TAB FOCUS AND `cursorIndex` ARE INDEPENDENT AND EITHER PAINTS THE HOT
 * STATE. A panel with its own keyboard cursor drives `cursorIndex` and never
 * gives the group Tab focus at all; a settings form does the opposite. Both
 * look the same to the person using them, which is the point.
 */
Row {
    id: root

    property var options: []
    property string value: ""
    property color foreground: Color.foreground
    property color background: Color.background
    property color accent: Color.accent
    property string fontFamily: Style.font.family
    property real fontSize: Style.font.body
    property bool focusable: true

    /* -1 disables the external highlight — the Tab-focus case. */
    property int cursorIndex: -1
    /* Where h/l is sitting while the group itself has focus. */
    property int focusedIndex: -1

    signal changed(string value)
    signal hovered(int index, bool isHovered)

    spacing: Style.spacing.md
    activeFocusOnTab: root.focusable

    function optionValue(o)   { return (o && typeof o === "object") ? String(o.value) : String(o) }
    function optionLabel(o)   { return (o && typeof o === "object" && o.label !== undefined)
                                       ? String(o.label) : String(o) }
    function optionIcon(o)    { return (o && typeof o === "object" && o.icon) ? String(o.icon) : "" }
    function optionTooltip(o) { return (o && typeof o === "object" && o.tooltip) ? String(o.tooltip) : "" }

    function selectedOptionIndex() {
        for (let i = 0; i < root.options.length; i++)
            if (root.optionValue(root.options[i]) === root.value) return i
        return -1
    }

    function activateFocused() {
        if (root.focusedIndex < 0 || root.focusedIndex >= root.options.length) return
        root.changed(root.optionValue(root.options[root.focusedIndex]))
    }

    onActiveFocusChanged: {
        if (root.activeFocus) {
            const idx = root.selectedOptionIndex()
            root.focusedIndex = idx < 0 ? 0 : idx
        } else {
            root.focusedIndex = -1
        }
    }

    /* BeforeItem, for the same reason PanelKeyCatcher needs it: a focused chip
     * would otherwise eat the arrows that are meant to move between chips. */
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Left || event.text === "h") {
            root.focusedIndex = Math.max(0, (root.focusedIndex < 0 ? 0 : root.focusedIndex) - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Right || event.text === "l") {
            root.focusedIndex = Math.min(root.options.length - 1,
                                         (root.focusedIndex < 0 ? 0 : root.focusedIndex) + 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Space) {
            root.activateFocused()
            event.accepted = true
        }
    }

    Repeater {
        model: root.options

        delegate: Button {
            required property var modelData
            required property int index
            text: root.optionLabel(modelData)
            iconText: root.optionIcon(modelData)
            tooltipText: root.optionTooltip(modelData)
            selected: root.optionValue(modelData) === root.value
            hasCursor: root.cursorIndex === index
                       || (root.activeFocus && root.focusedIndex === index)
            /* Every chip carries the bordered chrome, so the group reads as a
             * row of distinct options rather than as one long label. */
            bordered: true
            foreground: root.foreground
            background: root.background
            accent: root.accent
            fontFamily: root.fontFamily
            fontSize: root.fontSize
            onClicked: root.changed(root.optionValue(modelData))
            onHovered: (h) => root.hovered(index, h)
        }
    }
}
