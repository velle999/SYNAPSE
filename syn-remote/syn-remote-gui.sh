#!/usr/bin/env bash
# syn-remote-gui — the Remote Desktop window.
#
# A launcher, not an application: the window itself is shell.qml, run by
# quickshell. Kept as a wrapper rather than putting `qs --path ...` straight in
# the .desktop Exec= so that a missing quickshell produces a sentence somebody
# can act on instead of a menu entry that does nothing when clicked — the
# failure this distro has already been bitten by, where quickshell's errors land
# on tty1 and the UI merely looks dead.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

QML=/usr/share/syn-remote/shell.qml

if ! command -v qs >/dev/null 2>&1; then
    msg="The Remote Desktop window needs quickshell, which is not installed.

Install it with:  sudo pacman -S quickshell
Or use the terminal:  syn-remote hosts"
    # No terminal to print to when launched from a menu, so say it in whatever
    # dialog is available before falling back to stderr.
    if command -v zenity >/dev/null 2>&1; then
        zenity --error --no-wrap --title="Remote Desktop" --text="$msg"
    else
        printf '%s\n' "$msg" >&2
    fi
    exit 1
fi

[ -r "$QML" ] || { echo "syn-remote-gui: missing $QML" >&2; exit 1; }

# ⛔ QS_APP_ID, OR THE WINDOW WEARS SOMEBODY ELSE'S IDENTITY. quickshell names
# its windows "org.quickshell" unless told otherwise, and a window launched FROM
# another app inherits that app's id — so without this the dock draws the
# Software Manager's icon on this window, or none at all.
#
# ⚠ THE VALUE MUST EQUAL THIS PACKAGE'S .desktop BASENAME. synui's dock looks a
# pinned app up as "<app_id>.desktop" and does not consult StartupWMClass;
# a mismatch is a pin that looks right and does nothing when clicked.
export QS_APP_ID="${QS_APP_ID:-syn-remote}"

# -n (--no-duplicate) exits 0 if this config is already running. Safe only
# because shell.qml quits on window close, so a live instance always has a
# window on screen; if that handler is ever removed this turns every later
# launch into a silent no-op with a success exit code.
exec qs -n --path "$QML" "$@"
