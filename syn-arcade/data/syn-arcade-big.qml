// syn-arcade big screen mode — the ten-foot interface.
//
// A television is not a monitor with a bigger number. It is four metres away,
// driven with a controller by somebody who is not sitting at a desk, and every
// desktop assumption fails at that distance: there is no pointer, no window to
// drag, no 12px label anybody can read, and no keyboard within reach. So this
// file has exactly one job — draw big things and let a direction and a button
// move between them.
//
// ── A renderer, and nothing more ────────────────────────────────────────────
//
// Every fact on screen arrives as a record from `syn-arcade big …`. This file
// knows how to draw a tile; it knows nothing about Steam's library format,
// where cover art is cached, or what "Sleep" runs. Pressing a tile calls the
// binary back — `big launch <appid>`, `big run <id>` — rather than running
// anything itself, so what a tile does is decided in one place and the same
// answer is available over SSH.
//
// ⚠ That is not tidiness, it is the SIGPIPE bug. A launcher started from here
// inherits quickshell's pipes, and quickshell closes them the moment its direct
// child exits — the game then dies on its first line of logging, before it maps
// a window, with every visible sign saying it worked. big.c's spawn_detached()
// is where that is handled, and it can only be handled where the child is made.
//
// ── The three ways in, and the one way out ──────────────────────────────────
//
// Controller, keyboard and mouse all work. The controller is read by
// `syn-arcade big nav`, which turns evdev events into words on a pipe — NOT
// into synthetic key presses. There is no uinput device and nothing the
// compositor can see, so stick drift cannot type into somebody's browser and no
// other application on the machine is affected by any of this.
//
// The way out is a TILE, not just a key. A full-screen surface with exclusive
// keyboard focus that can only be dismissed by a combination somebody has to
// already know is a trap — and on a gamepad there is no combination at all.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Without this, an unqualified `root` inside a delegate is resolved at run time
// and binds to undefined the day it stops resolving — silently, which for a
// screen made entirely of delegates means a blank television.
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland

