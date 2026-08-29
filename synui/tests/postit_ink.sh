#!/bin/sh
# postit_ink.sh — the note is written in an ink that reads on what it is
# actually sitting on, and on a clear card that is the wallpaper.
#
# The post-it is the one thing on the desktop whose content the user typed, and
# on a glass desktop it is not on a card at all: `widget_glass` follows the
# DOCK's opacity (Theme.widgetAlpha), so a desktop with the dock at 0.00 puts a
# paragraph of 12px body text straight onto the picture. The theme's ink is
# chosen against the theme's own surface — macOS 26's is #1D1D1F, which is 12.6:1
# on Tahoe's pale desktop and 1.2:1 on a dark one — so the note went
# dark-on-dark and there was nothing on the control panel that moved it.
#
# So the note asks the same question the clear bar and the start menu ask
# (Theme.backdropFor → inkOn), and this is the check that the answer arrives on
# the screen rather than merely being computed.
#
# ⚠ THE WALLPAPER IS NEAR-BLACK AND THE THEME IS THE LIGHT ONE, which is the
# only arrangement where the two possible answers are far apart: the theme's own
# ink is #1D1D1F (luminance 0.012) and the backdrop's is #FFFFFF. Over a pale
# wallpaper both this and the bug look identical, and over a dark theme they are
# the same colour — which is why "it works under Prism" was true and told
# nobody anything.
#
# Usage: postit_ink.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node, quickshell, grim or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: postit_ink.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: postit_ink.sh /path/to/synui /path/to/quickshell-tree}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed."; exit 77; }

# SHORT: quickshell's ipc socket lives under XDG_RUNTIME_DIR and a unix path is
# capped at 108 bytes, which a build directory alone can blow.
TMP=$(mktemp -d /tmp/postitink.XXXXXX)
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
    echo "--- shell log (tail) ---"; tail -30 "$QSLOG" 2>/dev/null
    exit 1
}
ok() { printf '  ok    %s\n' "$1"; }

# Hermetic HOME and runtime dir, SYNUI_SOCKET unset: synctl prefers that
# variable over WAYLAND_DISPLAY, and WidgetState runs `synctl outputs` to find
# the primary — a rig that leaves it set asks the LIVE desktop and pins every
# widget to a screen this compositor does not have.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# Near-black, so the theme's dark ink cannot read on it and the flip has
# somewhere to go. 2,2,3 rather than pure black for the reason bar_scene_strip
# uses it: a picture, not a special case the code could be short-circuiting.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (2, 2, 3)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"

{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\n'
    # A nested compositor runs its own idle chain, and suspend is NOT
    # per-compositor: power.c hands it to logind, which is system-wide.
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
} > "$CFG/synuirc"

# The desktop this is about: the light glass theme, its card following a dock
# that is fully clear, and the legibility correction OFF — which is where the
# note has no surface at all and the ink is the only thing left to move.
{
    printf 'theme=macos26\ntransparency=on\nactive_opacity=0.94\n'
    printf 'square_chrome=off\nglass_chrome=on\nglass_surfaces=on\n'
    printf 'glass_legibility=off\n'
} > "$CFG/theme.state"

{
    printf 'dock_opacity = 0.00\n'
    printf 'widget_glass = on\n'
    printf 'glass_legibility = off\n'
} > "$CFG/settings.state"

# ⚠ WRITTEN BY HAND, because synui-apply-theme is what normally writes it and a
# nested instance deliberately does not run it (theme_apply_ex pushes only when
# synui_owns_seat — otherwise this rig would re-theme the machine it runs on).
# Without it Theme.qml falls back to the SYNAPSE palette, whose #c8e3ee ink
# reads on a black wallpaper perfectly well and the test would pass on a bug.
# These are [SYN_THEME_MACOS26]'s own numbers from theme.c.
cat > "$CFG/theme.json" <<'JSON'
{
  "scheme":     "light",
  "accent":     [0, 122, 255],
  "glyph":      [0, 86, 214],
  "bar":        [245, 245, 247],
  "barAlpha":   0,
  "popup":      [242, 242, 247],
  "popupAlpha": 0.99,
  "fg":         "#1d1d1f",
  "clockFg":    "#8a6d00"
}
JSON

printf 'postit = on\n' > "$CFG/widgets.state"
# Enough lines to fill the note, so the probe is looking at a body of text
# rather than at one word in the corner of it.
printf 'MMMM MMMM MMMM\nMMMM MMMM MMMM\nMMMM MMMM MMMM\nMMMM MMMM MMMM\n' \
    > "$CFG/postit.txt"

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

# The ink comes from a measurement, so wait for the measurement rather than for
# a guessed number of seconds — and assert what it says, because `dark` here
# would make the whole capture a puzzle rather than a failure.
i=0
while [ $i -lt 60 ]; do
    [ -f "$CFG/backdrop.state" ] && break
    sleep 0.1; i=$((i + 1))
