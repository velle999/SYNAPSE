#!/bin/sh
# spiral_layout.sh — the fibonacci layout tiles, and does not make slivers
#
# LAYOUT_SPIRAL exists for one reason, and it is not novelty: at two or three
# windows it places the same boxes as the master-stack tiler, so if it only ever
# had to pass "the windows do not overlap" it would be indistinguishable from
# the layout it sits next to in the cycle. The difference is what happens when
# the desktop gets busy, and it is a difference of SHAPE.
#
# Master-stack puts every window after the first into one column, so each new
# window makes that column's windows shorter without making them narrower. On a
# 1280x720 screen the stack goes 502x170 at five windows, 502x134 at six,
# 502x81 at nine — 2.9:1, then 3.7:1, then 6.2:1. A letterbox slot is not a
# shape any application is designed to draw in. The spiral halves an ever
# smaller box instead, alternating the direction of the cut, so every window
# stays between about 1:1 and 2:1 no matter how many are open.
#
# That is a TRADE, and the test is written to say so rather than to flatter the
# new layout. The spiral's smallest window is *smaller* in area than
# master-stack's — 52700 px² against 85340 at five windows, because halving
# compounds where dividing a column does not. What it buys is that nothing ever
# becomes a strip. So the comparative assertion is on the worst aspect ratio,
# which is the property the layout actually improves; asserting on area would
# fail, and should.
#
# On top of that, the invariants any tiler owes:
#   * every window has a real box inside the output;
#   * no two windows overlap (a spiral that mis-computes a remainder produces
#     overlap, not a gap, so this is the sharp end of the arithmetic);
#   * nothing is below MIN_WIN.
#
# Usage: spiral_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: spiral_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: spiral_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: spiral_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    for p in ${CLIENT_PIDS:-}; do kill -9 "$p" 2>/dev/null; done
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-spiral.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# Only animation_ms, so the gaps under test are the SHIPPED defaults — a
# synuirc that set `gap` would be testing this rig's arithmetic, not synui's.
# The fades are off because every assertion here is about where the windows
# SETTLE, and a reflow mid-fade is a reflow whose result is still moving.
printf 'animation_ms = 0\n' > "$SYNUI_CONFIG"
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
CTLSOCK="$TMP/synui-$SOCK.sock"

synctl()     { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients()    { synctl clients | tr '{' '\n' | grep '"app_id"'; }
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }

# "<x> <y> <w> <h>" per window, one line each.
boxes() {
    clients | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),\(-\{0,1\}[0-9]*\)\],"size":\[\([0-9]*\),\([0-9]*\)\].*/\1 \2 \3 \4/p'
}

open_window() {
    want=$1
    "$CLIENT" 0 90 >>"$TMP/client.out" 2>>"$TMP/client.err" &
    CLIENT_PIDS="$CLIENT_PIDS $!"
    i=0
    while [ $i -lt 40 ]; do
        [ "$(clients | grep -c '"app_id":"stubborn"')" -ge "$want" ] && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "a client never mapped (wanted $want): $(cat "$TMP/client.err")"
}

OUT=$(synctl outputs | tr '{' '\n' | grep '"name"' | head -1)
OW=$(echo "$OUT" | sed -n 's/.*"size":\[\([0-9]*\),.*/\1/p')
OH=$(echo "$OUT" | sed -n 's/.*"size":\[[0-9]*,\([0-9]*\)\].*/\1/p')
[ -n "$OW" ] && [ "$OW" -gt 0 ] || fail "could not read the headless output: $OUT"
echo "output:   ${OW}x${OH}"

# The WORST aspect ratio on screen, as a percentage so this stays in integer
# arithmetic that `test` can compare — 295 is a 2.95:1 letterbox. Always taken
# the long way round (the larger of w/h and h/w), so a window that is far taller
# than it is wide counts as just as bad as one that is far wider than it is
# tall: both are the same failure, and a portrait monitor produces the second.
worst_ratio() {
    boxes | awk '
    BEGIN { m = 0 }
    {
      r = ($3 > $4) ? $3 / $4 : $4 / $3
      if (r > m) m = r
    }
    END { printf "%d\n", m * 100 }'
}

