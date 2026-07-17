#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Vibe Code Setup ==="

# ── Parse args ───────────────────────────────────────────────────────────────
BACKEND="${1:-ollama}"   # "ollama" (default) or "llama_cpp"

# ── 1. Ensure pip is available ───────────────────────────────────────────────
if ! python3 -m pip --version &>/dev/null; then
    echo "[*] Installing python-pip via pacman..."
    sudo pacman -S --noconfirm python-pip
fi

# ── 2. Create venv ──────────────────────────────────────────────────────────
if [ ! -d ".venv" ]; then
    echo "[*] Creating virtual environment..."
    python3 -m venv .venv
fi
source .venv/bin/activate

# ── 3. Install Python deps ──────────────────────────────────────────────────
echo "[*] Installing Python dependencies..."
pip install -q rich prompt_toolkit

# ── 4. Backend-specific setup ────────────────────────────────────────────────
if [ "$BACKEND" = "ollama" ]; then
    echo "[*] Setting up Ollama backend..."

    # Install ollama if missing
    if ! command -v ollama &>/dev/null; then
        echo "[*] Installing Ollama..."
        curl -fsSL https://ollama.com/install.sh | sh
    fi

    # Start ollama service if not running
    if ! ollama list &>/dev/null 2>&1; then
        echo "[*] Starting Ollama service..."
        sudo systemctl start ollama 2>/dev/null || ollama serve &>/dev/null &
        sleep 2
    fi

    # Pull default model
    echo "[*] Pulling qwen3:14b (~9.3GB)..."
    ollama pull qwen3:14b

    echo ""
    echo "Setup complete! (backend: ollama)"
    echo ""
    echo "Other recommended models:"
    echo "  ollama pull qwen3.5:9b         # smaller, faster"
    echo "  ollama pull qwen3:30b-a3b      # larger MoE, needs /offload"
    echo "  ollama pull qwen2.5-coder:14b  # code-specialized"
    echo ""
    echo "Run with:  ./vibe.sh"

elif [ "$BACKEND" = "llama_cpp" ]; then
    echo "[*] Setting up llama-cpp backend..."

    # Install CUDA toolkit if missing
    if ! command -v nvcc &>/dev/null; then
        echo "[*] Installing CUDA toolkit via pacman..."
        sudo pacman -S --noconfirm cuda
        export PATH="/opt/cuda/bin:$PATH"
        export CUDA_HOME=/opt/cuda
    fi

    # Ensure CUDA is on PATH even if already installed
    export PATH="${CUDA_HOME:-/opt/cuda}/bin:$PATH"
    export CUDAToolkit_ROOT="${CUDA_HOME:-/opt/cuda}"

    echo "[*] Installing llama-cpp-python with CUDA support..."
    CMAKE_ARGS="-DGGML_CUDA=on -DCUDAToolkit_ROOT=${CUDAToolkit_ROOT}" \
        pip install llama-cpp-python --upgrade --force-reinstall --no-cache-dir

    # Download model
    echo "[*] Downloading Qwen3-8B-Q8_0.gguf (~8.5GB)..."
    mkdir -p models
    MODEL_PATH="models/Qwen3-8B-Q8_0.gguf"
    if [ -f "$MODEL_PATH" ]; then
        echo "    Model already exists, skipping download."
    else
        curl -L --progress-bar \
            "https://huggingface.co/bartowski/Qwen3-8B-GGUF/resolve/main/Qwen3-8B-Q8_0.gguf" \
            -o "$MODEL_PATH"
    fi

    echo ""
    echo "Setup complete! (backend: llama_cpp)"
    echo "Set BACKEND = \"llama_cpp\" in vibe/config.py"
    echo "Run with:  ./vibe.sh"

else
    echo "Unknown backend: $BACKEND"
    echo "Usage: ./setup.sh [ollama|llama_cpp]"
    exit 1
fi
