#!/bin/sh
# ws_bar_pills.sh — the bar's desktop pills light PER MONITOR
#
# ws_per_monitor.sh proves the compositor splits the desk. This is the surface
# the user actually looks at: the row of numbered pills in the bar, one bar per
# screen, each of which must highlight the desktop ITS OWN monitor is showing.
#
# ⚠ THE BUG THIS PINS IS INVISIBLE TO EVERY OTHER KIND OF CHECK. `synctl
# workspaces` marks a workspace `visible` when it is up on ANY monitor, so under
# per-monitor desktops several rows are visible at once and a bar reading that
# field lights every screen's desktop on every screen. Each row also carries
# `outputs` — the monitors showing it, by name — which is the per-screen answer;
# whether the QML actually uses it is a question only rendered pixels settle.
#
# And it is a STALE BINDING away from being wrong even with the right data.
# `active: shownHere(modelData)` in the delegate reads nothing QML can watch —
# modelData is a plain JS object — so the pill keeps whatever answer it was
# first given and the second screen ends up lighting BOTH its old desktop and
# its new one. That failure renders; it does not log. Hence pixels.
#
# Asserted on a two-headed headless rig, in both modes:
#   shared      — the control: both bars light the SAME pill, and it is the
#                 desktop the desk switched to.
#   per-monitor — the two bars light DIFFERENT pills, each its own monitor's.
#
# Usage: ws_bar_pills.sh /path/to/synui /path/to/synctl /path/to/quickshell-tree
# Skips (77) without a DRM render node, quickshell, grim, or python3 PIL/numpy.

set -u

SYNUI=${1:?usage: ws_bar_pills.sh /path/to/synui /path/to/synctl /path/to/quickshell-tree}
SYNCTL=${2:?usage: ws_bar_pills.sh /path/to/synui /path/to/synctl /path/to/quickshell-tree}
TREE=${3:?usage: ws_bar_pills.sh /path/to/synui /path/to/synctl /path/to/quickshell-tree}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -30 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${QS_PID:-}" ]    && kill -9 "$QS_PID"    2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t is not installed."; exit 77; }
done
python3 -c 'import PIL, numpy' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL/numpy not installed."; exit 77; }

