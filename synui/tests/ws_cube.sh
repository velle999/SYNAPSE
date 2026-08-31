#!/bin/sh
# ws_cube.sh — `anim_workspace = cube`: the desk really turns, and it lands flat
#
# The cube is the one switch style that is not a per-window animation. anim.c
# deliberately does nothing for it (the windows are inside the picture being
# turned), so every assertion ws_slide.sh makes — draw offsets, per-window alpha
# — is blind to it by construction. What can be seen instead is:
#
#   1. it DRAWS. cube.c logs one DEBUG line per turn saying how many frames of
#      it were actually drawn. Zero frames is the difference between "the cube
#      is unavailable on this machine" (a legitimate outcome — it falls back to
#      an un-animated switch) and "it ran", and nothing else distinguishes them:
#      both end on the same picture.
#   2. it LANDS FLAT. The turn's last frame is the incoming desktop square to
#      the viewer, and the frame after it comes from the ordinary commit path.
#      If those two disagree the desk would visibly jump at the end of every
#      switch, so the resting screenshot is compared against the one the same
#      desktop produces with the cube off — pixel for pixel.
#   3. the outgoing desktop is GONE. Its windows stay solid (they must — they
#      are the photograph) and are taken away by having their nodes disabled
#      instead. That path is anim_start's zero-length branch, which used to
#      disable a node only when the animation ended transparent, and therefore
#      left the desk you had just left drawn on top of the one you switched to.
#
# Usage: ws_cube.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node — the cube is a GLES2 pass and there is
# nothing to test without one.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: ws_cube.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: ws_cube.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: ws_cube.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    for p in ${CLIENT_PIDS:-}; do kill -9 "$p" 2>/dev/null; done
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — the cube is a GLES2/DMA-BUF pass."
    exit 77
fi
if ! command -v grim >/dev/null 2>&1; then
    echo "SKIP: grim is not installed — phase 2 compares rendered pixels."
    exit 77
fi
python3 -c 'import PIL, numpy' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL/numpy not installed (phase 2 needs them)."; exit 77; }

