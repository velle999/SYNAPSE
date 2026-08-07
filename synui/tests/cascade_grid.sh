#!/bin/sh
# cascade_grid.sh — cascade deals SMALL cards over the WHOLE desk
#
# The bug (velle, 2026-08-07, with a screenshot): six windows on a 2560x1440
# desktop came out as two piles of three, each card most of the height and half
# the width, with the right third of the screen and the bottom third of it bare.
# "cascade should be smaller tiles." The first cut of the layout dealt one ROW
# of piles across the screen, so the card size fell out of the pile count — and
# at six windows the pile count is two.
#
# It is the other way round now. A card is capped at CASCADE_CARD_W_PCT x
# CASCADE_CARD_H_PCT of the working box (a third wide, half tall), those two
# numbers give the grid three columns and two rows, and windows fill the SLOTS
# before any pile grows deep. Six windows is six small cards over the whole
# desk; twelve is six piles of two.
#
# The two assertions are the two halves of the complaint, and a layout can fail
# either one on its own:
#
#   * SMALL — no card is more than 40% of the output wide or 60% of it tall.
#     Generous against the 33/50 cap so the outer gap and the usable box are not
#     being re-derived here, and still nowhere near the 46% x 92% cards the
#     screenshot was of.
#   * THE WHOLE DESK — the arrangement reaches within a tenth of the right edge
#     and within a seventh of the bottom one. Small cards that all huddle in the
#     top-left corner would pass the first assertion and be a worse bug.
#
# Phase 2 is the other end of the trade: past the six slots the piles get deep
# again, so twelve windows still overlap. Cascade without overlap is a tiler.
#
# Usage: cascade_grid.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: cascade_grid.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: cascade_grid.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: cascade_grid.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-cascade.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# Only animation_ms, so the gaps and the pile cap under test are the SHIPPED
# defaults. The fades are off because every assertion here is about where the
# cards SETTLE, and a reflow read mid-fade is a box still in flight.
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

# The biggest card on screen, and how far the arrangement reaches.
max_w()    { boxes | awk 'BEGIN{m=0} $3>m{m=$3} END{print m}'; }
max_h()    { boxes | awk 'BEGIN{m=0} $4>m{m=$4} END{print m}'; }
right()    { boxes | awk 'BEGIN{m=0} $1+$3>m{m=$1+$3} END{print m}'; }
bottom()   { boxes | awk 'BEGIN{m=0} $2+$4>m{m=$2+$4} END{print m}'; }

# How many pairs of windows overlap. Zero at six windows (one card per slot),
# non-zero at twelve (the piles are two deep) — the same function reads both.
overlaps() {
    boxes | awk '
    { x[NR]=$1; y[NR]=$2; w[NR]=$3; h[NR]=$4; n=NR }
    END {
      c = 0
      for (i = 1; i <= n; i++)
        for (j = i+1; j <= n; j++)
          if (x[i] < x[j]+w[j] && x[j] < x[i]+w[i] &&
              y[i] < y[j]+h[j] && y[j] < y[i]+h[i]) c++
      print c
    }'
}

# ── 1. six windows, cascaded ─────────────────────────────────────────────
[ "$(layout_now)" = tiling ] || fail "expected tiling at startup, got $(layout_now)"
open_window 1
open_window 2
open_window 3
open_window 4
open_window 5
open_window 6

# Straight there, from the tiling desktop it started on — that the `cascade`
# action switches from any layout is retile.sh's phase 7, not this file's.
synctl dispatch cascade >/dev/null
[ "$(layout_now)" = cascade ] || fail "dispatch cascade left the layout at
       $(layout_now)"
[ "$(boxes | wc -l)" = 6 ] || fail "expected 6 windows, got $(boxes | wc -l):
$(boxes)"

CW=$(max_w); CH=$(max_h)
echo "cascade:  6 windows, biggest card ${CW}x${CH}"

[ $((CW * 100 / OW)) -le 40 ] || fail "a cascade card is ${CW}px wide on a ${OW}px
       output — $((CW * 100 / OW))% of it, over the 40% this asserts. Cards are
       capped at CASCADE_CARD_W_PCT of the WORKING box; a card this wide means
       the cap is not being applied and the pile count is setting the card size
       again, which is the two-half-screen-slabs bug.
$(boxes)"
[ $((CH * 100 / OH)) -le 60 ] || fail "a cascade card is ${CH}px tall on a ${OH}px
       output — $((CH * 100 / OH))% of it, over the 60% this asserts. See
       CASCADE_CARD_H_PCT: a pile is half the working box tall, not all of it.
$(boxes)"

R=$(right); B=$(bottom)
[ $((R * 100 / OW)) -ge 90 ] || fail "the cascade stops at x=$R on a ${OW}px output,
       leaving $((100 - R * 100 / OW))% of the desk bare. Small cards that all sit
       in one corner are a worse bug than big ones: the piles are dealt into a
       GRID that fills the working box.
$(boxes)"
[ $((B * 100 / OH)) -ge 85 ] || fail "the cascade stops at y=$B on a ${OH}px output,
       leaving the bottom $((100 - B * 100 / OH))% bare. Six windows should be two
       rows of piles, not one.
$(boxes)"
echo "spread:   reaches ${R}/${OW} across and ${B}/${OH} down"

# Six windows fill the six slots one card each, so nothing overlaps yet. This is
# an observation of the arrangement, not a rule cascade owes — phase 2 is where
# the overlap has to come back.
[ "$(overlaps)" = 0 ] || fail "six windows on six pile slots overlap $(overlaps)
       times; each should have a slot to itself.
$(boxes)"
echo "slots:    one card per slot, no overlap at six"

# ── 2. twelve windows: the piles get deep, the cards do not get big ──────
# THE SPLIT IS STILL THE FEATURE. Past the slots a cascade has to stack, or the
# cards shrink to slivers as the desk fills. Twelve windows is six piles of two.
open_window 7
open_window 8
open_window 9
open_window 10
open_window 11
open_window 12
[ "$(boxes | wc -l)" = 12 ] || fail "expected 12 windows, got $(boxes | wc -l)"

CW2=$(max_w); CH2=$(max_h)
echo "deep:     12 windows, biggest card ${CW2}x${CH2}"

[ $((CW2 * 100 / OW)) -le 40 ] && [ $((CH2 * 100 / OH)) -le 60 ] \
    || fail "twelve windows made the cards BIGGER (${CW2}x${CH2} on ${OW}x${OH}).
       The cap does not depend on the window count.
$(boxes)"
[ "$(overlaps)" -gt 0 ] || fail "twelve windows on six pile slots do not overlap at
       all. A cascade whose cards never overlap is a tiler with a worse name —
       past the slots the piles are supposed to deepen.
$(boxes)"
echo "pile:     twelve windows overlap $(overlaps) times — the piles deepened"

# ── 3. clean shutdown ────────────────────────────────────────────────────
for p in $CLIENT_PIDS; do kill -TERM "$p" 2>/dev/null; done
CLIENT_PIDS=

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

echo "PASS: cascade_grid"
cleanup
exit 0
