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

BIN="${1:-}"
[ -x "$BIN" ] || BIN=$(command -v syn-play)
[ -x "$BIN" ] || { echo "cli_test: no syn-play binary"; exit 1; }

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

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
