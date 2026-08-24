import QtQuick
import Quickshell
import Quickshell.Services.Mpris
import "../components"
import ".."

/*
 * Now playing (MPRIS).
 *
 * Event-driven off the Mpris service — no polling, no shelling out to
 * playerctl. Hidden entirely when nothing is playing, the same way Battery
 * hides itself on a desktop: an empty "now playing" slot is just noise.
 *
 * Track metadata is third-party text — a title comes from a file's tags or a
 * stream's server — so it goes through the same trimming every other untrusted
 * string in this desktop gets before it is drawn.
 */
BarModule {
    id: root

    // The first player that is actually playing wins; failing that, the first
    // one that exists, so a paused player still offers its controls.
    readonly property var player: {
        const ps = Mpris.players ? Mpris.players.values : []
        if (!ps || ps.length === 0) return null
        for (const p of ps) if (p.isPlaying) return p
        return ps[0]
    }

    readonly property bool playing: player ? player.isPlaying : false

    moduleVisible: player !== null

    icon: playing ? Icons.mediaPlay : Icons.mediaPause
    iconColor: playing ? root.pal.glyph : root.pal.dim

    // A 25-char cap rather than an elide: this sits in a row of fixed-width
    // readouts, and a title that grows the bar shifts every module beside it
    // every time the track changes.
    /*
     * ── A TITLE THAT IS REALLY A PATH ────────────────────────────────────
     *
     * ⚠ THE BAR DREW `xesam:title` RAW, and for cliamp that is not a title at
     * all: it queues by path and reports the path back, so a local file put an
     * absolute path in the bar, a Plex stream put THE ACCOUNT TOKEN there, and
     * a YouTube station put the word `watch` — the last segment of
     * `…/watch?v=<id>` — on every song in the playlist. The card next to it had
     * the same bug and the same report.
     *
     * MusicLibrary owns both halves of the answer: the titles cache
     * syn-arcade writes when it queues (nameFor), and the path reduction that
     * strips a query before anything is drawn (displayTitle). One home, because
     * a bar and a card that disagree about a song's name is two bugs.
     */
    readonly property string trackUrl:
        player && player.metadata
            ? String(player.metadata["xesam:url"] || "") : ""

    // Only cliamp is asked: `big music status` answers about cliamp alone.
    readonly property string wantNamed:
        (player && String(player.dbusName) === "org.mpris.MediaPlayer2.cliamp"
         && MusicLibrary.have) ? trackUrl : ""
    onWantNamedChanged: if (wantNamed !== "") MusicLibrary.nowUrl = wantNamed

    readonly property string trackName: {
        if (!player) return ""
        const named = MusicLibrary.nameFor(root.wantNamed)
        return named ? named : MusicLibrary.displayTitle(player.trackTitle)
    }

    text: {
        if (!player) return ""
        const title = root.trackName
        const artist = clean(player.trackArtist)
        if (!title) return "playing"
        const s = artist ? artist + " — " + title : title
        return s.length > 25 ? s.slice(0, 24) + "…" : s
    }

    tooltipText: {
        if (!player) return ""
        const title = root.trackName || "(unknown track)"
        const artist = clean(player.trackArtist)
        let s = title
        if (artist) s += "\n" + artist
        if (player.identity) s += "\n" + clean(player.identity)
        return s + "\nClick play/pause · scroll to change track"
    }

    // Strip control characters and collapse whitespace. Cairo is not in this
    // path (Qt renders the bar), so a bad byte will not poison a context the
    // way it does in synui's own panels — but a title carrying a newline or a
    // stray control code still wrecks the row it is drawn in.
    function clean(s) {
        if (!s) return ""
        return String(s).replace(/[\x00-\x1f\x7f]/g, " ").replace(/\s+/g, " ").trim()
    }

    onClicked: if (player && player.canTogglePlaying) player.togglePlaying()

    // Scroll for tracks: the same gesture as the volume module next door, and
    // it cannot be mis-hit into destroying anything.
    onScrolled: (delta) => {
        if (!player) return
        if (delta > 0) { if (player.canGoNext)     player.next() }
        else           { if (player.canGoPrevious) player.previous() }
    }
}
