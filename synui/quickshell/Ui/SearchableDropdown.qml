import QtQuick
import QtQuick.Controls
import qs.Commons

/*
 * SearchableDropdown — a Dropdown with a filter box in the popup, for lists too
 * long to arrow through: a timezone, a font, a Wi-Fi network in a block of
 * flats.
 *
 * ⛔ A SEPARATE FILE FROM Dropdown ON PURPOSE. A filter input changes the key
 * handling completely — every letter belongs to the field, not to the list — so
 * one component doing both would be a maze of `if (searchable)` around every
 * key. The visuals are shared by both looking the same, not by inheritance.
 *
 * ⚠ THE FIELD KEEPS FOCUS AND THE LIST IS DRIVEN THROUGH IT. Up/Down/Enter/Esc
 * are handled on the input and forwarded; if focus moved to the list to arrow
 * through it, typing another letter would go nowhere and the filter would look
 * frozen.
 */
Item {
    id: root

    property string label: ""
    property string value: ""
    property var options: []
    property string placeholderText: "Search..."
    property string emptyText: "No matches"
    property string triggerLabel: ""

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
    property int popupMinHeight: Style.spacing.searchablePopupMinHeight
    property bool showLabel: true
    property bool hasCursor: false

    readonly property bool popupOpen: popup.opened
    function open()   { popup.open() }
    function close()  { popup.close() }
    function toggle() { if (popup.opened) popup.close(); else popup.open() }

    signal changed(string value)
    signal hovered(bool isHovered)

    function optionValue(o)       { return (o && typeof o === "object") ? String(o.value) : String(o) }
    function optionLabel(o)       { return (o && typeof o === "object") ? String(o.label) : String(o) }
    function optionDescription(o) { return (o && typeof o === "object" && o.description)
                                           ? String(o.description) : "" }
    function currentLabel() {
        if (root.triggerLabel !== "") return root.triggerLabel
        for (let i = 0; i < root.options.length; i++)
            if (root.optionValue(root.options[i]) === root.value)
                return root.optionLabel(root.options[i])
        return root.value
    }

    /* Recomputed on demand rather than as a binding: the filter changes on
     * every keystroke and a binding would re-run for every unrelated change to
     * `options` as well. */
    property var filtered: root.options
    function recomputeFiltered() {
        const q = search.text.trim().toLowerCase()
        if (q === "") { root.filtered = root.options; return }
        const out = []
        for (let i = 0; i < root.options.length; i++) {
            const o = root.options[i]
            const hay = (root.optionLabel(o) + " " + root.optionValue(o) + " "
                         + root.optionDescription(o)).toLowerCase()
            if (hay.indexOf(q) >= 0) out.push(o)
        }
        root.filtered = out
    }
    onOptionsChanged: root.recomputeFiltered()

    implicitWidth: Style.spacing.searchableDropdownWidth
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
                }
            }

            Text {
                anchors {
                    left: parent.left; leftMargin: trigger.borderLeft + Style.spacing.controlPaddingX
                    right: parent.right; rightMargin: trigger.borderRight + Style.spacing.controlPaddingX
                    verticalCenter: parent.verticalCenter
                }
                text: root.currentLabel()
                color: root.foreground
                font { family: root.fontFamily; pixelSize: Style.font.body }
                elide: Text.ElideRight
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
                implicitHeight: Math.max(root.popupMinHeight,
                                         search.height + Style.spacing.md
                                         + Math.min(root.filtered.length, 8) * root.popupRowHeight)
                padding: Style.spacing.hairline
                leftPadding:   Border.left(root.popupBorderSpec)   + Style.spacing.sm
                rightPadding:  Border.right(root.popupBorderSpec)  + Style.spacing.sm
                topPadding:    Border.top(root.popupBorderSpec)    + Style.spacing.sm
                bottomPadding: Border.bottom(root.popupBorderSpec) + Style.spacing.sm
                focus: true

                background: BorderSurface {
                    color: root.background
                    borderSpec: root.popupBorderSpec
                    radius: Style.cornerRadius
                }

                onOpened: {
                    search.text = ""
                    root.recomputeFiltered()
                    optionList.currentIndex = 0
                    search.forceActiveFocus()
                }

                contentItem: Column {
                    spacing: Style.spacing.md

                    TextField {
                        id: search
                        width: parent.width
                        placeholderText: root.placeholderText
                        foreground: root.foreground
                        accent: root.accent
                        onTextChanged: {
                            root.recomputeFiltered()
                            optionList.currentIndex = 0
                        }
                        /* The list is driven from here — see the note at the
                         * top. Everything not handled falls through to the
                         * field as ordinary typing. */
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Escape) {
                                popup.close(); event.accepted = true
                            } else if (event.key === Qt.Key_Down) {
                                optionList.currentIndex =
                                    Math.min(root.filtered.length - 1, optionList.currentIndex + 1)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up) {
                                optionList.currentIndex = Math.max(0, optionList.currentIndex - 1)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                optionList.selectCurrent(); event.accepted = true
                            }
                        }
                    }

                    Text {
                        visible: root.filtered.length === 0
                        width: parent.width
                        text: root.emptyText
                        color: Qt.darker(root.foreground, 1.4)
                        font { family: root.fontFamily; pixelSize: Style.font.bodySmall }
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ListView {
                        id: optionList
                        width: parent.width
                        height: Math.min(root.filtered.length, 8) * root.popupRowHeight
                        visible: root.filtered.length > 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: root.filtered
                        currentIndex: 0

                        function selectCurrent() {
                            if (optionList.currentIndex < 0
                                || optionList.currentIndex >= root.filtered.length) return
                            const v = root.optionValue(root.filtered[optionList.currentIndex])
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
                                onPositionChanged: optionList.currentIndex = optionRow.index
                                onClicked: optionList.selectCurrent()
                            }
                        }
                    }
                }
            }
        }
    }
}
