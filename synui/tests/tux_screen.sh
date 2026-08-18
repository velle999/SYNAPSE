#!/bin/sh
# tux_screen.sh — the pet is drawn, in every mood it has, with no compositor.
#
# Tuxagotchi is almost entirely a picture: a pixel penguin that walks, blinks,
# eats, sleeps, gets ill, gets old and dies, plus eight printed icons and a
# status card. Every other way of looking at that picture involves running a
# desktop — which on the development machine is the LIVE seat — and shows one
# mood at a time, the one the pet happens to be in. A sick pet is three hours of
# not feeding it away.
#
# So the drawing is split off from the desktop: TuxShell and TuxScreen import
# QtQuick and tuxart.js and NOTHING else — no Theme, no Quickshell, no
# singleton — and take their colours, their fonts and the pet itself as plain
# properties. This runs them under the `qml` tool with the offscreen platform
# and the software renderer, sixteen pets side by side, and writes a PNG.
#
# What it CHECKS is that they draw at all: a QML binding that throws leaves a
# blank rectangle and no exit status anywhere, which is exactly the failure that
# would ship a widget with an empty screen. A human then looks at the PNG, which
# is the other half of why it exists.
#
# Usage: tux_screen.sh /path/to/quickshell-tree [out.png]
# Skips (77) without Qt 6's qml tool.
#
# ⚠ /usr/bin/qml IS QT 5's on Arch (qt5-declarative owns it) and answers a Qt 6
# file with "Did not load any objects, exiting." — no error, no line number,
# nothing about a version. Qt 6's lives in /usr/lib/qt6/bin, which is NOT on the
# PATH, so it has to be looked for by hand. Same trap as /usr/bin/qmllint.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

TREE=${1:?usage: tux_screen.sh /path/to/quickshell-tree [out.png]}
OUT=${2:-}

# The harness lives beside this script, and both are shipped in the tarball.
HERE=$(cd "$(dirname "$0")" && pwd)
HARNESS="$HERE/tux_screen.qml"
[ -f "$HARNESS" ] || { echo "FAIL: $HARNESS missing"; exit 1; }
[ -d "$TREE/widgets" ] || { echo "FAIL: $TREE/widgets is not a quickshell tree"; exit 1; }

# Qt 6's qml, wherever it is. `command -v qml` is the WRONG answer on Arch (see
# the warning above), so the versioned paths are tried first and the bare name
# is only a last resort for distributions that ship one Qt.
QML=""
for c in /usr/lib/qt6/bin/qml /usr/lib64/qt6/bin/qml /usr/lib/qt6/qml6 "$(command -v qml6 2>/dev/null)"; do
    [ -n "$c" ] && [ -x "$c" ] && { QML=$c; break; }
done
if [ -z "$QML" ]; then
    echo "SKIP: Qt 6's qml tool not found (qt6-declarative)."
    exit 77
fi

# Confirm it really is Qt 6 rather than a Qt 5 binary under a Qt 6 name: the
# whole point of the search above is that the wrong one fails SILENTLY.
case "$("$QML" --version 2>&1)" in
    *" 6."*) ;;
    *) echo "SKIP: $QML is not Qt 6."; exit 77 ;;
esac

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

png=${OUT:-$tmp/tux.png}
log=$tmp/qml.log

# The harness resolves `import "../quickshell/widgets"` against ITSELF, so it is
# run from a copy placed next to the tree under test rather than from the source
# directory — which is what makes an out-of-tree build test the tree it was
# given instead of the one it was written beside.
mkdir -p "$tmp/rig/tests"
cp "$HARNESS" "$tmp/rig/tests/"
ln -s "$(cd "$TREE" && pwd)" "$tmp/rig/quickshell"

# offscreen: no Wayland connection is opened AT ALL, which is what keeps this
# off the live seat. software: no GPU, so it runs on a build box with no card
# and in a VM with no acceleration.
cells=$tmp/cells
mkdir -p "$cells"

QT_QPA_PLATFORM=offscreen \
QT_QUICK_BACKEND=software \
QT_LOGGING_RULES="qt.qpa.*=false" \
    "$QML" "$tmp/rig/tests/tux_screen.qml" -- \
        --out "$png" --cells "$cells" >"$log" 2>&1
