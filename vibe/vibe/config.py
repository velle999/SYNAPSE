import os
from pathlib import Path

# Backend: "llama_cpp", "ollama", or "synapd".
# Env overrides let a packaged launcher pick the backend without patching this
# file (SynapseOS ships /usr/bin/vibe exporting VIBE_BACKEND=synapd); the plain
# upstream default stays neutral.
BACKEND = os.environ.get("VIBE_BACKEND") or "ollama"   # empty env → default

# synapd backend — SynapseOS's kernel-native AI daemon. Reuses the model
# already resident on the GPU (no second model, no extra VRAM). Local box uses
# the unix socket; leave SYNAPD_HOST empty for that. Set SYNAPD_HOST to reach
# the TCP bridge (synapd-bridge.socket) from another machine.
SYNAPD_SOCKET = os.environ.get("VIBE_SYNAPD_SOCKET", "/run/synapd/synapd.sock")
SYNAPD_HOST = os.environ.get("VIBE_SYNAPD_HOST", "")   # empty = local unix socket
SYNAPD_PORT = int(os.environ.get("VIBE_SYNAPD_PORT", "11435"))
SYNAPD_CTX = 8192           # synapd's context window (match synapd.conf)
SYNAPD_TIMEOUT = 600        # seconds — single-shot codegen can take a while
# synapd is single-shot with no streaming; MAX_TOKENS is clamped to the wire's
# 15-bit budget field (32767) and again to synapd's context window.

# Paths (llama_cpp backend)
ROOT_DIR = Path(__file__).parent.parent
MODEL_PATH = ROOT_DIR / "models" / "Qwen3-8B-Q8_0.gguf"

# Ollama backend
OLLAMA_HOST = "http://localhost:11434"
OLLAMA_MODEL = "qwen3:14b"
OLLAMA_CTX = 32768   # ollama defaults to 2048 — must set explicitly
OLLAMA_TIMEOUT = 600  # seconds — large codegen can take a while
OLLAMA_NUM_GPU = -1     # GPU layers for ollama (-1 = all, 0 = CPU only)

# Model settings
N_GPU_LAYERS = -1       # offload all layers to GPU (-1 = all, 0 = CPU only)
N_CTX = 32768           # context window
N_THREADS = 8
FLASH_ATTN = True
# Q8_0 KV cache: halves KV VRAM vs F16 default (~2.4GB vs ~4.8GB at 32k)
# Lets 32k context fit on 12GB alongside the 8B Q8_0 weights (~8.5GB)
KV_CACHE_TYPE = 8       # 8 = Q8_0

# Generation settings
TEMPERATURE = 0.6
TOP_P = 0.95
TOP_K = 20
MIN_P = 0.0
REPEAT_PENALTY = 1.1
MAX_TOKENS = 16384

# Qwen3 thinking mode: True = show CoT reasoning, False = /no_think
THINKING = False  # use /think to enable — thinking eats tokens before tool calls
