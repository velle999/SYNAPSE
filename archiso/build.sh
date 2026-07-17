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
#   --gpu=TYPE      Build llama.cpp with GPU backend: cuda, rocm, or auto.
#                   Default is CPU-only: a GPU build links the shipped
#                   libggml against the BUILD HOST's driver stack
#                   (libcuda.so.1), and synapd dies with exit 127 on any
#                   machine without that driver — including every VM.
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
# SynapseOS Project — GPLv2
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
    elif lspci 2>/dev/null | grep -qi "amd\|radeon"; then
        WITH_GPU="rocm"
        log "Detected AMD GPU — building llama.cpp with ROCm"
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
    *) err "Invalid --gpu value: $WITH_GPU (expected cuda, rocm, or auto)" ;;
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
step "Building llama.cpp"

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
# gate it off on CUDA 13+ (older toolkits keep cub). Idempotent, cuda-only.
if [[ "$WITH_GPU" == "cuda" ]]; then
    _cub_guard='ggml/src/ggml-cuda/common.cuh'
    if grep -q 'CUDART_VERSION >= 11070$' "$_cub_guard" 2>/dev/null; then
        log "Patching ggml to disable cub on CUDA 13+ (CCCL 3.4 API break)"
        sed -i 's|\(CUDART_VERSION >= 11070\)$|\1 \&\& CUDART_VERSION < 13000|' "$_cub_guard"
    fi
fi

# Belt and braces to the explicit -D flags below: if the reused build/ dir was
# configured for a DIFFERENT backend, wipe it. The -D flags alone would fix the
# configuration, but stale artifacts (a leftover libggml-cuda.so) would still be
# sitting there for `make install` to pick up.
if [[ -f build/CMakeCache.txt ]]; then
    _want_cuda=OFF; [[ "$WITH_GPU" == cuda ]] && _want_cuda=ON
    _have_cuda=$(sed -n 's/^GGML_CUDA:BOOL=//p' build/CMakeCache.txt | head -1)
    if [[ -n "$_have_cuda" && "$_have_cuda" != "$_want_cuda" ]]; then
        log "llama.cpp build/ was configured GGML_CUDA=${_have_cuda}, want ${_want_cuda} — wiping it"
        rm -rf build
    fi
fi

mkdir -p build && cd build

CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INSTALL_PREFIX=/usr"
    "-DLLAMA_BUILD_TESTS=OFF"
    "-DLLAMA_BUILD_EXAMPLES=ON"
    "-DLLAMA_SERVER=ON"
)

# EVERY backend toggle is stated explicitly on EVERY path, never left to
# default. `build/` is reused across runs (--no-clean), so CMakeCache.txt
# survives — and a cached `GGML_CUDA:BOOL=ON` from an earlier cuda build is
# STICKY: an option() not passed with -D keeps its cached value. A `cpu` build
# would then silently reconfigure with the CUDA backend still on, log
# "CMake configure (GPU: cpu)" while printing "Including CUDA backend", and
# produce an ISO whose synapd links libcuda.so.1 — so it fails to start on any
# machine without an NVIDIA driver. Which is most of them.
case "$WITH_GPU" in
    cuda)
        CMAKE_ARGS+=("-DGGML_CUDA=ON")
        # Ensure CMake can find CUDA even under sudo (which strips env vars)
        if [[ -d /opt/cuda ]]; then
            CMAKE_ARGS+=("-DCUDAToolkit_ROOT=/opt/cuda" "-DCMAKE_CUDA_COMPILER=/opt/cuda/bin/nvcc")
        fi
        ;;
    rocm)  CMAKE_ARGS+=("-DGGML_CUDA=OFF" "-DGGML_HIPBLAS=ON" "-DAMDGPU_TARGETS=gfx1030;gfx1100") ;;
    # GGML_NATIVE=OFF: NATIVE bakes the BUILD HOST's instruction set
    # (AVX2/AVX-512) into libggml, and synapd dies with SIGILL on any
    # CPU without those extensions — VMs without -cpu host included.
    # The ISO must run on baseline x86-64.
    cpu)   CMAKE_ARGS+=("-DGGML_CUDA=OFF" "-DGGML_HIPBLAS=OFF" "-DGGML_NATIVE=OFF") ;;
