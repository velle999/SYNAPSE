#!/bin/sh
# synui-bar — start SynapseOS's bar (quickshell).
#
# The bar was waybar until 2026-07-24 and is now a quickshell/QML shell. What
# forced the move was the start menu: waybar's GTK window is never handed
# keyboard focus by a layer-shell compositor, which is the whole reason
# src/menu.c exists as compositor-drawn code. quickshell takes keyboard focus
# fine (PanelWindow { focusable: true }), reads PipeWire/UPower/BlueZ/SNI
# directly instead of polling shell scripts, and gives one bar per screen off a
# live Quickshell.screens model — a monitor plug or unplug no longer needs the
# bar restarted by hand.
#
# Why a wrapper rather than shipping to ~/.config/quickshell: quickshell's `-c
# <name>` only looks under the user's config dir, so a packaged shell has to be
# named by path. /usr/share/synui/quickshell is ours outright — the same reason
# the bar config never went to /etc/xdg/waybar, which is owned by the waybar
# package and made the whole pacstrap transaction fail.
#
# A user config still wins: if ~/.config/quickshell/synapse exists we hand over
# to it by name, so copying the packaged tree into your home and editing it works
# the way XDG leads you to expect. There is deliberately no merging — a
# half-ours-half-yours QML tree would fail to load with an error pointing at a
# file the user never wrote.
#
# The palette is NOT passed here. Theme.qml watches
# ~/.config/synui/theme.json, which synui-apply-theme writes on every theme
# switch, so the running bar recolours itself without a restart.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
#
# ── Which shell ──────────────────────────────────────────────────────────────
#
# Two QML trees ship: quickshell/ (SYNAPSE's own bar) and quickshell-antiquity/
# (a port of diinki's linux-antiquity). `bar_shell = synapse|antiquity` in
# synuirc picks one, and Control panel ▸ Desktop ▸ Bar shell writes the same key
# to settings.state.
#
# Read HERE rather than passed in by the compositor, because synui does not
# start the bar — the session does, and `synui-bar ipc` is run by a compositor
# that may have been launched before the setting was ever written. The key is
# still declared in config.c so that synuirc has ONE spelling of it and the
# control panel can persist it the way it persists everything else; the
# compositor parses it and never acts on it.
#
# Both files are `key = value` in synuirc's language, so one reader does both.
# settings.state wins, exactly as it does inside synui: it is the panel's half
# of the config and holds only keys the user has actually changed.
set -u

CONF_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
SYNUI_ETC="$CONF_HOME/synui"

command -v quickshell >/dev/null 2>&1 || {
    echo "synui-bar: quickshell is not installed" >&2
    exit 1
}

# Last `key = value` for $1 out of $2, or nothing. Last, not first, because
# config_parse_kv() takes the last assignment too — a synuirc with the key
# written twice must resolve here the way it resolves in the compositor.
conf_get() {
    [ -f "$2" ] || return 0
    sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$2" |
        sed 's/[[:space:]]*$//' | tail -n 1
}

conf_lookup() {
    v=$(conf_get "$1" "$SYNUI_ETC/settings.state")
    [ -n "$v" ] || v=$(conf_get "$1" "$SYNUI_ETC/synuirc")
    [ -n "$v" ] || v=$(conf_get "$1" /etc/synui/synuirc)
    printf '%s' "$v"
}

# An unknown name falls back to the shipped bar rather than failing to start:
# a typo in synuirc must not be the difference between a desktop with a bar and
# a desktop without one. config.c logs the same case as an error, which is
# where a user will find out they mistyped it.
case "$(conf_lookup bar_shell)" in
    antiquity) SHELL_NAME=antiquity ;;
    *)         SHELL_NAME=synapse ;;
esac

SYNUI_BAR="/usr/share/synui/quickshell"
[ "$SHELL_NAME" = synapse ] || SYNUI_BAR="/usr/share/synui/quickshell-$SHELL_NAME"

# The packaged Antiquity tree not being installed is worth saying out loud
# rather than handing quickshell a path it will fail on with a QML error.
if [ ! -f "$SYNUI_BAR/shell.qml" ]; then
    echo "synui-bar: no shell.qml under $SYNUI_BAR — falling back to synapse" >&2
    SHELL_NAME=synapse
    SYNUI_BAR="/usr/share/synui/quickshell"
fi

