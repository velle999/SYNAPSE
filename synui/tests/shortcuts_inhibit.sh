#!/bin/sh
# shortcuts_inhibit.sh — a window that asks for the keys gets synui's binds
#
# keyboard-shortcuts-inhibit-v1 is how Moonlight in fullscreen and gtk-vnc's
# keyboard grab ask for the keys a compositor would otherwise act on. Before
# synui answered it, Super+O pressed in Moonlight on the ThinkPad moved the
# MOONLIGHT WINDOW between the ThinkPad's screens and never reached the desktop
# being streamed.
#
# What this pins, with a bind that leaves a file behind when it fires:
#
#   1. THE CONTROL. A window that did not ask: Super+Y runs synui's bind, and
#      the window never sees the key. Without this, a bind that never fired
#      would pass phase 2.
#   2. THE GRANT. A window that asked, with the keyboard on it, is told it is
#      active; Super+Y then reaches the window and the bind does not run.
#   3. THE GRANT FOLLOWS FOCUS. Another window takes the keyboard: the first is
#      told it is inactive, and the same Super+Y runs the bind again — an
#      inhibiting window cannot hold synui's keys once it has lost focus.
#
# Headless and hermetic; skips (77) without a render node or wtype.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: shortcuts_inhibit.sh /path/to/synui /path/to/synctl /path/to/inhibit_client}
SYNCTL=${2:?usage: shortcuts_inhibit.sh /path/to/synui /path/to/synctl /path/to/inhibit_client}
CLIENT=${3:?usage: shortcuts_inhibit.sh /path/to/synui /path/to/synctl /path/to/inhibit_client}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
if ! command -v wtype >/dev/null; then
    echo "SKIP: wtype is not installed (the keys are typed with it)."
    exit 77
fi

# ⚠ Before anything else: this rig types Super+Y, and a WAYLAND_DISPLAY left
# pointing at the live desktop would type it there.
unset SYNUI_SOCKET WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

TMP=$(mktemp -d /tmp/synui-inhibit.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
BOUND="$TMP/bound"

fail() { echo "FAIL: $*" >&2; [ -s "$LOG" ] && tail -30 "$LOG" >&2; cleanup; exit 1; }
ok()   { echo "  ok    $*"; }
cleanup() {
    for p in ${C1:-} ${C2:-} ${KBHOLD:-}; do kill "$p" 2>/dev/null; done
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"
# A bind whose only effect is a file: whether it ran is a test -e away.
printf 'power_blank_timeout = 0\npower_dim_timeout = 0\nbind = super+y spawn touch %s\n' \
    "$BOUND" > "$SYNUI_CONFIG"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1

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

wait_for() {   # wait_for <file> <line> — up to 5s
    n=0
    while [ $n -lt 50 ]; do
        grep -qx "$2" "$1" 2>/dev/null && return 0
        n=$((n + 1)); sleep 0.1
    done
    return 1
}
super_y() { wtype -M logo -k y -m logo || fail "wtype could not type Super+Y"; sleep 0.5; }

# ⚠ A KEYBOARD THAT OUTLIVES EACH KEYSTROKE. A headless synui has no keyboard
# until a virtual one connects, and each `wtype` takes its own away again when
# it exits — so a window would get the keyboard, lose it, and get it back around
# every keystroke. One idle wtype holds a keyboard for the whole run.
wtype -s 60000 >/dev/null 2>&1 &
KBHOLD=$!
sleep 0.5
# ⚠ COUNTED, NOT NAMED. wtype builds its own keymap and numbers the keys it
# types from 1 upward, so Y does not arrive as evdev 21 — a check for that code
# never matched, which made the control's "the window never saw it" vacuous.
presses() { grep -cE '^key [0-9]+ 1$' "$1" 2>/dev/null || true; }

# ── 1. the control: a window that did not ask ────────────────────────────
"$CLIENT" plain 6 > "$TMP/c0" 2>&1 &
C1=$!
wait_for "$TMP/c0" enter || fail "the plain window never got the keyboard: $(cat "$TMP/c0")"
# The window can be typed into at all — otherwise "it never saw Super+Y" below
# would be true of a window that sees nothing.
n0=$(presses "$TMP/c0"); wtype a; sleep 0.4
[ "$(presses "$TMP/c0")" -gt "$n0" ] || fail "the plain window never received a plain key: $(cat "$TMP/c0")"
n0=$(presses "$TMP/c0")
super_y
[ -e "$BOUND" ] || fail "Super+Y did not run the bind with an ordinary window focused — the control proves nothing"
[ "$(presses "$TMP/c0")" -eq "$n0" ] || fail "the plain window saw Super+Y as well as the bind running"
ok "with an ordinary window focused, Super+Y runs synui's bind"
kill "$C1" 2>/dev/null; wait "$C1" 2>/dev/null; C1=
rm -f "$BOUND"

# ── 2. a window that asked gets the key, and the bind does not run ───────
"$CLIENT" inhibit 8 > "$TMP/c1" 2>&1 &
C1=$!
wait_for "$TMP/c1" active || fail "the inhibiting window was never told it is active: $(cat "$TMP/c1")"
ok "the window that asked, with the keyboard on it, is granted the keys"
n1=$(presses "$TMP/c1")
super_y
[ -e "$BOUND" ] && fail "Super+Y ran synui's bind although the focused window had asked for the keys"
[ "$(presses "$TMP/c1")" -gt "$n1" ] || fail "the window that asked never saw Super+Y: $(cat "$TMP/c1")"
ok "Super+Y reaches that window, and synui's bind does not run"

# ── 3. focus leaves: the grant is withdrawn and the bind is synui's again ─
"$CLIENT" plain 4 > "$TMP/c2" 2>&1 &
C2=$!
wait_for "$TMP/c2" enter || fail "the second window never got the keyboard: $(cat "$TMP/c2")"
wait_for "$TMP/c1" inactive || fail "the first window kept its grant after losing focus: $(cat "$TMP/c1")"
ok "focus moving away withdraws the grant"
super_y
[ -e "$BOUND" ] || fail "Super+Y did not run the bind once the inhibiting window lost focus"
ok "...and Super+Y is synui's again"

for p in $C1 $C2; do kill "$p" 2>/dev/null; wait "$p" 2>/dev/null; done
C1= C2=
kill "$SYNUI_PID" 2>/dev/null
wait "$SYNUI_PID" 2>/dev/null
SYNUI_PID=
cleanup
echo "shortcuts_inhibit: all checks passed"
