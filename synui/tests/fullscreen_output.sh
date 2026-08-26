#!/bin/sh
# fullscreen_output.sh — leaving fullscreen leaves the window WHERE IT WAS
#
# ⛔ THE BUG: un-fullscreening threw the window onto another monitor. velle,
# 2026-08-25, laptop plugged into a television: "when it's unfullsized it's
# jumping the window back to the main display for no reason."
#
# Why it did that. Leaving fullscreen used to re-derive the placement from
# scratch — layout_float_place, which asks layout_restore_geometry, which reads
# windows.conf. That table records where an app was WHEN IT LAST CLOSED, and
# its x/y are absolute layout coordinates, so it names a monitor as much as a
# position. For a window that is opening, that is the whole point of the
# feature. For a window that has been on screen for an hour and merely came out
# of fullscreen, it is a teleport to wherever the app was last shut down — on a
# laptop, the built-in panel, which is exactly the screen the user had moved the
# video off.
#
# So the fix is not "restore a size", it is "do not ask a question about
# OPENING a window when the window is not opening": view->fs_geo records the box
# (and therefore the screen) on the way in, and the way out puts it back.
#
# ⚠ The seed in windows.conf is not scenery — it IS the trap. Without an entry
# pointing at the other monitor there is nothing for the old path to teleport
# to, and this rig would pass on the bug. It also does double duty, exactly as
# it does in move_output.sh: layout_restore_geometry marks the window
# hand_placed, which is the state a real dragged window is in and the one no
# synthetic input can produce here.
#
# Three phases, because there are three ways out of fullscreen and they used to
# fail DIFFERENTLY — a fix for one of them is not a fix:
#
#   1. a FLOATED window (Super+F, or dragged) — the reported jump. This is the
#      one that went through layout_float_place, and it landed on the monitor
#      windows.conf named.
#   2. a hand-placed window on a FLOATING desktop — view->floating is 0 there
#      (the DESKTOP is floating), so nothing placed it at all and it kept the
#      full-output box after leaving fullscreen. Same bug family as
#      move_output.sh: "does a LAYOUT own this window's position?"
#   3. a TILED window — the layout still owns the box, but it does not own the
#      SCREEN, so the window must be tiled back onto the monitor it left from.
#
# Usage: fullscreen_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, like every other rig here: synui renders
# through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: fullscreen_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: fullscreen_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: fullscreen_output.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-fsout.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
printf 'animation_ms = 0\n' > "$SYNUI_CONFIG"

# A box well inside the FIRST output — the one the window must NOT be dragged
# back to once it has been moved off it.
SEED_X=60 SEED_Y=50 SEED_W=320 SEED_H=240
cat > "$SYNUI_WINDOWS" <<SEED
window stubborn x=$SEED_X y=$SEED_Y w=$SEED_W h=$SEED_H maximized=0 floating=1
SEED

# ⚠ TWO OUTPUTS. With one there is nowhere to jump to and every assertion below
# would hold on the unfixed code.
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
win()        { synctl activewindow; }
win_x()      { win | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),.*/\1/p'; }
win_out()    { win | sed -n 's/.*"output":"\([^"]*\)".*/\1/p'; }
win_size()   { win | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1x\2/p'; }
win_fs()     { win | sed -n 's/.*"fullscreen":\(true\|false\).*/\1/p'; }

# ⚠ `synctl outputs` lists them in the COMPOSITOR's ring order, NOT
# left-to-right — HEADLESS-1 can perfectly well sit to the RIGHT of HEADLESS-2.
# So every position below is measured from the output the window itself named.
outs()  { synctl outputs | tr '{' '\n' | sed -n 's/.*"name":"\([^"]*\)","at":\[\([0-9-]*\),\([0-9-]*\)\].*/\1 \2/p'; }
out_x() { outs | awk -v n="$1" '$1 == n { print $2 }'; }
[ "$(outs | wc -l)" = 2 ] || fail "expected 2 headless outputs, got:
$(synctl outputs)"
[ "$(outs | awk '{print $2}' | sort -u | wc -l)" = 2 ] \
    || fail "both outputs are at the same x — they are stacked, so a jump
       between them cannot be measured on x. $(synctl outputs)"
echo "outputs:  $(outs | awk '{printf "%s at x=%s  ", $1, $2}')"

# ── the desktop has to be FLOATING before the window maps ────────────────
# layout_restore_geometry asks the layout whether it has any say, so a window
# opening on a tiling desktop never reaches the branch that hand-places it.
# Phase 2 needs that state, and it can only be had at map time.
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
       and there is no seeded entry for the old code to teleport back to."
HOME_OUT=$(win_out)
echo "seeded:   x=$(win_x) on $HOME_OUT, hand-placed by layout_restore_geometry"

# Move it to the OTHER monitor — the television, in the report. Through the
# real key path, so the window is where a user would have put it.
to_far() {
    synctl dispatch move_output >/dev/null
    [ "$(win_out)" != "$HOME_OUT" ] || fail "move_output did not step the ring —
       the window is still on $HOME_OUT, so there is nothing to fullscreen away
       from. (tests/move_output.sh owns that bug; this rig depends on it.)"
}

# Fullscreen, check it covered the monitor it was already on, and come back.
# The check in the middle is not a formality: if fullscreen went to the WRONG
# monitor then coming back to that same monitor would be a pass for the wrong
# reason.
round_trip() {
    want_out=$1
    synctl dispatch fullscreen_toggle >/dev/null
    [ "$(win_fs)" = true ] || fail "fullscreen_toggle did not take: $(win)"
    [ "$(win_out)" = "$want_out" ] || fail "fullscreen moved the window to
       $(win_out); it was on $want_out and no monitor was named."
    [ "$(win_x)" = "$(out_x "$want_out")" ] || fail "the fullscreen window sits
       at x=$(win_x) but $want_out starts at x=$(out_x "$want_out") — it is not
       covering the monitor it is supposed to be filling."
    synctl dispatch fullscreen_toggle >/dev/null
    [ "$(win_fs)" = false ] || fail "the second fullscreen_toggle did not take: $(win)"
}

# ── 1. a FLOATED window: the reported jump ───────────────────────────────
# Super+F on a tiling desktop, which is what a user does to a window before
# dragging it to the television. view->floating is 1 here, so leaving
# fullscreen used to go through layout_float_place → layout_restore_geometry →
# windows.conf, and windows.conf still says $HOME_OUT.
i=0
while [ "$(layout_now)" != tiling ]; do
    [ $i -ge 8 ] && fail "layout_cycle never reached tiling (stuck at $(layout_now))"
    synctl dispatch layout_cycle >/dev/null
    i=$((i + 1))
done
synctl dispatch float_toggle >/dev/null
case "$(win)" in
    *'"floating":true'*) ;;
    *) fail "float_toggle did not float the window, so this phase would test
       the same path as phase 2: $(win)" ;;
