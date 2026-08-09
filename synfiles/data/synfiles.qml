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
            sort: "name",
            reverse: false,
            showHidden: false,
            filter: ""
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
        root.setTab({ path: pathEnc, view: view || "dir", filter: "", rows: [] })
        root.reload()
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
        stdout: StdioCollector {
            onStreamFinished: {
                const table = root.parseRecords(this.text)
                const t = root.tab
                if (!t) return

                let rows = []
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
                        missing: false
                    }))
                }

                root.setTab({ rows: rows })
                root.loading = false
                root.statusLine = ""
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

    // ── Selection, clipboard and operations ─────────────────────────────────

    property string selected: ""     // the ENCODED name of the focused row

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
            placesProc.running = true
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

    function selectedRow() {
        for (const r of root.shownRows)
            if (r.name === root.selected) return r
        return null
    }

    function toTrash(row) {
        if (!row) return
        root.runOp(["trash", root.disp(row.full)],
                   "moving " + root.disp(row.name) + " to the trash")
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
        const row = root.selectedRow()
        if (!row) return
        root.clip = { op: cut ? "cut" : "copy", paths: [row.full] }
        root.statusLine = (cut ? "cut " : "copied ") + root.disp(row.name)
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
                root.volumes = root.parseRecords(this.text).filter(r => r.mounted === "1")
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
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // ── Sidebar ─────────────────────────────────────────────────────────
        Rectangle {
            id: sidebar
            anchors { top: parent.top; left: parent.left; bottom: parent.bottom }
            width: 220
            color: root.cPanel

            Flickable {
                anchors.fill: parent
                anchors.topMargin: 10
                contentHeight: sideCol.implicitHeight
                clip: true

                Column {
                    id: sideCol
                    width: parent.width
                    spacing: 2

                    Text {
                        x: 14
                        text: "SYNAPSE Files"
                        color: root.cAccent
                        font { pixelSize: 14; bold: true }
                        bottomPadding: 8
                    }

                    // Views that are not directories.
                    Repeater {
                        model: [{ label: "Recent",  icon: "document-open-recent", view: "recent" },
                                { label: "Trash",   icon: "user-trash",           view: "trash" },
                                { label: "About",   icon: "help-about",           view: "about" }]
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
                            onActivated: root.navigate(modelData.href, "dir")
                            onRemoved: root.unpin(modelData.href)
                        }
                    }

                    Item { width: 1; height: 10 }
                    SideHeading { text: "Devices"; visible: root.volumes.length > 0 }

                    Repeater {
                        model: root.volumes
                        delegate: SideRow {
                            required property var modelData
                            label: modelData.title
                            iconName: modelData.icon || "drive-harddisk"
                            sub: modelData.size
                            active: root.tab && root.tab.view === "dir"
                                    && root.tab.path === modelData.path
                            onActivated: root.navigate(modelData.path, "dir")
                        }
                    }
                }
            }
        }

        // ── Main pane ───────────────────────────────────────────────────────
        Item {
            anchors {
                top: parent.top; left: sidebar.right
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
                    id: crumbs
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: 0

                    Rectangle {
                        width: 26; height: 26; radius: 3
                        color: upMa.containsMouse ? root.wash(0.12) : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "↑"
                            color: root.cAccent
                            font.pixelSize: 14
                        }
                        MouseArea {
                            id: upMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.navigate(root.parentEnc(root.tab.path), "dir")
                        }
                    }

                    // Each crumb keeps the ENCODED prefix it navigates to;
                    // rebuilding a path by re-joining displayed text would lose
                    // exactly the names this whole scheme exists to protect.
                    Repeater {
                        model: {
                            if (!root.tab || root.tab.view !== "dir") return []
                            const parts = root.tab.path.split("/").filter(s => s !== "")
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
                                text: " / "
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

                Row {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 6

                    ToggleChip {
                        label: root.tab && root.tab.showHidden ? "Hidden ✓" : "Hidden"
                        on: root.tab ? root.tab.showHidden : false
                        onToggled: { root.setTab({ showHidden: !root.tab.showHidden }); root.reload() }
                    }
                    ToggleChip {
                        label: root.tab ? ("Sort: " + root.tab.sort) : "Sort"
                        on: false
                        onToggled: {
                            const order = ["name", "size", "mtime", "type"]
                            const i = order.indexOf(root.tab.sort)
                            root.setTab({ sort: order[(i + 1) % order.length] })
                            root.reload()
                        }
                    }
                    ToggleChip {
                        label: root.tab && root.tab.reverse ? "↓" : "↑"
                        on: root.tab ? root.tab.reverse : false
                        onToggled: { root.setTab({ reverse: !root.tab.reverse }); root.reload() }
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

                TextInput {
                    id: filterInput
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.cText
                    font.pixelSize: 12
                    clip: true
                    onTextChanged: root.setTab({ filter: text })
                    Keys.onEscapePressed: { text = ""; root.setTab({ filter: "" }) }
                }
                Text {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    text: "filter these items…"
                    color: root.cDim
                    font.pixelSize: 12
                    visible: filterInput.text === ""
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

            // Column headings
            Item {
                id: heads
                anchors { top: filterBar.bottom; left: parent.left; right: parent.right }
                anchors.margins: 8
                anchors.topMargin: 4
                height: 20
                visible: root.tab && root.tab.view !== "about"

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
                clip: true
                visible: root.tab && root.tab.view !== "about"
                model: root.shownRows
                spacing: 1
                currentIndex: -1
                focus: true

                // Shortcuts a file manager is expected to have. Delete goes to
                // the TRASH — the permanent one is a separate command behind a
                // separate flag, and no key reaches it.
                Keys.onPressed: (event) => {
                    const row = root.selectedRow()
                    if (event.key === Qt.Key_Delete) {
                        if (root.tab.view === "dir") root.toTrash(row)
                        event.accepted = true
                    } else if (event.key === Qt.Key_F2) {
                        if (row && root.tab.view === "dir") root.renaming = row.name
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (row) root.activate(row)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backspace) {
                        if (root.tab.view === "dir")
                            root.navigate(root.parentEnc(root.tab.path), "dir")
                        event.accepted = true
                    } else if (event.modifiers & Qt.ControlModifier) {
                        if (event.key === Qt.Key_C)      { root.copySelection(false); event.accepted = true }
                        else if (event.key === Qt.Key_X) { root.copySelection(true);  event.accepted = true }
                        else if (event.key === Qt.Key_V) { root.paste();              event.accepted = true }
                        else if (event.key === Qt.Key_T) { root.newTab(root.tab.path, "dir"); event.accepted = true }
                        else if (event.key === Qt.Key_W) { root.closeTab(root.current); event.accepted = true }
                        else if (event.key === Qt.Key_N) { root.creating = true; event.accepted = true }
                    }
                }

                delegate: Rectangle {
                    id: fileRow
                    required property var modelData
                    readonly property bool isSelected: fileRow.modelData.name === root.selected
                    readonly property bool isRenaming: fileRow.modelData.name === root.renaming
                    width: ListView.view.width
                    height: 30
                    radius: 3
                    color: fileRow.isSelected ? root.wash(0.22)
                         : (rowMa.containsMouse ? root.wash(0.10) : "transparent")

                    Image {
                        id: rowIcon
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 20; height: 20
                        sourceSize: Qt.size(20, 20)
                        source: root.iconFor(fileRow.modelData)
                        opacity: fileRow.modelData.missing ? 0.4 : 1.0
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
                        text: root.disp(fileRow.modelData.name)
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

                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        enabled: !fileRow.isRenaming
                        onClicked: (mouse) => {
                            root.selected = fileRow.modelData.name
                            fileList.forceActiveFocus()
                            if (mouse.button === Qt.RightButton) {
                                ctxMenu.row = fileRow.modelData
                                ctxMenu.x = fileRow.x + mouse.x
                                ctxMenu.y = fileRow.y + mouse.y - fileList.contentY + heads.height + 8
                                ctxMenu.open = true
                            }
                        }
                        // Double-click to open, matching every other file
                        // manager. Single-click-to-open is a setting worth
                        // having and a default worth not having.
                        onDoubleClicked: root.activate(fileRow.modelData)
                    }
                }
            }

            // Empty state
            Column {
                anchors.centerIn: parent
                spacing: 6
                visible: !root.loading && root.shownRows.length === 0

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        if (!root.tab) return ""
                        if (root.tab.filter) return "Nothing matches that filter."
                        if (root.tab.view === "recent") return "No recently used files."
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
                width: 190
                height: ctxCol.implicitHeight + 8
                radius: 4
                color: root.cPanel
                border { width: 1; color: root.wash(0.35) }
                z: 100

                // Keep the menu inside the window rather than letting it hang
                // off the bottom edge on a row near the end of a long list.
                onOpenChanged: {
                    if (!ctxMenu.open) return
                    if (ctxMenu.y + ctxMenu.height > parent.height)
                        ctxMenu.y = Math.max(0, parent.height - ctxMenu.height - 4)
                    if (ctxMenu.x + ctxMenu.width > parent.width)
                        ctxMenu.x = Math.max(0, parent.width - ctxMenu.width - 4)
                }

                Column {
                    id: ctxCol
                    anchors { fill: parent; margins: 4 }

                    Repeater {
                        model: {
                            const t = root.tab
                            if (!t || !ctxMenu.row) return []
                            const r = ctxMenu.row
                            if (t.view === "trash")
                                return [{ label: "Restore", act: "restore",
                                          on: !r.missing }]

                            const items = [
                                { label: r.type === "dir" ? "Open Folder" : "Open",
                                  act: "open", on: !r.missing },
                                { label: "Open in New Tab", act: "tab",
                                  on: r.type === "dir" }
                            ]
                            if (t.view === "dir") {
                                items.push({ label: "-", act: "", on: true })
                                items.push({ label: "Copy",   act: "copy",   on: true })
                                items.push({ label: "Cut",    act: "cut",    on: true })
                                items.push({ label: "Paste",  act: "paste",
                                             on: root.clip.paths.length > 0 })
                                items.push({ label: "-", act: "", on: true })
                                items.push({ label: "Rename…", act: "rename", on: true })
                                items.push({ label: "Move to Trash", act: "trash", on: true })
                            }
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
                                        case "copy":    root.selected = r.name; root.copySelection(false); break
                                        case "cut":     root.selected = r.name; root.copySelection(true); break
                                        case "paste":   root.paste(); break
                                        case "rename":  root.selected = r.name; root.renaming = r.name; break
                                        case "trash":   root.toTrash(r); break
                                        case "restore": root.restoreFromTrash(r); break
                                        }
                                    }
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
                        return n + (n === 1 ? " item" : " items")
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
        signal activated()
        signal removed()

        width: sidebar.width
        height: 28
        color: sideRow.active ? root.wash(0.18)
                              : (sideMa.containsMouse ? root.wash(0.08) : "transparent")

        Image {
            id: sideIcon
            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
            width: 16; height: 16
            sourceSize: Qt.size(16, 16)
            source: Quickshell.iconPath(sideRow.iconName, true)
        }
        Text {
            anchors {
                left: sideIcon.right; leftMargin: 8
                right: parent.right; rightMargin: 24
                verticalCenter: parent.verticalCenter
            }
            text: sideRow.label
            elide: Text.ElideRight
            color: sideRow.active ? root.cAccent : root.cText
            font.pixelSize: 12
        }
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
        MouseArea {
            id: sideMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: sideRow.activated()
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
