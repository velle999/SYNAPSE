import QtQuick
import Quickshell
import Quickshell.Wayland
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
 * underneath, so the desktop right-click menu is unreachable there. Unavoidable
 * for anything meant to be clicked, and part of why it is off by default.
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    visible: WidgetState.postit && modelData.name === WidgetState.primaryOutput

    WlrLayershell.layer: WlrLayer.Bottom
    // Bottom-left is the one corner nothing else claims: the quick-launch strip
    // is top-left, the system monitor top-right, the big clock bottom-right.
    anchors { bottom: true; left: true }
    // Clear of the visualiser when both are on, on the same terms and with the
    // same number BigClock uses at the other end of that strip.
    margins { bottom: WidgetState.visualizer ? 124 : 24; left: 20 }
    Behavior on margins.bottom { NumberAnimation { duration: Theme.animNormal } }

    implicitWidth: 260
    implicitHeight: 210

    exclusiveZone: 0
    focusable: false
    // No `mask: Region {}`, deliberately, and this is the one widget where that
    // is right: the others are pictures and must let clicks through, this one is
    // a button.
    color: "transparent"

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Theme.popupBg
        border.color: hover.containsMouse ? Theme.yellow : Theme.magenta
        border.width: 1
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

        // The dog-ear. Cheap, and it is what says "note" before a word is read.
        Rectangle {
            anchors { top: parent.top; right: parent.right; margins: 1 }
            width: 22
            height: 22
            radius: 6
            color: Theme.yellow
            opacity: 0.85
        }

        Text {
            id: head
            anchors { top: parent.top; left: parent.left; margins: 10 }
            text: "note"
            color: Theme.yellow
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }

        Text {
            anchors {
                top: head.bottom; topMargin: 8
                left: parent.left; leftMargin: 10
                right: parent.right; rightMargin: 10
                bottom: foot.top; bottomMargin: 6
            }
            text: PostItState.note.trim() !== "" ? PostItState.note
                                                 : "click to write something"
            color: PostItState.note.trim() !== "" ? Theme.fg : Theme.fgDim
            font.family: Theme.fontFamily
            font.pixelSize: 12
            wrapMode: Text.Wrap
            // NO elide, and that is a fix rather than an omission. Eliding
            // WRAPPED text needs a height to elide against, and this one comes
            // from anchors — so the first layout runs at height 0, every line
            // elides away to nothing, and the result is never recomputed once
            // the anchors resolve. Any later change to `text` re-lays it out,
            // which is why this looked fine in every test that wrote a note and
            // was blank in the one case that matters: a first run, where nobody
            // has written anything and the string never changes again. A note
            // that says "click to write something" and renders NOTHING is worse
            // than a long note losing its ellipsis, which is all clip costs.
            clip: true
        }

        Text {
            id: foot
            anchors { bottom: parent.bottom; left: parent.left; margins: 8 }
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
}
