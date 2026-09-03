// syn-play — the player window.
//
// A renderer, and nothing more. This file owns NO queue, NO history and NO idea
// of what is playing.
//
// ── Why that is the whole design ───────────────────────────────────────────
//
// mpv holds the playlist; `syn-play serve` reads it and this window draws what
// it is told. A window keeping its own copy would go out of step the first time
// somebody dropped a file on the mpv window, pressed `>` in mpv itself, or let
// a playlist run on — and the copy that is wrong is always the one on screen.
// Every button here sends one line to the same process the CLI and the TUI
// drive, so the three cannot come to disagree.
//
// ── The one rule for reading records ───────────────────────────────────────
//
// EVERY field that came from a filesystem arrives percent-encoded, including
// the ones that look like plain words — a filename may contain a tab and a
// title may contain a newline, and both are the separators. So: decode every
// such field, once, at the parse.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

// ⚠ Bound, so the delegates below may name ids from this file. Without it every
// reference out of a nested component is resolved at RUN time by scope lookup —
// which works until a property of the same name appears nearer, and then
// silently reads the wrong one.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

// ⛔ THE TRANSLATION SINGLETON, AND IT IS NOT qsTr(). quickshell 0.3.1 installs
// no QTranslator, so qsTr() compiles, looks up nothing and returns its own
// argument while looking exactly like a marked string in review — so
// qml/I18n.qml reads a JSON catalog compiled from the same po/ the CLI's .mo
// comes from, and a word this window and `syn-play status` share is translated
// once and cannot disagree.
import "qml"

