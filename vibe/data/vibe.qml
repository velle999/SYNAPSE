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

    // ── The small box, and the button out of it ─────────────────────────────
    //
    // velle, 2026-08-30: *"i figures we'd have a button for full size window
    // and default to small box when you click like this"* — with a screenshot
    // of this window dragged down to its minimum.
    //
    // ⚠ THE DEFAULT IS THE QUICK ASK, NOT THE SESSION. Most of what this window
    // is for is one question and one answer; 820×640 landing on the desktop for
    // that is a window that has to be moved and resized before it can be used.
    // The long sessions are the other case, and they get a button.
    implicitWidth: 460
    implicitHeight: 380
    minimumSize: Qt.size(360, 260)

    // How much room there is, and what that is enough for. ⚠ MEASURED IN ui()
    // UNITS, not in pixels: at a 150% desktop font every control in the header
    // is half as wide again, and a breakpoint written in raw pixels would keep
    // showing a row that no longer fits.
    // ⚠ COMFORTABLY BELOW THE DEFAULT WIDTH, not level with it. A threshold set
    // to exactly the size the window opens at is a word that appears and
    // disappears as the window is nudged a pixel either way.
    readonly property bool roomForLabels: root.width >= root.ui(360)
    readonly property bool roomForVoice:  root.width >= root.ui(660)
    readonly property bool roomForPanel:  root.width >= root.ui(760)

    // The companion panel — velle.ai's half of this window. Auto: it is open
    // wherever there is room for it, which is what "full size" gets you.
    property bool panelWanted: true
    readonly property bool panelOn: root.roomForPanel && root.panelWanted

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

    // ⛔ ABSENT MEANS NOTHING IS RUNNING, which is also what a desktop that has
    // never started a timer looks like — so a failed load is the ordinary case
    // here, not an error worth printing.
    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/pomodoro.state"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const m = this.text().match(/^\s*ends\s*=\s*(\d+)\s*$/m)
            root.pomEnds = m ? parseInt(m[1]) : 0
            root.pomNow = Math.floor(Date.now() / 1000)
        }
        onLoadFailed: root.pomEnds = 0
    }

    Timer {
        // ⚠ Stopped when there is no timer: a 1 Hz repaint of a window that has
        // nothing to count is a wakeup a second, forever, on a laptop.
        running: root.pomEnds > 0
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: root.pomNow = Math.floor(Date.now() / 1000)
    }

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

    /*
     * ⛔ ONE SCROLLBAR, USED BY EVERY SCROLLING VIEW IN THIS WINDOW.
     *
     * The default ScrollBar is a hairline that fades to nothing when the view
     * is still, which on a dark chat window is indistinguishable from having no
     * scrollbar at all — which is how this window shipped without one. A view
     * that scrolls has to SAY it scrolls even when nobody is touching it: the
     * handle is the only thing on screen that says there is more above, how
     * much more, and where in it you are.
     *
     * ⚠ VISIBLE AT REST, not only while active. `active` is true while the
     * flickable moves or the bar is hovered — every state except the one where
     * a reader is deciding whether there is anything to scroll to.
     *
     * ⚠ AsNeeded, so a conversation shorter than the window has no bar. A rule
     * that every view scrolls is not a rule that every view draws furniture it
     * does not need.
     */
    component SynScrollBar: ScrollBar {
        id: sb
        policy: ScrollBar.AsNeeded
        /*
         * ⛔ NOTHING TO SCROLL MEANS NO SCROLLBAR AT ALL. AsNeeded hides the
         * handle by fading its OPACITY, and a custom contentItem replaces the
         * binding that does it — so a bar styled to be visible at rest became
         * visible at rest everywhere, a full-length handle that cannot move
         * sitting on every short list on the desktop. velle, 2026-08-28:
         * "if there's nothing to scroll the scrollbar should autohide. i don't
         * need the fucking scrollbars literally everywhere when they can't even
         * do anything."
         *
         * `size` is the fraction of the content the view can show: 1.0 means it
         * all fits. Visible at rest is for the case where there IS more — that
         * is the whole point of it — and is clutter in every other case.
         */
        readonly property bool needed:
            sb.policy === ScrollBar.AlwaysOn ||
            (sb.policy === ScrollBar.AsNeeded && sb.size < 1.0)
        visible: sb.needed
        implicitWidth: root.ui(11)
        padding: root.ui(2)

        contentItem: Rectangle {
            implicitWidth: root.ui(7)
            radius: width / 2
            color: sb.pressed ? root.cAccent : sb.hovered ? root.cText : root.cDim
            opacity: sb.pressed || sb.hovered ? 1.0 : 0.5
            Behavior on color   { ColorAnimation  { duration: 90 } }
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }

        background: Rectangle {
            radius: width / 2
            color: root.cLine
            opacity: sb.hovered || sb.pressed ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }

    /*
     * ⛔ A MENU IS PART OF THE WINDOW, AND HAS TO LOOK LIKE IT.
     *
     * An unstyled QtQuick Controls Menu draws in Qt's default style: a WHITE
     * popup with a blue highlight, in a window that is otherwise the desktop's
     * own colours. That was survivable while the menus here were two rarely
     * opened pickers; it is not, now that ☰ is where the companion's lists are
     * found. A theme switch has to move this with everything else.
     *
     * ⚠ THE DELEGATE IS FOR THE ROWS QML DOES NOT DECLARE. A nested Menu is
     * turned into a row of its parent by `addMenu()`, which builds it from
     * `delegate` — so a file that styles only the items it wrote out gets a
     * themed menu with two default-styled submenu rows in the middle of it.
     */
    component SynMenuItem: MenuItem {
        id: mi
        implicitHeight: root.ui(27)
        // ⚠ SIZED TO THE LONGEST ROW, not to a number picked once. The mode
        // picker's rows are whole sentences and the panel toggle's label runs
        // to five words — pinned at a fixed width they elide to "Hide the
        // companion p…", which is a menu that has stopped saying what it does.
        // ⚠ PLUS THE MENU'S OWN PADDING. A row whose implicit width is exactly
        // its text's is handed `availableWidth` — the popup minus that padding
        // — and elides by those few pixels, which is a row that fits everywhere
        // except on screen.
        /*
         * ⛔ MEASURED WITH TextMetrics, NOT OFF THE LABEL'S OWN implicitWidth.
         *
         * An eliding Text inside a control is handed `availableWidth`, which is
         * derived from this very property — so asking the label how wide it
         * wants to be settles at whatever width it was already given. Every row
         * came back at the floor and the longest label elided in a menu with a
         * clear inch of room beside it, twice, at two different fixed points.
         * TextMetrics measures the string against the font and knows nothing
         * about the layout, which is the only way out of that loop.
         *
         * ⚠ EVERY PADDING IS COUNTED, INCLUDING THE MENU'S OWN. This row's (10
         * either side, plus the submenu arrow's room where there is one) AND
         * the popup's 4 each side — leaving those eight pixels out left the
         * longest label two characters short of fitting in a menu measured
         * exactly for it. Rounded up as well: TextMetrics answers in fractions
         * of a pixel and a row short by one elides as badly as one short by
         * twenty.
         */
        // ⚠ THE ROW OWNS ITS PADDING, so the sum below is complete. Left on the
        // label, the control's own inset sat between them uncounted and the
        // width came out eleven pixels short however carefully the text was
        // measured — which is two characters, which is an ellipsis.
        leftPadding: root.ui(10)
        rightPadding: mi.subMenu ? root.ui(26) : root.ui(10)

        implicitWidth: Math.max(root.ui(180),
                                Math.ceil(mlabelSize.width)
                                + mi.leftPadding + mi.rightPadding
                                + root.ui(14))          // the popup's own

        TextMetrics {
            id: mlabelSize
            text: mi.text
            font.family: root.uiFont || "sans-serif"
            font.pixelSize: root.ui(12)
        }

        contentItem: Text {
            text: mi.text
            color: mi.enabled ? root.cText : root.cDim
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: root.uiFont || "sans-serif"
            font.pixelSize: root.ui(12)
        }
        background: Rectangle {
            color: mi.highlighted ? root.cMine : "transparent"
        }
        // ⚠ Drawn here rather than left to the style, for the same reason as
        // everything else in this block: the style's arrow is a dark glyph
        // chosen for a light popup.
        arrow: Text {
            visible: mi.subMenu
            anchors { right: parent.right; rightMargin: root.ui(8)
                      verticalCenter: parent.verticalCenter }
            text: "▸"
            color: root.cDim
            font.family: root.uiFont || "sans-serif"
            font.pixelSize: root.ui(11)
        }
    }

    component SynMenuSeparator: MenuSeparator {
        padding: root.ui(4)
        contentItem: Rectangle {
            implicitHeight: 1
            color: root.cLine
        }
    }

    component SynMenu: Menu {
        id: mnu
        padding: root.ui(4)
        delegate: SynMenuItem {}

        /*
         * ⛔ A Menu HERE DOES NOT GROW FOR ITS ROWS — IT IS THE BACKGROUND'S
         * WIDTH AND NOTHING ELSE.
         *
         * Measured, after two wrong fixes: with the background pinned at 210 the
         * popup was 210 whatever the rows asked for, and dropping the background
         * to 120 gave a 120-wide popup with EVERY label elided — `Start a …`,
         * `Read ans…`, `New conv…`. The row's own implicitWidth reaches the
         * popup's width calculation nowhere, so the widest row has to be
         * measured and handed over.
         *
         * ⚠ RE-MEASURED ON EVERY OPEN, not once. Half the labels here change
         * with what is true — "Full size" becomes "Exit full size", "Start a
         * focus timer" becomes "Stop the focus timer" — and a width taken when
         * the menu was built is the width of last time's words.
         */
        property int rowWidth: root.ui(150)

        function measure() {
            var w = root.ui(150)
            for (var i = 0; i < mnu.count; i++) {
                var it = mnu.itemAt(i)
                if (it && it.implicitWidth > w)
                    w = it.implicitWidth
            }
            mnu.rowWidth = w
        }

        implicitWidth: mnu.rowWidth
        onAboutToShow: mnu.measure()
        onCountChanged: mnu.measure()

        background: Rectangle {
            // ⚠ A FLOOR, NOT THE WIDTH. A Menu takes the wider of its
            // background's implicit width and its rows', so a background sized
            // to look right is a menu that never grows for a long label.
            implicitWidth: root.ui(120)
            radius: root.ui(6)
            color: root.cPanel
            border.width: 1
            border.color: root.cLine
        }
    }

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

    // The assistant's voice, and the set to choose from. Both come from the
    // engine's state records rather than being listed here — a second copy of
    // the persona names in QML is a list that goes stale the day one is added.
    property string persona: "default"
    property var personaList: []

    // The focus timer, as the bar sees it: a deadline in epoch seconds, and a
    // clock that ticks only while there is something to count.
    property int pomEnds: 0
    property int pomNow: 0
    property bool   reading: false
    property bool   listening: false
    property bool   waking: false
    property string pendingId: ""
    property string pendingTool: ""
    property string pendingArgs: ""

    // The turns on screen. `kind` is who is speaking: me, it, tool, note, bad.
    ListModel { id: log }

    // ── The companion's own records ─────────────────────────────────────────
    //
    // ⛔ THEY ARRIVE AS FIELDS, NOT AS THE CLI's LINES. `/todo` prints
    // `[ ] !! #3 buy milk` and that is right in a terminal; a panel has to draw
    // a checkbox and know which id it belongs to, so the engine sends `P`
    // records carrying the rows themselves. Parsing the printed line back into
    // fields here would be a second renderer that goes wrong the day one of
    // them changes.
    ListModel { id: todos }
    ListModel { id: habits }
    ListModel { id: goals }
    property var stats: ({})

    function fill(model, json) {
        var arr = []
        try { arr = JSON.parse(json) } catch (e) { arr = [] }
        model.clear()
        for (var i = 0; i < arr.length; i++)
            model.append(arr[i])
    }

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

    // ⛔ FOLLOW THE TAIL ONLY WHEN THE READER IS AT IT. Jumping to the end on
    // every record is right while the answer is being watched and wrong the
    // moment somebody scrolls up to re-read something: a streaming reply would
    // yank them back to the bottom several times a second, which is a scrollbar
    // that cannot be used. Anchored at the bottom, it follows; scrolled away,
    // it stays put and the bar shows the new content arriving below.
    readonly property int followSlack: 24

    function atTail() {
        return chat.contentHeight <= chat.height ||
               chat.contentY >= chat.contentHeight - chat.height - root.followSlack
    }

    function follow() {
        if (root.atTail()) chat.positionViewAtEnd()
    }

    function say(kind, text) {
        const tail = root.atTail()
        log.append({ kind: kind, text: text })
        if (tail) chat.positionViewAtEnd()
    }

    // The assistant's reply arrives token by token, so the last turn GROWS
    // rather than a new one being appended per chunk — otherwise a paragraph
    // is fifty rows in the list and the scroll position fights the reader.
    function append(text) {
        const tail = root.atTail()
        if (log.count > 0 && log.get(log.count - 1).kind === "it") {
            log.setProperty(log.count - 1, "text", log.get(log.count - 1).text + text)
        } else {
            log.append({ kind: "it", text: text })
        }
        if (tail) chat.positionViewAtEnd()
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
            else if (a === "persona")  root.persona = b
            else if (a === "personas") root.personaList = b.split(" ").filter(x => x.length)
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
        } else if (tag === "P") {
            if (a === "todos")       root.fill(todos, b)
            else if (a === "habits") root.fill(habits, b)
            else if (a === "goals")  root.fill(goals, b)
            else if (a === "stats")  { try { root.stats = JSON.parse(b) } catch (e) { root.stats = ({}) } }
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
        // The panel has nothing in it until it is asked. ⚠ Asked ONCE, here,
        // rather than polled: every later change comes back on the `P` records
        // the engine sends at the end of a turn, so a timer would be a query a
        // second answering a question nothing had asked.
        root.send("companion")
    }

    // A companion command, run as though it had been typed. ⚠ THROUGH THE SAME
    // PATH: `serve._slash()` answers these before synsh and before the model,
    // so a menu entry and a typed line cannot come to mean different things.
    function slash(cmd) { root.send("ask " + encodeURIComponent(cmd)) }

    // …and for the ones that need an argument. ⛔ PREFILLED, NOT RUN. `/quant`
    // with no ticker prints its usage, and a menu entry whose whole effect is a
    // usage line teaches people that the menu does not work.
    function prefill(text) {
        input.text = text
        input.cursorPosition = text.length
        input.forceActiveFocus()
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
            height: Math.max(40, root.ui(40))
            color: root.cPanel

            /*
             * ⛔ A HEADER IS A BUDGET, NOT A CHAIN.
             *
             * Every control used to sit in one Row anchored to the RIGHT edge,
             * which fits at 820 wide and runs off the LEFT of the window at
             * 420: dragged to its minimum this window drew `efault ▾` where the
             * persona chip was, and the model name — anchored between the left
             * edge and a Row wider than the window — had negative width and was
             * not drawn at all. velle, 2026-08-30: "the text formatting is a
             * bit of a mess", over a screenshot of exactly that.
             *
             * A Row cannot be asked to fit. So the header is: one menu button
             * pinned left, the controls that must be reachable at any size
             * pinned right, the model name taking whatever is left over and
             * eliding, and EVERYTHING ELSE in the ☰ menu — which is one glyph
             * at one width whatever the window is doing.
             *
             * ⚠ THE BREAKPOINTS ARE IN ui() UNITS. At a 150% desktop font every
             * control here is half as wide again; a threshold written in raw
             * pixels would go on showing a row that no longer fits.
             */
            Text {
                id: burger
                anchors { left: parent.left; leftMargin: 12
                          verticalCenter: parent.verticalCenter }
                text: "☰"
                color: mainMenu.opened ? root.cAccent : root.cText
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(15)
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6          // a 15px glyph is not a target
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mainMenu.open()
                }

                /*
                 * ⛔ THIS IS WHERE THE COMPANION LIVES ON A SMALL WINDOW.
                 *
                 * velle, 2026-08-30: "i don't really see the velle.ai
                 * features". They shipped as slash commands and nothing on
                 * screen said so — a feature nobody can find is a feature that
                 * did not ship. Every one of them is named here, and the ones
                 * that need an argument PREFILL the input rather than running:
                 * a menu entry that fails because it was given no ticker
                 * teaches people not to use the menu.
                 */
                SynMenu {
                    id: mainMenu

                    SynMenuItem {
                        text: "Tasks"
                        onTriggered: root.slash("/todo")
                    }
                    SynMenuItem {
                        text: "Habits"
                        onTriggered: root.slash("/habit")
                    }
                    SynMenuItem {
                        text: "Goals"
                        onTriggered: root.slash("/goal")
                    }
                    // ⚠ IT SAYS WHICH WAY IT GOES. One entry that starts a
                    // timer and stops it is fine; one that does not say which
                    // is a coin toss on a timer somebody is relying on.
                    SynMenuItem {
                        text: root.pomEnds > 0 ? "Stop the focus timer"
                                               : "Start a focus timer"
                        onTriggered: root.slash(root.pomEnds > 0 ? "/pom stop"
                                                                 : "/pom start")
                    }
                    SynMenuItem {
                        text: "Markets…"
                        onTriggered: root.prefill("/quant ")
                    }

                    SynMenuSeparator {}

                    SynMenu {
                        title: "Persona"
                        // ⚠ The names come from the ENGINE (root.personaList,
                        // an S record), not from a list written out here — a
                        // second copy in QML is the one that goes stale the day
                        // a persona is added, and it would go stale silently.
                        Repeater {
                            model: root.personaList
                            SynMenuItem {
                                id: prow
                                required property string modelData
                                text: prow.modelData
                                onTriggered: root.send("persona " + prow.modelData)
                            }
                        }
                    }
                    SynMenu {
                        title: "Backend"
                        Repeater {
                            model: ["synapd", "ollama", "anthropic", "openai"]
                            SynMenuItem {
                                id: brow
                                required property string modelData
                                text: brow.modelData
                                onTriggered: root.send("provider " + brow.modelData)
                            }
                        }
                    }

                    SynMenuSeparator {}

                    // ⚠ HIDDEN, NOT GREYED, where the box cannot do it. A
                    // microphone that cannot be pressed is a question the user
                    // has to go and answer somewhere else; no microphone is a
                    // window that never raised it.
                    SynMenuItem {
                        text: root.reading ? "Stop reading aloud" : "Read answers aloud"
                        visible: root.canSpeak !== "no"
                        height: visible ? implicitHeight : 0
                        onTriggered: { root.send("hush")
                                       root.send("speak " + (root.reading ? "off" : "on")) }
                    }
                    SynMenuItem {
                        text: "Listen"
                        visible: root.canHear !== "no"
                        height: visible ? implicitHeight : 0
                        onTriggered: if (!root.listening) root.send("listen")
                    }
                    // ⛔ THE LOUDEST CONTROL IN THE WINDOW. Armed, this leaves a
                    // microphone open until it is turned off — so it says which
                    // state it is in rather than merely offering the switch.
                    SynMenuItem {
                        text: root.waking ? "Stop answering to its name"
                                          : "Answer to its name"
                        visible: root.canHear !== "no"
                        height: visible ? implicitHeight : 0
                        onTriggered: root.send("wake " + (root.waking ? "off" : "on"))
                    }

                    SynMenuSeparator {}

                    SynMenuItem {
                        text: root.panelWanted ? "Hide the companion panel"
                                               : "Show the companion panel"
                        // ⚠ It says why it cannot, rather than vanishing: a
                        // control that is not there reads as one that does not
                        // exist, and the panel does — there is no room for it.
                        enabled: root.roomForPanel
                        onTriggered: root.panelWanted = !root.panelWanted
                    }
                    SynMenuItem {
                        text: root.fullscreen ? "Leave full size" : "Full size"
                        onTriggered: root.fullscreen = !root.fullscreen
                    }
                    SynMenuItem {
                        text: "New conversation"
                        onTriggered: root.send("reset")
                    }
                }
            }

            // ⚠ BOUND ON BOTH SIDES, and elided. Anchored to the left alone it
            // keeps its full width at every window size, so shrinking the
            // window slides the model name straight under the controls — two
            // strings drawn in the same pixels, both unreadable.
            Text {
                anchors { left: burger.right; leftMargin: 10
                          right: ctrls.left; rightMargin: 10
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
                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                spacing: 10

                // ── The focus timer ────────────────────────────────────────
                //
                // ⛔ IT READS THE STATE FILE, NOT THE ENGINE. The timer outlives
                // this window on purpose (see vibe/pomodoro.py), and the bar
                // shows the same file — so the chat window is a THIRD reader of
                // one fact rather than a second source of it. The countdown is
                // arithmetic on the deadline: nothing is spawned per second and
                // nothing has to be rewritten to stay true.
                Text {
                    id: pomChip
                    visible: root.pomEnds > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        const left = Math.max(0, root.pomEnds - root.pomNow)
                        if (left <= 0) return "focus done"
                        const m = Math.floor(left / 60), sec = left % 60
                        return m + ":" + (sec < 10 ? "0" : "") + sec
                    }
                    color: (root.pomEnds - root.pomNow) <= 0 ? root.cWarn : root.cDim
                    font.family: "monospace"
                    font.pixelSize: root.ui(12)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.slash("/pom stop")
                    }
                }

                // ── The voice, where there is room for it ──────────────────
                //
                // ⚠ IN THE MENU AS WELL, ALWAYS. These two are the only
                // controls that come and go with the window's width, and a
                // control that disappears when a window is made smaller has to
                // still be somewhere — otherwise turning a listening microphone
                // off means making the window bigger first.
                Text {
                    id: micBtn
                    visible: root.canHear !== "no" && (root.roomForVoice || root.listening)
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
                    visible: root.canSpeak !== "no" && (root.roomForVoice || root.reading)
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

                // ⛔ THE WAKE INDICATOR IS NOT A CONVENIENCE AND DOES NOT HIDE.
                // Armed, a microphone in this room is open; a window narrow
                // enough to drop the switch must not drop the disclosure with
                // it. Off, there is nothing to say and it takes no room.
                Text {
                    id: wakeBtn
                    visible: root.waking
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.roomForLabels ? "◉ answering to its name" : "◉"
                    color: root.cBad
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.send("wake off")
                    }
                }

                Rectangle {       // the cloud/local tell, in a word and a colour
                    id: pill
                    anchors.verticalCenter: parent.verticalCenter
                    width: tag.implicitWidth + root.ui(14)
                    height: root.ui(20)
                    radius: height / 2
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
                    // ⚠ CAPPED AND ELIDED. `auto · plan ▾` is half again the
                    // width of `ask ▾`, and a control that grows with what the
                    // router chose is a header that reflows mid-answer.
                    width: Math.min(implicitWidth, root.width * 0.28)
                    elide: Text.ElideRight
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
                    SynMenu {
                        id: modeMenu
                        Repeater {
                            model: [["auto",  "Auto — picks per message"],
                                    ["ask",   "Ask — answers, touches nothing"],
                                    ["agent", "Agent — answers and does it"],
                                    ["plan",  "Plan — looks, and writes the steps"]]
                            SynMenuItem {
                                id: mrow
                                required property var modelData
                                text: mrow.modelData[1]
                                onTriggered: root.send("mode " + mrow.modelData[0])
                            }
                        }
                    }
                }

                /*
                 * ⛔ FULL SIZE IS A BUTTON, AND IT LOOKS LIKE ONE.
                 *
                 * This was already here as a bare `⤢` in the dim colour every
                 * other label uses, eighth along a row that had run off the
                 * edge of the window — velle, 2026-08-30: "i figures we'd have
                 * a button for full size window". It existed and it was not
                 * findable, which for a control is the same thing. A border, a
                 * hover, its own word where there is room, and pinned last so
                 * it is in the same place at every size.
                 *
                 * ⛔ FULLSCREEN IS A WINDOW PROPERTY, NOT A COMPOSITOR REQUEST.
                 * FloatingWindow carries `fullscreen`, so this works on KDE and
                 * GNOME as well as here — asking synui to do it would give the
                 * window a control that silently does nothing on two of the
                 * three desktops SynapseOS ships onto.
                 */
                Rectangle {
                    id: fsBtn
                    anchors.verticalCenter: parent.verticalCenter
                    width: fsRow.implicitWidth + root.ui(14)
                    height: root.ui(22)
                    radius: root.ui(5)
                    color: fsArea.containsMouse
                           ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.18)
                           : "transparent"
                    border.width: 1
                    border.color: fsArea.containsMouse ? root.cAccent : root.cDim
                    Behavior on color { ColorAnimation { duration: 90 } }

                    Row {
                        id: fsRow
                        anchors.centerIn: parent
                        spacing: root.ui(5)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.fullscreen ? "⤡" : "⤢"
                            color: fsArea.containsMouse ? root.cAccent : root.cText
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(13)
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.roomForLabels
                            text: root.fullscreen ? "Exit full size" : "Full size"
                            color: fsArea.containsMouse ? root.cAccent : root.cText
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(12)
                        }
                    }
                    MouseArea {
                        id: fsArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.fullscreen = !root.fullscreen
                    }
                }
            }
        }

        ListView {             // the conversation
            id: chat
            anchors { top: head.bottom; left: parent.left; bottom: ask.top
                      right: root.panelOn ? panel.left : parent.right }
            anchors.margins: 10
            clip: true
            spacing: 8
            model: log

            // ⛔ A WINDOW THAT SCROLLS SHOWS THAT IT SCROLLS. A wheel and a
            // drag are not a substitute: without a bar there is nothing saying
            // there is anything above the top of the view, no way to see how
            // far back it goes, and no way to get there in one gesture.
            // The rule is pinned by preflight's `scrollbar` gate.
            ScrollBar.vertical: SynScrollBar {}
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


        /*
         * ── The companion panel ─────────────────────────────────────────────
         *
         * ⛔ THE FEATURES HAD TO BE ON SCREEN. velle, 2026-08-30: "i don't
         * really see the velle.ai features". Todos, habits, goals and the focus
         * timer all shipped and all of them were reachable only by typing a
         * slash command that nothing in the window mentioned. A capability
         * nobody can find has not been delivered.
         *
         * ⚠ IT DRAWS `P` RECORDS, IT DOES NOT READ THE DATABASE. sqlite is two
         * processes away from here on purpose — one writer, one idea of what
         * "done" means, and the streak arithmetic a tick sets off lives in
         * productivity.py where the CLI calls it too.
         *
         * ⚠ ONLY WHERE THERE IS ROOM. On the small box a 300px panel is the
         * window; the ☰ menu is the way in there, and it names every one of
         * these lists. `panelOn` is the width test and the setting together.
         */
        Rectangle {
            id: panel
            visible: root.panelOn
            anchors { top: head.bottom; right: parent.right; bottom: ask.top }
            width: root.panelOn ? Math.min(root.ui(320), Math.round(root.width * 0.34)) : 0
            color: root.isLight ? Qt.darker(root.cBg, 1.04) : Qt.lighter(root.cBg, 1.5)

            Rectangle {         // the seam, so the panel is a place and not a margin
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: 1
                color: root.cLine
            }

            Flickable {
                id: panelView
                anchors { fill: parent; leftMargin: 1 }
                clip: true
                contentWidth: width
                contentHeight: sections.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                // ⛔ A VIEW THAT SCROLLS SAYS SO — the rule this window was
                // pinned by once already. A day's tasks outgrow 300 pixels.
                ScrollBar.vertical: SynScrollBar {}

                Column {
                    id: sections
                    width: panelView.width - root.ui(11)
                    spacing: root.ui(6)
                    topPadding: root.ui(10)
                    bottomPadding: root.ui(14)
                    leftPadding: root.ui(12)

                    // ── Focus ──────────────────────────────────────────────
                    //
                    // ⚠ ONE FACT, THREE READERS. This is the bar's file and the
                    // CLI's, arithmetic on a deadline rather than a countdown
                    // anybody has to keep writing down.
                    Text {
                        text: "FOCUS"
                        color: root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(10)
                        font.letterSpacing: 1
                    }
                    Row {
                        spacing: root.ui(8)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                if (root.pomEnds <= 0) return "—"
                                const left = Math.max(0, root.pomEnds - root.pomNow)
                                if (left <= 0) return "done"
                                const m = Math.floor(left / 60), s = left % 60
                                return m + ":" + (s < 10 ? "0" : "") + s
                            }
                            color: root.pomEnds > 0 ? root.cAccent : root.cDim
                            font.family: "monospace"
                            font.pixelSize: root.ui(18)
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.pomEnds > 0 ? "stop" : "start 25 min"
                            color: root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(11)
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.slash(root.pomEnds > 0 ? "/pom stop"
                                                                      : "/pom start")
                            }
                        }
                    }

                    Item { width: 1; height: root.ui(8) }

                    // ── Tasks ──────────────────────────────────────────────
                    Row {
                        width: sections.width - root.ui(12)
                        spacing: root.ui(6)
                        Text {
                            text: "TASKS"
                            color: root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(10)
                            font.letterSpacing: 1
                        }
                        Text {
                            // ⚠ The overdue count is the one worth a colour. A
                            // panel that shades every number teaches the eye to
                            // ignore all of them.
                            visible: (root.stats.overdue || 0) > 0
                            text: (root.stats.overdue || 0) + " overdue"
                            color: root.cBad
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(10)
                        }
                        Text {
                            text: "＋"
                            color: root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(11)
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.prefill("/todo add ")
                            }
                        }
                    }

                    Text {
                        visible: todos.count === 0
                        width: sections.width - root.ui(12)
                        text: "Nothing on the list. ＋, or type /todo add …"
                        wrapMode: Text.Wrap
                        color: root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(11)
                    }

                    Repeater {
                        model: todos
                        Row {
                            id: trow
                            required property int id
                            required property string text
                            required property string status
                            required property int prio
                            required property string due
                            readonly property bool done: trow.status === "done"

                            width: sections.width - root.ui(12)
                            spacing: root.ui(6)

                            // ⛔ A TICK IS A TOGGLE, both ways — the engine
                            // reopens a task it finds already done. A checkbox
                            // that only goes one way turns a misclick into
                            // something to be undone from a terminal.
                            Text {
                                anchors.top: parent.top
                                text: trow.done ? "☑" : "☐"
                                color: trow.done ? root.cDim : root.cAccent
                                font.family: root.uiFont || "sans-serif"
                                font.pixelSize: root.ui(13)
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.send("check todo " + trow.id)
                                }
                            }
                            Text {
                                width: trow.width - root.ui(25)
                                text: trow.text
                                      + (trow.due !== "" ? "  " + trow.due : "")
                                wrapMode: Text.Wrap
                                // ⚠ Struck through rather than removed. A list
                                // that deletes the row you just ticked gives no
                                // sign anything happened where you clicked.
                                font.strikeout: trow.done
                                color: trow.done ? root.cDim
                                     : trow.prio <= 1 ? root.cWarn : root.cText
                                font.family: root.uiFont || "sans-serif"
                                font.pixelSize: root.ui(12)
                            }
                        }
                    }

                    Item { width: 1; height: root.ui(8) }

                    // ── Habits ─────────────────────────────────────────────
                    Row {
                        width: sections.width - root.ui(12)
                        spacing: root.ui(6)
                        Text {
                            text: "HABITS"
                            color: root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(10)
                            font.letterSpacing: 1
                        }
                        Text {
                            text: "＋"
                            color: root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(11)
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.prefill("/habit add ")
                            }
                        }
                    }

                    Text {
                        visible: habits.count === 0
                        width: sections.width - root.ui(12)
                        text: "No habits yet. ＋, or type /habit add …"
                        wrapMode: Text.Wrap
                        color: root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(11)
                    }

                    Repeater {
                        model: habits
                        Row {
                            id: hrow
                            required property int id
                            required property string name
                            required property bool today
                            required property int streak
                            required property string week

                            width: sections.width - root.ui(12)
                            spacing: root.ui(6)

                            Text {
                                text: hrow.today ? "☑" : "☐"
                                color: hrow.today ? root.cAccent : root.cDim
                                font.family: root.uiFont || "sans-serif"
                                font.pixelSize: root.ui(13)
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.send("check habit " + hrow.id)
                                }
                            }
                            Text {
                                width: hrow.width - root.ui(96)
                                text: hrow.name
                                elide: Text.ElideRight
                                color: root.cText
                                font.family: root.uiFont || "sans-serif"
                                font.pixelSize: root.ui(12)
                            }
                            // ⚠ MONOSPACE, because it is a grid and not a word:
                            // seven cells that have to line up under each other
                            // down the column.
                            Text {
                                text: hrow.week
                                color: root.cAccent
                                font.family: "monospace"
                                font.pixelSize: root.ui(11)
                            }
                            Text {
                                text: hrow.streak > 0 ? hrow.streak + "d" : ""
                                color: root.cDim
                                font.family: "monospace"
                                font.pixelSize: root.ui(11)
                            }
                        }
                    }

                    Item { width: 1; height: root.ui(8) }

                    // ── Goals ──────────────────────────────────────────────
                    Row {
                        width: sections.width - root.ui(12)
                        spacing: root.ui(6)
                        Text {
                            text: "GOALS"
                            color: root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(10)
                            font.letterSpacing: 1
                        }
                        Text {
                            text: "＋"
                            color: root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(11)
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.prefill("/goal add ")
                            }
                        }
                    }

                    Text {
                        visible: goals.count === 0
                        width: sections.width - root.ui(12)
                        text: "No goals yet. ＋, or type /goal add …"
                        wrapMode: Text.Wrap
                        color: root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(11)
                    }

                    Repeater {
                        model: goals
                        Column {
                            id: grow
                            required property int id
                            required property string title
                            required property int progress

                            width: sections.width - root.ui(12)
                            spacing: root.ui(3)
                            topPadding: root.ui(3)

                            Row {
                                width: grow.width
                                spacing: root.ui(6)
                                Text {
                                    width: grow.width - root.ui(38)
                                    text: grow.title
                                    elide: Text.ElideRight
                                    color: root.cText
                                    font.family: root.uiFont || "sans-serif"
                                    font.pixelSize: root.ui(12)
                                }
                                Text {
                                    text: grow.progress + "%"
                                    color: root.cDim
                                    font.family: "monospace"
                                    font.pixelSize: root.ui(11)
                                }
                            }
                            Rectangle {
                                width: grow.width
                                height: root.ui(4)
                                radius: height / 2
                                color: root.cLine
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(100, grow.progress)) / 100
                                    height: parent.height
                                    radius: parent.radius
                                    color: root.cAccent
                                }
                            }
                        }
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

        /*
         * ── What a slash can do ─────────────────────────────────────────────
         *
         * ⛔ THE SECOND HALF OF "I DON'T REALLY SEE THE velle.ai FEATURES".
         * The ☰ menu is where they are found; this is where they are LEARNT.
         * `/todo add buy milk` is only obvious to somebody who already knows
         * the list exists, and a chat box gives no hint that a leading slash
         * means anything at all — so the moment one is typed, the window says
         * what it has.
         *
         * ⚠ IT IS A HINT, NOT A COMPLETER. It takes no focus and swallows no
         * keys: `/todo add buy milk` typed straight through must reach the
         * engine exactly as it did before this existed. Clicking a row fills
         * the verb in; Enter still sends whatever is in the box.
         */
        Rectangle {
            id: hints
            // Only while the verb is still being typed — once there is a space
            // the user is writing arguments and a list of verbs is in the way.
            visible: root.pendingId === "" && /^\/[a-z]*$/.test(input.text)
            anchors { left: parent.left; right: parent.right; bottom: ask.top }
            anchors.margins: 0
            height: visible ? hintCol.implicitHeight + root.ui(10) : 0
            color: root.cPanel

            Column {
                id: hintCol
                anchors { left: parent.left; right: parent.right
                          top: parent.top; topMargin: root.ui(5)
                          leftMargin: root.ui(12); rightMargin: root.ui(12) }

                Repeater {
                    model: [["/todo",    "tasks — list, add, done"],
                            ["/habit",   "habits, and the streak on each"],
                            ["/goal",    "goals and their milestones"],
                            ["/pom",     "the focus timer"],
                            ["/quant",   "a ticker's price and indicators"],
                            ["/persona", "the voice it answers in"]]

                    Rectangle {
                        id: hrowItem
                        required property var modelData
                        // ⚠ Filtered on what has been typed so far, so `/h`
                        // narrows to the one line that matters instead of
                        // making the reader find it again.
                        readonly property bool hit:
                            hrowItem.modelData[0].indexOf(input.text) === 0
                        visible: hrowItem.hit
                        width: hintCol.width
                        height: visible ? root.ui(20) : 0
                        color: hintArea.containsMouse ? root.cMine : "transparent"

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: root.ui(8)
                            Text {
                                text: hrowItem.modelData[0]
                                color: root.cAccent
                                font.family: "monospace"
                                font.pixelSize: root.ui(12)
                            }
                            Text {
                                text: hrowItem.modelData[1]
                                color: root.cDim
                                font.family: root.uiFont || "sans-serif"
                                font.pixelSize: root.ui(11)
                            }
                        }
                        MouseArea {
                            id: hintArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.prefill(hrowItem.modelData[0] + " ")
                        }
                    }
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

                    // The input wraps and grows past its box on a long question,
                    // so it scrolls too — and says so, same as the conversation.
                    ScrollBar.vertical: SynScrollBar {}

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
