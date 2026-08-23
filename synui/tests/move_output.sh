#!/bin/sh
# move_output.sh — Super+O actually carries the window to the other monitor
#
# ⛔ THE BUG THIS EXISTS FOR MOVED THE RECORD AND LEFT THE PIXELS. `move_output`
# reassigns view->output and then leans on layout_apply to put the window where
# that output is. That is right on a desktop whose layout places windows — and
# wrong on the two cases that carry their own absolute geometry, which the code
# tested for with `v->floating`.
#
# ⚠ `v->floating` IS THE WINDOW'S FLAG, NOT THE DESKTOP'S. On a FLOATING desktop
# nothing is marked floating (the desktop is), and LAYOUT_FLOATING's pass —
# layout_float_arrange — deliberately steps over every window the user has ever
# placed by hand. So a dragged window on a floating desktop was placed by
# nobody: the key looked completely dead, and pressing it again only walked the
# window's idea of its monitor round the ring, further out of step with the
# screen. Reported as "Super+O and Super+Shift+O have stopped responding".
#
# hand_placed is set at grab_release_constraints, from a pointer drag this rig
# cannot perform — but layout_restore_geometry sets it too, for a window read
# back out of windows.conf, and on a floating desktop it does so WITHOUT
# floating the window. That is the exact state to reproduce, through a real
# caller, with no synthetic input at all. (float_arrange.sh uses the same seam.)
#
#   1. a hand-placed window on a FLOATING desktop  — Super+O carries it over
#   2. Super+Shift+O                               — and brings it back
#   3. a TILING desktop                            — the path that always worked
#
# Usage: move_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, like every other rig here: synui renders
# through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: move_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: move_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: move_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-moveout.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
printf 'animation_ms = 0\n' > "$SYNUI_CONFIG"

# The seed IS the hand placement. A box well inside the first output, so a
# window that has not moved is unmistakable from one that has.
SEED_X=60 SEED_Y=50 SEED_W=320 SEED_H=240
cat > "$SYNUI_WINDOWS" <<SEED
window stubborn x=$SEED_X y=$SEED_Y w=$SEED_W h=$SEED_H maximized=0 floating=1
SEED

# ⚠ TWO OUTPUTS, which is the whole point — with one, move_output is correct to
# do nothing and this rig would pass on a bug.
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=2
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
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }
win_x()      { synctl activewindow | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),.*/\1/p'; }
win_out()    { synctl activewindow | sed -n 's/.*"output":"\([^"]*\)".*/\1/p'; }

# ⚠ `synctl outputs` lists them in the COMPOSITOR's order, which is the ring
# move_output walks and NOT left-to-right: here HEADLESS-1 sits to the right of
# HEADLESS-2. So every distance below is measured from the outputs the window
# actually named, never from the order they were printed in.
outs()  { synctl outputs | tr '{' '\n' | sed -n 's/.*"name":"\([^"]*\)","at":\[\([0-9-]*\),\([0-9-]*\)\].*/\1 \2/p'; }
out_x() { outs | awk -v n="$1" '$1 == n { print $2 }'; }
[ "$(outs | wc -l)" = 2 ] || fail "expected 2 headless outputs, got:
$(synctl outputs)"
[ "$(outs | awk '{print $2}' | sort -u | wc -l)" = 2 ] \
    || fail "both outputs are at the same x — they are stacked, so moving
       between them cannot be measured on x. $(synctl outputs)"
echo "outputs:  $(outs | awk '{printf "%s at x=%s  ", $1, $2}')"

# ── the desktop has to be FLOATING before the window maps ────────────────
# layout_restore_geometry asks the layout whether it has any say, so a window
# that opens on a tiling desktop never reaches the branch that hand-places it.
i=0
while [ "$(layout_now)" != floating ]; do
    [ $i -ge 8 ] && fail "layout_cycle never reached floating (stuck at $(layout_now))"
    synctl dispatch layout_cycle >/dev/null
    i=$((i + 1))
done

"$CLIENT" 0 90 >>"$TMP/client.out" 2>>"$TMP/client.err" &
CLIENT_PIDS="$CLIENT_PIDS $!"
i=0
while [ $i -lt 40 ]; do
    [ -n "$(win_x)" ] && break
    i=$((i + 1)); sleep 0.1
done
[ -n "$(win_x)" ] || fail "the client never mapped: $(cat "$TMP/client.err")"

[ "$(win_x)" = "$SEED_X" ] || fail "the window opened at x=$(win_x), not the
       x=$SEED_X in windows.conf — without the restore it is not hand-placed,
       and this rig would be testing a window the arranger is free to move."
START_OUT=$(win_out)
echo "seeded:   x=$(win_x) on $START_OUT, hand-placed by layout_restore_geometry"

# ── 1. the floating desktop: the window CROSSES ──────────────────────────
synctl dispatch move_output >/dev/null
NEW_X=$(win_x); NEW_OUT=$(win_out)
[ "$NEW_OUT" != "$START_OUT" ] \
    || fail "move_output left the window on $NEW_OUT — the ring did not step."
[ "$NEW_X" != "$SEED_X" ] || fail "move_output changed the window's output to
       $NEW_OUT and left its pixels at x=$NEW_X, exactly where they were. That
       is the bug: on a FLOATING desktop nothing is marked floating, so the
       branch that carries a window across was skipped and layout_float_arrange
       — which steps over hand-placed windows — was left to do a job it had
       already decided not to do."
DELTA=$((NEW_X - SEED_X))
APART=$(( $(out_x "$NEW_OUT") - $(out_x "$START_OUT") ))
[ "$DELTA" = "$APART" ] || fail "the window moved $DELTA px, but $START_OUT and
       $NEW_OUT are $APART px apart — it should keep the same place on the new
       monitor, not land somewhere else on it."
echo "floating: carried to $NEW_OUT, x $SEED_X → $NEW_X (+$DELTA)"

# ── 2. and Super+Shift+O brings it back ──────────────────────────────────
synctl dispatch move_output prev >/dev/null
[ "$(win_out)" = "$START_OUT" ] && [ "$(win_x)" = "$SEED_X" ] \
    || fail "move_output prev put it on $(win_out) at x=$(win_x); it started on
       $START_OUT at x=$SEED_X. The two directions have to be each other's
       undo, or the key that looks dead is replaced by one that drifts."
echo "prev:     back on $START_OUT at x=$(win_x)"

# ── 3. the tiling desktop still works ────────────────────────────────────
# The path that was never broken, asserted so a fix for the floating case
# cannot quietly take the laid-out one with it: here the target output's
# layout is what places the window, and it must still run.
i=0
while [ "$(layout_now)" != tiling ]; do
    [ $i -ge 8 ] && fail "layout_cycle never reached tiling (stuck at $(layout_now))"
    synctl dispatch layout_cycle >/dev/null
    i=$((i + 1))
done
TILED_X=$(win_x)
synctl dispatch move_output >/dev/null
[ "$(win_out)" != "$START_OUT" ] || fail "on a tiling desktop the window stayed
       on $START_OUT."
[ "$(win_x)" != "$TILED_X" ] || fail "on a tiling desktop the window's output
       changed but its box did not move from x=$TILED_X — layout_apply is no
       longer placing it on the monitor it was given."
echo "tiling:   laid out on $(win_out) at x=$(win_x)"

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer reported errors"
fi

cleanup
echo "move_output: 3 phases passed"
