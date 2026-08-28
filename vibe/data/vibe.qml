// vibe — the SynapseOS assistant, as a window.
//
// A renderer, and nothing more. This file owns NO conversation.
//
// ── Why that is the whole design ───────────────────────────────────────────
//
// The obvious way to build a chat window on quickshell is to hold the messages
// in QML and call the model from here. That would be a second assistant: a
// second system prompt, a second tool loop, a second idea of when to ask before
// writing a file — and none of the tools, because the tools are Python. Instead
// a long-lived `vibe serve` holds the conversation; this window sends lines and
// draws records. The synsh keyword path, the agentic tool loop, the
// confirmation gate and every backend work here because none of them are here.
//
// ── The one rule for reading records ───────────────────────────────────────
//
// EVERY field arrives percent-encoded, including the ones that look like plain
// words — an answer can contain a tab, and a file the model quotes can contain
// any byte at all. So: decode every field, once, at the parse.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

// ⚠ Bound, so the delegate below may name ids from this file. Without it
// every reference out of a nested component is resolved at RUN time by
// scope lookup — which works until a property of the same name appears
// nearer, and then silently reads the wrong one.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

FloatingWindow {
    id: root

    title: "SYNAPSE Assistant"
    implicitWidth: 820
    implicitHeight: 640
    minimumSize: Qt.size(420, 320)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("VIBE_BIN") || "vibe"

    // ── Theme ───────────────────────────────────────────────────────────────
    //
    // Read from the desktop, not hardcoded, and the same source and shape as
    // syn-edit, synfiles and the bar — so a theme switch moves all of them
    // together instead of leaving this one window in last month's colours.
    property var p: ({})
    readonly property bool isLight: p.scheme === "light"

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: { try { root.p = JSON.parse(this.text()) } catch (e) { root.p = ({}) } }
        onLoadFailed: root.p = ({})
    }

    // …and the colour the WALLPAPER offers. ⚠ `ok` AND `use` both have to hold:
    // `ok` is the picture's own answer (a greyscale wallpaper has no hue to
    // give) and `use` is the setting. Reading the colour without checking both
    // is how windows come to wear a wallpaper their theme never asked for.
    property string wpAccent: ""

    property FileView wpPaletteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/palette.state"
        watchChanges: true
        printErrors: false          // absent until a wallpaper has been measured
        onFileChanged: reload()
        onLoaded: {
            const t   = this.text()
            const ok  = /^\s*ok\s*=\s*yes\s*$/m.test(t)
            const use = !/^\s*use\s*=\s*no\s*$/m.test(t)
            const m   = t.match(/^\s*accent\s*=\s*(#[0-9A-Fa-f]{6})\s*$/m)
            root.wpAccent = (ok && use && m) ? m[1] : ""
        }
        onLoadFailed: root.wpAccent = ""
    }

    // ── The desktop's font ──────────────────────────────────────────────────
    //
    // ⚠ BOTH HALVES OR NEITHER. Qt resolves an application's default font ONCE
    // at startup and QML cannot change it afterwards, so the family has to be
    // named on every Text and every size has to go through ui(). Doing one and
    // not the other gives a window that follows the desktop right up until
    // somebody changes the setting — which looks fixed at exactly the moment it
    // is tested.
    property string uiFont: ""
    property int textScale: 100

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

    function ui(px) { return Math.max(6, Math.round(px * root.textScale / 100)) }

    function themed(key, r, g, b, a) {
        const c = root.p[key]
        return (c && c.length === 3) ? Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, a)
                                     : Qt.rgba(r / 255, g / 255, b / 255, a)
    }
    function pick(dark, light) { return root.isLight ? light : dark }
    function lum(c) {
        function ch(v) { return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b)
    }
    function contrast(a, b) {
        const la = root.lum(a), lb = root.lum(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    // A theme accent is chosen to look good on the BAR, not to be legible as
    // text on this window's background. Nudged until it is, rather than trusted.
    function readable(c, on, want) {
        if (root.contrast(c, on) >= want) return c
        const up = root.lum(on) <= 0.18
        let out = c
        for (let i = 0; i < 16; i++) {
            out = up ? Qt.lighter(out, 1.25) : Qt.darker(out, 1.25)
            if (root.contrast(out, on) >= want) return out
        }
        return up ? "#ffffff" : "#000000"
    }

    readonly property color cPanel: themed("bar", 11, 11, 20, 1.0)
    readonly property color cBg:    isLight ? Qt.lighter(cPanel, 1.15) : Qt.darker(cPanel, 1.4)
    readonly property color cInk:   p.fg ? Qt.color(p.fg) : pick("#e6e9ef", "#12141a")
    readonly property color cText:  contrast(cInk, cBg) >= 4.5
                                    ? cInk : (lum(cBg) > 0.18 ? "#12141a" : "#e6e9ef")
    readonly property color cDim:   readable(pick("#8b93a7", "#4a5568"), cBg, 4.5)
    readonly property color cAccentRaw: root.wpAccent !== ""
                                        ? Qt.color(root.wpAccent)
                                        : themed("accent", 167, 139, 250, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cBg, 4.5)
    readonly property color cWarn:  readable(pick("#e0af68", "#8a5a00"), cBg, 4.5)
    readonly property color cBad:   readable(pick("#f7768e", "#a01030"), cBg, 4.5)
    readonly property color cLine:  pick(Qt.rgba(1, 1, 1, 0.07), Qt.rgba(0, 0, 0, 0.07))
    readonly property color cMine:  Qt.rgba(cAccent.r, cAccent.g, cAccent.b, 0.14)

    // ── Engine state ────────────────────────────────────────────────────────
    property string backend: "…"
    property string modelName: ""
    property bool   cloud: false
    property bool   busy: false
    property string mode: "auto"
    // What AUTO actually chose for the turn being answered. Shown beside the
    // selector so a routed turn is legible: "auto · plan" is the assistant
    // saying which way it went, which is the only thing that makes an
    // automatic choice reviewable rather than mysterious.
    property string turnMode: ""
    // What this box can do with a voice, reported by the engine rather than
    // probed here — the window must not load piper to find out whether piper
    // is installed.
    property string canSpeak: "no"
    property string canHear: "no"
    property bool   reading: false
    property bool   listening: false
    property bool   waking: false
    property string pendingId: ""
    property string pendingTool: ""
    property string pendingArgs: ""

    // The turns on screen. `kind` is who is speaking: me, it, tool, note, bad.
    ListModel { id: log }

    // A tool result is whatever the tool returned — a grep over a big file is
    // thousands of lines, and pasting all of it into the transcript buries the
    // reply that follows. The head of it is the part that says whether it
    // worked.
    // ⚠ SIX LINES CUT THE ANSWER IN HALF. The budget was set for a directory
    // listing, where the first few rows are a sample and the rest is more of
    // the same. A machine's specifications are not a sample — clipped at six,
    // "pc stats" showed the CPU and the memory and hid every drive behind
    // "… (6 more lines)", which is a chat window keeping the answer from the
    // person who asked for it. A screenful, and anything genuinely long is
    // still cut rather than allowed to take the window.
    readonly property int clipLines: 16
    readonly property int clipChars: 1200

    function clip(text) {
        var lines = text.split("\n")
        var head = lines.slice(0, root.clipLines).join("\n")
        if (head.length > root.clipChars)
            head = head.substring(0, root.clipChars) + "…"
        if (lines.length > root.clipLines)
            head += "\n… (" + (lines.length - root.clipLines) + " more lines)"
        return head
    }

    function say(kind, text) {
        log.append({ kind: kind, text: text })
        chat.positionViewAtEnd()
    }

    // The assistant's reply arrives token by token, so the last turn GROWS
    // rather than a new one being appended per chunk — otherwise a paragraph
    // is fifty rows in the list and the scroll position fights the reader.
    function append(text) {
        if (log.count > 0 && log.get(log.count - 1).kind === "it") {
            log.setProperty(log.count - 1, "text", log.get(log.count - 1).text + text)
        } else {
            log.append({ kind: "it", text: text })
        }
        chat.positionViewAtEnd()
    }

    // ── The wire ────────────────────────────────────────────────────────────
    function dec(s) {
        // ⛔ "%00" IS THE ENGINE'S EMPTY FIELD, and decodeURIComponent turns it
        // into a NUL CHARACTER, which is not "". The X branch below tells a
        // tool CALL from its RESULT by asking whether the name is empty — so
        // without this line it never is, every result renders as the call's
        // "…", and the tool output is dropped exactly as it was before that
        // branch was written. Mirrors dec() in serve.py.
        if (s === "%00") return ""
        if (s.indexOf("%") < 0) return s
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    function onRecord(line) {
        if (line === "") return
        const f = line.split("\t")
        const tag = f[0]
        const a = f.length > 1 ? dec(f[1]) : ""
        const b = f.length > 2 ? dec(f[2]) : ""
        const c = f.length > 3 ? dec(f[3]) : ""

        if (tag === "S") {
            if (a === "backend")  root.backend = b
            else if (a === "model")    root.modelName = b
            else if (a === "cloud")    root.cloud = (b === "yes")
            else if (a === "mode")     root.mode = b
            else if (a === "reset")    log.clear()
            root.firstRecord = true
        } else if (tag === "V") {
            if (a === "speak")          root.canSpeak = b
            else if (a === "listen")    root.canHear = b
            else if (a === "reading")   root.reading = (b === "yes")
            else if (a === "listening") root.listening = (b === "yes")
            else if (a === "wake")      root.waking = (b === "on")
            else if (a === "heard")     root.say("me", b)
        } else if (tag === "M") {
            root.turnMode = a
        } else if (tag === "U") {
            root.say("me", a)
            root.busy = true
        } else if (tag === "K") {
            // synsh answered it. Marked as its own kind, because "the desktop
            // answered this without a model" is worth being able to see.
            root.say("note", a)
        } else if (tag === "T") {
            root.append(a)
        } else if (tag === "X") {
            // ⛔ TWO RECORDS, TWO DIFFERENT FIELDS. The engine sends the CALL
            // as ("X", name, "…") and the RESULT as ("X", "", result) — so a
            // branch reading only `a` shows every call and DROPS every result,
            // errors included. That is how `Error: nothing here is called
            // 'downloads'` became a window that said the folder was open: the
            // one line saying otherwise was never drawn.
            if (a !== "") root.say("tool", a + "…")
            else if (b !== "") root.say(b.indexOf("Error") === 0 ? "bad" : "tool",
                                        root.clip(b))
        } else if (tag === "C") {
            root.pendingId = a
            root.pendingTool = b
            root.pendingArgs = c
        } else if (tag === "A") {
            root.say("bad", a)
        } else if (tag === "E") {
            root.busy = false
        }
    }

    // ⛔ NOTHING MAY BE WRITTEN BEFORE THE ENGINE HAS SPAWNED. A Process write
    // made before the child exists is DROPPED, in silence — so a line typed
    // into a window that opened a moment ago would vanish and the assistant
    // would look like it had ignored it. The engine's first record is the
    // proof it is there; anything typed before that waits in `queued`.
    property bool firstRecord: false
    property var queued: []

    function send(cmd) {
        if (!root.firstRecord) { root.queued.push(cmd); return }
        eng.write(cmd + "\n")
    }

    onFirstRecordChanged: {
        if (!root.firstRecord) return
        for (const q of root.queued) eng.write(q + "\n")
        root.queued = []
    }

    Process {
        id: eng
        running: true
        stdinEnabled: true
        command: [root.bin, "serve"]
        stdout: SplitParser {
            splitMarker: "\n"
            onRead: (line) => root.onRecord(line)
        }
        // The engine exiting is the window's cue to go too — otherwise the
        // window sits there taking lines that reach nothing.
        onExited: Qt.quit()
    }

    // ── Chrome ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        Rectangle {           // header
            id: head
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 40
            color: root.cPanel

            // ⚠ BOUND ON THE RIGHT, and elided. Anchored to the left alone it
            // keeps its full width at every window size, so shrinking the
            // window slides the model name straight under the controls — two
            // strings drawn in the same pixels, both unreadable.
            Text {
                anchors { left: parent.left; leftMargin: 14
                          right: ctrls.left; rightMargin: 12
                          verticalCenter: parent.verticalCenter }
                text: root.modelName
                elide: Text.ElideRight
                color: root.cText
                font.pixelSize: root.ui(13)
                font.family: "monospace"
            }

            // The right-hand controls, laid out rather than chained. A Row
            // drops the ones that are hidden and closes the gap itself, which
            // is the whole reason the name above can anchor to its left edge:
            // there is one edge to anchor to whatever this box can do.
            Row {
                id: ctrls
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 12

                // ⚠ HIDDEN, NOT GREYED, where the box cannot do it. A microphone
                // that cannot be pressed is a question the user has to go and
                // answer somewhere else; no microphone is a window that never
                // raised it.
                // ⛔ THE LOUDEST CONTROL IN THE WINDOW, and it looks it while it is
                // on. Armed, this leaves a microphone open until it is turned off.
                Text {
                    id: wakeBtn
                    visible: root.canHear !== "no"
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.waking ? "◉ answering to its name" : "wake"
                    color: root.waking ? root.cBad : root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.send("wake " + (root.waking ? "off" : "on"))
                    }
                }

                Text {
                    id: micBtn
                    visible: root.canHear !== "no"
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.listening ? "◉ listening" : "🎤"
                    color: root.listening ? root.cWarn : root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(13)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (!root.listening) root.send("listen")
                    }
                }

                Text {
                    id: readBtn
                    visible: root.canSpeak !== "no"
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.reading ? "🔊" : "🔇"
                    color: root.reading ? root.cAccent : root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(13)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        // Left click turns reading on and off; while it is talking,
                        // any click shuts it up first — the button you reach for
                        // when a long answer starts reading itself is this one.
                        onClicked: {
                            root.send("hush")
                            root.send("speak " + (root.reading ? "off" : "on"))
                        }
                    }
                }

                Rectangle {       // the cloud/local tell, in a word and a colour
                    id: pill
                    anchors.verticalCenter: parent.verticalCenter
                    width: tag.implicitWidth + 14; height: 20; radius: 10
                    color: root.cloud ? Qt.rgba(root.cWarn.r, root.cWarn.g, root.cWarn.b, 0.22)
                                      : Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.18)
                    Text {
                        id: tag
                        anchors.centerIn: parent
                        text: root.cloud ? "cloud" : "local"
                        color: root.cloud ? root.cWarn : root.cAccent
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(11)
                    }
                }
                Text {
                    id: modeBtn
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.mode + (root.mode === "auto" && root.turnMode !== ""
                                       ? " · " + root.turnMode : "") + " ▾"
                    color: root.cAccent
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: modeMenu.open()
                    }
                    Menu {
                        id: modeMenu
                        Repeater {
                            model: [["auto",  "Auto — picks per message"],
                                    ["ask",   "Ask — answers, touches nothing"],
                                    ["agent", "Agent — answers and does it"],
                                    ["plan",  "Plan — looks, and writes the steps"]]
                            MenuItem {
                                id: mrow
                                required property var modelData
                                text: mrow.modelData[1]
                                onTriggered: root.send("mode " + mrow.modelData[0])
                            }
                        }
                    }
                }

                Text {
                    id: menuBtn
                    anchors.verticalCenter: parent.verticalCenter
                    text: "backend ▾"
                    color: root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backends.open()
                    }
                    Menu {
                        id: backends
                        Repeater {
                            model: ["synapd", "ollama", "anthropic", "openai"]
                            MenuItem {
                                id: pick
                                required property string modelData
                                text: pick.modelData
                                onTriggered: root.send("provider " + pick.modelData)
                            }
                        }
                    }
                }
            }
        }

        ListView {             // the conversation
            id: chat
            anchors { top: head.bottom; left: parent.left; right: parent.right; bottom: ask.top }
            anchors.margins: 10
            clip: true
            spacing: 8
            model: log
            delegate: Item {
                id: turn
                required property string kind
                required property string text
                width: chat.width
                height: bubble.height

                Rectangle {
                    id: bubble
                    width: turn.width
                    height: body.implicitHeight + 16
                    radius: 8
                    color: turn.kind === "me" ? root.cMine : "transparent"
                    border.width: turn.kind === "bad" ? 1 : 0
                    border.color: root.cBad

                    Text {
                        id: body
                        anchors { fill: parent; margins: 8 }
                        text: turn.text
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        font.family: turn.kind === "tool" ? "monospace"
                                                          : (root.uiFont || "sans-serif")
                        font.pixelSize: root.ui(turn.kind === "tool" ? 12 : 13)
                        color: turn.kind === "bad"  ? root.cBad
                             : turn.kind === "tool" ? root.cDim
                             : turn.kind === "note" ? root.cAccent
                                                    : root.cText
                    }
                }
            }
        }

        // ── The confirmation strip ──────────────────────────────────────────
        //
        // ⚠ IT IS A STRIP AND NOT A DIALOG. A modal over a chat window steals
        // the keyboard from the thing the user was in the middle of typing, and
        // the answer here is one key either way.
        Rectangle {
            id: confirm
            visible: root.pendingId !== ""
            anchors { left: parent.left; right: parent.right; bottom: ask.top }
            height: visible ? 56 : 0
            color: Qt.rgba(root.cWarn.r, root.cWarn.g, root.cWarn.b, 0.14)

            Text {
                anchors { left: parent.left; leftMargin: 12; right: yes.left; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                text: root.pendingTool + "  " + root.pendingArgs
                elide: Text.ElideRight
                color: root.cText
                font.family: "monospace"
                font.pixelSize: root.ui(12)
            }
            Text {
                id: yes
                anchors { right: no.left; rightMargin: 14; verticalCenter: parent.verticalCenter }
                text: "Allow"
                color: root.cAccent
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(13)
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: { root.send("confirm " + root.pendingId + " yes"); root.pendingId = "" }
                }
            }
            Text {
                id: no
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                text: "Refuse"
                color: root.cBad
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(13)
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: { root.send("confirm " + root.pendingId + " no"); root.pendingId = "" }
                }
            }
        }

        Rectangle {            // the input
            id: ask
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            // ⚠ EVERY INSET IS PART OF THE HEIGHT. The frame insets 7 and the
            // scroller 8 more, top and bottom — 30 in all — so a strip built to
            // the text's height plus anything less than that clips the line
            // being typed, top and bottom, from the very first character.
            readonly property int chrome: 7 * 2 + 8 * 2
            height: Math.min(160, input.implicitHeight + ask.chrome)
            color: root.cPanel

            Rectangle {
                anchors { fill: parent; margins: 7 }
                radius: 6
                color: root.cBg
                border.width: 1
                border.color: input.activeFocus ? root.cAccent : root.cLine

                // Past the cap the strip stops growing, so what is being typed
                // has to be able to move: without this the caret walks off the
                // bottom edge and the user is typing where they cannot see.
                //
                // ⚠ `TextArea.flickable`, NOT a TextArea parented to one. The
                // attached form is what sizes the editor to the viewport and
                // keeps the cursor in view as it is typed; a plain child scrolls
                // nowhere and takes its width from its own content, which loses
                // the wrap.
                Flickable {
                    id: scroller
                    anchors { fill: parent; margins: 8 }
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    TextArea.flickable: TextArea {
                        id: input
                        padding: 0
                        wrapMode: TextEdit.Wrap
                        color: root.cText
                        placeholderText: root.busy ? "thinking…" : "Ask, or say what you want done"
                        placeholderTextColor: root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(13)
                        background: null
                        focus: true

                        // Enter sends, Shift+Enter is a newline. The other way round
                        // is how a chat box comes to eat half-written questions.
                        //
                        // ⚠ THE NAMED HANDLERS, NOT `Keys.onPressed`. A TextArea is
                        // a TextEdit and takes Return for itself; a generic
                        // onPressed attached to one never sees it, which reads as
                        // an Enter key that does nothing while every other key
                        // types fine.
                        Keys.onReturnPressed: (e) => input.submit(e)
                        Keys.onEnterPressed: (e) => input.submit(e)

                        function submit(e) {
                            if (e.modifiers & Qt.ShiftModifier) { e.accepted = false; return }
                            e.accepted = true
                            const t = input.text.trim()
                            if (t === "" || root.busy) return
                            root.send("ask " + encodeURIComponent(t))
                            input.text = ""
                        }
                    }
                }
            }
        }
    }
}
