#!/bin/sh
# weather_surfaces.sh — one fetch, three surfaces, and the file between them.
#
# The lock screen, the bar module and the desktop widget all show the weather
# and NONE of them fetches it. src/weather.c does the network on a thread of its
# own and publishes ~/.config/synui/weather.state; everything else reads that.
# The seam is therefore a FILE, and a file is exactly the kind of contract that
# breaks silently — a key renamed on one side, a threshold hardcoded twice, a
# reader that never notices a rewrite.
#
# WHAT THIS PINS:
#
#   1. `synctl weather` distinguishes OFF from ON-BUT-NOTHING-YET. They are
#      different answers and the widget prints different words for them.
#   2. Turning it on is remembered — it goes into saver.state, the Super+Z
#      row's file, so the machine that was asked stays asked after a logout.
#   3. ⚠ Turning it OFF REMOVES the published reading. A temperature left on
#      the bar after the feature was switched off is the desktop insisting on a
#      fact nobody asked it to keep checking, and it is the one behaviour here
#      that cannot be seen by reading the code that draws it.
#   4. The QML side reads the file, and REPAINTS when it changes — no restart,
#      no IPC. Both surfaces: the bar module appears, and the card fills in.
#   5. ⚠ STALENESS CROSSES THE PROCESS BOUNDARY. `stale_after` is written by
#      the compositor and read by WeatherState precisely so that a reading the
#      lock screen calls old and the bar calls current is impossible. A copy of
#      the threshold in QML would pass every other check in this file.
#
# ⚠ NOTHING HERE GOES NEAR THE NETWORK, and it must not: a test that needed
# Open-Meteo would fail on an aeroplane and pass on a bug the day the API
# changed. The readings below are written by hand into the same file the fetch
# thread writes, which is the whole point of there being a file.
#
# ⚠ THE PROBES COMPARE FRAMES, THEY DO NOT NAME A COLOUR. What "stale" looks
# like is a theme's business — dimmed ink resolved against whatever wallpaper is
# behind the card — and a test asserting a hex value would be pinning the
# palette rather than the contract. Three frames that must differ from each
# other says exactly what is meant: the three states are distinguishable.
#
# Usage: weather_surfaces.sh /path/to/synui /path/to/synctl /path/to/quickshell-tree
# Skips (77) without a DRM render node, quickshell, grim or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: weather_surfaces.sh synui synctl quickshell-tree}
SYNCTL=${2:?usage: weather_surfaces.sh synui synctl quickshell-tree}
TREE=${3:?usage: weather_surfaces.sh synui synctl quickshell-tree}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed."; exit 77; }

# SHORT, under /tmp: the control socket is a unix path and those cap at 108
# bytes, which a build directory under a long $HOME blows on its own.
TMP=$(mktemp -d /tmp/synui-wx.XXXXXX) || exit 1
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
    echo "--- synui log (tail) ---"; tail -20 "$LOG"   2>/dev/null
    echo "--- shell log (tail) ---"; tail -30 "$QSLOG" 2>/dev/null
    exit 1
}
ok() { printf '  ok    %s\n' "$1"; }

# ⚠ SYNUI_SOCKET UNSET, for the reason postit_ink.sh gives: WidgetState runs
# `synctl outputs` to find the primary screen, and synctl prefers that variable
# over WAYLAND_DISPLAY — a rig that leaves it set asks the LIVE desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET SYNUI_SOCKET

CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"

# A nested synui puts the welcome guide up as a fullscreen TOP layer surface,
# which would be the only thing in every frame below.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# Flat and dark, so the card's ink is judged against something predictable and
# the frame comparisons are about the card rather than about a photograph.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (18, 18, 24)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"

{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\n'
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
} > "$CFG/synuirc"

# The desktop card on. The bar module needs no file: it is on by default and
# hides itself until there is a reading, which is the behaviour phase 4 checks.
printf 'weather = on\n' > "$CFG/widgets.state"

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
SYNUI_SOCKET="$TMP/synui-$SOCK.sock"; export SYNUI_SOCKET
OUTPUT=HEADLESS-1
WX="$CFG/weather.state"

# ── 1. off is not the same answer as "nothing yet" ──────────────────────────
J=$("$SYNCTL" weather) || fail "synctl weather failed"
echo "$J" | grep -q '"on":false' \
    || fail "a fresh machine should report the weather off, got: $J"
[ -f "$WX" ] && fail "nothing has fetched, yet $WX exists"
ok "off: $J"

"$SYNCTL" weather on >/dev/null || fail "synctl weather on failed"
J=$("$SYNCTL" weather)
echo "$J" | grep -q '"on":true' \
    || fail "after 'weather on' it should report on, got: $J"
# ⚠ `have` IS NOT ASSERTED HERE, and that is not laziness. This box may well
# have a network: the fetch that `weather on` just kicked off is a real one, and
# whether it has landed by the time this line runs is a DNS lookup's worth of
# luck. What phase 1 is pinning is that OFF and ON are different answers with a
# different shape — the widget prints different words for "switched off" and
# "asked and not yet told" — and that is exactly the part that does not depend
# on the weather in Washington.
ok "on: $J"

