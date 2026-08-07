#!/bin/sh
# niri_strip.sh — the three things that make the niri layout niri, and not
# another way of drawing the master-stack tiler.
#
#   1. A new window does not shrink the old ones. On the tiling layout every
#      window that opens takes width off the stack; on this one the strip just
#      gets longer, so every default column is the same width whether there are
#      two windows or ten. That is the whole reason the layout exists, and it is
#      the one property a "scrollable tiling" implementation can quietly lose by
#      dividing the screen up after all.
#
#   2. The strip scrolls to the FOCUS. A column that does not fit entirely on
#      the monitor is not drawn (layout_niri: synui cannot crop a frame at the
#      screen edge, so a half-off column would be painted across the monitor
#      next door). That makes "the focused window is on screen" a real
#      invariant with real teeth — get it wrong and you are typing into a
#      window parked off the side, which is exactly the bug monocle_focus.sh
#      was written for, one layout over.
#
#   3. Columns stack. Super+, pulls the focused window into the column on its
#      left and Super+. pushes it back out — the only two moves that are not
#      just "walk along the strip", which Super+Shift+J/K already does.
#
# Geometry is read from `synctl clients`, which reports the frame box the
# layout placed (at/size) and whether the scene node is enabled — the same
# fields monocle_focus.sh asserts on.
#
# Usage: niri_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: niri_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: niri_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: niri_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-niri.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# Only animation_ms, so the column width under test is the SHIPPED default. A
# synuirc that set gap or border_width would be testing this rig's arithmetic,
# not synui's — animation_ms sets neither.
#
# It is here because every assertion below is about where the strip SETTLES,
# and since the niri slide landed there is a whole animation between the action
# and the settled state. Mid-slide the rules are deliberately different: the
# strip is part-way to its target and columns are allowed to peek in at the
# edges (niri_place), so "all three columns are on screen" and "the focused one
# is not fully on screen" are both briefly TRUE and neither says anything about
# the layout. Turning the animation off makes every reflow land in one step, so
# this test measures geometry instead of racing a clock. The slide itself is
# niri_slide.sh's business.
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

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

clients()    { synctl clients | tr '{' '\n' | grep '"app_id"'; }
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }
n_enabled()  { clients | grep -c '"enabled":true'; }
focused()    { clients | grep '"focused":true'; }

# Geometry columns out of the client lines, one window per line, in strip order.
# "at":[X,Y],"size":[W,H]
xs()      { clients | sed -n 's/.*"at":\[\([0-9-]*\),.*/\1/p'; }
widths()  { clients | sed -n 's/.*"size":\[\([0-9]*\),.*/\1/p'; }
# "<x> <pid>" per window, for finding the one immediately left of another.
xpid()    { clients | sed -n 's/.*"at":\[\([0-9-]*\),.*"pid":\([0-9]*\).*/\1 \2/p'; }
# The live line for one window, re-read every time: the layout moves windows
# about, so a line captured earlier describes where something USED to be.
line_of() { clients | grep "\"pid\":$1,"; }
# Field 1 or 2 of a two-number JSON array ("at" or "size") on a client line.
field()   { echo "$1" | sed -n "s/.*\"$2\":\[\([0-9-]*\),\([0-9-]*\)\].*/\\$3/p"; }

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

# The scroll invariant: whatever else is true, the window with the keyboard is
# a window you can see.
check_focus_visible() {
    what=$1
    echo "$(focused)" | grep -q '"enabled":true' || fail "$what: the focused
       window is NOT on screen. layout_niri draws only the columns that fit
       entirely, and scrolls the strip so the focused one is among them —
       focus_view() reflows a niri desktop for exactly this. Without it the
       keyboard sits in a window parked off the side of the monitor.
       Live state:
$(clients)"
}

# ── 1. four presses to niri ──────────────────────────────────────────────
# tiling -> floating -> monocle -> AI -> niri. Appended to the cycle rather
# than slotted next to tiling, so these four are also the assertion that
# nothing renumbered the layouts underneath layouts.state.
[ "$(layout_now)" = tiling ] || fail "expected tiling at startup, got $(layout_now)"
for _ in 1 2 3 4; do synctl dispatch layout_cycle >/dev/null; done
[ "$(layout_now)" = niri ] || fail "four layout_cycles did not reach niri, got
       $(layout_now). The cycle order is the syn_layout_t order."
echo "cycle:    four presses reach niri"

# ── 2. two windows, two equal columns, both on screen ────────────────────
open_window 1
W1=$(widths)
open_window 2