FloatingWindow {
    id: root

    title: I18n.tr("Player")
    implicitWidth: 560
    implicitHeight: 680
    minimumSize: Qt.size(380, 420)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNPLAY_BIN") || "syn-play"

    // ── Theme ───────────────────────────────────────────────────────────────
    //
    // Read from the desktop, not hardcoded, and the same source and shape as
    // syn-edit, synfiles and the bar — so a theme switch moves all of them
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

    // …and the colour the WALLPAPER offers. ⚠ `ok` AND `use` both have to hold:
    // `ok` is the picture's own answer (a greyscale wallpaper has no hue to
    // give) and `use` is the setting.
    property string wpAccent: ""

    FileView {
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

    // ── The desktop's font ──────────────────────────────────────────────────
    //
    // ⚠ BOTH HALVES OR NEITHER. Qt resolves an application's default font ONCE
    // at startup and QML cannot change it afterwards, so the family has to be
    // named on every Text and every size has to go through ui(). Doing one and
    // not the other gives a window that follows the desktop right up until
    // somebody changes the setting — which looks fixed at exactly the moment it
    // is tested.
    property string uiFont: ""
    property int textScale: 100

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        printErrors: false      // no font.state is the normal case
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

    function ui(px) { return Math.max(6, Math.round(px * root.textScale / 100)) }

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
        const la = root.lum(a), lb = root.lum(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    // A theme accent is chosen to look good on the BAR, not to be legible as
    // text on this window's background. Nudged until it is, rather than trusted.
    function readable(c, on, want) {
        if (root.contrast(c, on) >= want) return c
        const up = root.lum(on) <= 0.18
        let out = c
        for (let i = 0; i < 16; i++) {
            out = up ? Qt.lighter(out, 1.25) : Qt.darker(out, 1.25)
            if (root.contrast(out, on) >= want) return out
        }
        return up ? "#ffffff" : "#000000"
    }

    readonly property color cPanel: themed("bar", 11, 11, 20, 1.0)
    readonly property color cBg:    isLight ? Qt.lighter(cPanel, 1.15) : Qt.darker(cPanel, 1.4)
    readonly property color cInk:   p.fg ? Qt.color(p.fg) : pick("#e6e9ef", "#12141a")
    readonly property color cText:  contrast(cInk, cBg) >= 4.5
                                    ? cInk : (lum(cBg) > 0.18 ? "#12141a" : "#e6e9ef")
    readonly property color cDim:   readable(pick("#8b93a7", "#4a5568"), cBg, 4.5)
    readonly property color cAccentRaw: root.wpAccent !== ""
                                        ? Qt.color(root.wpAccent)
                                        : themed("accent", 167, 139, 250, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cBg, 4.5)
    readonly property color cLine:  pick(Qt.rgba(1, 1, 1, 0.07), Qt.rgba(0, 0, 0, 0.07))
    readonly property color cRow:   Qt.rgba(cAccent.r, cAccent.g, cAccent.b, 0.14)

    /*
     * ⛔ ONE SCROLLBAR, USED BY EVERY SCROLLING VIEW IN THIS WINDOW.
     *
     * The default ScrollBar is a hairline that fades to nothing when the view
     * is still, which on a dark window is indistinguishable from having no
     * scrollbar at all. A view that scrolls has to SAY it scrolls even when
     * nobody is touching it: the handle is the only thing on screen that says
     * there is more above, how much more, and where in it you are.
     *
     * ⚠ AsNeeded, so a queue shorter than the window has no bar — and NOTHING
     * TO SCROLL MEANS NO BAR AT ALL. AsNeeded normally hides the handle by
     * fading its opacity, and a custom contentItem replaces the binding that
     * does it; without the `needed` test below, a bar styled to be visible at
     * rest becomes a full-length handle that cannot move sitting on every short
     * list on the desktop.
     */
    component SynScrollBar: ScrollBar {
        id: sb
        policy: ScrollBar.AsNeeded
        readonly property bool needed:
            sb.policy === ScrollBar.AlwaysOn ||
            (sb.policy === ScrollBar.AsNeeded && sb.size < 1.0)
        visible: sb.needed
        implicitWidth: root.ui(11)
        padding: root.ui(2)

        contentItem: Rectangle {
            implicitWidth: root.ui(7)
            radius: width / 2
            color: sb.pressed ? root.cAccent : sb.hovered ? root.cText : root.cDim
            opacity: sb.pressed || sb.hovered ? 1.0 : 0.5
            Behavior on color   { ColorAnimation  { duration: 90 } }
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }
        background: Rectangle {
            radius: width / 2
            color: root.cLine
            opacity: sb.hovered || sb.pressed ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }

    // A labelled control that looks like one, used for every button here.
    component Chip: Rectangle {
        id: chip
        property string label: ""
        property bool on: false
        property bool primary: false
        signal clicked()

        implicitWidth: chipText.implicitWidth + root.ui(18)
        implicitHeight: root.ui(28)
        radius: root.ui(6)
        color: chip.on ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.22)
             : chipArea.containsMouse
               ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.12)
               : "transparent"
        border.width: 1
        border.color: chip.on || chipArea.containsMouse ? root.cAccent : root.cLine
        Behavior on color { ColorAnimation { duration: 90 } }

        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.on ? root.cAccent : root.cText
            font.family: root.uiFont || "sans-serif"
            font.pixelSize: root.ui(chip.primary ? 15 : 12)
        }
        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }

    // ── What is playing ─────────────────────────────────────────────────────
    property string state: "stopped"
    property string nowPath: ""
    property string nowTitle: ""
    property real   pos: 0
    property real   duration: 0
    property real   volume: 100
    property int    index: -1
    property int    count: 0

    readonly property bool live: root.state !== "stopped"
    readonly property bool playing: root.state === "playing"

    // ⛔ THE BAR IS NOT DRIVEN WHILE IT IS BEING DRAGGED. `pos` updates four
    // times a second, and a handle bound to it snaps back under the finger
    // between the press and the seek landing — which reads as a seek bar that
    // fights you.
    property bool seeking: false
    property real seekAt: 0

    // Which list is on screen. ⛔ ON root, NOT on the Rectangle that holds the
    // tabs: a property reached through `parent` cannot be resolved statically,
    // and quickshell REFUSES a file whose bindings name a member it cannot
    // find — while qmllint reports it as a mere Warning, so an "only warnings"
    // gate passes a window that never opens.
    property int tab: 0

    ListModel { id: queue }
    ListModel { id: history }
    ListModel { id: playlists }
    ListModel { id: found }

    // Filled between a `-begin` and its `-end`, then swapped in. ⚠ Clearing the
    // live model at `-begin` and appending as rows arrive makes every list
    // flicker empty four times a second.
    property var pending: []

    // Rows the engine had to leave out of the queue list, from its `q-more`
    // record. ⚠ Reset at `q-begin`, so a queue that shrinks back under the
    // ceiling stops claiming there is more.
    property int queueMore: 0

    function fmt(t) {
        if (!(t > 0)) return "0:00"
        const s = Math.floor(t % 60), m = Math.floor(t / 60) % 60, h = Math.floor(t / 3600)
        const ss = (s < 10 ? "0" : "") + s
        return h > 0 ? h + ":" + (m < 10 ? "0" : "") + m + ":" + ss : m + ":" + ss
    }

    // A line the window says back. ⚠ TRANSIENT: it clears itself, because a
    // message with no way to dismiss it is one that is still on screen an hour
    // later describing something that stopped being true.
    property string note: ""
    function say(t) { root.note = t; noteClear.restart() }
    Timer { id: noteClear; interval: 4000; onTriggered: root.note = "" }

    /*
     * ⚠ A DROPPED URL IS PERCENT-ENCODED, and it is not the path.
     * A file manager hands over `file:///home/v/A%20Film%20(2001).mkv`; passed
     * along as-is, mpv is asked to open a file whose name contains a literal
     * `%20`. Decoded once, here, so the drop and the pickers cannot disagree
     * about what a path is.
     *
     * ⛔ ANYTHING THAT IS NOT file:// IS RETURNED WHOLE. An http:// URL dragged
     * out of a browser is a thing mpv can play — via yt-dlp — and stripping it
     * to a "path" would break the one case that needs no filesystem at all.
     */
    function urlToPath(u) {
        const t = ("" + u).trim()
        if (t === "") return ""
        if (t.indexOf("file://") !== 0) return t
        try { return decodeURIComponent(t.substring(7)) }
        catch (e) { return t.substring(7) }
    }

    /*
     * What a drop actually does, kept out of the handler on purpose.
     *
     * ⛔ A DragEvent CANNOT BE CONSTRUCTED, so nothing can drive `onDropped`
     * with a url in it — a Qt INTERNAL drag reaches the DropArea and matches its
     * keys, but carries no mimeData, so `hasUrls` is false and this never runs.
     * (Measured, not assumed.) Everything that can go wrong with the payload —
     * the percent-decoding, which item replaces and which append, a folder being
     * handed over whole — lives here instead, where the suite drives it with the
     * exact bytes a file manager sends and checks the queue that came out.
     *
     * ⚠ A FOLDER NEEDS NOTHING SPECIAL. mpv expands a directory into its
     * playable files itself, so a dropped folder is the same call as a dropped
     * file and there is no second idea here of what is playable.
     */
    function acceptDrop(urls, playNow) {
        const paths = []
        for (let i = 0; i < urls.length; i++) paths.push(root.urlToPath(urls[i]))
        root.openAll(paths, !playNow)
    }

    // ⛔ FIRST REPLACES, THE REST APPEND — the same rule as `syn-play a b c` on
    // the command line. Two front ends that disagree about what handing over
    // three files means is exactly the split this program is built to avoid.
    function openAll(paths, append) {
        let n = 0
        for (let i = 0; i < paths.length; i++) {
            const p = paths[i]
            if (p === "") continue
            root.send(((append || n > 0) ? "add " : "play ") + encodeURIComponent(p))
            n++
        }
        if (n === 0) return
        // ⛔ FOUR WHOLE SENTENCES, NOT THREE PIECES GLUED. "Queued " + n +
        // " items" can never match a msgid — whole-cell lookup is the rule —
        // and the count needs a plural form the concatenation cannot carry.
        root.say(append ? I18n.trn("Queued %1 item", "Queued %1 items", n).arg(n)
                        : I18n.trn("Playing %1 item", "Playing %1 items", n).arg(n))
    }

    function dec(s) {
        if (s === undefined || s === "") return ""
        if (s.indexOf("%") < 0) return s
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    function swap(model, rows) {
        model.clear()
        for (let i = 0; i < rows.length; i++) model.append(rows[i])
    }

    function onRecord(line) {
        if (line === "") return
        const f = line.split("\t")
        const tag = f[0]

        if (tag === "s") {
            const k = f[1], v = f.length > 2 ? f[2] : ""
            if (k === "state")         root.state = v
            else if (k === "path")     root.nowPath = root.dec(v)
            else if (k === "title")    root.nowTitle = root.dec(v)
            else if (k === "pos")      { if (!root.seeking) root.pos = parseFloat(v) }
            else if (k === "duration") root.duration = parseFloat(v)
            else if (k === "volume")   root.volume = parseFloat(v)
            else if (k === "index")    root.index = parseInt(v)
            else if (k === "count")    root.count = parseInt(v)
        } else if (tag === "q-begin" || tag === "h-begin" ||
                   tag === "l-begin" || tag === "f-begin") {
            root.pending = []
            if (tag === "q-begin") root.queueMore = 0
        } else if (tag === "q") {
            root.pending.push({ idx: parseInt(f[1]), current: f[2] === "1",
                                title: root.dec(f[3]), path: root.dec(f[4]) })
        } else if (tag === "h") {
            root.pending.push({ when: parseInt(f[1]), pos: parseFloat(f[2]),
                                dur: parseFloat(f[3]),
                                title: root.dec(f[4]), path: root.dec(f[5]) })
        } else if (tag === "playlist") {
            // ⚠ `sp_playlist_list()`'s own --rec record, forwarded verbatim
            // between l-begin and l-end rather than rewritten — one lister, and
            // the CLI, the TUI and this window all read its output.
            root.pending.push({ name: f[1], count: parseInt(f[2]) })
        } else if (tag === "f") {
            root.pending.push({ title: root.dec(f[1]), path: root.dec(f[2]) })
        } else if (tag === "q-more") {
            // ⚠ Held aside rather than pushed: `pending` becomes the model, and
            // a row that is not a queue entry would draw as a blank one.
            root.queueMore = parseInt(f[1])
        } else if (tag === "q-end") { root.swap(queue, root.pending)
        } else if (tag === "h-end") { root.swap(history, root.pending)
        } else if (tag === "l-end") { root.swap(playlists, root.pending)
        } else if (tag === "f-end") { root.swap(found, root.pending); root.searching = false
        } else if (tag === "e") {
            root.ready = true
        }
    }

    // ⛔ NOTHING MAY BE WRITTEN BEFORE THE ENGINE HAS SPAWNED. A Process write
    // made before the child exists is DROPPED, in silence — so a button pressed
    // the moment the window opened would do nothing at all, once, in a way
    // nobody can reproduce. The engine's first record is the proof it is there.
    property bool ready: false
    property var queued: []
    property bool searching: false

    function send(cmd) {
        if (!root.ready) { root.queued.push(cmd); return }
        eng.write(cmd + "\n")
    }

    onReadyChanged: {
        if (!root.ready) return
        for (const q of root.queued) eng.write(q + "\n")
        root.queued = []
    }

    /*
     * ── Opening a file, and opening a folder ────────────────────────────────
     *
     * ⛔ zenity, NOT a QML FileDialog. QtQuick.Dialogs is not shipped by
     * quickshell, so `FileDialog` is a type this window cannot import — the
     * same reason syn's Resolve installer picks its zip this way. zenity is
     * already in this suite's dependency graph and its chooser is one line.
     *
     * ⚠ A FOLDER NEEDS NO SPECIAL PATH. mpv expands a directory into its
     * playable files itself (`--directory-mode`, default `auto`), so "open
     * folder" is the same `loadfile` as "open file" and there is no second
     * walk here to disagree with mpv's about what is playable.
     */
    property bool hasPicker: true

    Process {
        // Asked once, rather than discovered when somebody presses the button
        // and nothing happens. ⚠ `command -v` and not `which`: which is not
        // installed everywhere and answers 0 for a shell builtin.
        running: true
        command: ["sh", "-c", "command -v zenity >/dev/null 2>&1"]
        onExited: (code) => root.hasPicker = (code === 0)
    }

    Process {
        id: pickFiles
        // ⚠ A NEWLINE SEPARATOR, because the default is `|` and a filename may
        // contain one. A newline cannot appear in a path on Linux… except it
        // can; it is merely the least bad of the separators zenity offers, and
        // the rows are trimmed and empty ones dropped below.
        // ⚠ THE TITLE IS ZENITY'S WINDOW TITLE — a person reads it, so it is
        // built rather than written whole: the `--title=` half is the flag.
        command: ["zenity", "--file-selection", "--multiple", "--separator=\n",
                  "--title=" + I18n.tr("Open files")]
        running: false
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = this.text.split("\n").map(x => x.trim()).filter(x => x !== "")
                if (lines.length === 0) return          // cancelled, which is not an error
                root.openAll(lines.map(root.urlToPath), false)
            }
        }
    }

    Process {
        id: pickFolder
        command: ["zenity", "--file-selection", "--directory",
                  "--title=" + I18n.tr("Open a folder")]
        running: false
        stdout: StdioCollector {
            onStreamFinished: {
                const dir = this.text.trim()
                if (dir === "") return
                root.openAll([root.urlToPath(dir)], false)
            }
        }
    }

    // ⚠ NOT `pick()`. That name is already the theme's dark/light chooser a
    // hundred lines up, and a second function of the same name silently
    // replaces it — every colour in this window resolved through a function
    // expecting a boolean. qmllint calls that a Warning, which is the level an
    // "only warnings" gate lets through.
    function openPicker(folder) {
        if (!root.hasPicker) {
            root.say(I18n.tr("zenity is not installed — synpkg install zenity"))
            return
        }
        // ⚠ `running = true` on a process that is ALREADY running is a silent
        // no-op in quickshell, so a second press while the chooser is open
        // would do nothing and read as a dead button. It is already open; say
        // so instead.
        const proc = folder ? pickFolder : pickFiles
        if (proc.running) { root.say(I18n.tr("the chooser is already open")); return }
        proc.running = true
    }

    Process {
        id: eng
        running: true
        stdinEnabled: true
        command: [root.bin, "serve"]
        stdout: SplitParser {
            splitMarker: "\n"
            onRead: (line) => root.onRecord(line)
        }
        // The engine exiting is the window's cue to go too — otherwise the
        // window sits there taking presses that reach nothing.
        onExited: Qt.quit()
    }

    // ── Chrome ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // ── Quick open ──────────────────────────────────────────────────────
        //
        // ⚠ AT THE TOP, AND FOCUSED. Opening something is the reason this window
        // gets opened, and a search box below three lists is one nobody finds.
        Rectangle {
            id: openBar
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: root.ui(46)
            color: root.cPanel

            // ⚠ THE BUTTONS ARE PINNED AND THE FIELD TAKES WHAT IS LEFT.
            // A Row holding all three fits at 620 wide and runs off the left at
            // 380 — the same failure the assistant's header shipped with. There
            // is one edge to anchor the field to, whatever this window is doing.
            Row {
                id: openBtns
                anchors { right: parent.right; rightMargin: root.ui(8)
                          verticalCenter: parent.verticalCenter }
                spacing: root.ui(6)

                // ⚠ SHORTER LABELS RATHER THAN NO LABELS. At the minimum width
                // the words still say which is which; hiding one of them would
                // leave two identical buttons doing different things.
                readonly property bool roomy: root.width >= root.ui(560)

                Chip {
                    label: openBtns.roomy ? I18n.tr("Open files…") : I18n.tr("Files…")
                    onClicked: root.openPicker(false)
                }
                Chip {
                    label: openBtns.roomy ? I18n.tr("Open folder…") : I18n.tr("Folder…")
                    onClicked: root.openPicker(true)
                }
            }

            Rectangle {
                anchors { left: parent.left; right: openBtns.left; top: parent.top
                          bottom: parent.bottom; margins: root.ui(8)
                          rightMargin: root.ui(8) }
                radius: root.ui(6)
                color: root.cBg
                border.width: 1
                border.color: openField.activeFocus ? root.cAccent : root.cLine

                TextInput {
                    id: openField
                    anchors { fill: parent; leftMargin: root.ui(10)
                              rightMargin: root.ui(10) }
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    color: root.cText
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(13)
                    focus: true

                    // Typing searches; Enter plays the first hit. ⚠ DEBOUNCED —
                    // a walk of the media roots per keystroke is a search box
                    // that stutters on a library of any size.
                    onTextChanged: findTimer.restart()

                    Keys.onReturnPressed: {
                        if (found.count > 0) {
                            root.send("play " + encodeURIComponent(found.get(0).path))
                            openField.text = ""
                            found.clear()
                        }
                    }
                    Keys.onEscapePressed: { openField.text = ""; found.clear() }

                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        visible: openField.text === ""
                        text: I18n.tr("Open something — a few letters of the name")
                        color: root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(13)
                    }
                }
            }

            Timer {
                id: findTimer
                interval: 180
                onTriggered: {
                    const q = openField.text.trim()
                    if (q === "") { found.clear(); return }
                    root.searching = true
                    root.send("find " + encodeURIComponent(q))
                }
            }
        }

        // ── Now playing ─────────────────────────────────────────────────────
        Rectangle {
            id: now
            anchors { top: openBar.bottom; left: parent.left; right: parent.right }
            height: nowCol.implicitHeight + root.ui(20)
            color: root.cPanel

            Column {
                id: nowCol
                anchors { left: parent.left; right: parent.right
                          verticalCenter: parent.verticalCenter
                          leftMargin: root.ui(14); rightMargin: root.ui(14) }
                spacing: root.ui(8)

                Text {
                    width: parent.width
                    text: root.live && root.nowTitle !== "" ? root.nowTitle
                                                            : I18n.tr("Nothing playing")
                    elide: Text.ElideRight
                    color: root.live ? root.cText : root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(15)
                }

                // ── The seek bar ────────────────────────────────────────────
                Item {
                    width: parent.width
                    height: root.ui(18)

                    Rectangle {
                        id: track
                        anchors { left: parent.left; right: timeLabel.left
                                  rightMargin: root.ui(10)
                                  verticalCenter: parent.verticalCenter }
                        height: root.ui(5)
                        radius: height / 2
                        color: root.cLine

                        readonly property real frac:
                            root.duration > 0
                            ? Math.max(0, Math.min(1, (root.seeking ? root.seekAt : root.pos)
                                                      / root.duration))
                            : 0

                        Rectangle {
                            width: track.width * track.frac
                            height: parent.height
                            radius: parent.radius
                            color: root.cAccent
                        }
                        Rectangle {
                            visible: root.duration > 0
                            x: track.width * track.frac - width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: root.ui(11); height: width
                            radius: width / 2
                            color: root.cAccent
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -root.ui(7)   // a 5px bar is not a target
                            enabled: root.duration > 0
                            cursorShape: Qt.PointingHandCursor
                            function at(x) {
                                return Math.max(0, Math.min(1, x / track.width)) * root.duration
                            }
                            onPressed: (e) => { root.seeking = true; root.seekAt = at(e.x) }
                            onPositionChanged: (e) => { if (root.seeking) root.seekAt = at(e.x) }
                            onReleased: (e) => {
                                root.seekAt = at(e.x)
                                root.send("seek-abs " + root.seekAt.toFixed(3))
                                // ⚠ HELD UNTIL THE ENGINE CATCHES UP. Released
                                // immediately, the next `pos` record is still
                                // the OLD position — mpv has not seeked yet —
                                // and the handle jumps back before going where
                                // it was put.
                                seekSettle.restart()
                            }
                        }
                    }

                    Timer {
                        id: seekSettle
                        interval: 400
                        onTriggered: root.seeking = false
                    }

                    Text {
                        id: timeLabel
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: root.fmt(root.seeking ? root.seekAt : root.pos)
                              + " / " + root.fmt(root.duration)
                        color: root.cDim
                        font.family: "monospace"
                        font.pixelSize: root.ui(11)
                    }
                }

                // ── Transport ───────────────────────────────────────────────
                Row {
                    spacing: root.ui(8)

                    Chip { label: "⏮"; onClicked: root.send("prev") }
                    Chip {
                        label: root.playing ? "⏸" : "▶"
                        primary: true
                        onClicked: root.send("toggle")
                    }
                    Chip { label: "⏭"; onClicked: root.send("next") }

                    Item { width: root.ui(6); height: 1 }

                    // ⛔ MPV'S SHUFFLE, AND MPV'S UNDO. `unshuffle` restores the
                    // order the files were ADDED in — mpv keeps that, and
                    // nothing here could reconstruct it after the fact.
                    Chip { label: I18n.tr("Shuffle");   onClicked: root.send("shuffle") }
                    Chip { label: I18n.tr("Unshuffle"); onClicked: root.send("unshuffle") }

                    Item { width: root.ui(6); height: 1 }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.count > 0 ? (root.index + 1) + " / " + root.count : ""
                        color: root.cDim
                        font.family: "monospace"
                        font.pixelSize: root.ui(11)
                    }
                }
            }
        }

        // ── The lists ───────────────────────────────────────────────────────

        Rectangle {
            id: tabs
            anchors { top: now.bottom; left: parent.left; right: parent.right }
            height: root.ui(34)
            color: root.cBg

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: root.cLine
            }

            Row {
                anchors { left: parent.left; leftMargin: root.ui(12)
                          verticalCenter: parent.verticalCenter }
                spacing: root.ui(4)

                Repeater {
                    // ⚠ THE NUMBER IS THE TAB'S IDENTITY and the word is what
                    // is drawn. `root.tab` is compared against the number, so
                    // there is no id-and-label pair here that could be marked
                    // by mistake.
                    model: [[I18n.tr("Queue"), 0], [I18n.tr("History"), 1],
                            [I18n.tr("Playlists"), 2]]
                    Item {
                        id: tabItem
                        required property var modelData
                        readonly property bool on: root.tab === tabItem.modelData[1]
                        width: tabLabel.implicitWidth + root.ui(20)
                        height: root.ui(28)

                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: tabItem.modelData[0]
                            color: tabItem.on ? root.cAccent : root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(12)
                        }
                        Rectangle {
                            visible: tabItem.on
                            anchors { left: parent.left; right: parent.right
                                      bottom: parent.bottom }
                            height: root.ui(2)
                            color: root.cAccent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.tab = tabItem.modelData[1]
                        }
                    }
                }
            }

            // Saving the queue lives beside the tab it belongs to rather than in
            // the transport row: it is about the list, not about playback.
            // What the window has to say back, where the eye already is. ⚠ It
            // takes the row from the buttons rather than sharing it: two things
            // fighting for the same pixels is how a message ends up half drawn
            // under a control.
            Text {
                anchors { right: parent.right; left: parent.left
                          rightMargin: root.ui(12); leftMargin: root.ui(120)
                          verticalCenter: parent.verticalCenter }
                visible: root.note !== ""
                text: root.note
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                color: root.cAccent
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(11)
            }

            Chip {
                anchors { right: parent.right; rightMargin: root.ui(12)
                          verticalCenter: parent.verticalCenter }
                visible: root.note === "" && root.tab === 0 && queue.count > 0
                label: I18n.tr("Save as playlist")
                onClicked: saveBox.open()
            }
            Chip {
                anchors { right: parent.right; rightMargin: root.ui(12)
                          verticalCenter: parent.verticalCenter }
                visible: root.note === "" && root.tab === 1 && history.count > 0
                label: I18n.tr("Clear history")
                onClicked: root.send("history-clear")
            }
        }

        // ── Quick-open results, over the lists while there is a query ───────
        //
        // ⚠ AN OVERLAY, NOT A FOURTH TAB. The results are about the thing being
        // typed right now; a tab would keep them after the query that produced
        // them had gone, and would make finding something a two-step.
        Rectangle {
            id: results
            visible: openField.text.trim() !== ""
            anchors { top: tabs.bottom; left: parent.left; right: parent.right
                      bottom: parent.bottom }
            color: root.cBg
            z: 2

            ListView {
                id: resultView
                anchors { fill: parent; margins: root.ui(6) }
                clip: true
                model: found
                spacing: root.ui(2)

                // ⛔ A VIEW THAT SCROLLS SHOWS THAT IT SCROLLS.
                ScrollBar.vertical: SynScrollBar {}

                delegate: Rectangle {
                    id: fRow
                    required property string title
                    required property string path
                    required property int index
                    width: resultView.width
                    height: root.ui(30)
                    radius: root.ui(4)
                    color: fArea.containsMouse ? root.cRow : "transparent"
                    /*
                     * ⛔ THE ROW-WIDE MOUSEAREA IS DECLARED FIRST, AND THAT IS THE
                     * WHOLE REASON THE INLINE LINKS IN THESE ROWS WORK.
                     *
                     * A later sibling is stacked ON TOP in QML, and a press goes
                     * to the topmost item that accepts it. With this area written
                     * last it covered every link in its own row: the ✕ on a
                     * playlist sent `plload`, the ✕ on a queue row sent `jump`.
                     * The button was hit and the wrong thing happened, silently.
                     *
                     * ⚠ Hover is not affected either way. The link areas are not
                     * hoverEnabled, so `containsMouse` here stays true underneath
                     * them and a link does not vanish as the pointer reaches it.
                     * tests/qml_test.sh drives a real pointer over both.
                     */
                    MouseArea {
                        id: fArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.send("play " + encodeURIComponent(fRow.path))
                            openField.text = ""
                            found.clear()
                        }
                    }

                    Text {
                        anchors { left: parent.left; right: playHere.left
                                  leftMargin: root.ui(10); rightMargin: root.ui(8)
                                  verticalCenter: parent.verticalCenter }
                        text: fRow.title
                        elide: Text.ElideRight
                        color: fRow.index === 0 ? root.cText : root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(12)
                    }
                    Text {
                        id: playHere
                        anchors { right: parent.right; rightMargin: root.ui(10)
                                  verticalCenter: parent.verticalCenter }
                        visible: fArea.containsMouse
                        text: I18n.tr("queue")
                        color: root.cAccent
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(11)
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -root.ui(4)
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.send("add " + encodeURIComponent(fRow.path))
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: found.count === 0 && !root.searching
                text: I18n.tr("Nothing matches that.")
                color: root.cDim
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(12)
            }
        }

        // ── Queue ───────────────────────────────────────────────────────────
        ListView {
            id: queueView
            visible: root.tab === 0
            anchors { top: tabs.bottom; left: parent.left; right: parent.right
                      bottom: parent.bottom; margins: root.ui(6) }
            clip: true
            model: queue
            spacing: root.ui(2)

            ScrollBar.vertical: SynScrollBar {}

            delegate: Rectangle {
                id: qRow
                required property int idx
                required property bool current
                required property string title
                width: queueView.width
                height: root.ui(30)
                radius: root.ui(4)
                color: qRow.current ? root.cRow
                     : qArea.containsMouse ? Qt.rgba(root.cAccent.r, root.cAccent.g,
                                                     root.cAccent.b, 0.07)
                                           : "transparent"
                /* ⛔ FIRST, so the ✕ below is stacked ABOVE it — written last it
                 * covered the ✕ and a click there sent `jump`. The search
                 * delegate above has the whole of it. */
                MouseArea {
                    id: qArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.send("jump " + qRow.idx)
                }

                Text {
                    anchors { left: parent.left; leftMargin: root.ui(10)
                              verticalCenter: parent.verticalCenter }
                    width: root.ui(26)
                    text: qRow.current ? "▶" : (qRow.idx + 1)
                    color: qRow.current ? root.cAccent : root.cDim
                    font.family: "monospace"
                    font.pixelSize: root.ui(11)
                }
                Text {
                    anchors { left: parent.left; leftMargin: root.ui(38)
                              right: qDrop.left; rightMargin: root.ui(8)
                              verticalCenter: parent.verticalCenter }
                    text: qRow.title
                    elide: Text.ElideRight
                    color: qRow.current ? root.cText : root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                }
                Text {
                    id: qDrop
                    anchors { right: parent.right; rightMargin: root.ui(10)
                              verticalCenter: parent.verticalCenter }
                    visible: qArea.containsMouse
                    text: "✕"
                    color: root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -root.ui(5)
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.send("remove " + qRow.idx)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: queue.count === 0
                text: I18n.tr("The queue is empty — open something above.")
                color: root.cDim
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(12)
            }

            // ⚠ THE LIST HAS A CEILING AND SAYS SO. The engine sends at most
            // 4096 rows; a queue longer than that is drawn short, and silence
            // there would read as "that is the whole queue".
            Text {
                anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                visible: root.queueMore > 0
                text: I18n.trn("… and %1 more not shown here",
                               "… and %1 more not shown here",
                               root.queueMore).arg(root.queueMore)
                color: root.cDim
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(11)
            }
        }

        // ── History ─────────────────────────────────────────────────────────
        ListView {
            id: histView
            visible: root.tab === 1
            anchors { top: tabs.bottom; left: parent.left; right: parent.right
                      bottom: parent.bottom; margins: root.ui(6) }
            clip: true
            model: history
            spacing: root.ui(2)

            ScrollBar.vertical: SynScrollBar {}

            delegate: Rectangle {
                id: hRow
                required property string title
                required property string path
                required property real pos
                required property real dur
                width: histView.width
                height: root.ui(30)
                radius: root.ui(4)
                color: hArea.containsMouse ? root.cRow : "transparent"
                /* ⛔ FIRST, so `queue` below is stacked ABOVE it — see the search
                 * delegate. */
                MouseArea {
                    id: hArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // ⛔ NO SEEK ON THE WAY IN. mpv wrote a watch-later file when
                    // this stopped and resumes from it on its own; seeking to the
                    // position in our history on top of that would be a second,
                    // staler answer fighting the correct one — visibly, as a jump
                    // a second after playback starts.
                    onClicked: root.send("play " + encodeURIComponent(hRow.path))
                }

                // ⚠ The position is shown only when it MEANS something. "0:00
                // in" beside every row is noise that hides the two rows where it
                // is the whole point — the film somebody is 40 minutes into.
                readonly property bool part:
                    hRow.pos > 30 && (hRow.dur <= 0 || hRow.pos < hRow.dur - 30)

                Text {
                    anchors { left: parent.left; leftMargin: root.ui(10)
                              right: hAt.left; rightMargin: root.ui(8)
                              verticalCenter: parent.verticalCenter }
                    text: hRow.title
                    elide: Text.ElideRight
                    color: root.cText
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                }
                Text {
                    id: hAt
                    anchors { right: hQueue.left; rightMargin: root.ui(10)
                              verticalCenter: parent.verticalCenter }
                    // Whole cell: "0:61 in" concatenated could never match a
                    // msgid, and the word may not follow the time in every
                    // language.
                    text: hRow.part ? I18n.tr("%1 in").arg(root.fmt(hRow.pos)) : ""
                    color: root.cAccent
                    font.family: "monospace"
                    font.pixelSize: root.ui(10)
                }
                Text {
                    id: hQueue
                    anchors { right: parent.right; rightMargin: root.ui(10)
                              verticalCenter: parent.verticalCenter }
                    visible: hArea.containsMouse
                    text: I18n.tr("queue")
                    color: root.cAccent
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(11)
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -root.ui(4)
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.send("add " + encodeURIComponent(hRow.path))
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: history.count === 0
                text: I18n.tr("Nothing played yet.")
                color: root.cDim
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(12)
            }
        }

        // ── Playlists ───────────────────────────────────────────────────────
        ListView {
            id: plView
            visible: root.tab === 2
            anchors { top: tabs.bottom; left: parent.left; right: parent.right
                      bottom: parent.bottom; margins: root.ui(6) }
            clip: true
            model: playlists
            spacing: root.ui(2)

            ScrollBar.vertical: SynScrollBar {}

            delegate: Rectangle {
                id: pRow
                required property string name
                required property int count
                width: plView.width
                height: root.ui(32)
                radius: root.ui(4)
                color: pArea.containsMouse ? root.cRow : "transparent"
                /* ⛔ FIRST, so `queue` and the ✕ below are stacked ABOVE it.
                 * Written last it covered both, and the ✕ sent `plload` instead
                 * of `plrm` — a delete button that played the playlist. */
                MouseArea {
                    id: pArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.send("plload " + encodeURIComponent(pRow.name))
                }

                Text {
                    anchors { left: parent.left; leftMargin: root.ui(10)
                              right: pCount.left; rightMargin: root.ui(8)
                              verticalCenter: parent.verticalCenter }
                    text: pRow.name
                    elide: Text.ElideRight
                    color: root.cText
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                }
                Text {
                    id: pCount
                    anchors { right: pAppend.left; rightMargin: root.ui(12)
                              verticalCenter: parent.verticalCenter }
                    text: I18n.trn("%1 item", "%1 items", pRow.count).arg(pRow.count)
                    color: root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(10)
                }
                Text {
                    id: pAppend
                    anchors { right: pDel.left; rightMargin: root.ui(12)
                              verticalCenter: parent.verticalCenter }
                    visible: pArea.containsMouse
                    text: I18n.tr("queue")
                    color: root.cAccent
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(11)
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -root.ui(4)
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.send("plappend " + encodeURIComponent(pRow.name))
                    }
                }
                Text {
                    id: pDel
                    anchors { right: parent.right; rightMargin: root.ui(10)
                              verticalCenter: parent.verticalCenter }
                    visible: pArea.containsMouse
                    text: "✕"
                    color: root.cDim
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(12)
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -root.ui(5)
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.send("plrm " + encodeURIComponent(pRow.name))
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: playlists.count === 0
                text: I18n.tr("No playlists yet — queue some things and Save as playlist.")
                wrapMode: Text.Wrap
                width: plView.width - root.ui(40)
                horizontalAlignment: Text.AlignHCenter
                color: root.cDim
                font.family: root.uiFont || "sans-serif"
                font.pixelSize: root.ui(12)
            }
        }

        /*
         * ── Files dragged in ────────────────────────────────────────────────
         *
         * ⛔ KEYED, AND FILES ONLY. A DropArea filling the window is the topmost
         * target for EVERY drag that crosses it — including, in a window that
         * had one, the application's own. `keys` is what stops that: a DropArea
         * whose keys do not match is never entered and delivery carries on
         * underneath it. There is no internal drag here yet; declaring the keys
         * now is what makes it safe to add one later, which is the exact bug
         * synstudio lost three releases to.
         *
         * ⚠ TWO ZONES, BECAUSE THE ANSWER IS NOT OBVIOUS. Dropping a film while
         * an album is playing could reasonably mean either thing, and a player
         * that silently picked one would be wrong half the time — destructively,
         * in the case where it wipes a queue somebody spent a while building.
         * The halves are the whole window, so neither has to be aimed at.
         */
        DropArea {
            id: fileDrop
            anchors.fill: parent
            keys: ["text/uri-list", "text/plain"]
            z: 10

            property bool onTop: true

            // ⚠ THE HALVING RULE, NAMED. A DragEvent cannot be built by hand,
            // so a test can drive neither handler below — but it can drive this,
            // and getting the comparison backwards is the whole of what the
            // zones can get wrong.
            function zoneFor(y) { return y < fileDrop.height / 2 }

            onEntered: (d) => {
                // ⚠ ASKED, NOT ASSUMED. A drag carrying no urls is somebody
                // dragging text about; refusing it here lets delivery CARRY ON
                // underneath rather than swallowing the gesture and showing an
                // overlay for a drop that could never have worked.
                if (!d.hasUrls) { d.accepted = false; return }
                fileDrop.onTop = fileDrop.zoneFor(d.y)
            }
            onPositionChanged: (d) => { fileDrop.onTop = fileDrop.zoneFor(d.y) }

            onDropped: (d) => {
                if (!d.hasUrls) { d.accepted = false; return }
                root.acceptDrop(d.urls, fileDrop.onTop)
                d.accept()
            }

            Rectangle {
                anchors.fill: parent
                visible: fileDrop.containsDrag
                color: Qt.rgba(root.cBg.r, root.cBg.g, root.cBg.b, 0.86)

                Repeater {
                    // ⚠ The boolean is the zone's identity; only the word is
                    // drawn.
                    model: [[true, I18n.tr("Play now")],
                            [false, I18n.tr("Add to the queue")]]
                    Rectangle {
                        id: zone
                        required property var modelData
                        readonly property bool isTop: zone.modelData[0]
                        readonly property bool active: fileDrop.onTop === zone.isTop

                        y: zone.isTop ? 0 : parent.height / 2
                        width: parent.width
                        height: parent.height / 2
                        color: zone.active
                               ? Qt.rgba(root.cAccent.r, root.cAccent.g,
                                         root.cAccent.b, 0.16)
                               : "transparent"

                        Rectangle {
                            anchors { fill: parent; margins: root.ui(10) }
                            radius: root.ui(8)
                            color: "transparent"
                            border.width: zone.active ? 2 : 1
                            border.color: zone.active ? root.cAccent : root.cLine
                        }
                        Text {
                            anchors.centerIn: parent
                            text: zone.modelData[1]
                            color: zone.active ? root.cAccent : root.cDim
                            font.family: root.uiFont || "sans-serif"
                            font.pixelSize: root.ui(zone.active ? 15 : 13)
                        }
                    }
                }
            }
        }

        // ── Naming a playlist ───────────────────────────────────────────────
        //
        // ⚠ A STRIP, NOT A MODAL. One field and two answers; a dialog over the
        // window would take the keyboard from a search box somebody may be half
        // way through typing into.
        Rectangle {
            id: saveBox
            visible: false
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: root.ui(52)
            color: root.cPanel
            z: 3

            function open() { saveBox.visible = true; nameField.text = ""
                              nameField.forceActiveFocus() }
            function close() { saveBox.visible = false; openField.forceActiveFocus() }

            Rectangle {
                anchors { left: parent.left; right: saveGo.left; top: parent.top
                          bottom: parent.bottom; margins: root.ui(9)
                          rightMargin: root.ui(8) }
                radius: root.ui(6)
                color: root.cBg
                border.width: 1
                border.color: nameField.activeFocus ? root.cAccent : root.cLine

                TextInput {
                    id: nameField
                    anchors { fill: parent; leftMargin: root.ui(10)
                              rightMargin: root.ui(10) }
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    color: root.cText
                    font.family: root.uiFont || "sans-serif"
                    font.pixelSize: root.ui(13)

                    // ⛔ THE SAME RULE THE ENGINE ENFORCES, said before the press
                    // rather than after it. A playlist name becomes a filename;
                    // `../x` is refused there, and a window that let it be typed
                    // and then silently did nothing would be worse than one that
                    // never offered it.
                    readonly property bool nameOk:
                        nameField.text.trim().length > 0 &&
                        nameField.text.trim().length <= 120 &&
                        nameField.text.trim()[0] !== "." &&
                        /^[A-Za-z0-9 ._-]+$/.test(nameField.text.trim())

                    Keys.onReturnPressed: if (nameField.nameOk) saveGo.clicked()
                    Keys.onEscapePressed: saveBox.close()

                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        visible: nameField.text === ""
                        text: I18n.tr("Name this playlist")
                        color: root.cDim
                        font.family: root.uiFont || "sans-serif"
                        font.pixelSize: root.ui(13)
                    }
                }
            }

            Chip {
                id: saveGo
                anchors { right: saveCancel.left; rightMargin: root.ui(8)
                          verticalCenter: parent.verticalCenter }
                label: I18n.tr("Save")
                on: nameField.nameOk
                onClicked: {
                    if (!nameField.nameOk) return
                    root.send("plsave " + encodeURIComponent(nameField.text.trim()))
                    saveBox.close()
                }
            }
            Chip {
                id: saveCancel
                anchors { right: parent.right; rightMargin: root.ui(12)
                          verticalCenter: parent.verticalCenter }
                label: I18n.tr("Cancel")
                onClicked: saveBox.close()
            }
        }
    }
}
