#!/bin/sh
# battery_surfaces.sh — the battery shows on a laptop and hides on a desktop.
#
# ⛔ THIS BOX HAS NO BATTERY, AND THAT IS THE PROBLEM THIS SOLVES. Every
# laptop-only path in the shell is unreachable on the machine it is developed
# on, and both surfaces that draw a battery hide themselves when there is none
# — so "correctly hidden" and "broken" look exactly alike from a chair, on the
# only machine anybody looks at them from. tests/mock_upower.py is a fake
# org.freedesktop.UPower on a PRIVATE bus (DBUS_SYSTEM_BUS_ADDRESS); the real
# system bus is never touched and the mock refuses to start without that
# variable set.
#
# WHAT IT PINS, and it is one predicate on two surfaces:
#
#   modules/Battery.qml     moduleVisible: dev.isLaptopBattery && dev.isPresent
#   widgets/SysMonitor.qml  the BAT row, on the same test
#
# quickshell's isLaptopBattery is `type == Battery && powerSupply` — both read
# off UPower's synthetic DisplayDevice, which upower fills in only when a
# power-supply battery exists (up_daemon_refresh_battery_props). One shared
# predicate on one synthetic device is why both surfaces vanish together, and
# why neither can tell you that is what happened.
#
# ⚠ THE CONTROL IS A DESKTOP-SHAPED UPOWER, NOT A MISSING ONE. Running the
# shell against no UPower at all would prove something weaker: it would pass
# even if the modules were keyed on the service existing rather than on the
# device being a battery. The desktop mock answers every call with the numbers
# this machine's real upower answers with, so the ONLY difference between the
# two runs is the device's shape.
#
# Usage: battery_surfaces.sh /path/to/synui /path/to/quickshell-tree /path/to/tests
# Skips (77) without a DRM render node, quickshell, grim, dbus-daemon or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: battery_surfaces.sh synui quickshell-tree tests-dir}
TREE=${2:?usage: battery_surfaces.sh synui quickshell-tree tests-dir}
TESTS=${3:?usage: battery_surfaces.sh synui quickshell-tree tests-dir}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
for t in quickshell grim dbus-daemon dbus-run-session; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done
python3 -c 'import PIL' >/dev/null 2>&1 || { echo "SKIP: python3 PIL."; exit 77; }
python3 -c 'import gi; gi.require_version("Gio","2.0"); from gi.repository import Gio' \
    >/dev/null 2>&1 || { echo "SKIP: python3 PyGObject."; exit 77; }

TMP=$(mktemp -d /tmp/synui-bat.XXXXXX) || exit 1
chmod 700 "$TMP"

