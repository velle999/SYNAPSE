#!/bin/sh
# bar_tooltip_edge.sh — a bar tooltip must open ON SCREEN, on the FIRST hover.
#
# THE BUG THIS EXISTS FOR (reported 2026-09-04, with a screenshot): hovering
# the network module — the RIGHTMOST module on the bar — drew a tooltip clipped
# off at the right edge of the screen, with the last word cut in half.
# Unhovering and hovering again drew it correctly. That "second time it works"
# is the whole tell, and it points at two things at once:
#
#   · BarModule.qml centred the popup on `tip.width`, which is the WINDOW's
#     CONFIGURED width and therefore always lags its content — unset before the
#     first configure, stale after it. The first show was placed with a width
#     the popup no longer had (measured here: 194px against a real 211), so it
#     hung 77px off the screen; then the true width landed, the binding re-ran
#     and the popup REPOSITIONED. The second show had the width already, placed
#     correctly at once, and never repositioned.
#   · layer.c unconstrained a popup on its INITIAL COMMIT only. An
#     xdg_popup.reposition installs a fresh positioner and recomputes the
#     geometry from it, throwing that away — so the repositioned popup went
#     exactly where the client asked, off the edge, and only the path that
#     never repositioned came out right.
#
# ⚠ THE ASSERTION IS THE RIGHT EDGE, NOT "DID A TOOLTIP APPEAR". A tooltip
# appeared in the screenshot too; two thirds of it were off the screen. A rig
# that only diffed frames would have passed on the bug it was written for —
# bar_module_menu.sh's header is about the same trap one popup over.
#
# The MEMORY module is the subject: with every other module switched off it is
# the last item in the bar's right-hand Row and therefore sits hard against the
# right edge, its readout comes from /proc/meminfo (no network, no service, no
# timer to wait on), and its tooltip is two lines of ~40 characters — wide
# enough that centring it on a module that close to the edge MUST overhang.
#
# Usage: bar_tooltip_edge.sh /path/to/synui /path/to/quickshell-tree /path/to/vpointer_click
# Skips (77) without a DRM render node, quickshell, grim, dbus-run-session or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: bar_tooltip_edge.sh synui quickshell-tree vpointer_click}
TREE=${2:?usage: bar_tooltip_edge.sh synui quickshell-tree vpointer_click}
VPTR=${3:?usage: bar_tooltip_edge.sh synui quickshell-tree vpointer_click}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim dbus-run-session; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 || { echo "SKIP: python3 PIL."; exit 77; }

TMP=$(mktemp -d /tmp/synui-tiptip.XXXXXX) || exit 1
chmod 700 "$TMP"

# A private session bus activates xdg-document-portal, which fuse-mounts
# $XDG_RUNTIME_DIR/doc — and that mount outlives every process here.
unmount_portals() {
    [ -d "$TMP/doc" ] || return 0
    fusermount3 -u "$TMP/doc" 2>/dev/null || fusermount -u "$TMP/doc" 2>/dev/null || true
}

cleanup() {
    [ -n "${QS_PID:-}" ]    && kill -9 "$QS_PID"    2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    unmount_portals
    rm -rf "$TMP"
}
trap cleanup INT TERM EXIT
fail() { echo "FAIL: $1"; tail -20 "$TMP/qs.log" 2>/dev/null; exit 1; }
ok() { printf '  ok    %s\n' "$1"; }

# SYNUI_SOCKET unset: WidgetState runs `synctl outputs` and synctl prefers that
# variable over WAYLAND_DISPLAY — a rig that leaves it set asks the LIVE desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"; mkdir -p "$CFG"

# A nested synui otherwise brings up the welcome guide, which is a fullscreen
# TOP layer surface — it would cover the bar and swallow the hover.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (22, 24, 32)).save('$TMP/wp.png')"
{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\npower_enabled = 0\n'
} > "$CFG/synuirc"

# ⚠ EVERY OTHER MODULE OFF, so the rightmost ink on the bar is unambiguously
# the memory readout — and so that it really is flush against the right edge.
# `sysinfo` leaves Cpu and Memory, in that order, Memory last.
printf '%s\n' '{ "HEADLESS-1": { "clock": false, "workspaces": false,
                                 "tray": false, "media": false,
                                 "volume": false, "netbt": false,
                                 "updates": false, "weather": false,
                                 "assistant": false, "pomodoro": false,
                                 "sysinfo": true } }' > "$CFG/bar.json"

"$SYNUI" > "$TMP/synui.log" 2>&1 &
SYNUI_PID=$!
SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' \
           "$TMP/synui.log" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died on startup"
    sleep 0.1; i=$((i + 1))
done
[ -n "$SOCK" ] || fail "no Wayland socket within 10s"
export WAYLAND_DISPLAY="$SOCK"
SYNUI_SOCKET="$TMP/synui-$SOCK.sock"; export SYNUI_SOCKET

# dbus-run-session: the bar reads the LIVE session bus otherwise.
dbus-run-session -- quickshell -p "$TREE/shell.qml" > "$TMP/qs.log" 2>&1 &
QS_PID=$!
sleep 6
kill -0 "$QS_PID" 2>/dev/null || fail "the shell died on startup"
grim -t ppm -o HEADLESS-1 "$TMP/cold.ppm" 2>>"$TMP/qs.log" || fail "grim"

