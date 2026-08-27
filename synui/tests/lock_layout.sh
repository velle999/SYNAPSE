#!/bin/sh
# lock_layout.sh — the keyboard-layout selector, end to end through synctl.
#
# THE BUG THIS EXISTS FOR is not on the desktop. `xkb_layout = us,no` has always
# compiled into a two-group keymap; what did not exist was any way to see which
# group was typing, and the screen where that matters is the LOGIN screen — a
# password typed in the wrong layout is rejected exactly like a wrong password,
# and nothing on that screen could say which had happened.
#
# The chip is the GUI half. This tests the half a rig can drive:
#
#   1. `synctl layout` lists what the KEYMAP has, in the names synuirc used
#   2. next/prev walk it and wrap
#   3. a name or an index goes straight to one
#   4. a layout the keymap does not have is REFUSED, not silently clamped
#   5. ⚠ a layout xkb could not resolve is not offered at all
#
# ⚠ 5 IS THE ONE WORTH THE RIG. `xkb_layout = us,zz` compiles to ONE group —
# xkb drops what it cannot resolve — so a selector that counted the commas in
# the config string would offer `zz`, print it on the chip, and leave the keys
# on `us`. That is worse than no chip: it is a label that lies about the exact
# thing it exists to tell the truth about. Counting comes off the keymap.
#
# ⚠ NO KEYBOARD ON A HEADLESS SEAT, which is the second thing this pins. The
# session remembers the group itself (syn_server::kbd_layout) precisely so that
# a keyboard arriving later adopts it rather than landing back on the first
# layout — the laptop-plus-external-keyboard bug. With no device at all that
# memory IS the answer, so the walk below is exercising it.
#
# Usage: lock_layout.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, like every other rig here.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: lock_layout.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: lock_layout.sh /path/to/synui /path/to/synctl}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

# SHORT, under /tmp: the control socket is a unix path and those cap at 108
# bytes, which a build directory under a long $HOME blows on its own — the
# failure is `ipc: path too long` in the log and no socket at all.
TMP=$(mktemp -d /tmp/synui-kbd.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup INT TERM EXIT

fail() { echo "FAIL: $*" >&2; tail -20 "$LOG" 2>/dev/null >&2; exit 1; }

# Hermetic, and SYNUI_SOCKET unset: it is exported inside a live synui session
# and synctl PREFERS it over XDG_RUNTIME_DIR, so a rig that leaves it set walks
# the layouts on the developer's real desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
# ⛔ AND THE GREETER PUBLISH ROOT, or this rig writes into /var/lib/synui/greeter
# and replaces the developer's real login-screen background with its own empty
# one. greeterbg_publish runs at startup on every session, test or not.
export SYNUI_GREETER_BG_DIR="$TMP/pub"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY SYNUI_SOCKET

start_synui() {
    printf '%s\n' "$1" > "$SYNUI_CONFIG"
    printf 'autostart =\ndock_enabled = off\ndesktop_icons = off\n' >> "$SYNUI_CONFIG"
    : > "$SYNUI_WINDOWS"
    "$SYNUI" -d >"$LOG" 2>&1 &
    SYNUI_PID=$!

    SOCK=
    i=0
    while [ $i -lt 100 ]; do
        for c in "$TMP"/wayland-*; do
            case "$c" in *.lock) continue ;; esac
            [ -S "$c" ] && SOCK=$(basename "$c") && break
        done
        [ -n "$SOCK" ] && [ -S "$TMP/synui-$SOCK.sock" ] && break
        kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
        i=$((i + 1)); sleep 0.1
    done
    [ -n "${SOCK:-}" ] || fail "no wayland socket after 10s"
    CTLSOCK="$TMP/synui-$SOCK.sock"
    [ -S "$CTLSOCK" ] || fail "no control socket at $CTLSOCK"
}

stop_synui() {
    kill "$SYNUI_PID" 2>/dev/null
    wait "$SYNUI_PID" 2>/dev/null
    SYNUI_PID=
    rm -f "$TMP"/wayland-* "$TMP"/synui-*.sock
}

layout() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" layout "$@" 2>/dev/null; }
active() { layout "$@" | sed -n 's/.*"active":\([0-9]*\).*/\1/p'; }

# ── 1. two layouts, named as synuirc named them ─────────────────────────
start_synui 'xkb_layout = us,no'

GOT=$(layout)
echo "$GOT" | grep -q '"layouts":\["us","no"\]' \
    || fail "synctl layout did not list the two configured layouts: $GOT"
[ "$(active)" = "0" ] || fail "the first layout is not the active one: $GOT"
echo "list:      us,no — active us"

# ── 2. next and prev walk, and wrap ─────────────────────────────────────
[ "$(active next)" = "1" ] || fail "layout next did not move off group 0.
       With no keyboard on the seat the session's own memory of the group is
       the whole answer — and that memory is what a keyboard plugged in later
       adopts, so a walk that does not move here is one that will not survive
       a hotplug either."
[ "$(active next)" = "0" ] || fail "layout next did not wrap back to the first"
[ "$(active prev)" = "1" ] || fail "layout prev did not wrap backwards"
echo "walk:      next/prev move and wrap"

# ── 3. by name and by index ─────────────────────────────────────────────
[ "$(active us)" = "0" ] || fail "layout us did not select the first layout"
[ "$(active 1)"  = "1" ] || fail "layout 1 did not select the second layout"
[ "$(active no)" = "1" ] || fail "layout no did not select the second layout"
echo "pick:      by name and by index"

# ── 4. an unknown layout is refused, not clamped ────────────────────────
# A silent clamp would report success and leave the keys where they were, which
# is the same failure mode as the chip that lies.
BAD=$(layout zz)
echo "$BAD" | grep -q '"error"' \
    || fail "an unknown layout was accepted rather than refused: $BAD"
[ "$(active)" = "1" ] || fail "a refused layout moved the active one anyway"
echo "refuse:    an unknown name is an error and changes nothing"

stop_synui

# ── 5. ⚠ the KEYMAP is the count, not the config string ─────────────────
# `zz` is not a layout xkb can resolve, so the keymap has ONE group. A selector
# that split the config on commas would offer two.
start_synui 'xkb_layout = us,zz'
GOT=$(layout)
echo "$GOT" | grep -q '"layouts":\["us"\]' \
    || fail "a layout xkb could not resolve was still offered: $GOT
       The keymap has one group, so the chip would print 'zz' while the keys
       stayed 'us' — a label that lies about the one thing it is for."
echo "truth:     an unresolvable layout is not offered"

stop_synui

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG" 2>/dev/null; then
    fail "sanitizer reported errors"
fi

echo "lock_layout: 5 phases passed"