ShellRoot {
    id: shell

    readonly property string bin: Quickshell.env("SYNARCADE_BIN") || "syn-arcade"

    // Which screen. `syn-arcade big start` resolves this before we are launched
    // — from --output, or by asking synui which output has focus — because no
    // Wayland protocol tells a layer-shell client where the pointer is.
    readonly property string wantOutput: Quickshell.env("SYN_BIG_OUTPUT") || ""

    // ── the data, fetched once at startup ───────────────────────────────────
    //
    // Not re-polled. A library scan is dozens of stat() calls per game and the
    // set of installed games does not change while somebody is looking at it;
    // the one moment it can is when they come back from Steam, and coming back
    // means this process was restarted anyway.
    property var games: []
    property var apps: []
    property bool loaded: false

    // decodeURIComponent THROWS on a percent sequence that is not valid UTF-8,
    // and a game's name comes off a store listing somebody else wrote. Letting
    // that escape would empty the whole screen because of one bad title.
    function disp(s) {
        try { return decodeURIComponent(s) } catch (e) { return s }
    }

    // The first record names the columns, so a column added in C arrives here
    // with no change to this file and the two cannot disagree about field three.
    function parseRecords(text) {
        const lines = String(text).split("\n").filter(l => l.length > 0)
        if (lines.length < 2) return []
        const cols = lines[0].split("\t")
        const out = []
        for (let i = 1; i < lines.length; i++) {
            const f = lines[i].split("\t")
            const o = {}
            for (let c = 0; c < cols.length; c++)
                o[shell.disp(cols[c])] = shell.disp(f[c] !== undefined ? f[c] : "")
            out.push(o)
        }
        return out
    }

    Process {
        id: gamesProc
        command: [shell.bin, "big", "games", "--rec"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                shell.games = shell.parseRecords(this.text)
                shell.loaded = true
            }
        }
    }

    Process {
        id: appsProc
        command: [shell.bin, "big", "apps", "--rec"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: shell.apps = shell.parseRecords(this.text)
        }
    }

    // ── the clock ───────────────────────────────────────────────────────────
    //
    // synui-clock rather than a Date() formatted here, for the same reason the
    // bar does it: synui's Date & Time panel writes clock.state (12/24 hour,
    // seconds, world clocks) and synui-clock is what reads it. Formatting in
    // QML would silently strand every one of those settings on this screen.
    property string clockText: ""
    property string dateText: ""

    Process {
        id: clockProc
        command: ["synui-clock"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const j = JSON.parse(this.text)
                    shell.clockText = j.text || ""
                    shell.dateText = j.date || ""
                } catch (e) {
                    shell.clockText = ""
                }
            }
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: clockProc.running = true
    }

    // ── the controller ──────────────────────────────────────────────────────
    //
    // One line per press. See pad.c: this synthesises nothing, and stops the
    // moment this process does, because it is a child holding the other end of
    // this pipe.
    Process {
        id: navProc
        command: [shell.bin, "big", "nav"]
        running: true
        stdout: SplitParser {
            onRead: (line) => shell.nav(line.trim())
        }
    }

    // ── launching ───────────────────────────────────────────────────────────
    //
    // ⚠ The quit waits for THIS process to exit, and that ordering is
    // load-bearing. quickshell kills a Process's children when it shuts down,
    // so calling Qt.quit() straight after setting `running` races the fork that
    // actually starts the game — sometimes launching nothing at all, on a
    // machine fast enough. big.c returns as soon as it has forked and detached,
    // so the wait is milliseconds.
    property bool quitWhenLaunched: false
    property string launchingName: ""

    function launch(args, name, thenQuit) {
        shell.launchingName = name
        shell.quitWhenLaunched = thenQuit === true
        launchProc.command = [shell.bin].concat(args)
        launchProc.running = true
    }

    Process {
        id: launchProc
        // No parameters on the handler: quickshell's exited(int, QProcess::
        // ExitStatus) has a second type QML cannot resolve, and a typed handler
        // silently never runs.
        onExited: {
            if (shell.quitWhenLaunched) Qt.quit()
            shell.launchingName = ""
        }
    }

    // ── what the tiles are, in the order they are shown ─────────────────────
    //
    // Three shelves. Play first because it is what somebody who just turned the
    // television on wants; the library second, because it is the longest and
    // scrolls; the machine's own switches last, where they cannot be hit by
    // accident on the way to something else.
    readonly property var shelves: {
        const appRows = shell.apps.filter(a => a.kind === "app")
        const actRows = shell.apps.filter(a => a.kind === "action")
        const out = []
        if (appRows.length) out.push({ title: "Play", kind: "app", items: appRows })
        if (shell.games.length) out.push({ title: "Games", kind: "game", items: shell.games })
        if (actRows.length) out.push({ title: "System", kind: "action", items: actRows })
        return out
    }

    // ── where the selection is ──────────────────────────────────────────────
    //
    // One row index and one column index PER ROW, kept when the row changes.
    // Moving down out of a library scrolled to game forty and back up again has
    // to land where it left — a selection that resets to the first tile makes a
    // long shelf unusable, which is the single most common failure of a ten-foot
    // interface.
    property int row: 0
    property var cols: ({})

    function col(r) {
        const v = shell.cols[r]
        return v === undefined ? 0 : v
    }

    function setCol(r, v) {
        const c = shell.cols
        c[r] = v
        shell.cols = c          // reassign: a mutated object emits no change
    }

    function moveRow(d) {
        const n = shell.shelves.length
        if (!n) return
        const next = shell.row + d
        if (next < 0 || next >= n) return   // no wrap: the ends are a landmark
        shell.row = next
    }

    function moveCol(d) {
        const sh = shell.shelves[shell.row]
        if (!sh) return
        const n = sh.items.length
        if (!n) return
        let next = shell.col(shell.row) + d
        if (next < 0) next = 0
        if (next > n - 1) next = n - 1
        shell.setCol(shell.row, next)
    }

    function current() {
        const sh = shell.shelves[shell.row]
        if (!sh) return null
        return sh.items[shell.col(shell.row)] || null
    }

    function activate() {
        const sh = shell.shelves[shell.row]
        const it = shell.current()
        if (!sh || !it) return

        if (sh.kind === "game") {
            // Quits: this surface is on the overlay layer, above everything,
            // so a game launched underneath it would render into a screen it
            // does not own. Super+F10 brings big screen mode back.
            shell.launch(["big", "launch", it.appid], it.name, true)
            return
        }

        // "Desktop" is the way out and runs nothing — closing this IS going
        // back to the desktop, which was there underneath all along.
        if (it.id === "desktop") {
            Qt.quit()
            return
        }

        // Sleep, restart and power off deliberately do NOT quit: the machine is
        // either coming back to this screen or going away entirely, and
        // dropping to the desktop for the half second in between is a flash of
        // somebody's email on a television.
        const stay = sh.kind === "action"
        shell.launch(["big", "run", it.id], it.name, !stay)
    }

    // ── one place where every input arrives ─────────────────────────────────
    //
    // The controller stream and the keyboard both call this, so a key and a
    // button cannot drift apart, and testing one tests the other. That matters
    // here more than usual: the controller path cannot be driven by a test at
    // all (it needs real hardware on the seat), and this makes the keyboard a
    // real proxy for it.
    function nav(cmd) {
        switch (cmd) {
        case "up":         shell.moveRow(-1); break
        case "down":       shell.moveRow(1); break
        case "left":       shell.moveCol(-1); break
        case "right":      shell.moveCol(1); break
        case "page-left":  shell.moveCol(-6); break
        case "page-right": shell.moveCol(6); break
        case "accept":     shell.activate(); break
        // Back goes UP a shelf, and from the top one it leaves. A button that
        // does nothing at the top of the screen is a button somebody presses
        // three times before reaching for the keyboard they left on the table.
        case "back":       if (shell.row > 0) shell.moveRow(-1); else Qt.quit(); break
        case "guide":      Qt.quit(); break
        default: break
        }
    }

    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: win

            required property var modelData
            screen: modelData

            // One window per screen, but only the wanted one is ever shown —
            // and the desktop stays live on every other monitor, which is the
            // point of a couch mode on a machine that also has a desk. An
            // output name that no longer exists (the television was unplugged)
            // falls back to the first screen rather than showing nothing.
            readonly property bool chosen: {
                if (!shell.wantOutput) return win.modelData === Quickshell.screens[0]
                const named = Quickshell.screens.find(s => s.name === shell.wantOutput)
                return named ? win.modelData === named
                             : win.modelData === Quickshell.screens[0]
            }

            visible: chosen

            anchors { top: true; left: true; right: true; bottom: true }

            // ⚠ Overlay, not Top. The bar, the dock and every desktop widget
            // are layer-shell surfaces of their own; on the Top layer this
            // would have a status bar across it and a dock through the bottom
            // row of tiles.
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "syn-arcade-big"

            // Never reserve space. An exclusive zone the size of the screen
            // would shove every window on the monitor into nothing, and they
            // would still be there when this closed.
            exclusionMode: ExclusionMode.Ignore

            // Exclusive: this is the whole interface while it is up, and
            // arrow keys have to reach it rather than whatever was focused
            // before. `focusable` is what asks layer-shell for that.
            focusable: true

            color: "#05060a"

            // ── palette ─────────────────────────────────────────────────────
            readonly property color ink:    "#f2f0fa"
            readonly property color dim:    "#a49cc4"
            readonly property color accent: "#a78bfa"

            // Everything scales off the screen height, so the same file is
            // right on a 1080p television and a 4K one. A fixed pixel size is
            // what makes a desktop UI unreadable on a TV in the first place.
            readonly property real u: Math.max(12, win.height / 54)

            onVisibleChanged: if (visible) keys.forceActiveFocus()
            Component.onCompleted: if (visible) keys.forceActiveFocus()

            // ── background: the selected game's own artwork ─────────────────
            //
            // Steam ships a pre-blurred hero for most titles and big.c prefers
            // it, so this is a plain Image rather than a blur pass — a
            // full-screen gaussian on a 4K panel for a picture Valve already
            // blurred would be an odd thing to spend a frame on.
            Image {
                id: hero
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
                source: {
                    const it = shell.current()
                    return (it && it.hero) ? "file://" + it.hero : ""
                }
                opacity: status === Image.Ready ? 0.5 : 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
            }

            // The scrim. Without it a label sits on whatever colour that
            // screenshot happens to be, which is the failure mode of every
            // artwork-backed launcher: legible on nine games and unreadable on
            // the tenth. Darkest at the bottom, where the shelves are.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#cc05060a" }
                    GradientStop { position: 0.45; color: "#e605060a" }
                    GradientStop { position: 1.0; color: "#f705060a" }
                }
            }

            // ── header ──────────────────────────────────────────────────────
            Item {
                id: header
                anchors { top: parent.top; left: parent.left; right: parent.right }
                anchors.margins: win.u * 1.6
                height: win.u * 3

                Column {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: win.u * 0.2

                    Text {
                        text: "SYNAPSE"
                        color: win.accent
                        font.pixelSize: win.u * 1.5
                        font.letterSpacing: win.u * 0.35
                        font.bold: true
                    }
                    Text {
                        text: "big screen"
                        color: win.dim
                        font.pixelSize: win.u * 0.8
                        font.letterSpacing: win.u * 0.16
                    }
                }

                Column {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: win.u * 0.1

                    Text {
                        anchors.right: parent.right
                        text: shell.clockText
                        color: win.ink
                        font.pixelSize: win.u * 1.4
                    }
                    Text {
                        anchors.right: parent.right
                        text: shell.dateText
                        color: win.dim
                        font.pixelSize: win.u * 0.8
                    }
                }
            }

            // ── the selected thing, named once, large ───────────────────────
            //
            // The title lives HERE and not under every tile. A label under a
            // cover has to be small enough to fit the cover, which at four
            // metres is a label nobody reads; one big name for the selection is
            // legible from the sofa and leaves the artwork uncovered.
            Item {
                id: banner
                anchors { top: header.bottom; left: parent.left; right: parent.right }
                anchors.leftMargin: win.u * 1.6
                anchors.rightMargin: win.u * 1.6
                height: win.u * 5.6

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.7
                    spacing: win.u * 0.3

                    Text {
                        width: parent.width
                        text: {
                            const it = shell.current()
                            return it ? (it.name || "") : ""
                        }
                        color: win.ink
                        font.pixelSize: win.u * 2.6
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        color: win.dim
                        font.pixelSize: win.u * 0.95
                        elide: Text.ElideRight
                        text: {
                            const sh = shell.shelves[shell.row]
                            const it = shell.current()
                            if (!sh || !it) return ""

                            // Deliberately NOT the exec string for the app and
                            // action shelves. "systemctl suspend" under the
                            // word Sleep is a developer's answer to a question
                            // nobody at four metres asked, and it makes every
                            // tile look like a terminal command that might go
                            // wrong.
                            if (sh.kind !== "game") return ""

                            const bits = []
                            const sz = parseFloat(it.size)
                            if (sz > 0) bits.push((sz / 1073741824).toFixed(1) + " GB")
                            const lp = parseFloat(it.lastplayed)
                            // 0 is Steam's "never", not 1970.
                            if (lp > 0)
                                bits.push("last played "
                                          + new Date(lp * 1000).toLocaleDateString(Qt.locale()))
                            return bits.join("   ·   ")
                        }
                    }
                }
            }

            // ── the shelves ─────────────────────────────────────────────────
            Item {
                id: stage
                anchors { top: banner.bottom; left: parent.left; right: parent.right; bottom: footer.top }
                anchors.topMargin: win.u * 0.4
                anchors.bottomMargin: win.u * 0.4
                clip: true

                Column {
                    id: rows
                    width: parent.width
                    spacing: win.u * 1.2

                    // Scrolled so the selected shelf comes up to the top of the
                    // stage. Computed from the real item positions rather than
                    // an assumed row height, because the shelves are different
                    // heights — a game cover is portrait and an app tile is
                    // not.
                    //
                    // ⚠ Clamped to the end of the content, which is the
                    // difference between a launcher and a broken one. Without
                    // it, selecting the last shelf scrolls it to the top and
                    // leaves two thirds of the television empty below it — and
                    // when every shelf fits on screen already, as it does on a
                    // machine with one or two of them, it would scroll for no
                    // reason at all.
                    y: {
                        const items = rows.visibleChildren
                        let off = 0
                        for (let i = 0; i < shell.row && i < items.length; i++)
                            off += items[i].height + rows.spacing
                        const most = Math.max(0, rows.height - stage.height)
                        return -Math.min(off, most)
                    }
                    Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                    Repeater {
                        model: shell.shelves

                        Item {
                            id: shelf
                            required property var modelData
                            required property int index

                            width: rows.width
                            height: label.height + win.u * 0.6 + strip.height
                            opacity: shell.row === shelf.index ? 1.0 : 0.45
                            Behavior on opacity { NumberAnimation { duration: 160 } }

                            Text {
                                id: label
                                x: win.u * 1.6
                                text: shelf.modelData.title
                                color: win.dim
                                font.pixelSize: win.u * 0.9
                                font.letterSpacing: win.u * 0.12
                                font.bold: true
                            }

                            ListView {
                                id: strip
                                anchors.top: label.bottom
                                anchors.topMargin: win.u * 0.6
                                width: parent.width
                                // Tall enough for the tile plus the room a
                                // selected one grows into. Sized so three
                                // shelves fit a 720p panel without scrolling —
                                // above that there is slack, and below it the
                                // rows scroll, which is the right way round.
                                height: shelf.modelData.kind === "game"
                                        ? win.u * 14.6 : win.u * 7.8
                                orientation: ListView.Horizontal
                                spacing: win.u * 0.8
                                // The pointer does not drive this; a stray
                                // flick from a touchpad would fight the
                                // selection for control of the same list.
                                interactive: false
                                // Room for a selected tile to grow into at
                                // either end, and for the first tile to sit
                                // clear of the screen edge.
                                leftMargin: win.u * 1.6
                                rightMargin: win.u * 1.6

                                currentIndex: shell.col(shelf.index)
                                highlightRangeMode: ListView.ApplyRange
                                preferredHighlightBegin: win.u * 1.6
                                preferredHighlightEnd: width - win.u * 1.6
                                highlightMoveDuration: 200

                                model: shelf.modelData.items

                                delegate: Item {
                                    id: tile
                                    required property var modelData
                                    required property int index

                                    readonly property bool selected:
                                        shell.row === shelf.index
                                        && shell.col(shelf.index) === tile.index
                                    readonly property bool portrait:
                                        shelf.modelData.kind === "game"

                                    // 2:3, which is the shape of every cover
                                    // Steam caches (600x900). Anything else
                                    // either letterboxes the art or crops
                                    // somebody's title off it.
                                    width: portrait ? win.u * 9 : win.u * 11
                                    height: portrait ? win.u * 13.5 : win.u * 7

                                    scale: selected ? 1.06 : 1.0
                                    Behavior on scale {
                                        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: win.u * 0.5
                                        color: tile.selected ? "#242038" : "#191527"
                                        border.width: tile.selected ? Math.max(2, win.u * 0.16) : 1
                                        border.color: tile.selected ? win.accent : "#332c4d"
                                        clip: true

                                        // The cover, when Steam has cached one.
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: parent.border.width
                                            source: tile.modelData.art
                                                    ? "file://" + tile.modelData.art : ""
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            visible: status === Image.Ready
                                            // Decoded at the size drawn, not at
                                            // 600x900 each: sixty covers at full
                                            // resolution is most of a gigabyte of
                                            // texture on a screen showing eight.
                                            sourceSize.width: Math.round(tile.width * 1.2)
                                            sourceSize.height: Math.round(tile.height * 1.2)
                                        }

                                        // …and when it has not. A tile with no
                                        // picture must still say which game it
                                        // is; a blank rectangle in a row of
                                        // covers reads as a broken launcher.
                                        Text {
                                            anchors.fill: parent
                                            anchors.margins: win.u * 0.7
                                            visible: !tile.modelData.art
                                            text: tile.modelData.name || ""
                                            color: win.ink
                                            font.pixelSize: win.u * 1.1
                                            font.bold: true
                                            wrapMode: Text.WordWrap
                                            elide: Text.ElideRight
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }

                                    // A mouse still works, because a machine
                                    // that is a television in the evening is a
                                    // desktop in the afternoon. Hover moves the
                                    // selection so the pointer and the
                                    // controller are never fighting over two
                                    // different ideas of what is selected.
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onEntered: {
                                            shell.row = shelf.index
                                            shell.setCol(shelf.index, tile.index)
                                        }
                                        onClicked: {
                                            shell.row = shelf.index
                                            shell.setCol(shelf.index, tile.index)
                                            shell.activate()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── footer: what the buttons do ─────────────────────────────────
            //
            // Always on screen. Every console interface does this and it is not
            // decoration: there is no tooltip, no menu bar and no manual within
            // reach of a sofa, so the only place a control scheme can live is on
            // the screen it applies to.
            Rectangle {
                id: footer
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: win.u * 2.6
                color: "#cc0b0916"

                Row {
                    anchors.centerIn: parent
                    spacing: win.u * 2

                    Repeater {
                        model: [
                            { k: "A", v: "Select" },
                            { k: "B", v: "Back" },
                            { k: "LB/RB", v: "Jump" },
                            { k: "Guide", v: "Desktop" }
                        ]
                        Row {
                            id: hint
                            required property var modelData
                            spacing: win.u * 0.4

                            Rectangle {
                                width: Math.max(win.u * 1.6, glyph.implicitWidth + win.u * 0.6)
                                height: win.u * 1.6
                                radius: height / 2
                                color: "#2a2340"
                                border.width: 1
                                border.color: "#453a66"
                                anchors.verticalCenter: parent.verticalCenter

                                Text {
                                    id: glyph
                                    anchors.centerIn: parent
                                    text: hint.modelData.k
                                    color: win.ink
                                    font.pixelSize: win.u * 0.75
                                    font.bold: true
                                }
                            }
                            Text {
                                text: hint.modelData.v
                                color: win.dim
                                font.pixelSize: win.u * 0.85
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            // ── the states that are not "a screen full of tiles" ────────────

            Text {
                anchors.centerIn: parent
                visible: !shell.loaded
                text: "Reading your library…"
                color: win.dim
                font.pixelSize: win.u * 1.4
            }

            Text {
                anchors.centerIn: parent
                width: parent.width * 0.6
                visible: shell.loaded && shell.shelves.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: win.dim
                font.pixelSize: win.u * 1.2
                text: "Nothing to show yet.\n\n"
                      + "Install Steam, or check `syn-arcade big` in a terminal — "
                      + "it says what it found and what it did not."
            }

            // Launching takes a moment (Steam has to be woken up), and a
            // ten-foot interface that does nothing visible for two seconds
            // after a button press is one people press again.
            Rectangle {
                anchors.fill: parent
                visible: shell.launchingName !== ""
                color: "#dd05060a"

                Text {
                    anchors.centerIn: parent
                    text: "Starting " + shell.launchingName + "…"
                    color: win.ink
                    font.pixelSize: win.u * 1.8
                }
            }

            // ── the keyboard ────────────────────────────────────────────────
            //
            // Mapped onto the same words the controller sends, so there is one
            // implementation of what a direction does. Escape is the way out
            // for somebody who opened this with the keyboard and wants it gone.
            Item {
                id: keys
                anchors.fill: parent
                focus: true

                Keys.onPressed: (event) => {
                    switch (event.key) {
                    case Qt.Key_Up:       shell.nav("up"); break
                    case Qt.Key_Down:     shell.nav("down"); break
                    case Qt.Key_Left:     shell.nav("left"); break
                    case Qt.Key_Right:    shell.nav("right"); break
                    case Qt.Key_PageUp:   shell.nav("page-left"); break
                    case Qt.Key_PageDown: shell.nav("page-right"); break
                    case Qt.Key_Home:     shell.setCol(shell.row, 0); break
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                    case Qt.Key_Space:    shell.nav("accept"); break
                    case Qt.Key_Backspace: shell.nav("back"); break
                    case Qt.Key_Escape:   Qt.quit(); break
                    default: return
                    }
                    event.accepted = true
                }
            }
        }
    }
}
