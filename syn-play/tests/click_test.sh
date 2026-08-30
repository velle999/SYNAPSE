#!/bin/bash
# click_test.sh — the inline links in a list row, and whether a click can reach
# them at all.
#
# ⛔ THIS IS THE ONE A SCREENSHOT AND A GREP BOTH PASS. The ✕ on a playlist was
# drawn in the right place, lit up on hover, had a MouseArea over it with the
# right verb in it, and sent `plload` — because the row-wide MouseArea was
# declared AFTER it and a later sibling is stacked on top. Every part read
# correctly. The button was hit and the wrong thing happened, in silence, in all
# four of these lists at once.
#
# ⚠ WHAT THIS DRIVES, AND WHAT IT CANNOT. A synthetic pointer needs a
# compositor, so hover cannot be produced here and neither can a real press:
#
#   - The STACKING is driven, against the shipped file's real geometry.
#     `childAt()` answers with the topmost child at a point, which is the same
#     question event delivery asks first, and it is the whole of this defect.
#     Run against the code before the fix it names the row instead of the link.
#   - The link is forced visible, because `visible: <row>Area.containsMouse`
#     needs a pointer this cannot supply. That is the state a person clicking in
#     is already in — the ✕ is only reachable while it is showing.
#   - Then the link is clicked and asserted against the FILESYSTEM, not against
#     a QML property: the playlist is gone, and the OTHER one is still there.
#
# ⚠ Its own SYNPLAY_HOME and socket, no window, same rules as the other suites.
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

T=$(mktemp -d)
export SYNPLAY_HOME="$T" HOME="$T" MPV_HOME="$T/mpv" XDG_RUNTIME_DIR="$T/run"
mkdir -p "$T/mpv" "$T/run" "$T/share/syn-play/playlists" "$T/Videos"
printf 'ao=null\nvo=null\nreally-quiet=yes\n' > "$T/mpv/mpv.conf"
trap '"$BIN" stop >/dev/null 2>&1; sleep 0.3; rm -rf "$T"' EXIT

PL="$T/share/syn-play/playlists"
printf '#EXTM3U\n/tmp/one.wav\n'  > "$PL/Alpha.m3u8"
printf '#EXTM3U\n/tmp/two.wav\n'  > "$PL/Beta.m3u8"

# History is a plain TSV — epoch, pos, dur, path, title, the last two
# percent-encoded. Written directly so the History and Quick open lists have
# rows without a player ever running; `find` searches history first, so one file
# fills both.
printf '1756500000\t0.000\t0.000\t%%2Ftmp%%2Fone.wav\tAlpha%%20Film\n' \
    > "$T/share/syn-play/history.tsv"

# The probe: the SHIPPED file with a driver appended, as drop_test.sh does. A
# replica of a delegate is a second layout, and the one that goes wrong is
# always the one nobody copied.
probe() {  # probe <js once the window is up> [js a beat later]
    awk -v js="$1" -v late="${2:-}" 'BEGIN{RS="\0"} {
        n = match($0, /}[ \t\r\n]*$/)
        printf "%s\n", substr($0,1,n-1)
        printf "    QtObject {\n"
        printf "        id: hit\n"
        printf "        // The link in row <idx> of <view> whose text is <label>.\n"
        printf "        function link(view, idx, label) {\n"
        printf "            const row = view.itemAtIndex(idx)\n"
        printf "            if (!row) return null\n"
        printf "            for (let i = 0; i < row.children.length; i++)\n"
        printf "                if (row.children[i].text === label) return row.children[i]\n"
        printf "            return null\n"
        printf "        }\n"
        printf "        // What a press at the middle of that link would land on.\n"
        printf "        function at(view, idx, label) {\n"
        printf "            const row = view.itemAtIndex(idx)\n"
        printf "            const it = hit.link(view, idx, label)\n"
        printf "            if (!row) return \"no-such-row\"\n"
        printf "            if (!it) return \"no-such-link\"\n"
        printf "            it.visible = true\n"
        printf "            const p = row.mapFromItem(it, it.width / 2, it.height / 2)\n"
        printf "            const got = row.childAt(p.x, p.y)\n"
        printf "            return got === it ? \"the link\"\n"
        printf "                 : got === null ? \"nothing\" : \"the row underneath\"\n"
        printf "        }\n"
        printf "        // ⚠ EMITS, so it BYPASSES hit testing on purpose: this says the\n"
        printf "        // verb behind the link is the right one, and means something only\n"
        printf "        // beside at(), which says a press could get there.\n"
        printf "        function press(view, idx, label) {\n"
        printf "            const it = hit.link(view, idx, label)\n"
        printf "            if (!it) return false\n"
        printf "            it.visible = true\n"
        printf "            it.children[0].clicked(null)\n"
        printf "            return true\n"
        printf "        }\n"
        printf "    }\n"
        printf "    Timer {\n"
        printf "        running: true; interval: 1600; repeat: false\n"
        printf "        onTriggered: { root.width = 700; root.height = 700; %s }\n", js
        printf "    }\n"
        printf "    Timer {\n"
        printf "        running: %s; interval: 2500; repeat: false\n", late == "" ? "false" : "true"
        printf "        onTriggered: { %s; done.start() }\n", late == "" ? "" : late
        printf "    }\n"
        printf "    Timer { id: done; interval: 2200; repeat: false; onTriggered: Qt.quit() }\n"
        printf "%s", substr($0,n)
    }' "$QML" > "$T/probe.qml"

    SYNPLAY_BIN="$BIN" QT_QPA_PLATFORM=offscreen GSETTINGS_BACKEND=memory \
    QT_ASSUME_STDERR_HAS_CONSOLE=1 timeout 40 quickshell -p "$T/probe.qml" 2>&1
}

