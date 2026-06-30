#!/usr/bin/env bash
# syn model — SynapseOS model manager
set -euo pipefail

VERSION="0.1.0-synapse"
MODEL_DIR="/var/lib/synapd/models"
MODEL_PATH="$MODEL_DIR/synapse.gguf"
SYNAPD_SERVICE="synapd"

# Download robustness knobs
RETRIES=3                              # download attempts before giving up
MIN_MODEL_BYTES=$((10 * 1024 * 1024))  # 10MB floor — anything smaller is an
                                       # error page, not a model
ASSUME_YES=0                           # -y/--yes: never prompt (first-boot)

# Default model — Mistral 7B Q4_K_M (~4.1GB)
DEFAULT_MODEL_URL="https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF/resolve/main/mistral-7b-instruct-v0.2.Q4_K_M.gguf"

# Smaller option — Phi-3 Mini Q4 (~2.2GB)
PHI3_URL="https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-gguf/resolve/main/Phi-3-mini-4k-instruct-q4.gguf"

# Tiny option — Qwen2 0.5B (~400MB, fast on CPU)
TINY_URL="https://huggingface.co/Qwen/Qwen2-0.5B-Instruct-GGUF/resolve/main/qwen2-0_5b-instruct-q4_k_m.gguf"

usage() {
    cat << HELP
syn-model $VERSION — SynapseOS Model Manager

Usage:
  syn-model download [MODEL] [-y]   Download a model (default: mistral-7b)
  syn-model list                    List available models
  syn-model status                  Show current model status
  syn-model remove [-y]             Remove installed model
  syn-model help                    This help

Options:
  -y, --yes    Assume yes — never prompt (used by first-boot)

Models:
  mistral-7b   Mistral 7B Instruct Q4_K_M (~4.1GB) — recommended
  phi3         Phi-3 Mini 4K Instruct Q4 (~2.2GB) — good balance
  tiny         Qwen2 0.5B Q4 (~400MB) — fast, lower quality

HELP
}

status() {
    echo "SynapseOS Model Status"
    echo "────────────────────────────────"
    if [ -f "$MODEL_PATH" ]; then
        SIZE=$(du -sh "$MODEL_PATH" | cut -f1)
        echo "  Model:    $MODEL_PATH"
        echo "  Size:     $SIZE"
        echo "  Status:   ✓ installed"
    else
        echo "  Model:    not installed"
        echo "  Status:   ✗ AI features disabled"
        echo ""
        echo "  Run: syn-model download"
    fi
    echo ""
    echo "  synapd:   $(systemctl is-active $SYNAPD_SERVICE 2>/dev/null || echo unknown)"
    journalctl -t synapd -n 1 --no-pager 2>/dev/null | grep -o "model=.*" || true
}

list_models() {
    cat << LIST
Available models:

  mistral-7b   Mistral 7B Instruct v0.2 Q4_K_M   ~4.1GB   ★★★★★ recommended
  phi3         Phi-3 Mini 4K Instruct Q4          ~2.2GB   ★★★★☆ good balance
  tiny         Qwen2 0.5B Instruct Q4_K_M         ~400MB   ★★★☆☆ fast/low RAM

LIST
}

# ── Download helpers ──────────────────────────────────────────
die() { echo "✗ $*" >&2; exit 1; }

human() { numfmt --to=iec --suffix=B "${1:-0}" 2>/dev/null || echo "${1:-0} bytes"; }

# Expected size in bytes from a HEAD request (follows redirects to the CDN).
# Echoes a number, or empty if the server doesn't report Content-Length.
remote_size() {
    local sz
    sz=$(curl -sIL "$1" 2>/dev/null \
         | awk 'BEGIN{IGNORECASE=1} /^content-length:/{v=$2} END{gsub(/\r/,"",v); print v}') || true
    [[ "$sz" =~ ^[0-9]+$ ]] && echo "$sz" || echo ""
}

# Free bytes on the filesystem holding $1.
free_bytes() { df -PB1 "$1" 2>/dev/null | awk 'NR==2{print $4}'; }

filesize() { stat -c%s "$1" 2>/dev/null || echo 0; }

# A real GGUF model begins with the ASCII magic "GGUF".
is_gguf() { [ "$(head -c4 "$1" 2>/dev/null)" = "GGUF" ]; }

# One download attempt; resumes a partial $2 if present. Returns curl/wget rc.
fetch_once() {
    local url="$1" dest="$2"
    if command -v curl &>/dev/null; then
        curl -fL -C - --progress-bar -o "$dest" "$url"
    elif command -v wget &>/dev/null; then
        wget -c --progress=bar:force -O "$dest" "$url"
    else
        die "curl or wget required"
    fi
}

# Classify the downloaded temp file:
#   0 = complete and valid     1 = incomplete (resume)     2 = corrupt (restart)
validate_download() {
    local dest="$1" expected="$2" actual
    [ -f "$dest" ] || return 1
    actual=$(filesize "$dest")

    if [ -n "$expected" ]; then
        [ "$actual" -lt "$expected" ] && return 1          # partial → resume
        [ "$actual" -gt "$expected" ] && return 2          # overshoot → corrupt
    else
        [ "$actual" -lt "$MIN_MODEL_BYTES" ] && return 2   # too small → error page
    fi
    is_gguf "$dest" || return 2                            # wrong content
    return 0
}

