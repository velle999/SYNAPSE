import QtQuick
import Quickshell
import ".."

/*
 * A post-it note.
 *
 * A scrap of desktop you can write on. Every other widget here reports
 * something the machine already knows; this is the only one whose content comes
 * from the person sitting in front of it, which is why it is the only one with
 * a store of its own (PostItState).
 *
 * There can be any number of them. `+` makes another, `×` throws one away, and
 * each is a window of its own with its own entry in widgets.pos — so they are
 * dragged, sent home and remembered one at a time, by exactly the machinery
 * every other widget uses. That is the whole reason the model this is
 * instantiated from is one entry per NOTE per screen rather than one per
 * screen: nothing about being several had to be invented here.
 *
 * THE NOTE NEVER TAKES THE KEYBOARD. Typing happens in PostItEditor, a separate
 * surface that is mapped only while you are editing, and that split is what the
 * compositor requires rather than a preference: layer.c grants a layer surface
 * the keyboard in layer_surface_map() and nowhere else, so flipping `focusable`
 * on this surface once it is up would be ignored and the note would be a text
 * box that silently swallowed every key. Leaving it focusable permanently is
 * worse — focusable means EXCLUSIVE here (see StartMenu.qml), and this widget is
 * up for as long as the desktop is, so it would take every keystroke away from
 * whatever window you were actually working in.
 *
 * It does take POINTER input, with the consequence QuickLaunch documents: while
 * it is on, clicks inside this rectangle go to it rather than the desktop
 * underneath, so the desktop right-click menu is unreachable there.
 */
