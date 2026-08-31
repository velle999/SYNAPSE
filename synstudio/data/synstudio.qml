// synstudio — the SynapseOS photo and video editor.
//
// A renderer, and nothing more. This file owns NO pixels, NO develop setting
// and NO timeline. Every value on screen came out of `synstudio get` or
// `synstudio timeline show`, every change goes back through `synstudio set`,
// `timeline set` or one of the edit verbs, and both pictures in the middle are
// PNGs the engine just wrote. The same is true of syn-edit and synfiles, and
// it is what makes the whole editor testable from tests/run.sh with no
// display.
//
// ── Two pages, one engine ──────────────────────────────────────────────────
//
// The darkroom develops a photograph; the cutting room cuts a timeline. They
// are separate pages because a still and a cut are different work with
// different tools on screen — but the Grade section of the clip inspector is
// the SAME table of controls the darkroom draws, applied to a clip. A slider
// moved there bakes an Iridas .cube and ffmpeg's lut3d applies it to every
// frame, so a still and a frame of video with the same settings come out the
// same colour by construction rather than by agreement.
//
// ── The panel builds ITSELF ────────────────────────────────────────────────
//
// The develop controls are not written out below. `synstudio keys` prints one
// row per setting with its group, its label, its slider range and its type,
// and the panel is built from that list at startup. Adding a control means
// adding a row to the table in src/develop.c; this file does not change, and
// cannot disagree with the engine about a limit. A GUI carrying its own copy
// of the ranges drifts, and the symptom is a slider that refuses at 90
// because the engine's real limit is 80.
//
// ── The viewport is NEUTRAL, on purpose ────────────────────────────────────
//
// Every other window in this suite takes its colours from the desktop theme,
// and so does the chrome here. The area immediately around the PICTURE does
// not: it is a fixed neutral grey. Colour judgement is relative, and a cyan
// accent behind a photograph pulls every white balance decision made against
// it. That is why Lightroom and Resolve are grey, and it is worth breaking
// the theme for.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

