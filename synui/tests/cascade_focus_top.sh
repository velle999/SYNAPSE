#!/bin/sh
# cascade_focus_top.sh — on a cascade desktop, does the window you focused stay
# IN FRONT when something reflows the desktop?
#
# velle, 2026-08-12: "windows in background resetting to foreground when i
# change volume, have noticed similar things". Desktop 1 is cascade, and cascade
# is the one layout whose arrangement IS its stacking order — layout_cascade()
# raises every window in ws->windows list order so each pile's front card lands
# on top. List order is not focus order, so a reflow re-buried whatever the user
# had brought forward. Measured before the fix, three windows on the pile, after
# focusing the FIRST one and retiling:
#
#   stubborn A  focused=true   stack=0    <- what you were typing in, at the back
#   stubborn B  focused=false  stack=1
#   stubborn C  focused=false  stack=2    <- on top, untouched for minutes
#
# layout_cascade's comment claimed this could not happen: "layout_apply() is not
# run on focus changes (only monocle asks for that), so a window raised by
# focus_view stays raised". True of the FOCUS path and false of every other one.
# The volume OSD is a layer surface, so it ran layer_arrange_output() when it
# mapped and again 1.6s later when it unmapped, and that called layout_apply()
# unconditionally — on a box that had not moved, because the OSD reserves no
# space at all (exclusionMode=Ignore). Two reflows per volume step. A window
# closing, a bar auto-hiding and an output change all did it too, which is what
# "similar things" was.
#
# Fixed at both ends and this file tests the one that holds regardless of the
# trigger: layout_cascade raises the focused view LAST, so focus_view()'s raise
# survives a retile. (The other end — layer_arrange_output only reflowing when
# the usable box actually moved — is what stops the reflow happening at all.)
#
# The observable is synctl's `stack`: the window's index among window_tree's
# children, 0 at the bottom, highest on top. Added with this test for the same
# reason `enabled` was added for the Steam wedge — every other field reported
# these windows as perfectly healthy.
#
# Usage: cascade_focus_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: cascade_focus_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: cascade_focus_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: cascade_focus_top.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-cascfocus.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# animation_ms = 0 or a stack read lands mid-fade — the same reason every other
# layout test sets it (see reference: the layout tests race the animation).
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

# "<pid> <stack> <focused>" per window, in ws->windows list order.
stacks() {
    clients | sed -n 's/.*"pid":\([0-9]*\).*"stack":\(-\{0,1\}[0-9]*\).*/\1 \2/p' >"$TMP/s1"
    clients | sed -n 's/.*"focused":\(true\|false\).*/\1/p' >"$TMP/s2"
    paste -d' ' "$TMP/s1" "$TMP/s2"
}
top_pid()     { stacks | sort -k2 -n | tail -1 | cut -d' ' -f1; }
focused_pid() { stacks | awk '$3=="true"{print $1}'; }

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

# ── 0. three windows on a cascade desktop ────────────────────────────────
open_window 1
open_window 2
open_window 3
synctl dispatch cascade >/dev/null
[ "$(layout_now)" = cascade ] || fail "dispatch cascade left the layout at $(layout_now)"
[ "$(stacks | wc -l)" = 3 ] || fail "expected 3 windows, got:
$(stacks)"

# The field itself has to be real before anything is asserted with it: three
# distinct indices, none of them -1 (not a child of window_tree).
NSTACK=$(stacks | cut -d' ' -f2 | sort -u | grep -cv '^-1$')
[ "$NSTACK" = 3 ] || fail "synctl's stack index is not reporting three distinct
       windows — every assertion below is vacuous without it.
$(stacks)"
echo "cascade:  3 windows, stack $(stacks | cut -d' ' -f2 | tr '\n' ' ')"

