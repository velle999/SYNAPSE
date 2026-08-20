#!/bin/sh
# close_focus_mru.sh — when a window closes, does focus fall back to the window
# you were LAST in, or to whatever happens to head the list?
#
# velle, 2026-08-19: "edit jumps forward"... "when i open software, when i close
# it edit jumps forward at the same moment". Closing one application threw an
# editor that had not been touched in an hour to the front of the stack.
#
# xdg_surface_unmap() calls workspace_focus_first() when the closing window had
# focus, and that walked ws->windows and took the first mapped hit. ws->windows
# is STACKING order — its own field comment in synui.h says so, right next to
# focus_seq, which exists because list order is not the order anyone means by
# "the last window I was in". Views are inserted at the HEAD, so the first hit
# is the NEWEST surviving window, which after closing something is very often a
# window the user has never looked at. And focus_view() raises what it focuses,
# so it did not merely become active — it jumped in front.
#
# The discriminating shape, and why the fourth window is here: focus the OLDEST
# window, then open a new one over it and close that again.
#
#   A B C opened, then A focused   -> A has the highest focus_seq of A B C
#   D opened (takes focus), closed -> who gets it?
#
#   before: C   (newest survivor = head of the stacking list)
#   after:  A   (highest focus_seq = what you were actually using)
#
# A and C are different windows either way round, so this cannot pass by
# accident on a one-window desktop or by getting the tie-break lucky.
#
# Usage: close_focus_mru.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: close_focus_mru.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: close_focus_mru.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: close_focus_mru.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-closefocus.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# animation_ms = 0, or a focus read lands mid-fade — the same reason every other
# layout test sets it.
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

synctl()  { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients() { synctl clients | tr '{' '\n' | grep '"app_id"'; }
count()   { clients | grep -c '"app_id":"stubborn"'; }

# ⚠ "focused" comes BEFORE "pid" in a client record (ipc.c's json_view), and
# they are adjacent — matching them in the wrong order silently yields nothing,
# which reads as "no window has focus" rather than as a broken pattern.
focused_pid() {
    clients | sed -n 's/.*"focused":true,"pid":\([0-9]*\).*/\1/p'
}

# Opens one window and echoes its pid, once the compositor has it.
open_window() {
    want=$1
    "$CLIENT" 0 90 >>"$TMP/client.out" 2>>"$TMP/client.err" &
    pid=$!
    CLIENT_PIDS="$CLIENT_PIDS $pid"
    i=0
    while [ $i -lt 40 ]; do
        [ "$(count)" -ge "$want" ] && echo "$pid" && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "a client never mapped (wanted $want): $(cat "$TMP/client.err")"
}

# ── 0. three windows ─────────────────────────────────────────────────────
A=$(open_window 1)
B=$(open_window 2)
C=$(open_window 3)
[ "$(count)" = 3 ] || fail "expected 3 windows, got $(count)"

# ── 1. make the OLDEST the most recently used ────────────────────────────
# focus_next cycles; step until A has it rather than assuming a direction.
i=0
while [ $i -lt 6 ]; do
    [ "$(focused_pid)" = "$A" ] && break
    synctl dispatch focus_next >/dev/null
    i=$((i + 1)); sleep 0.1
done
[ "$(focused_pid)" = "$A" ] || fail "could not focus the first window (A=$A, focused=$(focused_pid))"

# ── 2. a new window over it, which takes focus ───────────────────────────
D=$(open_window 4)
[ "$(focused_pid)" = "$D" ] || fail "a newly mapped window did not take focus (D=$D, focused=$(focused_pid))"

# ── 3. close it ──────────────────────────────────────────────────────────
kill "$D" 2>/dev/null
i=0
while [ $i -lt 40 ]; do
    [ "$(count)" -le 3 ] && break
    i=$((i + 1)); sleep 0.1
done
[ "$(count)" = 3 ] || fail "the closed window never went away (still $(count))"
sleep 0.3

# ── 4. focus goes back to what you were using ────────────────────────────
GOT=$(focused_pid)
[ -n "$GOT" ] || fail "nothing has focus after the close"
if [ "$GOT" = "$C" ]; then
    fail "focus went to the newest survivor (C=$C) — workspace_focus_first is
      walking ws->windows in stacking order again instead of ordering by
      focus_seq. This is the regression: closing a window throws an untouched
      one to the front, because focus_view() raises what it focuses."
fi
[ "$GOT" = "$A" ] || fail "focus went to $GOT, expected the most recently used (A=$A; B=$B, C=$C)"

echo "PASS: closing a window returns focus to the most recently used one"
cleanup
exit 0
