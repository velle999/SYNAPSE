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
    readonly property color cAccentRaw: themed("accent", 78, 201, 176, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)
    readonly property color cWarn: pick("#e0af68", "#5c3a00")
    // Folders take the theme accent, lightened or darkened only as far as it
    // takes to stay visible against the view background — a pale accent on a
    // pale theme would otherwise draw folder-shaped holes.
    readonly property color cFolder: readable(cAccentRaw, cBg, 2.0)

    function wash(a) { return Qt.rgba(cAccent.r, cAccent.g, cAccent.b, a) }

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

    // ── Tabs ────────────────────────────────────────────────────────────────
    //
    // A tab is {path, view, rows, sort, reverse, showHidden, selected}. Keeping
    // per-tab state in the model rather than in the visible pane is what makes
    // switching tabs instant and what stops a sort applied in one tab silently
    // reordering another.
    property var tabs: []
    property int current: 0
    property bool loading: false
    property string statusLine: ""

    readonly property var tab: tabs.length > 0 ? tabs[current] : null

    function newTab(pathEnc, view) {
        const t = {
            path: pathEnc || root.encodePath(root.homeDir),
            view: view || "dir",       // dir | recent | places-derived listing
            title: "",
            rows: [],
            sort: root.defaultSort,
            reverse: root.defaultReverse,
            showHidden: root.defaultHidden,
            filter: "",
            // Back/forward, per tab. Shared history across tabs would send Back
            // to a folder this tab was never in.
            hist: [{ path: pathEnc || root.encodePath(root.homeDir), view: view || "dir" }],
            hi: 0
        }
        const copy = root.tabs.slice()
        copy.push(t)
        root.tabs = copy
        root.current = copy.length - 1
        root.reload()
    }

    function closeTab(i) {
        if (root.tabs.length <= 1) return
        const copy = root.tabs.slice()
        copy.splice(i, 1)
        root.tabs = copy
        if (root.current >= copy.length) root.current = copy.length - 1
        root.reload()
    }

    // Mutating a property of an object inside an array does NOT re-evaluate
    // bindings on that array — QML only notices the array identity changing.
    // Every state change therefore rebuilds the outer array, which is why this
    // helper exists rather than `tabs[current].sort = x` at each call site.
    function setTab(fields) {
        if (!root.tab) return
        const copy = root.tabs.slice()
        const t = ({})
        for (const k in copy[root.current]) t[k] = copy[root.current][k]
        for (const k in fields) t[k] = fields[k]
        copy[root.current] = t
        root.tabs = copy
    }

    function navigate(pathEnc, view) {
        const v = view || "dir"
        const t = root.tab
        // Going somewhere new DISCARDS the forward entries, the way every
        // browser does: forward means "where I came back from", and keeping a
        // branch nobody can see would make the button lie.
        if (t) {
            const h = (t.hist || []).slice(0, (t.hi === undefined ? -1 : t.hi) + 1)
            const last = h[h.length - 1]
            if (!last || last.path !== pathEnc || last.view !== v)
                h.push({ path: pathEnc, view: v })
            root.setTab({ hist: h, hi: h.length - 1 })
        }
        root.go(pathEnc, v)
    }

    // The move itself, with no history written — what Back and Forward use.
    function go(pathEnc, view) {
        root.setTab({ path: pathEnc, view: view || "dir", filter: "", rows: [] })
        root.reload()
    }

    readonly property bool canGoBack: root.tab && root.tab.hi > 0
    readonly property bool canGoForward:
        root.tab && root.tab.hist && root.tab.hi < root.tab.hist.length - 1

    function goBack() {
        if (!root.canGoBack) return
        const i = root.tab.hi - 1
        const e = root.tab.hist[i]
        root.setTab({ hi: i })
        root.go(e.path, e.view)
    }

    function goForward() {
        if (!root.canGoForward) return
        const i = root.tab.hi + 1
        const e = root.tab.hist[i]
        root.setTab({ hi: i })
        root.go(e.path, e.view)
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

    Process {
        id: listProc
        property string kind: ""
        stdout: StdioCollector {
            onStreamFinished: {
                const table = root.parseRecords(this.text)
                const t = root.tab
                if (!t) return

                let rows = []
                if (listProc.kind === "find") {
                    listProc.kind = ""
                    // A search result is not in the current folder, so it
                    // carries its own full path rather than being joined onto
                    // the tab's. `dir` is relative to where the search started.
                    rows = table.map(r => ({
                        name: r.name,
                        full: r.dir ? root.joinEnc(root.joinEnc(root.searchRoot, r.dir), r.name)
                                    : root.joinEnc(root.searchRoot, r.name),
                        where: r.dir,
                        type: r.type, size: parseInt(r.size || "0"),
                        mtime: parseInt(r.mtime || "0"), mime: r.mime,
                        link: r.link, target: r.target, mode: r.mode,
                        missing: false
                    }))
                    root.setTab({ rows: rows })
                    root.loading = false
                    root.statusLine = ""
                    return
                }
                if (t.view === "about") {
                    root.aboutRows = table
                    root.loading = false
                    root.statusLine = ""
                    return
                } else if (t.view === "trash") {
                    // `trashName` is the handle `trash restore` takes, and it
                    // is NOT derivable from the path: two files called
                    // notes.txt become notes.txt and notes.txt.2 in the trash.
                    rows = table.map(r => ({
                        name: root.baseEnc(r.path), full: r.path,
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

                root.setTab({ rows: rows })
                root.loading = false
                root.statusLine = ""
                // What is inside each subfolder, for the icons. After the
                // listing rather than beside it: the rows are what the window
                // is waiting for, and this only decorates them.
                root.refreshPeek()
            }
        }
        stderr: StdioCollector {
            onStreamFinished: {
                if (this.text) root.statusLine = this.text.split("\n")[0]
            }
        }
    }

    function reload() {
        const t = root.tab
        if (!t) return
        root.loading = true
        root.statusLine = ""

        if (t.view === "about") {
            listProc.command = [root.bin, "--rec", "about"]
        } else if (t.view === "trash") {
            listProc.command = [root.bin, "--rec", "trash", "list"]
        } else if (t.view === "recent") {
            listProc.command = [root.bin, "--rec", "recent", "--limit=300"]
        } else {
            // The path goes to the binary DECODED — argv carries raw bytes and
            // needs no escaping. The encoded form exists for the record
            // stream, not for the process boundary.
            const args = [root.bin, "--rec", "list", "--sort=" + t.sort]
            if (t.reverse) args.push("--reverse")
            if (t.showHidden) args.push("--all")
            args.push(root.disp(t.path))
            listProc.command = args
        }
        listProc.running = true
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

    // Encoded names, because that is the identity. Storing display names here
    // would make two files that differ only in an escaped byte the same
    // selection.
    property var selection: []
    property string anchorName: ""   // where a Shift range started

    function isSelected(name) { return root.selection.indexOf(name) >= 0 }

    function selectOnly(name) {
        root.selection = [name]
        root.anchorName = name
    }

    function toggleSelect(name) {
        const i = root.selection.indexOf(name)
        const copy = root.selection.slice()
        if (i >= 0) copy.splice(i, 1)
        else copy.push(name)
        root.selection = copy
        root.anchorName = name
    }

    // A Shift range runs over the rows AS DISPLAYED, so it follows the current
    // sort and filter rather than some underlying order the user cannot see.
    function selectRange(name) {
        const rows = root.shownRows
        let a = -1, b = -1
        for (let i = 0; i < rows.length; i++) {
            if (rows[i].name === root.anchorName) a = i
            if (rows[i].name === name) b = i
        }
        if (a < 0) { root.selectOnly(name); return }
        if (b < 0) return
        const lo = Math.min(a, b), hi = Math.max(a, b)
        const copy = []
        for (let i = lo; i <= hi; i++) copy.push(rows[i].name)
        root.selection = copy
    }

    function selectAll() {
        root.selection = root.shownRows.map(r => r.name)
    }

    function clearSelection() {
        root.selection = []
        root.anchorName = ""
    }

    function selectedRows() {
        return root.shownRows.filter(r => root.isSelected(r.name))
    }

    // Decoded paths for the process boundary — argv carries raw bytes.
    function selectedPaths() {
        return root.selectedRows().map(r => root.disp(r.full))
    }

    // {op: "copy"|"cut", paths: [encoded...]}. Cut is not a move yet — nothing
    // leaves its directory until Paste, which is what makes Ctrl+X reversible
    // by simply not pasting.
    property var clip: ({ op: "", paths: [] })

    Process {
        id: opProc
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
            root.reload()
            root.refreshUndo()
            placesProc.running = true
            // Mounting changes what the Devices list should show, and so does
            // trashing something onto a volume. Cheaper to re-read both than
            // to work out which operations could have moved them.
            volProc.running = true
        }
    }
    property bool busy: false

    function runOp(args, note) {
        if (root.busy) return
        root.busy = true
        root.statusLine = note
        // Paths cross the process boundary DECODED: argv carries raw bytes and
        // needs no escaping. The encoding exists for the record stream, which
        // is a text format, not for exec().
        opProc.command = [root.bin].concat(args)
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
        if (n === 1) return root.disp(root.selectedRows()[0].name)
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

    function paste() {
        if (!root.tab || root.tab.view !== "dir") return
        if (!root.clip.paths || root.clip.paths.length === 0) return

        const args = [root.clip.op === "cut" ? "move" : "copy"]
        // rename rather than error: a paste into the folder something was
        // copied from is the common case, and refusing it there would be
        // pedantic. Anywhere else a collision is still a real question, but
        // the GUI has no way to ask one yet, so the non-destructive answer is
        // the only defensible default.
        args.push("--conflict=rename")
        for (const p of root.clip.paths)
            args.push(root.disp(p))
        args.push(root.disp(root.tab.path))

        root.runOp(args, root.clip.op === "cut" ? "moving…" : "copying…")
        if (root.clip.op === "cut")
            root.clip = ({ op: "", paths: [] })
    }

    // ── Rename ──────────────────────────────────────────────────────────────
    property string renaming: ""     // encoded name being renamed, "" if none

    function commitRename(newName) {
        const row = root.selectedRow()
        root.renaming = ""
        if (!row || !newName || newName === root.disp(row.name)) return
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

    // ── Folder previews ─────────────────────────────────────────────────────
    //
    // What a folder has inside it, drawn on its icon the way Dolphin does —
    // twenty subfolders of holiday photos are twenty identical shapes until
    // something tells them apart.
    //
    // ONE call per listing, keyed by the folder's encoded path. Asking per
    // folder as rows scroll into view would fork a process per row; see
    // peek.c, which answers for every subdirectory of one directory in a
    // single pass.
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
                root.folderPeek = map
            }
        }
    }

    function refreshPeek() {
        // Previews off means previews off, everywhere: a folder that keeps its
        // pictures after the toggle is turned off looks like the toggle broke.
        if (!root.thumbs || !root.tab || root.tab.view !== "dir") {
            root.folderPeek = ({})
            return
        }
        peekProc.running = false
        peekProc.command = [root.bin, "--rec", "peek", root.disp(root.tab.path)]
        peekProc.running = true
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

    Process {
        id: infoProc
        stdout: StdioCollector {
            onStreamFinished: root.propRows = root.parseRecords(this.text)
        }
    }

    function openProperties() {
        const rows = root.selectedRows()
        if (rows.length === 0) return
        root.showProps = true
        if (rows.length === 1) {
            root.propRows = []
            infoProc.command = [root.bin, "--rec", "info", root.disp(rows[0].full)]
            infoProc.running = true
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

    // Shared by the list and the grid: the two differ in arrangement, not
    // in what a key means.
    function handleKey(event) {
    const row = root.selectedRow()
    if (event.key === Qt.Key_Delete) {
        if (root.tab.view === "dir") root.trashSelection()
        event.accepted = true
    } else if (event.key === Qt.Key_F2) {
        // Rename is the one operation that cannot be done to
        // six things at once, so it needs exactly one.
        if (row && root.tab.view === "dir") root.renaming = row.name
        event.accepted = true
    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
        if (event.modifiers & Qt.AltModifier) root.openProperties()
        else if (row) root.activate(row)
        event.accepted = true
    } else if (event.key === Qt.Key_Escape) {
        if (root.showProps) root.showProps = false
        else if (root.searching) root.endSearch()
        else root.clearSelection()
        event.accepted = true
    } else if (event.key === Qt.Key_Backspace) {
        if (root.tab.view === "dir")
            root.navigate(root.parentEnc(root.tab.path), "dir")
        event.accepted = true
    } else if (event.modifiers & Qt.ControlModifier) {
        if (event.key === Qt.Key_C)      { root.copySelection(false); event.accepted = true }
        else if (event.key === Qt.Key_X) { root.copySelection(true);  event.accepted = true }
        else if (event.key === Qt.Key_V) { root.paste();              event.accepted = true }
        else if (event.key === Qt.Key_A) { root.selectAll();          event.accepted = true }
        else if (event.key === Qt.Key_Z) { root.doUndo();             event.accepted = true }
        else if (event.key === Qt.Key_F) { root.beginSearch();        event.accepted = true }
        else if (event.key === Qt.Key_L) { root.beginEditPath();      event.accepted = true }
        else if (event.key === Qt.Key_T) { root.newTab(root.tab.path, "dir"); event.accepted = true }
        else if (event.key === Qt.Key_W) { root.closeTab(root.current); event.accepted = true }
        else if (event.key === Qt.Key_N) { root.creating = true; event.accepted = true }
    }
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

    function beginDrag(row, label) {
        if (!root.tab || root.tab.view !== "dir") return
        // Whatever is selected, or the row under the cursor if it is not part
        // of the selection — the same rule the context menu follows.
        if (!root.isSelected(row.name)) root.selectOnly(row.name)
        root.dragPaths = root.selectedPaths()
        root.dragLabel = label

        const rows = root.selectedRows()
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
                    }
                }
                // Only after applying, or the act of applying would write every
                // default straight back out as if the user had chosen it.
                root.settingsLoaded = true

                // The first tab was built before the settings arrived, so it
                // gets them applied and re-read. Without this the remembered
                // sort only took effect on the SECOND tab, which reads as
                // "it forgot".
                if (root.tab) {
                    root.setTab({ sort: root.defaultSort,
                                  reverse: root.defaultReverse,
                                  showHidden: root.defaultHidden })
                    root.reload()
                }
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

    // These are single clicks, so they save immediately.
    onThumbsChanged: {
        root.saveSetting("previews", root.thumbs ? 1 : 0)
        root.refreshPeek()
    }
    onShowTreeChanged: root.saveSetting("tree", root.showTree ? 1 : 0)
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
        case "tree":
            root.showTree = !root.showTree
            if (root.showTree && root.treeChildren[root.encodePath(root.homeDir)] === undefined)
                root.treeToggle(root.encodePath(root.homeDir))
            break
        }
    }

    // Split view is NOT here yet, deliberately. Two panes means factoring the
    // 247-line file pane into a component and moving `selection` out of root
    // and into each tab — selection is per-pane or it is nonsense. That is a
    // structural change and it is its own commit; a Split button that toggled
    // a flag and drew nothing would be worse than no button at all.

    // ── Search ──────────────────────────────────────────────────────────────
    //
    // Scoped to the folder you are standing in, because "search everywhere" is
    // a different and much slower question, and the answer to it is almost
    // never what somebody pressing Ctrl+F in a folder wanted.
    property bool searching: false
    property string searchTerm: ""
    property bool searchContent: false
    property string searchRoot: ""

    function beginSearch() {
        if (!root.tab || root.tab.view !== "dir") return
        root.searchRoot = root.tab.path
        root.searching = true
        root.setTab({ rows: [], filter: "" })
        filterInput.forceActiveFocus()
    }

    function endSearch() {
        root.searching = false
        root.searchTerm = ""
        root.reload()
    }

    function runSearch() {
        if (!root.searchTerm) { root.setTab({ rows: [] }); return }
        root.loading = true
        root.statusLine = "searching " + root.disp(root.searchRoot) + "…"
        const args = [root.bin, "--rec", "find", root.disp(root.searchRoot),
                      "--limit=2000"]
        if (root.searchContent) args.push("--content=" + root.searchTerm)
        else                    args.push("--name=" + root.searchTerm)
        if (root.tab.showHidden) args.push("--all")
        listProc.kind = "find"
        listProc.command = args
        listProc.running = true
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

    function openTerminalHere() {
        const dir = root.tab && root.tab.view === "dir"
                    ? root.disp(root.tab.path) : root.homeDir
        termProc.command = ["sh", "-c",
            'cd "$1" || exit 1; ' +
            'for t in kitty foot alacritty konsole xterm; do ' +
            '  command -v "$t" >/dev/null 2>&1 && exec "$t"; done; ' +
            'echo "no terminal emulator found" >&2; exit 127',
            "sh", dir]
        termProc.running = true
        root.statusLine = "opened a terminal in " + dir
    }
    Process { id: termProc }

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
        onExited: root.reload()
    }

    // ── Disk context menu ───────────────────────────────────────────────────
    // Its own menu rather than a branch of the file one: nothing a disk offers
    // (mount, eject, pin the mount point) applies to a file, and nothing the
    // file menu offers applies to a disk.
    property var diskMenu: null      // the volume record, or null when closed
    property real diskMenuX: 0
    property real diskMenuY: 0

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
    Process { id: openProc }
    function openFile(pathEnc) {
        openProc.command = ["xdg-open", root.disp(pathEnc)]
        openProc.running = true
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

    function activate(row) {
        if (row.type === "dir") root.navigate(row.full, "dir")
        else if (!row.missing)  root.openFile(row.full)
    }

    readonly property var shownRows: {
        const t = root.tab
        if (!t) return []
        if (!t.filter) return t.rows
        const f = t.filter.toLowerCase()
        return t.rows.filter(r => root.disp(r.name).toLowerCase().includes(f))
    }

    Component.onCompleted: {
        const start = Quickshell.env("SYNFILES_DIR") || root.homeDir
        root.newTab(root.encodePath(start), "dir")
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
                    label: "View"
                    hint: ""
                    active: root.viewMenuOpen
                    onActivated: root.viewMenuOpen = !root.viewMenuOpen
                }
            }

            Rectangle {
                id: addressBar
                anchors {
                    left: navGroup.right; leftMargin: 6
                    right: toolActions.left; rightMargin: 8
                    verticalCenter: parent.verticalCenter
                }
                height: 28
                radius: 4
                color: root.cBg
                border {
                    width: 1
                    color: root.editingPath ? root.cAccent : root.wash(0.18)
                }

                // Breadcrumbs, until somebody wants to type.
                Row {
                    id: crumbs
                    anchors {
                        left: parent.left; leftMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
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
                                font.pixelSize: 12
                                visible: crumb.modelData.path !== "/"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: crumb.modelData.label
                                color: crumbMa.containsMouse ? root.cAccent : root.cText
                                font.pixelSize: 12
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
                    font.pixelSize: 12
                }

                TextInput {
                    id: pathInput
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: TextInput.AlignVCenter
                    visible: root.editingPath
                    color: root.cText
                    font.pixelSize: 12
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
            width: 220
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
                anchors.rightMargin: 12
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
                                font.pixelSize: 10
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
                                font.pixelSize: 11
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
                    SideHeading {
                        text: "Network"
                        visible: root.networkVolumes.length > 0
                    }

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

        // ── Main pane ───────────────────────────────────────────────────────
        Item {
            anchors {
                top: toolBar.bottom; left: sidebar.right
                right: parent.right; bottom: parent.bottom
            }


            // Tab strip
            Rectangle {
                id: tabStrip
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 34
                color: root.cPanel

                Row {
                    anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                    spacing: 2

                    Repeater {
                        model: root.tabs
                        delegate: Rectangle {
                            id: tabBtn
                            required property var modelData
                            required property int index
                            readonly property bool active: tabBtn.index === root.current
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
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: tabMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.current = tabBtn.index; root.reload() }
                            }
                            Text {
                                id: closeBtn
                                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                                text: "×"
                                color: closeMa.containsMouse ? root.cAccent : root.cDim
                                font.pixelSize: 14
                                visible: root.tabs.length > 1
                                MouseArea {
                                    id: closeMa
                                    anchors { fill: parent; margins: -4 }
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.closeTab(tabBtn.index)
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
                            font.pixelSize: 15
                        }
                        MouseArea {
                            id: addMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.newTab(root.tab ? root.tab.path
                                                            : root.encodePath(root.homeDir), "dir")
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
                visible: root.tab && root.tab.view === "trash"

                Text {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: "Deleted files. Restoring puts one back where it came from."
                    color: root.cDim
                    font.pixelSize: 12
                }
                ToggleChip {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    label: "Empty Trash…"
                    on: false
                    onToggled: root.confirmEmpty = true
                }
            }

            Item {
                id: pathBar
                anchors { top: tabStrip.bottom; left: parent.left; right: parent.right }
                anchors.margins: 8
                height: 30
                visible: root.tab && root.tab.view === "dir"

                Row {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 6

                    ToggleChip {
                        // Named, not just "Undo" — a recovery control that does
                        // not say what it reverses is one nobody dares press.
                        label: root.undoLabel !== "" ? "↶ " + root.undoLabel : "↶ Nothing to undo"
                        on: false
                        visible: root.undoLabel !== ""
                        onToggled: root.doUndo()
                    }
                    ToggleChip {
                        label: "New folder"
                        on: false
                        visible: root.tab && root.tab.view === "dir"
                        onToggled: root.creating = true
                    }
                    ToggleChip {
                        label: root.tab && root.isPinned(root.tab.path) ? "Pinned ✓" : "Pin"
                        on: root.tab ? root.isPinned(root.tab.path) : false
                        visible: root.tab && root.tab.view === "dir"
                        onToggled: {
                            if (root.isPinned(root.tab.path)) root.unpin(root.tab.path)
                            else root.pin(root.tab.path)
                        }
                    }
                }
            }

            // Filter box
            Rectangle {
                id: filterBar
                anchors { top: pathBar.visible ? pathBar.bottom : tabStrip.bottom
                          left: parent.left; right: parent.right }
                anchors.margins: 8
                height: 28
                visible: root.tab && root.tab.view !== "about"
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
                    font.pixelSize: 12
                    clip: true
                    onTextChanged: {
                        if (root.searching) root.searchTerm = text
                        else root.setTab({ filter: text })
                    }
                    onAccepted: if (root.searching) root.runSearch()
                    Keys.onEscapePressed: {
                        text = ""
                        if (root.searching) root.endSearch()
                        else root.setTab({ filter: "" })
                    }
                    onVisibleChanged: if (visible) text = ""
                }
                Text {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    text: root.searching
                          ? ("search " + root.disp(root.baseEnc(root.searchRoot))
                             + " and below — press Enter")
                          : "filter these items…   (Ctrl+F to search)"
                    color: root.cDim
                    font.pixelSize: 12
                    visible: filterInput.text === ""
                }

                Row {
                    id: searchChips
                    anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                    spacing: 4
                    visible: root.searching

                    ToggleChip {
                        label: root.searchContent ? "In contents" : "By name"
                        on: root.searchContent
                        onToggled: {
                            root.searchContent = !root.searchContent
                            if (root.searchTerm) root.runSearch()
                        }
                    }
                    ToggleChip {
                        label: "Done"
                        on: false
                        onToggled: { filterInput.text = ""; root.endSearch() }
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
                    right: parent.right; bottom: statusBar.top
                }
                anchors.margins: 18
                visible: root.tab && root.tab.view === "about"
                contentHeight: aboutCol.implicitHeight
                clip: true

                Column {
                    id: aboutCol
                    width: parent.width
                    spacing: 6

                    Text {
                        text: "SYNAPSE Files"
                        color: root.cAccent
                        font { pixelSize: 20; bold: true }
                    }
                    Text {
                        width: aboutCol.width
                        text: "A file browser for SynapseOS. Tabs, pinned places shared with "
                            + "Dolphin, recent files, volumes and network shares."
                        color: root.cDim
                        font.pixelSize: 12
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
                                font { pixelSize: 12; bold: true }
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
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: aboutRow.modelData.detail
                                    color: root.cDim
                                    font.pixelSize: 11
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
                                    font.pixelSize: 11
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
                    right: parent.right; bottom: statusBar.top
                }
                enabled: root.tab && root.tab.view === "dir"
                property bool hovering: false
                onEntered: (drag) => {
                    paneDrop.hovering = root.willAcceptDrop(root.tab.path, drag)
                    if (!paneDrop.hovering) drag.accepted = false
                }
                onExited: paneDrop.hovering = false
                onDropped: (drop) => {
                    paneDrop.hovering = false
                    root.handleDrop(root.tab.path, drop)
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
                visible: root.tab && root.tab.view !== "about" && !root.gridView

                Text {
                    anchors { left: parent.left; leftMargin: 40 }
                    text: "Name"; color: root.cDim; font.pixelSize: 10
                }
                Text {
                    anchors { right: parent.right; rightMargin: 190 }
                    text: "Size"; color: root.cDim; font.pixelSize: 10
                }
                Text {
                    anchors { right: parent.right; rightMargin: 20 }
                    text: "Modified"; color: root.cDim; font.pixelSize: 10
                }
            }

            ListView {
                id: fileList
                anchors {
                    top: heads.bottom; left: parent.left
                    right: parent.right; bottom: statusBar.top
                }
                anchors.margins: 8
                anchors.topMargin: 2
                // Room for the scrollbar, so a row never runs under the handle.
                anchors.rightMargin: 22
                clip: true
                visible: root.tab && root.tab.view !== "about" && !root.gridView
                model: root.shownRows
                spacing: 1
                currentIndex: -1
                focus: true

                // Shortcuts a file manager is expected to have. Delete goes to
                // the TRASH — the permanent one is a separate command behind a
                // separate flag, and no key reaches it.
                Keys.onPressed: (event) => root.handleKey(event)

                delegate: Rectangle {
                    id: fileRow
                    required property var modelData
                    readonly property bool isSelected: root.isSelected(fileRow.modelData.name)
                    readonly property bool isRenaming: fileRow.modelData.name === root.renaming
                    property bool dropHover: false
                    width: ListView.view.width
                    height: Math.max(30, root.iconSize + 10)
                    radius: 3
                    color: fileRow.dropHover ? root.wash(0.40)
                         : fileRow.isSelected ? root.wash(0.22)
                         : (rowMa.containsMouse ? root.wash(0.10) : "transparent")
                    border { width: fileRow.dropHover ? 1 : 0; color: root.cAccent }

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
                        previews: root.folderPeek[fileRow.modelData.full] || []
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
                        text: root.disp(fileRow.modelData.name)
                              + (fileRow.modelData.where
                                 ? "      " + root.disp(fileRow.modelData.where) + "/" : "")
                              + (fileRow.modelData.link === "1" && fileRow.modelData.target
                                 ? "  → " + root.disp(fileRow.modelData.target) : "")
                        elide: Text.ElideRight
                        color: fileRow.modelData.missing ? root.cDim
                             : (fileRow.modelData.type === "dir" ? root.cAccent : root.cText)
                        font.pixelSize: 12
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

                        TextInput {
                            id: renameInput
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter
                            color: root.cText
                            font.pixelSize: 12
                            clip: true
                            onVisibleChanged: {
                                if (visible) {
                                    text = root.disp(fileRow.modelData.name)
                                    forceActiveFocus()
                                    selectAll()
                                }
                            }
                            onAccepted: root.commitRename(text)
                            Keys.onEscapePressed: root.renaming = ""
                        }
                    }

                    Text {
                        anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                        text: fileRow.modelData.deleted || ""
                        color: root.cDim
                        font.pixelSize: 11
                        visible: root.tab && root.tab.view === "trash"
                    }

                    // Restore is offered only where it means something, and it
                    // passes the trashName back verbatim — the handle from the
                    // listing, not something re-derived from the path, because
                    // a second notes.txt is stored as notes.txt.2.
                    Rectangle {
                        anchors { right: parent.right; rightMargin: 150; verticalCenter: parent.verticalCenter }
                        width: 66; height: 22; radius: 3
                        visible: root.tab && root.tab.view === "trash"
                                 && !fileRow.modelData.missing
                        color: restoreMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                        border { width: 1; color: root.cAccent }
                        Text {
                            anchors.centerIn: parent
                            text: "Restore"
                            color: root.cAccent
                            font.pixelSize: 10
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
                        font.pixelSize: 11
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                        text: root.fmtTime(fileRow.modelData.mtime)
                        color: root.cDim
                        font.pixelSize: 11
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
                                if (!root.isSelected(name)) root.selectOnly(name)
                                ctxMenu.row = fileRow.modelData
                                const gp = rowMa.mapToItem(ctxMenu.parent, mouse.x, mouse.y)
                                ctxMenu.rawX = gp.x
                                ctxMenu.rawY = gp.y
                                ctxMenu.open = true
                                root.loadActions()
                                return
                            }

                            if (mouse.modifiers & Qt.ControlModifier)    root.toggleSelect(name)
                            else if (mouse.modifiers & Qt.ShiftModifier) root.selectRange(name)
                            else                                         root.selectOnly(name)
                        }
                        onPressed: (mouse) => {
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
                            root.beginDrag(fileRow.modelData,
                                           root.selection.length > 1
                                           ? root.selection.length + " items"
                                           : root.disp(fileRow.modelData.name))
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
                    right: parent.right; bottom: statusBar.top
                }
                anchors.margins: 8
                anchors.topMargin: 2
                // Room for the scrollbar, so a row never runs under the handle.
                anchors.rightMargin: 22
                // ...and room UNDER it in compact, where the bar is the
                // horizontal one and would otherwise sit on the status line.
                anchors.bottomMargin: root.compactView ? 22 : 8
                clip: true
                visible: root.tab && root.tab.view !== "about" && root.gridView
                model: root.shownRows
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
                focus: fileGrid.visible

                Keys.onPressed: (event) => root.handleKey(event)

                delegate: Rectangle {
                    id: gridCell
                    required property var modelData
                    readonly property bool isSelected: root.isSelected(gridCell.modelData.name)
                    property bool dropHover: false
                    width: fileGrid.cellWidth - 6
                    height: fileGrid.cellHeight - 6
                    radius: 4
                    color: gridCell.dropHover ? root.wash(0.40)
                         : gridCell.isSelected ? root.wash(0.22)
                         : (cellMa.containsMouse ? root.wash(0.10) : "transparent")
                    border { width: gridCell.dropHover ? 1 : 0; color: root.cAccent }

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
                        previews: root.folderPeek[gridCell.modelData.full] || []
                    }

                    Text {
                        anchors.top: root.compactView ? undefined : cellIconBox.bottom
                        anchors.topMargin: 4
                        anchors.left: root.compactView ? cellIconBox.right : parent.left
                        anchors.leftMargin: root.compactView ? 8 : 4
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: root.compactView ? parent.verticalCenter
                                                                 : undefined
                        text: root.disp(gridCell.modelData.name)
                        color: gridCell.modelData.missing ? root.cDim
                             : (gridCell.modelData.type === "dir" ? root.cAccent : root.cText)
                        font.pixelSize: root.compactView ? 11 : 10
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
                            root.beginDrag(gridCell.modelData,
                                           root.selection.length > 1
                                           ? root.selection.length + " items"
                                           : root.disp(gridCell.modelData.name))
                        }
                        onClicked: (mouse) => {
                            fileGrid.forceActiveFocus()
                            const name = gridCell.modelData.name
                            if (mouse.button === Qt.RightButton) {
                                if (!root.isSelected(name)) root.selectOnly(name)
                                ctxMenu.row = gridCell.modelData
                                const p = cellMa.mapToItem(ctxMenu.parent, mouse.x, mouse.y)
                                ctxMenu.rawX = p.x
                                ctxMenu.rawY = p.y
                                ctxMenu.open = true
                                root.loadActions()
                                return
                            }
                            if (mouse.modifiers & Qt.ControlModifier)    root.toggleSelect(name)
                            else if (mouse.modifiers & Qt.ShiftModifier) root.selectRange(name)
                            else                                         root.selectOnly(name)
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
                visible: !root.loading && root.shownRows.length === 0
                         && root.tab && root.tab.view !== "about"

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        if (!root.tab) return ""
                        if (root.searching)
                            return root.searchTerm === ""
                                   ? "Type to search this folder and everything below it."
                                   : "Nothing matched."
                        if (root.tab.filter) return "Nothing matches that filter."
                        if (root.tab.view === "recent") return "No recently used files."
                        if (root.tab.view === "trash") return "The trash is empty."
                        return "This folder is empty."
                    }
                    color: root.cText
                    font.pixelSize: 14
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.tab && root.tab.view === "dir" && !root.tab.showHidden
                          ? "Hidden items are not shown." : ""
                    color: root.cDim
                    font.pixelSize: 11
                    visible: text !== ""
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

                visible: ctxMenu.open
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
                    anchors { fill: parent; margins: 4 }
                    contentHeight: ctxCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: ctxCol
                    width: parent.width

                    Repeater {
                        model: {
                            const t = root.tab
                            if (!t || !ctxMenu.row) return []
                            const r = ctxMenu.row
                            const n = root.selection.length
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
                                items.push({ label: "Rename…", act: "rename", on: one })
                                items.push({ label: "Move to Trash" + many,
                                             act: "trash", on: n > 0 })
                            }
                            // Borrowed entries, appended flat rather than in a
                            // submenu: a submenu that is empty half the time is
                            // worse than four extra rows that are visibly about
                            // this file.
                            const opens = root.rowActions.filter(a => a.kind === "open-with")
                            const svcs  = root.rowActions.filter(a => a.kind === "service")

                            if (opens.length > 0) {
                                items.push({ label: "-", act: "", on: true })
                                for (const a of opens.slice(0, 6))
                                    items.push({ label: "Open with " + a.label,
                                                 act: "run", on: true,
                                                 desktop: a.desktop, actionId: "" })
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

                            items.push({ label: "-", act: "", on: true })
                            items.push({ label: "Copy Path", act: "copypath", on: one })
                            items.push({ label: "Open Terminal Here", act: "term",
                                         on: t.view === "dir" })
                            items.push({ label: root.isPinned(r.full) ? "Remove from Places"
                                                                     : "Add to Places",
                                         act: "pin", on: one && r.type === "dir" })
                            items.push({ label: "-", act: "", on: true })
                            items.push({ label: "New Folder…", act: "newdir", on: t.view === "dir" })
                            items.push({ label: "New Empty File…", act: "newfile", on: t.view === "dir" })
                            items.push({ label: "Select All", act: "selectall", on: true })
                            items.push({ label: "Invert Selection", act: "invert", on: true })
                            items.push({ label: "-", act: "", on: true })
                            items.push({ label: "Properties…", act: "props", on: n > 0 })
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
                                    anchors { left: parent.left; leftMargin: 10
                                              verticalCenter: parent.verticalCenter }
                                    text: ctxItem.modelData.label
                                    color: ctxItem.modelData.on ? root.cText : root.cDim
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: ctxMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: ctxItem.modelData.on
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const r = ctxMenu.row
                                        ctxMenu.open = false
                                        switch (ctxItem.modelData.act) {
                                        case "open":    root.activate(r); break
                                        case "tab":     root.newTab(r.full, "dir"); break
                                        case "copy":    root.copySelection(false); break
                                        case "cut":     root.copySelection(true); break
                                        case "paste":   root.paste(); break
                                        case "rename":  root.renaming = r.name; break
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
                                        case "selectall": root.selectAll(); break
                                        case "invert": {
                                            const inv = root.shownRows
                                                .filter(x => !root.isSelected(x.name))
                                                .map(x => x.name)
                                            root.selection = inv
                                            break
                                        }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                }
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
                                        font.pixelSize: 12
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
                                            return ""
                                        }
                                        color: root.cAccent
                                        font.pixelSize: 12
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
                Drag.hotSpot: Qt.point(8, 8)
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
                                    font.pixelSize: 12
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
                    font { pixelSize: 14; bold: true }
                }
                Text {
                    anchors { top: parent.top; right: parent.right; margins: 14 }
                    text: "×"
                    color: propCloseMa.containsMouse ? root.cAccent : root.cDim
                    font.pixelSize: 16
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
                                    font.pixelSize: 11
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
                                        if (k === "size")
                                            return root.fmtSize(parseInt(v || "0"), false)
                                                 + "  (" + v + " bytes)"
                                        if (k === "mtime" || k === "atime" || k === "ctime")
                                            return root.fmtTime(parseInt(v || "0"))
                                        return v
                                    }
                                    color: root.cText
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                    }
                }
            }

            // ── Empty-trash confirmation ────────────────────────────────────
            Rectangle {
                anchors.centerIn: parent
                width: 360; height: 130
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cWarn }
                visible: root.confirmEmpty
                z: 120

                Column {
                    anchors { fill: parent; margins: 16 }
                    spacing: 10

                    Text {
                        text: "Empty the trash?"
                        color: root.cWarn
                        font { pixelSize: 14; bold: true }
                    }
                    Text {
                        width: parent.width - 4
                        text: "Everything in the trash will be removed permanently. "
                            + "This cannot be undone."
                        color: root.cText
                        font.pixelSize: 12
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

            // ── New folder prompt ───────────────────────────────────────────
            Rectangle {
                anchors.centerIn: parent
                width: 320; height: 96
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cAccent }
                visible: root.creating
                z: 120

                Column {
                    anchors { fill: parent; margins: 14 }
                    spacing: 10

                    Text {
                        text: "New folder"
                        color: root.cAccent
                        font { pixelSize: 13; bold: true }
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
                            font.pixelSize: 12
                            clip: true
                            onVisibleChanged: if (visible) { text = ""; forceActiveFocus() }
                            onAccepted: root.commitNewFolder(text)
                            Keys.onEscapePressed: root.creating = false
                        }
                    }
                    Text {
                        text: "Enter to create, Escape to cancel"
                        color: root.cDim
                        font.pixelSize: 10
                    }
                }
            }

            // ── New empty file prompt ───────────────────────────────────────
            Rectangle {
                anchors.centerIn: parent
                width: 320; height: 96
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cAccent }
                visible: root.creatingFile
                z: 120

                Column {
                    anchors { fill: parent; margins: 14 }
                    spacing: 10

                    Text {
                        text: "New empty file"
                        color: root.cAccent
                        font { pixelSize: 13; bold: true }
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
                            font.pixelSize: 12
                            clip: true
                            onVisibleChanged: if (visible) { text = ""; forceActiveFocus() }
                            onAccepted: root.createEmptyFile(text)
                            Keys.onEscapePressed: root.creatingFile = false
                        }
                    }
                    Text {
                        text: "Enter to create, Escape to cancel"
                        color: root.cDim
                        font.pixelSize: 10
                    }
                }
            }

            Rectangle {
                id: statusBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 22
                color: root.cPanel

                Text {
                    anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                    text: {
                        if (root.loading) return "reading…"
                        if (root.statusLine) return root.statusLine
                        const n = root.shownRows.length
                        const s = root.selection.length
                        const base = n + (n === 1 ? " item" : " items")
                        // How much is selected is the thing you check right
                        // before pressing Delete, so it goes where you are
                        // already looking rather than only in a menu label.
                        return s > 0 ? base + "  ·  " + s + " selected" : base
                    }
                    color: root.statusLine ? root.cWarn : root.cDim
                    font.pixelSize: 11
                }
                Text {
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: root.tab && root.tab.view === "dir" ? root.disp(root.tab.path) : ""
                    color: root.cDim
                    font.pixelSize: 11
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
                                font.pixelSize: 12
                            }
                            Text {
                                anchors { right: parent.right; rightMargin: 10
                                          verticalCenter: parent.verticalCenter }
                                text: viewItem.modelData.tick ? "✓" : ""
                                color: root.cAccent
                                font.pixelSize: 12
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
                        font.pixelSize: 12
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
                                    font.pixelSize: 11
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

    // ── Small reusable pieces ───────────────────────────────────────────────

    component SideHeading: Text {
        leftPadding: 14
        bottomPadding: 4
        color: root.cDim
        font { pixelSize: 10; bold: true }
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
            font.pixelSize: 12
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
            font.pixelSize: 9
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
            font.pixelSize: 12
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
            font.pixelSize: 12
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
            x: tb.label === "" ? (tb.width - implicitWidth) / 2 : 9
            text: tb.glyph
            color: tb.active ? root.cAccent : root.cText
            font.pixelSize: 14
        }
        Text {
            id: tbLabel
            anchors.verticalCenter: parent.verticalCenter
            x: tbGlyph.x + tbGlyph.implicitWidth + (tb.label === "" ? 0 : 5)
            text: tb.label === "" ? "" : tb.label + " ⌄"
            color: tb.active ? root.cAccent : root.cText
            font.pixelSize: 11
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
                font.pixelSize: 10
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
        signal toggled()

        width: chipText.implicitWidth + 20
        height: 26
        radius: 3
        color: chip.on ? root.wash(0.20)
                       : (chipMa.containsMouse ? root.wash(0.10) : "transparent")
        border { width: 1; color: chip.on ? root.cAccent : "transparent" }

        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.on ? root.cAccent : root.cDim
            font.pixelSize: 11
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
