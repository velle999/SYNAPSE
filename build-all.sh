#!/usr/bin/env bash
# SynapseOS build script — builds all components in order
set -e

BASE="$(cd "$(dirname "$0")" && pwd)"
LLAMA_INC="${LLAMA_INC:-$BASE/llama-staging/usr/include}"
LLAMA_LIB="${LLAMA_LIB:-$BASE/llama-staging/usr/lib}"

build_component() {
    local name=$1
    echo "=== Building $name ==="
    cd "$BASE/$name"

    # Create tarball
    cd "$BASE"

    # Collect directories that exist
    local dirs=("$name/src/" "$name/include/" "$name/meson.build")
    [ -d "$name/config" ] && dirs+=("$name/config/")
    [ -d "$name/systemd" ] && dirs+=("$name/systemd/")
    [ -d "$name/rules" ] && dirs+=("$name/rules/")

    tar czf "$name/$name-0.1.0.tar.gz" \
        --transform "s|^$name/|$name-0.1.0/|" \
        --exclude="$name/src/$name-*" \
        --exclude="$name/pkg" \
        --exclude="$name/src/pkg" \
        --exclude="$name/*.pkg.tar*" \
        --exclude="$name/*.tar.gz" \
        "${dirs[@]}" 2>/dev/null || true

    cd "$BASE/$name"
    makepkg -sf --noconfirm
    local pkg
    pkg=$(ls -1t "$name"-*.pkg.tar.zst 2>/dev/null | head -1)
    if [ -n "$pkg" ]; then
        sudo pacman -U --noconfirm --overwrite '*' "$pkg"
        echo "=== $name installed ==="
    else
        echo "=== $name: no package built ==="
    fi
}

build_script_pkg() {
    local name=$1
    echo "=== Building $name ==="
    cd "$BASE/$name"
    makepkg -sf --noconfirm
    local pkg
    pkg=$(ls -1t "$name"-*.pkg.tar.zst 2>/dev/null | head -1)
    if [ -n "$pkg" ]; then
        sudo pacman -U --noconfirm --overwrite '*' "$pkg"
        echo "=== $name installed ==="
    else
        echo "=== $name: no package built ==="
    fi
}

# Add llama lib to ld path
echo "$LLAMA_LIB" | sudo tee /etc/ld.so.conf.d/llama.conf
sudo ldconfig

# Create synsh config
sudo mkdir -p /etc/synsh
sudo touch /etc/synsh/synshrc

# Build C components
build_component synapd
build_component synsh
build_component synnet
build_component synguard
build_component synui

# Build script packages
build_script_pkg syn
build_script_pkg syn-model
build_script_pkg syn-install
build_script_pkg syn-firstboot

echo ""
echo "=== All components built! ==="
echo "Run: sudo systemctl start synapd"
echo "Then: synsh"
