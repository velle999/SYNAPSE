#!/usr/bin/env bash
# saver_keys.sh — the Super+Z screensaver panel answers the keyboard
#
# The panel shipped (pkgrel 352) drawing its own footer:
#
#     Up/Down select · Left/Right adjust · p preview
#     s save · Esc close
#
# …and not one of those keys did anything. saver_key() was written, complete and
# correct, in saver.c; input.c's key chain never called it. So the panel opened,
# documented five keys on screen, and answered none of them — the mouse worked,
# because `saver` HAD been added to SYN_PANEL_LIST, which is the roster the
# pointer walks. The key path is a fifth walker of that same list that is kept by
# hand, and this is exactly the bug the comment above SYN_PANEL_LIST warns about.
#
# A build cannot catch it: saver_key() has a prototype and a definition, it is
# just never called, and a non-static function with no callers is not a warning.
# Nothing short of pressing the keys sees it. Hence a rig.
#
# The signals are chosen so a dead key path CANNOT fake them:
#
#   - `p` is the preview key, and saver_show() logs "saver: showing (<mode>)".
#     With the panel open there is nothing else on the key path that logs that.
#   - Down then Right then `s` must write saver.state with the TIMEOUT moved off
#     0 and the mode still `clock`. That pins navigation and adjustment apart:
#     if Down were ignored, Right would land on row 0 and change the MODE
#     instead, which is a different file and a visible failure rather than a
#     quieter one.
#
# Usage: saver_keys.sh /path/to/synui
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

SYNUI=${1:?usage: saver_keys.sh /path/to/synui}

if ! command -v wtype >/dev/null; then
    echo "SKIP wtype not installed (this rig is nothing without it)"
    exit 77
fi

fails=0
fail() { echo "  FAIL $*"; fails=$((fails + 1)); }
pass() { echo "  ok   $*"; }

# Whoever runs this may be sitting IN a synui session, which exports
# SYNUI_SOCKET — and synui's own tooling prefers it over XDG_RUNTIME_DIR. Left
# set, a rig that synthesises keystrokes drives the developer's real desktop.
unset SYNUI_SOCKET WAYLAND_DISPLAY

# Short private runtime dir: unix socket paths cap at 108 bytes, so /tmp rather
# than anywhere under the build tree. An empty SYNUI_CONFIG keeps the run
# hermetic — a developer's synuirc (autostart!) or /etc/synui/synuirc would
# otherwise leak in, and saver.state is read from XDG_CONFIG_HOME too, which is
# the file this test judges by.
TMP=$(mktemp -d /tmp/synui-saverkeys.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1

cleanup() {
    [ -n "${PID:-}" ] && kill -9 "$PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

"$SYNUI" >"$LOG" 2>&1 &
PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$PID" 2>/dev/null || { echo "ABORT synui died during startup:"; tail -5 "$LOG"; exit 1; }
    sleep 0.1
    i=$((i + 1))
done
[ -n "$SOCK" ] || { echo "ABORT no Wayland socket within 10s"; exit 1; }

# The guard that matters: only ever talk to the instance started above. A
# non-headless socket here means the isolation failed and the next wtype would
# land on somebody's desktop.
case "$SOCK" in
    wayland-[0-9]*) ;;
    *) echo "ABORT refusing to drive socket '$SOCK'"; exit 1 ;;
esac
export WAYLAND_DISPLAY="$SOCK"

# wtype's own errors are worth failing on: silently sending nothing would let
# every check below "pass" by never contradicting anything.
send() { wtype "$@" || { echo "ABORT wtype could not send: $*"; exit 1; }; }

# Count, never grep -q. The log accumulates: by the time the Escape check runs,
# an earlier "saver: showing" is still sitting in the file, and a presence test
# would be satisfied by it no matter what the key under test did. So every wait
# is for the count to RISE above what it was before the keypress.
#
# `grep -c` on no matches PRINTS 0 and EXITS 1, so the obvious `|| echo 0`
# appends a second zero and every later [ "$n" -gt … ] dies with "integer
# expected" — which is false, so the checks fail as if the keys were dead.
# Assign first, and only substitute on a non-zero exit.
count_log() {
    local n
    n=$(grep -c "$1" "$LOG" 2>/dev/null) || n=0
    printf '%s' "${n:-0}"
}

wait_count() {   # wait_count <pattern> <baseline> <what>
    local n=0
    while [ $n -lt 40 ]; do
        [ "$(count_log "$1")" -gt "$2" ] && return 0
        kill -0 "$PID" 2>/dev/null || { fail "synui died during: $3"; return 1; }
        sleep 0.1
        n=$((n + 1))
    done
    return 1
}

echo "== the panel's own footer, key by key =="

# ── p previews ─────────────────────────────────────────────
base=$(count_log "saver: showing")
send -M logo -k z -m logo          # Super+Z: open the panel
sleep 0.5
send -k p                          # unmodified: the panel's key, or nobody's
if wait_count "saver: showing" "$base" "p preview"; then
    pass "p previews the current mode"
else
    fail "p did nothing — saver_key() is not on input.c's key path"
fi

# ── Down/Right/s, as three separable claims ────────────────
send -M logo -k z -m logo          # the preview closed the panel; reopen
sleep 0.5
send -k Down                       # row 0 (Mode) -> row 1 (Timeout)
sleep 0.2
send -k Right                      # step the timeout up off 0
sleep 0.2
send -k s                          # write saver.state
sleep 0.5

STATE="$TMP/synui/saver.state"
if [ ! -f "$STATE" ]; then
    fail "s wrote no saver.state (looked in $STATE)"
else
    pass "s saves to saver.state"
    timeout=$(sed -n 's/^timeout=//p' "$STATE")
    mode=$(sed -n 's/^mode=//p' "$STATE")
    if [ "${timeout:-0}" -gt 0 ]; then
        pass "Down+Right adjusted the Timeout row (timeout=$timeout)"
    else
        fail "timeout is still ${timeout:-unset} — Right did not adjust"
    fi
    # The discriminating half: had Down been ignored, Right would have stepped
    # the MODE off clock and the timeout would have stayed 0. Both halves have
    # to hold at once.
    if [ "$mode" = "clock" ]; then
        pass "…and Down moved off the Mode row (mode still clock)"
    else
        fail "mode is '$mode', not clock — Right hit row 0, so Down was ignored"
    fi
fi

# ── Esc closes ─────────────────────────────────────────────
# Nothing is logged for the panel closing, so this is judged the way the user
# judges it: with the panel shut, Super+Z must OPEN it again rather than toggle
# it closed. Escape is checked by what the next keypress means.
send -k Escape
sleep 0.3
base=$(count_log "saver: showing")
send -M logo -k z -m logo
sleep 0.4
send -k p                          # previews only if the panel is open
if wait_count "saver: showing" "$base" "Esc close"; then
    pass "Esc closes (Super+Z afterwards re-opens rather than toggling shut)"
else
    fail "after Esc, Super+Z did not leave the panel open — Esc never landed"
fi

kill -TERM "$PID" 2>/dev/null
wait "$PID" 2>/dev/null

if [ $fails -eq 0 ]; then
    echo "all checks passed"
    exit 0
fi
echo "$fails check(s) failed"
exit 1
