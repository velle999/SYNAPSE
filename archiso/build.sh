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
#   5. Download the AI model (synapse-7b-q4_k_m.gguf)
#   6. Run mkarchiso
#   7. Verify the ISO
#
# Usage:
#   sudo ./build.sh [OPTIONS]
#
# Options:
#   --no-model      Skip model download (build ISO without embedded model)
#   --gpu=TYPE      Build llama.cpp with GPU backend: cuda, rocm, vulkan, or
#                   auto. Default is CPU-only: a CUDA/ROCm build links the
#                   shipped libggml against the BUILD HOST's driver stack
#                   (libcuda.so.1 / rocblas), and synapd dies with exit 127 on
#                   any machine without that driver — including every VM.
#                   'vulkan' (AMD/Intel) is the exception: it links only the
#                   Vulkan loader, loads with no GPU, and one build runs on any
#                   AMD/Intel card — so it is safe to offer broadly.
#   --no-gpu        (default; kept for compatibility)
#   --llama-only    Build and stage llama.cpp, then stop (no packages, no ISO).
#                   This is the recovery path for the local GPU runtime:
#                   sudo ./build.sh --gpu=cuda --llama-only, then reinstall
#                   synapd. Restoring GPU should not cost a full ISO build.
#   --no-clean      Skip cleaning previous build artifacts
#   --jobs N        Parallel build jobs (default: nproc)
#   --sign          GPG-sign the ISO
#   --help          This help
#
# Requirements:
#   - Arch Linux host (or Arch-based)
#   - archiso package installed
#   - ~20GB free disk space (with model), ~8GB without
#   - Internet access (for package downloads and model)
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
LLAMA_DIR="${BUILD_DIR}/llama.cpp"
# Keep in sync with LLAMA_REF in .github/workflows/build.yml
LLAMA_REF="b8272"
MODEL_DIR="${SCRIPT_DIR}/airootfs/var/lib/synapd/models"

# Model to embed — filename must match what synapd.service and syn-model expect
MODEL_NAME="synapse.gguf"
MODEL_HF_REPO="TheBloke/Mistral-7B-Instruct-v0.2-GGUF"
MODEL_HF_FILE="mistral-7b-instruct-v0.2.Q4_K_M.gguf"

JOBS="$(nproc)"
WITH_MODEL=true
# CPU-only by default — see --gpu note in the header before changing this.
WITH_GPU=cpu
CLEAN=true
SIGN=false
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

# ── Argument parsing ──────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --no-model)   WITH_MODEL=false ;;
        --no-gpu)     WITH_GPU=cpu ;;
        --gpu=*)      WITH_GPU="${arg#--gpu=}" ;;
        --llama-only) LLAMA_ONLY=true ;;
        --no-clean)   CLEAN=false ;;
        --sign)       SIGN=true ;;
        --jobs=*)     JOBS="${arg#--jobs=}" ;;
        --help|-h)
            sed -n '3,35p' "$0" | grep '^#' | sed 's/^# \?//'
            exit 0 ;;
        *)  warn "Unknown option: $arg" ;;
    esac
done

# ── Preflight ─────────────────────────────────────────────────
step "Preflight checks"

[[ "$(id -u)" -eq 0 ]] || err "Must run as root (needed for mkarchiso)"

command -v pacman &>/dev/null || err "pacman not found — must build on Arch Linux"

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
        # no per-arch compile and only the mesa RADV ICD at runtime. ROCm stays
        # an explicit opt-in (--gpu=rocm) for a known-supported card wanting
        # peak throughput.
        WITH_GPU="vulkan"
        log "Detected AMD GPU — building llama.cpp with Vulkan (portable; --gpu=rocm for peak perf)"
    else
        WITH_GPU="cpu"
        log "No discrete GPU detected — building llama.cpp CPU-only"
    fi
fi

