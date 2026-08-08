#!/usr/bin/env bash
#
# synui-volume — change the volume of the device that is actually making sound.
#
# ── WHY THIS EXISTS AND IS NOT JUST `wpctl @DEFAULT_AUDIO_SINK@` ────────────
#
# The equalizer is a virtual sink that FEEDS a real one (there is no smart
# filter in this PipeWire — see synui-eq). While it is on it is the default
# sink, so `@DEFAULT_AUDIO_SINK@` resolves to the CHAIN, and the volume keys
# start driving a second gain stage stacked on top of the device's own.
#
# Two stages multiply. At the levels a bluetooth headset actually sits at that
# is not a subtlety — velle's P20i at 29% gave 0.0244 amplitude, and squaring
# it lands at -64.5 dB, which is silence. It was reported as "turning eq on
# mutes my music through bluetooth" on 2026-08-08.
#
# So the chain is pinned at unity and the DEVICE owns the volume, which means
# the keys have to find the device even when the default sink is the chain.
# That is the whole job of this script.
#
# It deliberately never RAISES a device it had to resolve through the chain by
# any path other than the user's own keypress: on bluetooth a volume write is
# an AVRCP absolute-volume command to hardware in someone's ears.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -euo pipefail

CHAIN_SINK="effect_input.synui_eq"
CHAIN_OUT="effect_output.synui_eq"

# wpctl speaks node IDs, not names — @DEFAULT_AUDIO_SINK@ is the one name it
# knows. Everything below resolves to an ID so the existing `wpctl set-volume`
# semantics (linear steps, -l cap) are kept exactly as they were.
node_id_by_name() {
    pw-cli ls Node 2>/dev/null | awk -v want="$1" '
        /^[[:space:]]*id [0-9]+,/ { id = $2; sub(",", "", id) }
        /node\.name = / {
            name = $0
            sub(/.*node\.name = "/, "", name)
            sub(/".*/, "", name)
            if (name == want) { print id; exit }
        }'
}

# The device the chain is feeding, read from the LINK rather than remembered
# from when the chain started. The chain's output is passive, so WirePlumber
# re-links it whenever the default device changes — plug in a headset while the
# equalizer is on and the audio moves. Anything cached would be pointing at the
# speakers by then, and the keys would silently adjust a device nobody is
# listening to.
chain_target_name() {
    pw-link -o -l "$CHAIN_OUT" 2>/dev/null |
        sed -n 's/^[[:space:]]*|-> \([^:]*\):.*/\1/p' | head -1
}

# The node the volume keys should act on: normally just the default sink, but
# the device behind it whenever the equalizer is in the way. Falls back to the
# default sink if the chain's target cannot be read — a volume key that does
# the slightly wrong thing beats one that does nothing.
target_id() {
    local def target id
    def=$(pactl get-default-sink 2>/dev/null || true)

    if [[ $def == "$CHAIN_SINK" ]]; then
        target=$(chain_target_name)
        if [[ -n $target ]]; then
            id=$(node_id_by_name "$target")
            [[ -n $id ]] && { echo "$id"; return 0; }
        fi
    fi

    echo "@DEFAULT_AUDIO_SINK@"
}

case "${1:-}" in
up)
    # -l caps the raise at 100%: without it a held key keeps climbing into
    # software gain and clips.
    exec wpctl set-volume -l 1.0 "$(target_id)" 5%+
    ;;
down)
    exec wpctl set-volume "$(target_id)" 5%-
    ;;
mute)
    exec wpctl set-mute "$(target_id)" toggle
    ;;
target)
    # What the keys would hit, as a NAME — for the panel, and for debugging a
    # volume key that appears to do nothing.
    def=$(pactl get-default-sink 2>/dev/null || true)
    if [[ $def == "$CHAIN_SINK" ]]; then
        t=$(chain_target_name)
        [[ -n $t ]] && { echo "$t"; exit 0; }
    fi
    echo "$def"
    ;;
*)
    echo "synui-volume: unknown command '${1:-}'" >&2
    echo "usage: synui-volume {up|down|mute|target}" >&2
    exit 2
    ;;
esac
