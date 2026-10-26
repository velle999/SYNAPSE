#!/bin/sh
# cursor_hide.sh — the cursor hides over a fullscreen window and the screensaver
#
# THE FEATURE (synui 644): a film, a stream or the screensaver had the arrow
# parked on it until somebody nudged the mouse. Now the cursor hides once the
# pointer has rested `cursor_hide_fullscreen` seconds over a fullscreen window,
# and for as long as the screensaver is up; pointer input brings it back.
#
# ⚠ THE PROPERTY THAT MATTERS MOST IS WHAT DOES NOT HAPPEN: hiding and showing
# must never move pointer focus. The usual way to get a client's cursor back is
# to clear focus and re-enter, and over a fullscreen GAME that destroys a
# oneshot pointer lock (synui 512). So the client here prints `enter`/`leave`,
# and the count must not move across a hide and a show — while the image it
# set comes back as its own surface, and a client that asked for NO cursor
# gets none back.
#
# Two compositors, because the screensaver's idle stage counts from the last
# input and would fire in the middle of the fullscreen phases:
#
#   1. cursor_hide_fullscreen = 1, no saver. Not fullscreen: never hides.
#      Fullscreen: hides after the count; motion shows it at once with the
#      client's own image and no focus change; it hides again; leaving
#      fullscreen shows it with no motion at all.
#   2. cursor_hide_fullscreen = 0 and a 4 s screensaver, a client that hides
#      its own cursor. Fullscreen never hides (0 = off); the saver does; the
#      motion that dismisses the saver shows it, still as "none".
#
# Usage: cursor_hide.sh synui stubborn_client synctl vpointer_click
# Skips (77) without a DRM render node, like every other rig here.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: cursor_hide.sh synui stubborn_client synctl vpointer_click}
CLIENT=${2:?usage: cursor_hide.sh synui stubborn_client synctl vpointer_click}
SYNCTL=${3:?usage: cursor_hide.sh synui stubborn_client synctl vpointer_click}
VPTR=${4:?usage: cursor_hide.sh synui stubborn_client synctl vpointer_click}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -30 "$LOG" >&2; cleanup; exit 1; }
pass() { echo "  ok    $*"; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${CLIENT_PID:-}" ] && kill -9 "$CLIENT_PID" 2>/dev/null
    SYNUI_PID= CLIENT_PID=
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

# Whoever runs this may be sitting IN a synui session. Nothing here may reach it.
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY SYNUI_SOCKET
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1

# start CONFIG-LINES CLIENT-CURSOR — a private synui with one window mapped and
# the pointer resting on it.
start() {
    TMP=$(mktemp -d /tmp/synui-curhide.XXXXXX) || exit 1
    chmod 700 "$TMP"
    LOG="$TMP/synui.log"
    export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
    export SYNUI_CONFIG="$TMP/synuirc" SYNUI_WINDOWS="$TMP/windows.conf"
    # No welcome guide: a scratch HOME is a first run, and the guide is a
    # full-screen surface that takes pointer focus off the window under test.
    printf 'animation_ms = 0\nwelcome_at_startup = off\n%s\n' "$1" > "$SYNUI_CONFIG"

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
    CTLSOCK="$TMP/synui-$SOCK.sock"

    WAYLAND_DISPLAY="$SOCK" "$CLIENT" 0 600 0 0 "$2" >"$TMP/client.out" 2>"$TMP/client.err" &
    CLIENT_PID=$!
    i=0
    while [ -z "$(win_size)" ]; do
        [ $i -ge 40 ] && fail "the client never mapped: $(cat "$TMP/client.err")"
        i=$((i + 1)); sleep 0.1
    done
    # Point only once the client has PAINTED at the size the layout gave it:
    # pointed at the box's middle before that, the pointer is off the smaller
    # buffer still on screen, and nothing re-derives focus when it grows.
    i=0
    until painted; do
        [ $i -ge 40 ] && fail "the client never painted the size the layout gave it: $(tail -3 "$TMP/client.out")"
        i=$((i + 1)); sleep 0.1
    done
    # And point again until focus HOLDS: a fresh session clears pointer focus
    # once while it settles, and a window that is not focused gets no cursor.
    i=0
    until [ "$(pfocus)" = stubborn ] && sleep 0.5 && [ "$(pfocus)" = stubborn ]; do
        [ $i -ge 10 ] && fail "the window never kept pointer focus: $(synctl pointer)"
        point_at_window $i
        i=$((i + 1)); sleep 0.2
    done
}

synctl()  { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
vmove()   { WAYLAND_DISPLAY="$SOCK" "$VPTR" "$1" "$2" move >/dev/null 2>&1; }
win()     { synctl activewindow; }
win_size(){ win | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1 \2/p'; }
win_at()  { win | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),\(-\{0,1\}[0-9]*\)\].*/\1 \2/p'; }
win_fs()  { win | sed -n 's/.*"fullscreen":\(true\|false\).*/\1/p'; }
hidden()  { synctl pointer | sed -n 's/.*"hidden":\[\([^]]*\)\].*/\1/p'; }
image()   { synctl pointer | sed -n 's/.*"image":"\([^"]*\)".*/\1/p'; }
pfocus()  { synctl pointer | sed -n 's/.*"pointer_focus":"\([^"]*\)".*/\1/p'; }
enters()  { grep -c '^enter$' "$TMP/client.out"; }
# The client paints its own 552x304 first, then whatever the layout configures.
painted() { grep '^commit ' "$TMP/client.out" | grep -qv '^commit 552x304$'; }
leaves()  { grep -c '^leave$' "$TMP/client.out"; }

# The middle of the window, nudged by $1 px so two calls are two motions.
point_at_window() {
    set -- "${1:-0}" $(win_at) $(win_size)
    vmove $(( $2 + $4 / 2 + $1 )) $(( $3 + $5 / 2 + $1 ))
}

# wait_hidden WANT SECONDS — poll until `hidden` reads WANT.
wait_hidden() {
    n=$(( $2 * 10 ))
    i=0
    while [ "$(hidden)" != "$1" ]; do
        [ $i -ge $n ] && return 1
        i=$((i + 1)); sleep 0.1
    done
    return 0
}

# wait_image WANT — the client prints `enter` before its set_cursor is flushed
# (its loop sleeps between roundtrips), so its image arrives a moment later.
wait_image() {
    i=0
    while [ "$(image)" != "$1" ]; do
        [ $i -ge 20 ] && return 1
        i=$((i + 1)); sleep 0.1
    done
    return 0
}

echo "cursor_hide"

# ── 1. the fullscreen countdown ──────────────────────────────────────────
start "cursor_hide_fullscreen = 1" surface

wait_image surface || fail "the client's own cursor surface is not the
       image: image=$(image) — the fixture's set_cursor did not arrive"

sleep 2
[ "$(hidden)" = "" ] || fail "hidden over a window that is not fullscreen: $(synctl pointer)"
pass "a window that is not fullscreen keeps the cursor"

synctl dispatch fullscreen_toggle >/dev/null
[ "$(win_fs)" = true ] || fail "fullscreen_toggle did not take: $(win)"
point_at_window 3               # the window moved under a still pointer
e0=$(enters) l0=$(leaves)

wait_hidden '"fullscreen"' 4 || fail "resting over a fullscreen window never
       hid the cursor: $(synctl pointer)"
pass "resting over a fullscreen window hides it"
[ "$(image)" = surface ] || fail "hiding lost the client's image: $(image)"

point_at_window 6
wait_hidden '' 1 || fail "motion did not show the cursor again: $(synctl pointer)"
[ "$(image)" = surface ] || fail "the cursor came back as $(image), not the
       client's own surface"
pass "motion shows it again, as the client's own image"

# The client prints from a loop that sleeps between roundtrips, so a leave and
# an enter it was sent would show up here a moment late. Give them the moment.
sleep 0.5
[ "$(enters)" = "$e0" ] && [ "$(leaves)" = "$l0" ] || fail "pointer focus moved
       across a hide and a show: enter $e0 -> $(enters), leave $l0 -> $(leaves).
       Over a game that destroys a oneshot pointer lock."
pass "pointer focus never moved (no leave, no enter)"

wait_hidden '"fullscreen"' 4 || fail "it did not hide again after the motion"
pass "it hides again once the pointer rests"

synctl dispatch fullscreen_toggle >/dev/null
[ "$(win_fs)" = false ] || fail "the second fullscreen_toggle did not take"
wait_hidden '' 1 || fail "leaving fullscreen left the cursor hidden until the
       mouse moves: $(synctl pointer)"
pass "leaving fullscreen shows it with no motion"
cleanup

# ── 2. off, and the screensaver ──────────────────────────────────────────
start "cursor_hide_fullscreen = 0
screensaver = clock
screensaver_timeout = 4" none

wait_image none || fail "the client asked for no cursor, image=$(image)"
synctl dispatch fullscreen_toggle >/dev/null
[ "$(win_fs)" = true ] || fail "fullscreen_toggle did not take: $(win)"
point_at_window 3
sleep 2
[ "$(hidden)" = "" ] || fail "cursor_hide_fullscreen = 0 still hid it: $(synctl pointer)"
pass "cursor_hide_fullscreen = 0 never hides it"

i=0
until grep -q 'saver: showing' "$LOG"; do
    [ $i -ge 60 ] && fail "the screensaver never came up"
    i=$((i + 1)); sleep 0.1
done
[ "$(hidden)" = '"screensaver"' ] || fail "the screensaver is up and the cursor
       is not hidden: $(synctl pointer)"
pass "the screensaver hides it"

point_at_window 6
wait_hidden '' 1 || fail "dismissing the screensaver left it hidden: $(synctl pointer)"
# "(teardown)": power.c takes the saver down on motion with by_input false,
# which is what keeps that motion from also locking the screen.
grep -q 'saver: dismissed' "$LOG" || fail "the motion did not dismiss the saver"
[ "$(image)" = none ] || fail "a client that hides its own cursor got $(image) back"
pass "dismissing it shows the cursor, and a client's 'none' stays none"
cleanup

echo "cursor_hide: all passed"
