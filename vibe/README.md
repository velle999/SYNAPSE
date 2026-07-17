# Vibe Code

A local AI coding assistant. Runs entirely on your machine — no API keys, no cloud.

Supports two backends: **ollama** (recommended, easy model management) and **llama-cpp** (direct GGUF, full CUDA control).

## Features

- Agentic tool-call loop: reads files, writes files, edits files, runs bash commands, globs, greps
- System control: GPU stats, process management, systemd services, network info
- Qwen3/Qwen3.5 thinking mode (chain-of-thought reasoning, toggleable at runtime)
- Streaming output with Rich UI
- Session memory via `/save` → `.vibe/memory.md`
- Context usage tracking with visual bar
- Opens GUI apps and file managers directly

## Requirements

- Arch Linux (setup script uses pacman)
- NVIDIA GPU with CUDA support
- Python 3.12+
- [ollama](https://ollama.com) (recommended) or llama-cpp-python with CUDA

## Setup

### Ollama (recommended)

```bash
./setup.sh            # installs ollama, pulls qwen3:14b, creates venv
./vibe.sh
```

Other models you can pull:
```bash
ollama pull qwen3.5:9b         # smaller, faster
ollama pull qwen3:30b-a3b      # larger MoE, needs /offload for <16GB VRAM
ollama pull qwen2.5-coder:14b  # code-specialized
```

Switch models in `vibe/config.py`:
```python
OLLAMA_MODEL = "qwen3:14b"     # change to any pulled model
```

### llama-cpp (direct GGUF)

```bash
./setup.sh llama_cpp  # installs CUDA, builds llama-cpp-python, downloads GGUF
./vibe.sh
```

Set in `vibe/config.py`:
```python
BACKEND = "llama_cpp"
MODEL_PATH = ROOT_DIR / "models" / "Qwen3-8B-Q8_0.gguf"
```

### GPU offload

If a model doesn't fit entirely in VRAM, use `/offload` at runtime to split layers between GPU and CPU/RAM:

```
/offload 35    # 35 layers on GPU, rest on CPU/RAM
/offload 0     # CPU only (no VRAM used)
/offload -1    # all layers on GPU (default)
```

## Usage

```bash
# Launch
./vibe.sh

# Point at a project directory
./vibe.sh ~/my-project

# Verbose mode (shows tracebacks)
./vibe.sh --verbose
```

## Slash Commands

**Conversation**

| Command    | Description                                       |
|------------|---------------------------------------------------|
| `/reset`   | Clear conversation history                        |
| `/think`   | Enable chain-of-thought reasoning                 |
| `/nothink` | Disable chain-of-thought (faster)                 |
| `/tokens`  | Show context usage with a visual bar              |
| `/model`   | Show current backend and model info               |
| `/save`    | Summarize session to `.vibe/memory.md`            |
| `/memory`  | Print current `.vibe/memory.md`                   |
| `/exit`    | Quit                                              |
| `/help`    | Show all commands                                 |

**System Info**

| Command           | Description                                     |
|-------------------|-------------------------------------------------|
| `/sys`            | CPU, RAM, disk usage, uptime                    |
| `/gpu`            | GPU utilization, VRAM usage, temperature        |
| `/net`            | Network interfaces and listening ports          |
| `/ps [filter]`    | Top processes by CPU (optional name filter)     |
| `/files [path]`   | Open file manager (default: cwd)                |

**Process & Service Control**

| Command                        | Description                                    |
|--------------------------------|------------------------------------------------|
| `/kill <pid\|name>`            | Send SIGTERM to a PID or matching processes    |
| `/service <name> [action]`     | systemctl control (default: status). Actions: `start` `stop` `restart` `reload` `enable` `disable` |
| `/services [filter]`           | List running services                          |

**Runtime Config**

| Command                    | Description                              |
|----------------------------|------------------------------------------|
| `/offload <n>`             | GPU layers (-1=all, 0=CPU only, N=partial) |
| `/set temp <0.0-2.0>`      | Generation temperature                   |
| `/set tokens <n>`          | Max output tokens                        |
| `/set top_p <0.0-1.0>`     | Nucleus sampling probability             |
| `/set top_k <n>`           | Top-k sampling                           |
| `/set repeat_penalty <n>`  | Repetition penalty                       |

## Configuration

`vibe/config.py`:

```python
# Backend
BACKEND      = "ollama"      # "ollama" or "llama_cpp"

# Ollama
OLLAMA_HOST  = "http://localhost:11434"
OLLAMA_MODEL = "qwen3:14b"
OLLAMA_NUM_GPU = -1          # GPU layers (-1 = all, 0 = CPU only)

# llama-cpp
MODEL_PATH   = ROOT_DIR / "models" / "Qwen3-8B-Q8_0.gguf"
N_CTX        = 32768         # context window (tokens)
N_GPU_LAYERS = -1            # GPU layers (-1 = all, 0 = CPU only)

# Generation (both backends)
TEMPERATURE  = 0.6
MAX_TOKENS   = 16384
THINKING     = False         # /think to enable
```

## Project Structure

```
vibe-code/
├── main.py          # REPL entry point, slash command handling
├── vibe/
│   ├── config.py    # Backend selection, model paths, generation params
│   ├── llm.py       # VibeModel — agentic loop, ollama + llama-cpp backends
│   ├── tools.py     # Tool schemas + implementations (read, write, edit, bash, glob, grep, ls)
│   ├── system.py    # System commands (gpu, ps, services, file manager, etc.)
│   └── ui.py        # Rich console UI, prompt_toolkit session, streaming renderer
├── setup.sh         # llama-cpp setup script
├── vibe.sh          # Launch wrapper
└── requirements.txt
```