case "$WITH_GPU" in
    cpu) ;;
    cuda|rocm)
        warn "GPU build ($WITH_GPU): the ISO will only run AI features on"
        warn "machines with the matching driver installed — synapd will fail"
        warn "to start anywhere else (VMs included). CPU is the safe default." ;;
    vulkan)
        warn "GPU build (vulkan): synapd loads everywhere (the loader tolerates"
        warn "no GPU), but only offloads where a Vulkan ICD + GPU exist — else it"
        warn "runs on the CPU. The ISO still ships CPU by default." ;;
    *) err "Invalid --gpu value: $WITH_GPU (expected cuda, rocm, vulkan, or auto)" ;;
esac

# Stage each backend into its own directory. The install is a wipe-and-replace
# (see the rm -rf below — layered installs leave mismatched sonames), so a
# single shared staging dir meant an ISO build, which is CPU-only by default,
# silently destroyed the CUDA build this machine's synapd runs on. Keeping them
# apart means building the ISO can never take the local GPU runtime down.
# llama-staging stays as a symlink to the last build: synapd's PKGBUILD,
# build-all.sh and /etc/ld.so.conf.d all reference that path by name.
LLAMA_STAGING="${PROJECT_ROOT}/llama-staging-${WITH_GPU}"
LLAMA_STAGING_LINK="${PROJECT_ROOT}/llama-staging"

# ── Clean ─────────────────────────────────────────────────────
# Always wipe the mkarchiso work dir, even under --no-clean. It carries
# per-stage stamp files AND the pacstrapped airootfs; a stale one from a
# previous build makes mkarchiso skip straight to "Done!" — reusing the old
# ISO with the old packages (e.g. an unbumped synui) and never picking up
# freshly built ones. --no-clean is only meant to preserve the expensive
# build/ (llama.cpp) compile.
rm -rf "${WORK_DIR}"
if [[ "$CLEAN" == "true" ]]; then
    step "Cleaning previous build"
    rm -rf "${BUILD_DIR}"
    log "Cleaned work/ and build/"
else
    log "Cleaned work/ (kept build/ — llama.cpp)"
fi

mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${LOCAL_REPO}" "${MODEL_DIR}"

