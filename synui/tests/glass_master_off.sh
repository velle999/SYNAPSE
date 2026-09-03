#!/bin/sh
# glass_master_off.sh — `transparency = off` means NOTHING sees through.
#
# THE BUG THIS EXISTS FOR is a switch that did the opposite of its name. On a
# Prism desktop with the bar and dock dialled to 0.05 and PINNED there, turning
# Transparency off left every one of those surfaces at 0.05 and took the frost
# away — so the start menu, which had read as a dark frosted panel a moment
# earlier, became a hole with the wallpaper and the desktop clock legible
# straight through it. (velle, 2026-09-03, with a screenshot of exactly that.)
#
# Three rules met at that pixel and only two of them had been written down:
#   · synui's own panels ask syn_glass_resolve(), which refuses to make anything
#     translucent without transparency AND blur — so the control panel went
#     solid, correctly;
#   · foot is sent a flat 1.00 by glass_push() for the same reason;
#   · and the surfaces that ask for their OWN alpha — the bar, the dock, the
#     shell's popups and the widget cards — read bar_opacity/dock_opacity and
#     never consulted the master at all.
#
# ⚠ THE THEME'S OWN NUMBER IS NOT THE WAY BACK, which is the half a fix gets
# wrong: theme.json asks for barAlpha 0.05 on Prism, because on a glass preset
# the ask IS the glass. The no-glass answer is the Glass slider's bottom rung,
# 0.95 — syn_glass_bar_alpha_at(0), what Glass ▸ Off already resolves to.
#
# TWO RUNS OF ONE COMPOSITOR EACH, differing in ONE line of theme.state:
# transparency on, then off. Both are needed and neither is enough:
#
#   1. OFF draws a real surface. The menu's body is the theme's popup colour,
#      not the wallpaper.
#   2. ON still draws glass. Without this, "make everything opaque always"
#      passes check 1 perfectly and deletes the feature.
#
# ⚠ THE MEDIAN COLOUR of a strip inside the panel, never a pixel diff: the menu
# carries live rows and the clock in the bar above it moves. Not the MODAL one
# either — a frosted surface is dithered by blur_noise, so its most common
# single triple covers a third of a perfectly good panel. See
# reference_pixel_exact_capture_vs_live_widgets.
#
# Usage: glass_master_off.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node, quickshell, grim, or PIL/numpy.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: glass_master_off.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: glass_master_off.sh /path/to/synui /path/to/quickshell-tree}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL, numpy' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL/numpy not installed."; exit 77; }

TOP=$(mktemp -d /tmp/glassmaster.XXXXXX)
cleanup() { rm -rf "$TOP"; }
trap cleanup INT TERM EXIT

fail() { echo "FAIL: $1"; exit 1; }

# One run: transparency on or off, one capture out.
#
# ⚠ ITS OWN COMPOSITOR AND ITS OWN HOME. transparency is read at startup and
# exported into theme.state, which the bar reads once per change — a rig that
# flipped it under a running shell would be testing the reload path instead.
run() {   # run <on|off> <out.png>
    transp=$1; out=$2
    T="$TOP/run$transp"; mkdir -p "$T"
    export XDG_RUNTIME_DIR="$T" HOME="$T" XDG_CONFIG_HOME="$T/.config"
    export XDG_DATA_HOME="$T/.local/share" XDG_DATA_DIRS="$T/empty"
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_LIBINPUT_NO_DEVICES=1
    unset DISPLAY WAYLAND_DISPLAY SYNUI_SOCKET
    CFG="$XDG_CONFIG_HOME/synui"
    mkdir -p "$CFG" "$T/empty" "$XDG_DATA_HOME/applications"

    printf 'show_at_startup=0\n' > "$CFG/welcome.state"

    # A handful of categories, so the root page is a normal-looking menu. Their
    # names never appear in the probed strip; they are here so the panel has
    # rows at all.
    for c in Utility Network Office; do
        printf '[Desktop Entry]\nType=Application\nName=Probe %s\nExec=/bin/true\nCategories=%s;\n' \
               "$c" "$c" > "$XDG_DATA_HOME/applications/probe$c.desktop"
    done

    # ⚠ A FLAT MID-TEAL WALLPAPER, AND FLAT IS THE POINT. The legibility walk
    # (Theme.alphaWalkOn) raises a surface's alpha only until its ink clears AA,
    # and the theme's near-white ink clears it on this teal at 0.05 — so the
    # correction returns the asked-for alpha untouched and the ASK is what the
    # capture measures. On a photograph the walk would frost the menu itself and
    # the test would pass against the bug. It is also the desktop the bug was
    # reported from.
    python3 -c "
from PIL import Image
Image.new('RGB', (1280, 720), (0, 128, 128)).save('$T/wp.png')" \
        || fail "could not write the test wallpaper"
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$T/wp.png" > "$CFG/synuirc"
    printf 'animation_ms = 0\npower_enabled = 0\npower_dim_timeout = 86400\n' >> "$CFG/synuirc"
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n' >> "$CFG/synuirc"
    printf 'power_suspend_timeout = 86400\n' >> "$CFG/synuirc"

    # The reported desktop: both strips dialled clear BY HAND, which is what
    # pins them — and a pin is exactly what used to survive the master switch.
    printf 'bar_opacity = 0.05\ndock_opacity = 0.05\n' > "$CFG/settings.state"
    printf 'glass_pinned = bar_opacity dock_opacity\n' >> "$CFG/settings.state"

    # Prism, because the fix has to tell a GLASS preset's 0.05 (which is the
    # glass) from a retro preset's 0.85 (which is just a bar).
    printf 'theme=prism\ntransparency=%s\nactive_opacity=0.90\n' "$transp" \
        > "$CFG/theme.state"

    "$SYNUI" -d > "$T/synui.log" 2>&1 &
    SYNUI_PID=$!
    i=0
    SOCK=
    while [ $i -lt 100 ]; do
        for c in "$T"/wayland-*; do
            case "$c" in *.lock) continue;; esac
            [ -S "$c" ] && SOCK=$(basename "$c") && break
        done
        [ -n "$SOCK" ] && break
        kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
        i=$((i + 1)); sleep 0.1
    done
    [ -n "$SOCK" ] || fail "no wayland socket after 10s"
    export WAYLAND_DISPLAY="$SOCK"

    # The compositor's own export is the input the bar acts on, so a run whose
    # theme.state does not say what this run is about proves nothing.
    want="glass_master=$transp"
    grep -qx "$want" "$CFG/theme.state" \
        || fail "synui did not export $want (theme.state: $(tr '\n' ' ' < "$CFG/theme.state"))"

    quickshell -p "$TREE/shell.qml" > "$T/qs.log" 2>&1 &
    QS_PID=$!
    sleep 5
    kill -0 "$QS_PID" 2>/dev/null || fail "the bar died on startup ($(tail -3 "$T/qs.log"))"

    quickshell -p "$TREE/shell.qml" ipc call menu open HEADLESS-1 >>"$T/qs.log" 2>&1 \
        || fail "could not open the menu"
    sleep 2
    grim -o HEADLESS-1 "$out" 2>>"$T/qs.log" || fail "grim failed"
    kill -9 "$QS_PID" "$SYNUI_PID" 2>/dev/null
    wait "$QS_PID" 2>/dev/null || true
    wait "$SYNUI_PID" 2>/dev/null || true
    sleep 1
}

