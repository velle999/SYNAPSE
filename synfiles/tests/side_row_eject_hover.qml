// side_row_eject_hover.qml — the eject button must stay put under the pointer.
//
// Run by synfiles_test.sh through Qt 6's qmltestrunner. ⚠ NOT /usr/bin/
// qmltestrunner, which is Qt 5's and rejects these unversioned imports with
// "Library import requires a version" — on stderr the runner then eats, so a
// Qt 5 run looks exactly like a test that found nothing wrong.
//
// A REPLICA of synfiles.qml's SideRow, because the real file is a Quickshell
// document a plain Qt test cannot load. The static greps in synfiles_test.sh
// are what keep the two in step.
//
// ── the bug ────────────────────────────────────────────────────────────────
// Reported as "the eject button kind of moves a bit when you try to push or
// hover over it". It was two faults on the same row, and both are layout:
//
//  1. THE BUTTON WAS PULLED OUT FROM UNDER THE POINTER. Qt hands a hover
//     enter/exit pair to exactly ONE item — the topmost. The glyph carried its
//     own hoverEnabled MouseArea, so reaching it `exited` the ROW's MouseArea,
//     and the glyph was shown on `rowMa.containsMouse`. It went invisible at
//     the moment of arrival, hover fell back to the row, and it came straight
//     back: a flicker under a resting hand, and a press that landed in one of
//     the gaps did nothing. Same fault as the Open-with flyout — see
//     tests/ctx_flyout_hover.qml.
//  2. THE TEXT BESIDE IT REALLY DID SLIDE. The fill percentage chose its own
//     right margin off the same `containsMouse` — 10 normally, 26 while the
//     button showed — so it, and the elided label behind it, jumped 16px
//     sideways every time the pointer crossed the row.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest

