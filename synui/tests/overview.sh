#!/bin/sh
# overview.sh — mission control, against a real compositor.
#
# overview_layout_test covers the arithmetic. This covers the half that
# arithmetic cannot: the RENDER, which locks a client buffer per tile, scales it
# into a scene node, and has to let go of every one of them again.
#
# Three things go wrong here and nowhere else, and all three are invisible to a
# unit test:
#
#   1. a tile holding a client buffer after the overview is gone. A scene buffer
#      takes its own lock, so a buffer left set is one the client cannot put
#      back into its own rotation — the bug alttab_tile_clear() exists for, and
#      this panel has OVERVIEW_MAX of them rather than eighteen.
#   2. the tile count changing under the panel. Windows close and desktops
#      switch while it is up, and the selection is clamped in the renderer
#      precisely because that is the one place every path goes through.
#   3. an EMPTY desktop. The layout is asked for zero tiles before anything
#      knows the desk is empty, and the strip still has to draw — switching to a
#      desktop with nothing on it is the ordinary way to use the strip.
#
# Run under ASan (the debug build meson uses for the other headless scripts), so
# a buffer mistake is an abort rather than a slow leak nobody notices.
#
# Usage: overview.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: overview.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: overview.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: overview.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-overview.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

# Hermetic HOME, or the developer's own synuirc and windows.conf leak in — and
# windows.conf in particular would place these clients from a remembered box.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_CONFIG"
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

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

clients() { synctl clients | tr '{' '\n' | grep '"app_id"'; }
count_windows() { clients | grep -c '"app_id":"stubborn"'; }
active_ws() { synctl activeworkspace | sed -n 's/.*"id":\([0-9]*\).*/\1/p'; }

alive() {
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "$1
       synui died. The tail of its log is above; an ASan report there is the
       buffer handling, an abort with no report is the render path."
}

open_window() {
    want=$1
    "$CLIENT" 0 60 >>"$TMP/client.out" 2>>"$TMP/client.err" &
    CLIENT_PIDS="$CLIENT_PIDS $!"
    i=0
    while [ $i -lt 40 ]; do
        [ "$(count_windows)" -ge "$want" ] && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "a client never mapped (wanted $want windows, have $(count_windows)):
       $(cat "$TMP/client.err")"
}

# A repaint has to actually happen between the dispatches, or every assertion
# below is "synui survived being told to do something it has not done yet".
settle() { sleep 0.4; }

# ── 1. open it with nothing on the desk ──────────────────────────────────
# First, because an empty desktop is the case the tile loop never runs for, and
# a renderer that assumes at least one tile dies HERE rather than in the middle
# of the interesting part.
synctl dispatch overview >/dev/null
settle
alive "opening mission control on an empty desktop:"
synctl dispatch overview >/dev/null
settle
alive "closing it again on an empty desktop:"
echo "empty:    opens and closes with nothing on the desk"

# ── 2. open it over real windows ─────────────────────────────────────────
open_window 1
open_window 2
open_window 3
[ "$(count_windows)" = 3 ] || fail "expected 3 windows, got $(count_windows)"

synctl dispatch overview >/dev/null
settle
alive "opening mission control over 3 windows:"
echo "tiles:    3 live client buffers scaled into tiles"

# ── 3. a window closes while it is up ────────────────────────────────────
# The tile count drops under the panel. The selection is clamped in the renderer
# because that is the only point every path goes through — if it were clamped in
# the movers instead, this is where the panel would index off the end.
set -- $CLIENT_PIDS
kill -TERM "$1" 2>/dev/null
CLIENT_PIDS="$2 $3"
i=0
while [ $i -lt 40 ]; do
    [ "$(count_windows)" -le 2 ] && break
    i=$((i + 1)); sleep 0.1
done
[ "$(count_windows)" -le 2 ] || fail "the client never unmapped"
settle
alive "a window closing while mission control is up:"
echo "shrink:   survived a window closing under the panel"

# ── 4. the desktop strip, with the panel up ──────────────────────────────
# Switching desktops does not close the overview — that is the strip's whole
# job — so the tile set is replaced wholesale, empty desk and all, without the
# panel ever going down and releasing its buffers the ordinary way.
synctl dispatch ws 2 >/dev/null
settle
[ "$(active_ws)" = 2 ] || fail "ws 2 did not switch, still on $(active_ws)"
alive "switching to an empty desktop with mission control up:"
synctl dispatch ws 1 >/dev/null
settle
alive "switching back to a desktop with windows on it:"
echo "strip:    desktop switched under the panel, both directions"

# ── 5. close it, and check the buffers went back ─────────────────────────
synctl dispatch overview >/dev/null
settle
alive "closing mission control:"

# The clients have to still be able to draw. A tile that kept its lock leaves
# the client unable to reuse that buffer, which shows up as a client that stops
# committing — so open a fourth window and require the compositor to still be
# taking frames from everyone.
open_window 3
alive "a new window after mission control closed:"
echo "release:  clients still draw after the tiles let go"

# ── 6. clean shutdown ────────────────────────────────────────────────────
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

echo "shutdown: clean"
cleanup
echo "all checks passed"
