#!/bin/sh
# bar_opacity.sh — the Bar opacity row reaches the bar, and 0.00 really is NO
# background.
#
# `bar_opacity` is the user's answer on top of the theme's: the theme decides by
# default (macOS 26 asks for a clear bar, nothing else asks for anything), and
# this row overrides it on any theme. The interesting end is 0.00, which is not
# "very transparent" — it is a bar with no background at all, its ink taken off
# the wallpaper (backdrop.state; see contrast.h).
#
# THREE captures on one compositor and one bar:
#
#   A  the key absent      — the control, and the state the bar comes up in
#   B  bar_opacity = 1.00  — fully opaque
#   C  bar_opacity = 0.00  — clear
#
# WHAT IS ASSERTED. Nothing here needs to know what colour any theme's bar is.
# The wallpaper is a flat magenta no palette in the tree paints anything with, so
# every probe is one question of one pixel: is this the WALLPAPER, or is it not?
#
#   1. C is mostly wallpaper. Over half the strip's pixels match the wallpaper
#      exactly — that is the claim "no background" makes, and a merely
#      low-alpha bar cannot produce it: a tint of any strength shifts every
#      pixel it covers.
#   2. B is not. Essentially none of the strip matches, which is what an opaque
#      bar means, and is what says C's result came from the setting rather than
#      from the bar having failed to draw at all.
#   3. A differs from B. The control is the theme's 0.85/0.95, so a row that
#      only ever produced "clear or not" — a boolean wearing a number — would
#      make these two identical.
#   4. …and setting the key back to `auto` returns to A pixel for pixel. The
#      row has to be reversible on screen and not only in the config file, which
#      is the half ctlpanel_table_test cannot see.
#
# Usage: bar_opacity.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node or without quickshell/grim/PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: bar_opacity.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: bar_opacity.sh /path/to/synui /path/to/quickshell-tree}

# 77 is meson's SKIP code — same reason and the same fx_renderer as bar_shape.sh:
# this reads rendered pixels and scenefx is GLES2/DMA-BUF.
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
TMP=$(mktemp -d /tmp/baropacity.XXXXXX)
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

# A FLAT MAGENTA DESKTOP. Every probe below asks "is this pixel the wallpaper?",
# so the wallpaper has to be a colour nothing else on screen is: the bundled
# starfield is dark where the bar sits, and so is more than one theme's bar.
#
# The colour is not arbitrary either. It measures 0.161 relative luminance, which
# is BELOW the band where neither ink clears AA (roughly 0.184-0.238), so white
# ink is safe on it and the clear bar goes clear without a scrim. Inside that
# band the bar would lay a 34% wash instead — correct behaviour, and it would
# make capture C look like a tinted bar rather than no bar at all.
#
# Writing any synuirc also resets `autostart`, which config.c defaults to
# `kitty` — a mapped terminal would sit under the bar and put window chrome in
# the probes.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (200, 40, 160)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"
printf 'wallpaper = %s\n' "$TMP/wp.png" > "$CFG/synuirc"

opacity() {   # opacity <value|"">    "" removes the key entirely
    if [ -z "$1" ]; then : > "$CFG/settings.state"
    else printf 'bar_opacity = %s\n' "$1" > "$CFG/settings.state"
    fi
}

# The control, and the state the bar comes UP in — so capture A also proves the
# startup read, and the ones after it prove the FileView watch.
opacity ""

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

# The clear bar needs an ink, and the ink comes from the wallpaper the
# compositor measured. Waiting for the file rather than sleeping says which
# thing failed when it does: a missing backdrop.state is a compositor that never
# painted, and `none` here would make capture C legitimately opaque and this
# whole test a puzzle.
i=0
while [ $i -lt 60 ]; do
    [ -f "$CFG/backdrop.state" ] && break
    sleep 0.1; i=$((i + 1))