esac
to_far
FAR_OUT=$(win_out); FAR_X=$(win_x); FAR_SIZE=$(win_size)
echo "floated:  $FAR_SIZE at x=$FAR_X on $FAR_OUT"

round_trip "$FAR_OUT"
[ "$(win_out)" = "$FAR_OUT" ] || fail "LEAVING FULLSCREEN JUMPED THE WINDOW to
       $(win_out) at x=$(win_x). It was on $FAR_OUT and nothing asked for it to
       move. This is the reported bug: the un-fullscreen path re-derived the
       placement from windows.conf, whose entry still says $HOME_OUT because
       that is where this app was last CLOSED — so a video un-fullscreened on
       the television lands back on the laptop panel."
[ "$(win_x)" = "$FAR_X" ] && [ "$(win_size)" = "$FAR_SIZE" ] \
    || fail "the window came back $(win_size) at x=$(win_x), not the $FAR_SIZE
       at x=$FAR_X it had before it went fullscreen — the box is being
       re-derived rather than restored."
echo "floated:  back on $(win_out) at x=$(win_x), $(win_size)"

# ── 2. hand-placed on a FLOATING desktop ─────────────────────────────────
# view->floating is 0 (the DESKTOP is floating) and layout_float_arrange steps
# over hand-placed windows, so NOTHING placed this one on the way out and it
# stayed at the full-output box. Un-float it first, or it is phase 1 again.
synctl dispatch float_toggle >/dev/null
i=0
while [ "$(layout_now)" != floating ]; do
    [ $i -ge 8 ] && fail "layout_cycle never reached floating (stuck at $(layout_now))"
    synctl dispatch layout_cycle >/dev/null
    i=$((i + 1))
done
case "$(win)" in
    *'"floating":true'*)
        fail "the window is still marked floating on the floating desktop, so
       this phase is a copy of phase 1: $(win)" ;;
esac
HP_OUT=$(win_out); HP_X=$(win_x); HP_SIZE=$(win_size)
echo "placed:   $HP_SIZE at x=$HP_X on $HP_OUT"

round_trip "$HP_OUT"
[ "$(win_out)" = "$HP_OUT" ] || fail "on a FLOATING desktop, leaving fullscreen
       moved the hand-placed window to $(win_out); it was on $HP_OUT."
[ "$(win_size)" = "$HP_SIZE" ] || fail "the hand-placed window came back
       $(win_size), not the $HP_SIZE it had before fullscreen — on a floating
       desktop nothing else will ever resize it, so it is simply stuck at the
       size fullscreen gave it."
[ "$(win_x)" = "$HP_X" ] || fail "the hand-placed window came back at
       x=$(win_x), not the x=$HP_X it had before fullscreen."
echo "placed:   back on $(win_out) at x=$(win_x), $(win_size)"

# ── 3. a TILED window ────────────────────────────────────────────────────
# The layout owns the BOX here, and must go on owning it — but it does not own
# the SCREEN, so a window tiled on the far monitor has to come back tiled on
# the far monitor rather than wherever windows.conf points.
i=0
while [ "$(layout_now)" != tiling ]; do
    [ $i -ge 8 ] && fail "layout_cycle never reached tiling (stuck at $(layout_now))"
    synctl dispatch layout_cycle >/dev/null
    i=$((i + 1))
done
TILED_OUT=$(win_out); TILED_X=$(win_x); TILED_SIZE=$(win_size)
[ "$TILED_SIZE" != "${SEED_W}x${SEED_H}" ] || fail "the window is still at its
       remembered ${SEED_W}x${SEED_H} on a tiling desktop — it was never taken
       back into the flow, so this phase is not testing a tiled window."
echo "tiled:    $TILED_SIZE at x=$TILED_X on $TILED_OUT"

round_trip "$TILED_OUT"
[ "$(win_out)" = "$TILED_OUT" ] || fail "on a TILING desktop, leaving fullscreen
       moved the window to $(win_out) — it was tiled on $TILED_OUT."
[ "$(win_x)" = "$TILED_X" ] && [ "$(win_size)" = "$TILED_SIZE" ] \
    || fail "the window came back $(win_size) at x=$(win_x), not the
       $TILED_SIZE at x=$TILED_X the tiler had given it. The layout still owns
       the box on a tiling desktop; the remembered one must not win here."
echo "tiling:   back on $(win_out) at x=$(win_x), $(win_size)"

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer reported errors"
fi

cleanup
echo "fullscreen_output: 3 phases passed"
