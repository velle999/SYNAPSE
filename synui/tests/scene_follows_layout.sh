#!/bin/bash
# scene_follows_layout.sh — a screen that leaves the layout and comes back
# draws from where it now stands.
#
# ⛔ THE BUG. Taking an output out of the layout — "Built-in off", a lid closed
# at a desk, `wlr-randr --off` — destroys its wlr_output_layout_output, and the
# scene pairing server_new_output() made goes with it. Putting it back made a
# new layout entry that nothing paired. The layout, the cursor and wlr-randr
# all said the screen had moved; the screen itself went on rendering from where
# it last stood. On a laptop that had been through "Built-in off" once, moving
# the desk monitor above the built-in panel left the panel showing the desk
# monitor's top-left corner — exactly, pixel for pixel — and grey in between.
#
# Nothing looks wrong until the screen MOVES after coming back, which is why it
# lived: a built-in panel usually comes back to (0,0), the place it left.
#
# The signal cannot be faked by a dead path: the wallpaper is a frame with a
# yellow border, painted `fit` at 1:1 on the 1920x1080 screen, so all four of
# its corners are yellow when it draws its own box. A screen drawing a stale
# region shows the 2560x1440 neighbour's picture at 1.33x instead, or nothing,
# and its bottom-right corner is not yellow.
#
# ⚠ "Built-in off" needs an eDP connector, which headless cannot name, so the
# rig takes the screen out with `wlr-randr --off` — the other way out of the
# layout. Both come back through the same wlr_output_layout add, which is where
# the fix listens.
#
# Usage: scene_follows_layout.sh /path/to/synui
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -u
SYNUI=${1:?usage: scene_follows_layout.sh /path/to/synui}

for t in grim wlr-randr python3; do
    command -v "$t" >/dev/null || { echo "SKIP $t not installed"; exit 77; }
done

fails=0
fail() { echo "  FAIL $*"; fails=$((fails + 1)); }
pass() { echo "  ok   $*"; }

# A developer running this inside synui exports SYNUI_SOCKET and
# WAYLAND_DISPLAY; left set, wlr-randr below would rearrange THEIR monitors.
unset SYNUI_SOCKET WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

TMP=$(mktemp -d /tmp/synui-scenelayout.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
mkdir -p "$TMP/synui"
: > "$TMP/synuirc"
echo "show_at_startup=0" > "$TMP/synui/welcome.state"

# The frame: black, 24px yellow border, 1920x1080. Written by hand so the rig
# needs no image tool — zlib and struct are in every python3.
python3 - "$TMP/frame.png" <<'PY'
import struct, sys, zlib
W, H, B = 1920, 1080, 24
rows = bytearray()
edge = b"\xff\xff\x00" * W
mid = b"\xff\xff\x00" * B + b"\x00\x00\x00" * (W - 2 * B) + b"\xff\xff\x00" * B
for y in range(H):
    rows += b"\x00" + (edge if y < B or y >= H - B else mid)
def chunk(t, d):
    c = t + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
printf '%s\nmode fit\n' "$TMP/frame.png" > "$TMP/synui/wallpaper.state"

# Two screens of different sizes side by side, so a screen drawing its
# neighbour's region shows a DIFFERENT picture rather than the same one.
cat > "$TMP/synui/outputs.conf" <<'EOF'
output HEADLESS-1 enabled=1 width=1920 height=1080 refresh=0 transform=0 scale=1.000000 grid_x=1 grid_y=0 x=0 y=0 primary=0
output HEADLESS-2 enabled=1 width=2560 height=1440 refresh=0 transform=0 scale=1.000000 grid_x=2 grid_y=0 x=1920 y=0 primary=0
EOF

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=2
export WLR_RENDERER=pixman

cleanup() {
    [ -n "${PID:-}" ] && kill -9 "$PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

"$SYNUI" -d >"$LOG" 2>&1 &
PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    for c in "$TMP"/wayland-*; do
        case "$c" in *.lock) continue;; esac
        [ -S "$c" ] && SOCK=$(basename "$c") && break
    done
    [ -n "$SOCK" ] && break
    kill -0 "$PID" 2>/dev/null || { echo "ABORT synui died during startup"; tail -5 "$LOG"; exit 1; }
    i=$((i + 1)); sleep 0.1
done
case "$SOCK" in
    wayland-[0-9]*) ;;
    *) echo "ABORT no headless Wayland socket (got '$SOCK')"; exit 1 ;;
esac
export WAYLAND_DISPLAY="$SOCK"
sleep 1

# "yes" when all four corners of HEADLESS-1's own frame are yellow.
corners() {
    grim -t ppm -o HEADLESS-1 "$TMP/h1.ppm" 2>/dev/null || { echo "nocapture"; return; }
    python3 - "$TMP/h1.ppm" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
parts = d.split(b"\n", 3)          # P6, "W H", maxval, pixels
w, h = map(int, parts[1].split())
px = parts[3]
def rgb(x, y):
    o = (y * w + x) * 3
    return px[o], px[o + 1], px[o + 2]
pts = [(10, 10), (w - 11, 10), (10, h - 11), (w - 11, h - 11)]
bad = [(x, y, rgb(x, y)) for x, y in pts
       if not (rgb(x, y)[0] > 200 and rgb(x, y)[1] > 200 and rgb(x, y)[2] < 80)]
print("yes" if not bad else "no %s" % bad)
PY
}

pos_of() { wlr-randr | awk -v o="$1" '$1 == o { f = 1 } f && /Position/ { print $2; exit }'; }

echo "== its own frame at the start =="
r=$(corners)
[ "$r" = yes ] && pass "HEADLESS-1 draws its own frame" \
    || { fail "HEADLESS-1 does not draw its own frame even before anything moved ($r) — the rig proves nothing"; exit 1; }

echo "== moved, without ever leaving the layout =="
wlr-randr --output HEADLESS-2 --pos 0,0 --output HEADLESS-1 --pos 320,1440
sleep 1
[ "$(pos_of HEADLESS-1)" = "320,1440" ] || fail "the move did not take (HEADLESS-1 at $(pos_of HEADLESS-1))"
r=$(corners)
[ "$r" = yes ] && pass "it follows an ordinary move" \
    || fail "it does not follow even an ordinary move ($r)"

echo "== out of the layout and back, then moved =="
wlr-randr --output HEADLESS-1 --off
sleep 0.5
wlr-randr --output HEADLESS-1 --on --pos 0,0 --output HEADLESS-2 --pos 1920,0
sleep 1
r=$(corners)
[ "$r" = yes ] && pass "back in the layout where it was, it draws its own frame" \
    || fail "back in the layout where it was, it does not draw its own frame ($r)"

# ⚠ Somewhere it has NEVER stood. A stale scene output is frozen where the
# screen was when it left — here 320,1440 — so a move back there would look
# right by coincidence.
wlr-randr --output HEADLESS-2 --pos 0,0 --output HEADLESS-1 --pos 2560,0
sleep 1
[ "$(pos_of HEADLESS-1)" = "2560,0" ] || fail "the second move did not take (HEADLESS-1 at $(pos_of HEADLESS-1))"
r=$(corners)
[ "$r" = yes ] && pass "after coming back, it follows a move" \
    || fail "after coming back it still draws from where it was — the scene output was not re-paired with the layout ($r)"

kill -0 "$PID" 2>/dev/null || fail "synui died during the run"

[ "$fails" -eq 0 ] || { echo "scene_follows_layout: $fails failure(s)"; exit 1; }
echo "scene_follows_layout: ok"
