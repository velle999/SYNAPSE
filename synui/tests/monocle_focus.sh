#!/bin/sh
# monocle_focus.sh — on a monocle desktop, does the window you focus become the
# window you can SEE?
#
# velle, 2026-07-31, asking what the monocle layout does for Super+Tab. It did
# not do what layout.c's own header said it did ("MONOCLE — All windows
# fullscreen, cycle with Alt+Tab").
#
# layout_monocle() enables exactly one scene node per output and disables every
# other window on that output, keyed off s->focused_view. But that toggle —
# layout.c's only `set_enabled(view_node(v), v == top)` — runs from layout_apply()
# and nowhere else, and NOTHING on the focus path called layout_apply: not
# focus_view(), not focus_next(), not alttab_step() or alttab_finish(). So
# Alt+Tab moved the keyboard to a window that stayed hidden. Measured before the
# fix, three windows, after one alt_tab:
#
#   pid A  focused=false  enabled=true    <- still the one on screen
#   pid B  focused=true   enabled=false   <- has the keyboard, invisible
#   pid C  focused=false  enabled=false
#
# You were typing into a window you could not see, and it only came right when
# something unrelated reflowed the desktop — a window opening or closing, a
# desktop switch, Super+F, retile, a config reload. Which is also why this was
# easy to miss by hand: almost anything you try next fixes it.
#
# The invariant, and the whole test: on a monocle desktop, exactly one window
# per output is enabled, and it is the focused one. Checked after each of the
# three ways to change focus that do not otherwise touch the layout.
#
# `enabled` is synctl's own answer to "why is this window invisible when every
# other field says it is fine" (ipc.c) — mapped && has a node && node->enabled.
#
# Usage: monocle_focus.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: monocle_focus.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: monocle_focus.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: monocle_focus.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-monocle.XXXXXX) || exit 1
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
n_enabled()  { clients | grep -c '"enabled":true'; }
# The line for the focused window, or empty if nothing has focus.
focused()    { clients | grep '"focused":true'; }

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

# The invariant. $1 names what we just did, for the failure message.
check_monocle() {
    what=$1
    n=$(n_enabled)
    [ "$n" = 1 ] || fail "$what: $n windows enabled on a monocle desktop, want
       exactly 1. layout_monocle shows one window per output and this rig has a
       single headless output, so anything else means the layout did not run.
       Live state:
$(clients)"
    echo "$(focused)" | grep -q '"enabled":true' || fail "$what: the focused
       window is NOT the one on screen. This is the original bug — the keyboard
       moved and the scene node did not, so you are typing into a window you
       cannot see. focus_view() reflows a monocle desktop precisely so that
       these two agree.
       Live state:
$(clients)"
}

# ── 1. three windows, then switch the desktop to monocle ─────────────────
[ "$(layout_now)" = tiling ] || fail "expected tiling at startup, got $(layout_now)"
open_window 1
open_window 2
open_window 3
[ "$(n_enabled)" = 3 ] || fail "a tiling desktop hid one of its 3 windows
       (enabled count $(n_enabled)); nothing below would mean anything."

# Super+Tab twice: tiling -> floating -> monocle.
synctl dispatch layout_cycle >/dev/null
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = monocle ] || fail "two layout_cycles did not reach monocle,
       got $(layout_now)"
check_monocle "on entering monocle"
echo "enter:    3 windows, 1 on screen, and it is the focused one"

# ── 2. Alt+Tab ───────────────────────────────────────────────────────────
# A full cycle: the step focuses, the commit is what a real Alt release does
# (only keyboard_handle_modifiers sees that, so it cannot be synthesised here).
before=$(focused)
synctl dispatch alt_tab >/dev/null
synctl dispatch alt_tab_commit >/dev/null
[ "$(focused)" != "$before" ] || fail "alt_tab did not move focus at all, so the
       interesting half of this test never ran"
check_monocle "after Alt+Tab"
echo "alt_tab:  the window Alt+Tab landed on is the window on screen"

# ── 3. Super+J (focus_next) ──────────────────────────────────────────────
# The other cycle, and a separate code path: focus_next walks STACKING order
# rather than MRU. It reaches focus_view the same way, which is the point —
# the reflow lives at the choke point, not in either cycle.
before=$(focused)
synctl dispatch focus_next >/dev/null
[ "$(focused)" != "$before" ] || fail "focus_next did not move focus"
check_monocle "after Super+J"
echo "focus_j:  stacking-order focus reveals its window too"

synctl dispatch focus_prev >/dev/null
check_monocle "after Super+K"
echo "focus_k:  and backwards"

# ── 4. a floating window is EXEMPT ───────────────────────────────────────
# Monocle skips floating windows entirely (layout_monocle `continue`s over
# them), which is how you get a dialog, a picker or a video over the full-screen
# window — and the reason Super+F is the answer to "I need two things at once"
# on this layout. So here the count goes to 2 and stays there, and the invariant
# above no longer applies.
synctl dispatch float_toggle >/dev/null
[ "$(n_enabled)" = 2 ] || fail "floating a window on a monocle desktop left
       $(n_enabled) enabled, want 2 (the floating one plus the layout's pick).
       If this is 1, monocle has started hiding floating windows and there is no
       longer any way to see two windows at once on this layout."
echo "$(focused)" | grep -q '"floating":true' || fail "float_toggle did not float
       the focused window"
echo "float:    a floated window stays visible alongside the monocle window"

# Un-float it and the desktop goes back to one.
synctl dispatch float_toggle >/dev/null
check_monocle "after un-floating"
echo "unfloat:  back to one window on screen"

# ── 5. clean shutdown ────────────────────────────────────────────────────
for p in $CLIENT_PIDS; do kill -TERM "$p" 2>/dev/null; done
CLIENT_PIDS=

kill -TERM "$SYNUI_PID" 2>/dev/null
wait "$SYNUI_PID" 2>/dev/null
rc=$?
SYNUI_PID=
[ "$rc" = 0 ] || fail "synui exited $rc (ASan/LSan report above)"

cleanup
echo "PASS: monocle shows the window you focused"
