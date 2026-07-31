#!/bin/sh
# resize_heal.sh — does synui re-send a configure the client never took?
#
# The bug (velle, 2026-07-31, screenshot synapse-20260731-011022.png): a
# maximized Firefox frame at 2544x1396 with the page rendered into 552x304 in
# its top-left corner and the desktop showing through the rest of it. Moving the
# window fixed it; nothing else did.
#
# Why it could stick: view_resize() (layout.c) is the ONLY place synui sizes a
# window, and it moves the frame and configures the client together — so the two
# cannot disagree at the moment it runs. It just never looked again, and nothing
# else re-configures a window that is not being moved or re-laid-out. synui also
# answers a toplevel's initial commit with 0x0 ("pick your own size, the layout
# resizes you on map"), so a client that misses the configure that follows keeps
# the size it chose, forever, under full-size chrome.
#
# The test: tests/stubborn_client.c takes its own 552x304 from the 0x0 configure
# and then ignores (but acks) exactly one sized configure — precisely that
# failure. It prints `final WxH`. Pass means synui noticed and said it again:
#
#   1. the stubborn client ends up at the size synui actually laid it out at,
#      not at the 552x304 it chose for itself;
#   2. the control — the same client dropping NOTHING — lands on the same size,
#      which is what proves (1) is the heal and not some other placement;
#   3. a well-behaved client costs no extra configures at all: the heal is
#      silent for the control run (no "re-configuring" in the log), so this
#      cannot become a configure storm against every client on the desktop;
#   4. a client that sends set_maximized BEFORE its first commit (Firefox does,
#      restoring a maximized session) comes up genuinely maximized — floating,
#      with a saved box — and not the maximized-but-still-tiled window synui
#      used to build by writing the request straight into view->maximized.
#
# Usage: resize_heal.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
# Same suppressions smoke.sh uses, for the same reason: an ASan build otherwise
# reports cairo/fontconfig's own caches as synui leaks.
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
# And the same ASan flags, which this script was missing entirely — see the
# block in smoke.sh for what each buys. fast_unwind_on_malloc=0 is load-bearing
# for the suppressions: without it the NVIDIA driver's EGL-init leak stacks
# truncate inside libdbus-1 and cannot be attributed to the driver that owns
# them. Both scripts run standalone as well as under `meson test`, so they set
# this themselves rather than relying on meson.build's add_test_setup().
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: resize_heal.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: resize_heal.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: resize_heal.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${CLIENT_PID:-}" ] && kill -9 "$CLIENT_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

# Short runtime dir (108-byte socket paths) and a hermetic HOME, or the
# developer's own synuirc, windows.conf and autostart leak into the run.
TMP=$(mktemp -d /tmp/synui-heal.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

# -d, because the heal logs at WLR_DEBUG: it is a normal thing to do about a
# misbehaving client, not a warning, but the test has to be able to see it.
"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

# Wait for the socket rather than sleeping a guess at it.
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

# ── 1. the control: a client that takes every configure ──────────────────
CTRL=$("$CLIENT" 0 2 2>"$TMP/ctrl.err" | tail -1) || fail "control client failed"
CTRL_SIZE=${CTRL#final }
[ -n "$CTRL_SIZE" ] || fail "control printed no size (got '$CTRL')"
echo "control (drops 0): $CTRL_SIZE"

case "$CTRL_SIZE" in
    552x304) fail "control never got laid out — it is still at its own size,
       so this run proves nothing about the heal. Check that the client
       maps at all: $TMP/ctrl.err" ;;
esac

# The heal must be SILENT for a client that behaves. If it fires here it would
# fire for every client on the desktop, every resize.
if grep -q "re-configuring" "$LOG"; then
    fail "heal fired for a well-behaved client — that is a configure storm:
$(grep 're-configuring' "$LOG" | head -5)"
fi

# ── 1b. and silent for one that rounds, which is every terminal ──────────
# foot lands 2px under on height (14 at the worst) because it snaps to a
# character cell, and xdg-shell gives the compositor no way to know that. Before
# the proportional guard this cost two redundant configures per window per
# resize — 22 of them across three foot windows and eight layout dispatches.
SNAP=$("$CLIENT" 0 2 0 16 2>"$TMP/snap.err" | tail -1) || fail "snapping client failed"
echo "snapping (16px cells): ${SNAP#final }"
if grep -q "re-configuring" "$LOG"; then
    fail "heal chased a client that was merely rounding to a 16px cell — this is
       every terminal on the desktop, on every resize:
$(grep 're-configuring' "$LOG" | head -5)"
fi

# ── 2. the stubborn client: drops the configure that sizes it ────────────
OUT=$("$CLIENT" 1 3 2>"$TMP/stub.err" | tail -1) || fail "stubborn client failed"
SIZE=${OUT#final }
echo "stubborn (drops 1): $SIZE"

[ "$SIZE" = "552x304" ] && fail "the window kept the size it chose for itself.
       synui never re-sent the configure the client dropped — this is the
       reported bug, unfixed."

[ "$SIZE" = "$CTRL_SIZE" ] || fail "healed to $SIZE, but the control landed on
       $CTRL_SIZE — the window did not end up where the layout put it."

grep -q "re-configuring" "$LOG" || fail "the size is right but nothing logged a
       re-configure, so it was fixed by some other path and this test is not
       watching the heal."

echo "healed: $SIZE (= control), and silent for the well-behaved client"

# ── 3. set_maximized before the first commit ─────────────────────────────
# Held in the background so the window is still mapped while synctl reads it —
# this state is only visible on a live window.
"$CLIENT" 0 4 1 >"$TMP/premax.out" 2>"$TMP/premax.err" &
CLIENT_PID=$!

WIN=
i=0
while [ $i -lt 30 ]; do
    WIN=$(SYNUI_SOCKET="$TMP/synui-$SOCK.sock" "$SYNCTL" clients 2>/dev/null \
          | tr '{' '\n' | grep '"app_id":"stubborn"')
    [ -n "$WIN" ] && break
    i=$((i + 1)); sleep 0.1
done
[ -n "$WIN" ] || fail "the pre-maximized client never showed up in synctl clients"
echo "premax window: $WIN"

case "$WIN" in
    *'"maximized":true'*) ;;
    *) fail "set_maximized before the first commit was dropped — the window is
       not maximized at all: $WIN" ;;
esac
case "$WIN" in
    *'"floating":true'*) ;;
    *) fail "maximized but NOT floating: the request was written straight into
       view->maximized and view_apply_maximized never ran, so the window is in
       the tiling flow with no saved box to un-maximize back to.
       $WIN" ;;
esac
echo "premax: maximized AND floating — view_apply_maximized ran"

wait "$CLIENT_PID"; CLIENT_PID=

# ── 4. clean shutdown, so a heal in the commit path cannot wedge teardown ─
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

echo "PASS: resize_heal"
cleanup
exit 0
