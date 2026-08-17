#!/bin/bash
# synui-hdmi-audio — move sound to the screen you just plugged in, and back.
#
#   synui-hdmi-audio follow    a screen appeared: make its sink the default
#   synui-hdmi-audio restore   a screen went away: put the old sink back
#   synui-hdmi-audio status    what it would do, and why
#
# Called by synui on output hotplug when `hdmi_audio` is on (Control Panel ▸
# Sound ▸ Screen audio). Plugging a laptop into a TV and getting no sound out of
# it is the complaint this answers.
#
# ── WHY THE ELD AND NOT THE SINK NAME ───────────────────────────────────────
#
# "Switch to the sink with `hdmi` in its name" is the obvious rule and it is
# wrong. A machine advertises an HDMI sink PER PIN on every GPU that has one,
# plugged in or not: the dev desk here has
# `alsa_output.pci-0000_08_00.1.hdmi-stereo` sitting in the graph permanently,
# on a GPU driving three DisplayPort monitors. Following the name would hand
# audio to a dead connector and produce silence — the exact failure this is
# supposed to fix, with an extra step.
#
# The kernel already knows the answer. Each HDMI/DP audio pin has an ELD in
# /proc/asound/card*/eld#*, and a pin with a display on it that can take audio
# reads `monitor_present 1` + `eld_valid 1` and carries the display's own name:
#
#     monitor_present  1
#     eld_valid        1
#     monitor_name     Pixio PX248P
#
# So the test is "is there a live ELD?", and the name it carries is what lets
# the status line say which screen the sound went to.
#
# ── WHY IT WAITS ────────────────────────────────────────────────────────────
#
# The sink does not exist yet at the moment the monitor appears. The compositor
# sees the connector immediately; the kernel then reads the EDID, fills in the
# ELD, and wireplumber notices and switches the card's profile to one with a
# digital output on it. That chain takes a second or two, and a one-shot check
# fired from the hotplug event reliably finds nothing at all.
#
# ── WHAT IT WILL NOT DO ─────────────────────────────────────────────────────
#
# It never moves audio away from a sink the user chose by hand SINCE the last
# time it acted — see PREV below. Grabbing the default sink is rude, and a rule
# that overrode a deliberate choice would have to be switched off permanently by
# anyone who ever plugs in a screen and wants sound to stay on their headphones.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -uo pipefail

STATE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/synui"
# What the default sink was before we moved it, so `restore` can put it back.
# Also how `follow` knows the current default is one it set rather than one the
# user picked.
PREV="$STATE_DIR/hdmi-audio.state"

ELD_DIR="${SYNUI_ELD_DIR:-/proc/asound}"

# How long to wait for the sink to turn up, and how often to look. 8s covers a
# cold TV negotiating HDCP; the loop exits the moment it finds one, so the
# normal case costs a fraction of that.
WAIT_SECS="${SYNUI_HDMI_WAIT:-8}"
POLL_SECS=0.5

need() { command -v "$1" >/dev/null 2>&1; }

