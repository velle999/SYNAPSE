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
    // ORDER of the shelves is this file's business, and it is: what somebody
    // who just turned the television on wants, then the library, then the
    // things that are not games, then the machine's own switches — which are
    // last so they cannot be hit on the way to something else — and then the
    // news, which is the one shelf nobody is navigating TO.
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

        const play = shell.byShelf("play")
        if (play.length) out.push({ title: "Play", kind: "app", items: play })

        if (shell.games.length)
            out.push({ title: "Games", kind: "game", items: shell.games })

        // Installed media applications first, then whatever answered on the
        // network — a Plex client on this machine is a better tile than the
        // same server's web page, and both being here is the point.
        const media = shell.byShelf("media").concat(shell.media)
        if (media.length) out.push({ title: "Media", kind: "app", items: media })

        const apps = shell.byShelf("apps")
        if (apps.length) out.push({ title: "Apps", kind: "app", items: apps })

        const sys = shell.byShelf("system")
        if (sys.length) out.push({ title: "System", kind: "action", items: sys })

        if (shell.news.length)
            out.push({ title: "News", kind: "news", items: shell.news })

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

    onShelvesChanged: {
        const n = shell.shelves.length
        if (!n) return

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
    }

    function moveRow(d) {
        const n = shell.shelves.length
        if (!n) return
        const next = shell.row + d
        if (next < 0 || next >= n) return   // no wrap: the ends are a landmark
        shell.row = next
        shell.rowTitle = shell.shelves[next].title
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

        // "Desktop" is the way out and the only thing here that really quits:
        // closing this IS going back to the desktop, which was there
        // underneath all along.
        if (it.id === "desktop") {
            Qt.quit()
            return
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
            return
        }

        // Already running: this is a way BACK to it, not a second copy.
        if (shell.activeApp && shell.activeApp.id === it.id) {
            shell.stepAside()
            return
        }

        shell.launchApp(it, ["big", "run", it.id, "--wait"])
    }

    Process { id: actionProc }

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

        switch (cmd) {
        case "up":         shell.moveRow(-1); break
        case "down":       shell.moveRow(1); break
        case "left":       shell.moveCol(-1); break
        case "right":      shell.moveCol(1); break
        case "page-left":  shell.moveCol(-6); break
        case "page-right": shell.moveCol(6); break
        case "accept":     shell.activate(); break
        // Back goes UP a shelf, and from the top one it steps aside. A button
        // that does nothing at the top of the screen is a button somebody
        // presses three times before reaching for the keyboard they left on
        // the table.
        case "back":       if (shell.row > 0) shell.moveRow(-1); else shell.stepAside(); break
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
                    width: parent.width * 0.7
                    spacing: win.u * 0.3

                    Text {
                        width: parent.width
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
                                readonly property bool portrait:
                                    shelf.modelData.kind === "game"
                                readonly property real idealW:
                                    portrait ? win.u * 9
                                             : shelf.modelData.kind === "news" ? win.u * 14
                                                                               : win.u * 11
                                readonly property real peek: 0.5
                                readonly property real content:
                                    width - leftMargin - rightMargin

                                readonly property int slots: Math.max(1,
                                    Math.round((content + spacing) / (idealW + spacing)
                                               - peek))

                                // ⚠ CLAMPED, because rounding to ONE tile on a
                                // narrow screen would otherwise stretch that tile
                                // to the full width. Past the clamp the peek is
                                // wrong by a few percent, which is invisible; an
                                // eighty-percent-wide cover is not.
                                readonly property real slotW: {
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
                                    readonly property bool headline:
                                        shelf.modelData.kind === "news"

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
                                        // A headline never has art and is
                                        // always this.
                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: win.u * 0.7
                                            spacing: win.u * 0.3
                                            visible: !tile.modelData.art

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
                                            shell.setRow(shelf.index)
                                            shell.setCol(shelf.index, tile.index)
                                        }
                                        onClicked: {
                                            shell.setRow(shelf.index)
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