esac

log "CMake configure (GPU: ${WITH_GPU})..."
cmake .. "${CMAKE_ARGS[@]}"

log "Building llama.cpp (${JOBS} jobs)..."
make -j"${JOBS}"

log "Installing llama.cpp to staging area..."
# Wipe first: layering installs from different llama.cpp builds leaves
# mismatched libllama/libggml sonames that break linking against staging.
# Scoped to this backend's dir — never touches the other backends' builds.
rm -rf "${LLAMA_STAGING}"
DESTDIR="${LLAMA_STAGING}" make install

# Before the split, llama-staging was a real directory. ln can't overwrite one,
# so migrate it out of the way rather than die here — and never delete it, it may
# be the only copy of a build something still links against.
if [[ -d "${LLAMA_STAGING_LINK}" && ! -L "${LLAMA_STAGING_LINK}" ]]; then
    _orphan="${LLAMA_STAGING_LINK}.pre-split-$(date +%Y%m%d%H%M%S)"
    warn "llama-staging is a real directory (pre-split layout) — moving it to $(basename "${_orphan}")"
    mv "${LLAMA_STAGING_LINK}" "${_orphan}"
fi

# Repoint llama-staging at what we just built. If that steals the symlink from
# a GPU build, say so loudly: synapd resolves libllama through this path at
# runtime, so the machine silently drops to CPU inference until it is restored.
_prev="$(readlink "${LLAMA_STAGING_LINK}" 2>/dev/null || true)"
if [[ -n "$_prev" && "$_prev" != "$(basename "${LLAMA_STAGING}")" && "$_prev" != "llama-staging-cpu" ]]; then
    warn "llama-staging was pointing at ${_prev} — repointing it to the ${WITH_GPU} build."
    warn "Anything linking against llama-staging (synapd) now runs on ${WITH_GPU}."
    warn "Restore with: ln -sfn ${_prev} ${LLAMA_STAGING_LINK}"
fi
# -n: don't descend into the existing symlink and nest the link inside its target
ln -sfn "$(basename "${LLAMA_STAGING}")" "${LLAMA_STAGING_LINK}"
ok "llama.cpp built and staged to ${LLAMA_STAGING}"

if [[ "$LLAMA_ONLY" == "true" ]]; then
    if [[ "$WITH_GPU" == "cuda" && ! -e "${LLAMA_STAGING}/usr/lib/libggml-cuda.so" ]]; then
        err "CUDA was requested but libggml-cuda.so is not in the staging lib dir — this is a CPU build."
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
    synui
    synapse_kmod
    syn
    syn-firstboot
    syn-model
    syn-install
    nexus-chat
    tepris
    chibi
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

    tar czf "${tarball}" \
        --transform "s|^${pkg}/|${pkg}-${SYNAPSEOS_VERSION}/|" \
        --exclude="${pkg}/src/${pkg}-*" \
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
    local pkgdir="${PROJECT_ROOT}/${pkg}"

    if [[ ! -f "${pkgdir}/PKGBUILD" ]]; then
        warn "No PKGBUILD for ${pkg} — skipping"
        return 0
    fi

    log "Building ${pkg}..."

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
    if [[ -d "${LLAMA_STAGING}" ]]; then
        local _sname
        _sname="$(basename "${LLAMA_STAGING}")"
        rm -rf "${tmpbuild}/llama-staging" "${tmpbuild}/${_sname}"
        cp -a "${LLAMA_STAGING}" "${tmpbuild}/${_sname}"
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
    sudo -u synbuild -H env "SYNAPSE_LLAMA_BACKEND=${WITH_GPU}" \
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
