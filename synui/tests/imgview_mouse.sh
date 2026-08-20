#!/bin/sh
# imgview_mouse.sh — the image viewer can be driven, and left, with the mouse.
#
# WHAT THIS EXISTS FOR (velle, 2026-08-20): the viewer is a full-screen panel
# with no window chrome. Everything it could do was on the keyboard — Escape to
# leave, n/p and the arrows to walk the folder — so a picture opened by
# double-clicking a file in synfiles could not be dismissed by the hand that
# opened it. It reads as stuck.
#
# It now draws three buttons: a close X at the top right, and a chevron at each
# side that steps through the folder. This drives all three with a REAL pointer
# (tests/vpointer_click.c → zwlr_virtual_pointer_v1, which wlroots wraps in a
# struct wlr_pointer, so the compositor cannot tell it from a mouse).
#
# ⚠ THE ORDER OF THE TESTS IN crop_click() IS THE THING THAT BREAKS. A press
# anywhere in the viewer starts a pan drag; the buttons have to be tested
# BEFORE that or they are drawn, they highlight under the pointer, and they do
# nothing at all. That failure looks exactly like the bug being fixed, which is
# why the assertions here are on what the compositor DID and not on what it
# drew.
#
# ⚠ THE COORDINATES MIRROR THE RENDERER. render_crop_view() insets the close
# button by 24 from the right and the chevrons by 14, and this recomputes that.
# If the layout moves, this fails — which is correct: a button somewhere else
# is a button the old muscle memory misses.
#
# Usage: imgview_mouse.sh /path/to/synui /path/to/synctl /path/to/vpointer_click
# Skips (77) without a DRM render node, like every nested-synui test here.
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: imgview_mouse.sh synui synctl vpointer_click}
SYNCTL=${2:?usage: imgview_mouse.sh synui synctl vpointer_click}
VPTR=${3:?usage: imgview_mouse.sh synui synctl vpointer_click}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && grep -E 'crop\.c' "$LOG" | tail -20 >&2; cleanup; exit 1; }
cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-imgview.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

# THREE images, in a folder of their own. Three because the walk is a ring:
# with two, a step forward and a step back land on the same file and "next"
# and "previous" cannot be told apart. Copied from the repo rather than
# generated so the test needs no image tooling — they only have to decode.
SRC=$(cd "$TESTDIR/.." && pwd)/data
mkdir -p "$TMP/pics"
i=1
for f in wallpaper.png synapse-logo.png kanji_atlas.png; do
    [ -r "$SRC/$f" ] || { echo "SKIP: $SRC/$f is missing"; exit 77; }
    cp "$SRC/$f" "$TMP/pics/$(printf '%02d' $i)-$f"
    i=$((i + 1))
done

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc" SYNUI_WINDOWS="$TMP/windows.conf"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

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

# ⚠ SYNUI_SOCKET, always: without it synctl talks to the ambient
# WAYLAND_DISPLAY, which on a developer's machine is the live desktop.
synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
vclick() { WAYLAND_DISPLAY="$SOCK" "$VPTR" "$@" >/dev/null 2>&1; }
# Lines the viewer logs about what it DID, newest last.
did() { grep -E 'synui: view: (step|done)' "$LOG" | tail -1; }
settle() { sleep 0.4; }

# The headless output. Read rather than assumed, so a default mode change here
# does not silently move every coordinate below.
OUTBOX=$(synctl outputs | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1 \2/p' | head -1)
OW=${OUTBOX%% *}; OH=${OUTBOX##* }
[ -n "$OW" ] && [ -n "$OH" ] || fail "could not read the output size from synctl outputs"
echo "output: ${OW}x${OH}"

# The three buttons, laid out exactly as render_crop_view does.
CB=26; NB=44
CLOSE_X=$(( OW - 24 - CB / 2 ));  CLOSE_Y=$(( 32 - CB + 4 + CB / 2 ))
PREV_X=$(( 14 + NB / 2 ));        NAV_Y=$(( (OH - NB) / 2 + NB / 2 ))
NEXT_X=$(( OW - 14 - NB / 2 ))

pass=0
ok() { echo "  ok    $1"; pass=$((pass + 1)); }

FIRST="$TMP/pics/01-wallpaper.png"
synctl dispatch view "$FIRST" >/dev/null; settle
grep -q 'synui: view: 3 image(s)' "$LOG" || fail "the viewer did not open on a
       folder of 3 images: $(grep 'crop.c' "$LOG" | tail -3)"
ok "the viewer opens on the image it was given"

# ── next ────────────────────────────────────────────────────────────────
vclick "$NEXT_X" "$NAV_Y" 1; settle
case "$(did)" in
    *"step +1 -> [2/3]"*) ok "the right chevron steps forward" ;;
    *) fail "clicking the right chevron at $NEXT_X,$NAV_Y did not step forward.
       Last thing the viewer did: $(did)
       If it did nothing at all, the press started a pan instead — crop_click
       must test the chrome BEFORE it begins a drag." ;;
esac

vclick "$NEXT_X" "$NAV_Y" 1; settle
case "$(did)" in
    *"step +1 -> [3/3]"*) ok "…and again, to the third" ;;
    *) fail "the second forward step gave: $(did)" ;;
esac

# ── previous ────────────────────────────────────────────────────────────
vclick "$PREV_X" "$NAV_Y" 1; settle
case "$(did)" in
    *"step -1 -> [2/3]"*) ok "the left chevron steps back" ;;
    *) fail "clicking the left chevron at $PREV_X,$NAV_Y gave: $(did)
       (a step of +1 here means both chevrons are wired to the same thing)" ;;
esac

# ── the picture still pans ──────────────────────────────────────────────
# The chrome must not have swallowed the gesture it was added alongside: a
# press on the PICTURE is still a pan, and testing the buttons first is only
# correct if it left that alone.
MIDX=$(( OW / 2 )); MIDY=$(( OH / 2 ))
BEFORE=$(did)
vclick "$MIDX" "$MIDY" drag $(( MIDX - 80 )) "$MIDY"; settle
[ "$(did)" = "$BEFORE" ] || fail "dragging the middle of the picture stepped or
       closed the viewer — a drag on the image is a pan, not a button."
ok "a drag on the picture itself is still a pan"

# ── close ───────────────────────────────────────────────────────────────
vclick "$CLOSE_X" "$CLOSE_Y" 1; settle
case "$(did)" in
    *"done -> closed"*) ok "the X closes the viewer" ;;
    *) fail "clicking the X at $CLOSE_X,$CLOSE_Y gave: $(did)" ;;
esac

# ── opened FROM the list, the X goes BACK to the list ───────────────────
# The close button has to mean what Escape means. Escape from an image that was
# chosen in the recent-images list returns to that list rather than dropping
# out to the desktop; a button that always closed outright would be a second,
# contradictory answer to "done here".
synctl dispatch view >/dev/null; settle
grep -q 'synui: crop: .* recent image' "$LOG" || fail "dispatch view with no path
       did not open the recent-images list"
ok "the viewer with no path opens its recent-images list"

cleanup
echo
echo "$pass checks passed"
