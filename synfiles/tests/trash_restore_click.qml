// trash_restore_click.qml — a click on Restore must reach Restore.
//
// Run by synfiles_test.sh through Qt 6's qmltestrunner. ⚠ NOT /usr/bin/
// qmltestrunner, which is Qt 5's and rejects these unversioned imports with
// "Library import requires a version" — on stderr the runner then eats, so a
// Qt 5 run looks exactly like a test that found nothing wrong.
//
// A REPLICA of synfiles.qml's file-list row, because the real file is a
// Quickshell document a plain Qt test cannot load. The static greps in
// synfiles_test.sh are what keep the two in step.
//
// ── the bug ────────────────────────────────────────────────────────────────
// Reported as "the restore column looks like a button but doesn't click". The
// row's MouseArea (rowMa) fills the row and is declared AFTER the button, and
// Qt offers a press to the topmost item first — so rowMa took every click, the
// row was selected, and nothing was restored. It never worked, from the first
// release that drew the button (0.1.0-1) to 78. Hover went the same way, so
// the button never lit up either.
//
// The fix is `z: 5` on the button. That brings the other half of the
// hover rule with it: hover reaches ONE item, so the pointer arriving at the
// button EXITS rowMa, and a row highlight read off rowMa alone goes dark under
// the pointer. The row reads restoreMa as well.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest

Item {
    id: root
    width: 500; height: 200

    property int restores: 0
    property int selects: 0

    // The row as it ships now. Children in the real file's order: the button,
    // then the row-wide MouseArea declared below it.
    Rectangle {
        id: newRow
        x: 0; y: 0
        width: 480; height: 30
        readonly property bool hot: newRowMa.containsMouse || newRestoreMa.containsMouse
        color: hot ? "#303640" : "transparent"

        Rectangle {
            id: newBtn
            z: 5
            anchors { right: parent.right; rightMargin: 150; verticalCenter: parent.verticalCenter }
            width: 66; height: 22
            MouseArea {
                id: newRestoreMa
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.restores++
            }
        }
        MouseArea {
            id: newRowMa
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            onClicked: root.selects++
        }
    }

    // What 0.1.0-78 shipped: no z, and the highlight read off rowMa alone.
    // Kept as the negative control — a test that cannot fail proves nothing.
    Rectangle {
        id: oldRow
        x: 0; y: 100
        width: 480; height: 30
        readonly property bool hot: oldRowMa.containsMouse

        Rectangle {
            id: oldBtn
            anchors { right: parent.right; rightMargin: 150; verticalCenter: parent.verticalCenter }
            width: 66; height: 22
            MouseArea {
                id: oldRestoreMa
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.restores++
            }
        }
        MouseArea {
            id: oldRowMa
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            onClicked: root.selects++
        }
    }

    TestCase {
        name: "TrashRestoreClick"
        when: windowShown

        // Scene coordinates of each button's centre, and of a spot on the
        // row well clear of it.
        function centre(item) {
            return item.mapToItem(root, item.width / 2, item.height / 2)
        }

        function init() {
            root.restores = 0
            root.selects = 0
            mouseMove(root, 490, 190)          // clear of both rows
        }

        function test_1_a_click_on_restore_restores() {
            const b = centre(newBtn)
            mouseMove(root, b.x, b.y)
            mouseClick(root, b.x, b.y)
            compare(root.restores, 1, "the click never reached the button")
            compare(root.selects, 0, "the row took the click as well")
        }

        function test_2_the_old_wiring_gave_the_click_to_the_row() {
            const b = centre(oldBtn)
            mouseMove(root, b.x, b.y)
            mouseClick(root, b.x, b.y)
            compare(root.restores, 0,
                    "the negative control restored — this replica no longer "
                    + "reproduces the bug, so test_1 proves nothing")
            compare(root.selects, 1, "the old row should have taken the click")
        }

        function test_3_the_row_stays_lit_under_the_pointer_on_restore() {
            mouseMove(root, 40, 15)
            verify(newRow.hot, "the row should be hovered")
            const b = centre(newBtn)
            mouseMove(root, b.x, b.y)
            verify(newRestoreMa.containsMouse, "the button should be hovered")
            verify(newRow.hot, "the row went dark with the pointer still on it")
        }

        function test_4_rowMa_alone_goes_dark_on_the_button() {
            // Why the highlight reads restoreMa: with the button on top, the
            // row's own MouseArea is EXITED by it.
            mouseMove(root, 40, 15)
            verify(newRowMa.containsMouse, "the row MouseArea should have the pointer")
            const b = centre(newBtn)
            mouseMove(root, b.x, b.y)
            verify(!newRowMa.containsMouse,
                   "hover reached the row under the button — the highlight no "
                   + "longer needs restoreMa, and this rig no longer matches Qt")
        }

        function test_5_the_rest_of_the_row_still_selects() {
            mouseClick(root, 40, 15)
            compare(root.selects, 1, "a click on the name should select the row")
            compare(root.restores, 0, "a click on the name restored")
        }
    }
}
