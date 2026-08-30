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
       vibe samsung-m2020 syn-arsenal synpkg synfiles syn-settings syn-disks syn-cal
       syn-vault
       syn-confine syn-edit syntty limine-mkinitcpio-hook fetch
       synapse-wallpapers syn-arcade cliamp synstudio syn-gfn)
for _c in "${ONLY[@]}"; do
    case " ${KNOWN[*]} " in
        *" $_c "*) ;;
        *) echo "unknown component: $_c" >&2
           echo "known: ${KNOWN[*]}" >&2
           exit 1 ;;
    esac
done

# Components that DEPEND on another component of this repo, as "<pkg> <needs>".
#
# A selective build of the dependant alone dies inside makepkg with
#
#   ==> ERROR: 'pacman' failed to install missing dependencies.
#     -> syn-confine
#
# which says nothing about this script, nothing about ordering, and leaves
# somebody to work out that a second component had to be named on the command
# line. Worse, syn-update computes its own build set from the COMPONENTS list
# in the syn-update that is INSTALLED — so a machine running an older one asks
# for the dependant without the dependency and fails the same way through no
# fault of the person running it.
#
# So the need is pulled in rather than diagnosed. Only when it is not already
# installed: rebuilding a component nobody asked about would be a surprise, and
# an installed one already satisfies makepkg.
LOCAL_DEPS=(
    "vibe syn-confine"     # vibe's bash tool refuses to run without the sandbox
)
if [ ${#ONLY[@]} -gt 0 ]; then
    for _d in "${LOCAL_DEPS[@]}"; do
        _pkg=${_d%% *}; _needs=${_d##* }
        case " ${ONLY[*]} " in *" $_pkg "*) ;; *) continue ;; esac
        case " ${ONLY[*]} " in *" $_needs "*) continue ;; esac
        pacman -Qq "$_needs" >/dev/null 2>&1 && continue
        echo "note: $_pkg needs $_needs, which is not installed — building it too"
        ONLY+=("$_needs")
    done
fi

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
    # ⛔ THE OPTIONS FILE TRAVELS WITH meson.build. This list is an allowlist, so
    # a file it does not name is simply absent from the tarball — and a
    # meson.build calling get_option() for an option nothing defines fails at
    # `meson setup` inside makepkg, long after the tree it was tested in built
    # fine. syn-cal shipped a client id that way and the update died on
    # `Unknown option`. preflight.sh gates this now.
    [ -f "$name/meson_options.txt" ] && dirs+=("$name/meson_options.txt")
    [ -f "$name/meson.options" ]     && dirs+=("$name/meson.options")
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

# Bring an ALREADY-INSTALLED vendored package up to the tree's version.
#
# For limine-mkinitcpio-hook, which is vendored rather than ours. Two things
# make it unlike every other rule here:
#
# 1. IT MUST NEVER ARRIVE UNINVITED. It depends on `limine`, so installing it
#    would put a second bootloader on a GRUB box, and it ships
#    /etc/pacman.d/hooks/90-mkinitcpio-install.hook, which SHADOWS Arch's own
#    hook of that name, plus an interactive /usr/local/bin/mkinitcpio ahead of
#    the real one on PATH. On a machine that does not boot limine that is a
#    broken initramfs pipeline delivered as a build artifact. It reaches limine
#    machines deliberately — syn-install during a limine install, or
#    syn-settings' Kernel pane — and this script only ever moves it FORWARD.
#    That is the same policy as syn-update's NEVER_ADD, enforced where the
#    building happens, so `./build-all.sh` with no arguments cannot install it.
#
# 2. Building it is a GraalVM native-image build: a ~250 MB download and
#    several minutes. Nobody should pay that to re-package the version they
#    already run, so the installed version is compared first. The tree's
#    version is composed from _pkgver, so it is read by SOURCING the PKGBUILD,
#    not scraped — scraping it is what broke syn-update (it read the literal
#    string ${_pkgver}${_extver}, which vercmp ranks above every real version).
build_vendored_pkg() {
    local name=$1
    want "$name" || return 0

    local inst avail
    inst=$(pacman -Q "$name" 2>/dev/null | awk '{print $2}')
    if [ -z "$inst" ]; then
        echo "=== $name: not installed here; not adding it (see the comment above) ==="
        return 0
    fi

    # set +e +u INSIDE the subshell: this script runs under `set -e`, and a
    # PKGBUILD is not written to be sourced by it — one command returning
    # non-zero would take the whole build down instead of just this read.
    avail=$( set +e +u; cd "$BASE/$name" && . ./PKGBUILD >/dev/null 2>&1 &&
             printf '%s-%s' "$pkgver" "$pkgrel" ) || avail=""
    case "$avail" in
        *'$'*|-*|"")
            echo "=== $name: cannot read the tree's version, leaving $inst alone ==="
            return 0 ;;
    esac

    if [ "$(vercmp "$avail" "$inst")" -le 0 ]; then
        echo "=== $name: $inst is already current (tree has $avail) ==="
        return 0
    fi

    echo "=== $name: $inst -> $avail ==="
    build_script_pkg "$name"
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

