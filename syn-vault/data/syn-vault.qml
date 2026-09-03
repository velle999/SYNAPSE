// syn-vault.qml — the vault window.
//
// ⛔ EVERY ACTION GOES THROUGH THE BINARY. This file knows nothing about
// encryption, mount points or gocryptfs; it runs `syn-vault --rec list`, `open`,
// `close` and `create`, and draws what comes back. A second implementation of
// "is this vault open" would be a second answer to a question with one, and the
// wrong answer to that one tells somebody their files are locked when they are
// not.
//
// ⛔ AND THE PASSWORD CROSSES ON STDIN, NEVER IN argv. /proc/<pid>/cmdline is
// world-readable, so a password on a command line is visible to every process
// on the machine for as long as the command runs. syn-vault reads it from stdin
// when stdin is not a terminal, which is exactly this case.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

// ⛔ THE TRANSLATION SINGLETON, AND IT IS NOT qsTr(). quickshell 0.3.1 installs
// no QTranslator, so qsTr() compiles, looks up nothing and returns its own
// argument while looking exactly like a marked string in review — so
// qml/I18n.qml reads a JSON catalog compiled from the same po/ the CLI's .mo
// comes from, and a word this window and `syn-vault list` share is translated
// once and cannot disagree.
import "qml"
import Quickshell.Wayland

