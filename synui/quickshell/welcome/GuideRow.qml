import QtQuick
import ".."

/*
 * GuideRow — one line of a guide page: what it is, what it does, and the key.
 *
 * Two shapes, because a page is not only a list of doors. A `note` row is prose
 * — the workspace keys, where the documentation lives — and it is deliberately
 * NOT selectable: arrowing onto a line you cannot press and having Enter do
 * nothing is the kind of dead spot that teaches people the keyboard is
 * unreliable here.
 *
 * The description under the label is the half the old menu had nowhere to put.
 * It had one column of labels and one of chords in a 513px-wide panel, so
 * "Neural Overlay" and "Cat Mode" arrived with no explanation and the only way
 * to find out what either did was to press it.
 */
Rectangle {
    id: root

    property var    row:      null
    property bool   selected: false
    property string chord:    ""

    readonly property bool isNote: root.row && root.row.kind === "note"

    signal activated()

    implicitHeight: content.implicitHeight + (root.isNote ? 8 : 18)
    radius: Math.min(6, Theme.panelRadius)

    color: root.isNote        ? "transparent"
         : root.selected      ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.13)
         : root.hovered       ? Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.05)
                              : "transparent"

    // The selected row also carries a left edge in the accent, so selection
    // survives a theme whose accent is close to the surface it washes over —
    // a 13% wash is a hint, and a hint is not a state.
    Rectangle {
        width: 2
        radius: 1
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom
                  topMargin: 4; bottomMargin: 4 }
        color: Theme.cyan
        visible: root.selected && !root.isNote
    }

    HoverHandler {
        id: hover
        enabled: !root.isNote
        /*
         * ⚠ HoverHandler, NOT a MouseArea with hoverEnabled.
         *
         * A MouseArea filling this row is EXITED the moment the pointer crosses
         * onto the Text inside it, so a hover highlight built that way flickers
         * off over exactly the words you are pointing at. That is the same trap
         * the popup rows hit; the handler sees the whole subtree.
         */
    }
    readonly property bool hovered: hover.hovered

    TapHandler {
        enabled: !root.isNote
        acceptedButtons: Qt.LeftButton
        onTapped: root.activated()
    }

    Item {
        id: content
        anchors { left: parent.left; right: parent.right
                  verticalCenter: parent.verticalCenter
                  leftMargin: 14; rightMargin: 12 }
        implicitHeight: root.isNote ? note.implicitHeight
                                    : Math.max(labels.implicitHeight, chip.implicitHeight)

        // ── The prose shape ──────────────────────────────
        Text {
            id: note
            visible: root.isNote
            width: parent.width
            text: root.row && root.row.text ? root.row.text : ""
            color: Theme.fgDim
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        // ── The door shape ───────────────────────────────
        Column {
            id: labels
            visible: !root.isNote
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: parent.width - chip.width - 16
            spacing: 2

            Text {
                text: root.row && root.row.label ? root.row.label : ""
                color: Theme.fg
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize + 2
            }
            Text {
                width: parent.width
                visible: text !== ""
                text: root.row && root.row.desc ? root.row.desc : ""
                color: Theme.fgDim
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
            }
        }

        KeyChip {
            id: chip
            /* ⚠ THE `root.chord !== ""` HALF IS LOAD-BEARING. KeyChip hides
             * itself on an empty chord, and assigning `visible` here REPLACES
             * that binding rather than adding to it — which drew a 16px empty
             * cap beside every row that has no shortcut ("AI Model",
             * "Applications", "About this system"). A blank key cap is a
             * promise of a shortcut that does not exist. */
            visible: !root.isNote && root.chord !== ""
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: root.chord
            value: root.row && root.row.live !== undefined
        }
    }
}
