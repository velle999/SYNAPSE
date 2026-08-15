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

FloatingWindow {
    id: root

    title: "SYNAPSE Arcade"
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
    readonly property color dim:     "#9a92b8"
    readonly property color accent:  "#a78bfa"
    readonly property color good:    "#4ec9b0"
    readonly property color bad:     "#f2777a"

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

    Process {
        id: mapsProc
        stdout: StdioCollector {
            onStreamFinished: root.maps = root.parseRecords(this.text)
        }
    }

    Process {
        id: bindsProc
        stdout: StdioCollector {
            onStreamFinished: root.bindFields = root.parseFields(this.text)
        }
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
                    model: ["Overlay", "Controllers", "Mappings", "Shortcuts"]
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
                            font.pixelSize: 13
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
                    text: "Refresh"
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
                        text: "MangoHud overlay"
                        color: root.ink
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Frame rate, temperatures and load, drawn over any game. "
                            + "These take effect inside a game that is already running."
                        color: root.dim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.topMargin: 6
                        spacing: 8
                        ArcButton {
                            text: (root.hudFields.state === "hidden") ? "Show overlay"
                                                                     : "Hide overlay"
                            primary: true
                            onTriggered: root.run(["hud", "toggle"])
                        }
                        ArcButton {
                            text: "Move it"
                            onTriggered: root.run(["hud", "cycle"])
                        }
                    }

                    FieldRow { label: "State";      value: root.hudFields.state || "—" }
                    FieldRow { label: "Position";   value: root.hudFields.position || "—" }
                    FieldRow { label: "Font size";  value: root.hudFields.font_size || "—" }
                    FieldRow { label: "Background"; value: root.hudFields.background_alpha || "—" }
                    FieldRow {
                        label: "Config file"
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
                        text: "⚠ That file is not writable by you, so nothing here reaches "
                            + "MangoHud. /etc/MangoHud.conf outranks your own config. "
                            + "\"Take ownership\" copies the settings now in effect into "
                            + "your own file."
                        color: root.bad
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    ArcButton {
                        visible: !root.hudWritable && (root.hudFields.config || "") !== ""
                        text: "Take ownership"
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
                        text: "Controllers"
                        color: root.ink
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.pads.length === 0
                        text: "Nothing plugged in. Steam handles its own controller "
                            + "setup — this is for everything outside it."
                        color: root.dim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    ListView {
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
                                        font.pixelSize: 13
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: padRow.modelData.kind + " · "
                                            + padRow.modelData.bus + " · "
                                            + (padRow.modelData.rumble === "yes"
                                               ? "rumble" : "no rumble")
                                        color: root.dim
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }

                                ArcButton {
                                    text: "Test"
                                    onTriggered: root.run(["pads", "test",
                                        padRow.modelData.id, "--seconds=10"])
                                }
                                ArcButton {
                                    text: "Rumble"
                                    enabled: padRow.modelData.rumble === "yes"
                                    onTriggered: root.run(["pads", "rumble",
                                        padRow.modelData.id])
                                }
                                ArcButton {
                                    text: "Calibrate"
                                    onTriggered: root.run(["pads", "calibrate",
                                        padRow.modelData.id, "--seconds=5"])
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.pads.length > 0
                        text: "Calibrate measures stick drift for five seconds — let go of "
                            + "both sticks first, or it will refuse the reading."
                        color: root.dim
                        font.pixelSize: 11
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
                        text: "Controller mappings"
                        color: root.ink
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "For a pad whose buttons come out in the wrong places. Add a "
                            + "mapping string from antimicrox or the SDL gamepad tool with "
                            + "`syn-arcade map add`; every SDL game reads them."
                        color: root.dim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    ListView {
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
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: mapRow.modelData.guid + "  ·  "
                                            + mapRow.modelData.bindings + " bindings"
                                        color: root.dim
                                        font.pixelSize: 11
                                        font.family: "monospace"
                                        elide: Text.ElideRight
                                    }
                                }

                                ArcButton {
                                    text: "Remove"
                                    onTriggered: root.run(["map", "remove",
                                        mapRow.modelData.guid])
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.maps.length === 0
                        text: "No mappings added."
                        color: root.dim
                        font.pixelSize: 12
                    }
                }

                // ── Shortcuts ─────────────────────────────────────────────
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    visible: root.tab === 3

                    Text {
                        text: "Gaming shortcuts"
                        color: root.ink
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Two compositor keys that drive the overlay from inside a "
                            + "running game. They are written to synuirc as ordinary bind "
                            + "lines, so the Super+/ palette can rebind them like any other."
                        color: root.dim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    FieldRow {
                        label: "Installed"
                        value: root.bindFields.installed || "—"
                    }
                    FieldRow {
                        label: "Toggle overlay"
                        value: root.bindFields["toggle overlay"] || "—"
                    }
                    FieldRow {
                        label: "Move overlay"
                        value: root.bindFields["move overlay"] || "—"
                    }
                    FieldRow {
                        label: "Config file"
                        value: root.bindFields.config || "—"
                    }

                    RowLayout {
                        Layout.topMargin: 6
                        spacing: 8
                        ArcButton {
                            text: root.bindFields.installed === "yes" ? "Reinstall"
                                                                     : "Install shortcuts"
                            primary: root.bindFields.installed !== "yes"
                            onTriggered: root.run(["binds", "install", "--reload"])
                        }
                        ArcButton {
                            text: "Remove"
                            visible: root.bindFields.installed === "yes"
                            onTriggered: root.run(["binds", "remove", "--reload"])
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // status line
            Text {
                Layout.fillWidth: true
                text: root.status
                color: root.status.startsWith("syn-arcade:") ? root.bad : root.good
                font.pixelSize: 12
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
            font.pixelSize: 12
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

    component FieldRow: RowLayout {
        property string label: ""
        property string value: ""
        property bool warn: false

        Layout.fillWidth: true
        spacing: 10

        Text {
            Layout.preferredWidth: 120
            text: parent.label
            color: root.dim
            font.pixelSize: 12
        }
        Text {
            Layout.fillWidth: true
            text: parent.value
            color: parent.warn ? root.bad : root.ink
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
