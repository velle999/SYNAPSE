#!/bin/sh
# wallpaper_accent.sh — whether the desktop's accent comes off the WALLPAPER is
# a setting now, and `auto` is Prism and nothing else.
#
# It used to be one line in theme.c — `if (theme != PRISM) return` — which made
# it unreachable on the other twelve presets and unswitchable on Prism. And it
# was only ever half a gate: palette.state was written REGARDLESS, and the bar,
# the menus and the widgets read the accent straight out of it (Theme.qml's
# wpAccent), so a macOS 26 desktop drew systemBlue panels beside a bar the
# colour of the picture. One decision, published once, is what this pins.
#
# ⚠ THE ASSERTION IS `use=`, NOT `ok=`. They are different facts and the file
# says both: `ok` is whether the PICTURE had a colour to give, `use` is whether
# this desktop draws with it. Collapsing them — publishing ok=no for a
# switched-off desktop — would be the compositor telling the bar a magenta
# wallpaper is greyscale.
#
# Usage: wallpaper_accent.sh /path/to/synui
# Skips (77) without a DRM render node or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: wallpaper_accent.sh /path/to/synui}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (it writes the test wallpaper)."; exit 77; }

TMP=$(mktemp -d /tmp/wpaccent.XXXXXX)
chmod 700 "$TMP"
LOG="$TMP/synui.log"

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup INT TERM EXIT

fails=0
fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG" 2>/dev/null
    exit 1
}
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"
STATE="$CFG/palette.state"

printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# One saturated hue, so the measurement is not the thing under test: a flat
# magenta has exactly one answer and `ok=yes` is never in doubt.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (200, 40, 160)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"

{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\n'
    # A nested compositor runs its own idle chain, and suspend is NOT
    # per-compositor: power.c hands it to logind, which is system-wide.
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
} > "$CFG/synuirc"

theme() { printf 'theme=%s\n' "$1" > "$CFG/theme.state"; }
accent() {   # accent <auto|off|on>
    if [ "$1" = auto ]; then : > "$CFG/settings.state"
    else printf 'wallpaper_accent = %s\n' "$1" > "$CFG/settings.state"
    fi
}

theme macos26
accent auto

"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

i=0
while [ $i -lt 100 ]; do
    [ -f "$STATE" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
    sleep 0.1; i=$((i + 1))
done
[ -f "$STATE" ] || fail "no palette.state after 10s"

use() { sed -n 's/^use=\(.*\)$/\1/p' "$STATE" 2>/dev/null; }
pok() { sed -n 's/^ok=\(.*\)$/\1/p'  "$STATE" 2>/dev/null; }

# The file is written from the compositor, and a reload has to travel through a
# config load and a repaint — so wait for the VALUE rather than sleeping.
await() {  # await <shell-test> <description>
    i=0
    while [ $i -lt 60 ]; do
        eval "$1" && { ok "$2"; return 0; }
        sleep 0.1; i=$((i + 1))
    done
    bad "$2 — ok=\"$(pok)\" use=\"$(use)\""
    return 1
}

# `reload` writes the state files and SIGHUPs. The whole point of the row is
# that it moves without the wallpaper moving, so nothing here ever re-picks a
# picture: the measurement it re-publishes is the one taken at startup.
reload() { kill -HUP "$SYNUI_PID" 2>/dev/null; }

# ── 1. the picture's answer, which no switch touches ─────────
await '[ "$(pok)" = yes ]' "the magenta wallpaper measures a colour (ok=yes)"

# ── 2. auto is NOT every theme ───────────────────────────────
#
# The half that never existed. macOS 26 has its own accent — systemBlue — and
# had it on its panels the whole time; what it also had was a bar drawn in the
# wallpaper's colour, because this file said nothing about whether to use it.
await '[ "$(use)" = no ]' "…and on macOS 26 the desktop does not use it"
grep -q "not in use (wallpaper_accent)" "$LOG" \
    && ok "…and says so in the log, where somebody will go looking" \
    || bad "the log does not say the measured accent is unused"

# ── 3. …and the switch reaches it on any theme ───────────────
accent on
reload
await '[ "$(use)" = yes ]' "wallpaper_accent = on turns it on under macOS 26"

# ⚠ THE COLOURS ARE STILL THERE AND STILL THE SAME. `use` is a decision about
# the measurement, not a replacement for it: a row that suppressed the hexes
# would leave a future picker with nothing to show.
await '[ -n "$(sed -n "s/^accent=\(.*\)$/\1/p" "$STATE")" ]' \
      "…with the measured colour still published beside it"

# ── 4. auto IS Prism ─────────────────────────────────────────
accent auto
theme prism
reload
await '[ "$(use)" = yes ]' "auto turns it on for Prism, which is built on it"

# ── 5. …and off means off, even there ────────────────────────
#
# The other half nobody could say before: Prism's whole look is the wallpaper's
# colour, and wanting the house cyan instead was not expressible.
accent off
reload
await '[ "$(use)" = no ]' "wallpaper_accent = off turns it off under Prism"

echo
if [ "$fails" -eq 0 ]; then
    echo "wallpaper_accent: PASS"
    exit 0
fi
echo "wallpaper_accent: $fails check(s) failed"
exit 1
