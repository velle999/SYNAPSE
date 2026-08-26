#!/bin/sh
# display_scale.sh — one setting makes the WHOLE desktop bigger, and cannot
#                    make it unusable
#
# This is the accessibility control. velle, 2026-08-26: "it's for disabled, i
# need the entire os to scale together. however other systems handle it is
# fine." Other systems handle it with an output scale — GNOME's Display ▸
# Scale, macOS's Displays — and so does this: the compositor's own panels,
# every application and the cursor all grow together, drawn at the larger size
# rather than magnified.
#
# ⚠ THE MECHANISM WAS NEVER THE MISSING PART. wlroots has scaled since the
# port, output_persist.c has written a `scale` key the whole time, and
# wlr_fractional_scale_manager_v1 is created so 1.25 and 1.5 are crisp instead
# of drawn at 1x and stretched. What did not exist was any way to SET it: no
# config key, no panel row, no dispatch verb. So the checks here are about the
# CONTROL, not about wlroots.
#
# ⚠ AND IT IS NOT font.state's `scale`. That one sizes text inside the suite's
# own QML windows; it cannot reach a panel synui draws in cairo, and it cannot
# reach Firefox. Both settings are real and neither is the other's spelling —
# a test that confused them would pass while the desktop stayed small.
#
#   1. a scale applies, and the LOGICAL desktop shrinks by exactly that factor
#   2. it is written to outputs.conf, so it survives a login
#   3. it applies to EVERY screen, not the focused one
#   4. ⛔ it refuses a scale that would hide the panels used to undo it
#   5. the reset verb comes back to 100%
#
# Usage: display_scale.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, like every other rig here.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: display_scale.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: display_scale.sh /path/to/synui /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -30 "$LOG" >&2; cleanup; exit 1; }
cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-scale.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
mkdir -p "$TMP/home/.config/synui"
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/home/.config"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
# ⚠ TWO OUTPUTS. With one, "applies to every screen" and "applies to the
# focused screen" produce identical output and phase 3 would pass on a bug.
export WLR_HEADLESS_OUTPUTS=2
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY SYNUI_SOCKET

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

# name scale logical_w, one line per output.
outs() {
    synctl monitors | tr '{' '\n' |
        sed -n 's/.*"name":"\([^"]*\)".*"size":\[\([0-9]*\),\([0-9]*\)\].*"scale":\([0-9.]*\).*/\1 \4 \2 \3/p'
}
scale_of() { outs | awk -v n="$1" '$1 == n { print $2 }'; }
lw_of()    { outs | awk -v n="$1" '$1 == n { print $3 }'; }

# The headless outputs come up at 1280x720, which cannot hold the 1010x620 the
# settings panels want at anything above 1.0 — so give both a screen a real
# monitor's size before asking for a real scale.
for o in $(outs | awk '{print $1}'); do
    wlr-randr --output "$o" --custom-mode 2560x1440 >/dev/null 2>&1
done
sleep 1

[ "$(outs | wc -l)" = 2 ] || fail "expected 2 headless outputs, got:
$(synctl monitors)"
NAMES=$(outs | awk '{print $1}')
A=$(echo "$NAMES" | sed -n 1p)
B=$(echo "$NAMES" | sed -n 2p)
BASE_W=$(lw_of "$A")
[ "$BASE_W" -ge 2000 ] || fail "the outputs did not take the 2560x1440 mode
       ($A is ${BASE_W}px logical) — every scale below would be refused for the
       right reason and the rig would prove nothing."
echo "outputs:  $A and $B at ${BASE_W}px logical, scale $(scale_of "$A")"

# ── 1. it applies, and the logical desktop shrinks by the factor ─────────
synctl dispatch display_scale 2.0 >/dev/null
sleep 1
[ "$(scale_of "$A")" = "2.00" ] || fail "display_scale 2.0 left $A at
       $(scale_of "$A") — the verb did not take."
WANT=$((BASE_W / 2))
[ "$(lw_of "$A")" = "$WANT" ] || fail "$A reports scale 2.00 but a logical
       width of $(lw_of "$A"), not $WANT. The scale was recorded and the
       desktop was not actually resized — which is the shape of bug where the
       setting reads correct and nothing on screen changes."
echo "applied:  $A at $(scale_of "$A") — ${BASE_W} → $(lw_of "$A") logical"

# ── 2. EVERY screen, not the focused one ────────────────────────────────
[ "$(scale_of "$B")" = "2.00" ] || fail "$B is still at $(scale_of "$B") while
       $A scaled. 'Make the desktop bigger' is ONE intent: growing one monitor
       of two has not done it, it has made the desk inconsistent and left the
       user to find the other screen themselves."
echo "both:     $B at $(scale_of "$B") too"

# ── 3. it survives a login ──────────────────────────────────────────────
CONF="$TMP/home/.config/synui/outputs.conf"
[ -f "$CONF" ] || fail "no outputs.conf was written — the scale is lost at the
       next login, and an accessibility setting that has to be re-applied every
       morning is one that does not work."
grep -q "scale=2" "$CONF" || fail "outputs.conf does not record the scale:
$(grep '^output' "$CONF")"
echo "saved:    outputs.conf carries scale=2"

# ── 4. ⛔ the escape hatch cannot be closed ──────────────────────────────
# The Displays panel is 990px wide and the control panel 860, both laid out in
# columns at fixed offsets — so below about 1010 logical they do not clip, they
# put their values and their key legend off the screen. Those are exactly the
# panels somebody uses to undo a scale they regret, and the person most likely
# to reach for a large scale is the least able to read a tiny screen to escape
# it. A refusal has to SAY so rather than doing nothing.
synctl dispatch display_scale 3.0 >/dev/null
sleep 1
[ "$(scale_of "$A")" = "2.00" ] || fail "3.0 was accepted on a 2560-wide
       screen: that is 853px logical, narrower than the 1010 the settings
       panels need, so the panel that would undo it is off the screen."
grep -q "refusing scale 3.00" "$LOG" || fail "3.0 was refused but nothing said
       why. A key that silently stops working reads as broken — and this is the
       one the user reaches for when they cannot see."
echo "refused:  3.0 would leave 853px; the panels need 1010"

# ── 5. and there is a way back ──────────────────────────────────────────
# Super+Ctrl+0. Its own verb rather than "step down until it stops", because
# somebody who has scaled past what they can read needs ONE key that lands.
synctl dispatch display_scale 1.0 >/dev/null
sleep 1
[ "$(scale_of "$A")" = "1.00" ] && [ "$(scale_of "$B")" = "1.00" ] \
    || fail "reset left $A at $(scale_of "$A") and $B at $(scale_of "$B")."
[ "$(lw_of "$A")" = "$BASE_W" ] || fail "reset restored the scale but $A is
       ${BASE_W}px no longer — it reports $(lw_of "$A")."
echo "reset:    both back to 100%, $(lw_of "$A")px logical"

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer reported errors"
fi

cleanup
echo "display_scale: 5 phases passed"
