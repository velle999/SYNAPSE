#!/bin/sh
# wallpaper_live_accent.sh — the accent comes off the wallpaper you can SEE.
#
# A live wallpaper is not synui's picture. synui-wpengine runs
# linux-wallpaperengine as a wlr-layer-shell BACKGROUND client, and synui's
# layer_tree[BACKGROUND] sits above wallpaper_tree — so the engine's output
# covers the image wallpaper.c painted, completely. Every measurement before
# this walked that painted buffer, which meant a desktop running a Workshop
# wallpaper took its accent off a picture nobody could see: whatever static
# image happened to still be configured underneath.
#
# ⚠ THE DISCRIMINATING PART IS THE SECOND HUE. A test that asserted only "there
# is an accent" passed the whole time this was broken — the static wallpaper
# always had one. So the two wallpapers here are hues that cannot be mistaken
# for each other, and the assertion is WHICH ONE the published accent is:
#
#   1. magenta static wallpaper, nothing over it   → the accent is magenta
#   2. a green BACKGROUND layer client over it     → the accent is GREEN
#   3. that client goes away                       → magenta again
#
# swaybg stands in for linux-wallpaperengine, and it is the honest stand-in:
# what makes a surface the wallpaper here is that it is on the background layer
# and covers the screen, not who launched it or what namespace it chose. Nothing
# in the compositor knows about wpengine.
#
# Usage: wallpaper_live_accent.sh /path/to/synui
# Skips (77) without a DRM render node, swaybg, or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: wallpaper_live_accent.sh /path/to/synui}

# The measurement reads a CLIENT's texture back off the GPU — see
# live_read_argb32() — so this needs a real renderer for the same reason
# bar_opacity.sh does.
if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
command -v swaybg >/dev/null 2>&1 \
    || { echo "SKIP: swaybg not installed (it plays the live wallpaper)."; exit 77; }
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (it writes the test wallpaper)."; exit 77; }

TMP=$(mktemp -d /tmp/wplive.XXXXXX)
chmod 700 "$TMP"
LOG="$TMP/synui.log"
BGLOG="$TMP/swaybg.log"

cleanup() {
    [ -n "${BG_PID:-}" ]    && kill -9 "$BG_PID"    2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup INT TERM EXIT

fails=0
fail() {
    echo "FAIL: $1"
    echo "--- synui log (tail) ---"; tail -20 "$LOG" 2>/dev/null
    echo "--- swaybg log ---";       cat "$BGLOG" 2>/dev/null
    exit 1
}
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# ⚠ XDG_RUNTIME_DIR IS THE ISOLATION, NOT `unset WAYLAND_DISPLAY`. A Wayland
# client with no WAYLAND_DISPLAY connects to `wayland-0` in XDG_RUNTIME_DIR,
# which on a developer's machine is the live desktop — so swaybg would paint
# over the session running the test.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export GSETTINGS_BACKEND=memory
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"
STATE="$CFG/palette.state"

printf 'show_at_startup=0\n' > "$CFG/welcome.state"
# Prism, because `auto` is Prism — this test is about WHICH picture is measured,
# and a theme that does not use the answer would publish `use=no` and prove
# nothing about it.
printf 'theme=prism\n' > "$CFG/theme.state"

python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (200, 40, 160)).save('$TMP/wp.png')" \
    || fail "could not write the test wallpaper"

{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/wp.png"
    printf 'animation_ms = 0\n'
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
} > "$CFG/synuirc"

"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
    sleep 0.1; i=$((i + 1))
done
[ -n "$SOCK" ] || fail "no Wayland socket within 10s"
export WAYLAND_DISPLAY="$SOCK"

accent() { sed -n 's/^accent=#\(.*\)$/\1/p' "$STATE" 2>/dev/null; }

# Which hue the published accent IS, named rather than measured to a tolerance:
# the two wallpapers are a magenta and a green, and the contrast corrector may
# lighten or darken either. What it cannot do is move a hue from one of these
# names to the other.
hue() {
    a=$(accent)
    [ -n "$a" ] || { echo none; return; }
    python3 - "$a" <<'PY'
import sys
h = sys.argv[1]
r, g, b = (int(h[i:i+2], 16) for i in (0, 2, 4))
if g > r and g > b:            print("green")
elif r > g and b > g:          print("magenta")
else:                          print("other")
PY
}

await() {  # await <expected-hue> <description>
    i=0
    while [ $i -lt 150 ]; do
        [ "$(hue)" = "$1" ] && { ok "$2"; return 0; }
        sleep 0.1; i=$((i + 1))
    done
    bad "$2 — accent=#$(accent) reads as $(hue), wanted $1"
    return 1
}

# ── 1. the picture synui paints ──────────────────────────────
await magenta "the static wallpaper's magenta is the accent"

# ── 2. …and a live wallpaper over it ─────────────────────────
#
# ⚠ THIS IS THE ONE THAT FAILED BEFORE THE FIX, AND IT FAILED SILENTLY: the
# accent simply stayed magenta, which looks exactly like a feature that has not
# noticed anything happened.
swaybg -c '#00A000' >"$BGLOG" 2>&1 &
BG_PID=$!
sleep 1
kill -0 "$BG_PID" 2>/dev/null || fail "swaybg died on startup"

await green "…and a live wallpaper over it takes the screen's own colour"
grep -q "live wallpaper measured a hue" "$LOG" \
    && ok "…and says so in the log, where somebody will go looking" \
    || bad "the log does not record the live measurement"

# ── 3. …and giving it back ───────────────────────────────────
#
# `synui-wpengine off` is an unmap, and it uncovers the picture synui never
# stopped painting. An answer that stuck would leave a desktop themed off a
# wallpaper that is no longer on it.
kill "$BG_PID" 2>/dev/null
BG_PID=
await magenta "…and the static picture answers again once it unmaps"

echo
if [ "$fails" -eq 0 ]; then
    echo "wallpaper_live_accent: PASS"
    exit 0
fi
echo "wallpaper_live_accent: $fails check(s) failed"
exit 1
