#!/bin/sh
# bar_scene_strip.sh — the bar's ink comes off what is UNDER the bar, and the
# wallpaper is only sometimes that.
#
# bar_backdrop.sh pins the wallpaper half: synui measures the strip of picture
# the bar covers and publishes which ink survives it. That was the whole answer
# for as long as the bar reserved an exclusive zone, because a window laid out
# below the bar is not behind it. Two arrangements break it, and a user makes
# both on purpose:
#
#   * AUTO-HIDE. Bar.qml sets `exclusiveZone: bar.autohide ? 0 : Theme.barSpan`,
#     so an auto-hiding bar reserves nothing and every maximized window comes up
#     UNDERNEATH it. The strip of wallpaper wp_top_lum measured is then not on
#     screen anywhere.
#   * a FLOATING window dragged over the strip, which needs no setting at all.
#
# In both, the bar inks itself for a picture nobody can see — dark text on a
# dark browser, which is the failure this file exists to catch.
#
# What is asserted is backdrop.state's `bar_strip.<output>` row, because that is
# the interface: barscan.c fills it off the scene graph, and Theme.qml folds the
# columns each module covers. No bar and no pixels are needed.
#
# …and `scene.<output>` beside it, which is the same question asked of the WHOLE
# screen rather than of the bar's one strip. The bar was never the only surface
# with a backdrop it could not see — the start menu, the bar's menus, the mixer,
# the OSD and every panel synui draws open WHERE THEY ARE PUT, which is over a
# window far more often than over the wallpaper, and each of them was choosing
# its ink from the picture that window covers. Same bug, same measurement, one
# grid wider; `scene_ink` is the switch, and section 3d proves off means off
# without the row disappearing.
#
# ⚠ THE DISCRIMINATING ASSERTION IS THE DISAGREEMENT. A near-black wallpaper
# with a WHITE window over it must publish `bar_ink=light` (the wallpaper's
# answer, unchanged) AND a bar strip that reads white. A scan that merely echoed
# the wallpaper — the way this would fail if the scene walk found nothing and
# quietly fell back — passes every other check in this file and fails that one.
#
# ⚠ NO BAR RUNS HERE, WHICH IS THE POINT AND NOT A GAP. With no layer surface
# claiming an exclusive zone, usable_area is the whole output and an ordinary
# tiled window covers the strip — which is exactly the geometry an auto-hiding
# bar produces. The cut-off in barscan.c is layer_tree[TOP] itself, so it is the
# same code path either way.
#
# Usage: bar_scene_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node or PIL.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: bar_scene_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: bar_scene_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: bar_scene_strip.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

# 77 is meson's SKIP code. synui renders through scenefx's fx_renderer, which is
# GLES2 and DMA-BUF only — and this test reads client pixels back off the GPU,
# so a runner with no render node has nothing to measure.
if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    exit 77
fi
python3 -c 'import PIL' >/dev/null 2>&1 \
    || { echo "SKIP: python3 PIL not installed (it writes the test wallpaper)."; exit 77; }

TMP=$(mktemp -d /tmp/barstrip.XXXXXX)
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

