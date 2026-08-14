#!/usr/bin/env bash
# theme_seat_guard.sh — a synui with no seat must not re-theme the real desktop
#
# theme_apply() runs at EVERY startup and ends by shelling out to
# synui-apply-theme, which writes `$HOME/.config/...` literally plus gsettings
# and kwriteconfig over the session bus. None of that is isolated by
# XDG_CONFIG_HOME or by a private HOME — dconf writes travel over the session
# bus to a daemon with its OWN environment — so every headless instance was
# re-theming the desktop it was launched from, with the default dark preset.
#
# The visible symptom is Firefox: its System theme follows the portal's
# colour-scheme signal, so the chrome flips to dark grey and stays there until
# somebody re-applies the theme by hand. `meson test` starts about twenty
# compositors in a burst, so running the suite recoloured velle's desktop —
# twice, months apart, and the second time while fixing something else.
#
# The stub below is both the assertion and the seatbelt: if the guard regresses,
# the recorder catches the call instead of the live session.
#
# Judged on two signals that a broken guard cannot both fake:
#   - the recorder file must stay EMPTY (nothing was pushed), and
#   - the log must carry "no seat" (the guard RAN, rather than the push simply
#     never being reached because startup died early).
#
# Usage: theme_seat_guard.sh /path/to/synui
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

SYNUI=${1:?usage: theme_seat_guard.sh /path/to/synui}

fails=0
fail() { echo "  FAIL $*"; fails=$((fails + 1)); }
pass() { echo "  ok   $*"; }

# This rig runs INSIDE velle's session; leaving these set points its tooling at
# the live compositor. Same precaution as saver_keys.sh.
unset SYNUI_SOCKET WAYLAND_DISPLAY

TMP=$(mktemp -d /tmp/synui-seatguard.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CALLS="$TMP/calls"
: > "$CALLS"

# A nested synui runs its own idle chain, and SUSPEND is not per-compositor —
# power.c hands it to logind, which is system-wide. An empty rc means the
# DEFAULTS apply and the machine goes to S3 in 30 seconds flat. Push every
# stage out; power_enabled alone is not enough, blanking still fires.
cat > "$TMP/synuirc" <<'RC'
power_enabled = 0
power_dim_timeout = 86400
power_blank_timeout = 86400
power_lock_timeout = 86400
power_suspend_timeout = 86400
RC

# The helpers this test is about: record the call and do nothing.
mkdir -p "$TMP/bin"
for h in synui-apply-theme synui-glass synui-apply-font; do
    cat > "$TMP/bin/$h" <<RECORD
#!/bin/sh
echo "$h \$*" >> "$CALLS"
exit 0
RECORD
    chmod +x "$TMP/bin/$h"
done

export PATH="$TMP/bin:$PATH"
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1

cleanup() {
    [ -n "${PID:-}" ] && kill -9 "$PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

"$SYNUI" >"$LOG" 2>&1 &
PID=$!

# Startup is what applies the theme, so wait for the socket and then a moment
# for the spawn that would have followed it.
SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$PID" 2>/dev/null || { echo "ABORT synui died during startup:"; tail -5 "$LOG"; exit 1; }
    sleep 0.1
    i=$((i + 1))
done
[ -n "$SOCK" ] || { echo "ABORT no Wayland socket within 10s"; exit 1; }
sleep 1.5

echo "== a headless synui keeps its hands off the desktop =="

if [ -s "$CALLS" ]; then
    fail "a helper was called from a synui with no seat:"
    sed 's/^/         /' "$CALLS"
else
    pass "synui-apply-theme / -glass / -apply-font were not called"
fi

# The guard ran, rather than startup never getting that far. Without this a
# compositor that died before theme_apply() would look like a pass.
if grep -q "no seat (headless/nested)" "$LOG"; then
    pass "…and said why (the guard ran)"
else
    fail "no 'no seat' line — the push was skipped for some OTHER reason"
fi

# The compositor's OWN chrome must still be themed: the guard is about what
# leaves the process, not about leaving this instance unstyled.
if grep -q "theme applied" "$LOG"; then
    pass "…while still theming its own chrome"
else
    fail "no 'theme applied' line — the guard took the whole theme with it"
fi

kill -TERM "$PID" 2>/dev/null
wait "$PID" 2>/dev/null

if [ $fails -eq 0 ]; then
    echo "all checks passed"
    exit 0
fi
echo "$fails check(s) failed"
exit 1
