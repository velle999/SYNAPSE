#!/usr/bin/env bash
#
# omarchy-launch-or-focus-tui — start a terminal application, or raise the one
# already running.
#
# ⚠ THIS NAME IS NOT OURS AND THAT IS THE POINT. It is the launcher Omarchy's
# plugins call by name from inside their QML, hardcoded, with no way to
# configure it — terminal.tetris's Start button is literally
#
#     bar.run("omarchy-launch-or-focus-tui --app-id=org.omarchy.tetris " + path)
#
# so on a desktop where that command does not exist, Start opens a shell that
# exits immediately and the game never appears. A widget whose whole purpose is
# a game had a menu that worked and a Play that did nothing. Providing the
# command is the entire fix, and it is the same fix for every plugin in their
# catalogue that ships a TUI — there are two dozen behind the `games` tag alone.
#
# ⛔ WHAT IT IS NOT is a shim for `omarchy` the command. That one manages an
# Omarchy installation, and synui has synui-plugins for the part of it a plugin
# reaches (`omarchy plugin disable` is tetris's "remove from bar", which fails
# loudly and locally rather than half-removing anything). This is a launcher and
# nothing else.
#
# Usage:
#   omarchy-launch-or-focus-tui [--app-id=ID] [--title=T] CMD [ARGS…]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

APP_ID=""

while [ $# -gt 0 ]; do
    case "$1" in
        --app-id=*) APP_ID=${1#--app-id=} ;;
        --app-id)   shift; APP_ID=${1:-} ;;
        --title=*)  ;;   # accepted and dropped — see the exec at the end
        --title)    shift ;;
        --)         shift; break ;;
        -*)         ;;   # ignore an option meant for a terminal we are not
        *)          break ;;
    esac
    shift
done

[ $# -gt 0 ] || { echo "usage: omarchy-launch-or-focus-tui [--app-id=ID] CMD [ARGS…]" >&2; exit 2; }

# Nothing to distinguish it by means nothing to focus: launch and be done.
if [ -z "$APP_ID" ]; then
    exec syntty -e "$@"
fi

# ── Is it already up? ───────────────────────────────────────────────────────
#
# ⚠ ASKED OF THE COMPOSITOR, NOT OF `pgrep`. A process running is not a window
# on screen — the game may have been started and closed, leaving a shell, or
# started on another workspace — and `synctl clients` is the list of what is
# actually mapped, with the app_id each one reports. It is JSON, read with
# python3 rather than a grep for the id: a title is user text and can contain
# anything, including the id being searched for.
#
# ⛔ `synctl` EXITING 0 IS NOT "IT IS RUNNING". Every one of these exits 0
# whether or not it found anything — see reference_exit_status_is_not_did_it_launch
# — so the answer is what it PRINTS.
running=0
if command -v synctl >/dev/null 2>&1; then
    running=$(synctl clients 2>/dev/null | python3 -c '
import json, sys
want = sys.argv[1]
try:
    rows = json.load(sys.stdin)
except Exception:
    print(0); raise SystemExit
print(1 if any(r.get("app_id") == want for r in rows) else 0)
' "$APP_ID" 2>/dev/null) || running=0
fi

if [ "$running" = "1" ]; then
    exec synctl dispatch focus_app "$APP_ID"
fi

# ── Otherwise start it, wearing its own name ────────────────────────────────
#
# ⚠ --app-id IS WHAT MAKES THE FOCUS HALF WORK AT ALL. syntty set the literal
# "syntty" on every window it ever opened, so to the question above every
# terminal on the desk was the same application: "focus the tetris you already
# have open" could only ever mean "focus some terminal", and the second Play
# would raise a shell. The flag was added for this (syntty --app-id=, st_win_conf_t).
#
# ⚠ AND `-e` IS LOAD-BEARING FOR THE COMMAND, not decoration: syntty's window
# runs your shell unless -e says otherwise, and everything after -e belongs to
# the child untouched.
#
# ⛔ --title IS ACCEPTED ABOVE AND DELIBERATELY NOT PASSED ON. syntty has no
# such flag and its argument parser DIES on an unknown option, so forwarding one
# a caller happened to pass would turn a cosmetic request into a game that never
# starts. A TUI sets its own title through OSC once it is running, which is
# where a terminal's title comes from anyway.
exec syntty --app-id="$APP_ID" -e "$@"
