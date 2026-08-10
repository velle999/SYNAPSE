// syn-settings — SynapseOS system settings.
//
// A reader first. Every pane shows what the system ACTUALLY reports, from the
// tool that owns that answer, with the layer it came from — because the
// configuration bugs this OS has cost the most time on were invisible rather
// than wrong: units installed and disabled, a console keymap set while the
// desktop read the xkb one, a shipped sysctl overridden from /etc.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Quickshell
import Quickshell.Io

FloatingWindow {
    id: root

    title: "SYNAPSE Settings"
    implicitWidth: 900
    implicitHeight: 620
    // Nav is a fixed 190 and the widest pane has five columns. Below this the
    // table cannot hold its shape, and the lesson from synfiles and synpkg on
    // 2026-08-10 is that a layout with no floor does not degrade — it paints
    // over itself.
    minimumSize: Qt.size(640, 420)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNSETTINGS_BIN") || "syn-settings"

    // ── Theme ───────────────────────────────────────────────────────────────
    readonly property color cBg:     "#14161c"
    readonly property color cPanel:  "#1b1e26"
    readonly property color cText:   "#d7dae3"
    readonly property color cDim:    "#8b90a0"
    readonly property color cAccent: "#7aa2f7"
    readonly property color cWarn:   "#e0af68"
    readonly property color cBad:    "#f7768e"
    readonly property color cGood:   "#9ece6a"
    readonly property string uiFont: "monospace"
    function wash(a) { return Qt.rgba(1, 1, 1, a) }

    // ── State ───────────────────────────────────────────────────────────────
    readonly property var panes: [
        { id: "display",   label: "Display",  blurb: "connectors as the kernel sees them, beside what the compositor drives" },
        { id: "region",    label: "Keyboard & Region", blurb: "layout, locale, time zone and whether the clock is actually disciplined" },
        { id: "network",   label: "Network",  blurb: "interfaces, radios, and whether anything is filtering traffic" },
        { id: "bluetooth", label: "Bluetooth", blurb: "the adapter, both kinds of radio block, and what is paired" },
        { id: "power",     label: "Power & Sleep", blurb: "the units a working suspend depends on, and what the last one did" },
        { id: "system",    label: "System",   blurb: "identity, and which layer each configuration file comes from" }
    ]
    property string pane: Quickshell.env("SYNSETTINGS_PANE") || "display"
    property var cols: []
    property var rows: []
    property bool loading: false
    property string status: ""

    function paneMeta(id) {
        for (const p of root.panes) if (p.id === id) return p
        return root.panes[0]
    }

    // ── Loading ─────────────────────────────────────────────────────────────
    Process {
        id: readProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n").filter(l => l !== "")
                if (lines.length === 0) {
                    root.cols = []; root.rows = []
                    root.loading = false
                    root.status = "nothing reported"
                    return
                }
                // First line names the columns. Taking the header from the
                // data rather than hard-coding it per pane means a new column
                // in the C reader shows up here without a QML change — and,
                // more to the point, the GUI cannot quietly disagree with what
                // the command line prints.
                root.cols = lines[0].split("\t")
                const out = []
                for (let i = 1; i < lines.length; i++)
                    out.push(lines[i].split("\t"))
                root.rows = out
                root.loading = false
                root.status = ""
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = this.text.split("\n")[0]
        }
    }

    function reload() {
        root.loading = true
        root.status = ""
        readProc.command = [root.bin, "--rec", root.pane]
        readProc.running = true
    }

    Component.onCompleted: root.reload()

    // A value the eye should stop on. Deliberately narrow: only states that
    // mean something is off, so colour stays informative instead of decorative.
    function tone(col, val) {
        const v = (val || "").toLowerCase()
        if (v === "absent" || v === "not installed" || v === "not executable"
            || v === "failed" || v === "not driven")
            return root.cBad
        if (v === "disabled" || v === "inactive" || v === "unknown"
            || v === "disconnected" || v === "no")
            return root.cWarn
        if (v === "enabled" || v === "active" || v === "connected"
            || v === "executable" || v === "present" || v === "yes")
            return root.cGood
        return root.cText
    }

    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // ── Nav ─────────────────────────────────────────────────────────────
        Rectangle {
            id: nav
            anchors { top: parent.top; left: parent.left; bottom: parent.bottom }
            width: 190
            color: root.cPanel

            Column {
                anchors { top: parent.top; left: parent.left; right: parent.right
                          topMargin: 14 }

                Text {
                    x: 16
                    text: "SYNAPSE Settings"
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: 14; bold: true }
                }
                Item { width: 1; height: 14 }

                Repeater {
                    model: root.panes
                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        width: nav.width
                        height: 32
                        color: navItem.modelData.id === root.pane ? root.wash(0.10)
                             : navMa.containsMouse ? root.wash(0.05) : "transparent"

                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: 3
                            color: root.cAccent
                            visible: navItem.modelData.id === root.pane
                        }
                        Text {
                            anchors { left: parent.left; leftMargin: 16
                                      right: parent.right; rightMargin: 10
                                      verticalCenter: parent.verticalCenter }
                            elide: Text.ElideRight
                            text: navItem.modelData.label
                            color: navItem.modelData.id === root.pane ? root.cText : root.cDim
                            font { family: root.uiFont; pixelSize: 12 }
                        }
                        MouseArea {
                            id: navMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.pane = navItem.modelData.id; root.reload() }
                        }
                    }
                }
            }

            Text {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                          margins: 12 }
                wrapMode: Text.WordWrap
                text: "Reads the live system. Writes go through localectl, "
                    + "timedatectl and systemctl, which do their own "
                    + "authorisation."
                color: root.cDim
                font { family: root.uiFont; pixelSize: 9 }
            }
        }

        // ── Header ──────────────────────────────────────────────────────────
        Item {
            id: header
            anchors { top: parent.top; left: nav.right; right: parent.right }
            height: 64

            Text {
                id: headTitle
                anchors { left: parent.left; leftMargin: 18; top: parent.top; topMargin: 14 }
                text: root.paneMeta(root.pane).label
                color: root.cText
                font { family: root.uiFont; pixelSize: 15; bold: true }
            }
            Text {
                anchors { left: parent.left; leftMargin: 18
                          right: refreshBtn.left; rightMargin: 12
                          top: headTitle.bottom; topMargin: 4 }
                elide: Text.ElideRight
                text: root.paneMeta(root.pane).blurb
                color: root.cDim
                font { family: root.uiFont; pixelSize: 10 }
            }

            Rectangle {
                id: refreshBtn
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                width: 84; height: 26; radius: 4
                color: refreshMa.containsMouse ? root.wash(0.14) : root.wash(0.07)

                Text {
                    anchors.centerIn: parent
                    text: root.loading ? "reading…" : "Refresh"
                    color: root.cText
                    font { family: root.uiFont; pixelSize: 11 }
                }
                MouseArea {
                    id: refreshMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.reload()
                }
            }
        }

        // ── Column headings ─────────────────────────────────────────────────
        Item {
            id: headRow
            anchors { top: header.bottom; left: nav.right; right: parent.right }
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            height: root.cols.length > 0 ? 22 : 0
            clip: true

            Row {
                anchors.fill: parent
                Repeater {
                    model: root.cols
                    delegate: Text {
                        required property var modelData
                        required property int index
                        // The first column is the identifier and gets the room;
                        // the last is prose and takes whatever is left. Fixed
                        // fractions rather than content-derived widths, so the
                        // table does not reflow every refresh and make a
                        // changed value look like a moved row.
                        width: headRow.colWidth(index)
                        elide: Text.ElideRight
                        text: modelData
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: 10; bold: true }
                    }
                }
            }

            function colWidth(i) {
                const n = root.cols.length
                if (n <= 1) return headRow.width
                const first = Math.min(220, headRow.width * 0.30)
                const rest = Math.max(60, (headRow.width - first) / (n - 1))
                return i === 0 ? first : rest
            }
        }

        Rectangle {
            id: headRule
            anchors { top: headRow.bottom; left: nav.right; right: parent.right }
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            height: 1
            color: root.wash(0.12)
            visible: root.cols.length > 0
        }

        // ── Rows ────────────────────────────────────────────────────────────
        ListView {
            id: table
            anchors {
                top: headRule.bottom; topMargin: 4
                left: nav.right; leftMargin: 18
                right: parent.right; rightMargin: 18
                bottom: statusBar.top; bottomMargin: 6
            }
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: dataRow
                required property var modelData
                required property int index
                width: table.width
                height: 26
                color: rowMa.containsMouse ? root.wash(0.05)
                     : (dataRow.index % 2 === 1 ? root.wash(0.02) : "transparent")

                Row {
                    anchors { left: parent.left; right: parent.right
                              verticalCenter: parent.verticalCenter }
                    Repeater {
                        model: dataRow.modelData
                        delegate: Text {
                            required property var modelData
                            required property int index
                            width: headRow.colWidth(index)
                            elide: Text.ElideRight
                            text: modelData
                            color: index === 0 ? root.cText
                                               : root.tone(root.cols[index] || "", modelData)
                            font { family: root.uiFont; pixelSize: 11 }
                        }
                    }
                }
                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }
        }

        // Nothing to show is a state worth naming. An empty table that says
        // nothing looks identical to one that failed.
        Text {
            anchors.centerIn: table
            visible: !root.loading && root.rows.length === 0
            text: "This pane reported nothing.\nRun `" + root.bin + " --rec "
                  + root.pane + "` to see why."
            horizontalAlignment: Text.AlignHCenter
            color: root.cDim
            font { family: root.uiFont; pixelSize: 11 }
        }

        // ── Status ──────────────────────────────────────────────────────────
        Rectangle {
            id: statusBar
            anchors { left: nav.right; right: parent.right; bottom: parent.bottom }
            height: 22
            color: root.cPanel

            Text {
                id: statusLeft
                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                width: Math.min(implicitWidth, parent.width * 0.6 - 12)
                elide: Text.ElideRight
                text: root.status !== "" ? root.status
                    : root.loading ? "reading…"
                    : root.rows.length + (root.rows.length === 1 ? " row" : " rows")
                color: root.status !== "" ? root.cWarn : root.cDim
                font { family: root.uiFont; pixelSize: 10 }
            }
            Text {
                anchors { left: statusLeft.right; leftMargin: 12
                          right: parent.right; rightMargin: 12
                          verticalCenter: parent.verticalCenter }
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                text: root.bin + " --rec " + root.pane
                color: root.cDim
                font { family: root.uiFont; pixelSize: 10 }
            }
        }
    }
}
