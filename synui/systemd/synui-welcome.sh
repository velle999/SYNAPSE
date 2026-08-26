#!/bin/sh
# synui-welcome — open SynapseOS's welcome guide.
#
# The guide was a compositor-drawn panel until synui 0.1.0-496: one 513px column
# of nineteen labels and their keyboard chords, cairo in render.c, roughly 400
# lines of drawing and hit-testing inside the process that also draws every
# window on the desktop. It is QML now — `quickshell/welcome.qml`, a second
# entry point into the bar's own tree, so it inherits Theme.qml, the picked UI
# font and the glass namespace without copying any of them.
#
# ⚠ WHY IT IS A SEPARATE PROCESS AND NOT A WINDOW IN THE BAR. Two bars ship —
# `bar_shell = synapse|antiquity` picks one — and a guide inside the SYNAPSE bar
# would not exist for anyone running the other. The panel it replaces was drawn
# by the compositor, so every configuration had it; taking it away from one of
# two shipped shells would be a regression nobody would report as one. As its
# own quickshell it also costs nothing when closed (dismissing it quits) and
# does not come back every time game mode restarts the bar.
#
# ── Toggling across a process boundary ───────────────────────────────────────
#
# Super+Escape toggles. Closing the guide QUITS it, so "closed" and "not
# running" are the same state: ask the running instance to toggle, and start one
# when nothing answers. There is no third state and no pidfile.
#
# quickshell's IPC exits non-zero when no instance is listening, which is the
# whole signal this needs. Its complaint goes to stderr and is expected on the
# common path (the guide is usually not running), so it is discarded — a message
# per keypress saying the thing you are about to start is not started yet is how
# a log becomes something nobody reads.
#
# ── Which tree ───────────────────────────────────────────────────────────────
#
# The same user-tree-or-packaged-path decision synui-bar makes, and for the same
# reason: an IPC call has to name the config the instance was started with. A
# box with ~/.config/quickshell/synapse would otherwise start the guide from the
# user's tree and then fail to talk to it, with nothing on screen saying why.
#
# ⚠ THE ANTIQUITY TREE IS NOT CONSULTED. `bar_shell` selects a BAR; the guide is
# not one, it has no Antiquity variant, and reading the key here would mean the
# guide silently disappearing for anyone who switched bars — the exact
# regression this file's separate existence is meant to prevent.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

CONF_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
SYNUI_QS="/usr/share/synui/quickshell"

usage() {
    cat <<'USAGE'
usage: synui-welcome [toggle|show|hide|page N] [output]

  toggle   show the guide, or close it if it is already up (the default)
  show     show it, moving it to `output` if it is up on another monitor
  hide     close it
  page N   turn the running guide to page N (1-based)

`output` is a monitor name (DP-1, HDMI-A-1). The compositor passes the focused
one; omitted, the guide asks `synctl outputs` and falls back to every screen.
USAGE
}

command -v quickshell >/dev/null 2>&1 || {
    echo "synui-welcome: quickshell is not installed" >&2
    exit 1
}

# A user tree wins, exactly as it does for the bar: copying the packaged tree to
# ~/.config/quickshell/synapse and editing it works the way XDG leads you to
# expect. Named by PATH in both cases and never by `-c`, which quickshell
# refuses to combine with `-p` — and it is the file inside the config that has
# to be named here, since `welcome.qml` is not the tree's shell.qml.
if [ -f "$CONF_HOME/quickshell/synapse/welcome.qml" ]; then
    ENTRY="$CONF_HOME/quickshell/synapse/welcome.qml"
elif [ -f "$SYNUI_QS/welcome.qml" ]; then
    ENTRY="$SYNUI_QS/welcome.qml"
else
    echo "synui-welcome: no welcome.qml under $SYNUI_QS" >&2
    exit 1
fi

cmd="${1-toggle}"
case "$cmd" in
    -h|--help|help) usage; exit 0 ;;
esac
[ $# -eq 0 ] || shift

page=0

case "$cmd" in
    hide)
        # Nothing to start: "not running" already IS hidden, and reporting a
        # failure for it would make `synui-welcome hide` an error in the
        # overwhelmingly common case.
        quickshell -p "$ENTRY" ipc call welcome hide >/dev/null 2>&1
        exit 0
        ;;
    page)
        n="${1-1}"
        case "$n" in
            ''|*[!0-9]*) echo "synui-welcome: page takes a number" >&2; exit 2 ;;
        esac
        [ $# -eq 0 ] || shift
        # 1-based on the command line, 0-based inside the guide. The rail is
        # numbered "1. …" on screen, and a CLI that disagreed with the window it
        # drives would be its own bug report.
        page=$((n > 0 ? n - 1 : 0))
        quickshell -p "$ENTRY" ipc call welcome page "$page" >/dev/null 2>&1 && exit 0
        ;;
    toggle|show)
        # Ask the running guide first. Success means it was up and has now
        # toggled, moved or closed — there is nothing left to do.
        quickshell -p "$ENTRY" ipc call welcome "$cmd" "${1-}" >/dev/null 2>&1 && exit 0
        ;;
    *) usage >&2; exit 2 ;;
esac

# Nothing answered: start one. Both facts travel in the ENVIRONMENT because
# `quickshell -p` takes no positional arguments of its own to pass them in — see
# GuideState.qml, where the very first window is the one that cannot be told any
# other way.
out="${1-}"
[ -z "$out" ] || export SYNUI_WELCOME_OUTPUT="$out"
[ "$page" -eq 0 ] || export SYNUI_WELCOME_PAGE="$page"

exec quickshell -p "$ENTRY"
