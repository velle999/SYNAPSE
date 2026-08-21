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

    function valueOf(key) {
        for (let i = 0; i < root.rows.length; i++)
            if (root.rows[i].key === key) return parseFloat(root.rows[i].value) || 0
        return 0
    }

    // Reassigning one element of a `var` array does NOT re-evaluate bindings on
    // it — a documented quickshell trap in this repo. Rebuild the array.
    function setValue(key, v) {
        const next = root.rows.slice()
        for (let i = 0; i < next.length; i++)
            if (next[i].key === key)
                next[i] = { key: next[i].key, value: String(v), lo: next[i].lo, hi: next[i].hi,
                            type: next[i].type, group: next[i].group, label: next[i].label,
                            hardLo: next[i].hardLo, hardHi: next[i].hardHi }
        root.rows = next
    }

    Process {
        id: keysProc
        command: [root.bin, "keys"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.rows = root.parseKeys(this.text)
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
                const next = root.rows.slice()
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 2) continue
                    for (let j = 0; j < next.length; j++)
                        if (next[j].key === f[0])
                            next[j] = { key: next[j].key, value: f[1], lo: next[j].lo,
                                        hi: next[j].hi, type: next[j].type, group: next[j].group,
                                        label: next[j].label, hardLo: next[j].hardLo,
                                        hardHi: next[j].hardHi }
                }
                root.rows = next
                root.requestRender()
            }
        }
    }

    Process {
        id: setProc
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(this.text.split("\n")[0])
        }
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
        setProc.command = [root.bin, "set", root.file, key + "=" + v]
        setProc.running = true
        root.requestRender()
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
            // The darkroom develops pictures. A sound file has none, so Open
            // must not offer one — it would load nothing and say nothing.
            else if (root.pickerFor === "photo") {
                if (r.kind === "image" || r.kind === "video") out.push(r)
            }
            else if (r.kind !== "project") out.push(r)
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
            root.say(exitCode === 0 ? "exported" : "export failed")
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
        const doc = { w: 1920, h: 1080, fps: 25, tracks: [] }
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
                       hidden: f[4] === "1", clips: [] }
                doc.tracks.push(tr)
                cl = null
                break
            case "clip":
                if (!tr) break
                cl = { tlIn: parseFloat(f[1]), srcIn: parseFloat(f[2]),
                       srcOut: parseFloat(f[3]), speed: parseFloat(f[4]) || 1,
                       gain: parseFloat(f[5]), opacity: parseFloat(f[6]),
                       fadeIn: parseFloat(f[7]), fadeOut: parseFloat(f[8]),
                       path: f[9] || "", kind: "media", still: false,
                       text: "", trans: "none", graded: false, grade: ({}), keys: [] }
                cl.len = (cl.srcOut - cl.srcIn) / (cl.speed > 0 ? cl.speed : 1)
                tr.clips.push(cl)
                if (cl.tlIn + cl.len > dur) dur = cl.tlIn + cl.len
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
                        choices: (f[7] || "") ? f[7].split("|") : [] }
            out.push(r)
            if (!byGroup[r.group]) { byGroup[r.group] = true; seen.push(r.group) }
        }
        root.clipGroups = seen
        return out
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

    function loadClip() {
        if (root.selTrack < 0 || root.selClip < 0) { root.clipVals = ({}); return }
        clipGetProc.command = [root.bin, "timeline", "get", root.proj,
                               String(root.selTrack), String(root.selClip)]
        clipGetProc.running = true
    }

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
        stderr: StdioCollector { onStreamFinished: if (this.text) root.say(this.text.split("\n")[0]) }
        onExited: function (code, status) {
            root.reloadTimeline()
            root.loadClip()
        }
    }

    function tlRun(args) {
        if (tlSetProc.running) return false
        // The rendered preview is now a picture of a timeline that no longer
        // exists. Bumping here rather than in the exit handler means a play
        // pressed DURING an edit still re-renders.
        root.tlRev++
        tlSetProc.command = [root.bin, "timeline"].concat(args)
        tlSetProc.running = true
        return true
    }

    function setClip(key, v) {
        if (root.selTrack < 0 || root.selClip < 0) return
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

    // ── Starting a project ──────────────────────────────────────────────────
    //
    // A project file has to EXIST before any other verb works, so New both
    // creates it and lays down the tracks every cut needs. Coming up with an
    // empty track list and no way to add one was the first version's dead end.
    function newProject(path) {
        root.proj = path
        newProjProc.command = [root.bin, "timeline", "new", path,
                               "--size", "1920x1080", "--fps", "25"]
        newProjProc.running = true
    }

    Process {
        id: newProjProc
        onExited: function (code, status) {
            if (code !== 0) { root.say("cannot start a project there"); return }
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
            root.say("new project")
            root.reloadTimeline()
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
        if (pl.source !== url) {
            root.pendingSeek = root.playhead
            root.seekArmed = false
            pl.source = url
        } else {
            pl.seek(root.playhead * 1000)
        }
        root.playing = true
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

    // The player itself lives in synstudio-playback.qml, behind a Loader,
    // because it is the only thing here that needs QtMultimedia and a missing
    // import fails the whole FILE rather than the feature. See that file.
    readonly property bool playbackReady: playbackLoader.status === Loader.Ready
                                          && playbackLoader.item !== null

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
    readonly property var gradeGroups: ["Basic", "Colour mixer", "Grading"]

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
    function snap(t, ignoreTrack, ignoreClip) {
        const tol = 8 / root.pxPerSec
        let best = t, bestD = tol
        function tryEdge(e) {
            const d = Math.abs(e - t)
            if (d < bestD) { bestD = d; best = e }
        }
        tryEdge(0)
        tryEdge(root.playhead)
        for (let i = 0; i < root.tl.tracks.length; i++)
            for (let j = 0; j < root.tl.tracks[i].clips.length; j++) {
                if (i === ignoreTrack && j === ignoreClip) continue
                const c = root.tl.tracks[i].clips[j]
                tryEdge(c.tlIn)
                tryEdge(c.tlIn + c.len)
            }
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
            root.tlRun(["trim", root.proj, String(track), String(cl),
                        "--head", String(Math.round(dt * 1000) / 1000)])
        } else {
            root.tlRun(["trim", root.proj, String(track), String(cl),
                        "--tail", String(Math.round(dt * 1000) / 1000)])
        }
    }

    Process {
        id: tlExportProc
        stderr: StdioCollector { onStreamFinished: if (this.text) root.say(this.text.split("\n")[0]) }
        onExited: function (code, status) {
            root.say(code === 0 ? "exported " + root.proj.replace(/\.[^.\/]*$/, "") + ".mp4"
                                : "export failed")
        }
    }

    Component.onCompleted: {
        keysProc.running = true
        clipKeysProc.running = true
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
                    Tab { label: "Video"; on: root.mode === "video"
                          onClicked: { root.mode = "video"
                                       root.say(root.proj ? "" : "New project, then Add media") } }

                    Item { width: 10; height: 1 }

                    Btn { visible: root.mode === "photo"
                          label: "Open";  onClicked: root.openPicker() }
                    Btn { visible: root.mode === "photo"
                          label: "Export"; active: root.file !== ""; onClicked: {
                        exportProc.command = [root.bin, "render", root.file,
                                              "--out", root.file.replace(/\.[^.\/]*$/, "") + "-edited.jpg",
                                              "--quality", "95"]
                        exportProc.running = true
                        root.say("exporting…")
                    } }
                    Btn { visible: root.mode === "photo"
                          label: "Reset"; active: root.file !== ""; onClicked: {
                        setProc.command = [root.bin, "reset", root.file]
                        setProc.running = true
                        root.dirty = false
                        Qt.callLater(function () { root.loadFile(root.file) })
                    } }

                    Btn { visible: root.mode === "video"; label: "New project"
                          onClicked: root.newProject(
                              (Quickshell.env("HOME") || "/tmp") + "/synstudio-project.syntl") }
                    Btn { visible: root.mode === "video"; label: "Add media"
                          active: root.proj !== "" && root.selTrack >= 0
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
                    Btn { visible: root.mode === "video"; label: "Delete"
                          active: root.selClip >= 0
                          onClicked: {
                              root.tlRun(["delete", root.proj, String(root.selTrack),
                                          String(root.selClip)])
                              root.selClip = -1
                          } }
                    Btn { visible: root.mode === "video"; label: "Ripple delete"
                          active: root.selClip >= 0
                          onClicked: {
                              root.tlRun(["delete", root.proj, String(root.selTrack),
                                          String(root.selClip), "--ripple"])
                              root.selClip = -1
                          } }
                    Btn { visible: root.mode === "video"; label: "Export"
                          active: root.proj !== "" && root.tlDur > 0
                          onClicked: {
                              tlExportProc.command =
                                  [root.bin, "timeline", "export", root.proj, "--out",
                                   root.proj.replace(/\.[^.\/]*$/, "") + ".mp4"]
                              tlExportProc.running = true
                              root.say("exporting the cut…")
                          } }
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
                        visible: root.previewUrl !== ""
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.file === ""
                        text: "Open a photograph"
                        color: "#9a9a9a"
                        font.pixelSize: 18
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
                                color: "#4d9bff"; font.pixelSize: 10
                            }
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.margins: 6
                                visible: root.clipHi > 0
                                text: root.clipHi + " ▲"
                                color: "#ff6b6b"; font.pixelSize: 10
                            }
                        }

                        // Every group, every control, from the engine's table.
                        Flickable {
                            width: parent.width
                            height: parent.height - 110
                            contentHeight: panelCol.height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            Column {
                                id: panelCol
                                width: parent.width

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
                                                font.pixelSize: 12
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

                        Text {
                            anchors.centerIn: parent
                            visible: root.proj === ""
                            text: "New project, then Add media"
                            color: "#9a9a9a"
                            font.pixelSize: 18
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
                                font.pixelSize: 12
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
                            font.pixelSize: 12
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

                            // Track headers, pinned. They do not scroll with
                            // the clips: losing track of which lane is which
                            // is the fastest way to drop a clip on the wrong
                            // one, and the lane names are what prevent it.
                            Column {
                                width: 92
                                height: parent.height

                                Rectangle {
                                    width: parent.width; height: 22
                                    color: "transparent"
                                }

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
                                            font.pixelSize: 11
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
                                                font.pixelSize: 9
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

                            Flickable {
                                width: parent.width - 92
                                height: parent.height
                                contentWidth: Math.max(width,
                                    (root.tlDur + 10) * root.pxPerSec)
                                contentHeight: height
                                clip: true
                                flickableDirection: Flickable.HorizontalFlick
                                boundsBehavior: Flickable.StopAtBounds

                                Item {
                                    id: lanes
                                    width: Math.max(parent.width,
                                        (root.tlDur + 10) * root.pxPerSec)
                                    height: parent.height

                                    // Ruler. Clicking it is how the playhead
                                    // moves, which is the gesture every editor
                                    // has and the only one people try first.
                                    Rectangle {
                                        id: ruler
                                        width: parent.width
                                        height: 22
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
                                                    font.pixelSize: 9
                                                    font.family: "monospace"
                                                }
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
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
                                                        border.color: clipRect.isSel
                                                                      ? root.cAccent : root.wash(0.4)

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
                                                            font.pixelSize: 10
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
                                                                font.pixelSize: 9
                                                                color: (clipRect.isSel
                                                                        && root.selKey === index)
                                                                       ? root.cAccent : root.cDim
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
                                                            font.pixelSize: 11
                                                        }
                                                        Text {
                                                            anchors.left: parent.left
                                                            anchors.leftMargin: 6
                                                            anchors.bottom: parent.bottom
                                                            anchors.bottomMargin: 4
                                                            visible: clipRect.modelData.trans !== "none"
                                                            text: "⇥ " + clipRect.modelData.trans
                                                            color: root.cAccent
                                                            font.pixelSize: 9
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

                                    // The playhead, over everything.
                                    Rectangle {
                                        x: root.playhead * root.pxPerSec
                                        y: 0
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

                    Text {
                        anchors.centerIn: parent
                        visible: root.selClip < 0
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.proj === "" ? "No project yet"
                              : "Pick a clip to grade it"
                        color: root.cDim
                        font.pixelSize: 13
                    }

                    Flickable {
                        anchors.fill: parent
                        visible: root.selClip >= 0
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
                                    font.pixelSize: 12
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
                                    font.pixelSize: 10
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
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: cgrp.open = !cgrp.open
                                        }
                                    }

                                    Repeater {
                                        model: cgrp.open ? root.clipRowsIn(cgrp.modelData) : []
                                        ClipCtl {}
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
                                    font.pixelSize: 12
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
                                    font.pixelSize: 10
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
                                            font.pixelSize: 11
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
                    color: root.cDim
                    font.pixelSize: 11
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: statusSize.left
                    anchors.rightMargin: 12
                    text: root.status
                    color: root.cDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
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
                font.pixelSize: 26
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "one colour engine, two ways in"
                color: root.cDim
                font.pixelSize: 12
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
                    root.newProject((Quickshell.env("HOME") || "/tmp")
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
                        font.pixelSize: 12
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
                            font.pixelSize: 12
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
                            font.pixelSize: 12
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
                                } else if (root.pickerFor === "clip") {
                                    // The video page borrows the same picker.
                                    // It lists what the ENGINE can decode, so
                                    // a row that is drawn is a row that will
                                    // land on the timeline.
                                    root.pickerOpen = false
                                    root.pickerFor = "photo"
                                    root.tlRun(["clip", root.proj, String(root.selTrack),
                                                m.path, "--at", String(root.playhead)])
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
        height: row.type === "curve" ? 0 : 44
        visible: row.type !== "curve"

        readonly property real val: parseFloat(row.value) || 0

        Text {
            id: lbl
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            text: sl.row.label
            color: root.cText
            font.pixelSize: 11
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            // A control at its default reads as blank rather than as "0", so
            // the eye finds the handful that have been touched.
            text: sl.val === 0 ? "" : (Math.round(sl.val * 100) / 100)
            color: sl.val === 0 ? root.cDim : root.cAccent
            font.pixelSize: 11
        }

        Rectangle {
            id: track
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

                function commit(mx, live) {
                    const f = Math.max(0, Math.min(1, (mx + 10) / track.width))
                    let v = sl.row.lo + f * (sl.row.hi - sl.row.lo)
                    v = sl.row.type === "int" ? Math.round(v)
                                              : Math.round(v * 100) / 100
                    root.change(sl.row.key, v)
                }
                onPressed: function (m) { root.dragging = true; commit(m.x, true) }
                onPositionChanged: function (m) { if (pressed) commit(m.x, true) }
                onReleased: function (m) {
                    root.dragging = false
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
        width: 340
        height: 56
        radius: 5
        color: doorMa.containsMouse ? root.wash(0.24) : root.wash(0.10)
        border.width: 1
        border.color: root.wash(0.30)

        Text {
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.top: parent.top; anchors.topMargin: 10
            text: door.title
            color: root.cText
            font.pixelSize: 14
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.bottom: parent.bottom; anchors.bottomMargin: 10
            text: door.sub
            color: root.cDim
            font.pixelSize: 11
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
            font.pixelSize: 12
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
            font.pixelSize: 9
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
        width: inspCol.width
        height: cc.row.type === "text" ? 52 : 44

        Text {
            id: cclbl
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            text: cc.row.label
            color: root.cText
            font.pixelSize: 11
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 6
            visible: cc.row.type !== "text"
            text: cc.row.type === "enum" ? cc.raw
                                         : (Math.round(cc.val * 100) / 100)
            color: root.cAccent
            font.pixelSize: 11
        }

        // enum — click to advance. With at most six choices a cycler beats a
        // popup: it needs no overlay, no focus grab and no dismissal rule.
        Rectangle {
            visible: cc.row.type === "enum"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: cclbl.bottom; anchors.topMargin: 6
            height: 20
            radius: 3
            color: root.wash(0.14)
            Text {
                anchors.centerIn: parent
                text: "◂  " + cc.raw + "  ▸"
                color: root.cText
                font.pixelSize: 10
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

        // text — committed on Enter or on losing focus, never per keystroke.
        // A `set` per character would spawn a process per letter typed.
        Rectangle {
            visible: cc.row.type === "text"
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: cclbl.bottom; anchors.topMargin: 6
            height: 24
            radius: 3
            color: root.wash(0.14)
            border.width: 1
            border.color: cti.activeFocus ? root.cAccent : root.wash(0.2)

            TextInput {
                id: cti
                anchors.fill: parent
                anchors.leftMargin: 7
                anchors.rightMargin: 7
                verticalAlignment: TextInput.AlignVCenter
                color: root.cText
                font.pixelSize: 11
                clip: true
                text: cc.raw
                onEditingFinished: if (text !== cc.raw) root.setClip(cc.row.key, text)
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
                function commit(mx) {
                    const f = Math.max(0, Math.min(1, (mx + 10) / cctrack.width))
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
        height: grd.row.type === "curve" ? 0 : 40
        visible: grd.row.type !== "curve"

        Text {
            id: gclbl
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.top: parent.top; anchors.topMargin: 4
            text: grd.row.label
            color: root.cText
            font.pixelSize: 11
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.topMargin: 4
            text: grd.val === 0 ? "" : (Math.round(grd.val * 100) / 100)
            color: grd.val === 0 ? root.cDim : root.cAccent
            font.pixelSize: 11
        }

        Rectangle {
            id: gctrack
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
                function commit(mx) {
                    const f = Math.max(0, Math.min(1, (mx + 10) / gctrack.width))
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
        signal clicked()
        width: t.implicitWidth + 22
        height: 26
        radius: 4
        color: !btn.active ? "transparent"
               : ma.containsMouse ? root.wash(0.26) : root.wash(0.12)
        border.width: 1
        border.color: btn.active ? root.wash(0.35) : root.wash(0.12)

        Text {
            id: t
            anchors.centerIn: parent
            text: btn.label
            color: btn.active ? root.cText : root.cDim
            font.pixelSize: 12
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
