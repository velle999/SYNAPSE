#!/bin/sh
# niri_slide.sh — the niri strip GLIDES to its new scroll offset
#
# niri's signature is not that the strip scrolls, it is that you can see it
# scroll: the columns slide and you keep your bearings on a workspace wider than
# the monitor. synui landed each column instantly, so the screen simply
# contained different windows afterwards.
#
# This is the one geometry animation wlr_scene can carry, and the reason is
# worth restating because anim.c's header rules geometry animation out in
# general. It rules out animating SIZE — that re-configures the client every
# frame. A scroll changes no window's size at all: every column keeps its width
# and height for the whole slide and only x moves, so it is driven by moving
# scene nodes and costs zero client round trips.
#
# WHAT THIS TEST CAN AND CANNOT SEE. It cannot look at pixels. What it can do is
# poll `synctl clients` while the strip is moving and catch a window at an x
# that is neither where it started nor where it ends up — which is exactly the
# thing that was missing before, and cannot happen if the strip teleports.
#
#   1. animation_ms = 0   — the control: the strip lands in ONE step, so the
#                           test can tell "moved gradually" from "moved at all"
#   2. animation_ms > 0   — an intermediate position is observed, and
#   3.                    — the strip still ARRIVES (a slide that stalls
#                           half-way is worse than no slide)
#
# The timing is deliberately generous: a 600ms slide against synctl round trips
# measured in single-digit milliseconds. This is a "did it pass through the
# middle" test, not a frame-timing one.
#
# Usage: niri_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: niri_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: niri_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: niri_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-slide.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

synctl()     { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients()    { synctl clients | tr '{' '\n' | grep '"app_id"'; }
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }
# Every window's x, in strip order, on one line. The strip's position, as a
# single comparable string.
xs()         { clients | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),.*/\1/p' | tr '\n' ' '; }

start_synui() {
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
}