# ── 1. focusing a window puts it in front ────────────────────────────────
# Walk focus until it lands on a window that was NOT the one on top. Focusing
# the window already in front proves nothing — the phase-2 assertion would then
# pass on a compositor that never restacks at all, which is the whole bug.
TOP0=$(top_pid)
i=0
while [ $i -lt 4 ]; do
    synctl dispatch focus_next >/dev/null
    sleep 0.15
    i=$((i + 1))
    [ "$(focused_pid)" != "$TOP0" ] && break
done
FOCUS=$(focused_pid)
[ -n "$FOCUS" ] || fail "no window is focused after $i focus_next:
$(stacks)"
[ "$FOCUS" != "$TOP0" ] || fail "focus_next never moved the focus off pid $TOP0,
       the window that was already in front — phase 2 would be vacuous.
$(stacks)"
[ "$FOCUS" = "$(top_pid)" ] || fail "focus_view() did not raise pid $FOCUS to the
       top of the stack — this is broken before any reflow is involved.
$(stacks)"
echo "focus:    pid $FOCUS (was behind pid $TOP0) focused and on top after $i focus_next"

# ── 2. THE BUG: a reflow must not re-bury it ─────────────────────────────
# `dispatch cascade` on a desktop already cascading is the cheapest reflow that
# does not touch focus: it reclaims nothing, switches nothing, and runs
# layout_apply(). What velle actually hit was layer_arrange_output()'s reflow
# when the volume OSD mapped, but the trigger is not the invariant — ANY
# layout_apply() on a cascade desktop used to restack it.
#
# NOT `dispatch retile`: that is a switch TO tiling (input.c — "tiling is a
# destination, not a modifier"), so it reflows a desktop that is no longer
# cascade and layout_cascade never runs. The first cut of this test used it and
# passed against the unfixed compositor for that reason.
synctl dispatch cascade >/dev/null
sleep 0.3

[ "$(layout_now)" = cascade ] || fail "the reflow left the layout at
       $(layout_now) — layout_cascade did not run and this proves nothing."
[ "$(focused_pid)" = "$FOCUS" ] || fail "the reflow moved the focus off pid $FOCUS,
       so the stack assertion below would not mean anything.
$(stacks)"
[ "$(top_pid)" = "$FOCUS" ] || fail "a reflow dropped the FOCUSED window (pid
       $FOCUS) to stack $(stacks | awk -v p="$FOCUS" '$1==p{print $2}') and put pid
       $(top_pid) in front of it. This is the reported bug: layout_cascade raises
       in ws->windows list order, and the focused window is not last in that
       list, so every reflow buries the window you are working in. It has to
       raise the focused view LAST.
$(stacks)"
echo "reflow:   pid $FOCUS still on top after a cascade reflow"

# Three times, because the volume OSD reflowed twice per keypress (map, then
# unmap 1.6s later) — a fix that survived one pass and not the next would still
# show the window flicking backwards on every volume step.
synctl dispatch cascade >/dev/null
synctl dispatch cascade >/dev/null
sleep 0.3
[ "$(top_pid)" = "$FOCUS" ] || fail "pid $FOCUS survived one reflow and not three
       — the raise is not idempotent.
$(stacks)"
echo "repeat:   pid $FOCUS still on top after three reflows"

# ── 3. and focus still moves ─────────────────────────────────────────────
# The cheap wrong fix is to pin the front card to whatever was raised first.
# Focus has to keep taking the top, or cascade stops being usable.
synctl dispatch focus_next >/dev/null
sleep 0.2
NEXT=$(focused_pid)
[ "$NEXT" != "$FOCUS" ] || fail "focus_next did not move the focus off pid $FOCUS"
[ "$(top_pid)" = "$NEXT" ] || fail "focus moved to pid $NEXT but pid $(top_pid) is
       still in front — the fix pinned the stack instead of following the focus.
$(stacks)"
echo "still moves: focus_next brought pid $NEXT to the front"

echo "PASS: the focused window stays in front of a cascade reflow"
cleanup
exit 0
