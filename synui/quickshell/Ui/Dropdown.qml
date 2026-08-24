import QtQuick
import QtQuick.Controls
import qs.Commons

/*
 * Dropdown — single select, wearing the panel kit rather than the platform's
 * native ComboBox.
 *
 * `options` is a plain array of strings or an array of `{ value, label }`, read
 * row by row through the accessors so the two can be mixed.
 *
 * ⛔ `popupOpen` EXISTS SO THE PANEL CAN GET OUT OF THE WAY. While the list is
 * open it owns j/k and the arrows; a panel that kept driving its own cursor from
 * the same keys would move both at once — the list scrolling under the pointer
 * while the panel's selection walks off somewhere else. A panel hosting one of
 * these suspends its PanelKeyCatcher on this property.
 *
 * ⚠ THE POPUP TAKES FOCUS ON OPEN, and it has to be forced: `focus: true` on a
 * Popup makes it focusable, not focused, and without the explicit call the
 * arrows go to whatever had focus before and the list ignores them.
 *
 * SearchableDropdown is deliberately a separate file rather than a flag here —
 * a filter input changes the key handling completely, and one component doing
 * both ends up a maze of `if (searchable)`.
 */
Item {
    id: root

    property string label: ""
    property string value: ""
    property var options: []

    /* ⚠ THE POPUP PALETTE, NOT THE BAR'S. This draws a card floating over the
     * desktop, so it takes the popup ink — the bar's foreground is measured off
     * the wallpaper and would be wrong on a surface with its own background. */
    property color foreground: Color.popups.text
    property color background: Color.popups.background
    property color popupBorder: Color.popups.border
    property color accent: Color.accent
    readonly property var popupBorderSpec:
        Border.localOrSurfaceSpec("popups", "border", root.popupBorder,
                                  Color.popups.border, Style.normalBorderWidth)

    property string fontFamily: Style.font.family
    property int rowHeight: Style.spacing.controlHeight
    property int popupRowHeight: Style.spacing.popupRowHeight
    property bool showLabel: true
    property bool hasCursor: false

    readonly property bool popupOpen: popup.opened
    function open()   { popup.open() }
    function close()  { popup.close() }
    function toggle() { if (popup.opened) popup.close(); else popup.open() }

    signal changed(string value)
    signal hovered(bool isHovered)

    function optionValue(o) { return (o && typeof o === "object") ? String(o.value) : String(o) }
    function optionLabel(o) { return (o && typeof o === "object") ? String(o.label) : String(o) }
    function currentLabel() {
        for (let i = 0; i < root.options.length; i++)
            if (root.optionValue(root.options[i]) === root.value)
                return root.optionLabel(root.options[i])
        /* The raw value when nothing matches — a stale setting should show what
         * it actually is rather than an empty box. */
        return root.value
    }

    implicitWidth: Style.spacing.dropdownWidth
    implicitHeight: (root.showLabel && root.label !== "")
                    ? root.rowHeight + Style.spacing.huge : root.rowHeight

    Column {
        anchors.fill: parent
        spacing: Style.spacing.labelGap

        Text {
            visible: root.showLabel && root.label !== ""
            text: root.label
            color: Qt.darker(root.foreground, 1.4)
            font { family: root.fontFamily; pixelSize: Style.font.caption; bold: true }
        }

        BorderSurface {
            id: trigger
            width: parent.width
            height: root.rowHeight
            radius: Style.cornerRadius
            activeFocusOnTab: true

            readonly property bool focused: trigger.activeFocus
            readonly property bool hot: triggerHover.hovered || root.hasCursor

            color: Style.controlFill(trigger.focused, trigger.hot, root.foreground, root.accent)
            borderSpec: Border.controlSpec(
                trigger.focused ? "focus" : (trigger.hot ? "hover-cursor" : "normal"),
                root.foreground, root.accent, null)

            HoverHandler {
                id: triggerHover
                onHoveredChanged: root.hovered(triggerHover.hovered)
            }

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space || event.key === Qt.Key_Down) {
                    root.toggle(); event.accepted = true
                } else if (event.key === Qt.Key_Escape && popup.opened) {
                    popup.close(); event.accepted = true
                }
            }

            Text {
                anchors {
                    left: parent.left;   leftMargin:  trigger.borderLeft + Style.spacing.controlPaddingX
                    right: chevron.left; rightMargin: trigger.borderRight + Style.spacing.md
                    verticalCenter: parent.verticalCenter
                }
                text: root.currentLabel()
                color: root.foreground
                font { family: root.fontFamily; pixelSize: Style.font.body }
                elide: Text.ElideRight
            }

            Text {
                id: chevron
                anchors {
                    right: parent.right
                    rightMargin: trigger.borderRight + Style.spacing.controlGap
                    verticalCenter: parent.verticalCenter
                }
                /* A Nerd Font chevron. Falls back to nothing visible on a family
                 * without it, which is why the whole row is still clickable. */
                text: "0"
                color: Qt.darker(root.foreground, 1.2)
                font { family: root.fontFamily; pixelSize: Style.font.body }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { trigger.forceActiveFocus(); root.toggle() }
            }

            Popup {
                id: popup
                x: 0
                y: trigger.height + Style.spacing.xxs
                width: trigger.width
                /* Capped at eight rows: a list longer than that scrolls rather
                 * than growing off the bottom of the screen. */
                implicitHeight: Math.min(
                    root.options.length * root.popupRowHeight
                        + Math.max(0, root.options.length - 1) * Style.spacing.labelGap
                        + Style.spacing.xxs,
                    root.popupRowHeight * 8 + 7 * Style.spacing.labelGap + Style.spacing.xxs)
                padding: Style.spacing.hairline
                leftPadding:   Border.left(root.popupBorderSpec)   + Style.spacing.hairline
                rightPadding:  Border.right(root.popupBorderSpec)  + Style.spacing.hairline
                topPadding:    Border.top(root.popupBorderSpec)    + Style.spacing.hairline
                bottomPadding: Border.bottom(root.popupBorderSpec) + Style.spacing.hairline
                focus: true

                background: BorderSurface {
                    color: root.background
                    borderSpec: root.popupBorderSpec
                    radius: Style.cornerRadius
                }

                onOpened: {
                    /* Land on what is already chosen, not on the top. */
                    optionList.currentIndex = Math.max(0, optionList.indexOfValue(root.value))
                    optionList.forceActiveFocus()
                }

                contentItem: ListView {
                    id: optionList
                    spacing: Style.spacing.labelGap
                    implicitHeight: contentHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.options
                    currentIndex: -1

                    /* BeforeItem, or the ListView's own Up/Down scrolling eats
                     * the keys meant to move the selection. */
                    Keys.priority: Keys.BeforeItem
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Escape) {
                            popup.close(); event.accepted = true
                        } else if (event.key === Qt.Key_Down || event.text === "j") {
                            optionList.currentIndex =
                                Math.min(root.options.length - 1, optionList.currentIndex + 1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up || event.text === "k") {
                            optionList.currentIndex = Math.max(0, optionList.currentIndex - 1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            optionList.selectCurrent(); event.accepted = true
                        }
                    }

                    function indexOfValue(v) {
                        for (let i = 0; i < root.options.length; i++)
                            if (root.optionValue(root.options[i]) === v) return i
                        return -1
                    }

                    function selectCurrent() {
                        if (optionList.currentIndex < 0
                            || optionList.currentIndex >= root.options.length) return
                        const v = root.optionValue(root.options[optionList.currentIndex])
                        root.value = v
                        root.changed(v)
                        popup.close()
                    }

                    delegate: Rectangle {
                        id: optionRow
                        required property var modelData
                        required property int index
                        width: optionList.width
                        height: root.popupRowHeight
                        color: optionRow.index === optionList.currentIndex
                            ? Style.hoverFillFor(root.foreground, root.accent, null)
                            : "transparent"

                        Text {
                            anchors {
                                left: parent.left;   leftMargin:  Style.spacing.controlPaddingX
                                right: parent.right; rightMargin: Style.spacing.controlPaddingX
                                verticalCenter: parent.verticalCenter
                            }
                            text: root.optionLabel(optionRow.modelData)
                            color: optionRow.index === optionList.currentIndex
                                ? Style.hoverStateColor(root.foreground, root.accent, null)
                                : root.foreground
                            font { family: root.fontFamily; pixelSize: Style.font.body }
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            /* Hover moves the SAME index the keyboard uses, so
                             * there is one highlight — CursorSurface's rule,
                             * applied to a list that draws its own rows. */
                            onPositionChanged: optionList.currentIndex = optionRow.index
                            onClicked: optionList.selectCurrent()
                        }
                    }
                }
            }
        }
    }
}
