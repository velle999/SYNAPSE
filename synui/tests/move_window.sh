#!/bin/sh
# move_window.sh — Super+arrow actually MOVES the window on a floating desktop
#
# ⛔ THE BUG THIS EXISTS FOR SHIPPED THE DAY THE CHORD DID (438), and it is the
# second window action in a row to fall into the same hole. window_move_key
# chooses between two meanings of "move": a window the USER places slides by
# WINDOW_MOVE_STEP px, a TILED one has no position of its own so it moves
# through ws->windows order instead. It chose with `v->floating`.
#
# ⚠ `v->floating` IS THE WINDOW'S FLAG, NOT THE DESKTOP'S. On a FLOATING desktop
# nothing is marked floating — the desktop is — so every window there took the
# reorder branch. layout_move_in_stack duly rewrote the order and layout_apply
# ran LAYOUT_FLOATING's pass, layout_float_arrange, which deliberately steps
# over every window the user has ever placed by hand. So the list changed and
# the pixels did not: `synctl dispatch move_left` answered {"ok":true} while
# "at" never moved. Reported, twice over now, as "the key is not responding".
#
# ⚠ THE ASSERTION THAT CATCHES IT IS THE BOX, NEVER THE RETURN CODE — the
# dispatch succeeded throughout. See tests/move_output.sh for the same pair of
# facts about Super+O.
#
# hand_placed is set at grab_release_constraints, from a pointer drag this rig
# cannot perform — but layout_restore_geometry sets it too, for a window read
# back out of windows.conf, and on a floating desktop it does so WITHOUT
# floating the window. That is the exact broken state, through a real caller,
# with no synthetic input at all.
#
#   1. a hand-placed window on a FLOATING desktop — Super+Left SLIDES it
#   2. Super+Right                                — and puts it back
#   3. Super+Up / Super+Down                      — the other axis slides too
#   4. a TILING desktop, two windows              — the reorder still happens
#
# Usage: move_window.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, like every other rig here: synui renders
# through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: move_window.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: move_window.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: move_window.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

# Must match WINDOW_MOVE_STEP in src/input.c.
STEP=40

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

TMP=$(mktemp -d /tmp/synui-movewin.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
printf 'animation_ms = 0\n' > "$SYNUI_CONFIG"

# The seed IS the hand placement. Well inside the output on both axes, so a
# window that has not moved is unmistakable from one that has, and so neither
# direction of either axis lands on window_move_key's clamp.
SEED_X=300 SEED_Y=300 SEED_W=320 SEED_H=240
cat > "$SYNUI_WINDOWS" <<SEED
window stubborn x=$SEED_X y=$SEED_Y w=$SEED_W h=$SEED_H maximized=0 floating=1
SEED

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
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
# ⚠ ONE LINE of JSON comes back for every window, so it is split before it
# is counted — a bare grep -c over the raw reply counts LINES and answers 1
# no matter how many windows are open.
clients()    { synctl clients | tr '{' '\n' | grep '"app_id"'; }
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }
win_x()      { synctl activewindow | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),.*/\1/p'; }
win_y()      { synctl activewindow | sed -n 's/.*"at":\[-\{0,1\}[0-9]*,\(-\{0,1\}[0-9]*\)\].*/\1/p'; }

# ── the desktop has to be FLOATING before the window maps ────────────────
# layout_restore_geometry asks the layout whether it has any say, so a window
# that opens on a tiling desktop never reaches the branch that hand-places it.
i=0
while [ "$(layout_now)" != floating ]; do
    [ $i -ge 8 ] && fail "layout_cycle never reached floating (stuck at $(layout_now))"
    synctl dispatch layout_cycle >/dev/null
    i=$((i + 1))
done

LAST_PID=
open_window() {
    want=$1
    "$CLIENT" 0 90 >>"$TMP/client.out" 2>>"$TMP/client.err" &
    LAST_PID=$!
    CLIENT_PIDS="$CLIENT_PIDS $LAST_PID"
    i=0
    while [ $i -lt 40 ]; do
        [ "$(clients | grep -c '"app_id":"stubborn"')" -ge "$want" ] && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "a client never mapped (wanted $want): $(cat "$TMP/client.err")"
}