run on  "$TOP/on.png"
run off "$TOP/off.png"

python3 - "$TOP/on.png" "$TOP/off.png" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

WALLPAPER = np.array([0, 128, 128])

def surface(path):
    """The menu body's colour, and how uniform that body is.

    The strip is inside the panel and below the search box, in rows every page
    has: the background is what is being measured, not the labels on it.

    ⚠ THE MEDIAN, NOT THE MODAL COLOUR, and the first draft of this used the
    modal one and failed on the passing case. A FROSTED surface is not one
    colour: blur_noise dithers it by design (synuirc `blur_noise = 0.02`), so
    the most common single RGB triple covered 37% of a perfectly good glass
    panel while a solid one covered 95%. The median is the same answer for both
    and the deviation below says how uniform each is, which is the property the
    probe actually needs."""
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    body = img[120:200, 20:320].reshape(-1, 3)
    med = np.median(body, axis=0)
    mad = float(np.median(np.abs(body - med), axis=0).max())
    return med.astype(np.int16), mad

on, on_f = surface(sys.argv[1])
off, off_f = surface(sys.argv[2])
d_on  = int(np.abs(on  - WALLPAPER).max())
d_off = int(np.abs(off - WALLPAPER).max())
print(f"  menu surface   transparency on {tuple(int(v) for v in on)} "
      f"({d_on} from the wallpaper, deviation {on_f:.0f})")
print(f"  menu surface   transparency off {tuple(int(v) for v in off)} "
      f"({d_off} from the wallpaper, deviation {off_f:.0f})")

fails = []

# The probe has to be looking AT a panel body: a strip whose pixels are all over
# the place is text, or an edge, or nothing — and whatever it is, the colour
# below is not a surface. Frost dithers by a couple of levels; 8 is far above
# that and far below anything a row of glyphs measures.
for name, f in (("on", on_f), ("off", off_f)):
    if f > 8:
        fails.append(f"the transparency-{name} probe deviates by {f:.0f} — that is "
                     f"not a panel background, so the comparison means nothing")

# ── 1. OFF is a surface, not a hole ───────────────────────────
#
# 60 per channel is far outside anything compositing can produce here: the menu
# at 0.05 measured 8 from the wallpaper, at the theme's 0.97 it measures 113.
if d_off < 60:
    fails.append(f"transparency=off drew the menu {d_off} from the wallpaper — "
                 f"the master switch is not reaching the popup's alpha, which is "
                 f"the bug this test exists for")

# ── 2. …and ON is still glass ─────────────────────────────────
#
# The other half, and the one a lazy fix breaks: if the menu is opaque in BOTH
# runs then the switch does nothing again, in the other direction, and the glass
# desktop is gone.
if d_on > 40:
    fails.append(f"transparency=on drew the menu {d_on} from the wallpaper — "
                 f"the glass desktop is opaque, so the switch has stopped "
                 f"switching anything")

for f in fails:
    print("FAIL: " + f)
if fails:
    sys.exit(1)
print("ok 1 - transparency=off draws a real surface")
print("ok 2 - transparency=on still draws glass")
PYEOF

echo "PASS"
