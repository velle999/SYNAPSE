#!/bin/sh
# menu_surface_pages.sh — the start menu is made of the same stuff on every page.
#
# THE BUG THIS EXISTS FOR is one menu with two looks. `Accessories` opened
# clear — the wallpaper straight through it, the desktop clock's hands legible
# behind the rows — and `Internet`, four rows longer, opened as frosted glass.
# Same menu, same corner of the same screen, seconds apart.
#
# Nothing was wrong with either look. The panel corrects its own alpha against
# what is behind it (Theme.alphaWalkOn, the QML twin of panel_alpha_floor), and
# it asked that question about THE BOX IT HAPPENED TO OCCUPY. The menu is one
# surface that changes size as you walk it — five rows here, the whole screen
# there — and the luminance grid it asks is a ninth of the screen deep, so the
# answer moves in STEPS as a page grows. Measured on the desktop this was
# reported from: `Accessories` ends at y=277 and stays inside one grid row;
# `Internet` ends at 325, crosses into the next one, and is corrected.
#
# ⚠ A PAGE IS NOT A MATERIAL. Which submenu is open cannot be what decides
# whether this thing is glass, and the fix is to ask once about the strip the
# panel lives in rather than per page.
#
# TWO RUNS OF ONE COMPOSITOR EACH, differing in ONE thing: how many CATEGORIES
# the installed .desktop files fall into. The root page lists a row per
# category, so one category is a short page and eleven is a long one.
#
# ⚠ NOT "how many apps". Two hundred entries in Utility is ONE row on this page
# — the apps are behind it — and a first draft of this file varied the count,
# got two identical menus, and passed against the bug it was written for.
#
#   1. The two runs paint the panel the SAME colour. That is the whole claim.
#   2. …and that colour is not the wallpaper. Without it, "both clear" — the
#      failure with the correction never firing at all — would pass check 1
#      perfectly.
#
# ⚠ THE MODAL COLOUR, not a pixel diff: the panel carries live rows, and the
# two runs have different lists in them. The background is most of the probe
# and is the thing being measured; see pixel_exact_capture_vs_live_widgets.
#
# Usage: menu_surface_pages.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node, quickshell, grim, or PIL/numpy.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: menu_surface_pages.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: menu_surface_pages.sh /path/to/synui /path/to/quickshell-tree}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL, numpy' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL/numpy not installed."; exit 77; }

TOP=$(mktemp -d /tmp/menupages.XXXXXX)
cleanup() { rm -rf "$TOP"; }
trap cleanup INT TERM EXIT

fail() { echo "FAIL: $1"; exit 1; }

# The freedesktop categories StartMenu.qml's catTable files under its own
# headings — one app in each is one row each on the root page.
CATS="Game Development Graphics AudioVideo Office Science Education Network Settings System Utility"

