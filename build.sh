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
LOCAL_REPO="${BUILD_DIR}/repo"
LLAMA_DIR="${BUILD_DIR}/llama.cpp"
MODEL_DIR="${SCRIPT_DIR}/airootfs/var/lib/synapd/models"

# Model to embed
MODEL_NAME="synapse-7b-q4_k_m.gguf"
# Use Mistral-7B-Instruct as the base (fine-tuned for SynapseOS in production)
MODEL_HF_REPO="TheBloke/Mistral-7B-Instruct-v0.2-GGUF"
MODEL_HF_FILE="mistral-7b-instruct-v0.2.Q4_K_M.gguf"

JOBS="$(nproc)"
WITH_MODEL=true
WITH_GPU=auto
CLEAN=true
SIGN=false

# ── Colors ────────────────────────────────────────────────────
C_BRAND='\033[38;5;51m'   # electric cyan
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

# Detect distro — only Arch and Arch-based are supported
if ! command -v pacman &>/dev/null; then
    err "pacman not found. SynapseOS must be built on Arch Linux or an Arch-based distro."
fi

# ── Auto-install missing build dependencies ───────────────────
#
# Map each required command → the pacman package that provides it.
# If a tool is missing, we install it rather than aborting.
#
declare -A TOOL_PKG=(
    [mkarchiso]="archiso"
    [makepkg]="pacman"          # already implied by pacman existing
    [git]="git"
    [cmake]="cmake"
    [meson]="meson"
    [ninja]="ninja"
    [gcc]="gcc"
    [curl]="curl"
    [rsync]="rsync"
    [dialog]="dialog"
    [parted]="parted"
    [repo-add]="pacman"         # part of pacman itself
)

MISSING_PKGS=()
for cmd in "${!TOOL_PKG[@]}"; do
    if command -v "$cmd" &>/dev/null; then
        ok "$cmd"
    else
        pkg="${TOOL_PKG[$cmd]}"
        warn "$cmd not found — will install package: $pkg"
        MISSING_PKGS+=("$pkg")
    fi
done

# Deduplicate
MISSING_PKGS=($(printf '%s\n' "${MISSING_PKGS[@]}" | sort -u))

