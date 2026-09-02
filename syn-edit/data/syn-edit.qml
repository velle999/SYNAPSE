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

// The translation bridge. ⛔ NOT qsTr(): quickshell has no translator to
// install one into, so qsTr() compiles and returns its own argument. See
// qml/I18n.qml.
import "qml"

FloatingWindow {
    id: root

    title: (root.st.file ? root.st.file.replace(/^.*\//, "") : "syn-edit")
           + (root.st.modified === "1" ? " •" : "") + " — " + I18n.tr("SYNAPSE Edit")
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

    // ── Is the window looking at an up-to-date answer? ─────────────────────
    //
    // Every command is answered by exactly one frame, and every frame carries
    // the serial of the command that produced it. So `sent === acked` means
    // there is nothing in flight and the last frame is current.
    //
    // ⚠ THIS EXISTS BECAUSE A SELECTION IS A FRAME BEHIND. Shift+Right and
    // then a letter, typed as fast as anybody types, asks "is something
    // selected?" before the frame that says so has arrived — and a false
    // answer there is a typed character that did not replace the selection it
    // looked like it was replacing. While a command is in flight, what the
    // window ASKED FOR is the better answer than what it last saw, so `hasSel`
    // reads selHint instead. Nothing sets selHint optimistically true unless
    // the window has just asked for a selection, so the answer can only ever
    // err towards "yes" — and `gui delsel` with nothing selected is a no-op.
    property int sent: 0
    property int acked: 0
    property bool selHint: false
    readonly property bool hasSel: root.sent === root.acked ? root.isVisual
                                                            : root.selHint

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
                                     modified: f[3] === "1", current: f[4] === "1",
                                     // Whether it has a PATH, from the engine —
                                     // never by matching the "[No Name]" label,
                                     // which is a message and not a fact.
                                     named: f[5] === "1" })
        } else if (tag === "E") {
            // Every frame answers exactly one command and carries its serial,
            // INCLUDING the scan frame dropped below — an ack that skipped it
            // would leave the window permanently believing it was a command
            // behind, and `hasSel` would never trust a frame again.
            root.acked = parseInt(f[1])
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
            if (root.outbox.length > 0) {
                const held = root.outbox
                root.outbox = []
                for (const c of held) root.send(c)
            }
            if (root.st.quit === "1") { Qt.quit(); return }

            // The verdict on a pending "save and close" — see saveAndClose().
            // Not modified any more and no error: the write happened, so the
            // close follows. Otherwise the message the engine just wrote is
            // the answer, and it is left on screen.
            if (root.closeSerial >= 0 && root.acked >= root.closeSerial) {
                const idx = root.closeIdx
                const wrote = root.st.modified !== "1" && root.st.msgerr !== "1"
                root.closeSerial = -1
                root.closeIdx = -1
                if (wrote) root.doClose(idx, false)
            }

            // A question is about the list it was asked of. If that list
            // changed underneath it — something else closed, something opened
            // — the index it holds no longer names the same document, so the
            // question goes rather than being answered about the wrong file.
            if (root.askClose !== null && root.bufs.length !== root.askCount)
                root.askClose = null

            /* ⛔ NORMAL IS NOT A RESTING STATE IN THIS WINDOW. The engine is
             * modal and this window is not, so every path that ends in normal
             * mode — the editor's own first frame, an undo, a search
             * finishing, a file opening, a task being ticked — used to leave
             * it there, and the next key pressed was a vim command rather than
             * a character. That is one bug with a dozen call sites, and the
             * two that were reported are both of them: NORMAL-mode Backspace
             * is `h`, a motion that stops at column 0 and cannot join a line,
             * and Ctrl+V is `<C-v>`, a block selection.
             *
             * Answering it HERE rather than at each of those call sites is
             * what makes the rule hold for the ones added later.
             *
             * ⚠ Not a loop: `gui insert` always ends in INSERT, so the frame
             * it produces does not ask again. A SELECTION is visual mode and
             * is left alone, and so is the engine's command line — those are
             * states the user asked to be in. */
            if ((root.st.mode || "NORMAL") === "NORMAL") root.guiInsert()
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

    // ⛔ NOTHING MAY BE WRITTEN BEFORE THE ENGINE'S FIRST FRAME. A Process
    // write made before the process has actually spawned is DROPPED, in
    // silence — and the window makes several: `view 0 <rows>` on completion,
    // and one more each time the layout settles on a row count. Five of them
    // went nowhere, which left `sent` five ahead of `acked` for the whole life
    // of the window and `hasSel` above permanently reading the hint instead of
    // the frame. It only ever worked because a later onRowsChanged said the
    // same thing again, after the process was up.
    //
    // Held until the first frame proves the pipe is live, then flushed in
    // order. The engine emits that frame before it reads anything, so the wait
    // is bounded by the fork and not by anything the user does.
    property var outbox: []
    function send(s) {
        if (!root.engineUp) { root.outbox.push(s); return }
        root.sent++
        eng.write(s + "\n")
    }
    function sendKeys(k) { root.send("keys " + encodeURIComponent(k)) }
    function sendEx(c)   { root.send("ex " + encodeURIComponent(c)) }

    // ── The modeless verbs ──────────────────────────────────────────────────
    //
    // ⛔ THE WINDOW HAS NO MODES AND THE ENGINE DOES. These are the whole of
    // the translation, and each one is a PROTOCOL VERB rather than a key
    // sequence for the reason gotoPos is (see below, and serve.c): a key
    // sequence has to know which mode it will be read in, and the window has
    // spent its whole life not knowing. The verb states the mode it leaves
    // behind instead, so no caller here has to think about it.
    //
    // selHint is set alongside each one because the answer will not be in a
    // frame for another round trip — see `hasSel` above.
    function guiInsert() { root.selHint = false; root.send("gui insert") }
    function guiDelsel() { root.selHint = false; root.send("gui delsel") }
    function guiVisual() { root.selHint = true;  root.send("gui visual") }
    function guiCut()    { root.selHint = false; root.send("gui cut") }
    function guiPaste()  { root.selHint = false; root.send("gui paste") }
    // Copy alone does NOT touch the selection, here or in the engine: every
    // other program on the desktop leaves what you copied still selected.
    function guiCopy()   { root.send("gui copy") }

    // Select All, Undo and Redo stay the engine's own commands — there is no
    // undo stack in this file and there is not going to be one. actKeys()
    // leaves insert first; the frame that comes back reports NORMAL, and the
    // rule in onRecord puts the window back into INSERT.
    function selectAll() { root.selHint = true;  root.actKeys("ggVG") }
    function undo()      { root.selHint = false; root.actKeys("u") }
    function redo()      { root.selHint = false; root.actKeys("<C-r>") }

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

    // Line and column, 1-based.
    //
    // ⛔ THIS WAS `NG N|` AS KEYS, AND A MOTION SENT IN INSERT MODE IS TYPED,
    // NOT OBEYED. Clicking while typing filled the document with
    // `1G28|1G27|1G27|viw` — the mouse's own coordinates, as text, which is
    // unexplainable from the outside because they are characters nobody
    // pressed. The right-click path had already been patched for it once
    // (see onPressed below); every other mouse path had not.
    //
    // ⚠ ESCAPING FIRST IS NOT THE FIX EITHER. No editor throws you out of
    // insert mode for clicking somewhere, and none puts you into it. The mode
    // has to come out exactly as it went in, in all three of normal, insert
    // and visual — so this is a protocol verb that moves the caret and says
    // nothing about the mode, not a key sequence that has to know about them.
    //
    // It still EXTENDS a visual selection, which is what a drag needs: the
    // engine keeps the anchor in vy/vx and reads the selection as
    // anchor→caret, so moving the caret is extending it.
    function gotoPos(line, col) {
        root.send("goto " + Math.max(1, line) + " " + Math.max(1, col))
    }

    // Start a selection if there is not one already.
    //
    // ⛔ THIS WAS `<Esc>v` AND BOTH HALVES WERE WRONG. `v` is a letter in
    // insert mode, so a drag begun while typing left one in the document and
    // then selected nothing — and the `<Esc>` that was added to stop it moves
    // the caret one column LEFT (vim's rule, see leave_insert), so the
    // selection anchored one character to the left of the caret the user could
    // see. `gui visual` enters visual from whatever mode it finds and leaves
    // the caret exactly where it is.
    function beginVisual() {
        if (root.hasSel) return
        root.guiVisual()
    }

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
        switch (event.key) {
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

        // ⛔ NO Ctrl-<letter> BRANCH. This used to return "<C-v>", "<C-a>" and
        // the rest, which is what a terminal means by them — and it is why
        // Ctrl+V had never pasted in this window: `<C-v>` is a BLOCK
        // SELECTION. A modeless window has no mode for a vim control key to
        // run in, so Ctrl is answered as a GUI shortcut before anything gets
        // here (see Keys.onPressed) and never reaches the engine's key table.
        //
        // Letting one through would be worse than useless: in insert mode a
        // control code is a BYTE, written into the document.

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
        // Not `NG` as keys: in insert mode the count is typed. Scrolling must
        // not change the mode, and must not type a number into the document.
        root.gotoPos(want + 1, 1)
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
    // ⚠ THE ANSWER CAN BE PASSED IN, and the sidebar does pass it. `root.named`
    // is the CURRENT buffer's, read off the last frame — and "save and close"
    // has just asked for a different buffer, whose frame has not arrived. It
    // would branch on the wrong document, and on an unnamed one that means
    // opening a name prompt for a file nobody was closing.
    //
    // This is also the ONE place in the window a bare `save` is sent, which is
    // pinned in the suite: a bare save on a buffer with no name is the dead end
    // saveAs() exists to clear.
    function saveNow(named) {
        if (named === undefined) named = root.named
        if (named) root.send("save")
        else       root.saveAs()
    }

    // Pick the FOLDER with the pointer, type only the basename. The browser is
    // already a directory chooser; naming a file is the one part of the job it
    // cannot do, so it hands that part to the command line.
    function saveAs() {
        if (root.haveFiles) browser.showSave()
        else                root.promptWrite(root.saveDir() + "/")
    }

    // ── The sidebar ─────────────────────────────────────────────────────────
    //
    // Width while a drag is in progress. Zero means "not dragging", and the
    // panel then follows the width the engine remembers.
    property int dragW: 0

    // ── Closing a document ──────────────────────────────────────────────────
    //
    // ⛔ THE WINDOW COULD NOT CLOSE ONE AT ALL until now: it opened buffers
    // and switched between them, and the list only ever grew. `:bd` was the
    // only way out of a window that exists so nobody has to know `:bd`.
    //
    // ⚠ AND `:bd` REFUSES ON A MODIFIED BUFFER — "unsaved changes (:bd! to
    // discard)" — which is right in a terminal and a dead end in a window.
    // The same shape as the Save button that could refuse but not ask. So the
    // window asks, and both ways out are offered.
    property var askClose: null     // the row a question is being asked about
    property int askCount: 0        // how many documents there were when it was
    property int closeIdx: -1       // the one a save is running for
    property int closeSerial: -1    // the frame whose answer decides it

    // ⛔ NEVER THE LAST ONE. `:bd` on a single buffer sets quit, so an ✕ on the
    // only row would close the WINDOW — which is not what an ✕ on a row means
    // anywhere else, and is a whole session lost to a stray click. The button
    // hides itself on the last document too; this is the RULE, that is the
    // presentation of it, and only one of the two can be relied on.
    function doClose(idx, force) {
        if (root.bufs.length <= 1) return
        root.send("buf " + idx)
        root.sendEx(force ? "bd!" : "bd")
        editor.forceActiveFocus()
    }

    function closeBuffer(b) {
        if (root.bufs.length <= 1) return
        if (b.modified) {
            root.askClose = b
            root.askCount = root.bufs.length
            return
        }
        root.doClose(b.idx, false)
    }

    function discardAndClose() {
        const b = root.askClose
        root.askClose = null
        if (b) root.doClose(b.idx, true)
    }

    // ⚠ THE CLOSE CANNOT SIMPLY BE SENT AFTER THE SAVE. A write can fail —
    // a read-only file, a full disk — and `bd` behind a failed save refuses
    // with ITS message, which overwrites the write error with "unsaved
    // changes". The user would be told the wrong thing about the wrong
    // problem, and clicking again would say it again.
    //
    // So the save's own frame is the verdict, found by the serial it carries.
    function saveAndClose() {
        const b = root.askClose
        root.askClose = null
        if (!b) return
        root.send("buf " + b.idx)
        root.saveNow(b.named)
        root.closeIdx = b.idx
        root.closeSerial = root.sent    // the serial that save will answer with
        editor.forceActiveFocus()
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
            // ⚠ LEFT OF THIS IS THE SIDEBAR, top to bottom. The toolbar, the
            // tabs, the text and the status bar are one COLUMN beside it
            // rather than four full-width bands with the sidebar tucked into
            // the middle two — which is what put a document list under a
            // toolbar that did not act on it.
            anchors { top: parent.top; left: sidebar.right; right: parent.right }
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
                    ToolButton { label: I18n.tr("Documents"); tip: I18n.tr("show or hide the list")
                                 active: root.st.tree === "1"
                                 onTriggered: root.send("set tree!") }
                    // The tab strip says the same thing the sidebar says, and
                    // which of the two anybody wants is a matter of taste — so
                    // it is a switch rather than a decision made here. It had
                    // never had one: `:set tabbar!` was the only control, in a
                    // window that exists so nobody has to type that.
                    ToolButton { label: I18n.tr("Tabs"); tip: I18n.tr("show or hide the tab strip")
                                 active: root.st.tabbar === "1"
                                 onTriggered: root.send("set tabbar!") }
                    ToolButton { label: I18n.tr("About"); tip: I18n.tr("version and licence")
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

                    ToolButton { label: I18n.tr("New");  tip: I18n.tr("a new empty buffer");  onTriggered: root.send("new") }
                    ToolButton { label: I18n.tr("Open"); tip: root.haveFiles ? I18n.tr("browse for a file")
                                                                   : I18n.tr("type a path (:e)")
                                 onTriggered: root.haveFiles ? browser.show()
                                                             : root.actKeys(":e ") }
                    ToolButton { label: I18n.tr("Save"); tip: root.named ? I18n.tr("write this buffer")
                                                               : I18n.tr("name it, then write it")
                                 onTriggered: root.saveNow() }
                    ToolButton { label: I18n.tr("Save As"); tip: I18n.tr("write it somewhere else")
                                 onTriggered: root.saveAs() }
                    Rectangle { width: 1; height: Math.round(root.ui(20)); color: root.cDim; opacity: 0.4
                                anchors.verticalCenter: parent.verticalCenter }
                    ToolButton { label: I18n.tr("Undo"); tip: "Ctrl+Z";       onTriggered: root.undo() }
                    ToolButton { label: I18n.tr("Redo"); tip: "Ctrl+Shift+Z"; onTriggered: root.redo() }
                    Rectangle { width: 1; height: Math.round(root.ui(20)); color: root.cDim; opacity: 0.4
                                anchors.verticalCenter: parent.verticalCenter }
                    ToolButton { label: I18n.tr("Find"); tip: "Ctrl+F";       onTriggered: root.actKeys("/") }
                    ToolButton { label: I18n.tr("Replace"); tip: "Ctrl+R";    onTriggered: root.actKeys(":%s/") }
                }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                        color: root.cDim; opacity: 0.35 }
        }

        // ── document tabs ───────────────────────────────────────────────────
        Rectangle {
            id: tabstrip
            anchors { top: toolbar.bottom; left: sidebar.right; right: parent.right }
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
                            text: (modelData.name.replace(/^.*\//, "") || I18n.tr("[No Name]"))
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

        // ── the sidebar: a document list, not a tree ────────────────────────
        //
        // Full height and down the left, with a header of its own — the shape
        // every notes and document application on this desktop uses, and the
        // one velle asked for. It replaces a strip wedged between a toolbar
        // that did not act on it and a status bar about a different pane.
        //
        // A row is a CARD rather than a line of text: the name is what you
        // look for, the folder is what tells two files of the same name apart,
        // and neither fits on one line at this width. The current one carries
        // an accent bar rather than only a tint, because a tint is the first
        // thing a pale theme loses.
        Rectangle {
            id: sidebar
            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }

            // Capped as a FRACTION as well as an absolute. A fixed width is
            // most of a narrow window: at 350px wide a 200px panel left ~120px
            // of editor, less than the gutter plus any usable number of
            // columns, so the pane the list exists to point AT became
            // unreadable to make room for the list.
            //
            // ⚠ The floor is applied BEFORE the fraction, and then the
            // fraction wins — otherwise a very narrow window gets a sidebar
            // wider than itself and the editor disappears entirely.
            readonly property int want: root.dragW > 0
                                        ? root.dragW
                                        : parseInt(root.st.treewidth || "230")
            width: root.st.tree === "1"
                   ? Math.min(Math.max(sidebar.want, root.ui(150)),
                              Math.round(shell.width * 0.45))
                   : 0
            visible: width > 0
            clip: true
            color: Qt.rgba(root.cPanel.r, root.cPanel.g, root.cPanel.b, 0.55)

            // The header lines up with the toolbar across the divider, so the
            // two panes read as one window rather than two stacked at
            // different heights.
            Item {
                id: sideHead
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: toolbar.height

                Text {
                    anchors { left: parent.left; leftMargin: 14
                              verticalCenter: parent.verticalCenter }
                    text: I18n.tr("DOCUMENTS")
                    font.family: root.uiFont
                    font.pixelSize: root.ui(10)
                    font.letterSpacing: 1.2
                    font.bold: true
                    color: root.cDim
                }
                Text {
                    anchors { right: parent.right; rightMargin: 14
                              verticalCenter: parent.verticalCenter }
                    text: root.bufs.length
                    font.family: root.uiFont
                    font.pixelSize: root.ui(10)
                    color: root.cDim
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                            color: root.cDim; opacity: 0.35 }
            }

            // A ListView rather than a Column, because MAXBUF is 64 and a
            // Column of 64 rows in a 300px panel simply runs off the bottom
            // with no way to reach the rest.
            ListView {
                id: sideList
                anchors { top: sideHead.bottom; left: parent.left; right: parent.right
                          bottom: closeAsk.visible ? closeAsk.top : parent.bottom
                          topMargin: 6; bottomMargin: 6 }
                clip: true
                spacing: 3
                model: root.bufs
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: card
                    required property var modelData
                    // max(0, …): a row narrower than its own margins would go
                    // NEGATIVE, and a negative width does not clip — it paints
                    // outside the panel it is supposed to be inside.
                    width: Math.max(0, sideList.width - 12)
                    x: 6
                    height: Math.round(root.ui(44))
                    radius: 6
                    color: card.modelData.current ? root.wash(0.16)
                         : cardMa.containsMouse ? root.wash(0.07) : "transparent"

                    // ⚠ THE BAR, NOT ONLY THE TINT. wash() is the accent at
                    // 16% over the panel, and on a pale theme that is a
                    // difference of a few percent in luminance — legible on
                    // the dark preset and invisible on Prism. A solid bar is
                    // the same shape on both.
                    Rectangle {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom
                                  topMargin: 6; bottomMargin: 6 }
                        width: 3
                        radius: 2
                        color: root.cAccent
                        visible: card.modelData.current
                    }

                    Column {
                        anchors { left: parent.left; leftMargin: 14
                                  right: cardClose.left; rightMargin: 6
                                  verticalCenter: parent.verticalCenter }
                        spacing: 1

                        Text {
                            width: parent.width
                            // ElideMiddle keeps BOTH ends of a long name, and
                            // the ends are what tell "config.old.json" from
                            // "config.new.json".
                            elide: Text.ElideMiddle
                            text: (card.modelData.name.replace(/^.*\//, "") || I18n.tr("[No Name]"))
                            font.family: root.uiFont
                            font.pixelSize: root.ui(12)
                            font.bold: card.modelData.current
                            color: root.cText
                        }
                        Text {
                            width: parent.width
                            // ElideLeft: the END of a path is what disambiguates
                            // it, and the beginning is /home/somebody over and
                            // over. ~ for the home directory, the way every
                            // other window in the suite writes it.
                            elide: Text.ElideLeft
                            text: {
                                if (!card.modelData.named) return I18n.tr("not saved yet")
                                const home = Quickshell.env("HOME") || ""
                                let d = card.modelData.name.replace(/\/[^/]*$/, "")
                                if (d === "") d = "/"
                                if (home !== "" && d.indexOf(home) === 0)
                                    d = "~" + d.substring(home.length)
                                return d
                            }
                            font.family: root.uiFont
                            font.pixelSize: root.ui(10)
                            color: root.cDim
                        }
                    }

                    // Unsaved, as a dot — the same mark the tab strip and the
                    // status bar use, so one glance answers it in all three.
                    Rectangle {
                        anchors { right: parent.right; rightMargin: 12
                                  verticalCenter: parent.verticalCenter }
                        width: 7; height: 7; radius: 4
                        color: root.cWarn
                        visible: card.modelData.modified && !cardClose.visible
                    }

                    // ⛔ CLOSING A DOCUMENT HAD NO CONTROL AT ALL. The window
                    // could open buffers and switch between them and never let
                    // one go — the list only ever grew, and `:bd` was the only
                    // way out of a window whose whole point is not needing to
                    // know that.
                    //
                    // Hidden on the last one: `:bd` on a single buffer sets
                    // quit, and a stray click closing the whole window is not
                    // what an × on a row means anywhere else.
                    Rectangle {
                        id: cardClose
                        anchors { right: parent.right; rightMargin: 8
                                  verticalCenter: parent.verticalCenter }
                        width: Math.round(root.ui(18)); height: width
                        radius: 4
                        visible: root.bufs.length > 1
                                 && (cardMa.containsMouse || cardCloseMa.containsMouse)
                        color: cardCloseMa.containsMouse ? root.wash(0.3) : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            font.family: root.uiFont
                            font.pixelSize: root.ui(10)
                            color: root.cText
                        }
                        MouseArea {
                            id: cardCloseMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.closeBuffer(card.modelData)
                        }
                    }

                    MouseArea {
                        id: cardMa
                        anchors.fill: parent
                        hoverEnabled: true
                        // Below the close button, which is a child of this
                        // rectangle and therefore ON TOP of a fill-anchored
                        // MouseArea only because it declares its own.
                        z: -1
                        onClicked: {
                            root.send("buf " + card.modelData.idx)
                            editor.forceActiveFocus()
                        }
                    }
                }
            }

            // ── closing something with unsaved changes ──────────────────────
            //
            // ⛔ A REFUSAL IS NOT AN ANSWER. `:bd` on a modified buffer says
            // "unsaved changes (:bd! to discard)", which is correct in a
            // terminal and a dead end in a window — the same shape as the
            // Save button that could refuse but not ask (see saveNow above).
            // So the window asks, and both ways out are here.
            Rectangle {
                id: closeAsk
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: askCol.implicitHeight + 16
                visible: root.askClose !== null
                color: root.cPanel

                Rectangle { anchors.top: parent.top; width: parent.width; height: 1
                            color: root.cWarn; opacity: 0.6 }

                Column {
                    id: askCol
                    anchors { left: parent.left; right: parent.right; top: parent.top
                              margins: 8 }
                    spacing: 6

                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.askClose
                              ? I18n.tr("%1 has unsaved changes.").arg(
                                    root.askClose.name.replace(/^.*\//, "")
                                    || I18n.tr("[No Name]"))
                              : ""
                        font.family: root.uiFont
                        font.pixelSize: root.ui(11)
                        color: root.cText
                    }
                    // ⚠ A Flow, NOT A Row. Three buttons do not fit across a
                    // sidebar at its floor width, and a Row does not wrap — it
                    // runs off the edge, and the one that goes is Cancel: the
                    // answer somebody reaching for this question most often
                    // wants, and the only one that is not destructive.
                    Flow {
                        width: parent.width
                        spacing: 4
                        // Save is offered only when there is somewhere to save
                        // TO. On a buffer that has never been named it would
                        // open the file browser mid-question, which is a second
                        // question on top of the first.
                        ToolButton {
                            label: I18n.tr("Save & close")
                            tip: I18n.tr("write it, then close it")
                            centered: false
                            visible: root.askClose !== null && root.askClose.named
                            onTriggered: root.saveAndClose()
                        }
                        ToolButton { label: I18n.tr("Discard"); centered: false
                                     tip: I18n.tr("close it and lose the changes")
                                     onTriggered: root.discardAndClose() }
                        ToolButton { label: I18n.tr("Cancel"); centered: false
                                     tip: I18n.tr("keep it open")
                                     onTriggered: root.askClose = null }
                    }
                }
            }

            // ── the splitter ────────────────────────────────────────────────
            //
            // ⚠ preventStealing, or the window itself takes the drag: this
            // grip is inside no Flickable, and the toplevel will happily start
            // a window move from a press it was handed.
            //
            // The width is followed LIVE from a local property and written to
            // the engine ONCE, on release — a `set` per pixel of travel is a
            // frame per pixel and a settings file rewritten a hundred times to
            // answer one drag.
            MouseArea {
                id: sideGrip
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: 7
                hoverEnabled: true
                cursorShape: Qt.SizeHorCursor
                preventStealing: true

                onPressed: root.dragW = sidebar.width
                onPositionChanged: (m) => {
                    if (!sideGrip.pressed) return
                    // Read in the WINDOW's coordinates. The grip moves with the
                    // edge it is dragging, so a delta measured inside it chases
                    // its own tail and the panel accelerates away from the
                    // pointer.
                    root.dragW = Math.max(root.ui(150),
                                          Math.round(sideGrip.mapToItem(shell, m.x, 0).x))
                }
                onReleased: {
                    if (root.dragW > 0) root.send("set treewidth=" + sidebar.width)
                    root.dragW = 0
                }
                onCanceled: root.dragW = 0

                Rectangle {
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                    width: 1
                    color: sideGrip.containsMouse || sideGrip.pressed ? root.cAccent : root.cDim
                    opacity: sideGrip.containsMouse || sideGrip.pressed ? 0.9 : 0.25
                }
            }
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

            // ── Keys, in a window that has no modes ────────────────────
            //
            // ⛔ EVERY KEY IS ANSWERED AS A GUI KEY FIRST, and only what is
            // left over reaches the engine's key table — which this window now
            // guarantees is always reached in INSERT.
            //
            // The two symptoms this was written for were one bug. Ctrl+V had
            // never pasted, because `<C-v>` is a block selection; and
            // Backspace at the start of line 2 did nothing, because NORMAL
            // Backspace is `h`, a motion that stops dead at column 0. Both are
            // correct vim, and neither is what this window means — it had
            // silently left INSERT, which every mouse gesture used to do.
            //
            // ⚠ THE TERMINAL FRONT-END IS UNTOUCHED AND STILL FULLY MODAL.
            // `syn-edit` in a terminal is vim; none of this is reachable from
            // it, and tui.c hands raw keys straight to the engine.
            Keys.onPressed: (event) => {
                const mod = event.modifiers
                const k = event.key
                const ctrl = (mod & Qt.ControlModifier) !== 0

                // Find and Replace put the caret on the ENGINE's command line,
                // and it owns every key until it is done — Escape included.
                // The frame it leaves behind reports NORMAL, which is what
                // puts the window back into INSERT; there is no second copy of
                // that rule here.
                if (root.inCmd) {
                    // ⚠ EXCEPT PASTE. Pasting a search term is the ordinary
                    // reason to have a Find box open at all, and the engine
                    // knows to put it on the command line rather than in the
                    // document (see gui_paste in serve.c).
                    if (ctrl && k === Qt.Key_V) { root.guiPaste(); event.accepted = true; return }
                    const c = root.keyName(event)
                    if (c !== "") { root.sendKeys(c); event.accepted = true }
                    return
                }

                if (ctrl) {
                    event.accepted = true
                    switch (k) {
                    case Qt.Key_S: root.saveNow();   return
                    case Qt.Key_C: root.guiCopy();   return
                    case Qt.Key_X: root.guiCut();    return
                    // Ctrl+Shift+V arrives here too, and means the same thing:
                    // this window pastes plain text and has nothing else to
                    // offer, so the two are not worth telling apart.
                    case Qt.Key_V: root.guiPaste();  return
                    case Qt.Key_A: root.selectAll(); return
                    case Qt.Key_Z: (mod & Qt.ShiftModifier) ? root.redo() : root.undo(); return
                    case Qt.Key_Y: root.redo();      return
                    // Find and Replace open the engine's own command line
                    // prefilled, exactly as the toolbar buttons do — the
                    // typing is the editor's, not a text field written here.
                    case Qt.Key_F: root.actKeys("/");     return
                    case Qt.Key_R: root.actKeys(":%s/");  return
                    case Qt.Key_N: root.send("new");      return
                    case Qt.Key_O: browser.show();        return
                    }
                    // ⛔ EVERY OTHER Ctrl COMBINATION IS SWALLOWED, not passed
                    // on. It used to become `<C-x>` — a vim command, in a
                    // window with no mode to run one in, and in insert mode a
                    // control BYTE written into the document.
                    return
                }

                if (k === Qt.Key_Escape) {
                    // Escape drops the selection and nothing else. It does NOT
                    // go back to NORMAL: there is no normal mode to go back to
                    // in this window, and an Escape that quietly disarmed
                    // Backspace and Ctrl+V is the bug this file was rewritten
                    // for.
                    root.guiInsert()
                    event.accepted = true
                    return
                }

                const motion = root.motionKey(k)
                if (motion !== "") {
                    // Shift+<motion> extends a selection, a plain motion
                    // collapses one — which is what they do in every other
                    // editor, and neither is a selection kept in this file.
                    // The engine's visual mode IS the selection.
                    if (mod & Qt.ShiftModifier)   root.beginVisual()
                    else if (root.hasSel)         root.guiInsert()
                    root.sendKeys(motion)
                    event.accepted = true
                    return
                }

                const key = root.keyName(event)
                if (key === "") return
                // Typing, Backspace or Delete over a selection replaces it.
                // Told to the engine as its own command so that the window
                // never has to decide whether Backspace here means "delete
                // what is selected" or "join these two lines" — one of those
                // answers is a selection model living in this file.
                if (root.hasSel) root.guiDelsel()
                root.sendKeys(key)
                event.accepted = true
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
                        // way a left click does.
                        //
                        // ⛔ AND IT NO LONGER ESCAPES TO NORMAL FIRST. The
                        // escape was here because the menu's entries were vim
                        // keys and needed a mode to run in — a right click
                        // therefore disarmed Backspace and Ctrl+V for every
                        // key pressed after it. The entries are protocol verbs
                        // now, so the mode is nobody's business here.
                        if (!root.hasSel)
                            root.gotoPos(textMa.lineAt(m.y), textMa.colAt(m.x))
                        ctxMenu.openAt(m.x, m.y)
                        return
                    }
                    // A fresh click drops any selection: press, drag, release
                    // is one gesture and it starts from nothing. It drops it
                    // without DELETING it, and without leaving insert — the
                    // caret does not move either, because gotoPos below is
                    // what moves it.
                    if (root.hasSel) root.guiInsert()
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
                    // ⛔ THIS WAS `<Esc>viw` AND IT IS HALF THE BUG REPORT.
                    // The escape left the window in NORMAL and nothing ever
                    // put it back — so after one double click, Backspace was
                    // `h` and would not join line 2 to line 1, and Ctrl+V was
                    // a block selection. `gui visual` enters visual from
                    // whatever mode it finds and leaves the caret alone; `iw`
                    // is then the engine's own idea of a word, read in the
                    // mode where it is a text object rather than typed.
                    root.beginVisual()
                    root.sendKeys("iw")
                }

                onWheel: (w) => {
                    // Three lines a notch, the usual step, expressed as KEYS
                    // so the engine's own view bookkeeping stays the only copy
                    // of where we are.
                    // ⚠ NOT `3<Up>`. The arrows are fine in insert mode; the
                    // COUNT is not — `3` is typed and only then does the
                    // caret move. Three plain motions say the same thing in
                    // every mode.
                    const k = w.angleDelta.y > 0 ? "<Up>" : "<Down>"
                    root.sendKeys(k + k + k)
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
                // A BAR while typing and a BLOCK otherwise, which is the
                // convention every editor shares — and here it is the only
                // thing on screen that tells overwrite apart from insert.
                // ⚠ Not root.inserting: that is true in REPLACE too, so
                // overwrite drew the caret of the mode it is not.
                width: root.st.mode === "INSERT" ? Math.max(2, Math.round(root.charW / 8))
                                                 : root.charW
                height: root.lineH
                color: root.cAccent
                opacity: editor.activeFocus ? (root.st.mode === "INSERT" ? 0.9 : 0.45) : 0.25
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
            anchors { bottom: parent.bottom; left: sidebar.right; right: parent.right }
            height: Math.round(root.ui(26)) + msgline.height
            color: root.cPanel

            Row {
                id: statusrow
                anchors { top: parent.top; left: parent.left; leftMargin: 0 }
                height: Math.round(root.ui(26))
                spacing: 0

                // ⚠ THE WINDOW'S WORDS, NOT THE ENGINE'S. This front-end is
                // modeless, so "INSERT" is not news and "VISUAL" is not what
                // anything else on the desktop calls a selection. The two
                // states worth a chip are the ones that change what a key
                // does: something is selected, or typing overwrites.
                //
                // NORMAL is still shown as itself if it ever appears. It
                // should not — onRecord puts the window straight back into
                // INSERT — and a chip that hid it would hide the return of
                // exactly the bug this was written for.
                Rectangle {
                    readonly property string label: {
                        const m = root.st.mode || "INSERT"
                        if (m === "REPLACE") return I18n.tr("OVERWRITE")
                        if (m.indexOf("V") === 0) return I18n.tr("SELECT")
                        if (m === "INSERT") return I18n.tr("INSERT")
                        if (m === "NORMAL") return I18n.tr("NORMAL")
                        return m
                    }
                    width: modeText.implicitWidth + 20
                    height: parent.height
                    color: root.st.mode === "INSERT" ? root.cGood
                         : (root.st.mode || "").indexOf("V") === 0 ? root.cWarn
                         : root.cAccent
                    Text {
                        id: modeText
                        anchors.centerIn: parent
                        text: parent.label
                        font.family: root.uiFont
                        font.pixelSize: root.ui(11)
                        font.bold: true
                        color: root.lum(parent.color) > 0.4 ? "#12141a" : "#ffffff"
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 12
                    text: (root.st.file || I18n.tr("[No Name]"))
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
                    text: (root.st.binary === "1" ? I18n.tr("binary") + "  " : "")
                          + (root.st.eol === "crlf" ? "CRLF  " : "")
                          + (root.st.lang || "text")
                    font.family: root.uiFont
                    font.pixelSize: root.ui(11)
                    color: root.cDim
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: I18n.tr("%1:%2  of %3").arg(root.st.line || "1")
                                                    .arg(root.st.col || "1")
                                                    .arg(root.st.lines || "1")
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
        // Nothing here is implemented in this file: an entry is either a
        // protocol verb or the engine's own keys. The clipboard is the
        // desktop's, because the + register is (vim.c shells out to
        // wl-copy/wl-paste), so Copy here means what Copy means everywhere
        // else while still being the editor's own yank.
        //
        // ⛔ CUT, COPY AND PASTE ARE NO LONGER `"+d`, `"+y` AND `"+p`. Those
        // are normal-mode keys, so every one of them needed the right-click to
        // escape out of insert first — which is what left the window in a mode
        // where Backspace could not join a line. And `"+p` is vim's PUT: it
        // lands after the caret, takes a whole line whenever the register
        // happens to be linewise, and leaves the caret on the last character
        // rather than after it, none of which is what Paste means in a window.
        //
        // With no selection Cut and Copy take the line, which is what a menu
        // with nothing selected has to pick anyway — better than a greyed-out
        // entry.
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
                        const sel = root.hasSel
                        return [
                            { label: sel ? I18n.tr("Cut") : I18n.tr("Cut line"),
                              keys: "", act: "cut", hint: "Ctrl+X" },
                            { label: sel ? I18n.tr("Copy") : I18n.tr("Copy line"),
                              keys: "", act: "copy", hint: "Ctrl+C" },
                            { label: I18n.tr("Paste"), keys: "", act: "paste", hint: "Ctrl+V" },
                            { label: "-", keys: "", hint: "" },
                            { label: I18n.tr("Select All"), keys: "", act: "selectall", hint: "Ctrl+A" },
                            { label: "-", keys: "", hint: "" },
                            { label: I18n.tr("Undo"), keys: "", act: "undo", hint: "Ctrl+Z" },
                            { label: I18n.tr("Redo"), keys: "", act: "redo", hint: "Ctrl+Shift+Z" },
                            { label: "-", keys: "", hint: "" },
                            // Task list. `o- [ ] ` deliberately LEAVES the
                            // engine in INSERT with the caret after the bracket:
                            // the next thing anybody wants after "new task" is
                            // to type the task, and a menu item that dropped
                            // them in NORMAL would eat that first word.
                            { label: I18n.tr("Task list…"), keys: "", act: "tasks", hint: "" },
                            { label: I18n.tr("New task"), keys: "o- [ ] ", hint: "" },
                            { label: root.taskAtCaret()
                                     ? (root.taskAtCaret().done ? I18n.tr("Untick this task")
                                                                : I18n.tr("Tick this task"))
                                     : I18n.tr("Tick this task"),
                              keys: "", act: "tasktoggle", hint: "",
                              off: root.taskAtCaret() === null },
                            { label: "-", keys: "", hint: "" },
                            { label: I18n.tr("Find…"), keys: "/", hint: "Ctrl+F" },
                            { label: I18n.tr("Replace…"), keys: ":%s/", hint: "Ctrl+R" },
                            { label: "-", keys: "", hint: "" },
                            { label: I18n.tr("Open…"), keys: "", act: "open", hint: "" },
                            { label: I18n.tr("Save"), keys: "", act: "save", hint: "Ctrl+S" },
                            { label: I18n.tr("Save As…"), keys: "", act: "saveas", hint: "" }
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
                                    if (m.act === "cut")         root.guiCut()
                                    else if (m.act === "copy")   root.guiCopy()
                                    else if (m.act === "paste")  root.guiPaste()
                                    else if (m.act === "selectall") root.selectAll()
                                    else if (m.act === "undo")   root.undo()
                                    else if (m.act === "redo")   root.redo()
                                    else if (m.act === "open")   browser.show()
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
                    text: (browser.mode === "save" ? I18n.tr("Save in:") + "  " : "") + browser.dir
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
                    ToolButton { label: browser.mode === "save" ? I18n.tr("Save here")
                                                                : I18n.tr("Open")
                                 tip: browser.mode === "save"
                                      ? I18n.tr("write into this folder — then type a name")
                                      : I18n.tr("open the highlighted file")
                                 onTriggered: browser.mode === "save"
                                              ? browser.seedWrite(
                                                    (browser.dir === "/" ? "" : browser.dir) + "/")
                                              : browser.enter(browser.rows[browser.sel]) }
                    // Only in save mode, and only with a FILE highlighted —
                    // there is nothing to write over otherwise.
                    ToolButton { label: I18n.tr("Overwrite")
                                 visible: browser.mode === "save"
                                          && (browser.rows[browser.sel] || {}).type === "file"
                                 tip: I18n.tr("write over the highlighted file")
                                 onTriggered: browser.enter(browser.rows[browser.sel]) }
                    ToolButton { label: I18n.tr("Cancel"); tip: "Esc"
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
                        text: I18n.tr("Tasks")
                        font.family: root.uiFont
                        font.pixelSize: root.ui(14)
                        font.bold: true
                        color: root.cText
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.tasks.length === 0 ? ""
                              : I18n.trn("%1 of %2 done", "%1 of %2 done", root.tasks.length)
                                    .arg(tasksPanel.doneCount).arg(root.tasks.length)
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
                    text: I18n.tr("No tasks in this file.\n\nLines like  - [ ] something  are tasks.\n"
                                  + "Right-click ▸ New task adds one.")
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
                                  ? I18n.tr("(untitled task)") : taskRow.modelData.text
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
                    ToolButton { label: I18n.tr("New task")
                                 tip: I18n.tr("add a task at the caret")
                                 onTriggered: {
                                     tasksPanel.hide()
                                     root.actKeys("o- [ ] ")
                                 } }
                    ToolButton { label: I18n.tr("Refresh"); tip: I18n.tr("re-read the file")
                                 onTriggered: root.taskScan() }
                    ToolButton { label: I18n.tr("Close"); tip: "Esc"
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
                    text: I18n.tr("SYNAPSE Edit")
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
                        text: I18n.tr("Close")
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
        // ⛔ SET THIS false INSIDE A Flow OR A Column. A positioner REFUSES to
        // lay out a child that carries anchors — "Cannot specify anchors for
        // items inside Flow" — and the child then contributes NOTHING to the
        // positioner's implicit height. The unsaved-changes question rendered
        // its sentence and no buttons at all, in a bar sized to fit them: not
        // a clipped button, an invisible one. A Row happens to survive it,
        // which is why every existing caller does.
        property bool centered: true
        signal triggered()

        width: tbText.implicitWidth + 18
        height: Math.round(root.ui(26))
        radius: 4
        color: tb.active ? root.wash(0.3)
             : tbMa.containsMouse ? root.wash(0.16) : "transparent"
        anchors.verticalCenter: tb.centered && parent ? parent.verticalCenter
                                                      : undefined

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
