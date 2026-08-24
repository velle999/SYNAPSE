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

    /*
     * One level DOWN inside a source, or "" for the source's own list.
     *
     * ⚠ ONLY "mine" SO FAR, AND IT EARNS THE STATE ON ITS OWN. Signing a
     * browser in is what puts somebody's own playlists within reach — it is the
     * whole point of `yt login` — and until now the row that holds them was
     * drawn and then refused, so the reward for signing in was a line of grey
     * text. Their playlists are a LIST, not an errand: they cannot be shown by
     * opening a terminal, so the drawer has to be able to go into one and come
     * back.
     */
    property string drill: ""

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
        // ⚠ AND THE DRILL WITH IT. "Your playlists" belongs to YouTube Music;
        // left set across a switch to Plex it would send the next fetch to
        // `yt mine` and draw a YouTube library under the word Plex.
        root.drill = ""
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
        id: itemsJob

        /*
         * ⚠ WHAT THIS FETCH WAS ASKED ABOUT, stamped at LAUNCH.
         *
         * `itemsFor` was written from root.sourceId when the answer ARRIVED,
         * and that is a different question: a switch made while a fetch is in
         * flight changes sourceId under it, so the old source's rows were
         * stamped with the NEW source's name and passed the gate that exists to
         * catch exactly this. Worse, loadItems() bows out while this is running
         * — so the switch fetched nothing of its own and the wrong list was the
         * only list. `yt mine` is a round trip through yt-dlp and takes seconds,
         * which is what made a latent race into a reachable one.
         */
        property string forSource: ""
        property string forDrill: ""

        stdout: StdioCollector {
            onStreamFinished: {
                root.loading = false

                // Asked about something nobody is looking at any more. Dropped
                // rather than drawn — and the fetch that was skipped while this
                // one held the Process is started here, because this is the
                // moment it can run.
                if (itemsJob.forSource !== root.sourceId ||
                    itemsJob.forDrill  !== root.drill) {
                    root.loadItems()
                    return
                }

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
                /*
                 * ⚠ THE WAY BACK IS A ROW, because there is nowhere else to put
                 * one. The drawer is 246px wide with a source strip along the
                 * top and a list under it; a back chip in that strip would sit
                 * among the sources and read as a sixth source. A row at the
                 * head of the list is where the list came from.
                 *
                 * `kind` is "back" rather than "action" on purpose: an action
                 * is an errand this file may or may not be able to run, and
                 * pressable() answers for both from the one place.
                 */
                if (root.drill !== "")
                    out.unshift({ id: "", name: "‹ back", note: "",
                                  kind: "back" })

                root.items = out
                root.itemsFor = itemsJob.forSource

                // ⚠ AN EMPTY DRILL STILL HAS THE BACK ROW IN IT, so the centre
                // message that explains an empty list cannot fire and the
                // status line has to carry it. Nothing coming back from a
                // signed-in session is a real answer worth showing — see
                // yt_mine(), which says the same thing on the television.
                if (root.drill === "mine" && out.length <= 1)
                    root.status = "nothing came back from that YouTube session"
            }
        }
    }

    function loadItems() {
        if (!root.have) return

        /*
         * ⚠ THE DRILL IS ANSWERED FIRST, AND IT IS CHECKED AGAINST THE SOURCE.
         * refreshSources() ends here, so an errand exiting or the picker being
         * reopened re-runs whatever is on screen — which is right, and is only
         * right while the drill still belongs to the source. big.c decides
         * whether this source has a YouTube list at all; if it does not, there
         * is nothing to be one level down inside.
         */
        if (root.drill === "mine") {
            if (root.listVerb(root.sourceAction) !== "yt") {
                root.drill = ""
            } else {
                if (itemsJob.running) return
                root.status = ""
                root.loading = true
                itemsJob.forSource = root.sourceId
                itemsJob.forDrill = root.drill
                itemsJob.command = ["syn-arcade", "big", "music",
                                     "yt", "mine", "--rec"]
                itemsJob.running = true
                return
            }
        }

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
        if (itemsJob.running) return
        root.status = ""
        root.loading = true
        itemsJob.forSource = root.sourceId
        itemsJob.forDrill = root.drill
        itemsJob.command = ["syn-arcade", "big", "music", verb, "--rec"]
        itemsJob.running = true
    }

    /* ── What the track is CALLED ────────────────────────────────────────── */

    /*
     * ⚠ CLIAMP DOES NOT NAME A QUEUED TRACK, AND THE ANSWER IS ALREADY IN C.
     *
     * `cliamp queue <thing>` takes a path and reports that path back as the
     * title — no tags are read — so every song off a YouTube station arrives on
     * MPRIS called `watch`, from the last segment of `…/watch?v=<id>`, with no
     * artist at all. Measured on this machine while a playlist was playing:
     *
     *     xesam:title  "watch"        xesam:url  https://www.youtube.com/watch?v=…
     *
     * which is what put "watch" and "Cliamp" on the card while the playlist
     * itself had loaded perfectly. The list was never the broken half.
     *
     * big.c solved this once, for the television, and the fix is a CACHE rather
     * than a parser: whatever queues a track writes down what it queued
     * (`music-titles.rec`), keyed by music_key(), and `big music status` reads
     * it back. A second copy of that key rule here is the second roster this
     * project keeps being bitten by — see MusicPlayer.qml's header — so the
     * question asked is "what is playing", never "what is this URL called".
     *
     * ⚠ AND IT IS A FORK PER TRACK, NOT PER SECOND. The objection in
     * MusicPlayer.qml's header stands and is not weakened here: MPRIS says,
     * event-driven, when the track CHANGES, and only that edge asks. A track is
     * three minutes long.
     */

    // Written by whoever is drawing a track: the `xesam:url` it wants named.
    // Only for a player that names tracks by path — `big music status` answers
    // about CLIAMP, so asking it about Firefox would name the wrong song.
    property string nowUrl: ""
    onNowUrlChanged: root.resolveName()

    // The answer, and the url it belongs to. Kept as a pair for the reason
    // `itemsFor` exists: an answer that outlives the question it was asked
    // about is an answer drawn against the wrong track.
    property string nowKey: ""
    property string nowTitle: ""

    property Process nameProc: Process {
        id: nameJob

        // ⚠ Stamped at LAUNCH, not read on arrival — the same race itemsProc
        // documents. cliamp advances tracks on its own, so `nowUrl` can move
        // under a fetch that is already out.
        property string forUrl: ""

        stdout: StdioCollector {
            onStreamFinished: {
                if (nameJob.forUrl !== root.nowUrl) {
                    // Asked about a track nobody is looking at any more. The
                    // fetch that was skipped while this one held the Process is
                    // started here, because this is the moment it can run.
                    root.resolveName()
                    return
                }

                const rows = root.records(this.text)
                const r = rows.length > 0 ? rows[0] : null

                // ⚠ RECORDED EVEN WHEN IT IS EMPTY. `status` answering with no
                // title is a real answer — a track this file never queued — and
                // marking the question asked is what stops it being asked again
                // on every repaint. The caller falls back on its own.
                root.nowKey = nameJob.forUrl
                root.nowTitle = r && r.title ? r.title : ""
            }
        }
    }

    function resolveName() {
        if (!root.have || root.nowUrl === "") return
        if (root.nowUrl === root.nowKey) return   // already answered
        if (nameJob.running) return               // the arrival re-fires it
        nameJob.forUrl = root.nowUrl
        nameJob.command = ["syn-arcade", "big", "music", "status", "--rec"]
        nameJob.running = true
    }

    /*
     * What that url is called, or "" where nothing here knows.
     *
     * ⚠ A PURE READ, deliberately: this is called from a BINDING on two
     * surfaces, and a binding that starts a subprocess re-runs it on every
     * repaint. Wanting a name is `nowUrl`; asking for one is this.
     */
    function nameFor(url) {
        return (url !== "" && url === root.nowKey) ? root.nowTitle : ""
    }

    /*
     * A title that is really a path, made into something to draw — the fallback
     * for everything the cache does not know, and the one home for the rule.
     *
     * ⚠ THE QUERY COMES OFF FIRST, AND THAT IS NOT TIDYING. A Plex stream's
     * path carries the account token in its query string, and both callers sit
     * where a screenshot or a shoulder catches it — the wallpaper and the bar.
     * Cut the query, then take the last path segment, then drop a file
     * extension, which is what music_title_fallback() does on the television.
     */
    function displayTitle(raw) {
        let t = String(raw || "").replace(/[\x00-\x1f\x7f]/g, " ")
                                 .replace(/\s+/g, " ").trim()
        if (!t || t.indexOf("/") < 0) return t

        const q = t.indexOf("?")
        if (q >= 0) t = t.slice(0, q)
        t = t.slice(t.lastIndexOf("/") + 1)
        try { t = decodeURIComponent(t) } catch (e) { /* leave it as it came */ }
        const dot = t.lastIndexOf(".")
        // Only a short trailing extension: a track genuinely called
        // "Nine. Point. Five" must not lose its last word.
        if (dot > 0 && t.length - dot <= 5) t = t.slice(0, dot)
        return t.trim()
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

    /*
     * ── the errands ─────────────────────────────────────────────────────────
     *
     * The presses that do not play anything: signing in, installing what a
     * source needs, opening cliamp's own interface. Every one of them ends in
     * big.c's term_run_and_hold(), which opens a terminal and WAITS with what
     * it said still on the screen.
     *
     * ⚠ NOT THE TRANSPORT POOL, and the reason is the pool's own header. Each
     * of these blocks for as long as the terminal is open — minutes, while
     * somebody signs in to Spotify in a browser — so an errand put through
     * actPool would hold one of the three slots for all of it, and three of
     * them would leave every skip silently dropped. One Process, and its
     * `running` IS the guard: there is nothing to be gained by opening a
     * second sign-in terminal over the first.
     *
     * ⚠ AND THE PICKER IS ASKED AGAIN WHEN IT EXITS. Every errand exists to
     * change big.c's answer about this machine — a `[spotify]` section, a
     * yt-dlp on PATH, an OAuth client — and a row that still says "press to
     * sign in" after somebody has signed in is the fix looking like a failure.
     */
    property Process errandProc: Process {
        onExited: {
            root.status = ""
            root.refreshSources()
        }
    }

    function errand(args) {
        if (!root.have) return false
        if (errandProc.running) {
            root.status = "that is already open — look for the terminal"
            return false
        }
        errandProc.command = ["syn-arcade", "big", "music"].concat(args)
        errandProc.running = true
        // A terminal takes a moment to map, and a button that appears to do
        // nothing for a second is the bug being fixed here.
        root.status = "opening a terminal…"
        return true
    }

    /*
     * What the CHOSEN SOURCE needs before it can play anything, as the words to
     * put on a button — or "" where there is nothing to press.
     *
     * ⚠ IT IS KEYED ON THE ACTION AND NAMES NO PACKAGE. Which package YouTube
     * Music is missing is big.c's answer about this machine, it is already on
     * screen as the note above the button, and a second copy of it here is a
     * copy that stops being true — the same rule that keeps the action column
     * in C. The label only has to say what the press DOES.
     */
    readonly property string sourceErrand:
          sourceAction === "setup"   ? "Sign in"
        : sourceAction === "install" ? "Install it"
        : sourceAction === "browse"  ? "Open in cliamp"
        : ""

    function runSourceErrand() {
        if (root.sourceAction === "setup")   return root.errand(["setup"])
        if (root.sourceAction === "install") return root.errand(["install", root.sourceId])
        if (root.sourceAction === "browse")  return root.errand(["browse"])
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
     * ── which rows a press does something to ────────────────────────────────
     *
     * ⚠ ASKED HERE RATHER THAN DECIDED IN THE DELEGATE, for the reason big.c
     * keeps the `action` column: a row is drawn dim when pressing it will not
     * work, and a second opinion about that in the widget is a second thing to
     * keep in step with this file.
     */
    readonly property var errandRows: ["find", "login", "setup", "mine"]

    function pressable(it) {
        if (!it) return false
        if (it.kind !== "action") return true
        return root.errandRows.indexOf(it.id) >= 0
    }

    /*
     * Choose a row: play it, run the errand it stands for, or go back.
     *
     * ── ⚠ THE REFUSAL THAT USED TO LIVE HERE, AND WHY IT WAS WRONG ──────────
     *
     * This said that an `action` row must not be pressed because `big music yt
     * find` and `... yt login` READ FROM STDIN, and from a widget that is a
     * process with no stdin at all, sat waiting for a line that can never
     * arrive. That was true when it was written and it stopped being true: both
     * of those verbs now ask can_be_asked() FIRST and, with no terminal to be
     * read from, re-run themselves inside one — which is the same mechanism
     * that puts the on-screen keyboard in front of them on the television.
     *
     * So the whole YouTube list was inert. Signed in through Firefox, the
     * drawer drew Search…, Your playlists and Search inside cliamp… and refused
     * all three, which is exactly the shape of the bug the widget was built to
     * end: a picker that answers a press with a sentence. Every row here now
     * does the thing it is named after.
     *
     * ⚠ AND A ROW THIS FILE CANNOT RUN IS STILL DRAWN. Which errands exist is
     * big.c's answer about this machine — signed in or not, OAuth client or not
     * — and the header there is explicit that a copy of that reasoning in QML
     * is a copy that stops being true. So the list stays the television's list,
     * and anything with an id nobody here knows says what it needs rather than
     * doing nothing.
     */
    function chooseItem(it) {
        if (!it) return false

        if (it.kind === "back") {
            root.drill = ""
            root.status = ""
            // Cleared BEFORE the fetch, the same rule setSource() follows: the
            // playlists must not sit under the stations list, clickable, for
            // however long the round trip takes.
            root.items = []
            root.loadItems()
            return true
        }

        if (it.kind === "action") {
            // Their own playlists are a LIST and the only row here that is:
            // one level further into the same drawer rather than a terminal.
            if (it.id === "mine") {
                root.drill = "mine"
                root.items = []
                root.status = ""
                root.loadItems()
                return true
            }
            // `setup` is cliamp's own wizard, and it is the same command
            // whether it was reached from this row (a Google OAuth client for
            // searching inside cliamp) or from the Spotify source. big.c owns
            // which one it configures.
            if (it.id === "setup")
                return root.errand(["setup"])
            if (it.id === "find" || it.id === "login")
                return root.errand(["yt", it.id])

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
