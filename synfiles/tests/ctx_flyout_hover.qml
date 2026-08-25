// ctx_flyout_hover.qml — the "Open with" flyout must survive a resting pointer.
//
// Run by synfiles_test.sh through Qt 6's qmltestrunner. ⚠ NOT /usr/bin/
// qmltestrunner, which is Qt 5's and rejects these unversioned imports with
// "Library import requires a version" — on stderr that the runner then eats,
// so it looks like the test silently did nothing.
//
// This is a REPLICA of synfiles.qml's ctxMenu/ctxSub hover graph, because the
// real file is a Quickshell document and cannot be loaded by a plain Qt test.
// The static greps in synfiles_test.sh are what keep the two in step: they
// fail if the shipped flyout goes back to a panel-filling hoverEnabled
// MouseArea, which is the wiring `useHandler: false` reproduces here.
//
// The bug it exists for: Qt hands a hover enter/exit pair to exactly ONE item,
// the topmost under the pointer. A MouseArea filling the flyout therefore got
// `exited` the instant the pointer reached an ENTRY inside it — the entry's
// own MouseArea took the hover — so the close timer restarted and the flyout
// vanished 300ms later with the pointer sitting still on the thing it was
// aimed at. A HoverHandler reports the whole subtree instead.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest

Item {
    id: root
    width: 500; height: 400

    // false = the wiring synfiles 0.1.0-59 shipped, kept as the negative
    // control: a test that cannot fail proves nothing.
    property bool useHandler: true
    property string clicked: ""

    Rectangle {
        id: ctxMenu
        property bool subOpen: false
        x: 40; y: 40; width: 210; height: 130
        color: "#222"
        function closeSub() { ctxMenu.subOpen = false }

        Timer {
            id: subCloseTimer
            interval: 300
            onTriggered: if (!root.useHandler || !ctxSubHover.hovered) ctxMenu.closeSub()
        }

        Column {
            id: ctxCol
            width: parent.width
            Repeater {
                model: [ {act: "copy"}, {act: "submenu"}, {act: "rename"}, {act: "trash"} ]
                delegate: Item {
                    id: ctxItem
                    required property var modelData
                    width: ctxCol.width; height: 26
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: {
                            if (ctxItem.modelData.act === "submenu") {
                                subCloseTimer.stop()
                                ctxMenu.subOpen = true
                            } else if (ctxMenu.subOpen) {
                                subCloseTimer.restart()
                            }
                        }
                    }
                }
            }
        }
    }

    // Overlapping the menu by 4px, aligned with the row that opened it, as
    // the real one does.
    Rectangle {
        id: ctxSub
        visible: ctxMenu.subOpen
        x: ctxMenu.x + ctxMenu.width - 4
        y: ctxMenu.y + 26
        width: 210; height: 104
        color: "#222"
        z: 101

        MouseArea {                       // 0.1.0-59
            anchors.fill: parent
            hoverEnabled: true
            enabled: !root.useHandler
            acceptedButtons: Qt.NoButton
            onEntered: subCloseTimer.stop()
            onExited: subCloseTimer.restart()
        }

        HoverHandler {                    // the fix
            id: ctxSubHover
            enabled: root.useHandler
            onHoveredChanged: {
                if (!root.useHandler) return
                if (ctxSubHover.hovered) subCloseTimer.stop()
                else subCloseTimer.restart()
            }
        }

        Flickable {
            anchors { fill: parent; margins: 4 }
            contentHeight: subCol.implicitHeight
            clip: true
            Column {
                id: subCol
                width: parent.width
                Repeater {
                    model: ["GIMP", "Krita", "syn-edit"]
                    delegate: Item {
                        id: subItem
                        required property string modelData
                        width: subCol.width; height: 26
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.clicked = subItem.modelData
                        }
                    }
                }
            }
        }
    }

    TestCase {
        name: "OpenWithFlyout"
        when: windowShown

        // The reported path: open the flyout, move onto an entry — grazing
        // the row below the trigger on the way, which is unavoidable when the
        // target sits lower than the row that opened it — then pause the way
        // a person reading three application names pauses, then click.
        function reach_and_click(useHandler) {
            root.useHandler = useHandler
            root.clicked = ""
            ctxMenu.closeSub()
            mouseMove(root, 100, 79)     // the "Open with" row
            verify(ctxMenu.subOpen, "the flyout should have opened on hover")
            mouseMove(root, 100, 92)     // graze the row below
            mouseMove(root, 300, 92)     // land on the first entry
            wait(600)                    // read the names
            const stillOpen = ctxMenu.subOpen
            if (stillOpen) mouseClick(root, 300, 92)
            return stillOpen
        }

        function test_1_a_resting_pointer_keeps_the_flyout() {
            verify(reach_and_click(true),
                   "the flyout closed under a pointer resting on one of its entries")
            compare(root.clicked, "GIMP",
                    "the entry under the pointer never got the click")
        }

        // The negative control. If this ever passes, the harness stopped
        // reproducing the bug and test 1 is no longer evidence of anything.
        function test_2_the_old_wiring_still_fails_here() {
            verify(!reach_and_click(false),
                   "0.1.0-59's panel-filling MouseArea was expected to close it")
        }

        // Leaving for real still closes it — the grace period is a grace
        // period, not a permanent reprieve.
        function test_3_a_real_leave_still_closes_it() {
            root.useHandler = true
            ctxMenu.closeSub()
            mouseMove(root, 100, 79)
            mouseMove(root, 300, 92)
            wait(100)
            verify(ctxMenu.subOpen, "should still be open on arrival")
            mouseMove(root, 480, 380)    // clear of both panels
            wait(500)
            verify(!ctxMenu.subOpen, "a real leave should close the flyout")
        }
    }
}
