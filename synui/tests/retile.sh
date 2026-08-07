#!/bin/sh
# retile.sh — can a tiling desktop get its windows back?
#
# velle, 2026-07-31: "the tiling option isn't making sense to me it was working
# and then it just stops even if i try to remaximize and try again", and then
# "if it's in tile mode i want the tiling to work, this isn't intuitive."
#
# The live desk was on layout "tiling" with every window reporting
# floating:true, so the tiler was laying out an empty set. Four things set
# view->floating during a session — dragging a window to move it (input.c),
# snapping it to an edge (snap.c), maximizing it, and Super+F — and until this
# change the only thing that ever cleared it again was Super+F, one window at a
# time. Selecting the tiling layout did not bring anything back.
#
# Phase 3 is the one that explains the report. view_apply_maximized records
# saved_floating on the way in and restores it on the way out, so on a window
# that is ALREADY floating it reads 1 and writes 1 back. Maximize/un-maximize is
# a FIXED POINT: it can never hand a window to the tiler, which is exactly what
# "even if i try to remaximize and try again" was describing. That phase passes
# both before and after the fix — it is here to pin the behaviour that made the
# bug confusing, so that nobody later "fixes" tiling by making un-maximize clear
# floating unconditionally and silently breaks Super+F-then-maximize.
#
# Usage: retile.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: retile.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: retile.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: retile.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-retile.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

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

clients()    { synctl clients | tr '{' '\n' | grep '"app_id"'; }
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }
n_floating() { clients | grep -c '"floating":true'; }
focused_is_floating() { clients | grep '"focused":true' | grep -c '"floating":true'; }

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

# ── 1. two tiled windows on a tiling desktop ─────────────────────────────
[ "$(layout_now)" = tiling ] || fail "expected tiling at startup, got $(layout_now)"
open_window 1
open_window 2
[ "$(n_floating)" = 0 ] || fail "a freshly opened window was already floating on a
       tiling desktop — windows.conf should have no say here and the seed file is
       empty, so something else floated it."
echo "setup:    2 windows, both tiled"

# ── 2. floating one takes it out of the tiler ────────────────────────────
synctl dispatch float_toggle >/dev/null
[ "$(n_floating)" = 1 ] || fail "float_toggle did not float the focused window
       (floating count $(n_floating)) — the rest of the test has nothing to reclaim."
echo "float:    1 of 2 floating"

# ── 3. maximize/un-maximize is a FIXED POINT on a floating window ────────
# This is the behaviour the report described. It must NOT be "fixed" by making
# un-maximize clear floating: saved_floating is what lets Super+F, then
# maximize, then un-maximize give you back the floating window you asked for.
synctl dispatch maximize_toggle >/dev/null
[ "$(focused_is_floating)" = 1 ] || fail "maximizing did not leave the window
       floating — view_apply_maximized has to float it for the duration."
synctl dispatch maximize_toggle >/dev/null
[ "$(focused_is_floating)" = 1 ] || fail "un-maximizing cleared floating. That
       would make Super+F -> maximize -> un-maximize silently discard the float
       the user asked for. Reclaiming is retile's job, not un-maximize's."
[ "$(n_floating)" = 1 ] || fail "the maximize round trip changed the floating count
       to $(n_floating)"
echo "maximize: round trip leaves it floating — the reported dead end, pinned"

# ── 4. retile takes it back ──────────────────────────────────────────────
synctl dispatch retile >/dev/null
# No backticks in these messages: fail() takes a double-quoted string, so a
# backticked word is command substitution and the word disappears from the
# message it was meant to name.
[ "$(n_floating)" = 0 ] || fail "after 'retile', $(n_floating) window(s) are still
       floating. This is the reported bug: a tiling desktop with floating windows
       has nothing to tile, and nothing but Super+F per window could clear them."
[ "$(layout_now)" = tiling ] || fail "retile changed the layout of an already
       tiling desktop to $(layout_now); it should only reclaim."
echo "retile:   both windows back in the layout"

# ── 5. selecting the tiling layout reclaims too ──────────────────────────
# Seven presses right round the cycle (tiling -> floating -> monocle -> AI ->
# niri -> spiral -> cascade -> tiling). The window is floated first, so arriving at
# tiling has something to take back.
#
# The count is spelled out rather than derived on purpose: it is also the
# assertion that nothing has been SLOTTED INTO the middle of syn_layout_t, which
# would silently renumber layouts.state and give every desktop back the wrong
# layout on the next login. A new layout appended to the end costs one number
# here; a new layout inserted anywhere else should cost a failing test.
synctl dispatch float_toggle >/dev/null
[ "$(n_floating)" = 1 ] || fail "float_toggle did not float a window for phase 5"
for _ in 1 2 3 4 5 6 7; do synctl dispatch layout_cycle >/dev/null; done
[ "$(layout_now)" = tiling ] || fail "seven layout_cycles did not come back to
       tiling, got $(layout_now). The cycle order is the syn_layout_t order:
       tiling, floating, monocle, AI, niri, spiral, cascade."
[ "$(n_floating)" = 0 ] || fail "arriving back on the tiling layout left
       $(n_floating) window(s) floating. Choosing a layout that places windows
       has to mean 'place these windows', or the tiler lays out an empty set."
echo "cycle:    selecting tiling reclaimed the desktop"

# ── 6. retile works from another layout, and tiles ───────────────────────
# "tile shortcut should work in other modes still". From a FLOATING desktop the
# reclaim alone would be invisible — nothing tiles there — so retile switches.
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = floating ] || fail "expected floating, got $(layout_now)"
synctl dispatch retile >/dev/null
[ "$(layout_now)" = tiling ] || fail "retile from a FLOATING desktop left the
       layout at $(layout_now). A key called 'tile' that visibly does nothing is
       the bug this exists to fix, one level up."
[ "$(n_floating)" = 0 ] || fail "retile from floating left $(n_floating) floating"
echo "cross:    retile from floating switched to tiling and reclaimed"

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
SYNUI_PID=

echo "PASS: retile"
cleanup
exit 0
