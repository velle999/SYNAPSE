#!/bin/sh
# wallpaper_mono.sh — what the desktop draws with when the picture has NO
# colour in it.
#
# The accent has always followed the wallpaper and has always had one answer
# for a wallpaper it could not name a hue off: the theme's own accent. On Prism
# that is the house cyan — the one colour on screen that is nowhere in the
# picture — so a black-and-white photograph, or one simply too dark to match,
# produced a cyan desktop. Monochrome is the honest reading: a grey picture
# gets white and greys.
#
# ⚠ TWO WALLPAPERS, AND THE PAIR IS THE TEST. "The desktop went white" proves
# nothing on its own — a fallback that had stopped measuring altogether would
# pass it on every picture. The magenta run is what says the colour path is
# still there, and the two together are the only statement worth making:
# mono=yes for the grey one, mono=no and a hue for the coloured one.
#
# ⚠ AND `ok=yes` FOR BOTH. The monochrome palette is published exactly like a
# measured one, because every consumer — the bar, the widgets, the eight app
# windows — already gates on `ok=yes`, and a second way of saying "here is what
# to draw with" would be a second way for half the desktop to miss it. `mono=`
# is beside it for the log, the picker and this file.
#
# Usage: wallpaper_mono.sh /path/to/synui
# Skips (77) without a DRM render node or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: wallpaper_mono.sh /path/to/synui}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (it writes the test wallpapers)."; exit 77; }

TMP=$(mktemp -d /tmp/wpmono.XXXXXX)
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
# Prism, so `wallpaper_accent = auto` resolves ON and the answer is drawn with.
printf 'theme=prism\n' > "$CFG/theme.state"

# The two pictures. Grey is a black-to-mid-grey ramp — a wallpaper with real
# structure in it and no hue anywhere, which is what a black-and-white
# photograph is; a flat magenta is the control.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (200, 40, 160)).save('$TMP/colour.png')
g = Image.new('L', (64, 64))
g.putdata([min(y * 2, 255) for y in range(64) for _ in range(64)])
g.convert('RGB').save('$TMP/grey.png')" \
    || fail "could not write the test wallpapers"

field() { sed -n "s/^$1=\(.*\)\$/\1/p" "$STATE" 2>/dev/null; }

# One synui per wallpaper rather than a reload: the picture is decoded at
# startup and cached per output, and this test is about the MEASUREMENT, not
# about which events re-run it. A run is about a second.
run() {   # run <wallpaper>
    rm -f "$STATE"
    {
        printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$1"
        printf 'animation_ms = 0\n'
        # A nested compositor runs its own idle chain, and suspend is NOT
        # per-compositor: power.c hands it to logind, which is system-wide.
        printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
        printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
        printf 'power_suspend_timeout = 86400\n'
    } > "$CFG/synuirc"

    "$SYNUI" -d >"$LOG" 2>&1 &
    SYNUI_PID=$!
    i=0
    while [ $i -lt 100 ]; do
        [ -s "$STATE" ] && grep -q '^ok=' "$STATE" && return 0
        kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
        sleep 0.1; i=$((i + 1))
    done
    fail "no palette.state after 10s for $1"
}

stop() {
    kill -9 "$SYNUI_PID" 2>/dev/null
    wait "$SYNUI_PID" 2>/dev/null
    SYNUI_PID=
}

# ── 1. a picture with no hue in it ───────────────────────────
run "$TMP/grey.png"

[ "$(field ok)" = yes ] \
    && ok "a greyscale wallpaper still publishes a palette (ok=yes)" \
    || bad "a greyscale wallpaper published ok=$(field ok) — the desktop is back on the theme's accent"

[ "$(field mono)" = yes ] \
    && ok "…and says where it came from: mono=yes" \
    || bad "mono=$(field mono) on a greyscale wallpaper"

# ⚠ WHITE TO THE BYTE. The failure this pins is not "the wrong shade of grey":
# it is to_ui_band(), which clamps saturation up and has no hue to clamp, so a
# grey put through it comes out RED. #FFFFFF or a colour — there is no near miss.
[ "$(field accent)" = "#FFFFFF" ] \
    && ok "…and the accent on Prism's dark panel is white" \
    || bad "accent=$(field accent) on a greyscale wallpaper, expected #FFFFFF"

case "$(field secondary)" in
    "#FFFFFF"|"") bad "secondary=$(field secondary) — the clock and the icons are one colour" ;;
    *) if [ "$(field secondary | cut -c2-3)" = "$(field secondary | cut -c4-5)" ] \
          && [ "$(field secondary | cut -c4-5)" = "$(field secondary | cut -c6-7)" ]; then
           ok "…the clock a grey ($(field secondary)), which is a second value and not a second hue"
       else
           bad "secondary=$(field secondary) is not a grey"
       fi ;;
esac

grep -q "no usable hue — monochrome" "$LOG" \
    && ok "…and the log says so, where anybody asking why will look" \
    || bad "the log does not report the monochrome fallback"

stop

# ── 2. …and a picture that HAS one is untouched ──────────────
#
# The half that makes the half above mean anything.
run "$TMP/colour.png"

[ "$(field ok)" = yes ] && [ "$(field mono)" = no ] \
    && ok "the magenta wallpaper still measures a hue (mono=no)" \
    || bad "the magenta wallpaper published ok=$(field ok) mono=$(field mono)"

case "$(field accent)" in
    "#FFFFFF"|"") bad "accent=$(field accent) on a magenta wallpaper — the colour path is gone" ;;
    *) if [ "$(field accent | cut -c2-3)" = "$(field accent | cut -c4-5)" ]; then
           bad "accent=$(field accent) on a magenta wallpaper is a grey"
       else
           ok "…and it is a colour: accent=$(field accent)"
       fi ;;
esac

stop

echo
if [ "$fails" -eq 0 ]; then
    echo "wallpaper_mono: PASS"
    exit 0
fi
echo "wallpaper_mono: $fails check(s) failed"
exit 1
