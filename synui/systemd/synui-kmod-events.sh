#!/usr/bin/env bash
# synui-kmod-events — turn synapse_kmod's event capture on or off.
#
# /sys/kernel/synapse/config is root-owned 0644, so game mode cannot write it as
# the session user. This exists to be the ONE thing sudoers has to allow: two
# fixed arguments, one known key, one file. A `sudo tee /sys/...` rule would
# have let anything be written to that path; a shell rule would have let
# anything be run at all.
#
# What it is for: game mode's game_quiet_kmod. Be honest about the size of it —
# `events_enabled=0` does NOT remove the probes, it makes each one return early
# instead of building and queueing an event, and a desktop sits at roughly 50
# syscalls/second. The saving is close to nothing. It also stops synguard seeing
# events for the duration, which is why the setting is off by default and why
# this refuses to be clever about anything else in that file.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -uo pipefail

CONFIG=${SYNUI_KMOD_CONFIG:-/sys/kernel/synapse/config}

case "${1:-}" in
    on)  want=1 ;;
    off) want=0 ;;
    *)   echo "usage: synui-kmod-events on|off" >&2; exit 2 ;;
esac

# Not loaded is not an error. The module is optional, game mode fires this
# blind, and a failure here would only produce noise on a machine that was never
# going to be affected.
[ -w "$CONFIG" ] || {
    [ -e "$CONFIG" ] && echo "synui-kmod-events: $CONFIG is not writable (run as root)" >&2
    exit 0
}

printf 'events_enabled=%d\n' "$want" > "$CONFIG" || {
    echo "synui-kmod-events: write to $CONFIG failed" >&2
    exit 1
}

# Read it back. The kmod parses the write itself and can decline it, and this
# runs fire-and-forget from the compositor where nothing checks an exit status —
# so the log line is the only evidence, and it should say what is TRUE rather
# than what was asked for. That distinction is the whole reason the synapd stop
# went unnoticed for a month.
got=$(sed -n 's/^events_enabled=//p' "$CONFIG" 2>/dev/null | head -1)
if [ "${got:-}" = "$want" ]; then
    echo "synui-kmod-events: events_enabled=$got"
else
    echo "synui-kmod-events: asked for events_enabled=$want, file reads '${got:-?}'" >&2
    exit 1
fi
