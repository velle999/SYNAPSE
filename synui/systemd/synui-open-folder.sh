#!/bin/bash
# synui-open-folder — open a directory in whatever file manager this system has.
#
# ⚠ This is the FALLBACK path, not the normal one. Since 2026-08-10 synfiles
# is the distribution default for inode/directory (it ships the vendor
# mimeapps.list saying so), so step 2 below answers on any complete SynapseOS
# and the loop at the bottom is what runs on a system missing it.
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
#   3. A known file manager on PATH, synfiles first — it is SynapseOS's own and
#      the one the distribution's mimeapps.list names, so on a system where
#      step 2 found nothing it is still the right answer. Same list synsh uses
#      for its "open the files" intent (synsh/src/intents.c) — keep the two in
#      step.
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

# A REGISTERED HANDLER IS NOT NECESSARILY A FILE MANAGER.
#
# kitty ships kitty-open.desktop with `inode/directory` in its MimeType list.
# Nothing else claims that type on a fresh install, so it becomes the default
# by walkover — and then the bar's Files button, the desktop menu and the ISO
# mounter all opened a TERMINAL at $HOME, on a system with dolphin installed.
# It is not a broken association either: kitty really can display a directory,
# so nothing anywhere reports a problem.
#
# So the handler is resolved and its Exec inspected: if the program it runs is
# a terminal emulator, it is not what "open the folder" means and we move on to
# the list below. Only terminals are rejected — a user who genuinely set some
# unusual file manager as their default still gets it, which is the whole point
# of asking xdg-mime first.
handler_is_terminal() {
    local desktop=$1 d f exec_line prog
    for d in "${XDG_DATA_HOME:-$HOME/.local/share}" \
             $(printf '%s\n' "${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" | tr ':' ' '); do
        f="$d/applications/$desktop"
        [ -r "$f" ] || continue
        exec_line=$(sed -n 's/^Exec=//p' "$f" | head -1)
        prog=$(basename "${exec_line%% *}")
        case "$prog" in
            syntty|kitty|foot|foot-client|alacritty|wezterm|konsole| \
            gnome-terminal|kgx|xterm|urxvt|rxvt|st|terminator|tilix| \
            xfce4-terminal|ghostty|contour)
                return 0 ;;
        esac
        return 1
    done
    return 1
}

_handler=$(xdg-mime query default inode/directory 2>/dev/null)
if [ -n "$_handler" ] && ! handler_is_terminal "$_handler"; then
    xdg-open "$dir" 2>/dev/null && exit 0
fi

# synfiles first, and named separately rather than added to the loop: the
# browser is a SUBCOMMAND. The bare binary is a command-line file tool, so
# `synfiles "$dir"` would not open a window — it would exit 2 with a usage
# message into a terminal nobody is looking at, which is precisely the silent
# no-op this whole script exists to stop.
if command -v synfiles >/dev/null 2>&1; then
    exec synfiles gui "$dir"
fi

# The others stay: this script's job is to work on a system that has something
# ELSE, and demoting Dolphin is not the same as dropping it.
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
Install one with:  sudo pacman -S synfiles" 2>/dev/null
exit 1
