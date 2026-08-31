#!/bin/sh
# ws_per_monitor.sh — `workspace_mode = per-monitor`: a desktop switch moves ONE
# screen
#
# synui's workspaces are virtual desktops spanning the whole desk (KDE/GNOME),
# and that is still the default. This asserts the OTHER mode, in which each
# monitor remembers the desktop it is showing and Super+2 moves only the one the
# focus is on.
#
# Three things separate the feature from a claim of it, and all three need TWO
# monitors and a window on each — which is why this is its own test rather than a
# case in ws_slide.sh:
#
#   1. shared (the control)  — a switch moves BOTH outputs and hides BOTH
#                              windows. Without this, "the second window is
#                              still up" would not distinguish per-monitor
#                              desktops from a switch that is simply broken.
#   2. per-monitor           — the same switch moves ONLY the focused output.
#                              The other one keeps its desktop AND its window.
#   3. and the window that stays is genuinely on screen, not merely listed:
#                              its scene node is still enabled.
#
# ⚠ THE BUG THIS EXISTS TO CATCH is not "the switch does nothing" — it is the
# opposite, an unfocused monitor quietly following along, which on a real desk
# looks like the feature working until you glance at the other screen. So the
# assertions are about the output that was NOT switched.
#
# What it reads: `synctl outputs` now reports each screen's "workspace", which is
# the only external view of the split (`activeworkspace` answers about the
# focused screen alone and cannot tell a monitor that moved from one that did
# not), and `synctl clients` reports "enabled" per window.
#
# Usage: ws_per_monitor.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, like smoke.sh: synui renders through
# scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: ws_per_monitor.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: ws_per_monitor.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: ws_per_monitor.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-wspm.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"

# ⛔ SYNUI_SOCKET IS SET IN SOME SHELLS AND POINTS AT THE LIVE DESKTOP, which
# synctl prefers over WAYLAND_DISPLAY — a rig that leaves it set drives the
# machine it is running on instead of the one it started.
unset SYNUI_SOCKET

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=2
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

synctl()  { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients() { synctl clients | tr '{' '\n' | grep '"app_id"'; }

# One "output=NAME ws=N" line per monitor, and one "output=NAME enabled=B" per
# window. Both are read out of the JSON rather than counted, because "two
# windows are enabled" is true both when the feature works and when nothing
# switched at all.
out_ws()      { synctl outputs | tr '{' '\n' | sed -n \
                  's/.*"name":"\([^"]*\)".*"workspace":\([0-9]*\).*/\1=\2/p' \
                  | sort | tr '\n' ' '; }
win_enabled() { clients | sed -n \
                  's/.*"output":"\([^"]*\)".*"enabled":\([a-z]*\).*/\1=\2/p' \
                  | sort | tr '\n' ' '; }

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
    while [ $i -lt 60 ]; do
        [ "$(clients | grep -c '"app_id":"stubborn"')" -ge "$want" ] && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "a client never mapped (wanted $want): $(cat "$TMP/client.err")"
}

# A window on each monitor, both on desktop 1. move_output is Super+O: it
# re-homes a window to the next screen KEEPING its desktop, which is the axis
# this test is not about and therefore the right way to arrange the fixture.
setup_two_screens() {
    open_window 1
    synctl dispatch move_output >/dev/null
    sleep 0.4
    open_window 2
    [ "$(clients | wc -l)" = 2 ] || fail "expected 2 windows, got $(clients | wc -l)"
    # One window per output, or the assertions below cannot distinguish the
    # monitors from each other.
    HOMES=$(clients | sed -n 's/.*"output":"\([^"]*\)".*/\1/p' | sort -u | wc -l)
    [ "$HOMES" = 2 ] || fail "both windows landed on the same monitor —
       'dispatch move_output' did not re-home one. Nothing below can tell the
       two screens apart:
$(clients)"
    sleep 0.5
}

# ── 1. shared: the switch moves the WHOLE desk ──────────────────────────
# The control. Without it, "the other monitor kept its desktop" in phase 2
# proves nothing — it is also what a switch that reaches neither screen looks
# like.
printf 'workspace_mode = shared\nanim_workspace = off\nanimation_ms = 0\n' > "$SYNUI_CONFIG"
start_synui
setup_two_screens

