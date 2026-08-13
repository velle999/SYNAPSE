#!/bin/sh
# bar_shape.sh — `bar_shape` changes the bar's SHAPE, and does nothing at all
# with the corners off.
#
# The bar took the desktop's corner radius on its popups (bar_radius.sh) while
# the strip itself stayed an edge-to-edge rectangle. `bar_shape` is what it does
# with that radius:
#
#   full-width     the square strip, unchanged                      (the control)
#   rounded-ends   still edge to edge; the two corners facing the desktop curve
#   floating-pill  lifted off the edge and in from both sides, a capsule
#
# FOUR captures of the bar on one compositor and one bar:
#
#   A  full-width    radius 14
#   B  rounded-ends  radius 14
#   C  floating-pill radius 14
#   D  floating-pill radius 0   — the gating contract: no radius, no shape
#
# WHAT IS ASSERTED, AND WHY IT IS THESE PROBES. Nothing here needs to know what
# colour a bar is: every assertion compares one capture to another at the same
# pixels, so a theme change cannot rewrite the test. The clock is centred and
# vertically centred and ticks between captures, so no probe goes near the middle
# of the strip — the probes are the top row, the left column, and the two bottom
# corners, all of which are bar-or-not-bar and nothing else.
#
#   1. B vs A — the ends round: the bottom corners change and the top row does
#      NOT. A bar that curved all four would cut notches out of the screen's own
#      corners, and this is what says it does not.
#   2. C vs A — the pill floats: the top row changes across essentially the whole
#      width (the gap above it) and the left column at mid-strip changes too (the
#      gap beside it). Rounding alone cannot move either.
#   3. D vs A — pixel-identical on every probe. This is the contract that makes
#      the row honest: with `corner_radius = 0` the shape is a no-op, so a
#      desktop that never turns the corners on cannot be given a floating bar by
#      a settings file it has never read.
#
# Usage: bar_shape.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node or without quickshell/grim/PIL.

set -u

SYNUI=${1:?usage: bar_shape.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: bar_shape.sh /path/to/synui /path/to/quickshell-tree}

# 77 is meson's SKIP code — the same reason and the same fx_renderer as smoke.sh
# and bar_radius.sh: this reads rendered pixels and scenefx is GLES2/DMA-BUF.
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
TMP=$(mktemp -d /tmp/barshape.XXXXXX)
LOG="$TMP/synui.log"
QSLOG="$TMP/qs.log"

cleanup() {
    [ -n "${QS_PID:-}" ]    && kill -9 "$QS_PID"    2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
}
trap cleanup INT TERM EXIT

fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG"   2>/dev/null
    echo "--- bar log (tail) ---";   tail -30 "$QSLOG" 2>/dev/null
    exit 1
}

# Hermetic HOME and runtime dir: this writes settings.state and uifx.state, and
# SYNUI_SOCKET is set in some shells and points at the LIVE desktop — synctl and
# friends prefer it over WAYLAND_DISPLAY, so a rig that leaves it set
# reconfigures the machine it is running on.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless
unset DISPLAY WAYLAND_DISPLAY
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

# No welcome panel — a hermetic HOME is a first run.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# A FLAT GREY DESKTOP, AND NO AUTOSTART. Every probe below asks one question of
# one pixel — bar, or desktop? — so the two have to be TELLABLE APART, and the
# bundled wallpaper is a dark starfield at the top of the screen where the bar
# lives. Against a dark bar on a dark sky the top-row probe measures nothing.
# Grey is unsaturated (which is what bar_radius.sh's locator needs of this same
# rig) and bright (which is what these probes need).
#
# Writing any synuirc also resets `autostart`, which config.c defaults to
# `kitty` — a mapped terminal would sit under the bar and put a window border in
# the left-column probe. See bar_radius.sh, where that race actually bit.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (128, 128, 128)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"
printf 'wallpaper = %s\n' "$TMP/wp.png" > "$CFG/synuirc"

shape() {   # shape <bar_shape> <corner_radius>
    printf 'bar_shape = %s\n' "$1" > "$CFG/settings.state"
    printf 'corner_radius=%s\n' "$2" > "$CFG/uifx.state"
}

