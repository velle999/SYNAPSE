pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * MusicLibrary — the SOURCE and the LIBRARY behind the music widget.
 *
 * ⚠ THIS IS `big music`, AND THAT IS THE POINT. The widget shipped as `big
 * transport` alone — MPRIS, three buttons, a title — and velle's report of it
 * was exact: what had been built was a remote rather than a music player, with
 * no way to choose a source and no way to start anything, which is the whole
 * of what a music widget is for. A transport can only act on music somebody
 * else started. This is the half that STARTS it, and
 * it is deliberately the same interface the television drives so the two cannot
 * grow different libraries: `syn-arcade big music`, which owns the source
 * picker, the Plex albums, the YouTube stations and the queue.
 *
 * MPRIS stays where it was. The two answer different questions and the widget
 * uses both — see MusicPlayer.qml's `driveCliamp`.
 *
 *
 * WHY SHELLING OUT IS RIGHT HERE, HAVING BEEN WRONG FOR THE TRANSPORT
 *
 * The transport must not shell out: this process already has a live,
 * event-driven MPRIS binding, so a subprocess per second would cost a fork to
 * learn something Qt is already being told. None of that applies to a library.
 * There is no D-Bus interface for "what Plex albums are there" — the answer
 * lives in big.c, behind an HTTP call to a media server and a config file this
 * process cannot read — and it is fetched when somebody opens the picker, not
 * once a second.
 *
 * ⚠ AND SYN-ARCADE IS NOT A DEPENDENCY OF THE SHELL. That objection was real
 * and it is answered rather than ignored: `have` is probed once, everything
 * here no-ops without it, and MusicPlayer.qml draws no picker at all on a
 * machine that has not got it. A Minimal install keeps exactly the widget it
 * had before. The PKGBUILD carries syn-arcade as an OPTDEPEND for this reason.
 *
 *
 * THE RECORD FORMAT
 *
 * `--rec` is TAB-separated with percent-encoded fields and a header row, which
 * is util.c's rec_row() and is the same text big screen's own shell parses.
 * Decoding is one function here (`dec`) rather than at each call site, for the
 * reason rec_row() encodes in one place: a column added by somebody who did not
 * know the rule cannot arrive raw.
 */