stop_synui() {
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

kill_clients() {
    for p in $CLIENT_PIDS; do kill -9 "$p" 2>/dev/null; done
    CLIENT_PIDS=
}

# Wait until the strip stops moving, so a phase never starts measuring from the
# middle of the previous phase's slide.
settle() {
    _i=0
    while [ $_i -lt 40 ]; do
        _a=$(xs); sleep 0.1; _b=$(xs)
        [ "$_a" = "$_b" ] && return 0
        _i=$((_i + 1))
    done
    fail "the strip never came to rest"
}

# Three windows on a niri desktop, so the strip is longer than the monitor and
# there is somewhere to scroll TO. Two of the three 628px columns fit on the
# 1264px viewport at a time, so the strip has exactly two resting places: 0
# (columns 1 and 2) and 636 (columns 2 and 3).
setup_strip() {
    # Cycle round to niri rather than counting presses from tiling. Phase 2
    # restarts the compositor on the same XDG_CONFIG_HOME, and the layout is
    # remembered across restarts (layouts.state) — so the second run comes back
    # up ALREADY on niri and a fixed number of presses would walk straight past
    # it. Which press reaches niri is niri_strip.sh's assertion; this test only
    # needs to be standing on it.
    _i=0
    while [ "$(layout_now)" != niri ]; do
        synctl dispatch layout_cycle >/dev/null
        _i=$((_i + 1))
        [ $_i -gt 8 ] && fail "cycled 8 times without reaching niri — the layout
       is stuck on $(layout_now)"
    done
    open_window 1
    open_window 2
    open_window 3
    [ "$(clients | wc -l)" = 3 ] || fail "expected 3 windows, got $(clients | wc -l)"
    settle
}

# Poll the strip's position for up to $1 iterations, printing every DISTINCT
# position seen, one per line — the first line is where it started.
#
# TWO focus_prev presses, not one, and that is the whole subtlety of this test.
# The strip only scrolls when the focus lands on a column that is not already on
# screen. Focus starts on column 3 with the strip at 636, where column 2 is
# ALREADY visible — so a single focus_prev moves the keyboard and nothing else,
# and the test would be watching a strip that had no reason to move. The second
# press reaches column 1, which is off the left edge, and that is the scroll.
watch_scroll() {
    _budget=$1
    _seen=$(xs)
    printf '%s\n' "$_seen"
    synctl dispatch focus_prev >/dev/null
    synctl dispatch focus_prev >/dev/null
    _i=0
    while [ "$_i" -lt "$_budget" ]; do
        _now=$(xs)
        if [ "$_now" != "$_seen" ]; then
            printf '%s\n' "$_now"
            _seen=$_now
        fi
        _i=$((_i + 1))
    done
}

# ── 1. animations OFF: the strip lands in one step ───────────────────────
# The control. Without it, "we saw two positions" would not distinguish a slide
# from a plain scroll observed before and after — this establishes that the
# no-animation path produces exactly one transition, so any extra position in
# phase 2 is the animation and nothing else.
printf 'animation_ms = 0\n' > "$SYNUI_CONFIG"
start_synui
setup_strip

# Captured ONCE. watch_scroll moves the focus, so calling it again to build an
# error message would be reporting a different run from the one that failed.
INSTANT=$(watch_scroll 400)
STEPS=$(printf '%s\n' "$INSTANT" | wc -l)
echo "instant:  the strip took $STEPS position(s) to move"
[ "$STEPS" = 2 ] || fail "with animation_ms = 0 the strip passed through $STEPS
       positions, not the 2 (before, after) a jump produces. Either the
       animation is running when it was turned off — niri_scroll_to is supposed
       to snap strip_scroll straight to the target and clear strip_sliding — or
       the strip did not move at all and this rig is not testing anything.
$INSTANT"

kill_clients
stop_synui

# ── 2. animations ON: it is observed part-way ────────────────────────────
printf 'animation_ms = 600\n' > "$SYNUI_CONFIG"
start_synui
setup_strip

POSITIONS=$(watch_scroll 4000)
STEPS=$(printf '%s\n' "$POSITIONS" | wc -l)
echo "slide:    the strip was seen at $STEPS distinct positions"
[ "$STEPS" -gt 2 ] || fail "with animation_ms = 600 the strip was still only
       seen at $STEPS positions — it teleported. layout_scroll_tick is supposed
       to advance strip_scroll a frame at a time and re-place the columns with
       view_move; check that output_frame actually calls it, and that
       output_frame is being scheduled while a slide is in flight (the tick
       returns true for exactly that reason).
$POSITIONS"

# ── 3. and it ARRIVES ────────────────────────────────────────────────────
# A slide that eases out forever, or that stalls because nothing scheduled the
# next frame, is worse than no slide: the desktop would be permanently a few
# pixels short of where it belongs. Give it well over the 600ms it asked for,
# then require the position to have stopped changing AND the focused window to
# be fully on screen — the invariant niri_strip.sh asserts at rest.
i=0
SETTLED=
while [ $i -lt 40 ]; do
    A=$(xs); sleep 0.1; B=$(xs)
    [ "$A" = "$B" ] && { SETTLED=$A; break; }
    i=$((i + 1))
done
[ -n "$SETTLED" ] || fail "the strip never stopped moving — a slide that does
       not terminate. layout_scroll_tick must clamp t >= 1 to the target and
       clear strip_sliding."
echo "settle:   the strip came to rest at [$SETTLED]"

clients | grep '"focused":true' | grep -q '"enabled":true' \
    || fail "the strip settled with the FOCUSED window not on screen. Once the
       slide is over the at-rest rule applies again — whole column or nothing,
       scrolled so the focused one is among them. Mid-slide the columns are
       allowed to peek in at the edges; that allowance must not outlive the
       animation.
$(clients)"
echo "rest:     the focused window is on screen once the slide is over"

kill_clients
stop_synui

echo "PASS: the niri strip slides, and arrives"
cleanup
exit 0
