#!/bin/sh
# wallpaper_live_ink.sh — the INK comes off the wallpaper that is on the screen.
#
# wallpaper_live_gpu_accent.sh asks which picture the ACCENT is measured from.
# This asks the same question of the other measurement taken from the same
# image: the luminance grid every surface with no background of its own picks
# its ink from — the bar, the start menu, the control panel, the widgets.
#
# 388 fixed the accent and left the ink behind, so a desktop on a pale Workshop
# wallpaper drew light text on it: wallpaper.c measured the buffer synui PAINTS,
# the engine's BACKGROUND surface covers that buffer edge to edge, and the
# picture underneath was a dark photograph. Measured on velle's box, grid.DP-3
# averaged 0.130 (the hidden autumn jpg, 0.153) while the screen itself was
# 0.678, and the start menu was very nearly invisible.
#
# ⚠ THE ASSERTION IS WHICH DIRECTION THE INK GOES, NOT THAT THERE IS ONE. The
# broken build publishes a perfectly well-formed backdrop.state — it is just
# describing a picture nobody can see, and it never stops publishing it. So the
# static wallpaper here is nearly BLACK and the live one nearly WHITE: the
# unfixed build answers `light` throughout, and every "did we get an ink" check
# passes on it.
#
# Usage: wallpaper_live_ink.sh /path/to/synui /path/to/wp_live_gl_client
# Skips (77) without a DRM render node or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: wallpaper_live_ink.sh /path/to/synui /path/to/client}
CLIENT=${2:?usage: wallpaper_live_ink.sh /path/to/synui /path/to/client}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
[ -x "$CLIENT" ] \
    || { echo "SKIP: $CLIENT not built (it plays the live wallpaper)."; exit 77; }
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (it writes the test wallpaper)."; exit 77; }

TMP=$(mktemp -d /tmp/wplink.XXXXXX)
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
    echo "--- synui log (tail) ---";  tail -20 "$LOG" 2>/dev/null
    echo "--- client log ---";        cat "$BGLOG" 2>/dev/null
    echo "--- backdrop.state ---";    cat "$STATE" 2>/dev/null
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
STATE="$CFG/backdrop.state"

printf 'show_at_startup=0\n' > "$CFG/welcome.state"

# ⚠ A NEARLY BLACK STATIC WALLPAPER, which is the picture that must NOT answer
# once the live one is up. 0.02 is unambiguously "use light ink"; the live
# client below is unambiguously "use dark". Two wallpapers a shade apart would
# make a passing run and a failing one differ by a rounding.
python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (12, 12, 14)).save('$TMP/wp.png')" \
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

# The per-output ink, which is what a bar actually reads — the folded `bar_ink=`
# key is the older cross-monitor answer and there is one screen here anyway.
ink() { sed -n 's/^bar_ink\.[^=]*=\(.*\)$/\1/p' "$STATE" 2>/dev/null | head -1; }

# The wallpaper's own grid, averaged. This is the half the START MENU reads —
# the bar has a strip of its own and the two can disagree, so an ink check alone
# would leave the reported symptom untested.
gridmean() {
    line=$(sed -n 's/^grid\.[^=]*=\(.*\)$/\1/p' "$STATE" 2>/dev/null | head -1)
    [ -n "$line" ] || { echo none; return; }
    printf '%s' "$line" | tr ',' '\n' | awk '{s+=$1;n++} END{if(n)printf "%.2f\n",s/n; else print "none"}'
}

await() {  # await <expected-ink> <description>
    i=0
    while [ $i -lt 150 ]; do
        [ "$(ink)" = "$1" ] && { ok "$2"; return 0; }
        sleep 0.1; i=$((i + 1))
    done
    bad "$2 — bar_ink is '$(ink)', wanted '$1' (grid mean $(gridmean))"
    return 1
}

# Compare the grid mean against a threshold. `awk` rather than `[` because these
# are fractions and the shell only compares integers.
grid_is() {  # grid_is <lt|gt> <value> <description>
    g=$(gridmean)
    [ "$g" = "none" ] && { bad "$3 — no grid published at all"; return 1; }
    if awk -v g="$g" -v v="$2" -v op="$1" \
        'BEGIN{exit !(op=="lt" ? g<v : g>v)}'; then
        ok "$3"
    else
        bad "$3 — grid mean is $g, wanted $1 $2"
    fi
}

# ── 1. the picture synui paints ──────────────────────────────
await light "a near-black static wallpaper wants light ink"
grid_is lt 0.20 "…and publishes a dark grid for the panels to read"

# ── 2. …and a live wallpaper over it ─────────────────────────
#
# ⚠ THIS IS THE ONE THAT WAS BROKEN, AND IT WAS BROKEN QUIETLY: the ink simply
# stayed `light`, which is indistinguishable from a wallpaper that genuinely is
# dark — right up until you look at the screen.
"$CLIENT" '#F0F0F0' >"$BGLOG" 2>&1 &
BG_PID=$!
sleep 1
kill -0 "$BG_PID" 2>/dev/null || fail "the GL client died on startup"

await dark "…and a pale live wallpaper over it wants dark ink"
grid_is gt 0.60 "…and the grid follows the picture that is actually on screen"

# ── 3. …and giving it back ───────────────────────────────────
kill "$BG_PID" 2>/dev/null
BG_PID=
await light "…and the painted picture answers again once it unmaps"
grid_is lt 0.20 "…grid included, rather than sticking on the live one"

# ── 4. …and the buffer the compositor can only SAMPLE ────────
#
# ⚠ THE CASE THAT SHIPPED BROKEN TWICE. LINEAR is the modifier NVIDIA reports
# external_only, so this buffer arrives as GL_TEXTURE_EXTERNAL_OES and cannot be
# read back at all — only sampled. The tiled client above passes even on a build
# that reads the client texture directly; this one does not.
"$CLIENT" --linear '#F0F0F0' >"$BGLOG" 2>&1 &
BG_PID=$!
sleep 1
kill -0 "$BG_PID" 2>/dev/null || fail "the linear-DMA-BUF client died on startup"

await dark "…and one the compositor can only sample answers too"
grid_is gt 0.60 "…with a grid off the sampled copy, not the hidden picture"

kill "$BG_PID" 2>/dev/null
BG_PID=
await light "…and that one gives the painted picture back as well"

echo
if [ "$fails" -eq 0 ]; then
    echo "wallpaper_live_ink: PASS"
    exit 0
fi
echo "wallpaper_live_ink: $fails check(s) failed"
exit 1
