import QtQuick
import Quickshell
import ".."

/*
 * The post-it note.
 *
 * A scrap of desktop you can write on. Every other widget here reports
 * something the machine already knows; this is the only one whose content comes
 * from the person sitting in front of it, which is why it is the only one with
 * a file of its own (PostItState).
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

    widgetId: "postit"
    shown: WidgetState.postit
    label: "NOTE"
    accent: Theme.yellow
    interactive: true

    // Bottom-left is the one corner nothing else claims: the quick-launch strip
    // is top-left, the system monitor top-right, the big clock bottom-right.
    homeEdgeH: "left"; homeEdgeV: "bottom"
    homeMarginX: 20
    // Clear of the visualiser when both are on, on the same terms and with the
    // same number BigClock uses at the other end of that strip.
    homeMarginY: WidgetState.visualizer ? 124 : 24

    cardWidth: 264
    bodyHeight: 168

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
        text: PostItState.note.trim() !== "" ? PostItState.note
                                             : "click to write something"
        color: PostItState.note.trim() !== "" ? Theme.fg : Theme.fgDim
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
        anchors { bottom: parent.bottom; left: parent.left }
        text: hover.containsMouse ? "click to edit" : ""
        color: Theme.fgDim
        font.family: Theme.fontFamily
        font.pixelSize: 9
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onClicked: PostItState.editing = true
    }
}
