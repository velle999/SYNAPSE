#!/bin/bash
# synui-open-folder — open a directory in whatever file manager this system has.
#
# synui used to hardcode `dolphin` in three places: the bar's Files button
# (quickshell/widgets/QuickLaunch.qml), the desktop right-click menu
# (src/deskmenu.c) and the ISO mounter's open-the-mount step
# (synui-iso-mount.sh). dolphin is only an optdepend and syn-install stopped
# pacstrapping it unconditionally, so on a Minimal install all three were a
# click that did nothing at all — no window, no error, no log line.
#
# The order below is "honour a real choice, then find anything, then say so":
#
#   1. $SYNUI_FILE_MANAGER, for a user who wants a specific one.
#   2. xdg-open, but ONLY when a default handler for inode/directory is
#      actually registered. Straight xdg-open is not a safe first step: with no
#      registered handler it can fall through to a browser or to run-mailcap
#      and open the folder as *text*, and its desktop-specific paths
#      (kde-open5, gio) depend on XDG_CURRENT_DESKTOP being a desktop whose
#      tools are installed. Asking xdg-mime first turns it from a guess into a
#      lookup of the user's own preference.
#   3. A known file manager on PATH. Same list synsh uses for its "open the
#      files" intent (synsh/src/intents.c) — keep the two in step.
#   4. xdg-open anyway, for a handler that is registered in a way step 2 misses.
#   5. Tell the user, because a silent no-op is the bug this script exists for.
#
# xdg-utils is a hard depend of synui (see PKGBUILD), so steps 2 and 4 are
# always available; it is the file manager itself that may be missing.
#
# Usage: synui-open-folder [DIR]   (no DIR = $HOME)
set -u

dir=${1:-$HOME}

if [ ! -e "$dir" ]; then
    echo "synui-open-folder: $dir does not exist" >&2
    exit 1
fi

# Unquoted on purpose: this is a command line, not a path, so "flatpak run
# org.x.Y" or "thunar --new-window" both work.
if [ -n "${SYNUI_FILE_MANAGER:-}" ]; then
    exec $SYNUI_FILE_MANAGER "$dir"
fi

if [ -n "$(xdg-mime query default inode/directory 2>/dev/null)" ]; then
    xdg-open "$dir" 2>/dev/null && exit 0
fi

for fm in dolphin nautilus thunar nemo caja pcmanfm-qt pcmanfm; do
    if command -v "$fm" >/dev/null 2>&1; then
        exec "$fm" "$dir"
    fi
done

xdg-open "$dir" 2>/dev/null && exit 0

echo "synui-open-folder: no file manager found for $dir" >&2
notify-send -a SynapseUI -u critical \
    "No file manager installed" \
    "Nothing on this system can open $dir.
Install one with:  sudo pacman -S dolphin" 2>/dev/null
exit 1