FloatingWindow {
    id: root

    // Nothing open yet is not "no photograph" — that names a half of the
    // program the start screen is deliberately not choosing between.
    title: (root.atStart ? ""
            : root.mode === "video"
            ? (root.proj ? root.proj.replace(/^.*\//, "") : "no project") + " — "
            : (root.file ? root.file.replace(/^.*\//, "") : "no photograph")
              + (root.dirty ? " •" : "") + " — ")
           + "SYNAPSE Studio"
    implicitWidth: 1400
    implicitHeight: 880
    minimumSize: Qt.size(900, 560)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNSTUDIO_BIN") || "synstudio"
    readonly property string scratch: "/tmp/synstudio-gui-" + Quickshell.env("USER")

    // ── The UI font ─────────────────────────────────────────────────────────
    //
    // The same ~/.config/synui/font.state every other window in the suite
    // watches. This one read it nowhere: not one Text named a family and all
    // hundred-and-seven pixel sizes were literals, so the darkroom kept
    // whatever face and size Qt resolved at startup and a font picked for the
    // desktop reached every app except this one.
    //
    // ⚠ BOTH HALVES HAVE TO BE BINDINGS. Qt resolves an application's default
    // font ONCE at startup and QML cannot change it afterwards, so naming the
    // family on every Text is the only way the face can change while the
    // window is open, and the size has to go through ui() for the same reason.
    // Doing one and not the other gives a window that follows the desktop
    // until somebody changes it.
    //
    // ⚠ TWO FAMILIES HERE ARE NOT THE DESKTOP'S AND MUST STAY. The literal
    // "monospace" ones are values to read and type — a filter expression, a
    // path — and the font PICKER draws each row in the face it names, which is
    // the whole point of the list.
    property string uiFont: ""

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
            // The scale lives in the same file because it is a property of the
            // DESKTOP, not of this window.
            const sc = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            root.textScale = sc ? parseInt(sc[1]) : 100
        }
        onLoadFailed: { root.uiFont = ""; root.textScale = 100 }
    }

    property int textScale: 100
    function ui(px) { return Math.max(6, Math.round(px * root.textScale / 100)) }

    property string file: Quickshell.env("SYNSTUDIO_OPEN") || ""
    property bool   dirty: false
    property string status: Quickshell.env("SYNSTUDIO_PROJECT") ? "" : "open a photograph"
    property int    imgW: 0
    property int    imgH: 0

    // ── Theme, the same contract every other window in the suite reads ──────
    property var p: ({})
    readonly property bool isLight: p.scheme === "light"

    property FileView themeFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: { try { root.p = JSON.parse(this.text()) } catch (e) { root.p = ({}) } }
        onLoadFailed: root.p = ({})
    }

    // ⚠ `ok` AND `use` both have to hold: `ok` is whether the PICTURE offered a
    // hue (a greyscale wallpaper does not), `use` is whether the setting asked
    // for one. Reading the colour without checking both is how the bar came to
    // wear the wallpaper on themes that never requested it.
    property string wpAccent: ""
    property FileView wpPaletteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/palette.state"
        watchChanges: true
        printErrors: false
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
        const la = lum(a), lb = lum(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    function readable(c, on, want) {
        if (contrast(c, on) >= want) return c
        const up = lum(on) <= 0.18
        let out = c
        for (let i = 0; i < 16; i++) {
            out = up ? Qt.lighter(out, 1.25) : Qt.darker(out, 1.25)
            if (contrast(out, on) >= want) return out
        }
        return up ? "#ffffff" : "#000000"
    }

    readonly property color cPanel: themed("bar", 11, 11, 20, 1.0)
    readonly property color cBg:    isLight ? Qt.lighter(cPanel, 1.15) : Qt.darker(cPanel, 1.4)
    readonly property color cInk:   p.fg ? Qt.color(p.fg) : pick("#e6e9ef", "#12141a")
    readonly property color cText:  contrast(cInk, cBg) >= 4.5 ? cInk
                                    : (lum(cBg) > 0.18 ? "#12141a" : "#e6e9ef")
    readonly property color cDim:   pick("#8b93a7", "#4a5568")
    readonly property color cAccentRaw: root.wpAccent !== "" ? Qt.color(root.wpAccent)
                                                             : themed("accent", 78, 201, 176, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)
    readonly property color cBad:    readable(pick("#f7768e", "#a01030"), cBg, 4.5)
    function wash(a) { return Qt.rgba(cAccent.r, cAccent.g, cAccent.b, a) }

    // The one colour that does NOT follow the theme. See the note at the top.
    readonly property color cViewport: "#3a3a3a"

    function say(s) { root.status = s }

    // ── The setting table, read from the engine ─────────────────────────────
    //
    // rows: [{ key, value, lo, hi, type, group, label, hardLo, hardHi }]
    property var rows: []
    property var groups: []

    function parseKeys(text) {
        const out = [], seen = [], byGroup = ({})
        const lines = text.split("\n")
        for (let i = 0; i < lines.length; i++) {
            if (!lines[i]) continue
            const f = lines[i].split("\t")
            if (f.length < 9) continue
            const r = { key: f[0], value: f[1], lo: parseFloat(f[2]), hi: parseFloat(f[3]),
                        type: f[4], group: f[5], label: f[6],
                        hardLo: parseFloat(f[7]), hardHi: parseFloat(f[8]) }
            out.push(r)
            if (!byGroup[r.group]) { byGroup[r.group] = true; seen.push(r.group) }
        }
        root.groups = seen
        return out
    }

    function rowsIn(group) {
        const out = []
        for (let i = 0; i < root.rows.length; i++)
            if (root.rows[i].group === group) out.push(root.rows[i])
        return out
    }

    // The values live BESIDE the rows, not in them.
    //
    // `rows` is what the panel is built from and it must not change while a
    // hand is on a slider. It used to carry the values too, so every tick of a
    // drag rebuilt the array — and a Repeater whose JS-array model is
    // reassigned REBUILDS ITS DELEGATES, which destroys the MouseArea holding
    // the mouse grab. Measured: of ten mouse moves, the slider received one,
    // and the delegate was destroyed on it. That is the whole of "the sliders
    // jump instead of sliding": the drag ended on the first move and the value
    // was whatever the press had set. preventStealing does not help, because
    // nothing stole it.
    //
    // Reassigning one element of a `var` map does NOT re-evaluate bindings on
    // it either — a documented quickshell trap in this repo — so `vals` is
    // rebuilt whole. Rebuilding a map of 64 numbers is free; rebuilding the
    // panel is not.
    property var vals: ({})

    function seedVals(rowList) {
        const out = ({})
        for (let i = 0; i < rowList.length; i++) out[rowList[i].key] = rowList[i].value
        return out
    }

    function valueOf(key) {
        return parseFloat(root.vals[key]) || 0
    }

    function setValue(key, v) {
        const next = ({})
        for (const k in root.vals) next[k] = root.vals[k]
        next[key] = String(v)
        root.vals = next
    }

    Process {
        id: keysProc
        command: [root.bin, "keys"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.rows = root.parseKeys(this.text)
                root.vals = root.seedVals(root.rows)
                if (root.file) root.loadFile(root.file)
            }
        }
        stderr: StdioCollector { onStreamFinished: if (this.text) root.say(this.text.split("\n")[0]) }
    }

    Process {
        id: getProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n")
                const next = ({})
                for (const k in root.vals) next[k] = root.vals[k]
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 2) continue
                    next[f[0]] = f[1]
                }
                root.vals = next
                root.requestRender()
            }
        }
    }

    Process {
        id: setProc
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(this.text.split("\n")[0])
        }
        // ⚠ The history depth is read when the write has FINISHED, not beside
        // it. Asking while the `set` is still running answers about the
        // sidecar as it was a moment ago — which is zero on the first edit, so
        // Undo stayed grey until the SECOND change and the first one could
        // never be taken back.
        onExited: function (code, status) { root.readDevHistory() }
    }

    // ── Rendering the preview ───────────────────────────────────────────────
    //
    // ⚠ Setting `running = true` on a Process that is ALREADY running is a
    // SILENT no-op in quickshell — the second request simply never happens.
    // Dragging a slider generates far more render requests than renders, so
    // an in-flight render records that another is wanted and starts it on the
    // way out. Without this the picture stops updating mid-drag and stays
    // wrong until the next unrelated change.
    property bool renderBusy: false
    property bool renderAgain: false
    property int  renderSerial: 0
    property string previewUrl: ""

    // A drag is judged at a smaller size than the export. Anything with a
    // radius — clarity, sharpening, grain — is scaled to the frame in the
    // engine, so the smaller preview is the same LOOK and not a different one.
    readonly property int dragSize: 1100
    readonly property int restSize: 2200
    property bool dragging: false

    Timer {
        id: debounce
        interval: 90
        onTriggered: root.startRender()
    }

    function requestRender() {
        if (!root.file) return
        // Same as the monitor's: while a slider is under the hand, a restarting
        // debounce means the preview does not move until it is let go. The
        // render is already coalesced against itself, so ask straight away and
        // let it keep up at whatever rate it can.
        if (root.dragging) { debounce.stop(); root.startRender(); return }
        debounce.restart()
    }

    function startRender() {
        if (!root.file) return
        if (root.renderBusy) { root.renderAgain = true; return }
        root.renderBusy = true
        root.renderSerial++
        renderProc.command = [root.bin, "render", root.file,
                              "--out", root.scratch + "-preview.png",
                              "--size", String(root.dragging ? root.dragSize : root.restSize),
                              "--bits", "8"]
        renderProc.running = true
    }

    Process {
        id: renderProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f[0] === "width")  root.imgW = parseInt(f[1])
                    if (f[0] === "height") root.imgH = parseInt(f[1])
                }
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(this.text.split("\n")[0])
        }
        onExited: function (exitCode, exitStatus) {
            root.renderBusy = false
            // Qt caches an Image by its URL, and the file behind this one
            // changes without the path changing. The serial in the query
            // string is what makes the reload happen at all.
            root.previewUrl = "file://" + root.scratch + "-preview.png?v=" + root.renderSerial
            if (!root.dragging) histTimer.restart()
            if (root.renderAgain) { root.renderAgain = false; root.startRender() }
        }
    }

    // ── Histogram ───────────────────────────────────────────────────────────
    property var hist: []
    property int histMax: 1
    property int clipLo: 0
    property int clipHi: 0

    Timer { id: histTimer; interval: 200; onTriggered: root.loadHistogram() }

    function loadHistogram() {
        if (!root.file || histProc.running) return
        histProc.command = [root.bin, "histogram", root.file, "--size", "500"]
        histProc.running = true
    }

    Process {
        id: histProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n"), bins = []
                let mx = 1
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f[0] === "clipped_black") root.clipLo = parseInt(f[1])
                    else if (f[0] === "clipped_white") root.clipHi = parseInt(f[1])
                    else if (f[0] === "bin") {
                        const b = { r: +f[2], g: +f[3], b: +f[4], l: +f[5] }
                        if (b.r > mx) mx = b.r
                        if (b.g > mx) mx = b.g
                        if (b.b > mx) mx = b.b
                        bins.push(b)
                    }
                }
                root.histMax = mx
                root.hist = bins
                histCanvas.requestPaint()
            }
        }
    }

    // ── Opening and saving ──────────────────────────────────────────────────

    function loadFile(f) {
        root.file = f
        root.dirty = false
        root.say("reading " + f.replace(/^.*\//, ""))
        // ⚠ Asked on OPEN, not only after an edit. A photograph carries its
        // history beside it, so a picture with fifty steps behind it must not
        // come up with a dead Undo button — the same trap the cutting room's
        // history had.
        root.readDevHistory()
        // The thumbnail layout rides in the same sidecar, so it arrives with
        // the photograph rather than the first time the panel is opened.
        root.loadThumb()
        getProc.command = [root.bin, "get", f]
        getProc.running = true
    }

    function change(key, v) {
        root.setValue(key, v)
        root.dirty = true
        // ⚠ setProc may still be running from the last slider tick. The engine
        // is the source of truth and each `set` is a complete transaction on
        // the sidecar, so a dropped intermediate value is harmless — but the
        // LAST one must land, which is what the render debounce below and the
        // final set on release guarantee.
        //
        // ⚠ And a tick DURING a drag is not recorded in the history. Undo has
        // to step by gesture: recording each tick would put a hundred stops of
        // one slider in a hundred-deep ring, so Ctrl+Z would walk back through
        // a drag a hundredth of a stop at a time and the edit before it would
        // be gone. The release commits the same value again, without the flag.
        setProc.command = [root.bin, "set", root.file, key + "=" + v]
                          .concat(root.dragging ? ["--no-history"] : [])
        setProc.running = true
        root.requestRender()
    }

    // ── The thumbnail ───────────────────────────────────────────────────────
    //
    // A thumbnail is a SECOND picture made from the same photograph: a fixed
    // canvas, the developed frame framed into it, and a few words big enough
    // to read at the size a thumbnail is actually seen. It belongs to the
    // photograph — it rides in the same sidecar — so reopening the file a
    // month later brings the layout back with it.
    //
    // Every row in this panel comes from `thumb keys`, which is one table in
    // src/thumb.c. Nothing about a thumbnail is decided here.
    property bool   thumbOpen: false
    property var    thumbRows: []
    property var    thumbGroups: []
    property string thumbUrl: ""
    property int    thumbSerial: 0
    property bool   thumbBusy: false
    property bool   thumbAgain: false

    function thumbValue(key) {
        for (let i = 0; i < root.thumbRows.length; i++)
            if (root.thumbRows[i].key === key) return root.thumbRows[i].value
        return ""
    }

    function loadThumb() {
        if (!root.file) { root.thumbRows = []; return }
        thumbKeysProc.running = false
        thumbKeysProc.running = true
    }

    Process {
        id: thumbKeysProc
        command: [root.bin, "thumb", root.file, "keys"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [], seen = []
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    if (!lines[i]) continue
                    const f = lines[i].split("\t")
                    if (f.length < 7) continue
                    const r = { key: f[0], value: f[1],
                                lo: parseFloat(f[2]), hi: parseFloat(f[3]),
                                type: f[4], group: f[5], label: f[6],
                                choices: (f[7] || "") ? f[7].split("|") : [] }
                    out.push(r)
                    if (seen.indexOf(r.group) < 0) seen.push(r.group)
                }
                root.thumbGroups = seen
                root.thumbRows = out
                if (root.thumbOpen) root.requestThumb()
            }
        }
    }

    function setThumb(key, v) {
        if (!root.file) return
        thumbSetProc.command = [root.bin, "thumb", root.file, "set", key + "=" + v]
        thumbSetProc.running = true
    }

    Process {
        id: thumbSetProc
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(this.text.split("\n")[0])
        }
        // ⚠ The table is re-read when the write has FINISHED. The engine owns
        // the document — it clamps a value, resolves an enum name and turns
        // any setting at all into `on` — so what the panel shows has to come
        // back from it rather than from what was typed.
        onExited: function (code, status) { root.loadThumb() }
    }

    // The preview is the SAME command the export runs, at a smaller size:
    // what is on screen is what will be written, which is the whole reason
    // the render lives in the engine.
    function requestThumb() {
        if (!root.file || !root.thumbOpen) return
        if (root.thumbBusy) { root.thumbAgain = true; return }
        root.thumbBusy = true
        root.thumbSerial++
        thumbProc.command = [root.bin, "thumb", root.file, "render",
                             "--out", root.scratch + "-thumb.png",
                             "--size", "1400"]
        thumbProc.running = true
    }

    Process {
        id: thumbProc
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(this.text.split("\n")[0])
        }
        onExited: function (code, status) {
            root.thumbBusy = false
            if (code === 0)
                root.thumbUrl = "file://" + root.scratch + "-thumb.png?v="
                                + root.thumbSerial
            if (root.thumbAgain) { root.thumbAgain = false; root.requestThumb() }
        }
    }

    function resetThumb() {
        if (!root.file) return
        thumbSetProc.command = [root.bin, "thumb", root.file, "reset"]
        thumbSetProc.running = true
    }

    // ── Undo in the darkroom ────────────────────────────────────────────────
    //
    // The same three answers the cutting room gives, off the same machinery:
    // a photograph's document is its sidecar, history is a property of a file,
    // and `undo FILE` steps it. Nothing here knows what an edit WAS — the
    // engine moves whole documents, exactly as it does for a project.
    property int devUndo: 0
    property int devRedo: 0

    function readDevHistory() {
        if (!root.file) { root.devUndo = 0; root.devRedo = 0; return }
        devHistProc.running = false
        devHistProc.running = true
    }

    Process {
        id: devHistProc
        command: [root.bin, "history", root.file]
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f[0] === "undo") root.devUndo = parseInt(f[1]) || 0
                    if (f[0] === "redo") root.devRedo = parseInt(f[1]) || 0
                }
            }
        }
    }

    function devStep(verb) {
        if (!root.file) return
        if (verb === "undo" ? root.devUndo <= 0 : root.devRedo <= 0) return
        devStepProc.command = [root.bin, verb, root.file]
        devStepProc.running = true
    }

    Process {
        id: devStepProc
        onExited: function (code, status) {
            // The sliders, the masks and the picture all come off the sidecar,
            // and the engine has just replaced it wholesale — so this reloads
            // the file rather than trying to work out what moved.
            root.readDevHistory()
            root.loadFile(root.file)
        }
    }

    // ── The picker ──────────────────────────────────────────────────────────
    //
    // Its rows come from `synstudio browse`, not from a file manager. The
    // first version of this button ran `synfiles --pick image` on the
    // assumption that the house file browser had a chooser mode. It does not —
    // synfiles has no --pick at all — so the button spawned a process that
    // exited with a usage error and the window said "nothing chosen" forever.
    // Delegating is only free when the thing delegated to actually offers the
    // verb; check the --help, not the vibe.
    //
    // The engine lists it because the engine is the only thing that knows what
    // it can decode. A row that is drawn is a row that will open.

    property bool   pickerOpen: false
    property string pickerDir: ""
    property var    pickerRows: []

    // A picker opened to find a PROJECT should not offer photographs, and one
    // opened to add a clip should not offer projects. Same listing from the
    // engine, filtered by what the page that asked can do with a row —
    // because a row that is drawn is still a row that will be clicked.
    readonly property var pickerShown: {
        const out = []
        for (let i = 0; i < root.pickerRows.length; i++) {
            const r = root.pickerRows[i]
            if (r.kind === "dir" || r.kind === "up") { out.push(r); continue }
            if (root.pickerFor === "project") { if (r.kind === "project") out.push(r) }
            // A .cube is not a picture and not a clip, so it appears in this
            // one mode and nowhere else. It is listed at all for the reason a
            // project is: the engine cannot DECODE either, and a picker that
            // only offered what it can decode left both openable by typing a
            // path and no other way.
            else if (root.pickerFor === "lut") { if (r.kind === "look") out.push(r) }
            // The darkroom develops pictures. A sound file has none, so Open
            // must not offer one — it would load nothing and say nothing.
            else if (root.pickerFor === "photo") {
                if (r.kind === "image" || r.kind === "video") out.push(r)
            }
            else if (r.kind !== "project" && r.kind !== "look") out.push(r)
        }
        return out
    }
    // The fallback to $HOME must fire at most once. Retrying on every failure
    // is an infinite respawn loop the moment $HOME itself is unreadable.
    property bool   pickerFellBack: false
    // Which page asked. The same list of openable files serves the darkroom's
    // Open and the timeline's Add media; only what happens on click differs.
    property string pickerFor: "photo"

    function openPicker() {
        // Start where the current photograph is, or in Pictures, or at home —
        // browse resolves and reports where it actually landed, so a guess
        // that does not exist costs one failed row rather than a broken state.
        let start = root.pickerDir
        if (!start)
            start = root.file ? root.file.replace(/\/[^\/]*$/, "")
                              : Quickshell.env("HOME") + "/Pictures"
        root.pickerOpen = true
        root.pickerFellBack = false
        root.browseTo(start)
    }

    function browseTo(dir) {
        browseProc.command = [root.bin, "browse", dir]
        browseProc.running = true
    }

    Process {
        id: browseProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n").filter(l => l.length > 0)
                if (lines.length === 0) {
                    root.fallbackHome()
                    return
                }
                const rows = []
                for (const l of lines) {
                    const c = l.split("\t")
                    if (c.length < 3) continue
                    if (c[0] === ".") { root.pickerDir = c[2]; continue }
                    rows.push({ kind: c[0], name: c[1], path: c[2] })
                }
                root.pickerRows = rows
            }
        }
        onExited: function (exitCode, exitStatus) {
            if (exitCode !== 0) root.fallbackHome()
        }
    }

    // Pictures may simply not exist on a fresh install, which is the ordinary
    // case and not an error worth showing.
    function fallbackHome() {
        if (root.pickerFellBack) {
            root.say("cannot read that folder")
            return
        }
        root.pickerFellBack = true
        root.browseTo(Quickshell.env("HOME") || "/")
    }

    Process { id: exportProc
        stderr: StdioCollector { onStreamFinished: if (this.text) root.say(this.text.split("\n")[0]) }
        onExited: function (exitCode, exitStatus) {
            root.exportingStill = false
            root.say(exitCode === 0 ? "exported " + root.exportOut : "export failed")
        }
    }

    // ── Export as ───────────────────────────────────────────────────────────
    //
    // Export used to invent the name and the format: a photograph became
    // `<name>-edited.jpg` and a cut became `<project>.mp4`, with no way to say
    // otherwise short of renaming the result afterwards. Both are now chosen
    // before anything is encoded.
    //
    // The format lists come from the ENGINE — `formats` and `timeline formats`
    // — for the same reason the develop panel is built from `keys`: a list
    // written out here would drift, and the first thing anybody would notice
    // is a choice that fails at the end of a long encode rather than at the
    // moment it was offered.
    property bool   exportOpen: false
    property string exportName: ""
    property int    exportFmt: 0
    property bool   exportingStill: false
    // Which of the three things this window can write is being asked for.
    // "" is the page's own answer — a still in the darkroom, the cut in the
    // cutting room — and "thumb" is the thumbnail panel asking for its own.
    property string exportKind: ""

    // ── Naming a project ────────────────────────────────────────────────────
    //
    // There is no Save button and there would be nothing for one to do: every
    // verb this window runs ends in a write, so the .syntl on disk is the cut
    // as it stands after each edit, and closing the window has never lost a
    // frame of it. What was actually missing was a NAME. The window started
    // every project at ONE fixed path, so the second project quietly took the
    // first one's place, and there was no way to say "this one is Holiday" or
    // "keep a copy of it here".
    //
    // So: Save as, and a New project that asks for a name. One sheet for
    // both. Neither writes over a file already there on the first press — the
    // engine answers exit 3 for "that name is taken", which is an ANSWER, and
    // the sheet turns it into a Replace button rather than an error.
    property bool   saveOpen: false
    property string saveWhat: "as"       // "as" — a copy under a name; "new" — start one
    property string saveName: ""
    property string saveDir: ""
    property bool   saveReplace: false   // taken, and the user has been told
    property bool   saveBusy: false
    readonly property string savePath: root.saveDir + "/" + root.saveName + ".syntl"

    function openSaveAs() {
        if (!root.proj) return
        root.saveWhat = "as"
        root.saveDir  = root.proj.replace(/\/[^\/]*$/, "")
                        || (Quickshell.env("HOME") || "/tmp")
        root.saveName = root.proj.replace(/^.*\//, "").replace(/\.[^.]*$/, "")
        root.saveReplace = false
        root.saveOpen = true
    }

    function openNewProject() {
        root.saveWhat = "new"
        // Beside the project already open, because that is where this one's
        // footage almost certainly is.
        root.saveDir  = root.proj ? root.proj.replace(/\/[^\/]*$/, "")
                                  : (Quickshell.env("HOME") || "/tmp")
        root.saveName = "project"
        root.saveReplace = false
        root.saveOpen = true
    }

    function doSave() {
        if (!root.saveName || root.saveBusy) return
        root.saveBusy = true
        root.mode = "video"
        if (root.saveWhat === "new") {
            root.newProject(root.savePath, !root.saveReplace)
        } else {
            saveAsProc.command = [root.bin, "timeline", "saveas", root.proj,
                                  "--out", root.savePath]
                                 .concat(root.saveReplace ? ["--force"] : [])
            saveAsProc.running = true
        }
    }

    Process {
        id: saveAsProc
        property string got: ""
        stdout: StdioCollector { onStreamFinished: saveAsProc.got = this.text.trim() }
        onExited: function (code, status) {
            const out = saveAsProc.got
            saveAsProc.got = ""
            root.saveBusy = false
            if (code === 3) {
                root.saveReplace = true
                root.say("a project called that is there already — Replace writes over it")
                return
            }
            if (code !== 0 || !out) { root.say("cannot save it there"); return }
            root.saveOpen = false
            root.saveReplace = false
            // From here on THIS is the project being edited. A Save as that
            // leaves you editing the old file has made a copy, not a save.
            root.proj = out
            root.selClip = -1
            root.tlRev++
            root.reloadTimeline()
            root.say("saved as " + out.replace(/^.*\//, ""))
        }
    }

    // ── What is on the clipboard ────────────────────────────────────────────
    //
    // The clipboard is a FILE under ~/.config, so it outlives this window, the
    // project it was filled from and the session. A window that remembered its
    // own last copy instead would offer Paste after being restarted with
    // nothing on it, and refuse Paste with a clip sitting right there from the
    // project open next door. So it asks the engine — `timeline clipboard`,
    // which needs no project because the question is not about a cut.
    property string cbKind: ""
    property real   cbLen: 0

    // ⚠ running = true on a process already running is a silent no-op, and the
    // answer that mattered would be the one never asked for.
    function readClipboard() {
        cbProc.running = false
        cbProc.running = true
    }

    Process {
        id: cbProc
        command: [root.bin, "timeline", "clipboard"]
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n")
                let kind = "", len = 0
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f[0] === "kind")   kind = f[1]
                    if (f[0] === "length") len = parseFloat(f[1])
                }
                root.cbKind = kind
                root.cbLen = len
            }
        }
    }

    function copyClip() {
        if (root.selClip < 0) return
        // ⚠ The clipboard is re-read when the copy has RUN, not here: edits
        // QUEUE, so at this point the file on disk is still whatever was
        // copied last — which is how Paste comes to offer the clip before the
        // one somebody just picked. tlSetProc's exit does it.
        root.tlRun(["copy", root.proj, String(root.selTrack), String(root.selClip)])
    }

    function pasteClip() {
        if (!root.proj || root.selTrack < 0 || root.cbKind === "") return
        // At the PLAYHEAD, which is where a paste is looking. --at 0 is a
        // position like any other, and the engine tells the two apart now.
        root.tlRun(["paste", root.proj, String(root.selTrack),
                    "--at", String(root.playhead)])
    }

    // Monitoring level for the preview player. Not persisted and not in the
    // document: it is a property of this room, not of the cut.
    property real  monVolume: 1.0
    property bool  monMuted: false
    onMonVolumeChanged: root.pushMonitor()
    onMonMutedChanged: root.pushMonitor()
    function pushMonitor() {
        if (!root.playbackReady) return
        playbackLoader.item.volume = root.monVolume
        playbackLoader.item.muted = root.monMuted
    }
    property var    vidFormats: []
    property var    stillFormats: []

    readonly property var exportFormats:
        root.mode === "video" ? root.vidFormats : root.stillFormats

    readonly property string exportSrc: root.mode === "video" ? root.proj : root.file
    readonly property string exportDir:
        root.exportSrc.replace(/\/[^\/]*$/, "") || "/"
    readonly property string exportExt:
        (root.exportFmt >= 0 && root.exportFmt < root.exportFormats.length)
        ? root.exportFormats[root.exportFmt].ext : ""
    readonly property string exportPath:
        root.exportDir + "/" + root.exportName + "." + root.exportExt

    function parseFormats(text) {
        const out = []
        const lines = text.split("\n")
        for (let i = 0; i < lines.length; i++) {
            if (!lines[i]) continue
            const f = lines[i].split("\t")
            if (f.length < 3) continue
            out.push({ name: f[0], ext: f[1], label: f[2] })
        }
        return out
    }

    Process {
        id: vidFmtProc
        command: [root.bin, "timeline", "formats"]
        running: true
        stdout: StdioCollector { onStreamFinished: root.vidFormats = root.parseFormats(this.text) }
    }
    Process {
        id: stillFmtProc
        command: [root.bin, "formats"]
        running: true
        stdout: StdioCollector { onStreamFinished: root.stillFormats = root.parseFormats(this.text) }
    }

    function openExport() {
        if (!root.exportSrc) return
        // The source's own name, without its extension and without the
        // directory — the one part somebody actually wants to edit.
        const base = root.exportSrc.replace(/^.*\//, "").replace(/\.[^.]*$/, "")
        root.exportName = root.exportKind === "thumb" ? base + "-thumb"
                          : root.mode === "video" ? base : base + "-edited"
        root.exportFmt = 0
        root.exportOpen = true
    }

    function doExport() {
        if (!root.exportName || root.exportFormats.length === 0) return
        const fmt = root.exportFormats[root.exportFmt]
        const out = root.exportPath
        root.exportOpen = false
        // A thumbnail is written by the SAME command the preview runs, at the
        // full canvas size — which is what makes the panel a preview rather
        // than an impression.
        if (root.exportKind === "thumb") {
            root.exportKind = ""
            if (root.exportingStill) return
            root.exportingStill = true
            root.exportOut = out
            exportProc.command = [root.bin, "thumb", root.file, "render",
                                  "--out", out, "--quality", "95"]
            exportProc.running = true
            root.say("writing the thumbnail…")
            return
        }
        root.exportKind = ""
        if (root.mode === "video") {
            if (root.exportingCut) return
            root.exportingCut = true
            root.exportPct = -1
            root.exportErr = ""
            root.exportOut = out
            tlExportProc.command = [root.bin, "timeline", "export", root.proj,
                                    "--out", out, "--format", fmt.name]
            tlExportProc.running = true
            root.say("exporting the cut…")
        } else {
            if (root.exportingStill) return
            root.exportingStill = true
            root.exportOut = out
            exportProc.command = [root.bin, "render", root.file,
                                  "--out", out, "--quality", "95"]
            exportProc.running = true
            root.say("exporting…")
        }
    }

    // ── Dropping files on the window ────────────────────────────────────────
    //
    // What a dropped file IS gets asked of the engine, one file at a time
    // (`synstudio kind`), rather than guessed from its extension: a drop is a
    // deliberate gesture on a handful of files, so it can afford the process
    // that the directory listing cannot, and it accepts formats no list of
    // ours has ever heard of. A sound goes to an audio track, a picture to a
    // video track, a `.syntl` opens as a project, and a photograph dropped on
    // the darkroom opens there.
    property var dropQueue: []
    property bool dropBusy: false

    // ── Carrying the photograph to the Video tab ────────────────────────────
    //
    // Where the hand is, in the picture pane's own coordinates, and what
    // happens when it lets go. Both are plain functions so a test can drive
    // the whole gesture — a real drag cannot be synthesised without a seat,
    // and every previous version of this was "tested" by a harness that set
    // the very thing the bug was in.
    property real photoDragX: 0
    property real photoDragY: 0
    property bool photoCarrying: false

    function photoDragTo(x, y) {
        root.photoDragX = x
        root.photoDragY = y
    }

    // Whether the pointer is on the Video tab, in the picture pane's
    // coordinates. Six pixels of margin, because a tab is a small thing to
    // hit and the drop is forgiving anyway.
    function photoOverTab(x, y) {
        if (!videoTab.visible) return false
        const p = photoDrag.mapToItem(videoTab, x, y)
        return p.x >= -6 && p.y >= -6
               && p.x <= videoTab.width + 6 && p.y <= videoTab.height + 6
    }

    // ⚠ Anywhere OUT of the picture is the gesture; back onto the picture is
    // a mis-click. A 56-pixel tab is not a fair target, and a drop that does
    // nothing because it missed by four pixels is the same complaint as a
    // drop that does nothing at all — but answering a stray drag on the photo
    // by adding a clip and swapping the page under the hand is worse than
    // ignoring it.
    function photoDropAt(x, y) {
        if (!root.file) return
        if (x >= 0 && y >= 0 && x <= photoDrag.width && y <= photoDrag.height) {
            root.say("drag it up to the Video tab to put it in the cut")
            return
        }
        root.mode = "video"
        root.dropUrls(["file://" + encodeURI(root.file)])
    }

    // One attempt at starting a project for a dropped file, so a drop that
    // cannot be taken says so once instead of asking the engine for a new
    // project on every pump.
    property bool autoProjTried: false

    function dropUrls(urls) {
        const q = []
        for (let i = 0; i < urls.length; i++) {
            let u = String(urls[i])
            if (u.indexOf("file://") !== 0) continue
            // A path with a space arrives percent-encoded, and handing that
            // to the engine looks for a file with %20 in its name.
            q.push(decodeURIComponent(u.substring(7)))
        }
        if (q.length === 0) { root.say("nothing droppable there"); return }
        root.dropQueue = root.dropQueue.concat(q)
        root.pumpDrop()
    }

    // The engine takes one edit at a time and `running = true` on a busy
    // Process is a silent no-op, so a batch of dropped files has to wait its
    // turn rather than race — otherwise all but the first vanish.
    Timer {
        interval: 150; repeat: true
        running: root.dropQueue.length > 0
        onTriggered: root.pumpDrop()
    }

    function pumpDrop() {
        if (root.dropBusy || root.dropQueue.length === 0) return
        if (tlSetProc.running || addTrackProc.running) return
        root.dropBusy = true
        kindProc.path = root.dropQueue[0]
        kindProc.command = [root.bin, "kind", kindProc.path]
        kindProc.running = true
    }

    Process {
        id: kindProc
        property string path: ""
        property string answer: ""
        stdout: StdioCollector { onStreamFinished: kindProc.answer = this.text.trim() }
        onExited: function (code, status) {
            const path = kindProc.path
            const kind = kindProc.answer
            kindProc.answer = ""
            root.dropQueue = root.dropQueue.slice(1)
            root.dropBusy = false

            if (kind === "project") {
                root.mode = "video"
                root.proj = path
                root.selTrack = 0
                root.selClip = -1
                root.playhead = 0
                root.tlRev++
                root.reloadTimeline()
                root.say("opened " + path.replace(/^.*\//, ""))
            } else if (kind === "image" && root.mode === "photo") {
                root.loadFile(path)
            } else if (kind === "image" || kind === "video" || kind === "audio") {
                if (!root.proj) {
                    // "start a project first" is an instruction to go and do
                    // the thing the drop already asked for. Start one, put the
                    // file back at the head of the queue, and let the project
                    // coming up pump it — which is why this returns rather
                    // than falling through to the pump below.
                    if (root.autoProjTried) {
                        root.say("cannot start a project for that")
                    } else {
                        root.autoProjTried = true
                        root.mode = "video"
                        root.dropQueue = [path].concat(root.dropQueue)
                        root.newProjectUnique((Quickshell.env("HOME") || "/tmp")
                                              + "/synstudio-project.syntl")
                        return
                    }
                } else {
                    root.mode = "video"
                    root.autoProjTried = false
                    root.addMedia(path, kind)
                    root.say("added " + path.replace(/^.*\//, ""))
                }
            } else {
                root.say("nothing this engine can open: "
                         + path.replace(/^.*\//, ""))
            }
            root.pumpDrop()
        }
    }

    // ══ The video page ══════════════════════════════════════════════════════
    //
    // The same contract as the darkroom: this file holds no timeline and no
    // clip property. `timeline show` is the document, `timeline set` and the
    // edit verbs are the only way it changes, and the picture in the monitor
    // is a PNG `timeline frame` just wrote. Everything below is a renderer
    // over those, which is why the whole video editor is testable from
    // tests/run.sh with no display.

    // Launched on a project file, the window opens on the page that can edit
    // it. Coming up in the darkroom with a timeline loaded behind a tab
    // nobody pressed is the same bug as coming up empty.
    // The side panels give ground on a narrow window. 340 fixed took more than
    // half of a 620-wide one, leaving the picture — the thing both pages are
    // FOR — smaller than the controls describing it. It never grows past 340,
    // because a slider column wider than that is just a wider slider.
    readonly property int panelW:
        Math.round(Math.min(340, Math.max(215, root.width * 0.32)))

    property string mode: Quickshell.env("SYNSTUDIO_PROJECT") ? "video" : "photo"
    property string proj: Quickshell.env("SYNSTUDIO_PROJECT") || ""
    property var    tl: ({ w: 1920, h: 1080, fps: 25, tracks: [] })
    property real   tlDur: 0
    property real   playhead: 0
    property int    selTrack: -1
    property int    selClip: -1
    property real   pxPerSec: 70

    // Selection and the values on screen are ONE thing. Calling loadClip() at
    // each place that happens to change the selection leaves the inspector
    // showing the last clip's numbers whenever a new path appears — after a
    // split, after a delete renumbers the track, on opening a project. Hang it
    // off the change instead and there is no such path.
    onSelClipChanged:  root.loadClip()
    onSelTrackChanged: root.loadClip()

    readonly property var selClipObj: {
        if (root.selTrack < 0 || root.selTrack >= root.tl.tracks.length) return null
        const tr = root.tl.tracks[root.selTrack]
        if (root.selClip < 0 || root.selClip >= tr.clips.length) return null
        return tr.clips[root.selClip]
    }

    // ── The document ────────────────────────────────────────────────────────
    //
    // `timeline show` is tab-separated and line-oriented, and a grade is a
    // block between `grade` and `endgrade` belonging to the clip above it.
    function parseTimeline(text) {
        const doc = { w: 1920, h: 1080, fps: 25, master: 0,
                      markers: [], tracks: [] }
        const lines = text.split("\n")
        let tr = null, cl = null, inGrade = false, inKey = false
        let dur = 0
        for (let i = 0; i < lines.length; i++) {
            const f = lines[i].split("\t")
            if (inGrade) {
                if (f[0] === "endgrade" || f[0] === "endkey") {
                    inGrade = false; inKey = false; continue
                }
                if (cl && f.length >= 2) {
                    if (inKey) cl.keys[cl.keys.length - 1].grade[f[0]] = f[1]
                    else       cl.grade[f[0]] = f[1]
                }
                continue
            }
            switch (f[0]) {
            case "size": doc.w = parseInt(f[1]); doc.h = parseInt(f[2]); break
            case "fps":  doc.fps = parseFloat(f[1]); break
            case "track":
                tr = { type: f[1], name: f[2], muted: f[3] === "1",
                       hidden: f[4] === "1",
                       gain: 0, pan: 0, solo: false, clips: [] }
                doc.tracks.push(tr)
                cl = null
                break
            // Only written when it differs from a flat fader, so its absence
            // is what an unmixed track looks like.
            case "mix":
                if (tr) {
                    tr.gain = parseFloat(f[1]) || 0
                    tr.pan = parseFloat(f[2]) || 0
                    tr.solo = f[3] === "1"
                }
                break
            case "master": doc.master = parseFloat(f[1]) || 0; break
            case "marker":
                doc.markers.push({ t: parseFloat(f[1]) || 0,
                                   colour: parseInt(f[2]) || 0,
                                   text: f[3] || "" })
                break
            case "clip":
                if (!tr) break
                cl = { tlIn: parseFloat(f[1]), srcIn: parseFloat(f[2]),
                       srcOut: parseFloat(f[3]), speed: parseFloat(f[4]) || 1,
                       gain: parseFloat(f[5]), opacity: parseFloat(f[6]),
                       fadeIn: parseFloat(f[7]), fadeOut: parseFloat(f[8]),
                       path: f[9] || "", kind: "media", still: false,
                       text: "", trans: "none", graded: false, grade: ({}), keys: [],
                       anim: ({}), animAll: [], fx: [] }
                cl.len = (cl.srcOut - cl.srcIn) / (cl.speed > 0 ? cl.speed : 1)
                tr.clips.push(cl)
                if (cl.tlIn + cl.len > dur) dur = cl.tlIn + cl.len
                break
            // One line per effect, in the order they apply, with the knobs
            // by name. An effect this machine has not got still appears —
            // it is in the document and it is not this window's to drop.
            case "fx":
                if (cl) {
                    const p = ({})
                    for (let q = 3; q < f.length; q++) {
                        const eq = f[q].indexOf("=")
                        if (eq > 0) p[f[q].slice(0, eq)] = parseFloat(f[q].slice(eq + 1))
                    }
                    cl.fx.push({ name: f[1], on: f[2] === "1", param: p })
                }
                break
            // One line per parameter key: a property, a time, a value and how
            // it leaves. Unlike a grade key there is no block to enter.
            case "anim":
                if (cl) {
                    if (!cl.anim[f[1]]) cl.anim[f[1]] = []
                    cl.anim[f[1]].push({ t: parseFloat(f[2]) || 0,
                                         v: parseFloat(f[3]) || 0,
                                         ease: f[4] || "linear" })
                    // Flat as well as grouped: the clip bar draws every key
                    // it has, and a Repeater cannot walk a map.
                    cl.animAll.push({ key: f[1], t: parseFloat(f[2]) || 0 })
                }
                break
            case "kind":  if (cl) { cl.kind = f[1]; cl.still = f[2] === "1" } break
            case "text":  if (cl) cl.text = f[6] || ""; break
            case "trans": if (cl) cl.trans = f[1]; break
            case "grade": if (cl) { cl.graded = true; inGrade = true } break
            case "key":
                if (cl) {
                    cl.graded = true
                    cl.keys.push({ t: parseFloat(f[2]), grade: ({}) })
                    inGrade = true
                    inKey = true
                }
                break
            case "# duration": break
            default: break
            }
        }
        root.tlDur = dur
        return doc
    }

    function reloadTimeline() {
        if (!root.proj) return
        // The history belongs to the project, so opening one has to ask how
        // deep it is. Without this the Undo button is dead until the first
        // edit of the session — on a project with fifty steps behind it.
        root.readHistory()
        tlShowProc.command = [root.bin, "timeline", "show", root.proj]
        tlShowProc.running = true
    }

    Process {
        id: tlShowProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.tl = root.parseTimeline(this.text)
                if (root.selTrack >= root.tl.tracks.length) { root.selTrack = -1; root.selClip = -1 }
                root.requestFrame()
                root.ensureWaves()
            }
        }
        stderr: StdioCollector { onStreamFinished: if (this.text) root.say(this.text.split("\n")[0]) }
    }

    // ── The program monitor ─────────────────────────────────────────────────
    //
    // Same in-flight guard as the darkroom preview, and for the same reason:
    // `running = true` on a Process that is already running is a SILENT no-op
    // in quickshell, so a scrub generates far more requests than renders and
    // the monitor would stop following the playhead halfway through a drag.
    property bool   frameBusy: false
    property bool   frameAgain: false
    property int    frameSerial: 0
    property string frameUrl: ""

    Timer { id: frameDebounce; interval: 70; onTriggered: root.startFrame() }

    function requestFrame() {
        if (!root.proj) return
        // A debounce that RESTARTS on every mouse move never fires while the
        // hand is moving — so dragging the playhead rendered nothing at all
        // until it was released, which is exactly what it looked like. There
        // is nothing to debounce for during a scrub: the in-flight guard below
        // already coalesces, so asking on every move costs one render per
        // render rather than one per event, and the picture follows the hand.
        if (root.scrubbing) { frameDebounce.stop(); root.startFrame(); return }
        frameDebounce.restart()
    }

    function startFrame() {
        if (!root.proj) return
        if (root.frameBusy) { root.frameAgain = true; return }
        root.frameBusy = true
        root.frameSerial++
        frameProc.command = [root.bin, "timeline", "frame", root.proj,
                             "--at", String(root.playhead),
                             "--out", root.scratch + "-frame.png",
                             "--size", String(root.scrubbing ? 540 : 1400)]
        frameProc.running = true
    }

    property bool scrubbing: false

    Process {
        id: frameProc
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(this.text.split("\n")[0])
        }
        onExited: function (exitCode, exitStatus) {
            root.frameBusy = false
            // Qt caches an Image by URL and this path never changes, so the
            // serial in the query string is the only thing making it reload.
            root.frameUrl = "file://" + root.scratch + "-frame.png?v=" + root.frameSerial
            if (root.frameAgain) { root.frameAgain = false; root.startFrame() }
        }
    }

    // ── Waveforms ───────────────────────────────────────────────────────────
    //
    // One `synstudio peaks` per clip, cached against the clip's own source
    // range so the buckets map straight onto the rectangle being drawn — no
    // duration lookup and no arithmetic that can disagree with the picture.
    // A trim changes the range and costs one re-read, which is the right
    // trade: trims are deliberate and a decode at 8kHz mono is fast.
    //
    // Requests go through a QUEUE, one at a time. A timeline of twenty clips
    // asking at once would be twenty ffmpeg processes racing each other for
    // the same disk, and `running = true` on a busy Process is a silent no-op
    // here, so most of them would simply never happen.
    property var  wave: ({})
    property var  waveQueue: []
    property bool waveBusy: false

    function waveKey(c) {
        return c.path + "|" + c.srcIn.toFixed(3) + "|" + c.srcOut.toFixed(3)
    }

    function ensureWaves() {
        const q = []
        for (let i = 0; i < root.tl.tracks.length; i++)
            for (let j = 0; j < root.tl.tracks[i].clips.length; j++) {
                const c = root.tl.tracks[i].clips[j]
                // A photograph and a generated clip have no audio to read, and
                // asking costs a process to be told so.
                if (c.kind !== "media" || c.still || !c.path) continue
                const k = root.waveKey(c)
                if (root.wave[k] !== undefined) continue
                if (q.indexOf(k) < 0) q.push(k + "\u0000" + c.path
                                             + "\u0000" + c.srcIn
                                             + "\u0000" + c.srcOut)
            }
        if (q.length === 0) return
        root.waveQueue = root.waveQueue.concat(q)
        root.pumpWaves()
    }

    function pumpWaves() {
        if (root.waveBusy || root.waveQueue.length === 0) return
        const job = root.waveQueue[0].split("\u0000")
        root.waveBusy = true
        peaksProc.jobKey = job[0]
        peaksProc.command = [root.bin, "peaks", job[1],
                             "--in", job[2], "--out-at", job[3],
                             "--count", "600"]
        peaksProc.running = true
    }

    Process {
        id: peaksProc
        property string jobKey: ""

        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n")
                const out = []
                for (let i = 0; i < lines.length; i++) {
                    if (!lines[i]) continue
                    const f = lines[i].split("\t")
                    if (f.length < 2) continue
                    out.push(parseFloat(f[0]))
                }
                peaksProc.parsed = out
            }
        }
        property var parsed: []

        onExited: function (code, status) {
            // Rebuilt, not mutated: assigning into an existing `var` object
            // does not re-evaluate anything bound to it — a documented trap in
            // this repo, and here it would mean the waveform arrives and never
            // appears.
            const next = ({})
            for (const k in root.wave) next[k] = root.wave[k]
            // An empty array is a real answer: exit 100 means the file has no
            // audio, and recording that stops it being asked again every time
            // the timeline reloads.
            next[peaksProc.jobKey] = (code === 0) ? peaksProc.parsed : []
            root.wave = next

            peaksProc.parsed = []
            root.waveQueue = root.waveQueue.slice(1)
            root.waveBusy = false
            root.pumpWaves()
        }
    }

    // ── Clip properties, from the engine's table ────────────────────────────
    //
    // Exactly what the develop panel does with `keys`: the inspector is built
    // from `timeline keys`, so a property added to the table in timeline.c
    // appears here without this file being touched, and cannot disagree with
    // the engine about a limit or about which transitions exist.
    property var clipRows: []
    property var clipGroups: []

    function parseClipKeys(text) {
        const out = [], seen = [], byGroup = ({})
        const lines = text.split("\n")
        for (let i = 0; i < lines.length; i++) {
            if (!lines[i]) continue
            const f = lines[i].split("\t")
            if (f.length < 7) continue
            const r = { key: f[0], value: f[1], lo: parseFloat(f[2]), hi: parseFloat(f[3]),
                        type: f[4], group: f[5], label: f[6],
                        choices: (f[7] || "") ? f[7].split("|") : [],
                        // The last column is whether the renderer can animate
                        // it. The diamond appears on exactly those rows: a
                        // button offering to key something the export would
                        // then ignore is worse than no button at all.
                        anim: f[8] === "1" }
            out.push(r)
            if (!byGroup[r.group]) { byGroup[r.group] = true; seen.push(r.group) }
        }
        root.clipGroups = seen
        return out
    }

    // The transition catalogue: name -> what to call it. The picker is built
    // from the clip table like every other enum, but sixty rows of `smoothright`
    // is a list nobody can read, and the labels live in the engine beside the
    // names for the same reason the choices do.
    property var transLabels: ({})
    property string defaultTrans: "dissolve"
    property real   defaultTransDur: 1.0

    function enumLabel(key, choice) {
        if (key === "trans" && root.transLabels[choice])
            return root.transLabels[choice]
        return choice
    }

    // ── Title styles ────────────────────────────────────────────────────────
    //
    // A style is a starting point, not a property: it SETS a handful of the
    // Title rows below it and every one of them is still a control afterwards,
    // exactly as a look does to a grade. So it is a list to pick FROM and not
    // a value to read back — nothing on the clip records which one was used,
    // and a picker claiming to show the style in force would be inventing it.
    //
    // The list comes from the engine, so a style added in timeline.c appears
    // here and this file never learns what a lower third is.
    property var titleStyles: []

    function applyTitleStyle(name) {
        if (root.selTrack < 0 || root.selClip < 0) { root.say("pick a title first"); return }
        root.tlRun(["style", root.proj, String(root.selTrack),
                    String(root.selClip), name])
    }

    // ── The families a title can be lettered in ─────────────────────────────
    //
    // `text.font` is a plain text field in the clip table, which meant the
    // only way to letter a title in anything but the default was to TYPE a
    // family name — with no list, no spelling to check against, and no way to
    // find out what the machine has. `synstudio fonts` has existed the whole
    // time to answer exactly that and nothing called it.
    //
    // Read once. It is an fc-list over every installed face and it does not
    // change while the window is open; asking again per keystroke would be a
    // process per letter typed in the filter.
    property var fontList: []

    // Whether the family in force is one this machine actually has. A name
    // that is not resolves through fc-match to something else entirely at
    // render time, so a title lettered in a typo looks deliberate and wrong
    // rather than broken — the one failure a free text field could not report.
    function fontInstalled(name) {
        if (!name) return true            // empty = the default face, always fine
        // Before the list has arrived nothing is known, and a field that
        // flashes an error for the first half-second of every session is a
        // field people learn to ignore.
        if (root.fontList.length === 0) return true
        return root.fontList.indexOf(name) >= 0
    }

    Process {
        id: fontListProc
        command: [root.bin, "fonts"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [], lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++)
                    if (lines[i]) out.push(lines[i])
                root.fontList = out
            }
        }
    }

    Process {
        id: styleListProc
        command: [root.bin, "timeline", "styles"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [], lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length >= 2) out.push({ name: f[0], label: f[1] })
                }
                root.titleStyles = out
            }
        }
    }

    // ── Effects ─────────────────────────────────────────────────────────────
    //
    // The catalogue and every parameter in it, in two calls at startup. An
    // effect is a FILE — a recipe naming a filter chain — so this list is
    // whatever is installed plus whatever the user has dropped in their own
    // folder, and this window has no idea what any of them do.
    property var fxList: []
    property var fxParams: ({})     // name -> [{key, def, lo, hi, label}]
    property bool fxPicking: false

    function fxLabel(name) {
        for (let i = 0; i < root.fxList.length; i++)
            if (root.fxList[i].name === name) return root.fxList[i].label
        return name + " (missing)"
    }

    function fxParamsOf(name) { return root.fxParams[name] || [] }

    // What the clip's effect stack is, straight out of the document — no
    // extra process, because `timeline show` already carries it.
    function clipFx() {
        const c = root.selClipObj
        return (c && c.fx) ? c.fx : []
    }

    function fxRun(args) {
        if (root.selTrack < 0 || root.selClip < 0) return
        root.tlRun(["fx", root.proj, String(root.selTrack),
                    String(root.selClip)].concat(args))
    }

    // ── Looks and LUTs ──────────────────────────────────────────────────────
    //
    // Two different things and the window keeps them apart, because they
    // behave differently under the hand: a LOOK moves the sliders, so it can
    // be adjusted after it lands; a LUT is a table, so it can only be mixed
    // in and out. Both come from the engine's own catalogues, so a .synlook
    // dropped in this morning appears without the QML learning its name.
    property var lookList: []
    property var lutList: []
    property bool lutMenuOpen: false

    // ── The delivery frame ──────────────────────────────────────────────────
    //
    // The project's own size, which `timeline new --size` used to be the only
    // way to say — so a cut begun at 1920x1080 could never become a vertical
    // version of itself without being rebuilt. The status bar has shown these
    // numbers all along; they are a control now rather than a caption.
    //
    // The list matches the names `timeline size` accepts, and it is the only
    // list here: a preset the CLI does not know would be a menu entry that
    // fails, and the reverse is a name you can only reach by typing it.
    property bool sizeMenuOpen: false
    readonly property var sizePresets: [
        { name: "4k",       label: "4K UHD",        w: 3840, h: 2160 },
        { name: "dci4k",    label: "DCI 4K",        w: 4096, h: 2160 },
        { name: "1440p",    label: "1440p",         w: 2560, h: 1440 },
        { name: "1080p",    label: "1080p HD",      w: 1920, h: 1080 },
        { name: "720p",     label: "720p",          w: 1280, h:  720 },
        { name: "480p",     label: "480p",          w:  854, h:  480 },
        { name: "dci2k",    label: "DCI 2K",        w: 2048, h: 1080 },
        { name: "vertical", label: "Vertical 9:16", w: 1080, h: 1920 },
        { name: "square",   label: "Square 1:1",    w: 1080, h: 1080 }
    ]
    function setProjectSize(ref) {
        root.sizeMenuOpen = false
        if (!root.proj) return
        // Straight through the edit queue, so it takes its turn with every
        // other change and the reload that follows redraws the status bar.
        root.tlRun(["size", root.proj, ref])
    }
    // Which page asked for a LUT. The chooser and the file picker are shared
    // between the darkroom and the cutting room, and a LUT picked from the
    // clip inspector that landed on the open photograph instead would be the
    // kind of wrong that is only noticed later.
    property string lutTarget: "photo"

    function setLut(ref) {
        if (root.lutTarget === "clip") root.gradeClip("lut", ref)
        else                           root.change("lut", ref)
    }
    // The clip's LUT is a NAME, and gradeValue parses everything as a number
    // — which reads every reference as 0 and shows every clip as having none.
    function gradeRaw(key) {
        const c = root.selClipObj
        if (!c || !c.graded) return ""
        const src = (root.selKey >= 0) ? c.keys[root.selKey].grade : c.grade
        const v = src[key]
        return v === undefined ? "" : String(v)
    }

    // A look lands on whatever page asked for it: the darkroom writes the
    // sidecar, the cutting room writes the clip's grade. Same look, same
    // engine call shape, one line apart.
    function applyLook(name) {
        if (root.mode === "video") {
            if (root.selTrack < 0 || root.selClip < 0) { root.say("pick a clip first"); return }
            root.tlRun(["grade", root.proj, String(root.selTrack),
                        String(root.selClip), "--look", name])
        } else {
            if (!root.file) { root.say("open a photograph first"); return }
            lookApplyProc.command = [root.bin, "look", "apply", name, "--to", root.file]
            lookApplyProc.running = true
        }
    }

    Process {
        id: lookApplyProc
        onExited: function (code) {
            // The engine has just rewritten the sidecar, so every slider in
            // the panel is now stale. Re-reading it through the SAME `get`
            // the Open path uses is what makes a look ADJUSTABLE rather than
            // a one-way stamp — the panel comes back showing where the look
            // put each control, and the hand carries on from there.
            if (!root.file) return
            getProc.command = [root.bin, "get", root.file]
            getProc.running = true
            root.dirty = true
        }
    }

    Process {
        id: lookListProc
        command: [root.bin, "look", "list"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [], lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 2) continue
                    out.push({ name: f[0], label: f[1], about: f[2] || "" })
                }
                root.lookList = out
            }
        }
    }

    Process {
        id: lutListProc
        command: [root.bin, "luts"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [], lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 3) continue
                    out.push({ name: f[0], dims: f[1], size: f[2], path: f[3] || "" })
                }
                root.lutList = out
            }
        }
    }

    Process {
        id: fxListProc
        command: [root.bin, "fx", "list"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [], lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 3) continue
                    out.push({ name: f[0], label: f[1], group: f[2],
                               alpha: f[4] === "1", about: f[5] || "" })
                }
                root.fxList = out
            }
        }
    }

    Process {
        id: fxParamProc
        command: [root.bin, "fx", "params"]
        stdout: StdioCollector {
            onStreamFinished: {
                const m = ({}), lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 6) continue
                    if (!m[f[0]]) m[f[0]] = []
                    m[f[0]].push({ key: f[1], def: parseFloat(f[2]),
                                   lo: parseFloat(f[3]), hi: parseFloat(f[4]),
                                   label: f[5] })
                }
                root.fxParams = m
            }
        }
    }

    Process {
        id: transListProc
        command: [root.bin, "timeline", "transitions"]
        stdout: StdioCollector {
            onStreamFinished: {
                const m = ({}), lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length >= 3) m[f[1]] = f[2]
                }
                root.transLabels = m
            }
        }
    }

    Process {
        id: clipKeysProc
        command: [root.bin, "timeline", "keys"]
        stdout: StdioCollector {
            onStreamFinished: root.clipRows = root.parseClipKeys(this.text)
        }
    }

    // What the selected clip currently says, key -> value.
    property var clipVals: ({})

    // Which keys a property carries on the selected clip, [] for most of them.
    function clipAnimKeys(key) {
        const c = root.selClipObj
        if (!c || !c.anim) return []
        return c.anim[key] || []
    }

    // Whether ANY property on the selected clip is keyed. What it decides is
    // whether the inspector has to follow the playhead at all: a clip with no
    // keys reads the same at every instant, and asking the engine again on
    // every scrub step would be a process per frame for nothing.
    readonly property bool clipHasAnim: {
        const c = root.selClipObj
        if (!c || !c.anim) return false
        for (const k in c.anim) if (c.anim[k].length > 0) return true
        return false
    }

    // Where the playhead is INSIDE the selected clip, which is what a key is
    // timed against.
    readonly property real clipOffset: {
        const c = root.selClipObj
        if (!c) return 0
        return Math.max(0, Math.min(c.len, root.playhead - c.tlIn))
    }

    function loadClip() {
        if (root.selTrack < 0 || root.selClip < 0) { root.clipVals = ({}); return }
        // --at, so a moving property reports what it is AT THE PLAYHEAD. The
        // static field is not what the renderer reads once a property is
        // keyed, and a panel showing it would be describing a clip nobody is
        // looking at.
        clipGetProc.command = [root.bin, "timeline", "get", root.proj,
                               String(root.selTrack), String(root.selClip),
                               "--at", String(Math.round(root.clipOffset * 1000) / 1000)]
        clipGetProc.running = true
    }

    onClipOffsetChanged: if (root.clipHasAnim) root.loadClip()

    Process {
        id: clipGetProc
        stdout: StdioCollector {
            onStreamFinished: {
                const v = ({})
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length >= 2) v[f[0]] = f[1]
                }
                root.clipVals = v
            }
        }
    }

    function clipValue(key) {
        const v = root.clipVals[key]
        return v === undefined ? "" : v
    }

    // ── Changing things ─────────────────────────────────────────────────────
    //
    // Every one of these is a command and a reload. The engine owns the
    // document; the window never edits its own copy and hopes they agree.
    Process {
        id: tlSetProc
        // Which verb this run was, so the exit can tell a copy from every
        // other edit without parsing the command back.
        property bool wasCopy: false
        stderr: StdioCollector { onStreamFinished: if (this.text) root.say(this.text.split("\n")[0]) }
        onExited: function (code, status) {
            if (tlSetProc.wasCopy) root.readClipboard()
            if (root.tlQueue.length > 0) { root.pumpEdits(); return }
            root.reloadTimeline()
            root.loadClip()
            root.readHistory()
        }
    }

    // Edits QUEUE rather than being dropped.
    //
    // `running = true` on a busy Process is a silent no-op in quickshell, so
    // the old shape — refuse if busy — meant that asking for two things in one
    // gesture did one of them. Deleting six selected clips did one. The queue
    // is also what makes the order matter: several deletes on one track have
    // to happen back to front, and that is only true if they all happen.
    property var tlQueue: []

    function tlRun(args) {
        // The rendered preview is now a picture of a timeline that no longer
        // exists. Bumping here rather than in the exit handler means a play
        // pressed DURING an edit still re-renders.
        root.tlRev++
        root.tlQueue = root.tlQueue.concat([args])
        root.pumpEdits()
        return true
    }

    function pumpEdits() {
        if (tlSetProc.running || root.tlQueue.length === 0) return
        const args = root.tlQueue[0]
        root.tlQueue = root.tlQueue.slice(1)
        tlSetProc.wasCopy = args[0] === "copy"
        tlSetProc.command = [root.bin, "timeline"].concat(args)
        tlSetProc.running = true
    }

    // ── More than one clip ──────────────────────────────────────────────────
    //
    // `selTrack`/`selClip` stay the PRIMARY selection — the inspector is bound
    // to them and a panel of sliders showing six clips at once means nothing.
    // `selMore` is everything else that a move or a delete should also carry,
    // as "<track>.<clip>" strings so membership is a lookup rather than a
    // search through objects that are rebuilt on every reload.
    property var selMore: []

    // NOT `selKey` — that name is already the KEYFRAME the grade panel is
    // pointed at, and the collision fails at call time with a TypeError rather
    // than at load, so the file opens and one gesture is quietly broken.
    function selId(tr, cl) { return tr + "." + cl }

    function isSelected(tr, cl) {
        if (root.selTrack === tr && root.selClip === cl) return true
        return root.selMore.indexOf(root.selId(tr, cl)) >= 0
    }

    function toggleSelect(tr, cl) {
        if (root.selClip < 0) { root.selTrack = tr; root.selClip = cl; return }
        if (root.selTrack === tr && root.selClip === cl) return
        const k = root.selId(tr, cl)
        const i = root.selMore.indexOf(k)
        const next = root.selMore.slice()
        if (i >= 0) next.splice(i, 1)
        else next.push(k)
        root.selMore = next
    }

    // Primary first, then the rest — and within a track, HIGHEST CLIP INDEX
    // first. Deleting clip 2 renumbers everything above it, so a low-to-high
    // delete removes the wrong clips from the third one onwards.
    function selectionDescending() {
        const all = []
        if (root.selClip >= 0) all.push({ t: root.selTrack, c: root.selClip })
        for (let i = 0; i < root.selMore.length; i++) {
            const p = root.selMore[i].split(".")
            all.push({ t: parseInt(p[0]), c: parseInt(p[1]) })
        }
        all.sort(function (a, b) { return a.t !== b.t ? a.t - b.t : b.c - a.c })
        return all
    }

    function deleteSelection(ripple) {
        const all = root.selectionDescending()
        if (all.length === 0) return
        for (let i = 0; i < all.length; i++) {
            const a = ["delete", root.proj, String(all[i].t), String(all[i].c)]
            if (ripple) a.push("--ripple")
            root.tlRun(a)
        }
        root.selClip = -1
        root.selMore = []
    }

    // ── Snapping ────────────────────────────────────────────────────────────
    //
    // Every cut, every marker, the playhead and zero. Within a few PIXELS, not
    // a few frames: the tolerance has to shrink as you zoom in, or a tight
    // trim at high zoom keeps jumping to the thing next to it.
    property bool snapping: true

    // ── Markers ─────────────────────────────────────────────────────────────
    function addMarker() {
        if (!root.proj) return
        root.tlRun(["mark", root.proj, "--at",
                    String(Math.round(root.playhead * 1000) / 1000),
                    "--colour", "1"])
        root.say("marker at " + root.timecode(root.playhead))
    }

    function dropMarker(i) {
        if (!root.proj) return
        root.tlRun(["unmark", root.proj, String(i)])
    }

    // ── Undo ────────────────────────────────────────────────────────────────
    //
    // The engine keeps whole documents in `<project>.undo/`, so this is a
    // button and a number rather than a stack the window has to maintain — and
    // it survives the window being closed, which a session stack does not.
    property int undoDepth: 0
    property int redoDepth: 0

    function readHistory() {
        if (!root.proj) { root.undoDepth = 0; root.redoDepth = 0; return }
        tlHistProc.command = [root.bin, "timeline", "history", root.proj]
        tlHistProc.running = true
    }

    // NOT `histProc` — that name is the darkroom's HISTOGRAM, and a duplicate
    // id fails the whole file to load with one line of complaint.
    Process {
        id: tlHistProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f[0] === "undo") root.undoDepth = parseInt(f[1]) || 0
                    else if (f[0] === "redo") root.redoDepth = parseInt(f[1]) || 0
                }
            }
        }
    }

    function undoEdit() {
        if (root.undoDepth <= 0) return
        root.tlRev++
        root.selClip = -1
        root.selMore = []
        root.tlQueue = root.tlQueue.concat([["undo", root.proj]])
        root.pumpEdits()
        root.say("undone")
    }

    function redoEdit() {
        if (root.redoDepth <= 0) return
        root.tlRev++
        root.selClip = -1
        root.selMore = []
        root.tlQueue = root.tlQueue.concat([["redo", root.proj]])
        root.pumpEdits()
        root.say("redone")
    }

    function setClip(key, v) {
        if (root.selTrack < 0 || root.selClip < 0) return
        // A keyed property is driven by its keys; writing the static field
        // would change nothing anybody could see and the slider would appear
        // dead. Move the key under the playhead instead — the same rule the
        // grade sliders follow.
        if (root.clipAnimKeys(key).length > 0) {
            root.animKey(key, v)
            return
        }
        // Optimistic, so a slider does not snap back while the engine and the
        // reload catch up. The reload overwrites this with the truth.
        const next = ({})
        for (const k in root.clipVals) next[k] = root.clipVals[k]
        next[key] = String(v)
        root.clipVals = next
        root.tlRun(["set", root.proj, String(root.selTrack), String(root.selClip),
                    key + "=" + v])
    }

    function gradeClip(key, v) {
        if (root.selTrack < 0 || root.selClip < 0) return
        // With keyframes, a slider edits the KEY the panel is pointed at.
        // Writing to the static grade instead would change nothing anybody
        // could see — the keys are what the renderer reads — and the slider
        // would appear to do nothing at all.
        if (root.selKey >= 0)
            root.tlRun(["key", root.proj, String(root.selTrack), String(root.selClip),
                        "set", String(root.selKey), key + "=" + v])
        else
            root.tlRun(["grade", root.proj, String(root.selTrack), String(root.selClip),
                        key + "=" + v])
    }

    // The keyframe the grade panel is pointed at: the one nearest the
    // playhead. With no keyframes there is one static grade and this is -1.
    readonly property int selKey: {
        const c = root.selClipObj
        if (!c || !c.keys || c.keys.length === 0) return -1
        const off = root.playhead - c.tlIn
        let best = 0, bd = Math.abs(c.keys[0].t - off)
        for (let i = 1; i < c.keys.length; i++) {
            const d = Math.abs(c.keys[i].t - off)
            if (d < bd) { bd = d; best = i }
        }
        return best
    }

    function gradeValue(key) {
        const c = root.selClipObj
        if (!c || !c.graded) return 0
        const src = (root.selKey >= 0) ? c.keys[root.selKey].grade : c.grade
        const v = src[key]
        return v === undefined ? 0 : (parseFloat(v) || 0)
    }

    function addKey() {
        const c = root.selClipObj
        if (!c || root.selTrack < 0 || root.selClip < 0) return
        const off = Math.max(0, Math.min(c.len, root.playhead - c.tlIn))
        root.tlRun(["key", root.proj, String(root.selTrack), String(root.selClip),
                    "add", "--at", String(Math.round(off * 1000) / 1000)])
    }

    function removeKey() {
        if (root.selKey < 0) return
        root.tlRun(["key", root.proj, String(root.selTrack), String(root.selClip),
                    "remove", String(root.selKey)])
    }

    // ── Parameter keys ──────────────────────────────────────────────────────
    //
    // A key on ONE number, at the playhead. `add` replaces a key already at
    // that instant, so writing a value while parked on one edits it rather
    // than stacking a second key nothing will ever read.
    function animKey(key, v) {
        if (root.selTrack < 0 || root.selClip < 0) return
        const arg = ["anim", root.proj, String(root.selTrack), String(root.selClip),
                     "add", key, "--at",
                     String(Math.round(root.clipOffset * 1000) / 1000)]
        if (v !== undefined) { arg.push("--value"); arg.push(String(v)) }
        root.tlRun(arg)
    }

    // ── The curve, over time ────────────────────────────────────────────────
    //
    // The darkroom has a curve widget over TONE; this is the same idea over
    // TIME, and it is the last thing the keyframes were missing: they could
    // be dropped and nudged, but a move over four seconds was a list of
    // numbers rather than a shape.
    //
    // ⛔ THE CURVE IS SAMPLED BY THE ENGINE, never interpolated here.
    // `ss_clip_prop_at` is the one place a keyed property becomes a number,
    // and the whole reason it exists is that the monitor and the export have
    // to agree about it frame by frame. Five eases re-implemented in QML
    // would be a picture of something nothing renders — right until the day
    // one of the two changed.
    property string curveKey: ""     // whose curve is open, "" for none
    property var    curvePts: []     // [{t, v}] as the engine samples it
    property int    curveSerial: 0

    function openCurve(key) {
        root.curveKey = root.curveKey === key ? "" : key
        root.curvePts = []
        if (root.curveKey) root.readCurve()
    }

    function readCurve() {
        if (!root.curveKey || root.selTrack < 0 || root.selClip < 0) return
        curveProc.running = false
        curveProc.command = [root.bin, "timeline", "anim", root.proj,
                             String(root.selTrack), String(root.selClip),
                             "curve", root.curveKey, "--count", "160"]
        curveProc.running = true
    }

    Process {
        id: curveProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 2) continue
                    out.push({ t: parseFloat(f[0]), v: parseFloat(f[1]) })
                }
                root.curvePts = out
                root.curveSerial++
            }
        }
    }

    // The curve is a picture of the document, so it is re-read whenever the
    // document changes — not only when the editor is opened.
    onClipValsChanged: if (root.curveKey) root.readCurve()

    // One edit, not two. A drag that removed a key and added another would be
    // two processes, two writes and two steps of undo for one gesture, and
    // the second half can be dropped.
    function curveMove(key, i, t, v) {
        if (root.selTrack < 0 || root.selClip < 0) return
        root.tlRun(["anim", root.proj, String(root.selTrack), String(root.selClip),
                    "move", key, String(i),
                    "--at", String(Math.round(t * 1000) / 1000),
                    "--value", String(Math.round(v * 10000) / 10000)])
    }

    function curveAdd(key, t, v) {
        if (root.selTrack < 0 || root.selClip < 0) return
        root.tlRun(["anim", root.proj, String(root.selTrack), String(root.selClip),
                    "add", key, "--at", String(Math.round(t * 1000) / 1000),
                    "--value", String(Math.round(v * 10000) / 10000)])
    }

    function curveRemove(key, i) {
        if (root.selTrack < 0 || root.selClip < 0) return
        root.tlRun(["anim", root.proj, String(root.selTrack), String(root.selClip),
                    "remove", key, String(i)])
    }

    function curveEase(key, i, ease) {
        if (root.selTrack < 0 || root.selClip < 0) return
        root.tlRun(["anim", root.proj, String(root.selTrack), String(root.selClip),
                    "move", key, String(i), "--ease", ease])
    }

    // The index of the key sitting AT the playhead, or -1. Eight milliseconds
    // of tolerance: a key is placed from a playhead that has been rounded to
    // milliseconds, and an exact float compare would never find it again.
    function animKeyAt(key) {
        const ks = root.clipAnimKeys(key)
        for (let i = 0; i < ks.length; i++)
            if (Math.abs(ks[i].t - root.clipOffset) < 0.008) return i
        return -1
    }

    function animToggle(key) {
        if (root.selTrack < 0 || root.selClip < 0) return
        const i = root.animKeyAt(key)
        if (i >= 0)
            root.tlRun(["anim", root.proj, String(root.selTrack),
                        String(root.selClip), "remove", key, String(i)])
        else
            root.animKey(key, undefined)
    }

    // ── The mixer ───────────────────────────────────────────────────────────
    //
    // A fader, a pan, mute, solo and a meter per audio track, plus one master.
    // The engine owns all of it — `timeline track --gain/--pan/--solo` and
    // `timeline master --gain` — and this holds nothing but the value under
    // the hand while it is moving.
    //
    // ⚠ Optimistic and COMMITTED ON RELEASE, not per tick, for two reasons
    // that have both bitten this file: `running = true` on a busy Process is a
    // silent no-op, so most ticks would be dropped; and an edit reloads the
    // timeline, which rebuilds `tl.tracks`, which rebuilds the Repeater the
    // strips are delegates of — destroying the MouseArea holding the drag on
    // its first move. See the develop panel.
    // The panel on the right shows ONE of these at a time. A boolean per panel
    // would let two of them be open at once, which on a column this narrow
    // means one of them silently wins.
    property string panelMode: "clip"    // clip | mixer | voiceover
    readonly property bool mixerOpen: root.panelMode === "mixer"
    property var  mixLive: ({})          // "<track>.<key>" while a hand is on it

    function mixOf(i, key) {
        const k = i + "." + key
        if (root.mixLive[k] !== undefined) return root.mixLive[k]
        const t = root.tl.tracks
        if (i < 0 || i >= t.length) return 0
        return key === "gain" ? t[i].gain : t[i].pan
    }

    function mixLiveSet(i, key, v) {
        const next = ({})
        for (const k in root.mixLive) next[k] = root.mixLive[k]
        next[i + "." + key] = v
        root.mixLive = next
    }

    function mixCommit(i, key, v) {
        root.mixLive = ({})
        root.tlRun(["track", root.proj, String(i),
                    key === "gain" ? "--gain" : "--pan",
                    String(Math.round(v * 100) / 100)])
    }

    property real masterLive: NaN
    readonly property real masterDb:
        isNaN(root.masterLive) ? (root.tl.master || 0) : root.masterLive

    function masterCommit(v) {
        root.masterLive = NaN
        masterProc.command = [root.bin, "timeline", "master", root.proj,
                              "--gain", String(Math.round(v * 100) / 100)]
        masterProc.running = true
    }
    Process {
        id: masterProc
        onExited: root.reloadTimeline()
    }

    // ── Voiceover ───────────────────────────────────────────────────────────
    //
    // Resolve arms a track and punches in at the playhead; so does this. What
    // makes it work or not work is none of the recording:
    //
    //   · MONITORING FEEDS BACK. The timeline plays while you talk, and on
    //     speakers that goes straight back into the microphone. Playback is
    //     muted for the take unless somebody says they are on headphones, and
    //     whatever the monitor was set to is put back afterwards.
    //   · A COUNTDOWN, because a punch-in with no lead is a take that starts
    //     mid-word.
    //   · A LIMIT, because a forgotten session fills the disk. The engine caps
    //     it at an hour whether anybody asks or not.
    //   · The take lands BESIDE THE PROJECT, not in a scratch directory. A
    //     voiceover is footage; it has to survive a reboot.
    readonly property bool voOpen: root.panelMode === "voiceover"
    property var    voDevices: []
    property int    voDevice: 0
    property bool   voRecording: false
    property int    voCount: 0           // counting in; 0 = not counting
    property real   voElapsed: 0
    property real   voLevel: -120        // dB, live off the take
    property real   voStartAt: 0         // where on the timeline it goes
    property string voTake: ""
    property bool   voPlayWhile: true
    property bool   voHeadphones: false  // monitor through while recording
    property bool   voMonWas: false

    function loadDevices() {
        devProc.command = [root.bin, "devices"]
        devProc.running = true
    }

    Process {
        id: devProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    if (!lines[i]) continue
                    const f = lines[i].split("\t")
                    if (f.length < 4) continue
                    out.push({ kind: f[0], name: f[1], id: f[2],
                               isDefault: f[3] === "1" })
                }
                root.voDevices = out
                // A real input, not a monitor, and the system's own default
                // ahead of the first one that happens to be listed.
                let pick = -1
                for (let i = 0; i < out.length; i++)
                    if (out[i].kind === "input" && out[i].isDefault) { pick = i; break }
                if (pick < 0)
                    for (let i = 0; i < out.length; i++)
                        if (out[i].kind === "input") { pick = i; break }
                root.voDevice = pick < 0 ? 0 : pick
            }
        }
    }

    function voTakePath() {
        // Beside the project, named for it and stamped, so two takes never
        // land on the same name and a take from last week is still findable.
        const dir = root.proj.replace(/\/[^\/]*$/, "")
        const base = root.proj.replace(/^.*\//, "").replace(/\.[^.]*$/, "")
        const t = Qt.formatDateTime(new Date(), "yyyyMMdd-hhmmss")
        return dir + "/" + base + "-vo-" + t + ".wav"
    }

    function startVoiceover() {
        if (root.voRecording || root.voCount > 0) return
        if (!root.proj) { root.say("start a project first"); return }
        if (root.voDevices.length === 0) { root.say("nothing to record from"); return }
        root.voCount = 3
        voCountTimer.restart()
    }

    Timer {
        id: voCountTimer
        interval: 1000
        repeat: true
        onTriggered: {
            root.voCount--
            if (root.voCount <= 0) { voCountTimer.stop(); root.beginTake() }
        }
    }

    function beginTake() {
        const d = root.voDevices[root.voDevice]
        if (!d) return
        root.voStartAt = root.playhead
        root.voTake = root.voTakePath()
        root.voElapsed = 0
        root.voLevel = -120
        root.voRecording = true

        // The monitor is put back exactly as it was, whatever happens to the
        // take — including a failed one.
        root.voMonWas = root.monMuted
        if (!root.voHeadphones) root.monMuted = true
        if (root.voPlayWhile && root.playbackReady && root.tlDur > 0
            && !root.playing) root.togglePlay()

        recProc.command = [root.bin, "record", "--out", root.voTake,
                           "--device", d.id, "--limit", "3600", "--channels", "1"]
        recProc.running = true
        root.say("recording…")
    }

    function stopVoiceover() {
        if (!root.voRecording) { voCountTimer.stop(); root.voCount = 0; return }
        // SIGTERM, which the engine catches and turns into a finished file.
        recProc.running = false
    }

    Process {
        id: recProc
        stdout: SplitParser {
            splitMarker: "\n"
            onRead: function (line) {
                const f = line.split("\t")
                if (f[0] === "level") {
                    root.voElapsed = parseFloat(f[1]) || 0
                    root.voLevel = parseFloat(f[2]) || -120
                } else if (f[0] === "length") {
                    root.voElapsed = parseFloat(f[1]) || root.voElapsed
                }
            }
        }
        stderr: SplitParser {
            splitMarker: "\n"
            onRead: function (line) { if (line.trim()) recProc.err = line.trim() }
        }
        property string err: ""

        onExited: function (code, status) {
            root.voRecording = false
            root.monMuted = root.voMonWas
            if (root.playing) root.pausePlayback()

            if (code !== 0 || root.voElapsed < 0.1) {
                root.say(recProc.err || "the take came back empty")
                recProc.err = ""
                return
            }
            recProc.err = ""
            // Punched in where it started, not where the playhead ended up:
            // playback ran for the length of the take and moved it.
            root.addMediaAt(root.voTake, "audio", root.voStartAt)
            root.say("take of " + root.voElapsed.toFixed(1) + "s at "
                     + root.timecode(root.voStartAt))
            root.playhead = root.voStartAt
            root.requestFrame()
        }
    }

    // ── What the meters show ────────────────────────────────────────────────
    //
    // The ENVELOPE at the playhead, not the sound card's output. Nothing here
    // can see what the player is actually pushing — it is a separate process
    // holding a file we handed it — so the meter is computed from the same
    // per-clip peaks that draw the waveforms, with the clip's gain, the
    // track's fader and the master applied exactly as the export graph applies
    // them. It agrees with the file that comes out, which is the number that
    // matters, and it moves with the playhead whether that is a scrub or a
    // play.
    function trackLevel(i) {
        const t = root.tl.tracks
        if (i < 0 || i >= t.length) return 0
        if (t[i].muted || !root.soloOk(i)) return 0
        let lin = 0
        for (let j = 0; j < t[i].clips.length; j++) {
            const c = t[i].clips[j]
            if (c.kind !== "media" || !c.path) continue
            const off = root.playhead - c.tlIn
            if (off < 0 || off > c.len) continue
            const d = root.wave[root.waveKey(c)]
            if (!d || d.length === 0) continue
            const k = Math.min(d.length - 1,
                               Math.max(0, Math.floor(off / c.len * d.length)))
            // dB add; the meter multiplies, so the gains become one factor.
            lin += d[k] * Math.pow(10, (c.gain + t[i].gain) / 20)
        }
        return lin
    }

    function soloOk(i) {
        const t = root.tl.tracks
        for (let k = 0; k < t.length; k++) if (t[k].solo) return t[i].solo
        return true
    }

    readonly property real masterLevel: {
        let lin = 0
        for (let i = 0; i < root.tl.tracks.length; i++) lin += root.trackLevel(i)
        return lin * Math.pow(10, root.masterDb / 20)
    }

    // 0..1 across -48..0 dB, the same scale the waveforms are drawn on, so a
    // tall waveform and a high meter mean the same thing.
    function meterFrac(lin) {
        if (!(lin > 0.0001)) return 0
        const db = 20 * Math.log(lin) / Math.LN10
        return Math.max(0, Math.min(1, (db + 48) / 48))
    }

    // ── Normalise ───────────────────────────────────────────────────────────
    //
    // The engine measures AND decides. Reading a number here and doing the
    // subtraction would put "how loud should this be" in two places.
    property bool normalising: false

    function normaliseClip() {
        if (root.normalising || root.selTrack < 0 || root.selClip < 0) return
        root.normalising = true
        root.say("measuring…")
        normProc.command = [root.bin, "timeline", "normalise", root.proj,
                            String(root.selTrack), String(root.selClip),
                            "--target", "-14"]
        normProc.running = true
    }

    Process {
        id: normProc
        stdout: StdioCollector { onStreamFinished: normProc.answer = this.text }
        stderr: StdioCollector { onStreamFinished: if (this.text) normProc.err = this.text.split("\n")[0] }
        property string answer: ""
        property string err: ""
        onExited: function (code, status) {
            root.normalising = false
            if (code !== 0) { root.say(normProc.err || "nothing to measure there"); return }
            let was = "", now = ""
            const lines = normProc.answer.split("\n")
            for (let i = 0; i < lines.length; i++) {
                const f = lines[i].split("\t")
                if (f[0] === "measured") was = f[1]
                else if (f[0] === "gain") now = f[1]
            }
            normProc.answer = ""
            root.say("measured " + was + " LUFS · gain " + now + " dB")
            root.tlRev++
            root.reloadTimeline()
        }
    }

    // ── Adding media, onto a track that can hold it ─────────────────────────
    //
    // The destination is chosen by what the FILE is, not by whatever track
    // happens to be selected. Dropping a music bed on V1 — which is what the
    // selection usually is — put a clip with no picture on a video track,
    // where the only sign of it is a waveform drawn inside a video clip's
    // bar. The document allows it and the export honours it, so nothing
    // failed; it just read as the audio having gone missing.
    //
    // A picture goes to a video track, a sound to an audio track: the
    // selected one when it is already of the right type, so adding several
    // clips to a chosen track still works, otherwise the first one that fits.
    function trackFor(kind) {
        const want = kind === "audio" ? "audio" : "video"
        const t = root.tl.tracks
        if (root.selTrack >= 0 && root.selTrack < t.length
            && t[root.selTrack].type === want) return root.selTrack
        for (let i = 0; i < t.length; i++) if (t[i].type === want) return i
        return -1
    }

    function addMedia(path, kind) {
        root.addMediaAt(path, kind, root.playhead)
    }

    // The position is a parameter because a voiceover lands where the take
    // STARTED, and the playhead has been running for the length of it.
    function addMediaAt(path, kind, at) {
        const t = root.trackFor(kind)
        if (t >= 0) {
            root.selTrack = t
            root.tlRun(["clip", root.proj, String(t), path,
                        "--at", String(at)])
            return
        }
        // No track of that type yet — an audio-only project, or one whose
        // audio track was deleted. Lay one down and add to it, rather than
        // refusing a file the picker was willing to draw.
        const want = kind === "audio" ? "audio" : "video"
        let n = 0
        for (let i = 0; i < root.tl.tracks.length; i++)
            if (root.tl.tracks[i].type === want) n++
        addTrackProc.pendingPath = path
        addTrackProc.pendingAt = at
        addTrackProc.command = [root.bin, "timeline", "track", root.proj, want,
                                (want === "audio" ? "A" : "V") + String(n + 1)]
        addTrackProc.running = true
    }

    Process {
        id: addTrackProc
        property string pendingPath: ""
        property real pendingAt: 0
        property int newTrack: -1
        // `timeline track` prints the index it made, which is the only way to
        // know where the clip has to go without reloading the document first.
        stdout: StdioCollector {
            onStreamFinished: addTrackProc.newTrack = parseInt(this.text.trim())
        }
        onExited: function (code, status) {
            const t = addTrackProc.newTrack
            const path = addTrackProc.pendingPath
            addTrackProc.pendingPath = ""
            addTrackProc.newTrack = -1
            if (code !== 0 || !(t >= 0) || !path) {
                root.say("cannot add a track for that")
                return
            }
            root.selTrack = t
            root.tlRun(["clip", root.proj, String(t), path,
                        "--at", String(addTrackProc.pendingAt)])
        }
    }

    // ── Starting a project ──────────────────────────────────────────────────
    //
    // A project file has to EXIST before any other verb works, so New both
    // creates it and lays down the tracks every cut needs. Coming up with an
    // empty track list and no way to add one was the first version's dead end.
    function newProject(path, noClobber) {
        newProjProc.unique  = false
        newProjProc.pending = path
        newProjProc.command = [root.bin, "timeline", "new", path,
                               "--size", "1920x1080", "--fps", "25"]
                              .concat(noClobber ? ["--no-clobber"] : [])
        newProjProc.running = true
    }

    // Never asks, and never writes over anything: the engine takes the next
    // free name and PRINTS the one it used. This is what the start screen's
    // door wants, and what a photograph dropped on the Video tab with nothing
    // open wants — being stopped to name a file is not what either gesture
    // asked for, and both used to land on the same fixed path.
    function newProjectUnique(path) {
        newProjProc.unique  = true
        newProjProc.pending = ""
        newProjProc.command = [root.bin, "timeline", "new", path,
                               "--size", "1920x1080", "--fps", "25", "--unique"]
        newProjProc.running = true
    }

    Process {
        id: newProjProc
        property bool   unique: false
        property string pending: ""
        property string got: ""
        stdout: StdioCollector { onStreamFinished: newProjProc.got = this.text.trim() }
        onExited: function (code, status) {
            // ⚠ The path is only KNOWN here. With --unique the engine chose
            // it, so setting root.proj before the run — which is what this
            // did — pointed the whole window at a file that was never written.
            const chosen = newProjProc.unique ? newProjProc.got
                                              : newProjProc.pending
            newProjProc.got = ""
            root.saveBusy = false
            if (code === 3) {
                root.saveReplace = true
                root.say("a project called that is there already — Replace writes over it")
                return
            }
            if (code !== 0 || !chosen) {
                root.say("cannot start a project there")
                return
            }
            root.saveOpen = false
            root.saveReplace = false
            root.proj = chosen
            root.selClip = -1
            root.playhead = 0
            trackProc.command = [root.bin, "timeline", "track", root.proj, "video", "V1"]
            trackProc.running = true
        }
    }
    Process {
        id: trackProc
        onExited: function (code, status) {
            track2Proc.command = [root.bin, "timeline", "track", root.proj, "audio", "A1"]
            track2Proc.running = true
        }
    }
    Process {
        id: track2Proc
        onExited: function (code, status) {
            root.selTrack = 0
            root.say("new project — " + root.proj.replace(/^.*\//, ""))
            root.reloadTimeline()
            // A file dropped on the Video tab with nothing open is waiting on
            // this: it put itself back on the queue and started the project.
            root.pumpDrop()
        }
    }

    // ── Playing it ──────────────────────────────────────────────────────────
    //
    // One frame costs an ffmpeg process and about a tenth of a second, so
    // compositing live at twenty-five of them a second is not something this
    // architecture can do, and pretending otherwise would mean a second
    // renderer that disagrees with the first one about colour.
    //
    // So playback is the EXPORT, played. `timeline export --preview` runs the
    // same graph at 960 wide with the encoder set to ultrafast, which means
    // what you watch is what you will ship — the same grades, transitions,
    // transforms and audio — only rougher. It is cached against a revision
    // counter, so pressing play again after watching costs nothing, and any
    // edit invalidates it.
    //
    // The file is fragmented mp4 on purpose: a player can open it while
    // ffmpeg is still writing, instead of waiting for a moov atom that does
    // not exist until the encode ends.

    property int  tlRev: 0              // bumped by every edit
    property int  playRev: -1           // the revision the preview was built from
    property bool playing: false
    property bool rendering: false
    property string playFile: ""

    function playPath(rev) { return root.scratch + "-play-" + rev + ".mp4" }

    function togglePlay() {
        if (!root.proj || root.tlDur <= 0) return
        if (!root.playbackReady) {
            root.say("playback needs qt6-multimedia installed")
            return
        }
        if (root.playing) { root.pausePlayback(); return }
        if (root.playRev === root.tlRev && root.playFile) { root.startPlayback(); return }
        root.startPreviewRender(true)
    }

    // The revision the render in flight is FOR. Reading tlRev again when it
    // finishes is wrong: an edit made during the render moves it, and the
    // finished file then gets filed under a name nothing wrote.
    property int renderingRev: -1
    property bool playAfterRender: false

    function startPreviewRender(thenPlay) {
        if (root.rendering || !root.proj || root.tlDur <= 0) return
        if (!root.playbackReady) return
        root.rendering = true
        root.playAfterRender = thenPlay
        root.renderingRev = root.tlRev
        if (thenPlay) root.say("rendering a preview to play…")
        playRenderProc.command = [root.bin, "timeline", "export", root.proj,
                                  "--out", root.playPath(root.tlRev), "--preview"]
        playRenderProc.running = true
    }

    // Ready before it is asked for.
    //
    // Waiting for the play button meant every first press cost a render, and
    // adding a clip left the editor unable to play the thing that had just
    // been added. This renders in the background once the edits stop — on a
    // timer, because otherwise dragging one slider would start a render per
    // tick and they would queue up behind each other for as long as the hand
    // kept moving.
    Timer {
        id: previewIdle
        interval: 1200
        onTriggered: {
            if (root.playRev !== root.tlRev && !root.playing)
                root.startPreviewRender(false)
        }
    }
    onTlRevChanged: previewIdle.restart()

    Process {
        id: playRenderProc
        stderr: StdioCollector { onStreamFinished: if (this.text) root.say(this.text.split("\n")[0]) }
        onExited: function (code, status) {
            root.rendering = false
            const rev = root.renderingRev
            const want = root.playAfterRender
            root.playAfterRender = false
            if (code !== 0) {
                if (want) root.say("could not render a preview")
                return
            }
            // The previous one is dropped only once its replacement exists, so
            // a failed render leaves the last watchable preview in place.
            if (root.playFile && root.playFile !== root.playPath(rev)) {
                rmProc.command = ["rm", "-f", root.playFile]
                rmProc.running = true
            }
            root.playFile = root.playPath(rev)
            root.playRev = rev
            if (want) { root.say(""); root.startPlayback() }
            // An edit landed while this was rendering, so it is already out of
            // date. Go round again rather than leaving a stale preview to be
            // discovered at the next press of play.
            else if (rev !== root.tlRev) previewIdle.restart()
        }
    }
    Process { id: rmProc }

    // A seek issued in the same breath as a source change is DISCARDED —
    // there is no media loaded yet to seek in, so playback silently starts at
    // zero and the first position update drags the playhead back to the top of
    // the timeline. Pressing play after scrubbing anywhere then looks like the
    // playhead being thrown away. Hold the seek until the media reports itself
    // loaded, and ignore position updates until it has landed.
    property real pendingSeek: -1

    // `seekArmed` is not belt and braces. Assigning a new source while a
    // previous one is still loaded emits LoadedMedia for the OUTGOING media
    // before the new file starts loading — so a seek honoured on the first
    // Loaded lands on the file being replaced, clears itself, and the new one
    // then starts at zero with nothing pending. Observed exactly that, and
    // only on the SECOND play, which is what made it look like re-rendering
    // was to blame.
    //
    // A load cycle always passes through LoadingMedia, so requiring one since
    // the seek was armed tells the real transition from the echo.
    property bool seekArmed: false

    function startPlayback() {
        if (!root.playbackReady) { root.say("playback needs qt6-multimedia"); return }
        const pl = playbackLoader.item
        const url = "file://" + root.playFile
        // String(), because a `url` property read from QML is an OBJECT and
        // not a string: `pl.source !== url` is ALWAYS true, and printing the
        // two beside each other shows the same characters, so nothing about
        // the comparison looks wrong.
        //
        // Every play press therefore took the new-source path and armed a
        // seek for a load that never came — assigning a url the value it
        // already holds notifies nothing, so no LoadingMedia ever armed it.
        // The seek stayed pending forever, playback carried on from wherever
        // it had been paused, and the playhead froze where it had been
        // scrubbed to, because position updates are ignored while a seek is
        // pending. Rendering the preview in the BACKGROUND is what made it
        // reachable: before that every press rendered a file with a new name,
        // so the source really did change and the pending seek really did
        // land.
        if (String(pl.source) !== url) {
            root.pendingSeek = root.playhead
            root.seekArmed = false
            pl.source = url
        } else {
            pl.seek(root.playhead * 1000)
        }
        root.playing = true
        pl.rate = root.playRate
        pl.play()
    }

    function pausePlayback() {
        if (!root.playbackReady) { root.playing = false; return }
        const pl = playbackLoader.item
        pl.pause()
        root.playing = false
        root.pendingSeek = -1
        root.seekArmed = false
        // Back to the frame-accurate monitor, at full resolution and at
        // exactly where the picture stopped — the preview is 960 wide and
        // rough, and it is not what anyone should be grading against.
        root.playhead = pl.position / 1000
        root.requestFrame()
    }

    // ── J K L ───────────────────────────────────────────────────────────────
    //
    // L plays and doubles on each press: that is the PLAYER'S rate on the
    // rendered preview, so a fast pass is still the export, played.
    //
    // ⚠ J cannot be the same thing. Nothing plays an H.264 preview backwards,
    // and rendering the timeline in reverse to watch it would be a second
    // renderer with its own opinion of the cut — the one thing this program
    // has refused since the first commit. So J is a SHUTTLE: the frame
    // monitor, stepped back one, two or four frames at a time as fast as it
    // answers. It is a scrub wearing the transport's vocabulary, and the
    // status line says which it is rather than letting the speed be a lie.
    property real playRate: 1
    property int  revStep: 0            // frames per tick, 0 = not shuttling

    function applyRate() {
        if (root.playbackReady) playbackLoader.item.rate = root.playRate
    }

    function shuttleForward() {
        root.stopShuttle()
        if (!root.playing) { root.playRate = 1; root.applyRate(); root.togglePlay(); return }
        root.playRate = root.playRate >= 8 ? 1 : root.playRate * 2
        root.applyRate()
        root.say("play ×" + root.playRate)
    }

    function shuttleBack() {
        if (!root.proj || root.tlDur <= 0) return
        if (root.playing) root.pausePlayback()
        root.revStep = root.revStep === 0 ? 1 : Math.min(4, root.revStep * 2)
        revTimer.start()
        root.say("shuttle back ×" + root.revStep + " (frames, not playback)")
    }

    function stopShuttle() {
        root.revStep = 0
        revTimer.stop()
    }

    // K, and every other reason to stop.
    function shuttleStop() {
        root.stopShuttle()
        root.playRate = 1
        root.applyRate()
        if (root.playing) root.pausePlayback()
    }

    // ── The source monitor, and three-point editing ─────────────────────────
    //
    // The cut had one viewer and one way in: whole files landed at the end of
    // a track and were trimmed afterwards. What an editor actually does is
    // decide the in and the out ON THE FOOTAGE first, put the playhead where
    // it goes, and let the third point follow from the other two.
    //
    // `synstudio source FILE --at S` is one frame of a file that is not in a
    // project yet — and it goes through that file's sidecar, so a developed
    // photograph looks in here exactly like the clip an insert will make of
    // it. A flat source viewer beside a graded timeline is a disagreement the
    // eye catches at once and cannot explain.
    property string srcFile: ""
    property real   srcDur: 0
    property real   srcPos: 0
    property real   srcIn: 0
    property real   srcOut: -1        // -1: to the end of the file
    property bool   srcShown: false
    property string srcUrl: ""
    property int    srcSerial: 0
    property bool   srcBusy: false
    property bool   srcAgain: false

    function openSource(path) {
        root.srcFile = path
        root.srcIn = 0
        root.srcOut = -1
        root.srcPos = 0
        root.srcDur = 0
        root.srcShown = true
        root.requestSourceFrame()
        root.say("source: " + path.replace(/^.*\//, ""))
    }

    // ⚠ The same coalescing the program monitor uses. `running = true` on a
    // busy Process is a silent no-op, so a scrub without this renders the
    // first frame of the gesture and nothing else.
    function requestSourceFrame() {
        if (!root.srcFile) return
        if (root.srcBusy) { root.srcAgain = true; return }
        root.srcBusy = true
        root.srcSerial++
        srcProc.command = [root.bin, "source", root.srcFile,
                           "--at", String(root.srcPos),
                           "--out", root.scratch + "-source.png",
                           "--size", "1400"]
        srcProc.running = true
    }

    Process {
        id: srcProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    // The file says how long it is; nothing here guesses.
                    if (f[0] === "duration") root.srcDur = parseFloat(f[1]) || 0
                }
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(this.text.split("\n")[0])
        }
        onExited: function (code, status) {
            root.srcBusy = false
            if (code === 0)
                root.srcUrl = "file://" + root.scratch + "-source.png?v="
                              + root.srcSerial
            if (root.srcAgain) { root.srcAgain = false; root.requestSourceFrame() }
        }
    }

    function srcSeek(t) {
        root.srcPos = Math.max(0, Math.min(root.srcDur > 0 ? root.srcDur : 0, t))
        root.requestSourceFrame()
    }

    function srcMarkIn()  {
        root.srcIn = root.srcPos
        // An out point before the in point is not a range. Pushing it rather
        // than refusing keeps the gesture — mark in, then out — working in
        // either order.
        if (root.srcOut >= 0 && root.srcOut <= root.srcIn) root.srcOut = -1
        root.say("in " + root.timecode(root.srcIn))
    }
    function srcMarkOut() {
        root.srcOut = root.srcPos
        if (root.srcOut <= root.srcIn) root.srcIn = 0
        root.say("out " + root.timecode(root.srcOut))
    }

    // The third point: where the playhead is. `insert` makes room on every
    // track, `overwrite` cuts a hole its own length on the selected one.
    function srcEdit(how) {
        if (!root.proj) { root.say("start a project first"); return }
        if (!root.srcFile) { root.say("open something in the source monitor"); return }
        if (root.selTrack < 0) { root.say("pick a track first"); return }
        const args = [how, root.proj, String(root.selTrack), root.srcFile,
                      "--at", String(root.playhead), "--in", String(root.srcIn)]
        if (root.srcOut > root.srcIn) { args.push("--out-at"); args.push(String(root.srcOut)) }
        root.tlRun(args)
        root.say(how === "insert" ? "inserted at the playhead"
                                  : "overwrote at the playhead")
    }

    Timer {
        id: revTimer
        interval: 80
        repeat: true
        // ⚠ Faster than the monitor can answer ON PURPOSE. The frame request
        // is already coalesced — one in flight and one queued — so a tick
        // that arrives early is dropped rather than queued up behind the
        // render, and the shuttle runs at whatever the machine can do.
        onTriggered: {
            if (root.revStep === 0 || root.playhead <= 0) {
                root.stopShuttle()
                return
            }
            root.seekTo(root.playhead - root.revStep / (root.tl.fps || 25))
        }
    }

    // The player itself lives in synstudio-playback.qml, behind a Loader,
    // because it is the only thing here that needs QtMultimedia and a missing
    // import fails the whole FILE rather than the feature. See that file.
    readonly property bool playbackReady: playbackLoader.status === Loader.Ready
                                          && playbackLoader.item !== null
    // The player arrives after these are set, so it has to be told once when
    // it does — otherwise the first play ignores a muted monitor.
    onPlaybackReadyChanged: root.pushMonitor()

    Connections {
        target: playbackLoader.item
        enabled: root.playbackReady

        function onPositionMoved(ms) {
            if (root.playing && root.pendingSeek < 0) root.playhead = ms / 1000
        }
        function onStatusMoved(status) {
            const pl = playbackLoader.item
            if (status === pl.statusLoading) { root.seekArmed = true; return }
            if (root.pendingSeek >= 0 && root.seekArmed
                && (status === pl.statusLoaded || status === pl.statusBuffered)) {
                pl.seek(root.pendingSeek * 1000)
                root.pendingSeek = -1
                root.seekArmed = false
            }
        }
        function onStateMoved(state) {
            if (state === playbackLoader.item.stateStopped && root.playing) {
                root.playing = false
                root.requestFrame()
            }
        }
        function onFailed(text) {
            root.playing = false
            root.say("cannot play the preview: " + text)
        }
    }

    // ── Scrubbing, and the clock ────────────────────────────────────────────
    function seekTo(t) {
        if (root.scrubbing) t = root.snap(t, -1, -1, true)
        root.playhead = Math.max(0, Math.min(root.tlDur > 0 ? root.tlDur : 0, t))
        // Scrubbing during playback moves the PLAYER. Moving only the playhead
        // would have it snap straight back on the next position update, which
        // reads as the ruler refusing to be dragged.
        if (root.playing && root.playbackReady) {
            if (root.pendingSeek >= 0) root.pendingSeek = root.playhead
            else playbackLoader.item.seek(root.playhead * 1000)
        }
        else              root.requestFrame()
    }

    function timecode(t) {
        if (!(t >= 0)) t = 0
        const m = Math.floor(t / 60)
        const sec = Math.floor(t % 60)
        const fr = Math.floor((t - Math.floor(t)) * (root.tl.fps || 25))
        return (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec
             + "." + (fr < 10 ? "0" : "") + fr
    }

    function clipRowsIn(group) {
        const out = []
        for (let i = 0; i < root.clipRows.length; i++)
            if (root.clipRows[i].group === group) out.push(root.clipRows[i])
        return out
    }

    // The grade panel offers the pointwise half of the develop stack, which is
    // what a 3D LUT can actually carry. Clarity, sharpening, noise and grain
    // need a neighbouring pixel, so they are not colour and are NOT in the
    // cube — the engine says so on the command line and the panel should not
    // quietly imply otherwise by offering them here.
    //
    // LUT is on the list by the same test and not as an exception: an
    // imported look is applied at the end of the pointwise chain, so it bakes
    // into the clip's cube along with everything above it. Last, because that
    // is where it lands.
    readonly property var gradeGroups: ["Basic", "Colour mixer", "Grading", "LUT"]

    // ── Dragging a clip ─────────────────────────────────────────────────────
    //
    // The drag is DRAWN while the mouse moves and committed once on release.
    // Issuing a move per mouse move would queue dozens of engine calls behind
    // each other and the clip would crawl after the hand — and `running = true`
    // on a busy Process is a silent no-op here, so most of them would simply
    // vanish and the clip would land somewhere nobody asked for.
    property int  dragTrack: -1
    property int  dragClip: -1
    property string dragKind: ""
    property real dragFrom: 0
    property real dragDx: 0

    // Edges are magnetic. Without this, butting two clips together by hand
    // leaves a gap of a few milliseconds that shows as a black flash on
    // export and is invisible at any sane zoom.
    // Eight PIXELS, not eight frames: the tolerance has to shrink as the zoom
    // grows, or a tight trim at high zoom keeps leaping to the cut beside it.
    //
    // `ignorePlayhead` because a PLAYHEAD drag must not snap to the playhead,
    // which is a fixed point of that gesture and would pin it where it is.
    function snap(t, ignoreTrack, ignoreClip, ignorePlayhead) {
        if (!root.snapping) return t
        const tol = 8 / root.pxPerSec
        let best = t, bestD = tol
        function tryEdge(e) {
            const d = Math.abs(e - t)
            if (d < bestD) { bestD = d; best = e }
        }
        tryEdge(0)
        if (!ignorePlayhead) tryEdge(root.playhead)
        for (let i = 0; i < root.tl.tracks.length; i++)
            for (let j = 0; j < root.tl.tracks[i].clips.length; j++) {
                if (i === ignoreTrack && j === ignoreClip) continue
                const c = root.tl.tracks[i].clips[j]
                tryEdge(c.tlIn)
                tryEdge(c.tlIn + c.len)
            }
        // A marker is a place somebody chose. It is the one snap target that
        // is there BECAUSE it is worth landing on.
        const mk = root.tl.markers || []
        for (let m = 0; m < mk.length; m++) tryEdge(mk[m].t)
        return best
    }

    function commitDrag(clip, track) {
        const kind = root.dragKind, dx = root.dragDx
        const cl = root.dragClip
        root.dragTrack = -1; root.dragClip = -1; root.dragKind = ""; root.dragDx = 0
        if (!kind || Math.abs(dx) < 2) return
        const dt = dx / root.pxPerSec

        if (kind === "move") {
            const to = root.snap(Math.max(0, clip.tlIn + dt), track, cl)
            root.tlRun(["move", root.proj, String(track), String(cl),
                        "--to", String(Math.round(to * 1000) / 1000)])
        } else if (kind === "head") {
            // + shortens the head. The engine moves the in point and the
            // position together so the frame under the cursor stays put.
            //
            // Snapped on the EDGE being dragged rather than on the delta: an
            // edge pulled to meet the next cut is the whole reason snapping
            // exists, and a delta has nothing to land on.
            const edge = root.snap(clip.tlIn + dt, track, cl)
            root.tlRun(["trim", root.proj, String(track), String(cl),
                        "--head", String(Math.round((edge - clip.tlIn) * 1000) / 1000)])
        } else {
            const edge = root.snap(clip.tlIn + clip.len + dt, track, cl)
            root.tlRun(["trim", root.proj, String(track), String(cl),
                        "--tail", String(Math.round(
                            (edge - clip.tlIn - clip.len) * 1000) / 1000)])
        }
    }

    // ── Exporting the cut ───────────────────────────────────────────────────
    //
    // A minute of timeline takes long enough that a window which says nothing
    // reads as a window that ignored the click — and pressing again could not
    // help, because `running = true` on a Process that is ALREADY running is a
    // silent no-op in quickshell. So the second press really did do nothing,
    // and so did the third. The button goes busy and stays busy until the
    // encode ends.
    //
    // The progress is ffmpeg's own, and it writes `time=00:00:07.89` to stderr
    // on a CARRIAGE RETURN — which is exactly what a StdioCollector cannot
    // show, because it fires once, at the end of the stream. Splitting on "\r"
    // is what turns it into something to watch.
    property bool   exportingCut: false
    property real   exportPct: -1
    property string exportErr: ""
    property string exportOut: ""

    function exportCut() {
        if (root.exportingCut || !root.proj || !(root.tlDur > 0)) return
        root.exportingCut = true
        root.exportPct = -1
        root.exportErr = ""
        tlExportProc.command = [root.bin, "timeline", "export", root.proj, "--out",
                                root.proj.replace(/\.[^.\/]*$/, "") + ".mp4"]
        tlExportProc.running = true
        root.say("exporting the cut…")
    }

    Process {
        id: tlExportProc
        stderr: SplitParser {
            splitMarker: "\r"
            onRead: function (line) {
                const m = /time=(\d+):(\d\d):(\d\d(?:\.\d+)?)/.exec(line)
                if (!m) {
                    // Not progress, so it is the thing worth quoting if this
                    // ends badly. ffmpeg's last word is the one that says why.
                    const t = line.trim()
                    if (t) root.exportErr = t.split("\n").pop()
                    return
                }
                if (!(root.tlDur > 0)) return
                const at = parseInt(m[1]) * 3600 + parseInt(m[2]) * 60 + parseFloat(m[3])
                root.exportPct = Math.max(0, Math.min(100, at / root.tlDur * 100))
                root.say("exporting the cut… " + Math.round(root.exportPct) + "%")
            }
        }
        onExited: function (code, status) {
            root.exportingCut = false
            root.exportPct = -1
            // The path it actually wrote, not one reconstructed from the
            // project's name — those agreed only while the name and the
            // format were both assumed.
            root.say(code === 0 ? "exported " + root.exportOut
                                : (root.exportErr || "export failed"))
        }
    }

    Component.onCompleted: {
        cbProc.running = true
        keysProc.running = true
        clipKeysProc.running = true
        transListProc.running = true
        styleListProc.running = true
        fontListProc.running = true
        fxListProc.running = true
        fxParamProc.running = true
        lookListProc.running = true
        lutListProc.running = true
        if (root.proj) {
            root.selTrack = 0
            root.reloadTimeline()
            previewIdle.restart()
        }
        // Launched bare, the window shows the start screen below. It used to
        // open the photo picker immediately, which answers a question nobody
        // asked: this program edits photographs AND cuts video, and deciding
        // for someone that they meant the darkroom is the wrong half of it
        // half the time. An empty window with dead controls was the version
        // before that, and the picker was the fix for it — the start screen
        // is the fix that does not pick a side.
    }

    // ── Layout ──────────────────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: root.cBg

        Column {
            anchors.fill: parent

            // Top strip
            // A Flow, not a Row. Seven buttons and two tabs do not fit a
            // narrow window, and a Row simply runs off the edge — Export and
            // Ripple delete were unreachable rather than merely cramped.
            // Wrapping costs a second line only when there is no other way to
            // show them, and every action stays clickable at any width.
            Rectangle {
                id: topStrip
                width: parent.width
                height: Math.max(46, topBar.height + 12)
                color: root.cPanel

                Flow {
                    id: topBar
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    // The two halves of the application. One binary, one
                    // colour engine, two front pages — a still and a cut are
                    // different work with different tools on screen, and
                    // pretending one panel serves both is how an editor ends
                    // up serving neither.
                    Tab { label: "Photo"; on: root.mode === "photo"
                          onClicked: { root.mode = "photo"
                                       root.say(root.file ? "" : "open a photograph") } }
                    Tab {
                        id: videoTab
                        label: "Video"
                        // Lit for the whole gesture, and BRIGHT when the
                        // pointer is actually on it: a target that only
                        // appears once you have already hit it is not a
                        // target, and one that never says "here" is a guess.
                        on: root.mode === "video" || videoTabDrop.containsDrag
                            || root.photoCarrying
                        onClicked: { root.mode = "video"
                                     root.say(root.proj ? "" : "New project, then Add media") }

                        DropArea {
                            id: videoTabDrop
                            anchors.fill: parent
                            // A photograph dragged from the darkroom, and a
                            // file dragged in from anywhere else.
                            // Files dragged in from a file manager. The
                            // window's OWN photograph no longer comes through
                            // here at all — see the note on photoDragMa.
                            keys: ["text/uri-list", "text/plain"]
                            // A little wider than the word: a drop target the
                            // size of its label is a target you have to aim at.
                            anchors.margins: -6
                            onDropped: function (drop) {
                                // No project open is not a refusal any more:
                                // the drop starts one. Dropping the
                                // photograph on the tab IS the ask.
                                root.mode = "video"
                                if (drop.source && drop.source.filePath)
                                    root.dropUrls(["file://"
                                                   + encodeURI(drop.source.filePath)])
                                else if (drop.hasUrls) root.dropUrls(drop.urls)
                                else if (drop.hasText) root.dropUrls([drop.text])
                                drop.acceptProposedAction()
                            }
                        }
                    }

                    Item { width: 10; height: 1 }

                    Btn { visible: root.mode === "photo"
                          label: "Open";  onClicked: root.openPicker() }
                    Btn { visible: root.mode === "photo"
                          label: root.exportingStill ? "Export…" : "Export"
                          active: root.file !== "" && !root.exportingStill
                          onClicked: root.openExport() }
                    // A thumbnail is a second picture made from this one,
                    // so it is a MODE of the darkroom rather than a page:
                    // every develop control is still the thing underneath it.
                    Btn { visible: root.mode === "photo"; label: "Thumbnail"
                          on: root.thumbOpen
                          active: root.file !== ""
                          onClicked: {
                              root.thumbOpen = !root.thumbOpen
                              if (root.thumbOpen) {
                                  root.loadThumb()
                                  root.requestThumb()
                              }
                          } }
                    Btn { visible: root.mode === "photo"; label: "Undo"
                          active: root.devUndo > 0
                          onClicked: root.devStep("undo") }
                    Btn { visible: root.mode === "photo"; label: "Redo"
                          active: root.devRedo > 0
                          onClicked: root.devStep("redo") }
                    // ⚠ Reset is an edit like any other now, so Undo takes it
                    // back. It used to delete the sidecar, which put the work
                    // it threw away outside the history entirely.
                    Btn { visible: root.mode === "photo"
                          label: "Reset"; active: root.file !== ""
                          onClicked: root.resetPhoto() }

                    Btn { visible: root.mode === "video"; label: "New project"
                          onClicked: root.openNewProject() }
                    // Every edit is already on disk; this is what gives the
                    // file its NAME, and takes a copy at a point worth coming
                    // back to.
                    Btn { visible: root.mode === "video"; label: "Save as"
                          active: root.proj !== ""
                          onClicked: root.openSaveAs() }
                    // No selected track needed: the file picks its own track.
                    Btn { visible: root.mode === "video"; label: "Add media"
                          active: root.proj !== ""
                          onClicked: { root.pickerFor = "clip"; root.openPicker() } }
                    Btn { visible: root.mode === "video"; label: "Title"
                          active: root.proj !== "" && root.selTrack >= 0
                          onClicked: root.tlRun(["title", root.proj, String(root.selTrack),
                                                 "Title", "--at", String(root.playhead),
                                                 "--dur", "3"]) }
                    Btn { visible: root.mode === "video"; label: "Split"
                          active: root.proj !== "" && root.selTrack >= 0
                          onClicked: root.tlRun(["split", root.proj, String(root.selTrack),
                                                 "--at", String(root.playhead)]) }
                    // The cut under the playhead, with the overlap it needs.
                    // Setting the kind is a property and would leave the two
                    // clips butted together with nothing to dissolve THROUGH,
                    // which reads as the transition not working.
                    Btn { visible: root.mode === "video"; label: "Transition"
                          active: root.proj !== "" && root.selTrack >= 0
                          onClicked: root.tlRun(["transition", root.proj,
                                                 String(root.selTrack),
                                                 "--at", String(root.playhead),
                                                 "--kind", root.defaultTrans,
                                                 "--dur", String(root.defaultTransDur)]) }
                    Btn { visible: root.mode === "video"
                          label: root.selMore.length > 0
                                 ? "Delete " + (root.selMore.length + 1) : "Delete"
                          active: root.selClip >= 0
                          onClicked: root.deleteSelection(false) }
                    Btn { visible: root.mode === "video"; label: "Ripple delete"
                          active: root.selClip >= 0
                          onClicked: root.deleteSelection(true) }
                    Btn { visible: root.mode === "video"; label: "Copy"
                          active: root.selClip >= 0
                          onClicked: root.copyClip() }
                    // ⚠ Inactive with nothing on it, rather than pressed to
                    // find out: the clipboard is a file that outlives the
                    // window, so "is there anything to paste" is a question
                    // only the engine can answer.
                    Btn { visible: root.mode === "video"
                          label: root.cbKind === "" ? "Paste"
                                 : "Paste " + root.cbKind
                          active: root.proj !== "" && root.selTrack >= 0
                                  && root.cbKind !== ""
                          onClicked: root.pasteClip() }
                    Btn { visible: root.mode === "video"; label: "Undo"
                          active: root.undoDepth > 0
                          onClicked: root.undoEdit() }
                    Btn { visible: root.mode === "video"; label: "Redo"
                          active: root.redoDepth > 0
                          onClicked: root.redoEdit() }
                    Btn { visible: root.mode === "video"; label: "Mark"
                          active: root.proj !== ""
                          onClicked: root.addMarker() }
                    Btn { visible: root.mode === "video"; label: "Snap"
                          on: root.snapping
                          onClicked: root.snapping = !root.snapping }
                    Btn { visible: root.mode === "video"; label: "Mixer"
                          on: root.mixerOpen
                          active: root.proj !== ""
                          onClicked: root.panelMode =
                              root.mixerOpen ? "clip" : "mixer" }
                    Btn { visible: root.mode === "video"; label: "Voiceover"
                          on: root.voOpen
                          active: root.proj !== ""
                          onClicked: {
                              root.panelMode = root.voOpen ? "clip" : "voiceover"
                              if (root.voOpen && root.voDevices.length === 0)
                                  root.loadDevices()
                          } }
                    Btn { visible: root.mode === "video"
                          label: root.normalising ? "…" : "Normalise"
                          active: root.selClip >= 0 && !root.normalising
                          onClicked: root.normaliseClip() }
                    Btn { visible: root.mode === "video"
                          label: root.exportingCut ? "Export…" : "Export"
                          active: root.proj !== "" && root.tlDur > 0
                                  && !root.exportingCut
                          onClicked: root.openExport() }
                    // ⚠ A button for the key list. Bindings nobody can find
                    // are half a feature, and "press ? to see the keys" is
                    // itself a key nobody has been told about.
                    // Program or source in the one viewer. Named, because
                    // an editor knows both words and a glyph for either would
                    // be somebody's guess.
                    Btn { visible: root.mode === "video"
                          label: root.srcShown ? "Program" : "Source"
                          on: root.srcShown
                          onClicked: {
                              root.srcShown = !root.srcShown
                              if (root.srcShown && !root.srcFile) {
                                  root.pickerFor = "source"
                                  root.openPicker()
                              }
                          } }
                    Btn { label: "Keys"; on: root.helpOpen
                          onClicked: root.helpOpen = !root.helpOpen }
                }

            }

            Row {
                visible: root.mode === "photo"
                width: parent.width
                height: parent.height - topStrip.height - 24

                // ── The picture ─────────────────────────────────────────────
                Rectangle {
                    width: parent.width - root.panelW
                    height: parent.height
                    color: root.cViewport

                    Monitor {
                        id: preview
                        anchors.fill: parent
                        anchors.margins: 18
                        source: root.previewUrl
                        visible: root.previewUrl !== "" && !root.thumbOpen
                    }

                    // The thumbnail, in the same rectangle as the photograph
                    // — one viewer, so the picture is as big as the window
                    // can make it and nothing moves when the panel opens.
                    Monitor {
                        anchors.fill: parent
                        anchors.margins: 18
                        source: root.thumbUrl
                        visible: root.thumbOpen && root.thumbUrl !== ""
                    }

                    // Drag the photograph onto the Video tab to put it in the
                    // cut.
                    //
                    // The two pages are never on screen together, so there is
                    // nowhere on the timeline to drop it — the TAB is the
                    // target, which is also where the hand is already going.
                    // It carries a text/uri-list, the same thing a file
                    // manager sends, so the window's own drop handler takes it
                    // without knowing where it came from.
                    Item {
                        id: photoDrag
                        anchors.fill: parent
                        visible: root.mode === "photo" && root.file !== ""

                        // ⛔ NO `Drag` ATTACHED PROPERTY, AND NO DropArea.
                        //
                        // Three releases in a row lost this gesture to Qt's
                        // drag machinery: a window-wide DropArea that ate the
                        // event before the tab saw it, and `drag.target`
                        // writing an item's position from a start it captured
                        // at PRESS — before any handler could correct it. Both
                        // were fixed and it still did not arrive.
                        //
                        // So it is not Qt's drag any more. This is a press, a
                        // threshold, a pointer position and a release, all in
                        // this window's own coordinates: the hit test is one
                        // rectangle check written here, and the drop is a
                        // direct call. Nothing is hit-tested by a filter this
                        // file cannot see, and every step of it can be driven
                        // without a mouse — which is what let it be tested at
                        // last.
                        //
                        // Nothing is lost by leaving Qt's drag behind: this
                        // gesture never left the window. Files dragged IN from
                        // a file manager are a different path entirely, and
                        // still a DropArea.
                        MouseArea {
                            id: photoDragMa
                            anchors.fill: parent
                            // The gesture has to start before the develop
                            // panel's Flickable decides it was a scroll.
                            preventStealing: true
                            cursorShape: root.photoCarrying ? Qt.ClosedHandCursor
                                                            : Qt.ArrowCursor

                            property real pressX: 0
                            property real pressY: 0

                            onPressed: function (m) {
                                pressX = m.x
                                pressY = m.y
                                root.photoCarrying = false
                            }
                            onPositionChanged: function (m) {
                                if (!pressed) return
                                // Twelve pixels before it is a drag at all, so
                                // a click on the picture stays a click.
                                if (!root.photoCarrying
                                    && Math.abs(m.x - pressX) < 12
                                    && Math.abs(m.y - pressY) < 12) return
                                root.photoCarrying = true
                                root.photoDragTo(m.x, m.y)
                            }
                            // ⚠ The release position is still in THIS item's
                            // coordinates even when the pointer has left it —
                            // the grab holds — so a release over the tab
                            // arrives as a negative y, which is exactly the
                            // number the drop test wants.
                            onReleased: function (m) {
                                if (!root.photoCarrying) return
                                root.photoCarrying = false
                                root.photoDropAt(m.x, m.y)
                            }
                            onCanceled: root.photoCarrying = false
                        }

                        // What the hand is carrying. An invisible drag is why
                        // "is it even dragging?" was a question at all.
                        Rectangle {
                            visible: root.photoCarrying
                            x: Math.max(0, Math.min(parent.width - width,
                                                    root.photoDragX + 14))
                            y: Math.max(0, Math.min(parent.height - height,
                                                    root.photoDragY + 10))
                            width: ghostText.implicitWidth + 18
                            height: 26
                            radius: 4
                            color: root.cPanel
                            opacity: 0.92
                            border.width: 1
                            border.color: root.cAccent

                            Text {
                                id: ghostText
                                anchors.centerIn: parent
                                text: "→ " + root.file.replace(/^.*\//, "")
                                color: root.cText
                                font.pixelSize: root.ui(11)
                                font.family: root.uiFont
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.file === ""
                        text: "Open a photograph"
                        color: "#9a9a9a"
                        font.pixelSize: root.ui(18)
                        font.family: root.uiFont
                    }

                    // A quiet mark while a render is in flight. Not a spinner
                    // over the picture: the whole job of this pane is to show
                    // the photograph, and covering it to say "working" defeats
                    // the point of a live preview.
                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 10
                        width: 8; height: 8; radius: 4
                        color: root.cAccent
                        opacity: root.renderBusy ? 0.9 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }
                }

                // ── The develop panel ───────────────────────────────────────
                Rectangle {
                    width: root.panelW
                    height: parent.height
                    color: root.cPanel

                    Column {
                        anchors.fill: parent

                        // Histogram
                        Rectangle {
                            width: parent.width
                            height: 110
                            color: "#1c1c1c"

                            Canvas {
                                id: histCanvas
                                anchors.fill: parent
                                anchors.margins: 6
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    const h = root.hist
                                    if (!h || h.length === 0) return
                                    const w = width, ht = height
                                    // Additive drawing: where all three channels
                                    // overlap the result reads white, which is
                                    // how a histogram is supposed to look and
                                    // what makes a colour cast visible at all.
                                    ctx.globalCompositeOperation = "lighter"
                                    const chans = [["r", "#ff4d4d"], ["g", "#4dff88"], ["b", "#4d9bff"]]
                                    for (let c = 0; c < 3; c++) {
                                        ctx.beginPath()
                                        ctx.moveTo(0, ht)
                                        for (let i = 0; i < h.length; i++) {
                                            const v = h[i][chans[c][0]] / root.histMax
                                            ctx.lineTo(i / (h.length - 1) * w,
                                                       ht - Math.pow(v, 0.45) * ht)
                                        }
                                        ctx.lineTo(w, ht)
                                        ctx.closePath()
                                        ctx.fillStyle = chans[c][1]
                                        ctx.globalAlpha = 0.55
                                        ctx.fill()
                                    }
                                }
                            }

                            // Clipping warnings, which are the only reason to
                            // look at a histogram in a hurry.
                            Text {
                                anchors.left: parent.left; anchors.bottom: parent.bottom
                                anchors.margins: 6
                                visible: root.clipLo > 0
                                text: "▼ " + root.clipLo
                                color: "#4d9bff"
                                font.pixelSize: root.ui(10)
                                font.family: root.uiFont
                            }
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.margins: 6
                                visible: root.clipHi > 0
                                text: root.clipHi + " ▲"
                                color: "#ff6b6b"
                                font.pixelSize: root.ui(10)
                                font.family: root.uiFont
                            }
                        }

                        // ── The thumbnail panel ─────────────────────
                        //
                        // Its rows come from `thumb keys`, grouped the way
                        // that table groups them — Canvas, then a block per
                        // caption. A control here is a row there and nothing
                        // else, which is why the panel cannot offer a setting
                        // the renderer does not have.
                        Flickable {
                            width: parent.width
                            height: parent.height - 110
                            visible: root.thumbOpen
                            contentHeight: thumbCol.height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            Column {
                                id: thumbCol
                                width: parent.width

                                Repeater {
                                    model: root.thumbGroups

                                    Column {
                                        id: tgrp
                                        required property var modelData
                                        required property int index
                                        width: thumbCol.width
                                        // The canvas open, the captions
                                        // closed: three text blocks unrolled
                                        // is a panel nobody can find the top
                                        // of, and the first thing anybody
                                        // does is choose where it is going.
                                        property bool open: tgrp.index === 0

                                        Rectangle {
                                            width: parent.width
                                            height: 30
                                            color: root.wash(0.10)
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.left: parent.left
                                                anchors.leftMargin: 12
                                                text: (tgrp.open ? "▾  " : "▸  ")
                                                      + tgrp.modelData
                                                color: root.cText
                                                font.pixelSize: root.ui(12)
                                                font.family: root.uiFont
                                                font.bold: true
                                            }
                                            // What the caption says, on the
                                            // header, so a closed block is
                                            // still legible.
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.right: parent.right
                                                anchors.rightMargin: 12
                                                width: parent.width / 2
                                                horizontalAlignment: Text.AlignRight
                                                elide: Text.ElideRight
                                                visible: !tgrp.open && tgrp.index > 0
                                                text: root.thumbValue("text"
                                                          + tgrp.index + ".words")
                                                color: root.cDim
                                                font.pixelSize: root.ui(10)
                                                font.family: root.uiFont
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: tgrp.open = !tgrp.open
                                            }
                                        }

                                        Repeater {
                                            model: tgrp.open ? root.thumbRows : []
                                            ThumbCtl {
                                                required property var modelData
                                                required property int index
                                                row: modelData
                                                group: tgrp.modelData
                                            }
                                        }
                                    }
                                }

                                Item { width: 1; height: 10 }

                                Row {
                                    x: 12
                                    spacing: 8
                                    Btn { label: "Export thumbnail"
                                          active: root.file !== ""
                                          onClicked: { root.exportKind = "thumb"
                                                       root.openExport() } }
                                    Btn { label: "Reset"
                                          active: root.file !== ""
                                          onClicked: root.resetThumb() }
                                }

                                Item { width: 1; height: 14 }
                            }
                        }

                        // Every group, every control, from the engine's table.
                        Flickable {
                            width: parent.width
                            height: parent.height - 110
                            visible: !root.thumbOpen
                            contentHeight: panelCol.height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            Column {
                                id: panelCol
                                width: parent.width

                                // ── Looks ───────────────────────────────
                                //
                                // Above the controls, because that is the
                                // order the work happens in: pick a starting
                                // point, then adjust it. A look SETS the
                                // fields it names and leaves the rest, so the
                                // exposure and white balance a photograph
                                // needed are still there underneath — and
                                // every slider it moved is still a slider.
                                Column {
                                    id: lookGrp
                                    width: panelCol.width
                                    property bool open: false

                                    Rectangle {
                                        width: parent.width
                                        height: 30
                                        color: root.wash(0.10)
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 12
                                            text: (lookGrp.open ? "▾  " : "▸  ") + "Looks"
                                            color: root.cText
                                            font.pixelSize: root.ui(12)
                                            font.family: root.uiFont
                                            font.bold: true
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: lookGrp.open = !lookGrp.open
                                        }
                                    }

                                    Repeater {
                                        model: lookGrp.open ? root.lookList : []
                                        Rectangle {
                                            id: lookRow
                                            required property var modelData
                                            width: panelCol.width
                                            height: 28
                                            color: lookArea.containsMouse ? root.wash(0.16)
                                                                          : "transparent"
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.left: parent.left
                                                anchors.leftMargin: 20
                                                anchors.right: parent.right
                                                anchors.rightMargin: 12
                                                text: lookRow.modelData.label
                                                elide: Text.ElideRight
                                                color: root.cText
                                                font.pixelSize: root.ui(11)
                                                font.family: root.uiFont
                                            }
                                            MouseArea {
                                                id: lookArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: root.applyLook(lookRow.modelData.name)
                                            }
                                        }
                                    }
                                }

                                Repeater {
                                    model: root.groups

                                    Column {
                                        id: grp
                                        required property string modelData
                                        width: panelCol.width
                                        // The colour mixer is 24 controls and
                                        // the curve rows are text, not sliders;
                                        // both start folded so the panel opens
                                        // on the controls people reach for.
                                        property bool open: modelData === "Basic"
                                                            || modelData === "Presence"

                                        Rectangle {
                                            width: parent.width
                                            height: 30
                                            color: root.wash(0.10)
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.left: parent.left
                                                anchors.leftMargin: 12
                                                text: (grp.open ? "▾  " : "▸  ") + grp.modelData
                                                color: root.cText
                                                font.pixelSize: root.ui(12)
                                                font.family: root.uiFont
                                                font.bold: true
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: grp.open = !grp.open
                                            }
                                        }

                                        Repeater {
                                            model: grp.open ? root.rowsIn(grp.modelData) : []
                                            Slider2 {}
                                        }
                                    }
                                }

                                Item { width: 1; height: 16 }
                            }
                        }
                    }
                }
            }

            // ── The video page ──────────────────────────────────────────
            Row {
                visible: root.mode === "video"
                width: parent.width
                height: parent.height - topStrip.height - 24

                Column {
                    width: parent.width - root.panelW
                    height: parent.height

                    // ── Program monitor ─────────────────────────────────
                    //
                    // Neutral grey around the picture for the same reason the
                    // darkroom is: a grade is judged against what surrounds
                    // it, and a themed accent behind the frame pulls every
                    // decision made in front of it.
                    Rectangle {
                        width: parent.width
                        height: parent.height - 34 - 224
                        color: root.cViewport

                        Monitor {
                            anchors.fill: parent
                            anchors.margins: 14
                            source: root.frameUrl
                            visible: root.proj !== "" && root.tlDur > 0
                                     && !root.playing
                        }

                        // Playback draws here instead of the still monitor.
                        // Same rectangle, same margins, so the picture does
                        // not move when play is pressed.
                        //
                        // Qt.resolvedUrl, so this finds its sibling both in
                        // the source tree and in SYNSTUDIO_DATADIR without
                        // anybody having to tell it which one it is in.
                        Loader {
                            id: playbackLoader
                            anchors.fill: parent
                            anchors.margins: 14
                            visible: root.playing
                            asynchronous: false
                            source: Qt.resolvedUrl("synstudio-playback.qml")
                        }

                        // ── The source viewer ───────────────────────
                        //
                        // The SAME rectangle as the program monitor, not a
                        // second pane. Two viewers side by side on a 1400
                        // wide window leaves each of them too small to judge
                        // anything, and what a source monitor is for is
                        // deciding where a shot starts — which needs the
                        // picture at the size the window can give it.
                        Rectangle {
                            anchors.fill: parent
                            visible: root.srcShown
                            color: root.cViewport

                            Monitor {
                                anchors.fill: parent
                                anchors.margins: 14
                                anchors.bottomMargin: 58
                                source: root.srcUrl
                                visible: root.srcUrl !== ""
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: root.srcFile === ""
                                text: "Open something to edit from"
                                color: "#9a9a9a"
                                font.pixelSize: root.ui(16)
                                font.family: root.uiFont
                            }

                            // Its own scrub bar, with the in and out points
                            // drawn ON it: a range you cannot see is a range
                            // nobody trusts, and the numbers alone do not say
                            // how much of the file it is.
                            Rectangle {
                                id: srcBar
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 14
                                anchors.bottomMargin: 30
                                height: 16
                                radius: 3
                                color: root.wash(0.14)
                                visible: root.srcDur > 0

                                readonly property real inFrac:
                                    root.srcDur > 0 ? root.srcIn / root.srcDur : 0
                                readonly property real outFrac:
                                    root.srcDur > 0
                                    ? (root.srcOut > root.srcIn ? root.srcOut : root.srcDur)
                                      / root.srcDur : 1

                                Rectangle {
                                    x: srcBar.inFrac * srcBar.width
                                    width: Math.max(2, (srcBar.outFrac - srcBar.inFrac)
                                                       * srcBar.width)
                                    height: parent.height
                                    radius: 3
                                    color: root.wash(0.42)
                                }
                                Rectangle {
                                    x: (root.srcDur > 0 ? root.srcPos / root.srcDur : 0)
                                       * srcBar.width - 1
                                    width: 2
                                    height: parent.height
                                    color: root.cAccent
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -8
                                    preventStealing: true
                                    function scrub(mx) {
                                        root.srcSeek((mx + 8) / srcBar.width * root.srcDur)
                                    }
                                    onPressed: function (m) { scrub(m.x) }
                                    onPositionChanged: function (m) { if (pressed) scrub(m.x) }
                                }
                            }

                            Row {
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 14
                                anchors.bottomMargin: 6
                                spacing: 6

                                Btn { label: "Open"
                                      onClicked: { root.pickerFor = "source"
                                                   root.openPicker() } }
                                Btn { label: "Mark in";  active: root.srcFile !== ""
                                      onClicked: root.srcMarkIn() }
                                Btn { label: "Mark out"; active: root.srcFile !== ""
                                      onClicked: root.srcMarkOut() }
                                Btn { label: "Insert"
                                      active: root.srcFile !== "" && root.proj !== ""
                                              && root.selTrack >= 0
                                      onClicked: root.srcEdit("insert") }
                                Btn { label: "Overwrite"
                                      active: root.srcFile !== "" && root.proj !== ""
                                              && root.selTrack >= 0
                                      onClicked: root.srcEdit("overwrite") }
                            }

                            Text {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.rightMargin: 14
                                anchors.bottomMargin: 10
                                text: root.timecode(root.srcPos) + "   in "
                                      + root.timecode(root.srcIn) + "   out "
                                      + (root.srcOut > root.srcIn
                                         ? root.timecode(root.srcOut) : "end")
                                color: root.cDim
                                font.pixelSize: root.ui(11)
                                font.family: "monospace"
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: root.proj === ""
                            text: "New project, then Add media"
                            color: "#9a9a9a"
                            font.pixelSize: root.ui(18)
                            font.family: root.uiFont
                        }

                        // Rendering a preview is the one wait in this window
                        // long enough to need saying out loud.
                        Rectangle {
                            anchors.centerIn: parent
                            visible: root.rendering && root.playAfterRender
                            width: 210; height: 34
                            radius: 4
                            color: Qt.rgba(0, 0, 0, 0.72)
                            Text {
                                anchors.centerIn: parent
                                text: "rendering a preview…"
                                color: "#e6e9ef"
                                font.pixelSize: root.ui(12)
                                font.family: root.uiFont
                            }
                        }

                        Rectangle {
                            anchors.top: parent.top; anchors.right: parent.right
                            anchors.margins: 10
                            width: 8; height: 8; radius: 4
                            color: root.cAccent
                            opacity: root.frameBusy ? 0.9 : 0.0
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }
                    }

                    // ── Transport ───────────────────────────────────────
                    Rectangle {
                        width: parent.width
                        height: 34
                        color: root.cPanel
                        clip: true

                        Row {
                            id: transportRow
                            anchors.centerIn: parent
                            spacing: 6

                            Btn { label: "⏮"; onClicked: root.seekTo(0) }
                            // Named, not glyphed. A ◀ next to a ▶ reads as
                            // rewind and play, and this pair is neither —
                            // they are single frames.
                            Btn { label: "−1"; active: !root.playing
                                  onClicked: root.seekTo(
                                      root.playhead - 1 / (root.tl.fps || 25)) }
                            Btn {
                                label: root.rendering ? "…" : root.playing ? "⏸" : "▶"
                                active: root.proj !== "" && root.tlDur > 0
                                        && !root.rendering && root.playbackReady
                                onClicked: root.togglePlay()
                            }
                            Btn { label: "+1"; active: !root.playing
                                  onClicked: root.seekTo(
                                      root.playhead + 1 / (root.tl.fps || 25)) }
                            Btn { label: "⏭"; onClicked: root.seekTo(root.tlDur) }
                        }

                        // Monitoring, on the far side of the transport from
                        // the clock. It is how loud the room is and it reaches
                        // no file — which is worth saying, because a fader on
                        // a transport bar is otherwise indistinguishable from
                        // a fader on a mixer.
                        Row {
                            id: monRow
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: zoomRow.left
                            anchors.rightMargin: 14
                            spacing: 6
                            visible: root.playbackReady
                                     && (parent.width - transportRow.width) / 2
                                        > clockText.implicitWidth + zoomRow.width + 110

                            // Named, not glyphed. A speaker emoji renders as
                            // a box in the fixed UI font on a fresh install,
                            // and a control nobody can read is worse than a
                            // wider one.
                            Btn {
                                label: root.monMuted ? "Muted" : "Vol"
                                on: root.monMuted
                                onClicked: root.monMuted = !root.monMuted
                            }

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 62
                                height: 4
                                radius: 2
                                color: root.wash(0.20)

                                Rectangle {
                                    width: parent.width * (root.monMuted ? 0 : root.monVolume)
                                    height: parent.height
                                    radius: 2
                                    color: root.cAccent
                                }
                                Rectangle {
                                    x: parent.width * (root.monMuted ? 0 : root.monVolume) - 4
                                    y: -3
                                    width: 8; height: 10; radius: 2
                                    color: root.cAccent
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -9
                                    preventStealing: true
                                    function set(mx) {
                                        root.monVolume = Math.max(0, Math.min(1,
                                            (mx - 9) / parent.width))
                                        root.monMuted = false
                                    }
                                    onPressed: function (m) { set(m.x) }
                                    onPositionChanged: function (m) { if (pressed) set(m.x) }
                                }
                            }
                        }

                        // The clock and the zoom sit either side of the
                        // transport and give way to it, in that order. The
                        // transport buttons are the one thing on this bar
                        // that must always be reachable, so they are what the
                        // others make room for rather than overlap.
                        Text {
                            id: clockText
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            visible: (parent.width - transportRow.width) / 2
                                     > implicitWidth + 20
                            text: root.timecode(root.playhead) + "  /  " + root.timecode(root.tlDur)
                            color: root.cText
                            font.pixelSize: root.ui(12)
                            font.family: "monospace"
                        }

                        Row {
                            id: zoomRow
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            spacing: 6
                            visible: (parent.width - transportRow.width) / 2
                                     > clockText.implicitWidth + implicitWidth + 40
                            Btn { label: "−"; onClicked:
                                  root.pxPerSec = Math.max(8, root.pxPerSec / 1.5) }
                            Btn { label: "+"; onClicked:
                                  root.pxPerSec = Math.min(600, root.pxPerSec * 1.5) }
                        }
                    }

                    // ── The timeline ────────────────────────────────────
                    Rectangle {
                        width: parent.width
                        height: 224
                        color: root.isLight ? Qt.darker(root.cPanel, 1.06)
                                            : Qt.darker(root.cPanel, 1.5)

                        Row {
                            anchors.fill: parent

                            // Track headers, pinned SIDEWAYS. They do not scroll
                            // with the clips: losing track of which lane is
                            // which is the fastest way to drop a clip on the
                            // wrong one, and the lane names are what prevent it.
                            //
                            // Vertically they must scroll in lockstep, or the
                            // name beside a lane stops being that lane's name,
                            // which is the same failure by another route. They
                            // ride the lane Flickable's contentY rather than
                            // being a second Flickable, because two of them
                            // agree only until somebody flicks one.
                            Column {
                                width: 92
                                height: parent.height

                                Rectangle {
                                    width: parent.width; height: 22
                                    color: "transparent"
                                }

                                Item {
                                    width: 92
                                    height: parent.height - 22
                                    clip: true

                                    Column {
                                        width: 92
                                        y: -laneFlick.contentY

                                        Repeater {
                                            model: root.tl.tracks

                                            Rectangle {
                                                id: hdr
                                                required property var modelData
                                                required property int index
                                                width: 92
                                                height: 56
                                                color: root.selTrack === hdr.index ? root.wash(0.22)
                                                                                   : root.wash(0.07)
                                                border.width: 1
                                                border.color: root.wash(0.14)

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.leftMargin: 8
                                                    anchors.top: parent.top
                                                    anchors.topMargin: 7
                                                    text: hdr.modelData.name
                                                    color: root.cText
                                                    font.pixelSize: root.ui(11)
                                                    font.family: root.uiFont
                                                    font.bold: true
                                                }

                                                Row {
                                                    anchors.left: parent.left
                                                    anchors.leftMargin: 8
                                                    anchors.bottom: parent.bottom
                                                    anchors.bottomMargin: 7
                                                    spacing: 5

                                                    // Mute for audio, hide for video —
                                                    // one flag each, named for what the
                                                    // track actually does.
                                                    Tag {
                                                        label: hdr.modelData.type === "audio" ? "M" : "H"
                                                        on: hdr.modelData.type === "audio"
                                                            ? hdr.modelData.muted : hdr.modelData.hidden
                                                        onClicked: {
                                                            const a = hdr.modelData.type === "audio"
                                                                      ? "--mute" : "--hide"
                                                            const v = (hdr.modelData.type === "audio"
                                                                       ? hdr.modelData.muted
                                                                       : hdr.modelData.hidden) ? "0" : "1"
                                                            root.tlRun(["track", root.proj,
                                                                        String(hdr.index), a, v])
                                                        }
                                                    }
                                                    Text {
                                                        text: hdr.modelData.type
                                                        color: root.cDim
                                                        font.pixelSize: root.ui(9)
                                                        font.family: root.uiFont
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    onClicked: { root.selTrack = hdr.index
                                                                 root.selClip = -1 }
                                                }
                                            }
                                        }

                                        // Adding a lane is part of editing, not part
                                        // of setting a project up once.
                                        Row {
                                            spacing: 4
                                            Item { width: 4; height: 1 }
                                            Tag { label: "+V"; on: false
                                                  onClicked: root.tlRun(["track", root.proj, "video",
                                                             "V" + (root.tl.tracks.length + 1)]) }
                                            Tag { label: "+A"; on: false
                                                  onClicked: root.tlRun(["track", root.proj, "audio",
                                                             "A" + (root.tl.tracks.length + 1)]) }
                                        }
                                    }
                                }
                            }

                            Flickable {
                                id: laneFlick
                                width: parent.width - 92
                                height: parent.height
                                contentWidth: Math.max(width,
                                    (root.tlDur + 10) * root.pxPerSec)
                                // Tracks past the bottom of the strip used to
                                // be simply absent: added, in the document,
                                // exported, and invisible. The lanes scroll
                                // now, and the ruler stays where it is by
                                // riding contentY — a time axis that scrolls
                                // out of view is a ruler with nothing to rule.
                                contentHeight: Math.max(height,
                                    22 + root.tl.tracks.length * 56 + 8)
                                clip: true
                                flickableDirection: Flickable.HorizontalAndVerticalFlick
                                boundsBehavior: Flickable.StopAtBounds

                                Item {
                                    id: lanes
                                    width: Math.max(parent.width,
                                        (root.tlDur + 10) * root.pxPerSec)
                                    height: laneFlick.contentHeight

                                    // Ruler. Clicking it is how the playhead
                                    // moves, which is the gesture every editor
                                    // has and the only one people try first.
                                    Rectangle {
                                        id: ruler
                                        width: parent.width
                                        height: 22
                                        // Pinned to the top of the viewport,
                                        // not to the top of the content.
                                        y: laneFlick.contentY
                                        z: 2
                                        color: root.wash(0.10)

                                        Repeater {
                                            model: Math.ceil(lanes.width / root.pxPerSec)

                                            Item {
                                                id: tick
                                                required property int index
                                                x: tick.index * root.pxPerSec
                                                width: root.pxPerSec
                                                height: 22
                                                // Below about a tick every 40
                                                // pixels the numbers collide
                                                // into a grey smear, so they
                                                // thin out instead.
                                                readonly property int every:
                                                    root.pxPerSec > 40 ? 1
                                                    : root.pxPerSec > 16 ? 5 : 10
                                                Rectangle {
                                                    width: 1
                                                    height: tick.index % tick.every === 0 ? 9 : 4
                                                    color: root.wash(0.5)
                                                    anchors.bottom: parent.bottom
                                                }
                                                Text {
                                                    visible: tick.index % tick.every === 0
                                                    x: 3
                                                    y: 1
                                                    text: root.timecode(tick.index)
                                                    color: root.cDim
                                                    font.pixelSize: root.ui(9)
                                                    font.family: "monospace"
                                                }
                                            }
                                        }

                                        // Markers, over the ruler's ticks.
                                        // Left-click goes there, right-click
                                        // takes it away — the note is drawn
                                        // beside the flag rather than in a
                                        // tooltip, because a note nobody can
                                        // read without hovering is a note
                                        // nobody reads.
                                        Repeater {
                                            model: root.tl.markers || []

                                            Item {
                                                id: mk
                                                required property var modelData
                                                required property int index
                                                x: mk.modelData.t * root.pxPerSec
                                                y: 0
                                                width: 1
                                                height: 22
                                                z: 4

                                                readonly property color ink:
                                                    ["#e0a13a", "#e0463c", "#3fa06e",
                                                     "#4a86d8", "#a86ad8", "#d86aa8"]
                                                    [Math.max(0, Math.min(5, mk.modelData.colour))]

                                                Rectangle {
                                                    width: 2; height: 22
                                                    color: mk.ink
                                                }
                                                Rectangle {
                                                    x: 1; y: 1
                                                    width: 7; height: 7
                                                    color: mk.ink
                                                }
                                                // On a plate. Without one the
                                                // note and the ruler's own
                                                // timecodes are drawn on top
                                                // of each other and neither is
                                                // readable.
                                                Rectangle {
                                                    x: 9
                                                    y: 3
                                                    width: mkText.implicitWidth + 6
                                                    height: 12
                                                    radius: 2
                                                    color: root.cPanel
                                                    visible: mk.modelData.text !== ""
                                                }
                                                Text {
                                                    id: mkText
                                                    x: 12; y: 4
                                                    text: mk.modelData.text
                                                    color: mk.ink
                                                    font.pixelSize: root.ui(9)
                                                    font.family: root.uiFont
                                                    visible: mk.modelData.text !== ""
                                                }
                                                MouseArea {
                                                    x: -4; y: 0
                                                    width: 14; height: 22
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    onClicked: function (m) {
                                                        if (m.button === Qt.RightButton)
                                                            root.dropMarker(mk.index)
                                                        else
                                                            root.seekTo(mk.modelData.t)
                                                    }
                                                }
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            // A Flickable STEALS the drag.
                                            //
                                            // Every draggable thing in this
                                            // window sits inside one — the
                                            // timeline flicks, both panels
                                            // flick — and once the drag passes
                                            // the threshold the Flickable takes
                                            // the grab and pans instead. The
                                            // press still lands, so the
                                            // playhead jumps once and then
                                            // refuses to follow the hand.
                                            //
                                            // Measured at 2 moves out of 10
                                            // reaching the MouseArea without
                                            // this, and 10 out of 10 with it.
                                            //
                                            // It only steals when the content
                                            // can actually move that way, and
                                            // that is why it read as "hard"
                                            // rather than dead, and why it was
                                            // fine MAXIMIZED: a timeline that
                                            // fits the window has nothing to
                                            // scroll, and a drag towards a
                                            // bound it is already at is left
                                            // alone. Same gesture, same code,
                                            // decided by the window size.
                                            preventStealing: true
                                            onPressed: function (m) {
                                                root.scrubbing = true
                                                root.seekTo(m.x / root.pxPerSec)
                                            }
                                            onPositionChanged: function (m) {
                                                if (pressed) root.seekTo(m.x / root.pxPerSec)
                                            }
                                            onReleased: function (m) {
                                                root.scrubbing = false
                                                root.requestFrame()
                                            }
                                        }
                                    }

                                    Column {
                                        y: 22
                                        width: parent.width

                                        Repeater {
                                            model: root.tl.tracks

                                            Rectangle {
                                                id: lane
                                                required property var modelData
                                                required property int index
                                                width: lanes.width
                                                height: 56
                                                color: root.selTrack === lane.index
                                                       ? root.wash(0.05) : "transparent"
                                                border.width: 1
                                                border.color: root.wash(0.10)

                                                Repeater {
                                                    model: lane.modelData.clips

                                                    Rectangle {
                                                        id: clipRect
                                                        required property var modelData
                                                        required property int index

                                                        readonly property bool isSel:
                                                            root.isSelected(lane.index,
                                                                            clipRect.index)
                                                        readonly property bool isPrimary:
                                                            root.selTrack === lane.index
                                                            && root.selClip === clipRect.index
                                                        // The drag is drawn, not
                                                        // committed. The engine
                                                        // hears one command on
                                                        // release, not one per
                                                        // mouse move.
                                                        readonly property real dx:
                                                            (root.dragTrack === lane.index
                                                             && root.dragClip === clipRect.index
                                                             && root.dragKind === "move")
                                                            ? root.dragDx : 0
                                                        readonly property real dHead:
                                                            (root.dragTrack === lane.index
                                                             && root.dragClip === clipRect.index
                                                             && root.dragKind === "head")
                                                            ? root.dragDx : 0
                                                        readonly property real dTail:
                                                            (root.dragTrack === lane.index
                                                             && root.dragClip === clipRect.index
                                                             && root.dragKind === "tail")
                                                            ? root.dragDx : 0

                                                        x: clipRect.modelData.tlIn * root.pxPerSec
                                                           + clipRect.dx + clipRect.dHead
                                                        y: 4
                                                        width: Math.max(6,
                                                            clipRect.modelData.len * root.pxPerSec
                                                            - clipRect.dHead + clipRect.dTail)
                                                        height: 48
                                                        radius: 3
                                                        color: clipRect.modelData.kind === "title"
                                                               ? root.wash(0.42)
                                                               : clipRect.modelData.kind === "solid"
                                                               ? root.wash(0.30)
                                                               : lane.modelData.type === "audio"
                                                               ? Qt.rgba(0.25, 0.55, 0.4, 0.55)
                                                               : root.wash(0.22)
                                                        border.width: clipRect.isSel ? 2 : 1
                                                        // The primary is the
                                                        // one the inspector is
                                                        // showing, and it has
                                                        // to be tellable from
                                                        // the rest of a
                                                        // selection.
                                                        border.color: clipRect.isPrimary ? root.cAccent
                                                                      : clipRect.isSel ? root.wash(0.75)
                                                                      : root.wash(0.4)

                                                        // Under the label, over
                                                        // the plate. An audio
                                                        // lane gives it the
                                                        // whole clip; a video
                                                        // lane gives it a strip
                                                        // along the bottom,
                                                        // because there the
                                                        // picture's name is
                                                        // what identifies it
                                                        // and the sound is what
                                                        // you line it up by.
                                                        Waveform {
                                                            anchors.left: parent.left
                                                            anchors.right: parent.right
                                                            anchors.leftMargin: 1
                                                            anchors.rightMargin: 1
                                                            anchors.bottom: parent.bottom
                                                            anchors.bottomMargin: 2
                                                            height: lane.modelData.type === "audio"
                                                                    ? parent.height - 18
                                                                    : Math.round(parent.height * 0.42)
                                                            peaks: root.wave[root.waveKey(clipRect.modelData)]
                                                            ink: lane.modelData.type === "audio"
                                                                 ? Qt.rgba(0.06, 0.24, 0.16, 1)
                                                                 : root.cInk
                                                        }

                                                        Text {
                                                            anchors.left: parent.left
                                                            anchors.leftMargin: 6
                                                            anchors.top: parent.top
                                                            anchors.topMargin: 5
                                                            width: parent.width - 12
                                                            elide: Text.ElideMiddle
                                                            text: clipRect.modelData.kind === "title"
                                                                  ? "T  " + clipRect.modelData.text
                                                                  : clipRect.modelData.kind === "solid"
                                                                  ? "colour"
                                                                  : clipRect.modelData.path
                                                                        .replace(/^.*\//, "")
                                                            color: root.cText
                                                            font.pixelSize: root.ui(10)
                                                            font.family: root.uiFont
                                                        }

                                                        // Every keyframe, where it
                                                        // sits. A grade that
                                                        // moves is invisible
                                                        // otherwise — you would
                                                        // have to scrub to find
                                                        // out it does.
                                                        Repeater {
                                                            model: clipRect.modelData.keys

                                                            Text {
                                                                required property var modelData
                                                                required property int index
                                                                x: Math.max(0, Math.min(
                                                                       clipRect.width - 7,
                                                                       modelData.t * root.pxPerSec - 3))
                                                                y: 1
                                                                text: "◆"
                                                                font.pixelSize: root.ui(9)
                                                                font.family: root.uiFont
                                                                color: (clipRect.isSel
                                                                        && root.selKey === index)
                                                                       ? root.cAccent : root.cDim
                                                            }
                                                        }

                                                        // A transition into
                                                        // this clip, at its
                                                        // head where it
                                                        // happens. Otherwise
                                                        // the only way to
                                                        // find out a cut is
                                                        // not a cut is to
                                                        // open the inspector
                                                        // on every clip.
                                                        Text {
                                                            visible: clipRect.modelData.trans
                                                                     !== "none"
                                                            x: 2
                                                            anchors.bottom: parent.bottom
                                                            anchors.bottomMargin: 1
                                                            text: "◨"
                                                            font.pixelSize: root.ui(9)
                                                            font.family: root.uiFont
                                                            color: root.cAccent
                                                        }

                                                        // The same for a
                                                        // property that
                                                        // moves. Drawn a row
                                                        // lower and hollow,
                                                        // so a grade key and
                                                        // a scale key are not
                                                        // the same mark.
                                                        Repeater {
                                                            model: clipRect.modelData.animAll

                                                            Text {
                                                                required property var modelData
                                                                x: Math.max(0, Math.min(
                                                                       clipRect.width - 7,
                                                                       modelData.t * root.pxPerSec - 3))
                                                                y: 11
                                                                text: "◇"
                                                                font.pixelSize: root.ui(8)
                                                                font.family: root.uiFont
                                                                color: root.cDim
                                                            }
                                                        }

                                                        // A graded clip says so.
                                                        // Finding out at export
                                                        // that a shot was never
                                                        // graded is too late.
                                                        Text {
                                                            anchors.right: parent.right
                                                            anchors.rightMargin: 5
                                                            anchors.bottom: parent.bottom
                                                            anchors.bottomMargin: 4
                                                            visible: clipRect.modelData.graded
                                                            text: "◕"
                                                            color: root.cAccent
                                                            font.pixelSize: root.ui(11)
                                                            font.family: root.uiFont
                                                        }
                                                        Text {
                                                            anchors.left: parent.left
                                                            anchors.leftMargin: 6
                                                            anchors.bottom: parent.bottom
                                                            anchors.bottomMargin: 4
                                                            visible: clipRect.modelData.trans !== "none"
                                                            text: "⇥ " + clipRect.modelData.trans
                                                            color: root.cAccent
                                                            font.pixelSize: root.ui(9)
                                                            font.family: root.uiFont
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            // The edge zones are
                                                            // where a trim lives
                                                            // in every editor, so
                                                            // the cursor says so
                                                            // before the click.
                                                            cursorShape:
                                                                (mouseX < 7 || mouseX > width - 7)
                                                                ? Qt.SizeHorCursor
                                                                : Qt.OpenHandCursor
                                                            hoverEnabled: true

                                                            // Or the timeline
                                                            // pans instead of
                                                            // the clip moving.
                                                            // See the ruler.
                                                            preventStealing: true

                                                            // Measured in the LANE,
                                                            // never in the clip.
                                                            //
                                                            // This MouseArea fills
                                                            // the clip, and the clip
                                                            // MOVES as it is dragged
                                                            // — so `m.x` is a
                                                            // position in a frame
                                                            // that is itself being
                                                            // dragged. Push right and
                                                            // the rectangle follows,
                                                            // which puts the pointer
                                                            // back where it started
                                                            // relative to the
                                                            // rectangle, so the next
                                                            // event cancels the
                                                            // previous one: the clip
                                                            // stutters to the right
                                                            // and cannot go left at
                                                            // all. mapToItem gives
                                                            // the pointer's position
                                                            // in the lane, which does
                                                            // not move, so the
                                                            // difference is the real
                                                            // one.
                                                            onPressed: function (m) {
                                                                // Shift adds to
                                                                // the selection
                                                                // rather than
                                                                // replacing it.
                                                                if (m.modifiers & Qt.ShiftModifier) {
                                                                    root.toggleSelect(lane.index,
                                                                                      clipRect.index)
                                                                    return
                                                                }
                                                                root.selMore = []
                                                                root.selTrack = lane.index
                                                                root.selClip = clipRect.index
                                                                root.dragTrack = lane.index
                                                                root.dragClip = clipRect.index
                                                                root.dragKind =
                                                                    m.x < 7 ? "head"
                                                                    : m.x > width - 7 ? "tail"
                                                                    : "move"
                                                                root.dragFrom =
                                                                    clipRect.mapToItem(lane, m.x, 0).x
                                                                root.dragDx = 0
                                                            }
                                                            onPositionChanged: function (m) {
                                                                if (!pressed) return
                                                                root.dragDx =
                                                                    clipRect.mapToItem(lane, m.x, 0).x
                                                                    - root.dragFrom
                                                            }
                                                            onReleased: function (m) {
                                                                root.commitDrag(
                                                                    clipRect.modelData, lane.index)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // The playhead, over everything — the ruler
                                    // included, which is where the eye looks
                                    // for it, so it outranks the ruler's z.
                                    Rectangle {
                                        x: root.playhead * root.pxPerSec
                                        y: 0
                                        z: 3
                                        width: 2
                                        height: lanes.height
                                        color: "#ff5a5a"
                                    }
                                }
                            }
                        }
                    }
                }

                // ── The clip inspector ──────────────────────────────────
                Rectangle {
                    width: root.panelW
                    height: parent.height
                    color: root.cPanel

                    // ── The mixer ───────────────────────────────────────
                    //
                    // In the panel rather than in a window of its own: it is
                    // the same width a channel strip wants, and a mixer you
                    // have to go and find is a mixer nobody balances.
                    Item {
                        anchors.fill: parent
                        visible: root.mixerOpen

                        Text {
                            id: mixTitle
                            anchors.top: parent.top
                            anchors.topMargin: 12
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            text: "Mixer"
                            color: root.cText
                            font.pixelSize: root.ui(13)
                            font.family: root.uiFont
                            font.bold: true
                        }
                        Text {
                            anchors.verticalCenter: mixTitle.verticalCenter
                            anchors.left: mixTitle.right
                            anchors.leftMargin: 10
                            text: "levels at the playhead"
                            color: root.cDim
                            font.pixelSize: root.ui(10)
                            font.family: root.uiFont
                        }

                        Flickable {
                            anchors.top: mixTitle.bottom
                            anchors.topMargin: 10
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 0
                            contentWidth: strips.width
                            contentHeight: height
                            clip: true
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds

                            Row {
                                id: strips
                                height: parent.height
                                spacing: 0

                                Repeater {
                                    model: root.tl.tracks

                                    Strip {
                                        required property var modelData
                                        required property int index
                                        trackIndex: index
                                        label: modelData.name
                                        isAudio: modelData.type === "audio"
                                        height: strips.height
                                    }
                                }

                                Strip {
                                    trackIndex: -1
                                    label: "Master"
                                    isAudio: true
                                    height: strips.height
                                }
                            }
                        }
                    }

                    // ── Voiceover ───────────────────────────────────────
                    Item {
                        anchors.fill: parent
                        anchors.margins: 14
                        visible: root.voOpen

                        Column {
                            anchors.fill: parent
                            spacing: 10

                            Text {
                                text: "Voiceover"
                                color: root.cText
                                font.pixelSize: root.ui(13)
                                font.family: root.uiFont
                                font.bold: true
                            }

                            Text {
                                width: parent.width
                                text: "Records from the playhead onto an audio "
                                      + "track, into a file beside the project."
                                color: root.cDim
                                font.pixelSize: root.ui(10)
                                font.family: root.uiFont
                                wrapMode: Text.WordWrap
                            }

                            // What to record from. Monitors are listed but
                            // named as what they are: they capture what the
                            // machine is PLAYING, which is never what somebody
                            // asking for a voiceover meant.
                            Text {
                                text: "From"
                                color: root.cDim
                                font.pixelSize: root.ui(10)
                                font.family: root.uiFont
                            }

                            Column {
                                width: parent.width
                                spacing: 2

                                Repeater {
                                    model: root.voDevices

                                    Rectangle {
                                        id: devRow
                                        required property var modelData
                                        required property int index
                                        width: parent.width
                                        height: 26
                                        radius: 3
                                        color: root.voDevice === devRow.index
                                               ? root.wash(0.24)
                                               : devMa.containsMouse ? root.wash(0.12)
                                                                     : "transparent"
                                        border.width: root.voDevice === devRow.index ? 1 : 0
                                        border.color: root.cAccent

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 8
                                            anchors.right: parent.right
                                            anchors.rightMargin: 8
                                            text: (devRow.modelData.kind === "monitor"
                                                   ? "↻ " : "") + devRow.modelData.name
                                            color: devRow.modelData.kind === "monitor"
                                                   ? root.cDim : root.cText
                                            font.pixelSize: root.ui(11)
                                            font.family: root.uiFont
                                            elide: Text.ElideRight
                                        }
                                        MouseArea {
                                            id: devMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: !root.voRecording
                                            onClicked: root.voDevice = devRow.index
                                        }
                                    }
                                }
                            }

                            Row {
                                spacing: 6
                                Tag { label: "play"; on: root.voPlayWhile
                                      onClicked: root.voPlayWhile = !root.voPlayWhile }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "roll the timeline"
                                    color: root.cDim
                                    font.pixelSize: root.ui(10)
                                    font.family: root.uiFont
                                }
                            }
                            Row {
                                spacing: 6
                                // Lettered, like every other stud in this
                                // window. An emoji here renders as a box in
                                // the fixed UI font on a fresh install, and
                                // the sentence beside it is doing the work
                                // anyway.
                                Tag { label: "mon"; on: root.voHeadphones
                                      onClicked: root.voHeadphones = !root.voHeadphones }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 150
                                    text: root.voHeadphones
                                          ? "monitoring on — headphones"
                                          : "monitoring muted — speakers"
                                    color: root.cDim
                                    font.pixelSize: root.ui(10)
                                    font.family: root.uiFont
                                    wrapMode: Text.WordWrap
                                }
                            }

                            // The meter. This is the question a voiceover has
                            // to answer before anybody speaks: is it live.
                            Rectangle {
                                width: parent.width
                                height: 10
                                radius: 3
                                color: root.wash(0.14)

                                Rectangle {
                                    height: parent.height
                                    radius: 3
                                    width: parent.width
                                           * root.meterFrac(Math.pow(10, root.voLevel / 20))
                                    color: root.voLevel > -1 ? "#e0463c"
                                           : root.voLevel > -6 ? "#d8a13a" : "#3fa06e"
                                }
                            }

                            Text {
                                width: parent.width
                                text: root.voCount > 0
                                      ? "in " + root.voCount + "…"
                                      : root.voRecording
                                        ? "● " + root.timecode(root.voElapsed)
                                          + "   " + root.voLevel.toFixed(0) + " dB"
                                        : "from " + root.timecode(root.playhead)
                                color: root.voRecording ? "#e0463c" : root.cDim
                                font.pixelSize: root.ui(12)
                                font.family: "monospace"
                            }

                            Row {
                                spacing: 8
                                Btn {
                                    label: root.voRecording || root.voCount > 0
                                           ? "Stop" : "Record"
                                    active: root.proj !== ""
                                            && root.voDevices.length > 0
                                    onClicked: root.voRecording || root.voCount > 0
                                               ? root.stopVoiceover()
                                               : root.startVoiceover()
                                }
                                Btn {
                                    label: "Devices"
                                    active: !root.voRecording
                                    onClicked: root.loadDevices()
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.selClip < 0 && root.panelMode === "clip"
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.proj === "" ? "No project yet"
                              : "Pick a clip to grade it"
                        color: root.cDim
                        font.pixelSize: root.ui(13)
                        font.family: root.uiFont
                    }

                    Flickable {
                        anchors.fill: parent
                        visible: root.selClip >= 0 && root.panelMode === "clip"
                        contentHeight: inspCol.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: inspCol
                            width: parent.width

                            // What is selected, and where it sits. A panel of
                            // sliders with no idea which clip they belong to
                            // is how a grade lands on the wrong shot.
                            Rectangle {
                                width: parent.width
                                height: 46
                                color: root.wash(0.16)
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 12
                                    anchors.top: parent.top; anchors.topMargin: 6
                                    width: parent.width - 24
                                    elide: Text.ElideMiddle
                                    text: root.clipValue("path")
                                          ? root.clipValue("path").replace(/^.*\//, "")
                                          : (root.clipValue("kind") || "clip")
                                    color: root.cText
                                    font.pixelSize: root.ui(12)
                                    font.family: root.uiFont
                                    font.bold: true
                                }
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 12
                                    anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                                    text: root.timecode(parseFloat(root.clipValue("tl_in")) || 0)
                                          + "  ·  " +
                                          root.timecode(parseFloat(root.clipValue("length")) || 0)
                                          + " long"
                                    color: root.cDim
                                    font.pixelSize: root.ui(10)
                                    font.family: "monospace"
                                }
                            }

                            Repeater {
                                model: root.clipGroups

                                Column {
                                    id: cgrp
                                    required property string modelData
                                    width: inspCol.width
                                    // A caption's controls have nothing to say
                                    // about a video clip, and a panel offering
                                    // them anyway teaches people to ignore it.
                                    readonly property bool applies:
                                        cgrp.modelData === "Title"
                                        ? root.clipValue("kind") === "title"
                                        : cgrp.modelData === "Background"
                                        ? (root.clipValue("kind") === "solid"
                                           || root.clipValue("kind") === "title")
                                        : true
                                    visible: cgrp.applies
                                    height: cgrp.applies ? implicitHeight : 0
                                    property bool open: modelData === "Levels"
                                                        || modelData === "Title"

                                    Rectangle {
                                        width: parent.width
                                        height: 30
                                        color: root.wash(0.10)
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 12
                                            text: (cgrp.open ? "▾  " : "▸  ") + cgrp.modelData
                                            color: root.cText
                                            font.pixelSize: root.ui(12)
                                            font.family: root.uiFont
                                            font.bold: true
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: cgrp.open = !cgrp.open
                                        }
                                    }

                                    // The styles, above the controls they
                                    // move, because that is the order the
                                    // work happens in — pick how the title
                                    // is drawn, then adjust it. Same shape
                                    // as the darkroom's Looks list for the
                                    // same reason: it is a stamp with every
                                    // slider left live underneath.
                                    Repeater {
                                        model: (cgrp.open && cgrp.modelData === "Title")
                                               ? root.titleStyles : []
                                        Rectangle {
                                            id: styleRow
                                            required property var modelData
                                            width: inspCol.width
                                            height: 26
                                            color: styleArea.containsMouse ? root.wash(0.16)
                                                                           : "transparent"
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.left: parent.left
                                                anchors.leftMargin: 20
                                                anchors.right: parent.right
                                                anchors.rightMargin: 12
                                                // The name, then what it is
                                                // for: "lower-third" alone
                                                // means nothing to somebody
                                                // who has not made one.
                                                text: styleRow.modelData.name
                                                      + "  —  " + styleRow.modelData.label
                                                elide: Text.ElideRight
                                                color: root.cText
                                                font.pixelSize: root.ui(11)
                                                font.family: root.uiFont
                                            }
                                            MouseArea {
                                                id: styleArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: root.applyTitleStyle(
                                                    styleRow.modelData.name)
                                            }
                                        }
                                    }

                                    Repeater {
                                        model: cgrp.open ? root.clipRowsIn(cgrp.modelData) : []
                                        ClipCtl {}
                                    }
                                }
                            }

                            // ── Effects ─────────────────────────────────
                            //
                            // A stack, in the order it applies, after the
                            // grade. Every row in it — the list, the knobs,
                            // the ranges — comes from the engine's catalogue,
                            // so an effect somebody wrote this morning and
                            // dropped in a folder appears here with its own
                            // sliders and this file never learns its name.
                            Rectangle {
                                width: parent.width
                                height: 34
                                color: root.wash(0.20)
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12
                                    text: "Effects"
                                    color: root.cText
                                    font.pixelSize: root.ui(12)
                                    font.family: root.uiFont
                                    font.bold: true
                                }
                                Tag {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    label: root.fxPicking ? "✕" : "+ Add"
                                    on: root.fxPicking
                                    onClicked: root.fxPicking = !root.fxPicking
                                }
                            }

                            // The catalogue, when it is being picked from.
                            Rectangle {
                                width: parent.width
                                height: root.fxPicking ? 170 : 0
                                visible: root.fxPicking
                                color: root.wash(0.10)
                                clip: true

                                ListView {
                                    id: fxpick
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    model: root.fxList
                                    clip: true
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: fxpick.width
                                        height: 20
                                        color: "transparent"
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 6
                                            width: parent.width - 12
                                            elide: Text.ElideRight
                                            text: parent.modelData.label + "   ·   "
                                                  + parent.modelData.group
                                            color: root.cText
                                            font.pixelSize: root.ui(10)
                                            font.family: root.uiFont
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                root.fxRun(["add", parent.modelData.name])
                                                root.fxPicking = false
                                            }
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: root.clipFx()

                                Column {
                                    id: fxrow
                                    required property var modelData
                                    required property int index
                                    width: inspCol.width

                                    Rectangle {
                                        width: parent.width
                                        height: 26
                                        color: root.wash(0.14)
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 16
                                            width: parent.width - 130
                                            elide: Text.ElideRight
                                            text: (fxrow.index + 1) + ". "
                                                  + root.fxLabel(fxrow.modelData.name)
                                            color: fxrow.modelData.on ? root.cText : root.cDim
                                            font.pixelSize: root.ui(11)
                                            font.family: root.uiFont
                                        }
                                        Row {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.right: parent.right
                                            anchors.rightMargin: 10
                                            spacing: 4
                                            // Order is the reason the stack is
                                            // a list: a blur under a glow and a
                                            // glow under a blur are different
                                            // pictures.
                                            Tag { label: "▲"; on: false
                                                  onClicked: root.fxRun(
                                                      ["move", String(fxrow.index),
                                                       String(Math.max(0, fxrow.index - 1))]) }
                                            Tag { label: "▼"; on: false
                                                  onClicked: root.fxRun(
                                                      ["move", String(fxrow.index),
                                                       String(fxrow.index + 1)]) }
                                            Tag { label: fxrow.modelData.on ? "on" : "off"
                                                  on: fxrow.modelData.on
                                                  onClicked: root.fxRun(
                                                      ["set", String(fxrow.index),
                                                       "on=" + (fxrow.modelData.on ? 0 : 1)]) }
                                            Tag { label: "✕"; on: false
                                                  onClicked: root.fxRun(
                                                      ["remove", String(fxrow.index)]) }
                                        }
                                    }

                                    Repeater {
                                        model: root.fxParamsOf(fxrow.modelData.name)
                                        FxCtl {
                                            fxIndex: fxrow.index
                                            values: fxrow.modelData.param
                                        }
                                    }
                                }
                            }

                            // ── The grade ───────────────────────────────
                            //
                            // The SAME table the darkroom draws, on a clip.
                            // That is the whole architecture made visible: a
                            // slider moved here bakes a .cube and ffmpeg's
                            // lut3d applies it to every frame, so the still
                            // and the clip cannot disagree about colour.
                            Rectangle {
                                width: parent.width
                                height: 34
                                color: root.wash(0.20)
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12
                                    text: "Grade"
                                    color: root.cText
                                    font.pixelSize: root.ui(12)
                                    font.family: root.uiFont
                                    font.bold: true
                                }
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    spacing: 6

                                    Tag {
                                        label: "◆ key"
                                        on: false
                                        onClicked: root.addKey()
                                    }
                                    Tag {
                                        label: "✕"
                                        on: false
                                        visible: root.selKey >= 0
                                        onClicked: root.removeKey()
                                    }
                                }
                            }

                            // Which moment the sliders below are editing. With
                            // keyframes the panel is never editing "the clip"
                            // — it is editing one instant of it, and not
                            // saying so is how somebody grades the wrong one.
                            Rectangle {
                                width: parent.width
                                height: 22
                                color: root.wash(0.10)
                                visible: root.selClipObj && root.selClipObj.keys
                                         && root.selClipObj.keys.length > 0
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 20
                                    text: root.selKey < 0 ? ""
                                          : "editing key " + (root.selKey + 1) + " of "
                                            + root.selClipObj.keys.length + "  ·  "
                                            + root.timecode(root.selClipObj.keys[root.selKey].t)
                                            + " into the clip"
                                    color: root.cAccent
                                    font.pixelSize: root.ui(10)
                                    font.family: root.uiFont
                                }
                            }

                            // The same looks, on a clip. `timeline grade
                            // --look` takes one exactly as the sidecar does,
                            // so a look made on a photograph lands on the
                            // footage beside it — which is the whole reason
                            // the two pages share a develop stack.
                            Column {
                                id: glookGrp
                                width: inspCol.width
                                property bool open: false

                                Rectangle {
                                    width: parent.width
                                    height: 28
                                    color: root.wash(0.07)
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 20
                                        text: (glookGrp.open ? "▾  " : "▸  ") + "Looks"
                                        color: root.cText
                                        font.pixelSize: root.ui(11)
                                        font.family: root.uiFont
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: glookGrp.open = !glookGrp.open
                                    }
                                }

                                Repeater {
                                    model: glookGrp.open ? root.lookList : []
                                    Rectangle {
                                        id: glookRow
                                        required property var modelData
                                        width: inspCol.width
                                        height: 26
                                        color: glookArea.containsMouse ? root.wash(0.16)
                                                                       : "transparent"
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 28
                                            anchors.right: parent.right
                                            anchors.rightMargin: 12
                                            text: glookRow.modelData.label
                                            elide: Text.ElideRight
                                            color: root.cText
                                            font.pixelSize: root.ui(11)
                                            font.family: root.uiFont
                                        }
                                        MouseArea {
                                            id: glookArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: root.applyLook(glookRow.modelData.name)
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: root.gradeGroups

                                Column {
                                    id: ggrp
                                    required property string modelData
                                    width: inspCol.width
                                    property bool open: modelData === "Basic"

                                    Rectangle {
                                        width: parent.width
                                        height: 28
                                        color: root.wash(0.07)
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 20
                                            text: (ggrp.open ? "▾  " : "▸  ") + ggrp.modelData
                                            color: root.cText
                                            font.pixelSize: root.ui(11)
                                            font.family: root.uiFont
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: ggrp.open = !ggrp.open
                                        }
                                    }

                                    Repeater {
                                        model: ggrp.open ? root.rowsIn(ggrp.modelData) : []
                                        GradeCtl {}
                                    }
                                }
                            }

                            Item { width: 1; height: 20 }
                        }
                    }
                }
            }

            // Status
            Rectangle {
                width: parent.width
                height: 24
                color: root.cPanel
                // The document's own numbers, at the end of the status bar.
                // They were in the top strip, where they had to fight the
                // toolbar for room and lost; nothing else wants this corner,
                // and the message beside it elides rather than pushing.
                Text {
                    id: statusSize
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    text: root.atStart ? ""
                          : root.mode === "video"
                          ? (root.tl.w + " × " + root.tl.h + "  ·  " + root.tl.fps + " fps")
                          : (root.imgW > 0 ? root.imgW + " × " + root.imgH : "")
                    // Lit only when it does something. In the darkroom this is
                    // the picture's own size and there is nothing to pick.
                    color: sizeHit.containsMouse && sizeHit.enabled
                           ? root.cText : root.cDim
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont

                    MouseArea {
                        id: sizeHit
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        enabled: root.mode === "video" && !root.atStart && !!root.proj
                        cursorShape: enabled ? Qt.PointingHandCursor
                                             : Qt.ArrowCursor
                        onClicked: root.sizeMenuOpen = true
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: statusSize.left
                    anchors.rightMargin: 12
                    text: root.status
                    color: root.cDim
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                    elide: Text.ElideRight
                }
            }
        }
    }

    // ── Dropping files on the window ───────────────────────────────────
    //
    // Over everything, and last, so it is the topmost thing a drag can land
    // on. It handles DRAGS only — a DropArea does not take mouse events — so
    // nothing underneath loses a click to it.

    DropArea {
        id: fileDrop
        anchors.fill: parent
        // ⛔ FILES ONLY. Without this it is the topmost target for the
        // window's OWN photo drag as well, which is how dragging a photograph
        // onto the Video tab came to flash an overlay and do nothing: this
        // area took the drag, found no urls, refused it, and the tab
        // underneath never heard about it.
        // ⚠ text/plain as well: not every source offers a uri-list, and the
        // handler already falls back to the dropped TEXT.
        //
        // ⚠ Files from OUTSIDE only. The window's own photograph is carried
        // by the pointer, not by Qt's drag — see the note on photoDragMa —
        // so nothing internal arrives here any more.
        keys: ["text/uri-list", "text/plain"]
        onDropped: function (drop) {
            if (!drop.hasUrls) { drop.accepted = false; return }
            root.dropUrls(drop.urls)
            drop.acceptProposedAction()
        }

        // The window has to SAY it will take them. A drag with no answer from
        // the target looks exactly like a drag onto something that does not
        // want it, and the hand goes somewhere else.
        Rectangle {
            anchors.fill: parent
            visible: fileDrop.containsDrag
            color: Qt.rgba(0, 0, 0, 0.35)
            border.width: 3
            border.color: root.cAccent

            Text {
                anchors.centerIn: parent
                // ⚠ The window's own photograph drag says what IT will do.
                // "drop a photograph to open it" is the answer to a file
                // arriving from a file manager and reads as a refusal to the
                // one already open.
                text: root.mode === "video"
                      ? (root.proj ? "drop clips onto the timeline"
                                   : "drop clips to start a project")
                      : "drop a photograph to open it"
                color: "#f2f4f8"
                font.pixelSize: root.ui(16)
                font.family: root.uiFont
            }
        }
    }

    // ── Keys ────────────────────────────────────────────────────────────────
    //
    // The window bound NOTHING until 0.1.0-35. Every action was a button,
    // including the ones a hand keeps on the keyboard — play, step, split,
    // undo — and a cutting room where the transport is mouse-only is a
    // cutting room nobody can work quickly in.
    //
    // ⚠ A FOCUS ITEM, not a set of Shortcut objects. Qt matches a Shortcut
    // BEFORE the key is delivered to whatever has focus, so Ctrl+C over the
    // project-name field would copy a CLIP instead of the word under the
    // cursor, and J would shuttle the timeline while somebody typed "Jan"
    // into a name. A focused TextInput swallows its own keys and this item
    // never sees them, which is the behaviour every one of these needs.
    // Focus comes back here whenever a sheet closes.
    Item {
        id: keyCatcher
        anchors.fill: parent
        focus: true
        Keys.onPressed: function (e) { root.handleKey(e) }
    }

    onSaveOpenChanged:    if (!root.saveOpen)    keyCatcher.forceActiveFocus()
    onExportOpenChanged:  if (!root.exportOpen)  keyCatcher.forceActiveFocus()
    onPickerOpenChanged:  if (!root.pickerOpen)  keyCatcher.forceActiveFocus()
    onLutMenuOpenChanged: if (!root.lutMenuOpen) keyCatcher.forceActiveFocus()
    onHelpOpenChanged:    if (!root.helpOpen)    keyCatcher.forceActiveFocus()

    property bool helpOpen: false

    // ⚠ The sheet below is built from THIS table, so a binding that is added
    // to the handler and not to it is a binding nobody can find. `mode` is
    // which page it belongs to: "" is both.
    readonly property var keyRows: [
        { mode: "",      k: "?",              d: "this list" },
        { mode: "",      k: "Esc",            d: "close what is open" },
        { mode: "",      k: "Ctrl+O",         d: "open a file" },
        { mode: "",      k: "Ctrl+E",         d: "export" },
        { mode: "",      k: "Ctrl+Z / Ctrl+Shift+Z", d: "undo, redo" },
        { mode: "photo", k: "Ctrl+S",         d: "export the photograph" },
        { mode: "photo", k: "Ctrl+R",         d: "back to the picture as it arrived" },
        { mode: "video", k: "Ctrl+S",         d: "save the cut under a name" },
        { mode: "video", k: "Ctrl+N",         d: "new project" },
        { mode: "video", k: "Space",          d: "play, pause" },
        { mode: "video", k: "L / K / J",      d: "play faster, stop, shuttle back" },
        { mode: "video", k: "← →",            d: "a frame; with Shift, a second" },
        { mode: "video", k: "Home / End",     d: "the start, the end" },
        { mode: "video", k: "Ctrl+C / Ctrl+V", d: "copy a clip, paste at the playhead" },
        { mode: "video", k: "S",              d: "split at the playhead" },
        { mode: "video", k: "T",              d: "transition at the playhead" },
        { mode: "video", k: "M",              d: "a marker here" },
        { mode: "video", k: "I / O",          d: "mark in, mark out on the source" },
        { mode: "video", k: ", / .",          d: "insert, overwrite at the playhead" },
        { mode: "video", k: "Del / Shift+Del", d: "delete, ripple delete" }
    ]

    function handleKey(e) {
        const ctrl  = (e.modifiers & Qt.ControlModifier) !== 0
        const shift = (e.modifiers & Qt.ShiftModifier) !== 0
        const video = root.mode === "video"
        const sheet = root.saveOpen || root.exportOpen || root.pickerOpen
                      || root.lutMenuOpen || root.sizeMenuOpen
                      || root.helpOpen

        if (e.key === Qt.Key_Escape) {
            if (root.helpOpen)          root.helpOpen = false
            else if (root.saveOpen)     root.saveOpen = false
            else if (root.exportOpen)   root.exportOpen = false
            else if (root.pickerOpen)   root.pickerOpen = false
            else if (root.lutMenuOpen)  root.lutMenuOpen = false
            else if (root.sizeMenuOpen) root.sizeMenuOpen = false
            else if (root.playing || root.revStep > 0) root.shuttleStop()
            else return
            e.accepted = true
            return
        }
        // A sheet is over the window: it owns the keyboard. A shortcut acting
        // on the cut behind it is an edit nobody can see happening.
        if (sheet) return

        if (ctrl) {
            switch (e.key) {
            case Qt.Key_S:
                if (video) root.openSaveAs(); else root.openExport()
                break
            case Qt.Key_N: root.openNewProject(); break
            case Qt.Key_O:
                root.pickerFor = video ? "project" : "photo"
                root.openPicker()
                break
            case Qt.Key_E: root.openExport(); break
            case Qt.Key_C: if (video) root.copyClip(); break
            case Qt.Key_V: if (video) root.pasteClip(); break
            case Qt.Key_R: if (!video && root.file) root.resetPhoto(); break
            case Qt.Key_Y:
                if (video) root.redoEdit(); else root.devStep("redo")
                break
            case Qt.Key_Z:
                if (shift) { if (video) root.redoEdit(); else root.devStep("redo") }
                else       { if (video) root.undoEdit(); else root.devStep("undo") }
                break
            default: return
            }
            e.accepted = true
            return
        }

        if (e.key === Qt.Key_Question || e.text === "?" || e.key === Qt.Key_F1) {
            root.helpOpen = true
            e.accepted = true
            return
        }

        if (!video) return
        const frame = 1 / (root.tl.fps || 25)

        switch (e.key) {
        case Qt.Key_Space: root.stopShuttle(); root.togglePlay(); break
        case Qt.Key_L: root.shuttleForward(); break
        case Qt.Key_K: root.shuttleStop(); break
        case Qt.Key_J: root.shuttleBack(); break
        // ⚠ The arrows follow the VIEWER that is showing. Stepping the
        // program while looking at the source is a picture that does not move
        // for a key that clearly did something.
        case Qt.Key_Left:  root.stopShuttle()
                           if (root.srcShown) root.srcSeek(root.srcPos - (shift ? 1 : frame))
                           else root.seekTo(root.playhead - (shift ? 1 : frame))
                           break
        case Qt.Key_Right: root.stopShuttle()
                           if (root.srcShown) root.srcSeek(root.srcPos + (shift ? 1 : frame))
                           else root.seekTo(root.playhead + (shift ? 1 : frame))
                           break
        case Qt.Key_Home: root.stopShuttle(); root.seekTo(0); break
        case Qt.Key_End:  root.stopShuttle(); root.seekTo(root.tlDur); break
        case Qt.Key_S:
            if (root.selTrack >= 0)
                root.tlRun(["split", root.proj, String(root.selTrack),
                            "--at", String(root.playhead)])
            break
        case Qt.Key_T:
            if (root.selTrack >= 0)
                root.tlRun(["transition", root.proj, String(root.selTrack),
                            "--at", String(root.playhead),
                            "--kind", root.defaultTrans,
                            "--dur", String(root.defaultTransDur)])
            break
        case Qt.Key_M: if (root.proj) root.addMarker(); break
        // The source monitor's own three. I and O are what every editor binds
        // them to; comma and period are insert and overwrite for the same
        // reason — a hand that knows one NLE should not have to look these up.
        case Qt.Key_I: if (root.srcFile) root.srcMarkIn(); break
        case Qt.Key_O: if (root.srcFile) root.srcMarkOut(); break
        case Qt.Key_Comma:  root.srcEdit("insert"); break
        case Qt.Key_Period: root.srcEdit("overwrite"); break
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            if (root.selClip >= 0) root.deleteSelection(shift)
            break
        default: return
        }
        e.accepted = true
    }

    // Reset lives in a function now because two things ask for it — the
    // button and Ctrl+R — and a second copy of it in a key handler is how the
    // two come to do different things.
    function resetPhoto() {
        if (!root.file) return
        setProc.command = [root.bin, "reset", root.file]
        setProc.running = true
        root.dirty = false
        Qt.callLater(function () { root.loadFile(root.file) })
    }

    // ── What the keys do ───────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: root.helpOpen
        color: Qt.rgba(0, 0, 0, 0.55)

        MouseArea { anchors.fill: parent; hoverEnabled: true
                    onClicked: root.helpOpen = false }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(540, parent.width - 80)
            height: Math.min(helpCol.implicitHeight + 28, parent.height - 80)
            color: root.cPanel
            radius: 6
            border.width: 1
            border.color: root.wash(0.28)

            MouseArea { anchors.fill: parent; hoverEnabled: true }

            Column {
                id: helpCol
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 6

                Text {
                    text: "Keys"
                    color: root.cText
                    font.pixelSize: root.ui(13)
                    font.family: root.uiFont
                    font.bold: true
                    bottomPadding: 4
                }

                Repeater {
                    model: root.keyRows

                    Row {
                        id: keyRow
                        required property var modelData
                        // Only what this page can do. A list of bindings that
                        // do nothing here is a list nobody trusts.
                        visible: keyRow.modelData.mode === ""
                                 || keyRow.modelData.mode === root.mode
                        width: parent.width
                        height: visible ? 20 : 0
                        spacing: 10

                        Text {
                            width: 150
                            text: keyRow.modelData.k
                            color: root.cAccent
                            font.pixelSize: root.ui(11)
                            font.family: "monospace"
                        }
                        Text {
                            width: keyRow.width - 160
                            text: keyRow.modelData.d
                            color: root.cText
                            font.pixelSize: root.ui(11)
                            font.family: root.uiFont
                            elide: Text.ElideRight
                        }
                    }
                }

                Text {
                    width: parent.width
                    topPadding: 6
                    wrapMode: Text.WordWrap
                    // ⚠ Said out loud, because the speed is real and the
                    // direction is not: J steps the frame monitor backwards
                    // rather than playing in reverse, which nothing can do to
                    // an encoded preview.
                    text: "J shuttles back through the frame monitor — it is a "
                          + "fast scrub, not playback in reverse."
                    color: root.cDim
                    font.pixelSize: root.ui(10)
                    font.family: root.uiFont
                }
            }
        }
    }

    // ── The start screen ───────────────────────────────────────────────
    //
    // Shown only while nothing at all is open. Three doors, because there are
    // exactly three ways in, and the one you want is not knowable from here.

    readonly property bool atStart: root.file === "" && root.proj === ""
                                    && !root.pickerOpen

    Rectangle {
        anchors.fill: parent
        visible: root.atStart
        color: root.cBg

        // Swallows clicks so nothing behind the start screen can be operated
        // through it.
        MouseArea { anchors.fill: parent; hoverEnabled: true }

        Column {
            anchors.centerIn: parent
            spacing: 10

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "SYNAPSE Studio"
                color: root.cText
                font.pixelSize: root.ui(26)
                font.family: root.uiFont
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "one colour engine, two ways in"
                color: root.cDim
                font.pixelSize: root.ui(12)
                font.family: root.uiFont
                bottomPadding: 18
            }

            Door {
                title: "Open a photograph"
                sub: "RAW or a still — develop it in the darkroom"
                onClicked: { root.mode = "photo"; root.pickerFor = "photo"
                             root.openPicker() }
            }
            Door {
                title: "New video project"
                sub: "a timeline to cut, grade and export"
                onClicked: {
                    root.mode = "video"
                    // The next free name, never the last project's. Save as
                    // is where a project gets the name it deserves.
                    root.newProjectUnique((Quickshell.env("HOME") || "/tmp")
                                          + "/synstudio-project.syntl")
                }
            }
            Door {
                title: "Open a project"
                sub: "a .syntl timeline you started earlier"
                onClicked: { root.mode = "video"; root.pickerFor = "project"
                             root.openPicker() }
            }
        }
    }

    // ── Export as ──────────────────────────────────────────────────────
    //
    // A name and a format, and the full path spelled out underneath so there
    // is no question where the file is about to land. The formats are the
    // engine's own list; the extension follows the choice, because a ProRes
    // file called .mp4 is an mp4 that no editor will read as ProRes.

    Rectangle {
        anchors.fill: parent
        visible: root.exportOpen
        color: Qt.rgba(0, 0, 0, 0.55)

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.exportOpen = false
        }

        Rectangle {
            id: exportSheet
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 80)
            // Sized from what is IN it. A height computed from a row count
            // plus a guess at the chrome was twenty pixels short, and what it
            // cut off was the Cancel and Export buttons — the two things the
            // dialog exists for. A fixed dialog height can never survive a
            // list that grows.
            height: Math.min(exportCol.implicitHeight + 28, parent.height - 80)
            color: root.cPanel
            radius: 6
            border.width: 1
            border.color: root.wash(0.28)

            MouseArea { anchors.fill: parent; hoverEnabled: true }

            Column {
                id: exportCol
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 10

                Text {
                    text: root.mode === "video" ? "Export the cut as"
                                                : "Export the photograph as"
                    color: root.cText
                    font.pixelSize: root.ui(13)
                    font.family: root.uiFont
                    font.bold: true
                }

                // The name, without its extension: the extension is the
                // format's to decide and typing a second one only argues.
                Rectangle {
                    width: parent.width
                    height: 28
                    radius: 3
                    color: root.wash(0.10)
                    border.width: 1
                    border.color: nameIn.activeFocus ? root.cAccent : root.wash(0.24)

                    TextInput {
                        id: nameIn
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.cText
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        clip: true
                        text: root.exportName
                        onTextChanged: root.exportName = text
                        onAccepted: root.doExport()
                    }
                }

                Repeater {
                    model: root.exportFormats

                    Rectangle {
                        id: fmtRow
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 30
                        radius: 3
                        color: root.exportFmt === fmtRow.index ? root.wash(0.22)
                               : fmtMa.containsMouse ? root.wash(0.12) : "transparent"
                        border.width: root.exportFmt === fmtRow.index ? 1 : 0
                        border.color: root.cAccent

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            width: 62
                            text: "." + fmtRow.modelData.ext
                            color: root.exportFmt === fmtRow.index ? root.cAccent
                                                                   : root.cText
                            font.pixelSize: root.ui(12)
                            font.family: "monospace"
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 76
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            text: fmtRow.modelData.label
                            color: root.cDim
                            font.pixelSize: root.ui(11)
                            font.family: root.uiFont
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: fmtMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.exportFmt = fmtRow.index
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: root.exportPath
                    color: root.cDim
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                    elide: Text.ElideLeft
                }

                Row {
                    spacing: 8
                    Btn { label: "Cancel"; onClicked: root.exportOpen = false }
                    Btn { label: "Export"
                          active: root.exportName !== ""
                                  && root.exportFormats.length > 0
                          onClicked: root.doExport() }
                }
            }
        }
    }

    // ── A name for the project ─────────────────────────────────────────
    //
    // The same sheet does New project and Save as, because they ask the same
    // question and have the same answer to give when the name is taken. The
    // path is spelled out underneath for the reason the export sheet spells
    // its own out: a file lands somewhere, and finding out where afterwards
    // is not a feature.

    Rectangle {
        anchors.fill: parent
        visible: root.saveOpen
        color: Qt.rgba(0, 0, 0, 0.55)

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.saveOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 80)
            height: Math.min(saveCol.implicitHeight + 28, parent.height - 80)
            color: root.cPanel
            radius: 6
            border.width: 1
            border.color: root.wash(0.28)

            MouseArea { anchors.fill: parent; hoverEnabled: true }

            Column {
                id: saveCol
                x: 14
                y: 14
                width: parent.width - 28
                spacing: 10

                Text {
                    text: root.saveWhat === "new" ? "New project"
                                                  : "Save the cut as"
                    color: root.cText
                    font.pixelSize: root.ui(13)
                    font.family: root.uiFont
                    font.bold: true
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.saveWhat === "new"
                          ? "A name for it. Everything you do to it is written as you do it."
                          : "Every edit is already on disk. This gives the cut a name of its own, and leaves a copy behind under the old one."
                    color: root.cDim
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                }

                // The name, without the extension: a .syntl is what this
                // program writes and typing a second one only argues.
                Rectangle {
                    width: parent.width
                    height: 28
                    radius: 3
                    color: root.wash(0.10)
                    border.width: 1
                    border.color: saveIn.activeFocus ? root.cAccent : root.wash(0.24)

                    TextInput {
                        id: saveIn
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.cText
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        clip: true
                        text: root.saveName
                        // A typed name is a DIFFERENT name, so the Replace the
                        // last one earned does not carry over to it — that is
                        // how a second press writes over a file nobody warned
                        // about.
                        onTextChanged: {
                            if (text !== root.saveName) root.saveReplace = false
                            root.saveName = text
                        }
                        onAccepted: root.doSave()
                        // The field is the whole dialog: opening it with the
                        // cursor somewhere else is a dialog you have to click
                        // into before you can answer it.
                        focus: root.saveOpen
                    }
                }

                Text {
                    width: parent.width
                    text: root.savePath
                    color: root.cDim
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                    elide: Text.ElideLeft
                }

                Text {
                    width: parent.width
                    visible: root.saveReplace
                    wrapMode: Text.WordWrap
                    text: "A project of that name is there already. Replace writes over it."
                    color: root.cBad
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                }

                Row {
                    spacing: 8
                    Btn { label: "Cancel"; onClicked: { root.saveOpen = false
                                                        root.saveReplace = false } }
                    Btn { label: root.saveReplace ? "Replace"
                                                  : root.saveWhat === "new" ? "Create" : "Save"
                          active: root.saveName !== "" && !root.saveBusy
                          onClicked: root.doSave() }
                }
            }
        }
    }

    // ── Which frame ────────────────────────────────────────────────────
    //
    // Sized to its content on purpose: nine presets fit, so there is nothing
    // to scroll and therefore no scrollbar owed. A list that grew past the
    // screen would need one — see the preflight gate.
    Rectangle {
        anchors.fill: parent
        visible: root.sizeMenuOpen
        color: Qt.rgba(0, 0, 0, 0.55)

        MouseArea {
            anchors.fill: parent
            onClicked: root.sizeMenuOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(360, parent.width - 80)
            height: Math.min(sizeCol.height + 28, parent.height - 80)
            color: root.cPanel
            radius: 6
            border.width: 1
            border.color: root.wash(0.28)

            MouseArea { anchors.fill: parent }

            Column {
                id: sizeCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 8

                Text {
                    width: parent.width
                    text: "Project frame"
                    color: root.cText
                    font.pixelSize: root.ui(14)
                    font.family: root.uiFont
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: "What this cut delivers in. Clips are fitted to it — "
                          + "changing it re-frames the whole timeline, it does "
                          + "not re-cut anything."
                    wrapMode: Text.WordWrap
                    color: root.cDim
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                }

                Repeater {
                    model: root.sizePresets
                    Rectangle {
                        id: sizeRow
                        required property var modelData
                        width: sizeCol.width
                        height: 30
                        // The one in use is marked, so the menu answers "what
                        // is it now" without being read against the status bar.
                        readonly property bool current:
                            root.tl.w === sizeRow.modelData.w
                            && root.tl.h === sizeRow.modelData.h
                        color: sizeArea.containsMouse ? root.wash(0.16)
                             : sizeRow.current ? root.wash(0.10) : "transparent"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left; anchors.leftMargin: 8
                            text: (sizeRow.current ? "● " : "")
                                  + sizeRow.modelData.label
                            color: root.cText
                            font.pixelSize: root.ui(12)
                            font.family: root.uiFont
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right; anchors.rightMargin: 8
                            text: sizeRow.modelData.w + " × " + sizeRow.modelData.h
                            color: root.cDim
                            font.pixelSize: root.ui(11)
                            font.family: root.uiFont
                        }
                        MouseArea {
                            id: sizeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setProjectSize(sizeRow.modelData.name)
                        }
                    }
                }

                Btn { label: "Close"; onClicked: root.sizeMenuOpen = false }
            }
        }
    }

    // ── Which LUT ──────────────────────────────────────────────────────
    //
    // The installed catalogue, and a way to any file. Both, because the
    // catalogue is what TRAVELS — a project naming `kodak2383` opens on a
    // machine where that file lives somewhere else — while a pack somebody
    // downloaded this afternoon is a folder of .cube files and nothing more.
    // SynapseOS ships no LUTs of its own, so on most machines this list is
    // empty until somebody puts one in ~/.config/synstudio/luts, and the row
    // that opens the picker has to be there for the list to be worth opening.
    Rectangle {
        anchors.fill: parent
        visible: root.lutMenuOpen
        color: Qt.rgba(0, 0, 0, 0.55)

        MouseArea {
            anchors.fill: parent
            onClicked: root.lutMenuOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(460, parent.width - 80)
            height: Math.min(420, parent.height - 80)
            color: root.cPanel
            radius: 6
            border.width: 1
            border.color: root.wash(0.28)

            MouseArea { anchors.fill: parent }

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                Text {
                    text: "LUT"
                    color: root.cText
                    font.pixelSize: root.ui(14)
                    font.family: root.uiFont
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: root.lutList.length > 0
                          ? "Installed, or choose a .cube from anywhere."
                          : "Nothing installed yet — put .cube files in "
                            + "~/.config/synstudio/luts, or choose one."
                    wrapMode: Text.WordWrap
                    color: root.cDim
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                }

                Flickable {
                    width: parent.width
                    height: parent.height - 96
                    contentHeight: lutCol.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: lutCol
                        width: parent.width

                        Repeater {
                            model: root.lutList
                            Rectangle {
                                id: lutRow
                                required property var modelData
                                width: lutCol.width
                                height: 30
                                color: lutArea.containsMouse ? root.wash(0.16) : "transparent"
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left; anchors.leftMargin: 8
                                    text: lutRow.modelData.name
                                    color: root.cText
                                    font.pixelSize: root.ui(12)
                                    font.family: root.uiFont
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.right: parent.right; anchors.rightMargin: 8
                                    text: lutRow.modelData.dims + " · " + lutRow.modelData.size
                                    color: root.cDim
                                    font.pixelSize: root.ui(10)
                                    font.family: root.uiFont
                                }
                                MouseArea {
                                    id: lutArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        // The NAME, not the path: that is the
                                        // half of this that survives the
                                        // project being opened elsewhere.
                                        root.setLut(lutRow.modelData.name)
                                        root.lutMenuOpen = false
                                    }
                                }
                            }
                        }
                    }
                }

                Row {
                    spacing: 8
                    Btn {
                        label: "Choose a file…"
                        onClicked: {
                            root.lutMenuOpen = false
                            root.pickerFor = "lut"
                            root.openPicker()
                        }
                    }
                    Btn { label: "Close"; onClicked: root.lutMenuOpen = false }
                }
            }
        }
    }

    // ── The open dialog ────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        visible: root.pickerOpen
        color: Qt.rgba(0, 0, 0, 0.55)

        // Swallows every click that misses the panel, so the darkroom
        // underneath cannot be dragged while a modal sheet is over it.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.pickerOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(720, parent.width - 80)
            height: Math.min(560, parent.height - 80)
            color: root.cPanel
            radius: 6
            border.width: 1
            border.color: root.wash(0.28)

            // The panel is not the backdrop: without its own area the click
            // that lands on it falls through and closes the dialog.
            MouseArea { anchors.fill: parent; hoverEnabled: true }

            Column {
                anchors.fill: parent
                anchors.margins: 1

                Rectangle {
                    width: parent.width
                    height: 40
                    color: "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.right: closeBtn.left
                        anchors.rightMargin: 10
                        text: root.pickerDir
                        color: root.cText
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        // The tail is the part that says where you are; the
                        // leading /home/velle/... is the part you can lose.
                        elide: Text.ElideLeft
                    }
                    Btn {
                        id: closeBtn
                        label: "Cancel"
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        onClicked: root.pickerOpen = false
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: root.wash(0.20)
                }

                ListView {
                    width: parent.width
                    height: parent.parent.height - 43
                    clip: true
                    model: root.pickerShown
                    // No ScrollBar: that type lives in QtQuick.Controls, and
                    // this window imports none of it. The develop panel above
                    // scrolls the same way, by flicking a plain Flickable.
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: rowItem
                        required property var modelData
                        width: ListView.view.width
                        height: 28
                        color: rowMa.containsMouse ? root.wash(0.16) : "transparent"

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            width: 18
                            // A glyph rather than an icon theme lookup: this
                            // window loads no icon engine and one missing name
                            // would leave a column of empty boxes.
                            text: rowItem.modelData.kind === "up"      ? "↑"
                                : rowItem.modelData.kind === "dir"     ? "▸"
                                : rowItem.modelData.kind === "project" ? "⧉"
                                : rowItem.modelData.kind === "video"   ? "▶"
                                : rowItem.modelData.kind === "audio"   ? "♪"
                                :                                        "▣"
                            color: rowItem.modelData.kind === "dir"
                                || rowItem.modelData.kind === "up" ? root.cAccent
                                                                   : root.cDim
                            font.pixelSize: root.ui(12)
                            font.family: root.uiFont
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 36
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            text: rowItem.modelData.kind === "up"
                                  ? "…" : rowItem.modelData.name
                            color: root.cText
                            font.pixelSize: root.ui(12)
                            font.family: root.uiFont
                            elide: Text.ElideMiddle
                        }
                        MouseArea {
                            id: rowMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                const m = rowItem.modelData
                                if (m.kind === "dir" || m.kind === "up") {
                                    root.pickerFellBack = true
                                    root.browseTo(m.path)
                                } else if (m.kind === "project") {
                                    root.pickerOpen = false
                                    root.pickerFor = "photo"
                                    root.mode = "video"
                                    root.proj = m.path
                                    root.selTrack = 0
                                    root.selClip = -1
                                    root.playhead = 0
                                    // A different document entirely, so the
                                    // rendered preview belongs to the old one.
                                    root.tlRev++
                                    root.reloadTimeline()
                                    root.say("")
                                } else if (m.kind === "look") {
                                    // A path, because this one came from
                                    // somewhere the catalogue does not reach.
                                    // ⚠ pickerFor goes back to "photo": left
                                    // on "lut", the next Open would list
                                    // nothing but .cube files.
                                    root.pickerOpen = false
                                    root.pickerFor = "photo"
                                    root.setLut(m.path)
                                } else if (root.pickerFor === "source") {
                                    // Into the SOURCE monitor rather than
                                    // onto a track: what this picker chose is
                                    // footage to decide an in and an out on.
                                    root.pickerOpen = false
                                    root.pickerFor = "photo"
                                    root.openSource(m.path)
                                } else if (root.pickerFor === "clip") {
                                    // The video page borrows the same picker.
                                    // It lists what the ENGINE can decode, so
                                    // a row that is drawn is a row that will
                                    // land on the timeline — and the row
                                    // already says whether it found a picture
                                    // or a sound, which is what decides the
                                    // track it lands on.
                                    root.pickerOpen = false
                                    root.pickerFor = "photo"
                                    root.addMedia(m.path, m.kind)
                                    root.say("added " + m.name)
                                } else {
                                    root.pickerOpen = false
                                    root.loadFile(m.path)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── One control ─────────────────────────────────────────────────────────
    // ── One thumbnail control ───────────────────────────────────────────────
    //
    // Four kinds and no more: a number with a range, a choice, a switch and
    // some words. The develop panel's slider knows about curves, LUT rows,
    // keyframes and the develop stack's own setter — none of which a canvas
    // size has — so this is its own delegate rather than a fifth mode of
    // that one.
    component ThumbCtl: Item {
        id: tc
        required property var row
        required property string group

        readonly property bool mine: tc.row.group === tc.group
        readonly property real val: parseFloat(tc.row.value) || 0
        readonly property bool isSwitch: tc.row.type === "int" && tc.row.hi === 1
        // A custom size only means anything on a custom canvas, and a plate's
        // colour only once there is a plate. A control that does nothing is
        // worse than a missing one: it is a thing to try.
        readonly property bool applies:
            (tc.row.key === "width" || tc.row.key === "height")
                ? root.thumbValue("canvas") === "custom"
            : (tc.row.key.indexOf("plate.") >= 0 || tc.row.key.indexOf(".pad") >= 0)
                ? parseFloat(root.thumbValue(tc.row.key.split(".")[0] + ".plate")) > 0
            : (tc.row.key === "bg.r" || tc.row.key === "bg.g" || tc.row.key === "bg.b")
                ? root.thumbValue("fit") === "fit"
            : true

        width: thumbCol.width
        visible: tc.mine && tc.applies
        height: !visible ? 0 : tc.row.type === "text" ? 52 : 44

        Text {
            id: tclbl
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            text: tc.row.label
            color: root.cText
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            visible: tc.row.type !== "text"
            text: tc.row.type === "enum" ? tc.row.value
                  : tc.isSwitch ? (tc.val > 0 ? "on" : "off")
                  : (Math.round(tc.val * 100) / 100)
            color: root.cAccent
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }

        // A switch: the whole row is the target, because a checkbox drawn at
        // eleven pixels is a thing to aim at.
        MouseArea {
            anchors.fill: parent
            visible: tc.isSwitch
            enabled: tc.isSwitch
            onClicked: root.setThumb(tc.row.key, tc.val > 0 ? "0" : "1")
        }

        // An enum: click to advance, which needs no overlay and no dismissal.
        MouseArea {
            anchors.fill: parent
            visible: tc.row.type === "enum"
            enabled: tc.row.type === "enum"
            onClicked: {
                const c = tc.row.choices
                if (!c.length) return
                const at = c.indexOf(tc.row.value)
                root.setThumb(tc.row.key, c[(at + 1) % c.length])
            }
        }

        // A number with a range.
        Rectangle {
            visible: !tc.isSwitch && tc.row.type !== "enum" && tc.row.type !== "text"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: tclbl.bottom; anchors.topMargin: 8
            height: 4
            radius: 2
            color: root.wash(0.20)

            readonly property real frac:
                tc.row.hi > tc.row.lo
                ? Math.max(0, Math.min(1, (tc.val - tc.row.lo)
                                          / (tc.row.hi - tc.row.lo))) : 0

            Rectangle {
                width: parent.width * parent.frac
                height: parent.height
                radius: 2
                color: root.cAccent
            }
            Rectangle {
                x: parent.width * parent.frac - 5
                y: -4
                width: 10; height: 12; radius: 2
                color: root.cAccent
            }

            MouseArea {
                id: tcdrag
                anchors.fill: parent
                anchors.margins: -10
                preventStealing: true
                property real last: 0
                function pick(mx) {
                    const f = Math.max(0, Math.min(1, (mx - 10) / parent.width))
                    let v = tc.row.lo + f * (tc.row.hi - tc.row.lo)
                    v = tc.row.type === "int" ? Math.round(v)
                                              : Math.round(v * 1000) / 1000
                    tcdrag.last = v
                    return v
                }
                // ⚠ ONE `set` on release, not one per tick. Every tick is a
                // process, a sidecar write, an undo step and a re-render of
                // the whole thumbnail — the mixer's faders learned this and
                // so did the develop sliders.
                onPressed: function (m) { pick(m.x) }
                onPositionChanged: function (m) { if (pressed) pick(m.x) }
                onReleased: root.setThumb(tc.row.key, String(tcdrag.last))
                onDoubleClicked: root.setThumb(tc.row.key, String(tc.row.lo))
            }
        }

        // Words. Committed on Enter or on losing focus, never per keystroke:
        // a `set` per character is a process per letter typed.
        Rectangle {
            visible: tc.row.type === "text"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: tclbl.bottom; anchors.topMargin: 6
            height: 24
            radius: 3
            color: root.wash(0.14)
            border.width: 1
            border.color: tcin.activeFocus ? root.cAccent : root.wash(0.2)

            TextInput {
                id: tcin
                anchors.fill: parent
                anchors.leftMargin: 7
                anchors.rightMargin: 7
                verticalAlignment: TextInput.AlignVCenter
                color: root.cText
                font.pixelSize: root.ui(11)
                font.family: root.uiFont
                clip: true
                text: tc.row.value
                onEditingFinished: if (text !== tc.row.value)
                                       root.setThumb(tc.row.key, text)
            }
        }
    }

    component Slider2: Item {
        id: sl
        // Taken as a REQUIRED property rather than read off the delegate's
        // implicit `modelData`, which a nested component cannot reach without
        // the linter calling it an unqualified access, and which resolves to
        // the wrong scope once components are nested any further.
        //
        // (The word for that linter must not appear at the start of a comment
        // here: a comment beginning with it is parsed as a DIRECTIVE, and each
        // following word is reported as an unknown category.)
        required property var modelData
        readonly property var row: sl.modelData
        width: panelCol.width
        // A curve row is text and a LUT row is a NAME; neither is a number
        // with a range, and drawing a track for one puts a handle at zero for
        // a control that has no zero. The LUT row draws itself, below.
        height: row.type === "curve" ? 0 : row.type === "str" ? 46 : 44
        visible: row.type !== "curve"

        readonly property real val: parseFloat(root.vals[sl.row.key]) || 0

        // ── the LUT row ─────────────────────────────────────────────────
        //
        // Everything the engine can be given is here: the installed
        // catalogue, a file from anywhere, and a way back to none. A
        // catalogue NAME is what travels between machines and a path is what
        // a file dropped in from somewhere else is, so both are offered and
        // the name is preferred where there is one.
        Loader {
            active: sl.row.type === "str"
            anchors.fill: parent
            sourceComponent: Item {
                readonly property string cur: root.vals[sl.row.key] || ""
                // The tail of a path, or the catalogue name as it stands.
                readonly property string shown:
                    cur === "" ? "None" : cur.replace(/^.*\//, "").replace(/\.cube$/i, "")

                Text {
                    id: lutLbl
                    anchors.left: parent.left; anchors.leftMargin: 12
                    anchors.top: parent.top; anchors.topMargin: 6
                    text: sl.row.label
                    color: root.cText
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                }
                Rectangle {
                    anchors.left: parent.left; anchors.leftMargin: 12
                    anchors.right: clearBtn.left; anchors.rightMargin: 6
                    anchors.top: lutLbl.bottom; anchors.topMargin: 4
                    height: 22
                    radius: 3
                    color: root.wash(0.14)
                    border.width: 1
                    border.color: root.wash(0.22)
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left; anchors.leftMargin: 8
                        anchors.right: parent.right; anchors.rightMargin: 8
                        text: parent.parent.shown
                        elide: Text.ElideMiddle
                        color: parent.parent.cur === "" ? root.cDim : root.cAccent
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.lutTarget = "photo"
                            root.lutMenuOpen = !root.lutMenuOpen
                        }
                    }
                }
                Rectangle {
                    id: clearBtn
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.top: lutLbl.bottom; anchors.topMargin: 4
                    width: 22; height: 22
                    radius: 3
                    visible: parent.cur !== ""
                    color: root.wash(0.14)
                    border.width: 1
                    border.color: root.wash(0.22)
                    Text {
                        anchors.centerIn: parent
                        text: "\u00d7"
                        color: root.cText
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.change(sl.row.key, "")
                    }

                }
            }
        }

        // ⚠ every one of these is hidden for a string row. A Loader drawn
        // OVER a live slider is not a replacement for it: the track, the
        // handle and the readout all still paint, and the handle sits at zero
        // under the name of the LUT — which reads as a control at its default
        // and is also draggable through the gap.
        Text {
            id: lbl
            visible: sl.row.type !== "str"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            text: sl.row.label
            color: root.cText
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }
        Text {
            visible: sl.row.type !== "str"
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            // A control at its default reads as blank rather than as "0", so
            // the eye finds the handful that have been touched.
            text: sl.val === 0 ? "" : (Math.round(sl.val * 100) / 100)
            color: sl.val === 0 ? root.cDim : root.cAccent
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }

        Rectangle {
            id: track
            visible: sl.row.type !== "str"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: lbl.bottom; anchors.topMargin: 8
            height: 4
            radius: 2
            color: root.isLight ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(1, 1, 1, 0.14)

            // Where zero sits, so a two-sided control shows which way it went.
            readonly property real zeroFrac:
                sl.row.lo < 0 ? (0 - sl.row.lo) / (sl.row.hi - sl.row.lo) : 0
            readonly property real valFrac:
                Math.max(0, Math.min(1, (sl.val - sl.row.lo) / (sl.row.hi - sl.row.lo)))

            Rectangle {
                height: parent.height
                radius: 2
                color: root.cAccent
                x: Math.min(track.zeroFrac, track.valFrac) * track.width
                width: Math.abs(track.valFrac - track.zeroFrac) * track.width
            }

            Rectangle {
                width: 12; height: 12; radius: 6
                color: root.cAccent
                y: -4
                x: track.valFrac * track.width - 6
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                hoverEnabled: false
                // The develop panel is a Flickable, and a slider drag is never
                // perfectly horizontal, so it was handing the gesture over to
                // the scroll after ten pixels. See the timeline ruler.
                preventStealing: true

                property real lastVal: 0

                function commit(mx, live) {
                    const f = Math.max(0, Math.min(1, (mx - 10) / track.width))
                    let v = sl.row.lo + f * (sl.row.hi - sl.row.lo)
                    v = sl.row.type === "int" ? Math.round(v)
                                              : Math.round(v * 100) / 100
                    lastVal = v
                    root.change(sl.row.key, v)
                }
                onPressed: function (m) { root.dragging = true; commit(m.x, true) }
                onPositionChanged: function (m) { if (pressed) commit(m.x, true) }
                onReleased: function (m) {
                    root.dragging = false
                    // ⚠ The same value, set once more with the hand off the
                    // mouse — which is the press that goes in the undo
                    // history. Every tick before it carried --no-history, so
                    // one drag is one step back rather than a hundred.
                    root.change(sl.row.key, lastVal)
                    // The full-size render only happens once, here. Rendering
                    // at export resolution on every mouse move would make the
                    // slider lag behind the hand.
                    root.requestRender()
                }
                // Double click returns a control to its default, which is the
                // gesture every editor uses and the only quick way back from a
                // slider somebody dragged by accident.
                onDoubleClicked: root.change(sl.row.key, 0)
            }
        }
    }

    // ── A picture that does not blink ───────────────────────────────────────
    //
    // Both pages show a PNG the engine just wrote, at a path that never
    // changes, so the URL carries a serial to defeat Qt's cache. A plain Image
    // then goes BLANK for the whole time the new file is decoding — which is
    // every scrub step and every slider release, and reads as the viewport
    // flashing black between frames.
    //
    // Two images, one showing and one loading. The new frame is decoded into
    // whichever is hidden and they swap only once it is Ready, so the last
    // good frame stays on screen until there is a better one. A load that
    // FAILS never swaps, which is also right: the previous frame is a better
    // answer than a blank rectangle.
    component Monitor: Item {
        id: mon
        property string source: ""
        property int    front: 0

        onSourceChanged: {
            if (!mon.source) return
            if (mon.front === 0) imgB.source = mon.source
            else                 imgA.source = mon.source
        }

        Image {
            id: imgA
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            smooth: true
            cache: false
            asynchronous: true
            visible: mon.front === 0
            onStatusChanged: if (status === Image.Ready) mon.front = 0
        }
        Image {
            id: imgB
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            smooth: true
            cache: false
            asynchronous: true
            visible: mon.front === 1
            onStatusChanged: if (status === Image.Ready) mon.front = 1
        }
    }

    // ── A channel strip ─────────────────────────────────────────────────────
    //
    // One per track, plus a master at the end (trackIndex -1). A video track
    // gets a strip too: its clips carry dialogue more often than not, and a
    // fader you cannot reach because the picture is on the same track is the
    // reason the sound on most first cuts is wrong.
    component Strip: Rectangle {
        id: st
        property int trackIndex: -1
        property string label: ""
        property bool isAudio: true
        readonly property bool isMaster: st.trackIndex < 0
        readonly property var trk:
            (!st.isMaster && st.trackIndex < root.tl.tracks.length)
            ? root.tl.tracks[st.trackIndex] : null

        readonly property real gainDb:
            st.isMaster ? root.masterDb : root.mixOf(st.trackIndex, "gain")
        readonly property real panVal:
            st.isMaster ? 0 : root.mixOf(st.trackIndex, "pan")
        readonly property real level:
            st.isMaster ? root.masterLevel
                        : (st.trackIndex >= 0 ? root.trackLevel(st.trackIndex) : 0)

        width: 74
        color: st.isMaster ? root.wash(0.10) : "transparent"
        border.width: 1
        border.color: root.wash(0.10)

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            Text {
                width: parent.width
                text: st.label
                color: st.isMaster ? root.cAccent : root.cText
                font.pixelSize: root.ui(11)
                font.family: root.uiFont
                font.bold: true
                elide: Text.ElideRight
            }

            // The number, because a fader without one is a guess. Blank at
            // unity, the same way an untouched develop slider reads blank.
            Text {
                width: parent.width
                text: Math.abs(st.gainDb) < 0.05 ? "0.0 dB"
                      : (st.gainDb > 0 ? "+" : "") + st.gainDb.toFixed(1) + " dB"
                color: Math.abs(st.gainDb) < 0.05 ? root.cDim : root.cAccent
                font.pixelSize: root.ui(10)
                font.family: "monospace"
            }

            // Fader and meter side by side, which is the only arrangement that
            // lets one hand set a level while the eye reads it.
            Item {
                width: parent.width
                height: st.height - 148

                // Meter
                Rectangle {
                    id: meter
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 9
                    radius: 2
                    color: root.wash(0.14)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.height * root.meterFrac(st.level)
                        radius: 2
                        // Green until it is loud, amber approaching full
                        // scale, red at it — the one place in this window
                        // where colour means a number and not a theme.
                        color: st.level >= 1.0 ? "#e0463c"
                               : st.level >= 0.7 ? "#d8a13a" : "#3fa06e"
                    }

                    // Where full scale is, so "loud" has a line rather than a
                    // feeling.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        y: parent.height * (1 - root.meterFrac(1.0))
                        height: 1
                        color: root.wash(0.45)
                    }
                }

                // Fader
                Rectangle {
                    id: fader
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 6
                    width: 4
                    radius: 2
                    x: 14
                    color: root.wash(0.18)

                    // -60..+24 dB, with 0 where a fader's 0 always is: not in
                    // the middle, but where unity falls on that range.
                    readonly property real frac:
                        Math.max(0, Math.min(1, (st.gainDb + 60) / 84))

                    Rectangle {
                        width: 22
                        height: 12
                        radius: 2
                        color: root.cAccent
                        x: -9
                        y: (1 - fader.frac) * (fader.height - 12)
                    }
                    // Unity, drawn once and not moving.
                    Rectangle {
                        x: -6; width: 16; height: 1
                        y: (1 - (60 / 84)) * (fader.height - 12) + 6
                        color: root.wash(0.4)
                    }

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -12
                        // The strip lives in a Flickable and the panel is one
                        // too; without this the fader hands the drag over on
                        // the first move. See the timeline ruler.
                        preventStealing: true

                        function toDb(my) {
                            const f = 1 - Math.max(0, Math.min(1,
                                          (my - 12) / (fader.height - 12)))
                            return Math.round((f * 84 - 60) * 10) / 10
                        }
                        onPressed: function (m) {
                            if (st.isMaster) root.masterLive = toDb(m.y)
                            else root.mixLiveSet(st.trackIndex, "gain", toDb(m.y))
                        }
                        onPositionChanged: function (m) {
                            if (!pressed) return
                            if (st.isMaster) root.masterLive = toDb(m.y)
                            else root.mixLiveSet(st.trackIndex, "gain", toDb(m.y))
                        }
                        // ONE command, on release. A `set` per tick would be
                        // dropped by a busy Process and would reload the
                        // timeline under the hand that is still dragging.
                        onReleased: function (m) {
                            if (st.isMaster) root.masterCommit(toDb(m.y))
                            else root.mixCommit(st.trackIndex, "gain", toDb(m.y))
                        }
                        onDoubleClicked: {
                            if (st.isMaster) root.masterCommit(0)
                            else root.mixCommit(st.trackIndex, "gain", 0)
                        }
                    }
                }
            }

            Text {
                visible: !st.isMaster
                width: parent.width
                text: Math.abs(st.panVal) < 0.005 ? "pan C"
                      : "pan " + (st.panVal < 0 ? "L" : "R")
                           + Math.round(Math.abs(st.panVal) * 100)
                color: Math.abs(st.panVal) < 0.005 ? root.cDim : root.cAccent
                font.pixelSize: root.ui(9)
                font.family: "monospace"
            }

            // Pan, as a bar rather than a knob: a knob is a circle you have to
            // learn to drag and a bar says left and right by pointing.
            Rectangle {
                visible: !st.isMaster
                width: parent.width
                height: 5
                radius: 2
                color: root.wash(0.14)

                Rectangle {
                    x: st.panVal >= 0 ? parent.width / 2
                                      : parent.width / 2 * (1 + st.panVal)
                    width: Math.max(2, Math.abs(st.panVal) * parent.width / 2)
                    height: parent.height
                    radius: 2
                    color: root.cAccent
                }
                Rectangle {
                    x: parent.width / 2
                    width: 1; height: parent.height
                    color: root.wash(0.45)
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    preventStealing: true
                    function toPan(mx) {
                        const f = (mx - 8) / parent.width
                        return Math.round(Math.max(-1, Math.min(1, f * 2 - 1)) * 100) / 100
                    }
                    onPressed: function (m) { root.mixLiveSet(st.trackIndex, "pan", toPan(m.x)) }
                    onPositionChanged: function (m) {
                        if (pressed) root.mixLiveSet(st.trackIndex, "pan", toPan(m.x))
                    }
                    onReleased: function (m) { root.mixCommit(st.trackIndex, "pan", toPan(m.x)) }
                    onDoubleClicked: root.mixCommit(st.trackIndex, "pan", 0)
                }
            }

            Row {
                visible: !st.isMaster
                spacing: 4
                Tag {
                    label: "M"
                    on: st.trk ? st.trk.muted : false
                    onClicked: root.tlRun(["track", root.proj, String(st.trackIndex),
                                           "--mute", st.trk && st.trk.muted ? "0" : "1"])
                }
                Tag {
                    label: "S"
                    on: st.trk ? st.trk.solo : false
                    onClicked: root.tlRun(["track", root.proj, String(st.trackIndex),
                                           "--solo", st.trk && st.trk.solo ? "0" : "1"])
                }
            }
        }
    }

    // ── A waveform ──────────────────────────────────────────────────────────
    //
    // Drawn per PIXEL COLUMN, taking the loudest bucket that falls in each
    // one. Sampling one bucket per column instead would alias badly zoomed
    // out — a transient landing between two sampled buckets simply vanishes,
    // and the waveform quietly stops being a picture of the audio.
    component Waveform: Canvas {
        id: wf
        property var  peaks: null
        property color ink: "#000000"

        onPeaksChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onInkChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const d = wf.peaks
            if (!d || d.length === 0 || width < 2 || height < 3) return

            const w = Math.floor(width), h = height, mid = h / 2
            ctx.fillStyle = wf.ink
            ctx.globalAlpha = 0.75

            for (let x = 0; x < w; x++) {
                const i0 = Math.floor(x * d.length / w)
                const i1 = Math.max(i0 + 1, Math.floor((x + 1) * d.length / w))
                let m = 0
                for (let i = i0; i < i1 && i < d.length; i++)
                    if (d[i] > m) m = d[i]
                // Drawn on a dB scale, not linearly.
                //
                // Ordinary recorded audio peaks around -18 dBFS, which
                // LINEARLY is twelve percent of the height — a sliver you
                // cannot read a cut from, on material that is not remotely
                // quiet. Mapping -48..0 dB onto the height puts normal
                // programme material at about two thirds and still leaves
                // headroom to see something actually loud. It is what every
                // editor does and what the eye expects.
                let n = 0
                if (m > 0.0001) {
                    n = (20 * Math.log(m) / Math.LN10 + 48) / 48
                    if (n < 0) n = 0
                    if (n > 1) n = 1
                }
                // A floor of half a pixel, so a quiet passage still reads as a
                // line of audio rather than as a gap where the clip has none.
                const half = Math.max(0.5, n * (mid - 1))
                ctx.fillRect(x, mid - half, 1, half * 2)
            }
        }
    }

    // One choice on the start screen. Wide and two-line on purpose: these are
    // the three decisions that set up everything after them, and a row of
    // small buttons would make them look like the toolbar.
    component Door: Rectangle {
        id: door
        property string title: ""
        property string sub: ""
        signal clicked()

        // ⚠ THE BOX SCALES WITH THE TEXT IT HOLDS. 340×56 was a fixed size for
        // two lines at their default sizes, and at a text scale of 150% the
        // title and the subtitle overlapped inside it and the subtitle ran out
        // past the right edge — the one control on the start screen, unusable,
        // because a size that exists ONLY to hold N lines of text is not really
        // a size, it is a line count.
        //
        // The suite's rule is that ui() scales pixelSize and nothing else, and
        // this is the documented exception to it: heights that are a stand-in
        // for the text inside them. The width follows the widest label rather
        // than a second guess at it, so no scale can clip the subtitle.
        width: Math.max(340, doorSub.implicitWidth + 32)
        height: root.ui(56)
        radius: 5
        color: doorMa.containsMouse ? root.wash(0.24) : root.wash(0.10)
        border.width: 1
        border.color: root.wash(0.30)

        Text {
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.top: parent.top; anchors.topMargin: 10
            text: door.title
            color: root.cText
            font.pixelSize: root.ui(14)
            font.family: root.uiFont
        }
        Text {
            id: doorSub
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.bottom: parent.bottom; anchors.bottomMargin: 10
            text: door.sub
            color: root.cDim
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }
        MouseArea {
            id: doorMa
            anchors.fill: parent
            hoverEnabled: true
            onClicked: door.clicked()
        }
    }

    // A page selector. Not a Btn with a colour swapped: the pressed state has
    // to read as WHERE YOU ARE rather than as a button that happens to be lit,
    // so it carries an underline the eye reads as a tab and a Btn does not.
    component Tab: Item {
        id: tab
        property string label: ""
        property bool on: false
        signal clicked()
        width: tt.implicitWidth + 24
        height: 30

        Text {
            id: tt
            anchors.centerIn: parent
            text: tab.label
            color: tab.on ? root.cText : root.cDim
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
            font.bold: tab.on
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 10
            height: 2
            color: tab.on ? root.cAccent : "transparent"
        }
        MouseArea {
            anchors.fill: parent
            onClicked: tab.clicked()
        }
    }

    // A small on/off stud, for the per-track flags and the lane adders.
    component Tag: Rectangle {
        id: tg
        property string label: ""
        property bool on: false
        signal clicked()
        width: Math.max(18, tgt.implicitWidth + 8)
        height: 16
        radius: 3
        color: tg.on ? root.cAccent : root.wash(0.16)
        Text {
            id: tgt
            anchors.centerIn: parent
            text: tg.label
            color: tg.on ? root.cPanel : root.cDim
            font.pixelSize: root.ui(9)
            font.family: root.uiFont
            font.bold: true
        }
        MouseArea { anchors.fill: parent; onClicked: tg.clicked() }
    }

    // ── One clip property ───────────────────────────────────────────────────
    //
    // Four shapes, chosen by the type the ENGINE reported: a number is a
    // slider, an enum is a cycler over the choices the engine listed, and a
    // caption is a field. Nothing here knows which transitions exist or what
    // an opacity may be — that all arrived from `timeline keys`.
    component ClipCtl: Item {
        id: cc
        required property var modelData
        readonly property var row: cc.modelData
        readonly property string raw: root.clipValue(cc.row.key)
        readonly property real val: parseFloat(cc.raw) || 0
        readonly property int nkeys: root.clipAnimKeys(cc.row.key).length
        readonly property bool onKey: root.animKeyAt(cc.row.key) >= 0
        readonly property bool longEnum:
            cc.row.type === "enum" && cc.row.choices.length > 10
        // The font row is a text row with a LIST — see the picker below.
        readonly property bool isFont: cc.row.key === "text.font"
        // ⚠ A noise model is somebody else's FILE and travels no better than
        // a LUT does. The engine says whether the name resolved HERE, because
        // a project that names one is perfectly valid on the machine that has
        // it and silently does not denoise on the machine that does not.
        readonly property bool isModel: cc.row.key === "nr.model"
        readonly property bool modelMissing:
            cc.isModel && cc.raw !== "" && root.clipValue("nr.model.found") !== "1"
        property bool fontOpen: false
        // A colour to dip THROUGH is only a colour if the transition dips.
        readonly property bool applies:
            (cc.row.key === "trans.r" || cc.row.key === "trans.g"
             || cc.row.key === "trans.b") ? root.clipValue("trans") === "dip"
                                          : true
        visible: cc.applies
        width: inspCol.width
        readonly property bool curveOpen: root.curveKey === cc.row.key
        height: !cc.applies ? 0
                : cc.longEnum ? 150
                : (cc.isFont && cc.fontOpen) ? 214
                : cc.curveOpen ? 190
                : cc.row.type === "text" ? 52 : 44

        Text {
            id: cclbl
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            text: cc.row.label
            color: root.cText
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            spacing: 8

            Text {
                visible: cc.row.type !== "text"
                text: cc.row.type === "enum" ? cc.raw
                                             : (Math.round(cc.val * 100) / 100)
                color: root.cAccent
                font.pixelSize: root.ui(11)
                font.family: root.uiFont
            }

            // The diamond, on the rows the renderer can actually animate.
            //
            // Hollow means the property is a plain number; filled means the
            // playhead is parked ON a key, which is also when clicking it
            // takes the key away again. In between the two — keyed, but
            // between keys — it is hollow and lit, because the value under
            // the slider belongs to the moment and not to the clip.
            Text {
                visible: cc.row.anim && root.selClipObj !== null
                text: cc.onKey ? "◆" : "◇"
                color: cc.nkeys > 0 ? root.cAccent : root.cDim
                font.pixelSize: root.ui(11)
                font.family: root.uiFont
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -5
                    onClicked: root.animToggle(cc.row.key)
                }
            }

            // The curve, over time. Only where there is one to look at: a
            // graph of a property with a single key is a flat line and a
            // control that opens one is a control that wasted a click.
            Text {
                visible: cc.row.anim && cc.nkeys > 1
                text: cc.curveOpen ? "▴∿" : "▾∿"
                color: cc.curveOpen ? root.cAccent : root.cDim
                font.pixelSize: root.ui(11)
                font.family: root.uiFont
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -5
                    onClicked: root.openCurve(cc.row.key)
                }
            }
        }

        // enum — click to advance. With at most a handful of choices a cycler
        // beats a popup: it needs no overlay, no focus grab and no dismissal
        // rule. Past that it stops being a control at all — sixty transitions
        // is sixty clicks to reach the last one — so a long enum gets a list
        // instead. The threshold is on the CHOICES, not on the key, so the
        // table still decides and nothing here knows what a transition is.
        Rectangle {
            visible: cc.row.type === "enum" && !cc.longEnum
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: cclbl.bottom; anchors.topMargin: 6
            height: 20
            radius: 3
            color: root.wash(0.14)
            Text {
                anchors.centerIn: parent
                text: "◂  " + root.enumLabel(cc.row.key, cc.raw) + "  ▸"
                color: root.cText
                font.pixelSize: root.ui(10)
                font.family: root.uiFont
            }
            MouseArea {
                anchors.fill: parent
                onClicked: function (m) {
                    const ch = cc.row.choices
                    if (!ch || ch.length === 0) return
                    let i = ch.indexOf(cc.raw)
                    if (i < 0) i = 0
                    i = (m.x < width / 2) ? (i + ch.length - 1) % ch.length
                                          : (i + 1) % ch.length
                    root.setClip(cc.row.key, ch[i])
                }
            }
        }

        // The long form: every choice, scrolled, with the one in force marked.
        Rectangle {
            visible: cc.longEnum
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: cclbl.bottom; anchors.topMargin: 6
            height: cc.longEnum ? 128 : 0
            radius: 3
            color: root.wash(0.14)
            clip: true

            ListView {
                id: cclist
                anchors.fill: parent
                anchors.margins: 3
                model: cc.row.choices
                clip: true
                // The list opens on the choice in force rather than at the
                // top: with sixty rows, a picker that always starts at the
                // beginning is one that never shows you what you picked.
                Component.onCompleted: {
                    const i = cc.row.choices.indexOf(cc.raw)
                    if (i >= 0) cclist.positionViewAtIndex(i, ListView.Center)
                }
                delegate: Rectangle {
                    required property string modelData
                    width: cclist.width
                    height: 18
                    color: modelData === cc.raw ? root.wash(0.24) : "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        width: parent.width - 12
                        elide: Text.ElideRight
                        text: root.enumLabel(cc.row.key, parent.modelData)
                        color: parent.modelData === cc.raw ? root.cAccent : root.cText
                        font.pixelSize: root.ui(10)
                        font.family: root.uiFont
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.setClip(cc.row.key, parent.modelData)
                    }
                }
            }
        }

        // text — committed on Enter or on losing focus, never per keystroke.
        // A `set` per character would spawn a process per letter typed.
        Rectangle {
            id: ccfield
            visible: cc.row.type === "text"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right
            // The font row keeps a strip on the right for the ▾ that opens
            // the family list.
            anchors.rightMargin: cc.isFont ? 58 : 12
            anchors.top: cclbl.bottom; anchors.topMargin: 6
            height: 24
            radius: 3
            color: root.wash(0.14)
            border.width: 1
            // A family this machine has not got is not an error — the render
            // still happens, in whatever fc-match lands on — but it is never
            // what was meant, and a plain field had no way to say so.
            border.color: cti.activeFocus ? root.cAccent
                        : (cc.isFont && !root.fontInstalled(cc.raw)) ? root.cBad
                        : cc.modelMissing ? root.cBad
                        : root.wash(0.2)

            TextInput {
                id: cti
                anchors.fill: parent
                anchors.leftMargin: 7
                anchors.rightMargin: 7
                verticalAlignment: TextInput.AlignVCenter
                color: root.cText
                font.pixelSize: root.ui(11)
                font.family: root.uiFont
                clip: true
                text: cc.raw
                onEditingFinished: if (text !== cc.raw) root.setClip(cc.row.key, text)
            }
        }

        // ── The family list ─────────────────────────────────────────────────
        //
        // Opened by a button and not by FOCUS, deliberately. Focus is the
        // obvious trigger and it does not work: clicking a row in the list
        // takes focus off the field, which closes the list out from under the
        // click, and the row never fires. A button owns its own state and
        // needs no focus grab at all.
        Rectangle {
            visible: cc.isFont
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.verticalCenter: ccfield.verticalCenter
            width: 42
            height: 24
            radius: 3
            color: cc.fontOpen ? root.cAccent : root.wash(0.14)
            Text {
                anchors.centerIn: parent
                // The COUNT, not just an arrow: "there are 255 of these" is
                // the fact the field never conveyed, and it is also how a
                // machine with no fontconfig says so — it reads 0.
                text: (cc.fontOpen ? "▴ " : "▾ ") + root.fontList.length
                color: cc.fontOpen ? root.cPanel : root.cDim
                font.pixelSize: root.ui(9)
                font.family: root.uiFont
            }
            MouseArea {
                anchors.fill: parent
                onClicked: cc.fontOpen = !cc.fontOpen
            }
        }

        // ── The curve editor ────────────────────────────────────────────────
        //
        // x is time INSIDE the clip, y is the property's own range. The line
        // is what the engine sampled; the squares are the keys. Drag one,
        // click the empty space to put one there, double-click one to take it
        // away, and the row of eases below sets how the selected key LEAVES.
        Rectangle {
            id: cccurve
            visible: cc.curveOpen
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 30
            height: visible ? 110 : 0
            radius: 3
            color: root.wash(0.10)
            border.width: 1
            border.color: root.wash(0.22)
            clip: true

            readonly property real len: root.selClipObj ? root.selClipObj.len : 0
            readonly property real lo: cc.row.lo
            readonly property real hi: cc.row.hi
            property int picked: -1

            function xOf(t) { return cccurve.len > 0 ? t / cccurve.len * width : 0 }
            function yOf(v) {
                const f = cccurve.hi > cccurve.lo
                          ? (v - cccurve.lo) / (cccurve.hi - cccurve.lo) : 0
                return (1 - Math.max(0, Math.min(1, f))) * height
            }
            function tOf(x) {
                return Math.max(0, Math.min(cccurve.len,
                                            x / Math.max(1, width) * cccurve.len))
            }
            function vOf(y) {
                const f = 1 - Math.max(0, Math.min(1, y / Math.max(1, height)))
                return cccurve.lo + f * (cccurve.hi - cccurve.lo)
            }

            Canvas {
                id: curveCanvas
                anchors.fill: parent
                // ⚠ Repainted from the SERIAL, not from the array: assigning
                // a new array of the same length changes no property QML can
                // see a difference in, and the line would stay on the old
                // shape until something else happened to repaint it.
                property int serial: root.curveSerial
                onSerialChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    // A midline, so a value in the middle of the range is not
                    // a line floating in an empty box.
                    ctx.strokeStyle = Qt.rgba(root.cAccent.r, root.cAccent.g,
                                              root.cAccent.b, 0.18)
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(0, height / 2)
                    ctx.lineTo(width, height / 2)
                    ctx.stroke()

                    const pts = root.curvePts
                    if (!pts || pts.length < 2) return
                    ctx.strokeStyle = root.cAccent
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    for (let i = 0; i < pts.length; i++) {
                        const x = cccurve.xOf(pts[i].t)
                        const y = cccurve.yOf(pts[i].v)
                        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
            }

            // Where the playhead is inside the clip, so a key can be read
            // against the frame on screen.
            Rectangle {
                x: cccurve.xOf(root.clipOffset) - 1
                width: 2
                height: parent.height
                color: root.cText
                opacity: 0.35
            }

            // The keys. Dragged with the mouse, committed on RELEASE — one
            // `anim move` for the gesture rather than one per pixel, which
            // the busy Process would drop anyway.
            Repeater {
                model: root.clipAnimKeys(cc.row.key)

                Rectangle {
                    id: kn
                    required property var modelData
                    required property int index
                    width: 9; height: 9; radius: 2
                    x: cccurve.xOf(kn.modelData.t) - 4
                    y: cccurve.yOf(kn.modelData.v) - 4
                    color: cccurve.picked === kn.index ? root.cText : root.cAccent
                    border.width: 1
                    border.color: root.cPanel

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        preventStealing: true
                        property real lastT: 0
                        property real lastV: 0
                        onPressed: function (m) {
                            cccurve.picked = kn.index
                            lastT = kn.modelData.t
                            lastV = kn.modelData.v
                        }
                        onPositionChanged: function (m) {
                            if (!pressed) return
                            const p = mapToItem(cccurve, m.x, m.y)
                            lastT = cccurve.tOf(p.x)
                            lastV = cccurve.vOf(p.y)
                            // Moved live so the hand sees the key follow it;
                            // the document only hears about it on release.
                            kn.x = cccurve.xOf(lastT) - 4
                            kn.y = cccurve.yOf(lastV) - 4
                        }
                        onReleased: root.curveMove(cc.row.key, kn.index, lastT, lastV)
                        onDoubleClicked: root.curveRemove(cc.row.key, kn.index)
                    }
                }
            }

            // Empty space: a new key where it was clicked. LAST, so a click
            // that lands on a key reaches the key and not this.
            MouseArea {
                anchors.fill: parent
                z: -1
                onClicked: function (m) {
                    root.curveAdd(cc.row.key, cccurve.tOf(m.x), cccurve.vOf(m.y))
                }
            }
        }

        // How the picked key LEAVES. Five polynomials, because the export has
        // to evaluate the same shape in ffmpeg's expression language.
        Row {
            visible: cc.curveOpen
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.top: cccurve.bottom; anchors.topMargin: 6
            spacing: 6

            Repeater {
                model: ["linear", "in", "out", "inout", "hold"]

                Rectangle {
                    id: eb
                    required property var modelData
                    width: 46; height: 20; radius: 3
                    color: ebm.containsMouse ? root.wash(0.25) : root.wash(0.12)
                    Text {
                        anchors.centerIn: parent
                        text: eb.modelData
                        color: root.cText
                        font.pixelSize: root.ui(9)
                        font.family: root.uiFont
                    }
                    MouseArea {
                        id: ebm
                        anchors.fill: parent
                        hoverEnabled: true
                        // Nothing picked yet is not an error: the ease lands
                        // on the key the hand last touched, and until it has
                        // touched one there is nothing to change.
                        onClicked: {
                            if (cccurve.picked >= 0)
                                root.curveEase(cc.row.key, cccurve.picked,
                                               eb.modelData)
                            else root.say("pick a key on the curve first")
                        }
                    }
                }
            }
        }

        Rectangle {
            id: ccfonts
            visible: cc.isFont && cc.fontOpen
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: ccfield.bottom; anchors.topMargin: 6
            height: visible ? 130 : 0
            radius: 3
            color: root.wash(0.14)
            clip: true

            // The filter is its OWN field, not the value field above.
            //
            // Filtering with the value field would commit whatever was typed:
            // type "jet" to narrow the list, click JetBrains Mono, and the
            // field loses focus first — so the title is lettered in a family
            // called "jet" for as long as it takes the click to land. Two
            // fields, and that race does not exist.
            Rectangle {
                id: ccfilter
                anchors.top: parent.top; anchors.topMargin: 4
                anchors.left: parent.left; anchors.leftMargin: 4
                anchors.right: parent.right; anchors.rightMargin: 4
                height: 20
                radius: 3
                color: root.wash(0.10)
                TextInput {
                    id: ccfi
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.cText
                    font.pixelSize: root.ui(10)
                    font.family: root.uiFont
                    clip: true
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 6
                    visible: ccfi.text === ""
                    text: root.fontList.length > 0
                          ? "type to narrow the list"
                          : "no font list here — fontconfig is not installed"
                    color: root.cDim
                    font.pixelSize: root.ui(10)
                    font.family: root.uiFont
                }
            }

            ListView {
                id: ccfl
                anchors.top: ccfilter.bottom; anchors.topMargin: 4
                anchors.left: parent.left; anchors.leftMargin: 4
                anchors.right: parent.right; anchors.rightMargin: 4
                anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                clip: true
                model: {
                    const q = ccfi.text.toLowerCase()
                    if (!q) return root.fontList
                    const out = []
                    for (let i = 0; i < root.fontList.length; i++)
                        if (root.fontList[i].toLowerCase().indexOf(q) >= 0)
                            out.push(root.fontList[i])
                    return out
                }
                delegate: Rectangle {
                    required property string modelData
                    width: ccfl.width
                    height: 18
                    color: modelData === cc.raw ? root.wash(0.24)
                         : ccflArea.containsMouse ? root.wash(0.18)
                         : "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left; anchors.leftMargin: 6
                        width: parent.width - 12
                        elide: Text.ElideRight
                        text: parent.modelData
                        // Each row DRAWN IN ITS OWN FACE. A list of family
                        // names all set in the same font tells you their
                        // spelling and nothing else, and the whole question
                        // here is what one looks like.
                        font.family: parent.modelData
                        font.pixelSize: root.ui(11)
                        color: parent.modelData === cc.raw ? root.cAccent : root.cText
                    }
                    MouseArea {
                        id: ccflArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            root.setClip(cc.row.key, parent.modelData)
                            cc.fontOpen = false
                        }
                    }
                }
            }
        }

        // number
        Rectangle {
            id: cctrack
            visible: cc.row.type === "float" || cc.row.type === "int"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: cclbl.bottom; anchors.topMargin: 8
            height: 4
            radius: 2
            color: root.isLight ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(1, 1, 1, 0.14)

            readonly property real zeroFrac:
                cc.row.lo < 0 ? (0 - cc.row.lo) / (cc.row.hi - cc.row.lo) : 0
            readonly property real valFrac:
                Math.max(0, Math.min(1, (cc.val - cc.row.lo) / (cc.row.hi - cc.row.lo)))

            Rectangle {
                height: parent.height
                radius: 2
                color: root.cAccent
                x: Math.min(cctrack.zeroFrac, cctrack.valFrac) * cctrack.width
                width: Math.abs(cctrack.valFrac - cctrack.zeroFrac) * cctrack.width
            }
            Rectangle {
                width: 12; height: 12; radius: 6
                color: root.cAccent
                y: -4
                x: cctrack.valFrac * cctrack.width - 6
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                // The inspector is a Flickable too. See the timeline ruler.
                preventStealing: true
                function commit(mx) {
                    const f = Math.max(0, Math.min(1, (mx - 10) / cctrack.width))
                    let v = cc.row.lo + f * (cc.row.hi - cc.row.lo)
                    v = cc.row.type === "int" ? Math.round(v)
                                              : Math.round(v * 1000) / 1000
                    root.setClip(cc.row.key, v)
                }
                onPressed: function (m) { commit(m.x) }
                onPositionChanged: function (m) { if (pressed) commit(m.x) }
            }
        }
    }

    // ── One effect parameter ────────────────────────────────────────────────
    //
    // The same slider as everywhere else, over a row that came out of a text
    // file the engine parsed. The range is the recipe author's; the value
    // written back is a NUMBER, which is what stops a project file smuggling a
    // filter argument into somebody else's chain.
    component FxCtl: Item {
        id: fxc
        required property var modelData
        property int fxIndex: 0
        property var values: ({})
        readonly property var row: fxc.modelData
        readonly property real val: {
            const v = fxc.values[fxc.row.key]
            return v === undefined ? fxc.row.def : v
        }
        width: inspCol.width
        height: 34

        Text {
            id: fxlbl
            anchors.left: parent.left; anchors.leftMargin: 26
            anchors.top: parent.top; anchors.topMargin: 2
            text: fxc.row.label
            color: root.cText
            font.pixelSize: root.ui(10)
            font.family: root.uiFont
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 2
            text: Math.round(fxc.val * 1000) / 1000
            color: root.cAccent
            font.pixelSize: root.ui(10)
            font.family: root.uiFont
        }
        Rectangle {
            id: fxtrack
            anchors.left: parent.left; anchors.leftMargin: 26
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: fxlbl.bottom; anchors.topMargin: 6
            height: 4
            radius: 2
            color: root.isLight ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(1, 1, 1, 0.14)

            readonly property real frac:
                Math.max(0, Math.min(1, (fxc.val - fxc.row.lo)
                                        / (fxc.row.hi - fxc.row.lo)))
            Rectangle {
                height: parent.height; radius: 2
                color: root.cAccent
                width: fxtrack.frac * fxtrack.width
            }
            Rectangle {
                width: 11; height: 11; radius: 6
                color: root.cAccent
                y: -4
                x: fxtrack.frac * fxtrack.width - 5
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                preventStealing: true
                // On RELEASE. A `set` per tick would spawn a process per pixel
                // of travel and reload the document under the hand that is
                // dragging, which destroys the drag — the same rule the
                // mixer's faders follow.
                property real pending: fxc.val
                function pick(mx) {
                    const f = Math.max(0, Math.min(1, (mx - 10) / fxtrack.width))
                    pending = Math.round((fxc.row.lo
                              + f * (fxc.row.hi - fxc.row.lo)) * 1000) / 1000
                }
                onPressed: function (m) { pick(m.x) }
                onPositionChanged: function (m) { if (pressed) pick(m.x) }
                onReleased: root.fxRun(["set", String(fxc.fxIndex),
                                        fxc.row.key + "=" + pending])
            }
        }
    }

    // ── One grade control ───────────────────────────────────────────────────
    //
    // The develop table's own row, applied to a CLIP. Identical maths to the
    // darkroom slider above it — this one writes through `timeline grade`,
    // which bakes a .cube the export hands to lut3d.
    // (The id is `grd`, not `gc`: `gc` is the QML engine's global
    // garbage-collect function, and an id that masks a global JavaScript
    // property makes the WHOLE file fail to load — not that component, the
    // file, with the message pointing at the id and not at the collision.)
    component GradeCtl: Item {
        id: grd
        required property var modelData
        readonly property var row: grd.modelData
        readonly property real val: root.gradeValue(grd.row.key)
        width: inspCol.width
        height: grd.row.type === "curve" ? 0 : grd.row.type === "str" ? 46 : 40
        visible: grd.row.type !== "curve"

        // The clip's LUT row. The same control as the darkroom's, against the
        // clip's grade instead of the sidecar — and it has to exist here too,
        // because the grade on a clip is the SAME develop table and a table
        // row the inspector could not draw would be a setting reachable only
        // from the command line.
        Loader {
            active: grd.row.type === "str"
            anchors.fill: parent
            sourceComponent: Item {
                readonly property string cur: root.gradeRaw(grd.row.key)
                readonly property string shown:
                    cur === "" ? "None" : cur.replace(/^.*\//, "").replace(/\.cube$/i, "")

                Text {
                    id: gcLutLbl
                    anchors.left: parent.left; anchors.leftMargin: 20
                    anchors.top: parent.top; anchors.topMargin: 4
                    text: grd.row.label
                    color: root.cText
                    font.pixelSize: root.ui(11)
                    font.family: root.uiFont
                }
                Rectangle {
                    anchors.left: parent.left; anchors.leftMargin: 20
                    anchors.right: gcClear.left; anchors.rightMargin: 6
                    anchors.top: gcLutLbl.bottom; anchors.topMargin: 4
                    height: 22
                    radius: 3
                    color: root.wash(0.14)
                    border.width: 1
                    border.color: root.wash(0.22)
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left; anchors.leftMargin: 8
                        anchors.right: parent.right; anchors.rightMargin: 8
                        text: parent.parent.shown
                        elide: Text.ElideMiddle
                        color: parent.parent.cur === "" ? root.cDim : root.cAccent
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.lutTarget = "clip"
                            root.lutMenuOpen = !root.lutMenuOpen
                        }
                    }
                }
                Rectangle {
                    id: gcClear
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.top: gcLutLbl.bottom; anchors.topMargin: 4
                    width: 22; height: 22
                    radius: 3
                    visible: parent.cur !== ""
                    color: root.wash(0.14)
                    border.width: 1
                    border.color: root.wash(0.22)
                    Text {
                        anchors.centerIn: parent
                        text: "\u00d7"
                        color: root.cText
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.gradeClip(grd.row.key, "")
                    }
                }
            }
        }

        Text {
            id: gclbl
            visible: grd.row.type !== "str"
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.top: parent.top; anchors.topMargin: 4
            text: grd.row.label
            color: root.cText
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }
        Text {
            visible: grd.row.type !== "str"
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 4
            text: grd.val === 0 ? "" : (Math.round(grd.val * 100) / 100)
            color: grd.val === 0 ? root.cDim : root.cAccent
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }

        Rectangle {
            id: gctrack
            visible: grd.row.type !== "str"
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: gclbl.bottom; anchors.topMargin: 7
            height: 4
            radius: 2
            color: root.isLight ? Qt.rgba(0, 0, 0, 0.18) : Qt.rgba(1, 1, 1, 0.14)

            readonly property real zeroFrac:
                grd.row.lo < 0 ? (0 - grd.row.lo) / (grd.row.hi - grd.row.lo) : 0
            readonly property real valFrac:
                Math.max(0, Math.min(1, (grd.val - grd.row.lo) / (grd.row.hi - grd.row.lo)))

            Rectangle {
                height: parent.height
                radius: 2
                color: root.cAccent
                x: Math.min(gctrack.zeroFrac, gctrack.valFrac) * gctrack.width
                width: Math.abs(gctrack.valFrac - gctrack.zeroFrac) * gctrack.width
            }
            Rectangle {
                width: 11; height: 11; radius: 6
                color: root.cAccent
                y: -4
                x: gctrack.valFrac * gctrack.width - 5
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                // The inspector is a Flickable too. See the timeline ruler.
                preventStealing: true
                function commit(mx) {
                    const f = Math.max(0, Math.min(1, (mx - 10) / gctrack.width))
                    let v = grd.row.lo + f * (grd.row.hi - grd.row.lo)
                    v = grd.row.type === "int" ? Math.round(v)
                                              : Math.round(v * 100) / 100
                    root.gradeClip(grd.row.key, v)
                }
                onPressed: function (m) { commit(m.x) }
                onPositionChanged: function (m) { if (pressed) commit(m.x) }
                onDoubleClicked: root.gradeClip(grd.row.key, 0)
            }
        }
    }

    component Btn: Rectangle {
        id: btn
        property string label: ""
        // NOT `enabled`: that name already belongs to QQuickItem and shadowing
        // it makes the base property and this one fight over the same reads.
        property bool active: true
        // For a button that TOGGLES something rather than doing it once. A
        // panel you opened should be able to say so from its own button.
        property bool on: false
        signal clicked()
        width: t.implicitWidth + 22
        height: 26
        radius: 4
        color: !btn.active ? "transparent"
               : btn.on ? root.wash(0.34)
               : ma.containsMouse ? root.wash(0.26) : root.wash(0.12)
        border.width: 1
        border.color: btn.on ? root.cAccent
                      : btn.active ? root.wash(0.35) : root.wash(0.12)

        Text {
            id: t
            anchors.centerIn: parent
            text: btn.label
            color: btn.active ? root.cText : root.cDim
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            enabled: btn.active
            onClicked: btn.clicked()
        }
    }
}