field() { sed -n "s/.*$1=\(.*\)$/\1/p" <<<"$2" | head -1; }

echo "syn-play — a click on an inline link in a row"

# ── every list, because one declaration order broke all four at once ────────
out=$(probe 'root.tab = 2;
             console.log("PLDEL=" + hit.at(plView, 0, "✕"));
             console.log("PLADD=" + hit.at(plView, 0, "queue"));
             root.tab = 1;
             console.log("HIST=" + hit.at(histView, 0, "queue"));
             done.start()')

check "the window loads with no QML errors" "0" \
      "$(grep -cE 'ReferenceError|TypeError|Cannot assign|is not a type' <<<"$out")"
check "the playlists list has rows to click" "0" \
      "$(grep -c 'no-such-row' <<<"$out")"
# ⛔ THE BUG, NAMED. Before the fix each of these answered "the row underneath".
check "⛔ a press on a playlist's ✕ lands on the ✕, not the row" \
      "the link" "$(field PLDEL "$out")"
check "...and a press on its queue link lands on the link" \
      "the link" "$(field PLADD "$out")"
check "...and the same in the history list" "the link" "$(field HIST "$out")"

# ⚠ TYPED IN, AND READ A BEAT LATER. The results are an overlay gated on the
# field having text, and the list behind it is a ROUND TRIP through the engine —
# so the query goes in the way a person puts it in, and is read on a later tick.
# ⛔ `childAt` answers about EFFECTIVE visibility: sending `find` without typing
# leaves the overlay hidden and every row in it unhittable, which is not the
# defect being looked for.
out=$(probe 'openField.text = "Alpha"' \
            'console.log("FIND=" + hit.at(resultView, 0, "queue"))')
check "...and in the Quick open results" "the link" "$(field FIND "$out")"

# ── and it deletes, asserted against the filesystem ─────────────────────────
out=$(probe 'root.tab = 2;
             console.log("PRESSED=" + hit.press(plView, 0, "✕"));
             done.start()')
check "the ✕ was found and pressed" "true" "$(field PRESSED "$out")"
check "⛔ the playlist it was on is GONE from disk" "0" \
      "$([ -e "$PL/Alpha.m3u8" ] && echo 1 || echo 0)"
check "...and the other one is untouched" "1" \
      "$([ -e "$PL/Beta.m3u8" ] && echo 1 || echo 0)"

# ── the queue's ✕, which needs a player to have a row at all ────────────────
if command -v mpv >/dev/null 2>&1; then
    python3 - "$T/Videos" <<'PY'
import struct, os, sys
n, rate = 8000 * 30, 8000
hdr = (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
       struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8) +
       b"data" + struct.pack("<I", n))
open(os.path.join(sys.argv[1], "Reel.wav"), "wb").write(hdr + b"\x80" * n)
PY
    "$BIN" "$T/Videos/Reel.wav" >/dev/null 2>&1
    sleep 1
    out=$(probe 'root.tab = 0; console.log("QDROP=" + hit.at(queueView, 0, "✕")); done.start()')
    check "a press on a queue row's ✕ lands on the ✕, not the row" \
          "the link" "$(field QDROP "$out")"
else
    echo "  skip  mpv not installed — the queue row's ✕ is not checked"
fi

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
