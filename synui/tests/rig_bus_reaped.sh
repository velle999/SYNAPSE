#!/usr/bin/env bash
# rig_bus_reaped.sh — a rig that gives the bar a private session bus ends it.
#
# Five rigs start the bar as `dbus-run-session -- quickshell … &` so it reads a
# private session bus instead of the live one. That makes `$!` the PID of
# dbus-run-session, not of quickshell, and their cleanup was `kill -9` on it.
# A SIGKILLed dbus-run-session cannot stop the dbus-daemon it started, so the
# bus outlived the test, and so did everything the bar had activated on it:
# xdg-desktop-portal, the permission store, at-spi and its own bus. That came
# to six bus constellations and ~190 MB per synui suite run, reparented to
# PID 1 and never reaped. Five runs on 2026-10-01 left 123 processes and
# 968 MB, all on the machine of whoever ran `meson test`.
#
# Killing the bus daemon takes the activated services with it (measured), so
# the fix is to kill dbus-run-session's CHILDREN first: `pkill -9 -P "$PID"`.
#
# This is the static half: every rig that backgrounds dbus-run-session must
# reap its children somewhere in the file. A sixth rig copied from a
# pre-2026-10-01 cleanup fails here instead of on velle's RAM.
#
# Usage: rig_bus_reaped.sh /path/to/tests
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

DIR=${1:?usage: rig_bus_reaped.sh /path/to/tests}

pass=0 fail=0
n=0
for f in "$DIR"/*.sh; do
    # Backgrounded: `dbus-run-session -- … &` at the end of a line, outside a
    # comment. A foreground dbus-run-session returns only after its child has,
    # and then it stops the bus itself.
    grep -Eq '^[^#]*dbus-run-session[[:space:]].*&[[:space:]]*$' "$f" || continue
    n=$((n + 1))
    if grep -Eq '^[^#]*pkill[[:space:]]+-9[[:space:]]+-P' "$f"; then
        pass=$((pass + 1))
    else
        printf '  FAIL  %s backgrounds dbus-run-session and never reaps its children\n' \
            "$(basename "$f")" >&2
        fail=$((fail + 1))
    fi
done

# Five such rigs exist. Finding none means the pattern above stopped matching
# them, which would make this test pass about nothing.
if [ "$n" -eq 0 ]; then
    echo "  FAIL  no rig backgrounds dbus-run-session — the match is broken" >&2
    exit 1
fi

[ "$fail" -eq 0 ] || exit 1
printf '  ok    all %d private-bus rigs reap their bus\n' "$pass"
