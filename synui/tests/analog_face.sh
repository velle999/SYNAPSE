#!/usr/bin/env bash
# analog_face.sh — the analog clock's numerals are drawn in the accent, and the
# other faces' hour marks are not.
#
# Requested 2026-08-29: the face's numbers should carry the accent colour.
# Only ONE of the four faces has numbers on it — roman — so that is the only
# place the change belongs, and this pins both halves of that.
#
# ⚠ accentInk, NOT accent. The widget is drawn over the WALLPAPER, and
# WidgetFrame publishes `accentInk` as the accent already corrected against
# whatever backdrop the card is sitting on. Using the raw accent is the bug this
# widget's own comment describes for the neon face, and it fails the same way
# here: a Prism-cyan numeral on a pale photograph measures under 2:1 and there
# is nothing in the control panel that moves it.
#
# ⛔ AND THE OTHER THREE FACES KEEP INK FOR THEIR HOUR MARKS. Those ticks are
# what the HANDS are read against; accenting them too would leave the dial with
# no ink on it, which is the opposite of the request as well as unreadable.
#
# Usage: analog_face.sh /path/to/quickshell-tree
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

QS=${1:-quickshell}
F="$QS/widgets/AnalogClock.qml"
[ -f "$F" ] || { echo "no such file: $F" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# The numeral block: from the roman branch to the end of its loop.
NUM=$(sed -n '/face === "roman"/,/^            } else/p' "$F")

printf '%s' "$NUM" | grep -q 'ctx.fillStyle = Qt.rgba(accent.r'
check "the roman numerals are filled with the accent" $?

! printf '%s' "$NUM" | grep -q 'ctx.fillStyle = Qt.rgba(ink.r'
check "…and not with the ink they used to be" $?

# `accent` in this file is bound once, at the top of onPaint. That binding is
# what makes every use of it the corrected colour rather than the raw one.
grep -q 'const accent = root.accentInk' "$F"
check "…where 'accent' is bound to accentInk, corrected for the wallpaper" $?

# ⛔ A Canvas HAS NO BINDINGS. Without the colour in the repaint key the
# numerals would keep last theme's accent until the next second ticked over —
# and on a dial with no second hand, that is a minute of the wrong colour.
grep -q 'root.accentInk' "$F" && grep -A6 'readonly property string repaintKey' "$F" | grep -q 'accentInk'
check "…and a theme change repaints them, because the key names accentInk" $?

# The other three faces mark their hours with ticks, and those stay ink.
CLASSIC=$(sed -n '/face === "classic"/,/face === "roman"/p' "$F")
printf '%s' "$CLASSIC" | grep -q 'hour ? 3 : 1, ink,'
check "the classic face's railway marks are still ink" $?

MINIMAL=$(sed -n '/face === "minimal"/,/face === "classic"/p' "$F")
printf '%s' "$MINIMAL" | grep -qE 'ink|dim'
check "…and the minimal face's marks are still ink" $?

# Small text loses contrast when it is drawn thin, and accentOn() corrects to a
# ratio at FULL alpha — so the numerals must not be faded back down.
printf '%s' "$NUM" | grep -qE 'Qt\.rgba\(accent\.r, accent\.g, accent\.b, 0\.9[0-9]?\)'
check "…and the numerals are drawn at least 0.9 alpha, not faded" $?

echo
echo "$pass/$((pass+fail)) passed"
exit $([ "$fail" = 0 ] && echo 0 || echo 1)
