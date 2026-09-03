#!/bin/sh
# menu_uninstall.sh — right-clicking an application in the start menu opens a
# menu, and that menu is where the pointer is.
#
# THE CLASS OF BUG THIS EXISTS FOR is the one bar_module_menu.sh was written
# for, one panel over: a context menu that loads without a warning, has every
# row, every row works — and is somewhere else on the screen. Nothing else
# catches it. qmllint passes, the file loads, the rows are in the tree.
#
# ⚠ AND THE CHEAPER HALF IS WORTH AS MUCH. quickshell REFUSES a file with an
# unresolved type and says so only in its log: StartMenu.qml grew a Process and
# a StdioCollector for this feature, which live in Quickshell.Io — an import the
# file did not have. Without it the start menu simply stops existing, and the
# bar comes up looking perfectly normal. Booting the real tree is what notices.
#
# What is asserted:
#   1. the start menu opens at all (so the tree loaded)
#   2. right-clicking a row draws something that was not there before
#   3. and it is at the POINTER, not at the panel's origin
#   4. left-clicking a row still launches — the context menu did not eat it
#
# Usage: menu_uninstall.sh /path/to/synui /path/to/quickshell-tree /path/to/vpointer_click
# Skips (77) without a DRM render node, quickshell, grim, dbus-run-session or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: menu_uninstall.sh synui quickshell-tree vpointer_click}
TREE=${2:?usage: menu_uninstall.sh synui quickshell-tree vpointer_click}
VPTR=${3:?usage: menu_uninstall.sh synui quickshell-tree vpointer_click}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."; exit 77
fi
for t in quickshell grim dbus-run-session wtype; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 || { echo "SKIP: python3 PIL."; exit 77; }

TMP=$(mktemp -d /tmp/synui-uninst.XXXXXX) || exit 1
chmod 700 "$TMP"

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
fail() { echo "FAIL: $1"; tail -25 "$TMP/qs.log" 2>/dev/null; exit 1; }
ok()   { printf '  ok    %s\n' "$1"; }

# Every seatbelt bar_module_menu.sh documents, for the same reasons — most of
# all SYNUI_SOCKET, which synctl prefers over WAYLAND_DISPLAY: a rig that leaves
# it set drives the LIVE desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"; mkdir -p "$CFG"

printf 'show_at_startup=0\n' > "$CFG/welcome.state"
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (22, 24, 32)).save('$TMP/wp.png')"
{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\npower_enabled = 0\n'
} > "$CFG/synuirc"

# ⚠ THE MENU'S CONTENTS ARE THE SANDBOX'S, not the tester's installed software.
# XDG_DATA_HOME points here, so the list is one application with a name nothing
# else on the page shares — which is what makes "the row at this y" a knowable
# thing rather than a guess about somebody's desktop.
APPS="$TMP/.local/share/applications"; mkdir -p "$APPS"
export XDG_DATA_HOME="$TMP/.local/share"
cat > "$APPS/zzrigapp.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Zzrigapp
Exec=$TMP/launched.sh
Categories=Utility;
EOF
printf '#!/bin/sh\ntouch "%s/launched"\n' "$TMP" > "$TMP/launched.sh"
chmod +x "$TMP/launched.sh"

"$SYNUI" > "$TMP/synui.log" 2>&1 &
SYNUI_PID=$!
SOCK=; i=0
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

dbus-run-session -- quickshell -p "$TREE/shell.qml" > "$TMP/qs.log" 2>&1 &
QS_PID=$!
sleep 6
kill -0 "$QS_PID" 2>/dev/null || fail "the shell died on startup"

# ⛔ A REFUSED FILE IS SILENT ON SCREEN AND LOUD IN THIS LOG. quickshell drops a
# QML file it cannot resolve every type in and carries on with the rest of the
# tree, so the bar comes up and the start menu simply never opens.
if grep -qiE "StartMenu.*(error|is not a type|not installed|unable)" "$TMP/qs.log"; then
    echo "FAIL: quickshell would not load StartMenu.qml:"
    grep -iE "StartMenu" "$TMP/qs.log" | head -10
    exit 1
