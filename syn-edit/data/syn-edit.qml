// syn-edit — the SynapseOS text editor.
//
// A renderer, and nothing more. This file owns NO text.
//
// ── Why that is the whole design ───────────────────────────────────────────
//
// The obvious way to build an editor on quickshell is to put a TextEdit on
// screen and let Qt own the buffer. That would be a second editor: a second
// idea of what a word is, a second undo stack, and no modal editing at all
// unless the entire vim layer were written again in QML. Instead a long-lived
// `syn-edit serve` holds the buffer; this window sends keys and draws frames.
// ciw, :%s/…/…/g, undo, registers and macros all work here because none of
// them are implemented here.
//
// ── The one rule for reading records ───────────────────────────────────────
//
// EVERY field arrives percent-encoded, including the ones that look like plain
// words. A line of source code can hold a tab, a file name can hold any byte
// at all, and decoding "the ones that need it" means keeping a list that will
// drift. So: decode every field, once, at the parse. See disp().
//
// ── What the engine decides, and this file does not ────────────────────────
//
// Lines arrive with tabs ALREADY expanded and spans in DISPLAY COLUMNS, and
// the caret arrives as a display column too. None of that is recomputed here.
// A renderer that expanded its own tabs would eventually disagree with the one
// in the engine, and the visible symptom of that is a caret that sits next to
// the character it is actually on.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Quickshell
import Quickshell.Io

