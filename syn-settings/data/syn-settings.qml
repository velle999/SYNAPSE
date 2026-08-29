// syn-settings — SynapseOS system settings.
//
// A reader first. Every pane shows what the system ACTUALLY reports, from the
// tool that owns that answer, with the layer it came from — because the
// configuration bugs this OS has cost the most time on were invisible rather
// than wrong: units installed and disabled, a console keymap set while the
// desktop read the xkb one, a shipped sysctl overridden from /etc.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Quickshell
import Quickshell.Io
import QtQuick.Controls

FloatingWindow {
    id: root

    title: "SYNAPSE Settings"
    implicitWidth: 900
    implicitHeight: 620
    // Nav is a fixed 190 and the widest pane has five columns. Below this the
    // table cannot hold its shape, and the lesson from synfiles and synpkg on
    // 2026-08-10 is that a layout with no floor does not degrade — it paints
    // over itself.
    minimumSize: Qt.size(640, 420)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNSETTINGS_BIN") || "syn-settings"

    // ── Theme ───────────────────────────────────────────────────────────────
    //
    // Read from the desktop, not hardcoded. The first version of this file
    // baked in a dark palette, which meant a settings app was the one window
    // that ignored the setting — and on a PALE theme it would have been a
    // black slab. Same source and same shape as synfiles and the bar, so a
    // theme switch moves all of them together.
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
        const la = lum(a), lb = lum(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    // A theme accent is chosen to look good on the BAR, not to be legible as
    // text on this window's background. Nudged until it is, rather than trusted.
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
    readonly property color cBg: isLight ? Qt.lighter(cPanel, 1.15) : Qt.darker(cPanel, 1.4)
    readonly property color cInk: p.fg ? Qt.color(p.fg) : pick("#e6e9ef", "#12141a")
    readonly property color cText: contrast(cInk, cBg) >= 4.5
                                   ? cInk
                                   : (lum(cBg) > 0.18 ? "#12141a" : "#e6e9ef")
    readonly property color cDim:    pick("#8b93a7", "#4a5568")
    readonly property color cAccentRaw: root.wpAccent !== ""
                                        ? Qt.color(root.wpAccent)
                                        : themed("accent", 78, 201, 176, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)
    // The three status colours are meaning, not decoration, so they are held
    // to the same contrast rule as the accent rather than being fixed hexes
    // that vanish on a pale theme.
    readonly property color cWarn: readable(pick("#e0af68", "#8a5a00"), cBg, 4.5)
    readonly property color cBad:  readable(pick("#f7768e", "#a01030"), cBg, 4.5)
    readonly property color cGood: readable(pick("#9ece6a", "#2f6f10"), cBg, 4.5)

    function wash(a) { return Qt.rgba(cAccent.r, cAccent.g, cAccent.b, a) }

    // ── The UI font ─────────────────────────────────────────────────────────
    // Watched, exactly as the bar and synfiles watch it: font.state is written
    // by synui-apply-font(1) and outlives a theme switch, which is why it is
    // not a key in theme.json. Qt resolves an application's default font ONCE
    // at startup, so every Text below names the family and the name is a
    // binding — otherwise the window keeps the old face until it is reopened.
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
            // The text scale lives in the same file, because it is a property
            // of the desktop and not of this window. It was a per-app setting
            // once — synfiles had a slider writing its own config — and the
            // result was synfiles drawing at 115% beside two sibling windows
            // stuck at 100, which reads as "the theming missed those apps".
            const s = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            root.textScale = s ? parseInt(s[1]) : 100
        }
        onLoadFailed: { root.uiFont = ""; root.textScale = 100 }
    }

    // Every pixelSize in this file goes through here. Qt cannot restyle an
    // application's font after startup, so the size has to be a BINDING on
    // each Text rather than something applied once — the same reason the
    // family is named on every one of them.
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
            color: sb.pressed ? root.cAccent : sb.hovered ? root.cText : root.cDim
            opacity: sb.pressed || sb.hovered ? 1.0 : 0.5
            Behavior on color   { ColorAnimation  { duration: 90 } }
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }

        background: Rectangle {
            radius: Math.min(width, height) / 2
            color: Qt.rgba(root.cText.r, root.cText.g, root.cText.b, 0.08)
            opacity: sb.hovered || sb.pressed ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }


    // ── State ───────────────────────────────────────────────────────────────
    readonly property var panes: [
        { id: "display",   label: "Display",  blurb: "connectors as the kernel sees them, beside what the compositor drives" },
        { id: "region",    label: "Keyboard & Language", blurb: "the console keymap and the xkb layout, which are separate settings that sometimes disagree" },
        { id: "time",      label: "Date & Time", blurb: "the system clock — and how the desktop writes it: 12- or 24-hour, and which date order" },
        { id: "network",   label: "Network",  blurb: "interfaces, radios, and whether anything is filtering traffic" },
        { id: "bluetooth", label: "Bluetooth", blurb: "the adapter, both kinds of radio block, and what is paired" },
        { id: "power",     label: "Power & Sleep", blurb: "the units a working suspend depends on, and what the last one did" },
        { id: "apps",      label: "Default Apps", blurb: "what opens each kind of file — and whether anybody actually chose it" },
        { id: "kernel",    label: "Kernel",   blurb: "every kernel on offer, which are installed, and which one you booted" },
        { id: "ai",        label: "AI",       blurb: "the backend switch, the units that can restart it behind your back, and which model is on disk" },
        { id: "fprint",    label: "Fingerprint", blurb: "the reader, which fingers are on file, and enrolling another — the lock screen offers it only once something is" },
        { id: "system",    label: "System",   blurb: "identity, and which layer each configuration file comes from" }
    ]
    property string pane: Quickshell.env("SYNSETTINGS_PANE") || "display"
    property var cols: []
    property var rows: []
    property bool loading: false
    property string status: ""

    // What the LAST write ended up doing, kept apart from `status` because the
    // two have opposite lifetimes. `status` is about right now and every
    // reload clears it — which is how the one message that mattered was lost:
    // a write failed, said so, and reload() (which a finished write always
    // triggers) blanked the line before it could be read. Reported on
    // 2026-08-12 as "it said decline but it's closing too fast to tell".
    //
    // This one survives the reload and is cleared by the NEXT write, or by
    // clicking it away. A failure is not over when the command is.
    property string outcome: ""

    // ── Editing ─────────────────────────────────────────────────────────────
    //
    // The row the editor is pointed at, and what it is allowed to do. Driven
    // entirely by the reader's trailing `action` column — "set:keymap",
    // "toggle:ntp", "choice:date-format", "unit:<name>", "probe:<name>" or
    // "-". The GUI knows the VERBS and nothing about localectl, so a new
    // editable setting is a line of C and no QML at all.
    //
    // `choice` is the general form of what `app` and `mode` each do for one
    // pane: ask the binary what this setting can be, draw a button per answer.
    // Adding a settings row with a fixed set of options now costs no QML.
    property int selRow: -1
    property string selAction: ""
    property string selKey: ""
    property string selValue: ""
    property bool applying: false
    // Modes for the output the editor is pointed at. Emptied on every
    // selection change, so a slow `modes` call can never paint DP-2's list
    // under DP-3's name.
    property var modeList: []

    // The applications that could take the selected role. Same shape and same
    // reason as modeList: a role has a dozen candidates and they belong to the
    // row you picked, not to every row. Emptied on every selection change, so
    // a slow lookup can never paint the browser list under "Image Viewer".
    property var appList: []

    // What the selected setting can be set TO. Same shape and same reason as
    // appList and modeList: the options belong to the row you picked.
    //
    // Fetched rather than carried in the table because the date layouts come
    // from `synui-clock --layouts` — a different package. Hardcoding them here
    // would be a third copy of a list that already exists twice, and the one
    // that drifts is always the one furthest from what draws the pixels.
    property var choiceList: []

    Process {
        id: choicesProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of this.text.split("\n")) {
                    if (line === "") continue
                    const f = line.split("\t")
                    out.push({ id: f[0], name: f[1] || f[0],
                               current: f.length > 2 && f[2] === "current" })
                }
                root.choiceList = out
            }
        }
        stderr: StdioCollector { onStreamFinished: if (this.text) root.status = this.text.split("\n")[0] }
    }

    Process {
        id: appsProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of this.text.split("\n")) {
                    if (line === "") continue
                    const f = line.split("\t")
                    out.push({ id: f[0], name: f[1] || f[0],
                               current: f.length > 2 && f[2] === "current" })
                }
                root.appList = out
                if (out.length === 0)
                    root.status = "nothing installed offers to handle this"
            }
        }
        stderr: StdioCollector { onStreamFinished: if (this.text) root.status = this.text.split("\n")[0] }
    }

    Process {
        id: modesProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of this.text.split("\n")) {
                    if (line === "") continue
                    const f = line.split("\t")
                    out.push({ mode: f[0], current: f.length > 1 && f[1] === "current" })
                }
                root.modeList = out
                if (out.length === 0)
                    root.status = "no modes listed — is wlr-randr installed?"
            }
        }
        stderr: StdioCollector { onStreamFinished: if (this.text) root.status = this.text.split("\n")[0] }
    }

    readonly property int actionCol: root.cols.indexOf("action")
    function rowAction(r) {
        if (root.actionCol < 0 || !r || root.actionCol >= r.length) return "-"
        return r[root.actionCol]
    }
    function actionVerb(a) { const i = a.indexOf(":"); return i < 0 ? a : a.substring(0, i) }

    // An action cell is a SPACE-SEPARATED LIST of verb:arg, and a row means
    // every one of them. It used to be a single token, and the kernel pane's
    // `pkg:<name>` was answered here by drawing an Install button AND a Remove
    // button for every row that carried it — so a kernel that was not
    // installed offered to remove itself, and an installed one offered to
    // install itself again. The buttons described the verb; nothing consulted
    // the machine.
    //
    // Every other pane emits one token, which is a one-element list, so these
    // read the same there. Ask "does this row offer <verb>?" — never "what is
    // the verb of this row?", which cannot answer a row offering two.
    function actionHas(a, verb) {
        const t = String(a).split(" ")
        for (let i = 0; i < t.length; i++)
            if (root.actionVerb(t[i]) === verb) return true
        return false
    }
    function actionArgFor(a, verb) {
        const t = String(a).split(" ")
        for (let i = 0; i < t.length; i++)
            if (root.actionVerb(t[i]) === verb) return root.actionArg(t[i])
        return ""
    }
    // Every tool spells "on" differently — systemd says active, nmcli says
    // enabled, bluetoothctl says yes. A toggle that only knew one of them
    // offered "Turn on" for something already on, and then turned it off.
    function isOn(v) {
        return ["active", "enabled", "yes", "on", "true"].indexOf(String(v).toLowerCase()) >= 0
    }
    function actionArg(a)  { const i = a.indexOf(":"); return i < 0 ? "" : a.substring(i + 1) }

    // `unavailable:<desktop>` — a setting that EXISTS and cannot be taken in
    // this session, because it is one synui reads and synui is not what is
    // running. The third state the table needed: "-" says there is nothing
    // here to set, a verb says here is one you can set, and this says here is
    // one you cannot, and names what is in the way. Without it the clock
    // format under GNOME was a live control that wrote a file, said nothing,
    // and changed nothing on screen.
    function rowBlocked(r) {
        return root.actionHas(root.rowAction(r), "unavailable")
    }
    function rowBlockedBy(r) {
        return root.actionArgFor(root.rowAction(r), "unavailable")
    }

    function selectRow(i) {
        const r = root.rows[i]
        const a = root.rowAction(r)
        if (a === "-" || a === "" || root.actionHas(a, "unavailable")) {
            root.selRow = -1; root.selAction = ""; return
        }
        root.selRow = i
        root.selAction = a
        root.selKey = r[0]
        // Column 1 is the value on every pane whose rows are actionable: the
        // panes with a "kind" first column put the name in 1 and the value in
        // 2. Read it by header name rather than by position so a column added
        // later cannot silently shift what the editor edits.
        const vi = root.cols.indexOf("value")
        root.selValue = vi >= 0 && vi < r.length ? r[vi] : (r[1] || "")
        editField.text = root.selValue

        root.appList = []
        if (root.actionHas(a, "app")) {
            appsProc.command = [root.bin, "apps", root.actionArgFor(a, "app")]
            appsProc.running = true
        }

        root.modeList = []
        if (root.actionHas(a, "mode")) {
            modesProc.command = [root.bin, "modes", root.actionArgFor(a, "mode")]
            modesProc.running = true
        }

        root.choiceList = []
        if (root.actionHas(a, "choice")) {
            choicesProc.command = [root.bin, "choices", root.actionArgFor(a, "choice")]
            choicesProc.running = true
        }
    }

    // ── While a write is running ────────────────────────────────────────────
    //
    // Most writes here finish inside the click. TWO do not: installing a kernel
    // downloads a couple of hundred megabytes and runs mkinitcpio, and making
    // one bootable can fall through to an AUR source build. Those used to show
    // a dimmed button and a status line frozen at the moment of the click for
    // minutes at a time, which reads as a hung application — reported as
    // exactly that on 2026-08-12, twice in one sitting.
    //
    // So the tool streams (run_progress() in src/util.c) and this reads what it
    // streams. Three things carry it, in ascending order of how hard they are
    // to miss:
    //
    //   · the status bar — the latest line, elapsed time, a moving bar;
    //   · a work panel, after a delay, for anything still going;
    //   · nothing at all for a write that finishes quickly, which is most.
    property string applyNote: ""     // "Installing linux-cachyos"
    property string applyDetail: ""   // the paragraph under it, if any
    property string progressLine: ""  // the newest line the child printed
    property int progressPct: -1      // parsed out of it, or -1 for unknowable
    property var progressLog: []      // the last few, for a panel that moves
    property double applyStart: 0
    property int applyElapsed: 0
    // The panel appears only once a write has outstayed its welcome. Flashing
    // a modal for the 200ms that "Turn on" takes would be worse than the
    // silence it replaces.
    property bool workOpen: false

    Timer {
        id: applyClock
        interval: 1000
        repeat: true
        running: root.applying
        onTriggered: root.applyElapsed =
            Math.floor((Date.now() - root.applyStart) / 1000)
    }

    Timer {
        id: workDelay
        interval: 900
        onTriggered: root.workOpen = root.applying
    }

    function mmss(s) {
        const m = Math.floor(s / 60)
        const r = s % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    function noteProgress(text) {
        if (text === "") return
        root.progressLine = text

        // "(2/9) linux-cachyos   62%" — pacman's shape, which synpkg keeps.
        // Anything else leaves the bar indeterminate rather than inventing a
        // number, because a progress bar that is guessing is worse than one
        // that admits it does not know.
        const m = text.match(/(\d{1,3})\s*%\s*$/)
        root.progressPct = m ? Math.min(100, parseInt(m[1])) : -1

        // That line is redrawn once per percent. Logged naively it would be a
        // hundred entries of the same package name, so entries are keyed on
        // everything before the numbers: the same key replaces, a new one
        // appends. The log then reads as a list of steps, with the current one
        // counting up at the bottom.
        const key = text.replace(/[\s\d.,%\/()]+$/, "")
        const log = root.progressLog.slice()
        if (log.length > 0 && log[log.length - 1].key === key)
            log[log.length - 1] = { key: key, text: text }
        else
            log.push({ key: key, text: text })
        while (log.length > 6) log.shift()
        root.progressLog = log
    }

    // Runs a write, then reloads. Never parses the tool's output for the RESULT:
    // the reader is the source of truth for what the system now says, and
    // believing our own success message over a re-read is how a settings app
    // starts showing a value the system never accepted. What is parsed here is
    // only progress — what is happening, never what happened.
    Process {
        id: writeProc
        // This binary's OWN stderr, which is not where a failing child speaks:
        // run_progress() (src/util.c) folds the child's stderr into our stdout
        // as progress records, so synpkg's "declined: there is no terminal to
        // confirm on" arrives as the last progress line, not here. Both are
        // read below, and neither is thrown away on a failure any more.
        property string errLine: ""

        // SplitParser, not StdioCollector: a collector hands over its text when
        // the stream ENDS, which for a five-minute install is five minutes
        // after the only moment the text was worth having.
        stdout: SplitParser {
            onRead: (line) => {
                if (line.startsWith("progress\t"))
                    root.noteProgress(line.substring(9))
                else if (line !== "")
                    root.status = line
            }
        }
        stderr: StdioCollector {
            onStreamFinished: {
                if (!this.text) return
                writeProc.errLine = this.text.split("\n")[0]
                // Whether this arrives before or after onExited is not ours to
                // decide, so handle both: if the exit already ran and settled
                // for a weaker message, replace it with the real one.
                if (!root.applying && root.outcome !== "")
                    root.outcome = writeProc.errLine
            }
        }
        onExited: (code) => {
            root.applying = false
            root.workOpen = false
            workDelay.stop()
            // A non-zero exit ALWAYS leaves something on screen. This was once
            // gated on the status line being empty — which it never was, since
            // runWrite() had just put the note there — so a write could fail
            // and say nothing whatsoever.
            if (code !== 0)
                root.outcome = writeProc.errLine !== "" ? writeProc.errLine
                    : root.progressLine !== "" ? root.progressLine
                    : "refused (exit " + code + ") — polkit may have declined"
            root.reload()
        }
    }

    // `note` is the short label — it is what the status bar and the panel title
    // say. `detail` is the paragraph the panel adds underneath, for the writes
    // where "this takes minutes" is the thing worth knowing.
    function runWrite(args, note, detail) {
        if (root.applying) return
        root.applying = true
        root.applyNote = note
        root.applyDetail = detail || ""
        root.outcome = ""
        writeProc.errLine = ""
        root.progressLine = ""
        root.progressPct = -1
        root.progressLog = []
        root.applyStart = Date.now()
        root.applyElapsed = 0
        root.status = note
        workDelay.restart()
        writeProc.command = [root.bin].concat(args)
        writeProc.running = true
    }

    // ── Changing boot configuration ─────────────────────────────────────────
    //
    // This is the one action in the app that can leave a machine that does not
    // boot, so it is the one action that does not happen on a single click.
    //
    // The dialogue does NOT describe the change in its own words. It runs the
    // real thing under --dry-run and displays what came back, so what you are
    // asked to approve and what then runs are produced by the same code path
    // and cannot drift apart. `syn-settings boot` refuses without --confirm
    // regardless of what this QML does — the C binary is the boundary, and a
    // confirmation that only lives in a GUI is one anything else can skip.
    property bool confirmOpen: false
    property string confirmKernel: ""
    property var confirmPlan: ({})
    // Which of the two boot writes is being confirmed. The dialogue is shared
    // because the shape is identical — ask the binary what it would do, show
    // exactly that, run the same path with --confirm — and the verb is the
    // only thing that differs.
    property string confirmVerb: "boot"
    // step1..stepN out of the plan, in order. A dict cannot hold a list, so
    // the binary numbers them and this collects them back.
    readonly property var confirmSteps: {
        const out = []
        for (let i = 1; ; i++) {
            const v = root.confirmPlan["step" + i]
            if (!v) break
            out.push(v)
        }
        return out
    }

    Process {
        id: planProc
        stdout: StdioCollector {
            onStreamFinished: {
                const plan = {}
                for (const line of this.text.split("\n")) {
                    const t = line.indexOf("\t")
                    if (t > 0) plan[line.substring(0, t)] = line.substring(t + 1)
                }
                if (plan["command"]) {
                    root.confirmPlan = plan
                    root.confirmOpen = true
                } else {
                    root.status = "could not work out what to run"
                }
            }
        }
        // A refusal explains itself on stderr — no bootloader found, kernel not
        // installed. Showing that beats an empty dialogue.
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = this.text.split("\n")[0]
        }
    }

    function askBootWrite(verb, kernel) {
        if (root.applying) return
        root.confirmVerb = verb
        root.confirmKernel = kernel
        root.confirmPlan = ({})
        root.status = "working out what this would do…"
        planProc.command = [root.bin, "-n", verb, kernel]
        planProc.running = true
    }
    function askBootable(kernel) { root.askBootWrite("boot", kernel) }
    function askDefault(kernel)  { root.askBootWrite("default", kernel) }

    function paneMeta(id) {
        for (const p of root.panes) if (p.id === id) return p
        return root.panes[0]
    }

    // A button, once. Qt Quick Controls is deliberately not imported: this
    // window is four shapes and a table, and pulling in a styled control set
    // for one button would drag its own palette in alongside the theme we just
    // spent this file honouring.
    component SettingsButton: Rectangle {
        id: btn
        property string label: ""
        // Every button in this app is dead while a write is running — clicking
        // one would build a second command against a system mid-change. The
        // exception is a button that does nothing to the system: the work
        // panel's Hide, which exists PRECISELY for when a write is running and
        // would otherwise be dimmed and unclickable at exactly its moment.
        property bool ignoreBusy: false
        readonly property bool busy: root.applying && !btn.ignoreBusy
        signal go()
        width: btnText.implicitWidth + 22
        height: 26
        radius: 4
        color: btnMa.containsMouse && !btn.busy ? root.wash(0.22) : root.wash(0.10)
        opacity: btn.busy ? 0.5 : 1.0

        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: root.cText
            font { family: root.uiFont; pixelSize: root.ui(11) }
        }
        MouseArea {
            id: btnMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: !btn.busy
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.go()
        }
    }

    // A bar that is honest about what it knows. With a percentage it fills to
    // it; without one it runs a shuttle back and forth, which says "working"
    // without claiming to know how much is left. The alternative — a bar that
    // creeps to 90% and waits — is a lie the user finds out about.
    component ProgressTrack: Rectangle {
        id: track
        property int pct: -1
        property bool active: false
        height: 4
        radius: height / 2
        color: root.wash(0.10)
        clip: true

        Rectangle {
            visible: track.pct >= 0
            height: parent.height
            radius: parent.radius
            width: parent.width * Math.max(0, Math.min(100, track.pct)) / 100
            color: root.cAccent
            Behavior on width { NumberAnimation { duration: 200 } }
        }

        Rectangle {
            id: shuttle
            visible: track.active && track.pct < 0
            width: Math.max(48, track.width * 0.22)
            height: parent.height
            radius: parent.radius
            color: root.cAccent
            opacity: 0.8
            x: -width
        }

        // from/to are read at (re)start, not bound — so the width change that
        // comes with a window resize has to restart it, or the shuttle keeps
        // sweeping the width the window used to have.
        NumberAnimation {
            id: shuttleAnim
            target: shuttle
            property: "x"
            from: -shuttle.width
            to: track.width
            duration: 1400
            loops: Animation.Infinite
            running: shuttle.visible
        }
        onWidthChanged: if (shuttle.visible) shuttleAnim.restart()
    }

    // ── Loading ─────────────────────────────────────────────────────────────
    Process {
        id: readProc
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n").filter(l => l !== "")
                if (lines.length === 0) {
                    root.cols = []; root.rows = []
                    root.loading = false
                    root.status = "nothing reported"
                    return
                }
                // First line names the columns. Taking the header from the
                // data rather than hard-coding it per pane means a new column
                // in the C reader shows up here without a QML change — and,
                // more to the point, the GUI cannot quietly disagree with what
                // the command line prints.
                root.cols = lines[0].split("\t")
                const out = []
                for (let i = 1; i < lines.length; i++)
                    out.push(lines[i].split("\t"))
                root.rows = out
                root.loading = false
                root.status = ""
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = this.text.split("\n")[0]
        }
    }

    function reload() {
        root.loading = true
        root.status = ""
        // The selection is an index into rows that are about to be replaced.
        // Kept across a reload it would point at a different setting with the
        // same number, and the editor would happily write to it.
        root.selRow = -1
        root.selAction = ""
        root.modeList = []
        // And the candidate list with it, for the same reason: a write
        // triggers a reload, and a list left standing would keep its tick on
        // the application that was current BEFORE the write.
        root.appList = []
        readProc.command = [root.bin, "--rec", root.pane]
        readProc.running = true
    }

    Component.onCompleted: root.reload()

    // A value the eye should stop on. Deliberately narrow: only states that
    // mean something is off, so colour stays informative instead of decorative.
    function tone(col, val) {
        const v = (val || "").toLowerCase()
        if (v === "absent" || v === "not installed" || v === "not executable"
            || v === "failed" || v === "not driven" || v === "none")
            return root.cBad
        if (v === "disabled" || v === "inactive" || v === "unknown"
            || v === "disconnected" || v === "no"
            // Nobody chose this: it is whatever mimeinfo.cache listed first,
            // and it changes the day a package is installed. Not an error,
            // but not a setting either.
            || v === "fallback")
            return root.cWarn
        if (v === "enabled" || v === "active" || v === "connected"
            || v === "executable" || v === "present" || v === "yes"
            || v === "chosen")
            return root.cGood
        return root.cText
    }

    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // ── Nav ─────────────────────────────────────────────────────────────
        Rectangle {
            id: nav
            anchors { top: parent.top; left: parent.left; bottom: parent.bottom }
            width: 190
            color: root.cPanel

            Column {
                anchors { top: parent.top; left: parent.left; right: parent.right
                          topMargin: 14 }

                Text {
                    x: 16
                    text: "SYNAPSE Settings"
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                }
                Item { width: 1; height: 14 }

                Repeater {
                    model: root.panes
                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        width: nav.width
                        height: 32
                        color: navItem.modelData.id === root.pane ? root.wash(0.10)
                             : navMa.containsMouse ? root.wash(0.05) : "transparent"

                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: 3
                            color: root.cAccent
                            visible: navItem.modelData.id === root.pane
                        }
                        Text {
                            anchors { left: parent.left; leftMargin: 16
                                      right: parent.right; rightMargin: 10
                                      verticalCenter: parent.verticalCenter }
                            elide: Text.ElideRight
                            text: navItem.modelData.label
                            color: navItem.modelData.id === root.pane ? root.cText : root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }
                        MouseArea {
                            id: navMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.pane = navItem.modelData.id; root.reload() }
                        }
                    }
                }
            }

            Text {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                          margins: 12 }
                wrapMode: Text.WordWrap
                text: "Reads the live system. Writes go through localectl, "
                    + "timedatectl and systemctl, which do their own "
                    + "authorisation."
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(9) }
            }
        }

        // ── Header ──────────────────────────────────────────────────────────
        Item {
            id: header
            anchors { top: parent.top; left: nav.right; right: parent.right }
            height: 64

            Text {
                id: headTitle
                anchors { left: parent.left; leftMargin: 18; top: parent.top; topMargin: 14 }
                text: root.paneMeta(root.pane).label
                color: root.cText
                font { family: root.uiFont; pixelSize: root.ui(15); bold: true }
            }
            Text {
                anchors { left: parent.left; leftMargin: 18
                          right: refreshBtn.left; rightMargin: 12
                          top: headTitle.bottom; topMargin: 4 }
                elide: Text.ElideRight
                text: root.paneMeta(root.pane).blurb
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(10) }
            }

            Rectangle {
                id: refreshBtn
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                width: 84; height: 26; radius: 4
                color: refreshMa.containsMouse ? root.wash(0.14) : root.wash(0.07)

                Text {
                    anchors.centerIn: parent
                    text: root.loading ? "reading…" : "Refresh"
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
                MouseArea {
                    id: refreshMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.reload()
                }
            }
        }

        // ── Column headings ─────────────────────────────────────────────────
        Item {
            id: headRow
            anchors { top: header.bottom; left: nav.right; right: parent.right }
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            height: root.cols.length > 0 ? 22 : 0
            clip: true

            Row {
                anchors.fill: parent
                Repeater {
                    model: root.cols
                    delegate: Text {
                        required property var modelData
                        required property int index
                        // The first column is the identifier and gets the room;
                        // the last is prose and takes whatever is left. Fixed
                        // fractions rather than content-derived widths, so the
                        // table does not reflow every refresh and make a
                        // changed value look like a moved row.
                        width: headRow.colWidth(index)
                        visible: index !== root.actionCol
                        elide: Text.ElideRight
                        text: modelData
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                    }
                }
            }

            // `action` is plumbing, not information: it exists so the GUI
            // knows which rows can be changed. Printing "unit:synapd.service"
            // in a column would be showing the wiring to the user.
            function colWidth(i) {
                if (i === root.actionCol) return 0
                const n = root.cols.length - (root.actionCol >= 0 ? 1 : 0)
                if (n <= 1) return headRow.width
                const first = Math.min(220, headRow.width * 0.30)
                const rest = Math.max(60, (headRow.width - first) / (n - 1))
                return i === 0 ? first : rest
            }
        }

        Rectangle {
            id: headRule
            anchors { top: headRow.bottom; left: nav.right; right: parent.right }
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            height: 1
            color: root.wash(0.12)
            visible: root.cols.length > 0
        }

        // ── Rows ────────────────────────────────────────────────────────────
        ListView {
            // A view that scrolls says so — see SynScrollBar above.
            ScrollBar.vertical: SynScrollBar {}
            id: table
            anchors {
                top: headRule.bottom; topMargin: 4
                left: nav.right; leftMargin: 18
                right: parent.right; rightMargin: 18
                bottom: editor.top; bottomMargin: 6
            }
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: dataRow
                required property var modelData
                required property int index
                width: table.width
                height: 26
                // A setting this session cannot take. Distinct from both
                // "actionable" and "just a fact": it is drawn dimmed, with a
                // dimmed edge where an actionable row has an accent one, so
                // the table says "this is a setting, and not one for you here"
                // rather than hiding it or pretending it will work.
                readonly property bool blocked: root.rowBlocked(dataRow.modelData)
                readonly property bool actionable:
                    root.rowAction(dataRow.modelData) !== "-" && !dataRow.blocked
                readonly property bool chosen: root.selRow === dataRow.index

                color: dataRow.chosen ? root.wash(0.20)
                     : (rowMa.containsMouse && dataRow.actionable) ? root.wash(0.10)
                     : rowMa.containsMouse ? root.wash(0.05)
                     : (dataRow.index % 2 === 1 ? root.wash(0.02) : "transparent")

                // A left edge on the rows you can act on. Without it the only
                // way to find out which rows do something is to click every
                // one of them — which is how a settings app gets called a
                // read-only table.
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 2
                    color: dataRow.blocked ? root.cDim : root.cAccent
                    visible: dataRow.actionable || dataRow.blocked
                    opacity: dataRow.chosen ? 1.0 : 0.55
                }

                Row {
                    anchors { left: parent.left; right: parent.right
                              verticalCenter: parent.verticalCenter }
                    Repeater {
                        model: dataRow.modelData
                        delegate: Text {
                            required property var modelData
                            required property int index
                            width: headRow.colWidth(index)
                            visible: index !== root.actionCol
                            elide: Text.ElideRight
                            text: modelData
                            // One colour for the whole row when it is blocked.
                            // tone() would otherwise paint the VALUE green for
                            // "12-hour" on a row that cannot be changed —
                            // colour saying "good" on a dead control.
                            color: dataRow.blocked ? root.cDim
                                 : index === 0 ? root.cText
                                 : root.tone(root.cols[index] || "", modelData)
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                    }
                }
                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: dataRow.actionable ? Qt.PointingHandCursor
                                                    : Qt.ArrowCursor
                    onClicked: {
                        if (dataRow.actionable) root.selectRow(dataRow.index)
                        else {
                            root.selRow = -1; root.selAction = ""
                            // Clicking something greyed out should say why.
                            // The reason is in the detail column, which is the
                            // first thing to elide on a narrow window — which
                            // is exactly when somebody clicks it to find out.
                            if (dataRow.blocked)
                                root.status = "synui only — "
                                            + root.rowBlockedBy(dataRow.modelData)
                                            + " is the session running"
                        }
                    }
                }
            }
        }

        // Nothing to show is a state worth naming. An empty table that says
        // nothing looks identical to one that failed.
        Text {
            anchors.centerIn: table
            visible: !root.loading && root.rows.length === 0
            text: "This pane reported nothing.\nRun `" + root.bin + " --rec "
                  + root.pane + "` to see why."
            horizontalAlignment: Text.AlignHCenter
            color: root.cDim
            font { family: root.uiFont; pixelSize: root.ui(11) }
        }

        // ── Editor ──────────────────────────────────────────────────────────
        //
        // One strip rather than a control in every row. A table with a widget
        // per line is unreadable at six columns, and most rows here are facts
        // that cannot be set at all — a column of mostly-disabled buttons
        // would say less than an empty space does.
        Rectangle {
            id: editor
            anchors { left: nav.right; right: parent.right; bottom: statusBar.top }
            // Height follows the wrapped flow rather than a guessed constant:
            // how many lines eleven buttons take depends on the window width,
            // and a fixed height would clip the last row at some sizes and
            // leave a gap at others.
            height: root.selRow < 0 ? 0
                  : root.modeList.length > 0 ? Math.max(44, modeFlow.implicitHeight + 18)
                  : root.appList.length > 0 ? Math.max(44, appFlow.implicitHeight + 18)
                  : root.choiceList.length > 0 ? Math.max(44, choiceFlow.implicitHeight + 18)
                  : 44
            visible: height > 0
            clip: true
            color: root.cPanel

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: root.wash(0.25)
            }

            Text {
                id: editLabel
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                width: Math.min(implicitWidth, 200)
                elide: Text.ElideRight
                text: root.selKey
                color: root.cText
                font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
            }

            // A value to type: keymap, xkb layout, locale, time zone.
            Rectangle {
                id: editBox
                anchors { left: editLabel.right; leftMargin: 12
                          verticalCenter: parent.verticalCenter }
                width: 220; height: 26; radius: 4
                visible: root.actionHas(root.selAction, "set")
                color: root.cBg
                border { width: 1; color: editField.activeFocus ? root.cAccent : root.wash(0.25) }
                clip: true

                TextInput {
                    id: editField
                    anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                    selectByMouse: true
                    onAccepted: applyBtn.go()
                }
            }

            Row {
                anchors { left: editBox.visible ? editBox.right : editLabel.right
                          leftMargin: 12; verticalCenter: parent.verticalCenter }
                spacing: 8

                // set / toggle / probe all collapse to one button; only a unit
                // has several things you might do to it.
                SettingsButton {
                    id: applyBtn
                    visible: ["unit", "mode", "pkg", "device", "boot", "app", "choice",
                              "enroll", "forget"]
                             .indexOf(root.actionVerb(root.selAction)) < 0
                    label: {
                        const v = root.actionVerb(root.selAction)
                        if (v === "toggle") return root.isOn(root.selValue) ? "Turn off" : "Turn on"
                        if (v === "probe")  return "Re-probe"
                        return "Apply"
                    }
                    onGo: {
                        const v = root.actionVerb(root.selAction)
                        const arg = root.actionArg(root.selAction)
                        if (v === "set")
                            root.runWrite(["set", arg, editField.text], "setting " + arg + "…")
                        else if (v === "toggle")
                            root.runWrite(["set", arg, root.isOn(root.selValue) ? "off" : "on"],
                                          "switching " + arg + "…")
                        else if (v === "probe")
                            root.runWrite(["probe", arg], "re-probing " + arg + "…")
                    }
                }

                /*
                 * ⛔ A VERB THE QML DOES NOT KNOW IS A DEAD BUTTON — the row
                 * highlights, the strip opens, and nothing happens. `enroll`
                 * and `forget` are added here, to the exclusion list above, and
                 * to the test's allowlist, because all three are the contract.
                 *
                 * Enrolment TALKS while it runs: fprintd-enroll prints a line
                 * per swipe, and run_progress() folds them into the work panel,
                 * so this is one of the few writes where the panel is the point
                 * rather than reassurance.
                 */
                SettingsButton {
                    visible: root.actionHas(root.selAction, "enroll")
                    label: root.selValue === "enrolled" ? "Enrol again…" : "Enrol…"
                    onGo: root.runWrite(["enroll", root.actionArgFor(root.selAction, "enroll")],
                                        "rest your finger on the reader…",
                                        "lift and rest it again each time it asks; "
                                        + "several passes are needed")
                }

                // ⚠ ALL OF THEM. fprintd removes a user's prints together —
                // there is no per-finger delete — so the button says so rather
                // than looking like it acts on the selected row.
                SettingsButton {
                    visible: root.actionHas(root.selAction, "forget")
                    label: "Forget all fingerprints"
                    onGo: root.runWrite(["forget", "all"],
                                        "removing every fingerprint…")
                }

                // An interface: up or down. Wired included — a desktop whose
                // only link is ethernet had nothing to click before this.
                Repeater {
                    model: root.actionHas(root.selAction, "device")
                           ? ["connect", "disconnect"] : []
                    delegate: SettingsButton {
                        required property var modelData
                        label: modelData === "connect" ? "Connect" : "Disconnect"
                        onGo: root.runWrite(["device", modelData, root.actionArgFor(root.selAction, "device")],
                                            modelData + "ing " + root.actionArgFor(root.selAction, "device") + "…")
                    }
                }

                // An installed kernel the bootloader has never heard of. Goes
                // through the confirmation dialogue, never straight to a write.
                SettingsButton {
                    visible: root.actionHas(root.selAction, "boot")
                    label: "Make bootable…"
                    onGo: root.askBootable(root.actionArgFor(root.selAction, "boot"))
                }

                // Bootable and BOOTED are different things. A kernel with an
                // entry the loader never picks is a kernel you are not running,
                // and "catch the boot menu" is not a setting.
                SettingsButton {
                    visible: root.actionHas(root.selAction, "default")
                    label: "Make default…"
                    onGo: root.askDefault(root.actionArgFor(root.selAction, "default"))
                }

                // Install and Remove are now SEPARATE verbs, drawn only when
                // the row offers them: the C decides which of the two this
                // kernel can take, and a row can offer neither (the one you
                // booted) or both alongside "Make bootable…".
                //
                // "synpkg will ask to confirm" was a promise that could not be
                // kept: synpkg asks on a terminal, and there is no terminal
                // behind this button. The polkit challenge is the confirmation
                // the user actually gets.
                SettingsButton {
                    visible: root.actionHas(root.selAction, "install")
                    label: "Install"
                    onGo: root.runWrite(
                        ["pkg", "install", root.actionArgFor(root.selAction, "install")],
                        "Installing " + root.actionArgFor(root.selAction, "install"),
                        "Authorise when asked. A kernel and its headers are a few "
                        + "hundred megabytes to fetch and an initramfs to build, so "
                        + "this takes minutes, not seconds.")
                }

                SettingsButton {
                    visible: root.actionHas(root.selAction, "remove")
                    label: "Remove"
                    onGo: root.runWrite(
                        ["pkg", "remove", root.actionArgFor(root.selAction, "remove")],
                        "Removing " + root.actionArgFor(root.selAction, "remove"),
                        "Authorise when asked.")
                }

                Repeater {
                    model: root.actionHas(root.selAction, "unit")
                           ? ["enable", "disable", "start", "stop", "restart"] : []
                    delegate: SettingsButton {
                        required property var modelData
                        label: modelData.charAt(0).toUpperCase() + modelData.substring(1)
                        onGo: root.runWrite(["unit", modelData, root.actionArgFor(root.selAction, "unit")],
                                            modelData + " " + root.actionArgFor(root.selAction, "unit") + "…")
                    }
                }
            }

            // The modes this output can take, fetched when the row is picked
            // rather than carried in the table: a connector has a dozen of
            // them and they belong to the row you chose, not to every row.
            //
            // A Flow, not a Row. Eleven buttons at ~90px do not fit on one
            // line at any window width this app allows, and a Row would have
            // run them off the right edge — the same overflow that made
            // synfiles unreadable at 350px, which is a mistake worth not
            // repeating in the app built to fix it.
            Flow {
                id: modeFlow
                anchors {
                    left: editLabel.right; leftMargin: 12
                    right: closeBtn.left; rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                spacing: 6
                visible: root.modeList.length > 0

                Repeater {
                    model: root.modeList
                    delegate: SettingsButton {
                        required property var modelData
                        label: modelData.current ? modelData.mode + " ✓" : modelData.mode
                        onGo: root.runWrite(["mode", root.actionArgFor(root.selAction, "mode"), modelData.mode],
                                            "setting " + root.actionArgFor(root.selAction, "mode")
                                            + " to " + modelData.mode + "…")
                    }
                }
            }

            // One button per application that could take this role. A dropdown
            // would hide the count, and the count is the useful part: "nothing
            // else is installed" and "you have four browsers" are different
            // answers to "why can't I change this?".
            Flow {
                id: appFlow
                anchors {
                    left: editLabel.right; leftMargin: 12
                    right: closeBtn.left; rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                spacing: 6
                visible: root.appList.length > 0

                Repeater {
                    model: root.appList
                    delegate: SettingsButton {
                        required property var modelData
                        label: modelData.current ? modelData.name + " ✓" : modelData.name
                        onGo: root.runWrite(
                            ["set", "app", root.actionArgFor(root.selAction, "app"),
                             modelData.id],
                            "setting " + root.selKey + " to " + modelData.name + "…")
                    }
                }
            }

            // What this setting can be set to, one button each, with the
            // current one ticked. A text field would have been less code and
            // the wrong control: "dmy" is not a thing anybody knows to type,
            // and the reader already supplies both the name and a worked
            // example of what each choice looks like TODAY — which is the only
            // description of a date order that cannot be misread.
            Flow {
                id: choiceFlow
                anchors {
                    left: editLabel.right; leftMargin: 12
                    right: closeBtn.left; rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                spacing: 6
                visible: root.choiceList.length > 0

                Repeater {
                    model: root.choiceList
                    delegate: SettingsButton {
                        required property var modelData
                        label: modelData.current ? modelData.name + " ✓" : modelData.name
                        onGo: root.runWrite(
                            ["set", root.actionArgFor(root.selAction, "choice"),
                             modelData.id],
                            "setting " + root.selKey + " to " + modelData.name + "…")
                    }
                }
            }

            // Nothing came back. The desktop clock's layouts come from
            // synui-clock, so this is what a non-synui session sees — and an
            // empty strip beside a row you just clicked reads as a broken
            // control rather than as an answer.
            Text {
                anchors {
                    left: editLabel.right; leftMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                visible: root.actionHas(root.selAction, "choice")
                         && root.choiceList.length === 0
                text: "nothing offered this setting a list of choices"
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(11) }
            }

            // Nothing installed claims this role. Said out loud, because an
            // empty strip beside a row you just clicked reads as a broken
            // control rather than as an answer.
            Text {
                anchors {
                    left: editLabel.right; leftMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                visible: root.actionHas(root.selAction, "app")
                         && root.appList.length === 0
                text: "nothing installed offers to handle this"
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(11) }
            }

            SettingsButton {
                id: closeBtn
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                label: "Close"
                onGo: { root.selRow = -1; root.selAction = "" }
            }
        }

        // ── Status ──────────────────────────────────────────────────────────
        Rectangle {
            id: statusBar
            anchors { left: nav.right; right: parent.right; bottom: parent.bottom }
            height: 22
            color: root.cPanel

            // Along the very top edge of the bar, full width. Present for a
            // slow READ as well as a write: the Kernel pane asks pacman about
            // nine kernels and synpkg about a repository, which is seconds on a
            // cold cache, and "is it doing anything?" is the same question
            // whether or not the app is changing something.
            ProgressTrack {
                id: statusTrack
                anchors { left: parent.left; right: parent.right; top: parent.top }
                pct: root.applying ? root.progressPct : -1
                active: root.applying || root.loading
                visible: active
            }

            Text {
                id: statusLeft
                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                width: Math.min(implicitWidth, parent.width * 0.62 - 12)
                elide: Text.ElideRight
                // While a write runs, the newest line from the tool replaces
                // the note: the note said what was STARTED, and after a minute
                // of it the only useful question is what is happening now.
                text: root.applying
                        ? (root.applyNote
                           + (root.progressLine !== "" ? " — " + root.progressLine : ""))
                    : root.status !== "" ? root.status
                    // Ahead of "reading…" and the row count deliberately: the
                    // reload that follows a failed write must not be allowed to
                    // paint over the reason it failed.
                    : root.outcome !== "" ? root.outcome
                    : root.loading ? "reading…"
                    : root.rows.length + (root.rows.length === 1 ? " row" : " rows")
                color: root.applying ? root.cText
                     : (root.status !== "" || root.outcome !== "") ? root.cWarn
                     : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(10) }

                // Click it away once read. Nothing else dismisses it — that is
                // the point — but a message that cannot be cleared becomes
                // furniture and stops being read at all.
                MouseArea {
                    anchors.fill: parent
                    enabled: root.outcome !== "" && !root.applying
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.outcome = ""
                }
            }
            Text {
                anchors { left: statusLeft.right; leftMargin: 12
                          right: parent.right; rightMargin: 12
                          verticalCenter: parent.verticalCenter }
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                // The clock lives at this end, where nothing elides it away.
                // A number that changes every second is the difference between
                // "still going" and "stuck", and it is the one thing on screen
                // that keeps moving even when the tool has gone quiet.
                text: root.applying
                        ? (root.progressPct >= 0 ? root.progressPct + "%  ·  " : "")
                          + root.mmss(root.applyElapsed) + " elapsed"
                        : root.bin + " --rec " + root.pane
                color: root.applying ? root.cAccent : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(10) }
            }
        }

        // ── Confirm a boot-configuration change ─────────────────────────────
        //
        // Last in the file so it stacks above every other child without needing
        // a z-order anyone can accidentally out-bid later.
        Rectangle {
            id: confirmVeil
            anchors.fill: parent
            visible: root.confirmOpen
            color: Qt.rgba(0, 0, 0, 0.55)

            // Swallows every click and key that would otherwise reach the table
            // underneath. A modal you can click behind is not a modal, and the
            // thing behind this one changes kernels.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {}
            }

            Keys.onEscapePressed: root.confirmOpen = false
            focus: root.confirmOpen

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 560)
                height: confirmCol.implicitHeight + 36
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.wash(0.30) }

                Column {
                    id: confirmCol
                    anchors { left: parent.left; right: parent.right
                              top: parent.top; margins: 18 }
                    spacing: 10

                    Text {
                        text: "Change boot configuration?"
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: root.confirmVerb === "default"
                            ? "This makes " + root.confirmKernel + " the kernel this "
                              + "machine BOOTS. It changes nothing about the kernel you "
                              + "are running now; it takes effect at the next restart."
                            : "This makes " + root.confirmKernel + " bootable. It is the "
                              + "only change in this app that can leave a machine that does "
                              + "not start, so read what it will do first."
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    Grid {
                        columns: 2
                        rowSpacing: 4
                        columnSpacing: 10
                        width: parent.width

                        Text {
                            text: "Bootloader"
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                        Text {
                            width: confirmCol.width - 100
                            elide: Text.ElideRight
                            text: root.confirmPlan["loader"] || "-"
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(11); bold: true }
                        }

                        Text {
                            text: "Config"
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                        Text {
                            width: confirmCol.width - 100
                            elide: Text.ElideMiddle
                            text: root.confirmPlan["config"] || "-"
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: root.confirmPlan["why"] || ""
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    // The command, verbatim. Whoever is about to authorise a
                    // pkexec prompt is entitled to see exactly what it will run.
                    Rectangle {
                        width: parent.width
                        height: cmdText.implicitHeight + 16
                        radius: 4
                        color: root.cBg
                        border { width: 1; color: root.wash(0.20) }

                        Text {
                            id: cmdText
                            anchors { fill: parent; margins: 8 }
                            wrapMode: Text.WrapAnywhere
                            text: root.confirmPlan["command"] || ""
                            color: root.cText
                            font { family: "monospace"; pixelSize: root.ui(11) }
                        }
                    }

                    // WHAT THAT COMMAND THEN DOES, when it is this binary
                    // re-running itself as root. On grub that one line is
                    // three privileged acts — edit /etc/default/grub,
                    // regenerate grub.cfg, set the saved entry — and a
                    // dialogue that shows only the invocation is asking for
                    // approval of something it has not described. The binary
                    // produces these under --dry-run, so they are the steps
                    // that will actually run, not a retelling.
                    Column {
                        width: parent.width
                        spacing: 3
                        visible: root.confirmSteps.length > 0

                        Text {
                            text: "It will:"
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }

                        Repeater {
                            model: root.confirmSteps
                            delegate: Text {
                                required property var modelData
                                required property int index
                                width: confirmCol.width
                                wrapMode: Text.WrapAnywhere
                                text: "  " + (index + 1) + ". " + modelData
                                color: root.cText
                                font { family: "monospace"; pixelSize: root.ui(10) }
                            }
                        }
                    }

                    Item { width: 1; height: 2 }

                    Row {
                        spacing: 8
                        layoutDirection: Qt.RightToLeft
                        width: parent.width

                        SettingsButton {
                            label: "Cancel"
                            onGo: root.confirmOpen = false
                        }

                        SettingsButton {
                            label: root.confirmVerb === "default" ? "Make default"
                                                                  : "Make bootable"
                            onGo: {
                                root.confirmOpen = false
                                if (root.confirmVerb === "default")
                                    root.runWrite(["default", root.confirmKernel, "--confirm"],
                                                  "Making " + root.confirmKernel + " the default",
                                                  "You may be asked to authenticate.")
                                else
                                    root.runWrite(["boot", root.confirmKernel, "--confirm"],
                                                  "Making " + root.confirmKernel + " bootable",
                                                  "You may be asked to authenticate. On limine "
                                                  + "this can mean building the entry generator "
                                                  + "from source, which takes several minutes — "
                                                  + "the lines below are it working.")
                            }
                        }
                    }
                }
            }
        }

        // ── A write that is taking a while ──────────────────────────────────
        //
        // Deliberately LAST: it must sit above the confirm veil, which is the
        // thing it usually follows.
        //
        // It is not a modal for safety's sake — nothing underneath is clickable
        // while a write runs anyway. It is here because a dimmed button is not
        // an answer to "is this working?", and the honest answer — the tool's
        // own output, a clock, and a bar — needs more than a 22px strip.
        // Dismissable, because someone who wants to read the table underneath
        // should not have to wait out a kernel download to do it; the status
        // bar keeps every one of these numbers.
        Rectangle {
            id: workVeil
            anchors.fill: parent
            visible: root.workOpen && root.applying
            color: Qt.rgba(0, 0, 0, 0.45)

            MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: {} }
            Keys.onEscapePressed: root.workOpen = false
            focus: workVeil.visible

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 560)
                height: workCol.implicitHeight + 36
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.wash(0.30) }

                Column {
                    id: workCol
                    anchors { left: parent.left; right: parent.right
                              top: parent.top; margins: 18 }
                    spacing: 10

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: root.applyNote
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                    }

                    Text {
                        width: parent.width
                        visible: root.applyDetail !== ""
                        wrapMode: Text.WordWrap
                        text: root.applyDetail
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    ProgressTrack {
                        width: parent.width
                        pct: root.progressPct
                        active: true
                    }

                    Item {
                        width: parent.width
                        height: elapsedText.implicitHeight

                        Text {
                            id: elapsedText
                            anchors.left: parent.left
                            text: root.mmss(root.applyElapsed) + " elapsed"
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                        Text {
                            anchors.right: parent.right
                            visible: root.progressPct >= 0
                            text: root.progressPct + "%"
                            color: root.cAccent
                            font { family: root.uiFont; pixelSize: root.ui(11); bold: true }
                        }
                    }

                    // What the tool is actually saying, verbatim. Monospace and
                    // dim: it is evidence, not prose, and on an AUR build it is
                    // a compiler talking. Six lines, oldest dropped — enough to
                    // see movement, not so much that the panel resizes with
                    // every step.
                    Rectangle {
                        width: parent.width
                        visible: root.progressLog.length > 0
                        height: logCol.implicitHeight + 16
                        radius: 4
                        color: root.cBg
                        border { width: 1; color: root.wash(0.20) }

                        Column {
                            id: logCol
                            anchors { left: parent.left; right: parent.right
                                      top: parent.top; margins: 8 }
                            spacing: 2

                            Repeater {
                                model: root.progressLog
                                delegate: Text {
                                    required property var modelData
                                    required property int index
                                    width: logCol.width
                                    elide: Text.ElideRight
                                    text: modelData.text
                                    // The newest line is the one that is
                                    // happening; the rest are what led here.
                                    color: index === root.progressLog.length - 1
                                           ? root.cText : root.cDim
                                    font { family: "monospace"; pixelSize: root.ui(10) }
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Leave this running. Closing the window will not stop it — "
                            + "it will only stop you seeing how it went."
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10) }
                    }

                    Item { width: 1; height: 2 }

                    Row {
                        layoutDirection: Qt.RightToLeft
                        width: parent.width

                        SettingsButton {
                            label: "Hide"
                            ignoreBusy: true
                            onGo: root.workOpen = false
                        }
                    }
                }
            }
        }
    }
}
