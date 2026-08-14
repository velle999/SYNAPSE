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
    onClosed: Qt.quit()

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
    readonly property color cAccent: readable(themed("accent", 78, 201, 176, 1.0), cPanel, 4.5)
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
    property string selDisk: ""      // /dev/… of the drive being shown
    property string selPart: ""      // /dev/… of the highlighted row, or ""
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
    readonly property var drive: root.driveRow(root.selDisk)
    readonly property var part: root.partRow(root.selPart)

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
        root.health = ({})
        root.healthRows = []
        if (!root.selDisk) return
        partsProc.command = [root.bin, "--rec", "parts", root.selDisk]
        partsProc.running = true
    }

    function selectDisk(dev) {
        if (root.selDisk === dev) return
        root.selDisk = dev
        root.selPart = ""
        root.status = ""
        root.loadParts()
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
                root.planFormat()
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
        // It is prefixed with what was being attempted, because a tool's own
        // words are rarely a verdict — mkfs failing halfway still opens with
        // its version banner, which alone reads like a success.
        const why = root.oneLine((r && r.detail) || root.opErr)
        const what = "could not " + root.opVerb
                   + (root.opTarget !== "" ? " " + root.opTarget : "")
        root.status = why !== "" ? what + " — " + why
                    : (code !== 0 ? what + " (exit " + code + ")"
                                  : what + " — the tool reported nothing")
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

    // ── Formatting ──────────────────────────────────────────────────────────
    //
    // Two steps, and the dialogue does NOT describe the change in its own
    // words: it runs the real thing under --dry-run and shows what came back,
    // so what is approved and what then runs are produced by the same code
    // path and cannot drift apart. syn-settings does the same for changing the
    // bootloader, for the same reason.
    //
    // The binary refuses without --yes regardless of what this file does, and
    // refuses outright for a mounted device or anything sharing a disk with
    // "/" — so the worst a bug here can do is fail to offer something, never
    // erase something it should not have.
    property bool fmtOpen: false
    property string fmtDev: ""
    property string fmtFs: "ext4"
    property string fmtLabel: ""
    property var fmtPlan: ({})

    readonly property var fsChoices: [
        { id: "ext4",  blurb: "Linux, journalled" },
        { id: "btrfs", blurb: "Linux, snapshots" },
        { id: "xfs",   blurb: "Linux, large files" },
        { id: "vfat",  blurb: "reads everywhere; no files over 4GB" },
        { id: "exfat", blurb: "reads nearly everywhere; large files" },
        { id: "ntfs",  blurb: "Windows" }
    ]

    // The two streams and the exit are THREE events, and their order is not
    // guaranteed. This used to decide inside each handler as it landed, which
    // worked for a plan and failed for a refusal: on a refusal stdout is EMPTY,
    // so its handler reset fmtPlan to {} — and any time it landed after
    // stderr's, it wiped the reason that had just been recorded. What was left
    // was a dialogue with no explanation and a Format button that would not
    // light up, which is the single outcome this dialogue exists to prevent.
    //
    // So each event only STORES its text, and resolvePlan() fills the plan in.
    // It never clears one that is already good, so order cannot matter.
    property string planOut: ""
    property string planErr: ""

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
        const plan = root.parseFields(root.planOut)
        // A refusal now arrives as RECORDS, with a `fix` field naming the way
        // out. It used to come only on stderr, where the plan parser never
        // looked.
        if (plan["command"] !== undefined || plan["refused"] !== undefined) {
            root.fmtPlan = plan
            return
        }
        if (root.planErr) {
            // Something the binary reported on stderr instead — a name that is
            // not a block device, say. The WHOLE text, not split("\n")[0]:
            // the second line is the way out, and dropping it is what left
            // somebody reading "it is mounted" with nothing to do about it.
            root.fmtPlan = ({ refused: root.planErr.trim() })
            return
        }
        if (final)
            root.status = "could not work out what that would do"
    }

    // The way out, from the `fix` FIELD and never from the wording of the
    // sentence beside it. A window that decided whether to offer Unmount by
    // matching prose would stop offering it the day the prose improved.
    readonly property string fixHint: {
        switch (root.fmtPlan["fix"]) {
        case "unmount": return "It is mounted. Unmount it and this becomes possible."
        case "swapoff": return "Swap is live on it — run: swapoff " + root.fmtDev
        case "lock":    return "A volume is unlocked on top of it; lock it first."
        case "fstab":   return "/etc/fstab expects this at the next boot."
        case "none":    return "There is nothing that overrides this."
        default:        return ""
        }
    }

    // Unmount, then work the plan out again, so the dialogue that just said
    // "it is mounted" becomes the dialogue that can format it. Opening a stick
    // from Files mounts it, which is how most people arrive here — making them
    // close this, find the partition, unmount it and come back is a round trip
    // for a state the window already knows about. The binary still decides.
    property bool replanAfterOp: false
    function unmountForFormat() {
        if (!root.fmtDev || root.busy) return
        root.replanAfterOp = true
        root.runOp(["unmount", root.fmtDev], "unmounting " + root.fmtDev + "…",
                   root.fmtDev + " unmounted")
    }

    function askFormat(dev) {
        root.fmtDev = dev
        root.fmtLabel = ""
        root.fmtPlan = ({})
        root.fmtOpen = true
        root.planFormat()
    }

    function planFormat() {
        if (!root.fmtDev) return
        root.fmtPlan = ({})
        root.planOut = ""
        root.planErr = ""
        const args = [root.bin, "--rec", "format", root.fmtDev,
                      "--fs=" + root.fmtFs, "--dry-run"]
        if (root.fmtLabel !== "") args.push("--label=" + root.fmtLabel)
        planProc.command = args
        planProc.running = true
    }

    function doFormat() {
        const args = ["format", root.fmtDev, "--fs=" + root.fmtFs, "--yes"]
        if (root.fmtLabel !== "") args.push("--label=" + root.fmtLabel)
        root.fmtOpen = false
        root.runOp(args, "formatting " + root.fmtDev + "…",
                   root.fmtDev + " is now " + root.fmtFs
                   + (root.fmtLabel !== "" ? ", labelled " + root.fmtLabel : ""))
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

            readonly property var slices: {
                if (!root.drive) return []
                const total = root.num(root.drive.bytes)
                if (total <= 0) return []
                const out = []
                let covered = 0
                for (const p of root.parts) {
                    if (p.depth !== "0") continue
                    const b = root.num(p.bytes)
                    if (b <= 0) continue
                    out.push({ dev: p.device, label: p.label || p.fstype || p.device,
                               frac: b / total, gap: false })
                    covered += b
                }
                // What is left over. On a disk with no partition table this is
                // the whole bar, which is the honest picture: a drive with
                // nothing allocated on it.
                const left = 1 - covered / total
                if (left > 0.005)
                    out.push({ dev: "", label: "unallocated", frac: left, gap: true })
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
                        readonly property bool chosen: slice.modelData.dev !== ""
                                                       && slice.modelData.dev === root.selPart
                        color: slice.modelData.gap ? root.wash(0.04)
                             : slice.chosen ? root.wash(0.38)
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
                            enabled: !slice.modelData.gap
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selPart = slice.modelData.dev
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
                    onClicked: root.selPart = pRow.modelData.device
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

        // ── The format dialogue ─────────────────────────────────────────────
        MouseArea {
            anchors.fill: parent
            visible: root.fmtOpen
            z: 129
            onClicked: root.fmtOpen = false
        }
        Rectangle {
            anchors.centerIn: parent
            width: 480
            height: fmtCol.implicitHeight + 76
            radius: 6
            color: root.cPanel
            border { width: 1; color: root.cBad }
            visible: root.fmtOpen
            z: 130

            Column {
                id: fmtCol
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 18 }
                spacing: 8

                Text {
                    text: "Erase " + root.fmtDev
                    color: root.cBad
                    font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                }
                Text {
                    width: fmtCol.width
                    wrapMode: Text.WordWrap
                    text: "Everything on this device will be destroyed. There is no undo."
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }

                Item { width: 1; height: 2 }

                Text {
                    text: "Filesystem"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Flow {
                    width: fmtCol.width
                    spacing: 6
                    Repeater {
                        model: root.fsChoices
                        delegate: Rectangle {
                            id: fsChip
                            required property var modelData
                            width: fsText.implicitWidth + 18
                            height: 24
                            radius: 4
                            readonly property bool chosen: fsChip.modelData.id === root.fmtFs
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
                                onClicked: { root.fmtFs = fsChip.modelData.id; root.planFormat() }
                            }
                        }
                    }
                }
                Text {
                    width: fmtCol.width
                    wrapMode: Text.WordWrap
                    text: {
                        for (const c of root.fsChoices) if (c.id === root.fmtFs) return c.blurb
                        return ""
                    }
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }

                Item { width: 1; height: 2 }

                Text {
                    text: "Label (optional)"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Rectangle {
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
                        onTextChanged: { root.fmtLabel = text; root.planFormat() }
                    }
                }

                Item { width: 1; height: 4 }

                // What will actually run, produced by the same code path that
                // would run it. Not a sentence this file composed.
                Text {
                    text: root.fmtPlan["refused"] ? "This is not allowed:" : "This will run:"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
                }
                Rectangle {
                    width: fmtCol.width
                    height: planText.implicitHeight + 12
                    radius: 3
                    color: root.cBg

                    Text {
                        id: planText
                        anchors { fill: parent; margins: 6 }
                        wrapMode: Text.WrapAnywhere
                        text: root.fmtPlan["refused"] ? root.fmtPlan["refused"]
                            : root.fmtPlan["command"] ? root.fmtPlan["command"]
                            : "working it out…"
                        color: root.fmtPlan["refused"] ? root.cBad : root.cText
                        font { family: "monospace"; pixelSize: root.ui(10) }
                    }
                }
                Text {
                    width: fmtCol.width
                    wrapMode: Text.WordWrap
                    visible: root.fmtPlan["blocked"] !== undefined
                    text: root.fmtPlan["blocked"] || ""
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }
                // `warn` is NOT `blocked` and deliberately leaves the button
                // live: this kernel being unable to mount exFAT is no reason to
                // stop somebody making a stick for a camera. It is here so the
                // mount error afterwards is not a mystery — which is exactly
                // how it was met the first time, as an apparently bad format.
                Text {
                    width: fmtCol.width
                    wrapMode: Text.WordWrap
                    visible: root.fmtPlan["warn"] !== undefined
                    text: root.fmtPlan["warn"] || ""
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                }
                Text {
                    width: fmtCol.width
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
                    onGo: root.fmtOpen = false
                }
                Btn {
                    label: "Unmount it"
                    visible: root.fmtPlan["fix"] === "unmount"
                    onGo: root.unmountForFormat()
                }
                Btn {
                    label: "Erase and format"
                    danger: true
                    // Offered only when the dry run came back with a command
                    // and nothing blocking it. The binary refuses regardless;
                    // this stops the button existing to be pressed.
                    enabled2: root.fmtPlan["command"] !== undefined
                              && root.fmtPlan["refused"] === undefined
                              && root.fmtPlan["blocked"] === undefined
                    onGo: root.doFormat()
                }
            }
        }

        // ── Actions ─────────────────────────────────────────────────────────
        Rectangle {
            id: actions
            anchors { left: nav.right; right: parent.right; bottom: statusBar.top }
            height: 44
            color: root.cPanel

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: root.wash(0.25)
            }

            Text {
                id: selLabel
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                width: Math.min(implicitWidth, 220)
                elide: Text.ElideRight
                text: root.part ? root.part.device : "select a partition"
                color: root.part ? root.cText : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(12); bold: root.part !== null }
            }

            Row {
                anchors { left: selLabel.right; leftMargin: 14
                          verticalCenter: parent.verticalCenter }
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
                    onGo: root.askFormat(root.devOf(root.part))
                }
            }
        }

        // ── Status ──────────────────────────────────────────────────────────
        Rectangle {
            id: statusBar
            anchors { left: nav.right; right: parent.right; bottom: parent.bottom }
            height: 22
            color: root.cPanel

            Text {
                anchors { left: parent.left; leftMargin: 18; right: parent.right
                          rightMargin: 12; verticalCenter: parent.verticalCenter }
                elide: Text.ElideRight
                text: root.status !== "" ? root.status
                    : root.drives.length + " drive" + (root.drives.length === 1 ? "" : "s")
                color: root.status !== "" ? root.cText : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(10) }
            }
        }
    }
}
