#!/bin/sh
# ws_slide.sh — switching virtual desktops ANIMATES, and each style is its own
#
# The desktop switch used to be one hard-coded cross-fade timed by the same
# animation_ms that timed a window opening. It is now anim_workspace (none /
# fade / slide) on anim_workspace_ms, and this asserts the two things that
# separate a real animation from a claim of one:
#
#   1. anim_workspace = off   — the control: the incoming desktop is never
#                                observed displaced, and the outgoing window's
#                                node is disabled straight away.
#   2. anim_workspace = slide  — the incoming window IS observed displaced
#                                (drawn off to the side of where it belongs),
#                                and
#   3.                         — it ARRIVES: the offset returns to zero and the
#                                window is left enabled at full opacity. A
#                                slide that stalls half-way leaves the desktop
#                                permanently beside itself.
#
# WHAT THIS TEST CAN AND CANNOT SEE. It cannot look at pixels. What it can see
# is "anim_offset" in `synctl clients` — where the frame is being DRAWN relative
# to its logical position, which is precisely what a slide changes and a
# teleport does not. "at" deliberately does NOT move during a workspace slide:
# the window's geometry is not changing, only where it is painted for a few
# hundred milliseconds, and that distinction is why the offset had to become
# readable for this test to exist at all.
#
# Usage: ws_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: ws_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: ws_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: ws_slide.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-wsslide.XXXXXX) || exit 1
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

synctl()  { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients() { synctl clients | tr '{' '\n' | grep '"app_id"'; }
# The x half of every window's draw offset, one line each. Zero everywhere means
# nothing is displaced — which is both the resting state and what a teleport
# looks like from here.
offsets() { clients | sed -n 's/.*"anim_offset":\[\(-\{0,1\}[0-9]*\),.*/\1/p' | tr '\n' ' '; }

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

# One window on desktop 1 and one on desktop 2, so a switch always has both a
# desk to leave and a desk to arrive at. Ends standing on desktop 1.
setup_desks() {
    open_window 1
    synctl dispatch ws 2 >/dev/null
    open_window 2
    [ "$(clients | wc -l)" = 2 ] || fail "expected 2 windows, got $(clients | wc -l)"
    synctl dispatch ws 1 >/dev/null
    sleep 1     # let whatever that switch started finish before measuring
}

# Switch to desktop 2 and poll the draw offsets, printing every DISTINCT line
# seen. The first line is the resting state before the switch.
watch_switch() {
    _budget=$1
    _seen=$(offsets)
    printf '%s\n' "$_seen"
    synctl dispatch ws 2 >/dev/null
    _i=0
    while [ "$_i" -lt "$_budget" ]; do
        _now=$(offsets)
        if [ "$_now" != "$_seen" ]; then
            printf '%s\n' "$_now"
            _seen=$_now
        fi
        _i=$((_i + 1))
    done
}

# Every offset back to 0, i.e. nothing is displaced any more.
all_zero() {
    for _o in $(offsets); do
        [ "$_o" = 0 ] || return 1
    done
    return 0
}

# ── 1. anim_workspace = off: nothing is ever displaced ──────────────────
# The control. Without it, "we saw an offset" would not distinguish an animation
# from a field that is simply always set — this establishes that the off path
# never displaces anything, so any non-zero offset in phase 2 is the animation
# and nothing else.
printf 'anim_workspace = off\nanim_workspace_ms = 600\n' > "$SYNUI_CONFIG"
start_synui
setup_desks

INSTANT=$(watch_switch 400)
STEPS=$(printf '%s\n' "$INSTANT" | wc -l)
echo "instant:  offsets took $STEPS distinct value(s) across the switch"
[ "$STEPS" = 1 ] || fail "with anim_workspace = off the draw offset changed
       $STEPS times. Nothing should be displaced when the style is off:
       anim_start() is supposed to apply the end state on the spot for a
       duration of 0, and ANIM_WS_NONE forces the duration to 0.
$INSTANT"

kill_clients
stop_synui

# ── 2. anim_workspace = slide: it IS observed displaced ──────────────────
printf 'anim_workspace = slide\nanim_workspace_ms = 600\n' > "$SYNUI_CONFIG"
start_synui
setup_desks

POSITIONS=$(watch_switch 4000)
NONZERO=$(printf '%s\n' "$POSITIONS" | grep -c '[1-9]')
echo "slide:    $NONZERO of the observed offset lines were non-zero"
[ "$NONZERO" -ge 1 ] || fail "with anim_workspace = slide nothing was ever drawn
       away from its logical position — the desktop teleported. anim_tick is
       supposed to interpolate anim_dx a frame at a time and re-place the frame
       through view_place_node; check that output_frame is still scheduling
       frames while one is in flight (anim_tick returns true for exactly that
       reason), and that view_place_node is what positions the node.
$POSITIONS"

# ── 3. and it ARRIVES ────────────────────────────────────────────────────
# A slide that eases out forever, or that stalls because nothing scheduled the
# next frame, is worse than no slide: the desktop would sit permanently beside
# itself, and since "at" never moved, everything else on the system — clicks,
# tiling, geometry persistence — would disagree with the screen.
i=0
SETTLED=
while [ $i -lt 40 ]; do
    if all_zero; then SETTLED=yes; break; fi
    i=$((i + 1)); sleep 0.1
done
[ -n "$SETTLED" ] || fail "the slide never came to rest — every window should be
       back at offset 0 once it is over. anim_tick must clamp t >= 1 to
       anim_dx_to/anim_dy_to and clear fade_active.
$(clients)"
echo "settle:   every window is back at its logical position"

# The incoming desktop's window is visible and solid, and the outgoing one's
# node is off. Both are things the fade path got right and a new style could
# quietly break: a slide that forgets to re-enable, or one that leaves the
# window it slid away at alpha 0, looks like an empty desktop.
VIS=$(clients | grep -c '"enabled":true')
[ "$VIS" = 1 ] || fail "expected exactly 1 enabled window after the switch, got
       $VIS. hide() disables its node when it finishes and show() enables it up
       front; one of those did not happen.
$(clients)"
clients | grep '"enabled":true' | grep -q '"alpha":1.000' \
    || fail "the desktop that arrived is enabled but not solid. A slide keeps
       full opacity throughout — only the fade style touches alpha.
$(clients)"
echo "rest:     one window enabled, at full opacity"

kill_clients
stop_synui

echo "PASS"
exit 0
