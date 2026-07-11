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

    # Clean prior makepkg droppings so the source tarball never
    # re-ingests a previous extraction (this is what caused the
    # nested src/$name-0.1.0/src/$name-0.1.0/... recursion).
    # NB: match only the versioned extraction dir ($name-0.1.0), never the
    # broad "$name-*" glob — that would also match (and delete) any real source
    # file named "$name-*", e.g. a src/synui-foo.c.
    rm -rf "$BASE/$name/pkg" \
           "$BASE/$name/src/$name-0.1.0" \
           "$BASE/$name"/*.pkg.tar.zst \
           "$BASE/$name/$name-0.1.0.tar.gz"

    # Create tarball
    cd "$BASE"

    # Collect directories that exist
    local dirs=()
    [ -d "$name/src" ]     && dirs+=("$name/src/")
    [ -d "$name/include" ] && dirs+=("$name/include/")
    [ -f "$name/meson.build" ] && dirs+=("$name/meson.build")
    [ -d "$name/data" ]    && dirs+=("$name/data/")
    [ -d "$name/config" ]  && dirs+=("$name/config/")
    [ -d "$name/systemd" ] && dirs+=("$name/systemd/")
    [ -d "$name/rules" ]   && dirs+=("$name/rules/")
    [ -d "$name/protocols" ] && dirs+=("$name/protocols/")
    [ -d "$name/tests" ]   && dirs+=("$name/tests/")
    # synapse_kmod extras
    [ -f "$name/Makefile" ]  && dirs+=("$name/Makefile")
    [ -f "$name/dkms.conf" ] && dirs+=("$name/dkms.conf")
    [ -d "$name/hooks" ]     && dirs+=("$name/hooks/")
    [ -d "$name/tools" ]     && dirs+=("$name/tools/")
    [ -f "$name/synapse_kmod.install" ] && dirs+=("$name/synapse_kmod.install")

    tar czf "$name/$name-0.1.0.tar.gz" \
        --transform "s|^$name/|$name-0.1.0/|" \
        --exclude="$name/src/$name-0.1.0" \
        --exclude="$name/pkg" \
        --exclude="$name/src/pkg" \
        --exclude="$name/*.pkg.tar*" \
        --exclude="$name/*.tar.gz" \
        --exclude="$name/*.ko" \
        --exclude="$name/*.o" \
        --exclude="$name/*.mod*" \
        --exclude="$name/modules.order" \
        --exclude="$name/Module.symvers" \
        "${dirs[@]}" 2>/dev/null || true

    cd "$BASE/$name"
    makepkg -sf --noconfirm
    local pkg
    pkg=$(ls -1t "$name"-*.pkg.tar.zst 2>/dev/null | grep -v "^$name-debug-" | head -1)
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
    pkg=$(ls -1t "$name"-*.pkg.tar.zst 2>/dev/null | grep -v "^$name-debug-" | head -1)
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

# syn-install.sh writes a new user's ~/.config/waybar from heredocs that embed
# copies of waybar/synapse-menu-gen.py and waybar/network-menu.xml. They drifted
# once and shipped a start menu whose entries launched the wrong app — so the
# copies are checked, not trusted. `python3 waybar/sync-installer.py` re-splices.
python3 "$BASE/waybar/check-installer-sync.py" || exit 1

# Build C components
build_component synapd
build_component synsh
build_component synnet
build_component synguard
build_component synui

# Build kernel module (DKMS — no meson, uses kbuild)
build_component synapse_kmod

# Build script packages
build_script_pkg syn
build_script_pkg syn-model
build_script_pkg syn-install
build_script_pkg syn-firstboot

# Build web apps. No source tarball to stage — their PKGBUILDs pull a pinned
# commit from each app's own git repo, so these need network at build time.
build_script_pkg nexus-chat
build_script_pkg tepris

echo ""
echo "=== All components built! ==="
echo "Run: sudo systemctl start synapd"
echo "Then: synsh"
