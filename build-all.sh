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
KNOWN=(synapse-llama scenefx0.5 synapd synsh synnet synguard synui synapse_kmod
       syn syn-model syn-install syn-update syn-firstboot nexus-chat tepris
       vibe samsung-m2020 syn-arsenal synpkg synfiles)
for _c in "${ONLY[@]}"; do
    case " ${KNOWN[*]} " in
        *" $_c "*) ;;
        *) echo "unknown component: $_c" >&2
           echo "known: ${KNOWN[*]}" >&2
           exit 1 ;;
    esac
done

# Every name a build rule below actually asks about. KNOWN= gates the argument
# check and the build rules are a SEPARATE list, so a component can sit in the
# first with no entry in the second; this records the second so the check at the
# end can tell them apart. See the guard after the build sequence.
DISPATCHED=()

want() {
    DISPATCHED+=("$1")
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

    # Create tarball.
    #
    # A component that ships its own mktarball.sh wins: that script knows which
    # top-level dirs the component actually compiles and refuses to write a
    # tarball containing its own extraction dir. The generic tar below is a
    # best-effort fallback for everything else, and having both meant two
    # implementations of the same rule, free to drift.
    if [ -x "$BASE/$name/mktarball.sh" ]; then
        ( cd "$BASE/$name" && ./mktarball.sh )
        cd "$BASE/$name"
        makepkg -sf --noconfirm
        local pkg_mk
        pkg_mk=$(ls -1t "$name"-*.pkg.tar.zst 2>/dev/null \
                 | grep -v "^$name-debug-" | head -1)
        if [ -n "$pkg_mk" ]; then
            sudo pacman -U --noconfirm --overwrite '*' "$pkg_mk"
            echo "=== $name installed ==="
        else
            echo "=== $name: no package built ==="
        fi
        return 0
    fi

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

# scenefx BEFORE synui, which links it. It was left out of this script for a
# long time, which meant an installed system could never pick up a scenefx fix:
# syn-update reported it as unsupported for exactly that reason, and "on the ISO
# but frozen forever" is the bug that tool exists to prevent.
build_script_pkg scenefx0.5

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
# syn-update drives THIS script to rebuild components on an installed system,
# so it is deliberately built by it too — if build-all.sh cannot package
# syn-update, syn-update could never have shipped a fix to itself.
build_script_pkg syn-update
build_script_pkg syn-firstboot

# Build web apps. No source tarball to stage — their PKGBUILDs pull a pinned
# commit from each app's own git repo, so these need network at build time.
build_script_pkg nexus-chat
build_script_pkg tepris

# Also shipped on the ISO, and updatable now that they have rules here. vibe
# goes through build_component so its tarball is regenerated from the tree by
# vibe/mktarball.sh -- as a committed artefact it could ship stale code while
# reporting a successful build.
build_component vibe
build_script_pkg samsung-m2020
# syn-arsenal — the BlackArch category browser. Plain script package: its
# PKGBUILD sources the six files by basename out of the component directory, so
# there is no tarball to roll and build_script_pkg is the whole rule.
build_script_pkg syn-arsenal

# synpkg — the SynapseOS package manager. build_component, not
# build_script_pkg: it is C built with meson from a source tarball, so the
# tarball has to be regenerated from the tree or makepkg silently packages the
# previous build's sources. The generic collector above already picks up
# src/, include/, meson.build, data/ and tests/, which is the whole component.
#
# It links libalpm and takes the database lock, so it must not be built while a
# transaction is running -- the same constraint every other rule here already
# lives under, since they all end in `pacman -U`.
build_component synpkg

# synfiles — the SynapseOS file browser. Same shape as synpkg: C, meson, a
# source tarball the generic collector above assembles from src/, include/,
# meson.build, data/ and tests/.
#
# The switch was flipped on 2026-08-10 (velle: "have it replace dolphin and
# drop that from being default"). It is THE file manager now: in syn-update's
# COMPONENTS, in syn-install's SEL_CORE, on the ISO, declaring
# MimeType=inode/directory and shipping the distribution mimeapps.list that
# makes a folder open in it. Dolphin stays installed and stays working; it is
# simply not what a folder opens in any more.
build_component synfiles

# A name in KNOWN= with no build rule above is what this catches.
#
# syn-arsenal sat in KNOWN= and in syn-update's COMPONENTS for two releases with
# no rule here. `./build-all.sh syn-arsenal` therefore passed the argument
# check, matched no rule, built nothing and exited 0 — so `syn-update apply`
# reported success, published nothing, and `syn-update check` listed the same
# pending update again on the next run, forever. That is exactly the
# frozen-component bug syn-update exists to prevent, arriving through the one
# path nothing was watching. Exit status must never say "built" when no rule
# ran.
missing=()
for _c in "${ONLY[@]}"; do
    case " ${DISPATCHED[*]} " in
        *" $_c "*) ;;
        *) missing+=("$_c") ;;
    esac
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "" >&2
    echo "BUG: no build rule for: ${missing[*]}" >&2
    echo "     Listed in KNOWN= but never passed to build_component/build_script_pkg," >&2
    echo "     so nothing was built. Add a rule in the build sequence above." >&2
    exit 1
fi

echo ""
echo "=== All components built! ==="
echo "Run: sudo systemctl start synapd"
echo "Then: synsh"
