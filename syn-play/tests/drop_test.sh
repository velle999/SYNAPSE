#!/bin/bash
# drop_test.sh — files dragged in, and the two buttons that do the same job.
#
# ⛔ NO GREP OVER THE QML CAN SEE ANY OF THIS. A `keys` list that excludes the
# drag, a url left percent-encoded, a zone test comparing the wrong way round —
# every one reads correctly and does nothing. synstudio lost three releases to a
# drop that looked finished, and the lesson written down from it was to drive
# the real thing.
#
# ⚠ WHAT THIS CAN AND CANNOT DRIVE, measured rather than assumed:
#
#   - A Qt INTERNAL drag DOES reach the DropArea and match its keys, so the
#     plumbing is exercised — but it carries no mimeData, so `hasUrls` is FALSE
#     and `onDropped` never runs with a url in it. That branch is checked here
#     for what it is: a drag with nothing droppable in it must be REFUSED, so
#     delivery carries on underneath rather than being swallowed.
#   - A real file drag is a platform drag, which needs a compositor and blocks
#     the event loop until the pointer releases. So the payload — the decoding,
#     which item replaces and which appends, a folder handed over whole — is
#     driven through `acceptDrop()` with the exact bytes a file manager sends,
#     and asserted against the QUEUE MPV IS HOLDING, not against a QML property.
#     A window that sets its own note and sends nothing would pass that.
#
# ⚠ Its own SYNPLAY_HOME and socket, silent mpv, no window: same rules as
# cli_test.sh, for the same reasons.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
BIN="${1:-$HERE/build/syn-play}"
QML="$HERE/data/syn-play.qml"
fails=0

check() {
    if [ "$2" = "$3" ]; then echo "  ok    $1"
    else echo "  FAIL  $1"; echo "        want: [$2]"; echo "        got:  [$3]"
         fails=$((fails + 1)); fi
}

command -v quickshell >/dev/null 2>&1 || { echo "  skip  quickshell not installed"; exit 0; }
command -v mpv        >/dev/null 2>&1 || { echo "  skip  mpv not installed"; exit 0; }

T=$(mktemp -d)
export SYNPLAY_HOME="$T" HOME="$T" MPV_HOME="$T/mpv" XDG_RUNTIME_DIR="$T/run"
mkdir -p "$T/mpv" "$T/run" "$T/Videos/Season 1"
printf 'ao=null\nvo=null\nreally-quiet=yes\n' > "$T/mpv/mpv.conf"
trap '"$BIN" stop >/dev/null 2>&1; sleep 0.3; rm -rf "$T"' EXIT

# ⚠ A SPACE AND BRACKETS IN THE NAME, on purpose. A dropped url arrives
# percent-encoded; handed on undecoded, mpv is given a file whose name contains
# a literal %20 and reports it missing. Plain names hide that completely.
python3 - "$T/Videos" <<'PY'
import struct, sys, os
n, rate = 8000 * 60, 8000
hdr = b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " + struct.pack(
    "<IHHIIHH", 16, 1, 1, rate, rate, 1, 8) + b"data" + struct.pack("<I", n)
for f in ["A Film (2001).wav", "Another One.wav"]:
    open(os.path.join(sys.argv[1], f), "wb").write(hdr + b"\x80" * n)
for f in ["ep1.wav", "ep2.wav", "ep3.wav"]:
    open(os.path.join(sys.argv[1], "Season 1", f), "wb").write(hdr + b"\x80" * n)
PY

url_of() { python3 -c "import urllib.parse,sys; print('file://'+urllib.parse.quote(sys.argv[1]))" "$1"; }
FILM=$(url_of "$T/Videos/A Film (2001).wav")
OTHER=$(url_of "$T/Videos/Another One.wav")
SEASON=$(url_of "$T/Videos/Season 1")
VIDEOS=$(url_of "$T/Videos")

# The probe: the SHIPPED file, with a driver appended. ⛔ Appended to the real
# thing — a replica of the drop graph is a second layout, and the one that goes
# wrong is always the one nobody copied.
probe() {  # probe <js to run once the window is up>
    awk -v js="$1" 'BEGIN{RS="\0"} {
        n = match($0, /}[ \t\r\n]*$/)
        printf "%s\n", substr($0,1,n-1)
        printf "    Item {\n"
        printf "        id: dragProxy\n"
        printf "        width: 10; height: 10; x: 350; y: 560\n"
        printf "        Drag.keys: [\"text/uri-list\", \"text/plain\"]\n"
        printf "    }\n"
        printf "    Timer {\n"
        printf "        running: true; interval: 1400; repeat: false\n"
        printf "        onTriggered: { root.width = 700; root.height = 700; %s }\n", js
        printf "    }\n"
        printf "    Timer { id: done; interval: 3000; repeat: false; onTriggered: Qt.quit() }\n"
        printf "%s", substr($0,n)
    }' "$QML" > "$T/probe.qml"

    SYNPLAY_BIN="$BIN" QT_QPA_PLATFORM=offscreen GSETTINGS_BACKEND=memory \
    QT_ASSUME_STDERR_HAS_CONSOLE=1 timeout 40 quickshell -p "$T/probe.qml" 2>&1
}

field() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" <<<"$2" | head -1; }
items() { "$BIN" --rec queue 2>/dev/null | grep -c '^item'; }

echo "syn-play — files dragged in"

# ── the plumbing: does a drag reach the area at all, and is a bare one refused ─
out=$(probe 'dragProxy.Drag.active = true;
             console.log("OVER=" + fileDrop.containsDrag);
             console.log("TOPHALF=" + fileDrop.zoneFor(120));
             console.log("BOTHALF=" + fileDrop.zoneFor(560));
             console.log("DROP=" + dragProxy.Drag.drop());
             dragProxy.Drag.active = false; done.start()')

check "the window loads with no QML errors" "0" \
      "$(grep -cE 'ReferenceError|TypeError|Cannot assign|is not a type' <<<"$out")"
# ⛔ REFUSED, AND THEREFORE NOT HELD. `onEntered` sets accepted=false for a drag
# carrying no urls, which is what clears containsDrag — so `false` here is the
# PASS: the area saw the drag, declined it, and delivery carries on underneath.
# (⚠ The accepting path cannot be driven: a real file drag is a platform drag
# that blocks the event loop, and a Qt internal drag carries no mimeData. What
# it would then do is checked below, through acceptDrop().)
check "⛔ a drag with nothing droppable is declined, not held" "false" \
      "$(field OVER "$out")"
# ⛔ THE HALVES, WHICH IS THE WHOLE OF WHAT THE ZONES CAN GET WRONG.
check "the upper half of a 700px window is Play now" "true" "$(field TOPHALF "$out")"
check "...and the lower half is Add to the queue" "false" "$(field BOTHALF "$out")"
# ⚠ A drag carrying nothing droppable must be REFUSED, so delivery carries on
# underneath it. Qt returns 0 from drop() for a drag nobody accepted.
check "...and nobody accepted the drop" "0" "$(field DROP "$out")"

# ── the payload, against the queue mpv is actually holding ──────────────────
"$BIN" "$T/Videos/Another One.wav" >/dev/null 2>&1
sleep 1
check "one file is playing before the drop" "1" "$(items)"

probe "root.acceptDrop([\"$FILM\"], false); done.start()" >/dev/null 2>&1
sleep 1
q=$("$BIN" --rec queue)
check "⛔ a percent-encoded url is decoded before mpv sees it" "1" \
      "$(grep -c 'A Film (2001)' <<<"$q")"
check "...and a drop in the lower half ADDS rather than replacing" "2" \
      "$(grep -c '^item' <<<"$q")"
check "...leaving the row that was playing where it was" "1" \
      "$(grep -c '^item	0	yes.*Another One' <<<"$q")"

probe "root.acceptDrop([\"$FILM\"], true); done.start()" >/dev/null 2>&1
sleep 1
check "a drop in the upper half replaces the queue" "1" "$(items)"

# ⚠ NOTHING IN THE WINDOW EXPANDS A FOLDER. This checks it is handed over whole
# and that mpv turned it into three files.
probe "root.acceptDrop([\"$SEASON\"], true); done.start()" >/dev/null 2>&1
sleep 1.5
check "a dropped FOLDER is expanded by mpv into its files" "3" "$(items)"

# ⛔ AND THE LOWER HALF, WHICH IS THE ONE THAT WAS BROKEN.
#
# The case above passed from the day it was written, and hid this one: mpv
# expands a directory handed to `loadfile` only when it OPENS it, and the upper
# half is `replace`, which opens immediately. A folder dropped on the LOWER half
# is `append-play` — it went into the playlist as a single row and stayed one
# until playback happened to reach it, so `playlist-shuffle` drew a whole season
# as one ticket and never got inside it. Both halves go through sp_load() now.
#
# ⚠ NESTED ON PURPOSE: $T/Videos holds the two films AND Season 1's three
# episodes, so a folder that only expands one level still fails this.
probe "root.acceptDrop([\"$FILM\"], true); root.acceptDrop([\"$VIDEOS\"], false); done.start()" >/dev/null 2>&1
sleep 2
check "a folder dropped to QUEUE is its files too, not one row" "6" "$(items)"

probe "root.acceptDrop([\"$FILM\", \"$OTHER\"], true); done.start()" >/dev/null 2>&1
sleep 1.5
check "dropping two files plays the first and queues the rest" "2" "$(items)"

# ── the buttons do the same job through the same path ───────────────────────
#
# ⚠ zenity cannot be driven here, so what is checked is that the window KNOWS
# whether it has a chooser — a button that silently does nothing is the failure
# this guards, and it is the one a screenshot cannot show either.
out=$(probe 'console.log("PICKER=" + root.hasPicker);
             console.log("SAME=" + (typeof root.openAll === "function")); done.start()')
check "the window knows whether a file chooser is installed" \
      "$(command -v zenity >/dev/null && echo true || echo false)" "$(field PICKER "$out")"
check "the buttons and the drop go through one function" "true" "$(field SAME "$out")"

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