# ⚠ A PRIVATE SESSION BUS ACTIVATES xdg-document-portal, WHICH FUSE-MOUNTS
# $XDG_RUNTIME_DIR/doc. The mount outlives every process this script started, so
# `rm -rf` on the temporary directory fails on it — leaving both the mount and
# the directory behind, once per run, for ever. Unmounted explicitly before the
# remove; `|| true` throughout because a run that never got that far has nothing
# to unmount and that is not a failure.
unmount_portals() {
    for d in "$TMP"/*/doc "$TMP"/doc; do
        [ -d "$d" ] || continue
        fusermount3 -u "$d" 2>/dev/null || fusermount -u "$d" 2>/dev/null || true
    done
}

cleanup() {
    for p in ${PIDS:-}; do kill -9 "$p" 2>/dev/null; done
    unmount_portals
    rm -rf "$TMP"
}
PIDS=
trap cleanup INT TERM EXIT
fail() { echo "FAIL: $1"; tail -20 "$TMP/qs.log" 2>/dev/null; exit 1; }
ok() { printf '  ok    %s\n' "$1"; }

# One frame, with a mock of the given shape behind it.
shoot() {                       # shoot <shape> <out.ppm>
    shape=$1; out=$2
    run="$TMP/$shape"; mkdir -p "$run"

    # A private bus standing in for the system one. `--print-address` so the
    # address is never guessed, and a permissive policy because this bus exists
    # for the length of one test and has one client.
    cat > "$run/bus.conf" <<CONF
<busconfig>
  <type>system</type>
  <listen>unix:tmpdir=/tmp</listen>
  <policy context="default">
    <allow user="*"/><allow own="*"/>
    <allow send_type="method_call"/><allow send_type="signal"/>
    <allow send_type="method_return"/><allow send_type="error"/>
    <allow receive_type="method_call"/><allow receive_type="signal"/>
    <allow receive_type="method_return"/><allow receive_type="error"/>
  </policy>
</busconfig>
CONF
    dbus-daemon --config-file="$run/bus.conf" --print-address=1 --fork \
                --print-pid=3 3>"$run/buspid" > "$run/busaddr" 2>"$run/buserr" \
        || fail "dbus-daemon would not start: $(cat "$run/buserr")"
    PIDS="$PIDS $(cat "$run/buspid")"
    DBUS_SYSTEM_BUS_ADDRESS=$(cat "$run/busaddr"); export DBUS_SYSTEM_BUS_ADDRESS

    python3 "$TESTS/mock_upower.py" "$shape" 62 2 > "$run/mock.log" 2>&1 &
    PIDS="$PIDS $!"
    i=0
    while [ $i -lt 40 ]; do
        grep -q "mock upower up" "$run/mock.log" && break
        sleep 0.1; i=$((i + 1))
    done
    grep -q "mock upower up" "$run/mock.log" \
        || fail "the mock did not take the name: $(cat "$run/mock.log")"

    # SYNUI_SOCKET unset: WidgetState runs `synctl outputs` to find the primary
    # screen and synctl prefers that variable — a rig that leaves it set asks
    # the LIVE desktop.
    export XDG_RUNTIME_DIR="$run" HOME="$run" XDG_CONFIG_HOME="$run/.config"
    export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
    export GSETTINGS_BACKEND=memory
    unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET SYNUI_SOCKET
    CFG="$XDG_CONFIG_HOME/synui"; mkdir -p "$CFG"

    printf 'show_at_startup=0\n' > "$CFG/welcome.state"
    python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (22, 24, 32)).save('$run/wp.png')"
    {
        printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$run/wp.png"
        printf 'animation_ms = 0\npower_enabled = 0\n'
    } > "$CFG/synuirc"
    printf 'sysmon = on\n' > "$CFG/widgets.state"

    "$SYNUI" > "$run/synui.log" 2>&1 &
    PIDS="$PIDS $!"
    D=
    i=0
    while [ $i -lt 100 ]; do
        D=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' \
            "$run/synui.log" | head -1)
        [ -n "$D" ] && break
        sleep 0.1; i=$((i + 1))
    done
    [ -n "$D" ] || fail "no Wayland socket within 10s ($shape)"
    export WAYLAND_DISPLAY="$D"
    SYNUI_SOCKET="$run/synui-$D.sock"; export SYNUI_SOCKET

    # ⚠ dbus-run-session: THE BAR READS THE LIVE SESSION BUS OTHERWISE. MPRIS is
    # on it, so the Media module picks up whatever the real desktop is playing —
    # and a track title that changes between two frames lands inside the bar
    # probe below and can carry an assertion on its own. A private session bus
    # also keeps the tray and the notification daemon out. The SYSTEM bus is a
    # separate variable and is untouched by this, which is what lets the mock
    # UPower still be found.
    dbus-run-session -- quickshell -p "$TREE/shell.qml" > "$run/qs.log" 2>&1 &
    PIDS="$PIDS $!"
    sleep 6
    cp "$run/qs.log" "$TMP/qs.log"
    if grep -Eq "ReferenceError|TypeError|is not a type" "$run/qs.log"; then
        grep -E "ReferenceError|TypeError|is not a type" "$run/qs.log" | head -3
        fail "the shell logged a QML error ($shape)"
    fi
    grim -t ppm -o HEADLESS-1 "$out" 2>>"$run/qs.log" || fail "grim ($shape)"
}

shoot laptop  "$TMP/laptop.ppm"
ok "shell drew against a laptop-shaped UPower"
shoot desktop "$TMP/desktop.ppm"
ok "shell drew against a desktop-shaped UPower"

python3 - "$TMP/laptop.ppm" "$TMP/desktop.ppm" <<'ENDPY' || exit 1
import sys
from PIL import Image

lap, desk = (Image.open(p).convert('RGB') for p in sys.argv[1:3])
W, H = lap.size

# The bar's right-hand half, and the monitor card at its home corner
# (right/top, 18px in, under the bar). Probed apart because they are two
# independent readers of one predicate — that is the whole point.
STRIP = (W // 2, 4, W, 24)
CARD = (W - 290, 40, W - 10, 200)

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

s = differs(lap, desk, STRIP)
c = differs(lap, desk, CARD)
print(f"  bar strip {s}px   monitor card {c}px")

check(s > 40, f"the bar's battery module appears only on a laptop ({s}px)")
check(c > 40, f"the monitor's BAT row appears only on a laptop ({c}px)")

sys.exit(1 if fails else 0)
ENDPY

echo "battery_surfaces: 2 shapes, 2 surfaces, passed"
