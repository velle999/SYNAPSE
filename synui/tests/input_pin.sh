#!/bin/sh
# input_pin.sh — a pointer pinned to one screen stays on that screen
#
# `synctl input map <output> <device>` is what syn-remote's `stream prep` uses
# to put a Moonlight client's mouse on the virtual display the stream serves.
# sunshine injects that mouse as an ordinary device ("Mouse passthrough"), and a
# RELATIVE mouse moves the one shared cursor from wherever it already is — on
# the screens in the room, where the person streaming cannot see it.
#
# What this pins:
#
#   1. THE SAME MOTION, WITH AND WITHOUT THE PIN. Twelve relative moves of
#      -300px from the top left: unpinned, the cursor ends on the base screen;
#      pinned to the virtual display, it cannot leave it. The unpinned run is
#      the control — without it a pin that did nothing would pass.
#   2. THE RULE OUTLIVES THE DEVICE. The pin is asked for before the device
#      exists (sunshine creates its devices when a client connects), so the
#      rule is kept and applied on arrival — every run here is a new device.
#   3. REMOVING THE SCREEN UNDER A PINNED POINTER. wlroots keeps a raw
#      wlr_output pointer per cursor device and never clears it, and ABSOLUTE
#      motion dereferences it (output->transform) — so one device that outlives
#      its screen and keeps moving reads freed memory. That read happens inside
#      libwlroots, which is not built with ASAN, so no sanitizer here can see it:
#      measured, a synui with the unpin removed ran this phase clean under ASAN.
#      So the phase asserts the unpin itself — `synctl input` must count no
#      pinned device once the screen is gone — with the same device still
#      sending absolute motion across the removal (absloop), and synui alive.
#
# Headless and hermetic: its own runtime dir, its own synctl socket, and a
# virtual pointer that refuses to connect anywhere but $WAYLAND_DISPLAY.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: input_pin.sh /path/to/synui /path/to/synctl /path/to/vpointer_click}
SYNCTL=${2:?usage: input_pin.sh /path/to/synui /path/to/synctl /path/to/vpointer_click}
VPTR=${3:?usage: input_pin.sh /path/to/synui /path/to/synctl /path/to/vpointer_click}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }
ok()   { echo "  ok    $*"; }

cleanup() {
    [ -n "${VPID:-}" ] && kill "$VPID" 2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-inputpin.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"
printf 'power_blank_timeout = 0\npower_dim_timeout = 0\n' > "$SYNUI_CONFIG"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

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

# ⚠ ITS socket, never the one in the environment — a synctl that fell back to
# $SYNUI_SOCKET would pin the live desktop's mouse.
synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
cursor_x() { synctl cursor | sed -n 's/.*"x":\([-0-9.]*\).*/\1/p' | cut -d. -f1; }

DEV=wlr_virtual_pointer_v1      # the name wlroots gives a virtual pointer

ADD=$(synctl virtual add 1280x720@60)
case "$ADD" in *'"ok":true'*) ;; *) fail "virtual add was refused: $ADD" ;; esac
VD=$(printf '%s' "$ADD" | sed -n 's/.*"name":"\([^"]*\)".*/\1/p')
VX=$(synctl outputs | tr '{' '\n' | grep "\"name\":\"$VD\"" |
     sed -n 's/.*"at":\[\([-0-9]*\),.*/\1/p')
[ -n "$VD" ] && [ -n "$VX" ] || fail "could not read the virtual display: $ADD"
[ "$VX" -gt 0 ] || fail "$VD is at x=$VX; the test needs it right of the base screen"
ok "virtual display $VD at x=$VX"

# Twelve moves of -300px from the top left: far past any left edge.
drive() { "$VPTR" 40 40 rel -300 0 12 >/dev/null 2>&1 || fail "vpointer_click failed"; }

# ── 1. the control: unpinned, the motion leaves for the base screen ───────
drive
X=$(cursor_x)
[ -n "$X" ] && [ "$X" -lt "$VX" ] \
    || fail "unpinned, the cursor ended at x=$X — the control proves nothing"
ok "unpinned, the cursor ends on the base screen (x=$X)"

# ── 2. pinned before the device exists, the same motion stays on $VD ──────
R=$(synctl input map "$VD" "$DEV")
case "$R" in *'"ok":true'*) ;; *) fail "input map was refused: $R" ;; esac
synctl input | grep -q "\"device\":\"$DEV\",\"output\":\"$VD\"" \
    || fail "the rule is not listed: $(synctl input)"
ok "the pin is recorded before any such device exists"

drive
X=$(cursor_x)
[ -n "$X" ] && [ "$X" -ge "$VX" ] && [ "$X" -lt $((VX + 1280)) ] \
    || fail "pinned to $VD (x $VX..$((VX + 1280))), the cursor ended at x=$X"
ok "pinned, a new device's motion cannot leave $VD (x=$X)"

# ── 3. unpinned again, it is free again ──────────────────────────────────
R=$(synctl input unmap "$DEV")
case "$R" in *'"ok":true'*) ;; *) fail "input unmap was refused: $R" ;; esac
drive
X=$(cursor_x)
[ -n "$X" ] && [ "$X" -lt "$VX" ] || fail "after unmap the cursor is still held (x=$X)"
ok "unmapped, the same motion reaches the base screen again"

# ── 4. the screen goes away under a pinned, moving pointer ───────────────
synctl input map "$VD" "$DEV" >/dev/null
"$VPTR" 40 40 absloop 60 40 >"$TMP/absloop.out" 2>&1 &
VPID=$!
sleep 0.8
synctl input | grep -q '"pinned":1' \
    || fail "the moving pointer was never pinned, so this phase proves nothing: $(synctl input)"
R=$(synctl virtual remove "$VD")
case "$R" in *'"ok":true'*) ;; *) fail "virtual remove was refused: $R" ;; esac
# ⛔ Asked while the same device is still moving, which is the case that matters.
synctl input | grep -q '"pinned":0' \
    || fail "the pointer is still pinned to $VD after it was removed: $(synctl input)"
wait "$VPID" 2>/dev/null
VPID=
kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died when the pinned screen went away"
grep -q 'compositor went away' "$TMP/absloop.out" \
    && fail "the pointer's absolute motion after the removal killed synui"
drive
kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died on the next motion after the screen went"
X=$(cursor_x)
[ -n "$X" ] || fail "synui stopped answering after the screen went"
ok "removing the pinned screen unpins the pointer while it is still moving, and synui runs on (x=$X)"

kill "$SYNUI_PID" 2>/dev/null
wait "$SYNUI_PID" 2>/dev/null
SYNUI_PID=
cleanup
echo "input_pin: all checks passed"