done
INK=$(sed -n 's/^bar_ink=\(.*\)$/\1/p' "$CFG/backdrop.state" 2>/dev/null)
[ "$INK" = "light" ] || fail "the test wallpaper measured '$INK', not 'light' —
    nothing below can distinguish a note that ignored the backdrop from one
    that read it and was told to stay dark"
ok "the wallpaper under the note answers 'light'"

quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 5

grim -t ppm -o "$OUTPUT" "$TMP/shot.ppm" 2>>"$QSLOG" || fail "grim failed"

# The note is the bottom-left widget: PostItState.homeX/homeY put the first one
# 20px in from the left and 20px up from the bottom of the usable area, and
# WidgetFrame adds its own chrome to the 264x168 body it declares.
#
# Two probes, and the first is why the second can be trusted:
#   HEAD  — the card's top strip, where the accent tag and the "NOTE" label are.
#           Theme.yellow, which is not the backdrop's business on any theme, so
#           it says the widget is on screen at all. Without it a widget that
#           never mapped would read as a note whose ink stayed dark. The bar is
#           low because this theme's yellow IS low: a light scheme darkens it to
#           #8a6d00 (0.163) so that it reads on a pale bar, and the point of the
#           probe is "brighter than a 2,2,3 wallpaper", not "bright".
#   BODY  — the writing. Nothing in it is bright unless the ink is: the ruled
#           lines are the accent at 10%, which over this wallpaper is under 0.06.
python3 - "$TMP/shot.ppm" <<'ENDPY' || exit 1
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert('RGB')
W, H = im.size
px = im.load()

CARD_W, CARD_H = 264, 211          # 168 body + 32 header + 11 footer chrome
X0, Y1 = 20, H - 20                # 20 in from the left, 20 up from the bottom
Y0 = Y1 - CARD_H

def lin(v):
    v /= 255.0
    return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

def lum(c):
    return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2])

def peak(x0, y0, x1, y1):
    best, at = -1.0, None
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            l = lum(px[x, y])
            if l > best:
                best, at = l, (x, y, px[x, y])
    return best, at

head, head_at = peak(X0, Y0, X0 + CARD_W, Y0 + 30)
# The writing: the card inset by its chrome, and short of the footer hint row.
body, body_at = peak(X0 + 11, Y0 + 32, X0 + CARD_W - 11, Y1 - 25)

print("  head peak %.3f at %s   body peak %.3f at %s" % (head, head_at, body, body_at))

fails = 0
if head > 0.05:
    print("  ok    the note is on screen (its accent header reads %.2f)" % head)
else:
    print("  FAIL  no note on screen — the header peaks at %.3f, so the probe "
          "below is measuring an empty desktop" % head)
    fails += 1

# White ink lands near 1.0; #1d1d1f over this wallpaper lands near 0.012, and
# the brightest thing in the body that is NOT ink is a 10% accent rule.
if body > 0.5:
    print("  ok    the writing is inked for the WALLPAPER, not for the theme "
          "(%.2f)" % body)
else:
    print("  FAIL  the writing peaks at %.3f — that is the theme's #1d1d1f on a "
          "near-black desktop, which is the note being unreadable" % body)
    fails += 1

sys.exit(1 if fails else 0)
ENDPY

# ── the card is the same on every note, wherever it sits ────────────────────
#
# ⛔ STRUCTURAL, BECAUSE THE PIXEL VERSION NEEDS TWO NOTES OVER DIFFERENT
# WALLPAPER. alphaWalkOn() raises a surface's alpha until its text clears AA on
# what is behind it; asked per WIDGET that is a different answer per position,
# and three notes on one desktop drew three different cards — two walked up into
# frosted panels over a lit skyline, one left at `widgetAlpha` over flat dark
# sky, which on a clear dock is no card at all. Identical widgets, one of them
# apparently missing.
#
# ⚠ THE INK MUST STILL FOLLOW THE SPOT — that is what everything above this line
# tests, and it is the whole reason the note asks for a backdrop. The two halves
# answer different questions: how present the CARD is (furniture, and it has to
# match across a desktop) and what colour the WRITING is (which has to match the
# few hundred pixels behind those words).
FRAME="$(dirname "$0")/../quickshell/widgets/WidgetFrame.qml"
if [ -f "$FRAME" ]; then
    grep -q 'surfaceAlpha: Theme.widgetAlphaOn(win.screenBackdrop)' "$FRAME" \
        || fail "the card's alpha is not taken from the SCREEN's backdrop.
       Per-widget, alphaWalkOn gives a different answer per position and two
       identical notes draw different cards — one of them with no card at all."
    grep -q 'Theme.inkOn(win.backdrop' "$FRAME" \
        || fail "the ink is no longer taken from the widget's OWN backdrop —
       which is the feature everything above this line is testing."
    ok "the card follows the screen, the ink follows the spot"
fi

echo
echo "postit_ink: PASS"
