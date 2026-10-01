#!/bin/sh
# imgview_leak.sh — repainting the image viewer does not grow the compositor.
#
# render_crop_view() paints the whole output into a fresh cairo buffer on every
# repaint — each step, zoom, pan motion and re-open. create_cairo_buf() hands
# back a wlr_buffer AND a cairo_t, and the cairo_t holds its own reference on
# the surface. set_scene_buffer() drops only the wlr_buffer, so a painter that
# forgets cairo_destroy(cr) frees the wrapper and keeps the full-output ARGB
# image: 14.7 MB per repaint at 2560x1440.
#
# That is what happened (pkgrel 405 → 627). Four weeks of looking at pictures
# left 278 of those images in the live compositor's [heap] — 3.9 GB of 4 GB —
# and an earlier session was OOM-killed with swap full. Nothing crashes and
# nothing looks wrong on screen, and the ASan suite never saw it: every rig
# test ends in kill -9, so LeakSanitizer never gets to run at exit.
#
# So this measures the process instead: open the viewer, warm its caches, note
# RssAnon+VmSwap, repaint it REPAINTS more times, and require the growth to stay
# under a few frames' worth. RssAnon rather than [heap] so the same assertion
# holds under ASan, whose allocator does not use brk.
#
# Usage: imgview_leak.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, like every nested-synui test here.
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
# quarantine_size_mb=0: ASan holds every freed chunk in a 256 MB quarantine by
# default, so a painter that frees its surface correctly still grows RssAnon by
# one frame per repaint — the exact signature this test fails on. Measured
# under ASan: 117720 kB over 30 repaints with the quarantine, on a tree whose
# plain build passes. This test asks about RSS, not use-after-free.
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1:quarantine_size_mb=0"

SYNUI=${1:?usage: imgview_leak.sh synui synctl}
SYNCTL=${2:?usage: imgview_leak.sh synui synctl}

REPAINTS=30

fail() { echo "FAIL: $*" >&2; cleanup; exit 1; }
cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-imgleak.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

SRC=$(cd "$TESTDIR/.." && pwd)/data
mkdir -p "$TMP/pics"
for f in wallpaper.png synapse-logo.png; do
    [ -r "$SRC/$f" ] || { echo "SKIP: $SRC/$f is missing"; exit 77; }
    cp "$SRC/$f" "$TMP/pics/$f"
done
PIC="$TMP/pics/wallpaper.png"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc" SYNUI_WINDOWS="$TMP/windows.conf"
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
CTLSOCK="$TMP/synui-$SOCK.sock"

# ⚠ SYNUI_SOCKET, always: without it synctl talks to the ambient
# WAYLAND_DISPLAY, which on a developer's machine is the live desktop.
synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

# Anonymous memory the compositor holds, resident or swapped, in kB.
anon_kb() {
    awk '/^RssAnon:|^VmSwap:/ { s += $2 } END { print s + 0 }' \
        "/proc/$SYNUI_PID/status"
}
views() { grep -c 'synui: view: ' "$LOG"; }

OUTBOX=$(synctl outputs | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1 \2/p' | head -1)
OW=${OUTBOX%% *}; OH=${OUTBOX##* }
[ -n "$OW" ] && [ -n "$OH" ] || fail "could not read the output size from synctl outputs"
FRAME_KB=$(( OW * OH * 4 / 1024 ))
echo "output: ${OW}x${OH} — one leaked repaint would be ${FRAME_KB} kB"

# Open, then repaint a few times before the baseline: the first opens decode
# the picture and build the scaled copy crop_scaled() caches, and those are
# kept on purpose.
synctl dispatch view "$PIC" >/dev/null; sleep 0.5
[ "$(views)" -ge 1 ] || fail "the viewer did not open: $(grep 'crop.c' "$LOG" | tail -3)"
for i in 1 2 3 4 5; do synctl dispatch view "$PIC" >/dev/null; sleep 0.1; done
sleep 0.5
V0=$(views); A0=$(anon_kb)

i=0
while [ $i -lt $REPAINTS ]; do
    synctl dispatch view "$PIC" >/dev/null
    sleep 0.1
    i=$((i + 1))
done
sleep 0.5
kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died while the viewer repainted"
V1=$(views); A1=$(anon_kb)

PAINTED=$(( V1 - V0 ))
GREW=$(( A1 - A0 ))
echo "  $PAINTED viewer repaints: anon+swap ${A0} -> ${A1} kB (${GREW} kB)"

[ "$PAINTED" -ge $(( REPAINTS / 2 )) ] || fail "only $PAINTED of $REPAINTS re-opens
       reached the viewer — the test is not exercising the painter"

# Three frames of slack: allocator noise and a scene buffer still held by the
# renderer are real, a surface per repaint is thirty of them.
LIMIT=$(( FRAME_KB * 3 ))
if [ "$GREW" -gt "$LIMIT" ]; then
    fail "the compositor grew ${GREW} kB over $PAINTED repaints (limit ${LIMIT} kB,
       ~$(( GREW / FRAME_KB )) full-output images). A painter is keeping its
       surface alive — check that render_crop_view() calls cairo_destroy(cr)
       before set_scene_buffer()."
fi
echo "  ok    repainting the viewer does not grow the compositor"

cleanup
echo "imgview_leak: ok"
