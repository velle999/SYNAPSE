#!/bin/sh
# wallpaper_live_gpu_accent.sh — the accent comes off a wallpaper drawn on the GPU.
#
# wallpaper_live_accent.sh asks WHICH picture is measured, with swaybg standing
# in for the engine. swaybg commits wl_shm. A real live wallpaper never does:
# linux-wallpaperengine renders with EGL and commits a DMA-BUF, and on NVIDIA
# that import is `external_only` — GL_TEXTURE_EXTERNAL_OES. scenefx's
# fx_texture_bind() returns false for those, which makes
# wlr_texture_preferred_read_format() answer DRM_FORMAT_INVALID and
# wlr_texture_read_pixels() fail, BOTH WITHOUT LOGGING ANYTHING. The desktop
# then measured the invisible static picture underneath again — the original
# symptom, on a build whose swaybg test passed every check.
#
# So the stand-in here paints with GLES (tests/wp_live_gl_client.c) and the
# assertion is the same one that matters: a GREEN client over a MAGENTA static
# wallpaper makes the published accent green, and magenta again on unmap.
#
# Usage: wallpaper_live_gpu_accent.sh /path/to/synui /path/to/wp_live_gl_client
# Skips (77) without a DRM render node or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: wallpaper_live_gpu_accent.sh /path/to/synui /path/to/client}
CLIENT=${2:?usage: wallpaper_live_gpu_accent.sh /path/to/synui /path/to/client}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
[ -x "$CLIENT" ] \
    || { echo "SKIP: $CLIENT not built (it plays the live wallpaper)."; exit 77; }
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (it writes the test wallpaper)."; exit 77; }
TMP=$(mktemp -d /tmp/wpglive.XXXXXX)
chmod 700 "$TMP"
LOG="$TMP/synui.log"
BGLOG="$TMP/client.log"

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
    echo "--- client log ---";       cat "$BGLOG" 2>/dev/null
    exit 1
}
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# ⚠ XDG_RUNTIME_DIR IS THE ISOLATION, NOT `unset WAYLAND_DISPLAY`. A Wayland
# client with no WAYLAND_DISPLAY connects to `wayland-0` in XDG_RUNTIME_DIR,
# which on a developer's machine is the live desktop — so the stand-in would
# paint over the session running the test.
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
elif r > g and r > b:          print("red")
elif b > g and b > r:          print("blue")
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
"$CLIENT" '#00A000' >"$BGLOG" 2>&1 &
BG_PID=$!
sleep 1
kill -0 "$BG_PID" 2>/dev/null || fail "the GL client died on startup"

await green "…and a GPU-drawn live wallpaper takes the screen's own colour"
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

# ── 4. …and the buffer the compositor can only SAMPLE ────────
#
# ⚠ THIS IS THE CASE THAT SHIPPED BROKEN. The client above hands over a DMA-BUF
# with the driver's tiled modifier, which NVIDIA imports as GL_TEXTURE_2D — an
# FBO can be hung off it and glReadPixels works. LINEAR is the modifier NVIDIA
# reports external_only, so the same buffer arrives as GL_TEXTURE_EXTERNAL_OES,
# scenefx's fx_texture_bind() refuses it, and BOTH
# wlr_texture_preferred_read_format() and wlr_texture_read_pixels() fail without
# logging a word. The measurement then has nothing and the desktop goes back to
# the invisible picture underneath — which is the whole bug.
"$CLIENT" --linear '#00A000' >"$BGLOG" 2>&1 &
BG_PID=$!
sleep 1
kill -0 "$BG_PID" 2>/dev/null || fail "the linear-DMA-BUF client died on startup"

await green "…and a buffer the compositor can only sample answers too"

kill "$BG_PID" 2>/dev/null
BG_PID=
await magenta "…and that one gives the picture back as well"

# ── 5. …in the colour it actually is ───────────────────────
#
# ⚠ "IS THERE A HUE" AND "IS IT THE RIGHT ONE" ARE DIFFERENT QUESTIONS, and the
# byte order is where they part company: glReadPixels answers in whatever
# GL_IMPLEMENTATION_COLOR_READ_FORMAT says, which for an opaque copy is three
# bytes per pixel with red first, while palette.c reads four with blue first.
# Get that wrong and a warm wallpaper themes the desktop cold — with every check
# above still passing, because green and magenta both survive a red/blue
# exchange. A red client cannot: swapped, it reads blue.
"$CLIENT" --linear '#C03010' >"$BGLOG" 2>&1 &
BG_PID=$!
sleep 1
await red "…and a red wallpaper is red, not the blue a byte swap would make"
kill "$BG_PID" 2>/dev/null
BG_PID=

echo
if [ "$fails" -eq 0 ]; then
    echo "wallpaper_live_gpu_accent: PASS"
    exit 0
fi
echo "wallpaper_live_gpu_accent: $fails check(s) failed"
exit 1