# syntty — the terminal. meson C again, and the generic tarball collector above
# takes src/, include/, meson.build, data/ and tests/, which is all of it.
#
# ⚠ BEFORE synui, which DEPENDS ON IT since syntty replaced kitty in synui's
# depends. It used to be built near the end, next to syn-arcade, which was
# correct for as long as nothing required it — `pacman -U` on synui would now
# refuse on a machine that has not got syntty yet, and a fresh build host is
# exactly that machine. tools/preflight.sh checks this ordering now.
#
# ⚠ ITS check() NEEDS A COMPOSITOR AND HAS NONE. The suite covers the parser,
# the grid, the renderer and the mouse and paste encoders with no seat and no
# display; the window tests start their own headless cage and SKIP where cage
# is not installed, which is what makes this buildable on a machine that is not
# somebody's desktop. Budget about forty seconds for it — meson's own default
# would kill a passing suite at thirty and report the kill as a build failure,
# so syntty's meson.build states a timeout of its own.
build_component syntty

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

# fetch — areofyl/fetch, which is "About OS". Same shape as the two above: a
# pinned upstream commit plus two local patches applied in prepare(), so it
# needs network, which is precisely why nexus-chat sets the precedent that
# network at build time does not disqualify a component from updating. It is in
# packages.x86_64, so every installed system HAS it; before this rule it was on
# every machine and updatable on none.
build_script_pkg fetch

# cliamp — bjarneo/cliamp, the terminal music player big screen mode DRIVES.
# Same shape as the three above: a pinned upstream tag fetched at build time, so
# it needs network. Third-party and MIT; it is packaged because syn-arcade's
# whole Music path is built on it and it was an unowned binary in ~/.local/bin
# on exactly one machine.
build_script_pkg cliamp

# synapse-wallpapers — our own Wallpaper Engine wallpapers (dendrite + Tux).
# source=() and the renders are derived from the checkout at build time, so
# there is nothing to download and nothing to stage. It does NOT need
# linux-wallpaperengine to build (that is the runtime consumer); the
# makedepends are python-pillow, librsvg, ffmpeg and adwaita-fonts, all
# ordinary packages. Order does not matter — nothing depends on it.
build_script_pkg synapse-wallpapers

# syn-confine BEFORE vibe, which DEPENDS on it — the same rule as scenefx
# before synui above, and it is load-bearing rather than tidy. vibe's bash tool
# runs every model-proposed command inside syn-confine's Landlock sandbox and
# refuses to run one at all without it, so the dependency is hard. Built after
# vibe, makepkg cannot resolve it and the whole run dies at:
#
#   ==> ERROR: 'pacman' failed to install missing dependencies.
#     -> syn-confine
#
# One C file against libc, so it can sit anywhere above here.
build_component syn-confine

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
# makes a folder open in it. Dolphin came off the ISO and out of the installer
# on 2026-08-13; it stays an optdepend and still works if installed.
build_component synfiles

# syn-settings — the settings app. build_component, not build_script_pkg: it is
# a meson C project whose tarball has to be regenerated from the tree, the same
# shape as synfiles and synpkg.
build_component syn-settings