# ── Build llama.cpp ───────────────────────────────────────────
#
# build_llama <backend> <repoint:yes|no>
#
# A function because the ISO now needs TWO backends out of one run: the CPU
# build it actually ships, and a CUDA build that is packaged but never
# installed on the ISO — so that an installed system with an NVIDIA card has a
# synapse-llama-cuda to switch to. Before this, nothing ever built that package
# during a release, nothing shipped it, and every installed SynapseOS ran the
# headline feature on the CPU no matter what card was in the machine.
#
# Each backend gets its own build dir (build-cpu/, build-cuda/). That is not
# tidiness: `build/` was shared and reused across runs, and CMake option()
# values are STICKY, so a cached GGML_CUDA=ON from an earlier cuda build made a
# later "cpu" build silently reconfigure with CUDA still on — an ISO whose
# synapd links libcuda.so.1 and therefore fails to start on any machine without
# an NVIDIA driver. A directory that is only ever configured for one backend
# cannot have that bug at all. The cache check below stays as a safety net, and
# alternating backends no longer throws away either build's objects.
build_llama() {
    local backend="$1" repoint="$2"
    local staging="${PROJECT_ROOT}/llama-staging-${backend}"
    local bdir="${BUILD_DIR}/llama-build-${backend}"

    step "Building llama.cpp (${backend})"

    if [[ ! -d "${LLAMA_DIR}" ]]; then
        log "Cloning llama.cpp..."
        git clone --depth=1 --branch "${LLAMA_REF}" \
            https://github.com/ggerganov/llama.cpp "${LLAMA_DIR}"
    fi

    cd "${LLAMA_DIR}"
    log "(llama.cpp pinned at ${LLAMA_REF})"

    # CUDA 13+ ships CCCL 3.4, which changed the cub/thrust APIs this pinned
    # llama.cpp (${LLAMA_REF}) uses in argsort.cu / top-k.cu (cuda::make_*_iterator,
    # DeviceTopK::MaxPairs) — the build fails with "namespace cuda has no member".
    # ggml only reaches for cub as a fast path and has portable fallback kernels, so
    # gate it off on CUDA 13+ (older toolkits keep cub). Idempotent, cuda-only —
    # and harmless to a CPU build, which never compiles that file.
    if [[ "$backend" == "cuda" ]]; then
        local _cub_guard='ggml/src/ggml-cuda/common.cuh'
        if grep -q 'CUDART_VERSION >= 11070$' "$_cub_guard" 2>/dev/null; then
            log "Patching ggml to disable cub on CUDA 13+ (CCCL 3.4 API break)"
            sed -i 's|\(CUDART_VERSION >= 11070\)$|\1 \&\& CUDART_VERSION < 13000|' "$_cub_guard"
        fi
    fi

    # Safety net. Per-backend dirs mean this should never fire; it stays because
    # the failure it catches is silent and shipped once.
    if [[ -f "${bdir}/CMakeCache.txt" ]]; then
        local _want_cuda=OFF; [[ "$backend" == cuda ]] && _want_cuda=ON
        local _have_cuda
        _have_cuda=$(sed -n 's/^GGML_CUDA:BOOL=//p' "${bdir}/CMakeCache.txt" | head -1)
        if [[ -n "$_have_cuda" && "$_have_cuda" != "$_want_cuda" ]]; then
            log "${bdir} was configured GGML_CUDA=${_have_cuda}, want ${_want_cuda} — wiping it"
            rm -rf "${bdir}"
        fi
    fi

    mkdir -p "${bdir}"
    cd "${bdir}"

    local CMAKE_ARGS=(
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_INSTALL_PREFIX=/usr"
        "-DLLAMA_BUILD_TESTS=OFF"
        "-DLLAMA_BUILD_EXAMPLES=ON"
        "-DLLAMA_SERVER=ON"
    )

    # EVERY backend toggle is stated explicitly on EVERY path, never left to
    # default — see the note above build_llama about sticky option() values.
    case "$backend" in
        cuda)
            CMAKE_ARGS+=("-DGGML_CUDA=ON" "-DGGML_HIPBLAS=OFF" "-DGGML_VULKAN=OFF")
            # Ensure CMake can find CUDA even under sudo (which strips env vars)
            if [[ -d /opt/cuda ]]; then
                CMAKE_ARGS+=("-DCUDAToolkit_ROOT=/opt/cuda" "-DCMAKE_CUDA_COMPILER=/opt/cuda/bin/nvcc")
            fi
            ;;
        rocm)  CMAKE_ARGS+=("-DGGML_CUDA=OFF" "-DGGML_HIPBLAS=ON" "-DGGML_VULKAN=OFF" "-DAMDGPU_TARGETS=gfx1030;gfx1100") ;;
        # Vulkan: portable AMD/Intel GPU backend. Needs glslc (shaderc) +
        # Vulkan headers at build time to compile the compute shaders into
        # libggml-vulkan.so; both are checked before this runs. GGML_NATIVE=OFF
        # is REQUIRED here (unlike CUDA): the Vulkan build's whole point is that
        # it loads with no GPU and FALLS BACK TO THE CPU — a reachable path, so
        # baking the build host's AVX-512/AVX2 into it would SIGILL that fallback
        # on baseline x86-64 (and in a VM without -cpu host). CUDA gets away
        # without it only because its lib can't load at all without the GPU, so
        # its CPU path is never reached. GGML_NATIVE only touches the CPU
        # backend — it costs GPU users nothing.
        vulkan) CMAKE_ARGS+=("-DGGML_CUDA=OFF" "-DGGML_HIPBLAS=OFF" "-DGGML_VULKAN=ON" "-DGGML_NATIVE=OFF") ;;
        # GGML_NATIVE=OFF: NATIVE bakes the BUILD HOST's instruction set
        # (AVX2/AVX-512) into libggml, and synapd dies with SIGILL on any
        # CPU without those extensions — VMs without -cpu host included.
        # The ISO must run on baseline x86-64.
        cpu)   CMAKE_ARGS+=("-DGGML_CUDA=OFF" "-DGGML_HIPBLAS=OFF" "-DGGML_VULKAN=OFF" "-DGGML_NATIVE=OFF") ;;
    esac

    # The Vulkan backend compiles GLSL compute shaders at build time; without
    # glslc (shaderc) and the Vulkan headers CMake silently configures GGML_VULKAN
    # back OFF and produces a CPU build wearing the vulkan name. Catch it here.
    if [[ "$backend" == "vulkan" ]]; then
        command -v glslc &>/dev/null \
            || err "Vulkan build needs 'glslc' — install 'shaderc'. (also: vulkan-headers)"
    fi

    log "CMake configure (GPU: ${backend})..."
    cmake "${LLAMA_DIR}" "${CMAKE_ARGS[@]}"

    log "Building llama.cpp (${JOBS} jobs)..."
    make -j"${JOBS}"

    log "Installing llama.cpp to staging area..."
    # Wipe first: layering installs from different llama.cpp builds leaves
    # mismatched libllama/libggml sonames that break linking against staging.
    # Scoped to this backend's dir — never touches the other backends' builds.
    rm -rf "${staging}"
    DESTDIR="${staging}" make install

    # A cuda build that produced no cuda backend is a CPU build wearing the
    # wrong name — and it would be packaged as synapse-llama-cuda and installed
    # on exactly the machines that wanted the GPU. Fail loudly instead.
    if [[ "$backend" == "vulkan" && ! -e "${staging}/usr/lib/libggml-vulkan.so" ]]; then
        err "Vulkan build produced no libggml-vulkan.so — this is a CPU build. Refusing to package it as synapse-llama-vulkan."
    fi
    if [[ "$backend" == "cuda" && ! -e "${staging}/usr/lib/libggml-cuda.so" ]]; then
        err "CUDA build produced no libggml-cuda.so — this is a CPU build. Refusing to package it as synapse-llama-cuda."
    fi

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
        # a GPU build, say so loudly: synapd resolves libllama through this path at
        # runtime, so the machine silently drops to CPU inference until it is restored.
        local _prev
        _prev="$(readlink "${LLAMA_STAGING_LINK}" 2>/dev/null || true)"
        if [[ -n "$_prev" && "$_prev" != "$(basename "${staging}")" && "$_prev" != "llama-staging-cpu" ]]; then
            warn "llama-staging was pointing at ${_prev} — repointing it to the ${backend} build."
            warn "Anything linking against llama-staging (synapd) now runs on ${backend}."
            warn "Restore with: ln -sfn ${_prev} ${LLAMA_STAGING_LINK}"
        fi
        # -n: don't descend into the existing symlink and nest the link inside its target
        ln -sfn "$(basename "${staging}")" "${LLAMA_STAGING_LINK}"
    fi

    ok "llama.cpp (${backend}) built and staged to ${staging}"
    cd "${SCRIPT_DIR}"
}

