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
            const m = this.text().match(/^\s*family\s*=\s*(.+?)\s*$/m)
            root.uiFont = m ? m[1] : ""
        }
        onLoadFailed: root.uiFont = ""
    }

    // ── State ───────────────────────────────────────────────────────────────
    readonly property var panes: [
        { id: "display",   label: "Display",  blurb: "connectors as the kernel sees them, beside what the compositor drives" },
        { id: "region",    label: "Keyboard & Region", blurb: "layout, locale, time zone and whether the clock is actually disciplined" },
        { id: "network",   label: "Network",  blurb: "interfaces, radios, and whether anything is filtering traffic" },
        { id: "bluetooth", label: "Bluetooth", blurb: "the adapter, both kinds of radio block, and what is paired" },
        { id: "power",     label: "Power & Sleep", blurb: "the units a working suspend depends on, and what the last one did" },
        { id: "system",    label: "System",   blurb: "identity, and which layer each configuration file comes from" }
    ]
    property string pane: Quickshell.env("SYNSETTINGS_PANE") || "display"
    property var cols: []
    property var rows: []
    property bool loading: false
    property string status: ""

    // ── Editing ─────────────────────────────────────────────────────────────
    //
    // The row the editor is pointed at, and what it is allowed to do. Driven
    // entirely by the reader's trailing `action` column — "set:keymap",
    // "toggle:ntp", "unit:<name>", "probe:<name>" or "-". The GUI knows the
    // four VERBS and nothing about localectl, so a new editable setting is a
    // line of C and no QML at all.
    property int selRow: -1
    property string selAction: ""
    property string selKey: ""
    property string selValue: ""
    property bool applying: false

    readonly property int actionCol: root.cols.indexOf("action")
    function rowAction(r) {
        if (root.actionCol < 0 || !r || root.actionCol >= r.length) return "-"
        return r[root.actionCol]
    }
    function actionVerb(a) { const i = a.indexOf(":"); return i < 0 ? a : a.substring(0, i) }
    function actionArg(a)  { const i = a.indexOf(":"); return i < 0 ? "" : a.substring(i + 1) }

    function selectRow(i) {
        const r = root.rows[i]
        const a = root.rowAction(r)
        if (a === "-" || a === "") { root.selRow = -1; root.selAction = ""; return }
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
    }

    // Runs a write, then reloads. Never parses the tool's output: the reader
    // is the source of truth for what the system now says, and believing our
    // own success message over a re-read is how a settings app starts showing
    // a value the system never accepted.
    Process {
        id: writeProc
        stdout: StdioCollector { onStreamFinished: if (this.text) root.status = this.text.split("\n")[0] }
        stderr: StdioCollector { onStreamFinished: if (this.text) root.status = this.text.split("\n")[0] }
        onExited: (code) => {
            root.applying = false
            if (code !== 0 && root.status === "")
                root.status = "refused (exit " + code + ") — polkit may have declined"
            root.reload()
        }
    }

    function runWrite(args, note) {
        if (root.applying) return
        root.applying = true
        root.status = note
        writeProc.command = [root.bin].concat(args)
        writeProc.running = true
    }

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
        signal go()
        width: btnText.implicitWidth + 22
        height: 26
        radius: 4
        color: btnMa.containsMouse && !root.applying ? root.wash(0.22) : root.wash(0.10)
        opacity: root.applying ? 0.5 : 1.0

        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: root.cText
            font { family: root.uiFont; pixelSize: 11 }
        }
        MouseArea {
            id: btnMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: !root.applying
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.go()
        }
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
        readProc.command = [root.bin, "--rec", root.pane]
        readProc.running = true
    }

    Component.onCompleted: root.reload()

    // A value the eye should stop on. Deliberately narrow: only states that
    // mean something is off, so colour stays informative instead of decorative.
    function tone(col, val) {
        const v = (val || "").toLowerCase()
        if (v === "absent" || v === "not installed" || v === "not executable"
            || v === "failed" || v === "not driven")
            return root.cBad
        if (v === "disabled" || v === "inactive" || v === "unknown"
            || v === "disconnected" || v === "no")
            return root.cWarn
        if (v === "enabled" || v === "active" || v === "connected"
            || v === "executable" || v === "present" || v === "yes")
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
                    font { family: root.uiFont; pixelSize: 14; bold: true }
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
                            font { family: root.uiFont; pixelSize: 12 }
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
                font { family: root.uiFont; pixelSize: 9 }
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
                font { family: root.uiFont; pixelSize: 15; bold: true }
            }
            Text {
                anchors { left: parent.left; leftMargin: 18
                          right: refreshBtn.left; rightMargin: 12
                          top: headTitle.bottom; topMargin: 4 }
                elide: Text.ElideRight
                text: root.paneMeta(root.pane).blurb
                color: root.cDim
                font { family: root.uiFont; pixelSize: 10 }
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
                    font { family: root.uiFont; pixelSize: 11 }
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
                        font { family: root.uiFont; pixelSize: 10; bold: true }
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
                readonly property bool actionable:
                    root.rowAction(dataRow.modelData) !== "-"
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
                    color: root.cAccent
                    visible: dataRow.actionable
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
                            color: index === 0 ? root.cText
                                               : root.tone(root.cols[index] || "", modelData)
                            font { family: root.uiFont; pixelSize: 11 }
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
                        else { root.selRow = -1; root.selAction = "" }
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
            font { family: root.uiFont; pixelSize: 11 }
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
            height: root.selRow >= 0 ? 44 : 0
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
                font { family: root.uiFont; pixelSize: 12; bold: true }
            }

            // A value to type: keymap, xkb layout, locale, time zone.
            Rectangle {
                id: editBox
                anchors { left: editLabel.right; leftMargin: 12
                          verticalCenter: parent.verticalCenter }
                width: 220; height: 26; radius: 4
                visible: root.actionVerb(root.selAction) === "set"
                color: root.cBg
                border { width: 1; color: editField.activeFocus ? root.cAccent : root.wash(0.25) }
                clip: true

                TextInput {
                    id: editField
                    anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.cText
                    font { family: root.uiFont; pixelSize: 12 }
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
                    visible: root.actionVerb(root.selAction) !== "unit"
                    label: {
                        const v = root.actionVerb(root.selAction)
                        if (v === "toggle") return root.selValue === "active" ? "Turn off" : "Turn on"
                        if (v === "probe")  return "Re-probe"
                        return "Apply"
                    }
                    onGo: {
                        const v = root.actionVerb(root.selAction)
                        const arg = root.actionArg(root.selAction)
                        if (v === "set")
                            root.runWrite(["set", arg, editField.text], "setting " + arg + "…")
                        else if (v === "toggle")
                            root.runWrite(["set", arg, root.selValue === "active" ? "off" : "on"],
                                          "switching " + arg + "…")
                        else if (v === "probe")
                            root.runWrite(["probe", arg], "re-probing " + arg + "…")
                    }
                }

                Repeater {
                    model: root.actionVerb(root.selAction) === "unit"
                           ? ["enable", "disable", "start", "stop", "restart"] : []
                    delegate: SettingsButton {
                        required property var modelData
                        label: modelData.charAt(0).toUpperCase() + modelData.substring(1)
                        onGo: root.runWrite(["unit", modelData, root.actionArg(root.selAction)],
                                            modelData + " " + root.actionArg(root.selAction) + "…")
                    }
                }
            }

            SettingsButton {
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

            Text {
                id: statusLeft
                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                width: Math.min(implicitWidth, parent.width * 0.6 - 12)
                elide: Text.ElideRight
                text: root.status !== "" ? root.status
                    : root.loading ? "reading…"
                    : root.rows.length + (root.rows.length === 1 ? " row" : " rows")
                color: root.status !== "" ? root.cWarn : root.cDim
                font { family: root.uiFont; pixelSize: 10 }
            }
            Text {
                anchors { left: statusLeft.right; leftMargin: 12
                          right: parent.right; rightMargin: 12
                          verticalCenter: parent.verticalCenter }
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                text: root.bin + " --rec " + root.pane
                color: root.cDim
                font { family: root.uiFont; pixelSize: 10 }
            }
        }
    }
}
