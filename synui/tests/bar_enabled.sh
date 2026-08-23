#!/bin/sh
# bar_enabled.sh — turning the BAR off turns off THE BAR, and nothing else.
#
# The bug this pins, and it is why the test is shaped the way it is:
#
#   Control panel ▸ Desktop ▸ Bar used to do its work by running
#   `bar_stop_cmd`, whose default was `pkill -x quickshell ; pkill -x waybar`.
#   But the bar's process is not only the bar — quickshell/shell.qml maps the
#   bar, EVERY DESKTOP WIDGET, the OSD, the start menu, the mixer and the
#   post-it notes from one instance. So asking for the strip across the top to
#   go away killed the visualiser, the big clock, the notes and Tux with it,
#   with nothing on screen connecting the two.
#
# `bar_enabled` is a key the bar reads now (BarConfig.qml watches settings.state,
# Bar.qml maps or unmaps its window off it), so the switch reaches one window.
#
# THREE captures on one compositor and one shell:
#
#   A  bar_enabled unset   — the desktop everyone has: bar up, widget up
#   B  bar_enabled = off   — bar GONE, widget STILL THERE          (the bug)
#   C  bar_enabled = on    — bar BACK, live, with no restart
#
# WHAT IS ASSERTED. The wallpaper is a flat grey, so every probe is one
# question — furniture, or desktop? — and no probe needs to know what colour
# anything is, which is what stops a theme change from rewriting the test.
#
#   1. A: the strip is furniture and the bottom-right quadrant is furniture.
#      The control: without both there is nothing for B to lose.
#   2. B: where the strip was is FLAT WALLPAPER. The bar is gone, not merely
#      restyled. ⚠ The probe is INSIDE the strip, not on row 0: the compositor
#      draws a vignette a few pixels deep around the whole screen, which is
#      there with the bar off too and would make row 0 read "bar" forever.
#   3. B: the bottom-right quadrant still carries essentially all the widget it
#      had in A. ⚠ THIS IS THE WHOLE POINT — it was ZERO when the row killed the
#      process, and it is the assertion that cannot be satisfied by a stop
#      command however carefully it is written.
#   4. C: the strip is furniture again, from the watch alone.
#
# The big clock is the widget because it is anchored BOTTOM-right: the bar's
# exclusive zone is at the top, so switching it off moves the usable area's top
# edge and cannot move this widget. A top-anchored widget would slide up by a
# bar's height and fail probe 3 for a reason that is not the bug.
#
# Usage: bar_enabled.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node or without quickshell/grim/PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: bar_enabled.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: bar_enabled.sh /path/to/synui /path/to/quickshell-tree}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL, numpy' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL/numpy not installed."; exit 77; }

# SHORT: quickshell's ipc socket lives under XDG_RUNTIME_DIR and a unix path is
# capped at 108 bytes, which a build directory alone can blow.
TMP=$(mktemp -d /tmp/barenab.XXXXXX)
chmod 700 "$TMP"
LOG="$TMP/synui.log"
QSLOG="$TMP/qs.log"

cleanup() {
    [ -n "${QS_PID:-}" ]    && kill -9 "$QS_PID"    2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup INT TERM EXIT

fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG"   2>/dev/null
    echo "--- bar log (tail) ---";   tail -30 "$QSLOG" 2>/dev/null
    exit 1
}

# Hermetic HOME and runtime dir, and SYNUI_SOCKET unset: it is set in some
# shells and points at the LIVE desktop, and both synctl (which WidgetState runs
# to find the primary output) and friends prefer it over WAYLAND_DISPLAY — a rig
# that leaves it set reconfigures the machine it is running on.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

# No welcome panel — a hermetic HOME is a first run.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# A FLAT GREY DESKTOP. Every probe below asks bar-or-desktop and widget-or-
# desktop of raw pixels, so the wallpaper has to be one flat colour that neither
# resembles; the bundled starfield is dark exactly where the bar lives.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (128, 128, 128)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"

# NO AUTOSTART (config.c defaults it to `kitty`, and a mapped terminal lands
# under the bar), NO DOCK and NO DESKTOP ICONS — both are compositor-drawn
# furniture that would put non-wallpaper pixels inside a probe that is asking
# about the widget.
{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\n'
    printf 'dock_enabled = off\ndesktop_icons = off\n'
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
} > "$CFG/synuirc"

# The big clock, bottom-right. See the header for why not a top-anchored one.
printf 'clock = on\n' > "$CFG/widgets.state"

# A is the desktop with NO settings.state at all, which is also the state a
# fresh install is in: the first capture therefore proves the absent-key
# default as well as the control.
rm -f "$CFG/settings.state"

"$SYNUI" >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died on startup"
    sleep 0.1; i=$((i + 1))
done
[ -n "$SOCK" ] || fail "no Wayland socket within 10s"
export WAYLAND_DISPLAY="$SOCK"
OUTPUT=HEADLESS-1
echo "compositor: WAYLAND_DISPLAY=$SOCK output=$OUTPUT"

quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 5
kill -0 "$QS_PID" 2>/dev/null || fail "the shell died on startup"

shot() {   # shot <name>
    grim -t ppm -o "$OUTPUT" "$TMP/$1.ppm" 2>>"$QSLOG" || fail "grim failed for $1"
    echo "captured: $1"
}

shot a-on

# Written with everything already up, so these exercise the FileView watch
# rather than the startup read — which is the half a user actually meets, since
# the control panel writes this key on a running desktop. Generous settle: the
# bar's window is destroyed and rebuilt, and the compositor reflows around the
# exclusive zone going away and coming back.
printf 'bar_enabled = off\n' > "$CFG/settings.state"
sleep 3
shot b-off

# ⚠ The shell must still be alive here. If it is not, the switch did the very
# thing this test exists to stop, and every pixel below would be measuring an
# empty desktop rather than a hidden bar.
kill -0 "$QS_PID" 2>/dev/null \
    || fail "the shell EXITED when the bar was switched off — turning the bar
    off is not supposed to be able to reach the widgets, the notes, the OSD or
    the start menu, and it just took the whole process"

printf 'bar_enabled = on\n' > "$CFG/settings.state"
sleep 3
shot c-back

python3 - "$TMP" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

tmp = sys.argv[1]
WP = np.array([128, 128, 128], dtype=np.int16)     # the flat wallpaper

def load(name):
    return np.asarray(Image.open(f"{tmp}/{name}.ppm").convert("RGB"), dtype=np.int16)

A = load("a-on")
B = load("b-off")
C = load("c-back")
H, W = A.shape[:2]

def furniture(img, box):
    """Pixels inside (y0, y1, x0, x1) that are not the flat wallpaper.

    Not a diff against another capture: the clock's digits change between
    captures and its card does not, so 'is anything drawn here' is the question
    that survives a minute rolling over mid-test."""
    y0, y1, x0, x1 = box
    d = np.abs(img[y0:y1, x0:x1] - WP).sum(axis=2)
    return int((d > 12).sum())

# The middle of where the strip is — NOT the screen edge, and that is measured
# rather than tidy. The compositor draws a vignette about five pixels deep all
# the way round the screen (top rows, bottom rows and ~10 columns each side),
# which is furniture the bar has nothing to do with: it is still there with the
# bar off, so a probe on row 0 says "bar" forever. Rows 6..24 sit inside the
# 28px strip and clear of it, and trimming 12 columns off each side clears the
# vertical part of the same vignette. Measured on this rig: 22606 of 22608
# pixels with the bar up, and 0 with it off.
TOP = (6, 24, 12, W - 12)
TOP_AREA = (TOP[1] - TOP[0]) * (TOP[3] - TOP[2])
# The bottom-right quadrant, which is the big clock's corner and, with the dock
# and the desktop icons off, nothing but the vignette along two of its edges.
BR = (H // 2, H, W // 2, W)

a_top, a_br = furniture(A, TOP), furniture(A, BR)
b_top, b_br = furniture(B, TOP), furniture(B, BR)
c_top       = furniture(C, TOP)

print(f"  A bar on   strip {a_top:6d}px of {TOP_AREA}   clock corner {a_br:6d}px")
print(f"  B bar off  strip {b_top:6d}px of {TOP_AREA}   clock corner {b_br:6d}px")
print(f"  C bar on   strip {c_top:6d}px of {TOP_AREA}")

fails = []

# ── 1. the control ───────────────────────────────────────────
if a_top < 0.9 * TOP_AREA:
    fails.append(f"no bar to switch off: only {a_top}px of {TOP_AREA} where the "
                 f"strip belongs is anything but wallpaper")
if a_br < 500:
    fails.append(f"no widget to lose: only {a_br}px of the clock's corner is "
                 f"anything but wallpaper — check widgets.state and the "
                 f"primary output (`synctl outputs`)")

# ── 2. the bar is GONE ───────────────────────────────────────
# Not "changed": flat wallpaper, which is what an unmapped layer surface leaves
# behind and a restyled bar does not.
if b_top > 0.02 * TOP_AREA:
    fails.append(f"the bar did not go away: {b_top}px of {TOP_AREA} where the "
                 f"strip belongs is still furniture with bar_enabled = off")

# ── 3. THE BUG: the widget stayed ────────────────────────────
if b_br < 0.8 * a_br:
    fails.append(f"switching the bar off took the desktop widget with it: the "
                 f"clock's corner went from {a_br}px to {b_br}px. The bar and "
                 f"the widgets are one quickshell process, and the bar switch "
                 f"is not allowed to reach past its own window")

# ── 4. and it comes back, live ───────────────────────────────
if c_top < 0.9 * TOP_AREA:
    fails.append(f"the bar did not come back from the watch alone: {c_top}px of "
                 f"{TOP_AREA} where the strip belongs, with bar_enabled = on")

if fails:
    for f in fails:
        print(f"FAIL: {f}")
    sys.exit(1)
print("PASS")
PYEOF
