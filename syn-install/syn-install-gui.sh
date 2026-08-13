#!/usr/bin/env bash
# syn-install-gui — launcher for the graphical installer.
#
# The window is quickshell rendering syn-install-gui.qml, and everything it
# decides is handed back to `syn-install --config`. This script is only the
# three checks that have to happen before a window can be drawn at all, in the
# order that gives the most useful message.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

QML=/usr/share/syn-install/syn-install-gui.qml
# Straight out of a checkout, so the window can be worked on without installing
# the package first.
[ -r "$QML" ] || QML="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/data/syn-install-gui.qml"

# ── --if-requested ────────────────────────────────────────
#
# The live session's synuirc autostarts this on every boot, and the flag is
# written by exactly one thing: answering "Install graphically" at the live
# image's menu (syn-install --live). Without the flag this exits silently, so a
# live session that was only ever meant to be looked around in does not open an
# installer nobody asked for.
#
# The flag is removed as soon as it is read. It lives in /run, so it is gone on
# reboot anyway; removing it here is what stops a second window appearing if
# the session restarts the compositor.
FLAG=/run/synapseos/install-gui
if [ "${1:-}" = "--if-requested" ]; then
    [ -e "$FLAG" ] || exit 0
    rm -f "$FLAG"
    shift
fi

if [ -z "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then
    echo "no display — syn-install-gui needs a graphical session." >&2
    echo "Installing from a terminal is 'syn-install'." >&2
    exit 1
fi

command -v quickshell >/dev/null 2>&1 || {
    echo "quickshell is not installed — the graphical installer needs it." >&2
    echo "Installing from a terminal is 'syn-install'." >&2
    exit 1
}

# It partitions disks, so it is root or it is nothing. The live session already
# runs as root, which is the session this exists for; anywhere else, say so
# rather than drawing a form whose last button cannot work.
if [ "$(id -u)" != 0 ]; then
    echo "syn-install-gui must run as root — it partitions disks." >&2
    echo "From a live SynapseOS session it already does." >&2
    exit 1
fi

[ -r "$QML" ] || { echo "missing $QML" >&2; exit 1; }

# The window's Wayland app_id. Without it quickshell calls every window
# "org.quickshell": the generic icon in the dock, and no .desktop the dock can
# resolve. Overwritten rather than set — an INHERITED QS_APP_ID is the common
# accident, since anything spawned from another quickshell app arrives with
# that app's identity.
export QS_APP_ID=syn-install

exec quickshell -p "$QML"
