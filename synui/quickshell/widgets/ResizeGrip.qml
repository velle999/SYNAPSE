import QtQuick
import ".."

/*
 * ResizeGrip — the corner of a desktop widget you can drag to make it bigger.
 *
 * The mirror of DragGrip, and deliberately built the same way: 18 pixels, in
 * the opposite corner, with the same "it is small so it has to announce itself"
 * hover treatment.
 *
 * ⚠ OPPOSITE CORNER, NOT ADJACENT. DragGrip sits top-right. Putting this one
 * bottom-LEFT rather than bottom-right means the two can never be adjacent on
 * any card, however small it gets — on a widget dragged down to its floor the
 * two 18px targets would otherwise share an edge and a press near it would be a
 * coin flip between moving and resizing.
 *
 * ── One number, not two ─────────────────────────────────────────────────────
 *
 * This reports a single `size`, and the widget decides what to do with it. The
 * clock is square by construction — a dial is — so a width and a height would be
 * two numbers that must agree, and the one thing a resize handle must never do
 * is let them disagree. A widget that genuinely wants two axes should grow its
 * own grip rather than make this lie about what it measures.
 *
 * ── Why the delta is against the PRESS, not the last event ──────────────────
 *
 * DragGrip accumulates (`card.x += mouse.x - grabX`) because the surface moves
 * under it between press and first motion, and it re-grabs to cope. Nothing
 * moves under THIS one: the card's top-left corner is anchored for the whole
 * gesture, so the honest measurement is the total travel since the press.
 * Accumulating would let rounding drift over a long drag, and drift on a size
 * is a widget that does not come back to where it started.
 */
Item {
    id: grip

    property var frame
    /* The size when the press landed, and the pointer position it landed at,
     * both in the surface's own coordinates — which do not move for the length
     * of the gesture. */
    property real startSize: 0
    property real startX: 0
    property real startY: 0
    property bool active: false

    width: 18
    height: 18

    Rectangle {
        anchors.fill: parent
        radius: 4
        /* Same hover ramp as DragGrip: invisible until wanted, unmissable once
         * the pointer is on it. */
        color: grip.active ? Qt.rgba(1, 1, 1, 0.28)
             : ma.containsMouse ? Qt.rgba(1, 1, 1, 0.18)
                                : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        /* Two strokes in the corner — the resize glyph every toolkit draws, and
         * the one thing that says "drag me" without a label. Drawn in the same
         * ink the frame's own chrome uses so it belongs to the card rather than
         * sitting on it. */
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                width: 9 - index * 4
                height: 1.5
                radius: 1
                color: Theme.isLight ? Qt.rgba(0, 0, 0, 0.45)
                                     : Qt.rgba(1, 1, 1, 0.55)
                antialiasing: true
                rotation: 45
                x: 3 + index * 2
                y: 12 - index * 3.5
            }
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeBDiagCursor
        acceptedButtons: Qt.LeftButton

        onPressed: (mouse) => {
            grip.startSize = grip.frame.userSize
            /* mapToItem(null, …) is the SURFACE's coordinate space, which is the
             * one thing that stays still here — the grip itself moves as the
             * card resizes under the pointer, so measuring against the grip
             * would feed its own motion back in. */
            const p = ma.mapToItem(null, mouse.x, mouse.y)
            grip.startX = p.x
            grip.startY = p.y
            grip.active = true
            /* Nails the surface to the usable area for the gesture, exactly as
             * a move does — the card is about to change size and the window
             * must not re-anchor around it mid-drag. */
            WidgetLayout.dragging = true
        }

        onPositionChanged: (mouse) => {
            if (!grip.active) return
            const p = ma.mapToItem(null, mouse.x, mouse.y)
            /* Bottom-LEFT grip: dragging left and down both make it bigger, so
             * the x travel is negated. The larger of the two axes wins rather
             * than their sum — a diagonal drag should not grow twice as fast as
             * a straight one. */
            const dx = grip.startX - p.x
            const dy = p.y - grip.startY
            grip.frame.userSize = grip.frame.clampSize(
                grip.startSize + (Math.abs(dx) > Math.abs(dy) ? dx : dy))
        }

        function finish() {
            if (!grip.active) return
            grip.active = false
            WidgetLayout.dragging = false
            /* Persisted on RELEASE, not per event: a resize is one gesture and
             * one decision, and writing the file on every motion would put a
             * hundred atomic renames through it for one drag. */
            WidgetLayout.resize(grip.frame.widgetId, grip.frame.userSize)
            WidgetLayout.save()
        }
        onReleased: ma.finish()
        /* A pointer that leaves the surface mid-drag still ends the gesture.
         * Without it the size stays live against a pointer nobody is watching
         * and the file never gets written. */
        onCanceled: ma.finish()
    }
}
