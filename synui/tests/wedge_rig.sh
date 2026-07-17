#!/bin/sh
# wedge_rig.sh — reproduce the Steam close-to-tray wedge under a nested,
# headless synui with wlroots debug logging, and test one specific theory.
#
# THEORY: wlroots' xwm drops a WL_SURFACE_SERIAL that arrives while
# xsurface->serial is still non-zero (xwm.c:1450). serial is cleared only in
# xwayland_surface_dissociate(), which runs off the wl_surface destroy — a
# different fd from the X11 client message. X fd read first => pairing message
# dropped => the surface is never associated => the window can never map.
#
# FALSIFIABLE PREDICTION: at the moment of the wedge, wlroots logs
#   "Received multiple client messages WL_SURFACE_SERIAL for the same X11 window"
# Present at the wedge  => theory CONFIRMED.
# Wedge but no log line => theory DEAD, the drop is somewhere else.
# No wedge at all       => this rig does not reproduce it; do not conclude
#                          anything about the theory either way.
#
# Everything runs headless on its own XDG_RUNTIME_DIR, so it cannot touch the
# live session. Usage: wedge_rig.sh /path/to/synui [cycles]

set -u

SYNUI=${1:?usage: wedge_rig.sh /path/to/synui [cycles]}
CYCLES=${2:-40}
TESTDIR=$(dirname "$0")

command -v Xwayland >/dev/null 2>&1 || { echo "Xwayland not installed"; exit 1; }
command -v synctl   >/dev/null 2>&1 || { echo "synctl not installed";   exit 1; }

TMP=$(mktemp -d)
LOG="$TMP/synui.log"
BIN="$TMP/x11_wedge_test"

cleanup() {
    [ -n "${TEST_PID:-}" ]  && kill -9 "$TEST_PID"  2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
}
trap cleanup INT TERM EXIT

echo "building x11_wedge_test..."
cc -O1 -g -o "$BIN" "$TESTDIR/x11_wedge_test.c" -lX11 || { echo "build failed"; exit 1; }

# Headless + pixman: no GPU, no seat, no privileges. Own runtime dir so the ipc
# socket (XDG_RUNTIME_DIR/synui-$WAYLAND_DISPLAY.sock) cannot collide with the
# session's, and HOME so it cannot read or write real config/state.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export WLR_BACKENDS=headless WLR_RENDERER=pixman
unset DISPLAY WAYLAND_DISPLAY

# NB there is NO WLR_LOG/WLR_DEBUG env var — wlroots' level is set by the
# compositor via wlr_log_init(), and synui gates it on -d (synui_main.c:1620).
# Without -d the log is INFO-only, the xwm debug lines never appear, and a
# "0 hits" verdict would be a lie. Asserted below.
echo "booting nested synui (headless, -d for WLR_DEBUG)..."
"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || { echo "synui died on startup:"; tail -30 "$LOG"; exit 1; }
    sleep 0.1
    i=$((i + 1))
done
[ -n "$SOCK" ] || { echo "no Wayland socket within 10s"; tail -30 "$LOG"; exit 1; }
echo "  WAYLAND_DISPLAY=$SOCK"

# Xwayland starts lazily; anchor on DISPLAY= (the X11 in between is digits).
XDISP=
i=0
while [ $i -lt 150 ]; do
    XDISP=$(sed -n 's/.*Xwayland.*DISPLAY=\(:[0-9]\{1,\}\).*/\1/p' "$LOG" | head -1)
    [ -n "$XDISP" ] && break
    sleep 0.2
    i=$((i + 1))
done
[ -n "$XDISP" ] || { echo "Xwayland did not start"; tail -30 "$LOG"; exit 1; }
echo "  DISPLAY=$XDISP"

# Assert debug logging is really on. The entire verdict rests on being able to
# see a wlroots WLR_DEBUG line; if the log is INFO-only, "0 hits" means
# "we were blind", not "it did not happen". Fail loudly rather than lie.
grep -q "\[DEBUG\]" "$LOG" || {
    echo "BROKEN RIG: no [DEBUG] lines in the log — wlroots debug logging is off,"
    echo "  so the WL_SURFACE_SERIAL line could never appear and the verdict"
    echo "  would be meaningless. (Is -d still wired to wlr_log_init?)"
    exit 1
}
echo "  debug logging confirmed ($(grep -c '\[DEBUG\]' "$LOG") lines so far)"

