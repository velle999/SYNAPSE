import QtQuick

/*
 * PointerMoveGate — tells a real pointer movement from a delegate sliding under
 * a pointer that never moved.
 *
 * ⛔ THE PROBLEM IT SOLVES IS NOT OBVIOUS AND IS VERY ANNOYING. A list whose
 * rows move — a Wi-Fi list re-sorting, a filter narrowing it, a keyboard cursor
 * scrolling it — delivers hover events to whatever slid under the stationary
 * pointer. So arrowing down a list makes the selection jump back to wherever the
 * mouse happens to be sitting, once per re-layout, and the keyboard becomes
 * unusable while the pointer is over the panel.
 *
 * Call `reset()` after any change that moves rows, then `moved(item, mouse)`
 * from the row's `onPositionChanged` BEFORE changing the cursor. It answers
 * false for hover that the pointer did not cause.
 *
 * `allowInitialSample()` is the way back: a transition that genuinely came from
 * the pointer calls it so the row under the stationary pointer does get selected.
 */
QtObject {
    id: root

    /* Map into this rather than into each row, or "did it move" is measured in
     * the coordinates of a row that is itself moving. */
    property Item referenceItem: null
    property real threshold: 1
    property bool primed: false
    property bool initialSampleAllowed: false
    property real lastX: 0
    property real lastY: 0

    function reset() {
        root.primed = false
        root.initialSampleAllowed = false
        root.lastX = 0
        root.lastY = 0
    }

    function allowInitialSample() {
        root.reset()
        root.initialSampleAllowed = true
    }

    function moved(item, mouse) {
        if (!item || !mouse) { root.reset(); return false }

        const target = root.referenceItem || item
        const point = item.mapToItem(target, mouse.x, mouse.y)
        const firstSample = !root.primed
        const didMove = !firstSample
            ? (Math.abs(point.x - root.lastX) > root.threshold
               || Math.abs(point.y - root.lastY) > root.threshold)
            : root.initialSampleAllowed

        /* ⚠ THE LAST ACCEPTED POSITION IS KEPT WHILE JITTER IS FILTERED, so a
         * slow deliberate drag made of sub-threshold steps still accumulates
         * into movement instead of being rejected one step at a time for ever. */
        if (firstSample || didMove) {
            root.lastX = point.x
            root.lastY = point.y
        }
        root.primed = true
        root.initialSampleAllowed = false

        return didMove
    }
}
