#!/bin/sh
# dnd.sh — Do Not Disturb, against a real compositor.
#
# The mode has to do three separate things, and each of them is a different way
# to get it wrong:
#
#   1. Draw nothing. A card left on screen is the interruption being switched
#      off.
#   2. Make no sound. Suppressing only the draw leaves the chime, which is the
#      half people actually notice — and it is the half a test that only looked
#      at pixels would never catch. Both are covered here by asserting on the
#      ONE branch that gates them: a swallowed notification never reaches the
#      card allocation OR sound_play().
#   3. Let CRITICAL through anyway. synguard posts its intrusion alerts at that
#      urgency, and a quiet mode that can hide a security alert is a bug wearing
#      a feature's name. This is the assertion that matters most, because it is
#      the one whose absence is invisible until it matters.
#
# And a fourth thing that is not about notifications at all: the choice must
# SURVIVE. dnd.state is read from synui_config_load()'s tail rather than once at
# startup, because a config reload does `s->config = fresh` — filters.state and
# theme.state each shipped with that bug (Ctrl+Shift+R switched CRT effects back
# on), and here it would mean the desktop quietly starting to make noise again
# in the middle of whatever DND was switched on for. The reload assertion below
# is the guard against that regression.
#
# A PRIVATE session bus is not optional, for the reason notif.sh gives at
# length: without it notify-send reaches the developer's LIVE desktop, whose
# synui already owns org.freedesktop.Notifications, and the nested compositor
# under test never sees a thing — the test would pass by doing nothing.
#
# Usage: dnd.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node, dbus-run-session, or notify-send.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -u

SYNUI=${1:?usage: dnd.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: dnd.sh /path/to/synui /path/to/synctl}
TESTDIR=$(dirname "$0")

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
command -v dbus-run-session >/dev/null 2>&1 || {
    echo "SKIP: dbus-run-session not installed (dbus)"; exit 77; }
command -v notify-send >/dev/null 2>&1 || {
    echo "SKIP: notify-send not installed (libnotify)"; exit 77; }

TMP=$(mktemp -d /tmp/synui-dnd.XXXXXX) || exit 1
chmod 700 "$TMP"
trap 'rm -rf "$TMP"' INT TERM EXIT

# Hermetic HOME, or the developer's own synuirc leaks in — AND dnd.state would
# be written into it, leaving the real desktop silenced by a test run.
export TMP SYNUI SYNCTL
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc" SYNUI_WINDOWS="$TMP/windows.conf"
printf 'welcome_at_startup = off\nstart_overlay = off\n' > "$SYNUI_CONFIG"
: > "$SYNUI_WINDOWS"

export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

dbus-run-session -- sh -s <<'INNER'
set -u

fail() { echo "FAIL: $*" >&2; tail -60 "$TMP/log" >&2; exit 1; }

"$SYNUI" -d > "$TMP/log" 2>&1 &
PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    for c in "$TMP"/wayland-*; do
        case "$c" in *.lock) continue;; esac
        [ -S "$c" ] && SOCK=$(basename "$c") && break
    done
    [ -n "$SOCK" ] && break
    kill -0 "$PID" 2>/dev/null || fail "synui exited during startup"
    i=$((i + 1)); sleep 0.1
done
[ -n "$SOCK" ] || fail "no wayland socket after 10s"
export WAYLAND_DISPLAY="$SOCK"

# The NESTED compositor's control socket. Without SYNUI_SOCKET, synctl finds the
# developer's live desktop and every dispatch below lands there instead.
CTLSOCK="$TMP/synui-$SOCK.sock"
synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
sleep 0.5

swallowed() { grep -c "dnd swallowed" "$TMP/log"; }

# ── DND off: a notification is posted ───────────────────────────────────────
notify-send -a before "quiet-test-before" "body"
sleep 0.4
grep -q "notif: #.* from before" "$TMP/log" \
    || fail "a notification was not posted with DND off"
[ "$(swallowed)" = 0 ] || fail "something was swallowed before DND was on"

# ── Switch it on ────────────────────────────────────────────────────────────
synctl dispatch dnd >/dev/null
sleep 0.4
[ -f "$TMP/synui/dnd.state" ] || fail "dnd.state was not written"
grep -q '^dnd=1' "$TMP/synui/dnd.state" || fail "dnd.state does not record the mode"

# ── DND on: an ordinary notification is swallowed ───────────────────────────
notify-send -a during "quiet-test-during" "body"
sleep 0.4
grep -q "dnd swallowed 'quiet-test-during'" "$TMP/log" \
    || fail "an ordinary notification was NOT swallowed while DND was on"
grep -q "notif: #.* from during" "$TMP/log" \
    && fail "a swallowed notification was posted anyway"

# ── …but CRITICAL still gets through ────────────────────────────────────────
# The assertion this whole file exists for. synguard's intrusion alerts are
# posted at critical urgency; if DND could eat them the mode would be a way to
# silence a security warning.
notify-send -a synguard -u critical "quiet-test-critical" "body"
sleep 0.4
grep -q "notif: #.* from synguard" "$TMP/log" \
    || fail "a CRITICAL notification was swallowed by DND"

# ── A CONFIG RELOAD MUST NOT SWITCH THE RINGER BACK ON ──────────────────────
# synui_config_reload() does `s->config = fresh`. dnd.state is read from
# synui_config_load()'s tail precisely so the mode survives this; read only at
# startup it would not, and nothing on screen would say the desktop had started
# making noise again. Same trap filters.state and theme.state each shipped with.
# `wallpaper_reload` IS the reload action — it calls synui_config_reload()
# outright (input.c). The name is historical; the reload is the whole thing.
synctl dispatch wallpaper_reload >/dev/null
sleep 0.6
notify-send -a afterreload "quiet-test-reload" "body"
sleep 0.4
grep -q "dnd swallowed 'quiet-test-reload'" "$TMP/log" \
    || fail "a config reload turned Do Not Disturb OFF"

# ── Switch it off again ─────────────────────────────────────────────────────
synctl dispatch dnd >/dev/null
sleep 0.4
grep -q '^dnd=0' "$TMP/synui/dnd.state" || fail "dnd.state was not cleared"

notify-send -a after "quiet-test-after" "body"
sleep 0.4
grep -q "notif: #.* from after" "$TMP/log" \
    || fail "notifications did not come back after DND was switched off"

# ── Still alive and clean ───────────────────────────────────────────────────
kill -0 "$PID" 2>/dev/null || fail "synui died toggling Do Not Disturb"

if grep -qiE "AddressSanitizer|SEGV|Assertion|runtime error" "$TMP/log"; then
    echo "FAIL: sanitizer or assertion in the log" >&2
    grep -iE -A 20 "AddressSanitizer|SEGV|Assertion|runtime error" "$TMP/log" >&2
    exit 1
fi

kill -TERM "$PID" 2>/dev/null
i=0
while kill -0 "$PID" 2>/dev/null; do
    i=$((i + 1))
    [ $i -gt 50 ] && { echo "FAIL: synui did not exit within 5s of SIGTERM" >&2; exit 1; }
    sleep 0.1
done
wait "$PID"
rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL: synui exited $rc" >&2; tail -40 "$TMP/log" >&2; exit 1; }

if grep -qiE "AddressSanitizer|LeakSanitizer" "$TMP/log"; then
    echo "FAIL: sanitizer report on shutdown" >&2
    grep -iE -A 20 "AddressSanitizer|LeakSanitizer" "$TMP/log" >&2
    exit 1
fi

echo "PASS: do not disturb hides and mutes, lets critical through, survives a reload"
INNER
