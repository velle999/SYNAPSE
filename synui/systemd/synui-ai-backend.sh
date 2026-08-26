#!/usr/bin/env bash
# synui-ai-backend — toggle synapd between GPU and CPU inference.
#
# The welcome guide's "AI Backend" row (Super+Escape) runs `synui-ai-backend
# toggle`. It works by writing a systemd drop-in that overrides synapd's
# ExecStart --gpu-layers, recording the choice in /run/synapd/backend (which
# synui reads to label the row), then restarting synapd so the model reloads
# onto the chosen device.
#
#   gpu  → --gpu-layers -1  (auto-detect; offloads to the GPU when present)
#   cpu  → --gpu-layers 0   (force CPU-only)
#   off  → synapd.socket AND synapd.service stopped and MASKED (frees the
#          model's RAM/VRAM; no inference at all, nothing can socket-activate it
#          back, and it stays off across a reboot)
#
# Needs root (writes /etc/systemd, restarts synapd) — synui.service runs as
# root, so the menu action has the privilege it needs.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

# Every path this script WRITES hangs off one prefix, which is empty in
# production and a scratch directory under the test harness — one knob, so a
# test cannot half-redirect itself and stop the real daemon or delete the real
# desktop's state. The same escape hatch synui's own SYNUI_CONFIG is.
ROOT="${SYNUI_AI_ROOT:-}"

DROPIN_DIR="$ROOT/etc/systemd/system/synapd.service.d"
DROPIN="$DROPIN_DIR/backend.conf"
# /etc, not /run: /run is a tmpfs, so the choice recorded there was gone by the
# time the desktop came up, and the row read "auto" on a machine its owner had
# set to off. The state has to be as durable as the thing it describes, and what
# it describes is now a mask in /etc/systemd.
STATE="$ROOT/etc/synapd/backend"
LEGACY_STATE="$ROOT/run/synapd/backend"   # pre-335; read, then removed
# Not prefixed: these two go INTO the unit file, where they have to be the paths
# systemd will actually open.
MODEL=/var/lib/synapd/models/synapse.gguf
SOCK=/run/synapd/synapd.sock

usage() { echo "usage: synui-ai-backend {gpu|cpu|off|toggle|status}" >&2; exit 2; }

# A masked unit is off whatever any file says, so systemd is asked first: a mask
# put there by hand must not leave the toggle cycling gpu → cpu → off through a
# daemon that cannot start. Only then the recorded gpu/cpu choice.
current() {
    if [ "$(systemctl is-enabled synapd.service 2>/dev/null || true)" = masked ]; then
        echo off
        return
    fi
    if [ -r "$STATE" ]; then cat "$STATE"
    elif [ -r "$LEGACY_STATE" ]; then cat "$LEGACY_STATE"   # pre-335 installs
    else echo auto
    fi
}

apply() {
    local mode=$1 layers
    case "$mode" in
        gpu) layers=-1 ;;
        cpu) layers=0  ;;
        *)   usage ;;
    esac
    mkdir -p "$DROPIN_DIR"

    # This drop-in owns the WHOLE ExecStart, so rewriting it from scratch threw
    # away every other flag on the line. Anyone who had tuned --context or
    # --temperature lost it silently the next time they toggled GPU/CPU, and
    # nothing said so — the daemon just came back at its defaults.
    #
    # So: only the device changes. Everything else on an existing line is kept
    # exactly as it was, and the full default line is written only when there is
    # no drop-in yet.
    local exec_line
    exec_line=$(grep -m1 '^ExecStart=/usr/bin/synapd ' "$DROPIN" 2>/dev/null || true)
    if [ -n "$exec_line" ]; then
        if printf '%s' "$exec_line" | grep -q -- '--gpu-layers'; then
            exec_line=$(printf '%s' "$exec_line" |
                        sed -E "s/--gpu-layers[= ]-?[0-9]+/--gpu-layers $layers/")
        else
            exec_line="$exec_line --gpu-layers $layers"
        fi
    else
        exec_line="ExecStart=/usr/bin/synapd --foreground --model $MODEL --socket $SOCK --threads 4 --context 4096 --gpu-layers $layers"
    fi

    cat > "$DROPIN" <<EOF
# Managed by synui-ai-backend — the ExecStart device flag is rewritten on every
# GPU/CPU toggle. Other flags on the line (--context, --temperature, --threads)
# are PRESERVED across toggles, so tuning them here does stick.
[Service]
ExecStart=
$exec_line
EOF
    mkdir -p "$(dirname "$STATE")"
    echo "$mode" > "$STATE"
    rm -f "$LEGACY_STATE" || true    # one store; see current()
    systemctl daemon-reload
    # Coming back from 'off' the units are MASKED, and a masked unit refuses to
    # start — so the unmask is what makes gpu/cpu the inverse of off rather than
    # a command that reports success while systemd ignores it. Harmless when
    # nothing is masked.
    systemctl unmask synapd.socket synapd.service
    # Coming back from 'off' the sockets are stopped too, so start them
    # explicitly. (restart would pull synapd.socket in via Requires=, but saying
    # it here means the two verbs are symmetric and neither depends on that
    # resolution order.) The bridge only comes back if it was enabled.
    systemctl start synapd.socket
    systemctl restart synapd.service
    if systemctl is-enabled --quiet synapd-bridge.socket 2>/dev/null; then
        systemctl start synapd-bridge.socket || true
    fi
    echo "synapd backend → $mode (gpu-layers=$layers)"
}