# The ISO's own backend. Repoints llama-staging, because that is the build
# synapd on this host links against.
build_llama "${WITH_GPU}" yes

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

if [[ "$BUILD_CUDA_PKG" == "true" && "$LLAMA_ONLY" != "true" ]]; then
    # repoint=no: this build is for packaging only. Stealing the llama-staging
    # symlink would silently change what this host's synapd links against.
    build_llama cuda no
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
elif command -v glslc &>/dev/null; then
    BUILD_VULKAN_PKG=true
else
    warn "No Vulkan shader compiler (glslc) on this host — synapse-llama-vulkan will NOT be built."
    warn "The ISO stays valid, but an AMD/Intel machine installed from it will run"
    warn "inference on the CPU with no way to switch. Install 'shaderc' to fix."
fi

if [[ "$BUILD_VULKAN_PKG" == "true" && "$LLAMA_ONLY" != "true" ]]; then
    build_llama vulkan no
fi

if [[ "$LLAMA_ONLY" == "true" ]]; then
    if [[ "$WITH_GPU" == "cuda" && ! -e "${LLAMA_STAGING}/usr/lib/libggml-cuda.so" ]]; then
        err "CUDA was requested but libggml-cuda.so is not in the staging lib dir — this is a CPU build."
    fi
    if [[ "$WITH_GPU" == "vulkan" && ! -e "${LLAMA_STAGING}/usr/lib/libggml-vulkan.so" ]]; then
        err "Vulkan was requested but libggml-vulkan.so is not in the staging lib dir — this is a CPU build."
    fi
    ok "--llama-only: stopping before packages/ISO."
    log "Next: rebuild + reinstall synapd so it links against this staging, then"
    log "verify with: grep -c nvidia /proc/\$(pidof synapd)/maps"
    exit 0
