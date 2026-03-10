#!/usr/bin/env bash
# SynapseOS build script — builds all components in order
set -e

BASE="$(cd "$(dirname "$0")" && pwd)"
LLAMA_INC="${LLAMA_INC:-/home/velle/Downloads/SYNAPSE-main/build/llama-staging/usr/include}"
LLAMA_LIB="${LLAMA_LIB:-/home/velle/Downloads/SYNAPSE-main/build/llama-staging/usr/lib}"

build_component() {
    local name=$1
    echo "=== Building $name ==="
    cd "$BASE/$name"

    # Create tarball
    cd "$BASE"
    tar czf "$name/$name-0.1.0.tar.gz" \
        --transform "s|^$name/|$name-0.1.0/|" \
        --exclude="$name/src/$name-*" \
        --exclude="$name/pkg" \
        --exclude="$name/src/pkg" \
        --exclude="$name/*.pkg.tar*" \
        --exclude="$name/*.tar.gz" \
        "$name/src/" "$name/include/" "$name/meson.build" \
        $([ -d "$name/config" ] && echo "$name/config/") \
        $([ -d "$name/systemd" ] && echo "$name/systemd/") 2>/dev/null || true

    cd "$BASE/$name"
    makepkg -sf --noconfirm
    sudo pacman -U --noconfirm --overwrite '/etc/shells' \
        "$name-0.1.0-1-x86_64.pkg.tar.zst"
    echo "=== $name installed ==="
}

# Add llama lib to ld path
echo "$LLAMA_LIB" | sudo tee /etc/ld.so.conf.d/llama.conf
sudo ldconfig

# Create synsh config
sudo mkdir -p /etc/synsh
sudo touch /etc/synsh/synshrc

build_component synapd
build_component synsh

echo ""
echo "=== Build complete! ==="
echo "Run: sudo synapd --foreground --model /path/to/model.gguf"
echo "Then: synsh"
