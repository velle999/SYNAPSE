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
#   --no-gpu        Build llama.cpp CPU-only
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
# LOCAL_REPO lives inside archiso/ so mkarchiso includes it in the ISO image.
# On the live system it will be accessible at /run/archiso/bootmnt/arch/pkgs
# via the loop-mount — we reference it as file:///run/archiso/airootfs/local-repo
# in the live pacman.conf.
LOCAL_REPO="${SCRIPT_DIR}/local-repo"
LLAMA_DIR="${BUILD_DIR}/llama.cpp"
MODEL_DIR="${SCRIPT_DIR}/airootfs/var/lib/synapd/models"

# Model to embed
MODEL_NAME="synapse-7b-q4_k_m.gguf"
MODEL_HF_REPO="TheBloke/Mistral-7B-Instruct-v0.2-GGUF"
MODEL_HF_FILE="mistral-7b-instruct-v0.2.Q4_K_M.gguf"

JOBS="$(nproc)"
WITH_MODEL=true
WITH_GPU=auto
CLEAN=true
SIGN=false

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

# ── Clean ─────────────────────────────────────────────────────
if [[ "$CLEAN" == "true" ]]; then
    step "Cleaning previous build"
    rm -rf "${WORK_DIR}" "${BUILD_DIR}"
    log "Cleaned work/ and build/"
fi

mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${LOCAL_REPO}" "${MODEL_DIR}"

# ── Build llama.cpp ───────────────────────────────────────────
step "Building llama.cpp"

if [[ ! -d "${LLAMA_DIR}" ]]; then
    log "Cloning llama.cpp..."
    git clone --depth=1 https://github.com/ggerganov/llama.cpp "${LLAMA_DIR}"
fi

cd "${LLAMA_DIR}"
git pull --ff-only 2>/dev/null || log "(using cached llama.cpp)"

mkdir -p build && cd build

CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INSTALL_PREFIX=/usr"
    "-DLLAMA_BUILD_TESTS=OFF"
    "-DLLAMA_BUILD_EXAMPLES=ON"
    "-DLLAMA_SERVER=ON"
)

case "$WITH_GPU" in
    cuda)  CMAKE_ARGS+=("-DGGML_CUDA=ON") ;;
    rocm)  CMAKE_ARGS+=("-DGGML_HIPBLAS=ON" "-DAMDGPU_TARGETS=gfx1030;gfx1100") ;;
    cpu)   CMAKE_ARGS+=("-DGGML_NATIVE=ON") ;;
esac

log "CMake configure (GPU: ${WITH_GPU})..."
cmake .. "${CMAKE_ARGS[@]}"

log "Building llama.cpp (${JOBS} jobs)..."
make -j"${JOBS}"

# Stage into llama-staging/ at project root (referenced by synapd PKGBUILD)
LLAMA_STAGING="${PROJECT_ROOT}/llama-staging"
log "Installing llama.cpp to staging area..."
DESTDIR="${LLAMA_STAGING}" make install
ok "llama.cpp built and staged to ${LLAMA_STAGING}"

cd "${SCRIPT_DIR}"

# ── Build SynapseOS packages ──────────────────────────────────
step "Building SynapseOS packages"

# All packages with a PKGBUILD in the project root
PACKAGES=(
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
)

# Create build user for makepkg (can't run as root)
id -u synbuild &>/dev/null || useradd -r -s /bin/bash -m synbuild

build_package() {
    local pkg="$1"
    local pkgdir="${PROJECT_ROOT}/${pkg}"

    if [[ ! -f "${pkgdir}/PKGBUILD" ]]; then
        warn "No PKGBUILD for ${pkg} — skipping"
        return 0
    fi

    log "Building ${pkg}..."
    cd "${pkgdir}"
    chown -R synbuild: "${pkgdir}"

    sudo -u synbuild makepkg -sf --noconfirm \
        PKGDEST="${LOCAL_REPO}" \
        2>&1 | sed 's/^/  /' \
        || { warn "${pkg} build failed — skipping"; cd "${SCRIPT_DIR}"; return 0; }

    ok "${pkg} built"
    cd "${SCRIPT_DIR}"
}

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
cat > "${SCRIPT_DIR}/pacman.conf" << 'PACMANEOF'
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

# SynapseOS local packages — on the live ISO
[synapseos]
SigLevel = Optional TrustAll
Server = file:///run/archiso/airootfs/local-repo
PACMANEOF
ok "pacman.conf written (live-ISO paths)"

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