TMP=$(mktemp -d /tmp/synui-wscube.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"

# ⛔ SYNUI_SOCKET IS SET IN SOME SHELLS AND POINTS AT THE LIVE DESKTOP, which
# synctl prefers over WAYLAND_DISPLAY — a rig that leaves it set drives the
# machine it is running on instead of the one it started.
unset SYNUI_SOCKET

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

synctl()  { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
clients() { synctl clients | tr '{' '\n' | grep '"app_id"'; }

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
    export WAYLAND_DISPLAY="$SOCK"
    CTLSOCK="$TMP/synui-$SOCK.sock"
}

stop_synui() {
    kill -TERM "$SYNUI_PID" 2>/dev/null
    i=0
    while kill -0 "$SYNUI_PID" 2>/dev/null; do
        i=$((i + 1))
        [ $i -gt 50 ] && fail "synui did not exit within 5s of SIGTERM"
        sleep 0.1
    done
    wait "$SYNUI_PID"; RC=$?
    [ $RC -eq 0 ] || fail "synui exited $RC"
    SYNUI_PID=
}

open_window() {
    "$CLIENT" 0 90 >>"$TMP/client.out" 2>>"$TMP/client.err" &
    CLIENT_PIDS="$CLIENT_PIDS $!"
    i=0
    while [ $i -lt 60 ]; do
        [ "$(clients | grep -c '"app_id":"stubborn"')" -ge 1 ] && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "the client never mapped: $(cat "$TMP/client.err")"
}

kill_clients() { for p in $CLIENT_PIDS; do kill -9 "$p" 2>/dev/null; done; CLIENT_PIDS=; }

# ── 1. the cube draws, and its windows are taken away ───────────────────
printf 'anim_workspace = cube\nanim_workspace_ms = 600\n' > "$SYNUI_CONFIG"
start_synui
open_window
sleep 1.0
grim "$TMP/desk1.png" 2>/dev/null || fail "grim could not capture the headless output"

synctl dispatch ws 2 >/dev/null
sleep 2.0

FRAMES=$(sed -n 's/.*cube: .* ended after \([0-9]*\) frame(s) drawn.*/\1/p' "$LOG" | tail -1)
[ -n "$FRAMES" ] || fail "cube.c logged no turn at all. cube_begin() is called
       from workspace_switch's ws_switch_core() as its FIRST act — before the
       outgoing windows are hidden, since a hidden desktop photographs as bare
       wallpaper. Check that anim_workspace parsed as 'cube' (config.c's
       syn_anim_ws_names[]) and that the length is non-zero.
$(grep -i cube "$LOG" | tail -5)"

if [ "$FRAMES" = 0 ]; then
    # A legitimate outcome, and one this test must not turn into a red build:
    # cube.c refuses rather than risking the frame if the driver will not give
    # it a shader or an offscreen buffer, and the switch simply is not animated.
    echo "SKIP: the cube compiled no usable GL pass on this machine (0 frames)."
    grep -i "cube:" "$LOG" | tail -3
    kill_clients; stop_synui; cleanup
    exit 77
fi
echo "draw:     the turn drew $FRAMES frame(s)"
[ "$FRAMES" -ge 5 ] || fail "the turn drew only $FRAMES frame(s) over 600ms. It
       is supposed to re-damage and re-schedule the output every frame; one or
       two frames means it drew once and nothing asked for another, which on
       screen is a desk that freezes at an angle and then snaps."

# The outgoing desktop's window must be OFF, at full opacity. Full opacity is
# the part that is easy to get wrong: it is what says the window was taken away
# by the cube rather than faded out, and the zero-length hide path used to key
# its disable on the alpha instead.
VIS=$(clients | grep -c '"enabled":true')
[ "$VIS" = 0 ] || fail "the window on desktop 1 is still drawn after switching
       to the empty desktop 2. anim_workspace_hide() runs the CUBE style at
       length 0 and full alpha, so anim_start's zero-length branch is what has
       to disable the node — and it must do so regardless of the alpha it
       ended on.
$(clients)"
clients | grep -q '"alpha":1.000' || fail "the outgoing window is not at full
       opacity. The cube must not fade anything: its windows are inside the
       photograph being turned, and fading them fades the face of the cube.
$(clients)"
echo "hide:     the outgoing desktop is off, and was never faded"

kill_clients
stop_synui

# ── 2. it lands exactly where it started ────────────────────────────────
# The turn's last frame is the incoming desktop square to the viewer — the
# projection is chosen so that theta = 0 is the identity, corner for corner —
# and the frame after that comes from the ordinary commit path. If the two
# disagreed, every switch would end with a visible jump: a desk left slightly
# small, offset, or with black bars down the sides.
#
# Asserted by going AWAY and BACK inside ONE session and comparing the screen
# with the one captured before any of it happened. Same session, same window, so
# the two frames are supposed to be identical and any difference is the cube's
# residue rather than a clock ticking between two runs.
printf 'anim_workspace = cube\nanim_workspace_ms = 400\n' > "$SYNUI_CONFIG"
start_synui
open_window
sleep 1.5
grim -t ppm "$TMP/before.ppm" 2>/dev/null || fail "grim could not capture the headless output"
synctl dispatch ws 2 >/dev/null; sleep 1.5
synctl dispatch ws 1 >/dev/null; sleep 1.5
grim -t ppm "$TMP/after.ppm" 2>/dev/null || fail "grim failed after the round trip"
kill_clients
stop_synui

python3 - "$TMP" <<'PYEOF' || fail "the desk did not come back to rest where it left"
import sys
import numpy as np
from PIL import Image

tmp = sys.argv[1]
a = np.array(Image.open(f"{tmp}/before.ppm").convert("RGB")).astype(int)
b = np.array(Image.open(f"{tmp}/after.ppm").convert("RGB")).astype(int)

if a.shape != b.shape:
    print(f"the two captures are different sizes: {a.shape} vs {b.shape}")
    sys.exit(1)

diff = np.abs(a - b).max(axis=2)
# A handful of pixels may legitimately move — the bar carries a clock, and the
# two captures are 3s apart. A cube that landed wrong moves a LARGE FRACTION of
# the screen: even a one-degree residual rotation displaces every edge in the
# picture, and a scale residue moves the whole frame.
moved = float((diff > 24).mean())
print(f"round trip: {moved * 100:.2f}% of pixels differ from before the turn")
if moved > 0.02:
    print("the desktop did not come back to where it was. The cube's projection"
          " is the identity at theta = 0 (f = CUBE_DIST - 1, zoom = 0 at both"
          " ends), so the frame it lands on must be pixel-identical to the one"
          " the ordinary commit draws. Check CUBE_ZOOM's sin(2*theta) term and"
          " that cube_free() runs at t >= 1 rather than drawing a final frame.")
    Image.fromarray(((diff > 24) * 255).astype("uint8")).save(f"{tmp}/diff.png")
    sys.exit(1)
sys.exit(0)
PYEOF
echo "land:     the desk came back pixel-identical after a full turn each way"

echo "PASS"
exit 0