# The control, and the state the bar comes UP in — so the first capture also
# proves the startup read, and the three after it prove the FileView watch.
shape full-width 14

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
sleep 4
kill -0 "$QS_PID" 2>/dev/null || fail "the bar died on startup"

shot() {   # shot <name>
    grim -t ppm -o "$OUTPUT" "$TMP/$1.ppm" 2>>"$QSLOG" || fail "grim failed for $1"
    echo "captured: $1"
}

shot a-full

# Each written with everything already up, so these exercise the watch rather
# than the startup read. Generous settle: the shape moves the exclusive zone,
# which makes the compositor reflow, and the reveal animation rides on top.
shape rounded-ends 14
sleep 2
shot b-ends

shape floating-pill 14
sleep 2
shot c-pill

shape floating-pill 0
sleep 2
shot d-pill-noradius

python3 - "$TMP" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

tmp = sys.argv[1]

# Theme.qml: barHeight 28, and the pill's barGap 6 — so a pilled bar's window is
# 34 tall with the strip in the bottom 28 of it.
BAR, GAP = 28, 6
BOX = 16          # corner box, comfortably over the 14 under test

def load(name):
    return np.asarray(Image.open(f"{tmp}/{name}.ppm").convert("RGB"), dtype=np.int16)

A = load("a-full")
B = load("b-ends")
C = load("c-pill")
D = load("d-pill-noradius")
H, W = A.shape[:2]

def differs(p, q, box):
    """Pixels differing by more than sensor noise inside (y0, y1, x0, x1)."""
    y0, y1, x0, x1 = box
    d = np.abs(p[y0:y1, x0:x1] - q[y0:y1, x0:x1]).sum(axis=2)
    return int((d > 12).sum())

# ── The probes ───────────────────────────────────────────────
# Top row: the line the bar touches the screen edge along. Bar for full-width
# and rounded-ends; desktop for a floating pill.
TOP = (0, 1, 0, W)
# Left column at mid-strip, clear of the corner curves either way.
MIDY = GAP + BAR // 2
LEFT = (MIDY, MIDY + 1, 0, 1)
# The two corners that face the desktop on a top bar. On a pilled bar the strip
# ends BAR+GAP down; on the other two it ends at BAR. Both boxes are taken from
# the deepest of those so one window covers every shape.
BL = (BAR + GAP - BOX, BAR + GAP, 0, BOX)
BR = (BAR + GAP - BOX, BAR + GAP, W - BOX, W)

fails = []

# ── 1. rounded-ends curves the corners and NOTHING else ──────
bl, br = differs(A, B, BL), differs(A, B, BR)
top    = differs(A, B, TOP)
print(f"  ends vs full   bottom-left {bl:4d}px  bottom-right {br:4d}px  "
      f"top row {top:4d}px")
if bl == 0 or br == 0:
    fails.append("rounded-ends did not carve the bottom corners")
if top != 0:
    fails.append(f"rounded-ends moved the top row ({top}px) — it should still "
                 f"touch the screen edge; rounding all four corners cuts "
                 f"notches out of the screen")

# ── 2. floating-pill lifts off the edge AND in from the sides ─
top  = differs(A, C, TOP)
left = differs(A, C, LEFT)
print(f"  pill vs full   top row {top:4d}px of {W}  left column {left}px")
# Not the whole width: the launcher's own colours can coincide with the desktop
# behind it at a few columns, and that is not what is under test.
if top < 0.9 * W:
    fails.append(f"floating-pill left the top row unchanged at {W - top}px of "
                 f"{W} — it should have vacated the screen edge")
if left == 0:
    fails.append("floating-pill did not vacate the left edge")

# ── 3. the gating contract: no radius, no shape ──────────────
probes = {"top row": TOP, "left column": LEFT,
          "bottom-left": BL, "bottom-right": BR}
worst = {name: differs(A, D, box) for name, box in probes.items()}
print("  pill@radius 0 vs full  " +
      "  ".join(f"{n} {v}px" for n, v in worst.items()))
for name, v in worst.items():
    if v != 0:
        fails.append(f"floating-pill applied itself at corner_radius = 0 "
                     f"({name} moved {v}px) — the shape is supposed to be a "
                     f"no-op with the corners off")

if fails:
    for f in fails:
        print(f"FAIL: {f}")
    sys.exit(1)
print("PASS")
PYEOF