TMP=$(mktemp -d /tmp/synui-pills.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
QSLOG="$TMP/qs.log"

# ⛔ SYNUI_SOCKET IS SET IN SOME SHELLS AND POINTS AT THE LIVE DESKTOP, which
# synctl prefers over WAYLAND_DISPLAY. Leaving it set here does not merely make
# the test wrong, it makes it read the machine it is running on: the bar under
# test polls the LIVE compositor, reports the live desk's desktops, and the
# assertions below measure somebody's actual screen. Found exactly that way.
unset SYNUI_SOCKET DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_WINDOWS"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=2

synctl() { "$SYNCTL" "$@" 2>/dev/null; }

start_all() {
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
    # Exported, not per-call: the bar shells out to `synctl` itself, and this is
    # the only thing that points those children at the rig.
    export SYNUI_SOCKET="$TMP/synui-$SOCK.sock"

    quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
    QS_PID=$!
    # The bar polls every 400ms; give it the startup plus a few polls.
    sleep 6
    kill -0 "$QS_PID" 2>/dev/null || fail "the bar died on startup: $(tail -20 "$QSLOG")"
}

stop_all() {
    kill -9 "$QS_PID" 2>/dev/null; QS_PID=
    kill -TERM "$SYNUI_PID" 2>/dev/null
    i=0
    while kill -0 "$SYNUI_PID" 2>/dev/null; do
        i=$((i + 1))
        [ $i -gt 50 ] && fail "synui did not exit within 5s of SIGTERM"
        sleep 0.1
    done
    wait "$SYNUI_PID"; SYNUI_PID=
    unset WAYLAND_DISPLAY SYNUI_SOCKET
}

# Which pill is lit, read off the bar itself — as "<how many> <where>".
#
# The active pill is the only element in the strip with a FILLED background: the
# design gives it a solid accent-tinted block and a border, while every other
# pill is bare bar with a numeral on it ("three states worth telling apart at a
# glance", modules/Workspaces.qml). So the discriminator is not brightness — the
# numerals and the launcher's wordmark are bright too — but how much of the
# strip's HEIGHT a column covers: a filled pill is ~100% of it, a glyph stroke
# under half. Measured, not assumed: the runs come out at 1.00 and the text
# columns at 0.15-0.50.
#
# Reporting the COUNT as well as the position is what catches the stale-binding
# failure, where the screen that moved lights its old pill and its new one at
# once — two runs whose centre still differs from the other screen's.
lit_pill() {
    grim -t ppm -o "$1" "$TMP/$1.ppm" 2>>"$QSLOG" || fail "grim failed for $1"
    python3 - "$TMP/$1.ppm" <<'PYEOF'
import sys
import numpy as np
from PIL import Image

im = np.array(Image.open(sys.argv[1]).convert("RGB")).astype(int)
# The bar is the top strip; the pills sit just right of the launcher button.
# Generous bounds — extra bar on either side is background and costs nothing.
strip = im[2:24, 0:360]
bg = np.median(strip.reshape(-1, 3), axis=0)
filled = (np.abs(strip - bg).sum(axis=2) > 30).mean(axis=0) > 0.85

runs, centres, run_start = 0, [], None
for x, f in enumerate(list(filled) + [False]):
    if f and run_start is None:
        run_start = x
    elif not f and run_start is not None:
        # Ignore anything too narrow to be a pill (the row is 22px wide).
        if x - run_start >= 8:
            runs += 1
            centres.append((run_start + x) // 2)
        run_start = None

print(f"{runs} {centres[0] if centres else -1}")
PYEOF
}

# ── 1. shared: both bars light the same pill ────────────────────────────
# The control. Without it "the two bars differ" in phase 2 proves nothing — two
# bars can differ because one of them is simply wrong.
printf 'workspace_mode = shared\nanim_workspace = off\nanimation_ms = 0\nwelcome_at_startup = off\n' \
    > "$SYNUI_CONFIG"
start_all
synctl dispatch ws 3 >/dev/null
sleep 2.5
S1=$(lit_pill HEADLESS-1); S2=$(lit_pill HEADLESS-2)
echo "shared:     HEADLESS-1 [$S1]  HEADLESS-2 [$S2]  (count centre)"
[ "${S1% *}" = 1 ] || fail "HEADLESS-1 lights ${S1% *} pills under shared, not 1.
       The bar either did not load or is not drawing an active desktop:
$(tail -20 "$QSLOG")"
[ "$S1" = "$S2" ] || fail "under workspace_mode = shared a desktop spans the
       desk, so both bars must light the SAME pill. Got [$S1] and [$S2]."
stop_all

# ── 2. per-monitor: the two bars light DIFFERENT pills ──────────────────
printf 'workspace_mode = per-monitor\nanim_workspace = off\nanimation_ms = 0\nwelcome_at_startup = off\n' \
    > "$SYNUI_CONFIG"
start_all
FOCUSED=$(synctl outputs | tr '{' '\n' | sed -n \
            's/.*"name":"\([^"]*\)".*"focused":true.*/\1/p')
[ -n "$FOCUSED" ] || fail "no output reports itself focused: $(synctl outputs)"

synctl dispatch ws 3 >/dev/null
sleep 2.5
P1=$(lit_pill HEADLESS-1); P2=$(lit_pill HEADLESS-2)
echo "per-mon:    focused=$FOCUSED  HEADLESS-1 [$P1]  HEADLESS-2 [$P2]"

# Exactly ONE pill each. A screen shows exactly one desktop, and the stale
# delegate binding lights two on the screen that moved — which the "they differ"
# test below would happily pass, since the lit centre moves either way.
for pair in "HEADLESS-1 $P1" "HEADLESS-2 $P2"; do
    o=${pair%% *}; n=${pair#* }; n=${n%% *}
    [ "$n" = 1 ] || fail "$o lights $n pills, not 1. A screen shows exactly one
       desktop. Two is the stale delegate binding: `active` read modelData —
       a plain JS object — so nothing in the expression was a property QML could
       watch, and the pill kept the answer it was first given. The binding has
       to read root.workspaces, which the poll reassigns."
done

[ "${P1#* }" != "${P2#* }" ] || fail "both bars are lighting the same pill while
       the two monitors are on different desktops. The pill row is reading
       'visible' — true for every desktop that is up ANYWHERE — instead of the
       row's 'outputs' array. Compositor side: $(synctl outputs)"

echo "per-mon:    exactly one pill lit on each screen, and they differ"

stop_all
echo "PASS"
exit 0
