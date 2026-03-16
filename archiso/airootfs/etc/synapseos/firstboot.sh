#!/usr/bin/env bash
# /etc/synapseos/firstboot.sh
# Runs once on first boot of the installed system.
# Triggered by synapseos-firstboot.service.
#
# Tasks:
#   1. Download AI model if not present (large — deferred from installer)
#   2. Build synapse_kmod against the installed kernel (if using DKMS)
#   3. Generate synguard baseline (first 60s of system activity)
#   4. Show welcome notification via synui
#
# SynapseOS Project — GPLv2

set -euo pipefail

CONF="/etc/synapseos/firstboot.conf"
STATE="/var/lib/synapseos/firstboot.done"
LOG="/var/log/synapseos-firstboot.log"
MODEL_DIR="/var/lib/synapd/models"
MODEL_NAME="synapse-7b-q4_k_m.gguf"

exec > >(tee -a "$LOG") 2>&1

echo "=== SynapseOS first boot: $(date) ==="

# Only run once
[[ -f "$STATE" ]] && exit 0

# ── Source config ──────────────────────────────────────────────
DOWNLOAD_MODEL=false
[[ -f "$CONF" ]] && source "$CONF"

# ── Download model if needed ───────────────────────────────────
if [[ "$DOWNLOAD_MODEL" == "true" ]] && \
   [[ ! -f "${MODEL_DIR}/${MODEL_NAME}" ]]; then

    echo "Downloading AI model (synapse-7b, ~4.1GB)..."
    echo "This may take several minutes on first boot."

    mkdir -p "$MODEL_DIR"

    HF_URL="https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF/resolve/main/mistral-7b-instruct-v0.2.Q4_K_M.gguf"

    curl -L \
        --retry 10 \
        --retry-delay 30 \
        --connect-timeout 60 \
        -o "${MODEL_DIR}/${MODEL_NAME}.tmp" \
        "$HF_URL"

    mv "${MODEL_DIR}/${MODEL_NAME}.tmp" "${MODEL_DIR}/${MODEL_NAME}"
    echo "Model downloaded: $(du -h "${MODEL_DIR}/${MODEL_NAME}" | cut -f1)"
fi

# ── Register and build synapse_kmod via DKMS ─────────────────
KERNEL_VER="$(uname -r)"
KMOD_SRC="/usr/src/synapse_kmod-0.1.0"

if [[ -d "$KMOD_SRC" ]] && command -v dkms &>/dev/null; then
    echo "Registering synapse_kmod with DKMS for kernel ${KERNEL_VER}..."
    dkms add synapse_kmod/0.1.0 2>/dev/null || true
    if dkms build synapse_kmod/0.1.0 -k "$KERNEL_VER" && \
       dkms install synapse_kmod/0.1.0 -k "$KERNEL_VER"; then
        echo "synapse_kmod built and installed via DKMS"
        depmod -a "$KERNEL_VER"
    else
        echo "Warning: DKMS build failed — module may not load"
    fi
else
    echo "Note: DKMS or synapse_kmod source not available (non-fatal)"
fi

# ── Load kernel module ────────────────────────────────────────
modprobe synapse_kmod 2>/dev/null && \
    echo "synapse_kmod loaded" || \
    echo "synapse_kmod not available (non-fatal)"

# ── Start synapd ──────────────────────────────────────────────
systemctl start synapd || echo "synapd start failed"

# Wait for synapd to be ready
for i in $(seq 1 30); do
    if systemctl is-active --quiet synapd; then
        echo "synapd ready after ${i}s"
        break
    fi
    sleep 1
done

# ── Switch synguard to enforce mode (optional, prompts user) ──
# Default is audit — user can enable enforce via:
#   systemctl edit synguard → add --mode enforce
echo "synguard is in AUDIT mode (safe). To enable enforcement:"
echo "  sudo systemctl edit synguard"
echo "  Add: ExecStart= then ExecStart=/usr/bin/synguard --mode enforce ..."

# ── Mark firstboot complete ────────────────────────────────────
mkdir -p "$(dirname "$STATE")"
echo "firstboot completed: $(date)" > "$STATE"

echo "=== First boot setup complete ==="
