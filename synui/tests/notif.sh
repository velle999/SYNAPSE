#!/bin/sh
# notif.sh — notification text, against a real compositor.
#
# A notification's summary and body are UNTRUSTED INPUT. They arrive over
# org.freedesktop.Notifications from any client on the session bus, and synui
# draws them itself (notif.c posts, render.c lays them out), so a wrapper that
# loops forever or runs off the end of a buffer on a hostile string takes the
# whole desktop with it.
#
# The layout used to be `body[0] ? 82 : 58` pixels with the summary and the body
# each drawn through draw_clipped() — one line apiece, ellipsised at the card's
# width. Every notification longer than about forty characters was cut off, and
# that is nearly all of them: a download finishing, a KDE Connect message, a
# synguard verdict, anything with a filename in it. velle reported it as "I keep
# getting cropped notifications". The body wraps now and the card grows to fit,
# which means the height is MEASURED, which means the measuring path is on the
# critical path for every toast.
#
# So this feeds it the strings that break wrappers:
#
#   - a 400-character "word" with no space in it. Greedy wrapping has nowhere to
#     break, so it must break mid-word or spend its whole line budget on one
#     token and still overflow. URLs and long filenames are the real case.
#   - a wall of CJK. No spaces either, AND every glyph goes out through the
#     fontconfig fallback in text.c rather than the panel face.
#   - several hundred short words, far past the line cap, so the truncation path
#     runs with plenty of text left over.
#   - empty summary, empty body, empty app: all legal over the protocol.
#   - a lone space, which is the case where "skip leading spaces" can leave
#     nothing to measure.
#
# The assertion is that the compositor is STILL ALIVE afterwards and its log is
# clean. Under ASan (the debug build meson uses for the headless scripts) a
# buffer mistake here is an abort with a report rather than a slow corruption.
#
# A PRIVATE session bus is not optional: without it notify-send reaches the
# developer's LIVE desktop, whose synui already owns the notification name, and
# the nested compositor under test never sees a thing — the test would pass by
# doing nothing. (Hit while writing this, 2026-08-07: three test toasts went to
# velle's real screen.)
#
# Usage: notif.sh /path/to/synui
# Skips (77) without a DRM render node, dbus-run-session, or notify-send.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -u

TESTDIR=$(dirname "$0")
SYNUI=${1:?usage: notif.sh /path/to/synui}

fail() { echo "FAIL: $*" >&2; [ -n "${TMP:-}" ] && tail -40 "$TMP/log" >&2; exit 1; }

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
command -v dbus-run-session >/dev/null 2>&1 || {
    echo "SKIP: dbus-run-session not installed (dbus)"; exit 77; }
command -v notify-send >/dev/null 2>&1 || {
    echo "SKIP: notify-send not installed (libnotify)"; exit 77; }
command -v python3 >/dev/null 2>&1 || {
    echo "SKIP: python3 not installed"; exit 77; }

TMP=$(mktemp -d /tmp/synui-notif.XXXXXX) || exit 1
chmod 700 "$TMP"
trap 'rm -rf "$TMP"' INT TERM EXIT

# Hermetic HOME, or the developer's own synuirc leaks in.
export TMP SYNUI
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc" SYNUI_WINDOWS="$TMP/windows.conf"
# The welcome menu and the boot overlay both cover the screen and neither has
# anything to do with this.
printf 'welcome_at_startup = off\nstart_overlay = off\n' > "$SYNUI_CONFIG"
: > "$SYNUI_WINDOWS"

export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

dbus-run-session -- sh -s <<'INNER'
set -u

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
    kill -0 "$PID" 2>/dev/null || { echo "FAIL: synui exited during startup" >&2
                                    tail -40 "$TMP/log" >&2; exit 1; }
    i=$((i + 1)); sleep 0.1
done
[ -n "$SOCK" ] || { echo "FAIL: no wayland socket after 10s" >&2; exit 1; }
export WAYLAND_DISPLAY="$SOCK"
sleep 0.5

URL=$(python3 -c 'print("https://example.org/" + "a"*400)')
CJK=$(python3 -c 'print("你好世界"*120)')
WORDS=$(python3 -c 'print("word "*300)')
BLOCKS=$(python3 -c 'print("█"*80)')

# One unbroken 420-character token, in every field at once.
notify-send -a "$BLOCKS" "$URL" "$URL"
# No spaces AND the fontconfig fallback on every glyph.
notify-send -a cjk "$CJK" "$CJK"
# The ordinary greedy path, run far past the line cap.
notify-send -a words "$WORDS" "$WORDS"
# Empty is legal over the protocol.
notify-send -a "" "" ""
# Nothing but whitespace: "skip leading spaces" can leave zero to measure.
notify-send -a sp " " "  "
# And one critical, which takes the other accent branch.
notify-send -a synguard -u critical "$WORDS" "$URL"

sleep 1.5

kill -0 "$PID" 2>/dev/null || { echo "FAIL: synui died laying out a notification" >&2
                                tail -40 "$TMP/log" >&2; exit 1; }

if grep -qiE "AddressSanitizer|SEGV|Assertion|runtime error" "$TMP/log"; then
    echo "FAIL: sanitizer or assertion in the log" >&2
    grep -iE -A 20 "AddressSanitizer|SEGV|Assertion|runtime error" "$TMP/log" >&2
    exit 1
fi

# Clean shutdown, so a leak in the layout path is reported rather than skipped.
kill -TERM "$PID" 2>/dev/null
i=0
while kill -0 "$PID" 2>/dev/null; do
    i=$((i + 1))
    [ $i -gt 50 ] && { echo "FAIL: synui did not exit within 5s of SIGTERM" >&2; exit 1; }
    sleep 0.1
done
wait "$PID"; RC=$?
[ $RC -eq 0 ] || { echo "FAIL: synui exited $RC" >&2; tail -40 "$TMP/log" >&2; exit 1; }

echo "notif:    six hostile notifications laid out, no crash, clean exit"
INNER
rc=$?
[ $rc -eq 0 ] || exit $rc

echo "all checks passed"
