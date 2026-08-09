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
                if (t.view === "recent") {
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

        if (t.view === "recent") {
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

    // Sidebar sources. All three are read once at startup and on refresh —
    // they change rarely and re-running them per navigation would put three
    // process spawns in front of every double-click.
    property var places: []
    property var volumes: []

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
                        model: [{ label: "Recent", icon: "document-open-recent", view: "recent" }]
                        delegate: SideRow {
                            required property var modelData
                            label: modelData.label
                            iconName: modelData.icon
                            active: root.tab && root.tab.view === modelData.view
                            onActivated: root.navigate("", modelData.view)
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

            // Column headings
            Item {
                id: heads
                anchors { top: filterBar.bottom; left: parent.left; right: parent.right }
                anchors.margins: 8
                anchors.topMargin: 4
                height: 20

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
                model: root.shownRows
                spacing: 1
                currentIndex: -1

                delegate: Rectangle {
                    id: fileRow
                    required property var modelData
                    width: ListView.view.width
                    height: 30
                    radius: 3
                    color: rowMa.containsMouse ? root.wash(0.10) : "transparent"

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
                        acceptedButtons: Qt.LeftButton
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
