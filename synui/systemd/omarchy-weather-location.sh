#!/usr/bin/env bash
#
# omarchy-weather-location — show or set the location weather widgets report on.
#
# ⚠ THIS NAME IS NOT OURS, FOR THE SAME REASON omarchy-launch-or-focus-tui IS
# NOT. Every weather widget in Omarchy's catalogue reads ONE file for its
# location, and this is the only thing that writes it:
#
#     ~/.local/state/omarchy/settings/weather.json
#
# eduardodallecort.weather-radar hardcodes the call into its own QML —
#
#     locationSaveProc.command = ["omarchy-weather-location", "--set", name, lat + "," + lon]
#
# — with no configuration and no fallback, and its Service.qml then watches the
# file for the answer. On a desktop where this command does not exist the
# picker takes a city, spawns nothing, and goes on showing no location forever:
# Quickshell's Process reports a missing binary to the shell log and NOT to the
# widget that asked, so the city list works, the click works, and nothing
# happens. That is one line in a log nobody reads and a widget that looks
# broken.
#
# ⛔ AND IT IS EVERY WEATHER WIDGET AT ONCE, not one plugin's bug. The file is
# shared on purpose — the plugins' own headers say so ("there is one location
# on this machine, and it is the weather widget's file") — so a box without
# this command has no way to set a location for any of them.
#
# ⛔ NO jq. Upstream's version shells out to it for both halves; SynapseOS does
# not ship jq, so a verbatim copy would fail exactly as loudly as the missing
# binary it replaces — which is to say not at all. The write is a printf with
# an escaper and the read is a sed, because the file is three fields written by
# this script and read by a JSON.parse in QML.
#
# Contract, matching Omarchy's so the file stays interchangeable:
#
#   (no args)                 print the stored name, or the IP-detected city
#                             when nothing is stored
#   --set <name> [lat,lon]    store a location; coordinates are optional and
#                             are what make the forecast exact
#   --clear                   remove the file, returning to IP auto-detect
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

LOC_FILE="${HOME}/.local/state/omarchy/settings/weather.json"

usage() {
    echo "Usage: omarchy-weather-location [--set <name> [lat,lon]|--clear]" >&2
    exit 1
}

# JSON string escaping, for a value that came off a command line.
#
# ⚠ A CITY NAME IS SOMEBODY ELSE'S TEXT. The geocoder answers with the local
# spelling — "Côte-Saint-Luc", and quotes and backslashes are legal in one —
# and an unescaped quote here writes a file that JSON.parse rejects, which the
# reader turns into "no location set". Control characters are dropped rather
# than escaped: nothing in a place name needs them and \u escaping for a case
# that cannot arise is code that will never be exercised.
json_escape() {
    printf '%s' "$1" | tr -d '\000-\037' | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

# The stored name, or nothing.
#
# Deliberately tolerant: this reads a file the stock weather widget may have
# written, and a hand-written {"name": "Malibu"} with no coordinates is a
# documented way to set one.
stored_name() {
    [ -f "$LOC_FILE" ] || return 0
    sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\(\([^"\\]\|\\.\)*\)".*/\1/p' \
        "$LOC_FILE" 2>/dev/null |
        head -n 1 |
        sed -e 's/\\"/"/g' -e 's/\\\\/\\/g'
}

case "${1:-}" in
"")
    name=$(stored_name)
    if [ -z "$name" ]; then
        # Same source and same trimming as upstream, so a box with no stored
        # location prints what Omarchy's own menu would print. Failure here is
        # silence, not an error: no network is a perfectly ordinary state and
        # the caller is a widget, not a person.
        name=$(curl -fsS --max-time 4 "https://wttr.in/?format=%l" 2>/dev/null)
        name=${name%%,*}
    fi
    [ -n "$name" ] && printf '%s\n' "$name"
    ;;
--set)
    name=${2:-}
    [ -n "$name" ] || { echo "Usage: omarchy-weather-location --set <name> [lat,lon]" >&2; exit 1; }
    coords=${3:-}

    if [ -n "$coords" ]; then
        # ⚠ VALIDATED BEFORE IT IS WRITTEN, because the reader cannot tell a
        # malformed number from an absent one: RadarModel.parseLocationFile
        # runs parseFloat and calls the whole location invalid, so garbage
        # here reads back as "no location" and the picker looks like it did
        # nothing again.
        case "$coords" in
        *,*) ;;
        *) echo "Invalid coordinates: $coords (expected lat,lon)" >&2; exit 1 ;;
        esac
        lat=${coords%%,*}
        lon=${coords#*,}
        for n in "$lat" "$lon"; do
            case "$n" in
            "" | *[!0-9.eE+-]* | *.*.*)
                echo "Invalid coordinates: $coords (expected lat,lon)" >&2; exit 1 ;;
            esac
        done
    fi

    mkdir -p "$(dirname "$LOC_FILE")" || exit 1

    # Written whole and moved into place. The widgets watch this file and
    # reload on any change, so a partial write is a reader parsing half a JSON
    # object — and its answer to that is, once more, "no location set".
    tmp=$(mktemp "${LOC_FILE}.XXXXXX") || exit 1
    trap 'rm -f "$tmp"' EXIT
    if [ -n "$coords" ]; then
        printf '{"name": "%s", "latitude": %s, "longitude": %s}\n' \
            "$(json_escape "$name")" "$lat" "$lon" > "$tmp"
    else
        printf '{"name": "%s"}\n' "$(json_escape "$name")" > "$tmp"
    fi
    mv -f "$tmp" "$LOC_FILE" || exit 1
    trap - EXIT
    ;;
--clear)
    rm -f "$LOC_FILE"
    ;;
*)
    usage
    ;;
esac