# One run: N categories on the menu, one capture out.
#
# ⚠ EACH RUN GETS ITS OWN COMPOSITOR AND ITS OWN HOME. DesktopEntries is read
# once at startup, so changing the app set under a running shell proves nothing.
run() {   # run <n-categories> <out.ppm>
    n=$1; out=$2
    T="$TOP/run$n"; mkdir -p "$T"
    export XDG_RUNTIME_DIR="$T" HOME="$T" XDG_CONFIG_HOME="$T/.config"
    export XDG_DATA_HOME="$T/.local/share" XDG_DATA_DIRS="$T/empty"
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_LIBINPUT_NO_DEVICES=1
    unset DISPLAY WAYLAND_DISPLAY SYNUI_SOCKET
    CFG="$XDG_CONFIG_HOME/synui"
    mkdir -p "$CFG" "$T/empty" "$XDG_DATA_HOME/applications"

    printf 'show_at_startup=0\n' > "$CFG/welcome.state"

    # ⚠ XDG_DATA_DIRS AT AN EMPTY DIRECTORY, so the machine running the test
    # does not put its own applications on the menu and change its height.
    i=0
    for c in $CATS; do
        i=$((i + 1))
        [ "$i" -le "$n" ] || break
        printf '[Desktop Entry]\nType=Application\nName=Probe %s\nExec=/bin/true\nCategories=%s;\n' \
               "$c" "$c" > "$XDG_DATA_HOME/applications/probe$c.desktop"
    done

    # ⚠ DARK FOR THE TOP SIX GRID ROWS (720/9 = 80 each) AND BRIGHT FOR THE
    # LAST THREE, and the split is where it is because of how short the root
    # page can actually get: SYSTEM's five rows, POWER's four and two headers
    # are there whatever is installed, so even a one-category menu reaches
    # y≈400. The bright band starts below that and above the cap, which is the
    # only window in which the two pages disagree at all.
    python3 -c "
from PIL import Image
im = Image.new('RGB', (1280, 720), (12, 60, 30))
px = im.load()
for x in range(1280):
    for y in range(480, 720):
        px[x, y] = (232, 226, 210)
im.save('$T/wp.png')" || fail "could not write the test wallpaper"
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$T/wp.png" > "$CFG/synuirc"
    printf 'animation_ms = 0\npower_enabled = 0\npower_dim_timeout = 86400\n' >> "$CFG/synuirc"
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n' >> "$CFG/synuirc"
    printf 'power_suspend_timeout = 86400\n' >> "$CFG/synuirc"

    # A bar the wallpaper shows through, which is the desktop where a menu's own
    # alpha is low enough for the correction to be the thing deciding its look.
    printf 'bar_opacity = 0.05\n' > "$CFG/settings.state"

    # ⚠ AND A GLASS DESKTOP TO GO WITH IT, which this rig did without until 594
    # and cannot any more. The menus follow the bar down only where glass is
    # actually being DRAWN (Theme.popupAlpha, keyed on theme.state's
    # glass_surfaces) — so a hermetic HOME with no theme.state at all now opens
    # a solid 0.97 menu, both runs agree trivially, and the rig passes without
    # ever reaching the correction it exists to test. Prism with transparency on
    # is the desktop the reported bug was on.
    printf 'theme=prism\ntransparency=on\nactive_opacity=0.90\n' > "$CFG/theme.state"

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

    quickshell -p "$TREE/shell.qml" > "$T/qs.log" 2>&1 &
    QS_PID=$!
    sleep 5
    kill -0 "$QS_PID" 2>/dev/null || fail "the bar died on startup ($(tail -3 "$T/qs.log"))"

    # The menu is opened the way synui opens it — quickshell's own IPC, the same
    # call keys.c makes. No synthetic input, and nothing to miss.
    quickshell -p "$TREE/shell.qml" ipc call menu open HEADLESS-1 >>"$T/qs.log" 2>&1 \
        || fail "could not open the menu"
    sleep 2
    grim -t ppm -o HEADLESS-1 "$out" 2>>"$T/qs.log" || fail "grim failed"
    kill -9 "$QS_PID" "$SYNUI_PID" 2>/dev/null
    # Reaped explicitly, or the shell prints its own "Killed" line into the
    # test's output between the two runs.
    wait "$QS_PID" 2>/dev/null || true
    wait "$SYNUI_PID" 2>/dev/null || true
    sleep 1
}

run 1  "$TOP/short.ppm"
run 11 "$TOP/tall.ppm"

python3 - "$TOP/short.ppm" "$TOP/tall.ppm" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

WALLPAPER_TOP = (12, 60, 30)

def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.int16)

def modal(img):
    """The panel body's colour, and how uniform that body is.

    The strip probed is inside the panel and below the search box, in the rows
    every page has: a short page and a long one have different LISTS, and the
    background is what is being compared.

    ⚠ THE MEDIAN, NOT THE MOST COMMON COLOUR. A frosted surface is not one
    colour — blur_noise dithers it by design — so on the glass desktop this rig
    now sets up, the commonest single RGB triple covers about a fifth of a
    perfectly good panel and the old 50% guard failed the passing case. The
    median is the same answer for a solid panel and an honest one for a frosted
    panel; the deviation beside it is what says "this is a surface at all"."""
    body = img[70:150, 20:320].reshape(-1, 3)
    med = np.median(body, axis=0)
    mad = float(np.median(np.abs(body - med), axis=0).max())
    return tuple(int(v) for v in med), mad

a, b = load(sys.argv[1]), load(sys.argv[2])
(ca, fa), (cb, fb) = modal(a), modal(b)
print(f"  panel surface   1 category {ca} (deviation {fa:.0f})"
      f"   11 categories {cb} (deviation {fb:.0f})")

fails = []

# The probe has to be looking AT the panel. A strip whose pixels are all over
# the place is text, or an edge, or nothing — and whatever it is, the colour
# compared below is not a surface. Frost dithers by a couple of levels.
for name, f in (("1-category", fa), ("11-category", fb)):
    if f > 8:
        fails.append(f"the {name} probe deviates by {f:.0f} — that is not a panel "
                     f"background, so the comparison below means nothing")

# ── 1. one menu, one material ────────────────────────────────
agree = max(abs(x - y) for x, y in zip(ca, cb)) <= 4
if not agree:
    fails.append(f"the same menu is {ca} with a short page and {cb} with a long "
                 f"one — the page it is showing is deciding what it is made of")

# ── 2. …and it is a surface, not the wallpaper ───────────────
#
# Only where check 1 passed, which is the case it exists for: a build whose
# correction never runs at all leaves every page clear, and two identically
# clear menus satisfy check 1 perfectly.
elif max(abs(x - y) for x, y in zip(ca, WALLPAPER_TOP)) <= 6:
    fails.append(f"both pages are {ca}, which is the wallpaper — the correction "
                 f"fired on neither, so check 1 agreed on two menus that are not "
                 f"there")

for f in fails:
    print("FAIL: " + f)
sys.exit(1 if fails else 0)
PYEOF
echo "PASS"
