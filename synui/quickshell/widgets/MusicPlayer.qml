import QtQuick
import Quickshell.Services.Mpris
import ".."

/*
 * The music widget — what is playing, and the three buttons that act on it.
 *
 * ⚠ THIS IS THE BIG SCREEN'S `big transport`, NOT ITS `big music`, AND THE
 * DIFFERENCE IS WHOSE MUSIC IT IS. syn-arcade has both, and its own header
 * spells out why the television grew the second one: `big music` drives cliamp
 * — one player, over its own socket, with a queue and a source picker — which
 * is a great deal of machinery for one program and worth nothing the moment
 * somebody is listening to Spotify, watching a film in mpv, or playing a video
 * in a browser tab. A play/pause button on the desktop has to work on whatever
 * is making the noise or it is a button that works on Tuesdays.
 *
 * So this is MPRIS2, like the transport on the television and like the bar's
 * own Media module — and the two rules the television learned the hard way are
 * ported here rather than rediscovered:
 *
 *   1. WHICH PLAYER, when there is more than one. Whatever is playing wins;
 *      failing that whatever is paused, which is the player somebody most
 *      likely wants to start again; cliamp breaks a tie because it is the one
 *      SynapseOS starts itself.
 *
 *   2. A TITLE THAT IS REALLY A PATH. cliamp publishes the file path as
 *      `xesam:title`, and for a Plex stream that path is a URL WITH THE ACCOUNT
 *      TOKEN IN ITS QUERY. Drawing it raw would put a credential on the desktop
 *      wallpaper. Nothing that reaches the label below has a query on it.
 *
 * ⚠ AND IT DOES NOT SHELL OUT TO `syn-arcade big transport`. That command is
 * the same interface reached through `busctl`, which is right for a C program
 * that must not link libsystemd — and wrong here, because this file is already
 * inside a process with a live, event-driven MPRIS binding. Polling a
 * subprocess once a second to learn something the process is already being told
 * would cost a fork per second and still be a second behind. syn-arcade is also
 * not a dependency of the shell, and a widget that needs a package the desktop
 * does not require is a widget that is blank on some installs.
 *
 * `interactive`, with the price QuickLaunch, PostIt, Pizza and the pet already
 * pay: while this is on, clicks on the card go to it rather than to the desktop
 * beneath, so the desktop right-click menu is unreachable there. Unavoidable
 * for anything meant to be clicked.
 */
