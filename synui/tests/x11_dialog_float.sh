#!/bin/sh
# x11_dialog_float.sh — is an X11 dialog placed on a floating desktop?
#
# The bug (velle, 2026-09-17): a Windows installer run under Wine put an entry
# in the dock and no window on any screen. `synctl clients` on the live session:
#
#   {"app_id":"<installer>.tmp","title":"Select Setup Language",
#    "at":[0,0],"size":[0,0],"floating":true,...}
#
# — the installer's first dialog, mapped, focused, on the taskbar, and never
# given a geometry. On velle's layout 0,0 is dead space no output covers, so it
# was drawn nowhere.
#
# Why: layout_float_place() on a FLOATING desktop hands a window with no
# remembered box to layout_float_arrange() and returns. The arranger skips
# dialogs on purpose (a transient or modal X11 window, or an xdg toplevel with a
# parent), so a dialog was handed to the one placer that will not place it, and
# the centring code below the hand-off never ran. xw_map() sets view->floating
# for exactly those windows, so every Wine dialog took that path. An Inno Setup
# installer is all dialogs — its owner is a TApplication window that never maps.
#
# float_first_open.sh covers an ordinary window on the same desktop and passed
# throughout; it never opened a dialog.
#
#   1. tiling   — control: the dialog is centred, as it always was.
#   2. floating — the bug: unfixed, the dialog is at 0,0 sized 0x0.
#
# Usage: x11_dialog_float.sh /path/to/synui /path/to/x11_dialog_test /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

USAGE="usage: x11_dialog_float.sh /path/to/synui /path/to/x11_dialog_test /path/to/synctl"
SYNUI=${1:?$USAGE}
CLIENT=${2:?$USAGE}
SYNCTL=${3:?$USAGE}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${CLIENT_PID:-}" ] && kill -9 "$CLIENT_PID" 2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-x11dlg.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_GREETER_BG_DIR="$TMP/greeter"
printf 'welcome_at_startup = off\n' > "$SYNUI_CONFIG"
# EMPTY on purpose: a remembered box is placed before the arranger is asked
# anything, and would hide the bug.
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY SYNUI_SOCKET

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

# Xwayland is lazy; synui logs the DISPLAY it hands to the children it spawns.
i=0
while [ $i -lt 100 ]; do
    D=$(command grep -ao 'DISPLAY=:[0-9]*' "$LOG" | head -1 | cut -d= -f2)
    [ -n "${D:-}" ] && break
    i=$((i + 1)); sleep 0.1
done
[ -n "${D:-}" ] || fail "Xwayland never announced a DISPLAY"
export DISPLAY="$D"

open_dialog() {
    "$CLIENT" >"$TMP/client.out" 2>"$TMP/client.err" &
    CLIENT_PID=$!

    WIN=
    i=0
    while [ $i -lt 60 ]; do
        WIN=$(synctl clients | tr '{' '\n' | grep '"app_id":"x11dialog"')
        [ -n "$WIN" ] && break
        kill -0 "$CLIENT_PID" 2>/dev/null || fail "client exited before it mapped:
       $(cat "$TMP/client.err")"
        i=$((i + 1)); sleep 0.1
    done
    [ -n "$WIN" ] || fail "the dialog never showed up in synctl clients"
    # The open animation and the client's first configure both land after the
    # view appears; read the box once they have.
    sleep 0.5
    WIN=$(synctl clients | tr '{' '\n' | grep '"app_id":"x11dialog"')
    echo "$WIN" | grep -q '"floating":true' || fail "the dialog is not floating.
       xw_map floats a transient X11 window, and the bug lives on that path, so
       this rig is no longer testing it:
       $WIN"
}

close_dialog() {
    kill -TERM "$CLIENT_PID" 2>/dev/null
    wait "$CLIENT_PID" 2>/dev/null
    CLIENT_PID=
    i=0
    while [ $i -lt 40 ]; do
        synctl clients | grep -q '"app_id":"x11dialog"' || break
        i=$((i + 1)); sleep 0.1
    done
}

win_x() { echo "$WIN" | sed -n 's/.*"at":\[\(-\{0,1\}[0-9]*\),.*/\1/p'; }
win_y() { echo "$WIN" | sed -n 's/.*"at":\[-\{0,1\}[0-9]*,\(-\{0,1\}[0-9]*\)\].*/\1/p'; }
win_w() { echo "$WIN" | sed -n 's/.*"size":\[\([0-9]*\),.*/\1/p'; }
win_h() { echo "$WIN" | sed -n 's/.*"size":\[[0-9]*,\([0-9]*\)\].*/\1/p'; }
win_box() { echo "$(win_x),$(win_y) $(win_w)x$(win_h)"; }

layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }

OUT=$(synctl outputs | tr '{' '\n' | grep '"name"' | head -1)
OW=$(echo "$OUT" | sed -n 's/.*"size":\[\([0-9]*\),.*/\1/p')
OH=$(echo "$OUT" | sed -n 's/.*"size":\[[0-9]*,\([0-9]*\)\].*/\1/p')
[ -n "$OW" ] && [ -n "$OH" ] && [ "$OW" -gt 0 ] && [ "$OH" -gt 0 ] \
    || fail "could not read the headless output box from synctl outputs: $OUT"
echo "output:   ${OW}x${OH}"

assert_on_screen() {
    _w=$(win_w); _h=$(win_h); _x=$(win_x); _y=$(win_y)
    [ -n "$_w" ] && [ -n "$_h" ] && [ -n "$_x" ] && [ -n "$_y" ] \
        || fail "could not read at/size out of the dialog's line:
       $WIN"
    { [ "$_w" -gt 0 ] && [ "$_h" -gt 0 ]; } || fail "$1: the dialog is $_w x $_h.
       Nothing placed it: layout_float_place handed it to the arranger, which
       skips dialogs. On a layout whose origin is dead space it is drawn where
       no output can show it — a taskbar entry and no window.
       $WIN"
    { [ "$_x" -ge 0 ] && [ "$_y" -ge 0 ] \
      && [ $((_x + _w)) -le "$OW" ] && [ $((_y + _h)) -le "$OH" ]; } \
        || fail "$1: the dialog's box $(win_box) is not inside the ${OW}x${OH}
       output.
       $WIN"
}

# ── 1. tiling: the control ───────────────────────────────────────────────
[ "$(layout_now)" = "tiling" ] || fail "expected a tiling desktop at startup, got $(layout_now)"

open_dialog
echo "tiling:   $(win_box)"
assert_on_screen "on a TILING desktop"
close_dialog

# ── 2. floating, nothing remembered: the bug ─────────────────────────────
# Closing the dialog in (1) recorded nothing: geom_persist never saves a
# dialog, so this is still an app with no box to fall back on.
grep -q '^window x11dialog ' "$SYNUI_WINDOWS" \
    && fail "windows.conf holds a box for the dialog, so phase 2 would test the
       remembered-box path instead of the arranger hand-off:
       $(cat "$SYNUI_WINDOWS")"
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = "floating" ] || fail "layout_cycle did not reach floating, got $(layout_now)"

open_dialog
echo "floating: $(win_box)"
assert_on_screen "on a FLOATING desktop, with nothing in windows.conf"
close_dialog

# ── 3. clean shutdown ────────────────────────────────────────────────────
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

echo "PASS: x11_dialog_float"
cleanup
exit 0
