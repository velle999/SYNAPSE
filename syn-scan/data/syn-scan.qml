// syn-scan.qml — the malware scan window.
//
// ⛔ EVERY VERDICT IS THE BINARY'S. This file runs `syn-scan --rec scan`,
// `engines`, `status` and `quarantine` and draws what comes back. It does not
// decide that a file is infected, and it does not move one — a second opinion
// about malware is a second answer to a question that has one, and the wrong
// answer here is a file somebody deletes.
//
// ⛔ AND IT NEVER OFFERS DELETE. The binary has no such command; the window
// must not grow one. Quarantine, restore, purge-what-we-quarantined.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

// ⛔ NOT qsTr(). quickshell installs no QTranslator, so qsTr() compiles, looks
// up nothing and returns its argument while looking exactly like a marked
// string in review. qml/I18n.qml reads a JSON catalog compiled from the same
// po/ the CLI's .mo comes from.
import "qml"

ShellRoot {
    id: root

    readonly property string bin: Quickshell.env("SYNSCAN_BIN") || "syn-scan"

    property var findings: []
    property var engines: []
    property bool busy: false
    property string status: ""
    property string page: "scan"          // "scan" | "quarantine"
    property var quarantined: []

    // ── the desktop's font and text size ────────────────────────────────────
    //
    // ⛔ NOT THIS WINDOW'S SETTING. The family and the scale belong to the
    // desktop, in the file the bar, synfiles and syn-clean all watch.
    property string uiFont: ""
    property int textScale: 100
    function ui(n) { return Math.round(n * root.textScale / 100) }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        onLoaded: {
            const t = text()
            for (const line of t.split("\n")) {
                const kv = line.split("=")
                if (kv.length < 2) continue
                if (kv[0].trim() === "family") root.uiFont = kv[1].trim()
                if (kv[0].trim() === "scale")  root.textScale = parseInt(kv[1]) || 100
            }
        }
    }

    // ⚠ Fields are tab-separated and escaped by the binary, because a path may
    // legally contain a tab. Unescape in exactly the reverse order it escaped.
    function unesc(s) {
        return s.replace(/\\t/g, "\t").replace(/\\n/g, "\n").replace(/\\\\/g, "\\")
    }

    function parseRecords(out, kind) {
        const rows = []
        for (const line of out.split("\n")) {
            if (line === "" || line.startsWith("#")) continue
            const f = line.split("\t")
            if (f[0] !== kind) continue
            rows.push(f)
        }
        return rows
    }

    Process {
        id: enginesProc
        command: [root.bin, "--rec", "engines"]
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = root.parseRecords(this.text, "engine")
                // ⚠ present and runnable are TWO facts. Arch's rkhunter is
                // 0700 root:root — installed, and not ours to run. Drawing
                // that as "not installed" would send somebody to reinstall a
                // package they already have.
                root.engines = rows.map(f => ({
                    id: f[1], name: f[2],
                    present: f[3] === "1", runnable: f[4] === "1",
                    path: f[5] || ""
                }))
            }
        }
    }

    function toFindings(out) {
        return root.parseRecords(out, "finding").map(f => ({
            engine: f[1], verdict: f[2],
            path: root.unesc(f[3] || ""), detail: root.unesc(f[4] || "")
        }))
    }

    // What needs a person: an engine that did not finish is listed, never
    // counted — it says the scan was not whole, not that the machine is not.
    function outstanding(rows) {
        return rows.filter(f => f.verdict !== "clean" && f.verdict !== "incomplete").length
    }

    Process {
        id: scanProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.findings = root.toFindings(this.text)
                root.busy = false
                const n = root.outstanding(root.findings)
                root.status = n === 0
                    ? I18n.tr("Nothing found.")
                    : I18n.tr("%1 need a look.").arg(n)
            }
        }
        stderr: StdioCollector { id: scanErr }
        onExited: root.busy = false
    }

    // ── what the weekly scan found ─────────────────────────────────────────
    //
    // ⛔ THE WINDOW OPENED ON AN EMPTY LIST while Settings said "1 outstanding",
    // and nothing anywhere said what it was. It now opens on the scheduled
    // sweep's findings — the record Settings and the bar count — until a scan
    // started here replaces them.
    Process {
        id: weeklyProc
        command: [root.bin, "--rec", "status", "--weekly"]
        stdout: StdioCollector {
            onStreamFinished: {
                if (root.busy) return           // a scan started meanwhile
                const st = root.parseRecords(this.text, "status")[0]
                if (!st || st[1] !== "ran") {
                    root.status = I18n.tr("The weekly scan has not run yet.")
                    return
                }
                root.findings = root.toFindings(this.text)
                const when = Qt.formatDateTime(new Date(parseInt(st[2]) * 1000),
                                               "yyyy-MM-dd hh:mm")
                const n = root.outstanding(root.findings)
                root.status = n === 0
                    ? I18n.tr("The weekly scan on %1 found nothing.").arg(when)
                    : I18n.tr("The weekly scan on %1: %2 need a look.").arg(when).arg(n)
            }
        }
    }

    Process {
        id: quarProc
        command: [root.bin, "--rec", "quarantine", "list"]
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = root.parseRecords(this.text, "quarantine")
                root.quarantined = rows.map(f => ({
                    id: f[1], origin: root.unesc(f[2] || ""),
                    engine: f[3] || "", detail: root.unesc(f[4] || "")
                }))
            }
        }
    }

    function scanPath(p) {
        root.busy = true
        root.findings = []
        root.status = I18n.tr("Scanning...")
        scanProc.command = [root.bin, "--rec", "scan", p]
        scanProc.running = true
    }

    function scanSystem() {
        root.busy = true
        root.findings = []
        root.status = I18n.tr("Running the system checks...")
        scanProc.command = [root.bin, "--rec", "scan", "--system"]
        scanProc.running = true
    }

    Component.onCompleted: {
        enginesProc.running = true
        quarProc.running = true
        weeklyProc.running = true
    }

    FloatingWindow {
        title: "Malware Scan"
        implicitWidth: 760
        implicitHeight: 560
        color: "#12151a"

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Row {
                spacing: 8
                // ⛔ A BUTTON IS ITS OWN LABEL. These say what they do, not
                // what mode they leave you in.
                Button {
                    text: I18n.tr("Scan Downloads")
                    enabled: !root.busy
                    onClicked: root.scanPath(Quickshell.env("HOME") + "/Downloads")
                }
                Button {
                    text: I18n.tr("Scan Home Folder")
                    enabled: !root.busy
                    onClicked: root.scanPath(Quickshell.env("HOME"))
                }
                Button {
                    text: I18n.tr("Check For Rootkits")
                    enabled: !root.busy
                    onClicked: root.scanSystem()
                }
                Button {
                    text: root.page === "scan" ? I18n.tr("Quarantine") : I18n.tr("Findings")
                    onClicked: {
                        root.page = root.page === "scan" ? "quarantine" : "scan"
                        if (root.page === "quarantine") quarProc.running = true
                    }
                }
            }

            Text {
                text: root.status
                color: "#c8d2e0"
                font.family: root.uiFont
                font.pixelSize: root.ui(13)
            }

            // ── which engines are actually here ──────────────────────────────
            //
            // ⚠ AN ABSENT ENGINE IS SHOWN, NOT HIDDEN. chkrootkit comes from
            // [blackarch], which is an install-time choice, so a normal machine
            // has two of three. A window that silently drew two would look
            // complete while checking less than the user thinks.
            Row {
                spacing: 14
                Repeater {
                    model: root.engines
                    Text {
                        text: (modelData.runnable ? "● "
                             : modelData.present  ? "◐ " : "○ ") + modelData.name
                        color: modelData.runnable ? "#5ac87a"
                             : modelData.present  ? "#d9a441" : "#6b7688"
                        font.family: root.uiFont
                        font.pixelSize: root.ui(12)
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: parent.height - 130
                color: "#0d1014"
                radius: 6
                clip: true

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    model: root.page === "scan" ? root.findings : root.quarantined

                    // ⛔ THE SCROLLBAR IS NOT OPTIONAL, and it stays when the
                    // list is empty. A wheel does not tell a reader there is
                    // more, how much more, or where in it they are.
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: Column {
                        width: list.width - 16
                        spacing: 2

                        Text {
                            width: parent.width
                            text: root.page === "scan"
                                  ? modelData.path : modelData.origin
                            color: "#e6ecf5"
                            elide: Text.ElideMiddle
                            font.family: root.uiFont
                            font.pixelSize: root.ui(13)
                        }
                        // Wrapped, not elided: rkhunter's detail is the whole
                        // of what it found ("The file properties have changed:
                        // File: … Current hash: …"), and cutting it off at the
                        // edge of the window leaves the finding unreadable.
                        Text {
                            width: parent.width
                            text: root.page !== "scan"
                                  ? (modelData.id + " · " + modelData.engine)
                                  : modelData.verdict === "incomplete"
                                  ? (modelData.engine + " · " + I18n.tr("did not finish")
                                     + " · " + modelData.detail)
                                  : (modelData.engine + " · " + modelData.detail)
                            color: root.page === "scan" && modelData.verdict === "infected"
                                   ? "#ff6b6b"
                                 : root.page === "scan" && modelData.verdict === "incomplete"
                                   ? "#6b7688" : "#8b97a8"
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 4
                            elide: Text.ElideRight
                            font.family: root.uiFont
                            font.pixelSize: root.ui(11)
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: root.page === "scan"
                          ? I18n.tr("No findings.")
                          : I18n.tr("Nothing in quarantine.")
                    color: "#5b6675"
                    font.family: root.uiFont
                    font.pixelSize: root.ui(13)
                }
            }
        }
    }
}
