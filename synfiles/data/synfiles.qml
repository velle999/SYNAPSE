//@ pragma UseQApplication
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * SYNAPSE Files — the graphical front-end for synfiles.
 *
 * Every fact on screen comes from `synfiles --rec <command>`. Nothing here
 * stats a file, reads a bookmark or knows what a mount is; it renders records.
 *
 * ── The one rule that matters ───────────────────────────────────────────────
 *
 * Names and paths arrive PERCENT-ENCODED, because a filename is arbitrary
 * bytes and may contain tabs, newlines, quotes and sequences that are not
 * valid UTF-8. A plain tab-separated name would shift every later column; a
 * name containing a newline would arrive as two rows.
 *
 * So: `row.name` is the IDENTITY and is what goes back to the binary.
 *     `disp(row.name)` is for DRAWING and must never be handed back.
 *
 * decodeURIComponent() throws on byte sequences that are not valid UTF-8 —
 * which real filenames are — so disp() catches and degrades instead of
 * letting one undecodable name blank the whole pane.
 *
 * The palette block is lifted from synpkg's unchanged, so the two windows
 * cannot drift apart on a theme change.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
FloatingWindow {
    id: root

    title: "SYNAPSE Files"
    implicitWidth: 1240
    implicitHeight: 780

    // Below this the layout cannot hold its shape and it fails UGLY rather than
    // tight: the sidebar is a fixed 220, the toolbar's nav group and action
    // group are fixed, and the address bar in between is anchored
    // left:navGroup.right / right:toolActions.left — so once the window is
    // narrow enough that those two meet, the address bar's width goes NEGATIVE
    // and its breadcrumbs paint straight over the buttons on both sides.
    // Screenshot 2026-08-10 10:58 caught exactly that: back/forward/up, "View",
    // the whole path, and the three action icons all stacked in one 130px strip.
    //
    // 560 = sidebar 220 + a pane wide enough to show a filename. clip and
    // elide (below, and on the status bar) stop it looking broken on the way
    // down; this stops it getting there at all.
    minimumSize: Qt.size(560, 360)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNFILES_BIN") || "synfiles"
    readonly property string homeDir: Quickshell.env("HOME") || "/"

    // ── Palette ─────────────────────────────────────────────────────────────
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
    readonly property color cDim: pick("#8b93a7", "#4a5568")
    readonly property color cAccentRaw: root.wpAccent !== ""
                                        ? Qt.color(root.wpAccent)
                                        : themed("accent", 78, 201, 176, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)
    readonly property color cWarn: pick("#e0af68", "#5c3a00")
    // Folders take the theme accent, lightened or darkened only as far as it
    // takes to stay visible against the view background — a pale accent on a
    // pale theme would otherwise draw folder-shaped holes.
    readonly property color cFolder: readable(cAccentRaw, cBg, 2.0)

    function wash(a) { return Qt.rgba(cAccent.r, cAccent.g, cAccent.b, a) }

    // ── The UI font ─────────────────────────────────────────────────────────
    //
    // Its own file, watched, exactly as the bar watches it (synui's
    // quickshell/Theme.qml): font.state is written by synui-apply-font(1) and
    // outlives a theme switch, which is why it is not a key in theme.json.
    // Empty means the shipped default — an empty family string is Qt's own way
    // of saying "whatever the platform picked".
    //
    // A window that kept the old face until it was reopened is exactly what
    // velle reported, and the reason is worth stating: Qt resolves the default
    // font ONCE at startup from the platform theme, and nothing in QML can
    // change an application's font afterwards. So every Text here names the
    // family, and the name is a binding.
    property string uiFont: ""

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        // No font.state is the normal case on a box where nobody has picked
        // one. A warning per start for an expected miss is how a log becomes
        // something nobody reads.
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = this.text()
            const m = t.match(/^\s*family\s*=\s*(.+?)\s*$/m)
            root.uiFont = m ? m[1] : ""
            // The text scale moved OUT of synfiles' own settings and into the
            // desktop font state, because it was never a property of this
            // window: synfiles ran at 115% while syn-settings and syn-disks sat
            // at 100, which reads as the theming having missed those apps.
            //
            // This wins over the config key whenever the file carries it, and
            // it says so with a flag rather than by arriving later — the config
            // is read by a Process and this by a FileView, so which lands first
            // is a race, and "last writer wins" would make the setting depend
            // on disk timing.
            const sc = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            if (sc) {
                root.scaleFromDesktop = true
                root.textScale = parseInt(sc[1])
            }
        }
        onLoadFailed: root.uiFont = ""
    }

    // ── Text size ───────────────────────────────────────────────────────────
    // One multiplier over every pixelSize in the window, so the View menu's
    // slider moves all of it together and nothing drifts out of proportion.
    // Stored as a percentage because the settings file holds integers.
    property int textScale: 100
    // Whether font.state supplied the scale. See the FileView above: this makes
    // the precedence explicit instead of leaving it to whichever of two
    // asynchronous loads happens to finish last.
    property bool scaleFromDesktop: false
    property int configScale: 100
    property bool scaleMigrated: false
    readonly property int textMin: 75
    readonly property int textMax: 175
    function ui(px) { return Math.max(6, Math.round(px * root.textScale / 100)) }

    // ── Encoding ────────────────────────────────────────────────────────────

    // Decode for DISPLAY ONLY. Never feed the result back to the binary.
    function disp(enc) {
        if (!enc) return ""
        try {
            return decodeURIComponent(enc)
        } catch (e) {
            // A filename that is not valid UTF-8. Showing the escaped form is
            // ugly and unambiguous, which beats showing nothing — and the row
            // still works, because every action uses the encoded string.
            return enc
        }
    }

    // Join an encoded directory path with an encoded child name. Both sides
    // stay encoded, so the result is still an identity and never needs
    // re-encoding — the step where a "%" in a real filename would become
    // "%25" a second time.
    // How much of a name the inline rename SELECTS. Typing replaces the
    // selection, so this is what decides whether a rename keeps the extension.
    //
    // ⛔ selectAll() HERE COSTS THE EXTENSION. Renaming `tux95.png` to `tux95`
    // silently produced an extensionless file: still a perfectly good PNG, and
    // invisible to everything that dispatches on the suffix — synui's wallpaper
    // picker filters the folder by extension (wppick.c) and its thumbnailer
    // chooses a decoder the same way (wpthumb.c), so the picture vanished from
    // the wallpaper list and previewed as nothing, with no error anywhere.
    //
    // The extension stays out of the selection, the way every other file
    // manager does it — visible, editable by moving the cursor, but not
    // destroyed by typing.
    //
    // ⚠ A FOLDER HAS NO EXTENSION. Dots in `.config` or `My.Stuff` are part of
    // the name, so a directory selects whole.
    // ⚠ A LEADING DOT IS NOT A SEPARATOR either — `.bashrc` is all stem, which
    // is why this tests `dot > 0` and not `dot >= 0`.
    // Last dot, not first: `archive.tar.gz` offers `archive.tar`.
    function stemLen(name, isDir) {
        if (isDir) return name.length
        var dot = name.lastIndexOf(".")
        return dot > 0 ? dot : name.length
    }

    function joinEnc(dirEnc, nameEnc) {
        if (dirEnc === "/") return "/" + nameEnc
        return dirEnc + "/" + nameEnc
    }

    function parentEnc(dirEnc) {
        if (!dirEnc || dirEnc === "/") return "/"
        const i = dirEnc.lastIndexOf("/")
        if (i <= 0) return "/"
        return dirEnc.substring(0, i)
    }

    function baseEnc(pathEnc) {
        if (!pathEnc || pathEnc === "/") return "/"
        const i = pathEnc.lastIndexOf("/")
        return i < 0 ? pathEnc : pathEnc.substring(i + 1)
    }

    function encodePath(raw) {
        // encodeURIComponent escapes "/" too, which a path needs to keep.
        return raw.split("/").map(encodeURIComponent).join("/")
    }

    // ── Formatting ──────────────────────────────────────────────────────────

    function fmtSize(bytes, isDir) {
        if (isDir) return ""
        let v = bytes
        const u = ["B", "KiB", "MiB", "GiB", "TiB"]
        let i = 0
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
        return i === 0 ? v + " B" : v.toFixed(1) + " " + u[i]
    }

    function fmtTime(epoch) {
        if (!epoch) return ""
        const d = new Date(epoch * 1000)
        return Qt.formatDateTime(d, "yyyy-MM-dd hh:mm")
    }

    // The icon theme is resolved here, not in C: quickshell already has it
    // loaded. The fallback chain is derivable from the mime type, which is
    // why the row carries it — "text/x-csrc" tries text-x-csrc, then
    // text-x-generic, then a blank page.
    function iconFor(row) {
        if (row.type === "dir") return Quickshell.iconPath("folder", true)

        // A .desktop launcher names its OWN icon, and that is the icon a file
        // manager shows. Without this ~/Desktop is two hundred identical grey
        // sheets — breeze's generic application-x-desktop — where the user put
        // two hundred different games.
        if (row.icon) {
            // An absolute path is a real file, which is what Steam and Wine
            // launchers write; a bare name goes through the icon theme, and
            // falls through to the mime icon when the theme has not got it.
            if (root.disp(row.icon).indexOf("/") === 0) return "file://" + row.icon
            const named = Quickshell.iconPath(root.disp(row.icon), true)
            if (named) return named
        }

        const mime = row.mime || "application/octet-stream"
        const specific = mime.replace("/", "-")
        let path = Quickshell.iconPath(specific, true)
        if (path) return path
        const media = mime.split("/")[0]
        path = Quickshell.iconPath(media + "-x-generic", true)
        if (path) return path
        return Quickshell.iconPath("text-x-generic", true)
    }

    // ── Split view ──────────────────────────────────────────────────────────
    //
    // Two panes, side by side, each with its own tabs, history, listing and
    // selection — see the Pane component at the bottom of this file. What is
    // shared is the toolbar, the sidebar, the clipboard and the undo journal:
    // two places, not two programs.
    //
    // Everything above the panes acts on "the ACTIVE pane", and any click
    // inside a pane makes it the active one first. That is the whole contract,
    // and it is what lets the toolbar, the address bar, the menus and every
    // keyboard shortcut stay written exactly once.
    property bool split: false
    property int active: 0
    // Where the divider sits, as a fraction of the width. Not remembered
    // between runs: it is a gesture, not a preference, and the useful position
    // depends on what is in the two panes right now.
    property real splitRatio: 0.5

    readonly property Item ap: (root.split && root.active === 1) ? paneB : paneA

    function setActive(i) { root.active = (root.split && i === 1) ? 1 : 0 }

    // Closing a tab, including the LAST one.
    //
    // The × used to disappear when a pane was down to one tab and closeTab()
    // refused it, so the only way out of the last tab was the window's own
    // close button — which is not where anyone looks, and which made the tab
    // bar behave differently from every other tabbed application on the
    // machine. The × is always there now and always closes something.
    //
    // WHAT it closes depends on what is left. In a split, the last tab of a
    // pane closes THAT PANE and keeps the other one's contents — folding the
    // split is the local answer, and quitting the whole window because half of
    // it ran out of tabs would be a very expensive surprise. With no split
    // left, the last tab is the window.
    function closeTabOrQuit(p, i) {
        if (p.tabs.length > 1) { p.closeTab(i); return }

        if (!root.split) { Qt.quit(); return }

        if (p === paneB) {
            // The right pane is already the one being dropped; A keeps what it
            // has.
            paneB.discard()
        } else {
            // The LEFT pane ran out, so B's contents have to move into A —
            // the same handover toggleSplit() does, for the same reason.
            paneA.adopt(paneB)
            paneB.discard()
        }
        root.active = 0
        root.split = false
    }

    function toggleSplit() {
        if (root.split) {
            // Closing keeps what the ACTIVE pane was showing. Dropping back to
            // whatever the left pane happened to hold would throw away the
            // folder the user was actually working in.
            if (root.active === 1) {
                paneA.adopt(paneB)
                paneB.discard()
            }
            root.active = 0
            root.split = false
        } else {
            root.split = true
            // The new pane opens on the folder you are already in — the only
            // answer that is never a surprise — unless it still holds a place
            // from last time, which is what happens when the split was closed
            // from the LEFT pane and the right one was simply put away.
            paneB.ensureStarted(root.tab ? root.tab.path
                                         : root.encodePath(root.homeDir))
            root.active = 1
        }
    }
    onSplitChanged: root.saveSetting("split", root.split ? 1 : 0)

    // Messages about operations, which belong to the window rather than to
    // either pane: an op started in one can finish while you are looking at
    // the other.
    property string statusLine: ""

    // ── What the shared chrome talks to ─────────────────────────────────────
    //
    // Thin forwarders onto the active pane. They exist so that the toolbar,
    // the sidebar and the menus below read the same as they did when there was
    // one pane — `root.tab` still means "the folder on screen", it is just no
    // longer the only one.
    readonly property var tabs: root.ap.tabs
    readonly property int current: root.ap.current
    readonly property var tab: root.ap.tab
    readonly property var selection: root.ap.selection
    readonly property var shownRows: root.ap.shownRows
    readonly property bool loading: root.ap.loading
    readonly property bool canGoBack: root.ap.canGoBack
    readonly property bool canGoForward: root.ap.canGoForward

    function newTab(pathEnc, view) { root.ap.newTab(pathEnc, view) }
    function closeTab(i)           { root.ap.closeTab(i) }
    function setTab(fields)        { root.ap.setTab(fields) }
    function navigate(pathEnc, view) { root.ap.navigate(pathEnc, view) }
    function goBack()              { root.ap.goBack() }
    function goForward()           { root.ap.goForward() }
    function reload()              { root.ap.reload() }
    function beginSearch()         { root.ap.beginSearch() }
    function isSelected(name)      { return root.ap.isSelected(name) }
    function selectAll()           { root.ap.selectAll() }
    function clearSelection()      { root.ap.clearSelection() }
    function selectedRows()        { return root.ap.selectedRows() }
    function selectedPaths()       { return root.ap.selectedPaths() }

    // An operation can change what BOTH panes are showing — moving a file from
    // one to the other is the entire point of having two. Reloading only the
    // pane the command was issued from would leave the destination looking
    // like the drop did nothing.
    function reloadAll() {
        paneA.reload()
        if (root.split) paneB.reload()
    }
    function refreshPeekAll() {
        paneA.refreshPeek()
        if (root.split) paneB.refreshPeek()
    }

    // ── Backend ─────────────────────────────────────────────────────────────

    function parseRecords(text) {
        const lines = text.split("\n").filter(l => l !== "")
        if (lines.length === 0) return []
        const cols = lines[0].split("\t")
        const out = []
        for (let i = 1; i < lines.length; i++) {
            const f = lines[i].split("\t")
            const o = ({})
            for (let c = 0; c < cols.length; c++)
                o[cols[c]] = f[c] !== undefined ? f[c] : ""
            out.push(o)
        }
        return out
    }

    property var aboutRows: []

    // Sidebar sources. All three are read once at startup and on refresh —
    // they change rarely and re-running them per navigation would put three
    // process spawns in front of every double-click.
    property var places: []
    property var volumes: []

    // A USB stick and an internal disk want different things from you — one
    // gets ejected, the other never does — so they are listed apart rather
    // than sorted together under one "Devices".
    readonly property var fixedVolumes:
        root.volumes.filter(v => v.kind === "disk")
    readonly property var removableVolumes:
        root.volumes.filter(v => v.kind === "removable" || v.kind === "optical")
    readonly property var networkVolumes:
        root.volumes.filter(v => v.kind === "network")

    // ── Selection, clipboard and operations ─────────────────────────────────

    // {op: "copy"|"cut", paths: [encoded...]}. Cut is not a move yet — nothing
    // leaves its directory until Paste, which is what makes Ctrl+X reversible
    // by simply not pasting.
    property var clip: ({ op: "", paths: [] })

    // ── What the running operation is doing ─────────────────────────────────
    //
    // A copy used to say "copying…" once and then nothing at all until it
    // exited, however many minutes later. Reported as: no dialog, no way to
    // tell whether it was working, so try again — and the second attempt says
    // "busy", which is the first sign that anything is happening.
    //
    // THE SILENCE HAD TWO HALVES and fixing one changes nothing. This Process
    // read no stdout, and the ops were run WITHOUT --rec, so the per-file
    // records the C side already emits were never printed in the first place.
    // Both are fixed here: runOp passes --rec, and the parser below is a
    // SplitParser — a StdioCollector hands its text over when the stream ENDS,
    // which for a long copy is the one moment the progress is worthless.
    //
    // It also means a per-file FAILURE is finally visible. report() writes
    // those to stdout, and nothing read stdout, so a copy could fail on half
    // its files and the GUI would show the exit code's silence.
    property string opNote: ""
    property int    opDone: 0
    property int    opSkipped: 0
    property int    opFailed: 0
    property string opCurrent: ""
    property string opFirstError: ""
    property string opError: ""
    // What the last operation DID, kept until the next one starts or the view
    // moves. Not statusLine: that is cleared by every reload, and an operation
    // ends by reloading.
    property string opOutcome: ""

    // The panel appears only once an operation has outlived a blink. A modal
    // box that flashes for every rename is worse than no box; one that never
    // appears for a ten-minute copy is what this is fixing.
    property bool opPanel: false
    Timer {
        id: opPanelDelay
        interval: 500
        onTriggered: if (root.busy) root.opPanel = true
    }

    function baseOf(encPath) {
        const p = root.disp(encPath)
        const i = p.lastIndexOf("/")
        return i >= 0 ? p.substring(i + 1) : p
    }

    function opProgress() {
        let s = root.opNote
        if (root.opDone || root.opSkipped || root.opFailed) {
            s += " — " + root.opDone + " done"
            if (root.opSkipped) s += ", " + root.opSkipped + " skipped"
            if (root.opFailed)  s += ", " + root.opFailed + " failed"
        }
        return s
    }

    // What the panel says when nothing has been reported yet. "0 done" reads
    // as stuck, and for `trash empty` — which says nothing at all until it has
    // finished — it would read that way for the whole operation.
    function opCountLine() {
        if (root.opDone || root.opSkipped || root.opFailed)
            return root.opDone + " done"
                 + (root.opSkipped ? ", " + root.opSkipped + " skipped" : "")
                 + (root.opFailed  ? ", " + root.opFailed  + " failed"  : "")
        return "working…"
    }

    // The counters the records land in. A plain object, MUTATED — assigning to
    // the int properties below on every record would re-evaluate the panel's
    // bindings once per file, and a copy of a hundred thousand small files is
    // exactly the copy this panel exists for. The visible properties are
    // refreshed on a timer instead, which is as often as an eye can read them.
    // REAL, not int: QML's int is 32-bit and a copy passes 2 GB routinely.
    property real opBytes: 0
    property real opTotalBytes: 0
    property int  opTotalFiles: 0
    property real opStart: 0
    property real opElapsed: 0
    property bool opCancelling: false

    property var opRaw: ({ done: 0, skipped: 0, failed: 0, current: "",
                           removed: -1, bytes: 0, totalBytes: 0, totalFiles: 0,
                           cancelled: false })

    Timer {
        id: opTick
        interval: 120
        repeat: true
        running: root.busy
        onTriggered: root.flushOpCounts()
    }

    function flushOpCounts() {
        root.opDone       = root.opRaw.done
        root.opSkipped    = root.opRaw.skipped
        root.opFailed     = root.opRaw.failed
        root.opCurrent    = root.opRaw.current
        root.opBytes      = root.opRaw.bytes
        root.opTotalBytes = root.opRaw.totalBytes
        root.opTotalFiles = root.opRaw.totalFiles
        if (root.opStart) root.opElapsed = (Date.now() - root.opStart) / 1000
        root.statusLine = root.opProgress()
    }

    // ── How far along, and how much longer ──────────────────────────────────
    //
    // Measured in BYTES, because a count of files says nothing about time
    // remaining when one of them is 8 GB. The total comes from a stat-only
    // pre-pass in the C, which is what makes any of this possible.
    // MEASURED IN WHICHEVER UNIT DECIDES THE TIME. A copy is bytes: one 8 GB
    // file is the whole job. A DELETE is entries — one unlink each, whatever
    // they weigh — so a folder of ten thousand empty files is minutes and no
    // bytes at all, and a byte-shaped bar would call it finished before it
    // started. The C says which by sending a byte total of zero.
    readonly property bool opByBytes: root.opTotalBytes > 0
    readonly property real opUnitDone:
        root.opByBytes ? root.opBytes
                       : (root.opDone + root.opSkipped + root.opFailed)
    readonly property real opUnitTotal:
        root.opByBytes ? root.opTotalBytes : root.opTotalFiles

    readonly property real opFraction:
        root.opUnitTotal > 0 ? Math.min(1, root.opUnitDone / root.opUnitTotal) : 0

    readonly property real opRate:               // bytes/s, or items/s
        root.opElapsed > 0.4 ? root.opUnitDone / root.opElapsed : 0

    function fmtEta() {
        // Nothing to say yet is better than a wild number: the first second of
        // any operation predicts an eternity or an instant, and neither is true.
        if (root.opRate <= 0 || root.opUnitTotal <= 0) return ""
        const left = (root.opUnitTotal - root.opUnitDone) / root.opRate
        if (!isFinite(left) || left < 0) return ""
        if (left < 5)   return "a moment left"
        if (left < 90)  return Math.round(left) + "s left"
        if (left < 3600) return Math.round(left / 60) + " min left"
        return (left / 3600).toFixed(1) + " hours left"
    }

    function opRateLine() {
        if (root.opUnitTotal <= 0) return ""
        let s = root.opByBytes
                ? root.fmtSize(root.opBytes, false) + " of "
                  + root.fmtSize(root.opTotalBytes, false)
                : root.opUnitDone + " of " + root.opTotalFiles + " items"
        if (root.opRate > 0)
            s += "  ·  " + (root.opByBytes
                              ? root.fmtSize(root.opRate, false) + "/s"
                              : Math.round(root.opRate) + "/s")
        const eta = root.fmtEta()
        if (eta) s += "  ·  " + eta
        return s
    }

    // NOT EVERY OP SPEAKS THE SAME RECORD. copy, move, rename, mkdir, delete,
    // compress, trash and undo all emit `path status detail` per item — but
    // `trash empty` emits ONE row at the end, `removed <n>`; mount and unmount
    // emit `<device> mounted <path>`; and every one of them starts with a
    // header row whose second field is the literal word "status".
    //
    // So this counts by VOCABULARY rather than by position. Anything it does
    // not recognise is ignored, which is what keeps a header line or a summary
    // row from being counted as a failed file — emptying the trash reported
    // "removed — 12" as an error before this, and mounting a disk reported the
    // header.
    function noteOpRecord(line) {
        const f = line.split("\t")
        if (f.length < 2) return
        const status = f[1]

        // How much there is, sent once before anything is copied.
        if (f[0] === "total") {
            root.opRaw.totalFiles = parseInt(status, 10)
            root.opRaw.totalBytes = parseFloat(f[2])
            return
        }

        // Stopped on request. Not a failure — nobody needs an error about a
        // thing they asked for.
        if (f[0] === "cancelled") {
            root.opRaw.cancelled = true
            return
        }

        // Part-way through a single large file. The byte figure on every
        // record is CUMULATIVE for the whole operation, so this is an
        // assignment and never an addition.
        if (status === "progress") {
            if (f[3]) root.opRaw.bytes = parseFloat(f[3])
            root.opRaw.current = root.baseOf(f[0])
            return
        }

        // `trash empty` says how many it removed, once, at the end.
        if (f[0] === "removed") {
            root.opRaw.removed = parseInt(status, 10)
            return
        }

        if (status === "done" || status === "mounted" || status === "unmounted") {
            root.opRaw.done++
            if (f[3]) root.opRaw.bytes = parseFloat(f[3])
        }
        else if (status === "skipped")
            root.opRaw.skipped++
        else if (status === "failed" || status === "conflict") {
            root.opRaw.failed++
            if (!root.opFirstError)
                root.opFirstError = root.baseOf(f[0])
                                  + " — " + (f[2] ? f[2] : status)
        } else {
            return          // a header, or a shape this does not count
        }

        root.opRaw.current = root.baseOf(f[0])
    }

    Process {
        id: opProc
        stdout: SplitParser {
            onRead: (line) => root.noteOpRecord(line)
        }
        stderr: StdioCollector {
            onStreamFinished: {
                if (this.text) root.statusLine = this.text.split("\n")[0]
            }
        }
        // No parameters, deliberately: quickshell's exited(int, QProcess::
        // ExitStatus) has a second type QML cannot resolve, and a typed
        // handler silently never runs.
        onExited: {
            root.busy = false
            root.opPanel = false
            opPanelDelay.stop()
            root.flushOpCounts()   // the last records arrived after the last tick

            // Say how it ended. A failure used to reach the GUI as an exit
            // code and nothing else — the pane simply reloaded, and whatever
            // had not been copied was missing without a word.
            if (root.opRaw.cancelled || root.opCancelling)
                root.opOutcome = root.opNote + " — cancelled"
                    + (root.opDone ? " after " + root.opDone + " done" : "")
            else if (root.opError)
                root.opOutcome = root.opError
            else if (root.opFailed)
                root.opOutcome = root.opFirstError
                    ? root.opNote + " — " + root.opFirstError
                    : root.opNote + " — " + root.opFailed + " failed"
            else if (root.opRaw.removed >= 0)
                root.opOutcome = root.opNote + " — " + root.opRaw.removed
                    + (root.opRaw.removed === 1 ? " item removed" : " items removed")
            else if (root.opDone || root.opSkipped)
                root.opOutcome = root.opProgress()
            else
                // Ops that report nothing per item still have to say they are
                // over, or the panel simply vanishes and nothing replaces it.
                root.opOutcome = root.opNote + " — done"

            root.reloadAll()
            root.refreshUndo()
            placesProc.running = true
            // Mounting changes what the Devices list should show, and so does
            // trashing something onto a volume. Cheaper to re-read both than
            // to work out which operations could have moved them.
            volProc.running = true

            // A netmount only learns WHERE the share landed by asking again:
            // gvfs derives its FUSE directory from the URI and the command does
            // not report it back. Re-scanned only when one is outstanding — a
            // scan is subprocesses on the network, not a free refresh.
            if (root.netPendingUri !== "" && !root.netScanning) {
                root.netScanning = true
                netProc.running = true
            }
        }
    }
    property bool busy: false

    function runOp(args, note) {
        if (root.busy) {
            // Say so. A menu entry that returns in silence because something
            // else is still running is indistinguishable from one that is
            // broken — which is exactly how the xdg-open bug above was
            // experienced: "Open with … doesn't respond".
            root.statusLine = "busy — waiting for the last operation to finish"
            return
        }
        root.busy = true
        root.statusLine = note
        root.opNote = note
        root.opDone = 0
        root.opSkipped = 0
        root.opFailed = 0
        root.opCurrent = ""
        root.opFirstError = ""
        root.opError = ""
        root.opOutcome = ""
        root.opRaw = ({ done: 0, skipped: 0, failed: 0, current: "",
                        removed: -1, bytes: 0, totalBytes: 0, totalFiles: 0,
                        cancelled: false })
        root.opBytes = 0
        root.opTotalBytes = 0
        root.opTotalFiles = 0
        root.opElapsed = 0
        root.opCancelling = false
        root.opStart = Date.now()
        opPanelDelay.restart()
        // Paths cross the process boundary DECODED: argv carries raw bytes and
        // needs no escaping. The encoding exists for the record stream, which
        // is a text format, not for exec().
        //
        // --rec is what makes the operation SPEAK: one record per file, which
        // is where the progress above comes from. Without it report() takes
        // its human branch, which prints nothing at all for a file that
        // copied successfully.
        opProc.command = [root.bin, "--rec"].concat(args)
        opProc.running = true
    }

    // The single focused row, for the operations that only make sense on one
    // thing — rename, and opening.
    function selectedRow() {
        const rows = root.selectedRows()
        return rows.length === 1 ? rows[0] : null
    }

    function describeSelection() {
        const n = root.selection.length
        if (n === 1) return root.rowLabel(root.selectedRows()[0])
        return n + " items"
    }

    // One process for the whole selection rather than one per file: the C side
    // already takes many sources, and N spawns would each reload the pane on
    // exit and fight each other.
    function trashSelection() {
        const paths = root.selectedPaths()
        if (paths.length === 0) return
        root.runOp(["trash"].concat(paths),
                   "moving " + root.describeSelection() + " to the trash")
    }

    function restoreFromTrash(row) {
        if (!row || !row.trashName) return
        // The trashName is passed back EXACTLY as listed — percent-encoded.
        // synfiles decodes it; re-deriving it from the display name would
        // fail for anything that got a .2 suffix.
        root.runOp(["trash", "restore", row.trashName],
                   "restoring " + root.disp(row.name))
    }

    function copySelection(cut) {
        const rows = root.selectedRows()
        if (rows.length === 0) return
        root.clip = { op: cut ? "cut" : "copy", paths: rows.map(r => r.full) }
        root.statusLine = (cut ? "cut " : "copied ") + root.describeSelection()
    }

    // ── Paste ───────────────────────────────────────────────────────────────
    //
    // Two steps, because the interesting question comes first: does anything
    // at the destination already have these names?
    //
    // This used to paste straight through with --conflict=rename and a comment
    // saying the GUI had no way to ask. The cost of not asking was not just a
    // missing dialog: `--conflict=rename` on a folder whose name already
    // existed did not rename the FOLDER, it merged into the existing one and
    // renamed the files inside it. So pasting a folder into the folder that
    // contains it — the ordinary way to duplicate one — appeared to do nothing
    // and quietly filled the original with `(copy)` duplicates.
    //
    // The probe is `collisions`: one stat per source, no traversal. Reading
    // the destination pane's own rows instead would be cheaper and wrong —
    // that listing is filtered, so a collision with a hidden file is invisible
    // in it, and it can be stale.
    property var  pendingPaste: null    // {op, paths, dest}
    property var  pasteConflicts: []    // [{name, kind, same}]
    property bool pasteAsk: false

    readonly property bool pasteHasSame:
        root.pasteConflicts.some(c => c.same)

    Process {
        id: collideProc
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = []
                for (const line of this.text.split("\n")) {
                    const f = line.split("\t")
                    if (f.length < 4 || f[0] === "path") continue
                    rows.push({ name: root.disp(f[1]), kind: f[2],
                                same: f[3] === "yes" })
                }
                root.pasteConflicts = rows
                if (rows.length === 0) {
                    // Nothing to ask about. `error` rather than `rename`: at
                    // this point a collision can only be one that appeared
                    // between the probe and the paste, and inventing a new
                    // name for it silently is exactly the guess this whole
                    // path exists to stop making.
                    root.doPaste("error")
                } else {
                    root.pasteAsk = true
                    root.statusLine = rows.length + (rows.length === 1
                        ? " item already exists here" : " items already exist here")
                }
            }
        }
    }

    function paste() {
        if (!root.tab || root.tab.view !== "dir") return
        if (!root.clip.paths || root.clip.paths.length === 0) return
        if (root.busy) {
            root.statusLine = "busy — waiting for the last operation to finish"
            return
        }

        // Snapshotted: the dialogue is answered later, and by then the
        // clipboard or the current directory may be something else.
        root.pendingPaste = ({ op: root.clip.op,
                               paths: root.clip.paths.slice(),
                               dest: root.tab.path })

        const args = ["--rec", "collisions"]
        for (const p of root.pendingPaste.paths) args.push(root.disp(p))
        args.push(root.disp(root.pendingPaste.dest))

        root.statusLine = "checking the destination…"
        collideProc.command = [root.bin].concat(args)
        collideProc.running = true
    }

    function doPaste(policy) {
        root.pasteAsk = false
        const p = root.pendingPaste
        root.pendingPaste = null
        if (!p) return

        const n = p.paths.length
        const what = n === 1 ? root.baseOf(p.paths[0]) : n + " items"

        const args = [p.op === "cut" ? "move" : "copy", "--conflict=" + policy]
        for (const q of p.paths) args.push(root.disp(q))
        args.push(root.disp(p.dest))

        root.runOp(args, (p.op === "cut" ? "moving " : "copying ") + what)
        if (p.op === "cut")
            root.clip = ({ op: "", paths: [] })
    }

    // SIGTERM, not a kill: the C finishes the write it is in, removes the
    // half-written destination file and says it was cancelled. SIGKILL would
    // leave that fragment on disk looking like a real file of the wrong size.
    function cancelOp() {
        if (!root.busy || root.opCancelling) return
        root.opCancelling = true
        root.statusLine = "cancelling…"
        opProc.signal(15)
    }

    function cancelPaste() {
        root.pasteAsk = false
        root.pendingPaste = null
        root.statusLine = "paste cancelled"
    }

    // ── Rename ──────────────────────────────────────────────────────────────
    // The name being edited is the PANE's — two panes can be renaming two
    // different files at once, and the inline editor is drawn on a row.
    //
    // The row is a PARAMETER, not the current selection. The editor knows
    // exactly which row it is drawn on; re-deriving that from what happens to
    // be selected is the same class of mistake as beginDrag reading the active
    // pane instead of its own — it agrees with the visible state right up until
    // it doesn't, and then it renames the wrong file.
    // ⛔ AN EXTENSION IS NOT PART OF THE NAME YOU TYPE, so typing a name cannot
    // take it away. Renaming `tux95.png` to `tux95` produced an extensionless
    // file — still a perfectly good PNG, and invisible to everything that
    // dispatches on the suffix: synui's wallpaper picker filters the folder by
    // extension (wppick.c) and the thumbnailer picks its decoder the same way
    // (wpthumb.c), so the picture dropped out of the wallpaper list and
    // previewed as nothing. No error, nothing in a log, and the file looks
    // untouched in every listing — the only clue is the missing suffix.
    //
    // A new name carrying no extension KEEPS the old one. Typing one is how
    // you change it, which is the only way to say so deliberately.
    //
    // ⚠ stemLen() keeps the extension out of the initial selection, which is
    // what makes the ordinary case pleasant; THIS is what makes it safe. The
    // selection can be extended by hand, and a rename typed from a menu or
    // pasted in never went through that selection at all.
    //
    // ⚠ A FOLDER HAS NO EXTENSION — dots in `.config` or `My.Stuff` are name.
    // ⚠ A LEADING DOT IS NOT ONE either: `.bashrc` is all stem, so it counts as
    // an untyped extension and the old suffix is kept.
    function keepExt(oldName, newName, isDir) {
        if (isDir) return newName
        var oldDot = oldName.lastIndexOf(".")
        if (oldDot <= 0) return newName                 // nothing to preserve
        if (newName.lastIndexOf(".") > 0) return newName // one was typed
        return newName + oldName.substring(oldDot)
    }

    function commitRename(row, newName) {
        root.ap.renaming = ""
        if (!row || !newName) return
        var was = root.disp(row.name)
        newName = root.keepExt(was, newName, row.type === "dir")
        // Compared AFTER the extension is restored: typing `tux95` over
        // `tux95.png` is now the same name, and must not spend a rename op
        // (nor a line in the undo log) saying so.
        if (newName === was) return
        root.runOp(["rename", root.disp(row.full), newName],
                   "renaming to " + newName)
    }

    // ── Thumbnails ──────────────────────────────────────────────────────────
    //
    // The freedesktop thumbnail cache first: ~/.cache/thumbnails/<size>/<md5 of
    // the file's URI>.png. That is a SHARED cache, so anything Dolphin, GTK or
    // a video player has already thumbnailed shows up here for free — video and
    // PDF included, which synfiles has no way to render itself.
    //
    // The URI has to be percent-encoded for the hash to match, and it already
    // is: `full` is the encoded path, so "file://" + full is exactly the URI
    // every other implementation hashed. That is the whole reason this needs no
    // C code — Qt.md5() finishes the job.
    property bool thumbs: true

    // ── Icon size ───────────────────────────────────────────────────────────
    // One control, not two. The layout follows the size rather than being a
    // separate "view mode" setting: small icons want a details list with size
    // and date columns, and 96px icons want a grid, and nobody has ever wanted
    // 96px icons in a one-per-row list.
    property int iconSize: 20
    readonly property int iconMin: 16
    readonly property int iconMax: 128
    // ── View mode ───────────────────────────────────────────────────────────
    //
    // Icons, Compact and Details, the three Dolphin has, and each is a thing
    // you PICK. It used to be derived from the icon size — list below 48px,
    // grid above — which is a rule nobody can find and nobody can override:
    // wanting big icons in a list, or a compact list of large ones, was simply
    // not expressible.
    //
    // "auto" keeps that old rule as the default, so no existing window changes
    // shape on upgrade; the menu ticks whichever mode auto currently resolves
    // to, and picking one replaces the rule with an answer.
    property string viewMode: "auto"

    readonly property string effectiveView:
        root.viewMode !== "auto" ? root.viewMode
                                 : (root.iconSize >= 48 ? "icons" : "details")

    readonly property bool gridView: root.effectiveView !== "details"
    readonly property bool compactView: root.effectiveView === "compact"

    // Loading a 200MB TIFF to draw a 24px square is not a thumbnail, it is a
    // stall. Above this the icon is used and the cache is still consulted.
    readonly property int thumbMaxBytes: 12 * 1024 * 1024

    // Which hashes the cache actually holds, indexed once rather than probed
    // per row. Letting Image discover a miss works, but Qt logs a warning for
    // every failed load — a folder of 500 files with no thumbnails produced
    // 500 lines of "Cannot open" before this existed. Listing the cache is one
    // process and turns the whole question into a lookup.
    property var thumbIndex: ({ normal: ({}), large: ({}) })

    Process {
        id: thumbScanProc
        property string size: "normal"
        stdout: StdioCollector {
            onStreamFinished: {
                const set = ({})
                for (const r of root.parseRecords(this.text)) {
                    // Names here are plain hex + ".png", so decoding is safe
                    // and the encoded form carries no escapes anyway.
                    const n = r.name || ""
                    if (n.length > 4) set[n.substring(0, n.length - 4)] = true
                }
                const idx = ({ normal: root.thumbIndex.normal,
                               large: root.thumbIndex.large })
                idx[thumbScanProc.size] = set
                root.thumbIndex = idx
            }
        }
    }

    Process {
        id: thumbScanLargeProc
        stdout: StdioCollector {
            onStreamFinished: {
                const set = ({})
                for (const r of root.parseRecords(this.text)) {
                    const n = r.name || ""
                    if (n.length > 4) set[n.substring(0, n.length - 4)] = true
                }
                const idx = ({ normal: root.thumbIndex.normal, large: set })
                root.thumbIndex = idx
            }
        }
    }

    function scanThumbs() {
        thumbScanProc.size = "normal"
        thumbScanProc.command = [root.bin, "--rec", "list",
                                 root.homeDir + "/.cache/thumbnails/normal"]
        thumbScanProc.running = true
        thumbScanLargeProc.command = [root.bin, "--rec", "list",
                                      root.homeDir + "/.cache/thumbnails/large"]
        thumbScanLargeProc.running = true
    }

    // The whole fallback chain as ONE expression. The staged version advanced
    // on Image.Error, which stops working the moment a missing thumbnail
    // resolves to "" instead of a bad path — an empty source is Null, not
    // Error, so the stage never advanced and every uncached file drew nothing.
    function previewFor(row) {
        if (root.thumbs && row.type === "file") {
            const l = root.thumbUri(row, "large")
            if (l) return l
            const n = root.thumbUri(row, "normal")
            if (n) return n
            if (root.canRenderDirectly(row)) return "file://" + row.full
        }
        return root.iconFor(row)
    }

    function thumbUri(row, size) {
        if (!row || !row.full) return ""
        const hash = Qt.md5("file://" + row.full)
        if (!root.thumbIndex[size] || !root.thumbIndex[size][hash]) return ""
        return "file://" + root.homeDir + "/.cache/thumbnails/" + size + "/"
             + hash + ".png"
    }

    function canRenderDirectly(row) {
        return row && row.mime && row.mime.indexOf("image/") === 0
            && row.size > 0 && row.size <= root.thumbMaxBytes
    }

    // A preview tile's source, from a bare path — the peek rows carry no mime,
    // so the extension decides whether the file itself can stand in for a
    // thumbnail. A video never can: that needs a thumbnailer, and the shared
    // cache is exactly where one would have left the result.
    function peekSource(p) {
        const hash = Qt.md5("file://" + p.full)
        if (root.thumbIndex.large && root.thumbIndex.large[hash])
            return "file://" + root.homeDir + "/.cache/thumbnails/large/" + hash + ".png"
        if (root.thumbIndex.normal && root.thumbIndex.normal[hash])
            return "file://" + root.homeDir + "/.cache/thumbnails/normal/" + hash + ".png"
        if (root.looksLikeImage(p.full) && p.size > 0 && p.size <= root.thumbMaxBytes)
            return "file://" + p.full
        return ""
    }

    readonly property var imageExts: ["png", "jpg", "jpeg", "gif", "bmp", "webp",
                                      "avif", "ico", "tif", "tiff", "jxl"]
    function looksLikeImage(pathEnc) {
        const dot = pathEnc.lastIndexOf(".")
        if (dot < 0) return false
        return root.imageExts.indexOf(pathEnc.substring(dot + 1).toLowerCase()) >= 0
    }

    // ── Properties ──────────────────────────────────────────────────────────
    property bool showProps: false
    property var propRows: []

    // Closing the panel STOPS the walk. A `du` over a big tree runs for
    // seconds after the panel is gone otherwise, and its records would then
    // land in the next folder's row.
    onShowPropsChanged: if (!root.showProps) root.stopFolderSize()

    Process {
        id: infoProc
        stdout: StdioCollector {
            onStreamFinished: root.propRows = root.parseRecords(this.text)
        }
    }

    // ── How big a FOLDER is ─────────────────────────────────────────────────
    //
    // `info` reports st_size, and for a directory that is the size of the
    // directory entry — which is why the SYNAPSE folder, ISO and all, read
    // "890 B". The real answer needs a walk of the whole tree, and that takes
    // seconds on a big one, so it is a second command started after the panel
    // has already drawn rather than something the panel waits for.
    //
    // SplitParser, not StdioCollector: `du` prints a RUNNING total, and a
    // collector fires once at the end — which would leave the row saying
    // "calculating…" for the entire walk and then jump straight to the answer,
    // throwing away the only progress signal there is.
    property bool duRunning: false
    property var  duTotal: null      // {bytes, disk, files, dirs, done}

    Process {
        id: duProc
        stdout: SplitParser {
            onRead: (line) => {
                const f = line.split("\t")
                if (f.length < 5 || f[0] === "bytes") return   // header
                root.duTotal = {
                    bytes: parseInt(f[0]) || 0, disk: parseInt(f[1]) || 0,
                    files: parseInt(f[2]) || 0, dirs:  parseInt(f[3]) || 0,
                    done:  f[4] === "1"
                }
            }
        }
        onExited: root.duRunning = false
    }

    function startFolderSize(pathEnc) {
        root.duTotal = null
        root.duRunning = true
        duProc.command = [root.bin, "--rec", "du", root.disp(pathEnc)]
        duProc.running = true
    }

    // A walk of a large tree must not outlive the panel that asked for it.
    // Closing properties, or asking about something else, stops the old one —
    // otherwise two walks race and the row shows whichever reports last.
    function stopFolderSize() {
        root.duRunning = false
        root.duTotal = null
        duProc.running = false
    }

    function propValue(key) {
        for (const r of root.propRows) if (r.key === key) return r.value
        return ""
    }

    function fmtCount(n) {
        // Thousands separators: "375360 files" is a number nobody reads.
        return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ",")
    }

    function openProperties() {
        const rows = root.selectedRows()
        if (rows.length === 0) return
        root.showProps = true
        if (rows.length === 1) {
            root.propRows = []
            infoProc.command = [root.bin, "--rec", "info", root.disp(rows[0].full)]
            infoProc.running = true
            root.stopFolderSize()
            if (rows[0].type === "dir") root.startFolderSize(rows[0].full)
        } else {
            // No point running `info` N times to show one number. A
            // multi-selection answers a different question anyway: how much is
            // this, not what is it.
            let total = 0, dirs = 0
            for (const r of rows) {
                total += r.size || 0
                if (r.type === "dir") dirs++
            }
            root.propRows = [
                { key: "selected", value: rows.length + " items" },
                { key: "folders",  value: "" + dirs },
                { key: "files",    value: "" + (rows.length - dirs) },
                { key: "size",     value: root.fmtSize(total, false)
                                          + (dirs > 0 ? "  (folder contents not counted)" : "") }
            ]
        }
    }

    // The folder you are standing in, rather than what is selected inside it —
    // which is what a right-click on the empty space is asking about. Same
    // panel and the same `info` command; only the path differs, so there is
    // nothing here that can drift away from the row version above.
    function openFolderProperties() {
        if (!root.tab || root.tab.view !== "dir") return
        root.showProps = true
        root.propRows = []
        infoProc.command = [root.bin, "--rec", "info", root.disp(root.tab.path)]
        infoProc.running = true
        root.stopFolderSize()
        root.startFolderSize(root.tab.path)
    }

    // ── Folder tree ─────────────────────────────────────────────────────────
    //
    // Lazily loaded, one level at a time. Reading a whole home directory tree
    // up front to draw a sidebar is how a file manager takes four seconds to
    // open; a node is only listed when it is expanded, and the result is kept
    // so collapsing and reopening costs nothing.
    //
    // Directories only — a tree with files in it is the list, twice.
    property bool showTree: false
    property var treeChildren: ({})    // encoded path -> [{name, full}]
    property var treeOpen: ({})        // encoded path -> true

    Process {
        id: treeProc
        property string forPath: ""
        stdout: StdioCollector {
            onStreamFinished: {
                const kids = root.parseRecords(this.text)
                    .filter(r => r.type === "dir")
                    .map(r => ({ name: r.name,
                                 full: root.joinEnc(treeProc.forPath, r.name) }))
                const m = ({})
                for (const k in root.treeChildren) m[k] = root.treeChildren[k]
                m[treeProc.forPath] = kids
                root.treeChildren = m
            }
        }
    }

    function treeToggle(pathEnc) {
        const open = ({})
        for (const k in root.treeOpen) open[k] = root.treeOpen[k]
        if (open[pathEnc]) {
            delete open[pathEnc]
        } else {
            open[pathEnc] = true
            if (!root.treeChildren[pathEnc]) {
                treeProc.forPath = pathEnc
                treeProc.command = [root.bin, "--rec", "list", root.disp(pathEnc)]
                treeProc.running = true
            }
        }
        root.treeOpen = open
    }

    // Flattened for a plain ListView: a real tree view would need a delegate
    // that recurses, and QML makes that considerably more awkward than
    // computing the visible rows here.
    readonly property var treeRows: {
        const rootPath = root.encodePath(root.homeDir)
        const out = []
        function walk(pathEnc, name, depth) {
            out.push({ full: pathEnc, name: name, depth: depth,
                       open: root.treeOpen[pathEnc] === true,
                       loaded: root.treeChildren[pathEnc] !== undefined })
            if (root.treeOpen[pathEnc] && root.treeChildren[pathEnc])
                for (const k of root.treeChildren[pathEnc])
                    walk(k.full, k.name, depth + 1)
        }
        walk(rootPath, "Home", 0)
        return out
    }

    // ── Compress ────────────────────────────────────────────────────────────
    function compressSelection(fmt) {
        const paths = root.selectedPaths()
        if (paths.length === 0) return
        root.runOp(["compress", "--format=" + fmt].concat(paths),
                   "compressing " + root.describeSelection() + "…")
    }

    // ── Drag and drop ───────────────────────────────────────────────────────
    //
    // Internal only. Dragging OUT to another application needs a Wayland data
    // source, and the desktop-drop work already established that pre-v3
    // wl_data_source aborts — so that is a separate problem, not a smaller
    // version of this one.
    //
    // The source is a 1x1 item that draws nothing: dragging the delegate
    // itself would pull it out of the list and leave a hole where it was, and
    // with a system drag the picture is Drag.imageSource anyway.
    property bool dragging: false
    property var dragPaths: []
    property string dragLabel: ""
    property bool dragCopy: false      // Ctrl during the drag

    // What OTHER applications get handed. text/uri-list is the format every
    // file manager, browser and desktop speaks, and its lines are CRLF-
    // terminated file:// URIs — which is what the encoded path already IS,
    // so the identity rule holds all the way out of the process.
    property string dragUris: ""
    property string dragText: ""
    // The picture the cursor carries. Qt draws only what Drag.imageSource
    // names, so the hand-positioned ghost is not it: the dragged file's own
    // thumbnail is both cheaper and what every other file manager shows.
    property url dragImage: ""

    // A click that wanders two pixels is a click. Without a threshold, every
    // selection becomes a potential move.
    readonly property int dragThreshold: 8

    // ⚠ The PANE is a parameter, not root.ap. A press-and-drag never reaches
    // onClicked — the click is swallowed by the drag — so the pane the drag
    // started in was still not the active one when this ran, and every lookup
    // below went to the OTHER pane. Dragging out of the inactive half either
    // carried nothing (empty selection → canDropOn refuses → the drop looked
    // dead) or, worse, silently dragged a file of the same name out of the
    // other pane's folder. The press claims the pane now as well, but reading
    // a global for "which pane am I in" from inside a pane was the mistake.
    function beginDrag(src, row, label) {
        if (!src || !src.tab || src.tab.view !== "dir") return
        // Whatever is selected, or the row under the cursor if it is not part
        // of the selection — the same rule the context menu follows.
        if (!src.isSelected(row.name)) src.selectOnly(row.name)
        root.dragPaths = src.selectedPaths()
        root.dragLabel = label

        const rows = src.selectedRows()
        let uris = "", text = ""
        for (const r of rows) {
            uris += "file://" + r.full + "\r\n"   // r.full is already encoded
            text += root.disp(r.full) + "\n"
        }
        root.dragUris = uris
        root.dragText = text
        root.dragImage = rows.length === 1 ? root.previewFor(rows[0]) : ""

        root.dragging = true

        // A REAL drag, not an in-scene one: this is what reaches another
        // synfiles window, Dolphin, and synui's desktop — all of which are
        // separate processes that can only be handed a wl_data_source.
        //
        // ⚠ Setting `active` IS the start. For Drag.Automatic, Qt's setActive()
        // calls startDrag() itself; calling startDrag() from here instead is
        // refused with "startDrag() drag must be active" — which is exactly
        // what the log filled up with, 2528 times, while nothing dragged at
        // all. It BLOCKS until the button comes up, which is why endDrag() is
        // the next line and not in a release handler a system grab never
        // delivers.
        dragGhost.Drag.active = true

        // ⚠ …ON QT 6.11.1 AND EARLIER. Qt 6.11.2 (2026-08-20) stopped starting
        // the drag from setActive(): the assignment above returns immediately,
        // no wl_data_source is ever offered, and the only symptom is that
        // nothing drags — no warning, no error, and beginDrag() then re-fires
        // on every motion event, because `dragging` is cleared again a line
        // later. Every drag out of this window died that day: a row onto a
        // folder, a pane onto the other pane, synfiles onto mpv.
        //
        // startDrag() is the documented way to start one and it works on both
        // — but only while the drag is ACTIVE, which is exactly what tells the
        // two Qts apart and why the state is the guard rather than a version
        // test. On 6.11.2 the assignment leaves `Drag.active` true with the
        // button still down, and this call is what does the work. On 6.11.1 the
        // assignment blocked for the whole drag, so the drop has already
        // happened, `Drag.active` has gone false again, and calling startDrag()
        // here would earn a "drag must be active" warning for nothing.
        if (dragGhost.Drag.active) dragGhost.Drag.startDrag()

        root.endDrag()
    }

    function endDrag() {
        dragGhost.Drag.active = false
        root.dragging = false
        root.dragPaths = []
        root.dragLabel = ""
        root.dragUris = ""
        root.dragText = ""
        root.dragImage = ""
    }

    // ── Drops from somewhere else ───────────────────────────────────────────
    //
    // The other half of a real drag: another synfiles window, Dolphin, a
    // browser saving an image. They all arrive as text/uri-list.
    //
    // Local files ONLY, and refused rather than half-done — a drag of a web
    // link keeps its "no" cursor instead of producing an empty file. And a
    // foreign drop always COPIES: moving means deleting something owned by an
    // application that has not told us it finished with it.
    function urlsToPaths(urls) {
        const out = []
        for (const u of urls) {
            const s = "" + u
            if (s.indexOf("file://") !== 0) continue
            // The URI's path IS the encoded form — decode it exactly the way
            // every other path crosses into argv.
            out.push(root.disp(s.substring(7)))
        }
        return out
    }

    // Every DropArea in the window goes through these two, because "is this
    // drop ours or a foreign one" is one question and it should have one
    // answer. A drag we started is a move within this window; anything else is
    // a copy in from another process.
    function willAcceptDrop(destEnc, drag) {
        if (destEnc === "") return false
        if (root.dragging) return root.canDropOn(destEnc)
        return drag !== undefined && drag !== null && drag.hasUrls
    }

    function handleDrop(destEnc, drop) {
        if (root.dragging) { root.dropOn(destEnc); return }
        if (drop && drop.hasUrls) {
            root.dropUrls(destEnc, drop.urls)
            drop.accept(Qt.CopyAction)
        }
    }

    function dropUrls(destEnc, urls) {
        const paths = root.urlsToPaths(urls)
        if (paths.length === 0) {
            root.statusLine = "only local files can be dropped here"
            return
        }
        const dest = root.disp(destEnc)
        // Dropping something into the folder it already lives in is a no-op,
        // not an error worth a dialog.
        const useful = paths.filter(p => p.substring(0, p.lastIndexOf("/")) !== dest)
        if (useful.length === 0) return
        root.runOp(["copy", "--conflict=rename"].concat(useful).concat([dest]),
                   "copying " + useful.length
                   + (useful.length === 1 ? " item" : " items") + " here…")
    }

    // A drop onto the folder something already lives in is a no-op, and a drop
    // into one of its own descendants is the recursion the C side refuses
    // anyway — caught here so the cursor can say no before the button is let go.
    function canDropOn(destEnc) {
        if (!root.dragging || root.dragPaths.length === 0) return false
        const dest = root.disp(destEnc)
        for (const p of root.dragPaths) {
            if (p === dest) return false
            if (dest.indexOf(p + "/") === 0) return false
            const parent = p.substring(0, p.lastIndexOf("/"))
            if (parent === dest) return false
        }
        return true
    }

    function dropOn(destEnc) {
        if (!root.canDropOn(destEnc)) { root.endDrag(); return }
        const paths = root.dragPaths.slice()
        const copy = root.dragCopy
        root.endDrag()
        root.runOp([copy ? "copy" : "move", "--conflict=rename"]
                   .concat(paths).concat([root.disp(destEnc)]),
                   (copy ? "copying " : "moving ") + paths.length
                   + (paths.length === 1 ? " item" : " items") + "…")
    }

    // ── Remembered settings ─────────────────────────────────────────────────
    //
    // Read once at startup, written through the binary whenever one changes.
    // NOT through a FileView: quickshell's silently drops setText() on a path
    // that does not exist yet, so the first ever write of a settings file
    // vanishes with no error — which is precisely the state a fresh install is
    // in. The binary validates and clamps, so the GUI never has to.
    property bool settingsLoaded: false

    // Sort, direction and hidden-files live on the TAB, because two tabs
    // genuinely want different ones. What is remembered is the DEFAULT a new
    // tab starts from — which is what "it should remember my setting" means
    // when the setting is per-tab.
    property string defaultSort: "name"
    property bool   defaultReverse: false
    property bool   defaultHidden: false

    // The left panel. Not per-tab and not per-pane: it is the shape of the
    // WINDOW, like the split, so both panes and every tab see the same one.
    property bool   showSidebar: true

    Process {
        id: cfgReadProc
        command: [root.bin, "--rec", "config", "list"]
        stdout: StdioCollector {
            onStreamFinished: {
                for (const r of root.parseRecords(this.text)) {
                    switch (r.key) {
                    case "icon_size": root.iconSize = parseInt(r.value) || 20; break
                    case "previews":  root.thumbs   = r.value === "1"; break
                    case "tree":      root.showTree = r.value === "1"; break
                    case "sort":      root.defaultSort = r.value; break
                    case "reverse":   root.defaultReverse = r.value === "1"; break
                    case "hidden":    root.defaultHidden = r.value === "1"; break
                    case "view":      root.viewMode = r.value || "auto"; break
                    // Only when the desktop has not already answered. Kept as
                    // the fallback for a machine with synfiles but no synui,
                    // where there is no font.state and nothing to write one.
                    case "text_scale":
                        root.configScale = parseInt(r.value) || 100
                        if (!root.scaleFromDesktop) root.textScale = root.configScale
                        break
                    // Read straight into the property rather than through
                    // toggleSplit(): that function adopts one pane's state into
                    // the other and focuses the new pane, neither of which is
                    // what restoring a remembered layout means.
                    case "split":     root.split = r.value === "1"; break
                    case "sidebar":   root.showSidebar = r.value === "1"; break
                    }
                }
                // Only after applying, or the act of applying would write every
                // default straight back out as if the user had chosen it.
                root.settingsLoaded = true

                // ── One-time migration of the text scale ────────────────────
                //
                // Anyone who set a text size before it became a desktop setting
                // has it in this config and nowhere else, so the desktop still
                // reads 100 and their other windows stay small — the exact
                // symptom that prompted moving it. Pushing it once makes the
                // move invisible: the size they already chose simply starts
                // applying everywhere.
                //
                // Guarded three ways so it cannot fight the desktop: only when
                // font.state carried no scale at all, only when the config
                // holds something other than the default, and only once per
                // process. After this the desktop is authoritative and this
                // branch is never reached again.
                if (!root.scaleFromDesktop && root.configScale !== 100
                    && !root.scaleMigrated) {
                    root.scaleMigrated = true
                    Quickshell.execDetached(["synui-apply-font", "--scale",
                                             String(root.configScale)])
                }

                // The first tab was built before the settings arrived, so it
                // gets them applied and re-read. Without this the remembered
                // sort only took effect on the SECOND tab, which reads as
                // "it forgot".
                if (paneA.tab) {
                    paneA.setTab({ sort: root.defaultSort,
                                   reverse: root.defaultReverse,
                                   showHidden: root.defaultHidden })
                    paneA.reload()
                }
                // A remembered split has no second pane yet — the window was
                // built with one. Without this the setting restored a divider
                // with an empty half beside it.
                if (root.split)
                    paneB.ensureStarted(paneA.tab ? paneA.tab.path
                                                  : root.encodePath(root.homeDir))
                if (root.showTree
                    && root.treeChildren[root.encodePath(root.homeDir)] === undefined)
                    root.treeToggle(root.encodePath(root.homeDir))
            }
        }
    }

    Process { id: cfgWriteProc }

    function saveSetting(key, value) {
        if (!root.settingsLoaded) return
        cfgWriteProc.command = [root.bin, "config", "set", key, "" + value]
        cfgWriteProc.running = true
    }

    // The slider fires on every pixel of a drag. Writing a file per pixel is
    // both wasteful and a good way to lose a write to a rename() race, so the
    // save is coalesced to the end of the gesture.
    Timer {
        id: iconSaveTimer
        interval: 400
        onTriggered: root.saveSetting("icon_size", root.iconSize)
    }
    onIconSizeChanged: if (root.settingsLoaded) iconSaveTimer.restart()

    // Same bargain for the text slider, and for the same reason: a file write
    // per pixel of a drag is both wasteful and a good way to lose one to a
    // rename() race.
    Timer {
        id: textSaveTimer
        interval: 400
        onTriggered: {
            root.saveSetting("text_scale", root.textScale)
            // ...and to the desktop, which is what carries it to the other
            // windows. Best effort by design: on a box with synfiles and no
            // synui there is no helper and no font.state, and the config key
            // above is then the whole story rather than half of one.
            Quickshell.execDetached(["synui-apply-font", "--scale",
                                     String(root.textScale)])
        }
    }
    onTextScaleChanged: if (root.settingsLoaded) textSaveTimer.restart()

    // These are single clicks, so they save immediately.
    onThumbsChanged: {
        root.saveSetting("previews", root.thumbs ? 1 : 0)
        root.refreshPeekAll()
    }
    onShowTreeChanged: root.saveSetting("tree", root.showTree ? 1 : 0)
    onShowSidebarChanged: root.saveSetting("sidebar", root.showSidebar ? 1 : 0)
    onViewModeChanged: root.saveSetting("view", root.viewMode)

    // ── Address bar ─────────────────────────────────────────────────────────
    //
    // Breadcrumbs are faster to click; a text field is the only way to paste a
    // path, or type one that is not on screen. Every browser and every other
    // file manager offers both, switched by clicking the empty space beside
    // the crumbs or pressing Ctrl+L.
    property bool editingPath: false

    function beginEditPath() {
        if (!root.tab || root.tab.view !== "dir") return
        root.editingPath = true
    }

    // Typed by a person, so it is RAW text — the one place a path enters this
    // program already decoded. It is encoded on the way in, which is the exact
    // inverse of everywhere else and the reason this is worth a comment.
    function commitPath(text) {
        root.editingPath = false
        let p = text.trim()
        if (!p) return
        if (p.indexOf("file://") === 0) p = p.substring(7)
        if (p.indexOf("~") === 0) p = root.homeDir + p.substring(1)
        if (p.indexOf("/") !== 0) p = root.disp(root.tab.path) + "/" + p
        root.navigate(root.encodePath(p), "dir")
    }

    // ── Hamburger ───────────────────────────────────────────────────────────
    property bool menuOpen: false

    // ── View menu ───────────────────────────────────────────────────────────
    // Sort, hidden files, previews, the tree and icon size were nine chips in
    // a row above the list. They are one button's worth of menu now, next to
    // Forward, which is where a Dolphin user's hand goes for them.
    property bool viewMenuOpen: false

    // Every entry is a STATE, so each one re-applies whatever else it needs —
    // sort and hidden also persist as the default a new tab starts from.
    function applyViewAction(act) {
        if (!root.tab) return
        if (act.indexOf("view:") === 0) {
            root.viewMode = act.substring(5)
            return
        }
        if (act.indexOf("sort:") === 0) {
            const next = act.substring(5)
            root.setTab({ sort: next })
            root.defaultSort = next
            root.saveSetting("sort", next)
            root.reload()
            return
        }
        switch (act) {
        case "reverse": {
            const v = !root.tab.reverse
            root.setTab({ reverse: v })
            root.defaultReverse = v
            root.saveSetting("reverse", v ? 1 : 0)
            root.reload()
            break
        }
        case "hidden": {
            const v = !root.tab.showHidden
            root.setTab({ showHidden: v })
            root.defaultHidden = v
            root.saveSetting("hidden", v ? 1 : 0)
            root.reload()
            break
        }
        case "thumbs": root.thumbs = !root.thumbs; break   // onThumbsChanged saves
        case "split": root.toggleSplit(); break            // onSplitChanged saves
        case "sidebar": root.showSidebar = !root.showSidebar; break  // onShowSidebarChanged saves
        case "tree":
            root.showTree = !root.showTree
            if (root.showTree && root.treeChildren[root.encodePath(root.homeDir)] === undefined)
                root.treeToggle(root.encodePath(root.homeDir))
            break
        }
    }

    // ── Undo ────────────────────────────────────────────────────────────────
    //
    // The label says WHAT would be undone, not just "Undo". A recovery control
    // that does not tell you what it is about to reverse is one nobody presses
    // when it matters.
    property string undoLabel: ""

    Process {
        id: undoListProc
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = root.parseRecords(this.text)
                root.undoLabel = rows.length > 0 ? rows[0].what : ""
            }
        }
    }

    function refreshUndo() {
        undoListProc.command = [root.bin, "--rec", "undo", "list"]
        undoListProc.running = true
    }

    function doUndo() {
        if (root.undoLabel === "") return
        root.runOp(["undo"], "undoing " + root.undoLabel + "…")
    }

    // ── Borrowed menu entries ───────────────────────────────────────────────
    //
    // Open With and the Extract / Set as Wallpaper / Mount ISO style entries
    // are not built in — they come from the desktop's own data, and on
    // SynapseOS five of the service menus are synui's. Asked for fresh each
    // time the menu opens, because they depend on what is selected.
    property var rowActions: []

    Process {
        id: actionsProc
        stdout: StdioCollector {
            onStreamFinished: root.rowActions = root.parseRecords(this.text)
        }
    }

    function loadActions() {
        root.rowActions = []
        const paths = root.selectedPaths()
        if (paths.length === 0) return
        actionsProc.command = [root.bin, "--rec", "actions"].concat(paths)
        actionsProc.running = true
    }

    // The GUI names an action; the binary builds the command line. Exec
    // parsing and %F/%f/%U substitution stay in one testable place instead of
    // being reinvented in QML string handling.
    function runAction(desktop, actionId) {
        const paths = root.selectedPaths()
        if (paths.length === 0) return
        const args = ["action", desktop]
        if (actionId) args.push(actionId)
        args.push("--")
        root.runOp(args.concat(paths), "running…")
    }

    // Detached for the same reason openFile is: the terminal runs until it is
    // closed, and on a shared Process the SECOND "Open Terminal Here" silently
    // did nothing until the first window was gone.
    function openTerminalHere() {
        const dir = root.tab && root.tab.view === "dir"
                    ? root.disp(root.tab.path) : root.homeDir
        Quickshell.execDetached(["sh", "-c",
            'cd "$1" || exit 1; ' +
            'for t in syntty kitty foot alacritty konsole xterm; do ' +
            '  command -v "$t" >/dev/null 2>&1 && exec "$t"; done; ' +
            'echo "no terminal emulator found" >&2; exit 127',
            "sh", dir])
        root.statusLine = "opened a terminal in " + dir
    }

    function createEmptyFile(name) {
        root.creatingFile = false
        if (!name) return
        // No `touch` verb in the binary on purpose — this is the one place a
        // GUI needs it, and `mkdir` plus a redirect is not a file manager's
        // job. sh is already how the terminal is launched.
        newFileProc.command = ["sh", "-c", 'exec touch -- "$1/$2"', "sh",
                               root.disp(root.tab.path), name]
        newFileProc.running = true
    }
    property bool creatingFile: false
    Process {
        id: newFileProc
        onExited: root.reloadAll()
    }

    // ── Disk context menu ───────────────────────────────────────────────────
    // Its own menu rather than a branch of the file one: nothing a disk offers
    // (mount, eject, pin the mount point) applies to a file, and nothing the
    // file menu offers applies to a disk.
    property var diskMenu: null      // the volume record, or null when closed
    property real diskMenuX: 0
    property real diskMenuY: 0

    // ── Opening a drive in the disk utility ─────────────────────────────────
    //
    // syn-disks is an optdepend, not a dependency: this is still a file
    // manager on a machine that has not installed it. So the entry is PROBED
    // for rather than shown unconditionally — a menu entry that silently does
    // nothing is the exact failure this codebase keeps re-learning (Rename in
    // icon view, drag-and-drop in the grid, the dock's New Window).
    //
    // The probe runs `sh -c 'command -v …'` rather than the tool itself: sh
    // always exists, so the check itself can never be the thing that is
    // missing, and it costs one short-lived process at startup.
    property bool haveDisks: false

    Process {
        id: disksProbe
        command: ["sh", "-c", "command -v syn-disks >/dev/null 2>&1"]
        running: true
        onExited: (code) => root.haveDisks = (code === 0)
    }

    // Format needs a NEWER syn-disks than "Open in Disks" does — `gui
    // --format` arrived with this entry. Probed for the capability rather
    // than assumed from the binary being there: an older syn-disks answers
    // `--format` with "not a block device" and exits, and because the window
    // is launched detached that lands nowhere at all. A menu entry that does
    // nothing visible is the failure this file keeps a list of.
    property bool haveFormat: false

    Process {
        id: formatProbe
        command: ["sh", "-c", "syn-disks --help 2>&1 | grep -q -- 'gui --format'"]
        running: true
        onExited: (code) => root.haveFormat = (code === 0)
    }

    // ⚠ execDetached, NOT a Process — `syn-disks gui` execs quickshell in the
    // FOREGROUND and lives as long as its window, so a shared Process would
    // queue the next launch behind it and every later one would look dead
    // until this window was closed. See the start menu, which had exactly this
    // bug. Detaching also stops synfiles quitting from taking the disk window
    // with it.
    function openInDisks(vol) {
        if (!vol || !vol.device) return
        // The device is ASCII (/dev/<kernel name>), so encoded and decoded are
        // the same string — but it goes through disp() anyway, because "this
        // field happens not to need decoding" is a fact that stops being true
        // the moment somebody changes what the field holds.
        Quickshell.execDetached(["syn-disks", "gui", root.disp(vol.device)])
        root.statusLine = "opened " + root.disp(vol.device) + " in Disks"
    }

    // ── Formatting a stick ──────────────────────────────────────────────────
    //
    // The entry is here; the dialogue is syn-disks'. That split is deliberate
    // and worth defending: what may be formatted is decided by syn-disks'
    // guard.c and NOTHING re-derives it, the dry run that says what would be
    // erased is a `--rec` contract this window would have to reimplement, and
    // the refusal it hands back carries the way out ("it is mounted — unmount
    // it") which that dialogue already offers a button for. A second format
    // dialogue in this file would be a second set of those rules to keep in
    // step, and the first time they disagreed it would be about erasing a
    // disk.
    //
    // So this opens the real one, on the device that was right-clicked. It
    // asks before it does anything; nothing formats from a menu click here.
    function formatVolume(vol) {
        if (!vol || !vol.device) return
        Quickshell.execDetached(["syn-disks", "gui", "--format", root.disp(vol.device)])
        root.statusLine = "format " + root.disp(vol.device) + " — asking in Disks"
    }

    function openDiskMenu(vol, gx, gy) {
        root.diskMenu = vol
        root.diskMenuX = gx
        root.diskMenuY = gy
    }

    function copyToClipboard(text) {
        // No clipboard API in this QML surface, so it goes through the tool
        // every desktop already has. Failing quietly is fine — the status line
        // says what was attempted.
        clipProc.command = ["wl-copy", "--", text]
        clipProc.running = true
        root.statusLine = "copied: " + text
    }
    Process { id: clipProc }

    // ── Mounting ────────────────────────────────────────────────────────────
    // Clicking a drive means "open this drive", so mounting one is only half
    // the job — a stick that mounts and then sits there is indistinguishable
    // from a click that did nothing. The device is remembered and opened once
    // the refreshed list says it landed somewhere, rather than by parsing
    // udisksctl's sentence: the record stream already carries the mount point.
    property string pendingOpenDev: ""

    function mountVolume(vol, thenOpen) {
        if (!vol.device) return
        root.pendingOpenDev = thenOpen ? vol.device : ""
        root.runOp(["mount", vol.device], "mounting " + vol.title + "…")
    }
    function unmountVolume(vol) {
        if (!vol.device) return
        root.runOp(["unmount", vol.device], "unmounting " + vol.title + "…")
    }

    // ── Emptying the trash ──────────────────────────────────────────────────
    // The only irreversible thing this window can do, so it asks, and the
    // confirmation is what supplies --yes. The binary refuses without it.
    property bool confirmEmpty: false

    function emptyTrash() {
        root.confirmEmpty = false
        root.runOp(["trash", "empty", "--yes"], "emptying the trash…")
    }

    // ── New folder ──────────────────────────────────────────────────────────
    property bool creating: false

    function commitNewFolder(name) {
        root.creating = false
        if (!name) return
        root.runOp(["mkdir", root.disp(root.tab.path) + "/" + name],
                   "creating " + name)
    }

    Process {
        id: placesProc
        command: [root.bin, "--rec", "places"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.places = root.parseRecords(this.text).filter(
                    r => r.hidden !== "1" && r.kind === "path")
            }
        }
    }

    // ── Network discovery ───────────────────────────────────────────────────
    //
    // volumes.c lists network places that are already PATHS — gvfs has them
    // mounted, so they are folders like any other. This is the step before
    // that: `synfiles netscan` asks the network what it has (mDNS, and NetBIOS
    // for the Windows machines that announce nothing else), and the rows it
    // returns are OFFERS, not places. Nothing is mounted until one is clicked.
    //
    // On demand and never on a timer: a scan spawns avahi-browse and a
    // smbclient per host, and doing that every time the sidebar refreshes would
    // put a second of subprocesses in front of every navigation. It is also not
    // something to do to a network nobody asked about.
    property var netRows: []
    property bool netScanning: false
    property bool netScanned: false
    property string netPendingUri: ""

    // What to OFFER: discovered rows that are not already in the mounted list.
    // Matched on the gvfs path when there is one, because a discovered share
    // and its mount are the same thing under two names and listing both is how
    // a sidebar starts lying about how many servers exist.
    readonly property var netDiscovered: {
        const mounted = root.networkVolumes.map(v => v.path)
        return root.netRows.filter(r => r.kind === "share" || r.kind === "host")
                           .filter(r => r.mounted !== "1" || mounted.indexOf(r.path) < 0)
    }

    function scanNetwork() {
        if (root.netScanning) return
        root.netScanning = true
        root.statusLine = "looking for network shares…"
        netProc.running = true
    }

    // Mount a discovered row, then go there. gvfs owns the credential prompt,
    // so a share that needs a password is not a failure here — it is a dialogue
    // somewhere else, and the refresh below picks it up when it succeeds.
    function mountNetwork(row) {
        if (!row.uri) return
        if (row.mounted === "1" && row.path) { root.navigate(row.path, "dir"); return }
        root.netPendingUri = row.uri
        root.runOp(["netmount", root.disp(row.uri)], "mounting " + row.title + "…")
    }

    Process {
        id: netProc
        command: [root.bin, "--rec", "netscan"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.netRows = root.parseRecords(this.text)
                root.netScanning = false
                root.netScanned = true
                root.statusLine = root.netRows.length > 0
                    ? root.netRows.length + " network place(s) found"
                    : "nothing announced itself on this network"

                // A mount that was waiting on this scan to learn where it
                // landed. Cleared unconditionally — a mount that failed must
                // not leave a request that fires on some later scan and
                // navigates out from under whatever the user is doing, which is
                // the same rule pendingOpenDev follows for disks.
                if (root.netPendingUri !== "") {
                    const want = root.netPendingUri
                    root.netPendingUri = ""
                    for (const r of root.netRows) {
                        if (r.uri === want && r.mounted === "1" && r.path) {
                            root.navigate(r.path, "dir")
                            break
                        }
                    }
                }
            }
        }
    }

    Process {
        id: volProc
        command: [root.bin, "--rec", "volumes"]
        stdout: StdioCollector {
            onStreamFinished: {
                // Unmounted volumes are KEPT. A disk that is plugged in but not
                // mounted is the one case where a file manager has something
                // useful to offer, and hiding it means the only way to reach
                // the drive is to already know it exists.
                root.volumes = root.parseRecords(this.text)

                if (root.pendingOpenDev !== "") {
                    const dev = root.pendingOpenDev
                    // Cleared unconditionally: a mount that FAILED must not
                    // leave a request that fires on some later refresh and
                    // navigates out from under whatever the user is doing.
                    root.pendingOpenDev = ""
                    for (const v of root.volumes) {
                        if (v.device === dev && v.mounted === "1" && v.path) {
                            root.navigate(v.path, "dir")
                            break
                        }
                    }
                }
            }
        }
    }

    // Opening a file is xdg-open's job. Re-deriving "what opens a .kra" from
    // mimeapps.list would be a second, worse implementation of a thing every
    // desktop already agrees on.
    //
    // ⚠ execDetached, NOT a Process, and this was a real bug: **xdg-open does
    // not return until the application it started exits.** Measured — `xdg-open
    // notes.txt` with Kate as the handler sat there for the full six seconds of
    // a timeout, and Kate was still running.
    //
    // A shared Process then holds that command for the LIFETIME OF THE APP, and
    // assigning `command` to a running quickshell Process does nothing. So the
    // second file you opened did nothing at all, and kept doing nothing until
    // the first application was closed — with no error anywhere, because
    // nothing had failed. Same shape as the dock's queued launches and the
    // reason Open in Disks was written this way from the start.
    function openFile(pathEnc) {
        Quickshell.execDetached(["xdg-open", root.disp(pathEnc)])
        root.statusLine = "opening " + root.disp(root.baseEnc(pathEnc))
    }

    Process {
        id: pinProc
        onExited: placesProc.running = true
    }
    function pin(pathEnc)   { pinProc.command = [root.bin, "places", "pin", root.disp(pathEnc)];   pinProc.running = true }
    function unpin(pathEnc) { pinProc.command = [root.bin, "places", "unpin", root.disp(pathEnc)]; pinProc.running = true }

    function isPinned(pathEnc) {
        for (const pl of root.places)
            if (pl.href === pathEnc) return true
        return false
    }

    // What a row is called on screen, which is not always what it is called in
    // the selection: a trashed file keeps its original name here and its unique
    // trash name as its identity.
    function rowLabel(row) {
        return root.disp(row.label !== undefined && row.label !== ""
                         ? row.label : row.name)
    }

    function activate(row) {
        if (row.type === "dir") root.navigate(row.full, "dir")
        else if (!row.missing)  root.openFile(row.full)
    }

    Component.onCompleted: {
        const start = Quickshell.env("SYNFILES_DIR") || root.homeDir
        // Named, not root.newTab(): the active pane is paneA at this point and
        // will be, but a window opening depends on which pane it starts in and
        // that should not be inferred from a variable.
        paneA.newTab(root.encodePath(start), "dir")
        placesProc.running = true
        volProc.running = true
        root.scanThumbs()
        root.refreshUndo()
        cfgReadProc.running = true
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // ── Toolbar ─────────────────────────────────────────────────────────
        // Full width and closed by a rule, the shape Dolphin has: navigation
        // on the left, the address in the middle, and ONE line under all of it
        // that everything else hangs below — sidebar included. Recent, Trash
        // and Places start under that line rather than sharing the toolbar's
        // row, which is what made the sidebar look like it began in the wrong
        // place.
        Rectangle {
            id: toolBar
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 38
            color: root.cPanel

            Row {
                id: navGroup
                anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                spacing: 2

                // Back and Forward are per TAB. One shared history would send
                // Back to a folder this tab was never in.
                ToolButton {
                    glyph: "←"
                    hint: "Back"
                    dim: !root.canGoBack
                    onActivated: root.goBack()
                }
                ToolButton {
                    glyph: "→"
                    hint: "Forward"
                    dim: !root.canGoForward
                    onActivated: root.goForward()
                }
                ToolButton {
                    glyph: "↑"
                    hint: "Up one folder"
                    dim: !(root.tab && root.tab.view === "dir")
                    onActivated: {
                        if (root.tab && root.tab.view === "dir")
                            root.navigate(root.parentEnc(root.tab.path), "dir")
                    }
                }
                // Everything about how the list LOOKS lives behind this one
                // button, next to Forward, where Dolphin keeps it: sort order,
                // hidden files, icon size, previews, the tree. They were nine
                // chips in a row that had to be read left to right every time.
                ToolButton {
                    id: viewBtn
                    glyph: "▤"
                    // The word costs about 50 px of a toolbar that has none to
                    // spare when cascaded. It goes, and the hint takes over
                    // saying what the glyph means — which is why the hint is
                    // filled in here rather than left empty as it was when the
                    // label was always present.
                    label: toolBar.width >= 520 ? "View" : ""
                    hint: toolBar.width >= 520 ? "" : "View options"
                    active: root.viewMenuOpen
                    onActivated: root.viewMenuOpen = !root.viewMenuOpen
                }
            }

            Rectangle {
                id: addressBar
                // x and width, NOT left/right anchors.
                //
                // Anchored between two groups, this went to a NEGATIVE width
                // the moment they met — and clip: true on a negative-width
                // Rectangle does not clip, so everything inside was free to
                // paint across the whole toolbar. That is the failure mode the
                // clip was added to prevent, and it was the one case where clip
                // could not help. Floored at 0 the clip always applies: the bar
                // shrinks to nothing and takes its contents with it, instead of
                // spilling them over the buttons.
                anchors.verticalCenter: parent.verticalCenter
                x: navGroup.x + navGroup.width + 6
                width: Math.max(0, toolActions.x - 8 - x)
                height: 28
                radius: 4
                color: root.cBg
                // Nothing inside may paint outside. A long path used to run
                // straight through this rounded border and over the toolbar
                // buttons on either side — visible in the 2026-08-10 00:32
                // screenshot, where "› Downloads" sits outside the box.
                clip: true
                border {
                    width: 1
                    color: root.editingPath ? root.cAccent : root.wash(0.18)
                }

                // Breadcrumbs, until somebody wants to type.
                //
                // x rather than a left anchor. A short path sits at the left
                // margin like it always did; a long one slides left by exactly
                // its overflow so the END stays visible and clip eats the head.
                // Anchoring left and clipping would cut off the folder you are
                // actually in and leave you reading "/ › home"; anchoring right
                // would park short paths against the wrong edge.
                Row {
                    id: crumbs
                    anchors.verticalCenter: parent.verticalCenter
                    x: Math.min(10, parent.width - 10 - width)
                    spacing: 0
                    visible: !root.editingPath && root.tab && root.tab.view === "dir"

                    Repeater {
                        model: {
                            if (!root.tab || root.tab.view !== "dir") return []
                            const parts = root.tab.path.split("/").filter(x => x !== "")
                            const out = [{ label: "/", path: "/" }]
                            let acc = ""
                            for (const seg of parts) {
                                acc = acc + "/" + seg
                                out.push({ label: root.disp(seg), path: acc })
                            }
                            return out
                        }
                        delegate: Row {
                            id: crumb
                            required property var modelData
                            Text {
                                text: " › "
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                                visible: crumb.modelData.path !== "/"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: crumb.modelData.label
                                color: crumbMa.containsMouse ? root.cAccent : root.cText
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    id: crumbMa
                                    anchors { fill: parent; margins: -3 }
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.navigate(crumb.modelData.path, "dir")
                                }
                            }
                        }
                    }
                }

                // A non-directory view has no path to show, so it says what
                // it is instead of leaving an empty box.
                Text {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    visible: !root.editingPath && root.tab && root.tab.view !== "dir"
                    text: root.tab ? (root.tab.view === "recent" ? "Recently Modified"
                                    : root.tab.view === "trash"  ? "Trash"
                                    : "About") : ""
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                }

                TextInput {
                    id: pathInput
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: TextInput.AlignVCenter
                    visible: root.editingPath
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                    clip: true
                    selectByMouse: true
                    onVisibleChanged: {
                        if (visible) {
                            text = root.disp(root.tab.path)
                            forceActiveFocus()
                            selectAll()
                        }
                    }
                    onAccepted: root.commitPath(text)
                    Keys.onEscapePressed: root.editingPath = false
                }

                // Clicking the bar's empty space switches to editing, the
                // way a browser's does.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    enabled: !root.editingPath
                    cursorShape: Qt.IBeamCursor
                    onClicked: root.beginEditPath()
                    z: -1
                }
            }

            Row {
                id: toolActions
                anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                spacing: 2

                // Undo is the one journal, shared by both panes, so it
                // belongs to the window and not to a pane's tab strip — which
                // is where it used to live, and where a split window would
                // have drawn two of it.
                ToggleChip {
                    // Named, not just "Undo": a recovery control that does not
                    // say what it reverses is one nobody dares press.
                    //
                    // But the name is a FILENAME, and this chip had no width
                    // limit — so "↶ Move to Trash of synapse-20260811-121046.png"
                    // made this Row 474 px wide inside a 347 px window. Anchored
                    // right, that put its left edge at -134, which dragged the
                    // address bar's right anchor NEGATIVE, and a Rectangle with
                    // negative width does not clip — so its breadcrumbs painted
                    // straight across the toolbar and over the View button. One
                    // long undo label took the whole bar apart.
                    //
                    // Capped at a third of the toolbar, and dropped entirely
                    // when the bar is tight: navigation and the address are
                    // what a file manager cannot do without, and Ctrl+Z still
                    // undoes with no chip on screen.
                    label: "↶ " + root.undoLabel
                    maxWidth: Math.max(90, toolBar.width * 0.33)
                    on: false
                    visible: root.undoLabel !== "" && toolBar.width >= 520
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: root.doUndo()
                }
                ToolButton {
                    // Drawn, not a theme icon: it is the one glyph here that
                    // has to say WHICH pane is which, and Qt cannot re-tint a
                    // theme icon in a running process (see FolderIcon).
                    splitIcon: true
                    hint: root.split ? "Close split view" : "Split view (F3)"
                    active: root.split
                    onActivated: root.toggleSplit()
                }
                ToolButton { glyph: "⌕"; hint: "Search"; onActivated: root.beginSearch() }
                ToolButton {
                    glyph: "☰"
                    hint: "Menu"
                    active: root.menuOpen
                    onActivated: root.menuOpen = !root.menuOpen
                }
            }

            // The rule under the toolbar. Full width, so the sidebar hangs
            // below it too.
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: root.wash(0.25)
            }
        }

        // ── Sidebar ─────────────────────────────────────────────────────────
        Rectangle {
            id: sidebar
            anchors { top: toolBar.bottom; left: parent.left; bottom: parent.bottom }
            // Width AND visible, because the pane beside it anchors to
            // `sidebar.right`: hiding it without collapsing the width would
            // leave a 220px hole where the panel used to be, and collapsing the
            // width without hiding it would draw the panel's own background
            // colour as a hairline down the left edge.
            width: root.showSidebar ? 220 : 0
            visible: root.showSidebar
            color: root.cPanel

            Flickable {
                id: sideScroll
                anchors {
                    top: parent.top; left: parent.left; right: parent.right
                    bottom: parent.bottom
                }
                anchors.topMargin: 8
                // The gutter the scrollbar lives in. Reserved always, or the
                // eject glyph ends up underneath the handle exactly when a
                // long sidebar makes both appear.
                // Zero when the panel is collapsed: a 12px gutter inside a
                // 0px panel is a Flickable with width -12, which QML reports as
                // a binding warning on every toggle.
                anchors.rightMargin: root.showSidebar ? 12 : 0
                contentHeight: sideCol.implicitHeight
                clip: true

                Column {
                    id: sideCol
                    width: parent.width
                    spacing: 2


                    // Views that are not directories. About is NOT here — it is
                    // in the footer below, because it is read once and these
                    // are used constantly.
                    Repeater {
                        model: [{ label: "Recent",  icon: "document-open-recent", view: "recent" },
                                { label: "Trash",   icon: "user-trash",           view: "trash" }]
                        delegate: SideRow {
                            id: viewRow
                            required property var modelData
                            label: viewRow.modelData.label
                            iconName: viewRow.modelData.icon
                            active: root.tab && root.tab.view === viewRow.modelData.view
                            onActivated: root.navigate("", viewRow.modelData.view)
                        }
                    }

                    Item { width: 1; height: 10 }
                    SideHeading {
                        text: "Folders"
                        visible: root.showTree
                    }

                    Repeater {
                        model: root.showTree ? root.treeRows : []
                        delegate: Rectangle {
                            id: treeRow
                            required property var modelData
                            readonly property bool current:
                                root.tab && root.tab.view === "dir"
                                && root.tab.path === treeRow.modelData.full
                            width: sideScroll.width
                            height: 24
                            color: treeRow.dropHover ? root.wash(0.40)
                                 : treeRow.current ? root.wash(0.18)
                                 : (treeMa.containsMouse ? root.wash(0.08) : "transparent")
                            property bool dropHover: false

                            // The twisty is its own hit target: expanding a
                            // folder and going into it are different intentions
                            // and must not share a click.
                            Text {
                                id: twisty
                                anchors {
                                    left: parent.left
                                    leftMargin: 8 + treeRow.modelData.depth * 12
                                    verticalCenter: parent.verticalCenter
                                }
                                width: 12
                                text: treeRow.modelData.open ? "▾" : "▸"
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(10) }
                                MouseArea {
                                    anchors { fill: parent; margins: -4 }
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.treeToggle(treeRow.modelData.full)
                                }
                            }
                            Text {
                                anchors {
                                    left: twisty.right; leftMargin: 4
                                    right: parent.right; rightMargin: 8
                                    verticalCenter: parent.verticalCenter
                                }
                                text: root.disp(treeRow.modelData.name)
                                elide: Text.ElideRight
                                color: treeRow.current ? root.cAccent : root.cText
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }

                            DropArea {
                                anchors.fill: parent
                                onEntered: (drag) => {
                                    treeRow.dropHover =
                                        root.willAcceptDrop(treeRow.modelData.full, drag)
                                    if (!treeRow.dropHover) drag.accepted = false
                                }
                                onExited: treeRow.dropHover = false
                                onDropped: (drop) => {
                                    treeRow.dropHover = false
                                    root.handleDrop(treeRow.modelData.full, drop)
                                }
                            }
                            MouseArea {
                                id: treeMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.navigate(treeRow.modelData.full, "dir")
                            }
                        }
                    }

                    Item { width: 1; height: 10 }
                    SideHeading { text: "Places" }

                    Repeater {
                        model: root.places
                        delegate: SideRow {
                            required property var modelData
                            label: modelData.title || root.disp(root.baseEnc(modelData.href))
                            iconName: modelData.icon || "folder"
                            active: root.tab && root.tab.view === "dir"
                                    && root.tab.path === modelData.href
                            removable: modelData.system !== "1"
                            dropTarget: modelData.href
                            onActivated: root.navigate(modelData.href, "dir")
                            onRemoved: root.unpin(modelData.href)
                        }
                    }

                    // Three headings rather than one, because a USB stick and
                    // an internal disk want different things from you: one is
                    // ejected, the other never is.
                    Item { width: 1; height: 10 }
                    SideHeading {
                        text: "Devices"
                        visible: root.fixedVolumes.length > 0
                    }

                    Repeater {
                        model: root.fixedVolumes
                        delegate: SideRow {
                            id: volRow
                            required property var modelData
                            readonly property bool isMounted: volRow.modelData.mounted === "1"
                            label: volRow.modelData.title
                            iconName: volRow.modelData.icon || "drive-harddisk"
                            sub: volRow.modelData.size
                            // An unmounted volume is drawn dimmed rather than
                            // hidden: it is reachable, it just needs one click
                            // first, and that is worth showing.
                            dim: !volRow.isMounted
                            active: root.tab && root.tab.view === "dir"
                                    && volRow.isMounted
                                    && root.tab.path === volRow.modelData.path
                            // Eject on a mounted volume, mount on one that is
                            // not. Both go through udisks2.
                            trailing: volRow.isMounted ? "\u23cf" : "\u25b8"
                            trailingHint: volRow.isMounted ? "unmount" : "mount"
                            dropTarget: volRow.isMounted ? volRow.modelData.path : ""
                            usedBytes: parseFloat(volRow.modelData.used || "0")
                            totalBytes: parseFloat(volRow.modelData.total || "0")
                            onActivated: {
                                if (volRow.isMounted) root.navigate(volRow.modelData.path, "dir")
                                else root.mountVolume(volRow.modelData, true)
                            }
                            onTrailingClicked: {
                                if (volRow.isMounted) root.unmountVolume(volRow.modelData)
                                else root.mountVolume(volRow.modelData)
                            }
                            onContextRequested: (gx, gy) => root.openDiskMenu(volRow.modelData, gx, gy)
                        }
                    }

                    Item { width: 1; height: 10 }
                    SideHeading {
                        text: "Removable Devices"
                        visible: root.removableVolumes.length > 0
                    }

                    Repeater {
                        model: root.removableVolumes
                        delegate: SideRow {
                            id: remRow
                            required property var modelData
                            readonly property bool isMounted: remRow.modelData.mounted === "1"
                            label: remRow.modelData.title
                            iconName: remRow.modelData.icon || "drive-removable-media"
                            dim: !remRow.isMounted
                            active: root.tab && root.tab.view === "dir"
                                    && remRow.isMounted
                                    && root.tab.path === remRow.modelData.path
                            usedBytes: parseFloat(remRow.modelData.used || "0")
                            totalBytes: parseFloat(remRow.modelData.total || "0")
                            trailing: remRow.isMounted ? "\u23cf" : "\u25b8"
                            onActivated: {
                                if (remRow.isMounted) root.navigate(remRow.modelData.path, "dir")
                                else root.mountVolume(remRow.modelData, true)
                            }
                            onTrailingClicked: {
                                if (remRow.isMounted) root.unmountVolume(remRow.modelData)
                                else root.mountVolume(remRow.modelData)
                            }
                            onContextRequested: (gx, gy) => root.openDiskMenu(remRow.modelData, gx, gy)
                        }
                    }

                    Item { width: 1; height: 10 }
                    // ⚠ ALWAYS VISIBLE now, where it used to appear only once
                    // something was already mounted. That is backwards for the
                    // one section whose entire problem is that you cannot reach
                    // a share you have not mounted yet: the heading was hidden
                    // exactly when somebody was looking for it, and the only way
                    // to reach a NAS was to know its smb:// URI and type it.
                    SideHeading { text: "Network" }

                    Repeater {
                        model: root.networkVolumes
                        delegate: SideRow {
                            id: netRow
                            required property var modelData
                            label: netRow.modelData.title
                            iconName: netRow.modelData.icon || "folder-network"
                            active: root.tab && root.tab.view === "dir"
                                    && root.tab.path === netRow.modelData.path
                            onActivated: root.navigate(netRow.modelData.path, "dir")
                            onContextRequested: (gx, gy) => root.openDiskMenu(netRow.modelData, gx, gy)
                        }
                    }

                    // Discovered but not mounted — an OFFER. Dim for the same
                    // reason an unmounted disk is dim: it is somewhere you can
                    // go, not somewhere you are. Clicking mounts it through
                    // gvfs and navigates when that succeeds.
                    Repeater {
                        model: root.netDiscovered
                        delegate: SideRow {
                            id: netFound
                            required property var modelData
                            label: netFound.modelData.title
                            iconName: netFound.modelData.icon || "folder-network"
                            dim: true
                            trailing: "\u25b8"
                            trailingHint: "mount " + root.disp(netFound.modelData.uri)
                            onActivated: root.mountNetwork(netFound.modelData)
                            onTrailingClicked: root.mountNetwork(netFound.modelData)
                        }
                    }

                    SideRow {
                        label: root.netScanning ? "Scanning\u2026"
                             : root.netScanned  ? "Scan again"
                                                : "Find network shares"
                        iconName: "network-workgroup"
                        dim: !root.netScanning
                        onActivated: root.scanNetwork()
                    }
                }
            }

            // The sidebar scrolls too: with a tree open and three headings of
            // devices it is longer than the window on any laptop.
            VScroll {
                flick: sideScroll
                anchors {
                    top: sideScroll.top; bottom: sideScroll.bottom
                    right: parent.right; rightMargin: 2
                }
            }
        }

        // ── The content area ────────────────────────────────────────────────
        // Everything right of the sidebar and under the toolbar: one or two
        // panes, one status bar under both of them, and the menus and dialogs
        // that belong to the window rather than to either pane.
        Item {
            anchors {
                top: toolBar.bottom; left: sidebar.right
                right: parent.right; bottom: parent.bottom
            }


            // ── The panes ───────────────────────────────────────────────────
            //
            // Two of them, always constructed; the second is shown only when
            // the split is on. They are STATIC rather than a Repeater over a
            // model because a Repeater rebuilds its delegates whenever the
            // model array's identity changes — and every state change inside a
            // pane replaces an array. Panes recreated mid-listing would lose
            // their scroll position, their keyboard focus and the Process
            // reading the directory out from under them.
            Pane {
                id: paneA
                idx: 0
                anchors { top: parent.top; bottom: statusBar.top; left: parent.left }
                width: root.split
                       ? Math.round((parent.width - splitter.width) * root.splitRatio)
                       : parent.width
            }

            // The handle between them. Draggable, because the useful split is
            // rarely the even one — copying into a deep tree wants a wide
            // source and a narrow destination.
            Rectangle {
                id: splitter
                visible: root.split
                anchors { top: parent.top; bottom: statusBar.top }
                x: paneA.width
                width: 6
                color: splitMa.pressed || splitMa.containsMouse ? root.wash(0.30)
                                                                : root.cPanel

                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: parent.height
                    color: root.wash(0.25)
                }

                MouseArea {
                    id: splitMa
                    // Wider than it looks: a 6px grab target is one nobody
                    // catches on the first try.
                    anchors { fill: parent; leftMargin: -3; rightMargin: -3 }
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    property real grabX: 0
                    onPressed: (m) => { splitMa.grabX = m.x }
                    onPositionChanged: (m) => {
                        if (!splitMa.pressed) return
                        const total = splitter.parent.width - splitter.width
                        if (total <= 0) return
                        // Clamped. A pane dragged down to nothing is a pane you
                        // can only get back by finding a six-pixel handle.
                        root.splitRatio = Math.max(0.15, Math.min(0.85,
                            (splitter.x + m.x - splitMa.grabX) / total))
                    }
                }
            }

            Pane {
                id: paneB
                idx: 1
                visible: root.split
                enabled: root.split
                anchors { top: parent.top; bottom: statusBar.top; right: parent.right }
                width: root.split ? parent.width - splitter.x - splitter.width : 0
            }


            // ── Hamburger menu ──────────────────────────────────────────────
            // Where About went, and where the things that are settings rather
            // than actions live. Keeping them out of the toolbar is what stops
            // the toolbar becoming the thing it is meant to summarise.
            MouseArea {
                anchors.fill: parent
                visible: root.menuOpen
                acceptedButtons: Qt.AllButtons
                onClicked: root.menuOpen = false
                onPressed: root.menuOpen = false
            }

            Rectangle {
                id: mainMenu
                visible: root.menuOpen
                anchors { top: parent.top; topMargin: 2; right: parent.right; rightMargin: 8 }
                width: 230
                height: Math.min(menuCol.implicitHeight + 8, parent.height - 60)
                radius: 4
                color: root.cPanel
                border { width: 1; color: root.wash(0.35) }
                z: 160

                Flickable {
                    anchors { fill: parent; margins: 4 }
                    contentHeight: menuCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: menuCol
                        width: parent.width

                        Repeater {
                            model: [
                                { label: "New Tab",              act: "newtab",   on: true },
                                { label: "New Folder…",          act: "newdir",   on: true },
                                { label: "New Empty File…",      act: "newfile",  on: true },
                                { label: "-",                    act: "",         on: true },
                                { label: root.showTree ? "Hide Folder Tree" : "Show Folder Tree",
                                  act: "tree",    on: true },
                                { label: root.thumbs ? "Hide Previews" : "Show Previews",
                                  act: "thumbs",  on: true },
                                { label: root.split ? "Close Split View" : "Split View",
                                  act: "split",   on: true },
                                { label: root.showSidebar ? "Hide Sidebar" : "Show Sidebar",
                                  act: "sidebar", on: true },
                                { label: "-",                    act: "",         on: true },
                                { label: "Show Hidden Files",    act: "hidden",   on: true },
                                { label: "Select All",           act: "selectall",on: true },
                                { label: "-",                    act: "",         on: true },
                                { label: "Search…",              act: "search",   on: true },
                                { label: "Open Terminal Here",   act: "term",     on: true },
                                { label: "-",                    act: "",         on: true },
                                { label: "Open Trash",           act: "trash",    on: true },
                                { label: "Recently Modified",    act: "recent",   on: true },
                                { label: "-",                    act: "",         on: true },
                                { label: "About SYNAPSE Files",  act: "about",    on: true }
                            ]
                            delegate: Item {
                                id: menuItem
                                required property var modelData
                                width: menuCol.width
                                height: menuItem.modelData.label === "-" ? 5 : 26

                                Rectangle {
                                    anchors { left: parent.left; right: parent.right
                                              verticalCenter: parent.verticalCenter }
                                    height: 1
                                    color: root.wash(0.25)
                                    visible: menuItem.modelData.label === "-"
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 3
                                    visible: menuItem.modelData.label !== "-"
                                    color: menuMa.containsMouse ? root.wash(0.18) : "transparent"
                                    Text {
                                        anchors { left: parent.left; leftMargin: 10
                                                  verticalCenter: parent.verticalCenter }
                                        text: menuItem.modelData.label
                                        color: root.cText
                                        font { family: root.uiFont; pixelSize: root.ui(12) }
                                    }
                                    // A tick for the ones that are states, so
                                    // the menu says what is on without being
                                    // opened twice to find out.
                                    Text {
                                        anchors { right: parent.right; rightMargin: 10
                                                  verticalCenter: parent.verticalCenter }
                                        text: {
                                            const a = menuItem.modelData.act
                                            if (a === "hidden") return root.tab && root.tab.showHidden ? "✓" : ""
                                            if (a === "tree")   return root.showTree ? "✓" : ""
                                            if (a === "thumbs") return root.thumbs ? "✓" : ""
                                            if (a === "sidebar") return root.showSidebar ? "✓" : ""
                                            return ""
                                        }
                                        color: root.cAccent
                                        font { family: root.uiFont; pixelSize: root.ui(12) }
                                    }
                                    MouseArea {
                                        id: menuMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.menuOpen = false
                                            switch (menuItem.modelData.act) {
                                            case "newtab":  root.newTab(root.tab ? root.tab.path
                                                                       : root.encodePath(root.homeDir), "dir"); break
                                            case "newdir":  root.creating = true; break
                                            case "newfile": root.creatingFile = true; break
                                            case "tree":
                                                root.showTree = !root.showTree
                                                if (root.showTree && root.treeChildren[root.encodePath(root.homeDir)] === undefined)
                                                    root.treeToggle(root.encodePath(root.homeDir))
                                                break
                                            case "thumbs":  root.thumbs = !root.thumbs; break   // onThumbsChanged saves
                                            case "split":   root.toggleSplit(); break
                                            case "sidebar": root.showSidebar = !root.showSidebar; break
                                            case "hidden": {
                                                const v = !root.tab.showHidden
                                                root.setTab({ showHidden: v })
                                                root.defaultHidden = v
                                                root.saveSetting("hidden", v ? 1 : 0)
                                                root.reload()
                                                break
                                            }
                                            case "selectall": root.selectAll(); break
                                            case "search":  root.beginSearch(); break
                                            case "term":    root.openTerminalHere(); break
                                            case "trash":   root.navigate("", "trash"); break
                                            case "recent":  root.navigate("", "recent"); break
                                            case "about":   root.navigate("", "about"); break
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // The drag SOURCE. Nothing is drawn here any more: with an
            // automatic drag the compositor carries the picture named by
            // Drag.imageSource, and a hand-positioned ghost would sit frozen
            // on screen because the system grab delivers no more mouse moves.
            Item {
                id: dragGhost
                width: 1
                height: 1

                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
                Drag.proposedAction: root.dragCopy ? Qt.CopyAction : Qt.MoveAction
                Drag.imageSource: root.dragImage
                // Capped, or the cursor carries the file at its NATURAL size —
                // a 4K screenshot dragged across the screen at 4K. The
                // thumbnail is a label for what is moving, not a preview of
                // it, so it gets the size of a large icon.
                Drag.imageSourceSize: Qt.size(96, 96)
                Drag.hotSpot: Qt.point(48, 48)
                Drag.source: dragGhost
                // Both, because "which format" is the receiving application's
                // choice: uri-list is what a file manager or the desktop takes,
                // and a terminal or a text field takes the plain paths.
                Drag.mimeData: ({
                    "text/uri-list": root.dragUris,
                    "text/plain": root.dragText
                })

                // A drag that quietly fails to start is what cost this three
                // rounds: the refusal went to a log nobody was reading while
                // the window looked simply inert. The status line says what
                // happened now, so the next failure is visible where the
                // failure is.
                Drag.onDragStarted: root.statusLine = "dragging " + root.dragLabel + "…"
                Drag.onDragFinished: (dropAction) => {
                    root.statusLine = dropAction === Qt.IgnoreAction
                                      ? "nothing accepted that drop"
                                      : ""
                }
            }

            // ── Disk context menu ───────────────────────────────────────────
            MouseArea {
                anchors.fill: parent
                visible: root.diskMenu !== null
                acceptedButtons: Qt.AllButtons
                onClicked: root.diskMenu = null
                onPressed: root.diskMenu = null
            }

            Rectangle {
                id: diskCtx
                visible: root.diskMenu !== null
                x: Math.min(root.diskMenuX, parent.width - width - 4)
                y: Math.min(root.diskMenuY, parent.height - height - 4)
                width: 200
                height: diskCol.implicitHeight + 8
                radius: 4
                color: root.cPanel
                border { width: 1; color: root.wash(0.35) }
                z: 140

                Column {
                    id: diskCol
                    anchors { fill: parent; margins: 4 }

                    Repeater {
                        model: {
                            const v = root.diskMenu
                            if (!v) return []
                            const mounted = v.mounted === "1"
                            const isNet = v.kind === "network"
                            const items = [
                                { label: "Open", act: "open", on: mounted },
                                { label: "Open in New Tab", act: "tab", on: mounted }
                            ]
                            if (!isNet) {
                                items.push({ label: "-", act: "", on: true })
                                items.push({ label: "Mount", act: "mount",
                                             on: !mounted && v.device !== "" })
                                // Ejecting an automount would fight systemd,
                                // which remounts it on the next access.
                                items.push({ label: v.kind === "removable" ? "Eject" : "Unmount",
                                             act: "unmount",
                                             on: mounted && v.device !== ""
                                                 && v.fstype !== "autofs" })
                            }
                            items.push({ label: "-", act: "", on: true })
                            items.push({ label: "Pin to Places", act: "pin", on: mounted })
                            items.push({ label: "Copy Path", act: "copypath", on: mounted })

                            // The disk utility, when it is installed and when
                            // there is a real block device to point it at.
                            // A network share has no device and an automount
                            // target that lsblk never saw has none either, so
                            // both would open a window on nothing.
                            if (root.haveDisks && !isNet && v.device !== "") {
                                items.push({ label: "-", act: "", on: true })
                                items.push({ label: "Open in Disks", act: "disks",
                                             on: true })
                                // Format is offered for REMOVABLE media only.
                                // syn-disks refuses anything sharing a disk
                                // with "/" whatever is clicked here, so this
                                // is not the safety check — the safety check
                                // is in the binary. It is about what belongs
                                // in a file manager's menu: erasing the
                                // machine's second hard disk is a thing to go
                                // to the disk utility for, and erasing the
                                // stick you just plugged in is not.
                                //
                                // Offered while it is MOUNTED too, which is
                                // the state opening a stick from here leaves
                                // it in. syn-disks refuses to format a mounted
                                // device, but its dialogue says so and offers
                                // an Unmount button — greying this out would
                                // hide the way forward behind a menu entry
                                // that looks broken.
                                if (v.kind === "removable" && root.haveFormat)
                                    items.push({ label: "Format…", act: "format",
                                                 on: true })
                            }
                            return items
                        }
                        delegate: Item {
                            id: diskItem
                            required property var modelData
                            width: diskCol.width
                            height: diskItem.modelData.label === "-" ? 5 : 26

                            Rectangle {
                                anchors { left: parent.left; right: parent.right
                                          verticalCenter: parent.verticalCenter }
                                height: 1
                                color: root.wash(0.25)
                                visible: diskItem.modelData.label === "-"
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: 3
                                visible: diskItem.modelData.label !== "-"
                                color: diskMa.containsMouse && diskItem.modelData.on
                                       ? root.wash(0.18) : "transparent"
                                Text {
                                    anchors { left: parent.left; leftMargin: 10
                                              verticalCenter: parent.verticalCenter }
                                    text: diskItem.modelData.label
                                    color: diskItem.modelData.on ? root.cText : root.cDim
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                }
                                MouseArea {
                                    id: diskMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: diskItem.modelData.on
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const v = root.diskMenu
                                        root.diskMenu = null
                                        switch (diskItem.modelData.act) {
                                        case "open":     root.navigate(v.path, "dir"); break
                                        case "tab":      root.newTab(v.path, "dir"); break
                                        case "mount":    root.mountVolume(v); break
                                        case "unmount":  root.unmountVolume(v); break
                                        case "pin":      root.pin(v.path); break
                                        case "copypath": root.copyToClipboard(root.disp(v.path)); break
                                        case "disks":    root.openInDisks(v); break
                                        case "format":   root.formatVolume(v); break
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Properties ──────────────────────────────────────────────────
            // A panel over `synfiles info`, so what it can show and what the
            // CLI can show are the same list by construction.
            Rectangle {
                anchors.centerIn: parent
                width: 460
                height: Math.min(parent.height - 60, propCol.implicitHeight + 56)
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.wash(0.35) }
                visible: root.showProps
                z: 130

                Text {
                    id: propTitle
                    anchors { top: parent.top; left: parent.left; margins: 16 }
                    text: "Properties"
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: root.ui(14); bold: true  }
                }
                Text {
                    anchors { top: parent.top; right: parent.right; margins: 14 }
                    text: "×"
                    color: propCloseMa.containsMouse ? root.cAccent : root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(16) }
                    MouseArea {
                        id: propCloseMa
                        anchors { fill: parent; margins: -6 }
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.showProps = false
                    }
                }

                Flickable {
                    anchors {
                        top: propTitle.bottom; topMargin: 10
                        left: parent.left; right: parent.right; bottom: parent.bottom
                        leftMargin: 16; rightMargin: 16; bottomMargin: 14
                    }
                    contentHeight: propCol.implicitHeight
                    clip: true

                    Column {
                        id: propCol
                        width: parent.width
                        spacing: 4

                        Repeater {
                            model: root.propRows
                            delegate: Row {
                                id: propRow
                                required property var modelData
                                width: propCol.width
                                spacing: 10

                                Text {
                                    width: 96
                                    text: propRow.modelData.key
                                    color: root.cDim
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                }
                                Text {
                                    width: propCol.width - 106
                                    // Paths and names arrive encoded like every
                                    // other record; decode for reading only.
                                    text: {
                                        const k = propRow.modelData.key
                                        const v = propRow.modelData.value
                                        if (k === "path" || k === "name" || k === "target")
                                            return root.disp(v)
                                        if (k === "size") {
                                            // For a DIRECTORY this is the size
                                            // of the directory entry, not of
                                            // what is in it — the number that
                                            // read "890 B" for a tree holding
                                            // an ISO. Say which it is; the
                                            // contents row below is the other.
                                            const t = root.propValue("type")
                                            return root.fmtSize(parseInt(v || "0"), false)
                                                 + "  (" + v + " bytes"
                                                 + (t === "dir" ? ", the folder entry itself" : "")
                                                 + ")"
                                        }
                                        if (k === "mtime" || k === "atime" || k === "ctime")
                                            return root.fmtTime(parseInt(v || "0"))
                                        // C emits one token a script can split;
                                        // the × belongs to the reader, not to
                                        // the record.
                                        if (k === "resolution")
                                            return v.replace("x", " × ")
                                        return v
                                    }
                                    color: root.cText
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }

                        // ── What the folder actually holds ─────────────────
                        //
                        // Outside the Repeater because it is not an `info`
                        // record: it arrives later, from a walk that is still
                        // running, and it updates while you watch it. A row
                        // that changes cannot come from a model that was read
                        // once.
                        Row {
                            width: propCol.width
                            spacing: 10
                            visible: root.duRunning || root.duTotal !== null

                            Text {
                                width: 96
                                text: "contents"
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                            Text {
                                width: propCol.width - 106
                                text: {
                                    if (root.duTotal === null) return "calculating…"
                                    const t = root.duTotal
                                    // The running total is shown as it climbs,
                                    // with a trailing … so a number that is
                                    // still growing is never mistaken for the
                                    // answer.
                                    return root.fmtSize(t.bytes, false)
                                         + "  (" + root.fmtCount(t.files) + " files in "
                                         + root.fmtCount(t.dirs) + " folders)"
                                         + (t.done ? "" : " …")
                                }
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }

            // ── Empty-trash confirmation ────────────────────────────────────
            //
            // HEIGHT COMES FROM THE CONTENT. It was a hardcoded 130, and the
            // content is measured in root.ui() — a font SCALE — so the box
            // could only ever be right at one scale. Everywhere else the
            // buttons hung through the bottom border, which on THIS dialog
            // means "Empty permanently" half outside the box that is asking
            // you to confirm a permanent delete.
            //
            // Bumping the constant is what was tried before; it fixes one
            // machine and breaks the next. A constant cannot track a scale.
            Rectangle {
                anchors.centerIn: parent
                width: 360
                height: emptyCol.implicitHeight + 32   // margins, top and bottom
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cWarn }
                visible: root.confirmEmpty
                z: 120

                // NOT anchors.fill: that would make the Column's height derive
                // from the parent while the parent's height derives from the
                // Column, which is a binding loop. Left/right/top only, so the
                // height stays implicit and flows upward.
                Column {
                    id: emptyCol
                    anchors { left: parent.left; right: parent.right
                              top: parent.top; margins: 16 }
                    spacing: 10

                    Text {
                        text: "Empty the trash?"
                        color: root.cWarn
                        font { family: root.uiFont; pixelSize: root.ui(14); bold: true  }
                    }
                    Text {
                        width: parent.width - 4
                        text: "Everything in the trash will be removed permanently. "
                            + "This cannot be undone."
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                        wrapMode: Text.WordWrap
                    }
                    Row {
                        spacing: 8
                        ToggleChip {
                            label: "Cancel"
                            on: false
                            onToggled: root.confirmEmpty = false
                        }
                        ToggleChip {
                            label: "Empty permanently"
                            on: true
                            onToggled: root.emptyTrash()
                        }
                    }
                }
            }

            // ── Paste conflict ──────────────────────────────────────────────
            //
            // Only ever shown when there is something to decide: `collisions`
            // has already said these names are taken. Height from the content,
            // for the reason the trash box above spells out — the text is in
            // root.ui(), and a constant cannot track a scale.
            Rectangle {
                anchors.centerIn: parent
                width: 420
                height: pasteCol.implicitHeight + 32
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cWarn }
                visible: root.pasteAsk
                z: 120

                Column {
                    id: pasteCol
                    anchors { left: parent.left; right: parent.right
                              top: parent.top; margins: 16 }
                    spacing: 10

                    Text {
                        text: root.pasteConflicts.length === 1
                                ? "“" + root.pasteConflicts[0].name + "” already exists here"
                                : root.pasteConflicts.length + " of these already exist here"
                        color: root.cWarn
                        width: parent.width - 4
                        elide: Text.ElideMiddle
                        font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                    }

                    // WHICH ones. "3 items already exist" and nothing else
                    // makes the choice a guess about what is about to be
                    // replaced.
                    Text {
                        width: parent.width - 4
                        visible: root.pasteConflicts.length > 1
                        text: root.pasteConflicts.slice(0, 6)
                                  .map(c => "• " + c.name + (c.kind === "dir" ? "/" : ""))
                                  .join("\n")
                            + (root.pasteConflicts.length > 6
                                 ? "\n• …and " + (root.pasteConflicts.length - 6) + " more" : "")
                        color: root.cText
                        wrapMode: Text.WordWrap
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    Text {
                        width: parent.width - 4
                        text: root.pasteHasSame
                                ? "This is the folder it came from, so replacing would "
                                + "delete the original. Keep both, or cancel."
                                : "Overwriting replaces files; two folders of the same "
                                + "name are merged, and files inside them collide one "
                                + "by one."
                        color: root.cText
                        wrapMode: Text.WordWrap
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }

                    Row {
                        spacing: 8
                        ToggleChip {
                            label: "Cancel"
                            on: false
                            onToggled: root.cancelPaste()
                        }
                        ToggleChip {
                            label: "Skip those"
                            on: false
                            onToggled: root.doPaste("skip")
                        }
                        ToggleChip {
                            label: "Keep both"
                            on: true
                            onToggled: root.doPaste("rename")
                        }
                        // NOT OFFERED when a source IS the entry it collides
                        // with: overwriting there removes the destination and
                        // then has nothing to copy from. The C refuses it
                        // outright — this just declines to ask for something
                        // that can only be answered with an error.
                        ToggleChip {
                            label: "Overwrite"
                            on: false
                            visible: !root.pasteHasSame
                            onToggled: root.doPaste("overwrite")
                        }
                    }
                }
            }

            // ── What the running operation is doing ─────────────────────────
            //
            // Appears half a second in, so it never flickers for a rename and
            // never fails to appear for a copy worth waiting on.
            Rectangle {
                anchors.centerIn: parent
                width: 420
                height: opCol.implicitHeight + 32
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cAccent }
                visible: root.opPanel
                z: 119

                Column {
                    id: opCol
                    anchors { left: parent.left; right: parent.right
                              top: parent.top; margins: 16 }
                    spacing: 10

                    Text {
                        text: root.opNote
                        color: root.cAccent
                        width: parent.width - 4
                        elide: Text.ElideMiddle
                        font { family: root.uiFont; pixelSize: root.ui(14); bold: true }
                    }

                    // THE BAR, and it only exists when there is a real
                    // denominator behind it: copy and move count their bytes
                    // up front, everything else has nothing honest to draw.
                    Rectangle {
                        visible: root.opUnitTotal > 0
                        width: parent.width - 4
                        height: root.ui(6)
                        radius: height / 2
                        color: Qt.rgba(root.cText.r, root.cText.g, root.cText.b, 0.15)

                        Rectangle {
                            width: parent.width * root.opFraction
                            height: parent.height
                            radius: parent.radius
                            color: root.opFailed ? root.cWarn : root.cAccent
                            Behavior on width { NumberAnimation { duration: 120 } }
                        }
                    }

                    // Bytes, rate and estimate when they are known; the plain
                    // count when they are not.
                    Text {
                        width: parent.width - 4
                        text: root.opUnitTotal > 0
                                ? Math.round(root.opFraction * 100) + "%  ·  "
                                  + root.opRateLine()
                                : root.opCountLine()
                        color: root.opFailed ? root.cWarn : root.cText
                        elide: Text.ElideRight
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }

                    Text {
                        width: parent.width - 4
                        // Files done, when the bar above is measuring bytes —
                        // the two answer different questions and only one of
                        // them fits in the bar.
                        visible: root.opByBytes
                        text: root.opCountLine() + " of " + root.opTotalFiles
                            + (root.opTotalFiles === 1 ? " item" : " items")
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    Text {
                        width: parent.width - 4
                        text: root.opCurrent
                        color: root.cDim
                        elide: Text.ElideMiddle
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    // CANCEL. Without it the only way to stop a copy is to
                    // close the window, and closing the window does not stop
                    // it — the process is a child of the shell, not of the
                    // window, and it goes on writing.
                    Row {
                        spacing: 8
                        ToggleChip {
                            label: root.opCancelling ? "cancelling…" : "Cancel"
                            on: false
                            onToggled: root.cancelOp()
                        }
                    }
                }
            }

            // ── New folder prompt ───────────────────────────────────────────
            //
            // Same fixed-height bug as the trash confirmation above: 96 px
            // against roughly 107 px of ui()-scaled content, so "Enter to
            // create, Escape to cancel" — the line that tells you how to
            // work the dialog — sat through the border.
            Rectangle {
                anchors.centerIn: parent
                width: 320
                height: newFolderCol.implicitHeight + 28
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cAccent }
                visible: root.creating
                z: 120

                Column {
                    id: newFolderCol
                    anchors { left: parent.left; right: parent.right
                              top: parent.top; margins: 14 }
                    spacing: 10

                    Text {
                        text: "New folder"
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(13); bold: true  }
                    }
                    Rectangle {
                        width: parent.width; height: 28
                        radius: 3
                        color: root.cBg
                        border { width: 1; color: root.wash(0.35) }
                        TextInput {
                            id: newFolderInput
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                            clip: true
                            onVisibleChanged: if (visible) { text = ""; forceActiveFocus() }
                            onAccepted: root.commitNewFolder(text)
                            Keys.onEscapePressed: root.creating = false
                        }
                    }
                    Text {
                        text: "Enter to create, Escape to cancel"
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10) }
                    }
                }
            }

            // ── New empty file prompt ───────────────────────────────────────
            Rectangle {
                anchors.centerIn: parent
                width: 320
                height: newFileCol.implicitHeight + 28
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cAccent }
                visible: root.creatingFile
                z: 120

                Column {
                    id: newFileCol
                    anchors { left: parent.left; right: parent.right
                              top: parent.top; margins: 14 }
                    spacing: 10

                    Text {
                        text: "New empty file"
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(13); bold: true  }
                    }
                    Rectangle {
                        width: parent.width; height: 28
                        radius: 3
                        color: root.cBg
                        border { width: 1; color: root.wash(0.35) }
                        TextInput {
                            id: newFileInput
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                            clip: true
                            onVisibleChanged: if (visible) { text = ""; forceActiveFocus() }
                            onAccepted: root.createEmptyFile(text)
                            Keys.onEscapePressed: root.creatingFile = false
                        }
                    }
                    Text {
                        text: "Enter to create, Escape to cancel"
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10) }
                    }
                }
            }

            Rectangle {
                id: statusBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 22
                color: root.cPanel

                Text {
                    id: statusLeft
                    anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                    // Both halves of this bar were anchored to opposite edges
                    // with no width and no elide, so a narrow window drew them
                    // straight through each other — "79 items" and
                    // "/home/velle" came out as "79 /bomevvelle". The counts
                    // are the half worth keeping legible, so the path yields
                    // first: this one takes what it needs up to 60%, and the
                    // path gets the rest.
                    width: Math.min(implicitWidth, parent.width * 0.6 - 12)
                    elide: Text.ElideRight
                    text: {
                        // The OUTCOME outranks "reading…". An operation ends
                        // by reloading the pane, and reload() clears
                        // statusLine — so the line saying what just happened,
                        // including "3 failed", was erased by the refresh that
                        // followed it, every time. It survives here until
                        // something the person does replaces it.
                        if (root.opOutcome) return root.opOutcome
                        if (root.loading) return "reading…"
                        if (root.statusLine) return root.statusLine
                        const n = root.shownRows.length
                        const s = root.selection.length
                        // One status bar under both panes, so it has to say
                        // WHICH pane it is counting — otherwise a split window
                        // has one number and two possible meanings.
                        const side = root.split
                                     ? (root.active === 0 ? "Left  ·  " : "Right  ·  ") : ""
                        const base = side + n + (n === 1 ? " item" : " items")
                        // How much is selected is the thing you check right
                        // before pressing Delete, so it goes where you are
                        // already looking rather than only in a menu label.
                        return s > 0 ? base + "  ·  " + s + " selected" : base
                    }
                    color: (root.opOutcome && root.opFailed) ? root.cWarn
                         : root.opOutcome ? root.cAccent
                         : root.statusLine ? root.cWarn : root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
                Text {
                    anchors {
                        left: statusLeft.right; leftMargin: 12
                        right: parent.right; rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    // Anchored to the counts rather than only to the edge, so
                    // it can never start left of where they end. ElideLeft:
                    // the tail of a path says where you are, the head says
                    // "/home/velle/" for the thousandth time.
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                    text: root.tab && root.tab.view === "dir" ? root.disp(root.tab.path) : ""
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
            }
        }

        // ── View menu ───────────────────────────────────────────────────────
        // A sibling of the toolbar rather than a child of it, so it draws OVER
        // the panes: the toolbar is declared first and its children would fall
        // underneath everything that comes after.
        MouseArea {
            anchors.fill: parent
            visible: root.viewMenuOpen
            acceptedButtons: Qt.AllButtons
            z: 170
            onClicked: root.viewMenuOpen = false
            onPressed: root.viewMenuOpen = false
        }

        Rectangle {
            visible: root.viewMenuOpen
            // Dropped from the View button, not from the window edge: the
            // toolbar is at 0,0 here, so the button's own x is the offset.
            anchors { top: toolBar.bottom; topMargin: 2 }
            x: navGroup.x + viewBtn.x
            width: 220
            height: viewCol.implicitHeight + 8
            radius: 4
            color: root.cPanel
            border { width: 1; color: root.wash(0.35) }
            z: 180

            Column {
                id: viewCol
                anchors { fill: parent; margins: 4 }

                Repeater {
                    model: [
                        { label: "Icons",            act: "view:icons",
                          tick: root.effectiveView === "icons" },
                        { label: "Compact",          act: "view:compact",
                          tick: root.effectiveView === "compact" },
                        { label: "Details",          act: "view:details",
                          tick: root.effectiveView === "details" },
                        { label: "-",                act: "",           tick: false },
                        { label: "Sort by Name",     act: "sort:name",  tick: root.tab && root.tab.sort === "name" },
                        { label: "Sort by Size",     act: "sort:size",  tick: root.tab && root.tab.sort === "size" },
                        { label: "Sort by Modified", act: "sort:mtime", tick: root.tab && root.tab.sort === "mtime" },
                        { label: "Sort by Type",     act: "sort:type",  tick: root.tab && root.tab.sort === "type" },
                        { label: "-",                act: "",           tick: false },
                        { label: "Descending",       act: "reverse",    tick: root.tab && root.tab.reverse },
                        { label: "Show Hidden Files",act: "hidden",     tick: root.tab && root.tab.showHidden },
                        { label: "-",                act: "",           tick: false },
                        { label: "Previews",         act: "thumbs",     tick: root.thumbs },
                        { label: "Folder Tree",      act: "tree",       tick: root.showTree },
                        { label: "Split View",       act: "split",      tick: root.split },
                        { label: "Sidebar",          act: "sidebar",    tick: root.showSidebar },
                        { label: "-",                act: "",           tick: false }
                    ]
                    delegate: Item {
                        id: viewItem
                        required property var modelData
                        width: viewCol.width
                        height: viewItem.modelData.label === "-" ? 5 : 26

                        Rectangle {
                            anchors { left: parent.left; right: parent.right
                                      verticalCenter: parent.verticalCenter }
                            height: 1
                            color: root.wash(0.25)
                            visible: viewItem.modelData.label === "-"
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 3
                            visible: viewItem.modelData.label !== "-"
                            color: viewMa.containsMouse ? root.wash(0.18) : "transparent"

                            Text {
                                anchors { left: parent.left; leftMargin: 10
                                          verticalCenter: parent.verticalCenter }
                                text: viewItem.modelData.label
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                            Text {
                                anchors { right: parent.right; rightMargin: 10
                                          verticalCenter: parent.verticalCenter }
                                text: viewItem.modelData.tick ? "✓" : ""
                                color: root.cAccent
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                            MouseArea {
                                id: viewMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                // The menu STAYS OPEN for these: they are the
                                // controls you try two of in a row, and one
                                // that closed itself would have to be reopened
                                // to see the tick it just set.
                                onClicked: root.applyViewAction(viewItem.modelData.act)
                            }
                        }
                    }
                }

                // Text size. Same question as icon size — how big is this
                // window's content — so it sits directly under it, and it
                // scales every label in the window at once rather than the
                // list alone: half a window resized is a window that looks
                // broken. The font FAMILY is not a setting here; that is the
                // desktop's, and synfiles follows it live.
                Item {
                    width: viewCol.width
                    height: 30

                    Text {
                        id: textLabel
                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        text: "Text"
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    Text {
                        id: textPct
                        anchors { left: textLabel.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 34
                        text: root.textScale + "%"
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(10) }
                    }
                    Item {
                        anchors { left: textPct.right; leftMargin: 8
                                  right: parent.right; rightMargin: 10
                                  verticalCenter: parent.verticalCenter }
                        height: 20

                        Rectangle {
                            anchors { left: parent.left; right: parent.right
                                      verticalCenter: parent.verticalCenter }
                            height: 3
                            radius: 2
                            color: root.wash(0.20)

                            Rectangle {
                                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                width: textHandle.x + textHandle.width / 2
                                radius: 2
                                color: root.cAccent
                            }
                        }
                        Rectangle {
                            id: textHandle
                            width: 11; height: 11; radius: 6
                            anchors.verticalCenter: parent.verticalCenter
                            x: (root.textScale - root.textMin)
                               / (root.textMax - root.textMin) * (parent.width - width)
                            color: root.cAccent
                        }
                        MouseArea {
                            anchors { fill: parent; topMargin: -4; bottomMargin: -4 }
                            cursorShape: Qt.PointingHandCursor
                            function setFrom(mx) {
                                const w = width - textHandle.width
                                const f = Math.max(0, Math.min(1, (mx - textHandle.width / 2) / w))
                                // Stepped in fives: a per-pixel percentage is a
                                // number nobody can land on twice.
                                const v = root.textMin + f * (root.textMax - root.textMin)
                                root.textScale = Math.round(v / 5) * 5
                            }
                            onPressed: (m) => setFrom(m.x)
                            onPositionChanged: (m) => { if (pressed) setFrom(m.x) }
                        }
                    }
                }

                // Icon size lives in the same menu, because it is the same
                // question — how the list looks — and Dolphin keeps it there.
                Item {
                    width: viewCol.width
                    height: 30

                    Text {
                        id: sizeLabel
                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        text: "Size"
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    Row {
                        id: sizePresets
                        anchors { left: sizeLabel.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        spacing: 2
                        Repeater {
                            model: [{ label: "S", size: 16 },
                                    { label: "M", size: 32 },
                                    { label: "L", size: 96 }]
                            delegate: Rectangle {
                                id: sizeBtn
                                required property var modelData
                                width: 20; height: 20; radius: 3
                                color: root.iconSize === sizeBtn.modelData.size ? root.wash(0.25)
                                     : (sizeBtnMa.containsMouse ? root.wash(0.12) : "transparent")
                                Text {
                                    anchors.centerIn: parent
                                    text: sizeBtn.modelData.label
                                    color: root.iconSize === sizeBtn.modelData.size ? root.cAccent : root.cDim
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                }
                                MouseArea {
                                    id: sizeBtnMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.iconSize = sizeBtn.modelData.size
                                }
                            }
                        }
                    }
                    // Hand-rolled rather than QtQuick.Controls: importing the
                    // Controls module for one widget pulls in a style that
                    // matches nothing else in this window.
                    Item {
                        anchors { left: sizePresets.right; leftMargin: 8
                                  right: parent.right; rightMargin: 10
                                  verticalCenter: parent.verticalCenter }
                        height: 20

                        Rectangle {
                            anchors { left: parent.left; right: parent.right
                                      verticalCenter: parent.verticalCenter }
                            height: 3
                            radius: 2
                            color: root.wash(0.20)

                            Rectangle {
                                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                width: menuHandle.x + menuHandle.width / 2
                                radius: 2
                                color: root.cAccent
                            }
                        }
                        Rectangle {
                            id: menuHandle
                            width: 11; height: 11; radius: 6
                            anchors.verticalCenter: parent.verticalCenter
                            x: (root.iconSize - root.iconMin)
                               / (root.iconMax - root.iconMin) * (parent.width - width)
                            color: root.cAccent
                        }
                        MouseArea {
                            anchors { fill: parent; topMargin: -4; bottomMargin: -4 }
                            cursorShape: Qt.PointingHandCursor
                            function setFrom(mx) {
                                const w = width - menuHandle.width
                                const f = Math.max(0, Math.min(1, (mx - menuHandle.width / 2) / w))
                                root.iconSize = Math.round(root.iconMin
                                                + f * (root.iconMax - root.iconMin))
                            }
                            onPressed: (m) => setFrom(m.x)
                            onPositionChanged: (m) => { if (pressed) setFrom(m.x) }
                        }
                    }
                }
            }
        }
    }

    // ── A pane ──────────────────────────────────────────────────────────────
    //
    // Everything that is "where am I and what have I got selected" lives in
    // here rather than in root: the tabs, the history, the listing, the
    // selection, the filter, the search and the process that reads the
    // directory. Split view is two of these side by side, and that is exactly
    // why it needed this factoring first — a SELECTION shared by two folder
    // views is nonsense, because Move to Trash pressed in one pane would act
    // on rows the other pane is showing.
    //
    // What stays shared: the toolbar, the sidebar, the clipboard, the undo
    // journal and every dialog. Two panes are two places, not two programs.
    component Pane: Item {
        id: pane

        // 0 or 1. A pane knows which it is so that any click inside it can
        // make it the active one BEFORE whatever the shared toolbar, menu or
        // keyboard is about to do gets to run.
        required property int idx
        // Strictly one pane at a time, even with the split closed — this is
        // what decides which pane's views ask for the keyboard, and two views
        // both declaring focus:true means the last one CONSTRUCTED wins. That
        // would be the hidden pane, and every keystroke would have gone to a
        // pane with no tabs in it.
        readonly property bool isActive: root.active === pane.idx

        function claim() { root.setActive(pane.idx) }

        // Focus is the other way a pane becomes active: every row click ends
        // in forceActiveFocus() on the view, and typing has to go to the pane
        // the toolbar is talking about.
        onActiveFocusChanged: if (pane.activeFocus) pane.claim()

        // A context menu belongs to the pane it was opened in, and the overlay
        // that dismisses it only covers that pane — so a click in the OTHER
        // half would leave it hanging there over a pane nobody is using.
        onIsActiveChanged: if (!pane.isActive) ctxMenu.open = false

        // ── Tabs ────────────────────────────────────────────────────────────
        //
        // A tab is {path, view, rows, sort, reverse, showHidden, filter, hist,
        // hi}. Keeping per-tab state in the model rather than in the visible
        // pane is what makes switching tabs instant and what stops a sort
        // applied in one tab silently reordering another.
        property var tabs: []
        property int current: 0
        property bool loading: false

        readonly property var tab: pane.tabs.length > 0 ? pane.tabs[pane.current] : null
        readonly property bool started: pane.tabs.length > 0

        function newTab(pathEnc, view) {
            const t = {
                path: pathEnc || root.encodePath(root.homeDir),
                view: view || "dir",       // dir | recent | trash | about
                title: "",
                rows: [],
                sort: root.defaultSort,
                reverse: root.defaultReverse,
                showHidden: root.defaultHidden,
                filter: "",
                // Back/forward, per tab. Shared history across tabs would send
                // Back to a folder this tab was never in.
                hist: [{ path: pathEnc || root.encodePath(root.homeDir), view: view || "dir" }],
                hi: 0
            }
            const copy = pane.tabs.slice()
            copy.push(t)
            pane.tabs = copy
            pane.current = copy.length - 1
            pane.clearSelection()
            pane.reload()
        }

        function closeTab(i) {
            if (pane.tabs.length <= 1) return
            const copy = pane.tabs.slice()
            copy.splice(i, 1)
            pane.tabs = copy
            if (pane.current >= copy.length) pane.current = copy.length - 1
            pane.clearSelection()
            pane.reload()
        }

        // Switching tabs DROPS the selection. It is a set of names, and a name
        // selected in one folder means nothing in another — keeping it meant a
        // file that happened to share a name with a selected one came up
        // already highlighted in the tab you switched to.
        function showTab(i) {
            if (i === pane.current) return
            pane.current = i
            pane.clearSelection()
            pane.reload()
        }

        // Mutating a property of an object inside an array does NOT re-evaluate
        // bindings on that array — QML only notices the array identity
        // changing. Every state change therefore rebuilds the outer array,
        // which is why this helper exists rather than `tabs[current].sort = x`
        // at each call site.
        function setTab(fields) {
            if (!pane.tab) return
            const copy = pane.tabs.slice()
            const t = ({})
            for (const k in copy[pane.current]) t[k] = copy[pane.current][k]
            for (const k in fields) t[k] = fields[k]
            copy[pane.current] = t
            pane.tabs = copy
        }

        function navigate(pathEnc, view) {
            const v = view || "dir"
            const t = pane.tab
            // Going somewhere new DISCARDS the forward entries, the way every
            // browser does: forward means "where I came back from", and keeping
            // a branch nobody can see would make the button lie.
            if (t) {
                const h = (t.hist || []).slice(0, (t.hi === undefined ? -1 : t.hi) + 1)
                const last = h[h.length - 1]
                if (!last || last.path !== pathEnc || last.view !== v)
                    h.push({ path: pathEnc, view: v })
                pane.setTab({ hist: h, hi: h.length - 1 })
            }
            pane.go(pathEnc, v)
        }

        // The move itself, with no history written — what Back and Forward use.
        function go(pathEnc, view) {
            // The last operation's outcome is about the folder being left.
            root.opOutcome = ""
            pane.setTab({ path: pathEnc, view: view || "dir", filter: "", rows: [] })
            // Same argument as switching tabs: these names are about the folder
            // being left behind.
            pane.clearSelection()
            pane.reload()
        }

        readonly property bool canGoBack: pane.tab && pane.tab.hi > 0
        readonly property bool canGoForward:
            pane.tab && pane.tab.hist && pane.tab.hi < pane.tab.hist.length - 1

        function goBack() {
            if (!pane.canGoBack) return
            const i = pane.tab.hi - 1
            const e = pane.tab.hist[i]
            pane.setTab({ hi: i })
            pane.go(e.path, e.view)
        }

        function goForward() {
            if (!pane.canGoForward) return
            const i = pane.tab.hi + 1
            const e = pane.tab.hist[i]
            pane.setTab({ hi: i })
            pane.go(e.path, e.view)
        }

        // Opening the split has to put something in the new pane, and the
        // folder you are standing in is the only answer that is never a
        // surprise. Called again on a pane that already has tabs does nothing:
        // closing and reopening the split should come back to where you were.
        function ensureStarted(pathEnc) {
            if (pane.started) { pane.reload(); return }
            pane.newTab(pathEnc, "dir")
        }

        // Closing the split keeps what the ACTIVE pane was showing, so the
        // window does not jump back to a folder you left ten minutes ago.
        function adopt(other) {
            const copy = []
            for (const t of other.tabs) {
                const c = ({})
                for (const k in t) c[k] = t[k]
                copy.push(c)
            }
            pane.tabs = copy
            pane.current = other.current
            pane.selection = other.selection.slice()
            pane.anchorName = other.anchorName
            pane.reload()
        }

        // Emptied after its state has been taken over by the other pane. Left
        // as it was, reopening the split would show the same folder twice —
        // the one that was just moved across.
        function discard() {
            pane.tabs = []
            pane.current = 0
            pane.clearSelection()
            pane.searching = false
            pane.renaming = ""
        }

        // ── Backend ─────────────────────────────────────────────────────────
        //
        // One listing process PER PANE. A single shared one would have two
        // panes racing to claim the same stdout, and the loser would be handed
        // the other pane's directory.
        Process {
            id: listProc
            property string kind: ""
            stdout: StdioCollector {
                onStreamFinished: {
                    const table = root.parseRecords(this.text)
                    const t = pane.tab
                    if (!t) return

                    let rows = []
                    if (listProc.kind === "find") {
                        listProc.kind = ""
                        // A search result is not in the current folder, so it
                        // carries its own full path rather than being joined
                        // onto the tab's. `dir` is relative to where the search
                        // started.
                        rows = table.map(r => ({
                            name: r.name,
                            full: r.dir ? root.joinEnc(root.joinEnc(pane.searchRoot, r.dir), r.name)
                                        : root.joinEnc(pane.searchRoot, r.name),
                            where: r.dir,
                            type: r.type, size: parseInt(r.size || "0"),
                            mtime: parseInt(r.mtime || "0"), mime: r.mime,
                            link: r.link, target: r.target, mode: r.mode,
                            missing: false
                        }))
                        pane.setTab({ rows: rows })
                        pane.loading = false
                        root.statusLine = ""
                        return
                    }
                    if (t.view === "about") {
                        root.aboutRows = table
                        pane.loading = false
                        root.statusLine = ""
                        return
                    } else if (t.view === "trash") {
                        // `trashName` is the handle `trash restore` takes, and
                        // it is NOT derivable from the path: two files called
                        // notes.txt become notes.txt and notes.txt.2 in the
                        // trash.
                        //
                        // …which is also why `name` — the row's IDENTITY, what
                        // the selection is a set of — has to be that trash name
                        // and not the original basename. Two trashed notes.txt
                        // share a basename, so clicking one highlighted both.
                        // `label` is what the row shows: still the name the file
                        // had.
                        rows = table.map(r => ({
                            name: r.name, label: root.baseEnc(r.path),
                            full: r.path,
                            trashName: r.name,
                            type: r.present === "1" ? "file" : "missing",
                            size: 0, mtime: 0, deleted: r.deleted,
                            mime: "", link: "0", target: "",
                            missing: r.present !== "1"
                        }))
                    } else if (t.view === "recent") {
                        rows = table.map(r => ({
                            name: root.baseEnc(r.path), full: r.path,
                            type: r.exists === "1" ? "file" : "missing",
                            size: 0, mtime: parseInt(r.mtime || "0"),
                            mime: r.mime, link: "0", target: "",
                            missing: r.exists !== "1"
                        }))
                    } else {
                        rows = table.map(r => ({
                            name: r.name, full: root.joinEnc(t.path, r.name),
                            type: r.type, size: parseInt(r.size || "0"),
                            mtime: parseInt(r.mtime || "0"), mime: r.mime,
                            link: r.link, target: r.target, mode: r.mode,
                            icon: r.icon || "",
                            missing: false
                        }))
                    }

                    pane.setTab({ rows: rows })
                    pane.loading = false
                    root.statusLine = ""
                    // What is inside each subfolder, for the icons. After the
                    // listing rather than beside it: the rows are what the
                    // window is waiting for, and this only decorates them.
                    pane.refreshPeek()
                }
            }
            stderr: StdioCollector {
                onStreamFinished: {
                    if (this.text) root.statusLine = this.text.split("\n")[0]
                }
            }
        }

        function reload() {
            const t = pane.tab
            if (!t) return
            pane.loading = true
            root.statusLine = ""

            if (t.view === "about") {
                listProc.command = [root.bin, "--rec", "about"]
            } else if (t.view === "trash") {
                listProc.command = [root.bin, "--rec", "trash", "list"]
            } else if (t.view === "recent") {
                listProc.command = [root.bin, "--rec", "recent", "--limit=300"]
            } else {
                // The path goes to the binary DECODED — argv carries raw bytes
                // and needs no escaping. The encoded form exists for the record
                // stream, not for the process boundary.
                const args = [root.bin, "--rec", "list", "--sort=" + t.sort]
                if (t.reverse) args.push("--reverse")
                if (t.showHidden) args.push("--all")
                args.push(root.disp(t.path))
                listProc.command = args
            }
            listProc.running = true
        }

        // ── Folder previews ─────────────────────────────────────────────────
        //
        // Per pane, for the same reason as the listing: the map is keyed by
        // folder and answers "what is in the directory this pane is showing".
        // One shared map would be overwritten by whichever pane listed last,
        // and the other pane's folders would quietly lose their tiles.
        property var folderPeek: ({})

        Process {
            id: peekProc
            stdout: StdioCollector {
                onStreamFinished: {
                    const map = ({})
                    for (const r of root.parseRecords(this.text)) {
                        if (!r.dir || !r.file) continue
                        if (!map[r.dir]) map[r.dir] = []
                        map[r.dir].push({ full: r.file, size: parseInt(r.size || "0") })
                    }
                    pane.folderPeek = map
                }
            }
        }

        function refreshPeek() {
            // Previews off means previews off, everywhere: a folder that keeps
            // its pictures after the toggle is turned off looks like the toggle
            // broke.
            if (!root.thumbs || !pane.tab || pane.tab.view !== "dir") {
                pane.folderPeek = ({})
                return
            }
            peekProc.running = false
            peekProc.command = [root.bin, "--rec", "peek", root.disp(pane.tab.path)]
            peekProc.running = true
        }

        // ── Selection ───────────────────────────────────────────────────────
        //
        // Encoded names, because that is the identity. Storing display names
        // here would make two files that differ only in an escaped byte the
        // same selection.
        property var selection: []
        property string anchorName: ""   // where a Shift range started
        property string renaming: ""     // encoded name being renamed, "" if none

        // Where the KEYBOARD is, which is not the same thing as what is
        // selected. Ctrl+Arrow moves this without touching the selection, and
        // a Shift range needs a moving end that is not itself the anchor —
        // both of which are impossible if "the current row" is only ever
        // "the row that happens to be selected".
        property string cursorName: ""

        // The list's row pitch, declared once because three things need it and
        // they must agree: the delegate's own height, Page Up/Down, and the
        // rubber band, which has to know where a row IS without a delegate to
        // ask — off-screen rows have none, and a band that only selects what
        // was already visible is a band that stops at the fold.
        readonly property real rowH: Math.max(30, root.iconSize + 10)

        function isSelected(name) { return pane.selection.indexOf(name) >= 0 }

        function selectOnly(name) {
            pane.selection = [name]
            pane.anchorName = name
            pane.cursorName = name
        }

        function toggleSelect(name) {
            const i = pane.selection.indexOf(name)
            const copy = pane.selection.slice()
            if (i >= 0) copy.splice(i, 1)
            else copy.push(name)
            pane.selection = copy
            pane.anchorName = name
            pane.cursorName = name
        }

        // A Shift range runs over the rows AS DISPLAYED, so it follows the
        // current sort and filter rather than some underlying order the user
        // cannot see.
        function selectRange(name) {
            const rows = pane.shownRows
            let a = -1, b = -1
            for (let i = 0; i < rows.length; i++) {
                if (rows[i].name === pane.anchorName) a = i
                if (rows[i].name === name) b = i
            }
            if (a < 0) { pane.selectOnly(name); return }
            if (b < 0) return
            const lo = Math.min(a, b), hi = Math.max(a, b)
            const copy = []
            for (let i = lo; i <= hi; i++) copy.push(rows[i].name)
            pane.selection = copy
            // The anchor is NOT moved: Shift+Down four times and then Shift+Up
            // has to give back the row it just took, which it cannot do if
            // every extension re-anchors where it landed.
            pane.cursorName = name
        }

        function selectAll() { pane.selection = pane.shownRows.map(r => r.name) }

        function clearSelection() {
            pane.selection = []
            pane.anchorName = ""
            pane.cursorName = ""
        }

        // ── Keyboard navigation ─────────────────────────────────────────────
        //
        // The list had no arrow keys at all: every selection came from the
        // mouse, and Shift+click was the only way to take a range. What makes
        // that fixable in one place is that both views are laid out by Qt and
        // both answer indexAt() — so "the row below this one" is arithmetic on
        // an INDEX into shownRows, and the two views differ only in what one
        // press of Down is worth.
        function indexOfName(name) {
            const rows = pane.shownRows
            for (let i = 0; i < rows.length; i++)
                if (rows[i].name === name) return i
            return -1
        }

        // A grid's Down is a whole row of icons, and the compact view flows
        // DOWN its columns instead of across — so the step is asked of the
        // view rather than assumed. Qt does not publish the column count, but
        // it does publish the cell size the count is derived from, which is
        // the same number the layout itself used.
        function gridSpan() {
            if (!root.gridView) return 1
            if (root.compactView)
                return Math.max(1, Math.floor(fileGrid.height / Math.max(1, fileGrid.cellHeight)))
            return Math.max(1, Math.floor(fileGrid.width / Math.max(1, fileGrid.cellWidth)))
        }

        // mode: "select" (plain), "extend" (Shift), "move" (Ctrl — the cursor
        // travels and the selection stays where it was).
        function focusIndex(i, mode) {
            const rows = pane.shownRows
            if (rows.length === 0) return
            const c = Math.max(0, Math.min(rows.length - 1, i))
            const name = rows[c].name
            if (mode === "extend") {
                if (!pane.anchorName) pane.anchorName = name
                pane.selectRange(name)
            } else if (mode === "move") {
                pane.cursorName = name
            } else {
                pane.selectOnly(name)
            }
            // Keeping the cursor on screen is the difference between arrow
            // keys that work and arrow keys that scroll nothing while the
            // highlight walks off the bottom of the window.
            if (root.gridView) fileGrid.positionViewAtIndex(c, GridView.Contain)
            else               fileList.positionViewAtIndex(c, ListView.Contain)
        }

        // Where the next move starts FROM. The cursor when there is one, else
        // the selection, else the top — so the first Down in a folder nobody
        // has clicked in lands on the first row rather than doing nothing.
        function cursorIndex() {
            let i = pane.cursorName ? pane.indexOfName(pane.cursorName) : -1
            if (i < 0 && pane.selection.length > 0)
                i = pane.indexOfName(pane.selection[pane.selection.length - 1])
            return i
        }

        function moveCursor(delta, mode) {
            const rows = pane.shownRows
            if (rows.length === 0) return
            const from = pane.cursorIndex()
            // No cursor yet: the first press lands on an end rather than
            // stepping from an imaginary position outside the list.
            if (from < 0) { pane.focusIndex(delta > 0 ? 0 : rows.length - 1, mode); return }
            pane.focusIndex(from + delta, mode)
        }

        function selectedRows() {
            return pane.shownRows.filter(r => pane.isSelected(r.name))
        }

        // Decoded paths for the process boundary — argv carries raw bytes.
        function selectedPaths() {
            return pane.selectedRows().map(r => root.disp(r.full))
        }

        readonly property var shownRows: {
            const t = pane.tab
            if (!t) return []
            if (!t.filter) return t.rows
            const f = t.filter.toLowerCase()
            return t.rows.filter(r => root.disp(r.name).toLowerCase().includes(f))
        }

        // ── Search ──────────────────────────────────────────────────────────
        //
        // Scoped to the folder you are standing in, because "search everywhere"
        // is a different and much slower question, and the answer to it is
        // almost never what somebody pressing Ctrl+F in a folder wanted.
        property bool searching: false
        property string searchTerm: ""
        property bool searchContent: false
        property string searchRoot: ""

        function beginSearch() {
            if (!pane.tab || pane.tab.view !== "dir") return
            pane.claim()
            pane.searchRoot = pane.tab.path
            pane.searching = true
            pane.setTab({ rows: [], filter: "" })
            filterInput.forceActiveFocus()
        }

        function endSearch() {
            pane.searching = false
            pane.searchTerm = ""
            pane.reload()
        }

        function runSearch() {
            if (!pane.searchTerm) { pane.setTab({ rows: [] }); return }
            pane.loading = true
            root.statusLine = "searching " + root.disp(pane.searchRoot) + "…"
            const args = [root.bin, "--rec", "find", root.disp(pane.searchRoot),
                          "--limit=2000"]
            if (pane.searchContent) args.push("--content=" + pane.searchTerm)
            else                    args.push("--name=" + pane.searchTerm)
            if (pane.tab.showHidden) args.push("--all")
            listProc.kind = "find"
            listProc.command = args
            listProc.running = true
        }

        // Shared by the list and the grid: the two differ in arrangement, not
        // in what a key means.
        function handleKey(event) {
            if (!pane.tab) return
            pane.claim()
            const rows = pane.selectedRows()
            const row = rows.length === 1 ? rows[0] : null

            if (event.key === Qt.Key_Delete) {
                if (pane.tab.view === "dir") root.trashSelection()
                event.accepted = true
            } else if (event.key === Qt.Key_F2) {
                // Rename is the one operation that cannot be done to six things
                // at once, so it needs exactly one.
                if (row && pane.tab.view === "dir") pane.renaming = row.name
                event.accepted = true
            } else if (event.key === Qt.Key_F5) {
                pane.reload()
                event.accepted = true
            } else if (event.key === Qt.Key_F3) {
                // Dolphin's key for it, which is the one a hand already knows.
                root.toggleSplit()
                event.accepted = true
            } else if (event.key === Qt.Key_F9) {
                // Dolphin and Nautilus both hide the places panel on F9, so it
                // is the key a hand already knows here too.
                root.showSidebar = !root.showSidebar
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (event.modifiers & Qt.AltModifier) root.openProperties()
                else if (row) root.activate(row)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                // The paste question first: it is modal in intent, and Escape
                // on an open dialogue means "not that", never "deselect".
                if (root.pasteAsk) root.cancelPaste()
                else if (root.showProps) root.showProps = false
                else if (pane.searching) pane.endSearch()
                else pane.clearSelection()
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                if (pane.tab.view === "dir")
                    pane.navigate(root.parentEnc(pane.tab.path), "dir")
                event.accepted = true
            } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up
                       || event.key === Qt.Key_Left || event.key === Qt.Key_Right
                       || event.key === Qt.Key_Home || event.key === Qt.Key_End
                       || event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
                // Shift extends from the anchor, Ctrl moves the cursor and
                // leaves the selection alone, neither one selects. Checked
                // BEFORE the Ctrl block below, or Ctrl+Down would fall into
                // the shortcut table and do nothing at all.
                const mode = (event.modifiers & Qt.ShiftModifier) ? "extend"
                           : (event.modifiers & Qt.ControlModifier) ? "move"
                           : "select"
                const span = pane.gridSpan()
                // A screenful, computed from the view rather than a constant:
                // Page must move by what is actually on screen, and in the
                // grid that is a screenful of ROWS, not of icons.
                //
                // span is a row of icons across, or in compact a column of
                // them down — so the other axis is what a page is counted in,
                // and multiplying the two gives the cells actually on screen
                // either way.
                const page = root.gridView
                    ? span * Math.max(1, Math.floor(
                          root.compactView ? fileGrid.width / Math.max(1, fileGrid.cellWidth)
                                           : fileGrid.height / Math.max(1, fileGrid.cellHeight)))
                    : Math.max(1, Math.floor(fileList.height / Math.max(1, pane.rowH)))

                switch (event.key) {
                case Qt.Key_Down:  pane.moveCursor(root.compactView ? 1 : span, mode); break
                case Qt.Key_Up:    pane.moveCursor(root.compactView ? -1 : -span, mode); break
                // In a list Left/Right have nothing to move through, so they
                // stay unbound there rather than doing something invented.
                case Qt.Key_Right: if (root.gridView) pane.moveCursor(root.compactView ? span : 1, mode); break
                case Qt.Key_Left:  if (root.gridView) pane.moveCursor(root.compactView ? -span : -1, mode); break
                case Qt.Key_Home:  pane.focusIndex(0, mode); break
                case Qt.Key_End:   pane.focusIndex(pane.shownRows.length - 1, mode); break
                case Qt.Key_PageDown: pane.moveCursor(page, mode); break
                case Qt.Key_PageUp:   pane.moveCursor(-page, mode); break
                }
                event.accepted = true
            } else if (event.modifiers & Qt.ControlModifier) {
                if (event.key === Qt.Key_C)      { root.copySelection(false); event.accepted = true }
                else if (event.key === Qt.Key_X) { root.copySelection(true);  event.accepted = true }
                else if (event.key === Qt.Key_V) { root.paste();              event.accepted = true }
                else if (event.key === Qt.Key_A) { pane.selectAll();          event.accepted = true }
                else if (event.key === Qt.Key_Z) { root.doUndo();             event.accepted = true }
                else if (event.key === Qt.Key_F) { pane.beginSearch();        event.accepted = true }
                else if (event.key === Qt.Key_L) { root.beginEditPath();      event.accepted = true }
                else if (event.key === Qt.Key_T) { pane.newTab(pane.tab.path, "dir"); event.accepted = true }
                else if (event.key === Qt.Key_W) { root.closeTabOrQuit(pane, pane.current); event.accepted = true }
                else if (event.key === Qt.Key_N) { root.creating = true; event.accepted = true }
            }
        }

        // A click on the pane's own furniture — the strip beside the tabs, the
        // space under the last row — is still a statement about which pane you
        // are working in. Declared FIRST so it sits UNDER everything else and
        // only ever catches what nothing more specific wanted: Qt Quick
        // delivers a press to the last matching child first, and a full-size
        // MouseArea written last would swallow every button in the pane.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: (m) => { pane.claim(); m.accepted = false }
        }

        // Tab strip
        Rectangle {
            id: tabStrip
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 34
            color: root.cPanel

            // Bounded on the RIGHT by the pin control, and clipped.
            //
            // These were two independent Rows in one parent — this one growing
            // from the left, the pin control anchored to the right — with
            // nothing arbitrating between them. Cascaded, this pane is about
            // 120 px wide and cannot hold both, so the "velle" tab and the
            // "Pinned ✓" chip drew through each other and came out as
            // "vellPinned" with a tick over the new-tab plus.
            //
            // Same shape as the toolbar's address bar and the package row's
            // Update button: whenever something is laid out from each edge,
            // one of them has to yield explicitly or they meet in the middle.
            Row {
                // x + width, floored at 0, for the reason the address bar is:
                // anchored to both edges this goes NEGATIVE when they cross —
                // measured -20 on a pane that is not currently shown — and
                // clip: true does nothing to a negative-width item, which is
                // precisely when the clip is needed.
                anchors.verticalCenter: parent.verticalCenter
                x: 6
                width: Math.max(0, pinRow.x - 6 - x)
                spacing: 2
                clip: true

                Repeater {
                    model: pane.tabs
                    delegate: Rectangle {
                        id: tabBtn
                        required property var modelData
                        required property int index
                        readonly property bool active: tabBtn.index === pane.current
                        width: Math.min(200, tabLabel.implicitWidth + 46)
                        height: 26
                        radius: 3
                        color: tabBtn.active ? root.wash(0.20)
                                             : (tabMa.containsMouse ? root.wash(0.08) : "transparent")

                        Text {
                            id: tabLabel
                            anchors {
                                left: parent.left; leftMargin: 10
                                right: closeBtn.left; rightMargin: 4
                                verticalCenter: parent.verticalCenter
                            }
                            text: tabBtn.modelData.view === "recent"
                                  ? "Recent"
                                  : (root.disp(root.baseEnc(tabBtn.modelData.path)) || "/")
                            elide: Text.ElideRight
                            color: tabBtn.active ? root.cAccent : root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }
                        MouseArea {
                            id: tabMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { pane.claim(); pane.showTab(tabBtn.index) }
                        }
                        Text {
                            id: closeBtn
                            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                            text: "×"
                            color: closeMa.containsMouse ? root.cAccent : root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(14) }
                            // Always shown. See root.closeTabOrQuit: on the
                            // last tab this closes the pane, or the window.
                            MouseArea {
                                id: closeMa
                                anchors { fill: parent; margins: -4 }
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { pane.claim(); root.closeTabOrQuit(pane, tabBtn.index) }
                            }
                        }
                    }
                }

                Rectangle {
                    width: 26; height: 26; radius: 3
                    color: addMa.containsMouse ? root.wash(0.12) : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(15) }
                    }
                    MouseArea {
                        id: addMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            pane.claim()
                            pane.newTab(pane.tab ? pane.tab.path
                                                 : root.encodePath(root.homeDir), "dir")
                        }
                    }
                }
            }

            // The two controls that used to have a 30px band of their own
            // with nothing else in it. The tab strip already reserves this
            // row and the space to the right of the tabs was empty, so they
            // cost no height at all here. New Folder went to the hamburger,
            // which already had it.
            Row {
                id: pinRow
                anchors { right: parent.right; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                spacing: 6

                ToggleChip {
                    label: pane.tab && root.isPinned(pane.tab.path) ? "Pinned ✓" : "Pin"
                    on: pane.tab ? root.isPinned(pane.tab.path) : false
                    // Gone when the pane cannot hold it beside the tabs. The
                    // tabs are how you move around and cannot be given up; this
                    // is a convenience, and the same folder can still be pinned
                    // by right-clicking it ("Pin to Places" / "Remove from
                    // Places"), so nothing becomes unreachable.
                    //
                    // Hidden means zero-width here, so the tabs Row anchored to
                    // pinRow.left gets the whole strip back.
                    visible: pane.tab && pane.tab.view === "dir"
                             && tabStrip.width >= 240
                    onToggled: {
                        pane.claim()
                        if (root.isPinned(pane.tab.path)) root.unpin(pane.tab.path)
                        else root.pin(pane.tab.path)
                    }
                }
            }
        }

        // Path bar: breadcrumbs, filter, and the per-tab toggles.
        // Emptying the trash is the one destructive action reachable from
        // this window, so it is a deliberate button on its own view rather
        // than a menu entry next to something harmless — and the binary
        // still refuses it without --yes, which is passed only from here.
        Item {
            id: trashBar
            anchors { top: tabStrip.bottom; left: parent.left; right: parent.right }
            anchors.margins: 8
            height: 30
            visible: pane.tab && pane.tab.view === "trash"

            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: "Deleted files. Restoring puts one back where it came from."
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(12) }
            }
            ToggleChip {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                label: "Empty Trash…"
                on: false
                onToggled: root.confirmEmpty = true
            }
        }

        // Filter box
        Rectangle {
            id: filterBar
            // Below the trash's own banner when there is one. It used to
            // hang off the row that just went, and in the trash view both
            // landed on tabStrip.bottom and drew over each other.
            anchors { top: trashBar.visible ? trashBar.bottom : tabStrip.bottom
                      left: parent.left; right: parent.right }
            anchors.margins: 8
            height: 28
            visible: pane.tab && pane.tab.view !== "about"
            radius: 4
            color: root.cPanel
            border { width: 1; color: filterInput.activeFocus ? root.cAccent : "transparent" }

            // One box, two jobs, and the mode is explicit. Filtering hides
            // rows already loaded; searching walks the tree and takes a
            // moment — silently switching between them on the same
            // keystroke would make the slow one a surprise.
            TextInput {
                id: filterInput
                anchors {
                    left: parent.left; leftMargin: 10
                    right: searchChips.left; rightMargin: 8
                    top: parent.top; bottom: parent.bottom
                }
                verticalAlignment: TextInput.AlignVCenter
                color: root.cText
                font { family: root.uiFont; pixelSize: root.ui(12) }
                clip: true
                onTextChanged: {
                    if (pane.searching) pane.searchTerm = text
                    else pane.setTab({ filter: text })
                }
                onAccepted: if (pane.searching) pane.runSearch()
                Keys.onEscapePressed: {
                    text = ""
                    if (pane.searching) pane.endSearch()
                    else pane.setTab({ filter: "" })
                }
                onVisibleChanged: if (visible) text = ""
            }
            Text {
                anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                text: pane.searching
                      ? ("search " + root.disp(root.baseEnc(pane.searchRoot))
                         + " and below — press Enter")
                      : "filter these items…   (Ctrl+F to search)"
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(12) }
                visible: filterInput.text === ""
            }

            Row {
                id: searchChips
                anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                spacing: 4
                visible: pane.searching

                ToggleChip {
                    label: pane.searchContent ? "In contents" : "By name"
                    on: pane.searchContent
                    onToggled: {
                        pane.searchContent = !pane.searchContent
                        if (pane.searchTerm) pane.runSearch()
                    }
                }
                ToggleChip {
                    label: "Done"
                    on: false
                    onToggled: { filterInput.text = ""; pane.endSearch() }
                }
            }
        }

        // One scrollbar per view, each anchored to the view it drives.
        VScroll {
            flick: fileList
            anchors {
                top: fileList.top; bottom: fileList.bottom
                left: fileList.right; leftMargin: 4
            }
        }
        VScroll {
            flick: fileGrid
            anchors {
                top: fileGrid.top; bottom: fileGrid.bottom
                left: fileGrid.right; leftMargin: 4
            }
        }
        HScroll {
            flick: fileGrid
            anchors {
                left: fileGrid.left; right: fileGrid.right
                top: fileGrid.bottom; topMargin: 2
            }
        }

        // ── About ───────────────────────────────────────────────────────
        // Not a credits screen. Almost everything this browser does beyond
        // listing a directory leans on something optional — gvfs, lsblk,
        // shared-mime-info, xdg-open — and when one is missing the feature
        // is silently EMPTY rather than broken. This says which.
        Flickable {
            anchors {
                top: tabStrip.bottom; left: parent.left
                right: parent.right; bottom: parent.bottom
            }
            anchors.margins: 18
            visible: pane.tab && pane.tab.view === "about"
            contentHeight: aboutCol.implicitHeight
            clip: true

            Column {
                id: aboutCol
                width: parent.width
                spacing: 6

                Text {
                    text: "SYNAPSE Files"
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: root.ui(20); bold: true  }
                }
                Text {
                    width: aboutCol.width
                    text: "A file browser for SynapseOS. Tabs, pinned places shared with "
                        + "Dolphin, recent files, volumes and network shares."
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                    wrapMode: Text.WordWrap
                    bottomPadding: 10
                }

                Repeater {
                    model: root.aboutRows
                    delegate: Rectangle {
                        id: aboutRow
                        required property var modelData
                        width: aboutCol.width
                        height: 52
                        radius: 4
                        color: root.wash(0.05)

                        readonly property color stateColor:
                            aboutRow.modelData.state === "ok"      ? root.cAccent
                          : aboutRow.modelData.state === "off"     ? root.cWarn
                          : aboutRow.modelData.state === "missing" ? root.cDim
                                                                   : root.cAccent

                        // A detail that is a URL opens in a browser. It is
                        // never split and handed to a shell — that path is
                        // for commands, and conflating the two would run
                        // whatever a detail string happened to contain.
                        readonly property bool openable:
                            aboutRow.modelData.detail !== undefined
                            && aboutRow.modelData.detail.indexOf("https://") === 0

                        Rectangle {
                            id: aboutDot
                            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                            width: 8; height: 8; radius: 4
                            color: aboutRow.stateColor
                        }
                        Text {
                            id: aboutKey
                            anchors { left: aboutDot.right; leftMargin: 12; verticalCenter: parent.verticalCenter }
                            width: 120
                            text: aboutRow.modelData.item
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(12); bold: true  }
                            elide: Text.ElideRight
                        }
                        Column {
                            anchors {
                                left: aboutKey.right; leftMargin: 12
                                right: aboutBtn.left; rightMargin: 12
                                verticalCenter: parent.verticalCenter
                            }
                            spacing: 2
                            Text {
                                width: parent.width
                                text: aboutRow.modelData.value
                                color: aboutRow.stateColor
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: aboutRow.modelData.detail
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                                elide: Text.ElideRight
                                visible: text !== ""
                            }
                        }
                        Rectangle {
                            id: aboutBtn
                            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                            width: 74; height: 26; radius: 4
                            visible: aboutRow.openable
                            color: aboutBtnMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                            border { width: 1; color: root.cAccent }
                            Text {
                                anchors.centerIn: parent
                                text: "Open"
                                color: root.cAccent
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                            MouseArea {
                                id: aboutBtnMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Qt.openUrlExternally(aboutRow.modelData.detail)
                                    root.statusLine = "opened in your browser"
                                }
                            }
                        }
                    }
                }
            }
        }

        // The whole pane is a target for the CURRENT folder. Declared
        // before the views, so a row or a cell — which is a more specific
        // answer — takes the drop first and this catches the empty space
        // around them, which is what "drop it in here" means.
        DropArea {
            id: paneDrop
            anchors {
                top: heads.bottom; left: parent.left
                right: parent.right; bottom: parent.bottom
            }
            enabled: pane.tab && pane.tab.view === "dir"
            property bool hovering: false
            onEntered: (drag) => {
                paneDrop.hovering = root.willAcceptDrop(pane.tab.path, drag)
                if (!paneDrop.hovering) drag.accepted = false
            }
            onExited: paneDrop.hovering = false
            onDropped: (drop) => {
                paneDrop.hovering = false
                root.handleDrop(pane.tab.path, drop)
            }

            // Says which folder is about to receive it, because a drop on
            // empty space has no row under the cursor to highlight.
            Rectangle {
                anchors.fill: parent
                visible: paneDrop.hovering
                color: "transparent"
                border { width: 2; color: root.cAccent }
                radius: 4
                opacity: 0.7
            }
        }

        // Column headings
        Item {
            id: heads
            anchors { top: filterBar.bottom; left: parent.left; right: parent.right }
            anchors.margins: 8
            anchors.topMargin: 4
            // Matches the list's own gutter, or "Size" and "Modified" sit
            // 14px to the right of the column they name.
            anchors.rightMargin: 22
            // Column headings are the Details view's own furniture: over a
            // grid of icons they name columns that are not there.
            height: heads.visible ? 20 : 0
            visible: pane.tab && pane.tab.view !== "about" && !root.gridView

            Text {
                anchors { left: parent.left; leftMargin: 40 }
                text: "Name"; color: root.cDim; font { family: root.uiFont; pixelSize: root.ui(10) }
            }
            Text {
                anchors { right: parent.right; rightMargin: 190 }
                text: "Size"; color: root.cDim; font { family: root.uiFont; pixelSize: root.ui(10) }
            }
            Text {
                anchors { right: parent.right; rightMargin: 20 }
                text: "Modified"; color: root.cDim; font { family: root.uiFont; pixelSize: root.ui(10) }
            }
        }

        ListView {
            id: fileList
            anchors {
                top: heads.bottom; left: parent.left
                right: parent.right; bottom: parent.bottom
            }
            anchors.margins: 8
            anchors.topMargin: 2
            // Room for the scrollbar, so a row never runs under the handle.
            anchors.rightMargin: 22
            clip: true
            visible: pane.tab && pane.tab.view !== "about" && !root.gridView
            model: pane.shownRows
            spacing: 1
            currentIndex: -1
            // Not simply `pane.isActive`: the list and the grid are siblings
            // in one pane, and both asking for the keyboard is the same fight
            // one pane down. The grid takes it whenever it is the view; the
            // list keeps it otherwise, About included — where neither is drawn
            // but Escape still has to close a dialog.
            focus: pane.isActive && !fileGrid.visible

            // Shortcuts a file manager is expected to have. Delete goes to
            // the TRASH — the permanent one is a separate command behind a
            // separate flag, and no key reaches it.
            Keys.onPressed: (event) => pane.handleKey(event)

            delegate: Rectangle {
                id: fileRow
                required property var modelData
                readonly property bool isSelected: pane.isSelected(fileRow.modelData.name)
                readonly property bool isRenaming: fileRow.modelData.name === pane.renaming
                property bool dropHover: false
                width: ListView.view.width
                height: pane.rowH
                radius: 3
                color: fileRow.dropHover ? root.wash(0.40)
                     : fileRow.isSelected ? root.wash(0.22)
                     : (rowMa.containsMouse ? root.wash(0.10) : "transparent")
                // The keyboard cursor, drawn only when it says something the
                // highlight does not: with one row selected they are the same
                // row, and a ring around it is noise.
                readonly property bool isCursor:
                    pane.isActive && pane.cursorName === fileRow.modelData.name
                    && (pane.selection.length > 1 || !fileRow.isSelected)
                border {
                    width: (fileRow.dropHover || fileRow.isCursor) ? 1 : 0
                    color: root.cAccent
                }

                // Four stages, walked on load failure rather than by
                // testing for the files first: QML has no way to stat a
                // path, and Image already reports Error when a source does
                // not resolve. Large cache, then normal cache, then the
                // file itself for images, then the mime icon.
                Image {
                    id: rowIcon
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    width: root.iconSize; height: root.iconSize
                    // 2x the drawn size, so a scaled thumbnail stays sharp.
                    sourceSize: Qt.size(root.iconSize * 2, root.iconSize * 2)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    opacity: fileRow.modelData.missing ? 0.4 : 1.0
                    cache: true
                    // A folder is drawn below instead.
                    visible: fileRow.modelData.type !== "dir"

                    property bool failed: false
                    source: rowIcon.failed ? root.iconFor(fileRow.modelData)
                                           : root.previewFor(fileRow.modelData)
                    // A corrupt or unreadable image is the only way to get
                    // here now; the icon is the answer.
                    onStatusChanged: if (status === Image.Error) rowIcon.failed = true
                    Connections {
                        target: fileRow
                        function onModelDataChanged() { rowIcon.failed = false }
                    }
                }

                FolderIcon {
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    width: root.iconSize; height: root.iconSize
                    visible: fileRow.modelData.type === "dir"
                    dim: fileRow.modelData.missing
                    previews: pane.folderPeek[fileRow.modelData.full] || []
                }

                Text {
                    anchors {
                        left: rowIcon.right; leftMargin: 10
                        right: sizeText.left; rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                    visible: !fileRow.isRenaming
                    // disp() — display only. Every action below uses
                    // modelData.full, which stays encoded.
                    // "config.json" appears eleven times in a source tree;
                    // the only useful thing about a search hit is which one
                    // it is, so the containing folder rides along.
                    text: root.rowLabel(fileRow.modelData)
                          + (fileRow.modelData.where
                             ? "      " + root.disp(fileRow.modelData.where) + "/" : "")
                          + (fileRow.modelData.link === "1" && fileRow.modelData.target
                             ? "  → " + root.disp(fileRow.modelData.target) : "")
                    elide: Text.ElideRight
                    color: fileRow.modelData.missing ? root.cDim
                         : (fileRow.modelData.type === "dir" ? root.cAccent : root.cText)
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                }

                // Inline rename. Seeded with the DECODED name because that
                // is what a person edits; what comes back is a new name
                // typed by hand, so it needs no decoding on the way out.
                Rectangle {
                    anchors {
                        left: rowIcon.right; leftMargin: 8
                        right: sizeText.left; rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                    height: 24
                    radius: 3
                    visible: fileRow.isRenaming
                    color: root.cPanel
                    border { width: 1; color: root.cAccent }
                    // The row-wide MouseArea is declared BELOW this and input
                    // is delivered in reverse paint order, so without a z the
                    // row swallows every click meant for the editor: clicking
                    // into the text to fix one character re-selected the row
                    // instead, and dragging across it started a drag. Same
                    // shape as the eject glyph the sidebar row sat on top of.
                    z: 5

                    TextInput {
                        id: renameInput
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                        clip: true
                        onVisibleChanged: {
                            if (visible) {
                                text = root.disp(fileRow.modelData.name)
                                forceActiveFocus()
                                select(0, root.stemLen(text, fileRow.modelData.type === "dir"))
                            }
                        }
                        onAccepted: root.commitRename(fileRow.modelData, text)
                        Keys.onEscapePressed: pane.renaming = ""
                    }
                }

                Text {
                    anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                    text: fileRow.modelData.deleted || ""
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                    visible: pane.tab && pane.tab.view === "trash"
                }

                // Restore is offered only where it means something, and it
                // passes the trashName back verbatim — the handle from the
                // listing, not something re-derived from the path, because
                // a second notes.txt is stored as notes.txt.2.
                Rectangle {
                    anchors { right: parent.right; rightMargin: 150; verticalCenter: parent.verticalCenter }
                    width: 66; height: 22; radius: 3
                    visible: pane.tab && pane.tab.view === "trash"
                             && !fileRow.modelData.missing
                    color: restoreMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                    border { width: 1; color: root.cAccent }
                    Text {
                        anchors.centerIn: parent
                        text: "Restore"
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(10) }
                    }
                    MouseArea {
                        id: restoreMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.restoreFromTrash(fileRow.modelData)
                    }
                }

                Text {
                    id: sizeText
                    anchors { right: parent.right; rightMargin: 190; verticalCenter: parent.verticalCenter }
                    text: root.fmtSize(fileRow.modelData.size, fileRow.modelData.type === "dir")
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
                Text {
                    anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                    text: root.fmtTime(fileRow.modelData.mtime)
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }

                // Only folders are targets — dropping onto a file has no
                // meaning, and highlighting one would promise otherwise.
                DropArea {
                    anchors.fill: parent
                    enabled: fileRow.modelData.type === "dir"
                    onEntered: (drag) => {
                        fileRow.dropHover =
                            root.willAcceptDrop(fileRow.modelData.full, drag)
                        if (!fileRow.dropHover) drag.accepted = false
                    }
                    onExited: fileRow.dropHover = false
                    onDropped: (drop) => {
                        fileRow.dropHover = false
                        root.handleDrop(fileRow.modelData.full, drop)
                    }
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    enabled: !fileRow.isRenaming
                    // A Flickable STEALS the mouse grab from its own
                    // delegates once the pointer moves past Qt's
                    // start-drag distance — that is how a list scrolls by
                    // being dragged. It is also why dragging a file did
                    // nothing: the grab was taken a pixel or two after the
                    // drag began, this MouseArea was sent onCanceled
                    // instead of onReleased, and the drop that lives in
                    // onReleased never ran. Holding the grab is what makes
                    // dragging a row mean the row. The view still scrolls
                    // by wheel and by its scrollbar.
                    preventStealing: true
                    onClicked: (mouse) => {
                        fileList.forceActiveFocus()
                        const name = fileRow.modelData.name

                        if (mouse.button === Qt.RightButton) {
                            // Right-clicking inside an existing selection
                            // keeps it: "copy these six" is the whole point
                            // of having selected six. Outside it, the click
                            // moves the selection first, the way it does
                            // everywhere else.
                            if (!pane.isSelected(name)) pane.selectOnly(name)
                            ctxMenu.row = fileRow.modelData
                            const gp = rowMa.mapToItem(ctxMenu.parent, mouse.x, mouse.y)
                            ctxMenu.rawX = gp.x
                            ctxMenu.rawY = gp.y
                            ctxMenu.open = true
                            root.loadActions()
                            return
                        }

                        if (mouse.modifiers & Qt.ControlModifier)    pane.toggleSelect(name)
                        else if (mouse.modifiers & Qt.ShiftModifier) pane.selectRange(name)
                        else                                         pane.selectOnly(name)
                    }
                    onPressed: (mouse) => {
                        // On the PRESS, not the click. A drag never produces a
                        // click, so claiming the pane in onClicked left every
                        // drag out of the inactive half reading the other
                        // pane's selection.
                        pane.claim()
                        rowMa.pressX = mouse.x
                        rowMa.pressY = mouse.y
                    }
                    // No release or cancel handler: the drag runs to
                    // completion inside beginDrag(), and onCanceled fires
                    // when QDrag TAKES the grab — mid-drag — which would
                    // clear the very paths the drop is about to use.
                    onPositionChanged: (mouse) => {
                        if (!pressed || root.dragging) return
                        root.dragCopy = (mouse.modifiers & Qt.ControlModifier) !== 0
                        const dx = mouse.x - rowMa.pressX
                        const dy = mouse.y - rowMa.pressY
                        if (dx * dx + dy * dy < root.dragThreshold * root.dragThreshold)
                            return
                        root.beginDrag(pane, fileRow.modelData,
                                       pane.selection.length > 1
                                       ? pane.selection.length + " items"
                                       : root.rowLabel(fileRow.modelData))
                    }
                    property real pressX: 0
                    property real pressY: 0
                    // Double-click to open, matching every other file
                    // manager. Single-click-to-open is a setting worth
                    // having and a default worth not having.
                    onDoubleClicked: root.activate(fileRow.modelData)
                }
            }
        }

        // The grid, for when the icons are the point rather than the
        // metadata. Same selection and menu behaviour as the list — only
        // the arrangement differs, so nothing here re-implements a
        // decision the list already made.
        GridView {
            id: fileGrid
            anchors {
                top: heads.bottom; left: parent.left
                right: parent.right; bottom: parent.bottom
            }
            anchors.margins: 8
            anchors.topMargin: 2
            // Room for the scrollbar, so a row never runs under the handle.
            anchors.rightMargin: 22
            // ...and room UNDER it in compact, where the bar is the
            // horizontal one and would otherwise sit on the status line.
            anchors.bottomMargin: root.compactView ? 22 : 8
            clip: true
            visible: pane.tab && pane.tab.view !== "about" && root.gridView
            model: pane.shownRows
            // Compact is the same view turned on its side: cells flow DOWN
            // a column and the column list runs off to the right, which is
            // what makes it compact — a name gets the width it needs
            // instead of a square.
            flow: root.compactView ? GridView.FlowTopToBottom
                                   : GridView.FlowLeftToRight
            cellWidth: root.compactView ? Math.max(150, root.iconSize * 5 + 60)
                                        : root.iconSize + 46
            cellHeight: root.compactView ? Math.max(22, root.iconSize + 8)
                                         : root.iconSize + 46
            focus: pane.isActive && fileGrid.visible

            Keys.onPressed: (event) => pane.handleKey(event)

            delegate: Rectangle {
                id: gridCell
                required property var modelData
                readonly property bool isSelected: pane.isSelected(gridCell.modelData.name)
                readonly property bool isRenaming: gridCell.modelData.name === pane.renaming
                property bool dropHover: false
                width: fileGrid.cellWidth - 6
                height: fileGrid.cellHeight - 6
                radius: 4
                color: gridCell.dropHover ? root.wash(0.40)
                     : gridCell.isSelected ? root.wash(0.22)
                     : (cellMa.containsMouse ? root.wash(0.10) : "transparent")
                readonly property bool isCursor:
                    pane.isActive && pane.cursorName === gridCell.modelData.name
                    && (pane.selection.length > 1 || !gridCell.isSelected)
                border {
                    width: (gridCell.dropHover || gridCell.isCursor) ? 1 : 0
                    color: root.cAccent
                }

                // One box holding whichever icon this row uses, so the
                // two layouts are expressed once here instead of three
                // times over the Image, the folder and the label.
                Item {
                    id: cellIconBox
                    width: root.iconSize
                    height: root.iconSize
                    anchors.top: root.compactView ? undefined : parent.top
                    anchors.topMargin: 6
                    anchors.horizontalCenter: root.compactView ? undefined
                                                               : parent.horizontalCenter
                    anchors.left: root.compactView ? parent.left : undefined
                    anchors.leftMargin: 6
                    anchors.verticalCenter: root.compactView ? parent.verticalCenter
                                                             : undefined
                }

                Image {
                    id: cellIcon
                    anchors.fill: cellIconBox
                    sourceSize: Qt.size(root.iconSize * 2, root.iconSize * 2)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    opacity: gridCell.modelData.missing ? 0.4 : 1.0
                    visible: gridCell.modelData.type !== "dir"

                    property bool failed: false
                    source: cellIcon.failed ? root.iconFor(gridCell.modelData)
                                            : root.previewFor(gridCell.modelData)
                    onStatusChanged: if (status === Image.Error) cellIcon.failed = true
                    Connections {
                        target: gridCell
                        function onModelDataChanged() { cellIcon.failed = false }
                    }
                }

                FolderIcon {
                    anchors.fill: cellIconBox
                    visible: gridCell.modelData.type === "dir"
                    dim: gridCell.modelData.missing
                    previews: pane.folderPeek[gridCell.modelData.full] || []
                }

                Text {
                    id: cellLabel
                    anchors.top: root.compactView ? undefined : cellIconBox.bottom
                    anchors.topMargin: 4
                    anchors.left: root.compactView ? cellIconBox.right : parent.left
                    anchors.leftMargin: root.compactView ? 8 : 4
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: root.compactView ? parent.verticalCenter
                                                             : undefined
                    visible: !gridCell.isRenaming
                    text: root.rowLabel(gridCell.modelData)
                    color: gridCell.modelData.missing ? root.cDim
                         : (gridCell.modelData.type === "dir" ? root.cAccent : root.cText)
                    font { family: root.uiFont
                           pixelSize: root.ui(root.compactView ? 11 : 10) }
                    horizontalAlignment: root.compactView ? Text.AlignLeft
                                                          : Text.AlignHCenter
                    // Two lines and then elide: one line hides too much of
                    // a long name, and three turns the grid into a wall.
                    // Compact gets ONE — the cell is a row, and a second
                    // line there would push every column out of alignment.
                    maximumLineCount: root.compactView ? 1 : 2
                    wrapMode: root.compactView ? Text.NoWrap : Text.WrapAnywhere
                    elide: Text.ElideRight
                }

                // Inline rename, drawn where the label is. THE SAME OMISSION
                // AS DRAG-AND-DROP BELOW, found the same way: the editor was
                // wired into the list delegate only, so in Icons and Compact —
                // where anyone with previews on is working — the Rename entry
                // set pane.renaming, nothing was drawn to edit, and the menu
                // read as a dead button. F2 was dead there for the same reason.
                //
                // Whatever is added to one delegate has to be added to the
                // other; they are two renderings of one row, not two features.
                Rectangle {
                    // The label's own anchors, repeated rather than anchored
                    // TO the label: a top anchor and a verticalCenter on one
                    // item is the pair Qt refuses, and the label switches
                    // between them with the layout.
                    anchors.top: root.compactView ? undefined : cellIconBox.bottom
                    anchors.topMargin: 4
                    anchors.left: root.compactView ? cellIconBox.right : parent.left
                    anchors.leftMargin: root.compactView ? 8 : 4
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: root.compactView ? parent.verticalCenter
                                                             : undefined
                    height: Math.max(20, root.ui(20))
                    radius: 3
                    visible: gridCell.isRenaming
                    color: root.cPanel
                    border { width: 1; color: root.cAccent }
                    // Above the neighbouring cells: an editor for a long name
                    // is wider than the cell it belongs to, and being painted
                    // over by the next delegate is how it would look truncated.
                    z: 5

                    TextInput {
                        id: cellRenameInput
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                        clip: true
                        onVisibleChanged: {
                            if (visible) {
                                text = root.disp(gridCell.modelData.name)
                                forceActiveFocus()
                                select(0, root.stemLen(text, gridCell.modelData.type === "dir"))
                            }
                        }
                        onAccepted: root.commitRename(gridCell.modelData, text)
                        Keys.onEscapePressed: pane.renaming = ""
                    }
                }

                // Both halves of drag-and-drop, the same as the list
                // rows have. They were only ever wired into the list, so
                // dragging did nothing at all in icon view — which is the
                // view anybody who turned previews on is looking at.
                DropArea {
                    anchors.fill: parent
                    enabled: gridCell.modelData.type === "dir"
                    onEntered: (drag) => {
                        gridCell.dropHover =
                            root.willAcceptDrop(gridCell.modelData.full, drag)
                        if (!gridCell.dropHover) drag.accepted = false
                    }
                    onExited: gridCell.dropHover = false
                    onDropped: (drop) => {
                        gridCell.dropHover = false
                        root.handleDrop(gridCell.modelData.full, drop)
                    }
                }

                MouseArea {
                    id: cellMa
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // A Flickable STEALS the mouse grab from its own
                    // delegates once the pointer moves past Qt's
                    // start-drag distance — that is how a list scrolls by
                    // being dragged. It is also why dragging a file did
                    // nothing: the grab was taken a pixel or two after the
                    // drag began, this MouseArea was sent onCanceled
                    // instead of onReleased, and the drop that lives in
                    // onReleased never ran. Holding the grab is what makes
                    // dragging a row mean the row. The view still scrolls
                    // by wheel and by its scrollbar.
                    preventStealing: true
                    property real pressX: 0
                    property real pressY: 0
                    onPressed: (mouse) => {
                        pane.claim()
                        cellMa.pressX = mouse.x
                        cellMa.pressY = mouse.y
                    }
                    onPositionChanged: (mouse) => {
                        if (!cellMa.pressed || root.dragging) return
                        root.dragCopy = (mouse.modifiers & Qt.ControlModifier) !== 0
                        const dx = mouse.x - cellMa.pressX
                        const dy = mouse.y - cellMa.pressY
                        if (dx * dx + dy * dy < root.dragThreshold * root.dragThreshold)
                            return
                        root.beginDrag(pane, gridCell.modelData,
                                       pane.selection.length > 1
                                       ? pane.selection.length + " items"
                                       : root.rowLabel(gridCell.modelData))
                    }
                    onClicked: (mouse) => {
                        fileGrid.forceActiveFocus()
                        const name = gridCell.modelData.name
                        if (mouse.button === Qt.RightButton) {
                            if (!pane.isSelected(name)) pane.selectOnly(name)
                            ctxMenu.row = gridCell.modelData
                            const p = cellMa.mapToItem(ctxMenu.parent, mouse.x, mouse.y)
                            ctxMenu.rawX = p.x
                            ctxMenu.rawY = p.y
                            ctxMenu.open = true
                            root.loadActions()
                            return
                        }
                        if (mouse.modifiers & Qt.ControlModifier)    pane.toggleSelect(name)
                        else if (mouse.modifiers & Qt.ShiftModifier) pane.selectRange(name)
                        else                                         pane.selectOnly(name)
                    }
                    onDoubleClicked: root.activate(gridCell.modelData)
                }
            }
        }

        // Empty state
        Column {
            anchors.centerIn: parent
            spacing: 6
            // About is not a listing, so it has no rows and is not empty.
            // Without that test this printed "This folder is empty." across
            // the middle of the About pane, on top of the Support line.
            visible: !pane.loading && pane.shownRows.length === 0
                     && pane.tab && pane.tab.view !== "about"

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    if (!pane.tab) return ""
                    if (pane.searching)
                        return pane.searchTerm === ""
                               ? "Type to search this folder and everything below it."
                               : "Nothing matched."
                    if (pane.tab.filter) return "Nothing matches that filter."
                    if (pane.tab.view === "recent") return "No recently used files."
                    if (pane.tab.view === "trash") return "The trash is empty."
                    return "This folder is empty."
                }
                color: root.cText
                font { family: root.uiFont; pixelSize: root.ui(14) }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: pane.tab && pane.tab.view === "dir" && !pane.tab.showHidden
                      ? "Hidden items are not shown." : ""
                color: root.cDim
                font { family: root.uiFont; pixelSize: root.ui(11) }
                visible: text !== ""
            }
        }

        // ── The empty space: rubber band, and the menu that belongs to it ───
        //
        // One MouseArea over BOTH views, on top of them, that hands back every
        // press landing on a row. That is the whole trick, and it is written
        // this way for two reasons.
        //
        // Under the views it would never see a left press at all: a ListView
        // is a Flickable, and a Flickable takes the press anywhere in its
        // bounds — that is how a list is dragged to scroll. Over the views and
        // greedy, it would swallow the row clicks, the drags and the double
        // clicks the delegates already implement. Over them and DECLINING is
        // the only arrangement where both work: `indexAt()` answers whether
        // this point is a row, and `accepted = false` on a press passes it
        // down to the delegate exactly as if this were not here.
        //
        // The trade is that dragging the empty space no longer flicks the
        // view. That is the same trade every file manager makes — the wheel
        // and the scrollbar still scroll, and dragging in the empty space is
        // how a band is drawn.
        MouseArea {
            id: bandMa
            anchors.fill: root.gridView ? fileGrid : fileList
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            enabled: pane.tab && pane.tab.view !== "about" && pane.renaming === ""
            // The band is drawn in CONTENT coordinates, so once the view
            // scrolls under it the rectangle reaches past the top of the view —
            // over the column headings and the toolbar — unless it is clipped
            // to the area it is selecting in.
            clip: true
            // Not hoverEnabled: rows light up under the pointer from their own
            // MouseAreas, and an item that accepts hover on top of them would
            // take that away.

            // The band, in CONTENT coordinates. A band that scrolls with the
            // view has to be anchored to the content, not to the window: hold
            // the pointer at the bottom edge, the view scrolls under it, and
            // the corner the drag started from is metres up the list by then.
            property bool banding: false
            property real ax: 0
            property real ay: 0
            property real bx: 0
            property real by: 0
            // What was selected when the band began, so Ctrl+drag can add to
            // it rather than replace it.
            property var baseSel: []
            property bool additive: false

            readonly property Flickable view: root.gridView ? fileGrid : fileList

            function indexAtPoint(px, py) {
                return bandMa.view.indexAt(px + bandMa.view.contentX,
                                           py + bandMa.view.contentY)
            }

            // Every row the band touches, by SAMPLING the view rather than by
            // recomputing the layout. Qt already placed these items; asking it
            // where they are cannot disagree with where they were drawn, which
            // a second copy of the arithmetic eventually would — and it is the
            // only way that survives the compact view flowing top-to-bottom.
            //
            // The step is half a cell, which guarantees a sample inside every
            // cell the rectangle overlaps: nothing is ever a whole step
            // narrower than the gap between samples.
            function namesInBand() {
                const rows = pane.shownRows
                const out = []
                if (rows.length === 0) return out
                const x1 = Math.min(bandMa.ax, bandMa.bx), x2 = Math.max(bandMa.ax, bandMa.bx)
                const y1 = Math.min(bandMa.ay, bandMa.by), y2 = Math.max(bandMa.ay, bandMa.by)
                const stepX = root.gridView ? Math.max(4, fileGrid.cellWidth / 2)
                                            : Math.max(4, bandMa.width)
                const stepY = root.gridView ? Math.max(4, fileGrid.cellHeight / 2)
                                            : Math.max(4, pane.rowH / 2)
                const seen = {}
                for (let y = y1; ; y += stepY) {
                    const yy = Math.min(y, y2)
                    for (let x = x1; ; x += stepX) {
                        const xx = Math.min(x, x2)
                        const i = bandMa.view.indexAt(xx, yy)
                        if (i >= 0 && i < rows.length && !seen[i]) {
                            seen[i] = true
                            out.push(rows[i].name)
                        }
                        if (xx >= x2) break
                    }
                    if (yy >= y2) break
                }
                return out
            }

            function applyBand() {
                const hit = bandMa.namesInBand()
                if (!bandMa.additive) { pane.selection = hit; return }
                const copy = bandMa.baseSel.slice()
                for (const n of hit) if (copy.indexOf(n) < 0) copy.push(n)
                pane.selection = copy
            }

            function endBand() {
                bandMa.banding = false
                edgeScroll.stop()
                // Leave the keyboard where the band finished. Without this,
                // Shift+Down straight after dragging a selection extends from
                // nothing and collapses the lot to a single row.
                const sel = pane.selection
                if (sel.length > 0) {
                    pane.anchorName = sel[0]
                    pane.cursorName = sel[sel.length - 1]
                }
            }

            onPressed: (mouse) => {
                pane.claim()
                if (bandMa.indexAtPoint(mouse.x, mouse.y) >= 0) {
                    // A row. Everything about rows is the delegate's business.
                    mouse.accepted = false
                    return
                }
                bandMa.view.forceActiveFocus()

                if (mouse.button === Qt.RightButton) {
                    // The background menu is the SAME menu with no row in it,
                    // so there is one menu to style, position and dismiss.
                    ctxMenu.row = null
                    const gp = bandMa.mapToItem(ctxMenu.parent, mouse.x, mouse.y)
                    ctxMenu.rawX = gp.x
                    ctxMenu.rawY = gp.y
                    ctxMenu.open = true
                    return
                }

                bandMa.additive = (mouse.modifiers & Qt.ControlModifier) !== 0
                bandMa.baseSel = pane.selection.slice()
                if (!bandMa.additive) pane.clearSelection()
                bandMa.ax = mouse.x + bandMa.view.contentX
                bandMa.ay = mouse.y + bandMa.view.contentY
                bandMa.bx = bandMa.ax
                bandMa.by = bandMa.ay
                bandMa.banding = true
            }

            onPositionChanged: (mouse) => {
                if (!bandMa.banding) return
                bandMa.bx = mouse.x + bandMa.view.contentX
                bandMa.by = mouse.y + bandMa.view.contentY
                // Dragging past the edge scrolls, or the band can only ever
                // select what one screenful holds.
                //
                // Along the axis the VIEW scrolls on, which the compact grid
                // turns on its side: it flows into columns and scrolls
                // sideways, so there the edge to push against is the right
                // one, not the bottom.
                const along = root.compactView ? mouse.x : mouse.y
                const extent = root.compactView ? bandMa.width : bandMa.height
                edgeScroll.dir = along < 0 ? -1 : (along > extent ? 1 : 0)
                edgeScroll.running = edgeScroll.dir !== 0
                bandMa.applyBand()
            }

            onReleased: bandMa.endBand()
            // A grab taken away mid-drag must not leave the band painted over
            // the view forever.
            onCanceled: bandMa.endBand()

            Timer {
                id: edgeScroll
                property int dir: 0
                interval: 16
                repeat: true
                onTriggered: {
                    const v = bandMa.view
                    if (root.compactView) {
                        const maxX = Math.max(0, v.contentWidth - v.width)
                        v.contentX = Math.max(0, Math.min(maxX, v.contentX + edgeScroll.dir * 12))
                        bandMa.bx = (edgeScroll.dir < 0 ? 0 : bandMa.width) + v.contentX
                        bandMa.applyBand()
                        if (v.contentX <= 0 || v.contentX >= maxX) edgeScroll.stop()
                        return
                    }
                    const max = Math.max(0, v.contentHeight - v.height)
                    v.contentY = Math.max(0, Math.min(max, v.contentY + edgeScroll.dir * 12))
                    // The far corner follows the scroll, so the band keeps
                    // growing while the pointer is held still outside it.
                    bandMa.by = (edgeScroll.dir < 0 ? 0 : bandMa.height) + v.contentY
                    bandMa.applyBand()
                    if (v.contentY <= 0 || v.contentY >= max) edgeScroll.stop()
                }
            }

            Rectangle {
                visible: bandMa.banding
                x: Math.min(bandMa.ax, bandMa.bx) - bandMa.view.contentX
                y: Math.min(bandMa.ay, bandMa.by) - bandMa.view.contentY
                width: Math.abs(bandMa.bx - bandMa.ax)
                height: Math.abs(bandMa.by - bandMa.ay)
                color: Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.15)
                border { width: 1; color: root.cAccent }
                radius: 2
            }
        }

        // ── Context menu ────────────────────────────────────────────────
        // Built from a model rather than hand-placed rows so that what a
        // given entry DOES and whether it is offered at all live in one
        // place. "Move to Trash" is offered; permanent delete is not — it
        // exists in the binary behind an explicit flag and no click in
        // this window reaches it.
        MouseArea {
            anchors.fill: parent
            visible: ctxMenu.open
            acceptedButtons: Qt.AllButtons
            onClicked: ctxMenu.open = false
            onPressed: ctxMenu.open = false
        }

        Rectangle {
            id: ctxMenu
            property bool open: false
            property var row: null

            // ── The "Open with" submenu ─────────────────────────────────
            //
            // One flyout, driven by whichever row is currently pointing at it:
            // `subItems` is that row's list and `subY` its position, so there
            // is one panel rather than one per row and nothing to keep in step
            // when the menu is rebuilt.
            //
            // ⚠ IT IS A SIBLING OF THE FLICKABLE, NOT A CHILD OF THE ROW. A
            // child would be clipped by the parent menu's `clip: true` — the
            // flyout would be cut off at the menu's right edge and look like a
            // 4px sliver — and it would scroll away with the list, which is the
            // same reason VScroll below sits outside it.
            property var  subItems: []
            property real subY: 0
            property bool subOpen: false
            function closeSub() { ctxMenu.subOpen = false; ctxMenu.subItems = [] }

            // Rows have no gaps, and the flyout hangs beside whichever one is
            // further down the list than "Open with" — reaching an entry in
            // it means moving the pointer DOWN as well as right. That path
            // grazes the row below "Open with" for a frame, and an instant
            // close on entering it read as "the menu closes when I go for
            // it": the flyout vanished before the pointer ever reached it.
            // A short grace period survives that graze; only a pointer that
            // is still over neither the trigger row nor the flyout when the
            // timer fires actually meant to leave.
            Timer {
                id: subCloseTimer
                interval: 300
                onTriggered: ctxMenu.closeSub()
            }

            visible: ctxMenu.open
            onOpenChanged: if (!ctxMenu.open) ctxMenu.closeSub()
            onRowChanged: ctxMenu.closeSub()
            width: 210
            // Clamped as a BINDING, not assigned once on open. The action
            // list arrives asynchronously, so the menu grows AFTER it is
            // positioned — which is why it hung off the bottom of the
            // window and was clipped at the border.
            readonly property real wantX: ctxMenu.rawX
            readonly property real wantY: ctxMenu.rawY
            property real rawX: 0
            property real rawY: 0
            x: Math.max(4, Math.min(ctxMenu.rawX, parent.width - width - 4))
            y: Math.max(4, Math.min(ctxMenu.rawY, parent.height - height - 4))

            // And capped, so a menu with a dozen Open With entries scrolls
            // instead of being taller than the window can show.
            height: Math.min(ctxCol.implicitHeight + 8, parent.height - 16)
            radius: 4
            color: root.cPanel
            border { width: 1; color: root.wash(0.35) }
            z: 100

            Flickable {
                id: ctxFlick
                anchors { fill: parent; margins: 4 }
                contentHeight: ctxCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

            Column {
                id: ctxCol
                width: parent.width

                Repeater {
                    model: {
                        const t = pane.tab
                        if (!t) return []

                        // ── The empty space ─────────────────────────────
                        //
                        // No row means the click was about the FOLDER, and
                        // that is a different menu rather than a shorter one:
                        // nothing here acts on a file, and the file menu's own
                        // entries would all be greyed out.
                        //
                        // The create-and-select block used to live at the
                        // bottom of the file menu with a comment saying it was
                        // only there because there was no empty-space menu.
                        // This is that menu; the block moved rather than being
                        // copied, so New Folder is offered in one place.
                        //
                        // State rides in the HINT column, which is already
                        // drawn and right-aligned — a tick glyph pushed into
                        // the label would shift every other entry's text.
                        if (!ctxMenu.row) {
                            const dir = t.view === "dir"
                            const bg = [
                                { label: "New Folder…",     act: "newdir",  on: dir, hint: "Ctrl+N" },
                                { label: "New Empty File…", act: "newfile", on: dir },
                                { label: "-", act: "", on: true },
                                { label: "Paste", act: "paste", hint: "Ctrl+V",
                                  on: dir && root.clip.paths.length > 0 },
                                { label: "-", act: "", on: true },
                                { label: "Select All", act: "selectall", on: true, hint: "Ctrl+A" },
                                { label: "Invert Selection", act: "invert", on: true },
                                { label: "-", act: "", on: true },
                                { label: "Sort by Name",     act: "sort:name",
                                  on: dir, hint: t.sort === "name"  ? "✓" : "" },
                                { label: "Sort by Size",     act: "sort:size",
                                  on: dir, hint: t.sort === "size"  ? "✓" : "" },
                                { label: "Sort by Modified", act: "sort:mtime",
                                  on: dir, hint: t.sort === "mtime" ? "✓" : "" },
                                { label: "Descending",       act: "reverse",
                                  on: dir, hint: t.reverse ? "✓" : "" },
                                { label: "Show Hidden Files", act: "hidden",
                                  on: dir, hint: t.showHidden ? "✓" : "" },
                                { label: "-", act: "", on: true },
                                { label: "Refresh", act: "refresh", on: true, hint: "F5" },
                                { label: "Open Terminal Here", act: "term", on: dir },
                                { label: "Properties…", act: "folderprops", on: dir }
                            ]
                            return bg
                        }

                        const r = ctxMenu.row
                        const n = pane.selection.length
                        const one = n <= 1
                        if (t.view === "trash")
                            return [{ label: "Restore", act: "restore",
                                      on: !r.missing }]

                        // Labels count, so it is never a surprise how much
                        // a menu entry is about to act on.
                        const many = one ? "" : " (" + n + ")"
                        const items = [
                            { label: r.type === "dir" ? "Open Folder" : "Open",
                              act: "open", on: one && !r.missing },
                            { label: "Open in New Tab", act: "tab",
                              on: one && r.type === "dir" }
                        ]
                        if (t.view === "dir") {
                            items.push({ label: "-", act: "", on: true })
                            items.push({ label: "Copy" + many,  act: "copy", on: n > 0 })
                            items.push({ label: "Cut" + many,   act: "cut",  on: n > 0 })
                            items.push({ label: "Paste",        act: "paste",
                                         on: root.clip.paths.length > 0 })
                            items.push({ label: "-", act: "", on: true })
                            items.push({ label: "Rename…", act: "rename", on: one,
                                         hint: "F2" })
                            items.push({ label: "Move to Trash" + many,
                                         act: "trash", on: n > 0, hint: "Del" })
                        }
                        // Borrowed entries. The applications go in a SUBMENU
                        // and the service menus stay flat, and the difference
                        // is how many there are: an ordinary PNG offers six
                        // applications and two services here.
                        //
                        // This used to be flat too, with a comment saying a
                        // submenu that is empty half the time is worse than
                        // four extra rows. That was written when there were
                        // four. Six "Open with …" rows are most of the menu,
                        // they push Properties and Compress off the bottom of a
                        // short window, and their labels are the ones long
                        // enough to elide — "Open with GNU Image Manipulati…"
                        // says almost nothing while taking a whole row.
                        //
                        // ⚠ THE OLD COMMENT'S POINT STILL STANDS, so the row is
                        // only added when there IS something behind it: no
                        // applications, no "Open with" row. A submenu arrow
                        // that opens onto nothing is the failure it warned of.
                        const opens = root.rowActions.filter(a => a.kind === "open-with")
                        const svcs  = root.rowActions.filter(a => a.kind === "service")

                        if (opens.length > 0) {
                            items.push({ label: "-", act: "", on: true })
                            // No slice: the cap existed because six rows were
                            // already too many to sit in the middle of this
                            // menu. In a submenu of its own the list is the
                            // whole content, and it scrolls like the parent.
                            items.push({ label: "Open with", act: "submenu",
                                         on: true, sub: opens.map(a => ({
                                             label: a.label, desktop: a.desktop
                                         })) })
                        }
                        if (svcs.length > 0) {
                            items.push({ label: "-", act: "", on: true })
                            for (const a of svcs)
                                items.push({ label: a.label, act: "run", on: true,
                                             desktop: a.desktop, actionId: a.action })
                        }

                        if (t.view === "dir") {
                            items.push({ label: "-", act: "", on: true })
                            for (const f of [".tar.gz", ".zip", ".7z"])
                                items.push({ label: "Compress to " + f,
                                             act: "compress", on: n > 0,
                                             fmt: f.substring(1) })
                        }

                        // Properties leads the "what IS this" group rather than
                        // trailing the whole menu. It used to be the last of
                        // ~31 entries — for one PNG the menu is Open, Open in
                        // New Tab, Copy/Cut/Paste, Rename, Trash, three
                        // Open-with, two services, three Compress, and the
                        // create/select block before you reach it, about 650px
                        // of menu in a 700px window. It was present, enabled,
                        // and bound to Alt+Enter the whole time, and still read
                        // as missing, which is the only test that counts.
                        items.push({ label: "-", act: "", on: true })
                        items.push({ label: "Properties…", act: "props", on: n > 0,
                                     hint: "Alt+Enter" })
                        items.push({ label: "Copy Path", act: "copypath", on: one })
                        // ⚠ Refresh is HERE as well as in the empty-space menu,
                        // and it is not a duplicate worth removing: a pane that
                        // is full of files has no empty space to right-click,
                        // so the only menu reachable was the one without it —
                        // and F5 is no answer to somebody whose hand is on the
                        // mouse. It is about the pane rather than the row,
                        // which is why it sits in this trailing group with
                        // Open Terminal Here rather than up among Copy and Cut.
                        items.push({ label: "Refresh", act: "refresh", on: true,
                                     hint: "F5" })
                        items.push({ label: "Open Terminal Here", act: "term",
                                     on: t.view === "dir" })
                        items.push({ label: root.isPinned(r.full) ? "Remove from Places"
                                                                 : "Add to Places",
                                     act: "pin", on: one && r.type === "dir" })
                        // The create/select block that used to end this menu is
                        // GONE from here — it is about the folder, not about
                        // the file that was clicked, and it now lives in the
                        // empty-space menu above. Right-clicking a file for
                        // "New Folder" was thirty-one entries deep and the
                        // wrong place to look for it.
                        return items
                    }
                    delegate: Item {
                        id: ctxItem
                        required property var modelData
                        width: ctxCol.width
                        height: ctxItem.modelData.label === "-" ? 5 : 26

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
                            color: ctxMa.containsMouse && ctxItem.modelData.on
                                   ? root.wash(0.18) : "transparent"

                            Text {
                                anchors {
                                    left: parent.left; leftMargin: 10
                                    right: ctxHint.left; rightMargin: 6
                                    verticalCenter: parent.verticalCenter
                                }
                                // Elided: "Open with GNU Image Manipulation
                                // Program" is wider than the 210px menu and was
                                // simply cut off by the Flickable's clip, with
                                // no ellipsis to say so.
                                elide: Text.ElideRight
                                text: ctxItem.modelData.label
                                color: ctxItem.modelData.on ? root.cText : root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                            // The key that does the same thing, where the eye
                            // already looks for it. Properties was reachable by
                            // Alt+Enter long before anyone could have guessed.
                            //
                            // A row with a submenu shows an arrow here instead.
                            // "▸" is drawn as text on purpose: the two glyphs
                            // are in every font this UI can be set to, and the
                            // hint column is already right-aligned and already
                            // drawn, so it costs no layout.
                            Text {
                                id: ctxHint
                                anchors { right: parent.right; rightMargin: 10
                                          verticalCenter: parent.verticalCenter }
                                text: ctxItem.modelData.act === "submenu"
                                      ? "\u25B8" : (ctxItem.modelData.hint || "")
                                visible: text !== ""
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(10) }
                            }
                            MouseArea {
                                id: ctxMa
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: ctxItem.modelData.on
                                cursorShape: Qt.PointingHandCursor
                                // Hover is what opens and closes the flyout.
                                // Entering ANY other row STARTS the close
                                // grace timer (subCloseTimer, on ctxMenu) —
                                // it is what stops the submenu hanging over
                                // entries it has nothing to do with — but does
                                // not close it outright: reaching a row in the
                                // flyout that sits below "Open with" means
                                // moving the pointer down as well as right,
                                // which grazes the row below this one, and an
                                // instant close there closed the flyout before
                                // the pointer ever reached it.
                                onEntered: {
                                    if (ctxItem.modelData.act === "submenu") {
                                        subCloseTimer.stop()
                                        ctxMenu.subItems = ctxItem.modelData.sub
                                        ctxMenu.subY = ctxItem.mapToItem(ctxMenu, 0, 0).y
                                        ctxMenu.subOpen = true
                                    } else if (ctxMenu.subOpen) {
                                        subCloseTimer.restart()
                                    }
                                }
                                onClicked: {
                                    // A submenu row is a place to point at, not
                                    // a thing to do. Clicking it must not close
                                    // the menu out from under the flyout it
                                    // just opened.
                                    if (ctxItem.modelData.act === "submenu")
                                        return
                                    const r = ctxMenu.row
                                    ctxMenu.open = false
                                    switch (ctxItem.modelData.act) {
                                    case "open":    root.activate(r); break
                                    case "tab":     pane.newTab(r.full, "dir"); break
                                    case "copy":    root.copySelection(false); break
                                    case "cut":     root.copySelection(true); break
                                    case "paste":   root.paste(); break
                                    case "rename":  pane.renaming = r.name; break
                                    case "trash":   root.trashSelection(); break
                                    case "restore": root.restoreFromTrash(r); break
                                    case "props":   root.openProperties(); break
                                    case "run":     root.runAction(ctxItem.modelData.desktop,
                                                                   ctxItem.modelData.actionId); break
                                    case "copypath": root.copyToClipboard(root.disp(r.full)); break
                                    case "term":     root.openTerminalHere(); break
                                    case "pin":
                                        if (root.isPinned(r.full)) root.unpin(r.full)
                                        else root.pin(r.full)
                                        break
                                    case "compress": root.compressSelection(ctxItem.modelData.fmt); break
                                    case "newdir":   root.creating = true; break
                                    case "newfile":  root.creatingFile = true; break
                                    case "selectall": pane.selectAll(); break
                                    case "invert": {
                                        const inv = pane.shownRows
                                            .filter(x => !pane.isSelected(x.name))
                                            .map(x => x.name)
                                        pane.selection = inv
                                        break
                                    }
                                    // ── Entries that do not need a row ───
                                    // Everything above needs `r`; these are
                                    // reached with it null, which is what a
                                    // background click means. ⚠ Refresh is
                                    // also offered from the ROW menu, because
                                    // a full pane has no background to click.
                                    case "refresh":     pane.reload(); break
                                    case "folderprops": root.openFolderProperties(); break
                                    case "sort:name":
                                    case "sort:size":
                                    case "sort:mtime":
                                    case "reverse":
                                    case "hidden":
                                        // The view menu's own handler, not a
                                        // second copy: sort and hidden are
                                        // saved settings, and a menu that set
                                        // them without persisting them would
                                        // disagree with the View button.
                                        root.applyViewAction(ctxItem.modelData.act)
                                        break
                                    }
                                }
                            }
                        }
                    }
                }
            }
            }

            // A capped menu used to hide its tail in silence. The menu height
            // is min(content, window - 16), so on a short window the last
            // entries are simply not there to look at — which is one of the
            // two reasons Properties read as missing. A sibling of the
            // Flickable, not a child, or it would scroll away with the list.
            // VScroll hides itself when everything fits, so an ordinary short
            // menu is unchanged.
            VScroll {
                flick: ctxFlick
                anchors {
                    top: parent.top; bottom: parent.bottom
                    right: parent.right; margins: 4
                }
            }
        }

        // ── The "Open with" flyout ──────────────────────────────────────────
        //
        // A sibling of ctxMenu rather than a child, for the same reason VScroll
        // is a sibling of the Flickable: ctxFlick clips, and a flyout inside it
        // would be sliced off at the menu's own right edge.
        //
        // ⚠ z ABOVE ctxMenu (100). Sitting later in the file is not enough when
        // the sibling sets its own z — the flyout overlaps the menu by 4px so
        // the pointer cannot fall through the gap between them, and underneath
        // it that overlap would be the menu drawing over the flyout instead.
        Rectangle {
            id: ctxSub
            visible: ctxMenu.open && ctxMenu.subOpen && ctxMenu.subItems.length > 0
            width: 210
            height: Math.min(ctxSubCol.implicitHeight + 8, parent.height - 16)
            radius: 4
            color: root.cPanel
            border { width: 1; color: root.wash(0.35) }
            z: 101

            // To the right of the parent menu, or to its LEFT when there is no
            // room — a right-click near the right edge of the window is the
            // ordinary case, not an edge case, and a flyout clamped to the
            // screen edge would cover the menu it belongs to.
            readonly property bool flip:
                ctxMenu.x + ctxMenu.width - 4 + width > parent.width - 4
            x: ctxSub.flip ? Math.max(4, ctxMenu.x - width + 4)
                           : ctxMenu.x + ctxMenu.width - 4
            // Aligned with the row that opened it, then clamped so a row near
            // the bottom does not hang the flyout off the window.
            y: Math.max(4, Math.min(ctxMenu.y + ctxMenu.subY - 4,
                                    parent.height - height - 4))

            // Entering here cancels whatever grace period a graze over a
            // sibling row started — the pointer arrived, so it was headed
            // here after all. Leaving the flyout used to close it right
            // away, on the claim that no legitimate path leaves through here
            // without meaning to be done — but overshooting past the last
            // entry, or correcting back after clipping the 4px overlap band
            // toward a row near the top, both exit these bounds too. Same
            // 300ms grace as the row-to-flyout hop, not an instant close.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
                onEntered: subCloseTimer.stop()
                onExited: subCloseTimer.restart()
            }

            Flickable {
                id: ctxSubFlick
                anchors { fill: parent; margins: 4 }
                contentHeight: ctxSubCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: ctxSubCol
                    width: parent.width

                    Repeater {
                        model: ctxMenu.subItems
                        delegate: Item {
                            id: ctxSubItem
                            required property var modelData
                            width: ctxSubCol.width
                            height: 26

                            Rectangle {
                                anchors.fill: parent
                                radius: 3
                                color: ctxSubMa.containsMouse ? root.wash(0.18)
                                                              : "transparent"
                                Text {
                                    anchors {
                                        left: parent.left; leftMargin: 10
                                        right: parent.right; rightMargin: 10
                                        verticalCenter: parent.verticalCenter
                                    }
                                    // Still elided — "GNU Image Manipulation
                                    // Program" is wider than 210px even without
                                    // "Open with " in front of it. It has ten
                                    // characters more to work with here, which
                                    // is the difference between "GNU Image
                                    // Manipulati…" and something readable.
                                    elide: Text.ElideRight
                                    text: ctxSubItem.modelData.label
                                    color: root.cText
                                    font { family: root.uiFont
                                           pixelSize: root.ui(12) }
                                }
                                MouseArea {
                                    id: ctxSubMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        ctxMenu.open = false
                                        root.runAction(ctxSubItem.modelData.desktop, "")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            VScroll {
                flick: ctxSubFlick
                anchors {
                    top: parent.top; bottom: parent.bottom
                    right: parent.right; margins: 4
                }
            }
        }

        // Which pane the shared toolbar is talking about. Without it a split
        // window has two identical halves and every button in the toolbar is a
        // guess — the line is thin because it is a hint, not a border.
        Rectangle {
            visible: root.split
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 2
            color: pane.isActive ? root.cAccent : "transparent"
            z: 50
        }
    }

    // ── Small reusable pieces ───────────────────────────────────────────────

    component SideHeading: Text {
        leftPadding: 14
        bottomPadding: 4
        color: root.cDim
        font { family: root.uiFont; pixelSize: root.ui(10); bold: true  }
    }

    component SideRow: Rectangle {
        id: sideRow
        property string label: ""
        property string iconName: "folder"
        property string sub: ""
        property bool active: false
        property bool removable: false
        property bool dim: false
        property string trailing: ""       // a glyph shown on hover, "" for none
        property string trailingHint: ""
        // An encoded path this row accepts drops into, or "" for none. Recent,
        // Trash and About are not places anything can be dropped.
        property string dropTarget: ""
        property bool dropHover: false
        // 0 total means "unknown", NOT "empty" — an untriggered automount is
        // deliberately not measured, because statvfs would mount the disk just
        // to draw its meter.
        property real usedBytes: 0
        property real totalBytes: 0
        readonly property bool hasMeter: sideRow.totalBytes > 0
        readonly property real fillRatio:
            sideRow.hasMeter ? Math.min(1, sideRow.usedBytes / sideRow.totalBytes) : 0
        signal activated()
        signal removed()
        signal trailingClicked()
        signal contextRequested(real gx, real gy)

        width: sideScroll.width
        height: sideRow.hasMeter ? 38 : 28
        color: sideRow.dropHover ? root.wash(0.40)
             : sideRow.active ? root.wash(0.18)
             : (sideMa.containsMouse ? root.wash(0.08) : "transparent")

        Image {
            id: sideIcon
            anchors { left: parent.left; leftMargin: 14
                      top: parent.top; topMargin: sideRow.hasMeter ? 6 : 6 }
            anchors.verticalCenter: sideRow.hasMeter ? undefined : parent.verticalCenter
            width: 16; height: 16
            sourceSize: Qt.size(16, 16)
            source: Quickshell.iconPath(sideRow.iconName, true)
            opacity: sideRow.dim ? 0.45 : 1.0
        }
        Text {
            id: sideLabel
            anchors {
                left: sideIcon.right; leftMargin: 8
                right: sideRow.hasMeter ? meterPct.left : parent.right
                rightMargin: 8
                verticalCenter: sideIcon.verticalCenter
            }
            text: sideRow.label
            elide: Text.ElideRight
            color: sideRow.active ? root.cAccent : (sideRow.dim ? root.cDim : root.cText)
            font { family: root.uiFont; pixelSize: root.ui(12) }
        }

        // The fill meter, the way Dolphin shows one. It turns amber past 90%
        // because that is the point at which the number stops being trivia and
        // starts being the reason a copy is about to fail.
        Rectangle {
            id: meterTrack
            visible: sideRow.hasMeter
            anchors {
                left: sideIcon.left
                right: parent.right; rightMargin: 12
                top: sideIcon.bottom; topMargin: 5
            }
            height: 4
            radius: 2
            color: root.wash(0.14)

            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: Math.max(2, parent.width * sideRow.fillRatio)
                radius: 2
                color: sideRow.fillRatio >= 0.9 ? root.cWarn : root.cAccent
            }
        }
        // Sits on the LABEL line, left of the trailing glyph. Anchoring it to
        // the meter put it exactly where the eject button is, and the two
        // overlapped whenever a mounted drive was hovered.
        Text {
            id: meterPct
            visible: sideRow.hasMeter
            anchors {
                right: parent.right
                rightMargin: sideRow.trailing !== "" && sideMa.containsMouse ? 26 : 10
                verticalCenter: sideIcon.verticalCenter
            }
            text: Math.round(sideRow.fillRatio * 100) + "%"
            color: sideRow.fillRatio >= 0.9 ? root.cWarn : root.cDim
            font { family: root.uiFont; pixelSize: root.ui(9) }
        }

        // Declared BEFORE the two little buttons below it, and that order is
        // the whole contract: Qt Quick delivers a press to the LAST matching
        // child first, so a row-wide MouseArea written last sits on top of
        // everything and swallows every click meant for a button.
        MouseArea {
            id: sideMa
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    // Mapped to the MENU'S parent, not the window: the menu
                    // is a sibling of the content pane, whose origin is the
                    // sidebar's width to the right and the toolbar's height
                    // down. Mapping to the window put it that far off.
                    const p = sideRow.mapToItem(diskCtx.parent, mouse.x, mouse.y)
                    sideRow.contextRequested(p.x, p.y)
                } else {
                    sideRow.activated()
                }
            }
        }

        // Unpin, for a place the user added themselves.
        Text {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: "×"
            color: unpinMa.containsMouse ? root.cAccent : root.cDim
            font { family: root.uiFont; pixelSize: root.ui(12) }
            visible: sideRow.removable && sideMa.containsMouse
            MouseArea {
                id: unpinMa
                anchors { fill: parent; margins: -4 }
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sideRow.removed()
            }
        }

        // Mount / eject.
        Text {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: sideRow.trailing
            color: trailMa.containsMouse ? root.cAccent : root.cDim
            font { family: root.uiFont; pixelSize: root.ui(12) }
            visible: sideRow.trailing !== "" && sideMa.containsMouse
            MouseArea {
                id: trailMa
                anchors { fill: parent; margins: -5 }
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sideRow.trailingClicked()
            }
        }

        DropArea {
            anchors.fill: parent
            enabled: sideRow.dropTarget !== ""
            onEntered: (drag) => {
                sideRow.dropHover = root.willAcceptDrop(sideRow.dropTarget, drag)
                if (!sideRow.dropHover) drag.accepted = false
            }
            onExited: sideRow.dropHover = false
            onDropped: (drop) => {
                sideRow.dropHover = false
                root.handleDrop(sideRow.dropTarget, drop)
            }
        }
    }

    // A scrollbar you can actually grab. Flickable scrolls fine with a wheel,
    // but a folder with two thousand files needs a handle you can throw — and
    // it is the only thing that says how far down the list you are.
    //
    // Hand-rolled like every other control here: QtQuick.Controls has one, and
    // importing Controls for a single widget brings a style that matches
    // nothing else in this window.
    component VScroll: Item {
        id: vs
        required property Flickable flick

        width: 10
        // Hidden when everything fits, because a full-length handle that
        // cannot move is furniture.
        visible: vs.flick.visible && vs.flick.contentHeight > vs.flick.height + 1

        readonly property real handleH:
            Math.max(30, vs.height * Math.min(1, vs.flick.height
                                                 / Math.max(1, vs.flick.contentHeight)))
        readonly property real span: Math.max(0, vs.height - vs.handleH)
        readonly property real maxY: Math.max(0, vs.flick.contentHeight - vs.flick.height)

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: root.wash(0.07)
        }

        // Paging: a click on the track moves a screenful TOWARD the click,
        // never to it. Under the handle, so a click that lands on the handle
        // starts a drag instead.
        MouseArea {
            anchors.fill: parent
            onClicked: (m) => {
                const page = vs.flick.height * 0.9
                vs.flick.contentY = Math.max(0, Math.min(vs.maxY,
                    vs.flick.contentY + (m.y < handle.y ? -page : page)))
            }
        }

        Rectangle {
            id: handle
            x: 1
            width: parent.width - 2
            height: vs.handleH
            radius: width / 2
            // The position stays a BINDING on contentY: dragging moves the
            // view and the handle follows from that, so a wheel scroll and a
            // drag can never disagree about where the handle belongs.
            y: vs.maxY <= 0 ? 0
                            : Math.max(0, Math.min(vs.span, vs.flick.contentY / vs.maxY * vs.span))
            color: dragMa.pressed ? root.cAccent
                 : (dragMa.containsMouse ? root.wash(0.55) : root.wash(0.32))

            MouseArea {
                id: dragMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                property real grabY: 0
                onPressed: (m) => { dragMa.grabY = m.y }
                onPositionChanged: (m) => {
                    if (!dragMa.pressed || vs.span <= 0) return
                    const ny = Math.max(0, Math.min(vs.span, handle.y + m.y - dragMa.grabY))
                    vs.flick.contentY = ny / vs.span * vs.maxY
                }
            }
        }
    }

    // Folders are DRAWN, not fetched from the icon theme, and that is the whole
    // point: it is the one icon that follows the accent, and Qt cannot re-tint
    // a theme icon in a running process. Proven on this box — with
    // QT_QPA_PLATFORMTHEME=kde the folder SVG carries class="ColorScheme-Accent"
    // and KIconLoader substitutes the colour ONCE; changing kdeglobals, changing
    // the application palette and re-requesting at a different size all render
    // the OLD colour, so every folder in the window stayed the previous theme's
    // pink until synfiles was restarted. Drawing it from the palette we already
    // watch makes a theme switch land on the next frame, with no icon theme,
    // no cache and nothing to invalidate.
    component FolderIcon: Item {
        id: fi
        property bool dim: false
        // {full, size} records from `peek`, at most a handful.
        property var previews: []
        opacity: fi.dim ? 0.4 : 1.0

        // Resolved sources, empties dropped. A folder whose four candidates
        // are all un-thumbnailed videos gets a plain folder rather than four
        // blank tiles — an empty frame is worse than no frame.
        readonly property var tiles: {
            if (!root.thumbs || !fi.previews || fi.previews.length === 0) return []
            // Four tiles need about 20 pixels each to be anything but mush.
            const max = fi.width >= 44 ? 4 : 1
            const out = []
            for (const p of fi.previews) {
                const src = root.peekSource(p)
                if (src !== "") out.push(src)
                if (out.length >= max) break
            }
            return out
        }

        Rectangle {
            // The tab, drawn darker rather than as a separate shape: at 16px
            // an outline is one grey pixel and the folder reads as a blob.
            anchors { left: parent.left; top: parent.top }
            width: Math.max(6, parent.width * 0.45)
            height: Math.max(4, parent.height * 0.26)
            radius: Math.max(1, parent.width * 0.06)
            color: Qt.darker(root.cFolder, 1.4)
        }
        Rectangle {
            id: fiBody
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: parent.height * 0.74
            radius: Math.max(1, parent.width * 0.08)
            color: root.cFolder
            clip: true

            readonly property real pad: Math.max(1, fi.width * 0.05)
            readonly property int cols: fi.tiles.length > 1 ? 2 : 1
            readonly property int rws: fi.tiles.length > 2 ? 2 : 1

            Repeater {
                model: fi.tiles
                delegate: Image {
                    id: tile
                    required property int index
                    required property string modelData

                    width: (fiBody.width - fiBody.pad * (fiBody.cols + 1)) / fiBody.cols
                    height: (fiBody.height - fiBody.pad * (fiBody.rws + 1)) / fiBody.rws
                    x: fiBody.pad + (tile.index % fiBody.cols) * (tile.width + fiBody.pad)
                    y: fiBody.pad + Math.floor(tile.index / fiBody.cols) * (tile.height + fiBody.pad)

                    source: tile.modelData
                    // Cropped, not fitted: a letterboxed tile is mostly folder
                    // colour and reads as an empty slot.
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(Math.max(8, tile.width * 2),
                                        Math.max(8, tile.height * 2))
                    asynchronous: true
                    cache: true
                    clip: true
                    // A file that turns out to be unreadable leaves the folder
                    // colour showing, which is the fallback anyway.
                    visible: tile.status === Image.Ready
                }
            }
        }
    }

    // The same handle, on its side. Compact view is the only thing here that
    // scrolls horizontally, and a view you can only pan with a wheel is a view
    // whose far end nobody finds.
    component HScroll: Item {
        id: hs
        required property Flickable flick

        height: 10
        visible: hs.flick.visible && hs.flick.contentWidth > hs.flick.width + 1

        readonly property real handleW:
            Math.max(30, hs.width * Math.min(1, hs.flick.width
                                                / Math.max(1, hs.flick.contentWidth)))
        readonly property real span: Math.max(0, hs.width - hs.handleW)
        readonly property real maxX: Math.max(0, hs.flick.contentWidth - hs.flick.width)

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: root.wash(0.07)
        }
        MouseArea {
            anchors.fill: parent
            onClicked: (m) => {
                const page = hs.flick.width * 0.9
                hs.flick.contentX = Math.max(0, Math.min(hs.maxX,
                    hs.flick.contentX + (m.x < hHandle.x ? -page : page)))
            }
        }
        Rectangle {
            id: hHandle
            y: 1
            height: parent.height - 2
            width: hs.handleW
            radius: height / 2
            x: hs.maxX <= 0 ? 0
                            : Math.max(0, Math.min(hs.span, hs.flick.contentX / hs.maxX * hs.span))
            color: hDragMa.pressed ? root.cAccent
                 : (hDragMa.containsMouse ? root.wash(0.55) : root.wash(0.32))

            MouseArea {
                id: hDragMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                property real grabX: 0
                onPressed: (m) => { hDragMa.grabX = m.x }
                onPositionChanged: (m) => {
                    if (!hDragMa.pressed || hs.span <= 0) return
                    const nx = Math.max(0, Math.min(hs.span, hHandle.x + m.x - hDragMa.grabX))
                    hs.flick.contentX = nx / hs.span * hs.maxX
                }
            }
        }
    }

    component ToolButton: Rectangle {
        id: tb
        property string glyph: ""
        property string hint: ""
        // The split-view icon, drawn rather than named. Two panes with the
        // active one filled is not a character in any font, and a theme icon
        // could not be it either: Qt resolves an icon's colour ONCE per
        // process, so a shipped SVG would keep the old accent through a theme
        // change and go invisible on a light one. The same reason FolderIcon
        // exists. Two rectangles and a gap, bound to the palette.
        property bool splitIcon: false
        // A word beside the glyph, for the buttons whose icon alone would be a
        // guess — "View" is one, an arrow is not.
        property string label: ""
        property bool active: false
        // Drawn faint and unclickable. A Back button that vanishes when there
        // is nowhere to go back to moves everything next to it.
        property bool dim: false
        signal activated()

        width: tb.label === "" ? 30 : tbLabel.x + tbLabel.implicitWidth + 10
        height: 28
        radius: 4
        color: tb.active ? root.wash(0.22)
             : (tbMa.containsMouse && !tb.dim ? root.wash(0.14) : "transparent")
        opacity: tb.dim ? 0.35 : 1.0

        Text {
            id: tbGlyph
            anchors.verticalCenter: parent.verticalCenter
            // 30, not tb.width — and they are the same number, because 30 is
            // exactly what tb.width evaluates to when the label is empty.
            //
            // Reading tb.width here closes a cycle: tb.width -> tbLabel.x ->
            // tbGlyph.x -> tb.width. It lay dormant for as long as every
            // ToolButton's label was a constant, since only one branch of each
            // ternary was ever taken. Giving the View button a label that
            // switches to "" on a narrow toolbar made both branches live and
            // Qt started reporting "Binding loop detected for property width",
            // leaving the button unsized. The constant breaks it for good.
            x: tb.label === "" ? (30 - implicitWidth) / 2 : 9
            text: tb.glyph
            visible: !tb.splitIcon
            color: tb.active ? root.cAccent : root.cText
            font { family: root.uiFont; pixelSize: root.ui(14) }
        }

        Item {
            visible: tb.splitIcon
            anchors.centerIn: parent
            width: root.ui(15)
            height: root.ui(12)

            id: splitGlyph
            readonly property color ink: tb.active ? root.cAccent : root.cText

            // A frame with a divider down the middle, and the half that is
            // "the active pane" filled in — the same thing the window shows,
            // at button size.
            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: (parent.width - 2) / 2
                color: tb.active ? splitGlyph.ink : "transparent"
                border { width: 1; color: splitGlyph.ink }
                radius: 1
            }
            Rectangle {
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: (parent.width - 2) / 2
                color: "transparent"
                border { width: 1; color: splitGlyph.ink }
                radius: 1
            }
        }
        Text {
            id: tbLabel
            anchors.verticalCenter: parent.verticalCenter
            x: tbGlyph.x + tbGlyph.implicitWidth + (tb.label === "" ? 0 : 5)
            text: tb.label === "" ? "" : tb.label + " ⌄"
            color: tb.active ? root.cAccent : root.cText
            font { family: root.uiFont; pixelSize: root.ui(11) }
        }
        // A hover label rather than a permanent one: five glyphs with words
        // under them is a toolbar nobody can scan.
        Rectangle {
            visible: tbMa.containsMouse && tb.hint !== ""
            anchors { top: parent.bottom; topMargin: 2; horizontalCenter: parent.horizontalCenter }
            width: hintText.implicitWidth + 12
            height: 18
            radius: 3
            color: root.cBg
            border { width: 1; color: root.wash(0.3) }
            z: 300
            Text {
                id: hintText
                anchors.centerIn: parent
                text: tb.hint
                color: root.cText
                font { family: root.uiFont; pixelSize: root.ui(10) }
            }
        }
        MouseArea {
            id: tbMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: !tb.dim
            cursorShape: Qt.PointingHandCursor
            onClicked: tb.activated()
        }
    }

    component ToggleChip: Rectangle {
        id: chip
        property string label: ""
        property bool on: false
        // 0 = size to the label, which is right for a chip whose text is a
        // fixed word ("Cancel"). Set it wherever the label is DATA — the undo
        // chip's is a filename, and an unbounded chip carrying a filename is
        // what blew the toolbar apart. See the note on the undo chip.
        property real maxWidth: 0
        signal toggled()

        width: chip.maxWidth > 0
               ? Math.min(chipText.implicitWidth + 20, chip.maxWidth)
               : chipText.implicitWidth + 20
        height: 26
        radius: 3
        color: chip.on ? root.wash(0.20)
                       : (chipMa.containsMouse ? root.wash(0.10) : "transparent")
        border { width: 1; color: chip.on ? root.cAccent : "transparent" }

        Text {
            id: chipText
            anchors.centerIn: parent
            // implicitWidth is the UNCONSTRAINED width, so it does not change
            // when this width is set — no loop with chip.width above.
            width: Math.min(implicitWidth, chip.width - 20)
            elide: Text.ElideRight
            text: chip.label
            color: chip.on ? root.cAccent : root.cDim
            font { family: root.uiFont; pixelSize: root.ui(11) }
        }
        MouseArea {
            id: chipMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.toggled()
        }
    }
}
