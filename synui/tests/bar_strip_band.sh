#!/bin/sh
# bar_strip_band.sh — a bar module's ink is measured off the strip the BAR
# covers, and not off a grid cell four times deeper than the bar.
#
# THE BUG THIS EXISTS FOR was on screen for months and reads as "half the bar
# stopped inverting": the weather and the volume in white, the CPU meter, the
# Bluetooth glyph and the network glyph in BLACK, side by side on one strip, all
# of them identical code reading identical properties.
#
# Nothing was wrong with the ink rule. What was wrong was the picture it was
# asked about. The bar decides per MODULE — Theme.qml's barPaletteAt() folds the
# columns a module covers — and for a column no window covers, the only
# per-column answer available was wp_lum_grid's TOP ROW. A grid row is
# SYN_LUM_ROWS deep: a ninth of the screen, 160 pixels on a 1440 monitor,
# standing in for a bar 34 logical pixels tall.
#
# Four fifths of that cell is picture the bar is not standing on. On the
# photograph this was found with — a dark canopy along the top edge over lit
# leaves below it — the cell measured 0.29 where the strip itself measured 0.08,
# and those two numbers are on opposite sides of the ink flip. So the modules
# over that column inked for a bright backdrop and were drawn on a dark one.
#
# ⚠ AND THE WHOLE-BAR ANSWER WAS RIGHT THE WHOLE TIME, which is what made it
# look like an inversion bug rather than a measurement one: `bar_ink` has always
# been taken off the strip's own rows (wallpaper_strip_lum), so the modules with
# no per-column answer of their own — and the clock, whose span straddles two
# disagreeing columns and vetoes — came out correctly white.
#
# THE WALLPAPER HERE IS THAT SHAPE, deliberately and minimally: its right half
# is dark for exactly the rows the bar covers and bright underneath, so the top
# grid row and the bar's own strip disagree by more than the width of the flip.
# Its left half is that same dark all the way down, so the two agree there and
# the halves are each other's control.
#
# TWO PHASES, and the second is the one a user would recognise:
#
#   1. backdrop.state. `wp_strip.<output>` is the strip's own rows per column —
#      it must exist, and it must DISAGREE with grid.<output>'s top row over the
#      right half, to the point of implying the opposite ink. A row that merely
#      echoed the grid would pass every other check here.
#   2. The bar on screen. No pixel of the strip may be the DARK ink, and the
#      strip must carry a healthy count of the light one. Before the fix the
#      right-hand modules drew #1D1D1F on a backdrop of #4B4B4B.
#
# Usage: bar_strip_band.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node, quickshell, grim, or PIL/numpy.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: bar_strip_band.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: bar_strip_band.sh /path/to/synui /path/to/quickshell-tree}

# 77 is meson's SKIP code. synui renders through scenefx's fx_renderer, which is
# GLES2 and DMA-BUF only.
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
TMP=$(mktemp -d /tmp/barstripband.XXXXXX)
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

# Hermetic HOME and runtime dir, and SYNUI_SOCKET unset: this writes
# settings.state, and synctl and friends prefer that variable over
# WAYLAND_DISPLAY, so a rig that leaves it set reconfigures the live desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless
unset DISPLAY WAYLAND_DISPLAY
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

