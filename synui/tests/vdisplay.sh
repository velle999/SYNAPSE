#!/bin/sh
# vdisplay.sh — a display with no monitor behind it
#
# `synctl virtual add` grows a head on the running compositor: a wlroots
# headless output that takes a workspace, holds windows and renders like any
# other screen, and that nothing scans out. It is what `syn-remote stream`
# serves to Moonlight, so a remote session gets its own resolution instead of a
# copy of whatever monitor is plugged in.
#
# What this pins, and why each one has a silent failure behind it:
#
#   1. IT IS THERE, AT THE SIZE THAT WAS ASKED FOR. The mode is committed
#      AFTER wlr_headless_add_output() returns — that call takes a size and
#      nothing else — so a virtual display that comes up at the backend's
#      default instead of the requested refresh is the bug this phase catches.
#   2. IT IS MARKED. `synctl outputs` flags it `"virtual":true`; without that
#      nothing downstream can tell the head it made from the desk's monitors,
#      and `syn-remote stream` would happily serve the wrong one.
#   3. outputs.conf NEVER LEARNS IT. The connector names are recycled every
#      session (HEADLESS-1 is whichever one was made first), so a saved entry
#      would apply last session's mode to this session's request and the
#      caller's geometry would be dropped with nothing saying so.
#   4. SOLO BLANKS EVERY OTHER SCREEN — and, the half that matters, an
#      unblankable state is not reachable: solo naming an output that is gone
#      blanks NOTHING, and removing the soloed display turns solo off. Get that
#      wrong and the room's screens go dark with no way back from the keyboard.
#   5. THE IDLE BLANK STAGE SKIPS A VIRTUAL DISPLAY. A blanked output cannot be
#      captured at all — screencopy answers "failed to copy output" — so a head
#      that blanks itself after ten idle minutes is a remote session that goes
#      grey while nobody is at the machine to move the mouse.
#
# Usage: vdisplay.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: vdisplay.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: vdisplay.sh /path/to/synui /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }
ok()   { echo "  ok    $*"; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-vdisplay.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

# Hermetic HOME, or the developer's own synuirc and outputs.conf leak in — and
# outputs.conf is one of the things under test here.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

# ⚠ TWO SESSIONS, because the idle blank stage cannot be asked to fire. There
# is no bind action for it and nothing to synthesise input into a headless
# synui, so the only way to watch it blank a screen is a compositor whose
# power_blank_timeout is one second — and a compositor like that blanks itself
# under every other phase in this file while they are busy asserting what is
# lit. Phase 5 gets its own.
start_synui() {   # start_synui <blank-timeout>
    printf 'power_blank_timeout = %s\npower_dim_timeout = 0\n' "$1" > "$SYNUI_CONFIG"
    rm -f "$TMP"/wayland-*
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
}

stop_synui() {
    [ -n "${SYNUI_PID:-}" ] && kill "$SYNUI_PID" 2>/dev/null
    wait "$SYNUI_PID" 2>/dev/null
    SYNUI_PID=
}

# 0 is off: the ladder never reaches the blank stage, so phases 1-4 can assert
# what is lit without racing a timer.
start_synui 0

# ⚠ ITS socket, never the one in the environment. A synctl that falls back to
# $SYNUI_SOCKET talks to the live desktop, and this file creates and destroys
# outputs and turns screens off.
synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

# One output per line, so a phase can grep a whole record.
outputs() { synctl outputs | tr '{' '\n' | grep '"name"'; }
out_rec() { outputs | grep "\"name\":\"$1\""; }
field()   { sed -n "s/.*\"$1\":\\([^,}]*\\).*/\\1/p"; }

[ "$(outputs | wc -l)" = 1 ] || fail "the rig should start with exactly one output"
BASE=$(outputs | sed -n 's/.*"name":"\([^"]*\)".*/\1/p')
ok "the rig's own output is $BASE"

# ── 1. add one, at the size and refresh that were asked for ──────────────
ADD=$(synctl virtual add 1280x720@90)
case "$ADD" in
    *'"ok":true'*) ;;
    *) fail "virtual add was refused: $ADD" ;;
esac
VD=$(printf '%s' "$ADD" | sed -n 's/.*"name":"\([^"]*\)".*/\1/p')
[ -n "$VD" ] || fail "virtual add answered no name: $ADD"
ok "added $VD"

REC=$(out_rec "$VD")
[ -n "$REC" ] || fail "$VD is not in synctl outputs"
SIZE=$(printf '%s' "$REC" | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1x\2/p')
[ "$SIZE" = "1280x720" ] || fail "$VD came up $SIZE, not 1280x720"
ok "it is 1280x720"

# ⛔ THE REFRESH IS THE POINT OF THE SECOND COMMIT. wlr_headless_add_output()
# takes width and height and nothing else; a virtual display that reports 60Hz
# here is one whose requested mode was never applied, and the far end of a
# stream would be paced by a frame timer nobody asked for.
HZ=$(synctl virtual | tr '{' '\n' | grep "\"name\":\"$VD\"" | field refresh)
[ "$HZ" = "90000" ] || fail "$VD is running at ${HZ}mHz, not 90000"
ok "it is running at 90Hz"

[ "$(printf '%s' "$REC" | field virtual)" = "true" ] ||
    fail "$VD is not flagged virtual in synctl outputs"
[ "$(out_rec "$BASE" | field virtual)" = "false" ] ||
    fail "$BASE is flagged virtual and is not one"
ok "synctl outputs tells the two kinds apart"

# ── 1b. and it can be resized where it stands ────────────────────────────
#
# ⛔ THE ONE CALL A STREAM MAKES EVERY TIME SOMEBODY CONNECTS. sunshine hands
# its prep command the client's width, height and fps; `syn-remote stream prep`
# turns that into this. A new display per connection cannot work — the name is
# what sunshine.conf pins, and destroying a head re-homes every window on it —
# so the head stays and the mode moves.
case "$(synctl virtual mode "$VD" 1600x900@60)" in
    *'"ok":true'*) ;;
    *) fail "virtual mode was refused: $(synctl virtual mode "$VD" 1600x900@60)" ;;
