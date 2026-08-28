// syn-disks — the SynapseOS disk utility.
//
// A renderer, and nothing more. Every fact on screen arrives as a record from
// `syn-disks --rec`; this file knows how to draw a table and a bar, and knows
// nothing about block devices. Every refusal is enforced in the binary, not
// here — a confirmation that lives only in a GUI is one that anything else
// calling the same binary skips for free.
//
// ── The one rule for reading records ───────────────────────────────────────
//
// EVERY field arrives percent-encoded, including the ones that look like plain
// words. A filesystem label is arbitrary bytes and a mount point is a path, so
// decoding "the ones that need it" means keeping a list that will drift, and
// the day it drifts a tab in a label shifts every column of a row and this
// window offers to format a device other than the one on screen.
//
// So: decode every field, once, at the parse. See disp().
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Quickshell
import Quickshell.Io
import QtQuick.Controls

FloatingWindow {
    id: root

    title: "SYNAPSE Disks"
    implicitWidth: 980
    implicitHeight: 660
    // The sidebar is a fixed 230 and the partition table has six columns.
    // Below this the table cannot hold its shape, and a layout with no floor
    // does not degrade — it paints over itself.
    minimumSize: Qt.size(720, 460)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    //
    // A close is a REQUEST, and while a disk is being written to the answer is
    // "not yet". Closing this window used to SIGKILL syn-disks and leave the
    // mkfs it had started dying of SIGPIPE part way through a filesystem; the
    // binary is now detached so that cannot happen, and this is the second
    // half — nothing is served by tearing down the window that is reporting an
    // operation still in flight. The click is not swallowed silently: the
    // overlay says why, and once the write finishes the next close just works.
    onClosed: {
        if (!root.busy) {
            Qt.quit()
            return
        }
        root.visible = true
        root.status = "still working — this window will close when the drive "
                    + "is finished with"
        closeGuard.restart()
    }

    // The one thing worse than a window that will not close is a window that
    // closed and left the process running with nothing on screen: every later
    // launch would exit 0 having drawn nothing. If the re-show above did not
    // take, give up on it and go — the write survives this either way, which is
    // the whole point of the detached runner in the binary.
    Timer {
        id: closeGuard
        interval: 400
        onTriggered: if (!root.visible) Qt.quit()
    }

    readonly property string bin: Quickshell.env("SYNDISKS_BIN") || "syn-disks"

    // ── Theme ───────────────────────────────────────────────────────────────
    //
    // Read from the desktop, not hardcoded, and the same source and shape as
    // synfiles, syn-settings and the bar — so a theme switch moves all of them
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
    // Meaning, not decoration — so these are held to the same contrast rule as
    // the accent rather than being fixed hexes that vanish on a pale theme.
    readonly property color cWarn: readable(pick("#e0af68", "#8a5a00"), cBg, 4.5)
    readonly property color cBad:  readable(pick("#f7768e", "#a01030"), cBg, 4.5)
    readonly property color cGood: readable(pick("#9ece6a", "#2f6f10"), cBg, 4.5)

    function wash(a) { return Qt.rgba(cAccent.r, cAccent.g, cAccent.b, a) }

    // ── The UI font ─────────────────────────────────────────────────────────
    // Qt resolves an application's default font ONCE at startup from the
    // platform theme and QML cannot change it, so every Text below names the
    // family and the name is a binding — otherwise this window keeps the old
    // face until it is reopened. Same file the bar and synfiles watch.
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


    // ── Records ─────────────────────────────────────────────────────────────

    // decodeURIComponent THROWS on a percent sequence that is not valid UTF-8,
    // and real filesystem labels are not always valid UTF-8 — a stick formatted
    // on a Windows machine in a CP1252 locale is the ordinary case. Showing the
    // raw encoded form is ugly; letting the exception escape empties the whole
    // window, which is how one odd label makes every disk disappear.
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

    // A "field: value" reader — what `info`, `smart` and a format dry-run emit.
    function parseFields(text) {
        const out = {}
        for (const r of root.parseRecords(text)) out[r.field] = r.value
        return out
    }

    // ⚠ A device path handed BACK to the binary is the decoded one.
    //
    // Everywhere else in this suite the encoded form is the identity, because
    // the thing being named is a filename and may hold any byte. A device path
    // cannot: it is always /dev/<kernel name>, and a kernel name is lowercase
    // letters, digits and hyphens. So encoded and decoded are the same string
    // here, and the binary — which does not decode its arguments — gets what it
    // expects either way. This is stated rather than left to be noticed, so
    // that nobody later "fixes" it in the direction that would break a label.
    function devOf(row) { return row.device }

    // ── State ───────────────────────────────────────────────────────────────
    property var drives: []
    property var parts: []
    // `table`: the partitions AND the free space between them, in on-disk
    // order. A separate reader from `parts` because they answer different
    // questions — `parts` describes what is ON the drive, nested volumes
    // included, and this describes the SHAPE of the drive, which is the only
    // one of the two that knows where a new partition could go. Free space is
    // stored by nobody and is derived in C; nothing here works it out.
    property var slots: []
    property string selDisk: ""      // /dev/… of the drive being shown
    property string selPart: ""      // /dev/… of the highlighted row, or ""
    // The free space selected in the bar, identified by its START OFFSET in
    // bytes and never by its index: a gap's position in the list changes the
    // moment anything else on the disk does, and an index kept across a
    // refresh would name a different piece of the drive than the one clicked.
    property string selGap: ""
    property bool loading: false
    property string status: ""
    property bool busy: false

    function driveRow(dev) {
        for (const d of root.drives) if (d.device === dev) return d
        return null
    }
    function partRow(dev) {
        for (const p of root.parts) if (p.device === dev) return p
        return null
    }
    // The table row for a device, which exists only for a REAL PARTITION of
    // this drive. An unlocked volume shows up in `parts` and is not a partition
    // — sfdisk cannot delete or resize /dev/mapper/cryptroot — so every
    // partitioning button asks this rather than `parts`.
    function slotOf(dev) {
        for (const s of root.slots)
            if (s.kind === "partition" && s.device === dev) return s
        return null
    }
    function gapAt(start) {
        for (const s of root.slots)
            if (s.kind === "free" && s.start === start) return s
        return null
    }
    readonly property var drive: root.driveRow(root.selDisk)
    readonly property var part: root.partRow(root.selPart)
    readonly property var slot: root.slotOf(root.selPart)
    readonly property var gap: root.gapAt(root.selGap)
    readonly property var largestGap: {
        let best = null
        for (const s of root.slots)
            if (s.kind === "free" && (!best || root.num(s.bytes) > root.num(best.bytes)))
                best = s
        return best
    }
    // How far the selected partition could grow: itself, plus the free space
    // immediately AFTER it and nothing else. Free space elsewhere on the drive
    // is unreachable without moving the partition, which is a copy of every
    // byte on it rather than a table edit. The binary works this out for
    // itself and refuses anything larger; this is only what the field opens on.
    readonly property real growMax: {
        if (!root.slot) return 0
        let hit = false
        let total = 0
        for (const s of root.slots) {
            if (hit) {
                if (s.kind === "free") total += root.num(s.bytes)
                break
            }
            if (s.kind === "partition" && s.device === root.selPart) {
                hit = true
                total = root.num(s.bytes)
            }
        }
        return total
    }

    function num(v) { const n = parseFloat(v); return isNaN(n) ? 0 : n }

    function human(bytes) {
        const u = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
        let v = root.num(bytes), i = 0
        while (v >= 1024 && i + 1 < u.length) { v /= 1024; i++ }
        return i === 0 ? Math.round(v) + " B" : v.toFixed(1) + " " + u[i]
    }

    // ── Loading ─────────────────────────────────────────────────────────────

    // The re-read runs at the END of every operation, so nothing in it may
    // overwrite what the operation just reported: ejecting the only removable
    // drive makes it VANISH from the list, and "no drives reported" landing on
    // top of "safe to unplug" turns a success into what reads like a fault.
    // These speak only into an empty bar — a manual refresh and picking a drive
    // both clear it first, so the reader still gets its say when it is the only
    // thing that has happened.
    function say(s) {
        if (root.status === "") root.status = s
    }

    Process {
        id: listProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.drives = root.parseRecords(this.text)
                root.loading = false
                if (root.drives.length === 0) {
                    root.say("no drives reported")
                    return
                }
                // Keep the current selection across a refresh when the drive is
                // still there; a refresh that jumps back to the first disk
                // loses your place every time anything is mounted.
                if (!root.driveRow(root.selDisk))
                    root.selDisk = root.drives[0].device
                root.loadParts()
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(root.oneLine(this.text))
        }
    }

    Process {
        id: partsProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.parts = root.parseRecords(this.text)
                // The highlighted partition is an identity, not an index: after
                // a mount the rows are rebuilt and row 2 may be a different
                // device. Kept by index, the action strip would end up pointed
                // at something nobody selected.
                if (!root.partRow(root.selPart)) root.selPart = ""
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.say(root.oneLine(this.text))
        }
    }

    // `table` exits 100 — "nothing to list" — for a drive with no partition
    // table, which is an ANSWER and not a failure. Nothing is said about it
    // here: the empty bar and the line under the table already say it, and a
    // status message would arrive on top of whatever operation just ran.
    Process {
        id: tableProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.slots = root.parseRecords(this.text)
                // The gap is an offset, and after any write the offsets move:
                // a gap that has been partitioned is not there any more, and
                // one that has grown starts somewhere else. Dropped unless the
                // re-read still describes the same free space.
                if (!root.gapAt(root.selGap)) root.selGap = ""
            }
        }
    }

    // Does NOT clear the status line. It is called at the END of every
    // operation to re-read the machine, and clearing here wiped the outcome a
    // few milliseconds after it was written — the success and the mkfs error
    // alike. Clearing belongs to the things that mean "new context": picking a
    // different drive, or asking for a refresh by hand.
    function reload() {
        root.loading = true
        listProc.command = [root.bin, "--rec", "list"]
        listProc.running = true
    }

    function loadParts() {
        root.parts = []
        root.slots = []
        root.health = ({})
        root.healthRows = []
        if (!root.selDisk) return
        partsProc.command = [root.bin, "--rec", "parts", root.selDisk]
        partsProc.running = true
        tableProc.command = [root.bin, "--rec", "table", root.selDisk]
        tableProc.running = true
    }

    function selectDisk(dev) {
        if (root.selDisk === dev) return
        root.selDisk = dev
        root.selPart = ""
        root.selGap = ""
        root.status = ""
        root.loadParts()
    }

    // A partition and a piece of free space are the same kind of selection —
    // the thing the action strip is pointed at — so choosing either drops the
    // other. Two live selections would leave "Delete" and "New" both lit, and
    // no way to tell which one a button was about to act on.
    function selectPart(dev) {
        root.selPart = dev
        root.selGap = ""
    }
    function selectGap(start) {
        root.selGap = start
        root.selPart = ""
    }

    // ── Acting ──────────────────────────────────────────────────────────────
    //
    // Never parses the tool's success message: the reader is the source of
    // truth for what the system now says, and believing our own report over a
    // re-read is how a disk utility starts showing a filesystem mounted
    // somewhere the kernel never mounted it.
    // The same three-event trap as the plan below, and it bit here too: the
    // handlers decided as they landed, and then onExited called reload() —
    // which cleared the status line. A format that WORKED and a format that
    // FAILED both ended with an empty status bar and a list that redrew
    // identically, so the only way to tell them apart was the journal. Two
    // real formats were run on the same stick because the first gave no sign
    // it had happened.
    //
    // So: the streams only STORE, resolveOp() is the one place the outcome is
    // decided, and reload() no longer speaks for the operation.
    property string opOut: ""
    property string opErr: ""
    property string opDone: ""      // what to say when the tool says "ok"
    // Taken from the argv, so the failure names the tool's OWN verb rather
    // than a second copy of it kept in step by hand.
    property string opVerb: ""
    property string opTarget: ""

    Process {
        id: actProc
        stdout: StdioCollector { onStreamFinished: root.opOut = this.text }
        stderr: StdioCollector { onStreamFinished: root.opErr = this.text }
        onExited: (code) => {
            root.busy = false
            root.resolveOp(code)
            root.reload()
            // Chained off the real exit, never off a timer: a re-plan run
            // while the unmount was still in flight would read the OLD state
            // and refuse again, which looks exactly like the button being
            // broken.
            if (root.replanAfterOp) {
                root.replanAfterOp = false
                root.planNow()
            }
        }
    }

    // A refusal is an answer, and so is a success. Every branch here ends with
    // root.status holding a sentence — there is no path that leaves it empty,
    // because empty is exactly what "it did nothing" looks like.
    function resolveOp(code) {
        const f = root.parseRecords(root.opOut)
        const r = f.length > 0 ? f[0] : null
        if (r && r.status === "ok") {
            // mkfs's own chatter is a version banner and "Done." — nothing a
            // user needs. The tool's verdict is the `status` field; the
            // sentence is ours, and the re-read below is what confirms it.
            root.status = root.opDone !== "" ? root.opDone : "done"
            return
        }
        // Failed. Keep the WHOLE detail: mkfs puts its reason on the last line
        // as often as the first, and picking one line is how "it is mounted"
        // arrived with its way out already thrown away. Newlines collapse so a
        // one-line bar can hold it; nothing is dropped.
        //
        // The binary now puts that last line FIRST, because keeping all of it
        // is not the same as showing any of it: this bar reported a
        // write-protected stick as "could not format /dev/sde — mke2fs 1.47.4
        // (6-Mar-2025) · Creating filesystem with 1792000 4k blocks…" and
        // elided the rest, so the whole detail was kept and none of the reason
        // was read.
        //
        // It is prefixed with what was being attempted, because a tool's own
        // words are rarely a verdict — mkfs failing halfway still opens with
        // its version banner, which alone reads like a success.
        const why = root.oneLine((r && r.detail) || root.opErr)
        const what = "could not " + root.opVerb
                   + (root.opTarget !== "" ? " " + root.opTarget : "")
        let said = why !== "" ? what + " — " + why
                 : (code !== 0 ? what + " (exit " + code + ")"
                               : what + " — the tool reported nothing")
        // A failure carries a `fix` code exactly as a refusal does, and it is
        // worth more than the tool's sentence: the switch on the body of a
        // stick is the answer, and no amount of mke2fs output says so. "none"
        // is not a way out — it is the absence of one, and printing "there is
        // nothing that overrides this" after every ordinary failure would be
        // noise on the one line there is.
        const hint = (r && r.fix && r.fix !== "none")
                   ? root.hintFor(r.fix, r.device || root.opTarget) : ""
        root.status = hint !== "" ? said + "\n" + hint : said
    }

    function oneLine(s) {
        return (s || "").replace(/\s*\n+\s*/g, " · ").trim()
    }

    function runOp(args, note, done) {
        if (root.busy) return
        root.busy = true
        root.status = note
        root.opOut = ""
        root.opErr = ""
        root.opDone = done || ""
        root.opVerb = args[0] || "do that"
        root.opTarget = (args.length > 1 && args[1].indexOf("-") !== 0) ? args[1] : ""
        actProc.command = [root.bin, "--rec"].concat(args)
        actProc.running = true
    }

    // ── Health ──────────────────────────────────────────────────────────────
    //
    // Read on request, never on open. SMART lives behind an ioctl on the raw
    // device that a desktop user cannot issue, so reading it on window open
    // would pop an authentication dialogue at somebody who wanted to see how
    // full a disk was.
    property var health: ({})
    property var healthRows: []
    property bool healthBusy: false

    Process {
        id: smartProc
        stdout: StdioCollector {
            onStreamFinished: {
                root.healthRows = root.parseRecords(this.text)
                root.health = root.parseFields(this.text)
                root.healthBusy = false
            }
        }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.status = root.oneLine(this.text)
        }
        onExited: root.healthBusy = false
    }

    function readHealth(elevate) {
        if (!root.selDisk || root.healthBusy) return
        // Pressing Health is its own request, so it may take the bar over — and
        // it clears first so smartctl's complaint has somewhere to land.
        root.status = ""
        root.healthBusy = true
        root.healthRows = []
        root.health = ({})
        const args = [root.bin, "--rec", "smart", root.selDisk]
        if (elevate) args.push("--elevate")
        smartProc.command = args
        smartProc.running = true
    }

    // ── The dialogues that change something ─────────────────────────────────
    //
    // Format, and the five partitioning operations, are ONE mechanism with
    // six bodies. Every one of them works the same way, and the way is the
    // whole point: the dialogue does NOT describe the change in its own words.
    // It runs the real command under --dry-run and shows what came back, so
    // what is approved and what then runs are produced by the same code path
    // and cannot drift apart. syn-settings does the same for changing the
    // bootloader, for the same reason.
    //
    // dlgArgs() is where that guarantee lives: one function builds the argv,
    // the dry run appends --dry-run to it and the confirm appends --yes. A
    // second builder for the second half would be a second thing to keep in
    // step by hand, and the day it drifted the window would show one command
    // and run another.
    //
    // The binary refuses without --yes regardless of what this file does, and
    // refuses outright for a mounted device, for anything sharing a disk with
    // "/", and — for partitioning — for anything the guard protects. So the
    // worst a bug here can do is fail to offer something, never destroy
    // something it should not have.
    property string dlg: ""          // "" | format | add | delete | resize | copy | table
    property string dlgDev: ""       // the device it is about
    property var plan: ({})

    // What the bodies collect. Shared across the dialogues that need them
    // rather than duplicated per kind: "which filesystem" is one question
    // however it was arrived at.
    property string opFs: "ext4"
    property string opLabel: ""
    property string opSize: ""       // add/resize; "" on add means the whole gap
    property string copyDst: ""      // copy: the destination partition
    // mktable. GPT unless somebody says otherwise: a DOS table is for a BIOS
    // machine or a device with firmware that has never heard of anything else,
    // and it cannot hold a partition past 2TB.
    property string opPtType: "gpt"

    readonly property var fsChoices: [
        { id: "ext4",  blurb: "Linux, journalled" },
        { id: "btrfs", blurb: "Linux, snapshots" },
        { id: "xfs",   blurb: "Linux, large files" },
        { id: "vfat",  blurb: "reads everywhere; no files over 4GB" },
        { id: "exfat", blurb: "reads nearly everywhere; large files" },
        { id: "ntfs",  blurb: "Windows" },
        // Only offered when MAKING a partition: an empty partition is a
        // perfectly good thing to make and then hand to cryptsetup, to LVM or
        // to an installer. Formatting something to "nothing" is not.
        { id: "none",  blurb: "no filesystem — an empty partition", addOnly: true }
    ]

    // THE argv. Everything else about a dialogue is presentation.
    function dlgArgs() {
        switch (root.dlg) {
        case "format": {
            const a = ["format", root.dlgDev, "--fs=" + root.opFs]
            if (root.opLabel !== "") a.push("--label=" + root.opLabel)
            return a
        }
        case "add": {
            const a = ["mkpart", root.selDisk]
            // The gap by OFFSET, so the partition lands in the free space that
            // was clicked. Without it the binary picks the largest gap, which
            // is the right default and the wrong answer to a click on a
            // different one.
            if (root.selGap !== "") a.push("--start=" + root.selGap)
            if (root.opSize !== "") a.push("--size=" + root.opSize)
            if (root.opFs !== "none") a.push("--fs=" + root.opFs)
            if (root.opFs !== "none" && root.opLabel !== "")
                a.push("--label=" + root.opLabel)
            return a
        }
        case "delete": return ["rmpart", root.dlgDev]
        case "resize": return ["resize", root.dlgDev, "--size=" + root.opSize]
        case "copy":   return ["copypart", root.dlgDev, root.copyDst]
        case "table":  return ["mktable", root.dlgDev, "--type=" + root.opPtType]
        }
        return []
    }

    readonly property string dlgTitle: {
        switch (root.dlg) {
        case "format": return "Erase " + root.dlgDev
        case "add":    return "New partition on " + root.selDisk
        case "delete": return "Delete " + root.dlgDev
        case "resize": return "Grow " + root.dlgDev
        case "copy":   return "Copy " + root.dlgDev
        case "table":  return "Partition table on " + root.dlgDev
        default:       return ""
        }
    }
    readonly property string dlgBlurb: {
        switch (root.dlg) {
        case "format": return "Everything on this device will be destroyed. There is no undo."
        case "add":    return root.gap
            ? "Into the " + root.human(root.gap.bytes) + " of free space selected below."
            : "Into the largest free space on this drive."
        case "delete": return "The partition and everything on it are destroyed. There is no undo."
        case "resize": return "It grows into the free space that follows it, and nothing else. "
                            + "The filesystem inside still ends where it does now — grow that "
                            + "afterwards, with the tool that belongs to it."
        case "copy":   return "Every byte of " + root.dlgDev + " is written over the partition "
                            + "you choose. Everything on that one is destroyed."
        case "table":  return "A drive needs one before it can hold partitions. Writing a new "
                            + "table discards every partition on this drive — and, on a drive "
                            + "formatted without one, the filesystem written straight onto it."
        default:       return ""
        }
    }
    readonly property string dlgGo: {
        switch (root.dlg) {
        case "format": return "Erase and format"
        case "add":    return "Create it"
        case "delete": return "Delete it"
        case "resize": return "Grow it"
        case "copy":   return "Copy over it"
        case "table":  return "Write the table"
        default:       return ""
        }
    }

    // The two streams and the exit are THREE events, and their order is not
    // guaranteed. This used to decide inside each handler as it landed, which
    // worked for a plan and failed for a refusal: on a refusal stdout is EMPTY,
    // so its handler reset the plan to {} — and any time it landed after
    // stderr's, it wiped the reason that had just been recorded. What was left
    // was a dialogue with no explanation and a button that would not light up,
    // which is the single outcome this dialogue exists to prevent.
    //
    // So each event only STORES its text, and resolvePlan() fills the plan in.
    // It never clears one that is already good, so order cannot matter.
    property string planOut: ""
    property string planErr: ""
    // ⚠ And a FOURTH event, which is the previous dialogue's dry run landing in
    // this one. There is one plan Process; ask it for a second plan while the
    // first is still in flight — closing a delete dialogue and opening a copy,
    // or simply typing another digit into a size — and the old run's streams
    // finish AFTER the new run has started, into the state the new one cleared.
    // Probed headlessly and it was not theoretical: the copy dialogue opened
    // showing `rmpart /dev/sda2` as the command it was about to run.
    //
    // So a plan is stamped with the request it answers, and a reply that does
    // not match the question now being asked is DROPPED rather than shown. Not
    // matched by prose: it is the argv, which is the same array the confirm
    // button would run.
    property string planAsked: ""
    function planStamp() { return root.dlg + " " + root.dlgArgs().join(" ") }

    Process {
        id: planProc
        stdout: StdioCollector {
            onStreamFinished: { root.planOut = this.text; root.resolvePlan(false) }
        }
        stderr: StdioCollector {
            onStreamFinished: { root.planErr = this.text; root.resolvePlan(false) }
        }
        onExited: root.resolvePlan(true)
    }

    function resolvePlan(final) {
        // An answer to a question nobody is asking any more.
        if (root.planAsked !== root.planStamp()) return

        const p = root.parseFields(root.planOut)
        // A refusal arrives as RECORDS, with a `fix` field naming the way out.
        // It used to come only on stderr, where the plan parser never looked.
        if (p["command"] !== undefined || p["refused"] !== undefined) {
            root.plan = p
            return
        }
        if (root.planErr) {
            // Something the binary reported on stderr instead — a size that is
            // not a size, a partition that is not one. The WHOLE text, not
            // split("\n")[0]: the second line is the way out, and dropping it
            // is what left somebody reading "it is mounted" with nothing to do
            // about it.
            root.plan = ({ refused: root.planErr.trim() })
            return
        }
        if (final)
            root.status = "could not work out what that would do"
    }

    // The way out, from the `fix` FIELD and never from the wording of the
    // sentence beside it. A window that decided whether to offer Unmount by
    // matching prose would stop offering it the day the prose improved.
    //
    // ONE table, asked by two callers: the dialogue that has not acted yet and
    // the status bar reporting an operation that has. A failed write carries a
    // `fix` exactly as a refusal does — a stick that lies about its
    // write-protect switch is only found out afterwards, and the answer is the
    // same sentence whichever side of the write it arrives on.
    readonly property string fixHint: root.hintFor(root.plan["fix"], root.fixDev)

    // `dev` is passed in rather than read from the plan: the status bar asks
    // this about an operation that has already run, and the plan by then
    // describes whatever dialogue is open — which is not necessarily the device
    // that just failed.
    function hintFor(code, dev) {
        switch (code) {
        case "unmount": return "It is mounted. Unmount it and this becomes possible."
        case "swapoff": return "Swap is live on it — run: swapoff " + dev
        case "lock":    return "A volume is unlocked on top of it; lock it first."
        case "fstab":   return "/etc/fstab expects this at the next boot."
        // The switch on the side of the stick, which is the answer nine times
        // out of ten and is not something software can undo. Said in full
        // because "read-only" on its own reads as a broken drive: this window
        // watched mke2fs report "Read-only file system while setting up
        // superblock" and had nothing better to offer than that sentence.
        case "readonly": return "The kernel says this device is write-protected. "
                              + "Many sticks and cards have a switch on the body — "
                              + "check that first if this one has one; no option "
                              + "here overrides it."
        // The same flag, found on the other side of a write, and NOT the same
        // answer. Nobody flips a switch halfway through a format, and this
        // stick had none to flip: it took the request, refused every sector,
        // and the kernel then re-read it as read-only. Saying "check the
        // switch" to that is an instruction to go looking for a part the
        // device does not have.
        case "latched": return "It accepted this and then refused every write, "
                             + "so the drive has switched itself read-only. That "
                             + "is a worn or over-reported flash chip, and "
                             + "nothing in software undoes it."
        case "mktable": return "There is no partition table to put a partition in."
        case "reread":  return "The drive has changed since this was read — refresh it."
        case "none":    return "There is nothing that overrides this."
        default:        return ""
        }
    }
    // The device the REFUSAL is about, which is not always the one the
    // dialogue is titled after: a copy is refused for its source or for its
    // destination, and unmounting the wrong one of the two would report
    // success and leave the dialogue refusing exactly as before.
    readonly property string fixDev: root.plan["device"] || root.dlgDev

    // Unmount, then work the plan out again, so the dialogue that just said
    // "it is mounted" becomes the dialogue that can act. Opening a stick from
    // Files mounts it, which is how most people arrive here — making them close
    // this, find the partition, unmount it and come back is a round trip for a
    // state the window already knows about. The binary still decides.
    property bool replanAfterOp: false
    function unmountForDialog() {
        if (!root.fixDev || root.busy) return
        root.replanAfterOp = true
        root.runOp(["unmount", root.fixDev], "unmounting " + root.fixDev + "…",
                   root.fixDev + " unmounted")
    }

    function askDialog(kind, dev) {
        root.dlg = kind
        root.dlgDev = dev
        root.plan = ({})
        root.opLabel = ""
        root.copyDst = ""
        // Each dialogue opens on the answer that is almost always right, and
        // the field is the way to disagree with it. An empty size on `add`
        // means the whole gap, which is what the binary does with no --size.
        root.opSize = kind === "resize" ? String(Math.floor(root.growMax)) : ""
        if (kind === "add" && root.opFs === "none") root.opFs = "ext4"
        if (kind === "copy") root.browseFor(root.selDisk)
        root.planNow()
    }

    function planNow() {
        const args = root.dlgArgs()
        root.plan = ({})
        root.planOut = ""
        root.planErr = ""
        // Nothing is pending, so nothing that arrives may be shown. Set before
        // every early return below, not only on the way out of the last one.
        root.planAsked = ""
        if (args.length === 0) return
        // Copy has no plan until there is something to copy onto. Running one
        // anyway would report "need a destination" as though the dialogue were
        // broken, in the place the command is supposed to appear.
        if (root.dlg === "copy" && root.copyDst === "") return
        root.planAsked = root.planStamp()
        planProc.command = [root.bin, "--rec"].concat(args).concat(["--dry-run"])
        planProc.running = true
    }

    function applyNow() {
        const args = root.dlgArgs()
        if (args.length === 0) return
        const kind = root.dlg
        const dev = root.dlgDev
        root.dlg = ""
        switch (kind) {
        case "format":
            root.runOp(args.concat(["--yes"]), "formatting " + dev + "…",
                       dev + " is now " + root.opFs
                       + (root.opLabel !== "" ? ", labelled " + root.opLabel : ""))
            break
        case "add":
            root.runOp(args.concat(["--yes"]), "making a partition on " + root.selDisk + "…",
                       "the partition was made on " + root.selDisk)
            break
        case "delete":
            root.runOp(args.concat(["--yes"]), "deleting " + dev + "…",
                       dev + " is gone")
            break
        case "resize":
            root.runOp(args.concat(["--yes"]), "growing " + dev + "…",
                       dev + " is bigger — the filesystem inside it is not yet")
            break
        case "copy":
            // No progress to report: dd says nothing until it is finished, and
            // a copy of a large partition takes minutes. The status line says
            // what is happening and the window is busy until it is not.
            root.runOp(args.concat(["--yes"]),
                       "copying " + dev + " onto " + root.copyDst + " — this can take a while…",
                       root.copyDst + " now holds a copy of " + dev)
            break
        case "table":
            root.runOp(args.concat(["--yes"]),
                       "writing a " + root.opPtType + " table on " + dev + "…",
                       dev + " has a new " + root.opPtType + " table — it is empty")
            break
        }
    }

    // Kept only so `syn-disks gui --format` still has the entry point it names.
    function askFormat(dev) { root.askDialog("format", dev) }

    // ── The copy destination ────────────────────────────────────────────────
    //
    // A destination is a partition on any drive in the machine, so this reads
    // the TABLE of whichever drive is being browsed for one — the same command
    // the main view uses, for the same reason: it carries `protected`, which is
    // the guard's own answer about destroying that partition. The list greys
    // out what cannot be a destination and says why, in the binary's words.
    property string copyDisk: ""
    property var dstSlots: []

    Process {
        id: dstProc
        stdout: StdioCollector {
            onStreamFinished: root.dstSlots = root.parseRecords(this.text)
        }
    }

    // The ONE way the destination drive changes — opening the dialogue and
    // clicking a chip both come through here. Driving it from a property
    // change instead meant re-opening the dialogue on the same drive read
    // nothing, and showed whatever that drive's table had been the last time.
    function browseFor(disk) {
        root.copyDisk = disk
        root.dstSlots = []
        if (!disk) return
        dstProc.command = [root.bin, "--rec", "table", disk]
        dstProc.running = true
    }

    // Why this partition cannot be the destination, or "" when it can. The
    // guard's sentence is used verbatim where there is one; the size is the
    // only thing checked here, and the binary checks it again.
    function dstWhyNot(row) {
        // Its own row FIRST. The source is very often protected as well, and
        // "it is mounted at /mnt/data" beside the partition somebody just
        // asked to copy reads as a fault in the destination list rather than
        // as the reason a thing cannot be copied onto itself.
        if (row.device === root.dlgDev) return "this is the partition being copied"
        if (row.protected) return row.protected
        const src = root.slotOf(root.dlgDev)
        if (src && root.num(row.bytes) < root.num(src.bytes))
            return "too small — needs " + root.human(src.bytes)
        return ""
    }

    property bool aboutOpen: false

    Component.onCompleted: {
        // syn-disks gui <device> resolves the argument and passes the DISK
        // here, having already turned a partition into the drive holding it —
        // so a right-click on a partition in the file manager opens the window
        // on its drive with that partition highlighted.
        const d = Quickshell.env("SYN_DISKS_DISK")
        const s = Quickshell.env("SYN_DISKS_SELECT")
        if (d) root.selDisk = "/dev/" + d
        if (s && s !== d) root.selPart = "/dev/" + s
        root.reload()

        // `syn-disks gui --format <device>` — the file manager's Format… entry
        // arriving with a device already in mind. It opens the same dialogue
        // the Format button opens, on the device that was named, so nothing
        // here can erase anything the user has not confirmed in it.
        //
        // The SELECT device, not the disk: --format is given a partition (a
        // stick's filesystem), and formatting the whole drive when asked about
        // one partition would be the worst possible reading of the request.
        // It falls back to the disk only when the argument WAS a whole drive,
        // in which case the two are the same device anyway.
        if (Quickshell.env("SYN_DISKS_FORMAT"))
            root.askFormat(root.selPart || root.selDisk)
    }

    // ── Small shared shapes ─────────────────────────────────────────────────
    //
    // QtQuick.Controls is deliberately not imported: this window is a list, a
    // table and four buttons, and a styled control set would drag its own
    // palette in alongside the theme this file just spent ninety lines
    // honouring.
    component Btn: Rectangle {
        id: btn
        property string label: ""
        property bool danger: false
        property bool enabled2: true
        signal go()
        width: btnText.implicitWidth + 22
        height: 26
        radius: 4
        color: !btn.enabled2 ? root.wash(0.05)
             : btnMa.containsMouse ? (btn.danger ? Qt.rgba(root.cBad.r, root.cBad.g, root.cBad.b, 0.28)
                                                 : root.wash(0.22))
             : (btn.danger ? Qt.rgba(root.cBad.r, root.cBad.g, root.cBad.b, 0.14) : root.wash(0.10))
        opacity: btn.enabled2 ? 1.0 : 0.45

        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: btn.danger ? root.cBad : root.cText
            font { family: root.uiFont; pixelSize: root.ui(11) }
        }
        MouseArea {
            id: btnMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: btn.enabled2 && !root.busy
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.go()
        }
    }

    // A drive, drawn rather than themed from the icon set. Qt resolves an
    // icon's colour once per process, so a themed SVG keeps the old accent
    // after a theme switch and vanishes entirely on a light one — the same
    // reason synfiles draws its folders.
    component DriveGlyph: Item {
        id: glyph
        property string kind: "hdd"
        property bool system: false
        width: 22; height: 22

        Rectangle {
            anchors.centerIn: parent
            width: 18
            height: glyph.kind === "usb-stick" || glyph.kind === "sd-card" ? 18 : 13
            radius: 2
            color: "transparent"
            border { width: 1.5; color: glyph.system ? root.cWarn : root.cAccent }

            // A platter for a spinning disk, a chip for flash, a hole for a
            // stick: three shapes that read at 18 pixels without a label.
            Rectangle {
                visible: glyph.kind === "hdd"
                anchors.centerIn: parent
                width: 5; height: 5; radius: 3
                color: glyph.system ? root.cWarn : root.cAccent
            }
            Rectangle {
                visible: glyph.kind === "ssd" || glyph.kind === "dm"
                anchors.centerIn: parent
                width: 9; height: 4
                color: glyph.system ? root.cWarn : root.cAccent
            }
            Rectangle {
                visible: glyph.kind === "usb-stick" || glyph.kind === "sd-card"
                anchors { horizontalCenter: parent.horizontalCenter
                          top: parent.top; topMargin: 3 }
                width: 7; height: 3
                color: glyph.system ? root.cWarn : root.cAccent
            }
            Rectangle {
                visible: glyph.kind === "optical"
                anchors.centerIn: parent
                width: 6; height: 6; radius: 3
                color: "transparent"
                border { width: 1.5; color: root.cAccent }
            }
        }
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // ── Drives ──────────────────────────────────────────────────────────
        Rectangle {
            id: nav
            anchors { top: parent.top; left: parent.left; bottom: parent.bottom }
            width: 230
            color: root.cPanel

            Text {
                id: navTitle
                x: 16
                y: 14
                text: "Disks"
                color: root.cAccent
                font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
            }

            ListView {
                // A view that scrolls says so — see SynScrollBar above.
                ScrollBar.vertical: SynScrollBar {}
                id: driveList
                anchors { top: navTitle.bottom; topMargin: 12
                          left: parent.left; right: parent.right
                          bottom: aboutBtn.top; bottomMargin: 8 }
                clip: true
                model: root.drives
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: dRow
                    required property var modelData
                    width: driveList.width
                    height: 52
                    readonly property bool chosen: dRow.modelData.device === root.selDisk
                    color: dRow.chosen ? root.wash(0.12)
                         : dMa.containsMouse ? root.wash(0.05) : "transparent"

                    Rectangle {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                        width: 3
                        color: root.cAccent
                        visible: dRow.chosen
                    }

                    DriveGlyph {
                        id: dGlyph
                        anchors { left: parent.left; leftMargin: 14
                                  verticalCenter: parent.verticalCenter }
                        kind: dRow.modelData.kind
                        system: dRow.modelData.system === "system"
                    }

                    Text {
                        id: dName
                        anchors { left: dGlyph.right; leftMargin: 10
                                  right: parent.right; rightMargin: 10
                                  top: parent.top; topMargin: 9 }
                        elide: Text.ElideRight
                        text: dRow.modelData.name
                        color: dRow.chosen ? root.cText : root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    Text {
                        anchors { left: dGlyph.right; leftMargin: 10
                                  right: parent.right; rightMargin: 10
                                  top: dName.bottom; topMargin: 2 }
                        elide: Text.ElideRight
                        text: dRow.modelData.size + " · " + dRow.modelData.bus
                              + (dRow.modelData.system === "system" ? " · system" : "")
                        color: dRow.modelData.system === "system" ? root.cWarn : root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10) }
                    }
                    MouseArea {
                        id: dMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.selectDisk(dRow.modelData.device)
                    }
                }
            }

            Btn {
                id: aboutBtn
                anchors { left: parent.left; leftMargin: 14; bottom: parent.bottom; bottomMargin: 12 }
                label: "About"
                onGo: root.aboutOpen = true
            }
        }

        // ── Header ──────────────────────────────────────────────────────────
        Item {
            id: header
            anchors { top: parent.top; left: nav.right; right: parent.right }
            height: 70

            Text {
                id: headTitle
                anchors { left: parent.left; leftMargin: 18; top: parent.top; topMargin: 14 }
                width: parent.width - 220
                elide: Text.ElideRight
                text: root.drive ? root.drive.name : "No drive selected"
                color: root.cText
                font { family: root.uiFont; pixelSize: root.ui(15); bold: true }
            }
            Text {
                anchors { left: parent.left; leftMargin: 18
                          right: refreshBtn.left; rightMargin: 12
                          top: headTitle.bottom; topMargin: 4 }
                elide: Text.ElideRight
                text: {
                    if (!root.drive) return ""
                    const d = root.drive
                    let s = d.device + " · " + d.size + " · " + d.kind + " · " + d.bus
                    if (d.table && d.table !== "none") s += " · " + d.table
                    if (d.serial) s += " · " + d.serial
                    return s
                }
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(10) }
            }

            Btn {
                id: healthBtn
                anchors { right: refreshBtn.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                label: root.healthBusy ? "reading…" : "Health"
                enabled2: root.drive !== null && !root.healthBusy
                onGo: root.readHealth(false)
            }
            Btn {
                id: refreshBtn
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                label: root.loading ? "reading…" : "Refresh"
                // Asking by hand IS the new context, so this is one of the two
                // places allowed to drop the last message.
                onGo: { root.status = ""; root.reload() }
            }
        }

        // ── The drive, drawn to scale ───────────────────────────────────────
        //
        // Only the top-level partitions take a slice: a volume unlocked INSIDE
        // one (depth > 0) occupies its container's space, not more of the disk,
        // and giving it its own slice would draw a 234GB disk as 469GB.
        Item {
            id: barBox
            anchors { top: header.bottom; left: nav.right; right: parent.right
                      leftMargin: 18; rightMargin: 18 }
            height: root.drive ? 46 : 0
            visible: height > 0
            // The slices below take a 6px floor so a 22MB EFI partition on a
            // 1TB disk is still clickable, and a floor means the widths no
            // longer necessarily sum to the bar. On a disk with a dozen tiny
            // partitions the row would otherwise run off the edge of the
            // window — a layout with a minimum has no ceiling unless one is
            // given to it.
            clip: true

            // Drawn from `table`, which is the drive's SHAPE: the partitions
            // and the free space between them, in on-disk order, each with the
            // offset it actually sits at.
            //
            // It used to be derived from `parts` — the partitions summed, and
            // whatever was left over drawn as one block of "unallocated" at the
            // END. That is wrong on any drive whose free space is in the
            // middle, which is every drive somebody has repartitioned, and it
            // is the picture a user would then click on to put a partition
            // somewhere. The gaps are where the binary says they are.
            readonly property var slices: {
                if (!root.drive) return []
                const total = root.num(root.drive.bytes)
                if (total <= 0) return []
                const out = []
                for (const s of root.slots) {
                    const b = root.num(s.bytes)
                    if (b <= 0) continue
                    out.push({ dev: s.device, start: s.start,
                               label: s.kind === "free" ? "free space"
                                    : (s.label || s.fstype || s.device),
                               frac: b / total, gap: s.kind === "free" })
                }
                // A drive with no partition table has no slots at all, and one
                // unallocated bar is the honest picture of it: nothing is
                // allocated on it.
                if (out.length === 0)
                    out.push({ dev: "", start: "", label: "unallocated",
                               frac: 1, gap: true })
                return out
            }

            Row {
                id: barRow
                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 2 }
                height: 30
                spacing: 2

                Repeater {
                    model: barBox.slices
                    delegate: Rectangle {
                        id: slice
                        required property var modelData
                        // The spacing has to come out of the slices or the row
                        // overflows its parent by (n-1)*spacing — which on a
                        // disk with ten partitions runs the last one off the
                        // edge of the window.
                        width: Math.max(6, (barRow.width - (barBox.slices.length - 1) * barRow.spacing)
                                           * slice.modelData.frac)
                        height: barRow.height
                        radius: 2
                        // Free space is SELECTABLE, and by its offset: it is
                        // the thing "New…" acts on, and a bar that showed the
                        // gaps but would not let one be picked would be a
                        // picture of the answer with no way to give it.
                        readonly property bool chosen: slice.modelData.gap
                            ? (slice.modelData.start !== ""
                               && slice.modelData.start === root.selGap)
                            : (slice.modelData.dev !== ""
                               && slice.modelData.dev === root.selPart)
                        color: slice.chosen ? root.wash(0.38)
                             : slice.modelData.gap
                               ? (sliceMa.containsMouse ? root.wash(0.10) : root.wash(0.04))
                               : sliceMa.containsMouse ? root.wash(0.22) : root.wash(0.13)
                        border { width: 1; color: slice.chosen ? root.cAccent : root.wash(0.18) }
                        clip: true

                        Text {
                            anchors { left: parent.left; leftMargin: 6
                                      right: parent.right; rightMargin: 4
                                      verticalCenter: parent.verticalCenter }
                            elide: Text.ElideRight
                            text: slice.modelData.label
                            color: slice.modelData.gap ? root.cDim : root.cText
                            font { family: root.uiFont; pixelSize: root.ui(10) }
                        }
                        MouseArea {
                            id: sliceMa
                            anchors.fill: parent
                            hoverEnabled: true
                            // The one unselectable slice is the whole-bar
                            // stand-in drawn for a drive with no table: there
                            // is no free space to name until there is a table
                            // to name it in.
                            enabled: !slice.modelData.gap || slice.modelData.start !== ""
                            cursorShape: Qt.PointingHandCursor
                            onClicked: slice.modelData.gap
                                     ? root.selectGap(slice.modelData.start)
                                     : root.selectPart(slice.modelData.dev)
                        }
                    }
                }
            }
        }

        // ── Column headings ─────────────────────────────────────────────────
        Item {
            id: headRow
            anchors { top: barBox.bottom; topMargin: 6
                      left: nav.right; right: parent.right
                      leftMargin: 18; rightMargin: 18 }
            height: root.parts.length > 0 ? 20 : 0
            clip: true

            readonly property var titles: ["Device", "Label", "Type", "Size", "Mounted at", "Used"]
            // Fixed fractions rather than content-derived widths, so the table
            // does not reflow on every refresh and make a changed value look
            // like a moved row.
            readonly property var weights: [0.20, 0.18, 0.11, 0.11, 0.26, 0.14]
            function colWidth(i) { return headRow.width * headRow.weights[i] }

            Row {
                anchors.fill: parent
                Repeater {
                    model: headRow.titles
                    delegate: Text {
                        required property var modelData
                        required property int index
                        width: headRow.colWidth(index)
                        elide: Text.ElideRight
                        text: modelData
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                    }
                }
            }
        }
        Rectangle {
            id: headRule
            anchors { top: headRow.bottom; left: nav.right; right: parent.right
                      leftMargin: 18; rightMargin: 18 }
            height: 1
            color: root.wash(0.12)
            visible: root.parts.length > 0
        }

        // ── Partitions ──────────────────────────────────────────────────────
        ListView {
            // A view that scrolls says so — see SynScrollBar above.
            ScrollBar.vertical: SynScrollBar {}
            id: table
            anchors {
                top: headRule.bottom; topMargin: 4
                left: nav.right; leftMargin: 18
                right: parent.right; rightMargin: 18
                bottom: actions.top; bottomMargin: 6
            }
            clip: true
            model: root.parts
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: pRow
                required property var modelData
                required property int index
                width: table.width
                height: 30
                readonly property bool chosen: pRow.modelData.device === root.selPart
                readonly property int depth: parseInt(pRow.modelData.depth) || 0

                color: pRow.chosen ? root.wash(0.20)
                     : pMa.containsMouse ? root.wash(0.08)
                     : (pRow.index % 2 === 1 ? root.wash(0.02) : "transparent")

                Row {
                    anchors { left: parent.left; right: parent.right
                              verticalCenter: parent.verticalCenter }

                    // A nested volume is indented under the container holding
                    // it, so "this LUKS partition, opened, is that btrfs" reads
                    // as one fact instead of two unrelated rows.
                    Item {
                        width: headRow.colWidth(0)
                        height: 16
                        Text {
                            anchors { left: parent.left; leftMargin: pRow.depth * 14
                                      right: parent.right; verticalCenter: parent.verticalCenter }
                            elide: Text.ElideRight
                            text: (pRow.depth > 0 ? "└ " : "") + pRow.modelData.device
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                    }
                    Text {
                        width: headRow.colWidth(1)
                        elide: Text.ElideRight
                        text: pRow.modelData.label || "—"
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }
                    Text {
                        width: headRow.colWidth(2)
                        elide: Text.ElideRight
                        text: pRow.modelData.fstype || "—"
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }
                    Text {
                        width: headRow.colWidth(3)
                        elide: Text.ElideRight
                        text: pRow.modelData.size
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }
                    Text {
                        width: headRow.colWidth(4)
                        elide: Text.ElideRight
                        text: pRow.modelData.mounts || "not mounted"
                        color: pRow.modelData.mounts ? root.cGood : root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    // A meter, and only when there is something to measure.
                    // "used 0 of 0" drawn as an empty bar is indistinguishable
                    // from an empty disk, and the two mean opposite things —
                    // an unmounted filesystem cannot be measured at all.
                    Item {
                        width: headRow.colWidth(5)
                        height: 16
                        readonly property real total: root.num(pRow.modelData.total)
                        readonly property real used: root.num(pRow.modelData.used)

                        Rectangle {
                            anchors { left: parent.left; right: parent.right
                                      rightMargin: 8; verticalCenter: parent.verticalCenter }
                            height: 6
                            radius: 3
                            visible: parent.total > 0
                            color: root.wash(0.10)

                            Rectangle {
                                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                width: parent.width * Math.min(1, parent.parent.used / parent.parent.total)
                                radius: 3
                                // Over 90% is the point at which a disk starts
                                // causing problems rather than merely being
                                // full, so it says so in a colour.
                                color: (parent.parent.used / parent.parent.total) > 0.9
                                       ? root.cBad : root.cAccent
                            }
                        }
                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            visible: parent.total <= 0
                            text: "—"
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                    }
                }
                MouseArea {
                    id: pMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selectPart(pRow.modelData.device)
                }
            }
        }

        // Nothing to show is a state worth naming: an empty table that says
        // nothing looks identical to one that failed.
        Text {
            anchors.centerIn: table
            visible: !root.loading && root.parts.length === 0 && root.drive !== null
            horizontalAlignment: Text.AlignHCenter
            text: "This drive has no partition table.\nNothing is allocated on it."
            color: root.cDim
            font { family: root.uiFont; pixelSize: root.ui(11) }
        }

        // ── Health, when it has been asked for ──────────────────────────────
        Rectangle {
            id: healthPanel
            anchors.centerIn: parent
            width: 420
            height: Math.min(parent.height - 60, healthCol.implicitHeight + 60)
            radius: 6
            color: root.cPanel
            border { width: 1; color: root.wash(0.35) }
            visible: root.healthRows.length > 0
            z: 120

            Text {
                id: healthTitle
                anchors { top: parent.top; left: parent.left; margins: 16 }
                text: "Drive health"
                color: root.cAccent
                font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
            }

            Column {
                id: healthCol
                anchors { top: healthTitle.bottom; topMargin: 10
                          left: parent.left; right: parent.right
                          leftMargin: 16; rightMargin: 16 }
                spacing: 3

                Repeater {
                    model: root.healthRows
                    delegate: Item {
                        required property var modelData
                        width: healthCol.width
                        height: modelData.field === "retry" ? 0 : 20
                        visible: height > 0

                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            width: 150
                            elide: Text.ElideRight
                            text: parent.modelData.field
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                        Text {
                            anchors { left: parent.left; leftMargin: 150; right: parent.right
                                      verticalCenter: parent.verticalCenter }
                            elide: Text.ElideRight
                            text: parent.modelData.value
                            color: parent.modelData.value === "FAILING" ? root.cBad
                                 : parent.modelData.value === "healthy" ? root.cGood
                                 : root.cText
                            font {
                                family: root.uiFont
                                pixelSize: root.ui(11)
                                bold: parent.modelData.field === "health"
                            }
                        }
                    }
                }
            }

            Row {
                anchors { right: parent.right; bottom: parent.bottom; margins: 14 }
                spacing: 8

                // The binary says whether authorising would help, rather than
                // this file guessing what a refusal meant.
                Btn {
                    label: "Authorise and retry"
                    visible: root.health["retry"] === "elevate"
                    onGo: root.readHealth(true)
                }
                Btn {
                    label: "Close"
                    onGo: { root.healthRows = []; root.health = ({}) }
                }
            }
        }

        // ── About ───────────────────────────────────────────────────────────
        Rectangle {
            anchors.centerIn: parent
            width: 420
            height: aboutCol.implicitHeight + 70
            radius: 6
            color: root.cPanel
            border { width: 1; color: root.wash(0.35) }
            visible: root.aboutOpen
            z: 125

            Column {
                id: aboutCol
                anchors { top: parent.top; left: parent.left; right: parent.right
                          margins: 18 }
                spacing: 6

                Text {
                    text: "SYNAPSE Disks"
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: root.ui(15); bold: true }
                }
                Text {
                    width: aboutCol.width
                    wrapMode: Text.WordWrap
                    text: "Reads the storage tree from the kernel. Mounting and "
                        + "safe removal go through udisks2, health through "
                        + "smartmontools, and formatting through polkit — each "
                        + "of which does its own authorisation."
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
                Item { width: 1; height: 4 }
                Text {
                    width: aboutCol.width
                    wrapMode: Text.WordWrap
                    text: "Formatting anything on the disk holding this running "
                        + "system is refused, and there is no override."
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
                Item { width: 1; height: 4 }
                Text {
                    text: "Support: buymeacoffee.com/velle999"
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: root.ui(11); underline: true }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally("https://buymeacoffee.com/velle999")
                    }
                }
            }
            Btn {
                anchors { right: parent.right; bottom: parent.bottom; margins: 14 }
                label: "Close"
                onGo: root.aboutOpen = false
            }
        }

        // ── The dialogue ────────────────────────────────────────────────────
        //
        // One window for format, add, delete, resize, copy and the table
        // itself. Each shows the
        // real dry run at the bottom, and the button is offered only when that
        // dry run came back with a command — so a refusal is a dialogue that
        // explains itself rather than a button that does nothing.
        MouseArea {
            anchors.fill: parent
            visible: root.dlg !== ""
            z: 129
            onClicked: root.dlg = ""
        }
        Rectangle {
            anchors.centerIn: parent
            width: 480
            height: Math.min(root.height - 40, dlgCol.implicitHeight + 76)
            radius: 6
            color: root.cPanel
            border { width: 1; color: root.cBad }
            visible: root.dlg !== ""
            z: 130

            Column {
                id: dlgCol
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 18 }
                spacing: 8

                Text {
                    width: dlgCol.width
                    elide: Text.ElideRight
                    text: root.dlgTitle
                    color: root.cBad
                    font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                }
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    text: root.dlgBlurb
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }

                Item { width: 1; height: 2 }

                // ── Filesystem: format, and a new partition ─────────────────
                Text {
                    visible: root.dlg === "format" || root.dlg === "add"
                    text: "Filesystem"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Flow {
                    width: dlgCol.width
                    spacing: 6
                    visible: root.dlg === "format" || root.dlg === "add"
                    Repeater {
                        model: root.fsChoices
                        delegate: Rectangle {
                            id: fsChip
                            required property var modelData
                            // "no filesystem" is a partition you can make and
                            // not a filesystem you can write, so the chip is
                            // not there at all when formatting.
                            visible: !fsChip.modelData.addOnly || root.dlg === "add"
                            width: fsChip.visible ? fsText.implicitWidth + 18 : 0
                            height: fsChip.visible ? 24 : 0
                            radius: 4
                            readonly property bool chosen: fsChip.modelData.id === root.opFs
                            color: fsChip.chosen ? root.wash(0.30)
                                 : fsMa.containsMouse ? root.wash(0.14) : root.wash(0.06)
                            border { width: 1; color: fsChip.chosen ? root.cAccent : "transparent" }

                            Text {
                                id: fsText
                                anchors.centerIn: parent
                                text: fsChip.modelData.id
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                            MouseArea {
                                id: fsMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.opFs = fsChip.modelData.id; root.planNow() }
                            }
                        }
                    }
                }
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.dlg === "format" || root.dlg === "add"
                    text: {
                        for (const c of root.fsChoices) if (c.id === root.opFs) return c.blurb
                        return ""
                    }
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }

                // ── Table type: mktable ─────────────────────────────────────
                Text {
                    visible: root.dlg === "table"
                    text: "Kind of table"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Flow {
                    width: dlgCol.width
                    spacing: 6
                    visible: root.dlg === "table"
                    Repeater {
                        model: [
                            { id: "gpt", blurb: "the modern one — any size, up to 128 partitions" },
                            { id: "dos", blurb: "for old BIOS machines and devices that expect it; "
                                              + "nothing past 2TB" }
                        ]
                        delegate: Rectangle {
                            id: ptChip
                            required property var modelData
                            width: ptText.implicitWidth + 18
                            height: 24
                            radius: 4
                            readonly property bool chosen: ptChip.modelData.id === root.opPtType
                            color: ptChip.chosen ? root.wash(0.30)
                                 : ptMa.containsMouse ? root.wash(0.14) : root.wash(0.06)
                            border { width: 1; color: ptChip.chosen ? root.cAccent : "transparent" }

                            Text {
                                id: ptText
                                anchors.centerIn: parent
                                text: ptChip.modelData.id
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                            MouseArea {
                                id: ptMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.opPtType = ptChip.modelData.id; root.planNow() }
                            }
                        }
                    }
                }
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.dlg === "table"
                    text: root.opPtType === "gpt"
                        ? "the modern one — any size, up to 128 partitions"
                        : "for old BIOS machines and devices that expect it; nothing past 2TB"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }

                // ── Size: a new partition, and a resize ─────────────────────
                Text {
                    visible: root.dlg === "add" || root.dlg === "resize"
                    text: root.dlg === "add" ? "Size (blank fills the free space)" : "New size"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Rectangle {
                    visible: root.dlg === "add" || root.dlg === "resize"
                    width: 240; height: 26; radius: 4
                    color: root.cBg
                    border { width: 1; color: sizeField.activeFocus ? root.cAccent : root.wash(0.25) }
                    clip: true

                    TextInput {
                        id: sizeField
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                        selectByMouse: true
                        // Follows the property rather than owning it: the
                        // dialogue opens `resize` on the largest size that
                        // will fit, and a field that only ever wrote to the
                        // property would open empty and lose that answer.
                        text: root.opSize
                        onTextChanged: if (text !== root.opSize) { root.opSize = text; root.planNow() }
                    }
                }
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.dlg === "add" || root.dlg === "resize"
                    text: "20G, 512MiB or a plain number of bytes. IEC suffixes are powers of "
                        + "1024; KB, MB and GB are powers of 1000."
                        + (root.dlg === "resize" && root.growMax > 0
                           ? " Up to " + root.human(root.growMax) + " here."
                           : "")
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }

                // ── Label: format, and a new partition being formatted ──────
                Text {
                    visible: root.dlg === "format" || (root.dlg === "add" && root.opFs !== "none")
                    text: "Label (optional)"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Rectangle {
                    visible: root.dlg === "format" || (root.dlg === "add" && root.opFs !== "none")
                    width: 240; height: 26; radius: 4
                    color: root.cBg
                    border { width: 1; color: labelField.activeFocus ? root.cAccent : root.wash(0.25) }
                    clip: true

                    TextInput {
                        id: labelField
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                        selectByMouse: true
                        // The binary is the boundary for what a label may hold;
                        // this only keeps the preview in step with the typing.
                        onTextChanged: { root.opLabel = text; root.planNow() }
                    }
                }

                // ── Destination: copy ───────────────────────────────────────
                Text {
                    visible: root.dlg === "copy"
                    text: "Copy it onto"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Flow {
                    width: dlgCol.width
                    spacing: 6
                    visible: root.dlg === "copy"
                    Repeater {
                        model: root.drives
                        delegate: Rectangle {
                            id: dChip
                            required property var modelData
                            width: dChipText.implicitWidth + 18
                            height: 24
                            radius: 4
                            readonly property bool chosen: dChip.modelData.device === root.copyDisk
                            color: dChip.chosen ? root.wash(0.30)
                                 : dChipMa.containsMouse ? root.wash(0.14) : root.wash(0.06)
                            border { width: 1; color: dChip.chosen ? root.cAccent : "transparent" }

                            Text {
                                id: dChipText
                                anchors.centerIn: parent
                                text: dChip.modelData.device
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                            MouseArea {
                                id: dChipMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.browseFor(dChip.modelData.device)
                            }
                        }
                    }
                }
                // Every partition of that drive, with the guard's own sentence
                // beside the ones that cannot be written. Greyed rather than
                // hidden: "why can I not pick my other disk" is a question the
                // window should answer, and a missing row answers nothing.
                Column {
                    width: dlgCol.width
                    spacing: 2
                    visible: root.dlg === "copy"

                    Repeater {
                        model: root.dstSlots
                        delegate: Rectangle {
                            id: dstRow
                            required property var modelData
                            visible: dstRow.modelData.kind === "partition"
                            width: dlgCol.width
                            height: dstRow.visible ? 26 : 0
                            radius: 3
                            readonly property string whyNot: root.dstWhyNot(dstRow.modelData)
                            readonly property bool chosen: dstRow.modelData.device === root.copyDst
                            color: dstRow.chosen ? root.wash(0.30)
                                 : (dstRow.whyNot === "" && dstMa.containsMouse) ? root.wash(0.14)
                                 : root.wash(0.04)
                            border { width: 1; color: dstRow.chosen ? root.cAccent : "transparent" }

                            Text {
                                anchors { left: parent.left; leftMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                text: dstRow.modelData.device + "  "
                                    + root.human(dstRow.modelData.bytes)
                                color: dstRow.whyNot === "" ? root.cText : root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                            Text {
                                anchors { right: parent.right; rightMargin: 8
                                          left: parent.horizontalCenter
                                          verticalCenter: parent.verticalCenter }
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                                text: dstRow.whyNot
                                color: root.cWarn
                                font { family: root.uiFont; pixelSize: root.ui(10) }
                            }
                            MouseArea {
                                id: dstMa
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: dstRow.whyNot === ""
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.copyDst = dstRow.modelData.device; root.planNow() }
                            }
                        }
                    }
                }
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.dlg === "copy" && root.copyDst === ""
                    text: "Nothing on this drive can take it. A destination has to be a "
                        + "partition that already exists and is at least as large — make one "
                        + "with New… first."
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }

                Item { width: 1; height: 4 }

                // What will actually run, produced by the same code path that
                // would run it. Not a sentence this file composed.
                Text {
                    text: root.plan["refused"] ? "This is not allowed:" : "This will run:"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Rectangle {
                    width: dlgCol.width
                    height: planText.implicitHeight + 12
                    radius: 3
                    color: root.cBg

                    Text {
                        id: planText
                        anchors { fill: parent; margins: 6 }
                        wrapMode: Text.WrapAnywhere
                        text: root.plan["refused"] ? root.plan["refused"]
                            : root.plan["command"] ? root.plan["command"]
                            : (root.dlg === "copy" && root.copyDst === "")
                              ? "choose a destination above"
                              : "working it out…"
                        color: root.plan["refused"] ? root.cBad : root.cText
                        font { family: "monospace"; pixelSize: root.ui(10) }
                    }
                }
                // What it destroys, in the binary's words rather than this
                // file's. `delete` and `copy` have no other body, and a
                // dialogue whose only sentence is one the window made up is
                // the one place this design does not allow.
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.plan["destroys"] !== undefined
                    text: root.plan["destroys"] || ""
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.plan["blocked"] !== undefined
                    text: root.plan["blocked"] || ""
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }
                // `warn` is NOT `blocked` and deliberately leaves the button
                // live: this kernel being unable to mount exFAT is no reason to
                // stop somebody making a stick for a camera, and a copy
                // carrying the source's UUID is no reason to stop somebody
                // replacing a disk. It is here so that what follows is not a
                // mystery — which is exactly how the first one was met, as an
                // apparently bad format.
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.plan["warn"] !== undefined
                    text: root.plan["warn"] || ""
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }
                Text {
                    width: dlgCol.width
                    wrapMode: Text.WordWrap
                    visible: root.fixHint !== ""
                    text: root.fixHint
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }
            }

            Row {
                anchors { right: parent.right; bottom: parent.bottom; margins: 14 }
                spacing: 8

                Btn {
                    label: "Cancel"
                    onGo: root.dlg = ""
                }
                Btn {
                    label: "Unmount it"
                    visible: root.plan["fix"] === "unmount"
                    onGo: root.unmountForDialog()
                }
                // The other way out with a button on it. Unlike Unmount this
                // one is destructive, so it does NOT act — it moves this
                // dialogue onto mktable, where the same dry run, the same
                // warning and the same confirmation apply. A refusal whose
                // only remedy was a command line was a dead end in a window:
                // "Make one first: syn-disks mktable …" is a fine sentence in
                // a terminal and no help at all to somebody holding a mouse.
                Btn {
                    label: "Make a partition table…"
                    visible: root.plan["fix"] === "mktable"
                    onGo: root.askDialog("table", root.selDisk)
                }
                Btn {
                    label: root.dlgGo
                    danger: true
                    // Offered only when the dry run came back with a command
                    // and nothing blocking it. The binary refuses regardless;
                    // this stops the button existing to be pressed.
                    enabled2: root.plan["command"] !== undefined
                              && root.plan["refused"] === undefined
                              && root.plan["blocked"] === undefined
                    onGo: root.applyNow()
                }
            }
        }

        // ── Actions ─────────────────────────────────────────────────────────
        Rectangle {
            id: actions
            anchors { left: nav.right; right: parent.right; bottom: statusBar.top }
            // Two rows of 26px buttons with room to breathe. It was 44 for one
            // row; a second row added without the height would have been drawn
            // outside the strip, over the table.
            height: 74
            color: root.cPanel

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: root.wash(0.25)
            }

            // What the buttons are pointed at — a partition, or a piece of free
            // space. Naming it is the difference between "Delete" and "delete
            // WHAT", on a strip where every button is destructive.
            Text {
                id: selLabel
                anchors { left: parent.left; leftMargin: 18; top: parent.top; topMargin: 8 }
                width: 200
                elide: Text.ElideRight
                text: root.part ? root.part.device
                    : root.gap ? "free space · " + root.human(root.gap.bytes)
                    : "select a partition"
                color: (root.part || root.gap) ? root.cText : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(12)
                       bold: root.part !== null || root.gap !== null }
            }
            // The guard's own sentence about the selected partition, from
            // `table`'s `protected` column — the SAME call the refusal will
            // make. It is here so that "why is this about to be refused" is
            // answered before the dialogue is opened, and in the same words.
            Text {
                anchors { left: parent.left; leftMargin: 18; top: selLabel.bottom; topMargin: 2 }
                width: 200
                elide: Text.ElideRight
                visible: root.slot !== null && root.slot.protected !== ""
                text: root.slot ? root.slot.protected : ""
                color: root.cWarn
                font { family: root.uiFont; pixelSize: root.ui(9) }
            }

            // Two rows, because they are two different kinds of act: the top
            // one changes what the machine is DOING with a partition, and the
            // bottom one changes the partition table itself.
            Row {
                anchors { left: selLabel.right; leftMargin: 14; top: parent.top; topMargin: 7 }
                spacing: 8

                Btn {
                    label: "Mount"
                    enabled2: root.part !== null && root.part.mounts === ""
                    onGo: root.runOp(["mount", root.devOf(root.part)],
                                     "mounting " + root.part.device + "…",
                                     root.part.device + " mounted")
                }
                Btn {
                    label: "Unmount"
                    enabled2: root.part !== null && root.part.mounts !== ""
                    onGo: root.runOp(["unmount", root.devOf(root.part)],
                                     "unmounting " + root.part.device + "…",
                                     root.part.device + " unmounted")
                }
                Btn {
                    label: "Eject drive"
                    // Ejecting the drive the system is running from is a
                    // request the hardware will refuse and the user will read
                    // as a bug. It is not offered.
                    enabled2: root.drive !== null && root.drive.system !== "system"
                    onGo: root.runOp(["eject", root.devOf(root.drive)],
                                     "powering down " + root.drive.device + "…",
                                     root.drive.device + " is safe to unplug")
                }
                Btn {
                    label: "Format…"
                    danger: true
                    enabled2: root.part !== null
                    onGo: root.askDialog("format", root.devOf(root.part))
                }
            }

            // ⚠ These are enabled for PROTECTED partitions on purpose.
            //
            // The guard's answer belongs in the dialogue, where it arrives as a
            // sentence naming what is in the way and — where there is one — a
            // button that clears it. A greyed-out Delete says only "no", which
            // is the state this window spent a day in when a refusal went to a
            // stream nothing was reading. What IS greyed out here is what has
            // no meaning at all: deleting nothing, or a new partition on a
            // drive with no free space.
            Row {
                anchors { left: selLabel.right; leftMargin: 14
                          bottom: parent.bottom; bottomMargin: 7 }
                spacing: 8

                // mktable has existed in the binary since partitioning landed
                // and had no button, so the one drive shape that needs it most
                // — a stick formatted with no table at all — could be told
                // what was wrong and given no way to fix it. It acts on the
                // DRIVE, so it is enabled whenever one is selected; the guard
                // refuses the system disk, in the dialogue, in its own words.
                Btn {
                    label: "Partition table…"
                    danger: true
                    enabled2: root.selDisk !== ""
                    onGo: root.askDialog("table", root.selDisk)
                }
                Btn {
                    label: "New…"
                    danger: true
                    // The selected gap if there is one, and otherwise the
                    // largest — which is what the binary does when nothing
                    // names a gap, so the two agree.
                    enabled2: root.largestGap !== null
                    onGo: root.askDialog("add", root.selDisk)
                }
                Btn {
                    label: "Delete…"
                    danger: true
                    // `slot`, not `part`: an unlocked volume shows in the table
                    // and is not a partition, and sfdisk cannot delete one.
                    enabled2: root.slot !== null
                    onGo: root.askDialog("delete", root.selPart)
                }
                Btn {
                    label: "Resize…"
                    danger: true
                    enabled2: root.slot !== null
                    onGo: root.askDialog("resize", root.selPart)
                }
                Btn {
                    label: "Copy…"
                    danger: true
                    enabled2: root.slot !== null
                    onGo: root.askDialog("copy", root.selPart)
                }
            }
        }

        // ── Status ──────────────────────────────────────────────────────────
        //
        // It GROWS for an answer that does not fit. A 22-pixel bar with
        // ElideRight is right for "sde unmounted" and wrong for the only report
        // a failed format gets: the reason ran off the end of it, and what was
        // left on screen was a version banner. A bar that can hold three lines
        // costs nothing when there is one line to say.
        Rectangle {
            id: statusBar
            anchors { left: nav.right; right: parent.right; bottom: parent.bottom }
            height: Math.max(22, statusText.implicitHeight + 8)
            color: root.cPanel

            Text {
                id: statusText
                anchors { left: parent.left; leftMargin: 18; right: parent.right
                          rightMargin: 12; verticalCenter: parent.verticalCenter }
                // Wrapped and capped: a tool that prints a page of output must
                // not push the buttons off the window. What is beyond three
                // lines is beyond reading in a status bar anyway — and the
                // reason is on the first of them now, not the last.
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                text: root.status !== "" ? root.status
                    : root.drives.length + " drive" + (root.drives.length === 1 ? "" : "s")
                color: root.status !== "" ? root.cText : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(10) }
            }
        }

        // ── Working ─────────────────────────────────────────────────────────
        //
        // "I couldn't tell it was doing anything." A format is seconds to
        // minutes of writing, and the only thing this window changed while it
        // ran was a ten-pixel grey line at the bottom and some greyed-out
        // buttons — neither of which reads as "in progress" on a window that
        // looks idle. Somebody closed it part way through a format, reasonably,
        // because nothing said not to.
        //
        // It is over everything and it takes the mouse: while a disk is being
        // written to, there is nothing else in here worth clicking.
        Rectangle {
            anchors.fill: parent
            visible: root.busy
            color: root.isLight ? Qt.rgba(1, 1, 1, 0.72) : Qt.rgba(0, 0, 0, 0.62)

            MouseArea { anchors.fill: parent; hoverEnabled: true }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 80, 420)
                height: busyText.implicitHeight + 44
                color: root.cPanel
                border { width: 1; color: root.cAccent }
                radius: 2

                Column {
                    id: busyText
                    anchors { left: parent.left; right: parent.right
                              verticalCenter: parent.verticalCenter
                              leftMargin: 20; rightMargin: 20 }
                    spacing: 8

                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.status !== "" ? root.status : "working…"
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        // True as of the detached runner in the binary, and only
                        // because of it: closing this window used to SIGKILL
                        // syn-disks, and the mkfs it had started then died of
                        // SIGPIPE part way through writing a filesystem.
                        text: "The drive is being written to. This window stays "
                            + "until it finishes, and closing it could not stop "
                            + "the write in any case — leave the drive plugged in."
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10) }
                    }
                }
            }
        }
    }
}
