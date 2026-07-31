#!/bin/sh
# alttab_scope.sh — does Alt+Tab reach every window, or only the visible ones?
#
# velle, 2026-07-31: "alt tab preview should include all open windows not just
# on screen ones". alttab_candidates() walked server_active_workspace()->windows
# and skipped `v->minimized`, so the switcher could only reach what was already
# in front of you. The window you actually wanted was as often on another
# virtual desktop or sitting minimized — reachable only if you already
# remembered where you had put it, which is the thing a switcher exists to save
# you from.
#
# Two halves, and the second is the one worth testing:
#
#   1. the LIST — every mapped window on every desktop, minimized ones included.
#   2. the COMMIT — neither kind can take focus where it stands, so landing on
#      one has to switch desktop / restore it. That happens once, when the cycle
#      ends, NOT as you tab past: three minimized windows passed on the way to a
#      fourth must not all come back, and a cycle that crosses two desktops must
#      not cross-fade the desk twice before you have chosen anything.
#
# Phase 3 is the whole point of the split and is what fails loudest if someone
# "simplifies" alttab_step() by making it reveal as it goes.
#
# A cycle really ends on an Alt RELEASE, which produces a modifiers event that
# only keyboard_handle_modifiers sees — and there is no way to synthesise one
# into a headless synui (no input devices on the headless backend, and uinput
# would be delivered to the live session). `alt_tab_commit` is that release,
# exposed as a dispatch so the cycle is drivable end to end. Steps go through
# the ordinary `alt_tab` action, so only the release is stood in for.
#
# Usage: alttab_scope.sh /path/to/synui /path/to/stubborn_client /path/to/synctl
# Skips (77) without a DRM render node, for the same reason smoke.sh does.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: alttab_scope.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
CLIENT=${2:?usage: alttab_scope.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}
SYNCTL=${3:?usage: alttab_scope.sh /path/to/synui /path/to/stubborn_client /path/to/synctl}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    for p in ${CLIENT_PIDS:-}; do kill -9 "$p" 2>/dev/null; done
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-alttab.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CLIENT_PIDS=

# Hermetic HOME, or the developer's own synuirc and windows.conf leak in — and
# windows.conf in particular would place these clients from a remembered box.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export SYNUI_WINDOWS="$TMP/windows.conf"
: > "$SYNUI_CONFIG"
: > "$SYNUI_WINDOWS"

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

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }

# One client per line, so the assertions can grep whole windows.
clients() { synctl clients | tr '{' '\n' | grep '"app_id"'; }
active_ws() { synctl activeworkspace | sed -n 's/.*"id":\([0-9]*\).*/\1/p'; }
focused()   { synctl activewindow | sed -n 's/.*"title":"\([^"]*\)".*/\1/p'; }

# stubborn_client takes (drop_configures, seconds). Titles are what we identify
# windows by — every one of them is app_id "stubborn", which is the point: a
# grid of identical app_ids is exactly the case the desktop marker exists for.
count_windows() { clients | grep -c '"app_id":"stubborn"'; }

open_window() {
    want=$1
    "$CLIENT" 0 60 >>"$TMP/client.out" 2>>"$TMP/client.err" &
    CLIENT_PIDS="$CLIENT_PIDS $!"
    i=0
    while [ $i -lt 40 ]; do
        [ "$(count_windows)" -ge "$want" ] && return 0
        i=$((i + 1)); sleep 0.1
    done
    fail "a client never mapped (wanted $want windows, have $(count_windows)):
       $(cat "$TMP/client.err")"
}

# The field for the currently focused window, read off synctl clients.
focused_field() {
    clients | grep '"focused":true' | sed -n "s/.*\"$1\":\\([^,}]*\\).*/\\1/p"
}

# ── 1. one window per desktop, plus a minimized one ──────────────────────
# Desktop 1: window A, then minimized. Desktop 2: window B. Desktop 3: window C,
# and we end sitting on 3, so both A and B are off-desktop and A is also
# minimized — every case in one arrangement.
open_window 1
synctl dispatch minimize_toggle >/dev/null
i=0
while [ $i -lt 40 ]; do
    clients | grep -q '"minimized":true' && break
    i=$((i + 1)); sleep 0.1
done
clients | grep -q '"minimized":true' || fail "minimize_toggle did not minimize the
       first window — the rest of this test has nothing minimized to find."

synctl dispatch ws 2 >/dev/null
[ "$(active_ws)" = 2 ] || fail "ws 2 did not switch, still on $(active_ws)"
open_window 2