fi
ok "quickshell loaded StartMenu.qml"

grim -t ppm -o HEADLESS-1 "$TMP/desktop.ppm" 2>>"$TMP/qs.log" || fail "grim"
# ⚠ THE BAR'S OWN IPC, not `synctl dispatch start_menu`. That action forks
# `synui-bar ipc call menu toggle` — a wrapper this rig has not installed and
# which would resolve a DIFFERENT shell tree than the one under test. It also
# fails SILENTLY: the fork's exit status never comes back, so the dispatch
# returns 0 and nothing opens. Calling quickshell's ipc directly addresses the
# instance started above, by the same path.
quickshell -p "$TREE/shell.qml" ipc call menu open HEADLESS-1 \
    >>"$TMP/qs.log" 2>&1 || fail "the bar's menu IPC refused the call"
sleep 1.5
grim -t ppm -o HEADLESS-1 "$TMP/menu.ppm" 2>>"$TMP/qs.log" || fail "grim (menu)"

# ⛔ THE ROW HAS TO BE AN APPLICATION, AND "a row with ink on it" IS NOT ENOUGH.
# The root page is mostly CATEGORIES and actions, and right-clicking one of
# those is supposed to do nothing at all — so a rig that picked an arbitrary
# inked row would be asserting about a menu that correctly never opened, and
# would pass on a right-click handler that had been deleted.
#
# Searching for the sandbox's one planted application leaves a list with a
# single app row in it. wtype, because the search box is the keyboard's and
# there is no pointer path to it.
wtype "zzrigapp" || fail "wtype"
sleep 1.2
grim -t ppm -o HEADLESS-1 "$TMP/searched.ppm" 2>>"$TMP/qs.log" || fail "grim (searched)"

