#!/bin/sh
# cascade_maximize_top.sh — does a window you MAXIMIZED stay in front of the
# arrangement it is no longer part of?
#
# velle, 2026-08-12: "i've double clicked firefox and maximized it but the other
# two windows are being placed on top of it. since i just double clicked the
# window it should take precedence over the tiler at that point". Live desktop
# `main` was cascade, and synctl agreed:
#
#   firefox   floating=true maximized=true  stack=0   <- just double-clicked
#   kitty     floating=false               stack=1
#   synfiles  floating=false               stack=2
#
# A maximized window is floating BY CONSTRUCTION — view_apply_maximized forces
# the flag, or the arrangement would drag it straight back into a pile — and
# every layout skips floating windows. So layout_cascade's raise loop, which
# raises each card it places, never raised it and put every card in front of it.
#
# The double-click case is the sharpest: view_apply_maximized raises the window
# itself and THEN calls layout_apply() to reflow what it left behind, so the
# maximize buried its own window inside a single call. Nothing else had to
# happen — no volume keypress, no bar auto-hide, no second window opening.
#
# Same shape as cascade_focus_top.sh (an arrangement re-burying a window the
# user brought forward) and the same rule fixes both: layout_cascade may order
# the windows it arranges, and nothing else. It now re-raises the views it
# skipped, in their existing stacking order, before raising the focused one.
#
# Phase 2 is the half that does not follow from focus: focus is moved OFF the
# maximized window before the reflow, so a fix that only ever raised the focused
# view would fail there.
#
# Usage: cascade_maximize_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: cascade_maximize_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: cascade_maximize_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: cascade_maximize_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-cascmax.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# animation_ms = 0 or a stack read lands mid-fade — the same reason every other
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

synctl()     { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients()    { synctl clients | tr '{' '\n' | grep '"app_id"'; }
layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }

# "<pid> <stack> <focused> <maximized>" per window, in ws->windows list order.
stacks() {
    clients | sed -n 's/.*"pid":\([0-9]*\).*"stack":\(-\{0,1\}[0-9]*\).*/\1 \2/p' >"$TMP/s1"
    clients | sed -n 's/.*"maximized":\(true\|false\).*/\1/p' >"$TMP/s2"
    clients | sed -n 's/.*"focused":\(true\|false\).*/\1/p' >"$TMP/s3"
    paste -d' ' "$TMP/s1" "$TMP/s3" "$TMP/s2"
}
top_pid()      { stacks | sort -k2 -n | tail -1 | cut -d' ' -f1; }
focused_pid()  { stacks | awk '$3=="true"{print $1}'; }
stack_of()     { stacks | awk -v p="$1" '$1==p{print $2}'; }
max_pids()     { stacks | awk '$4=="true"{print $1}'; }

open_window() {
    want=$1
    "$CLIENT" 0 90 >>"$TMP/client.out" 2>>"$TMP/client.err" &
    LAST_CLIENT=$!
    CLIENT_PIDS="$CLIENT_PIDS $LAST_CLIENT"
    i=0
    while [ $i -lt 40 ]; do
        [ "$(clients | grep -c '"app_id":"stubborn"')" -ge "$want" ] && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "a client never mapped (wanted $want): $(cat "$TMP/client.err")"
}

# ── 0. three windows on a cascade desktop ────────────────────────────────
open_window 1
open_window 2
open_window 3
synctl dispatch cascade >/dev/null
[ "$(layout_now)" = cascade ] || fail "dispatch cascade left the layout at $(layout_now)"
[ "$(stacks | wc -l)" = 3 ] || fail "expected 3 windows, got:
$(stacks)"

NSTACK=$(stacks | cut -d' ' -f2 | sort -u | grep -cv '^-1$')
[ "$NSTACK" = 3 ] || fail "synctl's stack index is not reporting three distinct
       windows — every assertion below is vacuous without it.
$(stacks)"
echo "cascade:  3 windows, stack $(stacks | cut -d' ' -f2 | tr '\n' ' ')"

# ── 1. maximize a window that is NOT already the front card ──────────────
# Maximizing the card that happens to be on top would pass against a compositor
# that never raises anything at all — the same vacuity trap cascade_focus_top.sh
# hits with focus. Walk focus until it lands on a window behind the front one,
# then maximize THAT.
TOP0=$(top_pid)
i=0
while [ $i -lt 4 ]; do
    synctl dispatch focus_next >/dev/null
    sleep 0.15
    i=$((i + 1))
    [ "$(focused_pid)" != "$TOP0" ] && break
done
MAX=$(focused_pid)
[ -n "$MAX" ] || fail "no window is focused after $i focus_next:
$(stacks)"
[ "$MAX" != "$TOP0" ] || fail "focus_next never moved focus off pid $TOP0, the
       window already in front — the assertions below would be vacuous.
$(stacks)"

synctl dispatch maximize_toggle >/dev/null
sleep 0.3

[ "$(max_pids)" = "$MAX" ] || fail "maximize_toggle did not maximize pid $MAX
       (maximized: '$(max_pids | tr '\n' ' ')') — nothing below is being tested.
