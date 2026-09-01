import QtQuick
import qs.Commons
// ⚠ THE SHELL ROOT, for I18n. quickshell resolves `qs.Ui` to <shell
// root>/Ui, so `..` from here is the root module and its qmldir — the
// same directory `qs.Commons` sits beside. It is not reachable as
// `qs.something` because the root is the shell itself, not a submodule.
import ".."

/*
 * ConfirmDialog — "are you sure", over the panel that asked.
 *
 * ⚠ THE CONFIRM BUTTON IS THE DESTRUCTIVE ONE AND IS COLOURED AS SUCH, always
 * at index 1. Forget this network, unpair this device, delete this. The pair is
 * fixed rather than a list because a confirmation with three answers is not a
 * confirmation.
 *
 * ⚠ THE CARD GROWS WITH THE WRAPPED MESSAGE. A fixed height squeezed a long
 * question into the buttons on a narrow host, which is the one place a dialog
 * cannot afford to be unreadable.
 *
 * ⚠ AND HOVER MOVES THE SELECTION, so the pointer and the keyboard cannot
 * disagree about which button Enter presses. Same rule the panel cursor keeps.
 *
 * `handleKey(event)` returns whether it consumed the key, so the panel hosting
 * it can pass keys down and act on what is left. It is a function rather than a
 * Keys handler because the dialog does not own focus — the panel does.
 */
Item {
    id: root

    property bool opened: false
    property string message: ""
    property string cancelText: I18n.tr("Cancel")
    property string confirmText: I18n.tr("Confirm")
    /* 1 — the destructive one — is where the eye is going anyway; a dialog that
     * lands on Cancel makes every deliberate confirmation two keys. */
    property int selectedIndex: 1

    property color background: Color.background
    property color foreground: Color.foreground
    property color scrim: Util.alpha(Color.background, 0.7)
    property color selectedBackground: Util.alpha(Color.foreground, 0.08)
    property color selectedText: Color.accent
    property string fontFamily: Style.font.family
    property int cornerRadius: Style.cornerRadius

    signal canceled()
    signal confirmed()

    function handleKey(event) {
        if (!root.opened) return false

        if (event.key === Qt.Key_Escape) {
            root.canceled(); return true
        }
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
            || event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            root.selectedIndex = root.selectedIndex === 0 ? 1 : 0
            return true
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (root.selectedIndex === 0) root.canceled()
            else root.confirmed()
            return true
        }
        return false
    }

    visible: root.opened

    Rectangle {
        anchors.fill: parent
        color: root.scrim

        /* Outside the card cancels — the safe answer, which is the only answer
         * a stray click may ever give. */
        MouseArea { anchors.fill: parent; onClicked: root.canceled() }

        BorderSurface {
            id: card
            width: Math.min(parent.width - Style.space(32), Style.space(370))
            height: card.contentTopInset + card.contentBottomInset
                    + messageText.implicitHeight + Style.space(20) + Style.space(34)
            anchors.centerIn: parent
            color: root.background
            borderSpec: Border.flat(root.selectedText, Style.normalBorderWidth)
            padding: Style.space(18)
            radius: root.cornerRadius

            /* Swallows the click so the scrim below never sees it. */
            MouseArea { anchors.fill: parent }

            Item {
                anchors {
                    fill: parent
                    topMargin:    card.contentTopInset
                    rightMargin:  card.contentRightInset
                    bottomMargin: card.contentBottomInset
                    leftMargin:   card.contentLeftInset
                }

                Text {
                    id: messageText
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    text: root.message
                    color: root.foreground
                    font { family: root.fontFamily; pixelSize: Style.font.title }
                    wrapMode: Text.WordWrap
                }

                Row {
                    anchors { right: parent.right; bottom: parent.bottom }
                    spacing: Style.space(10)

                    Repeater {
                        model: [root.cancelText, root.confirmText]

                        BorderSurface {
                            required property int index
                            required property string modelData

                            readonly property bool selected: root.selectedIndex === index
                            readonly property bool destructive: index === 1

                            width: Style.space(88)
                            height: Style.space(34)
                            radius: 0
                            color: selected
                                ? (destructive ? Util.alpha(Color.urgent, 0.22)
                                               : root.selectedBackground)
                                : "transparent"
                            borderSpec: Border.flat(
                                destructive
                                ? (selected ? Color.urgent : Util.alpha(Color.urgent, 0.56))
                                : (selected ? root.selectedText
                                            : Util.alpha(root.foreground, 0.38)),
                                Style.normalBorderWidth)

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: destructive
                                    ? (selected ? Color.urgent : root.foreground)
                                    : (selected ? root.selectedText : root.foreground)
                                font { family: root.fontFamily; pixelSize: Style.font.caption }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: root.selectedIndex = index
                                onClicked: {
                                    if (index === 0) root.canceled()
                                    else root.confirmed()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
