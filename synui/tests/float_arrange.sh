#!/bin/sh
# float_arrange.sh — the floating desktop's own tiler
#
# LAYOUT_FLOATING used to place nothing at all: layout_apply's case for it was a
# bare no-op, so every window landed centred on whatever layout_float_place had
# centred before it, and the third terminal you opened sat squarely on top of
# the second. "You place the windows" is the right rule for a window you have an
# opinion about; it is a poor one for three you just opened and have not touched.
#
# layout_float_arrange arranges the ones nobody has touched, and the whole point
# is that it is INSET — it deliberately does not fill the screen, so the
# wallpaper reads as part of the composition instead of as the bit the windows
# failed to cover. That is what phase 2 measures, and it is the assertion most
# likely to be quietly broken by someone "fixing" the layout to use the full box.
#
# The other half of the contract is that a window the user has placed is left
# alone FOREVER, or the arrangement would be fighting him rather than helping.
# That flag (view->hand_placed) is set at grab_release_constraints, which needs
# a pointer drag this rig has no way to perform — but it is set on exactly one
# other path, and that path is a file: layout_restore_geometry marks a window
# restored from windows.conf as hand-placed, because geom_persist only ever
# records a window that was FREE when it closed, which is to say one the user
# had placed himself. Seeding windows.conf therefore tests the real flag through
# a real caller, with no synthetic input at all.
#
#   1. three untouched windows  — arranged, no overlap, all on screen
#   2. the inset               — the grid does NOT reach the screen edges
#   3. a remembered window     — keeps its box while two more open around it
#   4. Super+Shift+G           — forgets that, and takes it back into the grid
#
# Usage: float_arrange.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: float_arrange.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: float_arrange.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: float_arrange.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-floatarr.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# Only animation_ms, so float_inset and float_gap under test are the SHIPPED
# defaults — a synuirc that set them would be testing this rig's arithmetic
# rather than what a user actually gets. The fades are off because every
# assertion here is about where the windows settle.
printf 'animation_ms = 0\n' > "$SYNUI_CONFIG"
# Empty for phases 1-2: these windows must be ones synui has never seen, or the
# table would place them and there would be nothing for the arranger to do.
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

synctl()     { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients()    { synctl clients | tr '{' '\n' | grep '"app_id"'; }
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }

boxes() {
    clients | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),\(-\{0,1\}[0-9]*\)\],"size":\[\([0-9]*\),\([0-9]*\)\].*/\1 \2 \3 \4/p'
}
# The box of one window, found by pid — the arranger moves windows about, so a
# line captured earlier describes where something USED to be.
box_of() {
    clients | grep "\"pid\":$1," \
        | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),\(-\{0,1\}[0-9]*\)\],"size":\[\([0-9]*\),\([0-9]*\)\].*/\1,\2 \3x\4/p'
}

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

overlaps() {
    boxes | awk '
    { x[NR]=$1; y[NR]=$2; w[NR]=$3; h[NR]=$4; n=NR }
    END {
      for (i = 1; i <= n; i++)
        for (j = i+1; j <= n; j++)
          if (x[i] < x[j]+w[j] && x[j] < x[i]+w[i] &&
              y[i] < y[j]+h[j] && y[j] < y[i]+h[i])
            printf "window %d (%d,%d %dx%d) overlaps window %d (%d,%d %dx%d)\n",
                   i, x[i], y[i], w[i], h[i], j, x[j], y[j], w[j], h[j]
    }'
}

OUT=$(synctl outputs | tr '{' '\n' | grep '"name"' | head -1)
OW=$(echo "$OUT" | sed -n 's/.*"size":\[\([0-9]*\),.*/\1/p')
OH=$(echo "$OUT" | sed -n 's/.*"size":\[[0-9]*,\([0-9]*\)\].*/\1/p')
[ -n "$OW" ] && [ "$OW" -gt 0 ] || fail "could not read the headless output: $OUT"
echo "output:   ${OW}x${OH}"

# ── 1. three untouched windows are arranged ──────────────────────────────
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = floating ] || fail "layout_cycle did not reach floating, got $(layout_now)"

open_window 1
open_window 2
open_window 3
[ "$(boxes | wc -l)" = 3 ] || fail "expected 3 windows, got $(boxes | wc -l):
$(boxes)"

boxes | while read -r bx by bw bh; do
    { [ "$bw" -ge 40 ] && [ "$bh" -ge 40 ]; } \
        || { echo "a window is ${bw}x${bh} — nothing placed it" >&2; exit 1; }
    { [ "$bx" -ge 0 ] && [ "$by" -ge 0 ] \
      && [ $((bx + bw)) -le "$OW" ] && [ $((by + bh)) -le "$OH" ]; } \
        || { echo "a window's box ${bx},${by} ${bw}x${bh} is off the ${OW}x${OH} output" >&2; exit 1; }