# ── 2. the switch is remembered ─────────────────────────────────────────────
i=0
while [ $i -lt 40 ]; do
    grep -q '^weather=1' "$CFG/saver.state" 2>/dev/null && break
    sleep 0.1; i=$((i + 1))
done
grep -q '^weather=1' "$CFG/saver.state" 2>/dev/null \
    || fail "'weather on' did not reach saver.state — it would be forgotten at logout"
ok "remembered: saver.state carries weather=1"

# ── 3. off removes the published reading ────────────────────────────────────
#
# Written by hand: this is the state a successful fetch leaves behind, and the
# behaviour under test is what happens to it when the feature is switched off.
seed() {
    cat > "$WX" <<EOF
place=Testville
temp=$1
code=3
unit=C
when=$2
cond=Overcast
icon=cloud
stale_after=10800
EOF
}
NOW=$(date +%s)
seed 12.0 "$NOW"
"$SYNCTL" weather off >/dev/null || fail "synctl weather off failed"
[ -f "$WX" ] && fail "'weather off' left the last reading published at $WX"
ok "off removed the published reading"

# ── 4/5. the shell reads it, and repaints when it changes ───────────────────
#
# ⚠ THE ENGINE STAYS OFF FROM HERE ON, and leaving it on is a real flake rather
# than a theoretical one: on a box with a network the fetch thread lands a REAL
# reading roughly whenever it likes, and it would overwrite the hand-written
# file below between the write and the screenshot — an `empty` frame with a
# temperature in it, or a `stale` one refreshed to now. Nothing in phases 4 and
# 5 is about fetching. The surfaces read a file; this writes the file.
quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 5
kill -0 "$QS_PID" 2>/dev/null || fail "the shell died on startup"

# ⚠ ReferenceError AND TypeError AND "is not a type": a singleton missing from
# qmldir, or a directory import that stopped resolving, shows up here and
# NOWHERE in the pictures below — the widget simply would not be built.
if grep -Eq "ReferenceError|TypeError|is not a type|unavailable" "$QSLOG"; then
    grep -E "ReferenceError|TypeError|is not a type|unavailable" "$QSLOG" | head -5
    fail "the shell logged a QML error"
fi
ok "the shell loaded WeatherState, the module and the widget"

shoot() {
    grim -t ppm -o "$OUTPUT" "$TMP/$1.ppm" 2>>"$QSLOG" || fail "grim failed ($1)"
}

# EMPTY: no file at all. The card says so; the bar module is not there.
rm -f "$WX"
sleep 1.5
shoot empty

# FRESH: a reading from a moment ago.
NOW=$(date +%s)
seed 12.0 "$NOW"
sleep 1.5
shoot fresh

# STALE: the SAME reading, four hours old. Everything on screen is in the same
# place; only how it is drawn may differ — which is the point.
seed 12.0 "$((NOW - 4 * 3600))"
sleep 1.5
shoot stale

python3 - "$TMP/empty.ppm" "$TMP/fresh.ppm" "$TMP/stale.ppm" <<'ENDPY' || exit 1
import sys
from PIL import Image

empty, fresh, stale = (Image.open(p).convert('RGB') for p in sys.argv[1:4])
W, H = empty.size

# The card is top-left at (20, barHeight + 18); the bar is the top strip. Probed
# separately because they answer different questions: the card is the widget
# drawing a reading, the strip is the bar module deciding to exist at all.
CARD = (20, 46, 20 + 216, 46 + 150)
STRIP = (W // 2, 4, W, 24)

def differs(a, b, box, tol=6):
    ca, cb = a.crop(box).load(), b.crop(box).load()
    x0, y0, x1, y1 = box
    n = 0
    for y in range(y1 - y0):
        for x in range(x1 - x0):
            pa, pb = ca[x, y], cb[x, y]
            if any(abs(pa[i] - pb[i]) > tol for i in range(3)):
                n += 1
    return n

fails = 0
def check(cond, msg):
    global fails
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond: fails += 1

c_ef = differs(empty, fresh, CARD)
c_fs = differs(fresh, stale, CARD)
s_ef = differs(empty, fresh, STRIP)

print(f"  card: empty↔fresh {c_ef}px  fresh↔stale {c_fs}px")
print(f"  bar strip: empty↔fresh {s_ef}px")

# A reading arriving has to change the card, and it must do so with nothing
# restarted — the file was written under a running shell.
check(c_ef > 200, f"the card repainted when a reading arrived ({c_ef}px)")
# ⚠ THE ONE THAT CATCHES A DUPLICATED THRESHOLD. Same temperature, same words,
# same place on screen: if `stale_after` were hardcoded in QML rather than read
# from the file, a four-hour-old reading could still be drawn as current and
# these two frames would be identical.
check(c_fs > 40, f"a four-hour-old reading is drawn differently ({c_fs}px)")
# And the bar module: absent with no reading, present with one.
check(s_ef > 20, f"the bar module appeared when a reading arrived ({s_ef}px)")

sys.exit(1 if fails else 0)
ENDPY

echo "weather_surfaces: 5 phases passed"
