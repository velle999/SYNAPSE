// item_hover_info.qml — the hover panel must wait, follow, and let go.
//
// Run by synfiles_test.sh through Qt 6's qmltestrunner. ⚠ NOT /usr/bin/
// qmltestrunner, which is Qt 5's and rejects these unversioned imports with
// "Library import requires a version" — on a stderr the runner then eats, so
// it looks like the test silently did nothing.
//
// This is a REPLICA of synfiles.qml's hover graph, because the real file is a
// Quickshell document and cannot be loaded by a plain Qt test. The static
// greps in synfiles_test.sh are what keep the two in step.
//
// Three things a hover panel gets wrong, all of which need a real pointer to
// see:
//
//   - it appears WHILE THE POINTER IS PASSING. Crossing a grid brushes a dozen
//     icons, and a panel on each is a strobe. Hence the delay, and hence
//     `useDelay: false` as the negative control — a test that cannot fail
//     proves nothing.
//   - it stays after the pointer has moved to a DIFFERENT item, because the
//     enter of the new one raced the exit of the old.
//   - it stays put while the LIST MOVES UNDER IT. The panel is anchored to a
//     place on screen, so a scroll leaves it pointing at whatever has slid
//     into that place — and when the scroll is small enough that the same item
//     stays under the pointer, Qt sends no exit at all and nothing else would
//     ever take it down.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest

Item {
    id: root
    width: 500; height: 400

    // false = no delay at all, kept as the negative control.
    property bool useDelay: true

    property var tipRow: null
    property var tipWant: null
    property real tipX: 0
    property real tipY: 0

    Timer {
        id: tipDelay
        interval: root.useDelay ? 500 : 0
        onTriggered: root.tipRow = root.tipWant
    }

    function askInfo(row, pt) {
        if (!row) return
        root.tipWant = row
        root.tipX = pt.x
        root.tipY = pt.y
        tipDelay.restart()
    }
    // Matched by PATH, not by identity: a refresh rebuilds every row object.
    function dropInfo(row) {
        const f = row ? row.full : ""
        if (root.tipWant && root.tipWant.full === f) { root.tipWant = null; tipDelay.stop() }
        if (root.tipRow && root.tipRow.full === f) root.tipRow = null
    }
    function hideInfo() {
        root.tipWant = null
        root.tipRow = null
        tipDelay.stop()
    }

    // Long enough that a scroll can put the first row far outside the view's
    // cache buffer, which is what DESTROYS the delegate rather than merely
    // moving it — see test_scrolling_hides.
    ListModel {
        id: files
        Component.onCompleted: {
            const n = "abcdefghijklmnopqrstuvwxyz"
            for (let i = 0; i < n.length; i++)
                files.append({ full: "/" + n[i] + ".txt" })
        }
    }

    GridView {
        id: grid
        x: 0; y: 0; width: 400; height: 200
        cellWidth: 100; cellHeight: 100
        clip: true
        model: files

        onContentYChanged: root.hideInfo()

        delegate: Item {
            id: cell
            required property string full
            width: grid.cellWidth; height: grid.cellHeight

            MouseArea {
                id: cellMa
                anchors.fill: parent
                hoverEnabled: true
                onContainsMouseChanged: {
                    if (cellMa.containsMouse)
                        root.askInfo({ full: cell.full },
                                     cellMa.mapToItem(null, cellMa.mouseX, cellMa.mouseY))
                    else
                        root.dropInfo({ full: cell.full })
                }
                onPressed: root.hideInfo()
            }
        }
    }

    // The panel, only as far as this test cares: whether it is up and what it
    // is about.
    Rectangle {
        id: tipPanel
        visible: root.tipRow !== null
        z: 400
        width: 160; height: 60
        x: Math.max(8, Math.min(root.tipX + 14, root.width - width - 8))
        y: root.tipY + 22 + height > root.height - 8
           ? Math.max(8, root.tipY - 12 - height)
           : root.tipY + 22
        color: "#222"
    }

    TestCase {
        name: "HoverInfo"
        when: windowShown

        function init() {
            root.useDelay = true
            root.hideInfo()
            grid.contentY = 0
            // Off every cell to start, or the previous case's exit is still
            // the pointer's most recent event.
            mouseMove(root, 450, 350)
            wait(50)
        }

        function test_rest_shows() {
            mouseMove(root, 50, 50)
            wait(200)
            verify(!tipPanel.visible, "the panel appeared before the delay was up")
            wait(500)
            verify(tipPanel.visible, "resting the pointer on an item showed nothing")
            compare(root.tipRow.full, "/a.txt")
        }

        // ⛔ THE ONE THE DELAY EXISTS FOR. Crossing the grid to reach something
        // else must not light up every icon on the way.
        function test_passing_shows_nothing() {
            mouseMove(root, 50, 50);  wait(120)
            mouseMove(root, 150, 50); wait(120)
            mouseMove(root, 250, 50); wait(120)
            mouseMove(root, 450, 350)
            wait(600)
            verify(!tipPanel.visible, "a pointer passing across the grid raised a panel")
        }

        // The negative control: with no delay, that same pass DOES raise one.
        // Without this the case above would pass on a window that never shows
        // a panel at all.
        function test_passing_would_show_without_the_delay() {
            root.useDelay = false
            mouseMove(root, 50, 50);  wait(120)
            mouseMove(root, 150, 50); wait(120)
            verify(tipPanel.visible, "the no-delay control never showed a panel — the test cannot fail")
        }

        function test_moving_on_follows() {
            mouseMove(root, 50, 50)
            wait(700)
            compare(root.tipRow.full, "/a.txt")
            mouseMove(root, 150, 50)
            wait(700)
            verify(tipPanel.visible, "the panel went out when the pointer moved to the next item")
            compare(root.tipRow.full, "/b.txt")
        }

        function test_leaving_hides() {
            mouseMove(root, 50, 50)
            wait(700)
            verify(tipPanel.visible)
            mouseMove(root, 450, 350)
            wait(100)
            verify(!tipPanel.visible, "the panel stayed up after the pointer left")
        }

        // ⛔ A SMALL SCROLL, ON PURPOSE, AND IT IS THE ONLY ONE THAT PROVES
        // ANYTHING. Move the content by a whole row and the item under the
        // pointer changes, Qt delivers an ordinary exit, and the panel closes
        // by itself — so that version of this test passes against code with no
        // guard at all, which is how it was first written here.
        //
        // Ten pixels keeps the SAME item under the pointer. Nothing is entered
        // and nothing is exited, and without the views calling hideInfo() the
        // panel stays exactly where it was while the list slides out from
        // under it.
        function test_scrolling_hides() {
            mouseMove(root, 50, 50)
            wait(700)
            verify(tipPanel.visible)
            grid.contentY = 10
            wait(150)
            verify(!tipPanel.visible, "a scroll left the panel behind, anchored to a place the list has moved past")
        }

        // A press means the pointer is being used for something else.
        function test_pressing_hides() {
            mouseMove(root, 50, 50)
            wait(700)
            verify(tipPanel.visible)
            mousePress(root, 50, 50)
            mouseRelease(root, 50, 50)
            wait(50)
            verify(!tipPanel.visible, "the panel stayed up under a click")
        }

        // Near the right and bottom edges the natural place is off-screen, and
        // a panel drawn there is a panel nobody can read.
        function test_stays_inside_the_window() {
            root.tipRow = { full: "/a.txt" }
            root.tipX = root.width - 4
            root.tipY = root.height - 4
            wait(50)
            verify(tipPanel.x + tipPanel.width <= root.width,
                   "the panel runs off the right edge")
            verify(tipPanel.y + tipPanel.height <= root.height,
                   "the panel runs off the bottom edge")
            verify(tipPanel.x >= 0 && tipPanel.y >= 0,
                   "the panel was pushed off the top or left instead")
        }
    }
}