WidgetFrame {
    id: root

    widgetId: "music"
    shown: WidgetState.music
    label: "NOW PLAYING"
    accent: Theme.magenta
    interactive: true

    // Title, artist and the time readout are body text on a card that at a low
    // glass level is not there — the note's case exactly.
    inkOnBackdrop: true

    homeEdgeH: "left"; homeEdgeV: "bottom"
    homeMarginX: 20
    // Clear of the visualiser when both are on, the same courtesy BigClock pays.
    homeMarginY: WidgetState.visualizer ? 124 : 24

    cardWidth: 268
    bodyHeight: 96

    /*
     * Which player the buttons act on. `big transport`'s rule, scored the same
     * way: state first, then cliamp as the tie-break.
     *
     * ⚠ A STOPPED PLAYER IS STILL A PLAYER and is returned. A widget that said
     * "nothing playing" for a player sitting at the start of a track would be
     * lying about the machine — and its play button is exactly the button
     * somebody is reaching for.
     */
    readonly property var player: {
        const ps = Mpris.players ? Mpris.players.values : []
        if (!ps || ps.length === 0) return null

        let best = null, bestRank = -1
        for (const p of ps) {
            if (!p) continue
            const state = p.isPlaying ? 2
                        : (String(p.playbackState) === "Paused" ? 1 : 0)
            // ⚠ The rank is state*2 + cliamp, so cliamp NEVER outranks a
            // different player that is actually playing — it only settles a tie
            // between two players in the same state. Ranking it above state
            // would mean a paused Music tile stealing the buttons from the film
            // somebody is watching.
            const rank = state * 2 +
                         (String(p.dbusName || "").indexOf("cliamp") >= 0 ? 1 : 0)
            if (rank > bestRank) { bestRank = rank; best = p }
        }
        return best
    }

    readonly property bool playing: player ? player.isPlaying : false
    readonly property bool haveAny: player !== null

    /* ── Text that came from somewhere else ──────────────────────────────────
     *
     * A title is a file's tags or a stream server's JSON. Control characters
     * wreck the row they are drawn in, so they come out here exactly as the
     * bar's Media module strips them.
     */
    function clean(s) {
        if (!s) return ""
        return String(s).replace(/[\x00-\x1f\x7f]/g, " ")
                        .replace(/\s+/g, " ").trim()
    }

    /*
     * A title that is really a path, made into something to draw.
     *
     * ⚠ THE QUERY COMES OFF FIRST, AND THAT IS NOT TIDYING. A Plex stream's
     * path carries the account token in its query string; this widget sits on
     * the wallpaper where a screenshot or a shoulder catches it. Cut the query,
     * then take the last path segment, then drop a file extension — which is
     * what music_title_fallback() does on the television.
     */
    function displayTitle(raw) {
        let t = clean(raw)
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

    readonly property string titleText:
        haveAny ? (displayTitle(player.trackTitle) || "(unknown track)") : ""
    readonly property string artistText: {
        if (!haveAny) return ""
        const a = clean(player.trackArtist)
        const b = clean(player.trackAlbum)
        return a && b ? a + " · " + b : (a || b)
    }
    readonly property string whoText:
        haveAny ? clean(player.identity) : ""

    /*
     * Cover art. `trackArtUrl` is whatever the player publishes — a file:// path
     * for a local library, an https:// one for a streaming service.
     *
     * ⚠ THE ART IS ONLY A PICTURE IF IT LOADS. An Image whose source 404s or
     * whose host is unreachable sits at Image.Error forever showing nothing, so
     * the placeholder is what is drawn UNTIL the status says Ready — not the
     * other way round. A card with a hole in it reads as a broken widget.
     */
    readonly property string artUrl: haveAny ? String(player.trackArtUrl || "") : ""

    Item {
        id: cover
        anchors { left: parent.left; top: parent.top }
        width: 64; height: 64

        // The empty tile. Square, like the art it stands in for, so the card
        // does not change shape when a track turns out to have a picture.
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.10)
            border.width: 1
            border.color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.16)
            visible: art.status !== Image.Ready
        }

        Text {
            anchors.centerIn: parent
            visible: art.status !== Image.Ready
            text: Icons.mediaNote
            font.family: Theme.iconFamily
            font.pixelSize: 24
            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b,
                           root.haveAny ? 0.85 : 0.35)
        }

        Image {
            id: art
            anchors.fill: parent
            source: root.artUrl
            visible: status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            // Decoded at the size it is drawn: album art is routinely 1400px
            // square, and holding one of those per screen for a 64px tile is
            // several megabytes to draw a thumbnail.
            sourceSize.width: 128
            sourceSize.height: 128
            asynchronous: true
            smooth: true
        }

        /*
         * ⚠ THE ART IS SQUARE, ON PURPOSE.
         *
         * This was a rounded Rectangle drawn OVER the image, which rounds
         * nothing: the image's own square corners went on showing through the
         * transparent middle of the border, and the result was a square picture
         * wearing a rounded frame with the corners poking out of it. Qt has no
         * cheap rounded image — `clip` is a scissor rectangle and knows nothing
         * about a radius, and the mask that would work costs an offscreen
         * texture per widget per screen, which is the one thing WidgetFrame's
         * own header refuses. Also a `layer.enabled: true` with a null effect
         * was doing exactly that for no benefit at all.
         *
         * So the frame is square too, and honest. A hairline, not the 2px the
         * fake rounding needed to hide its seam.
         */
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.18)
            visible: art.status === Image.Ready
        }
    }

    Column {
        id: text
        anchors {
            left: cover.right; leftMargin: 12
            right: parent.right
            top: parent.top; topMargin: 2
        }
        spacing: 2

        Text {
            width: parent.width
            text: root.haveAny ? root.titleText : "nothing is playing"
            color: root.haveAny ? root.ink : root.inkDim
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.bold: root.haveAny
            elide: Text.ElideRight
            maximumLineCount: 1
        }
        Text {
            width: parent.width
            text: root.artistText
            visible: text !== ""
            color: root.inkDim
            font.family: Theme.fontFamily
            font.pixelSize: 11
            elide: Text.ElideRight
            maximumLineCount: 1
        }
        Text {
            width: parent.width
            text: root.whoText
            visible: text !== ""
            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.85)
            font.family: Theme.fontFamily
            font.pixelSize: 9
            font.letterSpacing: 0.8
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    /* ── The three buttons ───────────────────────────────────────────────────
     *
     * ⚠ A BUTTON A PLAYER CANNOT HONOUR IS DRAWN DIM AND DOES NOTHING, rather
     * than being hidden. The television SAYS so instead ("Spotify cannot skip
     * back") because it has a status line to say it in; a widget has not, and a
     * row of buttons that changes width when the track changes is worse than a
     * greyed one. A radio stream has no previous track, and pretending the press
     * worked is how a button teaches somebody it is broken.
     *
     * ⚠ PREVIOUS SOMETIMES NEEDS TWO PRESSES, and that is the player's rule
     * rather than this one's: MPRIS says a player more than a few seconds into a
     * track may restart it instead of going back, and cliamp documents exactly
     * that at three seconds. It is what every physical skip-back button on earth
     * does, so it is left alone — worth knowing before it is reported as a bug.
     */
    Row {
        id: buttons
        anchors { left: cover.right; leftMargin: 10; bottom: parent.bottom }
        spacing: 4

        Repeater {
            model: [
                { glyph: Icons.mediaPrev, big: false, act: "prev"   },
                { glyph: root.playing ? Icons.mediaPause : Icons.mediaPlay,
                  big: true,  act: "toggle" },
                { glyph: Icons.mediaNext, big: false, act: "next"   }
            ]

            Rectangle {
                id: btn
                required property var modelData

                readonly property bool usable: {
                    if (!root.haveAny) return false
                    switch (modelData.act) {
                    case "prev":   return root.player.canGoPrevious === true
                    case "next":   return root.player.canGoNext === true
                    default:       return root.player.canTogglePlaying === true
                    }
                }

                width: modelData.big ? 34 : 28
                height: 28
                radius: 8
                color: !usable ? "transparent"
                      : hover.hovered
                        ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.22)
                        : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.08)
                border.width: 1
                border.color: usable && hover.hovered
                    ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.55)
                    : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, usable ? 0.14 : 0.06)

                Text {
                    anchors.centerIn: parent
                    text: btn.modelData.glyph
                    font.family: Theme.iconFamily
                    font.pixelSize: btn.modelData.big ? 13 : 11
                    color: !btn.usable ? Qt.rgba(root.ink.r, root.ink.g,
                                                 root.ink.b, 0.25)
                         : btn.modelData.big ? root.accent : root.ink
                }

                HoverHandler { id: hover; enabled: btn.usable }

                TapHandler {
                    enabled: btn.usable
                    onTapped: {
                        const p = root.player
                        if (!p) return
                        switch (btn.modelData.act) {
                        case "prev":   p.previous();      break
                        case "next":   p.next();          break
                        default:       p.togglePlaying(); break
                        }
                    }
                }
            }
        }
    }

    /* ── The progress bar ────────────────────────────────────────────────────
     *
     * Only where the player answers for both numbers. A great many do not —
     * a radio stream has no length, and a browser tab often reports neither —
     * and a bar pinned at zero is a bar that says the track is not moving.
     */
    readonly property bool haveProgress:
        haveAny && player.lengthSupported === true
                && player.positionSupported === true
                && player.length > 0

    Rectangle {
        id: track
        anchors {
            right: parent.right
            bottom: parent.bottom; bottomMargin: 11
            left: buttons.right; leftMargin: 12
        }
        visible: root.haveProgress
        height: 3
        radius: 2
        color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.14)

        Rectangle {
            height: parent.height
            radius: parent.radius
            color: root.accent
            width: {
                if (!root.haveProgress) return 0
                const f = root.position / root.player.length
                return parent.width * Math.max(0, Math.min(1, f))
            }
        }
    }

    // Where in the track we are. Read on a timer rather than bound, because
    // `position` is a D-Bus GET: binding it would make every repaint of this
    // card a round trip to the player.
    property real position: 0

    Timer {
        interval: 1000
        // Only while there is a bar to move AND it is moving. A paused track
        // does not need a wake-up once a second to be told it is still paused.
        running: root.visible && root.haveProgress && root.playing
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            const p = root.player
            root.position = p ? p.position : 0
        }
    }

    // A track change resets the readout immediately rather than leaving the old
    // position on screen until the next tick — which on a short track is most
    // of the way through the new one.
    onTitleTextChanged: {
        const p = root.player
        root.position = p ? p.position : 0
    }
}
