#!/bin/sh
# bar_radius.sh — the BAR's panels follow the desktop's corner_radius, and a
# retro chrome squares them again.
#
# `corner_radius` rounded every window on the desktop and nothing the bar drew
# itself, so turning corners on rounded thirty applications and left the start
# menu, the right-click menu, the mixer, the tooltips and the OSD square. They
# now take Theme.panelRadius, which is BarConfig's reading of corner_radius with
# theme.state's `square_chrome` over the top.
#
# Three captures of the SAME open start menu, on one compositor and one bar:
#
#   A  corner_radius=0                    — the control: square corners
#   B  corner_radius=14                   — rounded, and written while the bar is
#                                           already up, so this also proves the
#                                           FileView watch rather than just the
#                                           startup read
#   C  corner_radius=14 square_chrome=on  — square AGAIN, with the radius
#                                           untouched: the Win95 rule
#
# WHAT IS ASSERTED, AND WHY IT IS THE CORNERS AND NOT THE FRAME. A whole-frame
# diff is not a usable signal here: the bar's clock changes between captures and
# would show up in every pair. So the panel is located by its own border (the
# only saturated colour on the screen — the accent, whatever the theme made it),
# and the diffs are counted inside 16px boxes at its four corners, with a
# separate assertion that nothing OUTSIDE those boxes below the bar moved at all.
# A radius that leaked into the panel's body, or a "fix" that repainted the whole
# menu, both fail.
#
# The start menu is the one surface a test can open: the bar exposes
# `ipc call menu open <output>` and nothing can synthesise a pointer, so the
# mixer and the tooltips (identical one-line bindings) are not reachable here.
#
# Usage: bar_radius.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node or without quickshell/grim/PIL.

set -u

SYNUI=${1:?usage: bar_radius.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: bar_radius.sh /path/to/synui /path/to/quickshell-tree}

# 77 is meson's SKIP code, so a runner that cannot render reports a skip rather
# than a pass — the same reason smoke.sh gives, and the same fx_renderer.
if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    echo "      synui renders through scenefx's fx_renderer, which is GLES2 and"
    echo "      DMA-BUF only, and this test reads the rendered pixels."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (the pixel assertions need it)."; exit 77; }

# SHORT, deliberately: quickshell's ipc socket lives under XDG_RUNTIME_DIR and a
# unix path is capped at 108 bytes, which a build directory alone can blow.
TMP=$(mktemp -d /tmp/barrad.XXXXXX)
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

# Hermetic HOME and runtime dir: this writes theme.state and uifx.state, and
# SYNUI_SOCKET is set in some shells and points at the LIVE desktop — synctl and
# friends prefer it over WAYLAND_DISPLAY, so a rig that leaves it set reconfigures
# the machine it is running on.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless
unset DISPLAY WAYLAND_DISPLAY
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

state() {   # state <corner_radius> <square_chrome>
    printf 'corner_radius=%s\n' "$1" > "$CFG/uifx.state"
    printf 'theme=dark\nsquare_chrome=%s\n' "$2" > "$CFG/theme.state"
}

# A hermetic HOME is a FIRST RUN, so the welcome panel comes up over the desktop
# — and since panel_chrome_sync() it rounds with everything else, which would put
# a second set of moving corners in every diff below. This test is about the BAR;
# the compositor's own panels are covered where they belong.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# A FLAT GREY DESKTOP, AND NO AUTOSTART — both because of how the panel is
# located below: as the only saturated colour on screen. Two things break that,
# and both did.
#
#   - The bundled wallpaper. It is a bright purple emblem on a starfield, and it
#     is far bigger than the menu, so the "panel" comes back as the emblem's
#     bounding box and three of the four corners report a failure the bar never
#     had. Grey is chosen over black for the OTHER test on this rig: it has to
#     differ from the bar's own dark background, or "bar or desktop?" cannot be
#     asked of a pixel. Unsaturated so this test's locator still works, bright so
#     that one does.
#   - `autostart`, which config.c defaults to `kitty`. A terminal's border is
#     saturated too. ANY synuirc resets that list so the file's entries replace
#     the defaults (config.c, "Config file found"), and this one names none.
#
# The autostart half was a RACE — the same code passed or failed on whether kitty
# won the four seconds before the first capture — which is the worst way for a
# test to be wrong.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (128, 128, 128)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"
printf 'wallpaper = %s\n' "$TMP/wp.png" > "$CFG/synuirc"

