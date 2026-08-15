#!/bin/sh
# bar_backdrop.sh — a bar with no background of its own takes its ink from the
# wallpaper, and there are wallpapers that have no ink to give.
#
# macOS 26's menu bar is CLEAR: the clock and the menus are drawn straight onto
# the wallpaper. That makes the wallpaper the surface, and the wallpaper is not
# something a theme can know — the theme's own #1D1D1F ink measures 12.6:1 on
# Tahoe's pale desktop and 1.2:1 on the near-black one this box runs. So the
# compositor measures the strip the bar covers and writes which ink survives it
# to backdrop.state, which the bar watches.
#
# The arithmetic is pinned by bar_ink_test.c and needs no compositor. What THIS
# asserts is the half that unit test cannot reach — that the number handed to
# that arithmetic is the right number:
#
#   * a near-black wallpaper reports `light`, and a near-white one `dark`
#   * a mid-grey reports `none`. Neither black nor white clears AA on it, and a
#     clear bar has no background to tint its way out — so the bar keeps its
#     background rather than going clear and unreadable. This is the case a
#     screenshot of a working desktop never shows
#   * the answer FOLLOWS a live wallpaper change, because Super+W is how it
#     actually changes and the file is written from the painter
#   * only the STRIP is measured. A wallpaper that is pale across one eighth and
#     near-black everywhere else averages dark as a PICTURE; what the bar reads
#     is the end it is drawn on. Both bands here would answer `light` if the
#     whole wallpaper were being measured, which is the obvious implementation
#   * and it is the BAR's strip, not the screen's top edge: moving the bar to the
#     bottom moves which end is read. That failure shows up on exactly one theme
#     and in no screenshot
#
# No bar and no pixels are needed: backdrop.state is the interface between the
# compositor and the bar, so the file is what is asserted.
#
# Usage: bar_backdrop.sh /path/to/synui
# Skips (77) without a DRM render node, PIL, or synctl.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: bar_backdrop.sh /path/to/synui}

# 77 is meson's SKIP code: a runner that cannot render reports a skip rather
# than a pass. synui renders through scenefx's fx_renderer, which is GLES2 and
# DMA-BUF only.
if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (it writes the test wallpapers)."; exit 77; }
command -v synctl >/dev/null 2>&1 \
    || { echo "SKIP: synctl not installed (the live wallpaper changes go through it)."; exit 77; }

TMP=$(mktemp -d /tmp/backdrop.XXXXXX)
LOG="$TMP/synui.log"

cleanup() { [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null; }
trap cleanup INT TERM EXIT

fails=0
fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG" 2>/dev/null
    exit 1
}

# ⚠ Hermetic HOME, and SYNUI_SOCKET unset. This writes backdrop.state and
# dispatches wallpaper changes; synctl prefers SYNUI_SOCKET over
# WAYLAND_DISPLAY, so a rig that leaves it set repaints the LIVE desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless
unset DISPLAY WAYLAND_DISPLAY
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"
STATE="$CFG/backdrop.state"

# A hermetic HOME is a first run, so the welcome panel would come up. It draws
# nothing this reads, but it costs a frame and a log line.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

python3 - "$TMP" <<'ENDPY' || fail "could not write the test wallpapers"
import sys
from PIL import Image
d = sys.argv[1]
Image.new('RGB', (64, 64), (2, 2, 3)).save(d + '/black.png')
Image.new('RGB', (64, 64), (245, 245, 247)).save(d + '/white.png')
# ~0.216 relative luminance: above white ink's ceiling and below black ink's
# floor, which is the band where a clear bar has no legible answer at all.
Image.new('RGB', (64, 64), (128, 128, 128)).save(d + '/grey.png')

# Pale across one eighth, near-black for the rest, and the mirror of it. As
# pictures both average ~0.11, which is dark; where they differ is which END.
# The strip is ~3% of the screen, so an eighth covers it whole either way.
def band(path, top):
    im = Image.new('RGB', (800, 400), (2, 2, 3))
    ys = range(0, 50) if top else range(350, 400)
    for y in ys:
        for x in range(800):
            im.putpixel((x, y), (245, 245, 247))
    im.save(path)
band(d + '/topband.png', True)
band(d + '/botband.png', False)
ENDPY

# `stretch`, not `fill`: fill CROPS, and which axis it crops depends on the
# headless output's aspect against the image's. The band cases below are about
# which STRIP is measured, and pinning them to a crop this test does not control
# would let them fail for a reason that is not the one under test.
wp_config() {  # wp_config <path> [extra synuirc lines...]
    path=$1; shift
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$path" \
        > "$CFG/synuirc"
    [ $# -gt 0 ] && printf '%s\n' "$@" >> "$CFG/synuirc"
    return 0
}
wp_config "$TMP/black.png"

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
echo "compositor: WAYLAND_DISPLAY=$SOCK"

ink() { sed -n 's/^bar_ink=\(.*\)$/\1/p' "$STATE" 2>/dev/null; }

# The file is written from the painter, which runs on its own schedule; wait for
# the VALUE rather than sleeping a guessed number of seconds.
await() {  # await <expected> <description>
    i=0
    while [ $i -lt 60 ]; do
        [ "$(ink)" = "$1" ] && break
        sleep 0.1; i=$((i + 1))
    done
    if [ "$(ink)" = "$1" ]; then
        printf '  ok    %s\n' "$2"
    else
        printf '  FAIL  %s — expected %s, got "%s"\n' "$2" "$1" "$(ink)"
        fails=$((fails + 1))
    fi
}

set_wp() { synctl dispatch wallpaper "$1" >/dev/null 2>&1; }

await light "a near-black wallpaper takes light ink"

# ── it follows a live change ─────────────────────────────────
set_wp "$TMP/white.png"
await dark  "…and a near-white one takes dark ink, live"

# ── the band with no answer ──────────────────────────────────
set_wp "$TMP/grey.png"
await none  "a mid-grey has no legible ink at all"

# ── and back, so `none` is not a one-way trap ────────────────
set_wp "$TMP/black.png"
await light "…and it recovers when the wallpaper does"

# ── the STRIP, not the picture ───────────────────────────────
set_wp "$TMP/topband.png"
await dark  "a pale band where a top bar sits is what a top bar reads"

set_wp "$TMP/botband.png"
await light "…and a pale band at the other end is not"

# ── and it is the BAR's strip ────────────────────────────────
# Same wallpaper, bar moved. Nothing about the picture changed; the answer has
# to, because the bar is now drawn on the pale end.
wp_config "$TMP/botband.png" 'bar_edge = bottom'
synctl dispatch wallpaper_reload >/dev/null 2>&1
await dark  "…until the bar moves to that end"

if [ "$fails" -eq 0 ]; then
    printf 'bar_backdrop: all checks passed\n'
    exit 0
fi
printf 'bar_backdrop: %d check(s) failed\n' "$fails"
exit 1
