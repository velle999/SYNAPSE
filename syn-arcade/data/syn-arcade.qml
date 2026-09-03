// syn-arcade — the SynapseOS game assistant.
//
// A renderer, and nothing more. Every fact on screen arrives as a record from
// `syn-arcade --rec`; this file knows how to draw a row and a button, and knows
// nothing about MangoHud, evdev or SDL. Every refusal — a deadzone over 50%, a
// mapping for the wrong platform, a config file that is not writable — is
// enforced in the binary, because a check that lives only in a GUI is one that
// anything else calling the same binary skips for free.
//
// ── The one rule for reading records ───────────────────────────────────────
//
// EVERY field arrives percent-encoded, including the ones that look like plain
// words. A controller name is arbitrary bytes off a USB descriptor, so decoding
// "the ones that need it" means keeping a list that will drift — and the day it
// drifts a tab inside a device name shifts every column of a row.
//
// So: decode every field, once, at the parse. See disp().
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Lets the delegates below refer to `root` — an id from an outer component —
// without every reference being an unqualified lookup resolved at run time.
// Without it qmllint flags each one, and the failure it is warning about is
// real: an unqualified name that stops resolving binds to undefined silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import QtQuick.Controls

// ⛔ THE TRANSLATION SINGLETON, AND IT IS A DIRECTORY IMPORT. quickshell has no
// translator at all — qsTr() compiles, returns its own argument, and translates
// nothing while looking exactly like a marked string in review — so qml/I18n.qml
// reads a JSON catalog compiled from the same po/*.po the binary's .mo comes
// from. Both of this program's windows import it, so a word they share is
// translated once.
import "qml"

