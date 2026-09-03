#!/bin/bash
# cli_test.sh — syn-play against a real mpv, on its own socket, in its own home.
#
# ⛔ EVERY PATH AND THE SOCKET MOVE TOGETHER. SYNPLAY_HOME redirects the history,
# the playlists, the config AND the IPC socket — because a suite that used the
# shared socket would `stop` the music the person running it was listening to,
# and one that used the real data directory would write into the history of
# whatever they had actually watched. Structural, not careful.
#
# ⚠ IT SKIPS RATHER THAN FAILS WITHOUT mpv. mpv is an optdepend: the frontend
# builds and its parser is tested without it, and a package that refused to
# build on a machine with no player installed would be a frontend nobody can
# package.
#
# ⚠ NO AUDIO REACHES THE SPEAKERS. --ao=null is forced through the environment,
# because a test suite that plays sound is one that gets run once. Same family
# as the headless-window rules elsewhere in this repo.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE LOCALE THIS SUITE ASSERTS IN IS PINNED. Every contains() below looks
# for an English phrase, and once syn-play is installed the binary answers the
# desktop's language — so on a German box these assertions fail for a program
# that is working exactly as intended.
# ⚠ LANGUAGE is UNSET, not set: gettext reads it before LC_ALL, so an ambient
# LANGUAGE=de wins over LC_ALL=C and the pin does nothing.
export LC_ALL=C.UTF-8
unset LANGUAGE