WidgetFrame {
    id: root

    // The model entry is { screen, noteId }, not a screen. See WidgetFrame.
    screenData: modelData.screen
    readonly property string noteId: modelData.noteId

    // Every note is its own widget as far as the layout is concerned, which is
    // what gives each one a position that survives a restart.
    widgetId: noteId
    shown: WidgetState.postit
    label: I18n.tr("NOTE")
    accent: Theme.yellow
    interactive: true

    /*
     * The writing goes on whatever this card turns out to be sitting on, rather
     * than on the theme's idea of a surface — see inkOnBackdrop in WidgetFrame.
     *
     * This is the widget that needed it. A note is a paragraph of body text at
     * 12px, it is the only thing on the desktop the user wrote themselves, and
     * with the dock at 0.00 its card is not a card at all: the words are on the
     * wallpaper, in an ink the theme picked to sit on a pane that is not there.
     * On macOS 26 that ink is #1D1D1F and the note goes dark-on-dark.
     *
     * The ACCENT is deliberately left alone. Yellow is what says "note" — it is
     * the header tag, the rule and the + — and it is a colour with a meaning
     * rather than ink on a surface, the same reason the bar keeps a red battery
     * red on a clear strip.
     */
    inkOnBackdrop: true

    // Bottom-left is the one corner nothing else claims: the quick-launch strip
    // is top-left, the system monitor top-right, the big clock bottom-right.
    // Notes cascade up and to the right from there — see PostItState.
    homeEdgeH: "left"; homeEdgeV: "bottom"
    homeMarginX: PostItState.homeX(noteId)
    homeMarginY: PostItState.homeY(noteId)

    cardWidth: 264
    bodyHeight: 168

    readonly property string text: PostItState.textOf(noteId)
    readonly property bool empty: text.trim() === ""

    // Whether the pointer is anywhere on this card, the buttons included. The
    // body's own MouseArea cannot answer that on its own: a button on top of it
    // takes the hover with the click, and the footer hint would blink out just
    // as you reached for the control it is describing.
    readonly property bool touched: hover.containsMouse || plus.hovered || del.hovered

    // Ruled paper. The lines are pinned to the same 16px rhythm the note's text
    // is set on, so the writing sits on them rather than across them.
    Repeater {
        model: Math.floor(parent.height / 16)
        delegate: Rectangle {
            required property int index
            y: (index + 1) * 16 - 1
            width: parent.width
            height: 1
            color: root.accent
            opacity: 0.10
        }
    }

    Text {
        id: note
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: foot.top }
        anchors.bottomMargin: 4
        text: root.empty ? I18n.tr("click to write something") : root.text
        color: root.empty ? root.inkDim : root.ink
        font.family: Theme.fontFamily
        font.pixelSize: 12
        lineHeight: 16
        lineHeightMode: Text.FixedHeight
        wrapMode: Text.Wrap
        // NO elide, and that is a fix rather than an omission. Eliding WRAPPED
        // text needs a height to elide against, and this one comes from anchors
        // — so the first layout runs at height 0, every line elides away to
        // nothing, and the result is never recomputed once the anchors resolve.
        // Any later change to `text` re-lays it out, which is why this looked
        // fine in every test that wrote a note and was blank in the one case
        // that matters: a first run, where nobody has written anything and the
        // string never changes again. A note that says "click to write
        // something" and renders NOTHING is worse than a long note losing its
        // ellipsis, which is all clip costs.
        clip: true
    }

    Text {
        id: foot
        anchors { bottom: parent.bottom; left: parent.left; right: tools.left }
        anchors.rightMargin: 6
        // Whatever the pointer is actually over. A + and a × on a note are not
        // self-explanatory enough to leave unlabelled, and this is the only
        // strip of the card that is not the note itself.
        text: plus.hovered ? plus.hint
            : del.hovered  ? del.hint
            : hover.containsMouse ? I18n.tr("click to edit") : ""
        color: root.inkDim
        font.family: Theme.fontFamily
        font.pixelSize: 9
        elide: Text.ElideRight
    }

    // Declared BEFORE the buttons so that they are on top of it: this fills the
    // whole body, and a control inside it has to win the press.
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onClicked: PostItState.editing = root.noteId
    }

    Row {
        id: tools
        anchors { bottom: parent.bottom; right: parent.right }
        spacing: 4

        NoteButton {
            id: plus
            glyph: "+"
            tint: Theme.yellow
            hint: I18n.tr("another note")
            onActivated: PostItState.add()
        }

        NoteButton {
            id: del
            // The last note is not removable. It is the only thing on the
            // desktop carrying the + that makes another one, and a widget you
            // can delete yourself out of is one you can only get back from a
            // terminal.
            visible: PostItState.ids.length > 1
            glyph: root.armed ? "!" : "×"
            tint: root.armed ? Theme.red : root.inkDim
            hint: root.armed ? I18n.tr("again to delete") : I18n.tr("delete this note")
            onActivated: {
                // Two presses, because this throws away something only the user
                // could have written and there is no undo for a deleted file.
                if (root.armed) PostItState.remove(root.noteId)
                else            { root.armed = true; disarm.restart() }
            }
        }
    }

    // Armed by the first press on ×, and forgotten again shortly after. A
    // delete that stayed armed would go off on a stray click minutes later.
    property bool armed: false
    Timer {
        id: disarm
        interval: 2600
        onTriggered: root.armed = false
    }
    // Disarm on the way out too: coming back to a note that is still primed is
    // the same surprise with a longer fuse.
    onTouchedChanged: if (!root.touched) root.armed = false

    /*
     * The two controls on the card.
     *
     * Self-contained on purpose, as WidgetFrame's Chamfered is: an inline
     * component is its own type and must not reach for ids in the file around
     * it. Singletons are not ids, which is why Theme is fair game.
     */
    component NoteButton: Rectangle {
        id: btn

        property string glyph: ""
        property string hint: ""
        property color  tint: Theme.fgDim
        readonly property alias hovered: ma.containsMouse
        signal activated()

        width: 15; height: 15
        radius: 3
        color: ma.containsMouse ? Qt.rgba(btn.tint.r, btn.tint.g, btn.tint.b, 0.20)
                                : "transparent"
        border.width: 1
        border.color: Qt.rgba(btn.tint.r, btn.tint.g, btn.tint.b,
                              ma.containsMouse ? 0.85 : 0.30)
        Behavior on color        { ColorAnimation { duration: Theme.animFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

        Text {
            anchors.centerIn: parent
            // The glyph sits a hair high in the box at this size.
            anchors.verticalCenterOffset: -1
            text: btn.glyph
            color: btn.tint
            opacity: ma.containsMouse ? 1.0 : 0.65
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }

        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.activated()
        }
    }
}
