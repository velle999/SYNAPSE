#!/bin/sh
# welcome_guide.sh — the welcome guide LOADS, ANSWERS, and GETS OUT OF THE WAY.
#
# The guide was compositor-drawn until 0.1.0-497 and is quickshell now
# (quickshell/welcome.qml + welcome/), started by synui-welcome(1) as its own
# process. Three things about that arrangement fail SILENTLY, and each one has a
# precedent in this tree:
#
#   1. A QML TYPE THAT IS NOT A TYPE. welcome/ declares a singleton, which turns
#      off implicit sibling resolution, so a qmldir missing one line gives
#      "Guide is not a type" — from a package that installed cleanly. The bar
#      shipped exactly that twice (BarMenu.qml, Osd.qml).
#   2. pages.js NOT BEING INSTALLED. It is a `.pragma library`, so the PKGBUILD
#      needs a `*.js` glob of its own; without it every page is `undefined`,
#      which draws an empty card and says nothing. tuxart.js did this.
#   3. THE GUIDE NOT DRAWING AT ALL. A load with no errors is not a window: it
#      is a layer surface on a compositor, and "Configuration Loaded" is printed
#      long before anything is on screen.
#
# So this asserts all three from the outside — the log for 1, the PIXELS for 2
# and 3 — plus the two behaviours the port had to keep:
#
#   4. IT ANSWERS ITS IPC. `page N` is what makes Super+Escape a toggle across a
#      process boundary (synui-welcome asks a running instance before starting
#      one), and a guide that ignores IPC would silently start a second copy.
#   5. IT CLOSES WHEN A WINDOW OPENS, AND NOT BEFORE. synui_main.c used to hide
#      the panel on the first map; the guide watches ToplevelManager and does it
#      itself. Both halves are asserted, because both have failed:
#      · if the Connections block is wrong — a mistyped signal is a QML warning,
#        not an error — the guide sits full-screen on top of the window you just
#        opened, and deaf, because synui grants a layer surface the keyboard at
#        map and has since handed it to that window;
#      · and if the guard in front of it is missing, the guide closes INSTANTLY
#        on any desktop that is not empty. `ToplevelManager.toplevels` is empty
#        at Component.onCompleted and the windows that were already open are
#        inserted one event-loop turn later, so they look exactly like new ones.
#        That is why this rig opens a window BEFORE the guide as well as after.
#
# ⚠ WHAT IS *NOT* ASSERTED: the keyboard. `wtype` gives a FALSE NEGATIVE against
# Qt clients — it uploads its own keymap through virtual-keyboard and Qt does not
# surface those keys to QML — so a headless keyboard test proving "zero key
# events" here would prove nothing. See project-synui-quickshell-integration.
#
# Usage: welcome_guide.sh /path/to/synui /path/to/quickshell-tree
# Skips (77) without a DRM render node or without quickshell/grim/PIL.

set -u

SYNUI=${1:?usage: welcome_guide.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: welcome_guide.sh /path/to/synui /path/to/quickshell-tree}

# 77 is meson's SKIP code. This reads rendered pixels, and scenefx is
# GLES2/DMA-BUF — the same gate smoke.sh and bar_shape.sh use.
if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL, numpy' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL/numpy not installed."; exit 77; }

[ -f "$TREE/welcome.qml" ] || { echo "FAIL: no welcome.qml in $TREE"; exit 1; }

# SHORT: quickshell's ipc socket lives under XDG_RUNTIME_DIR and a unix path is
# capped at 108 bytes, which a build directory alone can blow.
TMP=$(mktemp -d /tmp/welcome.XXXXXX)
LOG="$TMP/synui.log"
QSLOG="$TMP/qs.log"

cleanup() {
    [ -n "${TERM1_PID:-}" ] && kill -9 "$TERM1_PID" 2>/dev/null
    [ -n "${TERM2_PID:-}" ] && kill -9 "$TERM2_PID" 2>/dev/null
    [ -n "${QS_PID:-}" ]    && kill -9 "$QS_PID"    2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
}
trap cleanup INT TERM EXIT

fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG"   2>/dev/null
    echo "--- guide log (tail) ---"; tail -40 "$QSLOG" 2>/dev/null
    exit 1
}

# Hermetic HOME and runtime dir. SYNUI_SOCKET is set in some shells and points at
# the LIVE desktop — synctl prefers it over WAYLAND_DISPLAY, so a rig that leaves
# it set reconfigures the machine it is running on.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless
unset DISPLAY WAYLAND_DISPLAY
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

# The guide is started BY HAND below, so the compositor must not also start one:
# two instances would race for the same IPC socket and the `page` call would
# reach whichever won. This is also the setting the guide reads for its
# checkbox, so writing it exercises that FileView.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# A synuirc with no autostart. THIS RIG IS THE ONE PLACE THE COMPILED-IN
# DEFAULT BITES: config.c falls back to `syntty` only when it finds no config
# file at all, and a hermetic HOME has none — on a real install /etc/synui/synuirc
# exists and opening it zeroes the list before parsing, so nothing autostarts
# there. Writing any synuirc here does the same, and without it a terminal would
# map on its own and trip assertion 5 before the test got there. Same trap
# tests/bar_radius.sh documents.
printf 'wallpaper = none\n' > "$CFG/synuirc"

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

