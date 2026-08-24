#!/usr/bin/env bash
# music_title.sh — a queued track is drawn by its NAME, on both surfaces
#
# WHAT THIS EXISTS FOR (reported 2026-08-23): a YouTube playlist loaded and
# played perfectly through the music widget, and the card said
#
#     watch
#     Cliamp
#
# for every song in it. The list was never the broken half. `cliamp queue
# <thing>` takes a path and reports that path back as the title — no tags are
# read — so what reaches MPRIS for a station track is, measured on this machine:
#
#     xesam:title  "watch"     xesam:url  https://www.youtube.com/watch?v=…
#
# `watch` being the last path segment of the URL, and no xesam:artist at all,
# which is why the second line fell through to the player's own name.
#
# ⚠ AND THE WIDGET'S OWN PATH RULE COULD NOT SEE IT. MusicPlayer.qml ported the
# television's "a title that is really a path" rule — cut the query, take the
# last segment — which is right for a local file and for a Plex stream and is
# exactly what NAMES EVERY YOUTUBE SONG `watch`. cliamp had already done that
# reduction before publishing, so there was nothing left to strip and nothing to
# notice: no warning, no error, no line in the journal, and a title that looks
# like a title.
#
# big.c solved this once already, for the television, and it is a CACHE and not
# a parser: whatever queues a track writes down what it queued
# (`music-titles.rec`, keyed by music_key()) and `big music status` reads it
# back. So this asserts the two shells ASK, and that neither of them grew a
# second copy of the key rule — the second-roster trap this project keeps being
# bitten by, and the one that keyed every YouTube track to
# `https://www.youtube.com/watch` in the C the first time round.
#
# A TEXT CHECK, for the reason music_errands.sh gives at length: loading these
# surfaces means driving velle's live desktop and their music.
#
# Usage: music_title.sh [MusicLibrary.qml] [MusicPlayer.qml] [Media.qml]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
lib=${1:-$here/../quickshell/MusicLibrary.qml}
widget=${2:-$here/../quickshell/widgets/MusicPlayer.qml}
bar=${3:-$here/../quickshell/modules/Media.qml}

for f in "$lib" "$widget" "$bar"; do
    [ -f "$f" ] || { echo "  ABORT no such file: $f"; exit 1; }
done

fails=0
say() { echo "  FAIL $*"; fails=$((fails + 1)); }

# ── 1. the library asks the C side what is playing ──────────────────────────
grep -q '"big", "music", "status", "--rec"' "$lib" ||
    say "nothing asks \`big music status\` — the titles cache is never read"
grep -q 'function nameFor' "$lib" ||
    say "there is no way for a surface to ask what a track is called"
grep -q 'function displayTitle' "$lib" ||
    say "the path fallback has no home in MusicLibrary, so each surface needs its own"

# ⚠ THE FETCH IS STAMPED WITH THE TRACK IT ASKED ABOUT. cliamp advances tracks
# on its own, so `nowUrl` moves under a fetch that is already out — the same
# race itemsProc documents, and the same fix.
grep -q 'property string forUrl' "$lib" ||
    say "the name fetch is not stamped with the track it was asked about"
sed -n '/property Process nameProc/,/^    }$/p' "$lib" |
    grep -q 'nameJob.forUrl !== root.nowUrl' ||
    say "a stale name is accepted for whatever track is on screen when it lands"

# ⚠ ASKING IS NOT A BINDING. nameFor() is read from a binding on two surfaces;
# starting a subprocess in there re-runs it on every repaint, which is the fork
# per second the widget's header exists to refuse.
sed -n '/function nameFor/,/^    }$/p' "$lib" | grep -q 'running = true' &&
    say "nameFor() starts a fetch — a binding that forks on every repaint"

# ⚠ AND NO SECOND COPY OF music_key(). The C reduces a YouTube URL to its video
# id and strips a Plex token; a QML copy of that rule is a copy that goes stale,
# and the answer is stamped against the url that was ASKED so it never needs one.
# ⚠ COMMENTS STRIPPED FIRST. This file explains the C rule at length and a
# whole-file grep is satisfied by the prose describing what must not be there —
# the same reason music_errands.sh scopes its checks to chooseItem.
code=$(sed -e 's,//.*,,' -e '/^[[:space:]]*\*/d' -e '/^[[:space:]]*\/\*/d' "$lib")
printf '%s\n' "$code" | grep -qE '\?v=|youtu\.be|youtube\.com' &&
    say "MusicLibrary re-derives a YouTube key — music_key() is C's alone"

# ── 2. and only cliamp is asked about ───────────────────────────────────────
#
# `big music status` answers about cliamp and nothing else. Pointed at a
# Firefox tab it would draw cliamp's song under Firefox's title.
for f in "$widget" "$bar"; do
    grep -q 'org.mpris.MediaPlayer2.cliamp' "$f" ||
        say "$(basename "$f") asks about whatever player it found, not cliamp"
    # The URL is the identity: two songs off one playlist are both called
    # `watch`, and only their urls differ.
    grep -q 'xesam:url' "$f" ||
        say "$(basename "$f") keys the answer on something other than the track's url"
    grep -q 'MusicLibrary.nowUrl = wantNamed' "$f" ||
        say "$(basename "$f") never tells MusicLibrary which track to name"
    grep -q 'MusicLibrary.nameFor' "$f" ||
        say "$(basename "$f") never reads the name back — it still draws MPRIS raw"
done

# ── 3. the fallback is still there, and it still strips a query ─────────────
#
# ⚠ A PLEX PATH CARRIES THE ACCOUNT TOKEN. Both surfaces sit where a screenshot
# or a shoulder catches it — the wallpaper and the bar — and the bar drew
# `xesam:title` completely raw until this went in.
for f in "$widget" "$bar"; do
    grep -q 'MusicLibrary.displayTitle' "$f" ||
        say "$(basename "$f") has no fallback for a track the cache does not know"
    grep -q 'clean(player.trackTitle)' "$f" &&
        say "$(basename "$f") still draws the raw MPRIS title — a Plex token in the open"
done

# The reduction itself: query off FIRST, then the last segment.
dt=$(sed -n '/function displayTitle/,/^    }$/p' "$lib")
printf '%s\n' "$dt" | grep -q 'indexOf("?")' ||
    say "displayTitle does not cut the query, so a Plex token reaches the label"
printf '%s\n' "$dt" | grep -q 'lastIndexOf("/")' ||
    say "displayTitle does not reduce a path to its last segment"

if [ "$fails" -eq 0 ]; then
    echo "  ok  a queued track is drawn by its name on the card and in the bar"
    exit 0
fi
exit 1
