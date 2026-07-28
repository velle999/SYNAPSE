//@ pragma UseQApplication

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

/*
 * SynapseOS Updates — a window around `syn-update`.
 *
 * quickshell rather than GTK or a Qt C++ app because it is what the rest of
 * this desktop is already built on (the bar, the start menu, the OSD) and
 * because it costs NOTHING new on the ISO: quickshell and qt6-declarative are
 * both already there, the latter dragged in by Dolphin's Qt6/KF6 stack. A GTK4
 * app would have been a second toolkit for one window, and plain `qml` cannot
 * launch a process at all — Quickshell.Io's Process is the whole reason this
 * can be QML instead of C++.
 *
 * WHY "Install updates" OPENS A TERMINAL
 *
 * `syn-update apply` drives build-all.sh, which runs `sudo pacman -U` in the
 * middle of the build. sudo with no controlling terminal cannot prompt, so
 * running apply inside this window would fail at the install step every time,
 * several minutes into a build. The alternatives are worse: a NOPASSWD sudoers
 * rule for `pacman -U` is a rule that lets any package be installed as root,
 * which is a privilege-escalation hole, not a convenience. So the window owns
 * the read-only half — what is available, what changed, why something is not
 * updatable — and hands the privileged half to a terminal where sudo can do its
 * job and the build is visible while it runs.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
ShellRoot {
    id: root

    // ── palette (synui's dark chrome, cyan accent) ──────────
    readonly property color cBg:      "#11151c"
    readonly property color cPanel:   "#182029"
    readonly property color cLine:    "#243040"
    readonly property color cText:    "#dbe4ee"
    readonly property color cDim:     "#8296ad"
    readonly property color cAccent:  "#38bdf8"
    readonly property color cOk:      "#5ee68a"
    readonly property color cWarn:    "#f2b45c"

    property bool   busy:     false
    property string statusLine: "Checking for updates…"
    property string revision: ""
    property string logText:  ""
    property bool   upToDate: false

    property var updates: []    // [{name, from, to}]
    property var commits: []    // ["hash subject"]
    property var blocked: []    // ["name  reason"]

    function appendLog(t) {
        if (!t) return
        root.logText += t.endsWith("\n") ? t : t + "\n"
    }

    /*
     * Parse `syn-update check`.
     *
     * This is coupled to syn-update's own output, which is acceptable only
     * because the same package ships both — they version together and cannot
     * drift apart in the field. It is deliberately TOLERANT: anything it fails
     * to recognise still lands in the log pane verbatim, so a format change
     * degrades to "the window shows the raw output" rather than to a window
     * that confidently says the wrong thing.
     *
     * No ANSI stripping is needed: syn-update only colours when stdout is a
     * tty, and under Process it is a pipe.
     */
    function parseCheck(text) {
        const ups = [], cms = [], blk = []
        let section = ""
        root.upToDate = false

        for (const raw of String(text).split("\n")) {
            const line = raw.replace(/\s+$/, "")
            if (line === "") continue

            if (/everything build-all\.sh can update is already current/.test(line)) {
                root.upToDate = true; continue
            }
            let m = line.match(/source is already at (\S+)/) || line.match(/^==> ([0-9a-f]{7,}) -> ([0-9a-f]{7,})/)
            if (m) { root.revision = m[m.length - 1]; continue }

            if (/commits since your installed source revision/.test(line)) { section = "commits"; continue }
            if (/component\(s\) to rebuild/.test(line))                    { section = "updates"; continue }
            if (/not updatable from source/.test(line))                    { section = "blocked"; continue }

            if (section === "updates") {
                // "  synui            0.1.0-203      -> 0.1.0-204"
                m = line.match(/^\s{2}(\S+)\s+(\S+)\s+->\s+(\S+)\s*$/)
                if (m && m[1] !== "COMPONENT") ups.push({ name: m[1], from: m[2], to: m[3] })
                continue
            }
            if (section === "commits") {
                m = line.match(/^\s{2}([0-9a-f]{7,})\s+(.*)$/)
                if (m) cms.push({ hash: m[1], subject: m[2] })
                continue
            }
            if (section === "blocked") {
                m = line.match(/^\s{4}(\S+)\s{2,}(.*)$/)
                if (m) blk.push({ name: m[1], reason: m[2] })
                continue
            }
        }

        root.updates = ups
        root.commits = cms
        root.blocked = blk

        if (ups.length > 0)
            root.statusLine = ups.length + (ups.length === 1 ? " update available" : " updates available")
        else if (root.upToDate)
            root.statusLine = "SynapseOS is up to date"
        else
            root.statusLine = "Could not determine update status — see the log"
    }

    function check() {
        if (root.busy) return
        root.busy = true
        root.statusLine = "Checking for updates…"
        root.logText = ""
        checkProc.running = true
    }

    Process {
        id: checkProc
        command: ["syn-update", "check"]
        running: false
        stdout: StdioCollector {
            onStreamFinished: { root.appendLog(this.text); root.parseCheck(this.text) }
        }
        // The "not updatable" block and any git error go to stderr; it is part
        // of the answer, not noise, so it is parsed and logged like stdout.
        stderr: StdioCollector {
            onStreamFinished: { root.appendLog(this.text); root.parseCheck(root.logText) }
        }
        onExited: root.busy = false
    }

    // --hold so the window survives the run and the build output stays readable,
    // matching how the start menu already launches `shelly upgrade`.
    Process {
        id: applyProc
        command: ["foot", "--hold", "syn-update", "apply"]
        running: false
    }

    FloatingWindow {
        title: "SynapseOS Updates"
        minimumSize: Qt.size(760, 560)
        color: root.cBg

        Component.onCompleted: root.check()

        Rectangle {
            anchors.fill: parent
            color: root.cBg

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                // ── header ─────────────────────────────────
                Item {
                    width: parent.width
                    height: 52

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: root.statusLine
                            color: root.upToDate ? root.cOk
                                 : (root.updates.length > 0 ? root.cAccent : root.cText)
                            font.pixelSize: 21
                            font.bold: true
                        }
                        Text {
                            text: root.revision ? "source revision " + root.revision : ""
                            color: root.cDim
                            font.pixelSize: 12
                            font.family: "monospace"
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Button {
                            text: root.busy ? "Checking…" : "Check again"
                            enabled: !root.busy
                            onClicked: root.check()
                        }
                        Button {
                            text: "Install updates…"
                            enabled: !root.busy && root.updates.length > 0
                            onClicked: applyProc.running = true
                        }
                    }
                }

                // ── what will change ───────────────────────
                Rectangle {
                    width: parent.width
                    height: Math.min(150, 34 + root.updates.length * 26)
                    visible: root.updates.length > 0
                    color: root.cPanel
                    radius: 8
                    border.color: root.cLine

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Text {
                            text: "Components to rebuild"
                            color: root.cDim; font.pixelSize: 11; font.bold: true
                        }
                        Repeater {
                            model: root.updates
                            Row {
                                spacing: 10
                                Text {
                                    text: modelData.name; color: root.cText
                                    font.pixelSize: 13; font.family: "monospace"
                                    width: 170
                                }
                                Text {
                                    text: modelData.from + "  →  " + modelData.to
                                    color: root.cAccent
                                    font.pixelSize: 13; font.family: "monospace"
                                }
                            }
                        }
                    }
                }

                // ── the commits being applied ──────────────
                Rectangle {
                    width: parent.width
                    height: 150
                    visible: root.commits.length > 0
                    color: root.cPanel
                    radius: 8
                    border.color: root.cLine

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Text {
                            text: "Changes since your installed version"
                            color: root.cDim; font.pixelSize: 11; font.bold: true
                        }
                        ScrollView {
                            width: parent.width
                            height: parent.height - 22
                            clip: true
                            Column {
                                spacing: 2
                                Repeater {
                                    model: root.commits
                                    Row {
                                        spacing: 10
                                        Text {
                                            text: modelData.hash; color: root.cDim
                                            font.pixelSize: 12; font.family: "monospace"
                                        }
                                        Text {
                                            text: modelData.subject; color: root.cText
                                            font.pixelSize: 12
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── what this tool will NOT update ─────────
                Rectangle {
                    width: parent.width
                    height: Math.min(120, 30 + root.blocked.length * 20)
                    visible: root.blocked.length > 0
                    color: "transparent"
                    radius: 8
                    border.color: root.cLine

                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 3
                        Text {
                            text: "Not updated this way — these move with an ISO upgrade"
                            color: root.cWarn; font.pixelSize: 11; font.bold: true
                        }
                        Repeater {
                            model: root.blocked
                            Text {
                                text: "• " + modelData.name
                                color: root.cDim; font.pixelSize: 11
                            }
                        }
                    }
                }

                // ── raw log, the honest fallback ───────────
                Rectangle {
                    width: parent.width
                    height: 120
                    color: "#0c1016"
                    radius: 8
                    border.color: root.cLine

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        TextArea {
                            text: root.logText
                            readOnly: true
                            color: root.cDim
                            background: null
                            font.pixelSize: 11
                            font.family: "monospace"
                            wrapMode: TextArea.NoWrap
                        }
                    }
                }
            }
        }
    }
}
