#!/usr/bin/env bash
# =============================================================================
# build.sh — SynapseOS ISO Build Script
#
# Orchestrates the complete build pipeline:
#
#   1. Preflight checks (dependencies, disk space)
#   2. Build llama.cpp (with GPU auto-detection)
#   3. Build all SynapseOS packages via PKGBUILD
#   4. Set up a local pacman repository
#   5. Keep the AI model OFF the image (--with-model embeds one)
#   6. Run mkarchiso
#   7. Verify the ISO
#
# Usage:
#   sudo ./build.sh [OPTIONS]
#
# Options:
#   --with-model    Embed a ~4.1 GB gguf in the image. OFF by default since
#                   0.2.8: it was the single largest thing on the ISO, and the
#                   live session can only run it on the CPU — which in a VM is
#                   slow enough to be worse than not offering it at all. The
#                   installer asks which model to fetch and downloads it onto
#                   the target instead, so nothing is lost but the image size.
#   --no-model      (default; kept for compatibility)
#   --gpu=TYPE      Build llama.cpp with GPU backend: cuda, vulkan, or auto.
#                   Default is CPU-only: a CUDA build links the shipped libggml
#                   against the BUILD HOST's driver stack (libcuda.so.1), and
#                   synapd dies with exit 127 on any machine without that
#                   driver — including every VM.
#                   'vulkan' (AMD/Intel) is the exception: it links only the
#                   Vulkan loader, loads with no GPU, and one build runs on any
#                   AMD/Intel card — so it is safe to offer broadly.
#   --no-gpu        (default; kept for compatibility)
#   --llama-only    Build the synapse-llama package for --gpu and stage it,
#                   then stop (no other packages, no ISO). To put a GPU build
#                   on a machine, ./build-all.sh synapse-llama synapd does it
#                   without an ISO build at all.
#   --no-clean      Skip cleaning previous build artifacts
#   --jobs N        Parallel build jobs (default: nproc)
#   --no-sign       Do NOT sign the ISO. Signing is the DEFAULT, and the key is
#                   named by archiso/release-key.fingerprint — a release build
#                   needs nothing typed and nothing remembered. Use this for a
#                   throwaway image, or on a host that has no release key.
#   --sign          Accepted, and does nothing: signing is already the default.
#                   Set SYNAPSE_SIGNING_KEY=<fingerprint> (with `sudo -E`) only
#                   to override WHICH key. See "Signing a release" in README.md.
#   --help          This help
#
# Requirements:
#   - Arch Linux host (or Arch-based)
#   - archiso package installed
#   - ~8GB free disk space (~20GB with --with-model)
#   - Internet access (for package downloads)
#
# Output:
#   out/SynapseOS-0.1.0-YYYYMMDD-x86_64.iso
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
set -euo pipefail
IFS=$'\n\t'

# ── Config ────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

SYNAPSEOS_VERSION="0.1.0"
BUILD_DIR="${SCRIPT_DIR}/build"
OUT_DIR="${SCRIPT_DIR}/out"
WORK_DIR="${SCRIPT_DIR}/work"
# LOCAL_REPO must live inside airootfs/ — mkarchiso only packs the airootfs
# overlay, so that is the only way the package files reach the ISO. The live
# system sees them at /run/archiso/airootfs/local-repo (squashfs root), which
# is what the live pacman.conf and syn-install reference. A repo anywhere
# else in archiso/ silently ships nothing and the installer falls back to
# whatever stale copy exists in the overlay.
LOCAL_REPO="${SCRIPT_DIR}/airootfs/local-repo"
# The llama.cpp release. Pinned in ONE place, synapse-llama/PKGBUILD, which
# also owns the build recipe; read here only to say what is being built.
LLAMA_REF="$(sed -n 's/^_llama_ref=//p' "${PROJECT_ROOT}/synapse-llama/PKGBUILD")"
MODEL_DIR="${SCRIPT_DIR}/airootfs/var/lib/synapd/models"
# Where a gguf is parked when the ISO is not shipping one. It has to be OUTSIDE
# airootfs/: mkarchiso packs that overlay whole, so a model left in MODEL_DIR
# ships whatever this script decided. Outside the overlay and outside build/
# (which --clean wipes), so a 4 GB download survives being switched off and on.
MODEL_CACHE="${SCRIPT_DIR}/model-cache"

# Model to embed — filename must match what synapd.service and syn-model expect
MODEL_NAME="synapse.gguf"
MODEL_HF_REPO="TheBloke/Mistral-7B-Instruct-v0.2-GGUF"
MODEL_HF_FILE="mistral-7b-instruct-v0.2.Q4_K_M.gguf"

JOBS="$(nproc)"
# The ISO ships no model by default — see --with-model in the header.
WITH_MODEL=false
# CPU-only by default — see --gpu note in the header before changing this.
WITH_GPU=cpu
CLEAN=true
# ⚠ SIGNED BY DEFAULT. This was `false`, so every release had to remember both
# `--sign` and `sudo -E` — and 0.3.0 shipped an unsigned ISO because one build
# was started without them. An unsigned release is a decision, not a slip, so
# it is the thing you have to ask for: --no-sign. Which key is named by
# archiso/release-key.fingerprint, so there is nothing left to remember.
SIGN=true
LLAMA_ONLY=false

# ── Colors ────────────────────────────────────────────────────
C_BRAND='\033[38;5;51m'
C_OK='\033[38;5;82m'
C_WARN='\033[38;5;214m'
C_ERR='\033[38;5;196m'
C_DIM='\033[2m'
C_BOLD='\033[1m'
C_RESET='\033[0m'

log()   { echo -e "${C_BRAND}[synapse]${C_RESET} $*"; }
ok()    { echo -e "${C_OK}[  ok  ]${C_RESET} $*"; }
warn()  { echo -e "${C_WARN}[ warn ]${C_RESET} $*"; }
err()   { echo -e "${C_ERR}[ err  ]${C_RESET} $*" >&2; exit 1; }
step()  { echo -e "\n${C_BOLD}${C_BRAND}══ $* ══${C_RESET}"; }

# The Vulkan backend needs BOTH a shader compiler and the Vulkan headers, and
# they come from different packages (shaderc / vulkan-headers) so either can be
# absent alone. A host with shaderc but no headers configured GGML_VULKAN and
# then died inside CMake with "Could NOT find Vulkan (missing:
# Vulkan_INCLUDE_DIR)" -- minutes in, after llama.cpp had already been cloned.
#
# Do NOT reach for `pkg-config --exists vulkan` here: vulkan-icd-loader ships
# vulkan.pc, so it answers YES on a machine with no headers at all. CMake's
# FindVulkan sets Vulkan_INCLUDE_DIR by probing for vulkan/vulkan.h, so probe
# for the same file and be wrong in the same places it is.
have_vulkan_headers() {
    local d
    for d in ${VULKAN_SDK:+"$VULKAN_SDK/include"} /usr/include /usr/local/include; do
        [[ -e "$d/vulkan/vulkan.h" ]] && return 0
    done
    return 1
}
# SPIRV-Headers is a THIRD package, and it became required at llama.cpp b10241:
# ggml/src/ggml-vulkan/CMakeLists.txt now opens with
# find_package(SPIRV-Headers CONFIG REQUIRED). At b8272 it did not, so a host
# that built Vulkan fine before the bump fails after it — with the same shape of
# error the two checks above already exist to prevent, and from a package that
# (like vulkan-headers) ships no binary to probe for.
#
# Probed by its CMake config file, because that is literally what find_package
# looks for: spirv-tools is installed on this box and does NOT satisfy it.
have_spirv_headers() {
    local d
    for d in ${VULKAN_SDK:+"$VULKAN_SDK/share/cmake"} \
             /usr/share/cmake /usr/lib/cmake /usr/local/share/cmake; do
        [[ -e "$d/SPIRV-Headers/SPIRV-HeadersConfig.cmake" ]] && return 0
    done
    return 1
}
have_vulkan_toolchain() {
    command -v glslc &>/dev/null && have_vulkan_headers && have_spirv_headers
}

# ── Argument parsing ──────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --with-model) WITH_MODEL=true ;;
        --no-model)   WITH_MODEL=false ;;
        --no-gpu)     WITH_GPU=cpu ;;
        --gpu=*)      WITH_GPU="${arg#--gpu=}" ;;
        --llama-only) LLAMA_ONLY=true ;;
        --no-clean)   CLEAN=false ;;
        --sign)       SIGN=true ;;   # default; kept so old invocations still work
        --no-sign)    SIGN=false ;;
        --jobs=*)     JOBS="${arg#--jobs=}" ;;
        --help|-h)
            # ⚠ Printed by DELIMITER, not by line number. This was `sed -n
            # '3,35p'` for long enough that the header grew past 35, and --help
            # silently stopped listing --no-clean, --jobs, --llama-only and the
            # signing flags — the options most worth knowing about were the ones
            # it cut. A range that ends where the banner ends cannot drift.
            awk '/^# ={10,}/{n++; next} n==1' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *)  warn "Unknown option: $arg" ;;
    esac
done