# Kill the AI backend outright: stop synapd so the model's RAM/VRAM is freed and
# no inference happens at all. Deliberately NOT game mode — this must not light
# the waybar game-mode glyph or inhibit idle; it is only the daemon going down.
# The drop-in is left in place, so the next gpu/cpu apply restarts synapd on the
# device it last used. Record "off" so synui labels the row and the toggle cycle
# knows where it is.
#
# The SOCKETS go down with the service, and that is the whole fix. Stopping
# synapd.service alone leaves two listeners that each start it straight back up:
#
#   synapd.socket        — synapd.service Requires= it, so the next LOCAL client
#                          to connect socket-activates the daemon, in the same
#                          second. This is what "the AI backend is set to off but
#                          chibi turns it on anyway" was: chibi connects on its
#                          first message and off silently undid itself. Game mode
#                          had the identical bug, fixed there in synui pkgrel 198;
#                          this path was missed.
#   synapd-bridge.socket — 0.0.0.0:11435. synapd-bridge.service Requires=
#                          synapd.service, so any client on the LAN (the Pi's
#                          chibi polls this box) resurrects synapd over the
#                          network, where nothing on this machine can see why.
#                          Only touched when it is enabled; plenty of installs
#                          never turn the bridge on.
#
# Off must mean off until the user picks gpu/cpu again.
off_backend() {
    mkdir -p "$(dirname "$STATE")"
    echo off > "$STATE"
    rm -f "$LEGACY_STATE" || true
    if systemctl is-enabled --quiet synapd-bridge.socket 2>/dev/null; then
        systemctl stop synapd-bridge.socket synapd-bridge.service || true
    fi
    systemctl stop synapd.socket synapd.service

    # And the fourth resurrection path, the one a stop cannot close: the NEXT
    # BOOT. synapd.service is enabled (multi-user.target.wants), and synguard,
    # synnet and synui each Wants= it, so a reboot started it again no matter
    # what had been stopped — reported as "the AI backend toggle didn't stay off
    # through reboot". `disable` would not have been enough either: enablement
    # is not what a Wants= from another unit consults.
    #
    # Masking is, and it is the only verb that is. The bridge is deliberately
    # left alone: its service is After= synapd, not Requires= it (see
    # synapd-bridge.service), so with synapd masked a LAN client reaches a
    # listener that has nothing to proxy to rather than one that starts the
    # daemon.
    systemctl mask synapd.socket synapd.service
    # Say what actually happened, not what was intended. A stop that did not
    # hold looks identical from here unless it is checked — and one resurrection
    # path survives this one: synguard, synnet and synui all carry
    # Wants=synapd.service, so an automatic Restart= of any of them re-enqueues
    # it. Rare on healthy hardware (NRestarts stays 0), common in a VM.
    if systemctl is-active --quiet synapd.service; then
        echo "synapd backend → off requested, but synapd is STILL RUNNING" >&2
        exit 1
    fi
    echo "synapd backend → off (stopped, sockets closed)"
}

# The welcome guide / control panel row fires this as the SESSION USER: synui
# normally runs as root (synui.service), but under a greetd session it runs as
# the logged-in user, who cannot write the systemd drop-in, restart synapd, or
# write /run/synapd/backend. There is no polkit agent here to prompt, so the
# mutating verbs re-exec under `sudo -n` — a NOPASSWD rule in /etc/sudoers.d
# (shipped by syn-install) scopes it to exactly this command. Skipped when
# already root, and 'status' is left alone: it only reads a world-readable file.
case "${1:-status}" in
    gpu|cpu|off|toggle)
        [ "$(id -u)" -eq 0 ] || exec sudo -n /usr/bin/synui-ai-backend "$@" ;;
esac

case "${1:-status}" in
    status)  current ;;
    gpu|cpu) apply "$1" ;;
    off)     off_backend ;;
    # Cycle GPU → CPU → off → GPU. From auto/unknown, start at CPU (unchanged
    # first step); the off state is reached one more toggle along than before.
    toggle)
        case "$(current)" in
            gpu) apply cpu ;;
            cpu) off_backend ;;
            off) apply gpu ;;
            *)   apply cpu ;;
        esac
        ;;
    *)       usage ;;
esac