cleanup() {
    for p in $CLIENT_PIDS; do kill -9 "$p" 2>/dev/null; done
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
ok()   { printf '  ok    %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# ⚠ Hermetic HOME, and SYNUI_SOCKET unset. synctl prefers SYNUI_SOCKET over
# WAYLAND_DISPLAY, so a rig that leaves it set drives the LIVE desktop.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET
unset SYNUI_SOCKET
CFG="$XDG_CONFIG_HOME/synui"
mkdir -p "$CFG"
STATE="$CFG/backdrop.state"

# A hermetic HOME is a first run, so the welcome panel would come up.
printf 'show_at_startup=0\n' > "$CFG/welcome.state"

python3 - "$TMP" <<'ENDPY' || fail "could not write the test wallpaper"
import sys
from PIL import Image
# Near-black, so the WALLPAPER's answer is `light` ink. The client that covers
# it is opaque white, whose answer is `dark`. The two disagreeing is what makes
# the assertions below able to tell the measurements apart.
Image.new('RGB', (1920, 1080), (2, 2, 3)).save(sys.argv[1] + '/black.png')
ENDPY

# `stretch`, not `fill`: fill crops, and which axis it crops depends on the
# headless output's aspect. Nothing here is about the scaler.
{
    printf 'wallpaper = %s\nwallpaper_mode = stretch\nautostart =\n' "$TMP/black.png"
    printf 'animation_ms = 0\n'
    # ⚠ SUSPEND IS NOT PER-COMPOSITOR. A hermetic HOME means synui's DEFAULT
    # idle chain applies, and it ends at power.c handing suspend to logind,
    # which is system-wide — a headless rig that outlives the timeout suspends
    # the whole machine. power_enabled = 0 alone is not enough; every stage has
    # to be pushed past any plausible run.
    printf 'power_enabled = 0\npower_dim_timeout = 86400\n'
    printf 'power_blank_timeout = 86400\npower_lock_timeout = 86400\n'
    printf 'power_suspend_timeout = 86400\n'
} > "$CFG/synuirc"

"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    for c in "$TMP"/wayland-*; do
        case "$c" in *.lock) continue;; esac
        [ -S "$c" ] && SOCK=$(basename "$c") && break
    done
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
    i=$((i + 1)); sleep 0.1
done
[ -n "$SOCK" ] || fail "no wayland socket after 10s"
export WAYLAND_DISPLAY="$SOCK"
CTLSOCK="$TMP/synui-$SOCK.sock"

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

OUTNAME=$(synctl outputs | tr '{' '\n' | sed -n 's/.*"name":"\([^"]*\)".*/\1/p' | head -1)
[ -n "$OUTNAME" ] || fail "no output reported by synctl"
echo "compositor: WAYLAND_DISPLAY=$SOCK  output=$OUTNAME"

ink()   { sed -n 's/^bar_ink=\(.*\)$/\1/p' "$STATE" 2>/dev/null; }
strip() { sed -n "s/^bar_strip\.$OUTNAME=\(.*\)$/\1/p" "$STATE" 2>/dev/null; }
# The same measurement over the WHOLE output — 16x9 cells, what every surface
# that is not the bar folds. `grid.` is the WALLPAPER's and is deliberately not
# read here except to prove the two disagree.
scene() { sed -n "s/^scene\.$OUTNAME=\(.*\)$/\1/p" "$STATE" 2>/dev/null; }
wpgrid() { sed -n "s/^grid\.$OUTNAME=\(.*\)$/\1/p" "$STATE" 2>/dev/null; }

# The lowest and highest column in the row, so an assertion can say "every
# column" without sixteen greps. Empty row prints nothing, which every caller
# below reads as "not published yet".
strip_min() { strip | tr ',' '\n' | sort -g | head -1; }
strip_max() { strip | tr ',' '\n' | sort -g | tail -1; }
scene_min() { scene | tr ',' '\n' | sort -g | head -1; }
scene_max() { scene | tr ',' '\n' | sort -g | tail -1; }
wp_max()    { wpgrid | tr ',' '\n' | sort -g | tail -1; }

# The file is written from the compositor on its own schedule (barscan.c polls),
# so wait for the VALUE rather than sleeping a guessed number of seconds.
await() {  # await <shell-test> <description>
    i=0
    while [ $i -lt 80 ]; do
        eval "$1" && { ok "$2"; return 0; }
        sleep 0.1; i=$((i + 1))
    done
    bad "$2 — bar_ink=\"$(ink)\" bar_strip=\"$(strip)\""
    return 1
}

# ── 1. the row exists, and says "nothing covers the bar" ─────
#
# ⚠ PUBLISHED EVEN WHEN EVERY COLUMN IS -1, and the emptiness is the assertion.
# A bar reading a file with no row for its output cannot tell "nothing is under
# me" from "this synui does not measure that", and those want opposite
# behaviour — so the row is emitted unconditionally and -1 is a real answer.
await '[ -n "$(strip)" ]' "the bar strip row is published for this output"
await '[ "$(strip_max)" = "-1.00" ]' \
      "…and with no window on screen every column reads -1"

# The whole-screen grid, on the same terms and for the same reason. It is what
# the start menu, the bar's own menus, the mixer and the OSD fold — every
# surface whose position is not a constant, which is every surface but the bar.
await '[ -n "$(scene)" ]' "the scene grid is published for this output"
await '[ "$(scene_max)" = "-1.00" ]' \
      "…and with an empty screen every cell reads -1 too"

# The wallpaper half is untouched by any of this: a near-black picture still
# wants light ink, and that is what a bar with nothing under it draws.
await '[ "$(ink)" = light ]' "…and the wallpaper still answers light for the bar"

# ── 2. a window under the bar is what the strip reports ──────
"$CLIENT" 0 90 >>"$TMP/client.out" 2>>"$TMP/client.err" &
CLIENT_PIDS="$CLIENT_PIDS $!"
i=0
while [ $i -lt 40 ]; do
    [ "$(synctl clients | grep -c '"app_id":"stubborn"')" -ge 1 ] && break
    i=$((i + 1)); sleep 0.1
done
[ "$(synctl clients | grep -c '"app_id":"stubborn"')" -ge 1 ] \
    || fail "the client never mapped: $(cat "$TMP/client.err")"

# With no bar claiming an exclusive zone the window is laid out over the whole
# output, strip included, so every column should now read something.
#
# ⚠ NOT THE CLIENT'S WHITE. The top 34 logical rows of a decorated window are
# its TITLEBAR — a scene RECT synui drew itself, in synui's own colour — so what
# this proves is the cheap half of barscan.c: a window dragged under the bar by
# its titlebar is measured off a colour the compositor already knows, with no
# pixels read back at all. The client's own pixels get their turn below.
await '[ "$(strip_min)" != "-1.00" ]' \
      "a window over the strip is measured, on every column"

DECO=$(strip_max)
if awk -v v="$DECO" 'BEGIN{exit !(v > 0.05 && v < 0.9)}'; then
    ok "…as its titlebar, which is neither the wallpaper nor the client ($DECO)"
else
    bad "the strip read $DECO, which is the wallpaper or the client, not the chrome"
fi

# ── 3. …and it is NOT the wallpaper being re-read ────────────
#
# The assertion the whole file is for. The wallpaper under that white window is
# still near-black and still answers `light`; the strip answers white. If the
# scene walk found nothing and fell back, the strip would read the wallpaper's
# own 0.00-ish and this is the only check that would notice.
if [ "$(ink)" = light ]; then
    ok "…while the wallpaper's own answer is unchanged (light)"
else
    bad "the wallpaper's answer moved — expected light, got \"$(ink)\""
fi

if awk -v v="$DECO" -v w=0.01 'BEGIN{exit !(v > w + 0.05)}'; then
    ok "…so the two measurements disagree, which is the bug being fixed"
else
    bad "the strip ($DECO) is indistinguishable from the near-black wallpaper"
fi

# ── 3b. the CLIENT's own pixels, off the GPU ─────────────────
#
# The other half of barscan.c, and the half with the failure modes: a titlebar
# is a colour synui chose, but a client's surface is a buffer that has to be
# imported and read back — and gets declined outright on a format or a transform
# this cannot decode, which reads as -1 and is indistinguishable from "nothing
# there" unless something asserts the number.
#
# `decorations_toggle` is Super+Shift+D — it hides every titlebar, so the top of
# the window becomes the client's own surface and stubborn_client's opaque WHITE
# is what lands under the strip. Dispatched rather than configured: a synuirc
# key would need a reload to reach a window that is already mapped, and this is
# the path a user actually takes.
synctl dispatch decorations_toggle >/dev/null 2>&1
await '[ "$(strip_min)" != "-1.00" ] && \
       awk -v v="$(strip_min)" "BEGIN{exit !(v > 0.9)}"' \
      "an UNDECORATED window reports the client's own white pixels"

# ── 3c. the GRID says the same thing about the whole screen ──
#
# The strip is one row of the answer; this is all of it, and it is what a menu
# opened anywhere on the screen actually folds. The window covers the output, so
# every cell should read the client's white — and the same discriminating check
# applies: the WALLPAPER grid under it is still near-black, so a scan that fell
# back would publish ~0.00 here and pass nothing.
await '[ -n "$(scene_min)" ] && [ "$(scene_min)" != "-1.00" ] && \
       awk -v v="$(scene_min)" "BEGIN{exit !(v > 0.9)}"' \
      "the scene grid reads the client's white on every cell"

WP=$(wp_max)
if awk -v v="$WP" 'BEGIN{exit !(v < 0.05)}'; then
    ok "…while the wallpaper grid under it is still near-black ($WP)"
else
    bad "the wallpaper grid read $WP — it is not measuring the picture any more"
fi

# ── 3d. …and `scene_ink = off` puts it all back ──────────────
#
# ⚠ THE ASSERTION IS THAT THE ROWS SURVIVE AND GO EMPTY, not that they vanish.
# A reader cannot tell a missing row from a synui too old to publish one, and
# the two want opposite behaviour — so off is a row of -1, which is the same
# "the wallpaper answers here" an empty screen publishes. The window is still on
# screen for this check, which is what makes it about the switch rather than
# about the window.
printf 'scene_ink = off\n' >> "$CFG/synuirc"
synctl dispatch wallpaper_reload >/dev/null 2>&1
await '[ "$(scene_max)" = "-1.00" ] && [ "$(strip_max)" = "-1.00" ]' \
      "scene_ink = off empties both rows with the window still up"

# …and back on, so that "off" is proven to be the switch and not the reload.
sed -i 's/^scene_ink = off$/scene_ink = on/' "$CFG/synuirc"
synctl dispatch wallpaper_reload >/dev/null 2>&1
await '[ "$(scene_max)" != "-1.00" ] && [ "$(strip_max)" != "-1.00" ]' \
      "…and back on fills them again"

# ── 4. it goes back when the window does ─────────────────────
#
# Not a formality: the columns are re-cleared at the top of every scan, and a
# scan that only ever filled them would leave a bar inked for a window that has
# been closed for as long as the session lasts.
for p in $CLIENT_PIDS; do kill -TERM "$p" 2>/dev/null; done
CLIENT_PIDS=
await '[ "$(strip_max)" = "-1.00" ]' \
      "closing it puts every column back to -1"
await '[ "$(scene_max)" = "-1.00" ]' \
      "…and every cell of the grid with it"

echo
if [ "$fails" -eq 0 ]; then
    echo "bar_scene_strip: PASS"
    exit 0
fi
echo "bar_scene_strip: $fails check(s) failed"
exit 1