# ── Preflight ─────────────────────────────────────────────────
# Declared here rather than beside the build loop below because the
# preflight needs it: the dependency check reads these packages' PKGBUILDs,
# and it has to run BEFORE the 20-minute llama build, not after it.
# All packages with a PKGBUILD in the project root
PACKAGES=(
    # Must precede synapd — synapd depends on it.
    synapse-llama
    synapd
    synsh
    synguard
    synnet
    # Must precede synui — synui depends on it, and scenefx is in no pacman
    # repo (it's a local fork of wlr_scene), so pacstrap can only satisfy
    # synui's dependency from our local-repo. Omitting it fails the ISO at
    # pacstrap with "unable to satisfy dependency 'scenefx0.5'".
    # scenefx0.5, not scenefx: the two are separate packages tracking wlroots
    # (0.4.x = 0.19, 0.5.x = 0.20), and synui moved to the 0.20 pair.
    scenefx0.5
    # Must precede synui too, and for the SAME reason scenefx does: synui
    # depends on syntty (it is the default terminal and the one every fresh
    # synuirc names), and makepkg -s resolves a component's depends out of the
    # local repo as it builds. syntty used to sit far below, next to
    # syn-arcade, which was correct while nothing required it.
    #
    # The live session wants it for a reason of its own. kitty needs a GL
    # context, which is the one thing a live image cannot count on — an
    # unfamiliar GPU, a driver that did not load, a VM without acceleration —
    # and a live session with no way to open a prompt is a rescue disk that
    # cannot rescue anything. syntty links no GL at all, so it opens where
    # kitty cannot. Builds offline: meson, freetype, fontconfig, wayland and
    # xkbcommon, all already on the build host for synui.
    syntty
    synui
    synapse_kmod
    syn
    # Must precede syn-firstboot — syn-firstboot depends on it. These two were
    # the other way round here while build-all.sh had them right, which is the
    # drift this list keeps producing: it is a second, independent collector.
    syn-model
    syn-firstboot
    syn-install
    # Updates an installed system from git by driving build-all.sh. Pulls
    # base-devel + git (17 packages, ~118 MiB) onto the ISO as dependencies —
    # that toolchain is the whole point, since it rebuilds components from
    # source on the target rather than downloading binaries.
    syn-update
    nexus-chat
    tepris
    # synapse-voice BEFORE chibi, which depends on it (same rule as syn-confine
    # and vibe below). It carries the speech models, ~900 MB installed, which
    # chibi carried until 27 — so the ISO does not grow by it.
    synapse-voice
    chibi
    # syn-confine BEFORE vibe, which DEPENDS on it — this array is built in
    # order and makepkg resolves dependencies from the local repo as it goes,
    # so listing it after vibe fails the ISO build outright. vibe's bash tool
    # runs model-proposed commands inside its Landlock sandbox and refuses to
    # run one without it. Builds offline — meson and libc, no library for
    # Landlock at all.
    syn-confine
    vibe
    # areofyl/fetch — "About OS". Git-sourced from a pinned upstream commit
    # with two local patches applied in prepare(), so it needs network like
    # nexus-chat does. Order does not matter: nothing depends on it, it reaches
    # the ISO because packages.x86_64 names it.
    fetch
    # SYNAPSE Arsenal — the BlackArch category browser. Must be ON the ISO:
    # syn-install now names it in SEL_APPS, and an app the installer asks
    # for but the local repo does not carry fails the install at pacman.
    syn-arsenal
    # synpkg — the package manager. Must be ON the ISO for the same reason
    # syn-arsenal must: syn-install names it in its install set, and an app the
    # installer asks for but the local repo does not carry fails the install at
    # pacman. Builds offline (meson + libalpm headers from pacman).
    synpkg
    # synfiles — the file manager. Must be ON the ISO for the same reason
    # synpkg must: syn-install names it in SEL_CORE, and a package the
    # installer asks for but the local repo does not carry fails the install
    # at pacman. Since 2026-08-10 it is also the distribution default for
    # inode/directory, so the live session needs it too or clicking a folder
    # reaches an entry with nothing behind it. Builds offline — meson and libc.
    synfiles
    # syn-disks — the disk utility. Must be ON the ISO for the same reason
    # synfiles must: syn-install names it in SEL_CORE, and a package the
    # installer asks for but the local repo does not carry fails the install at
    # pacman. The live session wants it independently — a rescue boot is
    # exactly when somebody needs to see what disks a machine has. Builds
    # offline: meson and libc.
    syn-disks
    # syn-cal — the calendar. Must be ON the ISO for the same reason syn-disks
    # must: syn-install names it in SEL_CORE, and a package the installer asks
    # for but the local repo does not carry fails the install at pacman. It has
    # no use in the live session — a calendar with no account is an empty
    # folder — but the installer's list is what decides this, not usefulness.
    # Builds offline: meson, libc, curl, libxml2, libsecret.
    syn-cal
    # syn-vault — the file vault. On the ISO for the same reason as the two
    # above: syn-install names it, and a package the installer asks for that the
    # local repo does not carry fails the install at pacman.
    #
    # ⚠ IT PULLS gocryptfs, WHICH IS A HARD DEPENDENCY, so that lands on the ISO
    # too. That is correct and not an accident: a vault application installed
    # without the thing that encrypts can refuse every command it has.
    syn-vault
    # syn-clean — disk cleanup and secure delete, on the ISO for the same
    # reason: syn-install names it, and a package the installer asks for that
    # the local repo does not carry fails the install at pacman.
    #
    # ⚠ AND A LIVE SESSION IS WHERE IT IS MOST WANTED. "This disk is full" is a
    # thing people boot a USB stick to deal with, and the machine they are
    # trying to clean is the one that will not start.
    syn-clean
    # syn-scan — the malware scanner. On the ISO because syn-install names it,
    # and a package the installer asks for that the local repo does not carry
    # fails the install at pacman.
    #
    # ⚠ IT PULLS clamav, WHICH IS A HARD DEPENDENCY (185 MiB installed). The
    # signature database is NOT shipped — freshclam fetches it on first boot —
    # so the ISO pays the engine and not the ~1 GB of signatures.
    syn-scan
    syn-play
    # syn-edit — the text editor. Must be ON the ISO for the same reason
    # syn-disks must: syn-install names it in SEL_CORE, and a package the
    # installer asks for but the local repo does not carry fails the install at
    # pacman. The live session wants it independently, and more than most —
    # editing fstab or a bootloader entry from a rescue boot is the whole point
    # of having an editor there, and its terminal front-end works with no
    # display at all. Builds offline: meson and libc.
    syn-edit
    # syn-settings — the settings app. Must be ON the ISO for the same reason
    # syn-edit must, and one more: it is what a live session reaches for to set
    # a keyboard layout before installing anything, and it is the Kernel pane
    # an installed system needs on its first boot.
    #
    # It was in build-all.sh's component list and in packages.x86_64 and NOT
    # here, which is the failure mode this array's comments keep describing
    # from the other side: packages.x86_64 names what the ISO INSTALLS, this
    # names what gets BUILT into the local repo it installs from. A package on
    # one list and not the other fails pacstrap ~15 minutes in with "target not
    # found". Builds offline: meson and libc.
    syn-settings
    # syn-arcade — the game assistant. Must be ON the ISO for the same reason
    # syntty must: syn-install names it in SEL_APPS (Standard and Full), and a
    # package the installer asks for but the local repo does not carry fails
    # the install at pacman.
    #
    # The live session wants it independently, and for a reason no other app
    # here has: "does my controller actually work on this machine" is a
    # question worth answering BEFORE committing to an install, and it is the
    # one thing on this list that cannot be checked from a spec sheet.
    #
    # Builds offline: meson and libc. It depends on mangohud, which is in
    # `extra` and so comes off the mirror like any other upstream dependency.
    syn-arcade
    # cliamp — the terminal music player big screen mode DRIVES, and the one
    # component of that feature that is not ours. It is built here rather than
    # pulled from a repo because it is not in one: it was a 33MB unowned binary
    # in ~/.local/bin until this release. ⚠ Needs network at build time (a
    # pinned upstream tag and the Go module cache), exactly like nexus-chat,
    # tepris and fetch above.
    cliamp
    # synstudio — the darkroom and edit suite. Ordering is free: it links libc
    # and libm and nothing else, reaching ffmpeg, ffprobe and dcraw_emu as
    # subprocesses rather than as libraries, so no component here depends on it
    # and it depends on none of them. Its own ffmpeg dependency comes off the
    # mirror from `extra` like any other upstream one, and needs no network at
    # build time beyond that.
    synstudio
    # syn-gfn — three files, no compiler, no ordering constraint, and no
    # browser dependency: the launcher finds one at runtime and says what is
    # missing if it cannot. On the ISO because a live session is exactly where
    # somebody tries "does my hardware do this yet".
    syn-gfn
    # syn-remote — remote desktop over wayvnc. On the ISO because a live
    # session is a common way to reach a machine that will not boot, and
    # because wayvnc and wlopm are ordinary `extra` packages that the image
    # already knows how to pull.
    syn-remote
    # syn-mouse — mouse buttons rebound per game. libc only, no ordering
    # constraint. On the ISO because a live session is where somebody checks
    # whether their mouse's extra buttons can do anything here.
    syn-mouse
    # samsung-m2020 is deliberately NOT built here. Its EULA forbids
    # redistribution, so the driver cannot ride the ISO or sit in the local
    # repo — `syn printer samsung` installs it on demand instead. The PKGBUILD
    # still exists and still builds; it just fetches from Samsung now.
    # Wallpaper Engine renderer. Git-sourced from a pinned commit, so it needs
    # network, and cmake fetches a ~1.3 GB CEF blob at configure time into the
    # build dir — budget ~6 GB of /var/tmp scratch for it on top of the ISO.
    # Order does not matter: nothing depends on it (synui lists it nowhere),
    # it is on the ISO because packages.x86_64 names it.
    linux-wallpaperengine
    # SynapseOS' own Wallpaper Engine wallpapers (dendrite + Tux). Renders are
    # derived at build time, so this needs python-pillow/librsvg/ffmpeg/
    # adwaita-fonts — all upstream, and all picked up automatically by the
    # BUILD_DEPS scan below now that this is in PACKAGES.
    #
    # Order does not matter: nothing depends on it. linux-wallpaperengine is an
    # optdepend, not a depend, so leaving this OUT fails nothing — the ISO
    # builds clean and simply ships no Synapse wallpapers, which is exactly
    # what happened up to 0.2.6. The only witness is the package by name in
    # pkglist.x86_64.txt. See the 0.2.2 lesson in the release memory.
    synapse-wallpapers
    # limine-snapper-sync and limine-mkinitcpio-hook are NOT built here any
    # more: they come from [cachyos] — see FROM_CACHYOS below.
)

step "Preflight checks"

[[ "$(id -u)" -eq 0 ]] || err "Must run as root (needed for mkarchiso)"