ShellRoot {
    id: root

    readonly property string bin: Quickshell.env("SYNVAULT_BIN") || "syn-vault"

    property var vaults: []
    property string status: ""
    property bool busy: false

    // The panel: "" closed, otherwise the vault being unlocked or created.
    property string askFor: ""
    property bool askIsNew: false
    property string askMsg: ""

    function disp(s) {
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    // ── the desktop's font and text size ────────────────────────────────────
    //
    // ⛔ NOT THIS WINDOW'S SETTING. The family and the scale are properties of
    // the desktop, in the file the bar, synfiles and syn-cal all watch. An app
    // that picked its own drew at a different size beside its siblings, which
    // reads as the theming having missed it rather than as a choice.
    property string uiFont: ""
    property int textScale: 100

    function ui(n) { return Math.round(n * root.textScale / 100) }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        // No font.state is the normal case on a box where nobody has picked
        // one; a warning per start for an expected miss is how a log becomes
        // something nobody reads.
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
    readonly property bool isLight: false
    readonly property color cBg:     "#16171c"
    readonly property color cPanel:  "#1d1f26"
    readonly property color cText:   "#e9eaef"
    readonly property color cDim:    "#9aa0ad"
    readonly property color cAccent: "#5b8dd9"
    readonly property color cWarn:   "#e0af68"
    readonly property color cBad:    "#f7768e"

    // ── reading ─────────────────────────────────────────────────────────────

    Process {
        id: listProc
        command: [root.bin, "--rec", "list"]
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = text.trim().split("\n").filter(l => l.length > 0)
                const out = []
                for (let i = 1; i < rows.length; i++) {
                    const f = rows[i].split("\t")
                    if (f.length < 4) continue
                    out.push({
                        name: root.disp(f[0]),
                        state: root.disp(f[1]),
                        mount: root.disp(f[2]),
                        // ⚠ THE STRAY FLAG IS NOT COSMETIC. It means files are
                        // sitting unencrypted in the mountpoint of a CLOSED
                        // vault, which is the one state where somebody believes
                        // their files are protected and they are not.
                        stray: f[3] === "1"
                    })
                }
                root.vaults = out
            }
        }
    }

    function reload() {
        listProc.running = false
        listProc.running = true
    }

    Component.onCompleted: {
        reload()
        const want = Quickshell.env("SYNVAULT_OPEN")
        if (want && want.length > 0) root.ask(want, false)
    }

    // ── doing ───────────────────────────────────────────────────────────────

    function ask(name, isNew) {
        root.askFor = name
        root.askIsNew = isNew
        root.askMsg = ""
        pwField.text = ""
        pwAgain.text = ""
    }

    // The password this window is holding for the child it just asked for, and
    // only until the pipe is live. See onStarted.
    property string pendingPw: ""

    Process {
        id: actProc
        // ⛔ stdinEnabled, NOT a `stdin:` parser. StdioCollector reads a stream;
        // stdin is written, not parsed, so there is nothing for one to collect
        // and Process has no such property. Assigning it is not a bad password
        // path or a dead button — it fails to parse the file, so the window
        // never opens at all, from the menu or from synfiles alike.
        stdinEnabled: true
        stderr: StdioCollector { id: actErr }

        // ⛔ WRITTEN HERE, NOT AFTER `running = true`. That property is a
        // request to start, not a started child: a write issued before the fork
        // completes is discarded silently, and the binary then sits on an empty
        // pipe until somebody closes the window. `started` is the pipe saying
        // it exists.
        onStarted: {
            actProc.write(root.pendingPw + "\n")
            root.pendingPw = ""
        }

        onExited: (code) => {
            root.pendingPw = ""
            root.busy = false
            if (code === 0) {
                root.askFor = ""
                pwField.text = ""
                pwAgain.text = ""
                root.status = ""
            } else {
                // The binary's own words: "password incorrect" and "something is
                // still using it" are different problems with different fixes,
                // and a single "that failed" throws the difference away.
                // ⚠ WHITESPACE COLLAPSED, WORDS UNTOUCHED. The binary wraps
                // its longer messages for a terminal, with two spaces of
                // continuation indent; this panel word-wraps on its own, so
                // those hard breaks land mid-paragraph as ragged indented
                // lines. Reflowing is the window's business — the wording is
                // still entirely the binary's.
                const said = actErr.text.trim().replace(/\s*\n\s*/g, " ")
                root.askMsg = said || I18n.tr("that did not work")
            }
            root.reload()
        }
    }

    function submit() {
        if (root.busy) return
        if (pwField.text.length === 0) { root.askMsg = I18n.tr("It needs a password."); return }
        if (root.askIsNew && pwField.text !== pwAgain.text) {
            // ⛔ CHECKED HERE BECAUSE THE BINARY CANNOT. It confirms a password
            // only when it has a terminal to ask twice; piped input is this
            // window, so this window owns the confirmation. A vault created
            // with a mistyped password cannot be opened and cannot be
            // recovered — nothing anywhere keeps a second copy.
            root.askMsg = I18n.tr("Those two do not match.")
            return
        }

        root.busy = true
        root.askMsg = ""
        // ⚠ ONE LINE, EVEN WHEN CREATING. The binary asks twice only when it has
        // a terminal to ask twice; on a pipe it reads a single line and the
        // second would be left unread in it. The confirmation above is this
        // window's job precisely because the binary skips it here.
        root.pendingPw = pwField.text
        // ⚠ --rec IS WHAT MAKES THE ERROR FIT. Without it the binary lets
        // gocryptfs speak for itself on stderr, which is right at a terminal
        // and is three lines of "failed to unlock master key: cipher: message
        // authentication failed" in a panel with room for one. In --rec the
        // backend is silenced and the binary gives its own sentence.
        actProc.command = [root.bin, "--rec", root.askIsNew ? "create" : "open", root.askFor]
        actProc.running = false
        actProc.running = true
    }

    Process {
        id: closeProc
        stderr: StdioCollector { id: closeErr }
        onExited: (code) => {
            root.busy = false
            // Reflowed for the same reason as askMsg above: the binary wraps
            // "something is still using it" for a terminal, this draws it in a
            // window that wraps by itself.
            const said = closeErr.text.trim().replace(/\s*\n\s*/g, " ")
            root.status = code === 0 ? "" : (said || I18n.tr("could not close it"))
            root.reload()
        }
    }

    function closeVault(name) {
        if (root.busy) return
        root.busy = true
        closeProc.command = [root.bin, "--rec", "close", name]
        closeProc.running = false
        closeProc.running = true
    }

    Process {
        id: openerProc
    }

    function browse(path) {
        openerProc.command = ["synfiles", "gui", path]
        openerProc.running = false
        openerProc.running = true
    }

    // ── the window ──────────────────────────────────────────────────────────

    FloatingWindow {
        title: I18n.tr("File Vault")
        implicitWidth: root.ui(560)
        implicitHeight: root.ui(460)
        color: root.cBg

        Rectangle {
            anchors.fill: parent
            color: root.cBg

            Column {
                anchors { fill: parent; margins: 18 }
                spacing: 12

                Item {
                    width: parent.width
                    height: Math.max(head.height, newBtn.height)

                    Text {
                        id: head
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: I18n.tr("Vaults")
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(18); bold: true }
                    }

                    Rectangle {
                        id: newBtn
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        width: newTxt.implicitWidth + 20
                        height: 28
                        radius: 6
                        color: newMouse.containsMouse ? root.cAccent : "transparent"
                        border { width: 1; color: root.cAccent }
                        Text {
                            id: newTxt
                            anchors.centerIn: parent
                            text: I18n.tr("New vault")
                            color: newMouse.containsMouse ? root.cPanel : root.cAccent
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }
                        MouseArea {
                            id: newMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { nameField.text = ""; root.ask("", true) }
                        }
                    }
                }

                ListView {
                    id: list
                    width: parent.width
                    height: parent.height - 120
                    clip: true
                    model: root.vaults
                    spacing: 6

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
                        width: list.width - 12
                        height: 62
                        radius: 8
                        color: root.cPanel
                        border { width: 1
                                 color: modelData.stray ? root.cWarn
                                      : (modelData.state === "open" ? root.cAccent : root.cDim) }

                        Column {
                            anchors { left: parent.left; leftMargin: 12
                                      verticalCenter: parent.verticalCenter }
                            spacing: 2
                            Text {
                                text: modelData.name
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                            }
                            Text {
                                // ⛔ WHOLE CELLS, NOT PIECES. "open · " glued
                                // to a path can never be a msgid, and `open`
                                // and `locked` are the RECORD's words — the
                                // comparison two lines up is what decides
                                // which row this is, and it must stay English.
                                // These are the sentences a person reads.
                                text: modelData.state === "open"
                                      ? I18n.tr("open · %1").arg(modelData.mount)
                                      : (modelData.stray
                                         ? I18n.tr("locked · files in %1 are NOT encrypted").arg(modelData.mount)
                                         : I18n.tr("locked"))
                                color: modelData.stray ? root.cWarn : root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                        }

                        Row {
                            anchors { right: parent.right; rightMargin: 12
                                      verticalCenter: parent.verticalCenter }
                            spacing: 8

                            Rectangle {
                                visible: modelData.state === "open"
                                width: 64; height: 26; radius: 5
                                color: browseMouse.containsMouse ? root.cAccent : "transparent"
                                border { width: 1; color: root.cDim }
                                Text {
                                    anchors.centerIn: parent
                                    text: I18n.tr("Open")
                                    color: browseMouse.containsMouse ? root.cPanel : root.cText
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                }
                                MouseArea {
                                    id: browseMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.browse(modelData.mount)
                                }
                            }

                            Rectangle {
                                width: 70; height: 26; radius: 5
                                color: actMouse.containsMouse
                                       ? (modelData.state === "open" ? root.cWarn : root.cAccent)
                                       : "transparent"
                                border { width: 1
                                         color: modelData.state === "open" ? root.cWarn
                                                                           : root.cAccent }
                                Text {
                                    anchors.centerIn: parent
                                    // ⛔ THE BUTTON IS ITS OWN LABEL. Not a
                                    // padlock glyph that needs a tooltip to say
                                    // which way it is about to go.
                                    text: modelData.state === "open" ? I18n.tr("Lock") : I18n.tr("Unlock")
                                    color: actMouse.containsMouse ? root.cPanel
                                         : (modelData.state === "open" ? root.cWarn : root.cAccent)
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                }
                                MouseArea {
                                    id: actMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: !root.busy
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.state === "open") root.closeVault(modelData.name)
                                        else root.ask(modelData.name, false)
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: root.vaults.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: I18n.tr("No vaults yet. A vault is a folder whose contents are "
                                  + "encrypted with a password — it lives in ~/Vaults while "
                                  + "it is open, and is unreadable when it is not.")
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                }

                Text {
                    visible: root.status !== ""
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.status
                    color: root.cBad
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
            }

            // ── the password panel ──────────────────────────────────────────
            Rectangle {
                anchors.fill: parent
                visible: root.askFor !== "" || root.askIsNew
                color: Qt.rgba(0, 0, 0, 0.55)

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: if (!root.busy) { root.askFor = ""; root.askIsNew = false }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(400, parent.width - 48)
                    height: card.implicitHeight + 36
                    radius: 10
                    color: root.cPanel
                    border { width: 1; color: root.cDim }

                    MouseArea { anchors.fill: parent; hoverEnabled: true }

                    Column {
                        id: card
                        anchors { left: parent.left; right: parent.right; top: parent.top
                                  margins: 18 }
                        spacing: 10

                        Text {
                            text: root.askIsNew ? I18n.tr("New vault")
                                                : I18n.tr("Unlock %1").arg(root.askFor)
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(16); bold: true }
                        }

                        Rectangle {
                            visible: root.askIsNew
                            width: parent.width; height: 32; radius: 5
                            color: root.cBg
                            border { width: 1; color: nameField.activeFocus ? root.cAccent
                                                                            : root.cDim }
                            TextInput {
                                id: nameField
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                verticalAlignment: TextInput.AlignVCenter
                                clip: true
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(13) }
                                onTextEdited: root.askFor = text
                            }
                            Text {
                                anchors { left: parent.left; leftMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                visible: nameField.text === ""
                                text: I18n.tr("What to call it")
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(13) }
                            }
                        }

                        Rectangle {
                            width: parent.width; height: 32; radius: 5
                            color: root.cBg
                            border { width: 1; color: pwField.activeFocus ? root.cAccent
                                                                          : root.cDim }
                            TextInput {
                                id: pwField
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                verticalAlignment: TextInput.AlignVCenter
                                clip: true
                                color: root.cText
                                // ⛔ ECHO OFF IS NOT ENOUGH ON ITS OWN. A
                                // password field must also stay out of the
                                // predictive-text and clipboard machinery an
                                // ordinary field opts into.
                                echoMode: TextInput.Password
                                passwordCharacter: "•"
                                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData
                                                  | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                                font { family: root.uiFont; pixelSize: root.ui(13) }
                                onAccepted: root.submit()
                            }
                            Text {
                                anchors { left: parent.left; leftMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                visible: pwField.text === "" && !pwField.activeFocus
                                text: I18n.tr("Password")
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(13) }
                            }
                        }

                        Rectangle {
                            visible: root.askIsNew
                            width: parent.width; height: 32; radius: 5
                            color: root.cBg
                            border { width: 1; color: pwAgain.activeFocus ? root.cAccent
                                                                          : root.cDim }
                            TextInput {
                                id: pwAgain
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                verticalAlignment: TextInput.AlignVCenter
                                clip: true
                                color: root.cText
                                echoMode: TextInput.Password
                                passwordCharacter: "•"
                                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData
                                                  | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                                font { family: root.uiFont; pixelSize: root.ui(13) }
                                onAccepted: root.submit()
                            }
                            Text {
                                anchors { left: parent.left; leftMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                visible: pwAgain.text === "" && !pwAgain.activeFocus
                                text: I18n.tr("And again")
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(13) }
                            }
                        }

                        Text {
                            visible: root.askIsNew
                            width: parent.width
                            wrapMode: Text.WordWrap
                            // ⚠ SAID BEFORE, NOT AFTER. There is no recovery and
                            // no second copy of the key; somebody choosing a
                            // password is owed that while they are choosing it.
                            text: I18n.tr("Nothing keeps a second copy of this password. "
                                          + "A vault whose password is lost is lost with it.")
                            color: root.cWarn
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }

                        Text {
                            visible: root.askMsg !== ""
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: root.askMsg
                            color: root.cBad
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }

                        Row {
                            width: parent.width
                            spacing: 10
                            layoutDirection: Qt.RightToLeft

                            Rectangle {
                                width: 90; height: 30; radius: 6
                                color: goMouse.containsMouse ? root.cAccent : "transparent"
                                border { width: 1; color: root.cAccent }
                                Text {
                                    anchors.centerIn: parent
                                    text: root.busy ? I18n.tr("Working…")
                                                    : (root.askIsNew ? I18n.tr("Create")
                                                                     : I18n.tr("Unlock"))
                                    color: goMouse.containsMouse ? root.cPanel : root.cAccent
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                }
                                MouseArea {
                                    id: goMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: !root.busy
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.submit()
                                }
                            }

                            Rectangle {
                                width: 80; height: 30; radius: 6
                                color: cancelMouse.containsMouse ? root.cBg : "transparent"
                                border { width: 1; color: root.cDim }
                                Text {
                                    anchors.centerIn: parent
                                    text: I18n.tr("Cancel")
                                    color: root.cText
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                }
                                MouseArea {
                                    id: cancelMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: !root.busy
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.askFor = ""; root.askIsNew = false }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
