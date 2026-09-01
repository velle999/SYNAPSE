import QtQuick
import Quickshell.Services.Mpris
import ".."
import QtQuick.Controls

/*
 * The music widget — a source, a library, what is playing, and the buttons.
 *
 * ⚠ IT IS BOTH HALVES OF THE TELEVISION'S MUSIC, and it shipped as one.
 *
 * syn-arcade has `big transport` (MPRIS: act on whatever is making the noise)
 * and `big music` (drive cliamp: pick a source, browse a library, fill a
 * queue). This file was written as the transport alone, and the header that
 * used to sit here argued the case for that at length — MPRIS works on Spotify
 * and mpv and a browser tab, syn-arcade is not a dependency of the shell.
 *
 * Both of those are true and neither was the question, which velle put exactly:
 * this was a remote rather than a music player — nothing here chose a source
 * and nothing here started anything, which is what a music widget is for. A
 * transport can only act on music something else began; on a desktop with
 * nothing playing it was three dim buttons and the words "nothing is playing".
 *
 * So the picker and the library are here now, through MusicLibrary.qml, which
 * is `big music` and therefore the SAME library the television browses. The
 * card unfolds into it in place rather than opening a second surface — a
 * desktop widget that summoned a full-screen panel would be a launcher for a
 * player rather than a player.
 *
 * ⚠ AND THE MPRIS OBJECTION IS ANSWERED RATHER THAN OVERRULED. The transport
 * still goes over MPRIS for every player that is not cliamp, the picker is
 * simply absent on a machine without syn-arcade (MusicLibrary.have), and the
 * card in that state is byte-identical to the one that shipped. Nobody loses a
 * working widget; the installs that have the player get the player.
 *
 * The two rules the television learned the hard way are ported rather than
 * rediscovered:
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
 *      ⚠ AND STRIPPING IT IS NOT ENOUGH ON YOUTUBE, which is the half this
 *      widget shipped without: `…/watch?v=<id>` reduces to the word `watch`,
 *      so every song off a playlist was drawn with the same non-name. The
 *      television has never had that bug because it asks the titles cache
 *      first; so does the card now — see WHAT THE TRACK IS CALLED below.
 *
 * ⚠ AND THE TRANSPORT STILL DOES NOT SHELL OUT TO `big transport`. That command
 * is the same MPRIS interface reached through `busctl`, which is right for a C
 * program that must not link libsystemd — and wrong here, because this file is
 * already inside a process with a live, event-driven binding. Polling a
 * subprocess once a second to learn something the process is already being told
 * would cost a fork per second and still be a second behind.
 *
 * The LIBRARY is the opposite case and shells out gladly: there is no D-Bus
 * answer to "what Plex albums are there", it is asked when somebody opens the
 * picker rather than once a second, and MusicLibrary.qml's header sets out why
 * the two are not the same decision.
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
    label: I18n.tr("NOW PLAYING")
    accent: Theme.magenta
    interactive: true

    // Title, artist and the time readout are body text on a card that at a low
    // glass level is not there — the note's case exactly.
    inkOnBackdrop: true

    /*
     * ⚠ EVERYTHING BELOW DRAWS THROUGH accentInk, NOT accent.
     *
     * `accent` above is what this widget ASKS for; accentInk is that colour
     * after WidgetFrame has restored it onto whatever the card is actually
     * sitting on — same correction `ink` has always had, and the analog clock's
     * neon face is what proved it was missing.
     *
     * It matters here because Theme.magenta is the WALLPAPER'S accent, and this
     * card at a low glass level is barely there, so the accent is drawn close
     * to the picture it was measured from. Two of these are not decoration: the
     * artist line under the title is 9px of accent text with no outline behind
     * it, and the progress fill is a mark whose whole job is to be visible
     * against its own track.
     *
     * The washes and borders go through it too, and that is the point — one
     * accent, corrected once. Splitting it would put a corrected glyph inside
     * an uncorrected wash, which is the same colour disagreeing with itself on
     * one button.
     */

    homeEdgeH: "left"; homeEdgeV: "bottom"
    homeMarginX: 20
    // Clear of the visualiser when both are on, the same courtesy BigClock pays.
    homeMarginY: WidgetState.visualizer ? 124 : 24

    /* ── The card unfolds ────────────────────────────────────────────────────
     *
     * ⚠ EVERYTHING BELOW IS ABSENT, NOT DIM, ON A MACHINE WITHOUT SYN-ARCADE.
     * MusicLibrary probes once for the command; where it is missing there is no
     * chip, no drawer and no extra height, and the card is the 268×96 transport
     * that shipped. A handle that opens an empty drawer is worse than no handle,
     * and the shell does not depend on the package that fills it.
     */
    readonly property bool canBrowse: MusicLibrary.have

    property bool browsing: false

    // 96 is the transport's own height; the chip needs a row of its own and
    // takes it only where there is a library to point at.
    readonly property int npHeight:  canBrowse ? 118 : 96
    readonly property int libHeight: 176

    cardWidth: 268
    bodyHeight: npHeight + (browsing && canBrowse ? libHeight : 0)

    /*
     * ⚠ THE UNFOLD IS ANIMATED, AND NOT FOR PRETTINESS. This card's home edge
     * is the BOTTOM of the screen, so growing it moves the whole widget upward:
     * an instant change teleports the chip out from under the pointer that just
     * pressed it, which reads as the widget having been replaced rather than
     * opened. Over a couple of frames it reads as a drawer.
     */
    Behavior on bodyHeight {
        NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
    }

    // Opening asks again. The source list is a media server's answer, and the
    // one fetched at login is the answer from before the server was switched on
    // — a picker that can only ever be as fresh as the session is a picker that
    // says "no Plex" all evening.
    onBrowsingChanged: if (browsing) MusicLibrary.refreshSources()

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

    /*
     * ⚠ WHAT THE PLAY BUTTON DOES WHEN THERE IS NOTHING TO PLAY. This is the
     * whole of velle's report in one property: MPRIS can only act on a player
     * that already exists, so on a quiet desktop the transport was three dim
     * buttons — "a remote, not a music player". `big music play` is the verb
     * that STARTS one, filling an empty queue from the chosen source.
     *
     * It is a fallback and not a mode: the moment anything is playing, MPRIS
     * has the buttons again, cliamp included. Two paths, one button, and the
     * one that can act on what is actually making noise wins.
     */
    readonly property bool driveCliamp: !haveAny && canBrowse

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
     * ── WHAT THE TRACK IS CALLED ──────────────────────────────────
     *
     * ⚠ CLIAMP DOES NOT NAME A QUEUED TRACK, AND RULE 2 ABOVE IS ONLY HALF OF
     * IT. Stripping a path down to its last segment is right for a local file
     * and for a Plex stream; on YouTube it is what NAMES EVERY SONG `watch`.
     * cliamp has already done that reduction by the time MPRIS is published —
     * `xesam:title` arrives as the literal word `watch`, with no artist — so
     * there is nothing left here to strip and nothing to notice. Reported
     * exactly that way: a playlist that loaded and played perfectly, drawn on
     * the card as "watch" over "Cliamp".
     *
     * The name lives in syn-arcade's titles cache, written by whatever queued
     * the track, and MusicLibrary.nameFor() reads it back — one fork per TRACK
     * CHANGE, which does not touch the no-polling rule above. Where the cache
     * has nothing, or on a machine without syn-arcade, displayTitle() is
     * exactly the fallback that shipped.
     */

    // ⚠ THE URL IS THE IDENTITY, NOT THE TITLE. Two songs off one playlist can
    // both be called `watch`; their urls differ, and the url is what the answer
    // is stamped against.
    readonly property string trackUrl:
        haveAny && player.metadata
            ? String(player.metadata["xesam:url"] || "") : ""

    // ⚠ ONLY CLIAMP IS ASKED ABOUT. `big music status` answers about cliamp
    // and nothing else, so pointing it at a Firefox tab would put cliamp's song
    // under Firefox's. Matched on the bus name the same way big.c's transport
    // does — the part after `org.mpris.MediaPlayer2.`, compared whole.
    readonly property bool pathNamed:
        haveAny && String(player.dbusName) === "org.mpris.MediaPlayer2.cliamp"

    // The want, as one property so it re-fires when EITHER half moves: a track
    // change, or the player being swapped for cliamp mid-song.
    readonly property string wantNamed:
        (pathNamed && canBrowse) ? trackUrl : ""
    onWantNamedChanged: if (wantNamed !== "") MusicLibrary.nowUrl = wantNamed

    readonly property string titleText: {
        if (!haveAny) return ""
        const named = MusicLibrary.nameFor(root.wantNamed)
        if (named) return named
        return MusicLibrary.displayTitle(player.trackTitle) || I18n.tr("(unknown track)")
    }
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
     *
     * ── AND CLIAMP PUBLISHES NONE AT ALL ─────────────────────────────────
     *
     * ⚠ NOT AN EMPTY artUrl — NO `mpris:artUrl` KEY. Measured mid-playlist,
     * the whole of what a playing YouTube track offers:
     *
     *     xesam:title "watch"  xesam:url …/watch?v=…  mpris:length  mpris:trackid
     *
     * Four keys. So this tile drew its placeholder for every song off a station
     * — reported as the widget "still not loading all the way", and reported
     * accurately. The discriminating test was already in the report: the SAME
     * video played through Firefox fills the tile in, because Firefox publishes
     * a thumbnail. One player supplies the field and the other does not; the
     * QML was never the broken half.
     *
     * The answer is the one the title took, for the same reason and down the
     * same wire: a YouTube thumbnail is a pure function of the video id, big.c
     * already reduces a URL to that id in music_key(), and it now hands the
     * picture back as a column on the row MusicLibrary was fetching anyway.
     *
     * ⛔ NOT DERIVED HERE. Building an i.ytimg.com URL in QML would be a second
     * copy of music_key()'s rule — the mistake this file's header is about, and
     * the one that keyed every YouTube track to `…/watch` in the C the first
     * time. Ask; do not re-derive.
     *
     * ⚠ THE PLAYER STILL WINS WHERE IT HAS ONE. Firefox, Spotify and a local
     * library all publish real art, and theirs is the actual cover rather than
     * a derived thumbnail. The fallback is for the player that says nothing.
     */
    readonly property string artUrl: {
        if (!haveAny) return ""
        const own = String(player.trackArtUrl || "")
        return own !== "" ? own : MusicLibrary.artFor(root.wantNamed)
    }

    /* ── What is playing ─────────────────────────────────────────────────────
     *
     * ⚠ ONE ITEM, RATHER THAN THE FIVE ANCHORED TO `parent` THIS USED TO BE.
     * The transport's buttons and progress bar were anchored to the BOTTOM of
     * the card, which was the bottom of the widget until there was a drawer
     * under it — after which they would have sunk to the bottom of the open
     * drawer, three rows below the track they act on. They belong to the
     * now-playing block and now say so.
     */
    Item {
        id: np
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.npHeight

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
                color: Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b,
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
             * nothing: the image's own square corners went on showing through
             * the transparent middle of the border, and the result was a square
             * picture wearing a rounded frame with the corners poking out of it.
             * Qt has no cheap rounded image — `clip` is a scissor rectangle and
             * knows nothing about a radius, and the mask that would work costs
             * an offscreen texture per widget per screen, which is the one thing
             * WidgetFrame's own header refuses. Also a `layer.enabled: true`
             * with a null effect was doing exactly that for no benefit at all.
             *
             * So the frame is square too, and honest. A hairline, not the 2px
             * the fake rounding needed to hide its seam.
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
                text: root.haveAny ? root.titleText
                    : root.driveCliamp ? I18n.tr("nothing playing yet")
                                       : I18n.tr("nothing is playing")
                color: root.haveAny ? root.ink : root.inkDim
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.bold: root.haveAny
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            Text {
                width: parent.width
                // With a library present the second line is what the play
                // button WILL do, so the quiet card answers the question it
                // used to leave hanging.
                text: root.haveAny ? root.artistText
                    : root.driveCliamp
                        ? I18n.tr("press play to start %1")
                              .arg(MusicLibrary.sourceName || I18n.tr("a source"))
                        : ""
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
                color: Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.85)
                font.family: Theme.fontFamily
                font.pixelSize: 9
                font.letterSpacing: 0.8
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        /* ── The three buttons ───────────────────────────────────────────────
         *
         * ⚠ A BUTTON A PLAYER CANNOT HONOUR IS DRAWN DIM AND DOES NOTHING,
         * rather than being hidden. The television SAYS so instead ("Spotify
         * cannot skip back") because it has a status line to say it in; a widget
         * has not, and a row of buttons that changes width when the track
         * changes is worse than a greyed one. A radio stream has no previous
         * track, and pretending the press worked is how a button teaches
         * somebody it is broken.
         *
         * ⚠ PREVIOUS SOMETIMES NEEDS TWO PRESSES, and that is the player's rule
         * rather than this one's: MPRIS says a player more than a few seconds
         * into a track may restart it instead of going back, and cliamp
         * documents exactly that at three seconds. It is what every physical
         * skip-back button on earth does, so it is left alone — worth knowing
         * before it is reported as a bug.
         */
        Row {
            id: buttons
            anchors { left: cover.right; leftMargin: 10; top: cover.bottom; topMargin: 4 }
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
                        switch (modelData.act) {
                        case "prev":
                            return root.haveAny && root.player.canGoPrevious === true
                        case "next":
                            return root.haveAny && root.player.canGoNext === true
                        default:
                            // ⚠ Play is usable with NO player, where the library
                            // can start one. That is the button the report was
                            // about; the other two have nothing to act on and
                            // stay dim.
                            return root.haveAny
                                   ? root.player.canTogglePlaying === true
                                   : root.driveCliamp
                        }
                    }

                    width: modelData.big ? 34 : 28
                    height: 28
                    radius: 8
                    color: !usable ? "transparent"
                          : hover.hovered
                            ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.22)
                            : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.08)
                    border.width: 1
                    border.color: usable && hover.hovered
                        ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.55)
                        : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, usable ? 0.14 : 0.06)

                    Text {
                        anchors.centerIn: parent
                        text: btn.modelData.glyph
                        font.family: Theme.iconFamily
                        font.pixelSize: btn.modelData.big ? 13 : 11
                        color: !btn.usable ? Qt.rgba(root.ink.r, root.ink.g,
                                                     root.ink.b, 0.25)
                             : btn.modelData.big ? root.accentInk : root.ink
                    }

                    HoverHandler { id: hover; enabled: btn.usable }

                    TapHandler {
                        enabled: btn.usable
                        onTapped: {
                            const p = root.player
                            switch (btn.modelData.act) {
                            case "prev":   if (p) p.previous(); break
                            case "next":   if (p) p.next();     break
                            default:
                                if (p) p.togglePlaying()
                                else   MusicLibrary.play()
                                break
                            }
                        }
                    }
                }
            }
        }

        /* ── The progress bar ────────────────────────────────────────────────
         *
         * Only where the player answers for both numbers. A great many do not —
         * a radio stream has no length, and a browser tab often reports neither —
         * and a bar pinned at zero is a bar that says the track is not moving.
         */
        Rectangle {
            id: track
            anchors {
                right: parent.right
                left: buttons.right; leftMargin: 12
                verticalCenter: buttons.verticalCenter
            }
            visible: root.haveProgress
            height: 3
            radius: 2
            color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.14)

            Rectangle {
                height: parent.height
                radius: parent.radius
                color: root.accentInk
                width: {
                    if (!root.haveProgress) return 0
                    const f = root.position / root.player.length
                    return parent.width * Math.max(0, Math.min(1, f))
                }
            }
        }

        /* ── The source chip ─────────────────────────────────────────────────
         *
         * What the library is pointed at, and the handle that unfolds it.
         *
         * ⚠ IT NAMES THE SOURCE, NOT THE PLAYER. The accent line up in the text
         * column is MPRIS's identity — who is making the noise — and on a
         * machine with both they are routinely different things: Firefox
         * playing, Plex selected. Naming the source here is what makes a press
         * on the play button predictable.
         */
        Rectangle {
            id: chip
            visible: root.canBrowse
            anchors {
                left: parent.left; right: parent.right
                bottom: parent.bottom
            }
            height: 20
            radius: 6
            color: chipHover.hovered || root.browsing
                 ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.18)
                 : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.07)
            border.width: 1
            border.color: root.browsing
                ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.45)
                : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.12)

            Text {
                id: chipGlyph
                anchors { left: parent.left; leftMargin: 7; verticalCenter: parent.verticalCenter }
                text: Icons.mediaNote
                font.family: Theme.iconFamily
                font.pixelSize: 10
                color: Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.9)
            }

            Text {
                anchors {
                    left: chipGlyph.right; leftMargin: 7
                    right: chipCaret.left; rightMargin: 7
                    verticalCenter: parent.verticalCenter
                }
                // Before the first fetch comes back there is no name yet, and
                // the chip still has to say what pressing it is for.
                text: MusicLibrary.sourceName !== "" ? MusicLibrary.sourceName
                                                     : I18n.tr("choose a source")
                color: root.ink
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.letterSpacing: 0.4
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                id: chipCaret
                anchors { right: parent.right; rightMargin: 7; verticalCenter: parent.verticalCenter }
                text: root.browsing ? Icons.caretUp : Icons.caretDown
                font.family: Theme.iconFamily
                font.pixelSize: 10
                color: root.inkDim
            }

            HoverHandler { id: chipHover }
            TapHandler { onTapped: root.browsing = !root.browsing }
        }
    }

    /* ── The library ─────────────────────────────────────────────────────────
     *
     * The half that STARTS music: the sources big.c knows about, and what is in
     * the chosen one. Both come from `syn-arcade big music` through
     * MusicLibrary.qml, so this drawer and the television's own picker cannot
     * grow different libraries.
     */
    Item {
        id: library
        visible: root.browsing && root.canBrowse
        anchors {
            left: parent.left; right: parent.right
            top: np.bottom; topMargin: 10
            bottom: parent.bottom
        }
        // ⚠ The unfold animates the card's HEIGHT, so for those few frames this
        // is taller than the room it has. Without the clip the list draws
        // straight through the bottom of the card and out onto the wallpaper.
        clip: true

        /*
         * The sources, along the top. A row rather than a dropdown: there are
         * a handful of them, and a menu inside a widget that is already a
         * pop-out is a second surface to dismiss.
         *
         * ⚠ SWITCHING SOURCE RESTARTS THE PLAYER — `--provider` is a cliamp
         * start-up flag, so big.c can only honour a new one by bringing cliamp
         * back up. MusicLibrary's header has the mechanism; it is worth knowing
         * before it is reported as the picker killing the music.
         */
        ListView {
            /*
             * ⚠ NO SCROLLBAR ON THIS ONE, and it is the exception rather than
             * an oversight. It is a 24px-tall strip of source pills scrolling
             * SIDEWAYS: a horizontal bar would be half the height of the thing
             * it describes, and the strip already scrolls the current source
             * into frame by itself (showCurrent, below) so there is nothing to
             * go looking for. The track list further down is the view in this
             * widget that needed one, and has one.
             */
            id: sources
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 24
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            model: MusicLibrary.sources

            /*
             * ⚠ THE CHOSEN SOURCE IS SCROLLED TO, NOT JUST HIGHLIGHTED. There
             * are more pills than fit 246px, and the current one is routinely
             * off the right-hand end — measured: the chip said "Radio" while
             * the visible row was Plex, YouTube Music, Spotify and half of
             * Local, none of them lit. A picker whose selection is out of frame
             * reads as a picker with nothing selected.
             */
            function showCurrent() {
                const rows = MusicLibrary.sources
                for (let i = 0; i < rows.length; i++)
                    if (rows[i].id === MusicLibrary.sourceId) {
                        positionViewAtIndex(i, ListView.Contain)
                        return
                    }
            }
            onModelChanged: showCurrent()
            Connections {
                target: MusicLibrary
                function onSourceIdChanged() { sources.showCurrent() }
            }
            // Opening is the other moment it can be wrong: the drawer is not
            // laid out while it is closed, so a position asked for then is
            // asked of a list with no width yet.
            onVisibleChanged: if (visible) showCurrent()

            delegate: Rectangle {
                id: src
                required property var modelData

                readonly property bool current: modelData.id === MusicLibrary.sourceId

                width: srcText.implicitWidth + 18
                height: sources.height
                radius: 6
                color: current
                     ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.24)
                     : srcHover.hovered
                       ? Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.13)
                       : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.06)
                border.width: 1
                border.color: current
                    ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.55)
                    : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.10)

                Text {
                    id: srcText
                    anchors.centerIn: parent
                    text: src.modelData.name
                    color: src.current ? root.ink : root.inkDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                }

                HoverHandler { id: srcHover }
                TapHandler { onTapped: MusicLibrary.setSource(src.modelData.id) }
            }
        }

        /*
         * What is in it.
         *
         * ⚠ THE LIST IS GATED ON THE SOURCE IT CAME FROM. `items` outlives the
         * switch by however long the fetch takes, and drawing the old source's
         * albums under the new source's name would leave rows on screen that
         * play off a source nobody has selected. Empty until the answer for
         * THIS source arrives.
         */
        ListView {
            // A view that scrolls says so — see SynScrollBar.qml.
            ScrollBar.vertical: SynScrollBar {}
            id: items
            anchors {
                left: parent.left; right: parent.right
                top: sources.bottom; topMargin: 8
                bottom: parent.bottom
                // Room for the status line, which is drawn OVER the tail of the
                // list only when it has something to say — reserving the strip
                // unconditionally would leave a 14px gutter under every full
                // list for a message that is almost never there.
                bottomMargin: statusLine.visible ? 18 : 0
            }
            clip: true
            spacing: 2
            model: MusicLibrary.itemsFor === MusicLibrary.sourceId
                 ? MusicLibrary.items : []

            delegate: Rectangle {
                id: item
                required property var modelData

                /*
                 * ⚠ NOT EVERY ROW IS A TRACK. A YouTube list carries Search…,
                 * Your playlists and Sign in… beside the stations, and the way
                 * back out of a playlist is a row too.
                 *
                 * ⚠ AND THE ANSWER COMES FROM MusicLibrary RATHER THAN FROM
                 * `kind` HERE. This file used to draw every `action` row dim,
                 * which was right while none of them could be pressed and is
                 * the whole of what velle saw: signed in through Firefox, three
                 * grey rows and nothing to do with them. Which errands this
                 * machine can actually run is one answer in one place — see
                 * pressable() — and a second opinion about it here is a second
                 * thing to keep in step.
                 */
                readonly property bool playable: MusicLibrary.pressable(modelData)

                width: items.width
                height: 26
                radius: 5
                color: itemHover.hovered && playable
                     ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.16)
                     : "transparent"

                Text {
                    anchors {
                        left: parent.left; leftMargin: 8
                        right: itemNote.left; rightMargin: 8
                        verticalCenter: parent.verticalCenter
                    }
                    text: item.modelData.name
                    color: item.playable ? root.ink : root.inkDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Text {
                    id: itemNote
                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    // Under half the row: an artist name is worth
                    // showing and never worth eating the album's own title.
                    width: Math.min(implicitWidth, item.width * 0.45)
                    text: item.modelData.note
                    /*
                     * ⚠ A URL IS NOT A NOTE. A station's note IS its address —
                     * that is what `big music yt add` stores and what the
                     * television shows — and a column of truncated
                     * "https://music.you…" says nothing about the station while
                     * filling half the row. The same instinct as displayTitle()
                     * above: what reaches the wallpaper should be something to
                     * read, and an address may carry a token besides.
                     */
                    visible: text !== "" && text.indexOf("http") !== 0
                    color: root.inkDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                HoverHandler { id: itemHover }
                TapHandler { onTapped: MusicLibrary.chooseItem(item.modelData) }
            }
        }

        /*
         * WHY THE LIST IS EMPTY, where the list would have been.
         *
         * ⚠ AN EMPTY LIST ALWAYS SAYS WHY. "No albums" and "the server is
         * down" both draw nothing, and big.c has already worked out which it is
         * — "needs yt-dlp", "not set up", "no music folder on this machine".
         * Having fetched the reason it would be a waste not to show it, and a
         * picker that answers a press with silence is the thing that note
         * exists to prevent.
         *
         * ⚠ AND IT SITS IN THE MIDDLE OF THE SPACE IT IS EXPLAINING. This was
         * a line pinned to the bottom edge, which put "press play to start this
         * source" under 250px of nothing — the message read as a footnote to an
         * empty drawer rather than as the drawer's answer.
         */
        Column {
            anchors {
                left: parent.left; right: parent.right
                verticalCenter: items.verticalCenter
                margins: 10
            }
            visible: items.count === 0
            spacing: 9

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: MusicLibrary.loading ? I18n.tr("loading…")
                    : MusicLibrary.status !== "" ? MusicLibrary.status
                                                 : I18n.tr("nothing to show for this source")
                color: root.inkDim
                font.family: Theme.fontFamily
                font.pixelSize: 10
            }

            /*
             * ── and the thing that FIXES it ─────────────────────────────────
             *
             * ⚠ THE SENTENCE WAS NOT A BUTTON, AND IT READ LIKE ONE. Spotify's
             * note is "press to sign in — needs Spotify Premium", drawn in the
             * middle of an empty drawer, and the only thing to press was the
             * source chip that had already been pressed. So the widget invited
             * somebody to sign in and then had nowhere for them to do it,
             * which is how it was reported: a row that says to click to log in
             * and does nothing when it is clicked.
             *
             * ⚠ A BUTTON IS ITS OWN LABEL. The note above says what is missing
             * and why; this says what the press does, in its own words, and
             * MusicLibrary keys it off big.c's action column so a source that
             * grows a new one arrives here already working.
             *
             * ⚠ AND IT IS NOT DRAWN WHILE THE LIST IS STILL COMING. `action` is
             * "" until the first fetch lands, so a button bound to it would
             * flicker into existence a beat after the drawer opens.
             */
            Rectangle {
                id: errandBtn
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !MusicLibrary.loading && MusicLibrary.sourceErrand !== ""
                width: errandLabel.implicitWidth + 24
                height: 22
                radius: 6
                color: errandHover.hovered
                     ? Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.30)
                     : Qt.rgba(root.accentInk.r, root.accentInk.g, root.accentInk.b, 0.16)
                border.width: 1
                border.color: Qt.rgba(root.accentInk.r, root.accentInk.g,
                                      root.accentInk.b, 0.50)

                Text {
                    id: errandLabel
                    anchors.centerIn: parent
                    text: MusicLibrary.sourceErrand
                    color: root.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.letterSpacing: 0.4
                }

                HoverHandler { id: errandHover }
                TapHandler { onTapped: MusicLibrary.runSourceErrand() }
            }
        }

        /*
         * The status line proper, under a list that HAS something in it: the
         * one thing left to say there is that a press has not been acted on
         * yet ("still working on the last one"), which the message above cannot
         * carry because there is a list in the way.
         */
        Text {
            id: statusLine
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 14
            visible: items.count > 0 && MusicLibrary.status !== ""
            text: MusicLibrary.status
            color: root.inkDim
            font.family: Theme.fontFamily
            font.pixelSize: 9
            font.letterSpacing: 0.3
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    readonly property bool haveProgress:
        haveAny && player.lengthSupported === true
                && player.positionSupported === true
                && player.length > 0

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
