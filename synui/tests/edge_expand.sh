#!/bin/sh
# edge_expand.sh — double-click a border, fill that axis, double-click again.
#
# THE FEATURE (velle, 2026-08-20): "add double click top or bottom side resize
# to expand window vertically and the left or right for horizontally". The top
# or bottom border grows the window to the full usable HEIGHT; the left or right
# border grows it to the full usable WIDTH; the axis you did not touch keeps the
# size the user gave it, and a second double-click puts the axis back.
#
# HOW IT IS DRIVEN, IN TWO WAYS ON PURPOSE. Sections 1-5 go through
# `synctl dispatch expand_v_toggle` / `expand_h_toggle`, because the geometry is
# what has the interesting failures and driving it that way makes each one a
# single readable step. Section 6 then sends a REAL double-click, from a real
# pointer (tests/vpointer_click.c drives zwlr_virtual_pointer_v1, which wlroots
# wraps in a struct wlr_pointer — the compositor cannot tell it from a mouse),
# because the gesture is what was actually asked for and "the dispatch works"
# says nothing about whether the border ever sees the press.
#
# ⚠ THE USABLE BOX IS NOT THE OUTPUT BOX. A bar with an exclusive zone makes
# them differ, and hard-coding 1920x1080 would pass on a headless run and say
# nothing about a real desktop. It is read from the compositor instead, by
# maximizing the window once and asking where it landed: maximize is defined as
# "fill the usable box", so its answer IS the usable box.
#
# Usage: edge_expand.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, as every nested-synui test here does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: edge_expand.sh synui stubborn_client synctl vpointer_click}
CLIENT=${2:?usage: edge_expand.sh synui stubborn_client synctl vpointer_click}
SYNCTL=${3:?usage: edge_expand.sh synui stubborn_client synctl vpointer_click}
VPTR=${4:?usage: edge_expand.sh synui stubborn_client synctl vpointer_click}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${CLIENT_PID:-}" ] && kill -9 "$CLIENT_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