ROWY=$(python3 - "$TMP/desktop.ppm" "$TMP/searched.ppm" <<'ENDPY'
from PIL import Image
import sys
a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB')
W, H = a.size
pa, pb = a.load(), b.load()
# The menu hangs off the bar down the left edge. With the search narrowed to one
# result the list is: the search box, then the single row. Take the LOWEST band
# of ink — the row — rather than the box above it.
rows = [y for y in range(H)
        if sum(1 for x in range(20, 240) if pa[x, y] != pb[x, y]) > 4]
if len(rows) < 4:
    print("none"); sys.exit(0)
# The last contiguous band, and its middle.
end = rows[-1]; start = end
for y in reversed(rows[:-1]):
    if start - y > 3:
        break
    start = y
print((start + end) // 2)
ENDPY
)
[ "$ROWY" != "none" ] || fail "the start menu did not open, or the search found nothing"
ok "the start menu is open and searched; the application row is at y=$ROWY"

CLICKX=120
"$VPTR" "$CLICKX" "$ROWY" right || fail "vpointer_click"
sleep 1.5
grim -t ppm -o HEADLESS-1 "$TMP/ctx.ppm" 2>>"$TMP/qs.log" || fail "grim (ctx)"

python3 - "$TMP/searched.ppm" "$TMP/ctx.ppm" "$CLICKX" "$ROWY" <<'ENDPY' || exit 1
from PIL import Image
import sys
a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB')
cx, cy = int(sys.argv[3]), int(sys.argv[4])
W, H = a.size
pa, pb = a.load(), b.load()

# ⛔ "SOMETHING CHANGED" IS NOT THE TEST, and the first draft of this rig proved
# it: run against a StartMenu.qml with NO context menu at all, a right-click
# fell through to the dismiss catcher and CLOSED the whole start menu — a change
# of 155,000 pixels, which sailed past an assertion that only counted them. The
# rig passed on the exact absence it was written to detect.
#
# So the first thing asserted is that the start menu is STILL THERE. Its panel
# is the top-left corner; if right-click dismissed it, that region reverts to
# the wallpaper and this fails.
panel = [(x, y) for y in range(30, 150) for x in range(2, 330)]
same = sum(1 for x, y in panel if pa[x, y] == pb[x, y])
if same < len(panel) * 0.55:
    print("FAIL: right-click DISMISSED the start menu (%d%% of the panel changed)"
          % (100 - 100 * same // len(panel)))
    print("      a context menu has to open ON the menu, not instead of it")
    sys.exit(1)
print("  ok    the start menu is still open under it")

diff = [(x, y) for y in range(H) for x in range(W) if pa[x, y] != pb[x, y]]
if len(diff) < 150:
    print("FAIL: right-clicking a row drew nothing (%d px)" % len(diff))
    sys.exit(1)
xs = [p[0] for p in diff]; ys = [p[1] for p in diff]
print("  ok    it drew a menu (%d px, x %d..%d, y %d..%d)"
      % (len(diff), min(xs), max(xs), min(ys), max(ys)))

# ⚠ AND IT IS AT THE POINTER. A menu anchored on the panel's origin lands in the
# corner and looks perfectly correct in a screenshot nobody measured — the bug
# bar_module_menu.sh exists for, one panel over.
if min(xs) > cx + 40 or max(xs) < cx:
    print("FAIL: the menu is not under the pointer (x %d..%d, clicked at %d)"
          % (min(xs), max(xs), cx)); sys.exit(1)
if min(ys) > cy + 40 or max(ys) < cy - 10:
    print("FAIL: the menu is not at the pointer's height (y %d..%d, clicked at %d)"
          % (min(ys), max(ys), cy)); sys.exit(1)
print("  ok    and it is at the pointer, not at the panel's origin")
ENDPY

# ⛔ AND LEFT STILL LAUNCHES. A row that answered both buttons with a context
# menu would be a start menu that stopped starting anything — and it would pass
# every assertion above.
#
# ⚠ MEASURED AS "THE MENU CLOSED", not as "the program ran". activate() ends in
# MenuState.close() for every row kind, so the menu going away IS the row having
# been activated — and it is the only half of it this sandbox can see. Watching
# for the process instead was the first attempt and it failed against an
# UNMODIFIED StartMenu.qml too: a launch here goes out through
# DesktopEntry.execute() and the portal, neither of which reaches a temp
# directory in a headless session. An assertion that fails on correct code is
# worse than no assertion.
quickshell -p "$TREE/shell.qml" ipc call menu close >>"$TMP/qs.log" 2>&1 \
    || fail "the menu IPC refused close"
sleep 0.5
quickshell -p "$TREE/shell.qml" ipc call menu open HEADLESS-1 >>"$TMP/qs.log" 2>&1 \
    || fail "the menu IPC refused open"
sleep 1.2
wtype "zzrigapp" || fail "wtype (second)"
sleep 1.2
grim -t ppm -o HEADLESS-1 "$TMP/before-left.ppm" 2>/dev/null
"$VPTR" "$CLICKX" "$ROWY" move >/dev/null 2>&1
sleep 0.4
# ⚠ `1`, NOT `left`. There is no left keyword — the left button is the
# default and the third argument is a click COUNT. `left` read as
# atoi("left") == 0 and pressed nothing at all, which is why
# vpointer_click now refuses it outright.
"$VPTR" "$CLICKX" "$ROWY" 1 || fail "vpointer_click (left)"
sleep 2
grim -t ppm -o HEADLESS-1 "$TMP/after-left.ppm" 2>>"$TMP/qs.log" || fail "grim (left)"

python3 - "$TMP/before-left.ppm" "$TMP/after-left.ppm" <<'ENDPY' || exit 1
from PIL import Image
import sys
a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB')
pa, pb = a.load(), b.load()
panel = [(x, y) for y in range(30, 150) for x in range(2, 330)]
changed = sum(1 for x, y in panel if pa[x, y] != pb[x, y])
if changed < len(panel) * 0.30:
    print("FAIL: left-clicking the row did nothing — the menu is still open")
    print("      the context menu took both buttons and the row stopped launching")
    sys.exit(1)
print("  ok    left-clicking the same row still activates it (the menu closed)")
ENDPY

echo "menu_uninstall: passed"
