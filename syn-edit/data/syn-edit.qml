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
    readonly property color cAccent: readable(themed("accent", 167, 139, 250, 1.0), cPanel, 4.5)
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
            root.st = root.pending.st
            root.lines = root.pending.lines
            root.spans = root.pending.spans
            root.bufs = root.pending.bufs
            root.pending = ({ st: ({}), lines: [], spans: ({}), bufs: [] })
            root.engineUp = true
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
    readonly property int curLine: parseInt(root.st.line || "1")
    readonly property int curDcol: parseInt(root.st.dcol || "1")
    readonly property bool inserting: root.st.mode === "INSERT" || root.st.mode === "REPLACE"
    readonly property bool inCmd: (root.st.cmdline || "") !== ""

    Rectangle {
        id: shell
        anchors.fill: parent
        color: root.cBg

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
                    ToolButton { label: "Open"; tip: "type a path (:e)";    onTriggered: root.sendKeys(":e ") }
                    ToolButton { label: "Save"; tip: "write this buffer";   onTriggered: root.send("save") }
                    Rectangle { width: 1; height: Math.round(root.ui(20)); color: root.cDim; opacity: 0.4
                                anchors.verticalCenter: parent.verticalCenter }
                    ToolButton { label: "Undo"; tip: "u";                   onTriggered: root.sendKeys("u") }
                    ToolButton { label: "Redo"; tip: "Ctrl-R";              onTriggered: root.sendKeys("<C-r>") }
                    Rectangle { width: 1; height: Math.round(root.ui(20)); color: root.cDim; opacity: 0.4
                                anchors.verticalCenter: parent.verticalCenter }
                    ToolButton { label: "Find"; tip: "/";                   onTriggered: root.sendKeys("/") }
                    ToolButton { label: "Replace"; tip: ":%s/…/…/g";        onTriggered: root.sendKeys(":%s/") }
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
                const k = root.keyName(event)
                if (k !== "") {
                    root.sendKeys(k)
                    event.accepted = true
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: (m) => {
                    editor.forceActiveFocus()
                    // Clicking moves the cursor by asking the ENGINE to move
                    // it — as a line number and a column, not by setting a
                    // position this file keeps. :<n>| is "line n, column c".
                    const ln = root.top + Math.floor(m.y / root.lineH) + 1
                    const col = Math.max(1, Math.round((m.x - root.gutterW) / root.charW) + 1)
                    root.sendEx(String(ln))
                    root.sendKeys(String(col) + "|")
                }
                onWheel: (w) => {
                    // Three lines a notch, the usual step, expressed as KEYS
                    // so the engine's own view bookkeeping stays the only copy
                    // of where we are.
                    root.sendKeys(w.angleDelta.y > 0 ? "3<Up>" : "3<Down>")
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
