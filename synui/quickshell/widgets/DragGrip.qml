import QtQuick
import ".."

/*
 * DragGrip — the 18 pixels of a desktop widget you are allowed to click.
 *
 * For the reporting widgets this is the WHOLE input region of the surface
 * (`mask: Region { item: grip }` in WidgetFrame), which is the only way they
 * can be draggable without going back to being invisible rectangles that eat
 * the desktop's right-click menu. It is small on purpose, and it is why it
 * lights up as hard as it does on hover: a target this size has to announce
 * itself.
 *
 *
 * WHY THE DRAG IS NOT drag.target
 *
 * MouseArea's built-in drag records where its target started AT PRESS TIME and
 * moves it relative to that. Here the target moves once between press and the
 * first motion event, for reasons that have nothing to do with the pointer: the
 * surface expands to the usable area and the card is handed absolute
 * coordinates in it (see WidgetFrame). Qt's recorded start position is stale
 * from that moment and the card jumps by however far the widget sat from its
 * corner.
 *
 * So the movement is applied here, as `card.x += (mouse.x - grabX)`. That looks
 * like the naive accumulation WidgetFrame's header warns against, and is not:
 * the surface is nailed down for the length of the drag, so mouse.x is measured
 * against a card whose position we set ourselves, synchronously. Substituting
 * it back through, every event resolves to `pointer - grab offset` with the
 * card's current position cancelling out — which means it is exact, it is
 * self-correcting rather than accumulating, and it recovers by itself the
 * moment a clamped edge stops clamping.
 */
Item {
    id: grip

    property var frame
    readonly property bool dragging: frame.dragging

    width: 18
    height: 18

    // The grab offset inside the card, and whether it is trustworthy yet. It is
    // taken again after the surface expands, because that expansion moves the
    // card underneath a pointer that did not ask it to.
    property real grabX: 0
    property real grabY: 0
    property bool armed: false
    property bool moved: false

    // Re-arm on the way in AND on the way out: the collapse puts the card back
    // to the pad corner, and a grip that stayed armed across it would apply
    // that jump to whatever happened next.
    onDraggingChanged: grip.armed = false
    Connections {
        target: grip.frame
        function onExpandedChanged() { grip.armed = false }
    }

    // A plate behind the dots, so the hit target is visible as a target rather
    // than only inferable from the dots sitting in it.
    Rectangle {
        anchors.fill: parent
        radius: 3
        color: grip.dragging ? Theme.yellow : grip.frame.accent
        border.width: 1
        border.color: grip.dragging ? Theme.yellow : grip.frame.accent
        // Invisible until wanted: at rest this is a widget, not a control.
        opacity: grip.dragging ? 0.30 : (ma.containsMouse ? 0.22 : 0.0)
        Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
    }

    Grid {
        anchors.centerIn: parent
        columns: 3
        rows: 3
        spacing: 2
        Repeater {
            model: 9
            delegate: Rectangle {
                width: 2; height: 2
                color: grip.dragging ? Theme.yellow
                                     : ma.containsMouse ? grip.frame.accent : Theme.fgDim
                opacity: grip.dragging || ma.containsMouse ? 1.0 : 0.55
                Behavior on color   { ColorAnimation  { duration: Theme.animFast } }
                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
            }
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: grip.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        acceptedButtons: Qt.LeftButton

        onPressed: (m) => {
            grip.grabX = m.x
            grip.grabY = m.y
            grip.moved = false
            grip.armed = true
        }

        onPositionChanged: (m) => {
            if (!ma.pressed) return

            // Nothing happens until the pointer has actually gone somewhere.
            // Expanding the surface on a stray click would flash the drag
            // guides across the screen for a frame and cost a full-screen
            // buffer to do it.
            if (!grip.frame.dragging) {
                if (Math.abs(m.x - grip.grabX) < 3 && Math.abs(m.y - grip.grabY) < 3) return
                grip.moved = true
                grip.frame.beginDrag()
                return
            }

            // Re-anchoring the surface is a round trip. Until it lands there is
            // no big window to position the card in.
            if (!grip.frame.expanded) return

            if (!grip.armed) {
                grip.grabX = m.x
                grip.grabY = m.y
                grip.armed = true
                return
            }

            grip.frame.moveCard(m.x - grip.grabX, m.y - grip.grabY)
        }

        onReleased: {
            if (grip.moved) grip.frame.endDrag()
            else            grip.frame.cancelDrag()
            grip.moved = false
        }

        // A grab lost to a VT switch or a lock must not leave the widget stuck
        // as a full-screen surface with the guides up.
        onCanceled: {
            grip.frame.cancelDrag()
            grip.moved = false
        }

        // Put it back where it was designed to go. The only way out of a widget
        // dropped somewhere unreachable, and the reason WidgetLayout stores an
        // absent entry rather than the home numbers.
        onDoubleClicked: grip.frame.resetPosition()
    }
}
