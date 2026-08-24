import QtQuick

/*
 * PanelKeyCatcher — the key handling every keyboard-driven panel would
 * otherwise write out again.
 *
 * Wrap the panel's content in one and it emits SEMANTIC signals — move,
 * activate, close — leaving each panel its own state machine for what those
 * mean. The alternative is thirty panels each deciding for themselves whether
 * `j` moves down, which is how a desktop ends up with two conventions.
 *
 * ⛔ `Keys.priority: Keys.BeforeItem` IS THE LOAD-BEARING LINE. Without it a
 * descendant with focus eats the keys first — and the most common descendant
 * is a Flickable, whose built-in Up/Down scrolling would swallow exactly the
 * arrows that are supposed to drive the cursor. The panel would scroll and the
 * selection would never move.
 *
 * ⚠ WHICH IS ALSO WHY `blocked` EXISTS. A panel with a text field in it must
 * set `blocked: editor.activeFocus`, or every letter typed into the passphrase
 * box is read as a shortcut instead. When blocked, every key goes straight
 * through and no signal fires.
 *
 * Arrows and hjkl both, because a panel summoned from a keyboard is being used
 * by someone whose hands are already there.
 */
Item {
    id: root

    property bool blocked: false

    signal moveRequested(int dx, int dy)
    signal activateRequested()
    signal returnRequested()
    signal closeRequested()
    signal deleteRequested()
    signal tabRequested(int direction)
    signal textKey(string text)

    focus: true
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (root.blocked) return

        if (event.key === Qt.Key_Escape) {
            root.closeRequested(); event.accepted = true; return
        }
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            root.tabRequested(((event.modifiers & Qt.ShiftModifier)
                               || event.key === Qt.Key_Backtab) ? -1 : 1)
            event.accepted = true; return
        }
        if (event.key === Qt.Key_Down  || event.text === "j") {
            root.moveRequested(0, 1);  event.accepted = true; return
        }
        if (event.key === Qt.Key_Up    || event.text === "k") {
            root.moveRequested(0, -1); event.accepted = true; return
        }
        if (event.key === Qt.Key_Right || event.text === "l") {
            root.moveRequested(1, 0);  event.accepted = true; return
        }
        if (event.key === Qt.Key_Left  || event.text === "h") {
            root.moveRequested(-1, 0); event.accepted = true; return
        }
        /* ⚠ RETURN FIRES BOTH. A panel may listen for one or the other — a
         * confirm dialog wants `returnRequested`, a list wants `activate` — and
         * their own panels rely on Enter meaning both. */
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.returnRequested()
            root.activateRequested(); event.accepted = true; return
        }
        if (event.key === Qt.Key_Space) {
            root.activateRequested(); event.accepted = true; return
        }
        if (event.text === "x" || event.text === "X") {
            root.deleteRequested(); event.accepted = true; return
        }
        /* Anything else printable goes to the panel as a plain key, which is
         * how `r` comes to mean refresh in one panel and nothing in another.
         * NOT accepted: a panel that ignores it leaves it for a descendant. */
        if (event.text && event.text.length === 1) root.textKey(event.text)
    }
}