# ── The build date: from git, never from the clock ────────────────────────────
#
# makepkg stamps SOURCE_DATE_EPOCH into every package and mkarchiso into the
# image (the ISO volume dates, the squashfs timestamps); both fall back to the
# CURRENT time when it is unset, so two builds of one commit could never be the
# same bytes. SECURITY-ROADMAP §4. The image takes HEAD's commit time; each
# package takes the last commit that touched it (build_package below), which is
# what build-all.sh gives it too — so the ISO's packages and an installed
# machine's syn-update builds of the same commit agree.
#
# ⚠ ASKED AS THE INVOKING USER. This runs as root, and git refuses to read a
# repository another user owns ("dubious ownership"); the way round that,
# safe.directory, would let that repository's config run commands as root.
# An explicit SOURCE_DATE_EPOCH in the environment still wins.
repo_date() {   # [component]
    if [[ -n "${SUDO_USER:-}" ]]; then
        sudo -u "${SUDO_USER}" "${PROJECT_ROOT}/tools/source-date-epoch.sh" "$@" 2>/dev/null
    else
        "${PROJECT_ROOT}/tools/source-date-epoch.sh" "$@" 2>/dev/null
    fi
}
if [[ -z "${SOURCE_DATE_EPOCH:-}" ]]; then
    SOURCE_DATE_EPOCH=$(repo_date) || SOURCE_DATE_EPOCH=
fi
if [[ -n "${SOURCE_DATE_EPOCH}" ]]; then
    export SOURCE_DATE_EPOCH
else
    unset SOURCE_DATE_EPOCH
    warn "no build date from git — this image will carry today's date and cannot be reproduced"
fi

command -v pacman &>/dev/null || err "pacman not found — must build on Arch Linux"

# ⛔ THE SIGNING KEY IS CHECKED HERE, NOT AT THE END. Signing is the last step of
# an hour-long build, so a missing key used to be discovered after the expensive
# part — at the exact moment when the cheap way out is to shrug and publish it
# unsigned. Proving the key can sign takes a fraction of a second, and it costs
# nothing to do it before mkarchiso rather than after.
if [[ "$SIGN" == "true" ]]; then
    "${SCRIPT_DIR}/sign-iso.sh" --check-key \
        || err "cannot sign with the key named in archiso/release-key.fingerprint (see above).
     Fix the key, or build unsigned on purpose with --no-sign."
fi

# ── [cachyos] must be trusted by THIS host's pacman ───────────
#
# syn-remote depends on sunshine, which only [cachyos] carries, so the
# build-time pacman.conf below lists that repository and mkarchiso's pacstrap
# resolves from it. pacstrap runs with -G and checks signatures against the
# HOST's keyring (/etc/pacman.d/gnupg), so a host that has never trusted the
# CachyOS master key refuses sunshine "unknown trust" deep inside mkarchiso,
# long after the packages have been built. It is the same key syn-install pins.
CACHY_FPR="882DCFE48E2051D48E2562ABF3B607488DB35A47"
if ! gpg --homedir /etc/pacman.d/gnupg --with-colons --list-keys "$CACHY_FPR" 2>/dev/null \
        | grep -q '^pub:[fu]:'; then
    err "this host's pacman does not trust the CachyOS master key, and the ISO needs
     [cachyos] (sunshine, for syn-remote). Trust it once, then re-run:
       pacman-key --recv-keys ${CACHY_FPR} --keyserver keyserver.ubuntu.com
       pacman-key --lsign-key ${CACHY_FPR}"
fi

# ── Packages that must never be INSTALLED on the live ISO ─────
#
# Being in the local repo and being in packages.x86_64 are different things —
# the repo is filled from PACKAGES above so syn-install can pacman -S a package
# into the TARGET, while packages.x86_64 is what pacstrap puts in the live
# environment. A package can be required for the former and ruinous in the
# latter, and nothing but this check says so.
#
# Checked in preflight because the damage surfaces NOWHERE NEAR the cause. The
# entry that broke 0.2.8 killed the build forty minutes in, four mkarchiso steps
# after the package was installed, with a message about a missing initramfs glob.
# Two seconds here beats reading that backwards again — see packages.x86_64,
# where each of these has its reason written out in full.
declare -A ISO_NEVER_INSTALL=(
    [limine-mkinitcpio-hook]="its /etc/pacman.d/hooks/90-mkinitcpio-install.hook shadows mkinitcpio's, so NEITHER /boot/vmlinuz-linux NOR the initramfs is built and mkarchiso dies later in _make_boot_on_iso9660. Keep it in the local repo (FROM_CACHYOS puts it there); syn-install installs it into the target, which does boot limine."
)
for _pkg in "${!ISO_NEVER_INSTALL[@]}"; do
    if grep -qxa -- "${_pkg}" "${SCRIPT_DIR}/packages.x86_64"; then
        err "packages.x86_64 lists ${_pkg}, which must never be installed on the live ISO: ${ISO_NEVER_INSTALL[$_pkg]}"
    fi
done
unset _pkg

declare -A TOOL_PKG=(
    [mkarchiso]="archiso"
    [makepkg]="pacman"
    [git]="git"
    [cmake]="cmake"
    [meson]="meson"
    [ninja]="ninja"
    [gcc]="gcc"
    [curl]="curl"
    [rsync]="rsync"
    [dialog]="dialog"
    [parted]="parted"
    [repo-add]="pacman"
    # The Vulkan shader compiler. synapse-llama-vulkan is expected to build on
    # every release (see BUILD_VULKAN_PKG below), and it is an OPTIONAL package
    # — nothing depends on it, so a host without this does not fail the build,
    # it just quietly ships an ISO that cannot switch an AMD/Intel machine onto
    # its GPU. Install it rather than letting that happen by accident.
    [glslc]="shaderc"
)

# Dependencies that provide no command of their own, so the `command -v` loop
# below is structurally incapable of seeing them. vulkan-headers is headers
# only: a host with shaderc installed passes every command check above and then
# dies inside CMake with "Could NOT find Vulkan (missing: Vulkan_INCLUDE_DIR)",
# several minutes in, after llama.cpp has already been cloned and configured.
# For a package that ships no binary the check has to look for what it actually
# puts on disk.
declare -A FILE_PKG=(
    [/usr/include/vulkan/vulkan.h]="vulkan-headers"
    # Required by ggml-vulkan from llama.cpp b10241 onward — see
    # have_spirv_headers(). Keyed on the CMake config file rather than a header,
    # because find_package(SPIRV-Headers CONFIG) resolves that and nothing else:
    # spirv-tools is a different package and does not provide it.
    [/usr/share/cmake/SPIRV-Headers/SPIRV-HeadersConfig.cmake]="spirv-headers"
)

MISSING_PKGS=()
for cmd in "${!TOOL_PKG[@]}"; do
    if command -v "$cmd" &>/dev/null; then
        ok "$cmd"
    else
        pkg="${TOOL_PKG[$cmd]}"
        warn "$cmd not found — will install: $pkg"
        MISSING_PKGS+=("$pkg")
    fi
done

for file in "${!FILE_PKG[@]}"; do
    pkg="${FILE_PKG[$file]}"
    if [[ -e "$file" ]]; then
        ok "$pkg"
    else
        warn "$file not found — will install: $pkg"
        MISSING_PKGS+=("$pkg")
    fi
done

# ── Build deps, read from the PKGBUILDs themselves ─────────────────────────
# Packages are built with `makepkg -fd`: the -d skips dependency resolution and
# there is no -s, so NOTHING gets installed for them. Every build dependency
# has to already be on this host before the build starts.
#
# Derived rather than hand-listed, because the hand-listed version is how
# gradle, then clang, then bpf/glm/python-pillow/python-pip, then glew/glfw/mpv
# each got to fail a release build on their own — one 20-minute run per missing
# package, every one of them sitting in plain sight in a PKGBUILD the whole
# time. A list maintained here drifts from what the packages declare; reading
# the declarations cannot.
#
# BOTH depends and makedepends. `makepkg -fd` skips both, and a package that
# links a library needs that library's headers to compile — on Arch those ship
# in the same package, which is listed under depends, not makedepends. Checking
# only makedepends is what let linux-wallpaperengine reach CMake and die on
# "Could NOT find GLEW": glew is a depend.
#
# Scoped to PACKAGES, not */PKGBUILD. The tree still carries directories that
# are no longer built — legacy scenefx/ (the 0.4.x/wlroots0.19 pair synui left
# behind at 0.2.0) is one — and scanning those asks the host to install
# dependencies for something nobody builds.
#
# `pacman -T` rather than `pacman -Q`: it understands version constraints and
# provides, so 'pkg-config' resolves through pkgconf instead of being reported
# missing on every run forever.
BUILD_DEPS=()
for _p in "${PACKAGES[@]}"; do
    _pb="${PROJECT_ROOT}/${_p}/PKGBUILD"
    [[ -f "$_pb" ]] || { warn "no PKGBUILD for package '$_p'"; continue; }
    while IFS= read -r _d; do
        [[ -n "$_d" ]] && BUILD_DEPS+=("$_d")
    done < <(bash -c 'source "$1" >/dev/null 2>&1; printf "%s\n" "${depends[@]-}" "${makedepends[@]-}"' _ "$_pb" 2>/dev/null)
done

# SynapseOS's own packages appear in each other's depends (synui needs
# scenefx0.5, synapd needs synapse-llama). Those are built by this script in the
# order PACKAGES lists them, exist in no upstream repo, and must never be handed
# to pacman. Identify them by the pkgname/provides the PKGBUILDs declare.
#
# Do NOT identify them as "whatever `pacman -Si` cannot find". -Si matches on
# package NAME only, so it misses provides: it cannot find 'pkg-config' (pkgconf
# provides it) or 'sdl2' (sdl2-compat does), and both would be filed as ours and
# silently never installed. `pacman -T` gets provides right when deciding
# SATISFACTION; this list only has to answer "do we build it ourselves".
OUR_PKGS=()
for _p in "${PACKAGES[@]}"; do
    _pb="${PROJECT_ROOT}/${_p}/PKGBUILD"
    [[ -f "$_pb" ]] || continue
    while IFS= read -r _n; do
        [[ -n "$_n" ]] && OUR_PKGS+=("${_n%%[<>=]*}")
    done < <(bash -c 'source "$1" >/dev/null 2>&1; printf "%s\n" "${pkgname[@]-}" "${provides[@]-}"' _ "$_pb" 2>/dev/null)
done

