// syn-mouse.qml — the Mouse Buttons window.
//
// ⛔ EVERY CHANGE IS THE BINARY'S. This file runs `syn-mouse --rec profiles`,
// `status`, `devices`, `apps` and `keys` and draws what comes back, and every
// binding it makes is a `syn-mouse bind …` command. It never opens the
// bindings file — a second writer of that file would be a second set of rules
// about what a binding may be, and the left-button rule is one a window must
// not be able to get around.
//
// SynapseOS Project — GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

// ⛔ THE TRANSLATION SINGLETON, AND IT IS NOT qsTr(). quickshell 0.3.1 installs
// no QTranslator, so qsTr() compiles, looks up nothing and returns its own
// argument — qml/I18n.qml reads a JSON catalog compiled from the same po/ the
// CLI's .mo comes from.
import "qml"

ShellRoot {
    id: root

    readonly property string bin: Quickshell.env("SYNMOUSE_BIN") || "syn-mouse"

    // ── what the binary said ────────────────────────────────────────────────
    property var profiles: []          // [{name, app, title, everywhere}]
    property var binds: ({})           // profile -> button -> {mode, keys, ms}
    property var conf: ({ notify: "on", device: "" })
    property var buttons: []           // [{name, label}] — every button name
    property var mice: []              // [{name, node, inputs: [..], readable}]
    property var apps: []              // [{app, title}]
    property var keyNames: ({})        // code -> canonical name
    property var st: ({ running: false, focusKind: 0, app: "", title: "",
                        profile: "", mice: [], problems: [], active: [] })

    // ── what the person is doing ────────────────────────────────────────────
    property string page: "bind"       // "bind" | "new"
    property string cur: ""            // selected profile name
    property string sel: ""            // selected button name
    property string edMode: "none"
    property string edKeys: ""
    property string edSecs: "1"
    property bool capturing: false
    property var capMods: []
    property string message: ""
    property bool messageBad: false
    property bool confirmDelete: false
    property string newName: ""
    property string newApp: ""

    function disp(s) {
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    // ── the desktop's font and text size ────────────────────────────────────
    //
    // ⛔ NOT THIS WINDOW'S SETTING. The family and the scale are properties of
    // the desktop, in the file the bar and every sibling app watch.
    property string uiFont: ""
    property int textScale: 100
    function ui(n) { return Math.round(n * root.textScale / 100) }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = this.text()
            const m = t.match(/^\s*family\s*=\s*(.+?)\s*$/m)
            root.uiFont = m ? m[1] : ""
            const sc = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            root.textScale = sc ? parseInt(sc[1]) : 100
        }
        onLoadFailed: { root.uiFont = ""; root.textScale = 100 }
    }

    // ── palette ─────────────────────────────────────────────────────────────
    readonly property color cBg:     "#16171c"
    readonly property color cPanel:  "#1d1f26"
    readonly property color cText:   "#e9eaef"
    readonly property color cDim:    "#9aa0ad"
    readonly property color cAccent: "#5b8dd9"
    readonly property color cGood:   "#9ece6a"
    readonly property color cWarn:   "#e0af68"
    readonly property color cBad:    "#f7768e"
    readonly property color cWash:   Qt.rgba(1, 1, 1, 0.12)

    // ── reading ─────────────────────────────────────────────────────────────

    function rows(text) {
        return text.split("\n").filter(l => l.length > 0).map(l => l.split("\t"))
    }

    Process {
        id: profilesProc
        command: [root.bin, "--rec", "profiles"]
        stdout: StdioCollector {
            onStreamFinished: {
                const ps = [], bs = {}, set = { notify: "on", device: "" }
                for (const f of root.rows(text)) {
                    if (f[0] === "setting" && f.length >= 3) set[f[1]] = root.disp(f[2])
                    if (f[0] === "profile" && f.length >= 5) {
                        const name = root.disp(f[1])
                        ps.push({ name: name, app: root.disp(f[2]), title: root.disp(f[3]),
                                  everywhere: f[4] === "1" })
                        bs[name] = {}
                    }
                    if (f[0] === "bind" && f.length >= 6) {
                        const pn = root.disp(f[1])
                        if (!bs[pn]) bs[pn] = {}
                        bs[pn][f[2]] = { mode: f[3], keys: root.disp(f[4]), ms: parseInt(f[5]) || 0 }
                    }
                }
                root.profiles = ps
                root.binds = bs
                root.conf = set
                if (!ps.some(p => p.name === root.cur))
                    root.cur = ps.length ? ps[0].name : ""
                if (!ps.length && root.page === "bind") root.openNew()
                root.loadEditor()
            }
        }
    }

    Process {
        id: statusProc
        command: [root.bin, "--rec", "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                const s = { running: false, focusKind: 0, app: "", title: "",
                            profile: "", mice: [], problems: [], active: [] }
                for (const f of root.rows(text)) {
                    if (f[0] === "daemon") s.running = f[1] !== "0"
                    if (f[0] === "focus" && f.length >= 4) {
                        s.focusKind = parseInt(f[1]) || 0
                        s.app = root.disp(f[2]); s.title = root.disp(f[3])
                    }
                    if (f[0] === "profile") s.profile = root.disp(f[1] || "")
                    if (f[0] === "mouse" && f.length >= 4)
                        s.mice.push({ name: root.disp(f[1]), taken: f[3] === "1" })
                    if (f[0] === "problem") s.problems.push(f[1])
                    if (f[0] === "active" && f.length >= 7)
                        s.active.push({ profile: root.disp(f[1]), button: f[2], mode: f[3],
                                        keys: root.disp(f[4]), ms: parseInt(f[5]) || 0,
                                        running: f[6] === "1" })
                }
                root.st = s
            }
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: { if (!statusProc.running) statusProc.running = true }
    }

    Process {
        id: devicesProc
        command: [root.bin, "--rec", "devices"]
        stdout: StdioCollector {
            onStreamFinished: {
                const ms = []
                for (const f of root.rows(text))
                    if (f[0] === "mouse" && f.length >= 5)
                        ms.push({ name: root.disp(f[1]), node: f[2],
                                  inputs: f[3].split(",").filter(x => x.length > 0),
                                  readable: f[4] === "1" })
                root.mice = ms
            }
        }
    }

    Process {
        id: buttonsProc
        command: [root.bin, "--rec", "buttons"]
        stdout: StdioCollector {
            onStreamFinished: {
                const bs = []
                for (const f of root.rows(text))
                    if (f[0] === "button" && f.length >= 3)
                        bs.push({ name: f[1], label: root.disp(f[2]) })
                root.buttons = bs
            }
        }
    }

    Process {
        id: keysProc
        command: [root.bin, "--rec", "keys"]
        stdout: StdioCollector {
            onStreamFinished: {
                const k = {}
                for (const f of root.rows(text))
                    if (f[0] === "key" && f.length >= 3 && k[f[2]] === undefined) k[f[2]] = f[1]
                root.keyNames = k
            }
        }
    }

    Process {
        id: appsProc
        command: [root.bin, "--rec", "apps"]
        stdout: StdioCollector {
            onStreamFinished: {
                const seen = {}, out = []
                for (const f of root.rows(text)) {
                    if (f[0] !== "app" || f.length < 3) continue
                    const a = root.disp(f[1])
                    if (seen[a] || a === "syn-mouse") continue
                    seen[a] = true
                    out.push({ app: a, title: root.disp(f[2]) })
                }
                root.apps = out
            }
        }
    }

    // ── changing: every change is one command ───────────────────────────────

    property var queue: []

    Process {
        id: runProc
        stderr: StdioCollector {
            id: runErr
        }
        onExited: (code) => {
            const err = runErr.text.trim()
            if (code !== 0 && err.length) { root.message = err; root.messageBad = true }
            root.queue = root.queue.slice(1)
            if (root.queue.length) root.startNext()
            else { profilesProc.running = true; statusProc.running = true }
        }
    }

    function startNext() {
        runProc.command = [root.bin, "--rec"].concat(root.queue[0])
        runProc.running = true
    }

    function run(args, okMessage) {
        root.message = okMessage || ""
        root.messageBad = false
        root.queue = root.queue.concat([args])
        if (root.queue.length === 1) root.startNext()
    }

    Component.onCompleted: {
        profilesProc.running = true
        statusProc.running = true
        devicesProc.running = true
        buttonsProc.running = true
        keysProc.running = true
    }

    // ── the editor ──────────────────────────────────────────────────────────

    function bindOf(profile, button) {
        const p = root.binds[profile]
        return p && p[button] ? p[button] : null
    }

    function loadEditor() {
        const b = root.bindOf(root.cur, root.sel)
        root.edMode = b ? b.mode : "none"
        root.edKeys = b ? b.keys : ""
        root.edSecs = b && b.ms ? root.secs(b.ms) : "1"
        root.capturing = false
        root.confirmDelete = false
    }

    function secs(ms) {
        return String(ms / 1000)
    }

    readonly property var steps: [0.1, 0.25, 0.5, 1, 1.5, 2, 3, 4, 5, 8, 10, 15, 20, 30, 60]
    function stepSecs(dir) {
        const v = parseFloat(root.edSecs.replace(",", ".")) || 1
        let best = dir > 0 ? root.steps[root.steps.length - 1] : root.steps[0]
        for (const s of root.steps) {
            if (dir > 0 && s > v + 1e-9) { best = s; break }
        }
        if (dir < 0) for (const s of root.steps) if (s < v - 1e-9) best = s
        root.edSecs = String(best)
    }

    readonly property var modes: [
        { id: "none",   label: I18n.tr("Normal"),
          help: I18n.tr("The button does what it always did.") },
        { id: "key",    label: I18n.tr("Press a key"),
          help: I18n.tr("The key is held down for as long as you hold the button.") },
        { id: "repeat", label: I18n.tr("Repeat while held"),
          help: I18n.tr("Pressed again every few seconds for as long as you hold the button.") },
        { id: "toggle", label: I18n.tr("Auto-press toggle"),
          help: I18n.tr("One click starts pressing the key every few seconds; the next click stops it. It pauses while you are in another window.") },
        { id: "latch",  label: I18n.tr("Hold-down toggle"),
          help: I18n.tr("One click holds the key down; the next click lets go.") },
        { id: "off",    label: I18n.tr("Disabled"),
          help: I18n.tr("The button does nothing at all.") }
    ]

    function needsKeys(m) { return m === "key" || m === "repeat" || m === "toggle" || m === "latch" }
    function needsSecs(m) { return m === "repeat" || m === "toggle" }

    function save() {
        if (!root.cur || !root.sel) return
        if (root.edMode === "none") {
            root.run(["unbind", root.cur, root.sel], I18n.tr("Saved."))
            return
        }
        if (root.edMode === "off") {
            root.run(["bind", root.cur, root.sel, "off"], I18n.tr("Saved."))
            return
        }
        if (!root.edKeys) {
            root.message = I18n.tr("Choose a key first: click the key box and press it.")
            root.messageBad = true
            return
        }
        const args = ["bind", root.cur, root.sel, root.edMode, root.edKeys]
        if (root.needsSecs(root.edMode)) args.push("every", root.edSecs.replace(",", "."))
        root.run(args, I18n.tr("Saved."))
    }

    // ⚠ A KEY IS CAPTURED BY POSITION, NOT BY ITS LETTER. Qt's key code is the
    // character the layout makes; the bindings are evdev codes, which the
    // compositor turns into characters itself. On Wayland nativeScanCode is
    // the XKB keycode, evdev + 8, and that is what goes to the binary.
    readonly property var modCodes: [29, 42, 54, 56, 97, 100, 125, 126]

    function codeName(c) {
        return root.keyNames[String(c)] || ("code:" + c)
    }

    function capturePress(ev) {
        const code = ev.nativeScanCode - 8
        if (code <= 0 || code > 255) return
        ev.accepted = true
        if (root.modCodes.indexOf(code) >= 0) {
            if (root.capMods.indexOf(code) < 0) root.capMods = root.capMods.concat([code])
            return
        }
        root.finishCapture(root.capMods.concat([code]))
    }

    function captureRelease(ev) {
        const code = ev.nativeScanCode - 8
        ev.accepted = true
        // A modifier pressed and let go on its own is the binding: "shift".
        if (root.capMods.length && root.capMods.indexOf(code) >= 0)
            root.finishCapture(root.capMods)
    }

    function finishCapture(codes) {
        root.edKeys = codes.map(c => root.codeName(c)).join("+")
        root.capMods = []
        root.capturing = false
        if (root.edMode === "none" || root.edMode === "off") root.edMode = "key"
    }

    function openNew() {
        root.page = "new"
        root.newName = ""
        root.newApp = ""
        appsProc.running = true
    }

    function createProfile() {
        const n = root.newName.trim()
        if (!n) {
            root.message = I18n.tr("Give the profile a name.")
            root.messageBad = true
            return
        }
        const args = ["add", n]
        if (root.newApp) args.push("--app", root.newApp)
        root.run(args, "")
        root.cur = n
        root.sel = ""
        root.page = "bind"
    }

    // A title, made into a name the file accepts: no brackets, no tabs.
    function nameFrom(title) {
        return title.replace(/[\[\]\t\r\n]/g, " ").replace(/\s+/g, " ").trim().slice(0, 60)
    }

    // ── what a binding says, in words ───────────────────────────────────────

    function summary(b) {
        if (!b) return I18n.tr("Normal")
        const s = root.secs(b.ms)
        if (b.mode === "key")    return I18n.tr("Presses %1").arg(b.keys)
        if (b.mode === "repeat") return I18n.tr("Repeats %1 every %2 s while held").arg(b.keys).arg(s)
        if (b.mode === "toggle") return I18n.tr("Click: auto-press %1 every %2 s").arg(b.keys).arg(s)
        if (b.mode === "latch")  return I18n.tr("Click: hold %1 down").arg(b.keys)
        if (b.mode === "off")    return I18n.tr("Disabled")
        return b.mode
    }

    function activeFor(profile, button) {
        for (const a of root.st.active)
            if (a.profile === profile && a.button === button) return a
        return null
    }

    readonly property var shownButtons: {
        let have = {}
        for (const m of root.mice)
            for (const i of m.inputs) have[i] = true
        if (!Object.keys(have).length)
            for (const i of ["left", "right", "middle", "back", "forward", "wheelup", "wheeldown"])
                have[i] = true
        return root.buttons.filter(b => have[b.name])
    }

    readonly property var curProfile: {
        for (const p of root.profiles) if (p.name === root.cur) return p
        return null
    }

    // ── shared bits ─────────────────────────────────────────────────────────

    component Chip: Rectangle {
        id: chip
        property string label: ""
        property bool on: false
        property color tint: root.cAccent
        signal picked()
        width: chipTxt.implicitWidth + 20
        height: root.ui(28)
        radius: 6
        color: chip.on ? chip.tint : (chipMa.containsMouse ? root.cPanel : "transparent")
        border { width: 1; color: chip.on ? chip.tint : root.cDim }
        Text {
            id: chipTxt
            anchors.centerIn: parent
            text: chip.label
            color: chip.on ? root.cPanel : root.cText
            font { family: root.uiFont; pixelSize: root.ui(12) }
        }
        MouseArea {
            id: chipMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.picked()
        }
    }

    component Btn: Rectangle {
        id: btn
        property string label: ""
        property color tint: root.cAccent
        signal pressed()
        width: btnTxt.implicitWidth + 28
        height: root.ui(32)
        radius: 6
        color: btnMa.containsMouse ? btn.tint : "transparent"
        border { width: 1; color: btn.tint }
        Text {
            id: btnTxt
            anchors.centerIn: parent
            text: btn.label
            color: btnMa.containsMouse ? root.cPanel : btn.tint
            font { family: root.uiFont; pixelSize: root.ui(12) }
        }
        MouseArea {
            id: btnMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.pressed()
        }
    }

    // ── the editor, opened under the button it changes ──────────────────────
    Component {
        id: editorComp
        Rectangle {
            width: parent ? parent.width : 0
            height: edCol.implicitHeight + 24
            radius: 8
            color: root.cPanel
            border { width: 1; color: root.cAccent }

            Column {
                id: edCol
                x: 12; y: 12
                width: parent.width - 24
                spacing: 10

                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: root.modes
                        delegate: Chip {
                            required property var modelData
                            label: modelData.label
                            on: root.edMode === modelData.id
                            onPicked: root.edMode = modelData.id
                        }
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
                        for (const m of root.modes) if (m.id === root.edMode) return m.help
                        return ""
                    }
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }

                Row {
                    visible: root.needsKeys(root.edMode)
                    spacing: 8
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: I18n.tr("Key")
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    Rectangle {
                        id: capBox
                        width: root.ui(260); height: root.ui(32); radius: 6
                        color: root.capturing ? Qt.rgba(0.36, 0.55, 0.85, 0.15) : root.cBg
                        border { width: 1; color: root.capturing ? root.cAccent : root.cDim }
                        focus: root.capturing
                        Text {
                            anchors.centerIn: parent
                            text: root.capturing ? I18n.tr("Press a key or a combination…")
                                : root.edKeys ? root.edKeys
                                : I18n.tr("Click here, then press a key")
                            color: root.capturing || !root.edKeys ? root.cDim : root.cText
                            font { family: root.uiFont; pixelSize: root.ui(12); bold: root.edKeys !== "" && !root.capturing }
                        }
                        Keys.onPressed: (ev) => { if (root.capturing) root.capturePress(ev) }
                        Keys.onReleased: (ev) => { if (root.capturing) root.captureRelease(ev) }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.capturing = !root.capturing
                                root.capMods = []
                                if (root.capturing) capBox.forceActiveFocus()
                            }
                        }
                    }
                }

                Flow {
                    visible: root.needsKeys(root.edMode)
                    width: parent.width
                    spacing: 6
                    Text {
                        text: I18n.tr("or a mouse click:")
                        color: root.cDim
                        height: root.ui(28)
                        verticalAlignment: Text.AlignVCenter
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }
                    Repeater {
                        model: [ { k: "mouse1", label: I18n.tr("Left click") },
                                 { k: "mouse2", label: I18n.tr("Right click") },
                                 { k: "mouse3", label: I18n.tr("Middle click") },
                                 { k: "mouse4", label: I18n.tr("Back") },
                                 { k: "mouse5", label: I18n.tr("Forward") } ]
                        delegate: Chip {
                            required property var modelData
                            label: modelData.label
                            on: root.edKeys === modelData.k
                            onPicked: { root.edKeys = modelData.k; root.capturing = false }
                        }
                    }
                }

                Row {
                    visible: root.needsSecs(root.edMode)
                    spacing: 8
                    // ⚠ ONE LABEL BEFORE THE FIELD, not "Every [5] seconds":
                    // a sentence split around an input has English word order
                    // baked into the layout.
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: I18n.tr("Interval in seconds")
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    Chip { label: "−"; onPicked: root.stepSecs(-1) }
                    Rectangle {
                        width: root.ui(70); height: root.ui(28); radius: 6
                        color: root.cBg
                        border { width: 1; color: secIn.activeFocus ? root.cAccent : root.cDim }
                        TextInput {
                            id: secIn
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter
                            horizontalAlignment: TextInput.AlignHCenter
                            text: root.edSecs
                            onTextEdited: root.edSecs = text
                            color: root.cText
                            maximumLength: 8
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }
                    }
                    Chip { label: "+"; onPicked: root.stepSecs(1) }
                }

                Btn {
                    // ⛔ A BUTTON IS ITS OWN LABEL: it names the
                    // button it is about to change.
                    label: {
                        for (const b of root.buttons)
                            // i18n-dynamic: the labels are marked N_() in src/keys.c
                            if (b.name === root.sel) return I18n.tr("Save for %1").arg(I18n.tr(b.label))
                        return I18n.tr("Save")
                    }
                    onPressed: root.save()
                }
            }
        }
    }

    // ── the window ──────────────────────────────────────────────────────────

    FloatingWindow {
        title: I18n.tr("Mouse Buttons")
        implicitWidth: root.ui(720)
        implicitHeight: root.ui(680)
        color: root.cBg

        Flickable {
            id: page
            anchors.fill: parent
            contentWidth: width
            contentHeight: body.implicitHeight + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            // A view that scrolls says so.
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: root.cDim
                    opacity: 0.7
                }
            }

            Column {
                id: body
                x: 16; y: 16
                width: page.width - 32
                spacing: 12

                // ── the service ─────────────────────────────────────────
                Rectangle {
                    width: parent.width
                    height: statusCol.implicitHeight + 20
                    radius: 8
                    color: root.cPanel
                    border { width: 1; color: root.cWash }

                    Column {
                        id: statusCol
                        x: 12; y: 10
                        width: parent.width - 24
                        spacing: 6

                        Row {
                            spacing: 8
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                anchors.verticalCenter: parent.verticalCenter
                                color: !root.st.running ? root.cBad
                                     : (root.st.profile ? root.cGood : root.cDim)
                            }
                            Text {
                                text: !root.st.running ? I18n.tr("Not running — your bindings do nothing until it is on.")
                                    : root.st.profile ? I18n.tr("In force: %1").arg(root.st.profile)
                                    : I18n.tr("Running. Nothing in force — the mouse is as it always is.")
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
                            }
                        }

                        Text {
                            visible: root.st.running && root.st.mice.length > 0
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: root.st.mice.map(m => m.taken ? I18n.tr("%1 (taken over)").arg(m.name)
                                                               : m.name).join(" · ")
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }

                        Repeater {
                            model: root.st.problems
                            delegate: Text {
                                required property var modelData
                                width: statusCol.width
                                wrapMode: Text.WordWrap
                                text: modelData === "uinput"
                                      ? I18n.tr("Cannot create input devices. This account needs to be in the input group: sudo usermod -aG input $USER, then log out and back in.")
                                      : I18n.tr("No mouse it can read. If one is plugged in, this account needs to be in the input group: sudo usermod -aG input $USER, then log out and back in.")
                                color: root.cWarn
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                        }

                        Repeater {
                            model: root.st.active
                            delegate: Text {
                                required property var modelData
                                width: statusCol.width
                                wrapMode: Text.WordWrap
                                // ⛔ A WHOLE SENTENCE PER CASE: "(paused)" glued
                                // onto a translated sentence is a fragment no
                                // language can place.
                                text: modelData.mode === "toggle"
                                      ? (modelData.running
                                         ? I18n.tr("On: %1 is pressing %2 every %3 s")
                                         : I18n.tr("Paused until its app is in front: %1 pressing %2 every %3 s"))
                                            .arg(modelData.button).arg(modelData.keys).arg(root.secs(modelData.ms))
                                      : (modelData.running
                                         ? I18n.tr("On: %1 is holding %2 down")
                                         : I18n.tr("Paused until its app is in front: %1 holding %2 down"))
                                            .arg(modelData.button).arg(modelData.keys)
                                color: modelData.running ? root.cGood : root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(11) }
                            }
                        }

                        Row {
                            spacing: 8
                            Btn {
                                visible: !root.st.running
                                label: I18n.tr("Turn on, now and at every login")
                                onPressed: root.run(["enable"], "")
                            }
                            Btn {
                                visible: root.st.active.length > 0
                                label: I18n.tr("Stop everything that is on")
                                tint: root.cWarn
                                onPressed: root.run(["stop"], "")
                            }
                        }
                    }
                }

                // ── profiles ────────────────────────────────────────────
                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: root.profiles
                        delegate: Chip {
                            required property var modelData
                            label: modelData.name
                            on: root.page === "bind" && root.cur === modelData.name
                            onPicked: {
                                root.page = "bind"
                                root.cur = modelData.name
                                root.loadEditor()
                            }
                        }
                    }
                    Chip {
                        label: I18n.tr("+ New profile")
                        on: root.page === "new"
                        onPicked: root.openNew()
                    }
                }

                // ── NEW PROFILE ─────────────────────────────────────────
                Column {
                    visible: root.page === "new"
                    width: parent.width
                    spacing: 8

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: I18n.tr("A profile is a set of bindings for one game. It is in force only while that game's window is in front. Start the game, then pick its window here.")
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    ListView {
                        id: appList
                        width: parent.width
                        height: Math.min(contentHeight, root.ui(220))
                        clip: true
                        spacing: 4
                        model: root.apps
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            contentItem: Rectangle { implicitWidth: 6; radius: 3; color: root.cDim; opacity: 0.7 }
                        }
                        delegate: Rectangle {
                            id: appRow
                            required property var modelData
                            width: appList.width - 10
                            height: root.ui(40)
                            radius: 6
                            color: root.cPanel
                            border { width: 1; color: root.newApp === appRow.modelData.app ? root.cAccent : root.cWash }
                            Column {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    text: appRow.modelData.title || appRow.modelData.app
                                    color: root.cText
                                    elide: Text.ElideRight
                                    width: appList.width - 40
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                }
                                Text {
                                    text: appRow.modelData.app
                                    color: root.cDim
                                    font { family: root.uiFont; pixelSize: root.ui(10) }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.newApp = appRow.modelData.app
                                    root.newName = root.nameFrom(appRow.modelData.title || appRow.modelData.app)
                                }
                            }
                        }
                    }

                    Row {
                        spacing: 8
                        Chip {
                            label: I18n.tr("For every app")
                            on: root.newApp === ""
                            onPicked: { root.newApp = ""; if (!root.newName) root.newName = I18n.tr("Everywhere") }
                        }
                        Btn {
                            label: I18n.tr("Refresh the window list")
                            tint: root.cDim
                            onPressed: appsProc.running = true
                        }
                    }

                    Text {
                        visible: root.newApp === ""
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: I18n.tr("A profile for every app is in force wherever you are, so the left button cannot be rebound in it.")
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    Row {
                        spacing: 8
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: I18n.tr("Name")
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }
                        Rectangle {
                            width: root.ui(300); height: root.ui(30); radius: 6
                            color: root.cPanel
                            border { width: 1; color: nameIn.activeFocus ? root.cAccent : root.cDim }
                            TextInput {
                                id: nameIn
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                verticalAlignment: TextInput.AlignVCenter
                                text: root.newName
                                onTextEdited: root.newName = text
                                color: root.cText
                                maximumLength: 60
                                clip: true
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                        }
                    }

                    Btn {
                        label: root.newApp ? I18n.tr("Create the profile for %1").arg(root.newApp)
                                           : I18n.tr("Create the profile for every app")
                        onPressed: root.createProfile()
                    }
                }

                // ── BIND ────────────────────────────────────────────────
                Column {
                    visible: root.page === "bind" && root.curProfile !== null
                    width: parent.width
                    spacing: 8

                    Row {
                        spacing: 10
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: !root.curProfile ? ""
                                : root.curProfile.everywhere ? I18n.tr("In force in every app")
                                : I18n.tr("In force while %1 is in front").arg(root.curProfile.app || root.curProfile.title)
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                        }
                        Btn {
                            label: root.confirmDelete ? I18n.tr("Click again to delete %1").arg(root.cur)
                                                      : I18n.tr("Delete this profile")
                            tint: root.cBad
                            onPressed: {
                                if (!root.confirmDelete) { root.confirmDelete = true; return }
                                root.run(["remove", root.cur], "")
                                root.confirmDelete = false
                                root.sel = ""
                            }
                        }
                    }

                    Text {
                        text: I18n.tr("Click a button to change what it does.")
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    Repeater {
                        model: root.shownButtons
                        delegate: Column {
                          id: bCell
                          required property var modelData
                          width: parent.width
                          spacing: 6
                          Rectangle {
                            id: bRow
                            readonly property var modelData: bCell.modelData
                            readonly property var b: root.bindOf(root.cur, modelData.name)
                            readonly property var act: root.activeFor(root.cur, modelData.name)
                            width: parent.width
                            height: root.ui(44)
                            radius: 7
                            color: root.cPanel
                            border { width: 1; color: root.sel === bRow.modelData.name ? root.cAccent : root.cWash }
                            Text {
                                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                                // i18n-dynamic: the labels are marked N_() in src/keys.c
                                text: I18n.tr(bRow.modelData.label)
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(13); bold: true }
                            }
                            Text {
                                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                                text: (bRow.act && bRow.act.running ? "● " : "") + root.summary(bRow.b)
                                color: bRow.act && bRow.act.running ? root.cGood
                                     : bRow.b ? root.cAccent : root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.sel = root.sel === bRow.modelData.name ? "" : bRow.modelData.name
                                    root.loadEditor()
                                }
                            }
                          }
                          Loader {
                            width: parent.width
                            active: root.sel === bCell.modelData.name
                            visible: active
                            sourceComponent: editorComp
                          }
                        }
                    }

                }

                Text {
                    visible: root.message !== ""
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.message
                    color: root.messageBad ? root.cBad : root.cGood
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                }

                // ── settings ────────────────────────────────────────────
                Rectangle { width: parent.width; height: 1; color: root.cWash }

                Flow {
                    width: parent.width
                    spacing: 6
                    visible: root.mice.length > 1
                    Text {
                        text: I18n.tr("Which mouse:")
                        color: root.cDim
                        height: root.ui(28)
                        verticalAlignment: Text.AlignVCenter
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }
                    Chip {
                        label: I18n.tr("All of them")
                        on: root.conf.device === ""
                        onPicked: root.run(["set", "device", ""], "")
                    }
                    Repeater {
                        model: root.mice
                        delegate: Chip {
                            required property var modelData
                            label: modelData.name
                            on: root.conf.device === modelData.name
                            onPicked: root.run(["set", "device", modelData.name], "")
                        }
                    }
                }

                Text {
                    visible: root.mice.some(m => !m.readable)
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: I18n.tr("This account cannot read the mouse. Add it to the input group: sudo usermod -aG input $USER, then log out and back in.")
                    color: root.cWarn
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }

                Row {
                    spacing: 8
                    Chip {
                        label: root.conf.notify === "on" ? "✓" : " "
                        on: root.conf.notify === "on"
                        onPicked: root.run(["set", "notify", root.conf.notify === "on" ? "off" : "on"], "")
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: I18n.tr("Show a notice when a toggle switches on or off")
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                }
            }
        }
    }
}