# No welcome panel — a hermetic HOME is a first run.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# THE TRAP, and every number in it is load-bearing.
#
#   #4B4B4B is luminance 0.070. White ink clears 8.7:1 on it and black 2.0:1, so
#   the strip's own answer is `light` and it is not close.
#   #B4B4B4 is 0.457. Filling the rest of the top grid row with it drags that
#   CELL to 0.347, where black clears 6.4:1 and white 2.6:1 — `dark`, and not
#   close either. The two readings of the same column disagree completely.
#
# 34 rows deep because that is SYN_BAR_STRIP_LOGICAL, painted at the headless
# backend's own 1280x720 so nothing is resampled: a grid row is then 80 rows and
# the bar covers the top 42% of the one that used to answer for it.
#
# ⚠ AND THE GRID IS SAMPLED, NOT INTEGRATED — grid_luminance_px() steps every
# h/(ROWS*4) rows, so row 0 here is four probes at y=0, 20, 40 and 60. A band
# between 20 and 40 rows deep is what puts two of them on each side; one 45 deep
# would read as three-quarters dark and the cell would agree with the strip,
# which is a test that passes for the wrong reason. Phase 1 asserts the trap
# still bites rather than trusting this arithmetic to survive a backend that
# changes its default size.
python3 -c "
from PIL import Image
im = Image.new('RGB', (1280, 720), (75, 75, 75))
px = im.load()
for x in range(640, 1280):
    for y in range(34, 720):
        px[x, y] = (180, 180, 180)
im.save('$TMP/wp.png')" || fail "could not write the test wallpaper"

# Writing any synuirc also resets `autostart`, which config.c defaults to a
# terminal — a mapped window would sit under the bar and answer for the strip
# instead of the wallpaper, which is a different test (bar_scene_strip.sh).
printf 'wallpaper = %s\nwallpaper_mode = fill\n' "$TMP/wp.png" > "$CFG/synuirc"

# A CLEAR bar, so the ink is the wallpaper's answer rather than the theme's.
# 0.00 and not a glass preset: this must not depend on which theme is default.
printf 'bar_opacity = 0.00\n' > "$CFG/settings.state"

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

i=0
while [ $i -lt 60 ]; do
    [ -f "$CFG/backdrop.state" ] && break
    sleep 0.1; i=$((i + 1))
done
[ -f "$CFG/backdrop.state" ] || fail "no backdrop.state within 6s — the compositor never painted"

# ── phase 1: the measurement ────────────────────────────────────────────
python3 - "$CFG/backdrop.state" "$OUTPUT" <<'PYEOF' || exit 1
import re, sys

state = open(sys.argv[1], encoding="utf-8").read()
out   = sys.argv[2]
COLS, ROWS = 16, 9

def row(key):
    m = re.search(r"^\s*%s\.%s\s*=\s*([-0-9.,]+)\s*$" % (re.escape(key), re.escape(out)),
                  state, re.M)
    return [float(v) for v in m.group(1).split(",")] if m else None

def ink(l):
    """Theme.qml's rule and contrast.h's, in one place: which of the two inks
    clears AA on a backdrop of this luminance, '' when neither does."""
    def c(a, b):
        hi, lo = max(a, b), min(a, b)
        return (hi + 0.05) / (lo + 0.05)
    cd, cl = c(0.0122771, l), c(1.0, l)
    if cd < 4.5 and cl < 4.5: return ""
    return "dark" if cd >= cl else "light"

fails = []

wp = row("wp_strip")
if wp is None or len(wp) != COLS:
    print("FAIL: backdrop.state has no wp_strip.%s row — the bar has nothing to "
          "fall back to but the grid cell that caused this" % out)
    sys.exit(1)

grid = row("grid")
if grid is None or len(grid) != COLS * ROWS:
    print("FAIL: backdrop.state has no usable grid.%s" % out)
    sys.exit(1)
top = grid[:COLS]

print("  wp_strip  " + " ".join("%.2f" % v for v in wp))
print("  grid row0 " + " ".join("%.2f" % v for v in top))
print("  ink       strip " + " ".join((ink(v) or "-")[0] for v in wp))
print("            cell  " + " ".join((ink(v) or "-")[0] for v in top))