# ── Is a display with audio actually attached? ──────────────────────────────
#
# Prints "<card> <pin> <name>" for each live ELD, or nothing. `monitor_name` is
# optional even on a valid ELD (a few displays leave it empty), so it is filled
# in rather than required — an unnamed screen is still a screen.
live_elds() {
    local f card pin present valid name
    for f in "$ELD_DIR"/card*/eld#*; do
        [[ -e $f ]] || continue
        present=$(awk '$1=="monitor_present"{print $2}' "$f" 2>/dev/null)
        valid=$(awk '$1=="eld_valid"{print $2}' "$f" 2>/dev/null)
        [[ $present == 1 && $valid == 1 ]] || continue
        name=$(awk '$1=="monitor_name"{$1=""; sub(/^[ \t]+/,""); print}' "$f" 2>/dev/null)
        card=${f%/eld#*}; card=${card##*/card}
        pin=${f##*eld#}
        printf '%s %s %s\n' "$card" "$pin" "${name:-display}"
    done
}

has_live_eld() { [[ -n "$(live_elds)" ]]; }

# ── The graph ───────────────────────────────────────────────────────────────
#
# pw-dump + python3 rather than `wpctl status`: wpctl's output is a tree drawn
# for humans, and parsing it means depending on its indentation. jq would be the
# other option and is not installed on a stock SynapseOS.
#
# A sink counts as digital-display audio when its ALSA path or its profile says
# so — `api.alsa.path` is `hdmi:0` on the HDMI pins and `front:1` on the analog
# one, and the profile description carries "HDMI" or "Digital". Both are checked
# because a DisplayPort sink is not always spelled "hdmi" anywhere but the path.
pw_sinks() {   # -> "<id>\t<node.name>\t<description>\t<digital 0|1>" per line
    need pw-dump || return 1
    pw-dump 2>/dev/null | python3 -c '
import json, sys
try:
    objs = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for o in objs:
    if o.get("type") != "PipeWire:Interface:Node":
        continue
    p = o.get("info", {}).get("props", {})
    if p.get("media.class") != "Audio/Sink":
        continue
    blob = " ".join(str(p.get(k, "")) for k in
                    ("api.alsa.path", "device.profile.description",
                     "node.name", "node.description")).lower()
    digital = any(w in blob for w in ("hdmi", "displayport", "digital"))
    print("%s\t%s\t%s\t%d" % (o["id"], p.get("node.name", ""),
                              p.get("node.description", ""), digital))
' 2>/dev/null
}

default_sink_name() {
    need wpctl || return 1
    # `wpctl inspect @DEFAULT_AUDIO_SINK@` names it without any tree parsing.
    wpctl inspect @DEFAULT_AUDIO_SINK@ 2>/dev/null |
        awk -F'"' '/node\.name/ { print $2; exit }'
}

sink_id_by_name() {   # <node.name>
    pw_sinks | awk -F'\t' -v n="$1" '$2 == n { print $1; exit }'
}

first_digital_sink() {   # -> "<id>\t<node.name>\t<description>"
    pw_sinks | awk -F'\t' '$4 == 1 { print $1 "\t" $2 "\t" $3; exit }'
}

# ── Actions ─────────────────────────────────────────────────────────────────

cmd_follow() {
    has_live_eld || { echo "synui-hdmi-audio: no display with audio attached"; return 0; }

    local cur; cur=$(default_sink_name)

    # Wait for the sink. See "WHY IT WAITS" above.
    local waited=0 row=""
    while :; do
        row=$(first_digital_sink)
        [[ -n $row ]] && break
        # bash cannot compare floats; count in halves of a second.
        (( waited >= WAIT_SECS * 2 )) && break
        sleep "$POLL_SECS"
        waited=$((waited + 1))
    done

    if [[ -z $row ]]; then
        echo "synui-hdmi-audio: a display with audio is attached but no digital" \
             "sink appeared within ${WAIT_SECS}s — leaving the default alone" >&2
        return 0
    fi

    local id name desc
    IFS=$'\t' read -r id name desc <<<"$row"

    [[ $name == "$cur" ]] && { echo "synui-hdmi-audio: already on $desc"; return 0; }

    # Remember where we came from, so `restore` has somewhere to go back to.
    # Only a NON-digital sink is worth remembering: coming back from one screen
    # to another screen is not a restore, and overwriting the saved speaker with
    # a TV would strand the user on a disconnected sink later.
    if [[ -n $cur && $cur != "$name" ]]; then
        local cur_digital
        cur_digital=$(pw_sinks | awk -F'\t' -v n="$cur" '$2 == n { print $4; exit }')
        if [[ ${cur_digital:-0} != 1 ]]; then
            mkdir -p "$STATE_DIR" 2>/dev/null
            printf 'prev=%s\nset=%s\n' "$cur" "$name" > "$PREV"
        fi
    fi

    wpctl set-default "$id" 2>/dev/null &&
        echo "synui-hdmi-audio: sound -> $desc" ||
        echo "synui-hdmi-audio: could not set default sink $id" >&2
}

cmd_restore() {
    # Still a display attached? Then this was one screen of several going away
    # and there is nothing to restore to.
    has_live_eld && { echo "synui-hdmi-audio: a display with audio is still attached"; return 0; }

    [[ -f $PREV ]] || return 0
    local prev="" set=""
    while IFS='=' read -r k v; do
        case "$k" in prev) prev=$v ;; set) set=$v ;; esac
    done < "$PREV"
    [[ -n $prev ]] || return 0

    # ⚠ Only undo what we did. If the default is no longer the sink this script
    # set, the user has moved it since and restoring would override a choice
    # they made by hand. The saved state goes either way — it has served its
    # purpose and keeping it would fire at the next unplug instead.
    local cur; cur=$(default_sink_name)
    if [[ -n $set && -n $cur && $cur != "$set" ]]; then
        rm -f "$PREV"
        echo "synui-hdmi-audio: default sink was changed by hand — leaving it"
        return 0
    fi

    local id; id=$(sink_id_by_name "$prev")
    if [[ -z $id ]]; then
        rm -f "$PREV"
        echo "synui-hdmi-audio: previous sink '$prev' is gone" >&2
        return 0
    fi

    wpctl set-default "$id" 2>/dev/null &&
        echo "synui-hdmi-audio: sound -> $prev" ||
        echo "synui-hdmi-audio: could not restore sink $id" >&2
    rm -f "$PREV"
}

cmd_status() {
    echo "elds:"
    live_elds | sed 's/^/  /'
    [[ -n "$(live_elds)" ]] || echo "  (none — no display with audio attached)"
    echo "default=$(default_sink_name)"
    echo "sinks:"
    pw_sinks | awk -F'\t' '{ printf "  %-6s %-52s digital=%s\n", $1, $2, $4 }'
    [[ -f $PREV ]] && { echo "saved:"; sed 's/^/  /' "$PREV"; }
    return 0
}

case "${1:-status}" in
    follow)  cmd_follow  ;;
    restore) cmd_restore ;;
    status)  cmd_status  ;;
    -h|--help) sed -n '2,7p' "$0" ;;
    *) echo "synui-hdmi-audio: unknown command '$1'" >&2; exit 2 ;;
esac
