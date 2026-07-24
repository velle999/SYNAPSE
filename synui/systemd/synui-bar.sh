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
set -u

CONF_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
SYNUI_BAR="/usr/share/synui/quickshell"

command -v quickshell >/dev/null 2>&1 || {
    echo "synui-bar: quickshell is not installed" >&2
    exit 1
}

# `synui-bar ipc …` talks to the RUNNING bar instead of starting one.
#
# This exists so that exactly one place knows how the bar was launched. An IPC
# call has to name the same config the instance was started with, and that is
# the user-tree-or-packaged-path decision made below — duplicating it in the
# compositor would mean a box with a user tree silently failing to open its
# start menu, with nothing on screen saying why.
#
# The compositor calls this for the Super tap: synui owns the keyboard and
# dispatches keybinds before forwarding, but its own IPC (synctl) is
# request/response with no event stream, so it cannot tell the bar anything.
# quickshell's IPC runs the other way. See quickshell/shell.qml's IpcHandler.
if [ "${1-}" = "ipc" ]; then
    shift
    if [ -f "$CONF_HOME/quickshell/synapse/shell.qml" ]; then
        exec quickshell -c synapse ipc "$@"
    fi
    exec quickshell -p "$SYNUI_BAR/shell.qml" ipc "$@"
fi

# A user tree wins. `-c synapse` is how quickshell finds a config by name, and
# it searches the user's config dir — so this is the handover, not a duplicate
# of the packaged path below.
if [ -f "$CONF_HOME/quickshell/synapse/shell.qml" ]; then
    exec quickshell -c synapse "$@"
fi

exec quickshell -p "$SYNUI_BAR/shell.qml" "$@"