# ── Antiquity's state files ──────────────────────────────────────────────────
#
# The Antiquity shell keeps its settings, its desktop widgets and its favourite
# apps in three JSON files that a quickshell FileView reads and writes. A
# FileView will NOT create a file that does not exist — it drops the write with
# no `saved`, no `saveFailed` and no error — so something outside QML has to put
# them there once, or every setting in that shell silently fails to persist.
#
# Upstream never needed this: linux-antiquity is installed by copying its tree
# into ~/.config/quickshell, so the files sat beside shell.qml in a directory
# the user owned, and the first write created them. SYNAPSE ships the tree as a
# package under a root-owned /usr/share, so that path was not merely empty, it
# was unwritable — see the header in quickshell-antiquity/Config.qml.
#
# Seeded HERE rather than from QML because this runs before quickshell does and
# already knows which shell is starting. Doing it in Config.qml means racing the
# FileView's own first load: the seed lands after the read has already failed,
# and the reload needed to recover would clobber whatever the user changed in
# between.
#
# `{}` and not the default settings object: an empty JSON object leaves every
# JsonAdapter property at its declared default, so the shipped defaults stay in
# one place (Config.qml) instead of being duplicated here in a form that would
# drift. Existing files are never touched, so this is safe to re-run and cannot
# revert anybody's settings.
if [ "$SHELL_NAME" = antiquity ]; then
    _antiq="$SYNUI_ETC/antiquity"
    if mkdir -p "$_antiq" 2>/dev/null; then
        for _f in settings widgets favoriteapps; do
            [ -e "$_antiq/$_f.json" ] || printf '{}' > "$_antiq/$_f.json"
        done
    else
        echo "synui-bar: cannot create $_antiq — Antiquity settings will not persist" >&2
    fi
fi

# ── The icon theme ───────────────────────────────────────────────────────────
#
# Empty (the default) means "follow the system icon theme", which is what a
# theme switch changes and what both shells want. `bar_icon_theme` pins one
# instead. It is the dynamic equivalent of a `//@ pragma IconTheme` line, which
# is static and would override the theme synui-apply-theme just wrote for
# GTK/Qt — see the comment at the top of quickshell-antiquity/shell.qml, whose
# upstream hard-pinned buuf-nestort this way.
#
# Exported unconditionally when set, for both shells: an icon theme is not an
# Antiquity concept, and a setting that silently applied to one bar and not the
# other would be the worse surprise.
_icon_theme=$(conf_lookup bar_icon_theme)
[ -z "$_icon_theme" ] || export QS_ICON_THEME="$_icon_theme"

# `synui-bar ipc …` talks to the RUNNING bar instead of starting one.
#
# This exists so that exactly one place knows how the bar was launched. An IPC
# call has to name the same config the instance was started with, and that is
# the user-tree-or-packaged-path decision made below — duplicating it in the
# compositor would mean a box with a user tree silently failing to open its
# start menu, with nothing on screen saying why. The shell selection above is
# part of that same decision, which is why it runs before this branch.
#
# The compositor calls this for the Super tap: synui owns the keyboard and
# dispatches keybinds before forwarding, but its own IPC (synctl) is
# request/response with no event stream, so it cannot tell the bar anything.
# quickshell's IPC runs the other way. See quickshell/shell.qml's IpcHandler.
#
# BOTH trees answer to the `menu` target, which is what makes the shell
# swappable without the compositor knowing. Antiquity had no such handler when
# it first shipped — upstream drove Hyprland directly and its only IpcHandler
# was a per-screen `appLauncher_<name>`, which nothing here ever called — so the
# Super tap did nothing on it. Its shell.qml now exposes the same
# toggle/open/close(output) that quickshell/shell.qml does, opening the
# Antiquity app launcher instead of the SYNAPSE start menu.
if [ "${1-}" = "ipc" ]; then
    shift
    if [ -f "$CONF_HOME/quickshell/$SHELL_NAME/shell.qml" ]; then
        exec quickshell -c "$SHELL_NAME" ipc "$@"
    fi
    exec quickshell -p "$SYNUI_BAR/shell.qml" ipc "$@"
fi

# A user tree wins. `-c <name>` is how quickshell finds a config by name, and it
# searches the user's config dir — so this is the handover, not a duplicate of
# the packaged path below. The name follows the selected shell, so copying
# either packaged tree into ~/.config/quickshell/<name> works the same way.
if [ -f "$CONF_HOME/quickshell/$SHELL_NAME/shell.qml" ]; then
    exec quickshell -c "$SHELL_NAME" "$@"
fi

exec quickshell -p "$SYNUI_BAR/shell.qml" "$@"