# Short runtime dir (108-byte socket paths) and a hermetic HOME, or the
# developer's own synuirc, windows.conf and autostart leak into the run.
TMP=$(mktemp -d /tmp/synui-expand.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
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

# ⚠ SYNUI_SOCKET, always. Without it synctl talks to the ambient
# WAYLAND_DISPLAY, which on a developer's machine is the LIVE desktop — this
# test resizes and maximizes whatever it is pointed at.
synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

win() { synctl clients | tr '{' '\n' | grep '"app_id":"stubborn"'; }
# "at":[X,Y],"size":[W,H] as one "X,Y WxH" string, so a whole box compares in
# a single [ = ] and a failure prints something readable.
box() {
    win | sed -n 's/.*"at":\[\([0-9-]*\),\([0-9-]*\)\].*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1,\2 \3x\4/p'
}
# 1 if the named boolean is true on the window's line, 0 otherwise. Anchored to
# the field name and its comma so "expand_v" cannot match "expand_vh" or any
# later field that happens to end the same way.
flag() { win | grep -c "\"$1\":true"; }

# Settle: view_resize configures the client and the box is read back from the
# compositor, so give the round trip a moment rather than guessing at a sleep.
wait_box() {
    i=0
    while [ $i -lt 40 ]; do
        [ "$(box)" != "$1" ] && return 0
        i=$((i + 1)); sleep 0.05
    done
    return 0
}

# A FLOATING desktop: the gesture is about a window whose size is the user's,
# and on a tiling desktop the layout owns it. (The expand still works there —
# it floats the window, as maximize does — but then the interesting assertion
# would be about the tiler, not about this.)
synctl dispatch layout_cycle >/dev/null
LAYOUT=$(synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p')
[ "$LAYOUT" = "floating" ] || fail "expected a floating desktop, got $LAYOUT"

"$CLIENT" 0 30 >"$TMP/client.out" 2>"$TMP/client.err" &
CLIENT_PID=$!
i=0
while [ $i -lt 60 ]; do
    [ -n "$(win)" ] && break
    kill -0 "$CLIENT_PID" 2>/dev/null || fail "client exited before it mapped:
       $(cat "$TMP/client.err")"
    i=$((i + 1)); sleep 0.1
done
[ -n "$(win)" ] || fail "the client never showed up in synctl clients"

# ── the usable box, from the compositor rather than assumed ──────────────
START=$(box)
synctl dispatch maximize_toggle >/dev/null; wait_box "$START"
USABLE=$(box)
synctl dispatch maximize_toggle >/dev/null; wait_box "$USABLE"
HOME_BOX=$(box)
UX=${USABLE%%,*}; rest=${USABLE#*,}; UY=${rest%% *}
UW=$(echo "$USABLE" | sed -n 's/.* \([0-9]*\)x[0-9]*/\1/p')
UH=$(echo "$USABLE" | sed -n 's/.*x\([0-9]*\)/\1/p')
HX=${HOME_BOX%%,*}; rest=${HOME_BOX#*,}; HY=${rest%% *}
HW=$(echo "$HOME_BOX" | sed -n 's/.* \([0-9]*\)x[0-9]*/\1/p')
HH=$(echo "$HOME_BOX" | sed -n 's/.*x\([0-9]*\)/\1/p')

echo "usable box:  $USABLE"
echo "window box:  $HOME_BOX"
[ "$UH" != "$HH" ] || fail "the window is already the full usable height
       ($HH), so 'expanded vertically' and 'not expanded' would look identical.
       Nothing below could tell the feature from a no-op."
[ "$UW" != "$HW" ] || fail "the window is already the full usable width ($HW)."

pass=0
ok()  { echo "  ok    $1"; pass=$((pass+1)); }

# ── 1. vertical: height fills, WIDTH AND X ARE UNTOUCHED ─────────────────
# The second half is the whole point. An expand that also changed the width
# would be a maximize with extra steps, and would pass a test that only looked
# at the height.
synctl dispatch expand_v_toggle >/dev/null; wait_box "$HOME_BOX"
[ "$(box)" = "$HX,$UY ${HW}x${UH}" ] || fail "expand_v gave $(box), wanted $HX,$UY ${HW}x${UH}
       (the height and y should be the usable box's; x and width should not move)"
ok "the top/bottom axis fills the usable height and leaves the width alone"

# ── 2. …and again puts it back ───────────────────────────────────────────
V=$(box)
synctl dispatch expand_v_toggle >/dev/null; wait_box "$V"
[ "$(box)" = "$HOME_BOX" ] || fail "collapsing gave $(box), wanted $HOME_BOX"
ok "a second one restores the box it started from"

# ── 3. horizontal, the other axis ────────────────────────────────────────
synctl dispatch expand_h_toggle >/dev/null; wait_box "$HOME_BOX"
[ "$(box)" = "$UX,$HY ${UW}x${HH}" ] || fail "expand_h gave $(box), wanted $UX,$HY ${UW}x${HH}"
ok "the left/right axis fills the usable width and leaves the height alone"

H=$(box)
synctl dispatch expand_h_toggle >/dev/null; wait_box "$H"
[ "$(box)" = "$HOME_BOX" ] || fail "collapsing gave $(box), wanted $HOME_BOX"
ok "…and back"

# ── 4. both axes, collapsed in the OTHER order ───────────────────────────
# The trap this catches: recording the pre-expand box on the SECOND expand as
# well. Do that and the first axis is restored to the size the first expand
# gave it — i.e. it never collapses at all. Collapsing in reverse order is what
# makes the mistake visible.
synctl dispatch expand_v_toggle >/dev/null; wait_box "$HOME_BOX"
B=$(box)
synctl dispatch expand_h_toggle >/dev/null; wait_box "$B"
[ "$(box)" = "$UX,$UY ${UW}x${UH}" ] || fail "both axes gave $(box), wanted the whole
       usable box $UX,$UY ${UW}x${UH}"
ok "both axes together fill the usable box"

B=$(box)
synctl dispatch expand_v_toggle >/dev/null; wait_box "$B"
[ "$(box)" = "$UX,$HY ${UW}x${HH}" ] || fail "collapsing the vertical axis gave $(box),
       wanted $UX,$HY ${UW}x${HH} — the height should come back while the width,
       expanded SECOND, stays. A box saved on the second expand fails here."
ok "the axes collapse independently, in either order"

B=$(box)
synctl dispatch expand_h_toggle >/dev/null; wait_box "$B"
[ "$(box)" = "$HOME_BOX" ] || fail "the last collapse gave $(box), wanted $HOME_BOX"
ok "…leaving exactly the box it started at"

# ── 5. the actual gesture: a real double-click on a real border ──────────
#
# ⚠ THE CLICK HAS TO LAND ON THE GRAB RING, WHICH IS OUTSIDE THE CLIENT.
# deco_update_grab_ring() puts an 8px invisible rect at frame-local (-8,-8) for
# the top edge, so the row just above view->y is the border and the row just
# below it is the client's own surface — one pixel in and the compositor
# forwards the press to the client, and the test would pass or fail for a
# reason that has nothing to do with the feature.
#
# ⚠ AND NOT NEAR A CORNER. border_edges() returns TOP|LEFT within CORNER_GRAB
# pixels of one, which names two axes and means neither; the midpoint of an
# edge is the only unambiguous place to press.
#
# ⚠ AN EXPANDED AXIS CANNOT BE COLLAPSED BY MOUSE, AND THAT IS NOT A BUG HERE
# BUT A PROPERTY OF THE GESTURE. Filling the usable box vertically puts the
# window's top edge ON the usable edge, so its grab ring is off-screen (or, with
# a bar, underneath one) and there is nothing left to double-click. The way back
# is the keybind, or dragging one of the side borders — which is why the
# collapse is tested through the dispatch above and only the EXPAND is tested
# through the pointer. Do not "fix" this by asserting a second double-click
# collapses; it cannot, until the ring learns to sit inside a flush edge.
vclick() { WAYLAND_DISPLAY="$SOCK" "$VPTR" "$@" >/dev/null 2>&1; }

EXPANDED_V="$HX,$UY ${HW}x${UH}"
[ "$(box)" = "$HOME_BOX" ] || fail "section 5 started from $(box), not $HOME_BOX"

# A SINGLE click first, on a window that is not expanded: it must arm a resize
# and nothing else. An expand on every press would make the border unusable.
MIDX=$(( HX + HW / 2 ))
TOPY=$(( HY - 1 ))
vclick "$MIDX" "$TOPY" 1 || fail "the virtual pointer could not click"
sleep 0.3
[ "$(box)" = "$HOME_BOX" ] || fail "a SINGLE click on the border moved the window to
       $(box) — one press arms a resize, it does not expand."
[ "$(flag expand_v)" = 0 ] || fail "a single click set the expand state"
ok "a single click on the border expands nothing"

# …and now two, on the same edge, inside the window.
vclick "$MIDX" "$TOPY" 2
wait_box "$HOME_BOX"
[ "$(box)" = "$EXPANDED_V" ] || fail "double-clicking the TOP border at $MIDX,$TOPY gave
       $(box), wanted $EXPANDED_V. The dispatch sections above already proved the
       geometry, so this is the gesture: either the press is not reaching
       DECO_BORDER, or the two are not being read as one double-click."
[ "$(flag expand_v)" = 1 ] || fail "the gesture did not set the vertical axis"
ok "a real double-click on the top border fills the height"

# The LEFT border is still reachable — the vertical expand did not touch x — so
# the second axis can be driven by pointer too. Both axes at once, by mouse.
MIDY=$(( UY + UH / 2 ))
LEFTX=$(( HX - 1 ))
vclick "$LEFTX" "$MIDY" 2
wait_box "$EXPANDED_V"
[ "$(box)" = "$UX,$UY ${UW}x${UH}" ] || fail "double-clicking the LEFT border at
       $LEFTX,$MIDY gave $(box), wanted the whole usable box $UX,$UY ${UW}x${UH}"
[ "$(flag expand_h)" = 1 ] || fail "the left border did not set the horizontal axis"
ok "a real double-click on the left border fills the width"

# Back to the starting box the only way that is left, which is the point of the
# warning above.
synctl dispatch expand_h_toggle >/dev/null; sleep 0.3
synctl dispatch expand_v_toggle >/dev/null; sleep 0.3
[ "$(box)" = "$HOME_BOX" ] || fail "the keybind path did not restore $HOME_BOX, gave $(box)"
[ "$(flag expand_v)" = 0 ] || fail "expand_v survived the collapse"
[ "$(flag expand_h)" = 0 ] || fail "expand_h survived the collapse"
ok "the keybind collapses what the mouse expanded, on both axes"

# ── 6. maximize takes the state over ─────────────────────────────────────
#
# They share saved_geo and cannot nest. The rule, which is a decision and not
# an accident (see view_apply_maximized): maximize records the box it found —
# the EXPANDED one — so un-maximizing hands back the tall window, because
# "restore to what it was before I maximized" is what un-maximize means
# everywhere. The expand bits are cleared with it, or the next double-click on
# a border would collapse to a box maximize has already overwritten.
synctl dispatch expand_v_toggle >/dev/null; wait_box "$HOME_BOX"
[ "$(flag expand_v)" = 1 ] || fail "expand_v is not reported on the window"
[ "$(flag expand_h)" = 0 ] || fail "expand_h is set by a vertical expand"
ok "the axis in force is reported on the window"

synctl dispatch maximize_toggle >/dev/null; wait_box "$EXPANDED_V"
[ "$(box)" = "$USABLE" ] || fail "maximizing an expanded window gave $(box), wanted $USABLE"
[ "$(flag expand_v)" = 0 ] || fail "maximize left the expand bit set — the two share
       saved_geo, so the next collapse would restore a box maximize overwrote."
ok "maximize clears the expand state it is taking over from"

synctl dispatch maximize_toggle >/dev/null; wait_box "$USABLE"
[ "$(box)" = "$EXPANDED_V" ] || fail "un-maximizing gave $(box), wanted $EXPANDED_V —
       un-maximize restores what the window was when it was maximized, which was
       the expanded box, not the box it had been expanded from."
[ "$(flag expand_v)" = 0 ] || fail "un-maximizing brought the expand bit back"
ok "un-maximize gives back the box it was maximized from, and no expand state"

# ⚠ NOTHING MAY FOLLOW THIS SECTION that expects the starting box back. The
# window is left full height on purpose: after a maximize round trip the
# expansion is no longer collapsible — that is the documented cost of the two
# states sharing saved_geo, not a leak in the test.

# ── 7. THE REGRESSION THE GESTURE COULD HAVE CAUSED ─────────────────────
#
# A border press used to commit to a resize immediately. It is ARMED now — held
# until the pointer has travelled GRAB_DRAG_SLOP — because otherwise the first
# press of every double-click would un-maximize, un-tile and hand-place the
# window before the second press could be read. That is a change to how
# ordinary resizing starts, so ordinary resizing has to be shown still to work:
# press the left border, drag it left, and the window gets wider.
#
# ⚠ IT MUST BE A REAL TRAVEL, NOT A JUMP. The promotion happens in
# process_cursor_resize on MOTION, and the geobox is re-taken there because the
# armed press recorded it before grab_release_constraints ran. A single leap to
# the far end would still pass while that re-anchoring was wrong.
# ⚠ STAY ON SCREEN. vpointer_click sends coordinates as uint32_t, so a negative
# target wraps and the cursor is slammed to the opposite edge — the window then
# collapses to its minimum width and the failure reads as "the drag went the
# wrong way". The client refuses a negative now; keep the arithmetic positive
# here anyway.
# LAST, because it is the only section that leaves the window a size nothing
# else predicted — everything above compares against exact boxes.
NOW=$(box)
NX=${NOW%%,*}; nrest=${NOW#*,}; NY=${nrest%% *}
NW=$(echo "$NOW" | sed -n 's/.* \([0-9]*\)x[0-9]*/\1/p')
NH=$(echo "$NOW" | sed -n 's/.*x\([0-9]*\)/\1/p')
MIDY=$(( NY + NH / 2 ))
DRAG_FROM=$(( NX - 1 ))
DRAG_TO=20
[ "$DRAG_TO" -lt "$DRAG_FROM" ] || fail "the drag test needs room to the left of
       the window; it starts at $DRAG_FROM"
vclick "$DRAG_FROM" "$MIDY" drag "$DRAG_TO" "$MIDY"
wait_box "$NOW"
DRAGGED=$(box)
DW=$(echo "$DRAGGED" | sed -n 's/.* \([0-9]*\)x[0-9]*/\1/p')
DH=$(echo "$DRAGGED" | sed -n 's/.*x\([0-9]*\)/\1/p')
[ "$DW" -gt "$NW" ] || fail "dragging the LEFT border leftwards gave $DRAGGED —
       the width should have GROWN from $NW. Arming the border press broke
       ordinary resizing."
[ "$DH" = "$NH" ] || fail "dragging the left border changed the height too
       ($NH -> $DH) — the geobox was not re-taken after the armed press."
ok "dragging a border still resizes (width $NW -> $DW, height unchanged)"

kill -TERM "$CLIENT_PID" 2>/dev/null; wait "$CLIENT_PID" 2>/dev/null; CLIENT_PID=
cleanup
echo
echo "$pass checks passed"
