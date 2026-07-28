#!/usr/bin/env bash
# syn-update-gui — the SynapseOS Updates window.
#
# A launcher, not an application: the window itself is shell.qml, run by
# quickshell. Kept as a wrapper rather than putting `qs --path ...` straight in
# the .desktop Exec= so that a missing quickshell produces a sentence a person
# can act on instead of a desktop entry that does nothing when clicked — which
# is exactly the failure mode this distro has already been bitten by, where
# quickshell errors land on tty1 and the UI merely looks dead.

set -uo pipefail

QML=/usr/share/syn-update/shell.qml

if ! command -v qs >/dev/null 2>&1; then
    msg="The graphical updater needs quickshell, which is not installed.

Install it with:  sudo pacman -S quickshell
Or use the terminal:  syn-update check"
    # No terminal to print to when launched from a menu, so say it in whatever
    # dialog is available before falling back to stderr.
    if command -v zenity >/dev/null 2>&1; then
        zenity --error --no-wrap --title="SynapseOS Updates" --text="$msg"
    else
        printf '%s\n' "$msg" >&2
    fi
    exit 1
fi

[ -r "$QML" ] || { echo "syn-update-gui: missing $QML" >&2; exit 1; }

# -n (--no-duplicate) does NOT raise or focus an existing window — quickshell
# has no such flag. It only means "exit immediately if this config is already
# running", and it exits 0 doing so. It is safe here solely because shell.qml
# quits on window close, so a live instance always has a window on screen. If
# that handler is ever removed, this flag turns every later launch into a
# silent no-op with a success exit code.
exec qs -n --path "$QML" "$@"
