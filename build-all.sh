#!/usr/bin/env bash
# SynapseOS build script — builds all components in order
set -e

BASE="$(cd "$(dirname "$0")" && pwd)"

# Which llama backend the synapse-llama package is built from. The staged tree
# (llama-staging-$SYNAPSE_LLAMA_BACKEND) is produced by:
#     sudo archiso/build.sh --gpu=cuda --llama-only
#
# Default to whatever THIS host already runs, so a routine ./build-all.sh on a
# GPU box rebuilds the GPU variant instead of quietly trying to replace it with
# a CPU one. (It would not be quiet — synapse-llama-cuda conflicts with
# synapse-llama, so pacman would refuse — but failing a full build over a
# defaulted variable is a lousy way to find that out.)
if [ -z "${SYNAPSE_LLAMA_BACKEND:-}" ]; then
    if pacman -Qq synapse-llama-cuda >/dev/null 2>&1; then
        SYNAPSE_LLAMA_BACKEND=cuda
    else
        SYNAPSE_LLAMA_BACKEND=cpu
    fi
fi
export SYNAPSE_LLAMA_BACKEND
echo "llama backend: ${SYNAPSE_LLAMA_BACKEND}"

# Optional component filter: `./build-all.sh synapd` rebuilds only synapd.
# No arguments = build everything, which is the default and unchanged.
# Rebuilding one component must go through build_component, not a hand-rolled
# tar — the tarball regeneration below is what stops makepkg silently packaging
# the last build's source.
ONLY=("$@")
KNOWN=(synapse-llama synapd synsh synnet synguard synui synapse_kmod
       syn syn-model syn-install syn-firstboot nexus-chat tepris)
for _c in "${ONLY[@]}"; do
    case " ${KNOWN[*]} " in
        *" $_c "*) ;;
        *) echo "unknown component: $_c" >&2
           echo "known: ${KNOWN[*]}" >&2
           exit 1 ;;
    esac
done

want() {
    [ ${#ONLY[@]} -eq 0 ] && return 0
    local c
    for c in "${ONLY[@]}"; do
        [ "$c" = "$1" ] && return 0
    done
    return 1
}

build_component() {
    local name=$1
    want "$name" || return 0
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
    [ -d "$name/sysusers" ] && dirs+=("$name/sysusers/")
    [ -d "$name/tmpfiles" ] && dirs+=("$name/tmpfiles/")
    [ -d "$name/rules" ]   && dirs+=("$name/rules/")
    [ -d "$name/protocols" ] && dirs+=("$name/protocols/")
    [ -d "$name/tests" ]   && dirs+=("$name/tests/")
    # synapse_kmod extras
    [ -f "$name/Makefile" ]  && dirs+=("$name/Makefile")
    [ -f "$name/dkms.conf" ] && dirs+=("$name/dkms.conf")
    [ -d "$name/hooks" ]     && dirs+=("$name/hooks/")
    [ -d "$name/tools" ]     && dirs+=("$name/tools/")
    [ -f "$name/synapse_kmod.install" ] && dirs+=("$name/synapse_kmod.install")
    [ -f "$name/HARDENING.md" ]         && dirs+=("$name/HARDENING.md")

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
    want "$name" || return 0
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

# Retire the old ld.so.conf entries that pointed the DYNAMIC LINKER at this
# build tree. They made a root daemon (synapd) load its core libraries out of a
# user's $HOME, so a `git clean` or an ISO rebuild could take synapd down — and
# they let a stale unowned copy in /usr/lib shadow the real build in the ld.so
# cache, which is how synapd silently ran CPU-only libllama for a day while
# reporting GPU offload. synapse-llama installs into /usr/lib and owns the files.
for _stale in /etc/ld.so.conf.d/llama.conf /etc/ld.so.conf.d/synapse-llama.conf; do
    if [ -e "$_stale" ] && grep -q "llama-staging" "$_stale" 2>/dev/null; then
        echo "removing stale $_stale (pointed the linker at the build tree)"
        sudo rm -f "$_stale"
        sudo ldconfig
    fi
done

# Create synsh config
sudo mkdir -p /etc/synsh
sudo touch /etc/synsh/synshrc

# llama.cpp libraries — must be built and installed BEFORE synapd, which now
# depends on it. build_script_pkg (no source tarball): it packages the tree that
# archiso/build.sh staged.
build_script_pkg synapse-llama

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
