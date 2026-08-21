// synstudio — the SynapseOS photo and video editor.
//
// A renderer, and nothing more. This file owns NO pixels and NO develop
// setting. Every value on screen came out of `synstudio get`, every change
// goes back through `synstudio set`, and the picture in the middle is a PNG
// the engine just wrote. The same is true of syn-edit and synfiles, and it is
// what makes the whole editor testable from tests/run.sh with no display.
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

    title: (root.file ? root.file.replace(/^.*\//, "") : "no photograph")
           + (root.dirty ? " •" : "") + " — SYNAPSE Studio"
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
    property string status: "open a photograph"
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
    // The fallback to $HOME must fire at most once. Retrying on every failure
    // is an infinite respawn loop the moment $HOME itself is unreadable.
    property bool   pickerFellBack: false

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

    Component.onCompleted: {
        keysProc.running = true
        // Launched with no photograph, the window used to come up empty with
        // every control dead and no indication that Open was the way out. The
        // picker IS the empty state.
        if (!root.file) root.openPicker()
    }

    // ── Layout ──────────────────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: root.cBg

        Column {
            anchors.fill: parent

            // Top strip
            Rectangle {
                width: parent.width
                height: 46
                color: root.cPanel

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    spacing: 8

                    Btn { label: "Open";  onClicked: root.openPicker() }
                    Btn { label: "Export"; active: root.file !== ""; onClicked: {
                        exportProc.command = [root.bin, "render", root.file,
                                              "--out", root.file.replace(/\.[^.\/]*$/, "") + "-edited.jpg",
                                              "--quality", "95"]
                        exportProc.running = true
                        root.say("exporting…")
                    } }
                    Btn { label: "Reset"; active: root.file !== ""; onClicked: {
                        setProc.command = [root.bin, "reset", root.file]
                        setProc.running = true
                        root.dirty = false
                        Qt.callLater(function () { root.loadFile(root.file) })
                    } }
                }

                Text {
                    anchors.centerIn: parent
                    text: root.file ? root.file.replace(/^.*\//, "") : "synstudio"
                    color: root.cText
                    font.pixelSize: 14
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    text: root.imgW > 0 ? root.imgW + " × " + root.imgH : ""
                    color: root.cDim
                    font.pixelSize: 12
                }
            }

            Row {
                width: parent.width
                height: parent.height - 46 - 24

                // ── The picture ─────────────────────────────────────────────
                Rectangle {
                    width: parent.width - 340
                    height: parent.height
                    color: root.cViewport

                    Image {
                        id: preview
                        anchors.fill: parent
                        anchors.margins: 18
                        source: root.previewUrl
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        cache: false
                        asynchronous: true
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
                    width: 340
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

            // Status
            Rectangle {
                width: parent.width
                height: 24
                color: root.cPanel
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    text: root.status
                    color: root.cDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width - 24
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
                    model: root.pickerRows
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
                            text: rowItem.modelData.kind === "up"    ? "↑"
                                : rowItem.modelData.kind === "dir"   ? "▸"
                                : rowItem.modelData.kind === "video" ? "▶"
                                :                                      "▣"
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