# Pin SYNUI_SOCKET explicitly. synctl prefers it over WAYLAND_DISPLAY, and the
# test refuses to run without it — belt and braces against ever driving the
# live desktop from a loop that unmaps windows.
export WAYLAND_DISPLAY="$SOCK" DISPLAY="$XDISP"
export SYNUI_SOCKET="$XDG_RUNTIME_DIR/synui-$SOCK.sock"
echo "  SYNUI_SOCKET=$SYNUI_SOCKET"
echo

OUT="$TMP/out"
"$BIN" "$CYCLES" >"$OUT" 2>&1 &
TEST_PID=$!

# The test pause()s when it wedges, holding the state live so we can inspect
# X and the compositor before tearing anything down. So poll for the marker
# rather than wait()ing — a wedged test never exits on its own.
WEDGED=0
RC=0
i=0
while [ $i -lt 1200 ]; do
    if grep -q "WEDGED at cycle" "$OUT" 2>/dev/null; then WEDGED=1; break; fi
    if ! kill -0 "$TEST_PID" 2>/dev/null; then
        wait "$TEST_PID" 2>/dev/null
        RC=$?
        break
    fi
    sleep 0.5
    i=$((i + 1))
done

tail -5 "$OUT"

if [ "$WEDGED" = "1" ]; then
    echo
    echo "--- live wedge state (test is paused, holding it) ---"
    echo "synctl clients:"; synctl clients
    echo "_NET_CLIENT_LIST:"; xprop -root _NET_CLIENT_LIST 2>/dev/null
    WID=$(xwininfo -root -tree 2>/dev/null | grep x11-wedge-test | grep -oE '0x[0-9a-f]+' | head -1)
    echo "window $WID map state:"; xwininfo -id "$WID" 2>/dev/null | grep -E "Map State|Width|Height"
    kill -9 "$TEST_PID" 2>/dev/null
fi

echo
echo "=================== VERDICT ==================="
SERIAL_HITS=$(grep -c "multiple client messages WL_SURFACE_SERIAL" "$LOG" 2>/dev/null)
[ -n "$SERIAL_HITS" ] || SERIAL_HITS=0

# Proof the cycles genuinely destroyed and recreated the wl_surface: wlroots
# logs one "New xwayland surface" per creation. Roughly one per cycle means the
# race window was actually opened that many times. Far fewer means the rig is
# not exercising the path and any verdict below is worthless.
SURFACES=$(grep -c "New xwayland surface" "$LOG" 2>/dev/null)
[ -n "$SURFACES" ] || SURFACES=0
echo "wl_surface creations: $SURFACES (expected ~$CYCLES, one per cycle)"
echo "wlroots 'multiple client messages WL_SURFACE_SERIAL' lines: $SERIAL_HITS"

if [ "$RC" = "3" ]; then
    echo "BROKEN RIG: baseline map never produced a view. Verdict is meaningless."
elif [ "$RC" = "2" ]; then
    echo "BROKEN RIG: SYNUI_SOCKET guard tripped."
elif [ "$SURFACES" -lt "$CYCLES" ]; then
    echo "SUSPECT RIG: only $SURFACES surface creations for $CYCLES cycles — the"
    echo "  cycles are not tearing down and recreating the wl_surface, so the"
    echo "  race window is never opened. 'No wedge' proves nothing."
elif [ "$WEDGED" = "1" ]; then
    if [ "$SERIAL_HITS" -gt 0 ]; then
        echo "REPRODUCED + log line present => THEORY CONFIRMED."
    else
        echo "REPRODUCED but log line ABSENT => THEORY DEAD. The drop is elsewhere."
    fi
else
    echo "No wedge in $CYCLES cycles => rig did not reproduce. Concludes NOTHING"
    echo "about the theory. Try more cycles, or the timing here is too clean."
fi
echo "==============================================="
echo
echo "xwm/surface log lines:"
grep -iE "WL_SURFACE_SERIAL|WL_SURFACE_ID|xwayland surface|unpaired" "$LOG" | tail -25
echo
echo "full log: $LOG  (kept)"
trap - EXIT
[ -n "${SYNUI_PID:-}" ] && kill -TERM "$SYNUI_PID" 2>/dev/null
cp "$LOG" /tmp/wedge-rig-last.log 2>/dev/null && echo "copied to /tmp/wedge-rig-last.log"