# Does any pair of windows overlap? Prints the offending pair, or nothing.
# O(n²) over five windows is free, and a nested loop states the invariant more
# plainly than anything cleverer would.
overlaps() {
    boxes | awk '
    { x[NR]=$1; y[NR]=$2; w[NR]=$3; h[NR]=$4; n=NR }
    END {
      for (i = 1; i <= n; i++)
        for (j = i+1; j <= n; j++)
          if (x[i] < x[j]+w[j] && x[j] < x[i]+w[i] &&
              y[i] < y[j]+h[j] && y[j] < y[i]+h[i])
            printf "window %d (%d,%d %dx%d) overlaps window %d (%d,%d %dx%d)\n",
                   i, x[i], y[i], w[i], h[i], j, x[j], y[j], w[j], h[j]
    }'
}

# Every window has a real box, wholly inside the output, no smaller than MIN_WIN.
assert_sane() {
    what=$1
    boxes | while read -r bx by bw bh; do
        { [ "$bw" -ge 40 ] && [ "$bh" -ge 40 ]; } \
            || { echo "$what: a window is ${bw}x${bh}, under the 40px MIN_WIN floor" >&2; exit 1; }
        { [ "$bx" -ge 0 ] && [ "$by" -ge 0 ] \
          && [ $((bx + bw)) -le "$OW" ] && [ $((by + bh)) -le "$OH" ]; } \
            || { echo "$what: a window's box ${bx},${by} ${bw}x${bh} is not inside the ${OW}x${OH} output" >&2; exit 1; }
    done || fail "$what: see above.
$(boxes)"
}

# ── 1. five windows on the master-stack tiler: the baseline ──────────────
[ "$(layout_now)" = tiling ] || fail "expected tiling at startup, got $(layout_now)"
open_window 1
open_window 2
open_window 3
open_window 4
open_window 5
[ "$(boxes | wc -l)" = 5 ] || fail "expected 5 windows, got $(boxes | wc -l):
$(boxes)"

TILE_RATIO=$(worst_ratio)
assert_sane "tiling"
echo "tiling:   worst aspect ratio is $((TILE_RATIO / 100)).$((TILE_RATIO % 100)):1"

# ── 2. the same five, spiralled ──────────────────────────────────────────
# Five presses: tiling -> floating -> monocle -> AI -> niri -> spiral. Spelled
# out rather than derived, for the same reason retile.sh spells its six out —
# it is also the assertion that nothing has been slotted into the middle of
# syn_layout_t, which would renumber layouts.state under every desktop.
for _ in 1 2 3 4 5; do synctl dispatch layout_cycle >/dev/null; done
[ "$(layout_now)" = spiral ] || fail "five layout_cycles did not reach spiral, got
       $(layout_now). The cycle order is the syn_layout_t order: tiling,
       floating, monocle, AI, niri, spiral."

[ "$(boxes | wc -l)" = 5 ] || fail "the spiral lost a window: $(boxes | wc -l) of 5
$(boxes)"

SPIRAL_RATIO=$(worst_ratio)
assert_sane "spiral"
echo "spiral:   worst aspect ratio is $((SPIRAL_RATIO / 100)).$((SPIRAL_RATIO % 100)):1"

# ── 3. no overlap ────────────────────────────────────────────────────────
# The sharp end. Every step of the spiral hands the NEXT step a remainder, so an
# off-by-one in a half or a gap does not leave a crack — it double-books pixels,
# and two clients render on top of each other.
OVER=$(overlaps)
[ -z "$OVER" ] || fail "the spiral overlaps itself — a remainder is wrong:
$OVER"
echo "tile:     five windows, no two overlap"

# ── 4. the point of the layout ───────────────────────────────────────────
[ "$SPIRAL_RATIO" -lt "$TILE_RATIO" ] || fail "the spiral's worst aspect ratio
       ($SPIRAL_RATIO/100 : 1) is no better than the master-stack tiler's
       ($TILE_RATIO/100 : 1). Keeping every window near square is the one thing
       this layout is for \u2014 it gives up smallest-window AREA to get it. If it
       is not winning here it has degenerated into a stack: check the MIN_WIN
       bail-out in layout_spiral, which falls back to exactly that on purpose
       when a half gets too small, and which five windows on this output should
       be nowhere near."
# Also assert the trade is real rather than free, so nobody "optimises" the
# layout back into a stack and passes both halves by accident.
[ "$TILE_RATIO" -ge 250 ] || fail "master-stack's worst ratio is only
       $TILE_RATIO/100 : 1 at five windows, so this comparison has no teeth.
       Either the output got much taller or the master factor moved."
echo "spiral:   beats master-stack on shape ($SPIRAL_RATIO < $TILE_RATIO, hundredths)"

# ── 5. clean shutdown ────────────────────────────────────────────────────
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

echo "PASS: the spiral tiles without overlap and keeps every window near square"
cleanup
exit 0
