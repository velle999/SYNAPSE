#!/usr/bin/env bash
# syn-resolve-gui — the DaVinci Doctor window.
#
# A launcher, not an application: the window itself is resolve.qml, run by
# quickshell. Kept as a wrapper rather than putting `qs --path …` straight in
# the .desktop Exec= so that a missing quickshell produces a sentence somebody
# can act on, instead of a menu entry that does nothing when clicked — the
# failure this distro has been bitten by before, where quickshell's errors land
# on tty1 and the entry merely looks dead.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -uo pipefail

QML=${SYN_RESOLVE_QML:-/usr/share/syn/resolve.qml}

if ! command -v qs >/dev/null 2>&1; then
    msg="DaVinci Doctor needs quickshell, which is not installed.

Install it with:  sudo pacman -S quickshell
Or use the terminal:  syn resolve doctor"
    # No terminal to print to when launched from a menu, so say it in whatever
    # dialog is available before falling back to stderr.
    if command -v zenity >/dev/null 2>&1; then
        zenity --error --no-wrap --title="DaVinci Doctor" --text="$msg"
    else
        printf '%s\n' "$msg" >&2
    fi
    exit 1
fi

[ -r "$QML" ] || { echo "syn-resolve-gui: missing $QML" >&2; exit 1; }

# -n (--no-duplicate) does NOT raise or focus an existing window — quickshell
# has no such flag. It only means "exit immediately if this config is already
# running", and it exits 0 doing so. Safe here only because resolve.qml quits
# when its window closes, so a live instance always has a window on screen. If
# that handler is ever removed, this flag turns every later launch into a silent
# no-op with a success exit code.
#
# QS_APP_ID is the window's app_id, and it MUST equal this .desktop's basename
# or the dock cannot resolve the window to its entry: synui looks the icon and
# the launch command up by "<app_id>.desktop" and finds nothing, so the pinned
# icon draws from the theme and clicking it does nothing at all. Without it
# every quickshell window on the system is "org.quickshell".
export QS_APP_ID="${QS_APP_ID:-syn-resolve-gui}"

exec qs -n --path "$QML" "$@"
