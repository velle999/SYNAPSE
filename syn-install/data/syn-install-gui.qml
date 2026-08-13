// syn-install-gui — the graphical face of syn-install.
//
// A FORM, and nothing more. Every answer collected here is written to an
// install profile and handed to `syn-install --config`, which is the same
// script the text installer runs and the only thing in SynapseOS that knows
// how to partition a disk. Nothing in this file runs parted, pacstrap or
// mkfs, and nothing in it decides whether a disk is safe to erase — that
// answer arrives as records from `syn-install --list-disks`.
//
// The rule is the one synfiles, synpkg and syn-disks are built on: the tool
// does the work and prints records, the window only renders them. It matters
// more here than anywhere else in the system, because the alternative is two
// implementations of the partition rules, and the second one would be the
// copy with no test suite behind it.
//
// ── The one rule for the profile ───────────────────────────────────────────
//
// A key this form omits is a question syn-install ASKS — on a terminal nobody
// is looking at, because the window is in front of it. So every key a run can
// reach has to be written, and the keys that answer nothing must NOT be
// (syn-install reports unused keys at the end, and that report is how a
// typo in a hand-written profile gets caught; filling it with noise from here
// would blunt it). See buildConfig(), where each conditional says which
// question it is answering.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

FloatingWindow {
    id: root

    title: "Install SynapseOS"
    implicitWidth: 900
    implicitHeight: 640
    // Below this the two-column summary and the disk rows stop fitting, and a
    // layout with no floor does not degrade — it paints over itself.
    minimumSize: Qt.size(760, 560)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYN_INSTALL_BIN") || "syn-install"
    // Where the profile is written. /run is a tmpfs, which is the point: it
    // holds a password until the install reads it and never reaches a disk.
    readonly property string confPath: Quickshell.env("SYN_INSTALL_CONF") || "/run/synapseos/install.conf"

    // ── Theme ───────────────────────────────────────────────────────────────
    //
    // Same source and shape as syn-disks, synfiles and the bar, so a theme
    // switch moves all of them together. The live session may have no
    // theme.json at all, so every colour below has a literal fallback.
    property var p: ({})
    readonly property bool isLight: p.scheme === "light"

    FileView {
        path: (Quickshell.env("HOME") || "/root") + "/.config/synui/theme.json"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: { try { root.p = JSON.parse(this.text()) } catch (e) { root.p = ({}) } }
        onLoadFailed: root.p = ({})
    }

    function themed(key, fallback) {
        const c = root.p[key]
        if (c === undefined || c === null) return fallback
        if (typeof c === "string") return c
        // synui writes {r,g,b} floats in 0..1.
        if (c.r !== undefined) return Qt.rgba(c.r, c.g, c.b, c.a === undefined ? 1 : c.a)
        return fallback
    }

    readonly property color cBg:     themed("bg",     isLight ? "#f2f4f7" : "#12151a")
    readonly property color cPanel:  themed("panel",  isLight ? "#ffffff" : "#1a1f27")
    readonly property color cText:   themed("fg",     isLight ? "#12151a" : "#e6ecf3")
    readonly property color cDim:    themed("dim",    isLight ? "#5a6472" : "#8b97a8")
    readonly property color cAccent: themed("accent", "#33ccff")
    readonly property color cWarn:   themed("warn",   "#ffb454")
    readonly property color cErr:    themed("error",  "#ff5c66")
    readonly property color cLine:   isLight ? "#d3d9e0" : "#2a323d"

    color: cBg

    // ── The answers ─────────────────────────────────────────────────────────
    //
    // Defaults match the text installer's defaults exactly. Where they drift
    // the two installers become two products, and the one people compare
    // against is whichever they used first.
    property string aDisk: ""
    property string aMode: "erase"          // erase | alongside
    property string aFs: "ext4"
    property string aBoot: "grub"
    property bool   aSnapshots: false
    property bool   aEncrypt: false
    property string aLuks: ""
    property string aPreset: "standard"
    property string aModel: "mistral-7b"
    property string aUser: "syn"
    property string aFullname: ""
    property string aPass: ""
    property string aPass2: ""
    property string aDesktop: "synui"
    property string aLocale: "en_US.UTF-8"
    property string aKeymap: "us"
    property string aXkb: "us"
    property string aTz: "UTC"
    property bool   aGpuInference: true

    // Facts discovered about the machine, not chosen.
    property bool   hasNvidia: false
    property string release: ""

    property int page: 0
    readonly property var pageNames: ["Welcome", "Disk", "Software", "Account", "Region", "Summary", "Install"]

    // ── Disk records ────────────────────────────────────────────────────────
    //
    // dev, bytes, size, model, live, usable, reason — straight from
    // `syn-install --list-disks`. `live` marks the installer's own media and
    // `usable` is that plus the minimum size; both are decided there, not here.
    property var disks: []

    Process {
        id: diskProc
        command: [root.bin, "--list-disks"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of this.text.split("\n")) {
                    if (!line.trim()) continue
                    const f = line.split("\t")
                    if (f.length < 6) continue
                    out.push({
                        dev: f[0], bytes: parseInt(f[1]) || 0, size: f[2],
                        model: f[3], live: f[4] === "1", usable: f[5] === "1",
                        reason: f.length > 6 ? f[6] : ""
                    })
                }
                root.disks = out
                // Preselect the first disk that can actually be installed to,
                // never merely the first one: on a live USB the first row is
                // very often the stick itself.
                for (const d of out) { if (d.usable) { root.aDisk = d.dev; break } }
            }
        }
    }

    // The release the ISO says it is. Same source as the text installer's
    // header: /etc/os-release, stamped by archiso/build.sh from iso_version.
    Process {
        id: relProc
        command: ["sh", "-c", ". /etc/os-release 2>/dev/null; [ \"$ID\" = synapseos ] && printf %s \"$VERSION_ID\""]
        running: true
        stdout: StdioCollector { onStreamFinished: root.release = this.text.trim() }
    }

    // gpu_inference is asked ONLY on NVIDIA, so it may only be written on
    // NVIDIA — see the note at the top about not blunting the unused-key
    // report.
    Process {
        id: gpuProc
        command: ["sh", "-c", "lspci 2>/dev/null | grep -qiE 'vga|3d|display' && lspci 2>/dev/null | grep -qi nvidia && echo yes || echo no"]
        running: true
        stdout: StdioCollector { onStreamFinished: root.hasNvidia = this.text.trim() === "yes" }
    }

    // ── Validation ──────────────────────────────────────────────────────────
    //
    // Each page reports why Next is unavailable rather than greying the button
    // out silently. A disabled control with no reason is a bug report.
    function pageProblem(n) {
        if (n === 1) {
            if (!aDisk) return "Choose a disk to install to."
            if (aEncrypt && aLuks.length < 8)
                return "The encryption passphrase needs at least 8 characters."
        }
        if (n === 3) {
            if (!/^[a-z_][a-z0-9_-]*$/.test(aUser))
                return "A username is lower-case letters, digits, - and _, and cannot start with a digit."
            if (aPass.length < 1) return "Set a password for the account."
            if (aPass !== aPass2) return "The two passwords do not match."
        }
        if (n === 4) {
            if (!aLocale.trim()) return "A locale is needed, e.g. en_US.UTF-8."
            if (!aTz.trim()) return "A timezone is needed, e.g. Europe/Lisbon."
        }
        return ""
    }

    function selectedDisk() {
        for (const d of disks) if (d.dev === aDisk) return d
        return null
    }

    // ── The profile ─────────────────────────────────────────────────────────
    //
    // key=value lines, the format syn-install's config_load reads directly.
    // Read the header note before adding a key: every one here has to be a
    // question this run will actually reach.
    function buildConfig() {
        const L = []
        L.push("# Written by syn-install-gui. Answers a graphical run; not meant to be kept.")
        L.push("disk=" + aDisk)
        L.push("install_mode=" + aMode)
        // The destructive confirmation is per-mode and each one is a separate
        // question in the script. Pressing Install on the summary page IS this
        // answer — the summary is the read-back the text installer prints
        // before asking, in the same words.
        L.push(aMode === "erase" ? "confirm_erase=yes" : "confirm_alongside=yes")
        L.push("filesystem=" + aFs)
        L.push("bootloader=" + aBoot)
        // snapshots is only ASKED for btrfs + limine. Writing it anywhere else
        // would land in the unused-key report.
        if (aFs === "btrfs" && aBoot === "limine") L.push("snapshots=" + (aSnapshots ? "yes" : "no"))
        L.push("disk_plan_ok=yes")
        L.push("encrypt=" + (aEncrypt ? "yes" : "no"))
        // luks_passphrase is read only when encrypt=yes, and the form already
        // refuses one under 8 characters, so short_passphrase_ok is never asked.
        if (aEncrypt) L.push("luks_passphrase=" + aLuks)
        L.push("preset=" + aPreset)
        // The model question is asked on every preset except minimal.
        if (aPreset !== "minimal") L.push("ai_model=" + aModel)
        L.push("selection_ok=yes")
        L.push("username=" + aUser)
        L.push("fullname=" + aFullname)
        L.push("password=" + aPass)
        L.push("desktop=" + aDesktop)
        // "other" plus the three explicit keys, never a menu number: a number
        // means whatever that row is on the day it runs.
        L.push("language=other")
        L.push("locale=" + aLocale)
        L.push("keymap=" + aKeymap)
        L.push("xkb_layout=" + aXkb)
        L.push("timezone=" + aTz)
        // Only asked when there is no connection, and the install cannot start
        // without one — answering it keeps a flaky link from stopping on a
        // picker this window cannot draw.
        L.push("wifi_picker=no")
        if (hasNvidia) L.push("gpu_inference=" + (aGpuInference ? "yes" : "no"))
        return L.join("\n") + "\n"
    }

    // ── Running the install ─────────────────────────────────────────────────
    property bool running: false
    property bool finished: false
    property int exitCode: -1
    property string lastLine: ""

    // A ListModel, not a string the view splits. An install prints thousands of
    // lines, and `model: text.split("\n")` rebuilds and re-diffs the entire
    // list on every one of them — the window gets slower the longer the install
    // runs, which is precisely backwards.
    ListModel { id: logModel }

    function appendLog(s) {
        for (const l of String(s).split("\n")) logModel.append({ line: l })
        const t = String(s).trim()
        if (t) lastLine = t
        logView.positionViewAtEnd()
    }

    FileView {
        id: confWriter
        path: root.confPath
        // The file does not exist yet, and FileView will not write a path it
        // has never loaded unless it is allowed to create one.
        preload: false
        atomicWrites: true
    }

    function startInstall() {
        if (running) return
        page = 6
        running = true; finished = false; exitCode = -1
        logModel.clear(); lastLine = "Starting…"
        // mkdir -p, then the profile, then the run. Three steps rather than one
        // shell line so a failure to write says WHICH step failed.
        writeProc.running = true
    }

    Process {
        id: writeProc
        command: ["sh", "-c",
                  "mkdir -p \"$(dirname \"$1\")\" && umask 077 && cat > \"$1\"",
                  "sh", root.confPath]
        running: false
        stdinEnabled: true
        onStarted: {
            write(root.buildConfig())
            stdinEnabled = false     // EOF, or `cat` never returns
        }
        onExited: (code) => {
            if (code !== 0) {
                root.appendLog("Could not write " + root.confPath + " (exit " + code + ")")
                root.running = false; root.finished = true; root.exitCode = code
                return
            }
            root.appendLog("Profile written to " + root.confPath)
            installProc.running = true
        }
    }

    Process {
        id: installProc
        command: [root.bin, "--config", root.confPath]
        running: false
        // SplitParser, not StdioCollector: a collector fires once, when the
        // stream ENDS, which for a twenty-minute install means a blank window
        // for twenty minutes and then everything at once. This is the half of
        // live progress that lives on the reading side.
        stdout: SplitParser { onRead: (data) => root.appendLog(data) }
        stderr: SplitParser { onRead: (data) => root.appendLog(data) }
        onExited: (code) => {
            root.running = false; root.finished = true; root.exitCode = code
            root.lastLine = code === 0 ? "Installation complete." : "Installation failed — see the log."
            // The profile holds the account password and the LUKS passphrase.
            // /run is a tmpfs so it never reached a disk, but it should not sit
            // readable in the live session either.
            shredProc.running = true
        }
    }

    Process {
        id: shredProc
        command: ["sh", "-c", "rm -f \"$1\"", "sh", root.confPath]
        running: false
    }

    // ── Widgets ─────────────────────────────────────────────────────────────
    component Btn: Rectangle {
        id: btn
        property string text: ""
        property bool primary: false
        property bool enabled: true
        signal clicked()
        implicitWidth: label.implicitWidth + 34
        implicitHeight: 34
        radius: 6
        color: !btn.enabled ? Qt.rgba(0.5, 0.5, 0.5, 0.12)
             : btn.primary ? (ma.containsMouse ? Qt.lighter(root.cAccent, 1.15) : root.cAccent)
             : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))
        border.width: btn.primary ? 0 : 1
        border.color: root.cLine
        Text {
            id: label
            anchors.centerIn: parent
            text: btn.text
            font.pixelSize: 14
            font.bold: btn.primary
            color: !btn.enabled ? root.cDim
                 : btn.primary ? (root.isLight ? "#ffffff" : "#0b0f14") : root.cText
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (btn.enabled) btn.clicked()
        }
    }

    component Choice: Rectangle {
        id: ch
        property string text: ""
        property string subtext: ""
        property bool checked: false
        property bool enabled: true
        signal picked()
        implicitHeight: 44
        radius: 6
        color: ch.checked ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.14)
             : cma.containsMouse && ch.enabled ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
        border.width: 1
        border.color: ch.checked ? root.cAccent : root.cLine
        opacity: ch.enabled ? 1 : 0.45
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Rectangle {
                width: 14; height: 14; radius: 7
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"
                border.width: 2
                border.color: ch.checked ? root.cAccent : root.cDim
                Rectangle {
                    anchors.centerIn: parent
                    width: 6; height: 6; radius: 3
                    color: root.cAccent
                    visible: ch.checked
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text { text: ch.text; color: root.cText; font.pixelSize: 14 }
                Text {
                    text: ch.subtext; color: root.cDim; font.pixelSize: 11
                    visible: ch.subtext !== ""
                }
            }
        }
        MouseArea {
            id: cma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: ch.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (ch.enabled) ch.picked()
        }
    }

    component Field: Column {
        id: fld
        property string label: ""
        property string hint: ""
        property alias text: input.text
        property bool secret: false
        // The starting value, assigned ONCE. Binding `text` to the answer it
        // also writes back is a loop with nothing to break it — it settles
        // today only because the values happen to be equal by the time it
        // re-evaluates.
        property string initial: ""
        Component.onCompleted: if (initial !== "") text = initial
        spacing: 4
        width: 300
        Text { text: fld.label; color: root.cDim; font.pixelSize: 12 }
        Rectangle {
            width: parent.width
            height: 32
            radius: 5
            color: root.isLight ? "#ffffff" : Qt.rgba(1, 1, 1, 0.05)
            border.width: 1
            border.color: input.activeFocus ? root.cAccent : root.cLine
            TextInput {
                id: input
                anchors.fill: parent
                anchors.margins: 8
                verticalAlignment: TextInput.AlignVCenter
                color: root.cText
                font.pixelSize: 14
                selectByMouse: true
                echoMode: fld.secret ? TextInput.Password : TextInput.Normal
                clip: true
            }
        }
        Text {
            text: fld.hint; color: root.cDim; font.pixelSize: 11
            visible: fld.hint !== ""
        }
    }

    component Head: Column {
        id: hd
        property string title: ""
        property string blurb: ""
        spacing: 4
        // Addressed through the id, never `parent`: inside a Column, `parent`
        // happens to be this same object, so the two read alike right up until
        // something is nested one level deeper and it silently is not.
        Text { text: hd.title; color: root.cText; font.pixelSize: 20; font.bold: true }
        Text {
            text: hd.blurb; color: root.cDim; font.pixelSize: 13
            visible: hd.blurb !== ""
            width: 640; wrapMode: Text.WordWrap
        }
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // Step rail.
        Row {
            id: rail
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            spacing: 6
            Repeater {
                model: root.pageNames
                Row {
                    spacing: 6
                    Rectangle {
                        width: 22; height: 22; radius: 11
                        anchors.verticalCenter: parent.verticalCenter
                        color: index === root.page ? root.cAccent
                             : index < root.page ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.35)
                             : "transparent"
                        border.width: 1
                        border.color: index <= root.page ? root.cAccent : root.cLine
                        Text {
                            anchors.centerIn: parent
                            text: index < root.page ? "✓" : String(index + 1)
                            font.pixelSize: 11
                            color: index === root.page ? (root.isLight ? "#ffffff" : "#0b0f14") : root.cText
                        }
                    }
                    Text {
                        text: modelData
                        anchors.verticalCenter: parent.verticalCenter
                        color: index === root.page ? root.cText : root.cDim
                        font.pixelSize: 12
                        font.bold: index === root.page
                    }
                    Rectangle {
                        width: 18; height: 1
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.cLine
                        visible: index < root.pageNames.length - 1
                    }
                }
            }
        }

        Rectangle {
            id: body
            anchors.top: rail.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: foot.top
            anchors.margins: 16
            radius: 8
            color: root.cPanel
            border.width: 1
            border.color: root.cLine
            clip: true

            // ── 0: Welcome ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 16
                visible: root.page === 0
                Head {
                    title: "Install SynapseOS" + (root.release ? " " + root.release : "")
                    blurb: "This asks for a disk, an account and a few preferences, then hands "
                         + "the answers to the same installer the text version runs. Nothing "
                         + "is written to any disk until the last page, and that page says "
                         + "exactly what it is about to do."
                }
                Column {
                    spacing: 8
                    Repeater {
                        model: [
                            "A disk is partitioned and formatted",
                            "The base system and the SynapseOS packages are installed",
                            "An account and a desktop are set up",
                            "A bootloader is written"
                        ]
                        Row {
                            spacing: 8
                            Text { text: "•"; color: root.cAccent; font.pixelSize: 14 }
                            Text { text: modelData; color: root.cText; font.pixelSize: 14 }
                        }
                    }
                }
                Rectangle {
                    width: parent.width; height: 1; color: root.cLine
                }
                Text {
                    text: "Partitioning an existing layout by hand is the text installer's "
                        + "ADVANCED mode — quit this and run `syn-install` in a terminal for that."
                    color: root.cDim; font.pixelSize: 12
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }

            // ── 1: Disk ─────────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                visible: root.page === 1
                Head {
                    title: "Where should SynapseOS go?"
                    blurb: "The installer's own media is listed and cannot be chosen."
                }
                Column {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: root.disks
                        Choice {
                            width: parent.width
                            text: modelData.dev + "   " + modelData.size
                            subtext: modelData.usable ? modelData.model
                                                      : modelData.model + " — " + modelData.reason
                            enabled: modelData.usable
                            checked: root.aDisk === modelData.dev
                            onPicked: root.aDisk = modelData.dev
                        }
                    }
                    Text {
                        text: "No disks found."
                        color: root.cErr; font.pixelSize: 13
                        visible: root.disks.length === 0
                    }
                }
                Row {
                    spacing: 22
                    Column {
                        spacing: 6
                        Text { text: "Mode"; color: root.cDim; font.pixelSize: 12 }
                        Row {
                            spacing: 6
                            Choice {
                                width: 190; text: "Erase the disk"
                                subtext: "every partition and all data"
                                checked: root.aMode === "erase"
                                onPicked: root.aMode = "erase"
                            }
                            Choice {
                                width: 190; text: "Install alongside"
                                subtext: "use free space, UEFI only"
                                checked: root.aMode === "alongside"
                                onPicked: root.aMode = "alongside"
                            }
                        }
                    }
                }
                Row {
                    spacing: 22
                    Column {
                        spacing: 6
                        Text { text: "Filesystem"; color: root.cDim; font.pixelSize: 12 }
                        Row {
                            spacing: 6
                            Repeater {
                                model: ["ext4", "btrfs", "xfs", "f2fs"]
                                Choice {
                                    width: 92; text: modelData
                                    checked: root.aFs === modelData
                                    onPicked: root.aFs = modelData
                                }
                            }
                        }
                    }
                    Column {
                        spacing: 6
                        Text { text: "Bootloader"; color: root.cDim; font.pixelSize: 12 }
                        Row {
                            spacing: 6
                            Repeater {
                                model: ["grub", "systemd-boot", "limine"]
                                Choice {
                                    width: 118; text: modelData
                                    checked: root.aBoot === modelData
                                    onPicked: root.aBoot = modelData
                                }
                            }
                        }
                    }
                }
                Row {
                    spacing: 18
                    Choice {
                        width: 240
                        text: "Snapshots"
                        subtext: "btrfs + limine only"
                        enabled: root.aFs === "btrfs" && root.aBoot === "limine"
                        checked: root.aSnapshots && enabled
                        onPicked: root.aSnapshots = !root.aSnapshots
                    }
                    Choice {
                        width: 240
                        text: "Encrypt the disk"
                        subtext: "LUKS2"
                        checked: root.aEncrypt
                        onPicked: root.aEncrypt = !root.aEncrypt
                    }
                    Field {
                        width: 240
                        label: "Passphrase"
                        secret: true
                        visible: root.aEncrypt
                        hint: "8 characters or more"
                        onTextChanged: root.aLuks = text
                    }
                }
            }

            // ── 2: Software ─────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                visible: root.page === 2
                Head {
                    title: "What should be installed?"
                    blurb: "The SynapseOS core — the compositor, the daemons and the "
                         + "applications it is built on — is installed by every choice here."
                }
                Column {
                    width: parent.width
                    spacing: 6
                    Choice {
                        width: parent.width; text: "Full"
                        subtext: "Standard + Steam + Nix + Nexus Chat + TEPRIS"
                        checked: root.aPreset === "full"
                        onPicked: root.aPreset = "full"
                    }
                    Choice {
                        width: parent.width; text: "Standard"
                        subtext: "AI model, Bluetooth, printing, Wine, phone, Chibi + Vibe + Arsenal"
                        checked: root.aPreset === "standard"
                        onPicked: root.aPreset = "standard"
                    }
                    Choice {
                        width: parent.width; text: "Minimal"
                        subtext: "core daemons only — no apps, no model, none of the above"
                        checked: root.aPreset === "minimal"
                        onPicked: root.aPreset = "minimal"
                    }
                }
                Column {
                    spacing: 6
                    visible: root.aPreset !== "minimal"
                    Text { text: "AI model — downloaded during the install"; color: root.cDim; font.pixelSize: 12 }
                    Row {
                        spacing: 6
                        Choice {
                            width: 190; text: "Mistral 7B"; subtext: "~4.1 GB — recommended"
                            checked: root.aModel === "mistral-7b"
                            onPicked: root.aModel = "mistral-7b"
                        }
                        Choice {
                            width: 170; text: "Phi-3 Mini"; subtext: "~2.2 GB — weaker"
                            checked: root.aModel === "phi3"
                            onPicked: root.aModel = "phi3"
                        }
                        Choice {
                            width: 170; text: "Qwen2 0.5B"; subtext: "~0.4 GB — much weaker"
                            checked: root.aModel === "tiny"
                            onPicked: root.aModel = "tiny"
                        }
                        Choice {
                            width: 150; text: "None"; subtext: "AI stays inert"
                            checked: root.aModel === "none"
                            onPicked: root.aModel = "none"
                        }
                    }
                }
                Choice {
                    width: 340
                    visible: root.hasNvidia
                    text: "NVIDIA GPU inference"
                    subtext: "the CUDA runtime, ~4.7 GiB"
                    checked: root.aGpuInference
                    onPicked: root.aGpuInference = !root.aGpuInference
                }
            }

            // ── 3: Account ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                visible: root.page === 3
                Head { title: "Who is this machine for?" }
                Row {
                    spacing: 18
                    Field {
                        label: "Username"; initial: root.aUser
                        hint: "lower-case, no spaces"
                        onTextChanged: root.aUser = text
                    }
                    Field {
                        label: "Full name (optional)"; initial: root.aFullname
                        onTextChanged: root.aFullname = text
                    }
                }
                Row {
                    spacing: 18
                    Field {
                        label: "Password"; secret: true
                        onTextChanged: root.aPass = text
                    }
                    Field {
                        label: "Password again"; secret: true
                        hint: root.aPass2.length > 0 && root.aPass !== root.aPass2 ? "They do not match" : ""
                        onTextChanged: root.aPass2 = text
                    }
                }
                Column {
                    spacing: 6
                    Text { text: "Desktop"; color: root.cDim; font.pixelSize: 12 }
                    Row {
                        spacing: 6
                        Choice {
                            width: 200; text: "SynapseUI"; subtext: "the native compositor"
                            checked: root.aDesktop === "synui"
                            onPicked: root.aDesktop = "synui"
                        }
                        Choice {
                            width: 150; text: "KDE Plasma"
                            checked: root.aDesktop === "kde"
                            onPicked: root.aDesktop = "kde"
                        }
                        Choice {
                            width: 130; text: "GNOME"
                            checked: root.aDesktop === "gnome"
                            onPicked: root.aDesktop = "gnome"
                        }
                        Choice {
                            width: 130; text: "None"; subtext: "headless"
                            checked: root.aDesktop === "tty"
                            onPicked: root.aDesktop = "tty"
                        }
                    }
                }
            }

            // ── 4: Region ───────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                visible: root.page === 4
                Head {
                    title: "Language, keyboard and time"
                    blurb: "The console keymap and the desktop layout are separate on purpose: "
                         + "Swedish is 'sv-latin1' to the console and 'se' to the desktop, and "
                         + "one answer for both is how four of them ended up wrong."
                }
                Row {
                    spacing: 18
                    Field {
                        label: "Locale"; initial: root.aLocale
                        hint: "e.g. en_US.UTF-8"
                        onTextChanged: root.aLocale = text
                    }
                    Field {
                        label: "Timezone"; initial: root.aTz
                        hint: "a tzdata name, e.g. Europe/Lisbon"
                        onTextChanged: root.aTz = text
                    }
                }
                Row {
                    spacing: 18
                    Field {
                        label: "Console keymap"; initial: root.aKeymap
                        hint: "loadkeys name, e.g. sv-latin1"
                        onTextChanged: root.aKeymap = text
                    }
                    Field {
                        label: "Desktop layout"; initial: root.aXkb
                        hint: "XKB name, e.g. se"
                        onTextChanged: root.aXkb = text
                    }
                }
            }

            // ── 5: Summary ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12
                visible: root.page === 5
                Head {
                    title: "Read this back"
                    blurb: "Nothing has been written yet. The next button is the one that starts."
                }
                Rectangle {
                    width: parent.width
                    height: 44
                    radius: 6
                    color: Qt.rgba(root.cErr.r, root.cErr.g, root.cErr.b, 0.14)
                    border.width: 1
                    border.color: root.cErr
                    Text {
                        anchors.centerIn: parent
                        text: root.aMode === "erase"
                              ? "EVERY PARTITION ON " + root.aDisk + " WILL BE DELETED"
                              : "SynapseOS will be installed into the free space on " + root.aDisk
                        color: root.cText
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
                Grid {
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 6
                    Repeater {
                        model: [
                            ["Disk", root.aDisk + "  (" + (root.selectedDisk() ? root.selectedDisk().size : "?") + ")"],
                            ["Mode", root.aMode],
                            ["Filesystem", root.aFs + (root.aEncrypt ? " on LUKS2" : "")],
                            ["Bootloader", root.aBoot + (root.aFs === "btrfs" && root.aBoot === "limine" && root.aSnapshots ? " + snapshots" : "")],
                            ["Install", root.aPreset],
                            ["AI model", root.aPreset === "minimal" ? "none" : root.aModel],
                            ["Account", root.aUser + (root.aFullname ? "  (" + root.aFullname + ")" : "")],
                            ["Desktop", root.aDesktop],
                            ["Locale", root.aLocale + "   keys " + root.aKeymap + " / " + root.aXkb],
                            ["Timezone", root.aTz]
                        ]
                        Row {
                            spacing: 10
                            Text {
                                text: modelData[0]; color: root.cDim
                                font.pixelSize: 13; width: 96
                            }
                            Text { text: modelData[1]; color: root.cText; font.pixelSize: 13 }
                        }
                    }
                }
            }

            // ── 6: Install ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12
                visible: root.page === 6
                Head {
                    title: root.finished ? (root.exitCode === 0 ? "SynapseOS is installed" : "The install stopped")
                                         : "Installing SynapseOS"
                    blurb: root.finished && root.exitCode === 0
                           ? "Reboot and remove the installation media."
                           : root.finished ? "The log below is the whole story — the last lines say why."
                           : "This takes a while: the base system and the packages are downloaded, "
                           + "and an AI model is gigabytes on its own."
                }
                Rectangle {
                    width: parent.width
                    height: 28
                    radius: 4
                    color: Qt.rgba(1, 1, 1, 0.05)
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.lastLine
                        color: root.finished && root.exitCode !== 0 ? root.cErr : root.cText
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: parent.width - 20
                    }
                }
                Rectangle {
                    width: parent.width
                    height: parent.height - y
                    radius: 6
                    color: root.isLight ? "#ffffff" : Qt.rgba(0, 0, 0, 0.25)
                    border.width: 1
                    border.color: root.cLine
                    clip: true
                    ListView {
                        id: logView
                        anchors.fill: parent
                        anchors.margins: 8
                        model: logModel
                        clip: true
                        delegate: Text {
                            required property string line
                            text: line
                            color: root.cDim
                            font.family: "monospace"
                            font.pixelSize: 11
                            width: logView.width
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // ── Footer ──────────────────────────────────────────────────────────
        Item {
            id: foot
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 16
            height: 40

            Text {
                id: problem
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.pageProblem(root.page)
                color: root.cWarn
                font.pixelSize: 12
                width: parent.width - 320
                elide: Text.ElideRight
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Btn {
                    text: "Back"
                    visible: root.page > 0 && root.page < 6
                    onClicked: root.page--
                }
                Btn {
                    text: "Next"
                    primary: true
                    visible: root.page < 5
                    enabled: root.pageProblem(root.page) === ""
                    onClicked: root.page++
                }
                Btn {
                    text: "Install"
                    primary: true
                    visible: root.page === 5
                    onClicked: root.startInstall()
                }
                Btn {
                    text: "Reboot"
                    primary: true
                    visible: root.page === 6 && root.finished && root.exitCode === 0
                    onClicked: rebootProc.running = true
                }
                Btn {
                    text: "Close"
                    visible: root.page === 6 && root.finished
                    onClicked: Qt.quit()
                }
            }
        }
    }

    Process {
        id: rebootProc
        command: ["systemctl", "reboot"]
        running: false
    }
}
