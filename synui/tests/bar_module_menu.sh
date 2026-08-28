#!/bin/sh
# bar_module_menu.sh — a module's right-click menu opens UNDER THE MODULE.
#
# THE BUG THIS EXISTS FOR was written and found in the same hour, which is
# exactly why it is worth a rig: it is invisible to every other kind of check.
# The menu anchored on `root.x` — a module's position inside the bar's Row —
# while a PopupWindow anchors against the bar WINDOW. So the menu opened a few
# pixels from the left edge of the screen, under the launcher, however far
# right the module actually was. It loaded without a warning, it had every row,
# every row worked. It was simply somewhere else.
#
# A screenshot test that only asked "did a menu appear" would have passed on
# that. So the assertion is about WHERE:
#
#   1. right-clicking the module opens something that was not there before
#   2. and it is horizontally centred on the module, not at the screen edge
#
# ⚠ THE SECOND ONE IS THE WHOLE TEST. Dropping it leaves a rig that passes on
# the bug it was written for.
#
# The update notifier is the subject because its state is a FILE — no network,
# no timer, no service: write four lines and the badge is there with a known
# count. `syn-update ping` would need a git remote.
#
# Usage: bar_module_menu.sh /path/to/synui /path/to/quickshell-tree /path/to/vpointer_click
# Skips (77) without a DRM render node, quickshell, grim, dbus-run-session or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: bar_module_menu.sh synui quickshell-tree vpointer_click}
TREE=${2:?usage: bar_module_menu.sh synui quickshell-tree vpointer_click}
VPTR=${3:?usage: bar_module_menu.sh synui quickshell-tree vpointer_click}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim dbus-run-session; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 || { echo "SKIP: python3 PIL."; exit 77; }

TMP=$(mktemp -d /tmp/synui-menu.XXXXXX) || exit 1
chmod 700 "$TMP"

# A private session bus activates xdg-document-portal, which fuse-mounts
# $XDG_RUNTIME_DIR/doc — and that mount outlives every process here, so `rm -rf`
# fails on it and leaves both the mount and the directory behind on every run.
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
CFG="$XDG_CONFIG_HOME/synui"; mkdir -p "$CFG" "$TMP/.cache/syn-update"

printf 'show_at_startup=0\n' > "$CFG/welcome.state"
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (22, 24, 32)).save('$TMP/wp.png')"
{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\npower_enabled = 0\n'
} > "$CFG/synuirc"

# ⚠ THE CLOCK AND THE WORKSPACES ARE TURNED OFF, AND THAT IS NOT COSMETIC.
# The badge has to be FOUND before it can be clicked, and the first version of
# this rig took "the leftmost lit thing in the right half of the bar" — which on
# a 1920 screen is the CENTRED CLOCK, not the badge. So it right-clicked the
# clock, whose module declines the button, and the click fell through to the bar
# and opened the bar's OWN menu — which is anchored on the pointer and therefore
# lands exactly where a correct module menu would. Both the broken anchor and
# the fixed one "passed".
#
# ⚠ AND EVERY OTHER MODULE IS OFF TOO, for the same reason one step further. A
# second attempt located "the leftmost module of the right-hand group" and found
# the LAUNCHER — synui paints "◢ SYNAPSE" over the bar's top-left corner and it
# is not a bar module at all, so no bar setting hides it. With everything else
# switched off, the update badge is the only bar module on screen and the
# RIGHTMOST ink on the bar is unambiguously it.
printf '%s\n' '{ "HEADLESS-1": { "clock": false, "workspaces": false,
                                 "tray": false, "media": false,
                                 "volume": false, "sysinfo": false,
                                 "netbt": false, "weather": false } }' \
    > "$CFG/bar.json"

# The state `syn-update ping` writes. Four lines and the badge is on the bar
# with a known count and a known "held" for the menu's right-hand column.
cat > "$TMP/.cache/syn-update/pending" <<EOF
checked=$(( $(date +%s) - 1500 ))
status=ok
rev=deadbee
updates=3
new=1
held=2
EOF

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

# ⚠ dbus-run-session: the bar reads the LIVE session bus otherwise, and the
# Media module would put whatever the real desktop is playing into the frames
# this compares.
dbus-run-session -- quickshell -p "$TREE/shell.qml" > "$TMP/qs.log" 2>&1 &
QS_PID=$!
sleep 6
kill -0 "$QS_PID" 2>/dev/null || fail "the shell died on startup"
grim -t ppm -o HEADLESS-1 "$TMP/closed.ppm" 2>>"$TMP/qs.log" || fail "grim"

# WHERE the badge is, measured rather than assumed: it moves with every module
# beside it, so a hardcoded x would be a test that breaks when the bar gains a
# module rather than when the menu breaks. And it is VERIFIED to be a module
# that accepts right-click — see the bar.json note above for what happens when
# it is not.
BADGE=$(python3 - "$TMP/closed.ppm" <<'ENDPY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
W, H = im.size
px = im.load()
# Every other module is off, so the only two things with ink on this bar are
# the compositor's own "◢ SYNAPSE" corner on the left and this badge on the
# right. Taken from the RIGHT: the launcher is not a bar module and no setting
# hides it.
cols = [x for x in range(W)
        if any(sum(px[x, y]) > 240 for y in range(4, 24))]
if not cols:
    print("none"); sys.exit(0)
# Walk left off the right edge while the gaps are small. The icon and the text
# inside one module are 6px apart; the launcher is hundreds of pixels away.
end = cols[-1]
start = end
for x in reversed(cols[:-1]):
    if start - x > 12:
        break
    start = x
print((start + end) // 2)
ENDPY
)
[ "$BADGE" != "none" ] || fail "the update badge is not on the bar — the state file was not read"
ok "the update badge is at x=$BADGE"

"$VPTR" "$BADGE" 14 right || fail "vpointer_click failed"
sleep 1.5
grim -t ppm -o HEADLESS-1 "$TMP/open.ppm" 2>>"$TMP/qs.log" || fail "grim (open)"

python3 - "$TMP/closed.ppm" "$TMP/open.ppm" "$BADGE" <<'ENDPY' || exit 1
import sys
from PIL import Image

closed, opened = (Image.open(p).convert('RGB') for p in sys.argv[1:3])
badge = int(sys.argv[3])
W, H = closed.size
pc, po = closed.load(), opened.load()

# Everything BELOW the bar that changed. Below, because the bar itself repaints
# a hover wash under the pointer and that is not the menu.
xs = []
for y in range(30, min(H, 320), 2):
    for x in range(0, W, 2):
        if any(abs(pc[x, y][i] - po[x, y][i]) > 20 for i in range(3)):
            xs.append(x)

fails = 0
def check(cond, msg):
    global fails
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond: fails += 1

check(len(xs) > 300, f"right-click opened something below the bar ({len(xs)}px)")
if not xs:
    sys.exit(1)

lo, hi = min(xs), max(xs)
mid = (lo + hi) // 2
print(f"  menu spans x {lo}..{hi}, centre {mid}; badge at {badge}")

# ⚠ THE ASSERTION THE BUG WOULD HAVE FAILED. Anchored on `root.x` the menu
# clamped to the left edge — centre ~120 on a 1920 screen with the badge past
# 1300. Half the menu's width of slack: it is centred on the module and clamped
# only at a screen edge, neither of which is in play here.
check(abs(mid - badge) < 130,
      f"the menu is centred on the module, not at the screen edge "
      f"(centre {mid} vs badge {badge})")

sys.exit(1 if fails else 0)
ENDPY

echo "bar_module_menu: passed"