# syn-disks — the disk utility. Same shape again: meson C plus a quickshell
# front-end, built from a source tarball the generic collector above assembles
# from src/, include/, meson.build, data/ and tests/.
#
# ⚠ Its check() runs a suite that drives the binary against a FIXTURE of
# /sys/class/block and a fake /proc/self/mounts inside a mktemp -d. Nothing in
# it can reach a real block device — format is exercised only through
# --dry-run, and mount and eject against a fake udisksctl on PATH — which
# matters here more than anywhere else in this script, because this is the one
# component whose tests run as part of a build that also calls pacman.
build_component syn-disks

# syn-cal — the calendar and schedule planner. meson C, no front-end yet.
#
# ⚠ Its check() starts a Radicale on a loopback port and syncs against it. That
# is deliberate and it is the only way the CalDAV half is worth anything: the
# in-memory remote proves the algorithm agrees with itself, and a real server is
# what found that a percent-encoded UID in an href is refused 403 where the same
# event under a plain name is accepted 201. The suites skip where radicale is
# absent, so a build host without it still builds the component.
#
# ⚠ DBUS is pointed at a dead socket by the tests, so libsecret cannot write
# into the build user's real login keyring.
build_component syn-cal

# syn-vault — the file vault. meson C plus a quickshell window.
#
# ⚠ Its check() runs against a STUB gocryptfs, deliberately. A real vault needs
# /dev/fuse and a user allowed to use it, and a build host has neither — so a
# suite that mounted anything would be one that silently never ran. What the
# stub does prove is everything this component decides on its own: the names it
# refuses, the paths it composes, that the password reaches the backend on stdin
# and never on a command line, and that it will not mount over files somebody
# would then lose.
build_component syn-vault

# syn-edit — the text editor. Same shape again: meson C plus a quickshell
# front-end, built from a source tarball the generic collector above assembles
# from src/, include/, meson.build, data/ and tests/.
#
# Its check() drives the editing engine through `syn-edit run`, which applies
# keys to a file and prints the result instead of saving it, inside a
# mktemp -d. Nothing in it edits anything outside that directory.
build_component syn-edit

# syn-arcade — the game assistant. meson C plus a quickshell front-end, the same
# shape as syn-disks and syn-edit, built from a source tarball the generic
# collector above assembles from src/, include/, meson.build, data/ and tests/.
#
# ⚠ Its check() drives the binary against a FIXTURE of /sys/class/input, and
# every invocation runs with XDG_CONFIG_HOME redirected into a mktemp -d — the
# suite refuses to start if it is not. That matters more here than in most
# components, because three of the four files this binary writes are files the
# LIVE desktop reads, and synuirc is the worst of them: synui reads exactly one
# synuirc, so a two-line file written over the user's would replace the whole
# desktop configuration. The ioctl paths (controller test, rumble, calibrate)
# are never exercised, so a build cannot rumble a pad or rewrite its deadzones
# on the machine running it.
build_component syn-arcade

# synstudio — the darkroom and edit suite. meson C plus a quickshell front-end,
# the same shape again, built from the generic collector's src/, include/,
# meson.build, data/ and tests/.
#
# No ordering constraint: it links libc and libm and nothing else, and reaches
# ffmpeg, ffprobe and dcraw_emu as subprocesses rather than as libraries, so it
# depends on no other component in this script.
#
# Its check() is entirely headless — no display, no compositor, no GPU — which
# is possible because the window is only a renderer over the same command line
# the tests drive. Everything it writes is inside a mktemp -d, and the sidecar
# tests md5sum the source photograph before and after to prove the engine never
# writes to an original. The ffmpeg-dependent assertions (decode, export, the
# LUT-vs-engine PSNR comparison) are guarded by `have ffmpeg` and skip rather
# than fail on a host without it.
build_component synstudio

# syn-gfn — GeForce NOW in whichever Chromium-family browser the machine has.
# Three files and no compiler, so it has no ordering constraint of any kind;
# it is last only because it is the newest.
#
# ⚠ IT DEPENDS ON NO BROWSER, deliberately. The launcher finds one at runtime
# and names what is missing if it cannot, which is the right answer for a
# service not everybody uses — see its PKGBUILD.
build_component syn-gfn

# Vendored, boot-critical where it is installed, and never installed by this
# script. See build_vendored_pkg.
build_vendored_pkg limine-mkinitcpio-hook

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
