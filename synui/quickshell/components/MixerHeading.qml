import QtQuick
import ".."

/*
 * A section heading in the mixer, with the hairline that separates it from the
 * rows above. Its own component only so the four sections cannot drift apart.
 *
 * Column skips items whose `visible` is false, so a section that hides itself
 * (an empty Recording list) leaves no gap behind.
 */
Item {
    id: heading

    property string text: ""

    implicitHeight: label.implicitHeight + 12

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 4 }
        height: 1
        color: Theme.hoverBg
    }

    Text {
        id: label
        anchors { left: parent.left; bottom: parent.bottom }
        // QML has no text-transform, so the shouting is done here rather than at
        // four call sites that could each forget.
        text: heading.text.toUpperCase()
        color: Theme.magenta
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.letterSpacing: 1
    }
}