download() {
    local model="${1:-mistral-7b}"
    local url

    case "$model" in
        mistral-7b|mistral) url="$DEFAULT_MODEL_URL" ;;
        phi3|phi)           url="$PHI3_URL" ;;
        tiny|qwen)          url="$TINY_URL" ;;
        http*)              url="$model" ;;  # custom URL
        *)
            echo "Unknown model: $model"
            echo "Run: syn-model list"
            exit 1
            ;;
    esac

    echo "SynapseOS Model Downloader"
    echo "────────────────────────────────"
    echo "  Model:  $model"
    echo "  URL:    $url"
    echo "  Dest:   $MODEL_PATH"

    if [ -f "$MODEL_PATH" ]; then
        if [ "$ASSUME_YES" = 1 ]; then
            :
        elif [ -t 0 ]; then
            read -r -p "Model already installed. Replace? [y/N] " confirm
            [[ "$confirm" =~ ^[Yy]$ ]] || exit 0
        else
            echo "Model already installed; pass --yes to replace."
            exit 0
        fi
    fi

    mkdir -p "$MODEL_DIR"
    local tmp="$MODEL_PATH.tmp"

    # Pre-flight: make sure there's room for what's left to fetch (+5% slack).
    local expected free have needed
    expected=$(remote_size "$url")
    if [ -n "$expected" ]; then
        echo "  Size:   $(human "$expected")"
        have=$(filesize "$tmp")
        needed=$(( expected - have + expected / 20 ))
        (( needed < 0 )) && needed=0
        free=$(free_bytes "$MODEL_DIR")
        if [ -n "$free" ] && [ "$needed" -gt "$free" ]; then
            die "insufficient disk space in $MODEL_DIR: need ~$(human "$needed"), have $(human "$free")"
        fi
    fi
    echo ""

    # Fetch with resume + retries, validating after each attempt.
    local attempt rc v ok=0 before last_before=-1
    for (( attempt = 1; attempt <= RETRIES; attempt++ )); do
        # If the previous attempt didn't grow the file, the server likely
        # ignored our resume request — restart from scratch instead of looping
        # on a stuck partial.
        before=$(filesize "$tmp")
        if [ "$before" -gt 0 ] && [ "$before" = "$last_before" ]; then
            echo "  resume made no progress — restarting download from scratch" >&2
            rm -f "$tmp"; before=0
        fi
        last_before=$before

        echo "Downloading (attempt $attempt/$RETRIES)..."
        rc=0; fetch_once "$url" "$tmp" || rc=$?
        v=0; validate_download "$tmp" "$expected" || v=$?

        if [ "$v" = 0 ]; then ok=1; break; fi
        if [ "$v" = 2 ]; then
            echo "  ✗ downloaded data is not a valid model — discarding and retrying" >&2
            rm -f "$tmp"
        else
            echo "  download interrupted (rc=$rc) — will resume" >&2
        fi
        sleep 2
    done

    [ "$ok" = 1 ] || die "download failed after $RETRIES attempts (see messages above)"

    mv "$tmp" "$MODEL_PATH"
    echo ""
    echo "✓ Model downloaded and verified: $(human "$(filesize "$MODEL_PATH")")"
    echo ""

    if command -v systemctl &>/dev/null; then
        echo "Restarting synapd..."
        systemctl restart "$SYNAPD_SERVICE" 2>/dev/null \
            || echo "  (could not restart $SYNAPD_SERVICE — start it manually)"
        sleep 3
        systemctl status "$SYNAPD_SERVICE" --no-pager -l 2>/dev/null | tail -5 || true
        echo ""
    fi
    echo "✓ SynapseOS AI is ready. Run: synsh"
}

remove() {
    if [ ! -f "$MODEL_PATH" ]; then
        echo "No model installed at $MODEL_PATH"
        exit 0
    fi
    if [ "$ASSUME_YES" != 1 ]; then
        if [ -t 0 ]; then
            read -r -p "Remove $MODEL_PATH? [y/N] " confirm
            [[ "$confirm" =~ ^[Yy]$ ]] || exit 0
        else
            echo "Refusing to remove non-interactively without --yes"
            exit 1
        fi
    fi
    rm -f "$MODEL_PATH"
    command -v systemctl &>/dev/null && systemctl restart "$SYNAPD_SERVICE" 2>/dev/null || true
    echo "✓ Model removed"
}

# ── Argument parsing ──────────────────────────────────────────
# Collect -y/--yes from anywhere; keep remaining positional args.
ARGS=()
for arg in "$@"; do
    case "$arg" in
        -y|--yes) ASSUME_YES=1 ;;
        *)        ARGS+=("$arg") ;;
    esac
done
set -- "${ARGS[@]}"

case "${1:-help}" in
    download) download "${2:-mistral-7b}" ;;
    list)     list_models ;;
    status)   status ;;
    remove)   remove ;;
    help|-h|--help) usage ;;
    *) echo "Unknown command: $1"; usage; exit 1 ;;
esac