BIN="${1:-}"
[ -x "$BIN" ] || BIN=$(command -v syn-play)
[ -x "$BIN" ] || { echo "cli_test: no syn-play binary"; exit 1; }
# ⛔ ABSOLUTE, because one case below `cd`s into the media directory to check
# that a RELATIVE path is resolved before mpv sees it — and a relative $BIN
# stops existing the moment it does. meson passes a full path, so run by hand
# the suite failed two cases the build ran green.
case "$BIN" in /*) ;; *) BIN="$PWD/$BIN" ;; esac

fails=0
check() {  # check <what> <want> <got>
    if [ "$2" = "$3" ]; then
        echo "  ok    $1"
    else
        echo "  FAIL  $1"
        echo "        want: [$2]"
        echo "        got:  [$3]"
        fails=$((fails + 1))
    fi
}
contains() {  # contains <what> <needle> <haystack>
    case "$3" in
        *"$2"*) echo "  ok    $1" ;;
        *) echo "  FAIL  $1"; echo "        looked for: [$2]"
           echo "        in:         [$3]"; fails=$((fails + 1)) ;;
    esac
}

T=$(mktemp -d)
export SYNPLAY_HOME="$T"
export HOME="$T"
cleanup() {
    "$BIN" stop >/dev/null 2>&1
    sleep 0.3
    rm -rf "$T"
}
trap cleanup EXIT

echo "syn-play"

# ── the half that needs no player ───────────────────────────────────────────

out=$("$BIN" --version)
contains "--version names the program" "syn-play" "$out"

out=$("$BIN" status; echo "rc=$?")
contains "status with nothing playing says so, and exits non-zero" "rc=3" "$out"
contains "...in words, not a traceback" "Nothing is playing" "$out"

out=$("$BIN" --rec status)
contains "--rec says the same thing in one record" "state	stopped" "$out"

# ⛔ A MISTYPED VERB IS NOT A FILENAME. `syn-play film.mkv` has to work, so
# anything unrecognised falls through to being treated as a path — which means a
# typo would otherwise be reported as a missing file rather than a bad command.
out=$("$BIN" shufle 2>&1; echo "rc=$?")
contains "a mistyped verb is reported as a command, not a missing file" \
         "unknown command 'shufle'" "$out"
contains "...and exits 2" "rc=2" "$out"

out=$("$BIN" history)
contains "an empty history says so" "nothing played yet" "$out"

out=$("$BIN" playlist list)
contains "an empty playlist list says how to make one" "syn-play playlist save" "$out"

out=$("$BIN" playlist rm ../../.bashrc 2>&1; echo "rc=$?")
contains "⛔ a playlist name that escapes its directory is refused" \
         "not a playlist name" "$out"
check "...and nothing outside SYNPLAY_HOME was touched" "yes" \
      "$([ -e "$T/../.bashrc" ] && echo no || echo yes)"

out=$("$BIN" find nothingmatchesthis 2>&1; echo "rc=$?")
contains "find with no matches says so" "nothing matches" "$out"
contains "...and exits 4" "rc=4" "$out"

# ── quick open, which does not need a player either ─────────────────────────

mkdir -p "$T/Videos/Series"
touch "$T/Videos/Black Hawk Down (2001).mkv"
touch "$T/Videos/Series/S01E02. The One Where They Argue.mkv"
touch "$T/Videos/notes.txt"

out=$("$BIN" find hawk)
contains "quick open finds a file by part of its name" "Black Hawk Down" "$out"

out=$("$BIN" find argue)
contains "...including one nested in a subdirectory" "They Argue" "$out"

out=$("$BIN" find notes 2>&1)
contains "a .txt in a media folder is not offered" "nothing matches" "$out"

# ⚠ THE BASENAME IS WHAT PEOPLE TYPE. Scoring the whole path lets the folder
# name supply half the letters of every query, and "vid" then returns the entire
# library in whatever order the walk happened to produce.
out=$("$BIN" --rec find "bhd")
contains "a scattered subsequence still matches" "Black Hawk Down" "$out"

# ── everything past here needs mpv ──────────────────────────────────────────

if ! command -v mpv >/dev/null 2>&1; then
    echo "  skip  mpv is not installed — the transport half cannot be checked"
    echo ""
    [ "$fails" -eq 0 ] && echo "all checks passed" || { echo "$fails check(s) failed"; exit 1; }
    exit 0
fi

# ⚠ Silent, and with no window. A suite that opened a video window on the
# developer's desktop would be one nobody runs twice.
export MPV_HOME="$T/mpv"
mkdir -p "$MPV_HOME"
printf 'ao=null\nvo=null\nreally-quiet=yes\n' > "$MPV_HOME/mpv.conf"

# A real file mpv will actually open: one second of silence.
SILENT="$T/Videos/Silence One.wav"
python3 - "$SILENT" <<'PY'
import struct, sys
n, rate = 8000, 8000
data = b"\x80" * n
hdr = b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " + struct.pack(
    "<IHHIIHH", 16, 1, 1, rate, rate, 1, 8) + b"data" + struct.pack("<I", n)
open(sys.argv[1], "wb").write(hdr + data)
PY
cp "$SILENT" "$T/Videos/Silence Two.wav"

"$BIN" "$SILENT" >/dev/null 2>&1
for i in $(seq 1 40); do
    "$BIN" status >/dev/null 2>&1 && break
    sleep 0.25
done

out=$("$BIN" --rec status)
contains "playing a file brings a session up" "state	play" "$out"
# ⚠ mpv answers `media-title` with the BARE FILENAME when a file has no tags,
# extension and all. Taking that blindly would show `Silence One.wav` for a file
# this program would otherwise call `Silence One` — and only for untagged files,
# which is the half that makes it look arbitrary.
contains "an untagged file is named without its extension" "title	Silence One" "$out"
check "...and not with mpv's filename fallback" "0" \
      "$(printf '%s\n' "$out" | grep -c 'title	Silence One\.wav')"

"$BIN" add "$T/Videos/Silence Two.wav" >/dev/null 2>&1
sleep 0.5
out=$("$BIN" --rec queue)
check "add puts a second file in the queue" "2" "$(printf '%s\n' "$out" | grep -c '^item')"
contains "...and the queue marks which row is current" "item	0	yes" "$out"

# ⛔ A PATH IS RESOLVED BEFORE MPV SEES IT. mpv's working directory is wherever
# it was started — for a player launched from the dock, `/` — so a relative name
# handed over raw is a file-not-found for a file that is plainly there.
(cd "$T/Videos" && "$BIN" add "Silence Two.wav" >/dev/null 2>&1)
sleep 0.5
out=$("$BIN" --rec queue)
check "a relative path is resolved, not handed to mpv as typed" "3" \
      "$(printf '%s\n' "$out" | grep -c '^item')"

out=$("$BIN" shuffle)
contains "shuffle is mpv's own" "Shuffled" "$out"
out=$("$BIN" unshuffle)
contains "...and so is the undo, which restores the order added" "Unshuffled" "$out"

# ── a FOLDER, which is what makes shuffle fair ──────────────────────────────
#
# ⛔ THE BUG THIS EXISTS FOR: A QUEUED FOLDER STAYED ONE PLAYLIST ROW.
#
# mpv expands a directory handed to `loadfile` only when it OPENS it — so
# `replace` does and `append`/`append-play` do not, and the folder sits in the
# playlist as a single row until playback arrives there. An album of forty
# tracks therefore got ONE ticket in `playlist-shuffle`, played in track order
# when it came up, and most of a library was never reached.
#
# Two levels down it was worse whichever verb was used: `--directory-mode`
# defaults to `auto`, which mpv's manual defines as `recursive` with `--shuffle`
# and `lazy` otherwise — so even the folder that WAS opened left the album
# folders inside it as rows of their own.
#
# ⚠ SO THE NESTING BELOW IS THE POINT. A flat folder passes on the old code for
# the `replace` case and proves nothing.

mkdir -p "$T/Library/Album A/Disc 2" "$T/Library/Album B"
cp "$SILENT" "$T/Library/Album A/01 One.wav"
cp "$SILENT" "$T/Library/Album A/02 Two.wav"
cp "$SILENT" "$T/Library/Album A/Disc 2/05 Five.wav"
cp "$SILENT" "$T/Library/Album B/03 Three.wav"
# ⚠ Artwork sits beside the tracks in every real music folder, and mpv's default
# directory filter includes images — recursion is what makes that bite.
touch "$T/Library/Album B/cover.jpg"

"$BIN" clear >/dev/null; sleep 0.3
"$BIN" "$T/Library" >/dev/null 2>&1
sleep 0.8
out=$("$BIN" --rec queue)
check "playing a folder queues every file under it, not the folder" "4" \
      "$(printf '%s\n' "$out" | grep -c '^item')"
contains "...reaching a subdirectory two levels down" "05 Five.wav" "$out"
check "...and leaving the cover art out of the queue" "0" \
      "$(printf '%s\n' "$out" | grep -c 'cover\.jpg')"

# ⛔ THE HALF THAT WAS ACTUALLY BROKEN: queueing rather than playing.
"$BIN" clear >/dev/null; sleep 0.3
"$BIN" "$SILENT" >/dev/null 2>&1
sleep 0.5
"$BIN" add "$T/Library" >/dev/null 2>&1
sleep 0.8
out=$("$BIN" --rec queue)
check "ADDING a folder queues its files too, not one row" "5" \
      "$(printf '%s\n' "$out" | grep -c '^item')"

# ⛔ AND A QUEUE BUILT BEFORE ANY OF THIS. An mpv left running by an older build,
# or a folder dropped on the mpv window itself, still holds the folder as one
# row — and an option set afterwards does not go back for entries mpv has
# already taken. Shuffle re-asks for those rows before it draws.
# ⚠ WITH NOTHING PLAYING, on purpose. A folder appended to a player that is
# mid-file gets opened by the playback that reaches it, which expands it and
# hides the row this case is about — so the case would pass for the wrong
# reason, on timing.
"$BIN" clear >/dev/null; sleep 0.5
python3 - "$SYNPLAY_HOME/syn-play.sock" "$T/Library" <<'IPC'
import json, socket, sys
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1])
f = s.makefile("rwb")
# The old spelling, on the old default: one row for the whole folder.
for cmd in (["set_property", "directory-mode", "auto"],
            ["loadfile", sys.argv[2], "append"]):
    f.write((json.dumps({"command": cmd, "request_id": 1}) + "\n").encode())
    f.flush()
    while json.loads(f.readline()).get("request_id") != 1:
        pass
IPC
sleep 0.6
out=$("$BIN" --rec queue)
# ⚠ COUNTED AS "is the FOLDER there", not as a total. `playlist-clear` keeps
# whatever mpv is currently on, so the number of other rows is mpv's business
# and not something this case should be pinned to.
check "a folder queued the old way sits in the queue as itself" "1" \
      "$(printf '%s\n' "$out" | grep -c "	$T/Library\$")"
"$BIN" shuffle >/dev/null
sleep 0.8
out=$("$BIN" --rec queue)
check "...and shuffle expands it rather than drawing it as one ticket" "0" \
      "$(printf '%s\n' "$out" | grep -c "	$T/Library\$")"
contains "...down to the file two levels inside it" "05 Five.wav" "$out"
# ⚠ DISTINCT paths: `playlist-clear` kept whatever mpv was on, which is one of
# these tracks, so a plain count would be counting that twice.
check "...with every track in the folder now a row of its own" "4" \
      "$(printf '%s\n' "$out" | grep -o "	$T/Library/.*" | sort -u | wc -l)"

# ── a queue as long as a library ────────────────────────────────────────────
#
# ⛔ THE BUG THIS EXISTS FOR: AN EMPTY QUEUE BESIDE A PLAYER THAT WAS PLAYING.
#
# The socket reader read a reply into a fixed 64 KB buffer and, when one did
# not fit, returned the TRUNCATED line as though it were whole — leaving the
# rest of it in the socket for the next read to pick up as a fresh message.
# mpv puts `request_id` at the END of a reply, so the truncated one matched
# nothing and the command then blocked on a line that was never coming.
#
# ⚠ It could not fire while a folder was ONE queue row. Expanding folders made
# `get_property playlist` proportional to somebody's music library, and the
# first thing that broke was the queue: empty, while playback and every short
# reply carried on working perfectly.
#
# ⚠ SO THE COUNT IS THE POINT. Enough entries that the reply passes 64 KB —
# a few hundred paths does it — and long names so it does so on a short
# temporary path as well as a long one.

mkdir -p "$T/Big/Tracks"
python3 - "$SILENT" "$T/Big/Tracks" <<'BIG'
import os, sys
data = open(sys.argv[1], "rb").read()
for i in range(800):
    name = "%03d A Track Name Long Enough To Fill A Buffer.wav" % i
    open(os.path.join(sys.argv[2], name), "wb").write(data)
BIG

"$BIN" clear >/dev/null; sleep 0.3
"$BIN" "$T/Big" >/dev/null 2>&1
sleep 2
out=$("$BIN" --rec queue)
check "a library-sized queue is read whole, not truncated to nothing" "800" \
      "$(printf '%s\n' "$out" | grep -c '^item')"

# ⛔ AND THE ENGINE THE WINDOW TALKS TO, WHICH IS WHERE IT WAS SEEN.
#
# A CLI verb opens a connection, asks one thing and closes it, so a desynced
# socket dies with the process and the next command looks fine. `serve` holds
# ONE connection for the life of the window — so the unread tail of a
# truncated reply was still there for the next read, and the engine wedged
# with the queue never sent. That is the report: a window showing an empty
# queue beside a player that is playing.
#
# ⚠ UNDER `timeout`, because the failure being guarded against is a HANG. A
# case that waits for a wedged engine takes the whole suite with it.
out=$(sleep 2 | timeout 30 "$BIN" serve 2>/dev/null)
# ⚠ THE FIRST BLOCK ONLY. serve re-sends the queue as playback moves, so a
# range match over two seconds counts it as many times as it was sent.
check "the window's engine sends every queue row on one connection" "800" \
      "$(printf '%s\n' "$out" | awk '/^q-begin/{f=1;next} /^q-end/{if(f)exit} f&&/^q	/{n++} END{print n+0}')"
check "...and goes on reporting state afterwards" "yes" \
      "$(printf '%s\n' "$out" | grep -q '^s	state' && echo yes || echo no)"

# Back to the three plain files the cases below are written against.
"$BIN" clear >/dev/null; sleep 0.3
"$BIN" "$SILENT" >/dev/null 2>&1
"$BIN" add "$T/Videos/Silence Two.wav" >/dev/null 2>&1
(cd "$T/Videos" && "$BIN" add "Silence Two.wav" >/dev/null 2>&1)
sleep 0.5

"$BIN" playlist save "Test set" >/dev/null
out=$("$BIN" playlist list)
contains "a saved playlist is listed" "Test set" "$out"
check "...and it is an m3u8 any player can open" "yes" \
      "$([ -f "$T/share/syn-play/playlists/Test set.m3u8" ] && echo yes || echo no)"
contains "...with #EXTINF titles in it" "#EXTINF" \
         "$(cat "$T/share/syn-play/playlists/Test set.m3u8")"

"$BIN" clear >/dev/null
sleep 0.3
"$BIN" playlist load "Test set" >/dev/null
sleep 0.5
out=$("$BIN" --rec queue)
check "loading it back gives the same number of rows" "3" \
      "$(printf '%s\n' "$out" | grep -c '^item')"

out=$("$BIN" --rec volume 55; "$BIN" --rec volume)
contains "volume is set and read back" "volume	55" "$out"

# ⚠ HISTORY IS WRITTEN BY THE LOAD, not only by the watcher — which may not be
# running. A file played once, briefly, still belongs in the list.
out=$("$BIN" history)
contains "what was played is in the history" "Silence One" "$out"

out=$("$BIN" open silence 2>&1)
contains "quick open plays the best match without a path" "Playing" "$out"

"$BIN" history clear >/dev/null
out=$("$BIN" history)
contains "history clear empties it" "nothing played yet" "$out"

# ⚠ THE TUI WITH NO TERMINAL PRINTS ONCE AND EXITS. It is how it is tested and
# what a script gets; a TUI that blocked on a pipe would hang this suite.
out=$("$BIN" tui < /dev/null)
contains "the TUI works with no terminal at all" "Queue" "$out"

"$BIN" stop >/dev/null
sleep 0.5
out=$("$BIN" --rec status)
contains "stop ends the session" "state	stopped" "$out"

# ── what the window is told with NO PLAYER RUNNING ──────────────────────────
#
# ⛔ velle, 2026-08-30: "the playlist isn't showing after creating closing and
# reopening but if i add to queue it then appears". `serve`'s no-session branch
# used to `continue`, skipping the list sends at the bottom of its loop — so a
# window opened with nothing playing drew an empty Playlists tab AND an empty
# History, and both filled in the moment something started playing.
#
# ⚠ THE ENGINE IS DRIVEN, NOT THE WINDOW. What is asserted is the records that
# reach the pipe: a window test could pass on a window that draws its own stale
# copy of a list nothing sent it.
"$BIN" stop >/dev/null 2>&1
sleep 0.5
out=$(printf 'quit\n' | timeout 20 "$BIN" serve)

contains "with no player, the engine still says so" "state	stopped" "$out"
contains "⛔ …and still sends the saved playlists" "playlist	Test set" "$out"
check "…inside a list block the window can swap in" "1" \
      "$(printf '%s\n' "$out" | grep -c '^l-begin')"
contains "⛔ …and still sends the history" "h-begin" "$out"
check "…and says the queue is empty rather than leaving the last one drawn" "1" \
      "$(printf '%s\n' "$out" | grep -c '^q-begin')"

# ⛔ "delete playlist x button isn't working" — `plrm` sat below the guard that
# needs a live mpv, so the ✕ did nothing unless something happened to be
# playing, which on a window opened to tidy playlists it never is.
out=$(printf 'plrm Test%%20set\nquit\n' | timeout 20 "$BIN" serve)
check "⛔ a playlist is deleted with no player running" "no" \
      "$([ -f "$T/share/syn-play/playlists/Test set.m3u8" ] && echo yes || echo no)"
contains "…and the window is told the list changed" "l-begin" "$out"

# ⚠ AND IT MUST NOT KILL THE ENGINE. sp_playlist_rm() die()s on a playlist that
# is not there; the window follows the engine out, so a second click on a ✕
# would have closed the whole player.
out=$(printf 'plrm Test%%20set\nplrm nothing-here\nquit\n' | timeout 20 "$BIN" serve; echo "rc=$?")
contains "⛔ deleting one that is already gone does not take the engine down" "rc=0" "$out"

# ⚠ …and nothing it prints may reach the protocol pipe as a stray record.
check "the engine emits no line the window cannot parse" "0" \
      "$(printf '%s\n' "$out" | grep -cvE '^(s|q|h|l|f|playlist|hist)\b|^(q|h|l|f)-(begin|end)$|^e$|^rc=0$|^$')"

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
