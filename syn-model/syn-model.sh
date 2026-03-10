#!/usr/bin/env bash
# syn model — SynapseOS model manager
set -euo pipefail

VERSION="0.1.0-synapse"
MODEL_DIR="/var/lib/synapd/models"
MODEL_PATH="$MODEL_DIR/synapse.gguf"
SYNAPD_SERVICE="synapd"

# Default model — Mistral 7B Q4_K_M (~4.1GB)
DEFAULT_MODEL_URL="https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF/resolve/main/mistral-7b-instruct-v0.2.Q4_K_M.gguf"

# Smaller option — Phi-3 Mini Q4 (~2.2GB)
PHI3_URL="https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-gguf/resolve/main/Phi-3-mini-4k-instruct-q4.gguf"

# Tiny option — Qwen2 0.5B (~400MB, fast on CPU)
TINY_URL="https://huggingface.co/Qwen/Qwen2-0.5B-Instruct-GGUF/resolve/main/qwen2-0_5b-instruct-q4_k_m.gguf"

usage() {
    cat << HELP
syn-model $VERSION — SynapseOS Model Manager

Usage:
  syn-model download [MODEL]   Download a model (default: mistral-7b)
  syn-model list               List available models
  syn-model status             Show current model status
  syn-model remove             Remove installed model
  syn-model help               This help

Models:
  mistral-7b   Mistral 7B Instruct Q4_K_M (~4.1GB) — recommended
  phi3         Phi-3 Mini 4K Instruct Q4 (~2.2GB) — good balance
  tiny         Qwen2 0.5B Q4 (~400MB) — fast, lower quality

HELP
}

status() {
    echo "SynapseOS Model Status"
    echo "────────────────────────────────"
    if [ -f "$MODEL_PATH" ]; then
        SIZE=$(du -sh "$MODEL_PATH" | cut -f1)
        echo "  Model:    $MODEL_PATH"
        echo "  Size:     $SIZE"
        echo "  Status:   ✓ installed"
    else
        echo "  Model:    not installed"
        echo "  Status:   ✗ AI features disabled"
        echo ""
        echo "  Run: syn-model download"
    fi
    echo ""
    echo "  synapd:   $(systemctl is-active $SYNAPD_SERVICE 2>/dev/null || echo unknown)"
    journalctl -t synapd -n 1 --no-pager 2>/dev/null | grep -o "model=.*" || true
}

list_models() {
    cat << LIST
Available models:

  mistral-7b   Mistral 7B Instruct v0.2 Q4_K_M   ~4.1GB   ★★★★★ recommended
  phi3         Phi-3 Mini 4K Instruct Q4          ~2.2GB   ★★★★☆ good balance
  tiny         Qwen2 0.5B Instruct Q4_K_M         ~400MB   ★★★☆☆ fast/low RAM

LIST
}

download() {
    local model="${1:-mistral-7b}"
    local url

    case "$model" in
        mistral-7b|mistral) url="$DEFAULT_MODEL_URL" ;;
        phi3|phi)           url="$PHI3_URL" ;;
        tiny|qwen)          url="$TINY_URL" ;;
        http*)              url="$model" ;;  # custom URL
        *)
            echo "Unknown model: $model"
            echo "Run: syn-model list"
            exit 1
            ;;
    esac

    echo "SynapseOS Model Downloader"
    echo "────────────────────────────────"
    echo "  Model:  $model"
    echo "  URL:    $url"
    echo "  Dest:   $MODEL_PATH"
    echo ""

    if [ -f "$MODEL_PATH" ]; then
        read -r -p "Model already installed. Replace? [y/N] " confirm
        [[ "$confirm" =~ ^[Yy]$ ]] || exit 0
    fi

    mkdir -p "$MODEL_DIR"

    echo "Downloading... (this may take a while)"
    echo ""

    # Download with progress
    if command -v curl &>/dev/null; then
        curl -L --progress-bar -o "$MODEL_PATH.tmp" "$url"
    elif command -v wget &>/dev/null; then
        wget --progress=bar -O "$MODEL_PATH.tmp" "$url"
    else
        echo "Error: curl or wget required"
        exit 1
    fi

    mv "$MODEL_PATH.tmp" "$MODEL_PATH"
    echo ""
    echo "✓ Model downloaded: $(du -sh "$MODEL_PATH" | cut -f1)"
    echo ""
    echo "Restarting synapd..."
    systemctl restart "$SYNAPD_SERVICE"
    sleep 3
    systemctl status "$SYNAPD_SERVICE" --no-pager -l | tail -5
    echo ""
    echo "✓ SynapseOS AI is ready. Run: synsh"
}

remove() {
    if [ ! -f "$MODEL_PATH" ]; then
        echo "No model installed at $MODEL_PATH"
        exit 0
    fi
    read -r -p "Remove $MODEL_PATH? [y/N] " confirm
    [[ "$confirm" =~ ^[Yy]$ ]] || exit 0
    rm "$MODEL_PATH"
    systemctl restart "$SYNAPD_SERVICE" || true
    echo "✓ Model removed"
}

case "${1:-help}" in
    download) download "${2:-mistral-7b}" ;;
    list)     list_models ;;
    status)   status ;;
    remove)   remove ;;
    help|-h|--help) usage ;;
    *) echo "Unknown command: $1"; usage; exit 1 ;;
esac