# WHERE the memory module is, measured rather than assumed — it moves with the
# cpu readout beside it, so a hardcoded x would break when a digit changes.
MOD=$(python3 - "$TMP/cold.ppm" <<'ENDPY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
W, H = im.size
px = im.load()
# Taken from the RIGHT: the compositor's own "◢ SYNAPSE" corner is painted at
# the far LEFT and is not a bar module, so no setting hides it.
cols = [x for x in range(W)
        if any(sum(px[x, y]) > 240 for y in range(4, 24))]
if not cols:
    print("none"); sys.exit(0)
# Walk left off the right edge while the gaps are small: the icon and the text
# inside one module are 6px apart, the cpu module beside it much further.
end = cols[-1]
start = end
for x in reversed(cols[:-1]):
    if start - x > 12:
        break
    start = x
print((start + end) // 2)
ENDPY
)
[ "$MOD" != "none" ] || fail "no module ink on the bar at all"
ok "the memory module is at x=$MOD"

# ⚠ TWO HOVERS, AND THE COMPARISON BETWEEN THEM IS THE TEST.
#
# "Cut off at the edge" alone cannot be asserted from a screenshot: a tooltip
# that has been correctly slid inward sits FLUSH against the last column, and so
# does one that is clipped by it. Both end at x = W-1. The first version of this
# rig checked exactly that and failed on the fixed build.
#
# What tells them apart is the thing that was actually reported — the SECOND
# hover is right. So: hover, frame; leave, which drops the popup; hover again, frame.
# The second show already knows its width, never repositions, and is therefore
# the known-good placement. A first hover that draws a narrower box starting at
# the module's own midpoint is the bug, exactly and only.
"$VPTR" "$MOD" 14 move || fail "vpointer_click move failed"
sleep 2                        # the module's own hover delay is 450ms
grim -t ppm -o HEADLESS-1 "$TMP/first.ppm" 2>>"$TMP/qs.log" || fail "grim (first)"

# Off the module entirely: containsMouse goes false and the popup goes down.
"$VPTR" 5 400 move || fail "vpointer_click move away failed"
sleep 1
"$VPTR" "$MOD" 14 move || fail "vpointer_click re-hover failed"
sleep 2
grim -t ppm -o HEADLESS-1 "$TMP/second.ppm" 2>>"$TMP/qs.log" || fail "grim (second)"

python3 - "$TMP/cold.ppm" "$TMP/first.ppm" "$TMP/second.ppm" "$MOD" <<'ENDPY' || exit 1
import sys
from PIL import Image

cold, first, second = (Image.open(p).convert('RGB') for p in sys.argv[1:4])
mod = int(sys.argv[4])
W, H = cold.size
pc = cold.load()

# The extent of the tooltip in a frame, measured per ROW against the cold one.
# Per row because the pointer is drawn in both hover frames and was not in the
# cold frame: a cursor is ~32px of changed pixels on its rows and this tooltip
# is ~200px on its own, so the rows are told apart by how much of them moved
# rather than by guessing how tall a cursor is on this machine.
def extent(img):
    p = img.load()
    rows = {}
    for y in range(30, min(H, 260)):
        xs = [x for x in range(W)
              if any(abs(pc[x, y][i] - p[x, y][i]) > 20 for i in range(3))]
        if len(xs) > 100:
            rows[y] = xs
    if not rows:
        return None
    return (min(min(xs) for xs in rows.values()),
            max(max(xs) for xs in rows.values()),
            min(rows), max(rows))

fails = 0
def check(cond, msg):
    global fails
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond: fails += 1

a, b = extent(first), extent(second)

check(a is not None, "the FIRST hover drew a tooltip below the bar")
check(b is not None, "the SECOND hover drew one too")
if a is None or b is None:
    sys.exit(1)

print(f"  first  hover: x {a[0]}..{a[1]} (w {a[1] - a[0] + 1}), y {a[2]}..{a[3]}")
print(f"  second hover: x {b[0]}..{b[1]} (w {b[1] - b[0] + 1}), y {b[2]}..{b[3]}")
print(f"  module at {mod}, screen {W}px")

check(a[1] - a[0] > 100, f"it is the whole tooltip and not a sliver of one "
                         f"({a[1] - a[0] + 1}px wide)")

# ⚠ THE ASSERTION THE BUG FAILS. The second show knows its width and places
# correctly; the first must match it. Measured on the unfixed build, the first
# box was 1146..1279 against the second's 1069..1279 — 134px of a 211px tooltip,
# with the remaining 77px off the side of the screen.
check(abs(a[0] - b[0]) <= 2 and abs(a[1] - b[1]) <= 2,
      f"the first hover draws the tooltip in the SAME place as the second "
      f"({a[0]}..{a[1]} vs {b[0]}..{b[1]})")

# ⚠ NOTHING ELSE IS ASSERTED HERE ON PURPOSE. An earlier draft also checked
# that the first box did not begin at the module's own midpoint — and that
# check PASSED on the bug, because the stale width the popup was placed with
# (194px against a real 211) is wrong but not zero. A check that passes on the
# bug it was written for is worse than no check: it reads like coverage.

sys.exit(1 if fails else 0)
ENDPY

echo "bar_tooltip_edge: passed"
