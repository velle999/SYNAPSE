//@ pragma UseQApplication
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * SYNAPSE Software — the graphical front-end for synpkg.
 *
 * Every fact on screen comes from `synpkg --tsv <command>`, the same binary
 * the CLI and the terminal browser are, so the three can never disagree about
 * what is installed. Nothing here knows what a package IS; it renders rows.
 *
 * TSV rather than JSON across that boundary: the C side would have to escape
 * JSON by hand, and a package description containing a quote is not a hypo-
 * thetical. Parsing here is a split on newline and a split on tab.
 *
 * The palette block is lifted from syn-arsenal's, deliberately unchanged — it
 * encodes two shipped bugs (reading theme.json's [r,g,b] ARRAYS as strings,
 * and taking ink from the theme while drawing on hardcoded surfaces) and this
 * window would reproduce both.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
FloatingWindow {
    id: root

    title: "SYNAPSE Software"
    implicitWidth: 1120
    implicitHeight: 720

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNPKG_BIN") || "synpkg"

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

    // ── State ───────────────────────────────────────────────────────────────
    readonly property var sections: [
        { id: "updates",   label: "Updates" },
        { id: "suggested", label: "Suggested" },
        { id: "search",    label: "Search" },
        { id: "installed", label: "Installed" },
        { id: "arsenal",   label: "Arsenal" },
        { id: "system",    label: "SynapseOS" }
    ]
    property string section: "updates"
    property string busy: ""        // package currently being (un)installed
    property string statusLine: ""
    property bool   loading: false

    property var rows: []           // {name, installed, version, repo, desc, extra}
    property var categories: []     // arsenal categories / suggested categories
    property string currentGroup: ""
    property string filter: ""

    readonly property var shownRows: {
        if (filter === "") return rows
        const f = filter.toLowerCase()
        return rows.filter(r => r.name.toLowerCase().includes(f)
                             || (r.desc || "").toLowerCase().includes(f))
    }

    // ── Backend ─────────────────────────────────────────────────────────────
    // One reader for every list command. The C side emits a header row whose
    // second column is the literal "installed"; skipping it here rather than
    // suppressing it there keeps `synpkg --tsv` self-describing for anything
    // else that reads it.
    function parseRows(text) {
        const out = []
        for (const line of text.split("\n")) {
            if (!line) continue
            const f = line.split("\t")
            if (f[0] === "name" || f[0] === "category" || f[0] === "component") continue
            out.push({ name: f[0], installed: f[1] === "1", version: f[2] || "",
                       repo: f[3] || "", size: parseInt(f[4] || "0"),
                       desc: f[5] || "", extra: f[6] || "" })
        }
        return out
    }

    Process {
        id: listProc
        property string mode: ""
        stdout: StdioCollector {
            onStreamFinished: {
                root.loading = false
                if (listProc.mode === "categories") {
                    const out = []
                    for (const line of this.text.split("\n")) {
                        if (!line) continue
                        const f = line.split("\t")
                        if (f[0] === "category") continue
                        out.push({ name: f[0], total: parseInt(f[1] || "0"),
                                   installed: parseInt(f[2] || "0") })
                    }
                    root.categories = out
                } else if (listProc.mode === "updates") {
                    // updates has its own shape: name, old, new, repo, size.
                    const out = []
                    for (const line of this.text.split("\n")) {
                        if (!line) continue
                        const f = line.split("\t")
                        if (f[0] === "name") continue
                        out.push({ name: f[0], installed: true, version: f[2] || "",
                                   repo: f[3] || "", size: parseInt(f[4] || "0"),
                                   desc: (f[1] || "") + "  →  " + (f[2] || ""),
                                   extra: "update" })
                    }
                    root.rows = out
                } else if (listProc.mode === "system") {
                    const out = []
                    for (const line of this.text.split("\n")) {
                        if (!line) continue
                        const f = line.split("\t")
                        if (f[0] === "component") continue
                        out.push({ name: f[0], installed: true, version: f[2] || "",
                                   repo: "synapseos", size: 0,
                                   desc: (f[1] || "") + "  →  " + (f[2] || ""),
                                   extra: "component" })
                    }
                    root.rows = out
                } else if (listProc.mode === "suggested") {
                    const out = []
                    for (const line of this.text.split("\n")) {
                        if (!line) continue
                        const f = line.split("\t")
                        if (f[0] === "category") continue
                        out.push({ name: f[1], installed: f[4] === "1",
                                   version: "", repo: f[2] || "", size: 0,
                                   desc: f[5] || "", extra: f[0] + " · " + f[3] })
                    }
                    root.rows = out
                } else {
                    root.rows = root.parseRows(this.text)
                }
            }
        }
    }

    // Mutations re-read the pane rather than patching the row in place: an
    // install pulls dependencies, a remove takes unneeded ones with it, and a
    // list that disagrees with the disk is worse than a slower refresh.
    // No parameters on this handler, deliberately. Quickshell's `exited` signal
    // is exited(int, QProcess::ExitStatus), and a typed arrow-function handler
    // fails to compile because that second type is not resolvable from QML —
    // The linter flags it, and the handler would simply never run.
    //
    // Nothing is lost: the exit code only drove a cosmetic message, and the
    // reload below reports the truth either way. If the install failed, the row
    // comes back still uninstalled.
    Process {
        id: actProc
        onExited: {
            root.busy = ""
            root.statusLine = ""
            root.reload()
        }
    }

    function runList(mode, args) {
        root.loading = true
        root.rows = []
        listProc.mode = mode
        listProc.command = [root.bin, "--tsv"].concat(args)
        listProc.running = true
    }

    function reload() {
        statusLine = ""
        filter = ""
        if (section === "updates")        runList("updates", ["updates"])
        else if (section === "suggested") runList("suggested", ["suggest"])
        else if (section === "installed") runList("list", ["installed", "--explicit"])
        else if (section === "system")    runList("system", ["system", "check"])
        else if (section === "search")    { rows = []; }
        else if (section === "arsenal") {
            currentGroup = ""
            runList("categories", ["arsenal", "categories"])
        }
    }

    // --noconfirm because the GUI has no terminal to answer a prompt in; the C
    // side deliberately refuses to assume yes without it. Escalation to root
    // happens inside synpkg via pkexec, so nothing here runs privileged.
    function act(verb, pkg) {
        if (busy !== "") return
        busy = pkg
        statusLine = verb === "install" ? "installing " + pkg + "…"
                                        : "removing " + pkg + "…"
        actProc.command = [root.bin, "--tsv", "-y", verb, pkg]
        actProc.running = true
    }

    function doSearch(term) {
        if (!term) { rows = []; return }
        runList("list", ["search", term])
    }

    Component.onCompleted: reload()

    // ── Layout ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 56
            color: root.cPanel

            Text {
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                text: "SYNAPSE Software"
                color: root.cAccent
                font { pixelSize: 18; bold: true }
            }
            Text {
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                color: root.busy !== "" ? root.cAccent : root.cDim
                font.pixelSize: 12
                text: root.loading ? "loading…"
                                   : (root.statusLine !== "" ? root.statusLine
                                                             : root.rows.length + " items")
            }
        }

        // ── Sections ────────────────────────────────────────────────────────
        Rectangle {
            id: nav
            anchors { top: header.bottom; left: parent.left; bottom: parent.bottom }
            width: 170
            color: root.cPanel

            Column {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                anchors.topMargin: 8

                Repeater {
                    model: root.sections
                    delegate: Rectangle {
                        id: navRow
                        required property var modelData
                        readonly property bool current: navRow.modelData.id === root.section
                        width: nav.width
                        height: 34
                        color: navRow.current ? root.wash(0.16)
                                              : (navMa.containsMouse ? root.wash(0.08) : "transparent")

                        Text {
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            text: navRow.modelData.label
                            color: navRow.current ? root.cAccent : root.cText
                            font { pixelSize: 13; bold: navRow.current }
                        }
                        MouseArea {
                            id: navMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.section = navRow.modelData.id; root.reload() }
                        }
                    }
                }
            }

            // Whole-system actions live at the bottom of the nav, away from the
            // per-row buttons: "upgrade everything" and "remove this one thing"
            // being adjacent is how the wrong one gets clicked.
            Column {
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                anchors.margins: 10
                spacing: 6

                Rectangle {
                    width: parent.width; height: 30; radius: 4
                    color: upMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                    border { width: 1; color: root.cAccent }
                    Text {
                        anchors.centerIn: parent
                        text: "Upgrade all"
                        color: root.cAccent
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: upMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // A full upgrade downloads for minutes and prints as it
                        // goes; a window with a spinner and no output is where
                        // people force-quit mid-transaction. Hand it a terminal.
                        onClicked: {
                            root.statusLine = "opened a terminal for the upgrade"
                            termProc.command = ["foot", "--hold", root.bin, "upgrade"]
                            termProc.running = true
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: "runs in a terminal"
                    color: root.cDim
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Process { id: termProc }

        // ── Content ─────────────────────────────────────────────────────────
        Item {
            id: content
            anchors {
                top: header.bottom; left: nav.right
                right: parent.right; bottom: parent.bottom
            }

            // Arsenal gets a category column of its own; every other section is
            // a single list.
            Rectangle {
                id: catPane
                visible: root.section === "arsenal" && root.categories.length > 0
                anchors { top: parent.top; left: parent.left; bottom: parent.bottom }
                width: visible ? 230 : 0
                color: Qt.rgba(root.cPanel.r, root.cPanel.g, root.cPanel.b, 0.6)

                ListView {
                    anchors.fill: parent
                    anchors.topMargin: 6
                    clip: true
                    model: root.categories
                    spacing: 1

                    header: Rectangle {
                        width: ListView.view.width
                        height: 30
                        color: allMa.containsMouse ? root.wash(0.08) : "transparent"
                        Text {
                            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                            text: "Installed tools"
                            color: root.cAccent
                            font { pixelSize: 12; bold: true }
                        }
                        MouseArea {
                            id: allMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.currentGroup = "installed"
                                root.runList("list", ["arsenal", "installed"])
                            }
                        }
                    }

                    delegate: Rectangle {
                        id: catRow
                        required property var modelData
                        readonly property bool current: catRow.modelData.name === root.currentGroup
                        width: ListView.view.width
                        height: 28
                        color: catRow.current ? root.wash(0.16)
                                              : (catMa.containsMouse ? root.wash(0.08) : "transparent")

                        Text {
                            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                            // Every group is "blackarch-<thing>"; the prefix is
                            // noise repeated 50 times down the pane.
                            text: catRow.modelData.name.replace("blackarch-", "")
                            color: catRow.current ? root.cAccent : root.cText
                            font.pixelSize: 12
                        }
                        Text {
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            text: catRow.modelData.installed > 0
                                  ? catRow.modelData.installed + "/" + catRow.modelData.total
                                  : catRow.modelData.total
                            color: catRow.modelData.installed > 0 ? root.cAccent : root.cDim
                            font.pixelSize: 10
                        }
                        MouseArea {
                            id: catMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.currentGroup = catRow.modelData.name
                                root.runList("list", ["arsenal", "packages", catRow.modelData.name])
                            }
                        }
                    }
                }
            }

            // Search box: doubles as a filter for every other section, which is
            // why it is always present rather than only on the Search page.
            Rectangle {
                id: searchBar
                anchors { top: parent.top; left: catPane.right; right: parent.right }
                anchors.margins: 10
                height: 30
                radius: 4
                color: root.cPanel

                TextInput {
                    id: searchInput
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.cText
                    font.pixelSize: 13
                    clip: true
                    onTextChanged: if (root.section !== "search") root.filter = text
                    onAccepted: if (root.section === "search") root.doSearch(text)
                }
                Text {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    text: root.section === "search" ? "search the repositories — press Enter"
                                                    : "filter this list…"
                    color: root.cDim
                    font.pixelSize: 13
                    visible: searchInput.text === ""
                }
            }

            // Empty states say what to DO. "No results" with no next step is
            // how the old arsenal looked on a machine without BlackArch.
            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: !root.loading && root.shownRows.length === 0

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: root.cText
                    font.pixelSize: 15
                    text: {
                        if (root.section === "updates")   return "Everything is up to date."
                        if (root.section === "search")    return "Search the repositories."
                        if (root.section === "system")    return "SynapseOS components are current."
                        if (root.section === "arsenal")   return root.categories.length === 0
                                                                 ? "The BlackArch repository is not enabled."
                                                                 : "Pick a category."
                        if (root.section === "suggested") return "You already have everything suggested."
                        return "Nothing here."
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: root.cAccent
                    font { pixelSize: 12; family: "monospace" }
                    visible: text !== ""
                    text: (root.section === "arsenal" && root.categories.length === 0)
                          ? "sudo synpkg arsenal enable-repo" : ""
                }
            }

            ListView {
                anchors {
                    top: searchBar.bottom; topMargin: 6
                    left: catPane.right; right: parent.right; bottom: parent.bottom
                }
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.bottomMargin: 10
                clip: true
                model: root.shownRows
                spacing: 2

                delegate: Rectangle {
                    id: pkgRow
                    required property var modelData
                    width: ListView.view.width
                    height: 48
                    radius: 4
                    color: rowMa.containsMouse ? root.wash(0.07) : "transparent"

                    MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true }

                    Rectangle {
                        id: dot
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 7; height: 7; radius: 4
                        color: pkgRow.modelData.installed ? root.cAccent : "transparent"
                        border { width: pkgRow.modelData.installed ? 0 : 1; color: root.cDim }
                    }

                    Column {
                        anchors {
                            left: dot.right; leftMargin: 12
                            right: actionBtn.left; rightMargin: 12
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: 2

                        Row {
                            spacing: 8
                            Text {
                                text: pkgRow.modelData.name
                                color: root.cText
                                font { pixelSize: 13; bold: true }
                            }
                            Text {
                                text: pkgRow.modelData.repo
                                color: root.cDim
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: pkgRow.modelData.extra
                                color: root.cWarn
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                                visible: text !== "" && text !== "update" && text !== "component"
                            }
                        }
                        Text {
                            width: parent.width
                            text: pkgRow.modelData.desc
                            color: root.cDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }

                    Rectangle {
                        id: actionBtn
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 84; height: 26; radius: 4
                        // A SynapseOS component is not installable from here —
                        // syn-update owns that, and it needs a terminal.
                        visible: pkgRow.modelData.extra !== "component"
                        color: btnMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                        border { width: 1; color: root.cAccent }
                        opacity: root.busy === "" || root.busy === pkgRow.modelData.name ? 1 : 0.4

                        Text {
                            anchors.centerIn: parent
                            color: root.cAccent
                            font.pixelSize: 11
                            text: root.busy === pkgRow.modelData.name
                                  ? "working…"
                                  : (pkgRow.modelData.extra === "update" ? "Upgrade"
                                     : (pkgRow.modelData.installed ? "Remove" : "Install"))
                        }
                        MouseArea {
                            id: btnMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (pkgRow.modelData.extra === "update")
                                    root.act("install", pkgRow.modelData.name)
                                else
                                    root.act(pkgRow.modelData.installed ? "remove" : "install",
                                             pkgRow.modelData.name)
                            }
                        }
                    }
                }
            }
        }
    }
}