state 0 off

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
    quickshell -p "$TREE/shell.qml" ipc call menu open "$OUTPUT" >>"$QSLOG" 2>&1 \
        || fail "ipc call menu open failed — is the bar up?"
    sleep 1.5
    grim -t ppm -o "$OUTPUT" "$TMP/$1.ppm" 2>>"$QSLOG" || fail "grim failed for $1"
    quickshell -p "$TREE/shell.qml" ipc call menu close >>"$QSLOG" 2>&1
    sleep 0.5
    echo "captured: $1"
}

shot a-radius0

# Written with everything already running: a radius change has to land on a bar
# that is up, which is the FileView watch and not the startup read.
state 14 off
sleep 1.5
shot b-radius14

state 14 on
sleep 1.5
shot c-square

python3 - "$TMP" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

tmp = sys.argv[1]
BAR = 28          # Theme.barHeight: the clock lives up here and ticks between
                  # captures, so nothing above this line is evidence
BOX = 16          # corner box, comfortably over the 14 under test

def load(n):
    return np.array(Image.open(f"{tmp}/{n}.ppm").convert("RGB")).astype(int)

A, B, C = load("a-radius0"), load("b-radius14"), load("c-square")
h, w, _ = A.shape

# The panel's own border is the only saturated thing on a headless desktop (no
# wallpaper, near-black everywhere else). Its colour is the theme's accent, so it
# is found by SATURATION rather than by a hardcoded hex.
sat = A.max(2) - A.min(2)
ys, xs = np.nonzero((sat > 60) & (np.arange(h)[:, None] >= BAR))
if len(xs) < 100:
    print(f"FAIL: no panel border found below the bar ({len(xs)} saturated px) —"
          " did the start menu open at all?")
    sys.exit(1)
x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
print(f"panel:    x {x0}-{x1}  y {y0}-{y1}")
if x1 - x0 < 100 or y1 - y0 < 100:
    print("FAIL: the located panel is too small to be the start menu")
    sys.exit(1)

corners = {
    "top-left":     (x0,           y0),
    "top-right":    (x1 - BOX + 1, y0),
    "bottom-left":  (x0,           y1 - BOX + 1),
    "bottom-right": (x1 - BOX + 1, y1 - BOX + 1),
}

def changed(p, q, cx, cy):
    d = np.abs(p[cy:cy + BOX, cx:cx + BOX] - q[cy:cy + BOX, cx:cx + BOX])
    return int((d.sum(2) > 0).sum())

def outside(p, q):
    """Changed pixels below the bar that are NOT in one of the four corner
    boxes. A radius must not repaint the panel's body."""
    d = (np.abs(p - q).sum(2) > 0)
    d[:BAR, :] = False
    for cx, cy in corners.values():
        d[cy:cy + BOX, cx:cx + BOX] = False
    return int(d.sum())

ok = True
for name, (cx, cy) in corners.items():
    ab, bc, ac = (changed(A, B, cx, cy), changed(B, C, cx, cy),
                  changed(A, C, cx, cy))
    verdict = "ok"
    if ab < 30:
        verdict, ok = "FAIL (radius 14 did not carve this corner)", False
    elif bc < 30:
        verdict, ok = "FAIL (square_chrome did not square it again)", False
    elif ac != 0:
        verdict, ok = f"FAIL (r=0 and square_chrome differ by {ac} px)", False
    print(f"  {name:13s} 0->14: {ab:3d}px   14->square: {bc:3d}px   "
          f"0 vs square: {ac:3d}px   {verdict}")

for tag, n in (("0 -> 14", outside(A, B)), ("14 -> square", outside(B, C))):
    if n:
        print(f"FAIL: {tag} changed {n} px outside the corner boxes — a corner"
              " radius must not repaint the panel's body")
        ok = False
    else:
        print(f"  body {tag:12s} unchanged outside the corners")

sys.exit(0 if ok else 1)
PYEOF

echo "PASS"
exit 0
