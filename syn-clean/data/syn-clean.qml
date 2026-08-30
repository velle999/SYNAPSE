// syn-clean.qml — the disk cleanup window.
//
// ⛔ EVERY NUMBER AND EVERY DELETION IS THE BINARY'S. This file runs
// `syn-clean --rec scan`, `clean` and `shred` and draws what comes back. It
// does not stat a directory, sum a size or unlink anything of its own — a
// second implementation of "how big is the cache" is a second answer to a
// question with one, and the wrong answer here is a number somebody trusts
// before pressing a button that cannot be undone.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

ShellRoot {
    id: root

    readonly property string bin: Quickshell.env("SYNCLEAN_BIN") || "syn-clean"

    property var rows: []
    property var picked: ({})
    property bool busy: false
    property string status: ""
    property string page: "clean"        // "clean" | "shred"

    function disp(s) {
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    function human(b) {
        const u = ["B", "KB", "MB", "GB", "TB"]
        let v = b, i = 0
        while (v >= 1024 && i < 4) { v /= 1024; i++ }
        return i === 0 ? b + " B" : v.toFixed(1) + " " + u[i]
    }

    // ── the desktop's font and text size ────────────────────────────────────
    //
    // ⛔ NOT THIS WINDOW'S SETTING. The family and the scale are properties of
    // the desktop, in the file the bar, synfiles, syn-cal and syn-vault all
    // watch. An app that picked its own drew at a different size beside its
    // siblings, which reads as the theming having missed it.
    property string uiFont: ""
    property int textScale: 100
    function ui(n) { return Math.round(n * root.textScale / 100) }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = this.text()
            const m = t.match(/^\s*family\s*=\s*(.+?)\s*$/m)
            root.uiFont = m ? m[1] : ""
            const sc = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            root.textScale = sc ? parseInt(sc[1]) : 100
        }
        onLoadFailed: { root.uiFont = ""; root.textScale = 100 }
    }

    // ── palette ─────────────────────────────────────────────────────────────
    readonly property color cBg:     "#16171c"
    readonly property color cPanel:  "#1d1f26"
    readonly property color cText:   "#e9eaef"
    readonly property color cDim:    "#9aa0ad"
    readonly property color cAccent: "#5b8dd9"
    readonly property color cWarn:   "#e0af68"
    readonly property color cBad:    "#f7768e"

    // ── scanning ────────────────────────────────────────────────────────────

    Process {
        id: scanProc
        command: [root.bin, "--rec", "scan"]
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = text.trim().split("\n").filter(l => l.length > 0)
                const out = []
                for (let i = 1; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 7) continue
                    out.push({
                        id: root.disp(f[0]),
                        label: root.disp(f[1]),
                        what: root.disp(f[2]),
                        bytes: parseInt(f[3]) || 0,
                        files: parseInt(f[4]) || 0,
                        needsRoot: f[5] === "1",
                        // ⚠ THE ONE THAT CHANGES SOMETHING THE USER WILL NOTICE.
                        // Every other row here frees space invisibly; this one
                        // signs them out of every site they use, so it is never
                        // ticked by default and it says so in its own colour.
                        losesLogins: f[6] === "1"
                    })
                }
                root.rows = out
                root.busy = false
            }
        }
    }

    function rescan() {
        root.busy = true
        scanProc.running = false
        scanProc.running = true
    }

    Component.onCompleted: rescan()

    property int totalPicked: {
        let t = 0
        for (const r of root.rows) if (root.picked[r.id]) t += r.bytes
        return t
    }

    function togglePick(id) {
        // ⚠ REBUILT, NOT MUTATED. Assigning into a `var` object does not emit
        // a change signal in QML, so every binding that reads it keeps the old
        // value and the window shows the state before the click.
        const next = Object.assign({}, root.picked)
        next[id] = !next[id]
        root.picked = next
    }

    // ── cleaning ────────────────────────────────────────────────────────────

    Process {
        id: cleanProc
        stderr: StdioCollector { id: cleanErr }
        onExited: (code) => {
            root.busy = false
            const said = cleanErr.text.trim().replace(/\s*\n\s*/g, " ")
            root.status = code === 0 ? "" : (said || "some of that could not be removed")
            root.picked = ({})
            root.rescan()
        }
    }

    function cleanPicked() {
        if (root.busy) return
        const ids = root.rows.filter(r => root.picked[r.id] && !r.needsRoot)
                             .map(r => r.id)
        if (ids.length === 0) { root.status = "Nothing is ticked."; return }
        root.busy = true
        root.status = ""
        // ⛔ --yes BECAUSE THE WINDOW ALREADY ASKED. The binary refuses to take
        // silence on a pipe as consent, which is right: without --yes it would
        // block on a question with nobody to answer it. The confirmation this
        // replaces is the sheet below, which names what is about to go.
        cleanProc.command = [root.bin, "--rec", "--yes", "clean"].concat(ids)
        cleanProc.running = false
        cleanProc.running = true
    }

    // ── shredding ───────────────────────────────────────────────────────────

    property string shredPath: ""
    property string shredGround: ""
    property bool   shredCow: false
    property bool   shredSnaps: false

    // What the filesystem under a path can honour, asked before anything is
    // destroyed so the warning is on screen while somebody decides.
    Process {
        id: groundProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = text.trim().split("\n")
                if (lines.length < 2) return
                const f = lines[1].split("\t")
                root.shredGround = f[0] || ""
                root.shredCow = f[1] === "1"
                root.shredSnaps = f[2] === "1"
            }
        }
    }

    function askGround(path) {
        if (!path) return
        root.shredGround = ""
        groundProc.command = [root.bin, "--rec", "--dry-run", "--yes", "shred", path]
        groundProc.running = false
        groundProc.running = true
    }

    Process {
        id: shredProc
        stderr: StdioCollector { id: shredErr }
        onExited: (code) => {
            root.busy = false
            const said = shredErr.text.trim().replace(/\s*\n\s*/g, " ")
            root.status = code === 0
                ? "Gone. " + (root.shredCow ? "See the warning above about " + root.shredGround + "." : "")
                : (said || "that could not be destroyed")
            root.shredPath = ""
            root.rescan()
        }
    }

    function doShred() {
        if (root.busy || root.shredPath.length === 0) return
        root.busy = true
        root.status = ""
        shredProc.command = [root.bin, "--rec", "--yes", "shred", root.shredPath]
        shredProc.running = false
        shredProc.running = true
    }

    // ── the window ──────────────────────────────────────────────────────────

    FloatingWindow {
        title: "Disk Cleanup"
        implicitWidth: root.ui(620)
        implicitHeight: root.ui(560)
        color: root.cBg

        Rectangle {
            anchors.fill: parent
            color: root.cBg

            Column {
                anchors { fill: parent; margins: 16 }
                spacing: 12

                // ── the two pages ───────────────────────────────────────
                Row {
                    spacing: 8
                    Repeater {
                        model: [ { id: "clean", label: "Clean up" },
                                 { id: "shred", label: "Destroy a file" } ]
                        delegate: Rectangle {
                            required property var modelData
                            width: tabTxt.implicitWidth + 22
                            height: 30
                            radius: 6
                            color: root.page === modelData.id ? root.cAccent
                                 : (tabMa.containsMouse ? root.cPanel : "transparent")
                            border { width: 1; color: root.page === modelData.id
                                                     ? root.cAccent : root.cDim }
                            Text {
                                id: tabTxt
                                anchors.centerIn: parent
                                text: parent.modelData.label
                                color: root.page === parent.modelData.id
                                       ? root.cPanel : root.cText
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                            MouseArea {
                                id: tabMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.page = parent.modelData.id
                                             root.status = "" }
                            }
                        }
                    }
                }

                // ── CLEAN ───────────────────────────────────────────────
                Item {
                    visible: root.page === "clean"
                    width: parent.width
                    height: parent.height - 100

                    Column {
                        anchors.fill: parent
                        spacing: 10

                        Text {
                            text: root.busy ? "Looking…"
                                            : "Tick what to remove. Nothing goes until you press the button."
                            color: root.cDim
                            width: parent.width
                            wrapMode: Text.WordWrap
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }

                        ListView {
                            id: catList
                            width: parent.width
                            height: parent.height - 60
                            clip: true
                            model: root.rows
                            spacing: 5

                            // A view that scrolls says so.
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                                contentItem: Rectangle {
                                    implicitWidth: 6
                                    radius: 3
                                    color: root.cDim
                                    opacity: 0.7
                                }
                            }

                            delegate: Rectangle {
                                id: catRow
                                required property var modelData
                                width: catList.width - 10
                                height: 52
                                radius: 7
                                color: root.cPanel
                                border {
                                    width: 1
                                    color: root.picked[catRow.modelData.id]
                                           ? root.cAccent : root.wash
                                }
                                readonly property color wash: Qt.rgba(1, 1, 1, 0.12)

                                Rectangle {
                                    id: box
                                    anchors { left: parent.left; leftMargin: 12
                                              verticalCenter: parent.verticalCenter }
                                    width: 16; height: 16; radius: 3
                                    color: root.picked[catRow.modelData.id]
                                           ? root.cAccent : "transparent"
                                    border { width: 1; color: root.cDim }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: root.picked[catRow.modelData.id]
                                        text: "✓"
                                        color: root.cPanel
                                        font { family: root.uiFont; pixelSize: root.ui(11) }
                                    }
                                }

                                Column {
                                    anchors { left: box.right; leftMargin: 12
                                              verticalCenter: parent.verticalCenter }
                                    spacing: 1
                                    Text {
                                        text: catRow.modelData.label
                                        color: catRow.modelData.needsRoot ? root.cDim : root.cText
                                        font { family: root.uiFont
                                               pixelSize: root.ui(13); bold: true }
                                    }
                                    Text {
                                        text: catRow.modelData.needsRoot
                                              ? catRow.modelData.what + " — needs sudo"
                                              : catRow.modelData.what
                                        color: catRow.modelData.losesLogins ? root.cWarn : root.cDim
                                        font { family: root.uiFont; pixelSize: root.ui(10) }
                                    }
                                }

                                Text {
                                    anchors { right: parent.right; rightMargin: 14
                                              verticalCenter: parent.verticalCenter }
                                    text: root.human(catRow.modelData.bytes)
                                    color: root.cText
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !catRow.modelData.needsRoot
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.togglePick(catRow.modelData.id)
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 10

                            Rectangle {
                                width: 150; height: 32; radius: 6
                                color: goMa.containsMouse ? root.cAccent : "transparent"
                                border { width: 1; color: root.cAccent }
                                // ⛔ A BUTTON IS ITS OWN LABEL. Not "OK" beside a
                                // number somebody has to look up — the button says
                                // what pressing it does and how much it takes.
                                Text {
                                    anchors.centerIn: parent
                                    text: root.totalPicked > 0
                                          ? "Remove " + root.human(root.totalPicked)
                                          : "Remove"
                                    color: goMa.containsMouse ? root.cPanel : root.cAccent
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                }
                                MouseArea {
                                    id: goMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.cleanPicked()
                                }
                            }

                            Rectangle {
                                width: 96; height: 32; radius: 6
                                color: rsMa.containsMouse ? root.cPanel : "transparent"
                                border { width: 1; color: root.cDim }
                                Text {
                                    anchors.centerIn: parent
                                    text: "Look again"
                                    color: root.cText
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                }
                                MouseArea {
                                    id: rsMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.rescan()
                                }
                            }
                        }
                    }
                }

                // ── SHRED ───────────────────────────────────────────────
                Item {
                    visible: root.page === "shred"
                    width: parent.width
                    height: parent.height - 100

                    Column {
                        anchors.fill: parent
                        spacing: 12

                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "Overwrite a file or folder and delete it. There is no undo "
                                  + "and it does not go to the Trash."
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }

                        Rectangle {
                            width: parent.width
                            height: 34
                            radius: 6
                            color: root.cPanel
                            border { width: 1; color: root.cDim }
                            TextInput {
                                id: pathField
                                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                verticalAlignment: TextInput.AlignVCenter
                                clip: true
                                color: root.cText
                                text: root.shredPath
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                                onTextChanged: {
                                    root.shredPath = text
                                    root.askGround(text)
                                }
                            }
                            Text {
                                anchors { left: parent.left; leftMargin: 10
                                          verticalCenter: parent.verticalCenter }
                                visible: pathField.text === ""
                                text: "/path/to/the/file"
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                        }

                        // ⛔ THE WARNING IS ON SCREEN BEFORE THE BUTTON IS PRESSED,
                        // not after. On btrfs — what SynapseOS installs on —
                        // overwriting does not replace the old blocks, and a
                        // snapshot keeps a whole copy. Somebody deciding whether
                        // this is good enough needs that while deciding.
                        Rectangle {
                            visible: root.shredCow && root.shredPath.length > 0
                            width: parent.width
                            height: cowTxt.implicitHeight + 18
                            radius: 6
                            color: Qt.rgba(0.88, 0.69, 0.41, 0.13)
                            border { width: 1; color: root.cWarn }
                            Text {
                                id: cowTxt
                                anchors { fill: parent; margins: 9 }
                                wrapMode: Text.WordWrap
                                color: root.cWarn
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                                text: "This is " + root.shredGround + ", which is copy-on-write: "
                                      + "overwriting writes new blocks and leaves the old ones. "
                                      + (root.shredSnaps
                                         ? "Snapshots in /.snapshots also hold copies this cannot reach. "
                                         : "")
                                      + "The file will be gone; its contents may still be recoverable "
                                      + "from the raw disk. Full-disk encryption is what actually prevents that."
                            }
                        }

                        Rectangle {
                            width: 210; height: 34; radius: 6
                            enabled: root.shredPath.length > 0
                            opacity: root.shredPath.length > 0 ? 1 : 0.4
                            color: shMa.containsMouse ? root.cBad : "transparent"
                            border { width: 1; color: root.cBad }
                            Text {
                                anchors.centerIn: parent
                                text: "Overwrite and delete it"
                                color: shMa.containsMouse ? root.cPanel : root.cBad
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                            MouseArea {
                                id: shMa
                                anchors.fill: parent
                                enabled: root.shredPath.length > 0
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.doShred()
                            }
                        }
                    }
                }

                // ── what just happened ──────────────────────────────────
                Text {
                    visible: root.status !== ""
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.status
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
            }
        }
    }
}
