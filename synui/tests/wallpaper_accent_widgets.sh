#!/bin/sh
# wallpaper_accent_widgets.sh — the wallpaper's colours reach the ICONS and the
# CLOCK, not only the underline.
#
# The accent taken off the wallpaper used to substitute into exactly one of
# quickshell's colours — `magenta`, the accent and the underline — and that left
# the feature visibly half-applied: every module GLYPH kept the preset's cyan
# and the clock kept its yellow, so a Prism desktop drew two colours off the
# picture and two off a theme, side by side, with nothing saying why.
#
# WHAT THIS PINS, and it is the whole mapping:
#
#   glyph / icons  ← the measured ACCENT     (Prism's own preset has glyph and
#                                             accent equal; this is that
#                                             structure in the picture's colours)
#   the clock      ← the measured SECONDARY  (palette.c measures a second hue
#                                             from a different part of the image
#                                             precisely so there is one)
#
# ⚠ AND IT IS PINNED BY THE TWO WIDGETS SIDE BY SIDE, because either one alone
# proves much less. `secondary` was published but read by NOTHING before this,
# so "the clock is the secondary colour" alone could pass on a shell that
# happened to draw one stray pixel of it. LAUNCH is the control: it is the same
# widget chrome, one screen away, and it has to come out the OTHER colour. Both
# were the theme's #00D6E5 while this was broken.
#
# Usage: wallpaper_accent_widgets.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node or without quickshell/grim/PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: wallpaper_accent_widgets.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: wallpaper_accent_widgets.sh /path/to/synui /path/to/quickshell-tree}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed."; exit 77; }

TMP=$(mktemp -d /tmp/wpwidgets.XXXXXX)
chmod 700 "$TMP"
LOG="$TMP/synui.log"
QSLOG="$TMP/shell.log"

cleanup() {
    [ -n "${QS_PID:-}" ]    && kill -9 "$QS_PID"    2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup INT TERM EXIT

fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG" 2>/dev/null
    echo "--- shell log (tail) ---"; tail -30 "$QSLOG" 2>/dev/null
    exit 1
}
ok() { printf '  ok    %s\n' "$1"; }

# SYNUI_SOCKET unset for the reason postit_ink.sh gives: WidgetState runs
# `synctl outputs` to find the primary screen, and synctl prefers that variable
# over WAYLAND_DISPLAY — a rig that leaves it set asks the LIVE desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# Flat and saturated, so the measurement is not what is under test.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (200, 40, 160)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"

{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\n'
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
} > "$CFG/synuirc"

# Prism, because `auto` is Prism — the theme this palette exists for.
printf 'theme=prism\ntransparency=on\nglass_chrome=on\nglass_surfaces=on\n' \
    > "$CFG/theme.state"

# ⚠ BY HAND: synui-apply-theme is what normally writes this and a nested synui
# deliberately does not run it (theme_apply_ex pushes only when it owns the
# seat). Without it Theme.qml falls back to its built-in SYNAPSE palette, whose
# glyph is a cyan close enough to nothing here to make the result a puzzle.
# These are Prism's own numbers.
cat > "$CFG/theme.json" <<'JSON'
{
  "scheme":     "dark",
  "accent":     [0, 214, 229],
  "glyph":      [0, 214, 229],
  "bar":        [25, 28, 35],
  "barAlpha":   0.00,
  "popup":      [25, 28, 35],
  "popupAlpha": 0.97,
  "fg":         "#e6eaf1",
  "clockFg":    "#ffd319"
}
JSON

# The two widgets, at opposite corners: LAUNCH top-left, CLOCK bottom-right.
printf 'clock = on\nlauncher = on\n' > "$CFG/widgets.state"

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

STATE="$CFG/palette.state"
i=0
while [ $i -lt 60 ]; do
    [ -f "$STATE" ] && break
    sleep 0.1; i=$((i + 1))
done
[ -f "$STATE" ] || fail "no palette.state after 6s"

USE=$(sed -n 's/^use=\(.*\)$/\1/p' "$STATE")
ACC=$(sed -n 's/^accent=\(.*\)$/\1/p' "$STATE")
SEC=$(sed -n 's/^secondary=\(.*\)$/\1/p' "$STATE")
[ "$USE" = yes ] || fail "the desktop is not using the wallpaper's colour (use=$USE)"
[ -n "$ACC" ] && [ -n "$SEC" ] || fail "palette.state published no accent/secondary"
[ "$ACC" != "$SEC" ] \
    || fail "the measured accent and secondary are the same colour ($ACC) —
    nothing below could tell the two widgets apart"
ok "the wallpaper measured accent=$ACC secondary=$SEC"

quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 5
kill -0 "$QS_PID" 2>/dev/null || fail "the shell died on startup"

grim -t ppm -o "$OUTPUT" "$TMP/shot.ppm" 2>>"$QSLOG" || fail "grim failed"

# Each widget's header carries a 3x9 solid tag and its label, both drawn in that
# widget's accent — solid fills of the exact colour, which is what makes an
# exact-match count the right probe rather than a mean.
python3 - "$TMP/shot.ppm" "$ACC" "$SEC" <<'ENDPY' || exit 1
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert('RGB')
W, H = im.size
px = im.load()

def rgb(h): return tuple(int(h[i:i+2], 16) for i in (1, 3, 5))
acc, sec = rgb(sys.argv[2]), rgb(sys.argv[3])

# ±3 per channel: the compositor's own colour management is identity here, and
# this is only slack for the card's alpha rounding under the fill.
def count(want, x0, x1):
    n = 0
    for y in range(H):
        for x in range(x0, x1):
            p = px[x, y]
            if all(abs(p[i] - want[i]) <= 3 for i in range(3)):
                n += 1
    return n

# Split at the middle so each widget is counted in its own half — LAUNCH is
# top-left, CLOCK is bottom-right, and a count over the whole screen could not
# say which widget a colour came from.
half = W // 2
left_acc,  left_sec  = count(acc, 0, half), count(sec, 0, half)
right_acc, right_sec = count(acc, half, W), count(sec, half, W)

print(f"  LAUNCH half: accent {left_acc}px  secondary {left_sec}px")
print(f"  CLOCK  half: accent {right_acc}px  secondary {right_sec}px")

fails = 0
def check(cond, msg):
    global fails
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond: fails += 1

# 20px is a floor, not a measurement: the tag alone is 27 solid pixels and the
# label adds more. Anything at all here means the colour is being drawn; zero
# is what the preset's cyan produced for the whole life of the bug.
check(left_acc >= 20,
      "the LAUNCH widget's chrome is the wallpaper's accent (the icon colour)")
check(right_sec >= 20,
      "…and the CLOCK's is the SECOND measured hue, not the same colour again")
check(right_acc < left_acc,
      "…so the clock is not simply wearing the accent too")

sys.exit(1 if fails else 0)
ENDPY

echo
echo "wallpaper_accent_widgets: PASS"