# The LEFT half is the control: one flat colour top to bottom, so the two
# readings have to agree there. A wp_strip row that was wrong everywhere would
# otherwise look exactly like one that was right everywhere.
for c in range(0, COLS // 2):
    if abs(wp[c] - top[c]) > 0.03:
        fails.append("column %d is one flat colour and the two measurements "
                     "still disagree: strip %.2f, cell %.2f" % (c, wp[c], top[c]))

# …and the RIGHT half is the trap, where they must disagree — and disagree
# enough to land on opposite inks. This is the whole assertion: a wp_strip that
# echoed the grid row would satisfy everything above it.
for c in range(COLS // 2, COLS):
    if ink(wp[c]) != "light":
        fails.append("column %d measures %.2f over the bar's own rows, which "
                     "is not the light ink the strip is" % (c, wp[c]))
    if ink(top[c]) != "dark":
        fails.append("column %d's grid cell measures %.2f, which is not the "
                     "dark ink the trap needs — this wallpaper is no longer "
                     "testing anything" % (c, top[c]))

# And the whole bar's own answer, which was never wrong and is what the split
# was visible against.
m = re.search(r"^\s*bar_ink\.%s\s*=\s*(\S+)\s*$" % re.escape(out), state, re.M)
if not m or m.group(1) != "light":
    fails.append("bar_ink.%s is %s, not light" % (out, m.group(1) if m else "missing"))

for f in fails:
    print("FAIL: " + f)
sys.exit(1 if fails else 0)
PYEOF
echo "phase 1: the strip and the grid cell disagree, and the strip is right"

# ── phase 2: what is actually drawn ─────────────────────────────────────
quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 5
kill -0 "$QS_PID" 2>/dev/null || fail "the bar died on startup"

grim -t ppm -o "$OUTPUT" "$TMP/bar.ppm" 2>>"$QSLOG" || fail "grim failed"

python3 - "$TMP/bar.ppm" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

img = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.int16)

# Theme.qml: barHeight 28. Stop short of both edges — the top row is antialiased
# against the screen edge and the bottom carries the accent rule.
Y0, Y1 = 4, 24
# The compositor draws the "◢ SYNAPSE" launcher button over the top-left of the
# bar (launcher.c). It is not quickshell's ink and does not follow the backdrop.
X0 = 260
strip = img[Y0:Y1, X0:]
right = img[Y0:Y1, img.shape[1] // 2:]

def near(px, rgb, tol):
    return (np.abs(px - np.array(rgb, dtype=np.int16)).sum(axis=2) <= tol)

# The two inks barPaletteInked() names, and nothing else in the palette is
# within reach of either: the backdrop is #4B4B4B (136 away from the dark ink)
# and `dim` is #3A4A52 or #6B7280 (125 and 220 away). Antialiasing between the
# backdrop and white ink never runs darker than the backdrop, so a pixel this
# close to #1D1D1F was drawn by a module that chose the dark direction.
DARK, LIGHT = (29, 29, 31), (255, 255, 255)
dark_all  = int(near(strip, DARK,  18).sum())
dark_r    = int(near(right, DARK,  18).sum())
light_all = int(near(strip, LIGHT, 18).sum())
print(f"  strip ink   light {light_all}px   dark {dark_all}px "
      f"(right half {dark_r}px)")

fails = []

# The sanity half, and it counts BOTH inks on purpose: a bar that failed to
# draw has neither, and "no dark pixels" would then be a false pass. Asking for
# light ink alone would report the bug itself as "the bar is not drawing" — the
# failure moves the pixels from one colour to the other rather than removing
# them.
if light_all + dark_all < 200:
    fails.append(f"only {light_all + dark_all}px of ink of either colour on the "
                 f"strip — the bar is not drawing, so the count below proves "
                 f"nothing")

# …and the assertion. 20 is a stray-pixel allowance, not a band: the failure
# this catches drew three whole modules in the dark ink, which is thousands.
if dark_all > 20:
    fails.append(f"{dark_all}px of the strip are #1D1D1F, {dark_r} of them in "
                 f"the right half — modules inked for the grid cell under the "
                 f"bar rather than for the strip the bar is on")

for f in fails:
    print("FAIL: " + f)
sys.exit(1 if fails else 0)
PYEOF
echo "phase 2: one direction of ink across the whole bar"
echo "PASS"