BEFORE=$(out_ws)
synctl dispatch ws 3 >/dev/null
sleep 0.6
SHARED=$(out_ws)
SHARED_WINS=$(win_enabled)
echo "shared:   $BEFORE -> $SHARED"
case "$SHARED" in
    *=3*) ;;
    *) fail "under workspace_mode = shared, no monitor reached desktop 3:
       $SHARED" ;;
esac
[ "$(printf '%s\n' "$SHARED" | tr ' ' '\n' | grep -c '=3$')" = 2 ] \
    || fail "under workspace_mode = shared a desktop spans the desk, so BOTH
       monitors must be on 3 after the switch. Got: $SHARED"
[ "$(printf '%s\n' "$SHARED_WINS" | tr ' ' '\n' | grep -c '=true$')" = 0 ] \
    || fail "both windows are on desktop 1 and the desk moved to 3, so neither
       should still be drawn. Got: $SHARED_WINS
$(clients)"
echo "shared:   both windows hidden, as a desk-wide switch requires"

kill_clients() { for p in $CLIENT_PIDS; do kill -9 "$p" 2>/dev/null; done; CLIENT_PIDS=; }
kill_clients
stop_synui

# ── 2. per-monitor: the switch moves ONE screen ─────────────────────────
printf 'workspace_mode = per-monitor\nanim_workspace = off\nanimation_ms = 0\n' > "$SYNUI_CONFIG"
start_synui
setup_two_screens

FOCUSED=$(synctl outputs | tr '{' '\n' | sed -n \
            's/.*"name":"\([^"]*\)".*"focused":true.*/\1/p')
[ -n "$FOCUSED" ] || fail "no output reports itself focused: $(synctl outputs)"

synctl dispatch ws 3 >/dev/null
sleep 0.6
SPLIT=$(out_ws)
SPLIT_WINS=$(win_enabled)
echo "per-mon:  focused=$FOCUSED  ->  $SPLIT"

case "$SPLIT" in
    *"$FOCUSED=3"*) ;;
    *) fail "the FOCUSED monitor ($FOCUSED) did not reach desktop 3. Whatever
       else per-monitor desktops change, the screen you are looking at must
       still answer the bind: $SPLIT" ;;
esac

MOVED=$(printf '%s\n' "$SPLIT" | tr ' ' '\n' | grep -c '=3$')
[ "$MOVED" = 1 ] || fail "$MOVED of 2 monitors moved to desktop 3. Under
       workspace_mode = per-monitor exactly ONE — the focused one — may move.
       This is the failure that looks like the feature working until you glance
       at the other screen: $SPLIT"

# ...and the other screen's window is still genuinely drawn. A monitor that
# kept its NUMBER while its windows were disabled underneath it would pass
# every assertion above and be a blank screen.
[ "$(printf '%s\n' "$SPLIT_WINS" | tr ' ' '\n' | grep -c '=true$')" = 1 ] \
    || fail "exactly one window should still be enabled — the one on the
       monitor that did not switch. Got: $SPLIT_WINS
$(clients)"
case "$SPLIT_WINS" in
    *"$FOCUSED=false"*) ;;
    *) fail "the FOCUSED monitor's window is still drawn after that monitor
       switched away from its desktop: $SPLIT_WINS" ;;
esac
echo "per-mon:  the unswitched monitor kept its desktop AND its window"

# ── 3. and it comes back ────────────────────────────────────────────────
# A split desk has to be able to rejoin, or per-monitor mode is a one-way trip
# and the second screen is stranded on a desktop the bind can no longer reach.
synctl dispatch ws 1 >/dev/null
sleep 0.6
BACK=$(out_ws)
BACK_WINS=$(win_enabled)
[ "$(printf '%s\n' "$BACK" | tr ' ' '\n' | grep -c '=1$')" = 2 ] \
    || fail "switching the focused monitor back to desktop 1 should leave both
       monitors on 1 again (the other never left it): $BACK"
[ "$(printf '%s\n' "$BACK_WINS" | tr ' ' '\n' | grep -c '=true$')" = 2 ] \
    || fail "both windows are on desktop 1 and both monitors are showing it, so
       both must be drawn again. A window that came back to a visible desktop
       still disabled is view_refresh_visibility's job: $BACK_WINS
$(clients)"
echo "per-mon:  both screens back on desktop 1, both windows drawn"

kill_clients
stop_synui

echo "PASS"
exit 0
