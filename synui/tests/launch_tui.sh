#!/usr/bin/env bash
#
# launch_tui.sh — omarchy-launch-or-focus-tui, decided against stubs.
#
# ⚠ THE NAME IS HARDCODED IN SOMEBODY ELSE'S QML, which is what makes this worth
# a test of its own. Omarchy's terminal-game plugins call
# `bar.run("omarchy-launch-or-focus-tui --app-id=… " + path)` from inside their
# panels — no configuration, no fallback — so the command's behaviour is a
# contract with software this repository does not control and cannot fix.
#
# ⛔ AND THE FAILURE IS SILENT AT BOTH ENDS. `bar.run` detaches: a launcher that
# picks the wrong branch, dies, or forwards a flag syntty does not know reports
# nothing to the widget that called it. What the user sees is a Play button that
# does nothing — the same symptom as the missing host member this shipped
# alongside, and a different cause.
#
# Stubs on PATH rather than a real desktop: the whole decision is "what did
# synctl print", and running it for real would mean opening a game window on the
# seat the developer is sitting at.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
SHIM=${1:-$HERE/../systemd/omarchy-launch-or-focus-tui.sh}
[ -x "$SHIM" ] || { echo "not executable: $SHIM" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 not installed."; exit 77; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0 fails=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fails=$((fails + 1)); }
check() { # check <what> <want> <got>
    [ "$2" = "$3" ] && ok "$1" || bad "$1 (want '$2', got '$3')"
}

mkdir -p "$TMP/bin"
cat > "$TMP/bin/synctl" <<'STUB'
#!/bin/sh
if [ "$1" = "clients" ]; then cat "$STUB_CLIENTS"; exit 0; fi
echo "synctl $*" >> "$STUB_LOG"
exit 0
STUB
cat > "$TMP/bin/syntty" <<'STUB'
#!/bin/sh
echo "syntty $*" >> "$STUB_LOG"
exit 0
STUB
chmod +x "$TMP/bin/synctl" "$TMP/bin/syntty"
export PATH="$TMP/bin:$PATH" STUB_LOG="$TMP/log"

run() { # run <clients-json> <args…>
    : > "$STUB_LOG"
    STUB_CLIENTS="$TMP/clients.json"
    printf '%s' "$1" > "$STUB_CLIENTS"
    shift
    STUB_CLIENTS="$TMP/clients.json" bash "$SHIM" "$@" >/dev/null 2>&1
    cat "$STUB_LOG"
}

echo "omarchy-launch-or-focus-tui — $SHIM"

NONE='[{"app_id":"syntty","title":"a terminal"}]'
UP='[{"app_id":"org.omarchy.tetris","title":"TETRIS"}]'

# ⚠ --app-id REACHES syntty, and this is the assertion that matters most. The
# flag was added to syntty for this caller; without it every terminal on the
# desk answers to the same app_id and the focus branch below can never be right.
check "not running: launched under its own app-id" \
      "syntty --app-id=org.omarchy.tetris -e /games/tetris" \
      "$(run "$NONE" --app-id=org.omarchy.tetris /games/tetris)"

check "already running: focused, not launched again" \
      "synctl dispatch focus_app org.omarchy.tetris" \
      "$(run "$UP" --app-id=org.omarchy.tetris /games/tetris)"

# ⛔ MATCHED ON app_id, NEVER ON THE WHOLE LINE. A window title is user text and
# can contain anything, including the id being searched for — a grep would focus
# a text editor with the plugin's name in its title bar instead of starting the
# game.
check "a title containing the id is not a match" \
      "syntty --app-id=org.omarchy.tetris -e /games/tetris" \
      "$(run '[{"app_id":"syntty","title":"org.omarchy.tetris notes"}]' \
             --app-id=org.omarchy.tetris /games/tetris)"

check "nothing to distinguish it by: just launch" \
      "syntty -e htop" \
      "$(run "$NONE" htop)"

# ⛔ --title IS ACCEPTED AND MUST NOT BE FORWARDED. syntty has no such flag and
# its parser DIES on an unknown option, so passing one through would turn a
# cosmetic request into a game that never starts.
check "a --title is swallowed rather than forwarded" \
      "syntty --app-id=org.x -e /games/x" \
      "$(run "$NONE" --app-id=org.x --title='Some Game' /games/x)"

check "arguments after the command are the child's" \
      "syntty --app-id=org.x -e /games/x --level 3" \
      "$(run "$NONE" --app-id=org.x /games/x --level 3)"

# A compositor that answers with nothing parseable is a compositor that has not
# told us the window is up — so start one, rather than failing the launch.
check "unreadable client list still launches" \
      "syntty --app-id=org.x -e /games/x" \
      "$(run 'not json at all' --app-id=org.x /games/x)"

bash "$SHIM" >/dev/null 2>&1
check "no command at all is a usage error" "2" "$?"

printf '\n  %d passed, %d failed\n' "$pass" "$fails"
[ "$fails" = 0 ] || exit 1
echo "launch_tui: PASS"
