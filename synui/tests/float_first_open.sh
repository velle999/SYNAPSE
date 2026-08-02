#!/bin/sh
# float_first_open.sh — is an app's FIRST window placed on a floating desktop?
#
# The bug (velle, 2026-08-02: "the updater program is being weird about
# launch"). `synctl clients` on the live session:
#
#   {"app_id":"org.quickshell","title":"SynapseOS Updates",
#    "at":[0,0],"size":[0,0],"floating":false,...}
#
# — a mapped, enabled window that synui had never given a geometry to. Every
# other window on that desktop was fine, and every one of them had a line in
# windows.conf. org.quickshell did not.
#
# Why: nothing placed it.
#
#   * layout_apply() is a no-op for LAYOUT_FLOATING ("the user positions
#     windows"), so the map path's call did nothing;
#   * layout_restore_geometry() returns false when the app has nothing saved;
#   * layout_float_place() — the centred default — was only reached when
#     view->floating was already set, and that flag is set by Super+F, a snap,
#     or maximize. Never by a window merely mapping. The xdg path did not call
#     it at all, and the XWayland path only for a modal or a child window.
#
# So the view kept the 0,0,0,0 it was calloc'd with. That is not merely a
# badly-sized window: on velle's layout the origin is DEAD SPACE (DP-2 at
# 1400,0, HDMI-A-1 at 0,1080, DP-3 at 1080,1080 — nothing covers 0,0), so the
# window was drawn where no output could show it. And because syn-update-gui
# runs `qs -n`, whose --no-duplicate exits 0 when an instance is already alive,
# every later click on the menu entry then succeeded silently and did nothing.
#
# It only ever bit an app the desk had never seen, which is why it read as one
# program misbehaving rather than a compositor bug.
#
# The test opens the SAME client on a tiling desktop and then on a floating one
# with an EMPTY windows.conf — the one condition the existing geom_layout.sh
# never covers, since that rig seeds the table on purpose:
#
#   1. tiling   — control: the layout places it, as it always did.
#   2. floating — the bug: the window must have a real box inside the output.
#                 Unfixed, this is at 0,0 sized 0x0.
#   3. floating, second open — the remembered box from (2) still wins, so the
#      fix is an added fallback and not a replacement for windows.conf.
#
# Usage: float_first_open.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: float_first_open.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: float_first_open.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: float_first_open.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-float.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

# The whole point: the table is EMPTY. Pointing SYNUI_WINDOWS at a file that
# exists but holds nothing is what makes this "an app synui has never seen",
# and keeps the developer's own windows.conf out of the run.
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"

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

open_window() {
    "$CLIENT" 0 4 >"$TMP/client.out" 2>"$TMP/client.err" &
    CLIENT_PID=$!

    WIN=
    i=0
    while [ $i -lt 40 ]; do
        WIN=$(synctl clients | tr '{' '\n' | grep '"app_id":"stubborn"')
        [ -n "$WIN" ] && break
        kill -0 "$CLIENT_PID" 2>/dev/null || fail "client exited before it mapped:
       $(cat "$TMP/client.err")"
        i=$((i + 1)); sleep 0.1
    done
    [ -n "$WIN" ] || fail "the client never showed up in synctl clients"
}

close_window() {
    kill -TERM "$CLIENT_PID" 2>/dev/null
    wait "$CLIENT_PID" 2>/dev/null
    CLIENT_PID=
    i=0
    while [ $i -lt 40 ]; do
        synctl clients | grep -q '"app_id":"stubborn"' || break
        i=$((i + 1)); sleep 0.1
    done
}

win_x() { echo "$WIN" | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),.*/\1/p'; }
win_y() { echo "$WIN" | sed -n 's/.*"at":\[-\{0,1\}[0-9]*,\(-\{0,1\}[0-9]*\)\].*/\1/p'; }
win_w() { echo "$WIN" | sed -n 's/.*"size":\[\([0-9]*\),.*/\1/p'; }
win_h() { echo "$WIN" | sed -n 's/.*"size":\[[0-9]*,\([0-9]*\)\].*/\1/p'; }
win_box() { echo "$(win_x),$(win_y) $(win_w)x$(win_h)"; }

layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }

# The single headless output. Read it rather than assuming 1920x1080, so a
# wlroots default change cannot turn this into a silently-passing test.
OUT=$(synctl outputs | tr '{' '\n' | grep '"name"' | head -1)
OW=$(echo "$OUT" | sed -n 's/.*"size":\[\([0-9]*\),.*/\1/p')
OH=$(echo "$OUT" | sed -n 's/.*"size":\[[0-9]*,\([0-9]*\)\].*/\1/p')
[ -n "$OW" ] && [ -n "$OH" ] && [ "$OW" -gt 0 ] && [ "$OH" -gt 0 ] \
    || fail "could not read the headless output box from synctl outputs: $OUT"
echo "output:   ${OW}x${OH}"

# Every phase asserts the same thing: a real box, wholly on the screen.
assert_on_screen() {
    _w=$(win_w); _h=$(win_h); _x=$(win_x); _y=$(win_y)
    [ -n "$_w" ] && [ -n "$_h" ] && [ -n "$_x" ] && [ -n "$_y" ] \
        || fail "could not read at/size out of the client's line:
       $WIN"
    { [ "$_w" -gt 0 ] && [ "$_h" -gt 0 ]; } || fail "$1: the window is $_w x $_h.
       Nothing placed it, so it kept the 0x0 it was calloc'd with — no chrome,
       and on a multi-monitor layout whose origin is dead space it is drawn
       where no output can show it.
       $WIN"
    { [ "$_x" -ge 0 ] && [ "$_y" -ge 0 ] \
      && [ $((_x + _w)) -le "$OW" ] && [ $((_y + _h)) -le "$OH" ]; } \
        || fail "$1: the window's box $(win_box) is not inside the ${OW}x${OH}
       output. A window the user cannot see is the bug whether its size is zero
       or not.
       $WIN"
}

# ── 1. tiling: the control ───────────────────────────────────────────────
# The layout places its own windows, so this phase passed before the fix too.
# It is here to prove the rig itself can tell a placed window from an unplaced
# one — without it, a phase-2 pass could just mean the assertion is toothless.
[ "$(layout_now)" = "tiling" ] || fail "expected a tiling desktop at startup, got $(layout_now)"

open_window
echo "tiling:   $(win_box)"
assert_on_screen "on a TILING desktop"
close_window

# ── 2. floating, first ever open: the bug ────────────────────────────────
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = "floating" ] || fail "layout_cycle did not reach floating, got $(layout_now)"

open_window
echo "floating: $(win_box)"
assert_on_screen "on a FLOATING desktop, with nothing in windows.conf"
FIRST_BOX=$(win_box)
close_window

# ── 3. floating, second open: windows.conf still wins ────────────────────
# The fallback must not have replaced the remembered box — closing the window
# in (2) recorded one, and re-opening has to land on it. If this fails the fix
# is centring every window on every open, which is its own bug.
grep -q '^window stubborn ' "$SYNUI_WINDOWS" \
    || fail "closing the window on a floating desktop recorded nothing in
       windows.conf, so phase 3 cannot test what it means to:
       $(cat "$SYNUI_WINDOWS")"

open_window
echo "reopen:   $(win_box)"
assert_on_screen "re-opening on a FLOATING desktop"
[ "$(win_box)" = "$FIRST_BOX" ] || fail "the window reopened at $(win_box), not at
       the $FIRST_BOX it was closed at. layout_float_place is supposed to hand
       over to the remembered box before centring anything."
close_window

# ── 4. clean shutdown ────────────────────────────────────────────────────
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

echo "PASS: float_first_open"
cleanup
exit 0