Item {
    id: root
    width: 400; height: 300

    // false = the wiring synfiles 0.1.0-62 shipped, kept as the negative
    // control: a test that cannot fail proves nothing.
    property bool useHandler: true
    // Which glyph the button is showing. ⏏ on a mounted volume, ▸ on one that
    // is not — two characters with different advances, which is why a Text
    // sized to its own glyph moved its own hit target.
    property bool mounted: true
    property int  ejects: 0

    Rectangle {
        id: sideRow
        x: 0; y: 0
        width: 220; height: 38
        color: "#20242c"

        readonly property string trailing: root.mounted ? "⏏" : "▸"
        readonly property bool hovered: root.useHandler ? rowHover.hovered
                                                        : rowMa.containsMouse

        HoverHandler { id: rowHover; enabled: root.useHandler }

        Rectangle {                       // stands in for the volume icon
            id: sideIcon
            x: 14; y: 6; width: 16; height: 16
            color: "#888"
        }

        Text {
            id: sideLabel
            anchors {
                left: sideIcon.right; leftMargin: 8
                right: meterPct.left; rightMargin: 4
                verticalCenter: sideIcon.verticalCenter
            }
            text: "KINGSTON"
            elide: Text.ElideRight
            color: "#ddd"
        }

        // THE THING THAT MOVED. Anchored to the fixed gutter in the shipped
        // wiring; to a margin that depends on hover in the old one.
        Text {
            id: meterPct
            anchors {
                right: root.useHandler ? sideBtns.left : sideRow.right
                rightMargin: root.useHandler
                             ? 4 : (rowMa.containsMouse ? 26 : 10)
                verticalCenter: sideIcon.verticalCenter
            }
            text: "63%"
            color: "#999"
        }

        MouseArea {
            id: rowMa
            anchors.fill: parent
            hoverEnabled: true
        }

        // ── the shipped wiring ─────────────────────────────────────────────
        Row {
            id: sideBtns
            visible: root.useHandler
            anchors {
                right: parent.right; rightMargin: 6
                verticalCenter: sideIcon.verticalCenter
            }
            spacing: 2

            Rectangle {
                id: newBtn
                width: 20; height: 20; radius: 4
                // opacity, NOT visible: a Row drops an invisible child, and the
                // gutter collapsing is what moved the text beside it.
                opacity: sideRow.hovered ? 1 : 0
                enabled: sideRow.hovered
                color: newMa.pressed ? "#555" : (newMa.containsMouse ? "#3a3a3a"
                                                                     : "transparent")
                Text {
                    anchors.centerIn: parent
                    text: sideRow.trailing
                    color: newMa.containsMouse ? "#6479ff" : "#999"
                }
                MouseArea {
                    id: newMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.ejects++
                }
            }
        }

        // ── 0.1.0-62's wiring, the negative control ────────────────────────
        Text {
            id: oldBtn
            visible: !root.useHandler && rowMa.containsMouse
            anchors { right: parent.right; rightMargin: 8
                      verticalCenter: parent.verticalCenter }
            text: sideRow.trailing
            color: "#999"
            MouseArea {
                id: oldMa
                anchors { fill: parent; margins: -5 }
                hoverEnabled: true
                onClicked: root.ejects++
            }
        }
    }

    TestCase {
        name: "SideRowEject"
        when: windowShown

        // Dead centre of the shipped 20x20 button: x 194..214, centred on the
        // icon's line at y 14. Inside the old Text's -5 slop too, so one point
        // aims at both wirings.
        readonly property int bx: 204
        readonly property int by: 14
        // Somewhere on the row that is NOT a button.
        readonly property int rx: 60
        readonly property int ry: 19

        function init() {
            root.useHandler = true
            root.mounted = true
            root.ejects = 0
            mouseMove(root, 350, 250)     // clear of the row
            wait(50)
        }

        // The reported path: hover the row, move out to the button, PAUSE the
        // way a hand pauses before pressing something small, then press.
        function test_1_the_button_survives_the_pointer_reaching_it() {
            mouseMove(root, rx, ry)
            verify(sideRow.hovered, "the row should be hovered")
            mouseMove(root, bx, by)
            compare(newBtn.opacity, 1,
                    "the button vanished the moment the pointer reached it")
            wait(400)                     // a resting hand
            compare(newBtn.opacity, 1,
                    "the button did not survive a resting pointer")
            mouseClick(root, bx, by)
            compare(root.ejects, 1, "the press never reached the button")
        }

        // The negative control, sampled BEFORE a frame can run: the flicker
        // makes any later sample a coin toss, but the arrival itself is
        // deterministic — hover goes to the topmost item and the row's
        // MouseArea is left saying the pointer is somewhere else.
        function test_2_the_old_wiring_loses_the_button_on_arrival() {
            root.useHandler = false
            mouseMove(root, rx, ry)
            verify(oldBtn.visible, "the old button should show on a hovered row")
            mouseMove(root, bx, by)
            verify(!oldBtn.visible,
                   "0.1.0-62's containsMouse wiring was expected to hide the "
                   + "button under the pointer")
        }

        // The other half of "it moves a bit": nothing beside the button may
        // shift when the button appears.
        function test_3_nothing_slides_when_the_pointer_crosses_the_row() {
            const away = meterPct.x
            mouseMove(root, rx, ry)
            compare(meterPct.x, away,
                    "the fill percentage slid sideways when the row was hovered")
            mouseMove(root, bx, by)
            compare(meterPct.x, away,
                    "the fill percentage slid sideways when the button was hovered")
        }

        function test_4_the_old_wiring_did_slide() {
            root.useHandler = false
            const away = meterPct.x
            mouseMove(root, rx, ry)
            verify(meterPct.x !== away,
                   "the old margin was expected to move the percentage")
        }

        // ⏏ and ▸ have different advances. A Text sized to its own glyph
        // therefore moved and resized its own hit target the moment a disk
        // mounted or unmounted; a fixed box does not.
        function test_5_the_hit_target_does_not_move_when_the_glyph_changes() {
            const x = newBtn.x, w = newBtn.width, h = newBtn.height
            const gy = newBtn.mapToItem(root, 0, 0).y
            root.mounted = false
            compare(newBtn.x, x, "the button moved when the glyph changed")
            compare(newBtn.width, w, "the button resized when the glyph changed")
            compare(newBtn.height, h, "the button resized when the glyph changed")
            compare(newBtn.mapToItem(root, 0, 0).y, gy,
                    "the button changed line when the glyph changed")
            // ...and it is still the thing under the same pixel.
            mouseMove(root, bx, by)
            mouseClick(root, bx, by)
            compare(root.ejects, 1, "the mount arrow is not where the eject was")
        }

        function test_6_leaving_the_row_still_hides_it() {
            mouseMove(root, bx, by)
            compare(newBtn.opacity, 1, "should be showing on arrival")
            mouseMove(root, 350, 250)
            wait(100)
            compare(newBtn.opacity, 0, "the button should go away with the pointer")
        }
    }
}
