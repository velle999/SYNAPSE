#!/bin/sh
# layout_persist.sh — is the layout you chose still there after a restart?
#
# velle, 2026-08-02: "desktop mode should survive sessions, i have to reset it
# back to floating after restart."
#
# Every desktop was seeded LAYOUT_TILING in server init and nothing ever read a
# saved value back, so the four-layout cycle was session scratch: a desk left on
# floating came back tiling at the next login, and the only way back was to walk
# Super+Tab round the cycle again — every restart, by hand.
#
# The whole test is the second boot. Everything before it exists to put
# something on disk worth reloading, so the phases are: change layouts, kill the
# compositor, start a NEW one on the same XDG_CONFIG_HOME, ask what it thinks
# its layouts are.
#
# Usage: layout_persist.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: layout_persist.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: layout_persist.sh /path/to/synui /path/to/synctl}

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

TMP=$(mktemp -d /tmp/synui-layoutpersist.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"

# XDG_CONFIG_HOME is the point of the test: layouts.state has to be written and
# read under it, not under the real ~/.config/synui. The state file is
# deliberately NOT seeded — an absent file must mean "never chosen".
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_CONFIG"
: > "$SYNUI_WINDOWS"

export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

STATE="$TMP/synui/layouts.state"

# Starts a compositor and leaves SOCK/CTLSOCK/SYNUI_PID pointing at it. Called
# twice: the whole test is "the second one agrees with the first".
start_synui() {
    "$SYNUI" -d >>"$LOG" 2>&1 &
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
}

stop_synui() {
    kill -TERM "$SYNUI_PID" 2>/dev/null
    i=0
    while kill -0 "$SYNUI_PID" 2>/dev/null; do
        i=$((i + 1))
        [ $i -gt 50 ] && fail "synui did not exit within 5s of SIGTERM"
        sleep 0.1
    done
    wait "$SYNUI_PID"; RC=$?
    [ $RC -eq 0 ] || fail "synui exited $RC"
    SYNUI_PID=
}

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

layout_now() { synctl activeworkspace | sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'; }
# The layout of desktop N, from the full workspace list rather than by switching
# to it — switching would be a second thing under test in a assertion about the
# first.
layout_of() {
    synctl workspaces | tr '{' '\n' | grep "\"id\":$1," |
        sed -n 's/.*"layout":"\([a-z]*\)".*/\1/p'
}

# ── 1. first boot: the default, and no state file ────────────────────────
start_synui
[ "$(layout_now)" = tiling ] || fail "expected tiling on a fresh config, got $(layout_now)"
[ -e "$STATE" ] && fail "layouts.state exists before anything was chosen ($STATE).
       An untouched config must stay untouched: the file is what distinguishes
       'never chose one' from 'chose tiling'."
echo "boot 1:   tiling by default, nothing on disk"

# ── 2. choose floating on desktop 1 — the reported case ──────────────────
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = floating ] || fail "one layout_cycle from tiling should be
       floating, got $(layout_now)"
[ -s "$STATE" ] || fail "choosing a layout wrote no layouts.state — nothing can
       survive a restart that was never written down"
echo "boot 1:   desktop 1 → floating, state written"

# ── 3. and monocle on desktop 3, so it isn't just "desktop 1 works" ──────
# Two cycles from tiling: floating, monocle.
synctl dispatch ws 3 >/dev/null
synctl dispatch layout_cycle >/dev/null
synctl dispatch layout_cycle >/dev/null
[ "$(layout_now)" = monocle ] || fail "two cycles on desktop 3 should be monocle,
       got $(layout_now)"
synctl dispatch ws 1 >/dev/null
echo "boot 1:   desktop 3 → monocle"

# ── 4. the restart ───────────────────────────────────────────────────────
stop_synui
start_synui

[ "$(layout_now)" = floating ] || fail "desktop 1 came back as $(layout_now), not
       floating. This is the report: the layout is a per-desktop SETTING, and a
       restart must not silently re-seed it to tiling."
[ "$(layout_of 3)" = monocle ] || fail "desktop 3 came back as $(layout_of 3),
       not monocle — the file is per-desktop, so restoring must be too."
[ "$(layout_of 2)" = tiling ] || fail "desktop 2 was never touched and came back
       as $(layout_of 2); an unchosen desktop keeps the default."
echo "boot 2:   floating / tiling / monocle, as left"

# ── 5. an unreadable value keeps the default, it does not crash ──────────
stop_synui
printf 'desktop1=hexagonal\ndesktop2=\ndesktop9=floating\n' > "$STATE"
start_synui
[ "$(layout_now)" = tiling ] || fail "a state file naming a layout this build does
       not have left desktop 1 on $(layout_now); an unknown value must fall back
       to the default, not to whatever the enum's neighbour happens to be."
[ "$(layout_of 9)" = floating ] || fail "a bad line stopped the rest of the file
       being read: desktop 9 is $(layout_of 9), not floating."
echo "boot 3:   junk lines skipped, the good line still read"

stop_synui
echo "PASS: layout_persist"
cleanup
exit 0
