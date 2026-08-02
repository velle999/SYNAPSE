#!/bin/sh
# x11_or_move.sh — does a menu that moves after it maps actually move on screen?
#
# velle, 2026-08-02: "steam still being weird with menus", with three
# screenshots showing every Steam menu opening at the window's PREVIOUS
# position after a maximize / un-maximize.
#
# An X trace of the live desk (xspy on :0) caught the cause. Steam maps its menu
# at the position it believes its own toplevel is at, and corrects it about two
# milliseconds later:
#
#   16:31:35.658 configure 0x4000c6 or=1  1258,1170   <- first guess (stale)
#   16:31:35.660 configure 0x4000c6 or=1  1764,1444   <- correction (right)
#
# 1258,1170 is where that menu belonged while Steam was maximized at 1082,1136;
# 1764,1444 is the same point on the restored window at 1587,1410 — the same
# (505,274) delta. So the X server had the right coordinates the whole time.
# synui read the position once, in xw_map(), and listened for nothing after
# that: it kept drawing the menu at the first guess.
#
# The test is the second capture. A red unmanaged window is mapped at 100,100
# and then moved to 700,400 by the client alone — no compositor involvement,
# which is the point — and the screen is photographed either side.
#
# Usage: x11_or_move.sh /path/to/synui /path/to/x11_or_move_test
# Skips (77) without a DRM render node, grim, or python3+PIL to read the shot.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: x11_or_move.sh /path/to/synui /path/to/x11_or_move_test}
CLIENT=${2:?usage: x11_or_move.sh /path/to/synui /path/to/x11_or_move_test}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${CLIENT_PID:-}" ] && kill -9 "$CLIENT_PID" 2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
command -v grim >/dev/null 2>&1 || { echo "SKIP: grim not installed"; exit 77; }
python3 -c "import PIL" >/dev/null 2>&1 || { echo "SKIP: python3 PIL not installed"; exit 77; }

TMP=$(mktemp -d /tmp/synui-ormove.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# The welcome overlay is on by default and it is a big dimmed card in the
# middle of the screen. The window under test is measured by COLOUR, and the
# first run of this test put it half under that scrim: the covered part read as
# (40,20,36) instead of (255,0,0) and only the 10px sticking out past the card
# counted as red. Nothing to do with the bug — just turn the greeting off.
printf 'welcome_at_startup = off\n' > "$SYNUI_CONFIG"
: > "$SYNUI_WINDOWS"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    for c in "$TMP"/wayland-*; do
        case "$c" in *.lock) continue;; esac
        [ -S "$c" ] && SOCK=$(basename "$c") && break
    done
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
    i=$((i + 1)); sleep 0.1
done
[ -n "$SOCK" ] || fail "no wayland socket after 10s"
export WAYLAND_DISPLAY="$SOCK"

# Xwayland is lazy; synui logs the DISPLAY it hands to the children it spawns.
i=0
while [ $i -lt 100 ]; do
    D=$(command grep -ao 'DISPLAY=:[0-9]*' "$LOG" | head -1 | cut -d= -f2)
    [ -n "${D:-}" ] && break
    i=$((i + 1)); sleep 0.1
done
[ -n "${D:-}" ] || fail "Xwayland never announced a DISPLAY"
export DISPLAY="$D"

# Where is the red block? Prints "x1 y1 x2 y2" of its bounding box, or "none".
# Sampled every other pixel — the block is 200x150, it cannot be missed, and a
# full 1280x720 scan in python is slower than the rest of the test.
find_red() {
    python3 - "$1" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
w, h = im.size
px = im.load()
xs, ys = [], []
for y in range(0, h, 2):
    for x in range(0, w, 2):
        r, g, b = px[x, y]
        if r > 110 and r > 2 * g + 30 and r > 2 * b + 30:
            xs.append(x); ys.append(y)
print("none" if not xs else "%d %d %d %d" % (min(xs), min(ys), max(xs), max(ys)))
PY
}

"$CLIENT" >"$TMP/client.out" 2>"$TMP/client.err" &
CLIENT_PID=$!

i=0
while [ $i -lt 60 ]; do
    command grep -q '^mapped' "$TMP/client.out" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || fail "client exited: $(cat "$TMP/client.err")"
    i=$((i + 1)); sleep 0.1
done
command grep -q '^mapped' "$TMP/client.out" || fail "the client never mapped its window"
sleep 1

# ── 1. it lands where the client asked ───────────────────────────────────
grim "$TMP/before.png" 2>/dev/null || fail "grim could not capture the output"
BEFORE=$(find_red "$TMP/before.png")
[ "$BEFORE" = none ] && fail "no red block on screen at all — the unmanaged
       window never rendered, so the move this test is about cannot be measured.
       (An OR window goes to the overlay layer in xw_map.)"
set -- $BEFORE
BX=$1; BY=$2
# Placed at 100,100; the sampling grid and any border cost a pixel or two.
[ "$BX" -ge 96 ] && [ "$BX" -le 106 ] || fail "the menu mapped at x=$BX, not ~100"
[ "$BY" -ge 96 ] && [ "$BY" -le 106 ] || fail "the menu mapped at y=$BY, not ~100"
echo "map:   unmanaged window drawn at $BX,$BY as asked"

# ── 2. and follows a move the CLIENT makes afterwards ────────────────────
kill -USR1 "$CLIENT_PID" 2>/dev/null || fail "could not signal the client"
i=0
while [ $i -lt 60 ]; do
    command grep -q '^moved' "$TMP/client.out" && break
    i=$((i + 1)); sleep 0.1
done
command grep -q '^moved' "$TMP/client.out" || fail "the client never moved its window"
sleep 1

grim "$TMP/after.png" 2>/dev/null || fail "grim could not capture the output"
AFTER=$(find_red "$TMP/after.png")
[ "$AFTER" = none ] && fail "the red block vanished after the move"
set -- $AFTER
AX=$1; AY=$2
[ "$AX" -ge 696 ] && [ "$AX" -le 706 ] && [ "$AY" -ge 396 ] && [ "$AY" -le 406 ] || {
    if [ "$AX" -le 106 ] && [ "$AY" -le 106 ]; then
        fail "the window is still drawn at $AX,$AY. The client moved it to
       700,400 through the X server and the compositor kept it where it mapped:
       this is the Steam menu bug — synui read the position once in xw_map and
       listened for set_geometry nowhere, so a menu stays wherever its first
       guess put it."
    fi
    fail "after the move the block is at $AX,$AY, expected ~700,400"
}
echo "move:  it followed the client to $AX,$AY"

# ── 3. clean shutdown ────────────────────────────────────────────────────
kill -TERM "$CLIENT_PID" 2>/dev/null
CLIENT_PID=

kill -TERM "$SYNUI_PID" 2>/dev/null
i=0
while kill -0 "$SYNUI_PID" 2>/dev/null; do
    i=$((i + 1))
    [ $i -gt 50 ] && fail "synui did not exit within 5s of SIGTERM"
    sleep 0.1
done
wait "$SYNUI_PID"; RC=$?
[ $RC -eq 0 ] || fail "synui exited $RC"
SYNUI_PID=

echo "PASS: x11_or_move"
cleanup
exit 0
