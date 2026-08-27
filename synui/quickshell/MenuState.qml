pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * MenuState — whether the start menu is open, and which monitor it is on.
 *
 * Same split as OsdState: the state is a singleton and the WINDOW
 * (StartMenu.qml) is per-screen, because the menu is one logical thing drawn on
 * whichever monitor it was summoned from. Three monitors must not mean three
 * menus with three independent selections.
 *
 * THE OUTPUT IS PASSED IN, not probed. Both things that open this menu already
 * know the answer: a click knows which bar was clicked, and the Super tap comes
 * from synui, which is the only process that knows what has focus — so its IPC
 * call names the output (see input.c's start_menu action). The probe below is a
 * fallback for a caller that names nothing; relying on it for the normal path
 * would flash the menu onto the previously-focused monitor for however long the
 * round trip takes, which on a menu (unlike the OSD) is the thing you are
 * looking straight at.
 */
QtObject {
    id: root

    property bool   open:   false
    property string output: ""

    // The search text and the page live here rather than in the window so that
    // opening on a different monitor does not resurrect the last search.
    property string search: ""
    property string page:   ""      // "" = root, else a page id

    function show(outputName) {
        if (outputName) root.output = outputName
        else if (!root.output) outputProbe.running = true
        root.search = ""
        root.page   = ""
        root.open   = true
    }

    function close() { root.open = false }

    function toggle(outputName) {
        // Re-summoning on a DIFFERENT monitor moves the menu there rather than
        // closing it — the alternative is a menu that vanishes when you press
        // Super on the screen you are actually looking at.
        if (root.open && (!outputName || outputName === root.output)) root.close()
        else root.show(outputName)
    }

    // Fallback only. synui is the only thing that knows which output has focus;
    // there is no Wayland protocol that tells a layer-shell client.
    property Process outputProbe: Process {
        command: ["synctl", "outputs"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    for (const o of JSON.parse(this.text))
                        if (o.focused) { root.output = o.name; return }
                } catch (e) { /* keep whatever we had */ }
            }
        }
    }

    // ── Entries the menu does not list ───────────────────────────────────
    //
    // Two files, both `key = one .desktop id per line`, `#` comments, and a
    // leading `!` to UN-hide:
    //
    //   /usr/share/synui/menu-hidden.conf     shipped, curated
    //   ~/.config/synui/menu-hidden.conf      the user's, read second so it wins
    //
    // The shipped list is data and not a table in StartMenu.qml on purpose: it
    // is a judgement about which packages install diagnostic satellites nobody
    // opens from a start menu (avahi's demo browsers, hwloc's lstopo, the JDK's
    // jconsole), and a judgement is exactly the thing a user has to be able to
    // overrule without editing QML. `!lstopo.desktop` in the home file puts it
    // back.
    //
    // This is a different mechanism from the pattern rules in StartMenu.qml's
    // isNoise(): those describe SHAPES of non-application (an uninstaller, a
    // shortcut to a readme) and cannot be enumerated, because every Wine prefix
    // invents new ones. This file enumerates specific known entries.
    //
    // The id is the DesktopEntries id: the filename WITHOUT its `.desktop`
    // extension (quickshell derives it with completeBaseName), and with a
    // nested path flattened by '-' the way Wine's tree comes through — e.g.
    // `lstopo`, or `wine-Programs-Maxis-The Sims-Uninstall The Sims`.
    //
    // A trailing `.desktop` is stripped on the way in anyway. Writing the
    // filename is what anyone who has just run `ls /usr/share/applications`
    // will do, and a hidden-entry file that silently ignores the line you
    // wrote is worse than no file at all.
    property var hiddenIds: ({})

    function rebuildHidden() {
        const out = {}
        for (const text of [sysHidden.text(), userHidden.text()]) {
            for (let line of (text || "").split("\n")) {
                line = line.trim()
                if (line === "" || line.startsWith("#")) continue

                // A later file un-hides with `!id`; deleting the key rather
                // than storing false keeps the lookup a plain truthiness test.
                const off = line.startsWith("!")
                let id = (off ? line.slice(1) : line).trim()
                if (id.endsWith(".desktop")) id = id.slice(0, -8)
                if (id === "") continue

                if (off) delete out[id]
                else     out[id] = true
            }
        }
        root.hiddenIds = out
    }

    // Both follow Theme.qml's paletteFile pattern exactly: setting `path` is
    // what starts the read, and `printErrors: false` because ABSENT is the
    // normal case for both of them — the shipped file on an install that
    // predates it, the home file on every box where nobody has hidden
    // anything. A WARN per bar start for an expected miss is how a log becomes
    // something nobody reads.
    property FileView sysHidden: FileView {
        path: "/usr/share/synui/menu-hidden.conf"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.rebuildHidden()
        onLoadFailed: root.rebuildHidden()
    }

    property FileView userHidden: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/menu-hidden.conf"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.rebuildHidden()
        onLoadFailed: root.rebuildHidden()
    }

    // Is synpkg installed? "Software Manager" is omitted without it, and
    // "Update System" falls back to a raw `pacman -Syu`.
    //
    // WHY THIS EXISTS AT ALL. synui pkgrel 317 pointed both rows at synpkg the
    // release synpkg landed, but a component that is not yet installed could
    // never arrive through `syn-update apply` — so an updated system got a
    // "Software Manager" that silently did nothing (a failed exec on the argv
    // path is not reported anywhere) and an "Update System" that opened a
    // terminal reading `synpkg: command not found`. syn-update installs new
    // components now, but a menu row must not be a dead click even when
    // delivery goes wrong again.
    //
    // This mattered more when shelly was the fallback; it matters MORE now that
    // shelly is gone, because there is no second package manager to land on.
    //
    // `pacman -Qq`, with the answer read off STDOUT rather than the exit status:
    // Process's exited(int, QProcess::ExitStatus) cannot be given a typed
    // handler from QML, so the exit code is not reachable here — the same wall
    // synpkg.qml's actProc hit. Empty stdout means not installed.
    //
    // Unlike outputProbe this runs unconditionally at startup, because the
    // answer is wanted before the first click rather than in response to one.
    property bool synpkgPresent: false
    property Process synpkgProbe: Process {
        running: true
        command: ["pacman", "-Qq", "synpkg"]
        stdout: StdioCollector {
            onStreamFinished: root.synpkgPresent = this.text.trim() === "synpkg"
        }
    }

    // ── What a search could INSTALL ──────────────────────────────────────
    //
    // Type a name nothing on this machine answers to and the menu goes on to
    // offer the packages that would provide it — the launcher behaviour krunner
    // has, and the reason is the same one that put a search box here at all: a
    // person who types "inkscape" and gets an empty list has been told nothing.
    // "It is not installed, and here is what to install" is an answer.
    //
    // The lookup is `synpkg provides`, which is ranked, capped and repository-
    // local precisely so it can be asked while somebody types (see
    // synpkg/src/provides.c). Everything wider than that — the AUR, Flathub —
    // is a network round trip and lives behind the "search every source" row
    // the menu draws last, not behind a keystroke.
    //
    // THE STATE IS HERE RATHER THAN IN THE WINDOW for the same reason `search`
    // and `page` are: StartMenu.qml is instantiated once per monitor, and three
    // monitors must not mean three synpkg processes answering the same
    // question. The visible window asks (wantPackages) and every window reads
    // the one answer.
    property string pkgTerm:    ""   // the term pkgResults answers
    property var    pkgResults: []   // [{ name, version, repo, desc, match }]

    // What has been asked for and not yet answered. Also the debounce's
    // subject: the timer restarts on every keystroke, so a term is only ever
    // looked up once typing has stopped.
    property string pkgWanted:  ""
    // The term the running lookup was started for. On root rather than on the
    // Process because a `property Process` exposes the DECLARED type: an ad-hoc
    // property added inside the object is invisible through the handle — QML
    // resolves it to undefined at run time, and the linter says so.
    //
    // (⚠ that sentence cannot begin with the linter's name: a comment line
    // starting "// qmllint " is parsed as a lint DIRECTIVE, and every word of
    // it comes back as an unknown category.)
    property string pkgRunning: ""

    function wantPackages(term) {
        if (term === root.pkgWanted) return
        root.pkgWanted = term
        // Drop an answer to a DIFFERENT question the moment the question
        // changes. Leaving it would put the packages for "gim" under a search
        // now reading "gimpp" — a list that is wrong rather than merely late,
        // and the menu has no way to say which term a row came from.
        if (term !== root.pkgTerm) {
            root.pkgTerm    = ""
            root.pkgResults = []
        }
        pkgDebounce.restart()
    }

    function forgetPackages() {
        root.pkgWanted  = ""
        root.pkgTerm    = ""
        root.pkgResults = []
        pkgDebounce.stop()
    }

    property Timer pkgDebounce: Timer {
        // Long enough that a word typed at speed costs ONE lookup, short enough
        // that stopping to look at the list does not feel like waiting for it.
        // `provides` itself takes ~0.4s on a warm database, nearly all of it
        // loading ALPM, so the two together are about half a second after the
        // last keystroke.
        interval: 300
        onTriggered: {
            if (root.pkgWanted === "" || !root.synpkgPresent) return
            root.pkgRunning = root.pkgWanted
            // ⚠ Assigning `command` to a RUNNING Process queues it rather than
            // replacing it, so a still-running lookup for an older term has to
            // be stopped first — otherwise the stale one finishes, the queued
            // one starts, and the results arrive in the wrong order.
            root.pkgProbe.running = false
            root.pkgProbe.command = ["synpkg", "--tsv", "provides",
                                     root.pkgWanted, "--limit", "6"]
            root.pkgProbe.running = true
        }
    }

    property Process pkgProbe: Process {
        // No `command` here: it is written by the debounce, which is also the
        // only thing that starts this. Compared against pkgWanted on the way
        // out — a lookup that lands after the search has moved on is an answer
        // to a question nobody is asking any more.
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                const lines = this.text.split("\n")

                // BY HEADER NAME, never by position — the rule every synpkg
                // consumer follows, because a column added in the middle of a
                // TSV shape is otherwise a silent re-labelling of every field
                // after it. `provides` carries a seventh column the other
                // package listings do not (`match`), which is exactly the kind
                // of difference that breaks positional parsing.
                const head = (lines[0] || "").split("\t")
                const col = {}
                for (let i = 0; i < head.length; i++) col[head[i]] = i
                if (col.name === undefined) return   // not a listing at all

                for (let i = 1; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < head.length) continue
                    // Already on the machine. The menu is a place to LAUNCH
                    // things, and offering to install what is installed is a
                    // lie — a package with no .desktop file simply has nothing
                    // to show here, which is the same answer as before.
                    if (f[col.installed] === "1") continue
                    out.push({
                        name:    f[col.name],
                        version: f[col.version],
                        repo:    f[col.repo],
                        desc:    f[col.description],
                        match:   f[col.match]
                    })
                }

                if (root.pkgRunning !== root.pkgWanted) return
                root.pkgTerm    = root.pkgRunning
                root.pkgResults = out
            }
        }
    }
}