rc=$?

if [ $rc -ne 0 ]; then
    echo "FAIL: qml exited $rc"
    sed -n '1,40p' "$log"
    exit 1
fi

if [ ! -s "$png" ]; then
    echo "FAIL: no image written to $png"
    sed -n '1,40p' "$log"
    exit 1
fi

# A PNG, and not a two-pixel one: grabToImage on a window that has not laid out
# yet writes a valid, tiny, empty file.
head -c 8 "$png" | od -An -tx1 | tr -d ' \n' | grep -qi '^89504e470d0a1a0a$' || {
    echo "FAIL: $png is not a PNG"
    exit 1
}
bytes=$(wc -c < "$png")
if [ "$bytes" -lt 8000 ]; then
    echo "FAIL: $png is $bytes bytes — the sheet is blank or cropped"
    exit 1
fi

# A binding that THROWS prints a line and leaves that pet missing without
# failing anything. Cheap to check, so check it — but it is not the real test,
# because the commonest version of this bug does not throw at all (below).
if grep -Ein "TypeError|ReferenceError|is not defined|Unable to assign|Cannot assign|is not a type|unavailable" "$log"; then
    echo "FAIL: QML errors above — a pet is missing from the sheet"
    exit 1
fi

# ── THE ACTUAL CHECK ────────────────────────────────────────────────────────
#
# A misspelled sprite is `undefined`, TuxPixels draws nothing for it, and the
# render succeeds with the mess, the pill or the whole penguin missing and NOT
# ONE WORD anywhere. The first draft of this test passed with `TuxArt.poop`
# renamed to a sprite that does not exist.
#
# So: each pet is also grabbed on its own, and the pairs below differ in EXACTLY
# ONE property of the stub — poops, sick, asleep, the light, the game, the
# stage. If two of them come out byte-identical, the thing that property is
# supposed to draw was not drawn. The renders are deterministic (software
# renderer, animation off, a fixed frame per case), so identical bytes really do
# mean identical pictures.
fail=0
# Compares the LCDs. The icon row is compared by `differ_toy` below, and the two
# are separate BECAUSE they used to be one: comparing whole toys let every pair
# pass on the icon that lights up beside the thing being tested.
differ() {
    a="$cells/$1-lcd.png"; b="$cells/$2-lcd.png"
    if [ ! -s "$a" ] || [ ! -s "$b" ]; then
        echo "FAIL: $1 or $2 was not rendered"; fail=1; return
    fi
    if cmp -s "$a" "$b"; then
        echo "FAIL: $1 and $2 render identically — $3 is not drawn"
        fail=1
    fi
}

differ adult filthy     "the mess on the floor"
differ adult ill        "illness (the palette and the pill)"
differ adult asleep     "the sleeping face and the z's"
differ adult eating     "the open beak and the fish"
differ adult lights-out "the light being off"
differ adult playing    "the game (pips and the arrow row)"
differ adult hungry     "the heart meters emptying"
differ adult calling    "the call and the blinking icon"
differ adult status     "the status card"
differ adult gone       "the headstone"
differ adult egg        "the egg"
differ adult baby       "how small a baby is"
differ baby  child      "growing up a size"
differ adult senior     "an old pet's faded palette"
differ adult light-theme "the pale screen"

# And the shell around it: the icon over what the pet wants lights up, and the
# bottom row becomes the game's two arrows while a game is on.
differ_toy() {
    a="$cells/$1.png"; b="$cells/$2.png"
    if cmp -s "$a" "$b"; then
        echo "FAIL: $1 and $2 render identically — $3 is not drawn"
        fail=1
    fi
}
differ_toy adult filthy  "the flush button lighting up for a mess"
differ_toy adult ill     "the medicine button lighting up for an illness"
differ_toy adult playing "the arrow row taking over from the icon row"

[ "$fail" -eq 0 ] || exit 1

echo "ok: $bytes bytes to $png"
[ -n "$OUT" ] || echo "     (pass a second argument to keep it and look at it)"
exit 0