FloatingWindow {
    id: root

    title: (root.st.file ? root.st.file.replace(/^.*\//, "") : "syn-edit")
           + (root.st.modified === "1" ? " •" : "") + " — SYNAPSE Edit"
    implicitWidth: 1080
    implicitHeight: 720
    // Below this the gutter, the sidebar and a usable number of columns stop
    // fitting together, and a layout with no floor does not degrade — it
    // paints over itself.
    minimumSize: Qt.size(640, 360)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNEDIT_BIN") || "syn-edit"

    // ── Theme ───────────────────────────────────────────────────────────────
    //
    // Read from the desktop, not hardcoded, and the same source and shape as
    // synfiles, syn-disks and the bar — so a theme switch moves all of them
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

    // ── …and the colour the WALLPAPER offers ────────────────────────────────
    //
    // synui measures a small palette off the wallpaper and publishes it here;
    // the bar reads this same file, in synui's quickshell/Theme.qml. This
    // window read theme.json alone, so on a desktop with the wallpaper accent
    // switched on the bar went the colour of the picture and every app window
    // beside it stayed the preset's — the half-applied feature 387 fixed
    // inside the bar, one process further out.
    //
    // A file and not the bar's Theme singleton: that singleton lives in synui's
    // package and this is a different one, and an import across the two breaks
    // the moment either is installed alone. The contract is the file, exactly
    // as it already is for theme.json.
    //
    // ⚠ `ok` AND `use` BOTH HAVE TO HOLD. `ok` is the PICTURE's answer — a
    // greyscale wallpaper has no hue to offer. `use` is the SETTING (Control
    // panel ▸ Appearance ▸ Wallpaper accent, where auto means Prism and nothing
    // else) and synui writes the file whichever way it is set. Reading the
    // colour without checking it is how the bar came to wear the wallpaper on
    // themes that never asked for it (386).
    //
    // Missing, unreadable, or refused all come out as the empty string, which
    // falls through to the theme's own accent below — the same answer as a
    // desktop that has never measured one, or a synui too old to write the
    // file. None of those needs telling apart here.
    //
    // ⚠ NOT `paletteFile`. That name is theme.json's reader in some of these
    // windows, and QML answers a duplicate property by refusing to load the
    // TYPE, naming a line that is not this one.
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
    readonly property color cBg: isLight ? Qt.lighter(cPanel, 1.15) : Qt.darker(cPanel, 1.4)
    readonly property color cInk: p.fg ? Qt.color(p.fg) : pick("#e6e9ef", "#12141a")
    readonly property color cText: contrast(cInk, cBg) >= 4.5
                                   ? cInk
                                   : (lum(cBg) > 0.18 ? "#12141a" : "#e6e9ef")
    readonly property color cDim:    pick("#8b93a7", "#4a5568")
    readonly property color cAccentRaw: root.wpAccent !== ""
                                        ? Qt.color(root.wpAccent)
                                        : themed("accent", 167, 139, 250, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)
    readonly property color cWarn: readable(pick("#e0af68", "#8a5a00"), cBg, 4.5)
    readonly property color cBad:  readable(pick("#f7768e", "#a01030"), cBg, 4.5)
    readonly property color cGood: readable(pick("#9ece6a", "#2f6f10"), cBg, 4.5)
    readonly property color cLine: pick(Qt.rgba(1, 1, 1, 0.05), Qt.rgba(0, 0, 0, 0.05))
    readonly property color cSel:  Qt.rgba(cAccent.r, cAccent.g, cAccent.b, 0.28)

    function wash(a) { return Qt.rgba(cAccent.r, cAccent.g, cAccent.b, a) }

    // Syntax colours. Held to the same contrast rule as everything else rather
    // than being fixed hexes that vanish on a pale theme.
    function tokColour(k) {
        switch (k) {
        case "keyword":  return root.readable(root.pick("#c39cf7", "#6d28d9"), root.cBg, 4.5)
        case "type":     return root.readable(root.pick("#7dcfff", "#0369a1"), root.cBg, 4.5)
        case "constant": return root.readable(root.pick("#ff9e64", "#9a3412"), root.cBg, 4.5)
        case "string":   return root.readable(root.pick("#9ece6a", "#15803d"), root.cBg, 4.5)
        case "char":     return root.readable(root.pick("#9ece6a", "#15803d"), root.cBg, 4.5)
        case "number":   return root.readable(root.pick("#ff9e64", "#9a3412"), root.cBg, 4.5)
        case "comment":  return root.readable(root.pick("#6b7280", "#6b7280"), root.cBg, 3.0)
        case "preproc":  return root.readable(root.pick("#d8a0df", "#86198f"), root.cBg, 4.5)
        case "func":     return root.readable(root.pick("#7aa2f7", "#1d4ed8"), root.cBg, 4.5)
        case "operator": return root.cDim
        case "heading":  return root.cAccent
        case "added":    return root.cGood
        case "removed":  return root.cBad
        default:         return root.cText
        }
    }

    // ── Fonts ───────────────────────────────────────────────────────────────
    //
    // Qt resolves an application's default font ONCE at startup from the
    // platform theme and QML cannot change it, so every Text below names the
    // family and the name is a binding — otherwise this window keeps the old
    // face until it is reopened. Same file the bar and synfiles watch.
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
            // The text scale lives in the same file, because it is a property
            // of the DESKTOP and not of this window — a per-app slider is how
            // synfiles ended up drawing at 115% beside two sibling windows
            // stuck at 100, which reads as "the theming missed those apps".
            const s = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            root.textScale = s ? parseInt(s[1]) : 100
        }
        onLoadFailed: { root.uiFont = ""; root.textScale = 100 }
    }

    function ui(px) { return Math.max(6, Math.round(px * root.textScale / 100)) }

    // ⚠ The EDITOR font is deliberately NOT the desktop font. Code is read in
    // columns, and a proportional face makes the caret column and the text
    // disagree — which is the one thing this window is careful about
    // everywhere else. It follows the desktop SIZE, not the desktop family.
    readonly property string monoFont: "monospace"
    readonly property int monoSize: root.ui(13)

    TextMetrics {
        id: fm
        font.family: root.monoFont
        font.pixelSize: root.monoSize
        // A digit rather than "M": in a font that is not truly monospaced the
        // digits are still tabular far more often than the letters are, so a
        // near-miss stays a near-miss instead of accumulating across a line.
        text: "0"
    }
    readonly property real charW: Math.max(1, fm.advanceWidth)
    readonly property real lineH: Math.max(1, Math.ceil(fm.height * 1.25))

    // ── The engine ──────────────────────────────────────────────────────────

    property var st: ({})          // the S records of the last complete frame
    property var lines: []         // { no, text }
    property var spans: ({})       // line number -> [ { start, len, tok } ]
    property var bufs: []          // { idx, name, modified, current }
    property bool engineUp: false

    // A frame is accumulated and swapped in whole on the E record. Drawing as
    // records arrive would repaint through half-built states on every
    // keystroke, which looks exactly like the editor being slow.
    property var pending: ({ st: ({}), lines: [], spans: ({}), bufs: [] })

    // A vim-modal engine starts in NORMAL, which reads as "the editor is
    // broken" to someone who opens it and types: nothing appears. Fired once,
    // off the FIRST frame only — after that, leaving INSERT is something the
    // user did on purpose and no button gets to second-guess it.
    property bool startupInsertSent: false

    function disp(s) {
        // decodeURIComponent THROWS on a percent sequence that is not valid
        // UTF-8, and real files are not always valid UTF-8 — a file saved on a
        // Windows machine in a CP1252 locale is the ordinary case. Showing the
        // raw encoded form is ugly; letting the exception escape empties the
        // whole window, which is how one odd byte makes the file disappear.
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    function onRecord(raw) {
        if (raw === "") return
        const f = raw.split("\t")
        const tag = f[0]

        if (tag === "S") {
            root.pending.st[root.disp(f[1])] = root.disp(f[2] || "")
        } else if (tag === "L") {
            root.pending.lines.push({ no: parseInt(f[1]), text: root.disp(f[2] || "") })
        } else if (tag === "H") {
            const n = parseInt(f[1])
            if (!root.pending.spans[n]) root.pending.spans[n] = []
            root.pending.spans[n].push({ start: parseInt(f[2]), len: parseInt(f[3]),
                                         tok: root.disp(f[4] || "") })
        } else if (tag === "B") {
            root.pending.bufs.push({ idx: parseInt(f[1]), name: root.disp(f[2] || ""),
                                     modified: f[3] === "1", current: f[4] === "1" })
        } else if (tag === "E") {
            /* ⚠ A SCAN'S FRAME MUST NOT BE DRAWN. It is the whole buffer — the
             * renderer would lay out every line of the file for one frame, and
             * on a large file that is a visible stall for a panel that is only
             * reading. Harvest it, drop it, and put the real view back; the
             * restore is itself a command, so the next frame repaints normally.
             *
             * root.st is deliberately NOT swapped here, which is what makes
             * `root.top` below still the pre-scan top rather than the zero the
             * scan just asked for. */
            if (root.taskScanning) {
                root.taskScanning = false
                root.tasks = root.harvestTasks(root.pending.lines)
                root.pending = ({ st: ({}), lines: [], spans: ({}), bufs: [] })
                root.send("view " + root.top + " " + root.editorRows())
                return
            }
            root.st = root.pending.st
            root.lines = root.pending.lines
            root.spans = root.pending.spans
            root.bufs = root.pending.bufs
            root.pending = ({ st: ({}), lines: [], spans: ({}), bufs: [] })
            root.engineUp = true
            if (!root.startupInsertSent) {
                root.startupInsertSent = true
                if ((root.st.mode || "NORMAL") === "NORMAL") root.sendKeys("i")
            }
            if (root.st.quit === "1") Qt.quit()
        }
    }

    Process {
        id: eng
        running: true
        stdinEnabled: true
        command: {
            const c = [root.bin, "serve"]
            const open = Quickshell.env("SYN_EDIT_OPEN")
            if (open) for (const f of open.split("\n")) if (f !== "") c.push(f)
            return c
        }
        stdout: SplitParser {
            splitMarker: "\n"
            onRead: (line) => root.onRecord(line)
        }
        // The engine exiting is the window's cue to go too — otherwise the
        // window sits there accepting keys that reach nothing.
        onExited: Qt.quit()
    }

    function send(s) { eng.write(s + "\n") }
    function sendKeys(k) { root.send("keys " + encodeURIComponent(k)) }
    function sendEx(c)   { root.send("ex " + encodeURIComponent(c)) }

    // The file browser needs a directory reader, and synfiles is it. Probed
    // rather than assumed: it is an optdepend, and `sh -c 'command -v …'` can
    // never itself be the thing that is missing.
    property bool haveFiles: false

    Process {
        id: filesProbe
        command: ["sh", "-c", "command -v synfiles >/dev/null 2>&1"]
        running: true
        onExited: (code) => root.haveFiles = (code === 0)
    }

    // ── Selecting, in the engine's own terms ────────────────────────────────
    //
    // The mouse and Shift+Arrow both mean "select", and the editor already has
    // a selection: visual mode. So neither of them is implemented here — they
    // are translated into the keys a vim user would have pressed, and the
    // engine decides what a selection IS, what extends it and what cancels it.
    // A second selection model in this file would disagree with `v` the first
    // time somebody used both.
    readonly property bool isVisual: (root.st.mode || "").indexOf("V") === 0

    // Line and display column, 1-based, as the engine's own motions: NG goes
    // to a line, N| to a display column. In visual mode they EXTEND, which is
    // exactly what a drag needs and is why this is keys rather than `:N`.
    function gotoPos(line, dcol) {
        root.sendKeys(String(Math.max(1, line)) + "G" + String(Math.max(1, dcol)) + "|")
    }

    // Start a selection if there is not one already. `v` toggles, so sending
    // it blind would CANCEL the selection half the time.
    function beginVisual() { if (!root.isVisual) root.sendKeys("v") }

    // The keys Shift turns into a selection. Everything here is a MOTION —
    // Shift+<motion> means "extend the selection by that motion", and the
    // motion itself is unchanged, which is why this table holds no behaviour.
    function motionKey(k) {
        switch (k) {
        case Qt.Key_Up:       return "<Up>"
        case Qt.Key_Down:     return "<Down>"
        case Qt.Key_Left:     return "<Left>"
        case Qt.Key_Right:    return "<Right>"
        case Qt.Key_Home:     return "<Home>"
        case Qt.Key_End:      return "<End>"
        case Qt.Key_PageUp:   return "<PageUp>"
        case Qt.Key_PageDown: return "<PageDown>"
        }
        return ""
    }

    // ── Keys ────────────────────────────────────────────────────────────────
    //
    // Translated into the engine's own notation and forwarded. There is
    // deliberately no table of "what this key does" here: that table is
    // vim.c, and a second one would be a second editor.
    function keyName(event) {
        const k = event.key
        const mod = event.modifiers

        switch (k) {
        case Qt.Key_Escape:    return "<Esc>"
        case Qt.Key_Return:
        case Qt.Key_Enter:     return "<CR>"
        case Qt.Key_Tab:       return "<Tab>"
        case Qt.Key_Backspace: return "<BS>"
        case Qt.Key_Delete:    return "<Del>"
        case Qt.Key_Up:        return "<Up>"
        case Qt.Key_Down:      return "<Down>"
        case Qt.Key_Left:      return "<Left>"
        case Qt.Key_Right:     return "<Right>"
        case Qt.Key_Home:      return "<Home>"
        case Qt.Key_End:       return "<End>"
        case Qt.Key_PageUp:    return "<PageUp>"
        case Qt.Key_PageDown:  return "<PageDown>"
        case Qt.Key_Insert:    return "<Insert>"
        }

        // Ctrl-<letter> is the control code, which is what the engine and a
        // terminal both mean by it.
        if ((mod & Qt.ControlModifier) && k >= Qt.Key_A && k <= Qt.Key_Z)
            return "<C-" + String.fromCharCode(97 + k - Qt.Key_A) + ">"

        // A bare modifier press has no text and must not be forwarded, or
        // holding Shift to type a capital sends a stray key first.
        if (!event.text || event.text.length === 0) return ""
        const c = event.text.charCodeAt(0)
        if (c < 0x20) return ""
        // "<" is the one character the notation cannot carry literally.
        return event.text === "<" ? "<lt>" : event.text
    }

    // ── Layout ──────────────────────────────────────────────────────────────

    readonly property int gutterW: root.st.number === "1"
        ? Math.ceil(root.charW * (String(root.st.lines || "1").length + 2))
        : Math.ceil(root.charW)

    readonly property int top: parseInt(root.st.top || "0")
    readonly property int totalLines: Math.max(1, parseInt(root.st.lines || "1"))

    // Put line `t` (0-based) at the top of the view.
    //
    // TWO messages, and the order is load-bearing. `view` is applied first and
    // then serve.c drags the top back to wherever the caret is, on that same
    // command — so `view` on its own is a no-op unless the caret is already
    // there. Move the caret first, then state the top; the clamp then finds
    // the caret inside the window it was asked for and leaves it alone.
    function scrollToLine(t) {
        const want = Math.max(0, Math.min(t, root.totalLines - 1))
        root.sendKeys(String(want + 1) + "G")
        root.send("view " + want + " " + editorRows())
    }
    // The editor's row count, read through a function so this can live up here
    // with the rest of the engine plumbing rather than inside the layout.
    function editorRows() { return editor ? editor.rows : 1 }
    readonly property int curLine: parseInt(root.st.line || "1")
    readonly property int curDcol: parseInt(root.st.dcol || "1")
    readonly property bool inserting: root.st.mode === "INSERT" || root.st.mode === "REPLACE"
    readonly property bool inCmd: (root.st.cmdline || "") !== ""

    // ── Saving something that has never had a name ──────────────────────────
    //
    // A buffer with no path cannot be written, and the engine says so — "no
    // file name" — which is a correct answer and a dead end in a window. The
    // Save button reported the refusal and offered nothing that could clear
    // it: New → type → Save was unsaveable through the GUI, and the only way
    // out was knowing to type `:w name` at a command line the toolbar exists
    // to avoid.
    //
    // `named` comes from the engine rather than being read off the file label,
    // because the label says "[No Name]" and matching that string is matching a
    // message. See the s_row in serve.c.
    readonly property bool named: (root.st.named || "0") === "1"

    // Save, and ask for a name first if there is not one yet.
    //
    // ⚠ The ENGINE owns the typing, exactly as it does for Find (`/`) and
    // Replace (`:%s/`): those buttons open the engine's command line prefilled
    // and let it collect the text. This does the same with `:w <dir>/`, so the
    // name is edited by the editor — one undo stack, one set of keys, and no
    // text-editing item in a window whose whole architecture is that it owns
    // no text.
    function saveNow() {
        if (root.named) root.send("save")
        else            root.saveAs()
    }

    // Pick the FOLDER with the pointer, type only the basename. The browser is
    // already a directory chooser; naming a file is the one part of the job it
    // cannot do, so it hands that part to the command line.
    function saveAs() {
        if (root.haveFiles) browser.showSave()
        else                root.promptWrite(root.saveDir() + "/")
    }

    // ── A button is not a keystroke ─────────────────────────────────────────
    //
    // Every key a BUTTON or a MENU sends goes through here rather than through
    // sendKeys directly, because a button can be pressed in a mode where its
    // keys are not commands at all. In INSERT mode `/` is a slash, `:` is a
    // colon and `u` is a letter: clicking Find typed "/" into the document,
    // and Replace typed ":%s/". Proven against the engine — `keys ihello`
    // then `keys /` leaves the line reading "hello/" and the mode still
    // INSERT.
    //
    // ⚠ INSERT AND REPLACE ONLY. Escaping unconditionally looks tidier and is
    // wrong: the context menu's Copy and Cut are `"+y` and `"+d`, which need
    // the VISUAL selection they act on. <Esc> drops it, and the yank then
    // waits for a motion and takes the wrong text — `vll "ay` yanks "abc",
    // while `vll <Esc> "ay` does not. Both are pinned in the suite.
    function actKeys(k) {
        if (root.inserting) root.sendKeys("<Esc>")
        root.sendKeys(k)
    }

    // ── Task lists ──────────────────────────────────────────────────────────
    //
    // Markdown task syntax, because it is what the files people keep task lists
    // in already use — `- [ ] thing` and `- [x] done` survive being read in any
    // other editor, committed, and rendered by every forge. Nothing here
    // introduces a format of its own, so a list made in this panel is still a
    // plain text file afterwards and a list made anywhere else opens in it.
    //
    // ⚠ THE ENGINE STILL OWNS THE BUFFER. Ticking a box does not edit text in
    // QML — it sends the same keys a person would type. That keeps one undo
    // history (u undoes a tick), keeps the TUI and the GUI in step, and means
    // the panel cannot drift from the file it is describing.
    property bool taskScanning: false
    property var  tasks: []        // { line, dcol, done, text }

    // One task line, or null.
    //
    // Deliberately carries no COLUMN. The obvious implementation of a tick is
    // "replace the character between the brackets", and the obvious way to say
    // where that is — count characters in l.text — is wrong on any line with a
    // tab in front of the box. L records arrive tab-EXPANDED (serve.c's
    // expand_line), so a character index into them is a DISPLAY column, while
    // the engine's `|` motion counts the RAW line. On `\t- [ ] tabbed` those
    // are column 8 and column 5, and aiming at 8 replaces the "t" of "tabbed":
    // the box is untouched, the task text is quietly corrupted, and nothing
    // reports a thing. Measured, and pinned in the suite.
    function parseTask(l) {
        if (!l) return null
        const m = /^\s*(?:[-*+]|\d+\.)\s+\[([ xX])\]\s?(.*)$/.exec(l.text)
        if (!m) return null
        return { line: l.no, done: m[1] !== " ", text: m[2] }
    }

    function harvestTasks(ls) {
        const out = []
        for (let i = 0; i < ls.length; i++) {
            const t = root.parseTask(ls[i])
            if (t) out.push(t)
        }
        return out
    }

    // Ask for the WHOLE buffer, once.
    //
    // `view <top> 0` is the engine's own spelling for "every line": serve.c
    // falls back to the buffer length when view_rows is zero. It is also the
    // only view request that does NOT drag the top back to the caret, because
    // that clamp is itself guarded on view_rows — so a scan cannot move the
    // caret or scroll the window, which a panel that only READS must not do.
    function taskScan() {
        root.taskScanning = true
        root.send("view 0 0")
    }

    // Tick or untick: a substitution on that one line.
    //
    // An ex range rather than a column motion, because a range needs no
    // arithmetic and so cannot get the tab case wrong — see parseTask. The
    // engine substitutes on the raw line and writes back the raw line, so an
    // indent made of tabs stays tabs.
    //
    // Hitting the FIRST match on the line is correct here rather than merely
    // convenient: parseTask only calls a line a task when the box sits between
    // the bullet and the text, so there is nothing before it that `\[ \]`
    // could match instead. Task text that mentions brackets is after it.
    //
    // [xX] because a list edited by hand or by another tool has both, and a
    // tick that only understood one would refuse to clear half a file.
    //
    // <Esc> unconditionally: a box can be clicked while the buffer is in INSERT
    // (the panel is not modal to the engine) and while a VISUAL selection is
    // up, where `:` prefills the range `'<,'>` and would overwrite the line
    // number this is asking for.
    function taskToggle(t) {
        if (!t) return
        root.sendKeys("<Esc>")
        root.sendEx(String(t.line)
                    + (t.done ? "s/\\[[xX]\\]/[ ]/" : "s/\\[ \\]/[x]/"))
        root.taskScan()
    }

    // The task on the line the caret is on, from the lines already on screen —
    // the caret is always inside the view, so no scan is needed for this one.
    function taskAtCaret() {
        for (let i = 0; i < root.lines.length; i++)
            if (root.lines[i].no === root.curLine)
                return root.parseTask(root.lines[i])
        return null
    }

    // Open the engine's command line on `:w <prefix>`.
    //
    // The <Esc> here IS unconditional, unlike actKeys above, and for a second
    // reason on top of the insert-mode one: `:` pressed in visual mode prefills
    // the range `:'<,'>`, so the command line would read `:'<,'>w /path/` — a
    // whole-file write wearing a selection's range. This engine's `:w` happens
    // to ignore the range, which makes it a display problem today and a data
    // one the moment `:w` learns to honour it.
    function promptWrite(prefix) {
        root.sendKeys("<Esc>")
        root.sendKeys(":w " + prefix)
    }

    // Where a Save As should start: beside the file being edited, or home for
    // a buffer that has never been anywhere.
    function saveDir() {
        const f = root.named ? (root.st.file || "") : ""
        const slash = f.lastIndexOf("/")
        if (slash > 0)  return f.substring(0, slash)
        if (slash === 0) return ""      // a file at the root: ":w /name"
        return Quickshell.env("HOME") || ""
    }

    Rectangle {
        id: shell
        anchors.fill: parent
        color: root.cBg

        // A DropArea has no visual footprint and takes no mouse press, only
        // drag events, so it can sit over the whole window without stealing
        // clicks from anything under it. Local files only — the same rule
        // synfiles' own drop handling uses: a dragged web link is refused
        // rather than opened as an empty file named after a URL.
        DropArea {
            anchors.fill: parent
            onEntered: (drag) => { if (!drag.hasUrls) drag.accepted = false }
            onDropped: (drop) => {
                if (!drop.hasUrls) return
                for (const u of drop.urls) {
                    const s = "" + u
                    if (s.indexOf("file://") !== 0) continue
                    root.send("open " + encodeURIComponent(root.disp(s.substring(7))))
                }
                drop.accept(Qt.CopyAction)
                editor.forceActiveFocus()
            }
        }

        // ── toolbar ─────────────────────────────────────────────────────────
        Rectangle {
            id: toolbar
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: Math.round(root.ui(38))
            color: root.cPanel

            // ⚠ The two groups sit in CLIPPED boxes of an explicit width, and
            // are not simply anchored to opposite edges and left to meet in
            // the middle. minimumSize is not the floor it looks like: synui
            // enforces a client minimum only during an INTERACTIVE resize, so
            // a tiled or otherwise forced configure puts this window well
            // under 640 — and two anchored Rows with no floor do not degrade,
            // they paint through each other ("UndoDocuments", "F Abolut R").
            //
            // The widths are arithmetic with a max(0, …) rather than a left
            // AND right anchor pair, because a negative width silently
            // defeats clip — the overflow this exists to contain would escape
            // at exactly the sizes that produce it.
            Item {
                id: rightTools
                anchors { right: parent.right; rightMargin: 8
                          top: parent.top; bottom: parent.bottom }
                width: Math.min(rightRow.implicitWidth, Math.max(0, parent.width - 16))
                clip: true

                Row {
                    id: rightRow
                    // Anchored LEFT inside a right-anchored box, so the button
                    // that falls off the end when there is no room is About
                    // and not the Documents toggle.
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: 4
                    ToolButton { label: "Documents"; tip: "show or hide the list"
                                 active: root.st.tree === "1"
                                 onTriggered: root.send("set tree!") }
                    ToolButton { label: "About"; tip: "version and licence"
                                 onTriggered: aboutPane.visible = !aboutPane.visible }
                }
            }

            Item {
                id: leftTools
                anchors { left: parent.left; leftMargin: 8
                          top: parent.top; bottom: parent.bottom }
                width: Math.max(0, rightTools.x - leftTools.x - 6)
                clip: true

                Row {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: 4

                    ToolButton { label: "New";  tip: "a new empty buffer";  onTriggered: root.send("new") }
                    ToolButton { label: "Open"; tip: root.haveFiles ? "browse for a file"
                                                                   : "type a path (:e)"
                                 onTriggered: root.haveFiles ? browser.show()
                                                             : root.actKeys(":e ") }
                    ToolButton { label: "Save"; tip: root.named ? "write this buffer"
                                                               : "name it, then write it"
                                 onTriggered: root.saveNow() }
                    ToolButton { label: "Save As"; tip: "write it somewhere else"
                                 onTriggered: root.saveAs() }
                    Rectangle { width: 1; height: Math.round(root.ui(20)); color: root.cDim; opacity: 0.4
                                anchors.verticalCenter: parent.verticalCenter }
                    ToolButton { label: "Undo"; tip: "u";                   onTriggered: root.actKeys("u") }
                    ToolButton { label: "Redo"; tip: "Ctrl-R";              onTriggered: root.actKeys("<C-r>") }
                    Rectangle { width: 1; height: Math.round(root.ui(20)); color: root.cDim; opacity: 0.4
                                anchors.verticalCenter: parent.verticalCenter }
                    ToolButton { label: "Find"; tip: "/";                   onTriggered: root.actKeys("/") }
                    ToolButton { label: "Replace"; tip: ":%s/…/…/g";        onTriggered: root.actKeys(":%s/") }
                }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                        color: root.cDim; opacity: 0.35 }
        }

        // ── document tabs ───────────────────────────────────────────────────
        Rectangle {
            id: tabstrip
            anchors { top: toolbar.bottom; left: parent.left; right: parent.right }
            height: root.st.tabbar === "1" && root.bufs.length > 1
                    ? Math.round(root.ui(30)) : 0
            visible: height > 0
            clip: true
            color: Qt.rgba(root.cPanel.r, root.cPanel.g, root.cPanel.b, 0.6)

            Row {
                anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                spacing: 2
                Repeater {
                    model: root.bufs
                    Rectangle {
                        required property var modelData
                        height: Math.round(root.ui(24))
                        width: tabLabel.implicitWidth + 22
                        radius: 4
                        color: modelData.current ? root.wash(0.22) : "transparent"
                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: (modelData.name.replace(/^.*\//, "") || "[No Name]")
                                  + (modelData.modified ? " •" : "")
                            font.family: root.uiFont
                            font.pixelSize: root.ui(12)
                            color: modelData.current ? root.cText : root.cDim
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.send("buf " + modelData.idx)
                        }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                        color: root.cDim; opacity: 0.25 }
        }

        // ── sidebar: Documents ──────────────────────────────────────────────
        Rectangle {
            id: sidebar
            anchors { top: tabstrip.bottom; bottom: statusbar.top; left: parent.left }
            // Capped as a FRACTION as well as an absolute. A flat 200px is
            // most of a narrow window: at 350px wide it left ~120px of
            // editor, which is less than the gutter plus any usable number of
            // columns, so the pane the sidebar exists to list became
            // unreadable to make room for the list.
            width: root.st.tree === "1"
                   ? Math.min(Math.round(root.ui(200)), Math.round(parent.width * 0.4))
                   : 0
            visible: width > 0
            clip: true
            color: Qt.rgba(root.cPanel.r, root.cPanel.g, root.cPanel.b, 0.45)

            Column {
                anchors { fill: parent; margins: 8 }
                spacing: 2

                Text {
                    text: "DOCUMENTS"
                    font.family: root.uiFont
                    font.pixelSize: root.ui(10)
                    font.letterSpacing: 1
                    color: root.cDim
                    bottomPadding: 6
                }

                Repeater {
                    model: root.bufs
                    Rectangle {
                        required property var modelData
                        // max(0, …): the sidebar is capped against the window
                        // now, and a row narrower than its own margins would
                        // go negative — which does not clip, it paints out.
                        width: Math.max(0, sidebar.width - 16)
                        height: Math.round(root.ui(26))
                        radius: 4
                        color: modelData.current ? root.wash(0.18)
                             : sideMa.containsMouse ? root.wash(0.08) : "transparent"

                        Text {
                            anchors { left: parent.left; leftMargin: 8
                                      right: parent.right; rightMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            text: (modelData.name.replace(/^.*\//, "") || "[No Name]")
                                  + (modelData.modified ? "  •" : "")
                            elide: Text.ElideMiddle
                            font.family: root.uiFont
                            font.pixelSize: root.ui(12)
                            color: modelData.current ? root.cText : root.cDim
                        }
                        MouseArea {
                            id: sideMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.send("buf " + modelData.idx)
                        }
                    }
                }
            }
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1
                        color: root.cDim; opacity: 0.25 }
        }

        // ── the editor ──────────────────────────────────────────────────────
        Item {
            id: editor
            anchors { top: tabstrip.bottom; bottom: statusbar.top
                      left: sidebar.right; right: parent.right }
            // Room for the scrollbar, so the last column of a long line never
            // runs under the handle. Given back when there is nothing to
            // scroll, because a permanent empty strip is furniture.
            anchors.rightMargin: vscroll.visible ? vscroll.width + 4 : 0
            clip: true

            focus: true
            activeFocusOnTab: true

            // How many rows fit. Told to the engine, which then owns scrolling
            // — so the window and the terminal scroll identically and the
            // cursor is kept on screen in ONE place.
            readonly property int rows: Math.max(1, Math.floor(height / root.lineH))
            onRowsChanged: root.send("view " + root.top + " " + rows)
            Component.onCompleted: root.send("view 0 " + rows)

            Keys.onPressed: (event) => {
                // Shift+<motion> selects. This is the one meaning the window
                // adds that the engine's key table does not have — and it adds
                // it by pressing `v` first, not by keeping a selection of its
                // own. In INSERT mode it deliberately does NOT: `v` is a
                // letter there, and an editor that jumped out of insert mode
                // because Shift was held would be worse than one that just
                // moves the caret.
                // Ctrl+S saves. The engine has no such binding — it is a
                // window convention, and the same kind of translation as
                // Shift+Arrow below: it runs the editor's own write, it does
                // not implement one.
                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_S
                    && !root.inCmd) {
                    root.saveNow()
                    event.accepted = true
                    return
                }

                const motion = root.motionKey(event.key)
                if (motion !== "" && (event.modifiers & Qt.ShiftModifier)
                    && !root.inserting && !root.inCmd) {
                    root.beginVisual()
                    root.sendKeys(motion)
                    event.accepted = true
                    return
                }
                const k = root.keyName(event)
                if (k !== "") {
                    root.sendKeys(k)
                    event.accepted = true
                }
            }

            MouseArea {
                id: textMa
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                // Held, or the press is delivered and the drag that follows is
                // not: this MouseArea is inside no Flickable, but the window
                // itself will happily take a drag that started here.
                preventStealing: true

                // Where a point on screen is, in the engine's coordinates.
                // Both are 1-based, and the column is a DISPLAY column — the
                // only kind this window ever handles, because tabs are already
                // expanded by the time a line arrives.
                function lineAt(y)  { return root.top + Math.floor(y / root.lineH) + 1 }
                function colAt(x)   { return Math.max(1, Math.round((x - root.gutterW) / root.charW) + 1) }

                property bool dragging: false

                onPressed: (m) => {
                    editor.forceActiveFocus()
                    if (m.button === Qt.RightButton) {
                        // Right-click INSIDE a selection keeps it — "copy
                        // these three lines" is the whole reason they are
                        // selected. Outside one it moves the caret first, the
                        // way a left click does — but unlike a left click this
                        // one always opens a menu rather than continuing to
                        // type, so it can leave INSERT first. Skipping that
                        // made gotoPos's raw `NG N|` land in the document
                        // ahead of the menu opening: right-click ▸ Paste read
                        // as "pasting mouse code" because the click itself had
                        // just typed it.
                        if (!root.isVisual) {
                            if (root.inserting) root.sendKeys("<Esc>")
                            root.gotoPos(textMa.lineAt(m.y), textMa.colAt(m.x))
                        }
                        ctxMenu.openAt(m.x, m.y)
                        return
                    }
                    // A fresh click drops any selection: press, drag, release
                    // is one gesture and it starts from nothing. Only when
                    // there IS one — <Esc> in normal mode is harmless, but in
                    // INSERT it would throw the user out of insert mode for
                    // clicking somewhere, which no editor does.
                    if (root.isVisual) root.sendKeys("<Esc>")
                    root.gotoPos(textMa.lineAt(m.y), textMa.colAt(m.x))
                    textMa.dragging = true
                }

                onPositionChanged: (m) => {
                    if (!textMa.dragging || !pressed) return
                    // The selection is the ENGINE's: enter visual mode once,
                    // then keep moving the caret. The anchor stays where the
                    // press was because that is what visual mode does with it.
                    root.beginVisual()
                    root.gotoPos(textMa.lineAt(m.y), textMa.colAt(m.x))
                    // Dragging past the top or bottom edge scrolls, since the
                    // engine keeps the caret on screen: moving the caret to a
                    // line outside the view IS the scroll.
                    edgeScroll.dir = m.y < 0 ? -1 : (m.y > textMa.height ? 1 : 0)
                    edgeScroll.running = edgeScroll.dir !== 0
                }

                onReleased: { textMa.dragging = false; edgeScroll.stop() }
                onCanceled: { textMa.dragging = false; edgeScroll.stop() }

                // A double click takes the word under it, which is `viw` —
                // the engine's own idea of a word, not a regex written here.
                onDoubleClicked: (m) => {
                    if (m.button !== Qt.LeftButton) return
                    root.gotoPos(textMa.lineAt(m.y), textMa.colAt(m.x))
                    root.sendKeys("viw")
                }

                onWheel: (w) => {
                    // Three lines a notch, the usual step, expressed as KEYS
                    // so the engine's own view bookkeeping stays the only copy
                    // of where we are.
                    root.sendKeys(w.angleDelta.y > 0 ? "3<Up>" : "3<Down>")
                }

                Timer {
                    id: edgeScroll
                    property int dir: 0
                    interval: 60
                    repeat: true
                    onTriggered: root.sendKeys(edgeScroll.dir < 0 ? "<Up>" : "<Down>")
                }
            }

            Column {
                id: rowsCol
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: root.lines
                    Item {
                        id: rowItem
                        required property var modelData
                        width: editor.width
                        height: root.lineH

                        readonly property bool isCur: modelData.no === root.curLine

                        // the current line
                        Rectangle {
                            anchors.fill: parent
                            color: rowItem.isCur ? root.cLine : "transparent"
                        }

                        // the selection
                        Rectangle {
                            visible: root.st.sel_y0 !== undefined && root.st.sel_y0 !== ""
                                     && rowItem.modelData.no >= parseInt(root.st.sel_y0 || "0")
                                     && rowItem.modelData.no <= parseInt(root.st.sel_y1 || "0")
                            color: root.cSel
                            y: 0
                            height: parent.height
                            x: {
                                if (root.st.sel_line === "1") return root.gutterW
                                const y0 = parseInt(root.st.sel_y0 || "0")
                                return rowItem.modelData.no === y0
                                    ? root.gutterW + parseInt(root.st.sel_x0 || "0") * root.charW
                                    : root.gutterW
                            }
                            width: {
                                if (root.st.sel_line === "1")
                                    return Math.max(root.charW, rowItem.modelData.text.length * root.charW)
                                const y0 = parseInt(root.st.sel_y0 || "0")
                                const y1 = parseInt(root.st.sel_y1 || "0")
                                const x0 = rowItem.modelData.no === y0 ? parseInt(root.st.sel_x0 || "0") : 0
                                const x1 = rowItem.modelData.no === y1
                                    ? parseInt(root.st.sel_x1 || "0") + 1
                                    : rowItem.modelData.text.length
                                return Math.max(0, (x1 - x0)) * root.charW
                            }
                        }

                        // the line number
                        Text {
                            visible: root.st.number === "1"
                            anchors { left: parent.left; leftMargin: 0
                                      verticalCenter: parent.verticalCenter }
                            width: root.gutterW - Math.round(root.charW)
                            horizontalAlignment: Text.AlignRight
                            text: String(rowItem.modelData.no)
                            font.family: root.monoFont
                            font.pixelSize: root.monoSize
                            color: rowItem.isCur ? root.cText : root.cDim
                            opacity: rowItem.isCur ? 0.9 : 0.5
                        }

                        // the text
                        //
                        // ⚠ Each segment is given an EXPLICIT width of
                        // charW × characters rather than being laid out by its
                        // own advance. Left to itself a Row accumulates
                        // sub-pixel rounding across a long line, and the caret
                        // — which is positioned by arithmetic on charW — drifts
                        // away from the character it is on.
                        Row {
                            anchors { left: parent.left; leftMargin: root.gutterW
                                      verticalCenter: parent.verticalCenter }
                            spacing: 0
                            Repeater {
                                model: root.segsFor(rowItem.modelData.no, rowItem.modelData.text)
                                Text {
                                    required property var modelData
                                    width: modelData.t.length * root.charW
                                    text: modelData.t
                                    font.family: root.monoFont
                                    font.pixelSize: root.monoSize
                                    color: root.tokColour(modelData.k)
                                    textFormat: Text.PlainText
                                    maximumLineCount: 1
                                }
                            }
                        }
                    }
                }
            }

            // the caret
            Rectangle {
                visible: !root.inCmd && root.lines.length > 0
                x: root.gutterW + (root.curDcol - 1) * root.charW
                y: (root.curLine - root.top - 1) * root.lineH
                width: root.inserting ? Math.max(2, Math.round(root.charW / 8)) : root.charW
                height: root.lineH
                color: root.cAccent
                opacity: editor.activeFocus ? (root.inserting ? 0.9 : 0.45) : 0.25
            }
        }

        // ── the scrollbar ───────────────────────────────────────────────────
        //
        // Hand-rolled like every other control here, and for one more reason
        // than usual: there is no Flickable to attach a ScrollBar to. The
        // window holds one screenful of lines and nothing else, so "where am I
        // in this file" is a fact about the ENGINE (`top` of `lines`) and the
        // handle is drawn from those two numbers rather than from a content
        // height that does not exist.
        //
        // ⚠ Scrolling MOVES THE CARET, and that is not a shortcut. The engine
        // keeps the caret on screen — `view` alone is undone by that clamp on
        // the very same command, which is why a scrollbar that only sent
        // `view` would snap straight back. The wheel has always worked this
        // way here (it sends 3<Up>/3<Down>); the handle now matches it.
        Item {
            id: vscroll
            anchors { top: editor.top; bottom: editor.bottom; right: parent.right
                      rightMargin: 2 }
            width: Math.round(root.ui(10))
            visible: root.totalLines > editor.rows

            readonly property real frac:
                Math.min(1, editor.rows / Math.max(1, root.totalLines))
            readonly property real handleH: Math.max(Math.round(root.ui(28)), height * frac)
            readonly property real span: Math.max(0, height - handleH)
            readonly property int maxTop: Math.max(0, root.totalLines - editor.rows)

            // Where the top line should be if the handle's top edge is at y.
            function topFor(y) {
                if (vscroll.span <= 0) return 0
                return Math.round(Math.max(0, Math.min(1, y / vscroll.span)) * vscroll.maxTop)
            }

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: root.wash(0.07)
            }

            // The track. A click pages TOWARD the pointer rather than jumping
            // to it — under the handle, so a press that lands on the handle
            // starts a drag instead.
            MouseArea {
                anchors.fill: parent
                onPressed: (m) => {
                    root.scrollToLine(m.y < handle.y
                                      ? root.top - editor.rows
                                      : root.top + editor.rows)
                }
            }

            Rectangle {
                id: handle
                x: 0
                width: parent.width
                height: vscroll.handleH
                radius: width / 2
                y: vscroll.maxTop <= 0 ? 0
                   : vscroll.span * Math.max(0, Math.min(1, root.top / vscroll.maxTop))
                color: handleMa.pressed ? root.cAccent
                     : handleMa.containsMouse ? root.wash(0.45) : root.wash(0.28)

                MouseArea {
                    id: handleMa
                    anchors.fill: parent
                    hoverEnabled: true
                    // The grab offset, so the handle does not jump its own
                    // height the moment it is picked up anywhere but the top.
                    property real grabDy: 0
                    onPressed: (m) => { handleMa.grabDy = m.y }
                    onPositionChanged: (m) => {
                        if (!handleMa.pressed) return
                        const y = handle.y + m.y - handleMa.grabDy
                        root.scrollToLine(vscroll.topFor(y))
                    }
                }
            }
        }

        // ── status bar ──────────────────────────────────────────────────────
        Rectangle {
            id: statusbar
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: Math.round(root.ui(26)) + msgline.height
            color: root.cPanel

            Row {
                id: statusrow
                anchors { top: parent.top; left: parent.left; leftMargin: 0 }
                height: Math.round(root.ui(26))
                spacing: 0

                Rectangle {
                    width: modeText.implicitWidth + 20
                    height: parent.height
                    color: root.inserting ? root.cGood
                         : (root.st.mode || "").indexOf("V") === 0 ? root.cWarn
                         : root.cAccent
                    Text {
                        id: modeText
                        anchors.centerIn: parent
                        text: root.st.mode || "NORMAL"
                        font.family: root.uiFont
                        font.pixelSize: root.ui(11)
                        font.bold: true
                        color: root.lum(parent.color) > 0.4 ? "#12141a" : "#ffffff"
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 12
                    text: (root.st.file || "[No Name]")
                          + (root.st.modified === "1" ? "  [+]" : "")
                          + (root.st.readonly === "1" ? "  [RO]" : "")
                    font.family: root.uiFont
                    font.pixelSize: root.ui(11)
                    color: root.cText
                }
            }

            Row {
                anchors { top: parent.top; right: parent.right; rightMargin: 12 }
                height: Math.round(root.ui(26))
                spacing: 14

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.st.binary === "1" ? "binary  " : "")
                          + (root.st.eol === "crlf" ? "CRLF  " : "")
                          + (root.st.lang || "text")
                    font.family: root.uiFont
                    font.pixelSize: root.ui(11)
                    color: root.cDim
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.st.line || "1") + ":" + (root.st.col || "1")
                          + "  of " + (root.st.lines || "1")
                    font.family: root.uiFont
                    font.pixelSize: root.ui(11)
                    color: root.cDim
                }
            }

            // The command line and messages share a row, because the engine
            // never has both: while a command is being typed there is no
            // message, and a message is what a finished command produced.
            Item {
                id: msgline
                anchors { top: statusrow.bottom; left: parent.left; right: parent.right }
                height: (root.inCmd || (root.st.msg || "") !== "")
                        ? Math.round(root.ui(22)) : 0
                visible: height > 0
                clip: true

                Text {
                    anchors { left: parent.left; leftMargin: 12
                              verticalCenter: parent.verticalCenter }
                    text: root.inCmd ? root.st.cmdline : (root.st.msg || "")
                    font.family: root.inCmd ? root.monoFont : root.uiFont
                    font.pixelSize: root.ui(12)
                    color: root.inCmd ? root.cText
                         : (root.st.msgerr === "1" ? root.cBad : root.cDim)
                }

                // A caret on the command line, so a half-typed :command does
                // not look like a frozen window.
                Rectangle {
                    visible: root.inCmd
                    x: 12 + (root.st.cmdline || "").length * root.charW
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(2, Math.round(root.charW / 8))
                    height: root.lineH * 0.8
                    color: root.cAccent
                }
            }
        }

        // ── the context menu ────────────────────────────────────────────────
        //
        // Every entry is a KEY SEQUENCE, and that is the whole design of it:
        // Copy is `"+y`, not a copy implemented here. The + register is the
        // desktop clipboard already (vim.c shells out to wl-copy/wl-paste), so
        // Copy in this menu means what Copy means everywhere else on the
        // desktop — while still being the editor's own yank, with its own idea
        // of what a line is.
        //
        // With no selection the line is the unit, which is what vim does and
        // what a menu with nothing selected has to pick anyway: `"+yy` beats a
        // greyed-out Copy.
        MouseArea {
            anchors.fill: parent
            visible: ctxMenu.open
            acceptedButtons: Qt.AllButtons
            onPressed: ctxMenu.open = false
        }

        Rectangle {
            id: ctxMenu
            property bool open: false
            property real rawX: 0
            property real rawY: 0

            function openAt(x, y) {
                const p = editor.mapToItem(shell, x, y)
                ctxMenu.rawX = p.x
                ctxMenu.rawY = p.y
                ctxMenu.open = true
            }

            visible: ctxMenu.open
            width: Math.round(root.ui(210))
            height: ctxCol.implicitHeight + 8
            x: Math.max(4, Math.min(ctxMenu.rawX, parent.width - width - 4))
            y: Math.max(4, Math.min(ctxMenu.rawY, parent.height - height - 4))
            radius: 4
            color: root.cPanel
            border { width: 1; color: root.wash(0.35) }
            z: 200

            Column {
                id: ctxCol
                anchors { fill: parent; margins: 4 }

                Repeater {
                    model: {
                        const sel = root.isVisual
                        return [
                            { label: "Cut",   keys: sel ? "\"+d" : "\"+dd", hint: sel ? "" : "line" },
                            { label: "Copy",  keys: sel ? "\"+y" : "\"+yy", hint: sel ? "" : "line" },
                            { label: "Paste", keys: "\"+p", hint: "" },
                            { label: "-", keys: "", hint: "" },
                            { label: "Select All", keys: "ggVG", hint: "" },
                            { label: "-", keys: "", hint: "" },
                            { label: "Undo", keys: "u", hint: "u" },
                            { label: "Redo", keys: "<C-r>", hint: "Ctrl+R" },
                            { label: "-", keys: "", hint: "" },
                            // Task list. `o- [ ] ` deliberately LEAVES the
                            // engine in INSERT with the caret after the bracket:
                            // the next thing anybody wants after "new task" is
                            // to type the task, and a menu item that dropped
                            // them in NORMAL would eat that first word.
                            { label: "Task list…", keys: "", act: "tasks", hint: "" },
                            { label: "New task", keys: "o- [ ] ", hint: "" },
                            { label: root.taskAtCaret()
                                     ? (root.taskAtCaret().done ? "Untick this task"
                                                                : "Tick this task")
                                     : "Tick this task",
                              keys: "", act: "tasktoggle", hint: "",
                              off: root.taskAtCaret() === null },
                            { label: "-", keys: "", hint: "" },
                            { label: "Find…", keys: "/", hint: "/" },
                            { label: "Replace…", keys: ":%s/", hint: "" },
                            { label: "-", keys: "", hint: "" },
                            { label: "Open…", keys: "", act: "open", hint: "" },
                            { label: "Save", keys: "", act: "save", hint: "Ctrl+S" },
                            { label: "Save As…", keys: "", act: "saveas", hint: "" }
                        ]
                    }
                    delegate: Item {
                        id: ctxItem
                        required property var modelData
                        width: ctxCol.width
                        height: ctxItem.modelData.label === "-" ? 5 : Math.round(root.ui(26))

                        Rectangle {
                            anchors { left: parent.left; right: parent.right
                                      verticalCenter: parent.verticalCenter }
                            height: 1
                            color: root.wash(0.25)
                            visible: ctxItem.modelData.label === "-"
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 3
                            visible: ctxItem.modelData.label !== "-"
                            color: ctxMa.containsMouse ? root.wash(0.18) : "transparent"

                            Text {
                                anchors { left: parent.left; leftMargin: 10
                                          right: ctxHint.left; rightMargin: 6
                                          verticalCenter: parent.verticalCenter }
                                elide: Text.ElideRight
                                text: ctxItem.modelData.label
                                font.family: root.uiFont
                                font.pixelSize: root.ui(12)
                                color: ctxItem.modelData.off ? root.cDim : root.cText
                            }
                            Text {
                                id: ctxHint
                                anchors { right: parent.right; rightMargin: 10
                                          verticalCenter: parent.verticalCenter }
                                text: ctxItem.modelData.hint || ""
                                visible: text !== ""
                                font.family: root.uiFont
                                font.pixelSize: root.ui(10)
                                color: root.cDim
                            }
                            MouseArea {
                                id: ctxMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    ctxMenu.open = false
                                    editor.forceActiveFocus()
                                    const m = ctxItem.modelData
                                    if (m.off)                   return
                                    if (m.act === "open")        browser.show()
                                    else if (m.act === "save")   root.saveNow()
                                    else if (m.act === "saveas") root.saveAs()
                                    else if (m.act === "tasks")  tasksPanel.show()
                                    else if (m.act === "tasktoggle")
                                        root.taskToggle(root.taskAtCaret())
                                    else if (m.keys !== "")      root.actKeys(m.keys)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Open: a file browser ────────────────────────────────────────────
        //
        // The Open button used to type `:e ` for you and leave you at a command
        // line with a path to remember. That is the right thing in the TUI and
        // the wrong thing in a window with a pointer in it.
        //
        // The listing is `synfiles --rec list`, NOT a readdir of this window's
        // own: synfiles is the file manager, its records are already
        // percent-encoded (a filename is arbitrary bytes, and this window
        // decodes exactly one way, in one place), and it already sorts
        // directories first. A second directory reader here would be a second
        // set of answers about what is in a folder.
        //
        // It is an optdepend, so it is PROBED for — without it the button goes
        // back to `:e `, which is what it always did. An Open that silently
        // did nothing would be worse than the command line it replaced.
        Rectangle {
            id: browser
            visible: false
            anchors.centerIn: parent
            width: Math.max(0, Math.min(parent.width - 60, Math.round(root.ui(560))))
            height: Math.max(0, Math.min(parent.height - 60, Math.round(root.ui(420))))
            radius: 8
            clip: true
            color: root.cPanel
            border { width: 1; color: root.wash(0.4) }
            z: 300

            property string dir: ""
            property var rows: []
            property int sel: 0
            // "open" or "save". The listing is identical either way — what
            // changes is what happens to the thing you pick.
            property string mode: "open"

            function show() {
                // Beside the file being edited, which is where the next one
                // usually is. A [No Name] buffer has no directory, so home.
                const f = root.st.file || ""
                const slash = f.lastIndexOf("/")
                browser.mode = "open"
                browser.dir = slash > 0 ? f.substring(0, slash)
                            : (f !== "" && slash === 0) ? "/"
                            : (Quickshell.env("HOME") || "/")
                browser.visible = true
                browser.load()
                browser.forceActiveFocus()
            }

            function showSave() {
                browser.mode = "save"
                browser.dir = root.saveDir() || "/"
                browser.visible = true
                browser.load()
                browser.forceActiveFocus()
            }

            // Leave the browser and hand the rest to the engine's command
            // line, prefilled. `partial` is a directory to type a name into;
            // a whole path is an existing file being written over — and
            // seeding the full `:w /path/to/it` is what makes that an ANSWER
            // rather than a click: it is on screen, and it takes Return.
            function seedWrite(partial) {
                browser.visible = false
                editor.forceActiveFocus()
                root.promptWrite(partial)
            }

            function load() {
                browser.rows = []
                browser.sel = 0
                // -a: this is a text editor. The files people open by hand
                // that live nowhere else are dotfiles.
                lsProc.command = ["synfiles", "--rec", "list", "-a", browser.dir]
                lsProc.running = true
            }

            function enter(row) {
                if (!row) return
                const path = (browser.dir === "/" ? "" : browser.dir) + "/" + row.name
                if (row.type === "dir") {
                    browser.dir = path
                    browser.load()
                    return
                }
                if (browser.mode === "save") {
                    // Writing OVER something. Not done on the click: the path
                    // goes to the command line where it can be read and has to
                    // be confirmed with Return. `:w` overwrites without asking
                    // — vim's E13 is not implemented here — so the confirmation
                    // has to come from somewhere, and a dialogue this window
                    // does not otherwise have is a worse answer than the
                    // command line it already draws.
                    browser.seedWrite(path)
                    return
                }
                // The engine opens it — the same `open` a command line would
                // have reached, so a file opened here and a file opened with
                // :e arrive by exactly one route.
                root.send("open " + encodeURIComponent(path))
                browser.visible = false
                editor.forceActiveFocus()
            }

            function up() {
                if (browser.dir === "/" || browser.dir === "") return
                const slash = browser.dir.lastIndexOf("/")
                browser.dir = slash > 0 ? browser.dir.substring(0, slash) : "/"
                browser.load()
            }

            Process {
                id: lsProc
                stdout: StdioCollector {
                    onStreamFinished: {
                        const out = []
                        const lines = this.text.split("\n")
                        // Line 0 is the header naming the columns; the records
                        // that follow are positional against it.
                        for (let i = 1; i < lines.length; i++) {
                            if (lines[i] === "") continue
                            const f = lines[i].split("\t")
                            out.push({ name: root.disp(f[0]), type: f[1] || "file" })
                        }
                        browser.rows = out
                    }
                }
                stderr: StdioCollector {
                    onStreamFinished: {
                        if (this.text) browser.rows = []
                    }
                }
            }

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    browser.visible = false
                    editor.forceActiveFocus()
                } else if (event.key === Qt.Key_Down) {
                    browser.sel = Math.min(browser.rows.length - 1, browser.sel + 1)
                } else if (event.key === Qt.Key_Up) {
                    browser.sel = Math.max(0, browser.sel - 1)
                } else if (event.key === Qt.Key_Backspace) {
                    browser.up()
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    browser.enter(browser.rows[browser.sel])
                } else {
                    return
                }
                event.accepted = true
            }

            Column {
                anchors { fill: parent; margins: 12 }
                spacing: 8

                Text {
                    width: parent.width
                    text: (browser.mode === "save" ? "Save in:  " : "") + browser.dir
                    elide: Text.ElideLeft
                    font.family: root.monoFont
                    font.pixelSize: root.ui(12)
                    color: root.cText
                }

                Rectangle {
                    width: parent.width
                    height: browser.height - Math.round(root.ui(96))
                    color: "transparent"

                    ListView {
                        id: browserList
                        anchors.fill: parent
                        clip: true
                        model: browser.rows
                        currentIndex: browser.sel
                        // Keeping the keyboard selection on screen is the
                        // whole reason this is a ListView and not a Column.
                        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

                        header: Rectangle {
                            width: browserList.width
                            height: Math.round(root.ui(24))
                            color: upMa.containsMouse ? root.wash(0.14) : "transparent"
                            radius: 3
                            Text {
                                anchors { left: parent.left; leftMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                text: "../"
                                font.family: root.monoFont
                                font.pixelSize: root.ui(12)
                                color: root.cAccent
                            }
                            MouseArea {
                                id: upMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: browser.up()
                            }
                        }

                        delegate: Rectangle {
                            id: browserRow
                            required property var modelData
                            required property int index
                            width: browserList.width
                            height: Math.round(root.ui(24))
                            radius: 3
                            color: browserRow.index === browser.sel ? root.wash(0.22)
                                 : rowMa.containsMouse ? root.wash(0.10) : "transparent"

                            Text {
                                anchors { left: parent.left; leftMargin: 8
                                          right: parent.right; rightMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                text: browserRow.modelData.name
                                      + (browserRow.modelData.type === "dir" ? "/" : "")
                                elide: Text.ElideMiddle
                                font.family: root.monoFont
                                font.pixelSize: root.ui(12)
                                color: browserRow.modelData.type === "dir" ? root.cAccent : root.cText
                            }
                            MouseArea {
                                id: rowMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: browser.sel = browserRow.index
                                onDoubleClicked: browser.enter(browserRow.modelData)
                            }
                        }
                    }
                }

                Row {
                    spacing: 8
                    ToolButton { label: browser.mode === "save" ? "Save here" : "Open"
                                 tip: browser.mode === "save"
                                      ? "write into this folder — then type a name"
                                      : "open the highlighted file"
                                 onTriggered: browser.mode === "save"
                                              ? browser.seedWrite(
                                                    (browser.dir === "/" ? "" : browser.dir) + "/")
                                              : browser.enter(browser.rows[browser.sel]) }
                    // Only in save mode, and only with a FILE highlighted —
                    // there is nothing to write over otherwise.
                    ToolButton { label: "Overwrite"
                                 visible: browser.mode === "save"
                                          && (browser.rows[browser.sel] || {}).type === "file"
                                 tip: "write over the highlighted file"
                                 onTriggered: browser.enter(browser.rows[browser.sel]) }
                    ToolButton { label: "Cancel"; tip: "Esc"
                                 onTriggered: { browser.visible = false; editor.forceActiveFocus() } }
                }
            }
        }

        // ── Task list ───────────────────────────────────────────────────────
        //
        // Every `- [ ]` / `- [x]` line in the buffer, with a box that ticks it.
        //
        // It is a VIEW, not a second copy of the list: the rows come from a scan
        // of the buffer and a tick is sent back as keys, so there is nothing
        // here that can disagree with the file. Re-scanned after every tick and
        // whenever it is opened, which is also what makes it correct after an
        // edit made in the editor behind it, or an undo.
        Rectangle {
            id: tasksPanel
            visible: false
            anchors.centerIn: parent
            width: Math.max(0, Math.min(parent.width - 60, Math.round(root.ui(560))))
            height: Math.max(0, Math.min(parent.height - 60, Math.round(root.ui(420))))
            radius: 8
            clip: true
            color: root.cPanel
            border { width: 1; color: root.wash(0.4) }
            z: 300

            readonly property int doneCount: {
                let n = 0
                for (let i = 0; i < root.tasks.length; i++) if (root.tasks[i].done) n++
                return n
            }

            function show() {
                root.taskScan()
                tasksPanel.visible = true
                tasksPanel.forceActiveFocus()
            }
            function hide() {
                tasksPanel.visible = false
                editor.forceActiveFocus()
            }

            Keys.onEscapePressed: tasksPanel.hide()

            Column {
                anchors { fill: parent; margins: 12 }
                spacing: 8

                Item {
                    width: parent.width
                    height: Math.round(root.ui(20))
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Tasks"
                        font.family: root.uiFont
                        font.pixelSize: root.ui(14)
                        font.bold: true
                        color: root.cText
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.tasks.length === 0 ? ""
                              : tasksPanel.doneCount + " of " + root.tasks.length + " done"
                        font.family: root.uiFont
                        font.pixelSize: root.ui(11)
                        color: root.cDim
                    }
                }

                Rectangle {
                    width: parent.width; height: 1; color: root.wash(0.25)
                }

                // The empty state says how to make one rather than just being
                // blank — a panel with nothing in it and no explanation reads
                // as broken rather than as "this file has no tasks".
                Text {
                    width: parent.width
                    visible: root.tasks.length === 0
                    text: "No tasks in this file.\n\nLines like  - [ ] something  are tasks.\n"
                          + "Right-click ▸ New task adds one."
                    wrapMode: Text.WordWrap
                    font.family: root.uiFont
                    font.pixelSize: root.ui(12)
                    color: root.cDim
                }

                ListView {
                    id: tasksList
                    visible: root.tasks.length > 0
                    width: parent.width
                    height: parent.height - Math.round(root.ui(20)) - 9 - taskFoot.height - 16
                    clip: true
                    model: root.tasks
                    spacing: 2

                    delegate: Item {
                        id: taskRow
                        required property var modelData
                        width: tasksList.width
                        height: Math.round(root.ui(26))

                        Rectangle {
                            anchors.fill: parent
                            radius: 3
                            color: taskMa.containsMouse ? root.wash(0.15) : "transparent"
                        }

                        // The box. Clicking it ticks; clicking the text jumps.
                        // Two targets rather than one, because "take me to it"
                        // and "mark it off" are both things you want from a list
                        // and a single click cannot mean both.
                        Rectangle {
                            id: taskBox
                            anchors { left: parent.left; leftMargin: 6
                                      verticalCenter: parent.verticalCenter }
                            width: Math.round(root.ui(14))
                            height: width
                            radius: 3
                            color: taskRow.modelData.done ? root.cGood : "transparent"
                            border { width: 1
                                     color: taskRow.modelData.done ? root.cGood
                                                                   : root.wash(0.55) }
                            Text {
                                anchors.centerIn: parent
                                visible: taskRow.modelData.done
                                text: "\u2713"
                                font.family: root.uiFont
                                font.pixelSize: root.ui(11)
                                font.bold: true
                                color: root.cPanel
                            }
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.taskToggle(taskRow.modelData)
                            }
                        }

                        Text {
                            anchors { left: taskBox.right; leftMargin: 8
                                      right: taskLine.left; rightMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            elide: Text.ElideRight
                            text: taskRow.modelData.text === ""
                                  ? "(untitled task)" : taskRow.modelData.text
                            font.family: root.uiFont
                            font.pixelSize: root.ui(12)
                            font.strikeout: taskRow.modelData.done
                            color: taskRow.modelData.done ? root.cDim : root.cText
                        }

                        Text {
                            id: taskLine
                            anchors { right: parent.right; rightMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            text: taskRow.modelData.line
                            font.family: root.uiFont
                            font.pixelSize: root.ui(10)
                            color: root.cDim
                        }

                        MouseArea {
                            id: taskMa
                            anchors { left: taskBox.right; top: parent.top
                                      right: parent.right; bottom: parent.bottom }
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.scrollToLine(taskRow.modelData.line - 1)
                                tasksPanel.hide()
                            }
                        }
                    }
                }

                Row {
                    id: taskFoot
                    width: parent.width
                    spacing: 8
                    ToolButton { label: "New task"
                                 tip: "add a task at the caret"
                                 onTriggered: {
                                     tasksPanel.hide()
                                     root.actKeys("o- [ ] ")
                                 } }
                    ToolButton { label: "Refresh"; tip: "re-read the file"
                                 onTriggered: root.taskScan() }
                    ToolButton { label: "Close"; tip: "Esc"
                                 onTriggered: tasksPanel.hide() }
                }
            }
        }

        // ── About ───────────────────────────────────────────────────────────
        Rectangle {
            id: aboutPane
            visible: false
            anchors.centerIn: parent
            width: Math.max(0, Math.min(parent.width - 60, Math.round(root.ui(420))))
            height: aboutCol.implicitHeight + 32
            radius: 8
            // Belt and braces. The rows below are given a real width, which is
            // the actual fix; this is here so that the next row added without
            // one is merely cut off instead of drawn across the editor.
            clip: true
            color: root.cPanel
            border.color: root.wash(0.4)
            border.width: 1

            // ⚠ Sized from implicitHeight, and the inner Column must NOT use
            // anchors.fill — that is a binding loop, and a fixed height cannot
            // hold ui()-scaled content anyway: the buttons hang through the
            // border at 150%.
            Column {
                id: aboutCol
                anchors { left: parent.left; right: parent.right; top: parent.top
                          margins: 16 }
                spacing: 6

                Text {
                    text: "SYNAPSE Edit"
                    font.family: root.uiFont
                    font.pixelSize: root.ui(16)
                    font.bold: true
                    color: root.cText
                }
                Repeater {
                    model: root.aboutRows
                    Row {
                        required property var modelData
                        spacing: 8
                        Text {
                            id: aboutField
                            width: Math.round(root.ui(110))
                            text: modelData.field
                            font.family: root.uiFont
                            font.pixelSize: root.ui(11)
                            color: root.cDim
                        }
                        Text {
                            // ⚠ Widthless, this laid out to its own advance and
                            // ran clean through the panel's right border and
                            // across the editor behind it — `engine` and the
                            // wl-paste line both did. The width cap on
                            // aboutPane decides where the BORDER is drawn; it
                            // constrains nothing inside. max(0, …) because a
                            // negative width does not clip, it paints out.
                            width: Math.max(0, aboutCol.width - aboutField.width - 8)
                            wrapMode: Text.WordWrap
                            text: modelData.value
                                  + (modelData.detail ? "  " + modelData.detail : "")
                            font.family: root.uiFont
                            font.pixelSize: root.ui(11)
                            color: root.openable(modelData.detail) ? root.cAccent : root.cText
                            MouseArea {
                                anchors.fill: parent
                                enabled: root.openable(modelData.detail)
                                cursorShape: Qt.PointingHandCursor
                                // ⚠ Only an http(s) URL is ever handed to the
                                // system. The GUI must not learn to RUN a
                                // detail string — that would execute whatever
                                // a record happened to contain.
                                onClicked: Qt.openUrlExternally(modelData.detail)
                            }
                        }
                    }
                }
                Item { width: 1; height: 6 }
                Rectangle {
                    width: Math.round(root.ui(70)); height: Math.round(root.ui(26))
                    radius: 4
                    color: closeMa.containsMouse ? root.wash(0.3) : root.wash(0.16)
                    Text {
                        anchors.centerIn: parent
                        text: "Close"
                        font.family: root.uiFont
                        font.pixelSize: root.ui(11)
                        color: root.cText
                    }
                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: aboutPane.visible = false
                    }
                }
            }
        }

        // ── tooltips ────────────────────────────────────────────────────────
        //
        // ONE bubble for the whole window, declared LAST so it is above every
        // panel, and living outside the toolbar's clipped groups.
        //
        // A tip parented to its own button lost twice: the toolbar is declared
        // before the sidebar and the editor, and later siblings paint on top,
        // so the part of the bubble that hung below the toolbar was covered —
        // which is how "write this buffer" reached the screen as "write this
        // buf", not clipped but buried. Moving the groups into clip:true boxes
        // would then have cut it for real.
        Item {
            id: shellTip
            anchors.fill: parent
            visible: shellTip.owner !== null

            property var owner: null
            property string text: ""
            property real bx: 0
            property real by: 0
            property real bw: 0

            function showFor(o, t, x, y, w) {
                shellTip.owner = o; shellTip.text = t
                shellTip.bx = x; shellTip.by = y; shellTip.bw = w
            }
            // Keyed on the owner: a button that has already lost the pointer
            // must not hide the tip of the one that just gained it.
            function hideFor(o) { if (shellTip.owner === o) shellTip.owner = null }

            Rectangle {
                // Centred on the BUTTON. The old `x: -width / 2` was relative
                // to a zero-width Item at the button's origin, so it centred
                // the bubble on the button's left EDGE — every tip sat half a
                // button too far left. Then clamped into the window, or the
                // leftmost button's tip hangs off the side of it.
                x: Math.max(4, Math.min(shellTip.width - width - 4,
                                        shellTip.bx + (shellTip.bw - width) / 2))
                y: shellTip.by + Math.round(root.ui(22))
                width: ttText.implicitWidth + 12
                height: ttText.implicitHeight + 8
                radius: 3
                color: root.cBg
                border.color: root.wash(0.4)
                border.width: 1
                Text {
                    id: ttText
                    anchors.centerIn: parent
                    text: shellTip.text
                    font.family: root.uiFont
                    font.pixelSize: root.ui(10)
                    color: root.cText
                }
            }
        }
    }

    function openable(d) { return typeof d === "string" && d.indexOf("https://") === 0 }

    property var aboutRows: []
    Process {
        id: aboutProc
        command: [root.bin, "--rec", "about"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                const ls = this.text.split("\n").filter(l => l !== "")
                for (let i = 1; i < ls.length; i++) {
                    const f = ls[i].split("\t").map(root.disp)
                    out.push({ field: f[0], value: f[1], detail: f[2] || "" })
                }
                root.aboutRows = out
            }
        }
    }

    // Splits one line into coloured runs. The spans arrive already in display
    // columns and already sorted, and they never overlap, so the gaps between
    // them are plain text and nothing has to be measured here.
    function segsFor(no, text) {
        const sp = root.spans[no] || []
        const out = []
        let at = 0
        for (const s of sp) {
            if (s.start > at) out.push({ t: text.substring(at, s.start), k: "text" })
            out.push({ t: text.substring(s.start, s.start + s.len), k: s.tok })
            at = s.start + s.len
        }
        if (at < text.length) out.push({ t: text.substring(at), k: "text" })
        if (out.length === 0) out.push({ t: "", k: "text" })
        return out
    }

    // A small flat button, matching the rest of the suite rather than
    // QtQuick.Controls — whose style matches nothing else on this desktop.
    component ToolButton: Rectangle {
        id: tb
        property string label: ""
        property string tip: ""
        property bool active: false
        signal triggered()

        width: tbText.implicitWidth + 18
        height: Math.round(root.ui(26))
        radius: 4
        color: tb.active ? root.wash(0.3)
             : tbMa.containsMouse ? root.wash(0.16) : "transparent"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined

        Text {
            id: tbText
            anchors.centerIn: parent
            text: tb.label
            font.family: root.uiFont
            font.pixelSize: root.ui(11)
            color: root.cText
        }
        MouseArea {
            id: tbMa
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                tb.triggered()
                // Focus goes straight back to the text: a toolbar button that
                // swallows the keyboard makes the next keystroke vanish, and
                // in a modal editor that keystroke is usually a command.
                editor.forceActiveFocus()
            }
            onContainsMouseChanged: {
                if (tbMa.containsMouse && tb.tip !== "") {
                    // Resolved ONCE, on hover, and deliberately not as a
                    // binding: mapToItem() does not re-evaluate when the
                    // toolbar reflows, so a bound position keeps pointing at
                    // where the button used to be. Hover is exactly the moment
                    // the answer is fresh.
                    const pt = tb.mapToItem(shell, 0, 0)
                    shellTip.showFor(tb, tb.tip, pt.x, pt.y, tb.width)
                } else {
                    shellTip.hideFor(tb)
                }
            }
        }
    }
}