FloatingWindow {
    id: root

    title: I18n.tr("SYNAPSE Arcade")
    implicitWidth: 820
    implicitHeight: 600
    // The controller rows carry a name, a bus and two buttons. Below this the
    // rows cannot hold their shape, and a layout with no floor does not
    // degrade — it paints over itself.
    minimumSize: Qt.size(640, 440)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNARCADE_BIN") || "syn-arcade"

    // ── palette ─────────────────────────────────────────────────────────────
    readonly property color bg:      "#141021"
    readonly property color panel:   "#1e1830"
    readonly property color panelHi: "#2a2142"
    readonly property color ink:     "#e8e3f5"
    readonly property color dim:     "#c0b9d8"
    readonly property color accent:  "#a78bfa"
    readonly property color good:    "#4ec9b0"
    readonly property color bad:     "#f2777a"

    // ── The UI font ─────────────────────────────────────────────────────────
    //
    // The same ~/.config/synui/font.state every other window in the suite
    // watches. This one read it nowhere: no Text named a family and all
    // fifty-nine pixelSizes were literals, so the window kept whatever face
    // and size Qt resolved at startup and a desktop font change reached every
    // app except this one.
    //
    // ⚠ BOTH HALVES HAVE TO BE BINDINGS. Qt resolves an application's default
    // font ONCE at startup and QML cannot change it afterwards, so naming the
    // family on every Text is the only way the face can change while the
    // window is open — and the size has to go through ui() for the same
    // reason. Doing one and not the other gives a window that follows the
    // desktop until somebody changes it.
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

    /*
     * ⛔ A VIEW THAT SCROLLS SHOWS THAT IT SCROLLS. Without a bar there is
     * nothing on screen saying there is anything past the edge of the view,
     * nothing saying how much, and no way to cross a long list in one gesture.
     * velle, 2026-08-28: "you keep making windows without scrollbars and thats
     * dumb."
     *
     * ⚠ VISIBLE AT REST, which is why this exists rather than a bare ScrollBar:
     * Qt's default fades the handle out unless `active` — true while the view
     * moves or the bar is hovered, and false in exactly the state where
     * somebody is deciding whether there is more to see.
     *
     * ⚠ AsNeeded, so a view shorter than its window draws no furniture.
     *
     * ⚠ ORIENTATION-AWARE: attached as `ScrollBar.horizontal` it has to be
     * short and wide, not a vertical handle lying on its side.
     *
     * ⚠ INLINE, because this app carries its own palette — as every window here
     * does — and a scrollbar belongs with the colours it is drawn against.
     * There is no QML module shared across these packages to put it in.
     * Pinned by preflight's `scrollbar` gate.
     */
    component SynScrollBar: ScrollBar {
        id: sb
        readonly property bool vert: sb.orientation === Qt.Vertical

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
        padding: root.ui(2)
        implicitWidth:  sb.vert ? root.ui(11) : root.ui(48)
        implicitHeight: sb.vert ? root.ui(48) : root.ui(11)

        contentItem: Rectangle {
            implicitWidth:  sb.vert ? root.ui(7) : root.ui(32)
            implicitHeight: sb.vert ? root.ui(32) : root.ui(7)
            radius: Math.min(width, height) / 2
            color: sb.pressed ? root.accent : sb.hovered ? root.ink : root.dim
            opacity: sb.pressed || sb.hovered ? 1.0 : 0.5
            Behavior on color   { ColorAnimation  { duration: 90 } }
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }

        background: Rectangle {
            radius: Math.min(width, height) / 2
            color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.08)
            opacity: sb.hovered || sb.pressed ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }


    property int tab: 0
    property string status: ""

    property var hudFields: ({})
    property var pads: []
    property var maps: []
    property var bindFields: ({})

    // decodeURIComponent THROWS on a percent sequence that is not valid UTF-8,
    // and a controller name off a cheap USB descriptor is not always valid
    // UTF-8. Showing the raw encoded form is ugly; letting the exception escape
    // empties the whole window, which is how one odd pad makes every controller
    // disappear.
    function disp(s) {
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    // The first record names the columns, so a column added in C shows up here
    // with no change to this file and the two cannot quietly disagree about
    // what field three is.
    function parseRecords(text) {
        const lines = text.split("\n").filter(l => l !== "")
        if (lines.length === 0) return []
        const cols = lines[0].split("\t").map(root.disp)
        const rows = []
        for (let i = 1; i < lines.length; i++) {
            const f = lines[i].split("\t")
            const o = {}
            for (let j = 0; j < cols.length; j++) o[cols[j]] = root.disp(f[j] || "")
            rows.push(o)
        }
        return rows
    }

    function parseFields(text) {
        const out = {}
        for (const r of root.parseRecords(text)) out[r.field] = r.value
        return out
    }

    function oneLine(s) {
        return s.split("\n").filter(l => l !== "")[0] || ""
    }

    // ── readers ─────────────────────────────────────────────────────────────

    Process {
        id: hudProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.hudFields = root.parseFields(this.text)
                // The action column carries the one thing the value cannot:
                // whether the config path is ours to write.
                root.hudConfigAction = "detail"
                for (const r of root.parseRecords(this.text))
                    if (r.field === "config") root.hudConfigAction = r.action || "detail"
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
    }

    Process {
        id: padsProc
        stdout: StdioCollector {
            onStreamFinished: root.pads = root.parseRecords(this.text)
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
    }

    // ⚠ Long-lived, prints nothing, and NOT a reader despite living among them.
    //
    // A wireless pad falls asleep unless something holds its event node OPEN —
    // xpad polls the USB endpoint only while the device is open — so a window
    // whose only contact with a controller is re-running `pads --rec` sits and
    // watches the pad it is describing switch itself off, and gets the blame
    // for it. Steam stays out of trouble by holding every pad for as long as it
    // runs; this does the same for as long as the window is up.
    //
    // Killed by quickshell when this shell exits, and `pads hold` also watches
    // its own stdout for the pipe closing. See pads_hold_stream() in pad.c.
    Process {
        id: holdProc
        command: [root.bin, "pads", "hold"]
        running: true
    }

    Process {
        id: mapsProc
        stdout: StdioCollector {
            onStreamFinished: root.maps = root.parseRecords(this.text)
        }
    }

    // ── the mapping wizard ──────────────────────────────────────────────────
    //
    // ⚠ THE ONE TAB THAT EXISTS FOR A BROKEN CONTROLLER COULD NOT FIX ONE. It
    // listed mappings, removed them, and to ADD one told you to go and find
    // antimicrox — so the window's answer to "my pad's buttons are in the
    // wrong places" was the name of another program. That is not a settings
    // panel, it is a note about where the settings panel would be.
    //
    // `map learn` is a conversation: it prints one record per thing that
    // happens and reads one word per line back. This end of it is a reader and
    // three buttons; the binary decides everything, including which control
    // comes next, so the terminal wizard and this one cannot drift.
    property bool   wizOn: false
    property string wizPad: ""
    property var    wizAsk: ({})
    property var    wizBound: []
    property string wizNote: ""

    Process {
        id: wizProc
        stdinEnabled: true
        stdout: SplitParser {
            onRead: (line) => root.wizLine(line)
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
        onExited: {
            root.wizOn = false
            root.reload()
        }
    }

    // ⚠ COLUMNS BY NAME, from the record's own first line, exactly as
    // parseRecords does for the collected readers. A wizard that hard-coded
    // "field four is the control" would be a second place to update when a
    // column is added in C, and the failure would be a silently mislabelled
    // prompt rather than an error.
    property var wizCols: []

    function wizLine(raw) {
        const line = raw.replace(/\n$/, "")
        // map_add's own report comes down the same stream once the wizard
        // finishes — plain sentences, no tabs. Those belong in the status bar.
        if (line.indexOf("\t") < 0) {
            if (line.trim() !== "") root.status = line.trim()
            return
        }

        const f = line.split("\t").map(root.disp)
        if (root.wizCols.length === 0 || f[0] === "event") {
            root.wizCols = f
            return
        }
        const r = {}
        for (let i = 0; i < root.wizCols.length; i++) r[root.wizCols[i]] = f[i] || ""

        switch (r.event) {
        case "pad":
            root.wizPad = r.detail
            root.wizBound = []
            root.wizNote = ""
            break
        case "ask":
            root.wizAsk = r
            root.wizNote = ""
            break
        case "bound":
            root.wizBound = root.wizBound.concat([
                { control: r.control, binding: r.binding }])
            break
        case "skipped":
            root.wizBound = root.wizBound.concat([
                { control: r.control, binding: "" }])
            break
        // ⚠ Not an error and not a refusal to continue: it is the same
        // question again, with the reason. Two controls on one button is
        // always a mis-press, and recording it would give the pad a B that is
        // also an A — which shows up much later, in a game, as a button that
        // does two things.
        case "taken":
            // ⚠ r.detail is an SDL CONTROL NAME (`leftshoulder`) and is not
            // translated; the sentence around it is.
            root.wizNote = I18n.tr("That is already %1 — try another.")
                                  .arg(r.detail)
            break
        case "cancelled":
        case "error":
            root.status = r.detail
            break
        default:
            break
        }
    }

    function wizStart() {
        root.status = ""
        root.wizCols = []
        root.wizAsk = ({})
        root.wizBound = []
        root.wizNote = ""
        root.wizPad = ""
        root.wizOn = true
        wizProc.command = [root.bin, "map", "learn", "--rec"]
        wizProc.running = true
    }

    function wizSay(word) {
        if (wizProc.running) wizProc.write(word + "\n")
    }

    Process {
        id: bindsProc
        stdout: StdioCollector {
            onStreamFinished: root.bindFields = root.parseFields(this.text)
        }
    }

    // ── fit: the gamescope wrappers ─────────────────────────────────────────

    property var fits: []
    property var fitApps: []
    property var fitScreens: []
    property var fitFilters: []

    // ⚠ THE TEXT BOXES ARE THE MODEL, and are written to by name — fitBlank()
    // and fitFillForm() below assign fitNameField.text and the rest directly.
    //
    // The obvious arrangement is the opposite: a root property per field, with
    // `text: root.fitName` in the box. It does not survive contact with typing.
    // A TextInput edited by hand has its `text` written imperatively by the
    // input method, which DESTROYS the binding that was feeding it — so the
    // next `fitBlank()` sets the property, nothing is listening any more, and
    // the box still shows the previous game's command. Silently, and only after
    // somebody has typed in it, which is why it survives every quick test.
    //
    // Only the fields with no box of their own stay as properties.
    property bool   fitEditing: false
    property bool   fitPicking: false
    property string fitId: ""            // empty while creating
    property string fitIcon: ""
    property string fitCategories: ""
    property string fitFilter: "fsr"
    property bool   fitForceWin: false
    property bool   fitOverlay: false
    property bool   fitGamemode: false
    property bool   fitMenu: true
    property bool   fitDesktop: false

    // ── big screen mode ─────────────────────────────────────────────────────
    //
    // Its own tab since 0.1.0-23. It used to be three rows at the bottom of
    // Shortcuts, which was right while the only things worth saying about it
    // were which key opens it and whether it opens at login — and stopped being
    // right the moment it had a screen to choose and a music player to name.
    //
    // ⚠ Those rows are GONE from Shortcuts rather than repeated here. Two
    // panels writing one setting is the shape of every "I turned it off and it
    // came back" report in this project.
    property var bigFields: ({})
    property var bigScreens: []
    property var bigPlayers: []
    property var bigBackgrounds: []
    property var bigWebapps: []
    property var bigSources: []

    Process {
        id: bigProc
        stdout: StdioCollector {
            onStreamFinished: root.bigFields = root.parseFields(this.text)
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
    }
    Process {
        id: bigOutProc
        stdout: StdioCollector {
            onStreamFinished: root.bigScreens = root.parseRecords(this.text)
        }
    }
    Process {
        id: bigPlayerProc
        stdout: StdioCollector {
            onStreamFinished: root.bigPlayers = root.parseRecords(this.text)
        }
    }
    Process {
        id: bigBackgroundProc
        stdout: StdioCollector {
            onStreamFinished: root.bigBackgrounds = root.parseRecords(this.text)
        }
    }
    Process {
        id: bigWebappProc
        stdout: StdioCollector {
            onStreamFinished: root.bigWebapps = root.parseRecords(this.text)
        }
    }
    Process {
        id: bigSourceProc
        stdout: StdioCollector {
            onStreamFinished: root.bigSources = root.parseRecords(this.text)
        }
    }

    Process {
        id: fitsProc
        stdout: StdioCollector {
            onStreamFinished: root.fits = root.parseRecords(this.text)
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
    }

    Process {
        id: fitScreensProc
        stdout: StdioCollector {
            onStreamFinished: root.fitScreens = root.parseRecords(this.text)
        }
    }

    Process {
        id: fitFiltersProc
        stdout: StdioCollector {
            onStreamFinished: root.fitFilters = root.parseRecords(this.text)
        }
    }

    // ⚠ Read with everything else rather than when the picker opens, and that
    // is not an optimisation — a list fetched on open is EMPTY for as long as
    // the walk takes, and an empty picker is indistinguishable from a picker
    // that found nothing. It is a walk of three directory trees and costs
    // milliseconds. `fitOpenPicker` asks again anyway, so a game installed
    // while this window is up still turns up.
    Process {
        id: fitAppsProc
        stdout: StdioCollector {
            onStreamFinished: root.fitApps = root.parseRecords(this.text)
        }
    }

    // `fit inspect <desktop>` and `fit show <id>` answer in the SAME record
    // shape, which is why one reader fills the form for both "make one from
    // this game" and "change this wrapper".
    Process {
        id: fitFormProc
        stdout: StdioCollector {
            onStreamFinished: root.fitFillForm(this.text)
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
    }

    function fitBlank() {
        root.fitId = ""
        fitNameField.text = ""
        fitExecField.text = ""
        fitDirField.text = ""
        fitEnvArea.text = ""
        fitGameField.text = ""
        fitSharpField.text = ""
        root.fitIcon = ""
        root.fitCategories = "Game;"
        // The screen is a fact about the monitor, so it is filled in rather
        // than asked for. The primary one, which is also the screen synui puts
        // game windows on.
        fitScreenField.text = ""
        for (const s of root.fitScreens)
            if (s.primary === "yes") fitScreenField.text = s.size
        root.fitFilter = "fsr"
        root.fitForceWin = false
        root.fitOverlay = false
        root.fitGamemode = false
        root.fitMenu = true
        root.fitDesktop = false
    }

    function fitOpenNew() {
        root.status = ""
        root.fitBlank()
        root.fitPicking = false
        root.fitEditing = true
    }

    function fitOpenPicker() {
        root.status = ""
        fitAppsProc.command = [root.bin, "fit", "apps", "--rec"]
        fitAppsProc.running = true
        root.fitEditing = false
        root.fitPicking = true
    }

    function fitFromApp(path) {
        root.status = ""
        root.fitBlank()
        fitFormProc.command = [root.bin, "fit", "inspect", path, "--rec"]
        fitFormProc.running = true
    }

    function fitOpenEdit(id) {
        root.status = ""
        root.fitBlank()
        root.fitId = id
        fitFormProc.command = [root.bin, "fit", "show", id, "--rec"]
        fitFormProc.running = true
    }

    // ⚠ Every field arrives as its CONFIG KEY, so this is a rename table of
    // one column and not a second description of what a wrapper is. A key this
    // window does not draw is ignored rather than dropped from the wrapper:
    // the save path sends back only what it was given, and `fit edit` leaves
    // everything it is not told about alone.
    function fitFillForm(text) {
        const rows = root.parseRecords(text)
        const env = []
        for (const r of rows) {
            switch (r.field) {
            case "id":           root.fitId = r.value; break
            case "name":         fitNameField.text = r.value; break
            case "exec":         fitExecField.text = r.value; break
            case "workdir":      fitDirField.text = r.value; break
            case "icon":         root.fitIcon = r.value; break
            case "categories":   root.fitCategories = r.value; break
            case "game":         fitGameField.text = r.value; break
            case "screen":       fitScreenField.text = r.value; break
            case "filter":       root.fitFilter = r.value; break
            case "sharpness":    fitSharpField.text = r.value; break
            case "force_window": root.fitForceWin = r.value === "yes"; break
            case "overlay":      root.fitOverlay = r.value === "yes"; break
            case "gamemode":     root.fitGamemode = r.value === "yes"; break
            case "menu":         root.fitMenu = r.value === "yes"; break
            case "desktop":      root.fitDesktop = r.value === "yes"; break
            case "env":          env.push(r.value); break
            default: break
            }
        }
        fitEnvArea.text = env.join("\n")
        root.fitPicking = false
        root.fitEditing = true
    }

    function fitArgs() {
        const a = ["--name=" + fitNameField.text.trim(),
                   "--exec=" + fitExecField.text.trim(),
                   "--workdir=" + fitDirField.text.trim(),
                   "--icon=" + root.fitIcon.trim(),
                   "--game=" + fitGameField.text.trim(),
                   "--screen=" + fitScreenField.text.trim(),
                   "--filter=" + root.fitFilter.trim(),
                   "--sharpness=" + fitSharpField.text.trim(),
                   "--force-window=" + (root.fitForceWin ? "yes" : "no"),
                   "--overlay=" + (root.fitOverlay ? "yes" : "no"),
                   "--gamemode=" + (root.fitGamemode ? "yes" : "no"),
                   "--menu=" + (root.fitMenu ? "yes" : "no"),
                   "--desktop=" + (root.fitDesktop ? "yes" : "no")]
        if (root.fitCategories.trim() !== "")
            a.push("--categories=" + root.fitCategories.trim())

        // ⚠ An empty --env has to be SENT, not omitted. The first --env of a
        // run replaces the set, so leaving it out on an edit would make the
        // variables the one thing this form could add and never take away.
        const lines = fitEnvArea.text.split("\n")
                          .map(s => s.trim()).filter(s => s !== "")
        if (lines.length === 0) a.push("--env=")
        else for (const l of lines) a.push("--env=" + l)
        return a
    }

    function fitSave() {
        const args = root.fitId ? ["fit", "edit", root.fitId] : ["fit", "new"]
        root.fitEditing = false
        root.run(args.concat(root.fitArgs()))
    }

    // The line the wrapper will run, drawn as it is being built. Assembled here
    // rather than asked of the binary because it changes on every keystroke and
    // a process per keystroke is not a preview — it is a fan. The binary
    // assembles the real one; this one is a reading aid and says nothing the
    // form does not already show.
    function fitPreview() {
        let s = ""
        if (fitDirField.text.trim() !== "") s += "cd <folder> && "
        s += "gamescope"
        if (fitGameField.text.trim() !== "") s += " -w/-h " + fitGameField.text.trim()
        if (fitScreenField.text.trim() !== "") s += " -W/-H " + fitScreenField.text.trim()
        if (root.fitFilter.trim() !== "") s += " -F " + root.fitFilter.trim()
        s += " -- " + fitExecField.text.trim()
        return s
    }

    // Every mutation goes through here, and every one of them re-reads
    // afterwards rather than assuming it worked. The binary is the source of
    // truth for what the state now IS — a GUI that updates its own model on a
    // successful exit code drifts the first time something succeeds partially.
    Process {
        id: actProc
        property string after: ""
        stdout: StdioCollector {
            onStreamFinished: {
                const t = root.oneLine(this.text)
                if (t) root.status = t
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
        onExited: root.reload()
    }

    function run(args) {
        root.status = ""
        actProc.command = [root.bin].concat(args)
        actProc.running = true
    }

    function reload() {
        hudProc.command   = [root.bin, "hud", "--rec"];    hudProc.running = true
        padsProc.command  = [root.bin, "pads", "--rec"];   padsProc.running = true
        mapsProc.command  = [root.bin, "map", "--rec"];    mapsProc.running = true
        bindsProc.command = [root.bin, "binds", "--rec"];  bindsProc.running = true
        fitsProc.command  = [root.bin, "fit", "--rec"];    fitsProc.running = true
        fitScreensProc.command = [root.bin, "fit", "screens", "--rec"]
        fitScreensProc.running = true
        fitFiltersProc.command = [root.bin, "fit", "choices", "filter"]
        fitFiltersProc.running = true
        fitAppsProc.command = [root.bin, "fit", "apps", "--rec"]
        fitAppsProc.running = true
        bigProc.command   = [root.bin, "big", "status", "--rec"]; bigProc.running = true
        bigOutProc.command = [root.bin, "big", "output", "--rec"]
        bigOutProc.running = true
        bigPlayerProc.command = [root.bin, "big", "player", "--rec"]
        bigPlayerProc.running = true
        bigSourceProc.command = [root.bin, "big", "music", "source", "--rec"]
        bigSourceProc.running = true
        bigBackgroundProc.command = [root.bin, "big", "background", "--rec"]
        bigBackgroundProc.running = true
        bigWebappProc.command = [root.bin, "big", "webapps", "--rec"]
        bigWebappProc.running = true
    }

    Component.onCompleted: root.reload()

    // ── chrome ──────────────────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: root.bg

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // tabs
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    // ⚠ THE WORDS THEMSELVES, because nothing matches on them:
                    // the selected tab is an INDEX, and tabChip draws
                    // modelData. Translating the model is therefore safe here
                    // and would not be if a tab were looked up by name.
                    model: [I18n.tr("Overlay"), I18n.tr("Controllers"),
                            I18n.tr("Mappings"), I18n.tr("Fit to screen"),
                            I18n.tr("Big screen"), I18n.tr("Shortcuts")]
                    Rectangle {
                        id: tabChip
                        required property int index
                        required property string modelData
                        Layout.preferredHeight: 32
                        Layout.preferredWidth: label.implicitWidth + 28
                        radius: 6
                        color: root.tab === tabChip.index ? root.accent : root.panel
                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: tabChip.modelData
                            color: root.tab === tabChip.index ? "#1b1030" : root.ink
                            font.pixelSize: root.ui(13)
                            font.family: root.uiFont
                            font.bold: root.tab === tabChip.index
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.tab = tabChip.index; root.status = "" }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                ArcButton {
                    text: I18n.tr("Refresh")
                    onTriggered: { root.status = ""; root.reload() }
                }
            }

            // body
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: root.panel

                // ── Overlay ────────────────────────────────────────────────
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    visible: root.tab === 0

                    Text {
                        text: I18n.tr("MangoHud overlay")
                        color: root.ink
                        font.pixelSize: root.ui(16)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    // ⚠ "SHOW OVERLAY" WAS A PROMISE THIS WINDOW CANNOT KEEP,
                    // and it was reported as a dead button. MangoHud is a
                    // Vulkan and OpenGL layer living inside the game's own
                    // process: it draws over a game and it CANNOT draw on the
                    // desktop. So the switch worked perfectly, wrote the
                    // setting, relabelled itself — and put nothing on screen,
                    // because there was nothing for it to draw on.
                    //
                    // Nothing here is a new mechanism. The words are the fix:
                    // a button that says where it takes effect cannot be
                    // pressed in the expectation of something else.
                    Text {
                        Layout.fillWidth: true
                        text: I18n.tr("Frame rate, temperatures and load, drawn over a game. "
                            + "MangoHud draws INSIDE a game and cannot draw on the "
                            + "desktop, so switching it on here puts nothing on screen "
                            + "until you start one. A game that is already running picks "
                            + "the change up straight away.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.topMargin: 6
                        spacing: 8
                        ArcButton {
                            // ⛔ THE TEST IS ON THE RECORD, NOT ON THE LABEL.
                            // `state` is the raw cell from `hud --rec`; only
                            // what is DRAWN goes through the catalog.
                            text: (root.hudFields.state === "hidden")
                                  ? I18n.tr("Show in games")
                                  : I18n.tr("Hide in games")
                            primary: true
                            onTriggered: root.run(["hud", "toggle"])
                        }
                        ArcButton {
                            text: I18n.tr("Move it")
                            onTriggered: root.run(["hud", "cycle"])
                        }
                    }

                    FieldRow {
                        label: I18n.tr("State")
                        // ⚠ The bare word is what the record carries and it is
                        // right — `visible` is a fact about the config, and
                        // every other consumer of these records wants it that
                        // way. It is only READING IT BACK as though it meant
                        // "on screen now" that misleads, and only here, where
                        // somebody is looking at a desktop with no game on it.
                        value: root.hudFields.state === "visible"
                               ? I18n.tr("on — shows in games")
                             : root.hudFields.state === "hidden"
                               ? I18n.tr("off")
                             // i18n-dynamic: the state word comes from src/hud.c via N_()
                             : (I18n.tr(root.hudFields.state) || "—")
                    }
                    // ⚠ THE POSITION IS AN ID — `top-left` — and src/hud.c puts
                    // the same eight words in the catalog through a SECOND
                    // table, so this draws the translation and `hud position`
                    // still takes the id back.
                    FieldRow { label: I18n.tr("Position")
                               // i18n-dynamic: hud_position_labels[] in src/hud.c
                               value: I18n.tr(root.hudFields.position || "") || "—" }
                    FieldRow { label: I18n.tr("Font size")
                               value: root.hudFields.font_size || "—" }
                    FieldRow { label: I18n.tr("Background")
                               value: root.hudFields.background_alpha || "—" }
                    FieldRow {
                        label: I18n.tr("Config file")
                        value: root.hudFields.config || "—"
                        // The one field where "not writable" is the whole story:
                        // MangoHud reads exactly one config and /etc's outranks
                        // the user's, so a read-only path here means nothing
                        // this window does can reach the overlay at all.
                        warn: (root.hudFields.config || "") !== ""
                              && !root.hudWritable
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.hudWritable && (root.hudFields.config || "") !== ""
                        text: I18n.tr("⚠ That file is not writable by you, so nothing here reaches "
                            + "MangoHud. /etc/MangoHud.conf outranks your own config. "
                            + "\"Take ownership\" copies the settings now in effect into "
                            + "your own file.")
                        color: root.bad
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }
                    ArcButton {
                        visible: !root.hudWritable && (root.hudFields.config || "") !== ""
                        text: I18n.tr("Take ownership")
                        onTriggered: root.run(["hud", "adopt"])
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── Controllers ───────────────────────────────────────────
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    visible: root.tab === 1

                    Text {
                        text: I18n.tr("Controllers")
                        color: root.ink
                        font.pixelSize: root.ui(16)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.pads.length === 0
                        text: I18n.tr("Nothing plugged in. Steam handles its own controller "
                            + "setup — this is for everything outside it.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    ListView {
                        // A view that scrolls says so — see SynScrollBar above.
                        ScrollBar.vertical: SynScrollBar {}
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.pads

                        // ⚠ The delegate carries an id and everything inside it
                        // refers to `padRow.modelData`. Reaching the model
                        // through `parent.parent.parent` works right up until
                        // somebody adds a layout, at which point the chain
                        // resolves to the wrong object and the binding fails
                        // SILENTLY — a row that renders blank rather than an
                        // error anywhere.
                        delegate: Rectangle {
                            id: padRow
                            required property var modelData
                            width: ListView.view.width
                            height: 62
                            radius: 6
                            color: root.panelHi

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: padRow.modelData.name
                                        color: root.ink
                                        font.pixelSize: root.ui(13)
                                        font.family: root.uiFont
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        // ⚠ `kind` is a DEVICE NAME (DualSense)
                                        // and `bus` is what the bus calls
                                        // itself; neither is translated. The
                                        // rumble word is, and the test is on
                                        // the raw record cell.
                                        // i18n-dynamic: friendly_kind() fallback is N_() in src/pad.c
                                        text: I18n.tr(padRow.modelData.kind) + " · "
                                            + padRow.modelData.bus + " · "
                                            + (padRow.modelData.rumble === "yes"
                                               ? I18n.tr("rumble") : I18n.tr("no rumble"))
                                        color: root.dim
                                        font.pixelSize: root.ui(11)
                                        font.family: root.uiFont
                                        elide: Text.ElideRight
                                    }
                                }

                                ArcButton {
                                    text: I18n.tr("Test")
                                    onTriggered: root.run(["pads", "test",
                                        padRow.modelData.id, "--seconds=10"])
                                }
                                ArcButton {
                                    text: I18n.tr("Rumble")
                                    enabled: padRow.modelData.rumble === "yes"
                                    onTriggered: root.run(["pads", "rumble",
                                        padRow.modelData.id])
                                }
                                ArcButton {
                                    text: I18n.tr("Calibrate")
                                    onTriggered: root.run(["pads", "calibrate",
                                        padRow.modelData.id, "--seconds=5"])
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.pads.length > 0
                        text: I18n.tr("Calibrate measures stick drift for five seconds — let go of "
                            + "both sticks first, or it will refuse the reading.")
                        color: root.dim
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }
                }

                // ── Mappings ──────────────────────────────────────────────
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    visible: root.tab === 2

                    Text {
                        text: I18n.tr("Controller mappings")
                        color: root.ink
                        font.pixelSize: root.ui(16)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !root.wizOn
                        text: I18n.tr("For a pad whose buttons come out in the wrong places. "
                            + "Press each control once and this writes the mapping; every "
                            + "SDL game reads it from the next launch.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !root.wizOn
                        spacing: 8

                        ArcButton {
                            text: I18n.tr("Set up with the controller")
                            primary: true
                            onTriggered: root.wizStart()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: I18n.tr("or paste one:")
                            color: root.dim
                            font.pixelSize: root.ui(12)
                            font.family: root.uiFont
                        }
                        // The paste path stays, because somebody who already
                        // HAS a working string should not have to press
                        // twenty-one buttons to use it. It is second, and it
                        // is no longer the only way in.
                        Rectangle {
                            Layout.preferredWidth: 220
                            implicitHeight: 30
                            radius: 6
                            color: root.panelHi
                            TextInput {
                                id: pasteField
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.ink
                                font.pixelSize: root.ui(12)
                                font.family: root.uiFont
                                clip: true
                                selectByMouse: true
                                onAccepted: if (text.trim() !== "") {
                                    root.run(["map", "add", text.trim()])
                                    text = ""
                                }
                            }
                        }
                        ArcButton {
                            text: I18n.tr("Add")
                            onTriggered: if (pasteField.text.trim() !== "") {
                                root.run(["map", "add", pasteField.text.trim()])
                                pasteField.text = ""
                            }
                        }
                    }

                    // ── the wizard, while it is running ───────────────────
                    //
                    // ⚠ EVERY WORD ON THIS PANEL COMES FROM THE BINARY,
                    // including which control is being asked for and what to
                    // call it. A window that kept its own list of controls
                    // would be a second list to fall out of step with the one
                    // deciding the order — and the symptom of that is a
                    // mapping where every binding is one control out, which is
                    // well-formed, loads fine, and plays wrong.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.wizOn
                        radius: 8
                        color: root.panelHi

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: root.wizPad
                                      || I18n.tr("looking for a controller…")
                                color: root.dim
                                font.pixelSize: root.ui(12)
                                font.family: root.uiFont
                                elide: Text.ElideRight
                            }

                            Item { Layout.fillHeight: true }

                            Text {
                                Layout.fillWidth: true
                                text: root.wizAsk.detail
                                      // i18n-dynamic: wiz_controls[] N_() in src/sdlwiz.c
                                      ? I18n.tr("Press %1").arg(I18n.tr(root.wizAsk.detail))
                                      : "…"
                                color: root.ink
                                font.pixelSize: root.ui(26)
                                font.family: root.uiFont
                                font.bold: true
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.wizNote
                                visible: root.wizNote !== ""
                                color: root.bad
                                font.pixelSize: root.ui(13)
                                font.family: root.uiFont
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.wizAsk.total
                                      ? I18n.tr("%1 of %2")
                                            .arg(Number(root.wizAsk.index) + 1)
                                            .arg(root.wizAsk.total)
                                      : ""
                                color: root.dim
                                font.pixelSize: root.ui(13)
                                font.family: root.uiFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            // A thin bar rather than a number alone: on a
                            // twenty-one step walk the useful question is "how
                            // much is left", and that is a length.
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 320
                                Layout.preferredHeight: 4
                                radius: 2
                                color: root.panel
                                Rectangle {
                                    height: parent.height
                                    radius: parent.radius
                                    color: root.accent
                                    width: root.wizAsk.total
                                           ? parent.width * root.wizBound.length
                                             / Number(root.wizAsk.total)
                                           : 0
                                    Behavior on width {
                                        NumberAnimation { duration: 150 }
                                    }
                                }
                            }

                            Item { Layout.fillHeight: true }

                            Text {
                                Layout.fillWidth: true
                                // Skipping is not failure and must not read as
                                // it: plenty of pads have no Guide button and
                                // no second stick, and a wizard that cannot be
                                // told so is a wizard nobody finishes.
                                text: I18n.tr("A control this pad does not have — skip it.")
                                color: root.dim
                                font.pixelSize: root.ui(11)
                                font.family: root.uiFont
                                horizontalAlignment: Text.AlignHCenter
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 8
                                ArcButton {
                                    text: I18n.tr("Skip")
                                    onTriggered: root.wizSay("skip")
                                }
                                ArcButton {
                                    text: I18n.tr("Back")
                                    onTriggered: root.wizSay("back")
                                }
                                ArcButton {
                                    text: I18n.tr("Cancel")
                                    onTriggered: root.wizSay("cancel")
                                }
                            }
                        }
                    }

                    ListView {
                        // A view that scrolls says so — see SynScrollBar above.
                        ScrollBar.vertical: SynScrollBar {}
                        visible: !root.wizOn
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.maps

                        delegate: Rectangle {
                            id: mapRow
                            required property var modelData
                            width: ListView.view.width
                            height: 52
                            radius: 6
                            color: root.panelHi

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: mapRow.modelData.name
                                        color: root.ink
                                        font.pixelSize: root.ui(13)
                                        font.family: root.uiFont
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        // ⚠ ONE msgid with both numbers in it:
                                        // a language that counts differently
                                        // cannot be built from a suffix.
                                        text: I18n.tr("%1  ·  %2 bindings")
                                                  .arg(mapRow.modelData.guid)
                                                  .arg(mapRow.modelData.bindings)
                                        color: root.dim
                                        font.pixelSize: root.ui(11)
                                        font.family: "monospace"
                                        elide: Text.ElideRight
                                    }
                                }

                                ArcButton {
                                    text: I18n.tr("Remove")
                                    onTriggered: root.run(["map", "remove",
                                        mapRow.modelData.guid])
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.wizOn && root.maps.length === 0
                        text: I18n.tr("No mappings added — the pads SDL already knows are working "
                            + "from its own database, and need nothing here.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }
                }

                // ── Shortcuts ─────────────────────────────────────────────
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    visible: root.tab === 5

                    Text {
                        text: I18n.tr("Gaming shortcuts")
                        color: root.ink
                        font.pixelSize: root.ui(16)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.tr("Two compositor keys that drive the overlay from inside a "
                            + "running game. They are written to synuirc as ordinary bind "
                            + "lines, so the Super+/ palette can rebind them like any other.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    FieldRow {
                        label: I18n.tr("Installed")
                        // i18n-dynamic: yes/no are marked N_() in src/binds.c
                        value: I18n.tr(root.bindFields.installed || "") || "—"
                    }
                    FieldRow {
                        label: I18n.tr("Toggle overlay")
                        value: root.bindFields["toggle overlay"] || "—"
                    }
                    FieldRow {
                        label: I18n.tr("Move overlay")
                        value: root.bindFields["move overlay"] || "—"
                    }
                    FieldRow {
                        label: I18n.tr("Config file")
                        value: root.bindFields.config || "—"
                    }

                    RowLayout {
                        Layout.topMargin: 6
                        spacing: 8
                        ArcButton {
                            text: root.bindFields.installed === "yes"
                                  ? I18n.tr("Reinstall")
                                  : I18n.tr("Install shortcuts")
                            primary: root.bindFields.installed !== "yes"
                            onTriggered: root.run(["binds", "install", "--reload"])
                        }
                        ArcButton {
                            text: I18n.tr("Remove")
                            visible: root.bindFields.installed === "yes"
                            onTriggered: root.run(["binds", "remove", "--reload"])
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── Fit to screen ─────────────────────────────────────────
                //
                // Three states in one panel, never two at once: the wrappers
                // that exist, the picker for choosing an installed game, and
                // the editor. They are separate `visible` blocks rather than
                // separate windows because the whole point of this tab is that
                // making one of these is a single sitting — a dialog on top of
                // a dialog is how the hand-written version already felt.
                //
                // ⚠ NOTHING HERE PARSES A .desktop FILE OR A GAMESCOPE LINE.
                // `fit inspect` does both and answers in records, so the form
                // is filled with exactly what the binary would have produced.
                // A window that read the entry itself would be a second
                // implementation of the same two parsers, and the way that
                // fails is a form pre-filled with something `fit new` would
                // never write.
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    visible: root.tab === 3

                    Text {
                        text: root.fitEditing
                              ? (root.fitId ? I18n.tr("Edit a wrapper")
                                            : I18n.tr("New wrapper"))
                            : root.fitPicking ? I18n.tr("Which game?")
                            : I18n.tr("Fit a game to the screen")
                        color: root.ink
                        font.pixelSize: root.ui(16)
                        font.family: root.uiFont
                        font.bold: true
                    }

                    // ── the wrappers that exist ───────────────────────────
                    Text {
                        Layout.fillWidth: true
                        visible: !root.fitEditing && !root.fitPicking
                        text: I18n.tr("An old game renders at 640×480 or 1024×768 and has no idea "
                            + "what this monitor is, so it sits in the middle of a black "
                            + "screen. gamescope gives it a display of exactly the size it "
                            + "wants and stretches the result to fill yours. What you make "
                            + "here goes in the applications menu — and on the desktop if "
                            + "you ask — so it is a shortcut you click, not a line you have "
                            + "to remember.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !root.fitEditing && !root.fitPicking
                        spacing: 8
                        ArcButton {
                            text: I18n.tr("From an installed game")
                            primary: true
                            onTriggered: root.fitOpenPicker()
                        }
                        ArcButton {
                            text: I18n.tr("From a command")
                            onTriggered: root.fitOpenNew()
                        }
                        Item { Layout.fillWidth: true }
                    }

                    ListView {
                        // A view that scrolls says so — see SynScrollBar above.
                        ScrollBar.vertical: SynScrollBar {}
                        visible: !root.fitEditing && !root.fitPicking
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.fits

                        delegate: Rectangle {
                            id: fitRow
                            required property var modelData
                            width: ListView.view.width
                            height: 62
                            radius: 6
                            color: root.panelHi

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: fitRow.modelData.name
                                        color: root.ink
                                        font.pixelSize: root.ui(13)
                                        font.family: root.uiFont
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        // The two resolutions are the whole
                                        // reason the wrapper exists, so they
                                        // are what the row says.
                                        // ⚠ THE SIZES AND THE FILTER ARE
                                        // gamescope's own words and stay as
                                        // they are; only the tail is a phrase.
                                        text: fitRow.modelData.game + " → "
                                            + fitRow.modelData.screen
                                            + " · " + fitRow.modelData.filter
                                            + (fitRow.modelData.desktop === "yes"
                                               ? I18n.tr(" · on the desktop") : "")
                                        color: root.dim
                                        font.pixelSize: root.ui(11)
                                        font.family: root.uiFont
                                        elide: Text.ElideRight
                                    }
                                }

                                ArcButton {
                                    text: I18n.tr("Play")
                                    // ⚠ --detach. This window IS quickshell,
                                    // and a game started as its child inherits
                                    // quickshell's pipes: closing the window
                                    // would close them under a running game.
                                    onTriggered: root.run(["fit", "run",
                                        fitRow.modelData.id, "--detach"])
                                }
                                ArcButton {
                                    text: I18n.tr("Edit")
                                    onTriggered: root.fitOpenEdit(fitRow.modelData.id)
                                }
                                ArcButton {
                                    text: I18n.tr("Remove")
                                    onTriggered: root.run(["fit", "remove",
                                        fitRow.modelData.id])
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.fitEditing && !root.fitPicking
                                 && root.fits.length === 0
                        text: I18n.tr("Nothing wrapped yet.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                    }

                    // ── the picker ────────────────────────────────────────
                    Text {
                        Layout.fillWidth: true
                        visible: root.fitPicking
                        text: I18n.tr("Everything installed, games first. Its command, folder and "
                            + "icon come across with it — and if it already runs gamescope, "
                            + "that line is taken apart rather than wrapped again.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.fitPicking
                        spacing: 8
                        ArcInput {
                            id: appSearch
                            Layout.fillWidth: true
                            placeholder: I18n.tr("Search…")
                        }
                        ArcButton {
                            text: I18n.tr("Cancel")
                            onTriggered: root.fitPicking = false
                        }
                    }

                    ListView {
                        // A view that scrolls says so — see SynScrollBar above.
                        ScrollBar.vertical: SynScrollBar {}
                        visible: root.fitPicking
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: root.fitApps.filter(a =>
                            appSearch.text === ""
                            || a.name.toLowerCase().indexOf(
                                   appSearch.text.toLowerCase()) >= 0)

                        delegate: Rectangle {
                            id: appRow
                            required property var modelData
                            width: ListView.view.width
                            height: 46
                            radius: 6
                            color: appMa.containsMouse ? root.panel : root.panelHi

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: appRow.modelData.name
                                        + (appRow.modelData.kind === "game" ? "" : "")
                                    color: root.ink
                                    font.pixelSize: root.ui(12)
                                    font.family: root.uiFont
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: appRow.modelData.exec
                                    color: root.dim
                                    font.pixelSize: root.ui(10)
                                    font.family: "monospace"
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: appMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.fitFromApp(appRow.modelData.id)
                            }
                        }
                    }

                    // ── the editor ────────────────────────────────────────
                    Flickable {
                        // A view that scrolls says so — see SynScrollBar above.
                        ScrollBar.vertical: SynScrollBar {}
                        id: fitFlick
                        visible: root.fitEditing
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: fitForm.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: fitForm
                            width: fitFlick.width
                            spacing: 8

                            Text {
                                text: I18n.tr("Name in the menu")
                                color: root.dim
                                font.pixelSize: root.ui(11)
                                font.family: root.uiFont
                            }
                            ArcInput {
                                id: fitNameField
                                Layout.fillWidth: true
                                placeholder: I18n.tr("The Sims (Fullscreen)")
                            }

                            Text {
                                Layout.topMargin: 4
                                text: I18n.tr("Command")
                                color: root.dim
                                font.pixelSize: root.ui(11)
                                font.family: root.uiFont
                            }
                            ArcInput {
                                id: fitExecField
                                Layout.fillWidth: true
                                placeholder: I18n.tr("wine Sims.exe")
                            }

                            Text {
                                Layout.topMargin: 4
                                text: I18n.tr("Folder to run it in")
                                color: root.dim
                                font.pixelSize: root.ui(11)
                                font.family: root.uiFont
                            }
                            ArcInput {
                                id: fitDirField
                                Layout.fillWidth: true
                                placeholder: I18n.tr("(the game's own directory)")
                            }

                            // ⚠ THE ONE THING THIS TAB EXISTS TO GET RIGHT.
                            // gamescope's -w and -W are different flags, and
                            // swapping them renders the game at the monitor's
                            // resolution — which is the thing being avoided,
                            // and which most of these games cannot do at all.
                            // So they are never two boxes side by side: they
                            // are two labelled rows, in the order the sentence
                            // reads.
                            Text {
                                Layout.topMargin: 8
                                text: I18n.tr("What the game renders at")
                                color: root.ink
                                font.pixelSize: root.ui(12)
                                font.family: root.uiFont
                                font.bold: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: ["320x200", "640x480", "800x600",
                                            "1024x768", "1280x720"]
                                    ArcChip {
                                        required property string modelData
                                        label: modelData.replace("x", "×")
                                        on: fitGameField.text === modelData
                                        onPicked: fitGameField.text = modelData
                                    }
                                }
                                ArcInput {
                                    id: fitGameField
                                    Layout.preferredWidth: 110
                                    placeholder: "1024x768"
                                }
                            }

                            Text {
                                Layout.topMargin: 8
                                text: I18n.tr("The screen it fills")
                                color: root.ink
                                font.pixelSize: root.ui(12)
                                font.family: root.uiFont
                                font.bold: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: root.fitScreens
                                    ArcChip {
                                        required property var modelData
                                        label: modelData.label
                                        on: fitScreenField.text === modelData.size
                                        onPicked: fitScreenField.text = modelData.size
                                    }
                                }
                                ArcInput {
                                    id: fitScreenField
                                    Layout.preferredWidth: 110
                                    placeholder: "2560x1440"
                                }
                            }

                            Text {
                                Layout.topMargin: 8
                                text: I18n.tr("Upscaler")
                                color: root.ink
                                font.pixelSize: root.ui(12)
                                font.family: root.uiFont
                                font.bold: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    // From `fit choices filter`, so this and
                                    // what gamescope accepts cannot drift.
                                    model: root.fitFilters
                                    ArcChip {
                                        required property var modelData
                                        label: modelData.label
                                        on: root.fitFilter === modelData.id
                                        onPicked: root.fitFilter = modelData.id
                                    }
                                }
                                Text {
                                    text: I18n.tr("sharpness")
                                    color: root.dim
                                    font.pixelSize: root.ui(11)
                                    font.family: root.uiFont
                                }
                                ArcInput {
                                    id: fitSharpField
                                    Layout.preferredWidth: 60
                                    placeholder: "0–20"
                                }
                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                Layout.topMargin: 8
                                text: I18n.tr("Variables — one NAME=VALUE per line")
                                color: root.dim
                                font.pixelSize: root.ui(11)
                                font.family: root.uiFont
                            }
                            ArcArea {
                                id: fitEnvArea
                                Layout.fillWidth: true
                                Layout.preferredHeight: 54
                                placeholder: I18n.tr("WINEPREFIX=/home/you/Games/thegame")
                            }

                            ColumnLayout {
                                Layout.topMargin: 8
                                spacing: 6
                                ArcCheck {
                                    label: I18n.tr("Put it in the applications menu")
                                    checked: root.fitMenu
                                    onToggled: root.fitMenu = !root.fitMenu
                                }
                                ArcCheck {
                                    label: I18n.tr("Put an icon on the desktop")
                                    checked: root.fitDesktop
                                    onToggled: root.fitDesktop = !root.fitDesktop
                                }
                                ArcCheck {
                                    label: I18n.tr("Force the game's own window to fill the display")
                                    checked: root.fitForceWin
                                    onToggled: root.fitForceWin = !root.fitForceWin
                                }
                                ArcCheck {
                                    label: I18n.tr("Performance overlay over the game")
                                    checked: root.fitOverlay
                                    onToggled: root.fitOverlay = !root.fitOverlay
                                }
                                ArcCheck {
                                    label: I18n.tr("Run it under gamemode")
                                    checked: root.fitGamemode
                                    onToggled: root.fitGamemode = !root.fitGamemode
                                }
                            }

                            // What will actually run, before it is saved.
                            // Nothing else on this form says whether -w and -H
                            // landed where they were meant to, and this is one
                            // line somebody can read.
                            Text {
                                Layout.topMargin: 10
                                Layout.fillWidth: true
                                text: root.fitPreview()
                                color: root.dim
                                font.pixelSize: root.ui(10)
                                font.family: "monospace"
                                wrapMode: Text.Wrap
                            }

                            // Leaves room under the last row for the pinned
                            // buttons below, which are not part of this
                            // scrolling column.
                            Item { Layout.preferredHeight: 8 }
                        }

                        // ⚠ A scroll bar, because THE FORM IS TALLER THAN THE
                        // WINDOW. Without one there is no sign that anything
                        // is below the fold — a Flickable simply stops, and a
                        // field somebody cannot see is a field they will swear
                        // is missing.
                        Rectangle {
                            anchors.right: parent.right
                            anchors.rightMargin: 2
                            width: 4
                            radius: 2
                            color: root.panelHi
                            visible: fitFlick.contentHeight > fitFlick.height
                            y: fitFlick.contentY
                               + (fitFlick.contentY / fitFlick.contentHeight)
                                 * fitFlick.height
                            height: Math.max(24, fitFlick.height
                                    * fitFlick.height / fitFlick.contentHeight)
                        }
                    }

                    // ⚠ OUTSIDE the Flickable. Inside it, Create sat below the
                    // fold of an 820x600 window — the one button the whole tab
                    // exists to reach, invisible until somebody thought to
                    // scroll a form that gives no sign of scrolling.
                    RowLayout {
                        visible: root.fitEditing
                        Layout.fillWidth: true
                        spacing: 8
                        ArcButton {
                            text: root.fitId ? I18n.tr("Save") : I18n.tr("Create")
                            primary: true
                            enabled: fitExecField.text.trim() !== ""
                            onTriggered: root.fitSave()
                        }
                        ArcButton {
                            text: I18n.tr("Cancel")
                            onTriggered: root.fitEditing = false
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                // ── Big screen ────────────────────────────────────────────
                //
                // Everything about the ten-foot interface that is easier to
                // set with a keyboard in front of you than with a pad from
                // four metres away. The interface itself keeps the things that
                // only make sense there — what is playing, what to launch —
                // and this keeps the ones you set once.
                //
                // ⚠ These rows used to be the bottom of the Shortcuts tab and
                // are GONE from there rather than repeated here. Two panels
                // writing one setting is the shape of every "I turned it off
                // and it came back" report in this project.
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    visible: root.tab === 4

                    Text {
                        text: I18n.tr("Big screen mode")
                        color: root.ink
                        font.pixelSize: root.ui(16)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.tr("The ten-foot interface: your Steam library, Big Picture "
                            + "and the machine's own switches as tiles, drivable from a "
                            + "game controller.")
                        color: root.dim
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.topMargin: 4
                        spacing: 8
                        ArcButton {
                            text: root.bigFields.running === "yes"
                                  ? I18n.tr("Show it")
                                  : I18n.tr("Open big screen")
                            primary: true
                            // ⚠ --detach. This window IS quickshell, and `run`
                            // starts a child Process: without the fork the big
                            // screen shell would be that child, so closing this
                            // window would close its pipes.
                            onTriggered: root.run(root.bigFields.running === "yes"
                                ? ["big", "show"] : ["big", "start", "--detach"])
                        }
                        ArcButton {
                            text: I18n.tr("Close it")
                            visible: root.bigFields.running === "yes"
                            onTriggered: root.run(["big", "stop"])
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.bigFields.running === "yes"
                                  ? I18n.tr("running") : I18n.tr("not running")
                            color: root.bigFields.running === "yes" ? root.good : root.dim
                            font.pixelSize: root.ui(12)
                            font.family: root.uiFont
                        }
                    }

                    FieldRow {
                        label: I18n.tr("Opens with")
                        value: root.bindFields["big screen mode"] || "—"
                    }

                    // ⚠ The screen it opens on, first because it is the
                    // setting that fails invisibly: at login nobody has
                    // pointed at anything, so "wherever the pointer is" opens
                    // the television interface on whichever monitor the cursor
                    // was parked on — and nothing on that screen can explain
                    // why it is there.
                    Text {
                        Layout.topMargin: 6
                        text: I18n.tr("Which screen")
                        color: root.ink
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: root.bigScreens
                            ArcChip {
                                required property var modelData
                                label: modelData.label
                                on: modelData.current === "current"
                                onPicked: root.run(["big", "output", modelData.id])
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.tr("Opens on %1").arg(root.bigFields.screen || "—")
                        color: root.dim
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                    }

                    // ⚠ The music player, and the note under the chips is the
                    // point of the row: cliamp is the only one big screen mode
                    // can DRIVE. With it, Music plays without a window and the
                    // Start menu grows transport and a Now Playing meter; with
                    // anything else the tile opens a window somebody then has
                    // to get out of with a gamepad.
                    Text {
                        Layout.topMargin: 8
                        text: I18n.tr("Music player")
                        color: root.ink
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: root.bigPlayers
                            ArcChip {
                                required property var modelData
                                label: modelData.label
                                on: modelData.current === "current"
                                onPicked: root.run(["big", "player", modelData.id])
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: {
                            for (const p of root.bigPlayers)
                                if (p.current === "current") return p.note
                            return root.bigPlayers.length === 0
                                ? I18n.tr("No music player installed — "
                                          + "synpkg install cliamp")
                                : ""
                        }
                        color: root.dim
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    // Only worth showing for a player that HAS sources, which
                    // is the driven one: everything else browses its own
                    // library in its own window.
                    Text {
                        Layout.topMargin: 8
                        visible: root.bigSources.length > 0
                        text: I18n.tr("Where the music comes from")
                        color: root.ink
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.bigSources.length > 0
                        spacing: 6
                        Repeater {
                            model: root.bigSources
                            ArcChip {
                                required property var modelData
                                label: modelData.name
                                on: modelData.current === "1"
                                onPicked: root.run(["big", "music", "source",
                                                    modelData.id])
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // ⚠ The background, and the note under it is the point of
                    // the row rather than decoration. "Follow the desktop" is
                    // the default and resolves to synui's own wallpaper — but
                    // it resolves to NOTHING when that wallpaper is the kanji
                    // rain (a live surface, not a file) or a path that has
                    // since been deleted, and the only symptom of either is a
                    // television that looks plain. The line says which picture
                    // is actually being drawn, so the difference between "off"
                    // and "on but unresolvable" is visible here rather than
                    // being a thing to guess at from a sofa.
                    Text {
                        Layout.topMargin: 8
                        text: I18n.tr("Background")
                        color: root.ink
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    ListView {
                        // A view that scrolls says so — see SynScrollBar above.
                        //
                        // ⛔ AND THIS ONE HAS A CAPPED HEIGHT, which is what
                        // makes it scroll at all: the picture list is as long
                        // as somebody's Pictures folder — thirty rows on this
                        // machine — and a chip row left to its natural width
                        // would run off the edge of the panel with nothing
                        // saying there was more behind it.
                        ScrollBar.horizontal: SynScrollBar {}
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        orientation: ListView.Horizontal
                        clip: true
                        spacing: 6
                        model: root.bigBackgrounds
                        delegate: ArcChip {
                            required property var modelData
                            label: modelData.label
                            on: modelData.current === "current"
                            onPicked: root.run(["big", "background",
                                                modelData.id])
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.bigFields.background || "—"
                        color: root.dim
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    // ⚠ The three web services, and `installed` is why the
                    // record carries a fourth column. Twitch, YouTube and
                    // Spotify are opened in a browser told to behave like an
                    // application; with no browser on the machine there is
                    // nothing to open them in, and three switches that quietly
                    // do nothing are worse than three that say why.
                    Text {
                        Layout.topMargin: 8
                        text: I18n.tr("Web apps")
                        color: root.ink
                        font.pixelSize: root.ui(12)
                        font.family: root.uiFont
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.tr("Tiles on the media shelf. They open in "
                            + "your browser as their own full-screen window, "
                            + "with the controller acting as the mouse and "
                            + "keyboard they need.")
                        color: root.dim
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: root.bigWebapps
                            ArcChip {
                                required property var modelData
                                label: modelData.label
                                on: modelData.current === "on"
                                // ⚠ Sends the OPPOSITE of what it shows. These
                                // are switches, not a one-of-many pick like the
                                // rows above — the chip that is already `on` is
                                // the one somebody presses to turn it off.
                                onPicked: root.run(["big", "webapps",
                                    modelData.id,
                                    modelData.current === "on" ? "off" : "on"])
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.bigWebapps.length > 0
                                 && root.bigWebapps[0].installed !== "yes"
                        text: I18n.tr("No browser installed — these tiles "
                                      + "cannot open.")
                        color: root.bad
                        font.pixelSize: root.ui(11)
                        font.family: root.uiFont
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.topMargin: 10
                        spacing: 8
                        ArcButton {
                            text: root.bigFields["at login"] === "on"
                                  ? I18n.tr("Don't start at login")
                                  : I18n.tr("Start at login")
                            onTriggered: root.run(["big", "autostart",
                                root.bigFields["at login"] === "on" ? "off" : "on"])
                        }
                        // The pad's GUIDE button, from the desktop. Big screen
                        // mode's own Guide takes you out to the desktop; this
                        // watcher is what brings it back, so the two halves of
                        // one button are one setting.
                        ArcButton {
                            text: root.bigFields["guide button"] === "on"
                                  ? I18n.tr("Guide button off")
                                  : I18n.tr("Guide button on")
                            onTriggered: root.run(["big", "guide",
                                root.bigFields["guide button"] === "on" ? "off" : "on"])
                        }
                        Item { Layout.fillWidth: true }
                    }

                    FieldRow {
                        Layout.topMargin: 6
                        label: I18n.tr("Steam library")
                        value: I18n.tr("%1 games").arg(root.bigFields.games || "0")
                    }
                    FieldRow {
                        label: I18n.tr("quickshell")
                        value: root.bigFields.quickshell || "—"
                        warn: (root.bigFields.quickshell || "") !== "installed"
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // status line
            Text {
                Layout.fillWidth: true
                text: root.status
                color: root.status.startsWith("syn-arcade:") ? root.bad : root.good
                font.pixelSize: root.ui(12)
                font.family: root.uiFont
                elide: Text.ElideRight
                visible: root.status !== ""
            }
        }
    }

    // `hud --rec` marks the config row's action "detail readonly" when the file
    // cannot be written. Reading the C side's own verdict keeps this window from
    // having a second opinion about what "writable" means — it has no business
    // stat()ing the path itself and reaching a different answer.
    property string hudConfigAction: "detail"
    readonly property bool hudWritable: root.hudConfigAction.indexOf("readonly") < 0

    // ── small components ────────────────────────────────────────────────────

    component ArcButton: Rectangle {
        id: btn
        property string text: ""
        property bool primary: false
        // No `property bool enabled` of its own: Item already has one, and
        // redeclaring it shadows the base member — the two then disagree, and
        // the inherited one is what actually gates input delivery.
        signal triggered()

        implicitHeight: 30
        implicitWidth: t.implicitWidth + 26
        radius: 6
        opacity: btn.enabled ? 1 : 0.4
        color: ma.containsMouse && btn.enabled
               ? Qt.lighter(btn.primary ? root.accent : root.panelHi, 1.15)
               : (btn.primary ? root.accent : root.panelHi)

        Text {
            id: t
            anchors.centerIn: parent
            text: btn.text
            color: btn.primary ? "#1b1030" : root.ink
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
            font.bold: btn.primary
        }

        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (btn.enabled) btn.triggered()
        }
    }

    // A one-line text box with a placeholder.
    //
    // ⚠ `onTextEdited`, never `onTextChanged`, wherever one of these is bound
    // to a root property: onTextChanged also fires when the BINDING writes the
    // box — filling the form from `fit inspect` would write the property, which
    // writes the box, which writes the property. onTextEdited is typing only.
    component ArcInput: Rectangle {
        id: fieldBox
        property alias text: inp.text
        property string placeholder: ""

        implicitHeight: 28
        implicitWidth: 160
        radius: 6
        color: root.panelHi

        TextInput {
            id: inp
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: TextInput.AlignVCenter
            color: root.ink
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
            clip: true
            selectByMouse: true
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: inp.text === ""
            text: fieldBox.placeholder
            color: root.dim
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
            elide: Text.ElideRight
        }
    }

    // The same, several lines tall. One box per environment variable would be a
    // list widget with an add button and a remove button; lines in a box is the
    // same information and no chrome, and a value with a space in it survives
    // it (which is why they are not space-separated in one field).
    component ArcArea: Rectangle {
        id: areaBox
        property alias text: area.text
        property string placeholder: ""

        implicitHeight: 54
        radius: 6
        color: root.panelHi

        TextEdit {
            id: area
            anchors.fill: parent
            anchors.margins: 8
            color: root.ink
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
            wrapMode: TextEdit.NoWrap
            clip: true
            selectByMouse: true
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 8
            visible: area.text === ""
            text: areaBox.placeholder
            color: root.dim
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
        }
    }

    component ArcCheck: Item {
        id: chk
        property string label: ""
        property bool checked: false
        signal toggled()

        implicitHeight: 20
        implicitWidth: 16 + 8 + chkLabel.implicitWidth

        Rectangle {
            id: chkBox
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 4
            color: chk.checked ? root.accent : root.panelHi
            Text {
                anchors.centerIn: parent
                text: chk.checked ? "✓" : ""
                color: "#1b1030"
                font.pixelSize: root.ui(11)
                font.family: root.uiFont
                font.bold: true
            }
        }
        Text {
            id: chkLabel
            anchors.left: chkBox.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: chk.label
            color: root.ink
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: chk.toggled()
        }
    }

    component ArcChip: Rectangle {
        id: chip
        property string label: ""
        property bool on: false
        signal picked()

        implicitHeight: 24
        implicitWidth: chipText.implicitWidth + 18
        radius: 12
        color: chip.on ? root.accent : root.panelHi

        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.on ? "#1b1030" : root.ink
            font.pixelSize: root.ui(11)
            font.family: root.uiFont
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.picked()
        }
    }

    component FieldRow: RowLayout {
        property string label: ""
        property string value: ""
        property bool warn: false

        Layout.fillWidth: true
        spacing: 10

        // ⛔ A FIXED 120 px LABEL COLUMN OVERRAN IN EVERY LANGUAGE BUT ENGLISH.
        // "Config file" is 120 px wide; "Konfigurationsdatei" is not, and it
        // printed straight over the path beside it — a fixed pixel width is a
        // measurement of one language's words.
        //
        // ⚠ minimumWidth KEEPS THE COLUMN ALIGNED for the short labels, which
        // is what makes this read as a table at all; the row grows only for a
        // label that genuinely needs it, and elides rather than overlapping if
        // the window is too narrow for even that.
        Text {
            Layout.preferredWidth: Math.max(120, implicitWidth)
            Layout.maximumWidth: 260
            text: parent.label
            color: root.dim
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: parent.value
            color: parent.warn ? root.bad : root.ink
            font.pixelSize: root.ui(12)
            font.family: root.uiFont
            elide: Text.ElideRight
        }
    }
}