done
INK=$(sed -n 's/^bar_ink=\(.*\)$/\1/p' "$CFG/backdrop.state" 2>/dev/null)
[ "$INK" = "light" ] || fail "the test wallpaper measured '$INK', not 'light' —
    a clear bar over it would keep its background and capture C would be a
    false negative rather than a failure"

quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 4
kill -0 "$QS_PID" 2>/dev/null || fail "the bar died on startup"

shot() {   # shot <name>
    grim -t ppm -o "$OUTPUT" "$TMP/$1.ppm" 2>>"$QSLOG" || fail "grim failed for $1"
    echo "captured: $1"
}

shot a-auto

opacity 1.00
sleep 2
shot b-opaque

opacity 0.00
sleep 2
shot c-clear

# …and back. Written last so the comparison is against a capture taken before
# the key ever existed, which is what makes it a test of the whole round trip
# rather than of two ways of spelling the same number.
opacity auto
sleep 2
shot d-auto-again

python3 - "$TMP" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

tmp = sys.argv[1]

# Theme.qml: barHeight 28. The probe stops short of both edges of the strip —
# the top row is antialiased against the screen edge and the bottom carries the
# accent rule, and neither is the background this is measuring.
Y0, Y1 = 4, 24
# The compositor draws the "◢ SYNAPSE" launcher button over the top-left of the
# bar (launcher.c), which is not quickshell's background and never goes clear.
X0 = 260

WP = np.array([200, 40, 160], dtype=np.int16)

def load(name):
    return np.asarray(Image.open(f"{tmp}/{name}.ppm").convert("RGB"), dtype=np.int16)

A = load("a-auto")
B = load("b-opaque")
C = load("c-clear")
D = load("d-auto-again")
H, W = A.shape[:2]

def wallpaper_fraction(img):
    """How much of the strip is EXACTLY the wallpaper colour."""
    strip = img[Y0:Y1, X0:W]
    d = np.abs(strip - WP).sum(axis=2)
    return float((d <= 6).sum()) / strip[:, :, 0].size

def differs(p, q):
    d = np.abs(p[Y0:Y1, X0:W] - q[Y0:Y1, X0:W]).sum(axis=2)
    return int((d > 12).sum())

fa, fb, fc = wallpaper_fraction(A), wallpaper_fraction(B), wallpaper_fraction(C)
print(f"  strip that is wallpaper   auto {fa:.0%}   1.00 {fb:.0%}   0.00 {fc:.0%}")

fails = []

# ── 1. 0.00 is NO background ─────────────────────────────────
# Half is a deliberately loose floor: the clock, the tray and the desktop pills
# are drawn ON the clear bar and are not wallpaper, and how much they cover
# depends on the time of day and what is running.
if fc < 0.50:
    fails.append(f"bar_opacity = 0.00 left only {fc:.0%} of the strip showing "
                 f"the wallpaper — that is a tinted bar, not a clear one")

# ── 2. …and 1.00 is a background ─────────────────────────────
if fb > 0.02:
    fails.append(f"bar_opacity = 1.00 still showed the wallpaper through "
                 f"{fb:.0%} of the strip")

# ── 3. the row carries a NUMBER, not a switch ────────────────
d_ab = differs(A, B)
print(f"  auto vs 1.00   {d_ab}px differ")
if d_ab == 0:
    fails.append("bar_opacity = 1.00 was pixel-identical to the theme's own "
                 "default — the row is being read as clear-or-not rather than "
                 "as the alpha it is")

# ── 4. and it goes back ──────────────────────────────────────
d_ad = differs(A, D)
print(f"  auto vs auto-again   {d_ad}px differ")
if d_ad != 0:
    fails.append(f"`auto` did not restore the theme's bar ({d_ad}px differ from "
                 f"the capture taken before the key existed)")

if fails:
    for f in fails:
        print(f"FAIL: {f}")
    sys.exit(1)
print("PASS")
PYEOF