done || fail "phase 1: see above.
$(boxes)"

OVER=$(overlaps)
[ -z "$OVER" ] || fail "the floating grid overlaps itself:
$OVER"
echo "arrange:  three windows placed, none overlapping, all on screen"

# ── 2. the inset: the grid does NOT fill the screen ──────────────────────
# The reason this layout is not just layout_tile with a bigger gap. Every edge
# of the screen must have bare desktop on it — that is the wallpaper showing
# through, which is the entire look being asked for.
MINX=$(boxes | awk 'BEGIN{m=-1}{if(m<0||$1<m)m=$1}END{print m+0}')
MINY=$(boxes | awk 'BEGIN{m=-1}{if(m<0||$2<m)m=$2}END{print m+0}')
MAXR=$(boxes | awk 'BEGIN{m=0}{if($1+$3>m)m=$1+$3}END{print m+0}')
MAXB=$(boxes | awk 'BEGIN{m=0}{if($2+$4>m)m=$2+$4}END{print m+0}')

{ [ "$MINX" -gt 0 ] && [ "$MINY" -gt 0 ] \
  && [ "$MAXR" -lt "$OW" ] && [ "$MAXB" -lt "$OH" ]; } \
    || fail "the floating grid reaches the screen edge — left $MINX, top $MINY,
       right $MAXR of $OW, bottom $MAXB of $OH. float_inset is supposed to keep
       a margin clear at all four edges; a grid that fills the box is just
       layout_tile, and the wallpaper showing through IS the layout.
$(boxes)"
echo "inset:    margins of ${MINX}/${MINY} left/top, $((OW-MAXR))/$((OH-MAXB)) right/bottom"

# ── 3. a window the user placed is left alone ────────────────────────────
# Seed the table with a box that is NOT anywhere the grid would put something,
# restart onto it, and open two more windows around it. layout_restore_geometry
# marks it hand_placed, so the arranger must step over it on every reflow those
# two opens cause.
kill -TERM "$SYNUI_PID" 2>/dev/null; wait "$SYNUI_PID" 2>/dev/null
for p in $CLIENT_PIDS; do kill -9 "$p" 2>/dev/null; done
CLIENT_PIDS=

SEED_X=37 SEED_Y=41 SEED_W=311 SEED_H=213
cat > "$SYNUI_WINDOWS" <<EOF
window stubborn x=$SEED_X y=$SEED_Y w=$SEED_W h=$SEED_H maximized=0 floating=1
EOF
# The layout is remembered across restarts (layouts.state), so this comes back
# up floating without having to cycle again.
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
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during the restart"
    i=$((i + 1)); sleep 0.1
done
[ -n "$SOCK" ] || fail "no wayland socket after the restart"
export WAYLAND_DISPLAY="$SOCK"
CTLSOCK="$TMP/synui-$SOCK.sock"
[ "$(layout_now)" = floating ] || fail "the desktop did not come back floating,
       got $(layout_now) — layouts.state should have remembered it"

open_window 1
SEEDED=$LAST_PID
[ "$(box_of "$SEEDED")" = "$SEED_X,$SEED_Y ${SEED_W}x${SEED_H}" ] \
    || fail "the seeded window opened at $(box_of "$SEEDED"), not the
       $SEED_X,$SEED_Y ${SEED_W}x${SEED_H} in windows.conf. The arranger has
       overwritten a remembered box, which means remember_geometry no longer
       means anything on the one layout it was written for."

open_window 2
open_window 3
[ "$(box_of "$SEEDED")" = "$SEED_X,$SEED_Y ${SEED_W}x${SEED_H}" ] \
    || fail "opening two more windows moved the hand-placed one to
       $(box_of "$SEEDED"), off its remembered $SEED_X,$SEED_Y
       ${SEED_W}x${SEED_H}. A window the user has placed must be skipped by
       layout_float_arrange for good — otherwise every new window rearranges
       the desk out from under him."
echo "respect:  the hand-placed window kept its box while two opened around it"

# ── 4. Super+Shift+G takes it back ───────────────────────────────────────
synctl dispatch float_arrange >/dev/null
[ "$(box_of "$SEEDED")" != "$SEED_X,$SEED_Y ${SEED_W}x${SEED_H}" ] \
    || fail "float_arrange left the hand-placed window on its own box. That
       action exists precisely to clear hand_placed across the desktop and lay
       everything out again; if it is a no-op there is no way back from a drag."

OVER=$(overlaps)
[ -z "$OVER" ] || fail "after float_arrange the grid overlaps itself:
$OVER"
echo "release:  float_arrange took it back into the grid"

# ── 5. clean shutdown ────────────────────────────────────────────────────
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

echo "PASS: the floating desktop arranges what you haven't touched, and only that"
cleanup
exit 0
