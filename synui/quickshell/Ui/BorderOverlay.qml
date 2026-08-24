import QtQuick
import qs.Commons

/*
 * BorderOverlay — a border whose four sides are not the same width.
 *
 * Rectangle.border is one width on all four sides, so anything else has to be
 * drawn. Four rectangles rather than a shader: a per-side border is a rare case
 * (a row with a rule only along its bottom) and four flat fills are cheaper and
 * sharper than anything clever.
 *
 * ⚠ THE CORNERS ARE SQUARE, and on a rounded surface that shows. Their version
 * paints a rounded path; this one is honest about what it is — per-side borders
 * are used on rows and separators, which have no radius, and a widget asking for
 * both gets a border that meets the corner rather than following it.
 */
Item {
    id: root

    property var  borderSpec: Border.none()
    property real radius: 0

    readonly property color c: Border.color(root.borderSpec)

    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Border.top(root.borderSpec)
        visible: height > 0
        color: root.c
    }
    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: Border.bottom(root.borderSpec)
        visible: height > 0
        color: root.c
    }
    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        width: Border.left(root.borderSpec)
        visible: width > 0
        color: root.c
    }
    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: Border.right(root.borderSpec)
        visible: width > 0
        color: root.c
    }
}