set -- $(widths)
[ $# = 2 ] || fail "expected 2 windows, got $#"
[ "$1" = "$2" ] || fail "the two columns came out $1 and $2 wide. Every default
       column is the same width on this layout — differing widths mean the
       master/stack split leaked in from layout_tile."
COLW=$1
[ "$COLW" = "$W1" ] || fail "the first window was $W1 wide on its own and $COLW
       once a second opened. A column is a column whether or not it has
       neighbours — a first window that fills the monitor and then shrinks is
       layout_tile's behaviour (n == 1 takes the whole box), and it means the
       strip only really exists from the second window on."

set -- $(xs)
[ "$1" != "$2" ] || fail "both windows were placed at x=$1 — they are in the
       same column. A window opens in a column of ITS OWN (layout_strip_insert
       clears col_join); only Super+, puts one inside another."
[ "$(n_enabled)" = 2 ] || fail "$(n_enabled) of the 2 columns are on screen.
       Two default columns plus the gap between them fit a monitor exactly —
       that is what the slot arithmetic in niri_col_width() is for, and if this
       is 1 it has gone back to taking the fraction of the bare viewport, which
       costs a gap per column and pushes the second one off the edge."
echo "two:      two equal columns at ${COLW}px, both visible"

# ── 3. a third window does not shrink the other two ──────────────────────
open_window 3
set -- $(widths)
[ $# = 3 ] || fail "expected 3 windows, got $#"
for w in "$@"; do
    [ "$w" = "$COLW" ] || fail "opening a third window changed a column from
       $COLW to $w wide. On a scrollable-tiling desktop the strip gets LONGER,
       it does not get divided up further — this is the difference between niri
       and the master-stack tiler, and the reason the layout exists.
       Live state:
$(clients)"
done
check_focus_visible "with three windows open"
echo "three:    the strip grew, the columns did not shrink"

# ── 4. focus scrolls the strip ───────────────────────────────────────────
# Three columns cannot all fit, so one is always off-screen. Walking the focus
# right round the strip must bring each one on as it is reached.
[ "$(n_enabled)" -lt 3 ] || fail "all 3 columns are on screen at once, so
       nothing below can prove the strip scrolls. Either the output got wider
       or the default column narrower; this test needs a strip longer than the
       monitor."
for step in 1 2 3; do
    synctl dispatch focus_next >/dev/null
    check_focus_visible "after focus_next #$step"
done
for step in 1 2 3; do
    synctl dispatch alt_tab >/dev/null
    synctl dispatch alt_tab_commit >/dev/null
    check_focus_visible "after Alt+Tab #$step"
done
echo "scroll:   every focus change brought its column on screen"

# ── 5. consume and expel ─────────────────────────────────────────────────
# Super+, pulls the focused window into the column on its left: the two end up
# at the same x, splitting that column's height between them.
#
# Both windows are tracked BY PID and re-read after the move, never by the x
# they were at before it. Joining two columns makes the strip shorter, which
# re-scrolls it — the surviving column genuinely does end up somewhere else on
# screen, and an assertion against the old x fails on a layout that worked.
#
# Consume needs a window that HAS a column to its left, so walk the focus along
# until one does rather than trusting where section 4 finished.
FP= PP=
for _ in 1 2 3; do
    FOC=$(focused)
    FX=$(field "$FOC" at 1)
    FH=$(field "$FOC" size 2)
    FP=$(echo "$FOC" | sed -n 's/.*"pid":\([0-9]*\).*/\1/p')
    PP=$(xpid | awk -v fx="$FX" '$1 < fx { if (m == "" || $1 > m) { m = $1; p = $2 } }
                                 END { print p }')
    [ -n "$PP" ] && break
    synctl dispatch focus_next >/dev/null
done
[ -n "$PP" ] || fail "no column left of the focused one after walking the whole
       strip; the consume half of this test needs a window to consume INTO."

synctl dispatch column_consume >/dev/null
[ "$(field "$(line_of "$FP")" at 1)" = "$(field "$(line_of "$PP")" at 1)" ] ||
    fail "column_consume left pid $FP at x=$(field "$(line_of "$FP")" at 1) and
       the column it should have joined at $(field "$(line_of "$PP")" at 1).
       Consume joins the column on the LEFT (col_join = 1), so the two share an x.
       Live state:
$(clients)"
NEWH=$(field "$(line_of "$FP")" size 2)
[ "$NEWH" -lt "$FH" ] || fail "column_consume moved pid $FP into the column on
       its left but left it at full height ($NEWH, was $FH). Two windows in one
       column share it vertically — an unchanged height means they are stacked
       ON TOP of each other and one of them cannot be seen."
echo "consume:  the window joined the column on its left and they split it"

synctl dispatch column_expel >/dev/null
[ "$(field "$(line_of "$FP")" at 1)" != "$(field "$(line_of "$PP")" at 1)" ] ||
    fail "column_expel left pid $FP sharing x=$(field "$(line_of "$PP")" at 1)
       with the column it was consumed into. Expel is the undo for consume: the
       window goes back to a column of its own."
[ "$(field "$(line_of "$FP")" size 2)" = "$FH" ] || fail "column_expel gave pid
       $FP height $(field "$(line_of "$FP")" size 2), not the full-column $FH it
       had before it was consumed."
check_focus_visible "after column_expel"
echo "expel:    and back out into a column of its own"

# ── 6. clean shutdown ────────────────────────────────────────────────────
for p in $CLIENT_PIDS; do kill -TERM "$p" 2>/dev/null; done
CLIENT_PIDS=

kill -TERM "$SYNUI_PID" 2>/dev/null
wait "$SYNUI_PID" 2>/dev/null
rc=$?
SYNUI_PID=
[ "$rc" = 0 ] || fail "synui exited $rc (ASan/LSan report above)"

cleanup
echo "PASS: the niri strip scrolls, stacks and does not shrink"