if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
    log "Installing missing dependencies: ${MISSING_PKGS[*]}"
    pacman -Sy --noconfirm --needed "${MISSING_PKGS[@]}" \
        2>&1 | sed 's/^/  /' \
        || err "pacman failed to install dependencies. Check your internet connection and /etc/pacman.conf."

    # Verify everything is now present
    STILL_MISSING=()
    for cmd in "${!TOOL_PKG[@]}"; do
        command -v "$cmd" &>/dev/null || STILL_MISSING+=("$cmd")
    done
    if [[ ${#STILL_MISSING[@]} -gt 0 ]]; then
        err "Still missing after install: ${STILL_MISSING[*]}"
    fi
    ok "All dependencies installed"
else
    ok "All build dependencies present"
fi

# base-devel group — needed for makepkg/PKGBUILDs
if ! pacman -Qq base-devel &>/dev/null 2>&1; then
    log "Installing base-devel group..."
    pacman -Sy --noconfirm --needed base-devel 2>&1 | sed 's/^/  /'
    ok "base-devel installed"
fi

# Disk space check (~20GB required with model, ~8GB without)
REQUIRED_GB=$([[ "$WITH_MODEL" == "true" ]] && echo 22 || echo 9)
AVAIL_GB=$(df -BG "${SCRIPT_DIR}" | awk 'NR==2{print $4}' | tr -d G)
if [[ "$AVAIL_GB" -lt "$REQUIRED_GB" ]]; then
    err "Insufficient disk space: need ${REQUIRED_GB}GB, have ${AVAIL_GB}GB"
fi
ok "Disk space: ${AVAIL_GB}GB available"

# GPU detection for llama.cpp
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

log "Installing llama.cpp to staging area..."
DESTDIR="${BUILD_DIR}/llama-staging" make install

ok "llama.cpp built successfully"
cd "${SCRIPT_DIR}"

# ── Build SynapseOS packages ──────────────────────────────────
step "Building SynapseOS packages"

PACKAGES=(
    "synapd"
    "synsh"
    "synguard"
    "synapse_kmod"
    "synui"
)

build_package() {
    local pkg="$1"
    local pkgdir="${PROJECT_ROOT}/${pkg}"

    if [[ ! -f "${pkgdir}/PKGBUILD" ]]; then
        warn "No PKGBUILD for ${pkg} — skipping (add to ${pkgdir}/PKGBUILD)"
        return 0
    fi

    log "Building ${pkg}..."
    cd "${pkgdir}"

    # Build as non-root (makepkg requirement)
    # We use a build user if running as root
    if [[ "$(id -u)" -eq 0 ]]; then
        # Create build user if needed
        id -u synbuild &>/dev/null || useradd -r -s /bin/bash -m synbuild
        chown -R synbuild: "${pkgdir}"
        sudo -u synbuild makepkg -sf --noconfirm \
            PKGDEST="${LOCAL_REPO}" 2>&1 | sed 's/^/  /'
    else
        makepkg -sf --noconfirm PKGDEST="${LOCAL_REPO}"
    fi

    ok "${pkg} built"
    cd "${SCRIPT_DIR}"
}

for pkg in "${PACKAGES[@]}"; do
    build_package "$pkg"
done

# Add llama.cpp staging to a package
log "Packaging llama.cpp staging..."
rsync -a "${BUILD_DIR}/llama-staging/" "${SCRIPT_DIR}/airootfs/"
ok "llama.cpp staged into airootfs"

# Initialize the local repo database
log "Initializing local pacman repo..."
if ls "${LOCAL_REPO}"/*.pkg.tar.zst &>/dev/null; then
    repo-add "${LOCAL_REPO}/synapseos.db.tar.gz" "${LOCAL_REPO}"/*.pkg.tar.zst
    ok "Local repo initialized with $(ls "${LOCAL_REPO}"/*.pkg.tar.zst | wc -l) packages"
else
    warn "No local packages found — ISO will not include SynapseOS binaries"
fi

# ── Download AI model ─────────────────────────────────────────
if [[ "$WITH_MODEL" == "true" ]]; then
    step "Downloading AI model"

    MODEL_PATH="${MODEL_DIR}/${MODEL_NAME}"

    if [[ -f "${MODEL_PATH}" ]]; then
        ok "Model already present: $(du -h "${MODEL_PATH}" | cut -f1)"
    else
        log "Downloading ${MODEL_HF_FILE} from HuggingFace..."
        log "This is ~4.1GB — grab a coffee ☕"

        HF_URL="https://huggingface.co/${MODEL_HF_REPO}/resolve/main/${MODEL_HF_FILE}"

        curl -L \
            --progress-bar \
            --retry 5 \
            --retry-delay 10 \
            -o "${MODEL_PATH}.tmp" \
            "${HF_URL}"

        mv "${MODEL_PATH}.tmp" "${MODEL_PATH}"

        # Rename to our canonical name
        ok "Model downloaded: $(du -h "${MODEL_PATH}" | cut -f1)"
    fi

    # Write a model manifest
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

# For SynapseOS production: replace with a fine-tuned model
# trained on OS-specific tasks (syscall analysis, shell translation,
# scheduling decisions). See docs/model-finetuning.md
EOF
fi

# ── Configure airootfs ────────────────────────────────────────
step "Configuring airootfs"

# Add local repo to pacman.conf
PACMAN_CONF="${SCRIPT_DIR}/airootfs/etc/pacman.conf"
if [[ ! -f "${PACMAN_CONF}" ]]; then
    cp /etc/pacman.conf "${PACMAN_CONF}"
fi

# Inject local SynapseOS repo at the top
if ! grep -q "\[synapseos\]" "${PACMAN_CONF}"; then
    sed -i '1a [synapseos]\nSigLevel = Optional TrustAll\nServer = file://'"${LOCAL_REPO}" \
        "${PACMAN_CONF}"
    ok "Local repo added to pacman.conf"
fi

# ── Run mkarchiso ─────────────────────────────────────────────
step "Building ISO (mkarchiso)"

ISO_LABEL="SYNAPSEOS_$(date +%Y%m)"
log "Building ISO: label=${ISO_LABEL}"
log "This step takes 10-30 minutes depending on download speed..."

mkarchiso \
    -v \
    -w "${WORK_DIR}" \
    -o "${OUT_DIR}" \
    "${SCRIPT_DIR}"

ISO_FILE=$(ls -t "${OUT_DIR}"/*.iso 2>/dev/null | head -1)

if [[ -z "$ISO_FILE" ]]; then
    err "ISO not found in ${OUT_DIR} — mkarchiso may have failed"
fi

ok "ISO built: ${ISO_FILE}"

# ── Sign ──────────────────────────────────────────────────────
if [[ "$SIGN" == "true" ]]; then
    step "Signing ISO"
    gpg --detach-sign --armor "${ISO_FILE}"
    ok "Signed: ${ISO_FILE}.asc"
fi

# ── Checksum ──────────────────────────────────────────────────
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
echo -e "  ${C_DIM}./scripts/qemu-test.sh ${ISO_FILE}${C_RESET}"
echo