esac
sleep 0.3
SIZE=$(out_rec "$VD" | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1x\2/p')
[ "$SIZE" = "1600x900" ] || fail "$VD did not resize — it is $SIZE"
ok "it resizes to what a client asks for"

# ⛔ AND IT WILL NOT RESIZE A MONITOR. A second path to a physical mode would be
# a second owner of what outputs.conf remembers; the display panel owns that.
case "$(synctl virtual mode "$BASE" 800x600)" in
    *'"error"'*) ok "it refuses to resize a real monitor" ;;
    *) fail "virtual mode resized $BASE, which is not a virtual display" ;;
esac

synctl virtual mode "$VD" 1280x720@90 >/dev/null   # back, for the phases below
REC=$(out_rec "$VD")

# ── 2. it is a real screen: it holds a workspace and a position ──────────
WS=$(printf '%s' "$REC" | field workspace)
[ -n "$WS" ] && [ "$WS" -ge 1 ] 2>/dev/null ||
    fail "$VD has no workspace — it is not a first-class output"
ok "it holds a desktop (workspace $WS)"

# ── 3. outputs.conf never learns it ──────────────────────────────────────
#
# The save runs on any real geometry change, so make one: moving the desk's
# arrangement is what output_layout_changed() answers to.
synctl virtual add 800x600 >/dev/null
sleep 0.5
if [ -f "$TMP/synui/outputs.conf" ]; then
    if grep -q "^HEADLESS" "$TMP/synui/outputs.conf"; then
        fail "outputs.conf remembered a virtual display:
$(cat "$TMP/synui/outputs.conf")"
    fi
fi
ok "outputs.conf holds no virtual display"
synctl virtual remove 2>/dev/null >/dev/null   # both of them, back to a clean desk
[ "$(synctl virtual | grep -c '"name"')" = 0 ] || fail "virtual remove left one behind"
ok "remove with no name takes them all"

# Re-add the one the rest of the file works on.
VD=$(synctl virtual add 1280x720@90 | sed -n 's/.*"name":"\([^"]*\)".*/\1/p')
[ -n "$VD" ] || fail "could not re-add a virtual display"

# ── 4. solo, and the states it must not be able to reach ─────────────────
lit() { out_rec "$1" | field lit; }

[ "$(lit "$BASE")" = "true" ] || fail "$BASE should be lit before solo"
synctl virtual solo "$VD" >/dev/null
sleep 0.3
[ "$(lit "$BASE")" = "false" ] || fail "solo left $BASE lit"
[ "$(lit "$VD")"   = "true"  ] || fail "solo turned off the screen it is holding"
ok "solo lights one screen and turns the rest off"

synctl virtual solo off >/dev/null
sleep 0.3
[ "$(lit "$BASE")" = "true" ] || fail "solo off left $BASE dark"
ok "solo off lights everything again"

# ⛔ THE LOCKOUT. Removing the soloed display must not leave a machine with
# every screen off and nothing to turn one back on.
synctl virtual solo "$VD" >/dev/null
sleep 0.3
[ "$(lit "$BASE")" = "false" ] || fail "solo did not take for the lockout phase"
synctl virtual remove "$VD" >/dev/null
sleep 0.5
[ "$(lit "$BASE")" = "true" ] ||
    fail "removing the soloed display left every screen dark — this is the lockout"
SOLO=$(synctl virtual | field solo)
[ "$SOLO" = "null" ] || fail "solo survived the output it named: $SOLO"
ok "removing the soloed display turns solo off and lights the desk"

# A solo that was never going to work is refused rather than accepted and
# left to blank everything the moment it is believed.
case "$(synctl virtual solo NOSUCH-9)" in
    *'"error"'*) ok "solo on an output that is not there is refused" ;;
    *) fail "solo accepted an output that does not exist" ;;
esac
[ "$(lit "$BASE")" = "true" ] || fail "a refused solo turned a screen off anyway"

# ── 5. the idle blank stage leaves a virtual display alone ───────────────
#
# A second session, with power_blank_timeout at one second. The real output goes
# dark; the virtual one must not, because a blanked output cannot be captured
# and the whole point of the head is that somebody is watching it from
# elsewhere with nobody at the machine to move a mouse.
stop_synui
start_synui 1
BASE=$(outputs | sed -n 's/.*"name":"\([^"]*\)".*/\1/p' | head -1)
VD=$(synctl virtual add 1920x1080 | sed -n 's/.*"name":"\([^"]*\)".*/\1/p')
[ -n "$VD" ] || fail "could not add a virtual display for the blank phase"
i=0
while [ $i -lt 60 ]; do
    [ "$(lit "$BASE")" = "false" ] && break
    i=$((i + 1)); sleep 0.25
done
[ "$(lit "$BASE")" = "false" ] || fail "the idle blank stage never fired in 15s"
[ "$(lit "$VD")" = "true" ] ||
    fail "the idle blank stage turned off the virtual display — a stream of it would go grey"
ok "the idle blank stage blanks the monitor and leaves the virtual display lit"

echo "PASS: virtual displays"
cleanup
exit 0
