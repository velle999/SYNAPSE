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

# ── 5. Alt+Tab IS this panel now ─────────────────────────────────────────
# velle, 2026-08-07: "take mission control off super x and make the alt tab
# default". `alt_tab_style` defaults to `overview`, so the switcher gesture runs
# through here — and the gesture has to be the one every desktop has, or the
# default is a trap: a press moves the selection off the window you are in, and
# LETTING GO commits to it.
#
# Alt release is a modifiers event no headless synui can be given (same seam as
# alttab_scope.sh), so `alt_tab_commit` stands in for it. Both switchers are on
# that action and each is a no-op unless its own cycle is up.
#
# The wrap is the sharp end. The focused window is the most recently opened,
# which is the LAST tile — so a step that clamped instead of wrapping would
# leave the commonest press there is doing nothing at all. The assertion is that
# focus MOVED, and it fails on a clamp.
synctl dispatch overview >/dev/null   # down, so the gesture opens it itself
settle
BEFORE=$(clients | grep -c '"focused":true')
[ "$BEFORE" = 1 ] || fail "expected exactly one focused window before the
       gesture, got $BEFORE"
WAS=$(clients | grep -n '"focused":true' | cut -d: -f1)

synctl dispatch alt_tab >/dev/null
settle
alive "Alt+Tab opening mission control:"
# Mid-gesture nothing has been picked yet — the same contract the MRU strip
# has, and for the same reason: tabbing past a window must not focus it.
NOW=$(clients | grep -n '"focused":true' | cut -d: -f1)
[ "$NOW" = "$WAS" ] || fail "stepping the overview moved the FOCUS mid-gesture
       (window $WAS -> $NOW). The pick belongs in overview_alt_commit, at the
       release."

synctl dispatch alt_tab_commit >/dev/null
settle
alive "committing the Alt+Tab pick:"
AFTER=$(clients | grep -n '"focused":true' | cut -d: -f1)
[ "$AFTER" != "$WAS" ] || fail "Alt+Tab then release left the focus on the same
       window (#$WAS of $(count_windows)). The selection starts on the window you
       are in and steps ONE — and it has to WRAP, because the window you are in
       is usually the last tile. A clamp fails exactly here."
[ "$(clients | grep -c '"focused":true')" = 1 ] || fail "the commit left
       $(clients | grep -c '"focused":true') focused windows"
echo "alt_tab:  opened mission control, picked on release, focus moved"

# ── 5b. and it WRAPS off the end ─────────────────────────────────────────
# The commit above landed on the tile after the one we were on, which on this
# desk is the LAST one — so this press is the case the wrap exists for, and the
# one a clamp cannot pass: from the last tile there is nowhere to step but
# round to the first, and a clamped step would leave the focus exactly where it
# is and the key looking broken.
LAST=$(clients | grep -n '"focused":true' | cut -d: -f1)
[ "$LAST" = "$(count_windows)" ] || fail "this phase assumes the previous commit
       left the focus on the LAST tile (it is on #$LAST of $(count_windows)) —
       without that there is no wrap to test here."

synctl dispatch alt_tab >/dev/null
settle
synctl dispatch alt_tab_commit >/dev/null
settle
alive "wrapping off the end of the tiles:"
WRAPPED=$(clients | grep -n '"focused":true' | cut -d: -f1)
[ "$WRAPPED" = 1 ] || fail "stepping off the LAST tile left the focus on
       #$WRAPPED, not back round on #1. overview_alt_step has to wrap where the
       arrow keys clamp — the window you are in is usually the last tile, so a
       clamp makes the most ordinary Alt+Tab there is do nothing."
echo "wrap:     stepped off the last tile and came round to the first"

# ── 6. close it, and check the buffers went back ─────────────────────────
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

# ── 7. clean shutdown ────────────────────────────────────────────────────
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
