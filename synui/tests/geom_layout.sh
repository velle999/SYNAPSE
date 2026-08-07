#!/bin/sh
# geom_layout.sh — does remembered geometry stay out of the tiler's way?
#
# The bug (velle, 2026-07-31): "tiling isn't working". The desktop reported
# `"layout":"tiling"` and every window on it reported `"floating":true`, so the
# tiler — which skips floating windows in every layout — had nothing to place.
#
# Why: windows.conf records a floating flag per app (geom_persist_save), and
# layout_restore_geometry used to honour it on ANY desktop, re-floating the
# window on map so its remembered box would survive. One edge-snap was enough to
# seed it — snap_view sets floating for the duration and unmap records the live
# flag — and after that it fed itself: reopen floating, close floating, write
# floating=1 back out. Every app on velle's desk had gone that way.
#
# The rule now: a layout that places windows itself owns their geometry, so a
# window OPENING on a tiling or AI desktop ignores the table. Floating and
# monocle keep the whole feature, and Super+F still restores the box on any
# desktop, because that is the user asking for the window back rather than the
# window merely opening.
#
# The test seeds windows.conf with a floating 600x400 entry for the client's
# app_id and opens the same client on each of the four layouts in turn:
#
#   1. tiling   — bypass: the window comes up NOT floating, at the layout's size
#                 and not at 600x400. This is the reported bug; it fails here
#                 without the fix.
#   2. monocle  — restore: floating, at 600x400. Monocle skips floating windows
#                 the same way tiling does, so the entry has to re-float it for
#                 the box to survive.
#   3. AI       — bypass, same as tiling.
#   4. niri     — bypass, same as tiling: a scrollable-tiling desktop opens the
#                 window in a column of its own and owns its geometry.
#   5. floating — restore: at 600x400. The feature still works where it most
#                 obviously belongs, which is what stops the fix being a revert.
#
# The order is not arbitrary — it is the cycle order (tiling → floating →
# monocle → AI → niri → …) walked so that the seed survives to the phases that
# need it.
# The table is live: the compositor holds it in memory and rewrites the whole
# file when a window unmaps, so re-seeding the file between phases would do
# nothing. Monocle re-floats the window, so closing it there writes floating=1
# back and the seed stands; the AI and tiling phases leave a tiled window, which
# geom_persist_save declines to record at all. Floating goes last because a
# floating DESKTOP does not set view->floating on its windows — closing one
# there records floating=0 and would strand every later phase at full size.
#
# Usage: geom_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does:
# synui renders through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: geom_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: geom_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: geom_layout.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

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
TMP=$(mktemp -d /tmp/synui-geom.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

# The table under test. SYNUI_WINDOWS points geom_persist.c at it directly, so
# the seed cannot be confused with a real one. floating=1 is the flag that used
# to re-float the window on every desktop; 600x400 is small enough that no
# layout would ever produce it by accident on a headless 1920x1080 output.
SEED_W=600
SEED_H=400
export SYNUI_WINDOWS="$TMP/windows.conf"
cat > "$SYNUI_WINDOWS" <<EOF
window stubborn x=120 y=100 w=$SEED_W h=$SEED_H maximized=0 floating=1
EOF

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
CTLSOCK="$TMP/synui-$SOCK.sock"

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

# Open the client, hold it mapped, and hand back its line from synctl clients:
# floating/maximized are live state, only readable while the window exists.
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
    # Let the unmap settle before the next phase, or two windows overlap and
    # the tiler splits the screen between them.
    i=0
    while [ $i -lt 40 ]; do
        synctl clients | grep -q '"app_id":"stubborn"' || break
        i=$((i + 1)); sleep 0.1
    done
}

# "size":[W,H] out of the client's line.
win_size() { echo "$WIN" | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1x\2/p'; }

layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }

# ── 1. tiling: the table has no say ──────────────────────────────────────
[ "$(layout_now)" = "tiling" ] || fail "expected a tiling desktop at startup, got $(layout_now)"

open_window
echo "tiling:   $(win_size) $WIN"
case "$WIN" in
    *'"floating":true'*)
        fail "the window opened FLOATING on a tiling desktop — windows.conf
       re-floated it, so the tiler will skip it and the desktop stays empty of
       tiles. This is the reported bug, unfixed.
       $WIN" ;;