if [[ ${#BUILD_DEPS[@]} -gt 0 ]]; then
    UNSATISFIED=()
    while IFS= read -r _d; do
        [[ -z "$_d" ]] && continue
        _bare="${_d%%[<>=]*}"
        # Ours? Then this script builds it; pacman has never heard of it.
        for _o in "${OUR_PKGS[@]}"; do
            [[ "$_bare" == "$_o" ]] && continue 2
        done
        UNSATISFIED+=("$_bare")
    done < <(pacman -T "${BUILD_DEPS[@]}" 2>/dev/null || true)

    if [[ ${#UNSATISFIED[@]} -gt 0 ]]; then
        for _d in "${UNSATISFIED[@]}"; do
            warn "build dep not satisfied — will install: $_d"
            MISSING_PKGS+=("$_d")
        done
    else
        ok "all PKGBUILD build deps satisfied"
    fi
fi

MISSING_PKGS=($(printf '%s\n' "${MISSING_PKGS[@]}" | sort -u))

if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
    log "Installing missing dependencies: ${MISSING_PKGS[*]}"
    pacman -Sy --noconfirm --needed "${MISSING_PKGS[@]}" \
        2>&1 | sed 's/^/  /' \
        || err "pacman failed to install dependencies"

    STILL_MISSING=()
    for cmd in "${!TOOL_PKG[@]}"; do
        command -v "$cmd" &>/dev/null || STILL_MISSING+=("$cmd")
    done
    # Re-verify the file-provided ones too. Skipping them here is what would
    # let a failed vulkan-headers install read as "All dependencies installed".
    for file in "${!FILE_PKG[@]}"; do
        [[ -e "$file" ]] || STILL_MISSING+=("$file (${FILE_PKG[$file]})")
    done
    # And the build deps. One that pacman accepted but did not actually satisfy
    # would otherwise surface as a compile error deep inside a package's
    # build(), which is the exact failure this section exists to prevent. Our
    # own packages are skipped again here — they are not built yet, so pacman
    # still cannot see them and would report every one as missing.
    if [[ ${#BUILD_DEPS[@]} -gt 0 ]]; then
        while IFS= read -r _d; do
            [[ -z "$_d" ]] && continue
            _bare="${_d%%[<>=]*}"
            for _o in "${OUR_PKGS[@]}"; do
                [[ "$_bare" == "$_o" ]] && continue 2
            done
            STILL_MISSING+=("$_bare (build dep)")
        done < <(pacman -T "${BUILD_DEPS[@]}" 2>/dev/null || true)
    fi
    [[ ${#STILL_MISSING[@]} -eq 0 ]] || err "Still missing: ${STILL_MISSING[*]}"
    ok "All dependencies installed"
else
    ok "All build dependencies present"
fi

pacman -Qq base-devel &>/dev/null || {
    log "Installing base-devel..."
    pacman -Sy --noconfirm --needed base-devel 2>&1 | sed 's/^/  /'
    ok "base-devel installed"
}

REQUIRED_GB=$([[ "$WITH_MODEL" == "true" ]] && echo 22 || echo 9)
AVAIL_GB=$(df -BG "${SCRIPT_DIR}" | awk 'NR==2{print $4}' | tr -d G)
[[ "$AVAIL_GB" -ge "$REQUIRED_GB" ]] \
    || err "Insufficient disk space: need ${REQUIRED_GB}GB, have ${AVAIL_GB}GB"
ok "Disk space: ${AVAIL_GB}GB available"

if [[ "$WITH_GPU" == "auto" ]]; then
    if lspci 2>/dev/null | grep -qi "nvidia"; then
        WITH_GPU="cuda"
        log "Detected NVIDIA GPU — building llama.cpp with CUDA"
    elif lspci 2>/dev/null | grep -qiE "amd|radeon|advanced micro devices"; then
        # Vulkan, not ROCm: one build covers every AMD card (GCN/RDNA/APU) with
        # no per-arch compile and only the mesa RADV ICD at runtime.
        WITH_GPU="vulkan"
        log "Detected AMD GPU — building llama.cpp with Vulkan"
    else
        WITH_GPU="cpu"
        log "No discrete GPU detected — building llama.cpp CPU-only"
    fi
fi

case "$WITH_GPU" in
    cpu) ;;
    # ⚠ rocm WAS accepted here, and never produced an ISO: no synapse-llama-rocm
    # package was ever defined, so the backend pin below named a package that
    # did not exist and pacstrap failed at the end of the build. synapse-llama's
    # PKGBUILD refuses a backend it does not package; this says so before any
    # of the build is spent.
    rocm)
        err "--gpu=rocm: there is no ROCm build of synapse-llama — --gpu=vulkan is the AMD build" ;;
    cuda)
        warn "GPU build ($WITH_GPU): the ISO will only run AI features on"
        warn "machines with the matching driver installed — synapd will fail"
        warn "to start anywhere else (VMs included). CPU is the safe default." ;;
    vulkan)
        warn "GPU build (vulkan): synapd loads everywhere (the loader tolerates"
        warn "no GPU), but only offloads where a Vulkan ICD + GPU exist — else it"
        warn "runs on the CPU. The ISO still ships CPU by default." ;;
    *) err "Invalid --gpu value: $WITH_GPU (expected cuda, vulkan, or auto)" ;;
esac

# llama-staging stays a symlink to the ISO backend's staged tree (stage_llama):
# synapd/meson.build looks for it by that name.
LLAMA_STAGING_LINK="${PROJECT_ROOT}/llama-staging"

# ── Clean ─────────────────────────────────────────────────────
# Always wipe the mkarchiso work dir, even under --no-clean. It carries
# per-stage stamp files AND the pacstrapped airootfs; a stale one from a
# previous build makes mkarchiso skip straight to "Done!" — reusing the old
# ISO with the old packages (e.g. an unbumped synui) and never picking up
# freshly built ones. --no-clean keeps build/, which held the llama.cpp compile
# until that moved into synapse-llama/PKGBUILD; nothing expensive lives there now.
rm -rf "${WORK_DIR}"
if [[ "$CLEAN" == "true" ]]; then
    step "Cleaning previous build"
    rm -rf "${BUILD_DIR}"
    log "Cleaned work/ and build/"
else
    log "Cleaned work/ (kept build/)"
fi

mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${LOCAL_REPO}" "${MODEL_DIR}"

# ── llama.cpp ─────────────────────────────────────────────────
#
# Built by synapse-llama/PKGBUILD, through build_package below like every other
# package. That PKGBUILD owns the recipe — the backend flags, GGML_NATIVE, the
# check that refuses a CPU build wearing a GPU name — and the pin. Until 2026-10
# the recipe lived HERE, as a cmake run into llama-staging-<backend>/ that the
# PKGBUILD merely packaged. An installed machine has no ISO build, so syn-update
# could never move llama.cpp at all: every machine ran its ISO's llama.cpp for
# life. One recipe, in the package, is what lets every machine build it.
#
# stage_llama <backend> <repoint:yes|no>
#
# Unpacks the package just built into llama-staging-<backend>/. That tree is
# what synapd compiles against on a host with no libllama of its own (see
# synapd/meson.build, and the host check below), and what --llama-only leaves
# behind. Each backend keeps its own directory, so building the CPU ISO can
# never take a CUDA build out from under anything that uses one; the install is
# a wipe-and-replace, because layering two builds leaves mismatched sonames.
stage_llama() {
    local backend="$1" repoint="$2"
    local staging="${PROJECT_ROOT}/llama-staging-${backend}"
    local name=synapse-llama
    case "$backend" in cuda|vulkan) name="synapse-llama-${backend}" ;; esac

    # -[0-9]: synapse-llama-<ver>, never synapse-llama-cuda-<ver>.
    local pkg
    pkg=$(ls -1t "${LOCAL_REPO}/${name}"-[0-9]*.pkg.tar.zst 2>/dev/null | head -1)
    [[ -n "$pkg" ]] || err "no ${name} package in ${LOCAL_REPO} to stage"

    rm -rf "${staging}"
    mkdir -p "${staging}"
    bsdtar -xf "$pkg" -C "${staging}" usr \
        || err "could not unpack ${pkg} into ${staging}"

    if [[ "$repoint" == "yes" ]]; then
        # Before the split, llama-staging was a real directory. ln can't overwrite one,
        # so migrate it out of the way rather than die here — and never delete it, it may
        # be the only copy of a build something still links against.
        if [[ -d "${LLAMA_STAGING_LINK}" && ! -L "${LLAMA_STAGING_LINK}" ]]; then
            local _orphan="${LLAMA_STAGING_LINK}.pre-split-$(date +%Y%m%d%H%M%S)"
            warn "llama-staging is a real directory (pre-split layout) — moving it to $(basename "${_orphan}")"
            mv "${LLAMA_STAGING_LINK}" "${_orphan}"
        fi

        # Repoint llama-staging at what we just built. If that steals the symlink from
        # a GPU build, say so loudly: synapd resolves libllama through this path when
        # the host has none installed, so it would build against ${backend} from now on.
        local _prev
        _prev="$(readlink "${LLAMA_STAGING_LINK}" 2>/dev/null || true)"
        if [[ -n "$_prev" && "$_prev" != "$(basename "${staging}")" && "$_prev" != "llama-staging-cpu" ]]; then
            warn "llama-staging was pointing at ${_prev} — repointing it to the ${backend} build."
            warn "Restore with: ln -sfn ${_prev} ${LLAMA_STAGING_LINK}"
        fi
        # -n: don't descend into the existing symlink and nest the link inside its target
        ln -sfn "$(basename "${staging}")" "${LLAMA_STAGING_LINK}"
    fi

    ok "llama.cpp ${LLAMA_REF} (${backend}) staged to ${staging}"
}

# ── synapd must be compiled against the llama.cpp this ISO ships ──
#
# synapd/meson.build takes the HOST's libllama when there is one, and only falls
# back to llama-staging when there is not. So on a host with synapse-llama
# installed, the ISO's synapd is compiled against the HOST's llama.h — and then
# runs against the libllama this build packages. When the two differ, it links
# and starts cleanly and then crashes: llama_context_params grows fields between
# releases, so synapd hands llama_init_from_model() a struct that is too short,
# and the constructor reads off the end of it. That crash-looped this machine
# once, at the b8272 -> b10241 bump. Refuse the build instead of shipping it.
_llama_want="$(cd "${PROJECT_ROOT}/synapse-llama" && bash -c 'source ./PKGBUILD >/dev/null 2>&1; printf "%s-%s" "$pkgver" "$pkgrel"')"
# --llama-only builds no synapd, so it has nothing to protect here.
if [[ "$LLAMA_ONLY" != "true" && -e /usr/lib/libllama.so ]]; then
    _llama_owner="$(LC_ALL=C pacman -Qo /usr/lib/libllama.so 2>/dev/null | awk '{print $(NF-1), $NF}')"
    case "$_llama_owner" in
        "synapse-llama ${_llama_want}"|"synapse-llama-cuda ${_llama_want}"|"synapse-llama-vulkan ${_llama_want}") ;;
        *) err "this host's /usr/lib/libllama.so is ${_llama_owner:-owned by no package}, but this ISO ships synapse-llama ${_llama_want} (llama.cpp ${LLAMA_REF}). synapd would be compiled against the host's llama.h and crash against the shipped library. Bring the host up to date first: syn-update apply synapse-llama synapd" ;;
    esac
fi
unset _llama_want _llama_owner

# ── The CUDA package (built, never installed on the ISO) ──────
#
# The ISO must stay a CPU build: libggml-cuda.so NEEDs libcudart/libcublas and
# libcuda.so.1 at load, so a CUDA synapd cannot start on a machine with no
# NVIDIA driver — which is most of them. But an INSTALLED SynapseOS with an
# NVIDIA card should run on the GPU, and syn-install can only offer that if a
# synapse-llama-cuda package exists in the repo it copies to the target. Nothing
# ever built one during a release, so every install ran CPU inference forever,
# silently, on any hardware. This builds it so there is something to install.
#
# Skipped (loudly) with no CUDA toolkit: the ISO is still valid, it just cannot
# offer the GPU to anyone. That has to be a warning, not silence — silence is
# how this went unnoticed in the first place.
BUILD_CUDA_PKG=false
if [[ "$WITH_GPU" == "cuda" ]]; then
    # The ISO backend IS cuda (never for a release — see above). The one build
    # already covers the package.
    BUILD_CUDA_PKG=false
elif command -v nvcc &>/dev/null || [[ -x /opt/cuda/bin/nvcc ]]; then
    BUILD_CUDA_PKG=true
else
    warn "No CUDA toolkit (nvcc) on this host — synapse-llama-cuda will NOT be built."
    warn "The ISO stays valid, but an NVIDIA machine installed from it will run"
    warn "inference on the CPU with no way to switch. Install 'cuda' to fix."
fi


# ── The Vulkan package (built, never installed on the ISO) ────
#
# The AMD/Intel analogue of the CUDA package above. The ISO ships CPU; this
# builds synapse-llama-vulkan into the repo so syn-install can switch an
# AMD/Intel machine onto its GPU. Unlike CUDA the build host only needs the
# Vulkan shader toolchain (glslc + vulkan-headers), which is small and present
# on the CI runner — so this is expected to build on every release. Skipped
# (loudly) if that toolchain is absent, never silently.
BUILD_VULKAN_PKG=false
if [[ "$WITH_GPU" == "vulkan" ]]; then
    BUILD_VULKAN_PKG=false   # ISO backend already vulkan; the one build covers it
elif have_vulkan_toolchain; then
    BUILD_VULKAN_PKG=true
else
    # Name the half that is actually missing. These are two packages, and the
    # version of this check that only looked for glslc turned a host with
    # shaderc-but-no-headers into a CMake failure several minutes into the run
    # instead of a skip here.
    if command -v glslc &>/dev/null; then
        warn "No Vulkan headers on this host — synapse-llama-vulkan will NOT be built."
        warn "Install 'vulkan-headers' to fix."
    else
        warn "No Vulkan shader compiler (glslc) on this host — synapse-llama-vulkan will NOT be built."
        warn "Install 'shaderc' to fix."
    fi
    warn "The ISO stays valid, but an AMD/Intel machine installed from it will run"
    warn "inference on the CPU with no way to switch."
fi


cd "${SCRIPT_DIR}"

# ── Build SynapseOS packages ──────────────────────────────────
step "Building SynapseOS packages"


# Create build user for makepkg (can't run as root)
id -u synbuild &>/dev/null || useradd -r -s /bin/bash -m synbuild

create_source_tarball() {
    local pkg="$1"
    local pkgdir="${PROJECT_ROOT}/${pkg}"
    local tarball="${pkgdir}/${pkg}-${SYNAPSEOS_VERSION}.tar.gz"

    # Check if PKGBUILD uses a tarball source (skip script packages like
    # syn). Match both the literal name and the $pkgname-$pkgver form —
    # synapse_kmod uses the latter and used to slip through this check,
    # silently reusing a stale tarball.
    if ! grep -qE "(\\\$pkgname-\\\$pkgver|${pkg}-${SYNAPSEOS_VERSION})\.tar\.gz" "${pkgdir}/PKGBUILD"; then
        return 0
    fi

    log "Creating source tarball for ${pkg}..."
    cd "${PROJECT_ROOT}"

    # A component that ships its own mktarball.sh WINS — same rule build-all.sh
    # follows. That script knows which top-level dirs the component actually
    # compiles, and it hard-fails on a missing one instead of quietly shipping a
    # tarball without it.
    #
    # This is the drift the comment below warned about, arriving for real:
    # synui's QML tree (quickshell/) moved into the package when the bar was
    # ported from waybar, mktarball.sh listed it, and this collector never
    # learned about it. The tarball built fine, synui COMPILED fine, and
    # package() then died on `install -Dm644 quickshell/*.qml` — so every ISO
    # release after the quickshell port aborted at synui. Delegating is what
    # stops the generic list below from needing to know each component's layout
    # at all; it stays as a fallback for the ones that ship no script.
    if [[ -x "${pkgdir}/mktarball.sh" ]]; then
        ( cd "${pkgdir}" && ./mktarball.sh ) \
            || err "${pkg}: mktarball.sh failed — refusing to build from a stale tarball"
        ok "Source tarball: ${tarball} (via ${pkg}/mktarball.sh)"
        cd "${SCRIPT_DIR}"
        return 0
    fi

    # ⛔ ONE OWNER, AND THIS WAS THE THIRD COPY OF IT. The list of what goes
    # into a source tarball lived here, in build-all.sh, and now also had to
    # serve tools/publish-sources.sh — and the comments this replaces are a
    # record of the same drift arriving twice: a *.md glob here that was a named
    # HARDENING.md there, and an exclude fixed in one copy and not the other,
    # each of them breaking a release. It lives in tools/collect-source.sh now.
    #
    # ⚠ AND IT MATTERS MORE THAN IT USED TO. A PKGBUILD's source=() now names a
    # release URL after the local filename, so a collector that produced nothing
    # would no longer stop the build — makepkg would DOWNLOAD the last published
    # source and the ISO would ship something that is not this tree. Failing
    # here is what keeps that impossible.
    # ⚠ TWO KINDS OF "NO TARBALL", AND ONLY ONE IS A PROBLEM. collect-source.sh
    # exits 2 when the component contributes no local sources at all — scenefx
    # is the case: its source=() is an upstream URL and the filename half of it
    # happens to match the guard above. Downloading is the RIGHT answer there.
    # Exit 1 is our component with sources that could not be packed, and that
    # must stop the build.
    local crc=0
    set +e
    "${PROJECT_ROOT}/tools/collect-source.sh" "${pkg}" "${tarball}" >/dev/null
    crc=$?
    set -e
    if [[ $crc -eq 2 ]]; then
        log "${pkg}: no local sources to collect — its PKGBUILD fetches its own"
        cd "${SCRIPT_DIR}"
        return 0
    fi
    [[ $crc -eq 0 ]] || err "${pkg}: could not collect its source — refusing to build, because makepkg would silently fall back to the published tarball instead"

    ok "Source tarball: ${tarball}"
    cd "${SCRIPT_DIR}"
}

build_package() {
    local pkg="$1"
    # An optional second argument overrides which llama backend this package is
    # built against, so synapse-llama can be packaged twice from one run: once
    # as the CPU build the ISO installs, once as synapse-llama-cuda for targets
    # that have a GPU. Defaults to the ISO's own backend.
    local llama_backend="${2:-${WITH_GPU}}"
    local staging="${PROJECT_ROOT}/llama-staging-${llama_backend}"
    local pkgdir="${PROJECT_ROOT}/${pkg}"

    if [[ ! -f "${pkgdir}/PKGBUILD" ]]; then
        warn "No PKGBUILD for ${pkg} — skipping"
        return 0
    fi

    log "Building ${pkg}${2:+ (llama: ${llama_backend})}..."

    # Create source tarball for packages that need it
    create_source_tarball "${pkg}"

    # Copy package dir to a temp build area so we don't chown the source
    # tree. Build under /var/tmp, not inside the project: synbuild cannot
    # traverse /home/velle (mode 700), so paths under it are unreachable
    # once makepkg drops privileges.
    #
    # ⚠ THE SAME PATH EVERY BUILD. It was mktemp's random one, and makepkg
    # writes its build directory into .BUILDINFO — so every ISO carried
    # packages that differed from the last build's in that line alone. A fixed
    # name directly in /var/tmp would be a gift to anyone who can create it
    # first (this runs as root, and cp -a / chown -R follow what they find),
    # so the fixed part sits under a directory root owns and checks: a real
    # directory, not a link, uid 0, not writable by anyone else.
    local pkgroot=/var/tmp/synapse-pkgbuild
    if [[ ! -e "${pkgroot}" ]]; then
        install -d -m 0755 -o root -g root "${pkgroot}"
    fi
    [[ -d "${pkgroot}" && ! -L "${pkgroot}" && "$(stat -c '%u %a' "${pkgroot}")" == "0 755" ]] \
        || err "${pkgroot} is not a root-owned 0755 directory — refusing to build in it"
    local tmpbuild="${pkgroot}/${pkg}"
    rm -rf "${tmpbuild}"
    mkdir -m 0755 "${tmpbuild}"
    cp -a "${pkgdir}/." "${tmpbuild}/"

    # Copy llama-staging for packages that need it — synapd links against it,
    # synapse-llama packages it. A symlink back into the project tree would be
    # unreachable for synbuild, so copy the resolved backend dir; the bare
    # llama-staging symlink is relative to the project root and dangles here.
    #
    # Keep the backend-qualified NAME (llama-staging-cuda, not llama-staging):
    # synapse-llama's PKGBUILD resolves its tree by backend so it can never
    # package a CPU build as the CUDA one. synapd looks for the bare name, so
    # provide that as a local symlink alongside.
    if [[ "$pkg" != synapse-llama && -d "${staging}" ]]; then
        local _sname
        _sname="$(basename "${staging}")"
        rm -rf "${tmpbuild}/llama-staging" "${tmpbuild}/${_sname}"
        cp -a "${staging}" "${tmpbuild}/${_sname}"
        ln -sfn "${_sname}" "${tmpbuild}/llama-staging"
    fi

    # Drop stale artifacts copied along from the source tree: old packages
    # (so the post-build copy only picks up fresh ones) and previous makepkg
    # extractions (bsdtar overlays but never deletes, so a leftover meson
    # build dir from an older meson version fails the build). Wipe the whole
    # src/ tree, not just src/${pkg}-*: git-sourced packages (nexus-chat,
    # tepris) extract to a versionless src/${pkg} working copy whose .git
    # alternates point back into the original tree at /home/velle, which the
    # unprivileged synbuild user cannot read — leaving it makes makepkg abort
    # with "does not appear to be a git repository". makepkg re-extracts src/
    # (re-cloning git working copies from the cache dir copied alongside).
    rm -f "${tmpbuild}"/*.pkg.tar.zst
    rm -rf "${tmpbuild}/src" "${tmpbuild}/pkg"

    chown -R synbuild: "${tmpbuild}"

    cd "${tmpbuild}"
    # -d: intra-project deps (synapd, syn-model) aren't installable on the
    #     host, and synbuild has no sudo rights for -s to use anyway.
    # -H: keep root's HOME out of synbuild's makepkg environment.
    # No PKGDEST: makepkg silently ignores it as a trailing argument, and
    # local-repo isn't writable by synbuild — let the package land in the
    # build dir and copy it over as root below.
    # SYNAPSE_LLAMA_BACKEND selects which variant synapse-llama's PKGBUILD
    # builds (cpu -> synapse-llama, cuda -> synapse-llama-cuda). sudo scrubs the
    # environment, so hand it over explicitly via env.
    # SOURCE_DATE_EPOCH per package, handed over explicitly for the same
    # reason as the backend: sudo scrubs the environment.
    local pkg_date
    pkg_date=$(repo_date "${pkg}") || pkg_date="${SOURCE_DATE_EPOCH:-}"
    sudo -u synbuild -H env "SYNAPSE_LLAMA_BACKEND=${llama_backend}" \
        ${pkg_date:+"SOURCE_DATE_EPOCH=${pkg_date}"} \
        makepkg -fd --noconfirm \
        2>&1 | sed 's/^/  /' \
        || err "${pkg} build failed — aborting (packages.x86_64 needs every package)"

    cp "${tmpbuild}"/*.pkg.tar.zst "${LOCAL_REPO}/"

    # Before synapd, which PACKAGES builds next and which compiles against it.
    # Only the ISO's own backend takes the llama-staging symlink.
    if [[ "$pkg" == synapse-llama ]]; then
        local _repoint=no
        [[ "${llama_backend}" == "${WITH_GPU}" ]] && _repoint=yes
        stage_llama "${llama_backend}" "${_repoint}"
    fi

    ok "${pkg} built"
    cd "${SCRIPT_DIR}"
    rm -rf "${tmpbuild}"
}

# Start from a clean repo so stale packages from a previous run can't
# mask a failed build — pacstrap must see exactly what this run produced.
rm -f "${LOCAL_REPO}"/*.pkg.tar.zst

# --llama-only: the synapse-llama package for --gpu, staged, and nothing else.
# Restoring a GPU build on a machine should not cost a full ISO build.
if [[ "$LLAMA_ONLY" == "true" ]]; then
    build_package synapse-llama
    ok "--llama-only: stopping before the other packages and the ISO."
    log "To run it on this machine: ./build-all.sh synapse-llama synapd"
    log "(or syn-update apply synapse-llama synapd), then verify with:"
    log "  grep -c nvidia /proc/\$(pidof synapd)/maps"
    exit 0
fi

for pkg in "${PACKAGES[@]}"; do
    build_package "$pkg"
done

# synapse-llama, a second time, against the CUDA staging tree — producing
# synapse-llama-cuda. It goes into the repo but is NOT in packages.x86_64, so
# the ISO itself never installs it: pacstrap resolves synapd's dependency on
# `synapse-llama` to the exact-name package, and packages.x86_64 lists that
# explicitly so the choice can never fall to the provider.
#
# syn-install copies this whole repo to /var/cache/synapseos on the target and
# leaves it in pacman.conf, so the package stays installable for the life of the
# machine — which is what lets the installer switch an NVIDIA box to the GPU
# build, and what lets anyone switch later.
if [[ "$BUILD_CUDA_PKG" == "true" ]]; then
    build_package synapse-llama cuda
fi

# synapse-llama a third time, against the Vulkan staging tree — producing
# synapse-llama-vulkan for AMD/Intel targets. Same repo, same not-in-ISO rule
# as the CUDA package: pacstrap resolves synapd's dep to the exact-name CPU
# package, and syn-install swaps in this one when it detects an AMD/Intel GPU.
if [[ "$BUILD_VULKAN_PKG" == "true" ]]; then
    build_package synapse-llama vulkan
fi

# ── Packages taken from [cachyos], not built ─────────────────
#
# limine-snapper-sync and limine-mkinitcpio-hook are upstream's own packages,
# unpatched, which [cachyos] carries and keeps current. Until 2026-10 they were
# GraalVM native-image builds here — a ~300 MB download, gradle, and plugins
# fetched from the network — producing the same thing, later.
#
# The INSTALLER still needs them from this repo. The live session's pacman knows
# only [synapseos], and pacstrap installs limine-snapper-sync from it on a
# limine + snapshots install. So they are downloaded into the local repo, with
# their signatures checked against this host's keyring exactly as for anything
# else mkarchiso takes from [cachyos].
#
# On the installed system they do NOT stay in /var/cache/synapseos: syn-install
# moves them aside once [cachyos] is enabled, because [synapseos] is listed
# above it and a copy there would hide every update [cachyos] publishes.
FROM_CACHYOS=(limine-snapper-sync limine-mkinitcpio-hook)
step "Fetching ${FROM_CACHYOS[*]} from [cachyos]"
_cdir="$(mktemp -d)"
# A pacman.conf of [cachyos] alone, from the profile's own section. The
# profile's [synapseos] points at a file:// repo that has no database yet at
# this point, and pacman -Sy fails as a whole when any one repo does.
{
    printf '[options]\nArchitecture = auto\nSigLevel = Required DatabaseOptional\n\n'
    sed -n '/^\[cachyos\]/,/^$/p' "${SCRIPT_DIR}/pacman.conf"
} > "${_cdir}/pacman.conf"
mkdir -p "${_cdir}/db"
pacman --config "${_cdir}/pacman.conf" --dbpath "${_cdir}/db" -Sy >/dev/null \
    || err "could not sync [cachyos] to fetch ${FROM_CACHYOS[*]}"
# -dd: only these two. Their dependencies (limine, snapper, …) are ordinary
# packages pacstrap resolves from core/extra itself.
pacman --config "${_cdir}/pacman.conf" --dbpath "${_cdir}/db" --cachedir "${LOCAL_REPO}" \
    -Swdd --noconfirm "${FROM_CACHYOS[@]}" 2>&1 | sed 's/^/  /'
rm -rf "${_cdir}"
unset _cdir
for _p in "${FROM_CACHYOS[@]}"; do
    ls "${LOCAL_REPO}/${_p}"-[0-9]*.pkg.tar.zst &>/dev/null \
        || err "${_p} did not arrive from [cachyos]; a limine install from this ISO would fail"
done
unset _p
ok "${FROM_CACHYOS[*]} from [cachyos]"

# ── Rebuild local repo database ───────────────────────────────
step "Updating local pacman repo"

if ls "${LOCAL_REPO}"/*.pkg.tar.zst &>/dev/null; then
    # Remove stale db files before regenerating
    rm -f "${LOCAL_REPO}"/synapseos.db* "${LOCAL_REPO}"/synapseos.files*
    repo-add "${LOCAL_REPO}/synapseos.db.tar.gz" "${LOCAL_REPO}"/*.pkg.tar.zst
    ok "Local repo: $(ls "${LOCAL_REPO}"/*.pkg.tar.zst | wc -l) packages"
else
    warn "No packages in local-repo — ISO will not include SynapseOS binaries"
fi

# ── The AI model ──────────────────────────────────────────────
#
# OFF by default since 0.2.8. The gguf was ~4.1 GB of an ~8 GB image, for a
# model the live session runs on the CPU — which in a VM (where most people
# meet the live session) is slow enough that offering it is worse than not.
# syn-install asks which model to fetch and downloads it onto the target, so
# the installed system is unchanged and only the download is.
#
# The SWEEP is the load-bearing half. mkarchiso packs airootfs/ whole, so a
# model left over in MODEL_DIR from an earlier --with-model build ships no
# matter what this flag says — an 8 GB ISO from a run that reported no model.
# It is MOVED to model-cache/, not deleted: it is a 4 GB download, it is
# gitignored either way, and --with-model puts it straight back.
MODEL_PATH="${MODEL_DIR}/${MODEL_NAME}"

if [[ "$WITH_MODEL" != "true" ]]; then
    step "Keeping the AI model off the image"

    mkdir -p "${MODEL_CACHE}"
    _parked=0
    for _m in "${MODEL_DIR}"/*.gguf; do
        [[ -e "$_m" ]] || continue
        mv -f "$_m" "${MODEL_CACHE}/$(basename "$_m")"
        log "parked $(basename "$_m") in model-cache/ (kept, not shipped)"
        _parked=$((_parked + 1))
    done
    # A half-downloaded model does not match *.gguf and would have shipped as
    # several GB of nothing. It is not worth parking — the download restarts
    # from scratch anyway.
    rm -f "${MODEL_DIR}"/*.gguf.tmp
    # The manifest describes a model that is no longer there; left behind it
    # tells the live system it has one.
    rm -f "${MODEL_DIR}/manifest.txt"
    if (( _parked )); then
        ok "No model on this ISO — ${_parked} file(s) moved aside (--with-model to embed one)"
    else
        ok "No model on this ISO — the installer downloads one onto the target"
    fi
else
    step "Downloading AI model"

    # Put back whatever a previous no-model run parked, rather than spending
    # 4 GB of bandwidth on a file that is sitting one directory away.
    if [[ ! -f "${MODEL_PATH}" && -f "${MODEL_CACHE}/${MODEL_NAME}" ]]; then
        mv -f "${MODEL_CACHE}/${MODEL_NAME}" "${MODEL_PATH}"
        log "restored ${MODEL_NAME} from model-cache/"
    fi

    if [[ -f "${MODEL_PATH}" ]]; then
        ok "Model already present: $(du -h "${MODEL_PATH}" | cut -f1)"
    else
        log "Downloading ${MODEL_HF_FILE} from HuggingFace (~4.1GB)..."
        HF_URL="https://huggingface.co/${MODEL_HF_REPO}/resolve/main/${MODEL_HF_FILE}"

        curl -L \
            --progress-bar \
            --retry 5 \
            --retry-delay 10 \
            -o "${MODEL_PATH}.tmp" \
            "${HF_URL}" \
            || err "Model download failed"

        mv "${MODEL_PATH}.tmp" "${MODEL_PATH}"
        ok "Model downloaded: $(du -h "${MODEL_PATH}" | cut -f1)"
    fi

    cat > "${MODEL_DIR}/manifest.txt" << EOF
# SynapseOS Model Manifest
# Generated by build.sh $(date -u +%Y-%m-%dT%H:%M:%SZ)

model_name     = ${MODEL_NAME}
source_repo    = ${MODEL_HF_REPO}
source_file    = ${MODEL_HF_FILE}
format         = GGUF Q4_K_M
size_class     = 7B
context_window = 4096
quantization   = Q4_K_M  (4-bit, medium quality)
EOF
fi

# ── Sweep llama libs out of the airootfs overlay ─────────────
# llama used to be shipped by cp'ing the .so files straight into
# airootfs/usr/lib. That put files in /usr/lib owned by NO package: pacman
# could not upgrade or remove them, ldconfig complained about them on every
# transaction, and a stale set silently shadowed the real libraries in the
# ld.so cache — which is how synapd ran a months-old CPU-only libllama while
# reporting that GPU offload was on.
#
# The synapse-llama package now owns these files and synapd depends on it, so
# they arrive through pacman like everything else. This step only cleans up the
# orphans a previous build of this script left in the overlay; without it they
# would keep shipping and keep shadowing the packaged copies.
step "Sweeping unowned llama libs from airootfs"

_swept=0
for _f in "${SCRIPT_DIR}"/airootfs/usr/lib/libllama*.so* \
          "${SCRIPT_DIR}"/airootfs/usr/lib/libggml*.so* \
          "${SCRIPT_DIR}"/airootfs/usr/lib/libmtmd*.so* \
          "${SCRIPT_DIR}"/airootfs/usr/bin/llama-server \
          "${SCRIPT_DIR}"/airootfs/usr/bin/llama-cli; do
    [[ -e "$_f" ]] || continue
    rm -f "$_f"
    _swept=$((_swept + 1))
done
if (( _swept )); then
    warn "removed ${_swept} unowned llama file(s) from the airootfs overlay"
    warn "they are shipped by the synapse-llama package now"
fi
ok "airootfs carries no unowned llama libs"

# ── Configure airootfs ────────────────────────────────────────
step "Configuring airootfs"

mkdir -p \
    "${SCRIPT_DIR}/airootfs/etc/synguard/rules.d" \
    "${SCRIPT_DIR}/airootfs/etc/synui" \
    "${SCRIPT_DIR}/airootfs/etc/modprobe.d" \
    "${SCRIPT_DIR}/airootfs/etc/modules-load.d" \
    "${SCRIPT_DIR}/airootfs/etc/systemd/system" \
    "${SCRIPT_DIR}/airootfs/etc/pacman.d" \
    "${SCRIPT_DIR}/airootfs/usr/bin" \
    "${SCRIPT_DIR}/airootfs/var/lib/synapd/models" \
    "${SCRIPT_DIR}/airootfs/var/lib/synguard" \
    "${SCRIPT_DIR}/airootfs/var/log/synguard"

# ── Mirrorlist (must exist or pacman on live ISO has no servers) ──
cat > "${SCRIPT_DIR}/airootfs/etc/pacman.d/mirrorlist" << 'MIRROREOF'
Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch
Server = https://mirror.rackspace.com/archlinux/$repo/os/$arch
Server = https://mirrors.kernel.org/archlinux/$repo/os/$arch
MIRROREOF
ok "mirrorlist written"

# ── pacman.conf for live ISO ──────────────────────────────────
# The local-repo is accessible on the live system at:
#   /run/archiso/airootfs/local-repo  (squashfs mount)
# We do NOT embed the build machine's absolute path here.
# Build-time pacman.conf — points to the actual local-repo on the build host
cat > "${SCRIPT_DIR}/pacman.conf" << PACMANEOF
[options]
HoldPkg     = pacman glibc
Architecture = auto
ParallelDownloads = 5
SigLevel    = Required DatabaseOptional
LocalFileSigLevel = Optional

[core]
Include = /etc/pacman.d/mirrorlist

[extra]
Include = /etc/pacman.d/mirrorlist

# SynapseOS local packages — build-time path
[synapseos]
SigLevel = Optional TrustAll
Server = file://${LOCAL_REPO}

# CachyOS — LAST, so it can only supply names no repository above carries
# (sunshine). The same rule syn-install follows on an installed system.
[cachyos]
Server = https://cdn77.cachyos.org/repo/\$arch/\$repo
Server = https://mirror.cachyos.org/repo/\$arch/\$repo
PACMANEOF
ok "pacman.conf written (build-time paths)"

# Live-ISO pacman.conf — written into airootfs for the running system
cat > "${SCRIPT_DIR}/airootfs/etc/pacman.conf" << 'LIVEPACMANEOF'
[options]
HoldPkg     = pacman glibc
Architecture = auto
ParallelDownloads = 5
SigLevel    = Required DatabaseOptional
LocalFileSigLevel = Optional
# The Pac-Man progress bar, on by default on SynapseOS. Undocumented and in no
# stock pacman.conf, but a real option — `pacman-conf ILoveCandy` knows it — and
# a bare key with no value. syn-install writes the same line into the INSTALLED
# system's pacman.conf; this one is only for the live session.
ILoveCandy

[core]
Include = /etc/pacman.d/mirrorlist

[extra]
Include = /etc/pacman.d/mirrorlist

[synapseos]
SigLevel = Optional TrustAll
Server = file:///run/archiso/airootfs/local-repo
LIVEPACMANEOF
ok "airootfs/etc/pacman.conf written (live-ISO paths)"

# ── OS identity (os-release / issue / motd) ───────────────────
#
# These three files carry the version the booted system reports: os-release is
# what every tool reads (fastfetch, systemd, syn-install), issue is the getty
# banner, motd greets the shell. They used to be static files with 0.1.0 typed
# into them and nothing ever rewrote them, so EVERY ISO from v0.1.1 to v0.1.6
# booted announcing "0.1.0" — the write was fine, the distro was just lying
# about itself, which reads exactly like a failed dd and cost a debugging round.
#
# The single source of truth is iso_version in profiledef.sh (see the version
# model: iso_version is the DISTRO version and is the one to bump; the packages
# stay 0.1.0-N because PKGBUILDs source that tarball name literally, so
# SYNAPSEOS_VERSION here must NOT be used for this).
#
# Substituted in place rather than templated from a placeholder because
# mkarchiso copies airootfs/ verbatim with no hook to intercept, and because
# this step already generates airootfs content the same way (mirrorlist,
# pacman.conf above). The version pattern is matched loosely so this is
# idempotent — re-running rewrites whatever version is already there.
ISO_VERSION=$(sed -n 's/^iso_version="\([^"]*\)".*/\1/p' "${SCRIPT_DIR}/profiledef.sh")
[[ -n "$ISO_VERSION" ]] || err "Could not read iso_version from profiledef.sh"

_v='[0-9][0-9A-Za-z.+~-]*'

sed -i \
    -e "s|^PRETTY_NAME=\"SynapseOS ${_v}\"|PRETTY_NAME=\"SynapseOS ${ISO_VERSION}\"|" \
    -e "s|^VERSION=\"${_v}\"|VERSION=\"${ISO_VERSION}\"|" \
    -e "s|^VERSION_ID=\"${_v}\"|VERSION_ID=\"${ISO_VERSION}\"|" \
    -e "s|^BUILD_ID=${_v}|BUILD_ID=${ISO_VERSION}|" \
    "${SCRIPT_DIR}/airootfs/etc/os-release"

sed -i -e "s|Version ${_v} |Version ${ISO_VERSION} |" \
    "${SCRIPT_DIR}/airootfs/etc/issue"

sed -i -e "s|SynapseOS ${_v} |SynapseOS ${ISO_VERSION} |" \
    "${SCRIPT_DIR}/airootfs/etc/motd"

# Prove it landed rather than trusting sed's exit status, which is 0 whether or
# not the pattern matched — a silent no-match is precisely how the old version
# would persist into a release again.
for _f in etc/os-release etc/issue etc/motd; do
    grep -q -- "$ISO_VERSION" "${SCRIPT_DIR}/airootfs/${_f}" \
        || err "airootfs/${_f} does not carry version ${ISO_VERSION} after substitution"
done
grep -q "^VERSION_ID=\"${ISO_VERSION}\"$" "${SCRIPT_DIR}/airootfs/etc/os-release" \
    || err "airootfs/etc/os-release VERSION_ID is not ${ISO_VERSION}"
ok "OS identity stamped ${ISO_VERSION} (os-release, issue, motd)"

# ── Boot menu identity (grub + syslinux) ─────────────────────
#
# The same bug as above, one layer earlier, and it survived the 2026-07-16 fix
# because that fix asked "what does the booted system report about itself" and
# stopped there. The bootloader menu is what you read BEFORE any of that, and
# grub.cfg had "SynapseOS 0.1.0" typed into it — so the 0.2.9 image announced
# 0.1.0 on the very first screen, which is exactly the observation that once got
# a good USB write thrown away as a bad one.
#
# mkarchiso copies grub/ and syslinux/ out of the profile directory the same way
# it copies airootfs/, so the substitution happens in the tree here, before it
# runs. Same loose pattern, same idempotence.
#
# ⚠ ASCII only in the syslinux files. vesamenu.c32 draws from a fixed VGA
# codepage, so a UTF-8 dash there is not a dash that renders plainly, it is
# bytes that render as noise. grub is fine with UTF-8.
_bootcfgs=(
    "${SCRIPT_DIR}/grub/grub.cfg"
    "${SCRIPT_DIR}/grub/loopback.cfg"
    "${SCRIPT_DIR}/syslinux/archiso_head.cfg"
    "${SCRIPT_DIR}/syslinux/archiso_sys-linux.cfg"
    "${SCRIPT_DIR}/syslinux/archiso_pxe-linux.cfg"
)
for _f in "${_bootcfgs[@]}"; do
    [[ -f $_f ]] || err "boot config missing: $_f"
    sed -i -e "s|SynapseOS ${_v}|SynapseOS ${ISO_VERSION}|g" "$_f"
done

# Prove it, and prove the negative too: a menu that still says the OLD version
# is the failure this whole block exists to prevent, and sed exits 0 either way.
for _f in "${_bootcfgs[@]}"; do
    grep -q "SynapseOS ${ISO_VERSION}" "$_f" \
        || err "$(basename "$_f") does not carry version ${ISO_VERSION} after substitution"
done
if grep -rn "Arch Linux" "${SCRIPT_DIR}/grub/" "${SCRIPT_DIR}/syslinux/" >/dev/null 2>&1; then
    err "boot menus still carry Arch branding — see grub/ and syslinux/"
fi
ok "Boot menu identity stamped ${ISO_VERSION} (grub, loopback, syslinux, PXE)"

# ── Pin the ISO's synapse-llama backend in packages.x86_64 ────
#
# packages.x86_64 names the llama package to install on the ISO. For a CPU ISO
# the exact package `synapse-llama` exists, so pacstrap installs it and never
# reaches a provider. But a --gpu=X ISO builds NO plain `synapse-llama` — only
# `synapse-llama-cuda`/`-vulkan`, both of which `provides` it — so the bare name
# falls through to pacstrap's provider default, which is ALPHABETICAL: cuda wins
# and a `--gpu=vulkan` ISO silently ships CUDA (needs libcuda.so.1, won't boot
# without an NVIDIA driver). Pin the exact backend name so the ISO installs the
# one this build actually made. Idempotent; a cpu build restores the bare name.
_llama_pkg=synapse-llama
[[ "$WITH_GPU" != cpu ]] && _llama_pkg="synapse-llama-${WITH_GPU}"
sed -i -E "s#^synapse-llama(-(cuda|vulkan|rocm))?\$#${_llama_pkg}#" \
    "${SCRIPT_DIR}/packages.x86_64"
grep -qx "${_llama_pkg}" "${SCRIPT_DIR}/packages.x86_64" \
    || err "packages.x86_64 does not pin ${_llama_pkg} after substitution"
ok "ISO llama backend pinned: ${_llama_pkg}"

# ── Run mkarchiso ─────────────────────────────────────────────
step "Building ISO (mkarchiso)"

log "Building ISO..."
log "This step takes 10-30 minutes depending on download speed..."

mkarchiso \
    -v \
    -w "${WORK_DIR}" \
    -o "${OUT_DIR}" \
    "${SCRIPT_DIR}" \
    || err "mkarchiso failed — check work/ for logs"

ISO_FILE=$(ls -t "${OUT_DIR}"/*.iso 2>/dev/null | head -1)
[[ -n "$ISO_FILE" ]] || err "ISO not found in ${OUT_DIR}"
ok "ISO built: ${ISO_FILE}"

# ── Hand the output back BEFORE signing ──────────────────────
#
# ⛔ BEFORE, not after. The .asc is written next to the ISO by the invoking
# user, while out/ is created by mkarchiso as ROOT — so on a machine where out/
# did not already exist from an earlier build, the signature died on a directory
# it could not write, at the end of the build. It has only ever worked here
# because out/ was already velle's from a previous run.
#
# The chown further down still runs: it covers the checksum files, which are
# written after this point.
if [[ -n "${SUDO_USER:-}" ]]; then
    chown "${SUDO_USER}:$(id -gn "${SUDO_USER}")" \
        "${OUT_DIR}" "${ISO_FILE}" 2>/dev/null || true
fi

# ── Sign ──────────────────────────────────────────────────────
#
# ⚠ ONE OWNER. Which key, as which user, and the verify that follows all live in
# sign-iso.sh — which is also what you run to sign an image that was built
# without a signature. This block used to carry its own copy of the gpg
# invocation, and two copies of "sign the release correctly" is one more than
# can be kept true.
if [[ "$SIGN" == "true" ]]; then
    step "Signing ISO"
    # ⚠ The failure message names the recovery, because the ISO is FINE: a
    # detached signature does not touch the image, so a failure here costs a
    # passphrase, never a rebuild.
    "${SCRIPT_DIR}/sign-iso.sh" "${ISO_FILE}" \
        || err "signing failed — the ISO itself is built and intact.
     Nothing needs rebuilding. Sign it with:
       ./archiso/sign-iso.sh $(basename "${ISO_FILE}")"
else
    warn "Unsigned build (--no-sign) — sign it later with ./archiso/sign-iso.sh"
fi

# ── Checksums ─────────────────────────────────────────────────
step "Generating checksums"
cd "${OUT_DIR}"
sha256sum "$(basename "${ISO_FILE}")" > "$(basename "${ISO_FILE}").sha256"
b2sum    "$(basename "${ISO_FILE}")" > "$(basename "${ISO_FILE}").b2sum"
ok "sha256: $(cat "$(basename "${ISO_FILE}").sha256" | cut -d' ' -f1)"

# ── Fix output ownership ─────────────────────────────────────
# mkarchiso runs as root, so fix ownership of output files
# so the user who invoked sudo can read/test the ISO without issues.
if [[ -n "${SUDO_USER:-}" ]]; then
    # Chown the out/ dir too, not just its files: publish-release.sh runs as
    # the normal user and writes the split .part* files into out/, which fails
    # if the mkarchiso-created directory stays root-owned.
    chown "${SUDO_USER}:$(id -gn "${SUDO_USER}")" \
        "${OUT_DIR}" "${OUT_DIR}"/*.iso "${OUT_DIR}"/*.sha256 "${OUT_DIR}"/*.b2sum 2>/dev/null || true
    ok "Output dir + files owned by ${SUDO_USER}"
fi

# ── Summary ───────────────────────────────────────────────────
step "Build complete"
echo
echo -e "${C_BRAND}"
echo "  ╭───────────────────────────────────────────╮"
echo "  │  SynapseOS ${ISO_VERSION} — Build complete        │"
echo "  ╰───────────────────────────────────────────╯"
echo -e "${C_RESET}"
echo -e "  ISO:    ${C_BOLD}${ISO_FILE}${C_RESET}"
echo -e "  Size:   ${C_DIM}$(du -h "${ISO_FILE}" | cut -f1)${C_RESET}"

# ⚠ SIGNED OR NOT IS STATED, always. "It did not say anything about signing" is
# indistinguishable from "it signed it", and that is how an unsigned ISO gets
# published by someone who believed otherwise. Read off the FILE rather than the
# SIGN flag: the flag says what was asked for, the .asc says what is there.
if [[ -f "${ISO_FILE}.asc" ]]; then
    echo -e "  Signed: ${C_OK}yes${C_RESET} ${C_DIM}$(basename "${ISO_FILE}").asc${C_RESET}"
else
    echo -e "  Signed: ${C_WARN}NO${C_RESET} ${C_DIM}— ./archiso/sign-iso.sh $(basename "${ISO_FILE}")${C_RESET}"
fi
echo
echo -e "  ${C_BRAND}1. Test in QEMU:${C_RESET}"
echo -e "  ${C_DIM}./archiso/build_scripts/qemu-test.sh ${ISO_FILE}${C_RESET}"
echo -e "  ${C_DIM}QEMU_NO_CD=1 ./archiso/build_scripts/qemu-test.sh   # boot the INSTALLED disk, no media${C_RESET}"
echo
# ⛔ write-usb.sh, NOT a bare dd. dd reporting the byte count does not mean the
# stick holds those bytes — a short or unflushed tail leaves the label, the
# version file and the kernel all readable while grub.cfg and the squashfs are
# garbage, which reads as a broken ISO on a machine where the ISO is blameless.
# 0.2.9.2 and 0.1.7 both went that way. write-usb.sh reads the whole stick back
# with cmp and prints VERIFIED GOOD or fails; the summary used to print the dd
# line that cost those two releases.
echo -e "  ${C_BRAND}2. Write to USB${C_RESET} ${C_DIM}(reads the stick back and verifies it — this erases /dev/sdX):${C_RESET}"
echo -e "  ${C_DIM}lsblk                                              # find the DISK, not a partition${C_RESET}"
echo -e "  ${C_DIM}sudo ./archiso/write-usb.sh /dev/sdX ${ISO_VERSION}${C_RESET}"
echo
echo -e "  ${C_BRAND}3. Publish the GitHub release:${C_RESET}"
echo -e "  ${C_DIM}\$EDITOR archiso/release-notes/${ISO_VERSION}.md              # optional, but it is the page${C_RESET}"
echo -e "  ${C_DIM}./archiso/publish-release.sh ${ISO_VERSION}${C_RESET}"
echo -e "  ${C_DIM}   splits the ISO into 1900 MiB parts (GitHub caps assets at 2 GiB),${C_RESET}"
echo -e "  ${C_DIM}   uploads them with the checksums and the .asc, as YOUR user (gh's token).${C_RESET}"
echo
