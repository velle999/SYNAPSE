#!/bin/sh
# weather_location.sh — a new location is a new place, not a new label.
#
# The place lives in ~/.local/state/omarchy/settings/weather.json, which
# omarchy-weather-location and the radar plugin's city picker WRITE AT RUNTIME.
# src/weather.c is the only thing that fetches, and the bar, the desktop card
# and the lock screen all show what it fetched — so a compositor that read the
# file once, at startup, went on fetching the old city for the rest of the
# session, and the whole desktop said so.
#
# WHAT THIS PINS:
#
#   1. ⚠ A CACHED READING FOR ANOTHER PLACE IS REFUSED AT STARTUP. A location
#      set since the last session would otherwise open on the old city's
#      temperature under the new city's name, and — being fresh — hold it for a
#      whole refresh interval, because a fresh reading is not re-asked.
#   2. The SAME place is accepted, and case-blind: a name-only location is
#      stored as typed and cached as the geocoder spells it.
#   3. A refresh with the file untouched keeps the reading. Change detection
#      that always fired would drop it on every refresh, and with no network
#      the weather would simply vanish.
#   4. ⚠ A LOCATION WRITTEN AT RUNTIME, by the real omarchy-weather-location,
#      is picked up by the next refresh, and the old city's reading goes with
#      it.
#
# ⚠ NOTHING HERE REACHES THE NETWORK: every proxy variable libcurl reads points
# at a closed local port, so each fetch fails at once. That is what makes the
# assertions deterministic — the only readings in play are the ones written
# into the cache by hand.
#
# Usage: weather_location.sh /path/to/synui /path/to/synctl /path/to/omarchy-weather-location.sh
# Skips (77) without a DRM render node.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: weather_location.sh synui synctl omarchy-weather-location.sh}
SYNCTL=${2:?usage: weather_location.sh synui synctl omarchy-weather-location.sh}
LOCSET=${3:?usage: weather_location.sh synui synctl omarchy-weather-location.sh}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi

# SHORT, under /tmp: the control socket is a unix path and those cap at 108
# bytes, which a build directory under a long $HOME blows on its own.
TMP=$(mktemp -d /tmp/synui-wxloc.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

stop_synui() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && wait "$SYNUI_PID" 2>/dev/null
    SYNUI_PID=
}
cleanup() {
    stop_synui
    rm -rf "$TMP"
}
trap cleanup INT TERM EXIT

fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG" 2>/dev/null
    exit 1
}
ok() { printf '  ok    %s\n' "$1"; }

# ⚠ SYNUI_SOCKET UNSET, for the reason postit_ink.sh gives: synctl prefers it
# over WAYLAND_DISPLAY, and a rig that leaves it set asks the LIVE desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET SYNUI_SOCKET

# Port 9 is discard: nothing listens on it locally, so every fetch is refused
# before it leaves the machine.
DEAD=http://127.0.0.1:9
export ALL_PROXY=$DEAD all_proxy=$DEAD HTTPS_PROXY=$DEAD https_proxy=$DEAD
export http_proxy=$DEAD
unset NO_PROXY no_proxy

CFG="$XDG_CONFIG_HOME/synui"
LOC="$HOME/.local/state/omarchy/settings/weather.json"
WX="$CFG/weather.state"
mkdir -p "$CFG" "$(dirname "$LOC")"
printf 'show_at_startup=0\n' > "$CFG/welcome.state"
{
    printf 'autostart =\nanimation_ms = 0\n'
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
    printf 'weather = on\nweather_unit = c\n'
} > "$CFG/synuirc"

# The state a successful fetch for $1 leaves behind, fresh: well inside the
# refresh interval, so nothing at startup re-asks on the reading's account.
seed() {
    cat > "$WX" <<EOF
place=$1
temp=12.0
code=3
unit=C
when=$(date +%s)
cond=Overcast
icon=cloud
stale_after=10800
EOF
}

start_synui() {
    : > "$LOG"
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
}

# ── 1. a cached reading for another place is refused at startup ─────────────
printf '{"name": "Newtown", "latitude": 1.5, "longitude": 2.5}\n' > "$LOC"
seed Oldtown
start_synui
J=$("$SYNCTL" weather) || fail "synctl weather failed"
echo "$J" | grep -q '"on":true' || fail "the rig should have the weather on, got: $J"
echo "$J" | grep -q 'Oldtown' \
    && fail "Oldtown's cached reading was shown with the location set to Newtown: $J"
echo "$J" | grep -q '"have":false' \
    || fail "with no reading for Newtown and no network there is nothing to show, got: $J"
ok "another place's cache refused: $J"
stop_synui

# ── 2. the same place is accepted, whatever its case ────────────────────────
printf '{"name": "oldtown"}\n' > "$LOC"
seed Oldtown
start_synui
J=$("$SYNCTL" weather) || fail "synctl weather failed"
echo "$J" | grep -q '"have":true' \
    || fail "a cache for the configured place (case aside) should be shown, got: $J"
ok "same place, other case, accepted: $J"

# ── 3. a refresh with the file untouched keeps the reading ──────────────────
"$SYNCTL" weather refresh >/dev/null || fail "synctl weather refresh failed"
J=$("$SYNCTL" weather)
echo "$J" | grep -q '"have":true' \
    || fail "a refresh with the location unchanged dropped the reading: $J"
ok "refresh, location unchanged, reading kept"

# ── 4. a location written at runtime replaces the place ─────────────────────
HOME="$TMP" bash "$LOCSET" --set "Newtown" 1.5,2.5 \
    || fail "omarchy-weather-location --set failed"
"$SYNCTL" weather refresh >/dev/null || fail "synctl weather refresh failed"
J=$("$SYNCTL" weather)
echo "$J" | grep -qi 'oldtown' \
    && fail "the location changed to Newtown, but the reading still says: $J"
echo "$J" | grep -q '"have":false' \
    || fail "the old place's reading should be gone and the new one not yet in, got: $J"
ok "runtime location change took effect: $J"

stop_synui
echo "PASS"
