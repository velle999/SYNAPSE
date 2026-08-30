// syn-cal — the SynapseOS calendar and schedule planner.
//
// A renderer, and nothing more. Every event on screen arrives as a record from
// `syn-cal --rec agenda`; this file knows how to draw a week and a day, and
// knows nothing about CalDAV. Nothing here decides a conflict, writes a file or
// talks to a server — all of that is in the binary, where the CLI, the TUI and
// this window meet the same rules.
//
// ── The one rule for reading records ───────────────────────────────────────
//
// EVERY field arrives percent-encoded, including the ones that look like plain
// words. A summary is arbitrary text — "Sam & Jo: 1-2-1" is an ordinary meeting
// name — so decoding "the ones that need it" means keeping a list that will
// drift, and the day it drifts a tab in a title shifts every column of a row.
//
// So: decode every field, once, at the parse. See disp().
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Quickshell
import Quickshell.Io
import QtQuick.Controls

FloatingWindow {
    id: root

    title: "SYNAPSE Calendar"
    implicitWidth: 1020
    implicitHeight: 700
    // Seven day columns and a sidebar. Below this a week stops being readable
    // and the columns start overlapping their own text.
    minimumSize: Qt.size(760, 480)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNCAL_BIN") || "syn-cal"

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
    readonly property color cAccentRaw: root.wpAccent !== ""
                                        ? Qt.color(root.wpAccent)
                                        : themed("accent", 78, 201, 176, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)
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

    /*
     * ⛔ A VIEW THAT SCROLLS SHOWS THAT IT SCROLLS. Without a bar there is
     * nothing on screen saying there is anything past the edge of the view,
     * nothing saying how much, and no way to cross a long list in one gesture.
     * velle, 2026-08-28: "you keep making windows without scrollbars and thats
     * dumb."
     *
     * ⚠ VISIBLE AT REST, which is why this exists rather than a bare ScrollBar:
     * Qt's default fades the handle out unless `active` — true while the view
     * moves or the bar is hovered, and false in exactly the state where
     * somebody is deciding whether there is more to see.
     *
     * ⚠ AsNeeded, so a view shorter than its window draws no furniture.
     *
     * ⚠ ORIENTATION-AWARE: attached as `ScrollBar.horizontal` it has to be
     * short and wide, not a vertical handle lying on its side.
     *
     * ⚠ INLINE, because this app carries its own palette — as every window here
     * does — and a scrollbar belongs with the colours it is drawn against.
     * There is no QML module shared across these packages to put it in.
     * Pinned by preflight's `scrollbar` gate.
     */
    component SynScrollBar: ScrollBar {
        id: sb
        readonly property bool vert: sb.orientation === Qt.Vertical

        policy: ScrollBar.AsNeeded
        /*
         * ⛔ NOTHING TO SCROLL MEANS NO SCROLLBAR AT ALL. AsNeeded hides the
         * handle by fading its OPACITY, and a custom contentItem replaces the
         * binding that does it — so a bar styled to be visible at rest became
         * visible at rest everywhere, a full-length handle that cannot move
         * sitting on every short list on the desktop. velle, 2026-08-28:
         * "if there's nothing to scroll the scrollbar should autohide. i don't
         * need the fucking scrollbars literally everywhere when they can't even
         * do anything."
         *
         * `size` is the fraction of the content the view can show: 1.0 means it
         * all fits. Visible at rest is for the case where there IS more — that
         * is the whole point of it — and is clutter in every other case.
         */
        readonly property bool needed:
            sb.policy === ScrollBar.AlwaysOn ||
            (sb.policy === ScrollBar.AsNeeded && sb.size < 1.0)
        visible: sb.needed
        padding: root.ui(2)
        implicitWidth:  sb.vert ? root.ui(11) : root.ui(48)
        implicitHeight: sb.vert ? root.ui(48) : root.ui(11)

        contentItem: Rectangle {
            implicitWidth:  sb.vert ? root.ui(7) : root.ui(32)
            implicitHeight: sb.vert ? root.ui(32) : root.ui(7)
            radius: Math.min(width, height) / 2
            color: sb.pressed ? root.cAccent : sb.hovered ? root.cText : root.cDim
            opacity: sb.pressed || sb.hovered ? 1.0 : 0.5
            Behavior on color   { ColorAnimation  { duration: 90 } }
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }

        background: Rectangle {
            radius: Math.min(width, height) / 2
            color: Qt.rgba(root.cText.r, root.cText.g, root.cText.b, 0.08)
            opacity: sb.hovered || sb.pressed ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }

    // A labelled text box. Inline for the same reason SynScrollBar is: this
    // window carries its own palette and there is no shared QML module here.
    component SynField: Column {
        id: fld
        property string label: ""
        property string placeholder: ""
        property alias text: box.text
        // ⚠ ECHO OFF IS NOT ENOUGH ON ITS OWN — a password box must also keep
        // out of the predictive/clipboard machinery a normal field opts into.
        property bool secret: false
        signal textEdited(string text)

        spacing: root.ui(4)

        Text {
            text: fld.label
            visible: fld.label !== ""
            color: root.cDim
            font { family: root.uiFont; pixelSize: root.ui(10); bold: true }
        }

        Rectangle {
            width: fld.width
            height: root.ui(30)
            radius: root.ui(5)
            color: root.cBg
            border { width: 1; color: box.activeFocus ? root.cAccent : root.cDim }

            TextInput {
                id: box
                anchors { fill: parent; leftMargin: root.ui(8); rightMargin: root.ui(8) }
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: root.cText
                selectionColor: root.cAccent
                selectedTextColor: root.cPanel
                echoMode: fld.secret ? TextInput.Password : TextInput.Normal
                passwordCharacter: "\u2022"
                inputMethodHints: fld.secret
                    ? (Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                       | Qt.ImhNoAutoUppercase)
                    : Qt.ImhNone
                font { family: root.uiFont; pixelSize: root.ui(13) }
                onTextEdited: fld.textEdited(box.text)
                // Enter submits, because a form that can only be finished with
                // the mouse is a form that gets finished with the mouse.
                Keys.onReturnPressed: root.submitAuth()
                Keys.onEnterPressed: root.submitAuth()

                Text {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    visible: box.text === "" && !box.activeFocus
                    text: fld.placeholder
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(13) }
                }
            }
        }
    }

    // ── The records ─────────────────────────────────────────────────────────
    //
    // ⚠ DECODE EVERY FIELD, ONCE, AT THE PARSE. See the note at the top: the
    // alternative is a list of "fields that need it" which drifts, and the day
    // it drifts a tab inside a meeting title shifts every column of a row.
    function disp(s) {
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    property int days: 7
    property var events: []
    property var calendars: []
    property string status: ""
    property bool busy: false

    // The window opens on the week containing today, and `offset` moves it in
    // whole weeks. Kept as a day count rather than a Date so that adding a week
    // across a clock change cannot silently land an hour out.
    property int offset: 0

    readonly property date anchorDay: {
        const d = new Date()
        d.setHours(0, 0, 0, 0)
        d.setDate(d.getDate() + root.offset * root.days)
        return d
    }

    function reload() {
        agendaProc.running = false
        agendaProc.running = true
        calsProc.running = false
        calsProc.running = true
    }

    Component.onCompleted: reload()

    // ── syn-cal --rec agenda ────────────────────────────────────────────────

    Process {
        id: agendaProc
        // ⚠ THE BINARY DOES THE WORK. Recurrence expansion, time zones and the
        // clock change all happen in libical behind this command; nothing in
        // QML is capable of getting those right and nothing here tries.
        command: [root.bin, "--rec", "agenda", "--days=" + root.days]
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = text.trim().split("\n").filter(l => l.length > 0)
                const out = []
                // Row 0 is the header. A file with only a header is an empty
                // agenda, not a failure.
                for (let i = 1; i < rows.length; i++) {
                    const f = rows[i].split("\t")
                    if (f.length < 9) continue
                    out.push({
                        start: parseInt(f[0], 10) * 1000,
                        end: parseInt(f[1], 10) * 1000,
                        allDay: f[2] === "1",
                        recurring: f[3] === "1",
                        account: root.disp(f[4]),
                        calendar: root.disp(f[5]),
                        summary: root.disp(f[6]),
                        location: root.disp(f[7]),
                        uid: root.disp(f[8])
                    })
                }
                root.events = out
            }
        }
    }

    // The sidebar's list of calendars, across every account.
    Process {
        id: calsProc
        command: [root.bin, "--rec", "accounts"]
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = text.trim().split("\n").filter(l => l.length > 0)
                const out = []
                for (let i = 1; i < rows.length; i++) {
                    const f = rows[i].split("\t")
                    if (f.length < 7) continue
                    // ⛔ kind AND secret ARE DECODED TOO, despite coming from
                    // a fixed vocabulary — "caldav", "keyring", "not set".
                    // Leaving them raw is the start of the list of "fields
                    // that need it" this file's header warns about: it is
                    // correct today, and it is the thing that drifts.
                    out.push({
                        name: root.disp(f[0]),
                        kind: root.disp(f[1]),
                        secret: root.disp(f[4]),
                        total: parseInt(f[5], 10),
                        on: parseInt(f[6], 10)
                    })
                }
                root.calendars = out
            }
        }
    }

    // ── Adding an account, and signing in ───────────────────────────────────
    //
    // ⛔ THE WINDOW SIGNS IN, NOT A TERMINAL. This pane used to say "add one
    // from a terminal", which for the one flow a person is most likely to reach
    // first — connect my Google calendar — meant the GUI was a viewer for
    // something only the CLI could set up. Same binary, same verbs, same rules:
    // everything below shells out to `syn-cal account add…` and `syn-cal login`.
    property bool authOpen: false
    property string authKind: "google"
    property string authName: ""
    property string authUrl: ""
    property string authUser: ""
    property bool authBusy: false
    property string authMsg: ""
    readonly property bool authOauth: root.authKind !== "caldav"

    function authReset() {
        root.authName = ""; root.authUrl = ""; root.authUser = ""
        root.authMsg = ""; root.authBusy = false
        authPass.text = ""
    }

    // Adding and signing in are two commands, so the second is started by the
    // first one's exit rather than fired alongside it.
    Process {
        id: addProc
        property string acct: ""
        stdout: StdioCollector { id: addOut }
        stderr: StdioCollector { id: addErr }
        onExited: (code) => {
            if (code !== 0) {
                root.authBusy = false
                root.authMsg = addErr.text.trim() || "could not add the account"
                return
            }
            root.beginLogin(addProc.acct)
        }
    }

    Process {
        id: loginProc
        stderr: StdioCollector { id: loginErr }
        onExited: (code) => {
            root.authBusy = false
            // ⛔ THE PASSWORD DIES WITH THE CHILD. This object outlives the
            // panel, so a credential left on its environment would be handed to
            // whatever this window runs next. Same rule as syn-settings.
            loginProc.environment = ({})
            if (code === 0) {
                root.authOpen = false
                root.authReset()
                root.status = "Signed in."
                root.reload()
            } else {
                root.authMsg = loginErr.text.trim() || "sign-in did not complete"
            }
        }
    }

    function beginLogin(name) {
        root.authBusy = true
        if (root.authKind === "caldav") {
            root.authMsg = "Checking the password…"
            /*
             * ⛔ A PASSWORD NEVER GOES IN argv. /proc/<pid>/cmdline is
             * world-readable; /proc/<pid>/environ is not. syn-cal already reads
             * the password from stdin when stdin is not a terminal, so the
             * shell pipes it in from the environment and nothing sensitive ever
             * reaches a command line.
             *
             * ⚠ AND NOT THROUGH Process.write() EITHER. A write issued before
             * the child has spawned is dropped, and the usual fix — wait for its
             * first output — cannot work here: syn-cal prints no prompt when
             * stdin is a pipe, so there is no first output to wait for.
             */
            loginProc.environment = ({ "SYNCAL_PW": authPass.text })
            loginProc.command = ["sh", "-c",
                                 "printf '%s' \"$SYNCAL_PW\" | exec \"$0\" login \"$1\"",
                                 root.bin, name]
        } else {
            root.authMsg = "Finish signing in in your browser…"
            // ⚠ --browser, NOT the default. syn-cal decides from isatty when it
            // is not told, and a window is not a terminal — without this the
            // sign-in prints a URL into a pipe nobody reads and then times out.
            loginProc.command = [root.bin, "login", name, "--browser"]
        }
        loginProc.running = false
        loginProc.running = true
    }

    // Signing in an account that already exists — the row's own button.
    function signIn(name, kind) {
        if (root.authBusy) return
        root.authKind = kind === "caldav" ? "caldav" : "google"
        if (kind === "caldav") { root.authOpen = true; root.authName = name; return }
        root.authMsg = ""
        root.beginLogin(name)
    }

    function submitAuth() {
        if (root.authBusy) return
        const name = root.authName.trim()
        if (name === "") { root.authMsg = "Give the account a name first."; return }
        if (root.authKind === "caldav" && root.authUrl.trim() === "") {
            root.authMsg = "A CalDAV account needs a server URL."; return
        }
        root.authMsg = "Adding…"
        root.authBusy = true
        addProc.acct = name
        if (root.authKind === "caldav")
            addProc.command = [root.bin, "account", "add", name, root.authUrl.trim(),
                               "--user", root.authUser.trim()]
        else
            addProc.command = [root.bin, "account", "add-" + root.authKind, name]
        addProc.running = false
        addProc.running = true
    }

    Process {
        id: syncProc
        command: [root.bin, "sync"]
        onExited: (code) => {
            root.busy = false
            root.status = code === 0 ? "Up to date." : "Some calendars did not sync."
            root.reload()
        }
    }

    function sync() {
        if (root.busy) return
        root.busy = true
        root.status = "Syncing…"
        // ⛔ `running = true` ON AN ALREADY-RUNNING Process IS A SILENT NO-OP.
        // Two clicks in quick succession would otherwise drop the second, and
        // the button would look broken exactly when somebody pressed it twice
        // because it looked broken. The busy guard above is the real fix; this
        // is the belt.
        syncProc.running = false
        syncProc.running = true
    }

    // ── Layout ──────────────────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: root.cBg

        Row {
            anchors.fill: parent

            // ── the sidebar ─────────────────────────────────────────────────
            Rectangle {
                id: side
                width: root.ui(230)
                height: parent.height
                color: root.cPanel

                Column {
                    anchors.fill: parent
                    anchors.margins: root.ui(14)
                    spacing: root.ui(10)

                    Text {
                        text: "Calendars"
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11); bold: true }
                    }

                    // ⚠ A CAPPED HEIGHT NEEDS clip AND A SCROLLBAR. Without
                    // both, a machine with eight accounts draws its list over
                    // the buttons underneath and nothing says there is more.
                    ListView {
                        id: calList
                        width: parent.width
                        height: Math.min(contentHeight, side.height - root.ui(190))
                        clip: true
                        model: root.calendars
                        spacing: root.ui(2)
                        ScrollBar.vertical: SynScrollBar {}

                        delegate: Column {
                            width: calList.width
                            spacing: root.ui(1)
                            Text {
                                text: modelData.name
                                color: root.cText
                                elide: Text.ElideRight
                                width: parent.width
                                font { family: root.uiFont; pixelSize: root.ui(13) }
                            }
                            Row {
                                width: parent.width
                                spacing: root.ui(6)
                                Text {
                                    text: modelData.secret === "not set"
                                          ? "not signed in"
                                          : modelData.on + " of " + modelData.total + " on"
                                    color: modelData.secret === "not set" ? root.cWarn : root.cDim
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                }
                                // An account with no token is one click from
                                // having one — saying "not signed in" and
                                // offering nothing is where this pane used to
                                // stop.
                                Rectangle {
                                    visible: modelData.secret === "not set"
                                    width: rowSignTxt.implicitWidth + root.ui(10)
                                    height: root.ui(16)
                                    radius: root.ui(4)
                                    color: rowSign.containsMouse ? root.cAccent : "transparent"
                                    border { width: 1; color: root.cAccent }
                                    Text {
                                        id: rowSignTxt
                                        anchors.centerIn: parent
                                        text: "Sign in"
                                        color: rowSign.containsMouse ? root.cPanel : root.cAccent
                                        font { family: root.uiFont; pixelSize: root.ui(10) }
                                    }
                                    MouseArea {
                                        id: rowSign
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: !root.authBusy
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.signIn(modelData.name, modelData.kind)
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: root.calendars.length === 0
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "No accounts yet."
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    // ⛔ THE BUTTON SAYS WHAT IT DOES, and it is always here —
                    // not only while the list is empty. A second account is the
                    // same job as the first.
                    Rectangle {
                        width: parent.width
                        height: root.ui(28)
                        radius: root.ui(6)
                        color: addMouse.containsMouse ? root.cAccent : "transparent"
                        border { width: 1; color: root.cDim }
                        Text {
                            anchors.centerIn: parent
                            text: "Add account"
                            color: addMouse.containsMouse ? root.cPanel : root.cText
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }
                        MouseArea {
                            id: addMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.authReset(); root.authKind = "google"; root.authOpen = true }
                        }
                    }
                }

                // ⛔ THE BUTTON SAYS WHAT IT DOES. Not an icon that needs a
                // tooltip to explain itself — a button is its own label.
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                              margins: root.ui(14) }
                    height: root.ui(34)
                    radius: root.ui(6)
                    color: syncMouse.containsMouse ? root.cAccent : "transparent"
                    border { width: 1; color: root.cAccent }
                    opacity: root.busy ? 0.5 : 1.0

                    Text {
                        anchors.centerIn: parent
                        text: root.busy ? "Syncing…" : "Sync now"
                        color: syncMouse.containsMouse ? root.cPanel : root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(13); bold: true }
                    }
                    MouseArea {
                        id: syncMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !root.busy
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.sync()
                    }
                }
            }

            // ── the agenda ──────────────────────────────────────────────────
            Item {
                width: parent.width - side.width
                height: parent.height

                Column {
                    anchors.fill: parent
                    anchors.margins: root.ui(18)
                    spacing: root.ui(12)

                    // ⛔ ANCHORED, NOT A ROW WITH A COMPUTED SPACER. This was a
                    // Row whose spacer was `parent.width - ui(340)`, and 340 is
                    // the width of one particular title in one particular font:
                    // "The next 7 days" is wider than the number allowed for, so
                    // the row overflowed and clipped "Next" off the right edge.
                    // A title is arbitrary text — a localised long month, a
                    // larger UI scale, a different font — so no constant is the
                    // right one. The title takes the space the buttons leave.
                    Item {
                        width: parent.width
                        height: Math.max(title.height, nav.height)

                        Text {
                            id: title
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter
                                      right: nav.left; rightMargin: root.ui(12) }
                            elide: Text.ElideRight
                            text: root.offset === 0 ? "The next " + root.days + " days"
                                                    : Qt.formatDate(root.anchorDay, "d MMMM yyyy")
                            color: root.cText
                            font { family: root.uiFont; pixelSize: root.ui(18); bold: true }
                        }

                        Row {
                            id: nav
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            spacing: root.ui(10)

                            Repeater {
                                model: [
                                    { label: "Back",  step: -1 },
                                    { label: "Today", step: 0 },
                                    { label: "Next",  step: 1 }
                                ]
                                Rectangle {
                                    width: root.ui(64); height: root.ui(26)
                                    radius: root.ui(5)
                                    color: navMouse.containsMouse ? root.cPanel : "transparent"
                                    border { width: 1; color: root.cDim }
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: root.cText
                                        font { family: root.uiFont; pixelSize: root.ui(11) }
                                    }
                                    MouseArea {
                                        id: navMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (modelData.step === 0) root.offset = 0
                                            else root.offset += modelData.step
                                            root.reload()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        id: agenda
                        width: parent.width
                        height: parent.height - root.ui(80)
                        clip: true
                        model: root.events
                        spacing: root.ui(2)
                        // A view that scrolls says so — see SynScrollBar above.
                        ScrollBar.vertical: SynScrollBar {}

                        delegate: Column {
                            width: agenda.width
                            spacing: root.ui(4)

                            // The day heading, drawn only when the day changes.
                            // Comparing formatted dates rather than timestamps:
                            // two events in the same local day can be more than
                            // 86400 seconds apart across a clock change.
                            property bool newDay: index === 0 ||
                                Qt.formatDate(new Date(root.events[index - 1].start), "yyyy-MM-dd") !==
                                Qt.formatDate(new Date(modelData.start), "yyyy-MM-dd")

                            Item { width: 1; height: parent.newDay && index > 0 ? root.ui(14) : 0 }

                            Text {
                                visible: parent.newDay
                                text: Qt.formatDate(new Date(modelData.start), "dddd d MMMM")
                                color: root.cAccent
                                font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
                            }

                            Rectangle {
                                width: parent.width
                                height: root.ui(38)
                                radius: root.ui(5)
                                color: rowMouse.containsMouse ? root.cPanel : "transparent"

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: root.ui(8)
                                    spacing: root.ui(12)

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: root.ui(58)
                                        text: modelData.allDay ? "all day"
                                             : Qt.formatDateTime(new Date(modelData.start), "HH:mm")
                                        color: root.cDim
                                        font { family: root.uiFont; pixelSize: root.ui(12) }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - root.ui(160)
                                        elide: Text.ElideRight
                                        text: modelData.summary === "" ? "(no title)" : modelData.summary
                                        color: root.cText
                                        font { family: root.uiFont; pixelSize: root.ui(13) }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.calendar
                                        color: root.cDim
                                        elide: Text.ElideRight
                                        width: root.ui(80)
                                        font { family: root.uiFont; pixelSize: root.ui(11) }
                                    }
                                }
                                MouseArea {
                                    id: rowMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }
                        }
                    }

                    Text {
                        visible: root.events.length === 0
                        text: root.calendars.length === 0
                              ? "No calendars are set up yet."
                              : "Nothing in the next " + root.days + " days."
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(13) }
                    }

                    Text {
                        visible: root.status !== ""
                        text: root.status
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }
                }
            }
        }

        // ── Add account / sign in ───────────────────────────────────────────
        //
        // ⚠ A FULL-WINDOW SCRIM, and it takes the mouse. Without a MouseArea of
        // its own the buttons underneath stay clickable through the dimming,
        // which is how a modal panel turns into a decoration.
        Rectangle {
            anchors.fill: parent
            visible: root.authOpen
            color: Qt.rgba(0, 0, 0, root.isLight ? 0.28 : 0.55)

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onClicked: if (!root.authBusy) { root.authOpen = false; root.authReset() }
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(root.ui(420), parent.width - root.ui(48))
                height: card.implicitHeight + root.ui(36)
                radius: root.ui(10)
                color: root.cPanel
                border { width: 1; color: root.cDim }

                // Swallow clicks that land on the card, so they do not reach the
                // scrim behind it and close the thing being filled in.
                MouseArea { anchors.fill: parent; hoverEnabled: true }

                Column {
                    id: card
                    anchors { left: parent.left; right: parent.right; top: parent.top
                              margins: root.ui(18) }
                    spacing: root.ui(12)

                    Text {
                        text: "Add an account"
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(15); bold: true }
                    }

                    Row {
                        spacing: root.ui(8)
                        Repeater {
                            model: [
                                { id: "google",    label: "Google" },
                                { id: "microsoft", label: "Microsoft 365" },
                                { id: "caldav",    label: "CalDAV" }
                            ]
                            Rectangle {
                                width: kindTxt.implicitWidth + root.ui(18)
                                height: root.ui(26)
                                radius: root.ui(5)
                                color: root.authKind === modelData.id ? root.cAccent
                                     : kindMouse.containsMouse ? root.cBg : "transparent"
                                border { width: 1
                                         color: root.authKind === modelData.id ? root.cAccent : root.cDim }
                                Text {
                                    id: kindTxt
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: root.authKind === modelData.id ? root.cPanel : root.cText
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                }
                                MouseArea {
                                    id: kindMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: !root.authBusy
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.authKind = modelData.id; root.authMsg = "" }
                                }
                            }
                        }
                    }

                    SynField {
                        id: nameField
                        width: parent.width
                        label: root.authOauth ? "Your address at this provider" : "A name for this account"
                        placeholder: root.authOauth ? "you@gmail.com" : "work"
                        text: root.authName
                        onTextEdited: root.authName = text
                    }

                    SynField {
                        width: parent.width
                        visible: !root.authOauth
                        label: "Server URL"
                        placeholder: "https://example.org/dav/"
                        text: root.authUrl
                        onTextEdited: root.authUrl = text
                    }

                    SynField {
                        width: parent.width
                        visible: !root.authOauth
                        label: "Username"
                        text: root.authUser
                        onTextEdited: root.authUser = text
                    }

                    SynField {
                        id: authPass
                        width: parent.width
                        visible: !root.authOauth
                        label: "Password"
                        secret: true
                    }

                    // ⚠ SAID BEFORE IT HAPPENS, not after. A window that opens a
                    // browser with no warning looks like it has crashed and
                    // launched something at random.
                    Text {
                        width: parent.width
                        visible: root.authOauth
                        wrapMode: Text.WordWrap
                        text: "Signing in opens your browser. Your password is typed at "
                              + (root.authKind === "google" ? "Google" : "Microsoft")
                              + ", never here."
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    Text {
                        width: parent.width
                        visible: root.authMsg !== ""
                        wrapMode: Text.WordWrap
                        text: root.authMsg
                        color: root.authBusy ? root.cDim : root.cBad
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }

                    Row {
                        anchors.right: parent.right
                        spacing: root.ui(8)

                        Rectangle {
                            width: root.ui(80); height: root.ui(30)
                            radius: root.ui(6)
                            color: cancelMouse.containsMouse ? root.cBg : "transparent"
                            border { width: 1; color: root.cDim }
                            Text {
                                anchors.centerIn: parent
                                text: "Cancel"
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(12) }
                            }
                            MouseArea {
                                id: cancelMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !root.authBusy
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.authOpen = false; root.authReset() }
                            }
                        }

                        Rectangle {
                            width: signTxt.implicitWidth + root.ui(22); height: root.ui(30)
                            radius: root.ui(6)
                            color: signMouse.containsMouse ? root.cAccent : "transparent"
                            border { width: 1; color: root.cAccent }
                            opacity: root.authBusy ? 0.5 : 1.0
                            Text {
                                id: signTxt
                                anchors.centerIn: parent
                                // ⛔ THE BUTTON IS ITS OWN LABEL. "OK" would make
                                // the sentence above it load-bearing.
                                text: root.authBusy ? "Working…"
                                    : root.authOauth ? "Sign in with browser" : "Add and sign in"
                                color: signMouse.containsMouse ? root.cPanel : root.cAccent
                                font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
                            }
                            MouseArea {
                                id: signMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !root.authBusy
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.submitAuth()
                            }
                        }
                    }
                }
            }
        }
    }
}
