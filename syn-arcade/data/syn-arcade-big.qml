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
// where cover art is cached, which machine on the network is running Jellyfin,
// or what "Sleep" runs. Pressing a tile calls the binary back — `big launch
// <appid>`, `big run <id>` — rather than running anything itself, so what a
// tile does is decided in one place and the same answer is available over SSH.
//
// ⚠ That is not tidiness, it is the SIGPIPE bug. A launcher started from here
// inherits quickshell's pipes, and quickshell closes them the moment its direct
// child exits — the game then dies on its first line of logging, before it maps
// a window, with every visible sign saying it worked. big.c's spawn_detached()
// is where that is handled, and it can only be handled where the child is made.
//
// ── STEPPING ASIDE, which is what a console does ────────────────────────────
//
// This used to QUIT whenever it launched anything. That is the single thing
// that made it feel unfinished: opening the controller window, or the browser,
// closed the television interface, and getting back meant finding a keyboard.
// A console does not do that — it goes away while you use the thing, and it
// comes back.
//
// So the shell stays alive and hides instead, and there are three ways back:
//
//   · the application EXITS. `big run <id> --wait` lives exactly as long as
//     what it started, so its Process exiting is the news that somebody has
//     finished with the browser.
//   · GUIDE, at any time, from the controller. Its stream is still running
//     while this is hidden, which is the whole reason hiding beats quitting.
//   · `syn-arcade big show` — which is what Super+F10 now sends.
//
// While hidden the window is not merely transparent, it is not MAPPED: a
// full-screen overlay surface left on top of a game would cost a composite
// pass per frame and break direct scanout, for a screen nobody is looking at.
//
// ── The three ways in ───────────────────────────────────────────────────────
//
// Controller, keyboard and mouse all work. The controller is read by
// `syn-arcade big nav`, which turns evdev events into words on a pipe — NOT
// into synthetic key presses. There is no uinput device and nothing the
// compositor can see, so stick drift cannot type into somebody's browser.
//
// The ONE exception is deliberate and bounded: while an application that needs
// a pointer is up, `big mouse` drives the compositor's cursor from the stick,
// because a browser takes pointer events and cannot be handed words. It runs
// only while this is out of the way, it stops the moment the interface comes
// back, and it moves a cursor rather than pressing keys. See vptr.c.
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

    // The dendrite mark for the header, resolved by big.c against its own data
    // directory — the same icon_file() path every tile glyph takes. Empty on a
    // machine where the drawing did not ship, and the header then simply has no
    // emblem rather than a broken-image box.
    readonly property string logoFile: Quickshell.env("SYN_BIG_LOGO") || ""

    // ── the data ────────────────────────────────────────────────────────────
    //
    // The library is fetched once: a scan is dozens of stat() calls per game
    // and the set of installed games does not change while somebody is looking
    // at it. The headlines and the media servers are different — they go stale
    // by the clock rather than by anything the user did — so those refresh on
    // a timer, and both read a cache first so the screen draws immediately
    // whether or not this machine is online.
    property var games: []
    property var apps: []
    property var news: []
    property var media: []
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

    // ── what is open ────────────────────────────────────────────────────────
    //
    // ⚠ ASKED, never remembered. This is `synctl clients` by way of
    // `big windows` — what synui actually has mapped — and not a tally kept
    // here of what this file launched. The two disagree within a minute of
    // anybody touching a keyboard: an application opens a second window, or
    // exits by itself, or is closed from the desktop, or was never started
    // from here at all. A Running shelf built from a private list would offer
    // to switch to windows that are gone and miss the ones that are there,
    // and every close aimed at a stale row would land on nothing — or on
    // something else.
    //
    // Refreshed on the way back rather than on a timer: this list only has to
    // be true when it is about to be LOOKED at, and polling a subprocess
    // forever behind a full-screen game is exactly the sort of thing the
    // header warns about.
    property var windows: []

    Process {
        id: winProc
        command: [shell.bin, "big", "windows", "--rec"]
        stdout: StdioCollector {
            onStreamFinished: shell.windows = shell.parseRecords(this.text)
        }
    }

    function refreshWindows() {
        if (!winProc.running) winProc.running = true
    }

    // After a launch, because the window does not exist the moment the
    // launcher returns — the same reason big.c's fullscreen waiter has to wait.
    Timer {
        id: windowsTimer
        interval: 1600
        onTriggered: shell.refreshWindows()
    }

    // Headlines. The first run takes whatever the cache has — which on a cold
    // machine is nothing, and then this fetches — and the timer refreshes long
    // after anybody has stopped watching the shelf.
    Process {
        id: newsProc
        command: [shell.bin, "big", "news", "--rec"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: shell.news = shell.parseRecords(this.text)
        }
    }

    Timer {
        interval: 20 * 60 * 1000
        running: true
        repeat: true
        onTriggered: {
            newsProc.command = [shell.bin, "big", "news", "--rec", "--refresh"]
            newsProc.running = true
        }
    }

    // Plex and Jellyfin, wherever they are. Same shape: cache first, refresh
    // behind it. A server that has just been switched on appears within ten
    // minutes without anybody restarting anything.
    Process {
        id: mediaProc
        command: [shell.bin, "big", "media", "--rec"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: shell.media = shell.parseRecords(this.text)
        }
    }

    Timer {
        interval: 10 * 60 * 1000
        running: true
        repeat: true
        onTriggered: {
            mediaProc.command = [shell.bin, "big", "media", "--rec", "--refresh"]
            mediaProc.running = true
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
        // Stopped while the interface is hidden. There is no clock on screen
        // to update, and a process every second for the whole time somebody is
        // playing a game is a thing that would eventually be noticed in a
        // profile and not understood.
        running: !shell.away
        repeat: true
        triggeredOnStart: true
        onTriggered: clockProc.running = true
    }

    // ── the controller ──────────────────────────────────────────────────────
    //
    // One line per press. See pad.c: this synthesises nothing, and stops the
    // moment this process does, because it is a child holding the other end of
    // this pipe. ⚠ It keeps running while we are hidden — that is what makes
    // Guide able to bring the interface back.
    Process {
        id: navProc
        command: [shell.bin, "big", "nav"]
        running: true
        stdout: SplitParser {
            onRead: (line) => shell.nav(line.trim())
        }
    }

    // ── the keybind, and anything else that wants a word with us ────────────
    //
    // `big listen` prints what is written to a FIFO. Super+F10 runs
    // `big toggle`, which — finding a shell already running — sends "toggle"
    // here rather than killing it. Without this the key could only stop what
    // it could not show.
    Process {
        id: ctlProc
        command: [shell.bin, "big", "listen"]
        running: true
        stdout: SplitParser {
            onRead: (line) => {
                const cmd = line.trim()
                if (cmd === "show") shell.comeBack()
                else if (cmd === "hide") shell.stepAside()
                else if (cmd === "toggle") {
                    if (shell.away) shell.comeBack(); else shell.stepAside()
                } else if (cmd === "quit") Qt.quit()
            }
        }
    }

    // ── stepping aside ──────────────────────────────────────────────────────

    // True while the interface is out of the way. The window is unmapped, not
    // transparent — see the header.
    property bool away: false

    // What is running, if anything: the record of the tile that started it.
    // Kept while we are back on screen too, so the banner can say what is
    // waiting behind this and Guide can go back to it.
    property var activeApp: null

    // A line of text that appears for a few seconds and goes away — for the
    // things that used to happen silently. See launchApp.
    property string notice: ""

    Timer {
        id: noticeTimer
        interval: 4000
        onTriggered: shell.notice = ""
    }

    // The hint that appears for a few seconds when the interface gets out of
    // the way, because a screen that simply vanishes gives somebody holding a
    // controller nothing to go on.
    property bool hintShown: false

    Timer {
        id: hintTimer
        interval: 4500
        onTriggered: shell.hintShown = false
    }

    function stepAside() {
        if (shell.away) return
        shell.away = true
        shell.oskOpen = false
        shell.hintShown = true
        hintTimer.restart()
    }

    function comeBack() {
        shell.away = false
        shell.oskOpen = false
        shell.hintShown = false
        hintTimer.stop()
        // The Running shelf has to be true at the moment it is looked at, and
        // this is that moment. Anything could have opened or closed while the
        // television was out of the way.
        shell.refreshWindows()

        // ⚠ AND THE SELECTION GOES THERE. Without this the shelf is correct
        // and useless: coming back leaves the selection wherever it was, the
        // shelves scroll to keep it in view, and Running — being at the top —
        // is off the screen. The rig showed exactly that, and X did nothing
        // because the selected shelf was still Apps.
        //
        // Pressing Guide while something is open is not browsing. It is
        // "deal with the thing I was just in", so that is what is selected.
        if (shell.windows.length || shell.anyRunning()) {
            shell.rowTitle = "Running"
            shell.rowChosen = true
            for (let i = 0; i < shell.shelves.length; i++)
                if (shell.shelves[i].title === "Running") { shell.row = i; break }
        }
    }

    // ── launching ───────────────────────────────────────────────────────────

    property string launchingName: ""

    Timer {
        id: launchingTimer
        interval: 2500
        onTriggered: shell.launchingName = ""
    }

    // ── one process per application, and why it is a POOL ───────────────────
    //
    // ⚠ THIS WAS ONE `Process`, AND THAT IS WHY ONLY ONE APPLICATION COULD EVER
    // BE OPENED.
    //
    // `big run <id> --wait` lives for as long as the application does, so the
    // single process was still running when the next tile was pressed — and
    // `running = true` on a process that is already running is a NO-OP in
    // quickshell. Silently: no error, nothing in the log. Everything else in
    // this function still happened, so the television recorded the new tile as
    // active, re-pointed the controller-as-mouse and the on-screen keyboard at
    // it, and got out of the way — revealing the application that was already
    // there. From the sofa that reads as "it will not open anything else", and
    // with no way to close from a gamepad the only way out was a keyboard.
    //
    // A FIXED POOL rather than dynamically created objects. Six is more than
    // anybody opens on a television, a slot is free exactly when its process is
    // not running, and there is no model-reconciliation behaviour to be wrong
    // about — a Variants/Instantiator delegate that got re-created on a model
    // reassign would restart every running application at once, and that is a
    // failure this file has no way to notice.
    readonly property var procs: [proc0, proc1, proc2, proc3, proc4, proc5]

    // slot index → { tile, at }. `at` is per slot because the hand-off rule
    // below is per launch: two applications started a second apart must not
    // share one clock.
    //
    // ⚠ Assigned as a COPY for the reason setCol documents at length — mutating
    // this object and assigning the same reference back emits nothing.
    property var slotApp: ({})
    function setSlot(i, v) {
        shell.slotApp = Object.assign({}, shell.slotApp, { [i]: v })
    }

    function anyRunning() {
        for (let i = 0; i < shell.procs.length; i++)
            if (shell.procs[i].running) return true
        return false
    }

    // The tile of some other still-running slot, for when the one in front
    // exits and something else is still open.
    function otherApp(notTile) {
        for (let i = 0; i < shell.procs.length; i++) {
            const rec = shell.slotApp[i]
            if (shell.procs[i].running && rec && rec.tile !== notTile)
                return rec.tile
        }
        return null
    }

    // An application, which this gets out of the way for.
    //
    // `cmd` is whatever `big` verb starts it. For the ordinary tiles that is
    // `run <id> --wait`; for a headline it is `open <url> --wait`. Both end
    // when the application does, which is the signal to come back.
    function launchApp(tile, cmd) {
        let i = -1
        for (let k = 0; k < shell.procs.length; k++)
            if (!shell.procs[k].running) { i = k; break }

        // Saying so rather than doing nothing, which is the whole lesson of
        // the bug above.
        if (i < 0) {
            shell.launchingName = ""
            shell.notice = "Six applications are already open — close one first"
            noticeTimer.restart()
            return
        }

        shell.setSlot(i, { tile: tile, at: Date.now() })
        shell.activeApp = tile
        shell.launchingName = tile.name || ""
        launchingTimer.restart()
        shell.procs[i].command = [shell.bin].concat(cmd)
        shell.procs[i].running = true
        shell.stepAside()

        // The window does not exist yet; `big windows` is asked again once it
        // has had time to map, so the Running shelf has it on the way back.
        windowsTimer.restart()
    }

    function launchEnded(i) {
        const rec = shell.slotApp[i]
        shell.setSlot(i, null)
        shell.launchingName = ""
        if (!rec) return

        // ⚠ A FAST exit is not somebody closing the application.
        //
        // Firefox, Steam and half of everything else are single-instance:
        // start one while it is already running and the second process hands
        // its arguments to the first over a socket and exits at once. Treating
        // that as "they have finished reading" would throw the television back
        // over a browser somebody just opened — reliably, and only on the
        // machines where the browser was already up, which is the worst way for
        // a bug to be distributed.
        //
        // So a launcher that returns immediately is a hand-off: stay out of the
        // way, and let Guide be what comes back. Anything that lived longer
        // than a few seconds really did close.
        //
        // ⚠ This clock is why the fullscreen wait in big.c is forked. See the
        // comment on fullscreen_after_launch there before adding anything to
        // the launch path.
        const lived = Date.now() - rec.at
        if (lived < 3000) return

        shell.refreshWindows()

        // Something else is still open: stay out of the way and hand the front
        // to it, rather than throwing the television over the top of it.
        if (shell.anyRunning()) {
            shell.activeApp = shell.otherApp(rec.tile) || shell.activeApp
            return
        }

        shell.activeApp = null
        shell.comeBack()
    }

    // No parameters on these handlers: quickshell's exited(int, QProcess::
    // ExitStatus) has a second type QML cannot resolve, and a typed handler
    // silently never runs.
    Process { id: proc0; onExited: shell.launchEnded(0) }
    Process { id: proc1; onExited: shell.launchEnded(1) }
    Process { id: proc2; onExited: shell.launchEnded(2) }
    Process { id: proc3; onExited: shell.launchEnded(3) }
    Process { id: proc4; onExited: shell.launchEnded(4) }
    Process { id: proc5; onExited: shell.launchEnded(5) }

    // ── the controller as a mouse ───────────────────────────────────────────
    //
    // Running only while all three are true: we are out of the way, what is
    // running wants a pointer, and the on-screen keyboard is not up (it takes
    // the same buttons, and A cannot both click and type). Stopping the
    // process is what removes the virtual pointer — there is no other state to
    // unwind, and vptr.c releases any held button on the way out.
    Process {
        id: mouseProc
        command: [shell.bin, "big", "mouse"]
        running: shell.away && shell.activeApp !== null
                 && shell.activeApp.pointer === "1" && !shell.oskOpen
    }

    // ── the on-screen keyboard ──────────────────────────────────────────────
    //
    // Started as soon as something that might want typing is launched, rather
    // than when the keyboard opens: `big keys` has to be alive and reading
    // before the first key is pressed, and starting a process on the press
    // loses it.
    Process {
        id: keysProc
        command: [shell.bin, "big", "keys"]
        stdinEnabled: true
        running: shell.away && shell.activeApp !== null
                 && shell.activeApp.keys === "1"
    }

    property bool oskOpen: false
    property int  oskLayout: 0        // 0 lower, 1 upper, 2 symbols
    property int  oskRow: 1
    property int  oskCol: 0

    function type(s)  { if (keysProc.running) keysProc.write("t " + encodeURIComponent(s) + "\n") }
    function key(n)   { if (keysProc.running) keysProc.write("k " + n + "\n") }
    function ctrl(n)  { if (keysProc.running) keysProc.write("c " + n + "\n") }

    // The layouts. Four rows of characters and one row of the keys that are
    // not characters — which is where Address (Ctrl+L) lives, because a
    // browser you cannot type a URL into is a browser that can only follow
    // links somebody else opened.
    readonly property var oskRows: {
        const chars = shell.oskLayout === 1
            ? ["!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL_", "ZXCVBNM:;?"]
            : shell.oskLayout === 2
            ? ["~`|\\{}[]<>", "+=-_*/%$#@", "&^()\"':;!?", ",.…€£¥°·§"]
            : ["1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm.,/"]

        const rows = chars.map(r => r.split("").map(c => ({ t: c, v: c, w: 1 })))

        rows.push([
            { t: shell.oskLayout === 1 ? "abc" : "ABC", act: "case", w: 1.6 },
            { t: "#+=",   act: "sym",   w: 1.5 },
            { t: "space", act: "space", w: 3.4 },
            { t: "⌫",     act: "back",  w: 1.6 },
            { t: "⏎",     act: "enter", w: 1.6 },
            { t: "Tab",   act: "tab",   w: 1.4 },
            { t: "Esc",   act: "esc",   w: 1.4 },
            { t: "Address", act: "url", w: 2.0 },
            { t: "Close", act: "close", w: 1.8 }
        ])
        return rows
    }

    function oskMove(dr, dc) {
        const rows = shell.oskRows
        let r = shell.oskRow + dr
        if (r < 0) r = 0
        if (r > rows.length - 1) r = rows.length - 1

        let c = shell.oskCol + dc
        // Moving between rows of different lengths keeps the column where it
        // was rather than resetting it, and clamps — which is what makes a
        // thumb able to walk down the keyboard in a straight line.
        if (r !== shell.oskRow) c = shell.oskCol
        if (c < 0) c = 0
        if (c > rows[r].length - 1) c = rows[r].length - 1

        shell.oskRow = r
        shell.oskCol = c
    }

    function oskPress() {
        const row = shell.oskRows[shell.oskRow]
        const k = row ? row[shell.oskCol] : null
        if (!k) return

        switch (k.act) {
        case "case":  shell.oskLayout = shell.oskLayout === 1 ? 0 : 1; return
        case "sym":   shell.oskLayout = shell.oskLayout === 2 ? 0 : 2; return
        case "space": shell.type(" ");        return
        case "back":  shell.key("BackSpace"); return
        case "enter": shell.key("Return");    return
        case "tab":   shell.key("Tab");       return
        case "esc":   shell.key("Escape");    return
        case "url":   shell.ctrl("l");        return
        case "close": shell.oskOpen = false;  return
        default:      shell.type(k.v);        return
        }
    }

    function oskToggle() {
        if (!keysProc.running) return
        shell.oskOpen = !shell.oskOpen
        if (shell.oskOpen) {
            shell.oskRow = 1
            shell.oskCol = 0
        }
    }

    // ── what the tiles are, in the order they are shown ─────────────────────
    //
    // The shelf a tile belongs on is a property of the TILE, decided in big.c,
    // so adding one there puts it in the right place with no change here. The
    // ORDER of the shelves is this file's business, and it is three rows:
    //
    //     1  the library, which is what somebody turned the television on for
    //     2  the launchers, the media and the applications — one row, packed
    //     3  the headlines, which is the one shelf nobody is navigating TO
    //
    // ⚠ THE GAMES SHELF IS FIRST NOW, and that is the change the rest follow
    // from. Play used to sit above it: two tiles across the top of a
    // television, with the fifty covers somebody actually came for pushed a
    // row down. A launcher's first row should be its content.
    //
    // ⚠ AND THE MACHINE'S OWN SWITCHES ARE NO LONGER A SHELF. Sleep, restart
    // and power off are not things anybody browses to — they were a row of the
    // television spent on four buttons pressed once a day, and a row that has
    // to be scrolled past on the way to the news. They live behind Start now;
    // see `menuItems` below. They are still `shelf = system` in big.c, because
    // where a tile goes is still decided there — this file is what stopped
    // drawing them in a row.
    function byShelf(name) {
        return shell.apps.filter(a => a.shelf === name)
    }

    readonly property var shelves: {
        const out = []

        // What is already open goes FIRST, because it is the shelf somebody
        // pressing Guide came here for: they are not browsing, they are going
        // back to something. It is also the only shelf that can be empty and
        // simply not appear, which is why the selection is remembered by NAME
        // below rather than by row number.
        if (shell.windows.length)
            out.push({ title: "Running", kind: "running", items: shell.windows })

        if (shell.games.length)
            out.push({ title: "Games", kind: "game", items: shell.games })

        // ── one row: Play, Media, Apps ──────────────────────────────────────
        //
        // All three are `kind: "app"`, which is what marks them as a BAR — a
        // short shelf that shares a row and scrolls rather than claiming one.
        // See isBar() and the packer.
        const play = shell.byShelf("play")
        if (play.length) out.push({ title: "Play", kind: "app", items: play })

        // Installed media applications first, then whatever answered on the
        // network — a Plex client on this machine is a better tile than the
        // same server's web page, and both being here is the point.
        const media = shell.byShelf("media").concat(shell.media)
        if (media.length) out.push({ title: "Media", kind: "app", items: media })

        const apps = shell.byShelf("apps")
        if (apps.length) out.push({ title: "Apps", kind: "app", items: apps })

        if (shell.news.length)
            out.push({ title: "News", kind: "news", items: shell.news })

        return out
    }

    // ── what is playing ─────────────────────────────────────────────────────
    //
    // Only cliamp can answer this, because it is the only player big screen
    // mode can drive rather than launch — see music_headless() in big.c. An
    // empty `state` means there is nothing to control, and the menu then has no
    // music row at all rather than a row whose buttons do nothing.
    //
    // ⚠ ASKED ONLY WHILE THE MENU IS OPEN. This is a subprocess per poll, and
    // the mistake to avoid is the one the Running shelf's comment names: a
    // timer that keeps asking forever behind a full-screen game.
    property var music: ({})

    Process {
        id: musicProc
        command: [shell.bin, "big", "music", "status", "--rec"]
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = shell.parseRecords(this.text)
                shell.music = rows.length ? rows[0] : ({})
            }
        }
    }

    function refreshMusic() {
        if (!musicProc.running) musicProc.running = true
    }

    // ── the visualizer ──────────────────────────────────────────────────────
    //
    // cliamp's own bands, one NDJSON frame per line, drawn behind the Now
    // Playing row. Ten floats in 0..1.
    //
    // ⚠ IT RUNS ONLY WHILE THE MENU IS OPEN. Twenty frames a second is twenty
    // scene-graph updates a second, and a launcher that keeps that up behind a
    // full-screen game is exactly the sort of thing the header of this file
    // warns about. The Process's `running` condition is the whole bound.
    property var musicBands: []

    Process {
        id: visProc
        command: [shell.bin, "big", "music", "vis"]
        running: shell.menuOpen && shell.musicLive
        stdout: SplitParser {
            onRead: (line) => {
                // ⚠ GUARDED, because this is a parser pointed at another
                // program's output. cliamp answers `{"ok":false,...}` when it
                // has no bands to give, a half-written line is possible on any
                // stream, and one throw here would take the whole menu down.
                try {
                    const f = JSON.parse(line)
                    if (f && f.ok && Array.isArray(f.bands))
                        shell.musicBands = f.bands
                } catch (e) { /* not a frame; the next one will be */ }
            }
        }
        // Nothing to draw once the stream ends, and a visualizer frozen on its
        // last frame reads as the interface having hung.
        onExited: shell.musicBands = []
    }

    Timer {
        id: musicTimer
        interval: 2000
        repeat: true
        // The menu is the only thing that reads it, so it is the only thing
        // that makes it worth asking.
        running: shell.menuOpen
        onTriggered: shell.refreshMusic()
    }

    readonly property bool musicLive:
        shell.music && String(shell.music.state || "") !== ""

    // The Music tile's own glyph, reused for the menu row. Taken from the tile
    // rather than resolved again, so there is still exactly one place that
    // knows where this package put its drawings — big.c's icon_file().
    readonly property string musicIcon: {
        const t = shell.apps.filter(a => a.id === "music")
        return t.length ? (t[0].iconfile || "") : ""
    }

    // ── where the music comes from ──────────────────────────────────────────
    //
    // The picker, and the Plex library behind it. Both are records from big.c,
    // for the same reason every other list here is: which sources exist, which
    // one is chosen and what choosing it DOES are facts about cliamp, and a
    // copy of them in QML is a copy that stops being true.
    //
    // ⚠ ASKED WHEN THE MENU OPENS, never on a timer. The albums are a hundred
    // and thirty rows off a server; the sources are two file reads. Neither is
    // worth a subprocess behind a full-screen game.
    property var sourceItems: []
    property var albumItems: []
    property string menuBusy: ""

    Process {
        id: sourcesProc
        command: [shell.bin, "big", "music", "source", "--rec"]
        stdout: StdioCollector {
            onStreamFinished: {
                shell.sourceItems = shell.parseRecords(this.text).map(r => ({
                    id: r.id, name: r.name, kind: "source",
                    action: r.action, current: r.current === "1",
                    // The row's second line: what this one is, said on the row
                    // it applies to. "· playing now" is the only state a
                    // picker has to show, and the note from C is the only
                    // thing that knows a source is not set up.
                    note: r.note && r.note.length ? r.note
                          : (r.current === "1" ? "· playing from this now" : "")
                }))
            }
        }
    }

    Process {
        id: albumsProc
        command: [shell.bin, "big", "music", "plex", "--rec"]
        stdout: StdioCollector {
            onStreamFinished: {
                shell.albumItems = shell.parseRecords(this.text).map(r => ({
                    id: r.id, name: r.name, kind: "album",
                    note: r.artist + (r.year && r.year.length
                                      ? "   ·   " + r.year : "")
                }))
                shell.menuBusy = ""
            }
        }
    }

    function refreshSources() {
        if (!sourcesProc.running) sourcesProc.running = true
    }

    // Which source is playing, for the row that opens the picker. Empty until
    // the first read comes back, and the row says nothing rather than guessing.
    readonly property string musicSourceName: {
        const cur = shell.sourceItems.filter(s => s.current)
        return cur.length ? cur[0].name : ""
    }

    // Whether there is a player here that can be DRIVEN — which is what makes
    // a source picker mean anything. big.c already decides this: the Music tile
    // is an `action` when the player is cliamp and an `app` when it is anything
    // else (see music_headless), so this reads the answer instead of keeping a
    // second one.
    readonly property bool musicDrivable: {
        const t = shell.apps.filter(a => a.id === "music")
        return t.length > 0 && t[0].kind === "action"
    }

    // What is behind the Start button: what is playing, where it comes from,
    // then the machine's own switches in the order big.c lists them — the way
    // out first, because it is the one somebody reaches for without having
    // decided anything.
    //
    // ⚠ ONE LIST, and the music rows are entries in it rather than special
    // cases above it. Up and down have to walk the whole menu; a row drawn
    // outside the model is a row the d-pad goes straight past.
    //
    // ⚠ AND THE MENU HAS PAGES NOW. The picker and the album list are the same
    // list on a different page rather than a second panel: one delegate, one
    // set of keys, and a d-pad that cannot end up driving the thing underneath.
    property string menuPage: "main"		// main | source | albums

    readonly property var menuItems: {
        if (shell.menuPage === "source") return shell.sourceItems
        if (shell.menuPage === "albums") return shell.albumItems

        const out = []
        if (shell.musicLive)
            out.push({ id: "now-playing", kind: "music",
                       name: shell.music.title || "Music" })
        if (shell.musicDrivable)
            out.push({ id: "music-source", kind: "page", page: "source",
                       name: "Music Source",
                       note: shell.musicSourceName
                             ? "· " + shell.musicSourceName : "" })
        return out.concat(shell.byShelf("system"))
    }

    function openMenuPage(page) {
        shell.menuPage = page
        shell.menuIndex = 0
        if (page === "albums") {
            // Said out loud, because a server on the other end of a network
            // can take a moment and an empty panel reads as a broken button.
            shell.menuBusy = "Reading the library…"
            shell.albumItems = []
            if (!albumsProc.running) albumsProc.running = true
        }
    }

    // ── shelves that share a row: BANDS ─────────────────────────────────────
    //
    // ⚠ A SHELF USED TO OWN A WHOLE ROW OF THE TELEVISION WHETHER IT NEEDED ONE
    // OR NOT. Media has three tiles and Apps has three, so on any screen wider
    // than it is tall each of them spent two thirds of a row on nothing — while
    // System and the headlines sat off the bottom edge, reachable only by
    // scrolling. The empty right-hand half of one row and the missing row below
    // it were the SAME SPACE, and the interface was leaving it and then asking
    // for it back.
    //
    // So consecutive shelves that fit ACROSS are packed into a band and drawn
    // side by side, labels and all.
    //
    // ⚠ A SHELF THAT HAS TO SCROLL KEEPS ITS OWN ROW, and that is a rule about
    // travel rather than about tidiness: half a row is half the tiles per
    // press, so narrowing a fifty-game library doubles how far somebody pushes
    // a stick to cross it. A shelf that already fits, by contrast, gains
    // nothing at all from the extra width — which is exactly the space this
    // hands to the shelf below.
    //
    // ⚠ IT ANSWERS TO THE SCREEN, not to a count of tiles: the same six shelves
    // pack differently on 4:3, 16:9 and 21:9, for the same reason `u` is
    // clamped by width — see the comment on it. `rowUnits` is what the window
    // that is actually on screen reports, so this is measured, never assumed.
    property real rowUnits: 96          // 16:9, until the chosen window says

    // The strip's own margins, both ends, in units. Part of a SHELF's width
    // rather than the stage's — which is what lets two of them sit side by side
    // and still hold their tiles clear of each other and of the screen edge.
    readonly property real shelfMargins: 3.2
    readonly property real tileGap: 0.8

    // ⚠ ONE PLACE. The strip reads this too, so the width the packer reserves
    // and the width the tiles are drawn at cannot drift apart — and a band
    // whose arithmetic disagreed with its contents would clip the last tile of
    // a shelf that was promised to fit whole.
    function idealUnits(kind) {
        return kind === "game" ? 9 : kind === "news" ? 14 : 11
    }

    // How much a selected tile grows. ⚠ ONE HOME, for the same reason
    // idealUnits is: the delegate scales by it and the strip has to RESERVE
    // room for it on every side. Two copies is a highlight drawn outside the
    // space set aside for it, which — now that the strips clip — is a
    // highlight with an edge missing.
    readonly property real tileSelectedScale: 1.06

    // What a shelf wants, split into the part that can be squeezed and the part
    // that cannot: tiles scale, gaps and margins do not.
    function shelfTiles(sh) { return sh.items.length * shell.idealUnits(sh.kind) }
    function shelfFixed(sh) {
        return Math.max(0, sh.items.length - 1) * shell.tileGap + shell.shelfMargins
    }

    // How much every tile in a band has to shrink for the whole band to fit.
    // 1 when it fits as drawn, and never below 0.85 — the SAME 15% the strip
    // already allows itself when it snaps a row to whole tiles, because a tile
    // that is a sixth off its size stops reading as the same kind of thing as
    // its neighbours.
    readonly property real bandSqueeze: 0.85

    // ── a BAR: a shelf that would rather scroll than own a row ──────────────
    //
    // The rule above — a shelf that has to scroll keeps its own row — is a rule
    // about TRAVEL, and it is right for the library: half a row is half the
    // tiles per press, so narrowing fifty covers doubles how far somebody
    // pushes a stick to cross them. It is wrong for a handful of launchers.
    // Play, Media and Apps between them are a fixed, small roster that somebody
    // crosses in two presses either way, and giving each of them a row of its
    // own is how the television ended up with more rows than screen.
    //
    // So these three are BARS: they always share one row, whatever the
    // arithmetic says, and any of them that runs out of width scrolls right.
    //
    // ⚠ THIS IS WHAT RESERVES ROOM FOR HEROIC AND LUTRIS. Both are already in
    // apps_table() behind a have() check, so they appear on the Play bar the
    // day they are installed — and without this rule that arrival would be a
    // silent RELAYOUT: four launchers no longer fit beside Media and Apps at
    // the 15% squeeze, the packer would break the row in three, and installing
    // a game launcher would rearrange the whole television. Now the bar takes
    // its share of the row and the extra tiles go off the right-hand edge with
    // the peek that says so.
    function isBar(sh) { return sh.kind === "app" }

    function bandScale(band) {
        let tiles = 0, fixed = 0
        for (let i = 0; i < band.length; i++) {
            tiles += shell.shelfTiles(band[i])
            fixed += shell.shelfFixed(band[i])
        }
        if (tiles <= 0) return 1
        return Math.min(1, (shell.rowUnits - fixed) / tiles)
    }

    // Each entry is one row of the screen: [{ row, units, scale }, …], `row`
    // being the index into `shelves` so nothing downstream has to care that a
    // shelf is no longer the same thing as a row.
    readonly property var bands: {
        const out = []
        let cur = []

        function close() {
            if (!cur.length) return
            const shs = cur.map(r => shell.shelves[r])
            // A lone shelf that does not fit is the library or the headlines:
            // it keeps its tiles at full size and the strip's own snapping
            // deals with what runs off the edge.
            //
            // ⚠ FLOORED AT THE SQUEEZE, never below it. A band of bars is
            // packed whether or not it fits, so bandScale can now answer with
            // something far under 0.85 — and letting that through would draw a
            // launcher tile at half the size of a cover, which stops reading as
            // the same kind of thing rather than as a smaller one. Past the
            // floor the width is shared out instead and the bars scroll.
            const s = cur.length === 1
                    ? 1
                    : Math.max(shell.bandSqueeze, shell.bandScale(shs))

            const w = shs.map(sh => shell.shelfTiles(sh) * s + shell.shelfFixed(sh))
            let used = 0
            for (let i = 0; i < w.length; i++) used += w[i]

            // Three ways a row is divided, and they are three different
            // situations rather than three cases of one:
            //   one shelf   takes the row
            //   it fits     each shelf gets what it asked for, and the slack
            //               is split evenly so the row reaches both edges
            //   it does not each shelf gets its SHARE of the row — the bar
            //               that wanted most keeps most — and whatever does
            //               not fit inside that share scrolls
            let units
            if (cur.length === 1)
                units = w.map(() => shell.rowUnits)
            else if (used <= shell.rowUnits) {
                const spare = (shell.rowUnits - used) / w.length
                units = w.map(x => x + spare)
            } else
                units = w.map(x => x * shell.rowUnits / used)

            out.push(cur.map((r, i) => ({ row: r, units: units[i], scale: s })))
            cur = []
        }

        for (let i = 0; i < shell.shelves.length; i++) {
            const sh = shell.shelves[i]
            const bar = shell.isBar(sh)

            // ⚠ A BAR JOINS THE BAR BAND UNCONDITIONALLY — no trial, no
            // squeeze test. This is the one place the "a shelf that scrolls
            // keeps its own row" rule is deliberately not applied; see isBar.
            if (bar && cur.length && shell.isBar(shell.shelves[cur[0]])) {
                cur.push(i)
                continue
            }

            // Does it still work with this one added? If not, the band ends
            // here and this shelf starts the next one — which may itself be a
            // shelf that fits nowhere, and then it is a row of its own.
            const trial = cur.map(r => shell.shelves[r]).concat([sh])
            if (cur.length && shell.bandScale(trial) < shell.bandSqueeze) close()
            cur.push(i)
            // …and a bar is never closed for not fitting, or the first one
            // would take a row of its own before the second could join it.
            if (!bar && shell.bandScale([sh]) < shell.bandSqueeze) close()
        }
        close()
        return out
    }

    // Where a shelf sits: [band, position along it]. Used by everything that
    // moves — up and down step between BANDS now, left and right run along one.
    function place(r) {
        const bs = shell.bands
        for (let i = 0; i < bs.length; i++)
            for (let j = 0; j < bs[i].length; j++)
                if (bs[i][j].row === r) return [i, j]
        return [0, 0]
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

    // ⚠ KEYED BY SHELF NAME, not by row number, and the Running shelf is what
    // forced it — but it was always wrong. Shelves appear and disappear while
    // somebody is looking at them: the media servers arrive from a broadcast,
    // the headlines from the network, and Running comes and goes with every
    // launch. A column remembered against row 2 belongs to whatever shelf is
    // second AT THE TIME, so a shelf arriving above it silently hands its
    // scroll position to a different row.
    function colKey(r) {
        const sh = shell.shelves[r]
        return sh ? sh.title : String(r)
    }

    function col(r) {
        const v = shell.cols[shell.colKey(r)]
        return v === undefined ? 0 : v
    }

    // Which shelf the selection is ON, by name, for the same reason. Kept up
    // to date by every deliberate move, and used to put `row` back where it
    // was when the list of shelves changes underneath it.
    property string rowTitle: ""

    // ⚠ FALSE until somebody has actually MOVED, and without it the interface
    // opened on the wrong shelf every single time.
    //
    // The shelves do not arrive together. Each one is a separate `syn-arcade
    // big …` and they land in whatever order they finish — the cached ones
    // (media, news) in a millisecond, the library only after Steam's manifests
    // have been read, which on a real library is the slowest of them. The
    // first answer therefore becomes shelves[0] for an instant, `rowTitle`
    // adopts it as though it were a choice, and when Games finally arrives and
    // is inserted ABOVE, the name-matching below faithfully moves the selection
    // DOWN to keep it on that shelf. The rows scroll to keep the selection in
    // view, so the library — the thing somebody turned the television on for —
    // ends up off the top of the screen with Media selected.
    //
    // It is invisible in a spot check, because it depends on which query wins a
    // race. The rig showed it only once the library fixture had ARTWORK in it
    // and the missing hero band was suddenly obvious.
    //
    // An adoption is not a choice. Until a button is pressed the selection sits
    // on the top shelf and is re-decided every time another one lands.
    property bool rowChosen: false

    onShelvesChanged: {
        const n = shell.shelves.length
        if (!n) return

        if (!shell.rowChosen) {
            if (shell.row !== 0) shell.row = 0
            shell.rowTitle = shell.shelves[0].title
            return
        }

        if (shell.rowTitle) {
            for (let i = 0; i < n; i++) {
                if (shell.shelves[i].title === shell.rowTitle) {
                    if (shell.row !== i) shell.row = i
                    return
                }
            }
        }
        // The shelf we were on is gone (the last window closed, say). Stay in
        // range and adopt whatever is there now.
        if (shell.row >= n) shell.row = n - 1
        shell.rowTitle = shell.shelves[shell.row].title
    }

    // ⚠ A COPY. Mutating the object and assigning the SAME REFERENCE back is
    // the obvious spelling and it does not work: Qt compares the incoming
    // QVariant against the stored one, finds the identical JS object, and drops
    // the write — so nothing that reads `cols` is ever re-evaluated. Reassigning
    // "to emit a change" emits nothing.
    //
    // That is what made big screen mode look half-built. Everything HORIZONTAL
    // was dead — left, right, the shoulder-button page jumps, Home, and the
    // mouse moving along one shelf — while up and down worked perfectly, because
    // `row` is an int and an int compares unequal.
    function setCol(r, v) {
        shell.cols = Object.assign({}, shell.cols, { [shell.colKey(r)]: v })
    }

    // ── the pointer has to have actually MOVED ──────────────────────────────
    //
    // ⚠ A hover event is not evidence that anybody touched the mouse.
    //
    // Qt re-delivers hover at the LAST KNOWN cursor position on every frame in
    // which the scene graph is dirty (QQuickDeliveryAgentPrivate::
    // flushFrameSynchronousEvents), and it is right to: an item that slides out
    // from under a stationary pointer has genuinely stopped being hovered. But
    // every selection move here animates — the shelf column slides in y for
    // 200ms, the strip slides in x for 200ms under ApplyRange, the tile scales
    // for 140ms — so one press of the d-pad is a dozen frames of tiles being
    // dragged past a cursor that is sitting perfectly still.
    //
    // Each of those frames used to re-enter a tile and write row and col from
    // whatever had drifted under the pointer, which is a FEEDBACK LOOP: hover
    // sets the selection, the selection scrolls the strip, the scroll moves
    // another tile under the pointer, and that sets the selection again.
    //
    // It is worth spelling out the three faces this wore, because none of them
    // looks like a mouse problem and all three are the same bug:
    //
    //   · up and down "not taking" — the row moved and the cursor put it back
    //   · the selection "jumping" — it landed on whatever the animation happened
    //     to drag past, which is any shelf, not the next one
    //   · launching "one behind" — the strip scrolls by exactly one tile, so the
    //     tile now under the cursor is one off from the highlight that was drawn
    //     when the move started, and A activates the state, not the picture
    //
    // And it only bites once the surface has EVER seen the pointer, which is why
    // it arrived with the controller-as-mouse: coming back from an app leaves
    // the cursor wherever the stick left it, which is over the tiles.
    //
    // So hover is gated on the cursor's SCENE position changing. Sitting still
    // while the world moves underneath is not input.
    property real ptrX: -1
    property real ptrY: -1

    function pointerMoved(g) {
        if (Math.abs(g.x - shell.ptrX) < 1 && Math.abs(g.y - shell.ptrY) < 1)
            return false
        shell.ptrX = g.x
        shell.ptrY = g.y
        return true
    }

    // Every deliberate move goes through here, so `rowTitle` cannot fall out
    // of step with `row` — the mouse path included, which is why this exists
    // rather than two assignments in the delegate.
    function setRow(i) {
        if (i < 0 || i >= shell.shelves.length) return
        shell.row = i
        shell.rowTitle = shell.shelves[i].title
        shell.rowChosen = true
    }

    // ⚠ UP AND DOWN MOVE A BAND, not a shelf. Two shelves packed onto one row
    // are one row to the person holding the controller, and a d-pad that needed
    // two presses to leave a row it had visibly already left is the same fault
    // as a scrollbar that moves the wrong list.
    //
    // The place ALONG the band is kept: leaving Apps, which is the second shelf
    // of its row, lands on the second shelf of the next one where there is one.
    // Dropping to the first every time drags the selection back across the
    // screen for a press that was purely vertical.
    function moveRow(d) {
        const bs = shell.bands
        if (!bs.length) return
        const at = shell.place(shell.row)
        const nb = at[0] + d
        if (nb < 0 || nb >= bs.length) return   // no wrap: the ends are a landmark
        const band = bs[nb]
        const tgt = band[Math.min(at[1], band.length - 1)]
        shell.row = tgt.row
        shell.rowTitle = shell.shelves[tgt.row].title
        shell.rowChosen = true
    }

    // ⚠ AND LEFT AND RIGHT RUN ALONG THE WHOLE BAND. Running off the end of a
    // shelf that has another one drawn beside it steps into that one, at the
    // edge you arrived from — otherwise the last tile of Media is a wall with
    // three tiles of Apps visible on the other side of it.
    function moveCol(d) {
        const sh = shell.shelves[shell.row]
        if (!sh) return
        const n = sh.items.length
        if (!n) return

        const next = shell.col(shell.row) + d
        if (next >= 0 && next <= n - 1) { shell.setCol(shell.row, next); return }

        const at = shell.place(shell.row)
        const band = shell.bands[at[0]] || []
        const step = next < 0 ? -1 : 1
        const over = band[at[1] + step]
        if (!over) { shell.setCol(shell.row, step < 0 ? 0 : n - 1); return }

        shell.row = over.row
        shell.rowTitle = shell.shelves[over.row].title
        shell.rowChosen = true
        const m = shell.shelves[over.row].items.length
        shell.setCol(over.row, step > 0 ? 0 : Math.max(0, m - 1))
    }

    function current() {
        const sh = shell.shelves[shell.row]
        if (!sh) return null
        return sh.items[shell.col(shell.row)] || null
    }

    // ── switching to, and closing, something already open ───────────────────

    // Which window a close is waiting on confirmation for; null when nothing
    // is being asked. The confirm is not optional and not a setting: this is a
    // gamepad on a sofa, a button is easy to catch with a sleeve, and the
    // thing on the other side of it may be a half-written message.
    property var closing: null

    function switchTo(win) {
        if (!win || !win.app_id) return
        focusProc.command = [shell.bin, "big", "focus", win.app_id]
        focusProc.running = true
        // The tile record carries the pointer/keys columns for whatever the
        // shelf knew about it; a window nothing matched gets a pointer, which
        // is the safe default for something we cannot describe.
        shell.activeApp = { id: win.app_id, name: win.name,
                            pointer: "1", keys: "1" }
        shell.stepAside()
    }

    function askClose(win) {
        if (win && win.app_id) shell.closing = win
    }

    function confirmClose() {
        const win = shell.closing
        shell.closing = null
        if (!win || !win.app_id) return
        closeProc.command = [shell.bin, "big", "close", win.app_id]
        closeProc.running = true
        // Not instant: the application is being ASKED to close, and a well
        // behaved one may put up its own "save first?" before it goes.
        windowsTimer.restart()
    }

    Process { id: focusProc }
    Process { id: closeProc; onExited: shell.refreshWindows() }

    function activate() {
        const sh = shell.shelves[shell.row]
        const it = shell.current()
        if (!sh || !it) return

        // Something already open: go to it. Nothing is launched, because it is
        // already running — this is the way BACK.
        if (sh.kind === "running") {
            shell.switchTo(it)
            return
        }

        if (sh.kind === "game") {
            // Steam is handed a URL and returns at once, so there is nothing
            // to wait for and nothing that can tell us the game has been
            // quit. Get out of the way and let Guide be the way back — which
            // is what a console does anyway.
            shell.launchApp({ name: it.name, pointer: "0", keys: "0" },
                            ["big", "launch", it.appid])
            return
        }

        if (sh.kind === "news") {
            if (!it.link) return
            shell.launchApp({ name: it.title, pointer: "1", keys: "1" },
                            ["big", "open", it.link, "--wait"])
            return
        }

        if (shell.runAction(it)) return

        // Already running: this is a way BACK to it, not a second copy.
        if (shell.activeApp && shell.activeApp.id === it.id) {
            shell.stepAside()
            return
        }

        shell.launchApp(it, ["big", "run", it.id, "--wait"])
    }

    Process { id: actionProc }

    // ── the machine's own switches ──────────────────────────────────────────
    //
    // ⚠ ONE IMPLEMENTATION, and that is the whole reason this is a function.
    // These used to be reachable only as tiles on a shelf; they are now
    // reachable only from the Start menu, and the shelf path is still here
    // because `activate()` must keep working for a `kind: "action"` tile that
    // arrives from big.c on some other shelf. Two copies of "what Sleep does"
    // is how one of them ends up not restarting the launch overlay, which
    // looks like a button that did nothing.
    //
    // Returns whether it took the item, so the caller can carry on.
    function runAction(it) {
        if (!it) return false

        // ⚠ TWO WAYS OUT, AND ONLY ONE OF THEM ENDS THE PROCESS. Both reveal
        // the desktop that was underneath all along, and from a sofa they look
        // the same — the difference is what is left running.
        //
        // Desktop steps aside: the surface is unmapped and the shell stays
        // loaded, so Guide comes straight back to the same screen with the
        // same selection. That is the common case and it is why this is not
        // simply a second Quit.
        if (it.id === "desktop") {
            shell.stepAside()
            return true
        }

        // Quit really does end it. Super+F10 only ever hides this, so without
        // a tile the ordinary way out left big screen mode resident for the
        // rest of the session — and a layer-shell surface is not a window, so
        // nothing in the dock or the switcher could close it either.
        if (it.id === "quit") {
            Qt.quit()
            return true
        }

        // Sleep, restart and power off deliberately do NOT get out of the way:
        // the machine is either coming back to this screen or going away
        // entirely, and dropping to the desktop for the half second in between
        // is a flash of somebody's email on a television.
        if (it.kind === "action") {
            actionProc.command = [shell.bin, "big", "run", it.id]
            actionProc.running = true
            shell.launchingName = it.name || ""
            launchingTimer.restart()
            return true
        }

        return false
    }

    // ── the Start menu ──────────────────────────────────────────────────────
    //
    // Where the system row went. Four buttons pressed once a day do not earn a
    // row of a television, and the row they had was one more thing to scroll
    // past on the way to the headlines — but they cannot be BURIED either,
    // because "how do I turn this off" has to have an answer that is one
    // button from anywhere.
    //
    // Start is that button, and the keyboard's spelling of it is S. Escape
    // still quits and Backspace is still Back; S is free, it stands for the
    // button's own name, and unlike a modifier chord it is one key somebody
    // can find while holding a pad in the other hand.
    property bool menuOpen: false
    property int menuIndex: 0

    function menuToggle() {
        if (shell.menuOpen) { shell.menuOpen = false; return }
        // Nothing behind it is not an empty menu, it is no menu. big.c emits
        // the four switches unconditionally, so this is the state during the
        // first frames before `big apps` has answered.
        if (!shell.menuItems.length) return
        shell.menuIndex = 0
        shell.menuPage = "main"
        shell.menuBusy = ""
        shell.menuOpen = true
        // Asked as it opens, not when it was last drawn: the track may have
        // changed since, and a menu that says the wrong song is worse than one
        // that takes a moment to say the right one.
        shell.refreshMusic()
        shell.refreshSources()
    }

    // Transport, which is the one thing in this menu that does NOT close it —
    // pausing a track and being thrown back to the tiles means opening the menu
    // again for every press, and skipping three tracks is three presses.
    function musicCmd(verb) {
        musicCmdProc.command = [shell.bin, "big", "music", verb]
        musicCmdProc.running = true
    }

    Process {
        id: musicCmdProc
        // ⚠ The state after a transport command is what the row must show, and
        // it is only true once the command has finished. Asking before that
        // draws the state from before the press — a pause that still says
        // playing, which reads as a button that did not work.
        onExited: shell.refreshMusic()
    }

    function menuMove(d) {
        const n = shell.menuItems.length
        if (!n) return
        shell.menuIndex = Math.max(0, Math.min(n - 1, shell.menuIndex + d))
    }

    // ── choosing a source, and picking an album ─────────────────────────────
    //
    // Both are one command that takes a couple of seconds — the player is
    // stopped and started again, and an album is queued a track at a time — so
    // both say so on the panel while they run. ⚠ The Starting… overlay cannot
    // do that job here: it is drawn UNDER the menu, which is the right order
    // for a tile launch and the wrong one for something happening inside the
    // menu itself.
    //
    // ⚠ GUARDED ON `running`. Setting `running = true` on a quickshell Process
    // that is already running is a SILENT no-op, and these are the two places
    // in this file where somebody can press A twice in a second.
    function chooseSource(it) {
        if (sourceSetProc.running) return
        shell.menuBusy = "Switching to " + it.name + "…"
        sourceSetProc.next = it.action || "play"
        sourceSetProc.command = [shell.bin, "big", "music", "source", it.id]
        sourceSetProc.running = true
    }

    Process {
        id: sourceSetProc
        property string next: "play"
        onExited: {
            shell.menuBusy = ""
            shell.refreshSources()
            shell.refreshMusic()

            // What happens NEXT is the source's own answer, not this file's:
            // Plex has a library to pick from, YouTube Music and Spotify are
            // reachable only inside cliamp, and the other two are already
            // playing. See source_action() in big.c.
            if (sourceSetProc.next === "albums") {
                shell.openMenuPage("albums")
            } else if (sourceSetProc.next === "browse") {
                shell.menuOpen = false
                shell.menuPage = "main"
                shell.launchApp({ id: "music-browse", name: "cliamp",
                                  pointer: "0", keys: "1" },
                                ["big", "music", "browse"])
            } else {
                shell.menuPage = "main"
                shell.menuIndex = 0
            }
        }
    }

    function playAlbum(it) {
        if (albumPlayProc.running) return
        shell.menuBusy = "Loading " + it.name + "…"
        albumPlayProc.command = [shell.bin, "big", "music", "plex", it.id]
        albumPlayProc.running = true
    }

    Process {
        id: albumPlayProc
        onExited: {
            shell.menuBusy = ""
            shell.menuPage = "main"
            shell.menuIndex = 0
            shell.refreshMusic()
        }
    }

    function menuActivate() {
        const it = shell.menuItems[shell.menuIndex]
        if (!it) return

        if (it.kind === "music") {
            shell.musicCmd("toggle")
            return				// stays open, deliberately
        }
        // The pages stay open too — they ARE the menu, one level down.
        if (it.kind === "page") {
            shell.openMenuPage(it.page)
            return
        }
        if (it.kind === "source") {
            shell.chooseSource(it)
            return
        }
        if (it.kind === "album") {
            shell.playAlbum(it)
            return
        }

        // Closed FIRST. Sleep comes back to this screen, and coming back to a
        // menu somebody left open half an hour ago is the interface having
        // remembered the wrong thing.
        shell.menuOpen = false
        shell.menuPage = "main"
        if (shell.runAction(it)) return

        // ⚠ AND AN APPLICATION IF IT IS ONE. runAction takes the switches and
        // the way out; everything else on this menu is a tile like any other,
        // and before the visualizer arrived there was nothing here that was —
        // so a `kind: "app"` row reached this point and QUIETLY DID NOTHING.
        shell.launchApp(it, ["big", "run", it.id, "--wait"])
    }

    // ── one place where every input arrives ─────────────────────────────────
    //
    // The controller stream and the keyboard both call this, so a key and a
    // button cannot drift apart, and testing one tests the other. That matters
    // here more than usual: the controller path cannot be driven by a test at
    // all (it needs real hardware on the seat), and this makes the keyboard a
    // real proxy for it.
    function closeCurrent() {
        const sh = shell.shelves[shell.row]
        if (!sh || sh.kind !== "running") return
        shell.askClose(shell.current())
    }

    function nav(cmd) {
        if (shell.away) { shell.navAway(cmd); return }

        // A question on screen owns every button until it is answered. Without
        // this, the d-pad would still be moving the selection behind a dialog
        // asking about a window that is no longer the selected one — and A
        // would then close whatever the selection had wandered onto.
        if (shell.closing) {
            if (cmd === "accept")                      shell.confirmClose()
            else if (cmd === "back" || cmd === "guide") shell.closing = null
            return
        }

        // The Start menu owns every button while it is up, for the same reason
        // and drawn UNDER the close question, which is the order these two are
        // guarded in here.
        if (shell.menuOpen) {
            const on = shell.menuItems[shell.menuIndex]
            const onMusic = on && on.kind === "music"
            switch (cmd) {
            case "up":     shell.menuMove(-1); break
            case "down":   shell.menuMove(1); break
            // ⚠ Left and right belong to the music row and to nothing else.
            // A switch that reads "Power off" does not have a sideways, and a
            // d-pad that appears to do something on it is a d-pad somebody
            // will press again to find out what.
            case "left":   if (onMusic) shell.musicCmd("prev"); break
            case "right":  if (onMusic) shell.musicCmd("next"); break
            case "accept": shell.menuActivate(); break
            // ⚠ BACK GOES UP A PAGE, and only closes from the top one — the
            // same rule B follows on the shelves. A hundred and thirty albums
            // that shut the whole menu on one wrong press is a list nobody
            // browses twice.
            case "back":
                if (shell.menuPage !== "main") {
                    shell.menuPage = "main"
                    shell.menuIndex = 0
                } else {
                    shell.menuOpen = false
                }
                break
            // Start and Guide close it outright from wherever it is: they are
            // the way OUT of the menu, not a step in it.
            case "menu":
            case "guide":
                shell.menuOpen = false
                shell.menuPage = "main"
                break
            default: break
            }
            return
        }

        switch (cmd) {
        case "up":         shell.moveRow(-1); break
        case "down":       shell.moveRow(1); break
        case "left":       shell.moveCol(-1); break
        case "right":      shell.moveCol(1); break
        case "page-left":  shell.moveCol(-6); break
        case "page-right": shell.moveCol(6); break
        case "accept":     shell.activate(); break
        // ⚠ START IS THE ONE BUTTON WITH A DIFFERENT JOB ON EACH SIDE OF
        // STEPPING ASIDE. Here it is the machine's own switches; while an
        // application is up it is the on-screen keyboard (see navAway). That
        // is not an inconsistency to tidy away — there is nothing to type into
        // on this screen, and nothing to suspend from behind somebody's game.
        case "menu":       shell.menuToggle(); break
        // Back goes UP a shelf, and from the top one it steps aside. A button
        // that does nothing at the top of the screen is a button somebody
        // presses three times before reaching for the keyboard they left on
        // the table.
        // ⚠ THE TOP BAND, not the top shelf. On a row where two shelves are
        // packed side by side, `row > 0` is true while the selection is still
        // on the very first row of the screen — and B would then move sideways
        // instead of stepping aside, which is the one thing it must do when
        // there is nowhere left above.
        case "back":       if (shell.place(shell.row)[0] > 0) shell.moveRow(-1)
                           else shell.stepAside()
                           break
        // X closes what the selection is on, and only on the Running shelf —
        // it is the one place where the thing under the cursor is a window
        // rather than a way to open one. Everywhere else it does nothing,
        // deliberately: a close button that works on a tile would be a close
        // button that closes something you did not mean.
        case "search":     shell.closeCurrent(); break
        // Guide is the way OUT of the interface and, from the desktop, the way
        // back in — that half is `big guard`, which is watching the same
        // button while this is not running.
        case "guide":      shell.stepAside(); break
        default: break
        }
    }

    // While hidden, almost everything belongs to whatever is on screen. Only
    // two buttons are ours, and both have to be, because they are the way back
    // from a full-screen application on a machine with no keyboard in reach.
    function navAway(cmd) {
        if (cmd === "guide") { shell.comeBack(); return }

        if (!shell.oskOpen) {
            // Start opens the keyboard, when there is something to type into.
            if (cmd === "menu") shell.oskToggle()
            return
        }

        switch (cmd) {
        case "up":         shell.oskMove(-1, 0); break
        case "down":       shell.oskMove(1, 0); break
        case "left":       shell.oskMove(0, -1); break
        case "right":      shell.oskMove(0, 1); break
        case "accept":     shell.oskPress(); break
        case "back":       shell.oskOpen = false; break
        case "menu":       shell.oskToggle(); break
        case "search":     shell.key("BackSpace"); break   // X
        case "info":       shell.type(" "); break          // Y
        case "page-left":  shell.oskLayout = (shell.oskLayout + 2) % 3; break
        case "page-right": shell.oskLayout = (shell.oskLayout + 1) % 3; break
        default: break
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // The interface itself
    // ═══════════════════════════════════════════════════════════════════════

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

            // ⚠ `away` unmaps this. Not opacity, not a colour — the surface
            // itself goes, because an overlay-layer window the size of the
            // screen sitting on top of a game is a composite pass per frame
            // and the end of direct scanout, for something nobody can see.
            visible: chosen && !shell.away

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

            // Everything scales off the screen, so the same file is right on a
            // 1080p television and a 4K one. A fixed pixel size is what makes
            // a desktop UI unreadable on a TV in the first place.
            //
            // ⚠ HEIGHT ALONE WAS NOT ENOUGH, and the failure is invisible on
            // the shape this was written on. Height gives the same answer for
            // every 16:9 panel — 1080p, 1440p and 4K all land on the identical
            // layout, which is the part that already worked — but it says
            // nothing about how much room there is ACROSS. A 1080x1920 portrait
            // panel got u=35.6 and fitted less than three tiles on a screen
            // wide enough for six.
            //
            // ⚠ 96 IS NOT A SECOND TASTE DECISION: 54 × 16/9 = 96 exactly, so
            // on any 16:9 screen the two terms are equal and this is a NO-OP.
            // It can only ever make the unit smaller, and only on a screen
            // proportionally narrower than the one the design assumes.
            readonly property real u: Math.max(12, Math.min(win.height / 54,
                                                            win.width / 96))

            // How wide a row is, in units, for the packer that decides which
            // shelves can share one.
            //
            // ⚠ FROM THE CHOSEN WINDOW ONLY. There is one of these per screen
            // and only ever one on show, so without the guard the desk
            // monitor's width would decide how the television packs its
            // shelves — and which screen won would come down to the order
            // Quickshell happened to build them in, which is the sort of bug
            // that is right on the machine it was written on.
            Binding {
                target: shell
                property: "rowUnits"
                value: win.width / win.u
                when: win.chosen
            }

            onVisibleChanged: if (visible) keys.forceActiveFocus()
            Component.onCompleted: if (visible) keys.forceActiveFocus()

            // ── the artwork band: the top of the screen IS the picture ──────
            //
            // ⚠ THIS WAS A FULL-SCREEN WASH AND IT WAS VERY NEARLY INVISIBLE.
            // The hero was drawn over the whole window at opacity 0.5 and a
            // scrim then went over it at alpha 0.80 rising to 0.97, so what
            // reached the panel was about a TENTH of the picture at the top of
            // the screen and under two percent at the bottom. Valve ships a
            // 1920x620 hero per title and this was spending a full-screen
            // texture on a rumour of one.
            //
            // The art gets a BAND of its own instead, edge to edge across the
            // screen and near-opaque inside it, and the scrim stops being one
            // flat wash over everything. Two directional ones over the band
            // alone do the same job better:
            //
            //   across — solid where the title sits, clear at the right-hand
            //            edge. The words get ground under them without the
            //            picture being hidden everywhere to provide it.
            //   down   — into the page, so the band has no edge. A picture
            //            that stops on a line reads as a header; one that
            //            dissolves reads as the screen it is on.
            //
            // Below the band there is nothing left to scrim, which is why the
            // old full-screen gradient could go rather than be kept and
            // weakened: `win.color` is already this exact colour, so the old
            // bottom stop (#f705060a over #05060a) was painting the background
            // onto the background.
            Item {
                id: artBand
                anchors { top: parent.top; left: parent.left; right: parent.right }
                clip: true

                // ⚠ It BLEEDS PAST the banner rather than making the banner
                // taller. Every unit this takes as LAYOUT comes off the stage
                // and pushes the third shelf off a 720p panel; taken as
                // BACKGROUND it costs nothing at all, because the shelves are
                // declared after it and draw straight over the tail of it.
                //
                // ⚠ NINE units of tail, and it was five. The band has to end
                // by DISSOLVING, and a fade needs room: at five the picture
                // was still at a third of its brightness when the clip took
                // it, which draws a horizontal line straight across the
                // television at the top of the first shelf. The rig showed it
                // as a hard edge under the hero's glow. The tail is free —
                // it is background, and the shelves draw over it — so the
                // only thing spending it buys is the absence of that line.
                height: banner.y + banner.height + win.u * 9

                // ⚠ SIZED, not anchored to fill, and PreserveAspectCrop is a
                // fallback here rather than the mechanism. A crop fits the box
                // and then centres what is left over, and this box is far
                // wider than 3:1 — so on a hero it would keep a horizontal
                // slice out of the MIDDLE of the picture, which is where the
                // subject's waist is. Giving the image the band's width and
                // its own aspect-correct height instead puts the crop at the
                // BOTTOM, where a hero has its ground and its gradient, and
                // `clip` on the band takes it.
                Image {
                    id: hero
                    width: artBand.width
                    height: Math.max(artBand.height,
                                     implicitWidth > 0
                                     ? artBand.width * implicitHeight / implicitWidth
                                     : artBand.height)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    source: {
                        const it = shell.current()
                        return (it && it.hero) ? "file://" + it.hero : ""
                    }
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 220 } }
                }

                // Across. The title, the subtitle and the logo all live in the
                // left half, and this is the ground they stand on — which is
                // the whole reason the picture can be left alone on the right.
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.00; color: "#fa05060a" }
                        GradientStop { position: 0.38; color: "#f005060a" }
                        GradientStop { position: 0.62; color: "#a805060a" }
                        GradientStop { position: 1.00; color: "#2605060a" }
                    }
                }

                // Down. Dark again at the very top so the clock has ground
                // too, open through the middle, and closed at the bottom
                // because that edge has to not exist.
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.00; color: "#8c05060a" }
                        GradientStop { position: 0.26; color: "#4505060a" }
                        GradientStop { position: 0.48; color: "#4505060a" }
                        GradientStop { position: 0.70; color: "#9905060a" }
                        GradientStop { position: 0.88; color: "#ee05060a" }
                        GradientStop { position: 1.00; color: "#ff05060a" }
                    }
                }
            }

            // ── header ──────────────────────────────────────────────────────
            Item {
                id: header
                anchors { top: parent.top; left: parent.left; right: parent.right }
                anchors.margins: win.u * 1.6
                // 3.4 rather than 3: the dendrite mark stands beside two lines
                // of type and has to be taller than they are to read as an
                // emblem rather than a stray glyph, and it is bounded by this
                // box. The 0.4u comes off the stage, which scrolls anyway.
                height: win.u * 3.4

                Column {
                    id: wordmark
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

                // The dendrite mark, beside the wordmark rather than above or
                // behind it — the same pairing synui's welcome panel uses, so
                // the television and the desktop introduce the system the same
                // way.
                //
                // ⚠ ANCHORED TO THE WORDMARK, not placed in a Row with it. A
                // Row positions its children by x and refuses to position one
                // that anchors itself, so the two would silently overlap at the
                // left margin the first time somebody added a vertical anchor
                // to keep the mark centred against two lines of type.
                Image {
                    id: mark
                    anchors.left: wordmark.right
                    anchors.leftMargin: win.u * 0.9
                    anchors.verticalCenter: wordmark.verticalCenter
                    // ⚠ THE DRAWING IS NOT CENTRED IN ITS OWN CANVAS. The mark
                    // spans y −464…272 of a 1024 box centred on 512, so its ink
                    // sits about 9% of the box ABOVE the middle — centre the
                    // box on the type and the emblem reads as floating. This
                    // pushes the box down by that much so the INK lines up.
                    anchors.verticalCenterOffset: height * 0.094

                    source: shell.logoFile ? "file://" + shell.logoFile : ""
                    visible: status === Image.Ready

                    // Tied to the wordmark's own size: the emblem has to keep
                    // its footing beside the type at every screen shape, and a
                    // fixed multiple of u drifts against text that is already
                    // a multiple of u.
                    //
                    // ⚠ BIGGER THAN THE TYPE IT STANDS BESIDE, because the ink
                    // fills only about three quarters of the canvas in each
                    // direction. Matched to the wordmark's height it rendered
                    // a third smaller than it looks here and read as a stray
                    // mark rather than an emblem.
                    //
                    // ⚠ AND BOUNDED BY THE HEADER, which is not belt and
                    // braces: at 1.3x the type it stood 11px PAST the bottom of
                    // its own box and into the banner underneath. Nothing
                    // clips there, so it drew fine — until a wider piece of
                    // game logo art reached the same place, which is a
                    // collision nobody would think to look for in the header.
                    height: Math.min(wordmark.height * 1.3, header.height * 0.95)
                    width: height
                    fillMode: Image.PreserveAspectFit
                    // An SVG is rasterised at sourceSize, NOT at the drawn
                    // size — left alone it decodes at the 1024x1024 in the file
                    // and is then scaled down, which is a megabyte of texture
                    // for a mark two centimetres across. Twice the drawn height
                    // is enough for a 2x screen.
                    sourceSize.width: Math.round(height * 2)
                    sourceSize.height: Math.round(height * 2)
                    smooth: true
                    asynchronous: true
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

            // ── what is running behind this ─────────────────────────────────
            //
            // Only when something is. Coming back to the interface while the
            // browser is still open and being told nothing about it is how
            // somebody ends up with four browsers.
            Rectangle {
                id: runningStrip
                anchors { top: header.bottom; left: parent.left; right: parent.right }
                anchors.leftMargin: win.u * 1.6
                anchors.rightMargin: win.u * 1.6
                anchors.topMargin: win.u * 0.4
                height: visible ? win.u * 1.9 : 0
                visible: shell.activeApp !== null
                radius: win.u * 0.4
                color: "#332a4d"
                border.width: 1
                border.color: "#4b3f73"

                Text {
                    anchors.centerIn: parent
                    color: win.ink
                    font.pixelSize: win.u * 0.85
                    text: shell.activeApp
                          ? (shell.activeApp.name || "Something")
                            + " is still open   ·   Guide goes back to it"
                          : ""
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
                anchors { top: runningStrip.bottom; left: parent.left; right: parent.right }
                anchors.leftMargin: win.u * 1.6
                anchors.rightMargin: win.u * 1.6
                height: win.u * 5.2

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    // ⚠ Narrower than it was (0.7), and that is not a
                    // reduction. The right-hand side of this band is where the
                    // artwork now reads, and a title column running 70% of the
                    // way across puts two lines of text over the brightest
                    // part of it. What fills the screen across here is the
                    // PICTURE; the words only have to be legible.
                    width: parent.width * 0.58
                    spacing: win.u * 0.3

                    // ── the game's own title art, when Steam has it ─────────
                    //
                    // ⚠ big.c has ALWAYS found this — art_find() checks the
                    // user's grid override and all three cache layouts for
                    // logo.png, and big_games emits it as a column — and this
                    // file has never once read it. It is the single asset that
                    // makes a library look like the game's own front door
                    // rather than a list of names, and it was arriving on
                    // every record and being dropped on the floor.
                    //
                    // ⚠ BOUNDED BOTH WAYS, because a logo has no reliable
                    // shape. They run from nearly square to 8:1 ribbons, and
                    // one given only a height will happily draw itself off the
                    // side of the screen and over its own artwork.
                    Image {
                        id: titleLogo
                        width: parent.width
                        height: win.u * 3.8
                        fillMode: Image.PreserveAspectFit
                        horizontalAlignment: Image.AlignLeft
                        asynchronous: true
                        cache: false
                        // Decoded to the height it is drawn at. Valve's logos
                        // are up to 1280 wide and the selection changes on
                        // every press of the stick.
                        sourceSize.height: Math.round(win.u * 5)
                        source: {
                            const it = shell.current()
                            return (it && it.logo) ? "file://" + it.logo : ""
                        }
                        // ⚠ Visible as soon as there IS one, not once it has
                        // LOADED, and the two are a frame or more apart on a
                        // 1280-wide PNG. Gating the space on `Ready` shows the
                        // text title in the gap and then swaps it for the logo
                        // — a flash of the wrong thing on every press of the
                        // stick. The slot is held from the moment the record
                        // says there is art; only the ink fades in.
                        visible: String(source) !== "" && status !== Image.Error
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                    }

                    Text {
                        width: parent.width
                        // The name in words, whenever there is no logo — which
                        // is most of the shelves (an app, an action and a
                        // headline have no artwork at all) and plenty of games.
                        // This is the unchanged original, kept as the fallback
                        // rather than replaced.
                        visible: !titleLogo.visible
                        text: {
                            const it = shell.current()
                            if (!it) return ""
                            // A headline is a title, a game is a name. Both
                            // end up here, so both are asked for.
                            return it.name || it.title || ""
                        }
                        color: win.ink
                        font.pixelSize: win.u * 2.4
                        font.bold: true
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
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

                            if (sh.kind === "news")
                                return it.source || "news"

                            // Deliberately NOT the exec string for the app and
                            // action shelves. "systemctl suspend" under the
                            // word Sleep is a developer's answer to a question
                            // nobody at four metres asked, and it makes every
                            // tile look like a terminal command that might go
                            // wrong.
                            if (sh.kind !== "game") {
                                if (it.kind === "server")
                                    return it.source === "plex"
                                        ? "Plex server on this network"
                                        : "Jellyfin server on this network"
                                return ""
                            }

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
                    //
                    // ⚠ BY BAND, and it has to be: two shelves drawn side by
                    // side are ONE row of this Column, so counting shelves here
                    // would scroll a row too far for every band that holds more
                    // than one of them — and the amount of the error would
                    // depend on the screen's width, which is the shape of bug
                    // this file has already been bitten by once.
                    y: {
                        const items = rows.visibleChildren
                        const band = shell.place(shell.row)[0]
                        let off = 0
                        for (let i = 0; i < band && i < items.length; i++)
                            off += items[i].height + rows.spacing
                        const most = Math.max(0, rows.height - stage.height)
                        return -Math.min(off, most)
                    }
                    Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                    Repeater {
                        model: shell.bands

                        // One row of the television: the shelves that fit
                        // across it. A Row takes the height of its tallest
                        // child and lines them all up at the TOP, which is
                        // where the labels are — so a band mixing a shelf of
                        // covers with a shelf of app tiles still reads as one
                        // row rather than two things floating.
                        Row {
                            id: band
                            required property var modelData

                            width: rows.width

                            Repeater {
                                model: band.modelData

                                Item {
                                    id: shelf

                                    // { row, units, scale } — `row` indexes
                                    // shell.shelves, because a shelf is no
                                    // longer the same thing as a row and
                                    // everything downstream still needs to
                                    // name one.
                                    required property var modelData

                                    readonly property int shelfRow: shelf.modelData.row
                                    readonly property var sh:
                                        shell.shelves[shelf.shelfRow]
                                        || ({ title: "", kind: "app", items: [] })

                                    width: shelf.modelData.units * win.u
                                    height: label.height + win.u * 0.6 + strip.height

                                    // ⚠ CLIPPED AT THE SHELF, and until bars shared a
                                    // row nothing here needed to clip at all. A shelf
                                    // that overflowed always had the whole row, so what
                                    // ran past its right edge ran to the edge of the
                                    // SCREEN and the stage's own clip caught it. Three
                                    // bars on one row means the thing to the right of a
                                    // shelf is another shelf: Play's half-tile peek drew
                                    // ON TOP of Media's first tile. It did not read as a
                                    // clipping bug — it read as two tiles at slightly
                                    // wrong positions.
                                    //
                                    // ⚠ AND NOT ON THE STRIP, which is where it went
                                    // first and is one level too deep. A selected tile
                                    // scales from its CENTRE, and a delegate sits at y=0
                                    // in a horizontal ListView — the view owns the
                                    // cross-axis position, so a `y` on the delegate is
                                    // simply ignored and every bit of the strip's slack
                                    // is BELOW the tile. Clipping there sheared the
                                    // highlight's top border off flush, leaving the two
                                    // side borders standing with nothing joining them.
                                    //
                                    // This boundary is the same one horizontally — the
                                    // strip fills the shelf's width — and vertically it
                                    // has the label's 0.6u gap above the strip to grow
                                    // into, which is more than the 3% a tile takes.
                                    clip: true

                                    opacity: shell.row === shelf.shelfRow ? 1.0 : 0.45
                                    Behavior on opacity { NumberAnimation { duration: 160 } }

                                    Text {
                                        id: label
                                        x: win.u * 1.6
                                        text: shelf.sh.title
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
                                        height: strip.slotH + (strip.portrait ? win.u * 1.1
                                                                              : win.u * 0.8)
                                        orientation: ListView.Horizontal
                                        spacing: win.u * 0.8

                                        // ── fitting a whole number of tiles ─────────
                                        //
                                        // ⚠ THE TILE WAS A FIXED MULTIPLE OF u, so what
                                        // landed at the right-hand edge was whatever was
                                        // left over — and that leftover is decided by the
                                        // screen's ASPECT RATIO, which nothing here was
                                        // looking at. Measured across the shapes people
                                        // actually own, the last tile came out anywhere
                                        // from 10% visible (4:3, a sliver that reads as a
                                        // rendering fault) to 92% (21:9, a tile that looks
                                        // whole until you notice it is clipped). Only 16:9
                                        // looked deliberate, because 16:9 is what it was
                                        // drawn on.
                                        //
                                        // So the leftover stops being an accident: the
                                        // shelf picks the number of whole tiles that best
                                        // matches the intended size, then stretches the
                                        // pitch slightly so that number PLUS a constant
                                        // half-tile peek fills the row exactly. The peek
                                        // is kept on purpose — a row cut clean at the edge
                                        // gives no sign there is more along it, which is
                                        // the one thing a ten-foot list has to say.
                                        //
                                        // ⚠ AND IT IS ONLY WORTH SAYING WHEN IT IS TRUE.
                                        // A shelf whose tiles all fit has nothing past
                                        // the edge to promise, so the peek — and the
                                        // stretch that pays for it — is skipped there.
                                        // That is what lets Media and Apps sit side by
                                        // side with tiles of the SAME SIZE: each of them
                                        // stretching its own leftover would have made
                                        // one row of app tiles two different sizes, half
                                        // a screen apart, which reads as a rendering
                                        // fault rather than a layout.
                                        readonly property bool portrait:
                                            shelf.sh.kind === "game"
                                        // ⚠ THE BAND'S SCALE, not a fresh multiple of u.
                                        // The packer reserved this shelf's width from
                                        // the same number; a strip that worked out its
                                        // own would silently disagree with the row it
                                        // was given and clip its last tile.
                                        readonly property real idealW:
                                            win.u * shell.idealUnits(shelf.sh.kind)
                                                  * shelf.modelData.scale
                                        readonly property real peek: 0.5
                                        readonly property real content:
                                            width - leftMargin - rightMargin

                                        readonly property bool overflows:
                                            shelf.sh.items.length * (idealW + spacing)
                                                - spacing > content + 0.5

                                        readonly property int slots: Math.max(1,
                                            Math.round((content + spacing) / (idealW + spacing)
                                                       - peek))

                                        // ⚠ CLAMPED, because rounding to ONE tile on a
                                        // narrow screen would otherwise stretch that tile
                                        // to the full width. Past the clamp the peek is
                                        // wrong by a few percent, which is invisible; an
                                        // eighty-percent-wide cover is not.
                                        readonly property real slotW: {
                                            if (!strip.overflows) return idealW
                                            const fit = (content + spacing) / (slots + peek)
                                                        - spacing
                                            return Math.max(idealW * 0.85,
                                                            Math.min(idealW * 1.15, fit))
                                        }
                                        // ⚠ 2:3 IS THE ART, NOT A STYLE CHOICE — every
                                        // cover Steam caches is 600x900, so the height has
                                        // to follow the snapped width or the snapping
                                        // starts letterboxing 53 pictures.
                                        readonly property real slotH:
                                            portrait ? slotW * 1.5 : win.u * 7
                                        // The pointer does not drive this; a stray
                                        // flick from a touchpad would fight the
                                        // selection for control of the same list.
                                        interactive: false
                                        // Room for a selected tile to grow into at
                                        // either end, and for the first tile to sit
                                        // clear of the screen edge.
                                        leftMargin: win.u * 1.6
                                        rightMargin: win.u * 1.6

                                        currentIndex: shell.col(shelf.shelfRow)
                                        highlightRangeMode: ListView.ApplyRange
                                        preferredHighlightBegin: win.u * 1.6
                                        preferredHighlightEnd: width - win.u * 1.6
                                        highlightMoveDuration: 200

                                        model: shelf.sh.items

                                        delegate: Item {
                                            id: tile
                                            required property var modelData
                                            required property int index

                                            readonly property bool selected:
                                                shell.row === shelf.shelfRow
                                                && shell.col(shelf.shelfRow) === tile.index
                                            readonly property bool portrait:
                                                shelf.sh.kind === "game"
                                            readonly property bool headline:
                                                shelf.sh.kind === "news"

                                            // 2:3, which is the shape of every cover
                                            // Steam caches (600x900). Anything else
                                            // either letterboxes the art or crops
                                            // somebody's title off it. A headline is
                                            // wider than it is tall, because it is
                                            // words.
                                            //
                                            // ⚠ TAKEN FROM THE STRIP, not worked out
                                            // again from u: the strip has already
                                            // nudged this to whatever makes a whole
                                            // number of tiles fit the screen, and a
                                            // delegate that recomputed the ideal would
                                            // quietly undo that and put the ragged
                                            // edge back.
                                            width: strip.slotW
                                            height: strip.slotH
                                            scale: selected ? shell.tileSelectedScale : 1.0
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
                                                // A headline never has art and is
                                                // always this.
                                                Column {
                                                    id: face

                                                    // ⚠ CENTRED, and it used to be
                                                    // anchored to fill. A label pinned
                                                    // to the top of a tile with a
                                                    // glyph under it leaves the whole
                                                    // bottom third empty and the two
                                                    // pieces drift apart as the tile
                                                    // grows; a headline is different,
                                                    // because a headline is a
                                                    // paragraph and reads from its
                                                    // first line down.
                                                    x: win.u * 0.7
                                                    width: parent.width - win.u * 1.4
                                                    y: tile.headline
                                                       ? win.u * 0.7
                                                       : Math.max(win.u * 0.7,
                                                                  (parent.height - height) / 2)
                                                    spacing: win.u * 0.3
                                                    visible: !tile.modelData.art

                                                    // ── the drawn glyph ─────────────
                                                    //
                                                    // ⚠ big.c has been emitting the
                                                    // `icon` column since the day this
                                                    // table existed and this file has
                                                    // never read it — the same gap the
                                                    // logo was in. `iconfile` is that
                                                    // name resolved to a drawing that
                                                    // exists; empty means there is no
                                                    // glyph for it, and a tile without
                                                    // one is the tile as it was.
                                                    //
                                                    // A game has cover art and a
                                                    // headline is words, so this is
                                                    // only ever an app or an action —
                                                    // which is exactly the set that had
                                                    // nothing to look at.
                                                    Image {
                                                        id: tileGlyph
                                                        anchors.horizontalCenter: parent.horizontalCenter
                                                        width: win.u * 3.2
                                                        height: win.u * 3.2
                                                        visible: String(source) !== ""
                                                                 && status !== Image.Error
                                                        source: tile.modelData.iconfile
                                                                ? "file://" + tile.modelData.iconfile : ""
                                                        fillMode: Image.PreserveAspectFit
                                                        asynchronous: true
                                                        // Rasterised at the size drawn.
                                                        // These are vectors, so asking
                                                        // for the wrong one is a blurry
                                                        // glyph rather than a missing
                                                        // one — which is worse, because
                                                        // it looks like a decision.
                                                        sourceSize.width: Math.round(win.u * 4.4)
                                                        sourceSize.height: Math.round(win.u * 4.4)
                                                        opacity: tile.selected ? 1.0 : 0.75
                                                        Behavior on opacity {
                                                            NumberAnimation { duration: 140 }
                                                        }
                                                    }

                                                    Text {
                                                        width: parent.width
                                                        text: tile.modelData.name
                                                              || tile.modelData.title || ""
                                                        color: win.ink
                                                        font.pixelSize: tile.headline
                                                                        ? win.u * 0.9 : win.u * 1.1
                                                        font.bold: true
                                                        wrapMode: Text.WordWrap
                                                        elide: Text.ElideRight
                                                        maximumLineCount: tile.headline ? 4 : 3
                                                        horizontalAlignment: tile.headline
                                                            ? Text.AlignLeft : Text.AlignHCenter
                                                    }

                                                    Text {
                                                        width: parent.width
                                                        visible: text !== ""
                                                        text: tile.modelData.source || ""
                                                        color: win.dim
                                                        font.pixelSize: win.u * 0.7
                                                        elide: Text.ElideRight
                                                        horizontalAlignment: tile.headline
                                                            ? Text.AlignLeft : Text.AlignHCenter
                                                    }
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
                                                // ⚠ NOT onEntered. A tile sliding under
                                                // a parked cursor enters it just as
                                                // truly as a cursor moving onto the
                                                // tile, and only one of those is a
                                                // person choosing something — see
                                                // shell.pointerMoved. onPositionChanged
                                                // carries the coordinates that tell
                                                // them apart; entered() does not carry
                                                // any.
                                                onPositionChanged: (mouse) => {
                                                    if (!shell.pointerMoved(
                                                            tile.mapToItem(null, mouse.x, mouse.y)))
                                                        return
                                                    shell.setRow(shelf.shelfRow)
                                                    shell.setCol(shelf.shelfRow, tile.index)
                                                }
                                                onClicked: {
                                                    shell.setRow(shelf.shelfRow)
                                                    shell.setCol(shelf.shelfRow, tile.index)
                                                    shell.activate()
                                                }
                                            }
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
                        // ⚠ X appears only on the Running shelf, because that
                        // is the only shelf it does anything on. A legend that
                        // advertises a button everywhere and means it in one
                        // place teaches people it is broken.
                        model: {
                            const sh = shell.shelves[shell.row]
                            const running = sh && sh.kind === "running"
                            const out = [
                                { k: "A", v: running ? "Switch to" : "Select" },
                                { k: "B", v: "Back" }
                            ]
                            if (running) out.push({ k: "X", v: "Close" })
                            out.push({ k: "LB/RB", v: "Jump" })
                            // Start is where sleep, restart and power off went
                            // when they stopped being a row. A switch nobody
                            // can find is a switch that is not there, and this
                            // legend is the only place on a television it can
                            // be advertised.
                            if (shell.menuItems.length)
                                out.push({ k: "Start", v: "System" })
                            out.push({ k: "Guide",
                                       v: shell.activeApp ? "Resume" : "Desktop" })
                            return out
                        }
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

            // ── the Start menu ──────────────────────────────────────────────
            //
            // The machine's own switches, off the shelves and behind one
            // button. Dimmed screen behind it and the guard at the top of
            // nav(), the same shape as the close question below — and drawn
            // BEFORE it, so that if both ever coexist the question is on top,
            // which is the order nav() guards them in.
            //
            // Bottom left, above the legend that names the button that opened
            // it. A menu that appears in the middle of a television covers the
            // thing somebody was looking at when they pressed Start.
            Rectangle {
                anchors.fill: parent
                visible: shell.menuOpen
                color: "#cc05060a"

                // A real mouse is still a thing on this machine; clicking away
                // is the same "no" that B is.
                MouseArea {
                    anchors.fill: parent
                    onClicked: shell.menuOpen = false
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: win.u * 1.6
                    // ⚠ TO ITS OWN PARENT, with the footer's height as a
                    // NUMBER. `anchors.bottom: footer.top` is a generation too
                    // far — the footer is a sibling of the dimmed backdrop,
                    // not of this panel — and Qt answers that with one
                    // "cannot anchor to an item that isn't a parent or
                    // sibling" line and then draws the panel at y=0. It looked
                    // like a deliberate top-left menu until the rig rendered
                    // it against the row it is supposed to sit above.
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: footer.height + win.u * 1.2
                    // Wider on the album page: a menu of one-word switches and
                    // a menu of "Grandmaster Flash and the Furious Five" are
                    // not the same panel, and elide is not an answer when
                    // every row ends in the same three dots.
                    width: Math.min(parent.width * (shell.menuPage === "albums"
                                                    ? 0.60 : 0.42),
                                    win.u * (shell.menuPage === "albums"
                                             ? 34 : 24))
                    height: menuCol.implicitHeight + win.u * 2.4
                    radius: win.u * 0.8
                    color: "#1a1430"
                    border.width: Math.max(1, win.u * 0.08)
                    border.color: win.accent

                    Column {
                        id: menuCol
                        anchors.centerIn: parent
                        width: parent.width - win.u * 1.6
                        spacing: win.u * 0.3

                        Text {
                            text: {
                                if (shell.menuBusy) return shell.menuBusy.toUpperCase()
                                if (shell.menuPage === "source") return "MUSIC SOURCE"
                                if (shell.menuPage === "albums") return "PLEX ALBUMS"
                                return shell.musicLive ? "NOW PLAYING" : "SYSTEM"
                            }
                            color: win.dim
                            font.pixelSize: win.u * 0.8
                            font.letterSpacing: win.u * 0.12
                            font.bold: true
                            leftPadding: win.u * 0.7
                            bottomPadding: win.u * 0.3
                        }

                        // ── the rows ────────────────────────────────────
                        //
                        // ⚠ A LIST, NOT A REPEATER, and that arrived with the
                        // album page. A Repeater builds every row and the
                        // Column grows to hold them: fine for six switches,
                        // and a hundred and thirty albums is a panel taller
                        // than the television with its first row off the top
                        // of the screen. The list is capped at ten rows and
                        // scrolls; the panel is still only as tall as what it
                        // has to draw.
                        ListView {
                            id: menuList
                            width: menuCol.width
                            height: Math.min(contentHeight,
                                             win.u * (shell.menuPage === "main"
                                                      ? 26 : 22))
                            spacing: win.u * 0.3
                            clip: true
                            interactive: false
                            model: shell.menuItems
                            currentIndex: shell.menuIndex
                            // ⚠ Keeping the SELECTION on screen is the whole
                            // reason this scrolls: the d-pad moves an index,
                            // and an index that has walked off the bottom is a
                            // menu that has stopped responding as far as
                            // anybody watching can tell.
                            onCurrentIndexChanged:
                                menuList.positionViewAtIndex(menuList.currentIndex,
                                                             ListView.Contain)

                            delegate: Rectangle {
                                id: entry
                                required property var modelData
                                required property int index

                                readonly property bool chosen:
                                    shell.menuIndex === entry.index

                                readonly property bool isMusic:
                                    entry.modelData.kind === "music"

                                // A row carries a second line when it has
                                // something to say about itself: an album's
                                // artist, a source that is not set up, the
                                // transport legend on the music row.
                                readonly property bool hasNote:
                                    String(entry.modelData.note || "") !== ""

                                width: menuList.width
                                // The music row is taller because it carries two
                                // lines: what is playing, and what the d-pad does
                                // to it. Nothing else in this menu needs saying
                                // twice.
                                // ⚠ Tall enough for the text AND the meter to
                                // have their own band. They shared the row at
                                // 3.6 and the legend was read across the top of
                                // solid bars — legible up close, and exactly
                                // the sort of thing that stops being legible at
                                // four metres.
                                height: entry.isMusic ? win.u * 4.0
                                        : (entry.hasNote ? win.u * 3.4
                                                         : win.u * 2.6)
                                radius: win.u * 0.4
                                color: entry.chosen ? "#2b2450" : "transparent"
                                border.width: entry.chosen
                                              ? Math.max(1, win.u * 0.08) : 0
                                border.color: win.accent

                                // ── the visualizer ──────────────────────
                                //
                                // Drawn FIRST, so it is behind the title and
                                // the legend rather than over them, and
                                // clipped to the row's rounded corners.
                                //
                                // ⚠ It has to stay a BACKGROUND. The row's job
                                // is to say what is playing; bars bright enough
                                // to compete with the text would make a
                                // legibility problem out of a decoration, and
                                // this is read from four metres away.
                                clip: true

                                // ⚠ EVERY ANCHOR AND SIZE IN THIS DELEGATE
                                // NAMES `entry`, NEVER `parent`. A ListView
                                // reparents a delegate to null on its way out,
                                // and a binding that reads `parent.width` at
                                // that moment throws "cannot read property of
                                // null" — a stream of TypeErrors in the log
                                // and nothing wrong on screen, which is a
                                // fault that gets ignored until it is hiding a
                                // real one. It arrived the moment these rows
                                // stopped being a Repeater in a Column.
                                Row {
                                    id: meter
                                    visible: entry.isMusic
                                             && shell.musicBands.length > 0
                                    anchors.left: entry.left
                                    anchors.right: entry.right
                                    anchors.bottom: entry.bottom
                                    anchors.margins: entry.border.width
                                    // The bottom band only. See the row height.
                                    height: entry.height * 0.30
                                    spacing: win.u * 0.12

                                    Repeater {
                                        model: shell.musicBands

                                        Rectangle {
                                            id: bar
                                            required property var modelData
                                            required property int index

                                            // Share the row between however
                                            // many bands arrived, rather than
                                            // assuming the ten cliamp sends
                                            // today.
                                            width: Math.max(1,
                                                (meter.width
                                                 - (shell.musicBands.length - 1)
                                                   * meter.spacing)
                                                / shell.musicBands.length)
                                            anchors.bottom: meter.bottom
                                            height: Math.max(1,
                                                meter.height
                                                * Math.min(1, Math.max(0,
                                                    Number(bar.modelData) || 0)))
                                            radius: win.u * 0.08
                                            color: win.accent
                                            // Low, and low on purpose — see
                                            // above. The selected row's own
                                            // fill is already lighter, so the
                                            // bars sit a little further back
                                            // there to keep the contrast the
                                            // text needs.
                                            // Brighter than a watermark, now
                                            // that it has the row to itself —
                                            // a visualizer nobody can see is a
                                            // subprocess running for nothing.
                                            opacity: entry.chosen ? 0.55 : 0.40

                                            // 20 frames a second is visibly
                                            // steppy on a bar meter; this
                                            // carries each one to the next
                                            // rather than snapping.
                                            Behavior on height {
                                                NumberAnimation {
                                                    duration: 70
                                                    easing.type: Easing.OutQuad
                                                }
                                            }
                                        }
                                    }
                                }

                                // ⚠ CLICK ONLY, never hover. A hovered entry
                                // that moved the selection would be the tile
                                // bug this file already has a comment about:
                                // Qt re-delivers hover at the last cursor
                                // position on every dirty frame, so a menu
                                // opening under a stationary pointer would
                                // choose whatever it opened under.
                                MouseArea {
                                    anchors.fill: entry
                                    onClicked: {
                                        shell.menuIndex = entry.index
                                        shell.menuActivate()
                                    }
                                }

                                Image {
                                    id: entryIcon
                                    anchors.left: entry.left
                                    anchors.leftMargin: win.u * 0.7
                                    // ⚠ To the TEXT, not to the row. On the
                                    // music row the row's middle is where the
                                    // meter starts.
                                    anchors.verticalCenter: entryText.verticalCenter
                                    height: win.u * 1.4
                                    width: height
                                    source: {
                                        // Every music row borrows the Music
                                        // tile's glyph — what is playing, where
                                        // it comes from, and an album on the
                                        // server are all the same subject, and
                                        // three near-identical drawings would
                                        // be three things to keep in step.
                                        const k = entry.modelData.kind
                                        if (k === "music" || k === "page"
                                            || k === "source" || k === "album")
                                            return shell.musicIcon
                                                   ? "file://" + shell.musicIcon : ""
                                        return entry.modelData.iconfile
                                               ? "file://" + entry.modelData.iconfile
                                               : ""
                                    }
                                    visible: status === Image.Ready
                                    fillMode: Image.PreserveAspectFit
                                    sourceSize.width: Math.round(height * 2)
                                    sourceSize.height: Math.round(height * 2)
                                    smooth: true
                                }

                                // ⚠ Anchored to the icon whether or not the
                                // icon DREW. An invisible Image still has a
                                // width, so a machine missing one glyph keeps
                                // its menu in a column instead of shuffling
                                // one row left.
                                Column {
                                    id: entryText
                                    anchors.left: entryIcon.right
                                    anchors.leftMargin: win.u * 0.7
                                    anchors.right: entry.right
                                    anchors.rightMargin: win.u * 0.7
                                    // One anchor with a computed margin rather
                                    // than two conditional ones: an ordinary
                                    // row centres, and the music row sits up
                                    // to leave the meter its band.
                                    anchors.top: entry.top
                                    anchors.topMargin: entry.isMusic
                                        ? win.u * 0.5
                                        : (entry.height - entryText.height) / 2
                                    spacing: win.u * 0.15

                                    Text {
                                        width: entryText.width
                                        text: entry.modelData.name || ""
                                        color: entry.chosen ? win.ink : win.dim
                                        font.pixelSize: win.u * 1.0
                                        // A track title is somebody else's
                                        // text and can be any length; a switch
                                        // is one word.
                                        elide: Text.ElideRight
                                    }

                                    // What the d-pad does here, said on the row
                                    // it applies to. The transport is the only
                                    // place in this interface where left and
                                    // right mean something other than moving
                                    // the selection, and a legend at the bottom
                                    // of the screen cannot say "except here".
                                    Text {
                                        visible: entry.isMusic || entry.hasNote
                                        width: entryText.width
                                        text: entry.isMusic
                                            ? (String(shell.music.state) === "playing"
                                               ? "Playing" : "Paused")
                                              + "   ·   A pause   ·   ‹ › track"
                                            : String(entry.modelData.note || "")
                                        color: win.dim
                                        font.pixelSize: win.u * 0.75
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        // ⚠ AN EMPTY PAGE HAS TO SAY WHY. A panel with a
                        // heading and nothing under it is what a server that
                        // did not answer looks like, and from a sofa it is
                        // indistinguishable from a button that half worked.
                        Text {
                            visible: shell.menuPage !== "main"
                                     && shell.menuItems.length === 0
                                     && shell.menuBusy === ""
                            width: menuCol.width
                            wrapMode: Text.WordWrap
                            leftPadding: win.u * 0.7
                            topPadding: win.u * 0.4
                            bottomPadding: win.u * 0.4
                            color: win.dim
                            font.pixelSize: win.u * 0.9
                            text: shell.menuPage === "albums"
                                ? "Nothing came back from Plex. Check the "
                                  + "server, or run `cliamp setup` to give it "
                                  + "an address and a token."
                                : "Nothing to choose from."
                        }
                    }
                }
            }

            // ── "close this?" ───────────────────────────────────────────────
            //
            // Drawn LAST of the visible things, so it is over the shelves, and
            // paired with the guard at the top of nav() that stops every other
            // button while it is up. A dialog that only LOOKS modal is worse
            // than none: the selection would still be moving behind it.
            //
            // The confirm is not a setting. This is a gamepad on a sofa, X is
            // easy to catch with a sleeve, and on the other side of it may be
            // a half-written message or an unsaved file. One button press is a
            // cheap price for that never happening by accident.
            Rectangle {
                anchors.fill: parent
                visible: shell.closing !== null
                color: "#d905060a"

                MouseArea {
                    anchors.fill: parent
                    onClicked: shell.closing = null
                    // A real mouse is still a thing on this machine; clicking
                    // the dimmed area is the same "no" that B is.
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.7, win.u * 34)
                    height: body.implicitHeight + win.u * 5
                    radius: win.u * 0.8
                    color: "#1a1430"
                    border.width: Math.max(1, win.u * 0.08)
                    border.color: win.accent

                    Column {
                        id: body
                        anchors.centerIn: parent
                        width: parent.width - win.u * 4
                        spacing: win.u * 1.6

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: win.ink
                            font.pixelSize: win.u * 1.7
                            font.bold: true
                            text: "Close " + (shell.closing ? shell.closing.name
                                                            : "") + "?"
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: win.dim
                            font.pixelSize: win.u * 1.0
                            text: "Anything unsaved in it will be lost."
                        }

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: win.u * 2

                            Repeater {
                                model: [ { k: "A", v: "Close it" },
                                         { k: "B", v: "Keep it open" } ]
                                Row {
                                    id: choice
                                    required property var modelData
                                    spacing: win.u * 0.4

                                    Rectangle {
                                        width: win.u * 1.6
                                        height: win.u * 1.6
                                        radius: height / 2
                                        color: "#2a2340"
                                        border.width: 1
                                        border.color: "#453a66"
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text {
                                            anchors.centerIn: parent
                                            text: choice.modelData.k
                                            color: win.ink
                                            font.pixelSize: win.u * 0.85
                                            font.bold: true
                                        }
                                    }
                                    Text {
                                        text: choice.modelData.v
                                        color: win.dim
                                        font.pixelSize: win.u * 1.0
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Something that used to happen silently, said out loud. Bottom of
            // the screen so it never covers what it is about.
            Rectangle {
                visible: shell.notice !== ""
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: win.u * 4.5
                width: msg.implicitWidth + win.u * 3
                height: win.u * 2.8
                radius: height / 2
                color: "#e61a1430"
                border.width: 1
                border.color: win.accent

                Text {
                    id: msg
                    anchors.centerIn: parent
                    text: shell.notice
                    color: win.ink
                    font.pixelSize: win.u * 1.1
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
                    // The keyboard's spelling of the X button, so the two
                    // input paths stay one implementation — and so a test can
                    // reach a close, which a controller path cannot be driven
                    // to on a build machine.
                    case Qt.Key_X:
                    case Qt.Key_Delete:   shell.nav("search"); break
                    // The keyboard's spelling of Start — S, for Start. Escape
                    // quits and Backspace is Back, so neither of the obvious
                    // keys was free, and a modifier chord is the wrong shape
                    // for a screen somebody is looking at from a sofa. It was
                    // P until 0.1.0-23, which stood for nothing and was
                    // remembered by nobody.
                    case Qt.Key_S:        shell.nav("menu"); break
                    // Escape QUITS, where Guide steps aside. Somebody at a
                    // keyboard has a way back that somebody on a sofa does
                    // not, so the keyboard keeps the stronger verb.
                    case Qt.Key_Escape:   Qt.quit(); break
                    default: return
                    }
                    event.accepted = true
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // The strip that stays behind: the on-screen keyboard, and the hint
    // ═══════════════════════════════════════════════════════════════════════
    //
    // A SECOND window, and it has to be, for one reason: this one must never
    // take keyboard focus. The whole point of the on-screen keyboard is that
    // the browser underneath keeps focus and receives what wtype types — a
    // surface that grabbed the keyboard to draw a keyboard would be typing
    // into itself.
    //
    // It is also why the keys are pressed with the CONTROLLER and not by
    // clicking: while this is up, `big mouse` is stopped, because A cannot be
    // both a click and a keypress. A real mouse still works — the mask below
    // is the whole window while the keyboard is open — for the afternoon when
    // the television is a desktop again.
    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: kwin

            required property var modelData
            screen: modelData

            readonly property bool chosen: {
                if (!shell.wantOutput) return kwin.modelData === Quickshell.screens[0]
                const named = Quickshell.screens.find(s => s.name === shell.wantOutput)
                return named ? kwin.modelData === named
                             : kwin.modelData === Quickshell.screens[0]
            }

            visible: chosen && shell.away && (shell.oskOpen || shell.hintShown)

            anchors { left: true; right: true; bottom: true }
            implicitHeight: shell.oskOpen ? kb.implicitHeight : hintRow.height + u * 1.2

            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "syn-arcade-big-keys"
            // ⚠ None, not OnDemand. OnDemand takes focus on a click, and a
            // click on this keyboard would move focus off the text field the
            // keys are meant for.
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            exclusionMode: ExclusionMode.Ignore

            // The hint is a label, not a target: with an empty input region it
            // cannot swallow a click meant for the window underneath it. The
            // keyboard is the opposite and takes the whole surface.
            mask: shell.oskOpen ? fullMask : nullMask

            color: "transparent"

            // ⚠ THE SAME UNIT AS THE SHELF ABOVE, and it has to stay that way:
            // this is a SECOND window, so it cannot read win.u, and the two
            // drifting apart puts a keyboard on screen at a different scale
            // from the interface it is typing into. Both clamp the same way —
            // see the note at win.u for why 96 makes 16:9 a no-op.
            readonly property real u: Math.max(12, Math.min(
                (kwin.screen ? kwin.screen.height : 1080) / 54,
                (kwin.screen ? kwin.screen.width  : 1920) / 96))
            readonly property color ink:    "#f2f0fa"
            readonly property color dim:    "#a49cc4"
            readonly property color accent: "#a78bfa"

            // ⚠ The keyboard's mask names the ITEM rather than being an empty
            // Region left to mean "everything". An empty region means the
            // opposite — nothing is clickable — and the two spellings look
            // identical in a diff. The hint's mask really is nothing: it is a
            // label, and a label that swallowed a click meant for the window
            // underneath it would be a small mystery at the bottom of the
            // screen for four seconds.
            Region { id: fullMask; item: kb }
            Region { id: nullMask; width: 0; height: 0 }

            // ── the hint ────────────────────────────────────────────────────
            Rectangle {
                id: hintRow
                visible: !shell.oskOpen
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: kwin.u * 0.6
                width: hintText.implicitWidth + kwin.u * 2
                height: kwin.u * 2.2
                radius: height / 2
                color: "#e60b0916"
                border.width: 1
                border.color: "#453a66"

                Text {
                    id: hintText
                    anchors.centerIn: parent
                    color: kwin.ink
                    font.pixelSize: kwin.u * 0.85
                    text: {
                        const t = "Guide  ▸  big screen"
                        return keysProc.running ? t + "      Start  ▸  keyboard" : t
                    }
                }
            }

            // ── the keyboard ────────────────────────────────────────────────
            Rectangle {
                id: kb
                visible: shell.oskOpen
                anchors.fill: parent
                color: "#f20b0916"

                // Sized from the rows so the window can ask for exactly the
                // height it needs; anchoring the other way round would make
                // the strip and its contents disagree during a layout change
                // and flicker.
                implicitHeight: keyCol.implicitHeight + kwin.u * 1.6

                Column {
                    id: keyCol
                    anchors.centerIn: parent
                    spacing: kwin.u * 0.35

                    Repeater {
                        model: shell.oskRows

                        Row {
                            id: krow
                            required property var modelData
                            required property int index
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: kwin.u * 0.35

                            Repeater {
                                model: krow.modelData

                                Rectangle {
                                    id: cap
                                    required property var modelData
                                    required property int index

                                    readonly property bool here:
                                        shell.oskRow === krow.index
                                        && shell.oskCol === cap.index

                                    width: kwin.u * 2.6 * (cap.modelData.w || 1)
                                    height: kwin.u * 2.4
                                    radius: kwin.u * 0.35
                                    color: cap.here ? "#3a2f5c" : "#1d1830"
                                    border.width: cap.here ? Math.max(2, kwin.u * 0.14) : 1
                                    border.color: cap.here ? kwin.accent : "#3a3159"

                                    Text {
                                        anchors.centerIn: parent
                                        text: cap.modelData.t
                                        color: kwin.ink
                                        font.pixelSize: cap.modelData.w > 1
                                                        ? kwin.u * 0.8 : kwin.u * 1.1
                                        font.bold: true
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            shell.oskRow = krow.index
                                            shell.oskCol = cap.index
                                            shell.oskPress()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // The controls, on the screen they apply to, for the same
                    // reason the main footer exists.
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: kwin.dim
                        font.pixelSize: kwin.u * 0.75
                        text: "A  type      X  backspace      Y  space      "
                              + "LB/RB  layout      B  close      Guide  big screen"
                    }
                }
            }
        }
    }
}