shot() {   # shot <name>
    grim -t ppm -o "$OUTPUT" "$TMP/$1.ppm" 2>>"$QSLOG" || fail "grim failed for $1"
}

# ── A window that is ALREADY OPEN ────────────────────────────────────────────
#
# Opened FIRST, and deliberately: the guide has to survive it. syntty before
# foot — foot is what the older rigs here reach for, but it is not a SynapseOS
# dependency, and syntty is present wherever synui is. Either will do; all this
# needs is something that maps an xdg_toplevel.
TERMBIN=
for t in syntty foot kitty; do
    command -v "$t" >/dev/null 2>&1 && { TERMBIN=$t; break; }
done

if [ -n "$TERMBIN" ]; then
    "$TERMBIN" -e sleep 300 >/dev/null 2>&1 &
    TERM1_PID=$!
    sleep 3
fi

# The desktop as it is before the guide — WITH that window on it, so the pixel
# comparisons below also say the card draws over a window rather than only over
# the wallpaper.
shot a-bare

quickshell -p "$TREE/welcome.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 4
# Dead already means one of two things, and the second is the likelier: it
# failed to load at all, or it CLOSED on the window opened above — which is what
# an unguarded ToplevelManager watch does on any desktop that is not empty.
kill -0 "$QS_PID" 2>/dev/null \
    || fail "the guide is gone 4s after starting — it either failed to load, or
       closed on the window that was ALREADY open (Guide.qml's arm timer)"

# 1 + 2: a type that is not a type, and a page table that is `undefined`, both
# arrive as QML diagnostics. Matched on the Qt/QML error words rather than on
# "Error:", which the FileView warnings for an absent theme.json also contain —
# a hermetic HOME has no theme, and that miss is expected and harmless.
if grep -Eq 'is not a type|ReferenceError|TypeError|unavailable' "$QSLOG"; then
    echo "--- guide log ---"; cat "$QSLOG"
    fail "the guide loaded with QML errors"
fi
echo "ok 1 - the QML tree loads with no type or reference errors"

# 5a, and it has to be asked HERE — before anything else opens a window. The
# guide is still up four seconds after starting on a desktop that already had
# one, so the initial model population did not read as "a window just opened".
if [ -n "$TERMBIN" ]; then
    echo "ok 5a - a window that was ALREADY open did not close the guide"
fi

shot b-guide

# 4: the IPC answers. Page 3 (0-based 2) is "Make it yours", which is a
# different page from the one it opened on, so this also proves the rail moved.
quickshell -p "$TREE/welcome.qml" ipc call welcome page 2 >/dev/null 2>&1 \
    || fail "the guide did not answer 'ipc call welcome page 2'"
sleep 1
shot c-page3
echo "ok 4 - the guide answers its IPC"

# 5b: a window opens NOW, and the guide gets out of the way.
if [ -n "$TERMBIN" ]; then
    "$TERMBIN" -e sleep 300 >/dev/null 2>&1 &
    TERM2_PID=$!
    sleep 4
    kill -0 "$QS_PID" 2>/dev/null && GUIDE_ALIVE=1 || GUIDE_ALIVE=0
else
    GUIDE_ALIVE=skip
fi

python3 - "$TMP" "$GUIDE_ALIVE" <<'PYEOF' || exit 1
import sys
import numpy as np
from PIL import Image

tmp, alive = sys.argv[1], sys.argv[2]

def load(name):
    return np.asarray(Image.open(f"{tmp}/{name}.ppm").convert("RGB"), dtype=np.int16)

A = load("a-bare")      # the desktop, nothing on it
B = load("b-guide")     # …and with the guide up
C = load("c-page3")     # …turned to page 3

def changed(p, q):
    """Pixels differing by more than sensor noise."""
    return int((np.abs(p - q).sum(axis=2) > 12).sum())

H, W = A.shape[:2]

# 3: THE GUIDE DREW. The card is capped at 880x620 and centred, so it covers a
# large, KNOWN-minimum fraction of any screen this runs on. A floor rather than
# an exact count, because the version string in the rail and the accent are the
# theme's and a palette change must not rewrite this test.
drew = changed(A, B)
floor = 200 * 200
if drew < floor:
    print(f"FAIL: the guide drew {drew} px, expected at least {floor} — "
          "an empty card is what an uninstalled pages.js looks like")
    sys.exit(1)
print(f"ok 2 - the guide drew ({drew} px changed)")

# …and it drew TEXT, not just a slab. A card with `undefined` pages would still
# fill its rectangle: what it could not do is differ between two PAGES.
turned = changed(B, C)
if turned < 2000:
    print(f"FAIL: page 3 differs from page 1 by only {turned} px — "
          "the pages are not rendering their rows")
    sys.exit(1)
print(f"ok 3 - the pages render different content ({turned} px changed)")

if alive == "skip":
    print("ok 5b # SKIP no terminal (syntty/foot/kitty) installed")
elif alive == "1":
    print("FAIL: a toplevel mapped and the guide is still running — "
          "Guide.qml's ToplevelManager watch did not fire")
    sys.exit(1)
else:
    print("ok 5b - the guide closed when a window opened")
PYEOF

echo "PASS"