fi

cd "${SCRIPT_DIR}"

# ── Build SynapseOS packages ──────────────────────────────────
step "Building SynapseOS packages"

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
    # pacstrap with "unable to satisfy dependency 'scenefx'".
    scenefx
    synui
    synapse_kmod
    syn
    syn-firstboot
    syn-model
    syn-install
    nexus-chat
    tepris
    chibi
    vibe
    # Printer driver for the Xpress M2020W — an SPL printer too old for the
    # driverless IPP path cups otherwise relies on. Script/data package:
    # ships the vendor ULD rastertospl filter, its dlopen'd libscmssc.so, and
    # the rastertospl PPD. See samsung-m2020/PKGBUILD for the two traps.
    samsung-m2020
    # Shelly — GTK4/libalpm package manager (dir 'shelly' builds pkg
    # 'shelly-bin'). Prebuilt-binary PKGBUILD: makepkg downloads the pinned
    # release tarball at build time, so this needs network like pacstrap does.
    shelly
)

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

    # Collect directories/files that exist in the package
    local items=()
    [ -d "${pkg}/src" ]     && items+=("${pkg}/src/")
    [ -d "${pkg}/include" ] && items+=("${pkg}/include/")
    [ -f "${pkg}/meson.build" ] && items+=("${pkg}/meson.build")
    [ -d "${pkg}/data" ]    && items+=("${pkg}/data/")
    [ -d "${pkg}/config" ]  && items+=("${pkg}/config/")
    [ -d "${pkg}/systemd" ] && items+=("${pkg}/systemd/")
    [ -d "${pkg}/sysusers" ] && items+=("${pkg}/sysusers/")
    [ -d "${pkg}/tmpfiles" ] && items+=("${pkg}/tmpfiles/")
    [ -d "${pkg}/rules" ]   && items+=("${pkg}/rules/")
    [ -d "${pkg}/protocols" ] && items+=("${pkg}/protocols/")
    [ -d "${pkg}/tests" ]   && items+=("${pkg}/tests/")
    # synapse_kmod extras
    [ -f "${pkg}/Makefile" ]  && items+=("${pkg}/Makefile")
    [ -f "${pkg}/dkms.conf" ] && items+=("${pkg}/dkms.conf")
    [ -d "${pkg}/hooks" ]     && items+=("${pkg}/hooks/")
    [ -d "${pkg}/tools" ]     && items+=("${pkg}/tools/")
    [ -f "${pkg}/synapse_kmod.install" ] && items+=("${pkg}/synapse_kmod.install")
    # vibe extras: a pure-Python app (entry point + package + launcher), not the
    # src/include/meson layout the C packages use.
    [ -f "${pkg}/main.py" ]    && items+=("${pkg}/main.py")
    [ -d "${pkg}/vibe" ]       && items+=("${pkg}/vibe/")
    [ -d "${pkg}/packaging" ]  && items+=("${pkg}/packaging/")
    # Top-level docs: PKGBUILDs install them (synapse_kmod ships HARDENING.md to
    # /usr/share/doc), so omitting them fails package() with "cannot stat".
    # Globbed rather than named, because this collector is a SECOND COPY of the
    # one in build-all.sh and the two drift: build-all.sh grew a HARDENING.md
    # line and this one did not, so the ISO build broke the first time anyone
    # cut a release after that file was added.
    for _md in "${pkg}"/*.md; do
        [ -f "$_md" ] && items+=("$_md")
    done

    if [[ ${#items[@]} -eq 0 ]]; then
        warn "No source files found for ${pkg}"
        return 1
    fi

    # Exclude ONLY the versioned makepkg extraction dir, never the broad
    # ${pkg}-* glob: that also matched real source files named ${pkg}-*
    # (e.g. src/synui-lock-auth.c), silently dropping them from the tarball so
    # the build failed with "File src/synui-lock-auth.c does not exist".
    # build-all.sh's collector was already fixed this way; this copy drifted.
    tar czf "${tarball}" \
        --transform "s|^${pkg}/|${pkg}-${SYNAPSEOS_VERSION}/|" \
        --exclude="${pkg}/src/${pkg}-${SYNAPSEOS_VERSION}" \
        --exclude="${pkg}/pkg" \
        --exclude="${pkg}/*.pkg.tar*" \
        --exclude="${pkg}/*.tar.gz" \
        --exclude="${pkg}/*.ko" \
        --exclude="${pkg}/*.o" \
        --exclude="${pkg}/*.mod*" \
        --exclude="${pkg}/modules.order" \
        --exclude="${pkg}/Module.symvers" \
        "${items[@]}" 2>/dev/null

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
    local tmpbuild
    tmpbuild="$(mktemp -d "/var/tmp/synapse-pkg-${pkg}.XXXXXX")"
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
    if [[ -d "${staging}" ]]; then
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
    sudo -u synbuild -H env "SYNAPSE_LLAMA_BACKEND=${llama_backend}" \
        makepkg -fd --noconfirm \
        2>&1 | sed 's/^/  /' \
        || err "${pkg} build failed — aborting (packages.x86_64 needs every package)"

    cp "${tmpbuild}"/*.pkg.tar.zst "${LOCAL_REPO}/"

    ok "${pkg} built"
    cd "${SCRIPT_DIR}"
    rm -rf "${tmpbuild}"
}

# Start from a clean repo so stale packages from a previous run can't
# mask a failed build — pacstrap must see exactly what this run produced.
rm -f "${LOCAL_REPO}"/*.pkg.tar.zst

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

# ── Download AI model ─────────────────────────────────────────
if [[ "$WITH_MODEL" == "true" ]]; then
    step "Downloading AI model"

    MODEL_PATH="${MODEL_DIR}/${MODEL_NAME}"

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

# ── Sign ──────────────────────────────────────────────────────
if [[ "$SIGN" == "true" ]]; then
    step "Signing ISO"
    gpg --detach-sign --armor "${ISO_FILE}"
    ok "Signed: ${ISO_FILE}.asc"
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
echo "  │  SynapseOS ${SYNAPSEOS_VERSION} — Build complete        │"
echo "  ╰───────────────────────────────────────────╯"
echo -e "${C_RESET}"
echo -e "  ISO:    ${C_BOLD}${ISO_FILE}${C_RESET}"
echo -e "  Size:   ${C_DIM}$(du -h "${ISO_FILE}" | cut -f1)${C_RESET}"
echo
echo -e "  ${C_BRAND}To write to USB:${C_RESET}"
echo -e "  ${C_DIM}dd if=${ISO_FILE} of=/dev/sdX bs=4M status=progress${C_RESET}"
echo
echo -e "  ${C_BRAND}To test in QEMU:${C_RESET}"
echo -e "  ${C_DIM}./build_scripts/qemu-test.sh ${ISO_FILE}${C_RESET}"
echo