QtObject {
    id: root

    /* ── Is there anything to drive ──────────────────────────────────────── */

    // Probed once. `command -v` and not an exit status: see
    // reference_exit_status_is_not_did_it_launch — `a || b` is not "if a isn't
    // installed", and asking syn-arcade itself would run it.
    property bool have: false
    property Process haveProbe: Process {
        running: true
        command: ["sh", "-c",
                  "command -v syn-arcade >/dev/null 2>&1 && echo yes"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.have = this.text.trim() === "yes"
                if (root.have) root.refreshSources()
            }
        }
    }

    /* ── State ───────────────────────────────────────────────────────────── */

    /*
     * The sources, as `big music source --rec` gives them:
     *   id  name  current  action  note
     *
     * `action` is what CHOOSING the row does, and it is big.c's judgement
     * rather than this file's — "albums" for Plex, "yt" for YouTube Music,
     * "play" for the queueable ones, and "browse"/"install"/"setup" for the
     * three that cannot play from here. Reading it rather than re-deciding is
     * what keeps the desktop and the television offering the same thing.
     */
    property var sources: []

    // The chosen one, split out because every binding wants a field of it and
    // `sources.find(...)` in a binding re-runs the search on every repaint.
    property string sourceId: ""
    property string sourceName: ""
    property string sourceAction: ""
    property string sourceNote: ""

    /*
     * What is IN the chosen source: { id, name, note }.
     *
     * ⚠ Kept with the id of the source it came from. Without that the list
     * outlives the switch, so choosing YouTube Music showed Plex's albums until
     * the fetch came back — and a press in that window would have played an
     * album off a source that was no longer selected.
     */
    property var items: []
    property string itemsFor: ""
    property bool loading: false

    // One line under the list when something cannot be done and the reason is
    // not visible. A picker that answers a press with silence is the thing
    // big.c's `note` column exists to prevent, and it would be a waste to have
    // fetched the note and then not shown it.
    property string status: ""

    /* ── Records ─────────────────────────────────────────────────────────── */

    function dec(s) {
        try { return decodeURIComponent(String(s)) }
        // A malformed escape is corrupt input from a media server, not a reason
        // to lose the row: show it as it came rather than throwing out of the
        // parse and leaving the whole list empty.
        catch (e) { return String(s) }
    }

    // TSV with a header row → array of objects keyed by that header. Returns []
    // for empty output, which is also what a command that failed produces —
    // deliberately the same, because "no albums" and "the server is down" both
    // mean there is nothing to draw and the status line is what tells them
    // apart.
    function records(text) {
        const lines = String(text || "").split("\n").filter(l => l !== "")
        if (lines.length < 2) return []

        const head = lines[0].split("\t").map(root.dec)
        const out = []
        for (let i = 1; i < lines.length; i++) {
            const f = lines[i].split("\t")
            const rec = {}
            for (let c = 0; c < head.length; c++)
                rec[head[c]] = root.dec(f[c] === undefined ? "" : f[c])
            out.push(rec)
        }
        return out
    }

    /* ── The sources ─────────────────────────────────────────────────────── */

    property Process sourcesProc: Process {
        command: ["syn-arcade", "big", "music", "source", "--rec"]
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = root.records(this.text)
                root.sources = rows

                let cur = null
                for (const r of rows) if (r.current === "1") { cur = r; break }
                // No `current` at all means big.c fell back to its default and
                // said so on stderr; take the first row rather than leaving the
                // chip blank, which reads as a widget that failed to load.
                if (!cur && rows.length > 0) cur = rows[0]

                root.sourceId     = cur ? cur.id     : ""
                root.sourceName   = cur ? cur.name   : ""
                root.sourceAction = cur ? cur.action : ""
                root.sourceNote   = cur ? cur.note   : ""
                root.loadItems()
            }
        }
    }

    function refreshSources() {
        if (!root.have) return
        // Sequential re-use of one Process is fine and is NOT the shared-object
        // trap: that bites when jobs OVERLAP. This one is driven by a click and
        // by the probe, and the guard is what makes the claim true.
        if (sourcesProc.running) return
        sourcesProc.running = true
    }

    /* ── Choosing one ────────────────────────────────────────────────────── */

    /*
     * ⚠ SWITCHING SOURCE RESTARTS THE PLAYER. `--provider` is a cliamp START-UP
     * flag, so big.c can only honour a new source by bringing cliamp back up
     * with it — which means the music stops. That is the mechanism and there is
     * no gentler one; it is worth knowing before it is reported as the picker
     * killing the music.
     */
    property Process setProc: Process {
        stdout: StdioCollector { onStreamFinished: root.refreshSources() }
    }

    function setSource(id) {
        if (!root.have || !id || setProc.running) return
        // Cleared BEFORE the switch, not after: the fetch is a round trip to a
        // media server and the old source's albums must not sit under the new
        // source's name in the meantime, clickable.
        root.items = []
        root.itemsFor = ""
        root.status = ""
        setProc.command = ["syn-arcade", "big", "music", "source", String(id)]
        setProc.running = true
    }

    /* ── What is in it ───────────────────────────────────────────────────── */

    // Which `big music` verb lists the chosen source, or "" for a source that
    // has no list to show. Keyed on the ACTION column, so a source added to
    // big.c arrives here with its behaviour already decided.
    function listVerb(action) {
        if (action === "albums") return "plex"
        if (action === "yt")     return "yt"
        return ""
    }

    property Process itemsProc: Process {
        stdout: StdioCollector {
            onStreamFinished: {
                root.loading = false
                const rows = root.records(this.text)

                /*
                 * Plex answers `key title artist year` and YouTube Music
                 * answers `id name note kind`. Normalised to one shape here
                 * rather than in the delegate: a list that has to know which
                 * source it is drawing is a list with two layouts to keep in
                 * step, and the widget's row is the same row either way.
                 */
                const out = []
                for (const r of rows) {
                    const id = r.id !== undefined ? r.id : r.key
                    if (!id) continue
                    const name = r.name !== undefined ? r.name : r.title
                    let note = r.note !== undefined ? r.note : r.artist
                    if (r.year) note = note ? note + " · " + r.year : r.year
                    /*
                     * ⚠ `kind` IS CARRIED, and big.c's own comment says why in
                     * as many words: "kind is what the shell dispatches on".
                     * A YouTube list is not all stations — Search…, Sign in…
                     * and Search inside cliamp… come back in the same list, and
                     * whether each exists is a fact about the machine that the
                     * C side works out. Dropping the column would leave the
                     * widget unable to tell a playlist from an errand.
                     * Plex answers no `kind` at all; those rows are all tracks.
                     */
                    out.push({ id: id, name: name || id, note: note || "",
                               kind: r.kind !== undefined ? r.kind : "item" })
                }
                root.items = out
                root.itemsFor = root.sourceId
            }
        }
    }

    function loadItems() {
        if (!root.have) return
        const verb = root.listVerb(root.sourceAction)
        if (verb === "") {
            // Nothing to browse. Say WHY, from the note big.c already worked
            // out for this machine — "needs yt-dlp", "not set up", "no music
            // folder on this machine" — and otherwise say what the play button
            // will do, so the empty list never reads as a failed fetch.
            root.items = []
            root.itemsFor = root.sourceId
            root.status = root.sourceNote !== "" ? root.sourceNote
                        : root.sourceAction === "play"
                          ? "press play to start this source"
                          : "this source opens in cliamp"
            return
        }
        if (itemsProc.running) return
        root.status = ""
        root.loading = true
        itemsProc.command = ["syn-arcade", "big", "music", verb, "--rec"]
        itemsProc.running = true
    }

    /* ── Making noise ────────────────────────────────────────────────────── */

    /*
     * A POOL, and three of it.
     *
     * A press here is short-lived, so one object re-used sequentially would
     * almost always be free — and "almost always" is exactly the shape of the
     * bug this pool exists for: setting `running = true` on a Process that is
     * already running is a SILENT no-op, every statement around it still runs,
     * and the widget goes on describing a track it never asked for. Three
     * covers a double-tapped skip landing on a slow `play`.
     */
    readonly property list<Process> actPool: [
        Process {}, Process {}, Process {}
    ]

    function run(args) {
        if (!root.have) return false
        for (const p of root.actPool) {
            if (p.running) continue
            p.command = ["syn-arcade", "big", "music"].concat(args)
            p.running = true
            return true
        }
        // ⚠ A full pool SAYS SO. Dropping the press silently is how the
        // original bug read from the outside.
        root.status = "still working on the last one"
        return false
    }

    // Start the chosen source from nothing. This is the verb that answers "no
    // way to play music through the widget": `big music play` fills an empty
    // queue from whatever `--provider` is set, rather than trying to resume a
    // player that has nothing to resume.
    function play() { return root.run(["play"]) }

    // Play one thing off the list. `plex <key>` and `yt <id>` are the same
    // shape on purpose — big.c's own comment says a search result's id IS its
    // URL so the shell plays one with exactly the station command.
    function playItem(id) {
        const verb = root.listVerb(root.sourceAction)
        if (verb === "" || !id) return false
        return root.run([verb, String(id)])
    }

    /*
     * Choose a row: play it, or say why it cannot be played HERE.
     *
     * ⚠ AN `action` ROW MUST NOT BE PLAYED, AND THE REASON IS NOT COSMETIC.
     * `big music yt find` and `... yt login` READ FROM STDIN — on the
     * television that is a terminal with the on-screen keyboard pointed at it,
     * and from a widget it is a process with no stdin at all, sat waiting for a
     * line that can never arrive. Nothing would play and nothing would say so.
     *
     * The rows are still SHOWN. Which of them exist is big.c's answer about
     * this machine (signed in or not, OAuth client or not), and its header is
     * explicit that a copy of that reasoning in QML is a copy that stops being
     * true. So the list is exactly the television's list, the errands are drawn
     * as errands, and pressing one says what it needs — which is the note the
     * C side already wrote for that purpose.
     */
    function chooseItem(it) {
        if (!it) return false
        if (it.kind === "action") {
            root.status = it.note !== ""
                        ? it.note : "that one has to be done on the television"
            return false
        }
        return root.playItem(it.id)
    }

    // The transport, for when cliamp is the player. MPRIS is the other path and
    // MusicPlayer.qml chooses between them.
    function control(verb) { return root.run([verb]) }
}