$(stacks)"
[ "$(layout_now)" = cascade ] || fail "maximizing left the layout at
       $(layout_now); layout_cascade is not what ran."

# THE BUG. view_apply_maximized raises the window and then calls layout_apply()
# to reflow what it left behind — and the cascade's raise loop, which does not
# count a floating window as one of its cards, put every card back in front of
# it before the dispatch even returned.
[ "$(top_pid)" = "$MAX" ] || fail "the window just MAXIMIZED (pid $MAX) is at
       stack $(stack_of "$MAX") with pid $(top_pid) in front of it. A maximized
       window is floating by construction, so layout_cascade places nothing for
       it — and then raised every card it DID place over the top. Double-clicking
       a titlebar buries the window it just maximized, in the same call.
$(stacks)"
echo "maximize: pid $MAX (was behind pid $TOP0) maximized and on top"

# ── 2. and later reflows must not take it back ───────────────────────────
# A window opening and a window closing, which are two of the three triggers
# velle named for the sibling bug ("a bar auto-hiding, a window closing, an
# output change"). NOT `dispatch cascade`: that dispatch runs layout_reclaim
# first, which un-maximizes every window on the desktop by design, so the state
# under test is gone before layout_cascade ever runs. cascade_focus_top.sh can
# use it precisely because nothing there is maximized.
#
# The focused card is legitimately allowed on top — a window that opens or is
# clicked comes forward, maximized neighbour or not. What must not happen is the
# maximized window falling BELOW the cards nobody touched.
assert_max_above_cards() {
    what=$1
    [ "$(layout_now)" = cascade ] || fail "$what left the layout at $(layout_now)
       — layout_cascade did not run and this proves nothing."
    [ "$(max_pids)" = "$MAX" ] || fail "pid $MAX stopped being maximized across
       $what, so the stack assertion would not mean anything.
$(stacks)"
    F=$(focused_pid)
    for p in $(stacks | cut -d' ' -f1); do
        [ "$p" = "$MAX" ] && continue
        [ "$p" = "$F" ] && continue
        [ "$(stack_of "$MAX")" -gt "$(stack_of "$p")" ] || fail "after $what the
       maximized window (pid $MAX, stack $(stack_of "$MAX")) is behind untouched
       card pid $p (stack $(stack_of "$p")). The cascade re-buried a window it
       does not arrange — raising the FOCUSED view last is not enough, the views
       the loop skips have to be raised back too.
$(stacks)"
    done
}

open_window 4
sleep 0.3
NEW=$LAST_CLIENT
assert_max_above_cards "a window opening"
echo "open:     pid $MAX still above every unfocused card after a 4th window mapped"

kill -9 "$NEW" 2>/dev/null
i=0
while [ $i -lt 40 ]; do
    [ "$(stacks | wc -l)" = 3 ] && break
    i=$((i + 1)); sleep 0.1
done
[ "$(stacks | wc -l)" = 3 ] || fail "the 4th window never went away:
$(stacks)"
assert_max_above_cards "a window closing"
echo "close:    pid $MAX still above every unfocused card after that window closed"

# A third reflow, because the volume OSD reflowed twice per keypress — a raise
# that survives one pass and not the next still flickers on every volume step.
open_window 4
sleep 0.3
assert_max_above_cards "a third reflow"
kill -9 "$LAST_CLIENT" 2>/dev/null
i=0
while [ $i -lt 40 ]; do
    [ "$(stacks | wc -l)" = 3 ] && break
    i=$((i + 1)); sleep 0.1
done
echo "repeat:   pid $MAX still in place after three reflows"

# ── 3. un-maximizing hands it back ───────────────────────────────────────
# The cheap wrong fix is to pin the window to the front for good. Once it is an
# ordinary card again it is the arrangement's to order, and focus takes the top.
i=0
while [ $i -lt 4 ]; do
    [ "$(focused_pid)" = "$MAX" ] && break
    synctl dispatch focus_next >/dev/null
    sleep 0.15
    i=$((i + 1))
done
[ "$(focused_pid)" = "$MAX" ] || fail "could not focus pid $MAX again to un-maximize it"
synctl dispatch maximize_toggle >/dev/null
sleep 0.3
[ -z "$(max_pids)" ] || fail "maximize_toggle did not un-maximize pid $MAX"

synctl dispatch focus_next >/dev/null
sleep 0.2
NEXT=$(focused_pid)
[ "$NEXT" != "$MAX" ] || fail "focus_next did not move focus off pid $MAX"
synctl dispatch cascade >/dev/null
sleep 0.3
[ "$(top_pid)" = "$NEXT" ] || fail "pid $MAX is un-maximized and pid $NEXT is
       focused, but pid $(top_pid) is in front — an un-maximized window is an
       ordinary card again and must not keep the front of the stack.
$(stacks)"
echo "release:  un-maximized, focus (pid $NEXT) takes the front again"

echo "PASS: a maximized window stays in front of the cascade it is not part of"
cleanup
exit 0
