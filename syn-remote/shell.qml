//@ pragma UseQApplication

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

/*
 * Remote Desktop — the connections this machine can open.
 *
 * The OTHER half of syn-remote. `syn-remote status` and the Remote Desktop pane
 * in syn-settings are about serving THIS desktop; this window is about reaching
 * somebody else's, and it is a list of saved machines with a password
 * remembered for each.
 *
 * ⛔ IT OWNS NO PART OF THE STORE. Every row here comes from
 * `syn-remote hosts --tsv` and every change goes back through `syn-remote add`,
 * `forget` and `saved` — the same commands a person types. Where a password
 * actually lives (a keyring if one is running, a 0600 file if not, and the
 * read-back that tells those apart, because secret-tool exits 0 with no keyring
 * at all) is decided once, in the script, and this window only reports which of
 * the two answered. A second implementation of that decision is how a password
 * gets saved in one place and looked for in another.
 *
 * ⚠ AND CONNECTING IS NOT THIS WINDOW'S JOB EITHER. `syn-remote connect` pipes
 * the password to syn-remote-view on stdin and execs it. Doing that from here
 * would mean the password crossing a QML Process's argv or environment, both of
 * which are readable from outside the process for as long as it runs.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
ShellRoot {
    id: root

    /* ── palette ────────────────────────────────────────────
     * From ~/.config/synui/theme.json, which synui-apply-theme rewrites on every
     * theme switch — the same file the bar reads. This window used to hard-code
     * synui's dark chrome, so the updater was a slab of navy in the middle of a
     * beige XP desktop or a Gruvbox one: the one window on the system that no
     * theme could touch.
     *
     * Read here rather than by importing the bar's Theme singleton, because that
     * singleton lives in synui's package and this is a different one — an import
     * path across packages breaks the moment either is installed alone. The
     * contract is the JSON, not the QML.
     *
     * Every colour falls back to the old hard-coded value, so a box that has
     * never applied a theme (a fresh install, the live ISO) looks exactly as it
     * did. NOTE the two shapes in that file: accent/bar/popup are [r,g,b]
     * ARRAYS, fg is a "#rrggbb" STRING. Handing an array to a QML colour paints
     * nothing at all, silently.
     */
    property var pal: ({})
    readonly property bool palLight: pal.scheme === "light"

    property FileView paletteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        // Written as a temp file and renamed, so this never fires on a partial
        // palette — and the window restyles live, without a relaunch.
        onFileChanged: reload()
        onLoaded: {
            try { root.pal = JSON.parse(this.text()) }
            catch (e) { root.pal = ({}) }   // half a palette is worse than none
        }
        onLoadFailed: root.pal = ({})
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

    // [r,g,b] 0..255 → colour, or `fb` when the key is missing or malformed.
    function rgbOf(key, fb) {
        const c = root.pal[key]
        return (c && c.length === 3) ? Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, 1)
                                     : fb
    }
    // t is a position from `a` toward `b`, and is allowed to go NEGATIVE — that
    // is how the sunken pane is expressed: a step AWAY from the ink, which is
    // darker than the surface on a dark theme and lighter on a light one, the
    // same way a KDE view sinks under a dark window and lifts under a pale one.
    function mix(a, b, t) {
        function ch(x, y) { return Math.max(0, Math.min(1, x + (y - x) * t)) }
        return Qt.rgba(ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 1)
    }

    // ── Legibility ──────────────────────────────────────────────────────────
    //
    // The contrast corrector synfiles, synpkg, syn-disks and the bar all carry.
    // It is here for the WALLPAPER accent alone — see cAccent below for why the
    // theme's own accent is deliberately not put through it.
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

    readonly property color cBg:      rgbOf("popup", "#11151c")
    // The panel and the rules are positions between the surface and the ink, so
    // they invert with the theme instead of staying a fixed dark slate.
    readonly property color cPanel:   mix(cBg, cText, 0.06)
    readonly property color cLine:    mix(cBg, cText, 0.18)
    readonly property color cText:    root.pal.fg ? root.pal.fg : "#dbe4ee"
    readonly property color cDim:     mix(cBg, cText, 0.58)
    // ⚠ THE CORRECTOR RUNS ON THE MEASURED COLOUR ONLY, and the asymmetry is
    // the point: a theme's accent was chosen by a person against these exact
    // surfaces, and a hue lifted off a photograph was not. Putting the preset
    // through it as well would re-tint windows this change is not about.
    readonly property color cAccent:  root.wpAccent !== ""
                                      ? readable(Qt.color(root.wpAccent), cBg, 4.5)
                                      : rgbOf("accent", "#38bdf8")
    // Success and warning carry meaning, so they are picked per scheme rather
    // than derived — the dark pair is unreadable on a light surface.
    readonly property color cOk:      palLight ? "#1a7f3d" : "#5ee68a"
    readonly property color cWarn:    palLight ? "#8a5a00" : "#f2b45c"
    // The log pane sits below the surface rather than at a fixed near-black.
    readonly property color cSunken:  mix(cBg, cText, -0.03)

    /* ── the UI font ────────────────────────────────────────
     * ~/.config/synui/font.state, written by synui-apply-font(1), is the
     * desktop-wide font setting — deliberately NOT a key in theme.json,
     * because the font outlives a theme switch. It carries the family AND
     * the text scale, and an app that honours one without the other still
     * looks wrong beside its siblings.
     *
     * Qt resolves an application's default font ONCE at startup, so BOTH
     * have to be bindings on every Text: a window that merely inherits the
     * app font keeps the face it launched with, and the control panel's
     * font picker appears to do nothing here while Settings and Files
     * follow it immediately. That is exactly what this window did — it read
     * font.state nowhere at all — and it is the same gap Arsenal and
     * Software had before 9ccefbd.
     *
     * Only pixelSize is scaled, never a width; the few heights that exist
     * solely to hold N lines of text are scaled too, or the card clips its
     * own contents at 150%.
     */
    property string uiFont: ""
    property int    textScale: 100
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


    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        // No font.state is the normal case on a box where nobody has picked a
        // font; a warning per start for an expected miss is how a log becomes
        // something nobody reads.
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = this.text()
            const m = t.match(/^\s*family\s*=\s*(.+?)\s*$/m)
            root.uiFont = m ? m[1] : ""
            const s = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            root.textScale = s ? parseInt(s[1]) : 100
        }
        onLoadFailed: { root.uiFont = ""; root.textScale = 100 }
    }

    /*
     * ⚠ AT THE ROOT, NOT BESIDE THE ROW THAT USES THEM. An inline component may
     * only be declared in a document's top-level object; nested inside the Row
     * these are used in, the file parses and then refuses to LOAD — and what
     * quickshell reports is a line number that is not the one at fault.
     */
    component Field: TextField {
        id: fld
        color: root.cText
        font.family: root.uiFont
        font.pixelSize: root.ui(12)
        placeholderTextColor: root.cDim
        background: Rectangle {
            radius: root.ui(6)
            color: root.cPanel
            border.width: 1
            border.color: fld.activeFocus ? root.cAccent : root.cLine
        }
    }

    // The quiet buttons: an outline, and a tint for the one that destroys
    // something. ⚠ Each one carries its own words — none is labelled from a
    // setting or a state name.
    component Act: Button {
        id: act
        property color tint: root.cText
        contentItem: Text {
            text: act.text
            color: act.enabled ? act.tint : root.cDim
            font.family: root.uiFont
            font.pixelSize: root.ui(12)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitWidth: root.ui(160)
            implicitHeight: root.ui(34)
            radius: root.ui(6)
            color: act.down ? Qt.rgba(root.cText.r, root.cText.g, root.cText.b, 0.14)
                            : root.cPanel
            border.width: 1
            border.color: root.cLine
        }
    }

    // The one filled button, for the one action that adds something.
    component Primary: Button {
        id: pri
        contentItem: Text {
            text: pri.text
            color: pri.enabled ? root.cBg : root.cDim
            font.family: root.uiFont
            font.pixelSize: root.ui(12)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitWidth: root.ui(74)
            implicitHeight: root.ui(34)
            radius: root.ui(6)
            color: !pri.enabled ? root.cPanel
                 : pri.down    ? Qt.darker(root.cAccent, 1.2)
                               : root.cAccent
        }
    }

    /* ── What is saved ──────────────────────────────────────
     *
     * One row per saved connection, straight out of `hosts --tsv`. The first
     * line of that output NAMES THE COLUMNS and is skipped here — it is there
     * so a reader can tell an empty list from a broken command, which a bare
     * "no output" cannot.
     */
    property var hosts: []      // [{name, host, port, user, secret, pinned}]
    property string selected: ""
    property bool   busy: false
    property string problem: ""

    readonly property var chosen: {
        for (const h of root.hosts)
            if (h.name === root.selected) return h
        return null
    }

    function reload() {
        root.busy = true
        root.problem = ""
        listProc.running = true
    }

    function parseHosts(text) {
        const out = []
        const lines = String(text).split("\n")
        for (const raw of lines) {
            const line = raw.replace(/\s+$/, "")
            if (line === "") continue
            const f = line.split("\t")
            // The header row, and anything that is not five columns. A short
            // row is a format change, not a connection, and inventing fields
            // for it would put an empty host in the list that cannot be opened.
            if (f[0] === "name" || f.length < 6) continue
            out.push({ name: f[0], host: f[1], port: f[2], user: f[3],
                       secret: f[4], pinned: f[5] === "yes" })
        }
        root.hosts = out
        if (out.length && !root.chosen) root.selected = out[0].name
        if (!out.length) root.selected = ""
    }

    // What the `secret` column means, said in words rather than as a state name.
    function secretWords(s) {
        if (s === "keyring") return "Password remembered in the keyring"
        if (s === "file")    return "Password remembered in a file on disk"
        return "No password remembered — it will ask"
    }

    /*
     * ⛔ AN UNPINNED CONNECTION CANNOT OPEN AT ALL, so the row says so rather
     * than letting somebody press Connect and watch nothing happen. wayvnc
     * offers one security type this viewer speaks (VeNCrypt X509Plain), so the
     * server's certificate has to be checked before anything else — and until
     * it has been, the TLS session comes up and is dropped with no error
     * anywhere but "Client handshake timed out" in the SERVER's journal.
     */
    function pinWords(h) {
        return h.pinned ? "Certificate trusted"
                        : "⚠ Certificate not checked yet — connect will ask first"
    }

    // ⚠ syntty, for the same reason the password flow uses it: `trust` shows a
    // fingerprint and waits for a yes, and that is a question for a person at a
    // terminal, not something a window should answer on their behalf.
    function trustServer(name) {
        run(["syntty", "--hold", "-e", "syn-remote", "trust", name])
    }

    Process {
        id: listProc
        command: ["syn-remote", "hosts", "--tsv"]
        running: false
        stdout: StdioCollector { onStreamFinished: root.parseHosts(this.text) }
        stderr: StdioCollector {
            onStreamFinished: if (this.text) root.problem = String(this.text).trim()
        }
        onExited: root.busy = false
    }

    /*
     * ⚠ ONE Process, REBUILT PER RUN, AND running IS SET false FIRST.
     * quickshell answers `running = true` on a Process that is ALREADY running
     * with a silent no-op — so a second Connect while the first is still
     * starting simply does nothing, with no error to see. Dropping it to false
     * first makes the restart explicit.
     */
    Process {
        id: actProc
        running: false
        onExited: root.reload()
    }
    function run(args) {
        actProc.running = false
        actProc.command = args
        actProc.running = true
    }

    // ⚠ syntty, not this window. `saved <name> set` reads the password with
    // `read -rs`, which needs a terminal — and a password typed into a QML
    // TextField would have to reach the script somehow, which is exactly the
    // argv/environment exposure the header refuses. --hold so a message about
    // where it was stored is still on screen afterwards.
    function setPassword(name) {
        run(["syntty", "--hold", "-e", "syn-remote", "saved", name, "set"])
    }

    FloatingWindow {
        title: "Remote Desktop"
        minimumSize: Qt.size(680, 460)
        color: root.cBg

        Component.onCompleted: root.reload()

        // Closing the window ends the process — ShellRoot is built for a
        // persistent shell and this is one dialog. Without it `qs` survives
        // owning nothing, and the launcher's --no-duplicate then sees that
        // corpse and exits 0 without drawing anything.
        onClosed: Qt.quit()

        Rectangle {
            anchors.fill: parent
            color: root.cBg

            Column {
                anchors.fill: parent
                anchors.margins: root.ui(18)
                spacing: root.ui(14)

                // ── header ─────────────────────────────────
                Column {
                    width: parent.width
                    spacing: root.ui(3)
                    Text {
                        text: "Remote Desktop"
                        color: root.cText
                        font.family: root.uiFont
                        font.pixelSize: root.ui(21)
                        font.bold: true
                    }
                    Text {
                        text: "Machines you can open from here."
                        color: root.cDim
                        font.family: root.uiFont
                        font.pixelSize: root.ui(12)
                    }
                }

                // ── the list ───────────────────────────────
                Rectangle {
                    width: parent.width
                    height: parent.height - root.ui(160)
                    radius: root.ui(8)
                    color: root.cSunken
                    border.width: 1
                    border.color: root.cLine
                    clip: true

                    ListView {
                        id: list
                        anchors.fill: parent
                        anchors.margins: root.ui(4)
                        model: root.hosts
                        spacing: root.ui(2)
                        clip: true
                        // A view that scrolls says so — see SynScrollBar above.
                        ScrollBar.vertical: SynScrollBar {}

                        delegate: Rectangle {
                            width: list.width - root.ui(10)
                            height: root.ui(52)
                            radius: root.ui(6)
                            readonly property bool isSel: modelData.name === root.selected
                            color: isSel ? Qt.rgba(root.cAccent.r, root.cAccent.g,
                                                   root.cAccent.b, 0.18)
                                 : hover.hovered ? Qt.rgba(root.cText.r, root.cText.g,
                                                           root.cText.b, 0.06)
                                 : "transparent"

                            // ⚠ HoverHandler, not a MouseArea filling the row —
                            // a MouseArea over a list is EXITED by whatever it
                            // contains, so the highlight lands on one item and
                            // then flickers.
                            HoverHandler { id: hover }
                            TapHandler {
                                onTapped: root.selected = modelData.name
                                onDoubleTapped: root.run(["syn-remote", "connect",
                                                          modelData.name])
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: root.ui(12)
                                anchors.right: parent.right
                                anchors.rightMargin: root.ui(12)
                                spacing: root.ui(2)

                                Text {
                                    text: modelData.name
                                    color: root.cText
                                    font.family: root.uiFont
                                    font.pixelSize: root.ui(14)
                                    font.bold: true
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                                Text {
                                    text: modelData.host + ":" + modelData.port
                                        + (modelData.user ? "   as " + modelData.user : "")
                                        + "   ·   " + root.secretWords(modelData.secret)
                                        + "   ·   " + root.pinWords(modelData)
                                    color: !modelData.pinned || modelData.secret === "none"
                                           ? root.cWarn : root.cDim
                                    font.family: root.uiFont
                                    font.pixelSize: root.ui(11)
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                            }
                        }
                    }

                    // The empty case says what to do next rather than leaving a
                    // blank rectangle that reads as a window still loading.
                    Column {
                        anchors.centerIn: parent
                        spacing: root.ui(6)
                        visible: !root.busy && root.hosts.length === 0
                        Text {
                            text: "No saved connections yet."
                            color: root.cDim
                            font.family: root.uiFont
                            font.pixelSize: root.ui(14)
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            text: "Add one below, or:  syn-remote add <name> <host>"
                            color: root.cDim
                            font.family: root.uiFont
                            font.pixelSize: root.ui(11)
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }

                // ── adding one ─────────────────────────────
                Row {
                    width: parent.width
                    spacing: root.ui(8)


                    Field {
                        id: fName
                        width: root.ui(140)
                        placeholderText: "name"
                    }
                    Field {
                        id: fHost
                        width: root.ui(200)
                        placeholderText: "host or host:port"
                    }
                    Field {
                        id: fUser
                        width: root.ui(120)
                        placeholderText: "user (optional)"
                    }

                    Primary {
                        text: "Add"
                        enabled: fName.text.trim() !== "" && fHost.text.trim() !== ""
                        onClicked: {
                            root.run(["syn-remote", "add", fName.text.trim(),
                                      fHost.text.trim(), fUser.text.trim()])
                            fName.text = ""; fHost.text = ""; fUser.text = ""
                        }
                    }
                }

                // ── what you can do with the chosen one ────
                //
                // ⚠ EVERY BUTTON SAYS WHAT IT DOES. None of these is labelled
                // from a setting or a state name — a button is its own label.
                Row {
                    width: parent.width
                    spacing: root.ui(8)


                    Act {
                        text: root.chosen ? "Connect to " + root.chosen.name : "Connect"
                        tint: root.cAccent
                        onClicked: root.run(["syn-remote", "connect", root.chosen.name])
                    }
                    Act {
                        // The label names which of the two this press does, for
                        // the same reason the password button does.
                        text: root.chosen && root.chosen.pinned
                              ? "Re-check the certificate" : "Check the certificate"
                        tint: root.chosen && !root.chosen.pinned ? root.cWarn : root.cText
                        onClicked: root.trustServer(root.chosen.name)
                    }
                    Act {
                        // The label changes because the ACTION changes: there is
                        // a difference between storing a password and replacing
                        // one, and a row that says "Password…" either way hides
                        // which of the two is about to happen.
                        text: root.chosen && root.chosen.secret !== "none"
                              ? "Replace the password" : "Remember a password"
                        onClicked: root.setPassword(root.chosen.name)
                    }
                    Act {
                        text: "Forget this connection"
                        tint: root.cWarn
                        onClicked: root.run(["syn-remote", "forget", root.chosen.name])
                    }
                }

                // ── anything the command complained about ──
                Text {
                    width: parent.width
                    visible: root.problem !== ""
                    text: root.problem
                    color: root.cWarn
                    font.family: root.uiFont
                    font.pixelSize: root.ui(11)
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
