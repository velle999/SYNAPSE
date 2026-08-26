#!/bin/sh
# pointer_smoothing.sh — a smoothed pointer arrives late, never short.
#
# THE FEATURE (2026-08-26): a Logitech M317C was hard to hold steady — a 1000
# DPI wireless sensor rattles, and the pointer wobbles with it. libinput has no
# smoothing to turn on, so synui grew `pointer_smoothing`: a low-pass filter
# over the cursor's own path.
#
# ⚠ THE ONE PROPERTY WORTH TESTING IS THAT NO TRAVEL IS LOST. A filter that
# averages the last few reports feels smooth and quietly eats the tail of every
# movement: the pointer ends up short of where you put it, by a little, every
# time, and the complaint that comes back is "it doesn't go where I aim" — the
# exact thing smoothing was turned on to fix. synui's filter is a leaky bucket
# with a settle timer instead, so the assertion here is an EQUALITY against the
# unsmoothed run and not a tolerance band.
#
# It is driven through the RELATIVE motion path, which is the only one that is
# smoothed. `vpointer_click X Y rel DX DY N` sends N real relative reports; the
# absolute motion every other mode uses is deliberately exempt (a tablet has to
# sit under the stylus) and would test the wrong branch.
#
# ⚠ WHAT THIS CANNOT ASSERT, AND HOW TO CHECK IT BY HAND. That the filter is
# engaging AT ALL is invisible from here: the only moment the two runs differ is
# between the last report and the settle timer 16 ms later, and a synctl round
# trip does not reliably fit inside that — a check placed there would be flaky
# in one direction or false-passing in the other. Everything below would still
# pass if `pointer_smoothing` were being ignored outright.
#
# To confirm it is not, raise the interval in psmooth_arm() from 16 to 2000, and
# send two reports of (100,0) 8 ms apart from (200,200) with the settle wait off
# (`vpointer_click 200 200 rel 100 0 2 8 0`). The first report always emits in
# full and the second emits 1-exp(-8/tau) of itself, so the cursor should read:
#
#     pointer_smoothing = 0   ->  x = 400.000   (no filter)
#     pointer_smoothing = 1   ->  x = 379.810   (tau  5 ms)
#     pointer_smoothing = 10  ->  x = 314.786   (tau 50 ms)
#
# Measured 2026-08-26, matching the closed form to three decimals. Put the 16
# back afterwards — with it stretched, the pointer visibly coasts.
#
# Usage: pointer_smoothing.sh /path/to/synui /path/to/synctl /path/to/vpointer_click
# Skips (77) without a DRM render node, as every nested-synui test here does.
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: pointer_smoothing.sh synui synctl vpointer_click}
SYNCTL=${2:?usage: pointer_smoothing.sh synui synctl vpointer_click}
VPTR=${3:?usage: pointer_smoothing.sh synui synctl vpointer_click}

PASS=0; FAILED=0
ok()   { echo "  ok    $*"; PASS=$((PASS + 1)); }
bad()  { echo "  FAIL  $*" >&2; FAILED=$((FAILED + 1)); }
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

TMP=$(mktemp -d /tmp/synui-psmooth.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

# ⚠ SYNUI_SOCKET, always: without it synctl talks to the ambient
# WAYLAND_DISPLAY, which on a developer's machine is the LIVE desktop, and this
# test would then be reading — and the vpointer moving — velle's own cursor.
synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

# Boot a compositor with the given synuirc body and leave it running.
boot() {
    printf '%s\n' "$1" > "$TMP/synuirc"
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

halt() {
    kill -9 "$SYNUI_PID" 2>/dev/null
    wait "$SYNUI_PID" 2>/dev/null
    SYNUI_PID=
    rm -f "$TMP"/wayland-* "$TMP"/synui-*.sock
}

# The cursor, as "X,Y" rounded to whole pixels. Rounded because the filter works
# in doubles and the comparison is about where the pointer LANDED, not about the
# last bit of a float — a sub-pixel difference is not something anyone can see
# or click differently.
cursor() {
    synctl cursor | sed -n 's/.*"x":\([0-9.-]*\),"y":\([0-9.-]*\).*/\1 \2/p' \
        | awk '{ printf "%d,%d\n", int($1 + 0.5), int($2 + 0.5) }'
}

# Start at (200,200), then 40 reports of (4,3) — 160,120 of travel — spaced 8 ms
# apart, which is a 125 Hz mouse. Small per-report deltas on purpose: a single
# large jump is the one shape a smoothing filter has nothing to do.
run_stroke() {
    "$VPTR" 200 200 rel 4 3 40 8 >/dev/null 2>&1 \
        || fail "vpointer_click rel failed (is the virtual-pointer global up?)"
    cursor
}

echo "an unsmoothed pointer is the reference"
boot "pointer_smoothing = 0"
BASE=$(run_stroke)
[ -n "$BASE" ] || fail "synctl cursor returned nothing — is the verb wired up?"
if [ "$BASE" = "360,320" ]; then
    ok "40 reports of (4,3) from (200,200) land at $BASE"
else
    bad "expected the raw stroke to land at 360,320, got $BASE"
fi
halt

# Both ends of the range, because the interesting failure is at the SLOW end:
# the more the filter holds back per report, the more is left in hand when the
# reports stop, and the further short the cursor would sit without the settle
# timer that applies the remainder.
for STRENGTH in 1 5 10; do
    echo "pointer_smoothing = $STRENGTH lands in the same place"
    boot "pointer_smoothing = $STRENGTH"
    GOT=$(run_stroke)
    if [ "$GOT" = "$BASE" ]; then
        ok "the same stroke still lands at $GOT"
    else
        bad "smoothed to $STRENGTH the stroke landed at $GOT, not $BASE — the
        filter is eating travel, which is the bug it must not have"
    fi
    halt
done

# The filter must not survive a reload with the setting turned off, and it must
# not leave a remainder behind that a later movement inherits.
echo "turning it off mid-session leaves the pointer where it is"
boot "pointer_smoothing = 10"
FIRST=$(run_stroke)
printf 'pointer_smoothing = 0\n' > "$TMP/synuirc"
kill -HUP "$SYNUI_PID" 2>/dev/null
sleep 0.5
AFTER=$(cursor)
if [ "$AFTER" = "$FIRST" ]; then
    ok "SIGHUP with smoothing off did not move the cursor from $AFTER"
else
    bad "the cursor moved from $FIRST to $AFTER across the reload"
fi
SECOND=$(run_stroke)
# The second stroke starts with an absolute motion back to (200,200), so it must
# land in exactly the same place as the first — a remainder carried across the
# reload would show up here as an offset.
if [ "$SECOND" = "$BASE" ]; then
    ok "and the next stroke lands unsmoothed at $SECOND"
else
    bad "after the reload the stroke landed at $SECOND, not $BASE"
fi
halt

cleanup
echo "$PASS passed, $FAILED failed"
[ "$FAILED" -eq 0 ] || exit 1