open_window 1

[ "$(win_x)" = "$SEED_X" ] && [ "$(win_y)" = "$SEED_Y" ] \
    || fail "the window opened at $(win_x),$(win_y), not the $SEED_X,$SEED_Y in
       windows.conf — without the restore it is not hand-placed, and this rig
       would be testing a window the arranger is free to move."
echo "seeded:   $(win_x),$(win_y) on a $(layout_now) desktop, hand-placed"

# ── 1. the floating desktop: the window SLIDES ───────────────────────────
synctl dispatch move_left >/dev/null
NEW_X=$(win_x)
[ "$NEW_X" != "$SEED_X" ] || fail "move_left answered ok and left the window at
       x=$NEW_X, exactly where it was. That is the bug: on a FLOATING desktop
       nothing is marked floating, so the key took the branch that only
       reorders ws->windows, and layout_float_arrange — which steps over
       hand-placed windows — was left to do a job it had already declined."
[ "$NEW_X" = "$((SEED_X - STEP))" ] \
    || fail "move_left put the window at x=$NEW_X; one step left of $SEED_X is
       $((SEED_X - STEP)). Either the step changed or something else moved it."
[ "$(win_y)" = "$SEED_Y" ] || fail "move_left moved the window on Y too
       ($(win_y), was $SEED_Y) — a horizontal key must leave the other axis be."
echo "left:     x $SEED_X → $NEW_X"

# ── 2. and the opposite key is its undo ──────────────────────────────────
synctl dispatch move_right >/dev/null
[ "$(win_x)" = "$SEED_X" ] || fail "move_right put it at x=$(win_x); it started
       at x=$SEED_X. The two directions have to be each other's undo, or the
       key that looked dead is replaced by one that drifts."
echo "right:    back at x=$(win_x)"

# ── 3. the vertical pair, on the same window ─────────────────────────────
synctl dispatch move_up >/dev/null
[ "$(win_y)" = "$((SEED_Y - STEP))" ] \
    || fail "move_up put the window at y=$(win_y), not $((SEED_Y - STEP)).
       ⚠ Up and Down fold onto the ORDER on a tiled window, so it is only here,
       where the window has a position of its own, that they can be measured."
[ "$(win_x)" = "$SEED_X" ] || fail "move_up moved the window on X too."
synctl dispatch move_down >/dev/null
[ "$(win_y)" = "$SEED_Y" ] || fail "move_down put it at y=$(win_y), not back at
       $SEED_Y."
echo "up/down:  y $SEED_Y → $((SEED_Y - STEP)) → $(win_y)"

# ── 4. the tiling desktop still REORDERS ─────────────────────────────────
# The path that was never broken, asserted so the fix for the floating case
# cannot quietly take the laid-out one with it. Two windows, because
# layout_move_in_stack returns early on a desktop holding fewer than two.
i=0
while [ "$(layout_now)" != tiling ]; do
    [ $i -ge 8 ] && fail "layout_cycle never reached tiling (stuck at $(layout_now))"
    synctl dispatch layout_cycle >/dev/null
    i=$((i + 1))
done
open_window 2

TILED_X=$(win_x); TILED_Y=$(win_y)
synctl dispatch move_left >/dev/null
[ "$(win_x)" != "$TILED_X" ] || [ "$(win_y)" != "$TILED_Y" ] \
    || fail "on a TILING desktop the focused window sat still at
       $TILED_X,$TILED_Y — the reorder branch is no longer being reached, so
       the floating fix has swallowed the case that always worked."
[ "$(win_x)" = "$((TILED_X - STEP))" ] && [ "$(win_y)" = "$TILED_Y" ] \
    && fail "on a TILING desktop the window slid one step instead of moving
       through the layout. A tiled window has no position of its own: the next
       layout_apply will overwrite it, so the slide is a frame of a lie."
echo "tiling:   reordered, $TILED_X,$TILED_Y → $(win_x),$(win_y)"

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer reported errors"
fi

cleanup
echo "move_window: 4 phases passed"
