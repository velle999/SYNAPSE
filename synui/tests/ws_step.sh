#!/bin/sh
# ws_step.sh — the actions the touchpad gestures run, in a real compositor.
#
# gesture_test.c proves a four-finger swipe LEFT comes out as `ws next`. What it
# cannot prove is what `ws next` then does, and that is what this drives, over
# the control socket, in a headless synui:
#
#   - `ws next` / `ws prev` move one desktop and STOP at 1 and 9. A wrap would
#     turn one swipe on desktop 9 into a jump of eight desktops, the opposite
#     way from the fingers.
#   - `ws <n>` still means desktop n. Adding words to the argument must not
#     change what a number does, and a word that is neither must do nothing.
#   - `overview open` twice shows mission control ONCE, and `overview close`
#     twice hides it once. A toggle there would let a second swipe up shut the
#     overview the first one opened. Bare `overview` stays a toggle.
#   - `synctl gestures` lists the four shipped gestures and the switch.
#   - under per-monitor desktops, next/prev step the FOCUSED monitor alone.
#
# There is no way to put a swipe into a headless synui — the headless backend
# has no input devices, and uinput would reach the live session. So the gesture
# itself stops at gesture_test.c, and this starts from the action it runs.
#
# Usage: ws_step.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: ws_step.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: ws_step.sh /path/to/synui /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-wsstep.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

# Hermetic, as in overview.sh — and the greeter background root too, which a
# headless synui would otherwise publish into for the real uid.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
export SYNUI_GREETER_BG_DIR="$TMP/greeter"
: > "$SYNUI_CONFIG"
: > "$SYNUI_WINDOWS"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY SYNUI_SOCKET

start_synui() {
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
}

start_synui

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
active_ws() { synctl activeworkspace | sed -n 's/.*"id":\([0-9]*\).*/\1/p'; }
settle() { sleep 0.3; }

alive() {
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "$1
       synui died. The tail of its log is above."
}

# Dispatch, then expect a desktop.
step() {
    want=$1; shift
    synctl dispatch "$@" >/dev/null
    settle
    alive "dispatch $*:"
    got=$(active_ws)
    [ "$got" = "$want" ] || fail "dispatch $* should leave desktop $want, got '$got'"
}

# ── the table ────────────────────────────────────────────
g=$(synctl gestures)
for want in '"enabled":true' \
            '"gesture":"swipe:4:left","action":"ws","arg":"next"' \
            '"gesture":"swipe:4:right","action":"ws","arg":"prev"' \
            '"gesture":"swipe:4:up","action":"overview","arg":"open"' \
            '"gesture":"swipe:4:down","action":"overview","arg":"close"'; do
    case "$g" in *"$want"*) ;; *) fail "synctl gestures is missing $want:
       $g";; esac
done
echo "table:    synctl gestures lists the four defaults, switched on"

# ── ws next / prev ───────────────────────────────────────
[ "$(active_ws)" = 1 ] || fail "a fresh desk should start on desktop 1, got $(active_ws)"
step 1 ws prev
echo "floor:    ws prev on desktop 1 stays on 1"
step 2 ws next
step 3 ws next
step 2 ws prev
echo "step:     next, next, prev walks 1 -> 2 -> 3 -> 2"
step 9 ws 9
step 9 ws next
echo "ceiling:  ws next on desktop 9 stays on 9 (no wrap to 1)"
step 8 ws prev
step 4 ws 4
step 4 ws sideways
step 4 ws
echo "numbers:  ws 4 still means 4; an unknown word and no argument do nothing"

# ── overview open / close ────────────────────────────────
shown()  { grep -c 'synui: overview shown' "$LOG"; }
hidden() { grep -c 'synui: overview hidden' "$LOG"; }

s0=$(shown); h0=$(hidden)
synctl dispatch overview open >/dev/null; settle
synctl dispatch overview open >/dev/null; settle
alive "overview open twice:"
[ "$(shown)" = $((s0 + 1)) ] || fail "overview open twice should show it once, shown $(( $(shown) - s0 )) times"
[ "$(hidden)" = "$h0" ] || fail "overview open must never hide it"
echo "open:     overview open twice shows it once"

synctl dispatch overview close >/dev/null; settle
synctl dispatch overview close >/dev/null; settle
alive "overview close twice:"
[ "$(hidden)" = $((h0 + 1)) ] || fail "overview close twice should hide it once, hid $(( $(hidden) - h0 )) times"
[ "$(shown)" = $((s0 + 1)) ] || fail "overview close must never show it"
echo "close:    overview close twice hides it once"

synctl dispatch overview >/dev/null; settle
synctl dispatch overview >/dev/null; settle
alive "bare overview twice:"
[ "$(shown)" = $((s0 + 2)) ] && [ "$(hidden)" = $((h0 + 2)) ] ||
    fail "bare overview should still toggle (shown +$(( $(shown) - s0 )), hidden +$(( $(hidden) - h0 )))"
echo "toggle:   bare overview still opens and closes"

# ── a desktop swipe with mission control up ──────────────
synctl dispatch overview open >/dev/null; settle
step 5 ws next
alive "ws next under mission control:"
synctl dispatch overview close >/dev/null; settle
echo "overlay:  ws next with mission control up moves the desk and survives"

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer report in the log"
fi

# ── per-monitor desktops: next is next on the FOCUSED screen ──
# Under workspace_mode = per-monitor, ws 3 moves only the focused monitor
# (ws_per_monitor.sh). ws next has to be counted from that monitor's desktop
# and move that monitor alone — a step taken from the desk-wide index would
# carry the other screen along or land one desktop off.
kill -9 "$SYNUI_PID" 2>/dev/null
wait "$SYNUI_PID" 2>/dev/null
SYNUI_PID=
rm -f "$TMP"/wayland-* "$TMP"/synui-*.sock
printf 'workspace_mode = per-monitor\nanim_workspace = off\nanimation_ms = 0\n' > "$SYNUI_CONFIG"
export WLR_HEADLESS_OUTPUTS=2
LOG="$TMP/synui-2.log"
start_synui

out_ws() { synctl outputs | tr '{' '\n' | sed -n \
             's/.*"name":"\([^"]*\)".*"workspace":\([0-9]*\).*/\1=\2/p' \
             | sort | tr '\n' ' '; }
FOCUSED=$(synctl outputs | tr '{' '\n' | sed -n \
            's/.*"name":"\([^"]*\)".*"focused":true.*/\1/p')
[ -n "$FOCUSED" ] || fail "no output reports itself focused: $(synctl outputs)"
[ "$(out_ws | wc -w)" = 2 ] || fail "expected two headless monitors: $(out_ws)"

synctl dispatch ws 3 >/dev/null; settle
synctl dispatch ws next >/dev/null; settle
alive "ws next under per-monitor desktops:"
SPLIT=$(out_ws)
case "$SPLIT" in
    *"$FOCUSED=4"*) ;;
    *) fail "ws 3 then ws next should put the focused monitor ($FOCUSED) on 4: $SPLIT" ;;
esac
[ "$(printf '%s\n' "$SPLIT" | tr ' ' '\n' | grep -c '=1$')" = 1 ] \
    || fail "the other monitor must stay on desktop 1: $SPLIT"
synctl dispatch ws prev >/dev/null; settle
case "$(out_ws)" in
    *"$FOCUSED=3"*) ;;
    *) fail "ws prev should bring the focused monitor back to 3: $(out_ws)" ;;
esac
echo "per-mon:  ws next/prev step the focused monitor only ($SPLIT)"

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer report in the per-monitor log"
fi

cleanup
echo "ws_step: ok"
exit 0