synctl dispatch ws 3 >/dev/null
[ "$(active_ws)" = 3 ] || fail "ws 3 did not switch, still on $(active_ws)"
open_window 3

[ "$(count_windows)" = 3 ] || fail "expected 3 windows across 3 desktops, got $(count_windows)"
echo "setup:    3 windows — ws1 (minimized), ws2, ws3; sitting on ws3"

# ── 2. the cycle reaches a window on another desktop ─────────────────────
# One step back from the window we are on. The MRU order is C (here, focused),
# B (ws2, focused a moment ago), A (ws1, focused before that), so one step lands
# on B — a window on a desktop we are not looking at.
synctl dispatch alt_tab >/dev/null

# Mid-cycle: nothing may have moved yet. This is the half that a "reveal as you
# go" implementation gets wrong, and it fails RIGHT HERE rather than leaving a
# passing test and a desk that cross-fades under every keypress.
[ "$(active_ws)" = 3 ] || fail "stepping onto a window on desktop 2 switched the
       desk to $(active_ws) mid-cycle. The switch belongs in alttab_reveal(), at
       commit — tabbing PAST a window must not move you to it."

synctl dispatch alt_tab_commit >/dev/null
[ "$(active_ws)" = 2 ] || fail "committing on a window on desktop 2 left us on
       desktop $(active_ws). Either the window was never a candidate (the
       reported bug: alttab_candidates only walked the active workspace) or
       alttab_reveal did not switch to it."
[ "$(focused_field workspace)" = 2 ] || fail "landed on desktop 2 but the focused
       window is on desktop $(focused_field workspace)"
echo "ws2:      reached a window on another desktop, and only at commit"

# ── 3. tabbing PAST a minimized window does not restore it ───────────────
# From ws2 the MRU order is B (here), then C and A behind it. Two steps reach A,
# the minimized one, passing over C on the way — and nothing may be restored
# until the cycle ends.
synctl dispatch alt_tab >/dev/null
synctl dispatch alt_tab >/dev/null
clients | grep -q '"minimized":true' || fail "the minimized window was restored
       while merely being tabbed OVER. Restoring on each step means a cycle that
       passes three minimized windows brings all three back."

synctl dispatch alt_tab_commit >/dev/null
clients | grep -q '"minimized":true' && fail "committing on the minimized window
       left it minimized — focus went to a window that is not on screen, which
       is a window you cannot see eating your keystrokes. alttab_reveal must
       un-minimize it."
[ "$(active_ws)" = 1 ] || fail "the minimized window is on desktop 1; committing
       on it left us on desktop $(active_ws). Both halves of alttab_reveal have
       to run, and the desktop switch has to come FIRST — view_apply_minimized
       only shows a window whose workspace is visible."
[ "$(focused_field minimized)" = false ] || fail "the window we landed on still
       reports minimized:true"
echo "min:      restored a minimized window, and only at commit"

# ── 4. narrowing the list back is still possible ─────────────────────────
# alt_tab_all_desktops = off must put the cycle back where it was: from ws1,
# with only the (now restored) window A on it, there is nothing to switch to at
# all, so the cycle must not move us off desktop 1.
cat > "$SYNUI_CONFIG" <<EOF
alt_tab_all_desktops = off
EOF
# SIGHUP, not a dispatch: synui_config_reload() is wired to the signal and there
# is no reload action. It rebuilds the whole syn_config_t, so the knob lands.
kill -HUP "$SYNUI_PID" 2>/dev/null || fail "could not signal synui to reload"
i=0
while [ $i -lt 40 ]; do
    grep -q 'config reloaded' "$LOG" && break
    i=$((i + 1)); sleep 0.1
done
grep -q 'config reloaded' "$LOG" || fail "synui never logged a config reload"

synctl dispatch alt_tab >/dev/null
synctl dispatch alt_tab >/dev/null
synctl dispatch alt_tab_commit >/dev/null
[ "$(active_ws)" = 1 ] || fail "with alt_tab_all_desktops = off the cycle still
       reached desktop $(active_ws). The knob narrows alttab_candidates; off it
       must never leave the desktop you are on."
echo "off:      alt_tab_all_desktops = off keeps the cycle on this desktop"

# ── 5. clean shutdown ────────────────────────────────────────────────────
for p in $CLIENT_PIDS; do kill -TERM "$p" 2>/dev/null; done
CLIENT_PIDS=

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

echo "PASS: alttab_scope"
cleanup
exit 0
