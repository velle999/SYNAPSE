#!/usr/bin/env bash
# synui-ai-backend — toggle synapd between GPU and CPU inference.
#
# The welcome menu's "AI Backend" row (Super+Escape) runs `synui-ai-backend
# toggle`. It works by writing a systemd drop-in that overrides synapd's
# ExecStart --gpu-layers, recording the choice in /run/synapd/backend (which
# synui reads to label the row), then restarting synapd so the model reloads
# onto the chosen device.
#
#   gpu  → --gpu-layers -1  (auto-detect; offloads to the GPU when present)
#   cpu  → --gpu-layers 0   (force CPU-only)
#
# Needs root (writes /etc/systemd, restarts synapd) — synui.service runs as
# root, so the menu action has the privilege it needs.
#
# SynapseOS Project — GPLv2
set -euo pipefail

DROPIN_DIR=/etc/systemd/system/synapd.service.d
DROPIN="$DROPIN_DIR/backend.conf"
STATE=/run/synapd/backend
MODEL=/var/lib/synapd/models/synapse.gguf
SOCK=/run/synapd/synapd.sock

usage() { echo "usage: synui-ai-backend {gpu|cpu|toggle|status}" >&2; exit 2; }

current() { [ -r "$STATE" ] && cat "$STATE" || echo auto; }

apply() {
    local mode=$1 layers
    case "$mode" in
        gpu) layers=-1 ;;
        cpu) layers=0  ;;
        *)   usage ;;
    esac
    mkdir -p "$DROPIN_DIR"
    cat > "$DROPIN" <<EOF
# Managed by synui-ai-backend — do not edit by hand.
# Overrides synapd's inference device (GPU vs CPU); see synui welcome menu.
[Service]
ExecStart=
ExecStart=/usr/bin/synapd --foreground --model $MODEL --socket $SOCK --threads 4 --context 4096 --gpu-layers $layers
EOF
    mkdir -p "$(dirname "$STATE")"
    echo "$mode" > "$STATE"
    systemctl daemon-reload
    systemctl restart synapd
    echo "synapd backend → $mode (gpu-layers=$layers)"
}

case "${1:-status}" in
    status)  current ;;
    gpu|cpu) apply "$1" ;;
    toggle)  [ "$(current)" = cpu ] && apply gpu || apply cpu ;;
    *)       usage ;;
esac
