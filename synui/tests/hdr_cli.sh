#!/bin/sh
# hdr_cli.sh — `synctl hdr` answers whether it WORKED, not whether it parsed
#
# ⛔ THE FAILURE THIS PINS IS A SUCCESSFUL-LOOKING NO-OP. Enabling HDR10 fails
# for reasons that have nothing to do with the request being well formed — a
# connector that will not carry PQ, a link without the bandwidth for it, a mode
# that has no headroom — and a caller reading this socket is not reading the
# journal. `synctl dispatch` shipped for a long time answering {"ok":true} to
# actions that did not exist, for exactly this reason: the honest answer is
# harder and it is the only one a script can act on.
#
# A headless output is the ideal subject, and not a compromise one: it CANNOT be
# driven in HDR10 — there is no connector, no EDID and no CRTC — so every enable
# here must be refused. That makes this rig a check on the refusal path, which
# is the path a real desk hits on two monitors out of three.
#
# Usage: hdr_cli.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, like every other rig here: synui renders
# through scenefx's fx_renderer, which is GLES2/DMA-BUF only.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: hdr_cli.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: hdr_cli.sh /path/to/synui /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-hdrcli.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

# ⛔ A PRIVATE HOME AND RUNTIME DIR, OR THIS DRIVES THE LIVE DESKTOP. synctl
# finds the compositor through SYNUI_SOCKET or $XDG_RUNTIME_DIR, and outputs.conf
# is written under $XDG_CONFIG_HOME — an `hdr on` that escaped to the real
# session would change what a real monitor is doing.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_OUTPUTS="$TMP/outputs.conf"
printf 'animation_ms = 0\n' > "$SYNUI_CONFIG"

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
CTLSOCK="$TMP/synui-$SOCK.sock"

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

pass=0
ok()   { echo "  ok    $1"; pass=$((pass + 1)); }

OUT=$(synctl outputs | sed -n 's/.*"name":"\([^"]*\)".*/\1/p' | head -1)
[ -n "$OUT" ] || fail "no outputs from synctl outputs"
echo "output:   $OUT"

# ── the report still reports, and carries the new columns ─────────────────
# ⛔ NOTHING IT DOES CHANGES THE SCREEN. Every question is
# wlr_output_test_state(), which the atomic backend validates with TEST_ONLY.
REPORT=$(synctl hdr)
case "$REPORT" in
    *"$OUT"*) ok "synctl hdr names $OUT" ;;
    *) fail "synctl hdr did not mention $OUT: $REPORT" ;;
esac
for col in pq bt2020 image_description tf_curve matrix pipeline pq_lut sdr primaries capable on; do
    case "$REPORT" in
        *"$col="*) ;;
        *) fail "synctl hdr lost the $col= column: $REPORT" ;;
    esac
done
ok "every probe column is present"

# A headless output has no connector to signal anything to.
case "$REPORT" in
    *capable=no*) ok "a headless output reports capable=no" ;;
    *) fail "a headless output claimed it could be driven in HDR10: $REPORT" ;;
esac
case "$REPORT" in
    *on=no*) ok "and is not being driven in HDR10" ;;
    *) fail "a headless output claims it IS in HDR10: $REPORT" ;;
esac

# ── the verbs answer honestly ─────────────────────────────────────────────
R=$(synctl hdr on "$OUT")
case "$R" in
    *'"error"'*) ok "hdr on an incapable output is an error, not {\"ok\":true}" ;;
    *) fail "hdr on a headless output answered: $R" ;;
esac

# ⚠ Off is NOT symmetrical with on, and must not be made so. An output that is
# already SDR is in the state that was asked for, so saying so is the truth —
# and a script that turns HDR off on every monitor at shutdown must not have to
# know which ones ever had it.
R=$(synctl hdr off "$OUT")
case "$R" in
    *'"ok":true'*) ok "hdr off an SDR output succeeds — it is already there" ;;
    *) fail "hdr off a headless output answered: $R" ;;
esac

R=$(synctl hdr on NOPE-9)
case "$R" in
    *'"error"'*'no such output'*) ok "an unknown output is named, not ignored" ;;
    *) fail "hdr on an unknown output answered: $R" ;;
esac

R=$(synctl hdr sideways "$OUT")
case "$R" in
    *'"error"'*) ok "an unknown verb is refused" ;;
    *) fail "hdr <nonsense> answered: $R" ;;
esac

R=$(synctl hdr on)
case "$R" in
    *'"error"'*) ok "a missing output name is refused" ;;
    *) fail "hdr on with no output answered: $R" ;;
esac

# ── the white level clamps, and says where it landed ──────────────────────
# ⚠ Settable with HDR OFF, on purpose: it is where SDR white will go when the
# mode is turned on, so a person who sets it first and enables second must not
# find their setting quietly dropped.
R=$(synctl hdr white "$OUT" 250)
case "$R" in
    *'"white":250'*) ok "hdr white 250 is taken while the output is SDR" ;;
    *) fail "hdr white 250 answered: $R" ;;
esac

R=$(synctl hdr white "$OUT" 99999)
case "$R" in
    *'"white":600'*) ok "an absurd level clamps to the ceiling and says so" ;;
    *) fail "hdr white 99999 answered: $R" ;;
esac

R=$(synctl hdr white "$OUT" 1)
case "$R" in
    *'"white":80'*) ok "and below the floor clamps to it" ;;
    *) fail "hdr white 1 answered: $R" ;;
esac

R=$(synctl hdr white "$OUT")
case "$R" in
    *'"error"'*) ok "hdr white with no level is refused" ;;
    *) fail "hdr white with no level answered: $R" ;;
esac

# ── and it is written down ────────────────────────────────────────────────
# The panel saves through the same output_persist_save the CLI calls, so a level
# set over the socket has to survive a restart exactly as one set with the
# bracket keys does.
grep -q "^output $OUT .*hdr=0 hdr_white=80" "$SYNUI_OUTPUTS" 2>/dev/null \
    || fail "outputs.conf did not record hdr=0 hdr_white=80 for $OUT:
$(cat "$SYNUI_OUTPUTS" 2>/dev/null)"
ok "outputs.conf records the mode and the level"

# ⛔ AND THE COMPOSITOR IS STILL ALIVE. Every refusal above went through
# wlr_output_test_state on a backend with no CRTC at all; a null deref in that
# path would have shown up as a passing test with a dead session behind it.
kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died during the HDR probes"
ok "synui survived every probe and refusal"

echo "hdr_cli: $pass check(s) passed"
cleanup
exit 0