esac
[ "$(win_size)" = "${SEED_W}x${SEED_H}" ] && fail "the window took its remembered
       ${SEED_W}x${SEED_H} box on a tiling desktop instead of the layout's size."
TILED_SIZE=$(win_size)
close_window

# ── 2. monocle: the entry still gets its say ─────────────────────────────
# Through floating without opening anything: a window closing there would
# rewrite the seed (see the header).
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = "floating" ] || fail "layout_cycle did not reach floating, got $(layout_now)"
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = "monocle" ] || fail "layout_cycle did not reach monocle, got $(layout_now)"

open_window
echo "monocle:  $(win_size) $WIN"
case "$WIN" in
    *'"floating":true'*) ;;
    *) fail "on a MONOCLE desktop the window came up in the flow — the entry
       says the user had floated this app, and monocle skips floating windows
       just like tiling does, so the remembered box cannot survive without it.
       $WIN" ;;
esac
[ "$(win_size)" = "${SEED_W}x${SEED_H}" ] || fail "on a MONOCLE desktop the window
       came up $(win_size), not its remembered ${SEED_W}x${SEED_H}. The bypass was
       supposed to be scoped to tiling and AI — this is a revert of the whole
       feature.
       $WIN"
close_window

# ── 3. AI: same as tiling ────────────────────────────────────────────────
# No synapd here, so layout_request_ai falls straight through to layout_tile —
# which is the point: the placement is still the layout's, not the table's.
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = "ai" ] || fail "layout_cycle did not reach ai, got $(layout_now)"

open_window
echo "ai:       $(win_size) $WIN"
case "$WIN" in
    *'"floating":true'*)
        fail "the window opened FLOATING on an AI desktop — windows.conf
       re-floated it out of the flow the AI layout is meant to arrange.
       $WIN" ;;
esac
[ "$(win_size)" = "$TILED_SIZE" ] || fail "the AI desktop placed the window at
       $(win_size); with no synapd it falls back to layout_tile, which put the
       same window at $TILED_SIZE on the tiling desktop."
close_window

# ── 4. niri: same as tiling ──────────────────────────────────────────────
# A niri desktop places the window itself — a fresh column beside the focused
# one — so the table has no say here either. Its own phase rather than a line in
# the tiling one because it reaches the bypass through a different branch of
# layout_restore_geometry's `layout_places_it`, and because the size it lands at
# is NOT the tiling size (a default column is half the usable width), so a
# bypass that quietly fell through to the tiler would still show up here.
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = "niri" ] || fail "layout_cycle did not reach niri, got $(layout_now)"

open_window
echo "niri:     $(win_size) $WIN"
case "$WIN" in
    *'"floating":true'*)
        fail "the window opened FLOATING on a niri desktop — windows.conf
       re-floated it out of the strip, and a floating window is in no column at
       all, so it can never be scrolled to.
       $WIN" ;;
esac
[ "$(win_size)" = "${SEED_W}x${SEED_H}" ] && fail "the window took its remembered
       ${SEED_W}x${SEED_H} box on a niri desktop instead of a column of the
       layout's own width.
       $WIN"
close_window

# ── 5. floating: the feature still works ─────────────────────────────────
# Round the cycle back: niri → spiral → tiling → floating.
synctl dispatch layout_cycle >/dev/null
synctl dispatch layout_cycle >/dev/null
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = "floating" ] || fail "layout_cycle did not come back round to
       floating, got $(layout_now). The cycle order is the syn_layout_t order:
       tiling, floating, monocle, AI, niri, spiral."

open_window
echo "floating: $(win_size) $WIN"
# Only the box is asserted here: on a floating desktop every window is free
# whether or not view->floating is set, so the flag is not the interesting half.
[ "$(win_size)" = "${SEED_W}x${SEED_H}" ] || fail "on a FLOATING desktop the window
       came up $(win_size), not its remembered ${SEED_W}x${SEED_H}.
       $WIN"
close_window

# ── 6. clean shutdown ────────────────────────────────────────────────────
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

echo "PASS: geom_layout"
cleanup
exit 0
