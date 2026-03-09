<div align="center">

<br/>

```
  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·
      ●━━━━━━━━━━━━━━━━━━━━━●
      ┃   S Y N A P S E O S  ┃
      ●━━━━━━━━━━━━━━━━━━━━━●
  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·
```

**Where the kernel thinks.**

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-cyan.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Arch Linux](https://img.shields.io/badge/base-Arch%20Linux-1793D1?logo=arch-linux&logoColor=white)](https://archlinux.org)
[![Wayland](https://img.shields.io/badge/display-Wayland-orange?logo=wayland)](https://wayland.freedesktop.org)
[![Status: Pre-Alpha](https://img.shields.io/badge/status-pre--alpha-red)](https://github.com/synapseos/synapseos)

<br/>

*SynapseOS is a Linux distribution where a local 7B AI model is not an app — it is the kernel scheduler, the security monitor, the shell interpreter, and the window manager.*

<br/>

</div>

---

## What is this

Most "AI-integrated" operating systems bolt a chatbot onto a standard desktop. SynapseOS goes deeper. The AI runs as a privileged system daemon (`synapd`) that every layer of the OS talks to directly:

- The **kernel module** hooks syscalls and asks the AI whether a process looks malicious
- The **scheduler** asks the AI to assign priority based on what a process says it's doing
- The **shell** asks the AI to translate natural language into commands before running them
- The **compositor** asks the AI to arrange windows based on workspace intent
- The **security monitor** asks the AI to classify threats before deciding to kill a process

The model never leaves your machine. No cloud. No API calls. `synapse-7b-q4_k_m.gguf` runs locally via llama.cpp.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  User                                                   │
│    synsh  "find all logs from today with errors"        │
│    synui  Super+Space → "open my coding workspace"      │
├─────────────────────────────────────────────────────────┤
│  Middleware                                             │
│    synapd — persistent AI inference daemon              │
│             llama.cpp · synapse-7b-q4_k_m.gguf          │
│             Unix socket · 8-worker thread pool          │
├─────────────────────────────────────────────────────────┤
│  Security                                               │
│    synguard — syscall event stream → rule engine →      │
│               AI classifier → deny/alert/quarantine     │
├─────────────────────────────────────────────────────────┤
│  Kernel                                                 │
│    synapse_kmod — kprobes on execve/open/socket/ptrace  │
│                   /sys/kernel/synapse/ sysfs interface  │
│                   AI_CTX syscall family (Linux 7.0)     │
│                   per-PID scheduling hint application   │
├─────────────────────────────────────────────────────────┤
│  Hardware                                               │
│    CPU · NVIDIA (CUDA) · AMD (ROCm) · NPU               │
└─────────────────────────────────────────────────────────┘
```

---

## Components

| Component | What it is | Language |
|---|---|---|
| [`synapd`](synapd/) | AI inference daemon. Loads the GGUF model, serves queries from all other components via Unix socket. Kernel-aware: reads sysfs events, writes scheduling hints. | C |
| [`synsh`](synsh/) | Natural language shell. Classifies input as shell command, builtin, or AI query. `?find large files` → translates → confirms → runs. | C |
| [`synapse_kmod`](synapse_kmod/) | Kernel module. kprobes on 8 syscalls, ring buffer event log, sysfs interface, AI scheduling hint application, AI_CTX syscall handlers. | C (kernel) |
| [`synguard`](synguard/) | Security monitor. Rule engine + AI classifier + action engine. Four modes: `audit`, `enforce`, `learning`, `lockdown`. | C |
| [`synui`](synui/) | Wayland compositor. wlroots-based tiling WM with AI layout engine, neural overlay, command bar, security-colored window borders. | C |
| [`archiso/`](archiso/) | ISO build system. One script builds llama.cpp, packages all components, downloads the model, runs mkarchiso. | Bash |

---

## Demo

```
$ syn ask "what process is using the most memory right now"
⟳ thinking...
The process using the most memory is firefox (pid 3421) at 1.2GB RSS,
followed by code (pid 2891) at 847MB. Both are expected for your
current workspace.

$ syn do "compress all png files in this directory"
CMD: find . -maxdepth 1 -name "*.png" -exec optipng -o2 {} \;
WHY: optipng losslessly compresses PNGs in-place; -o2 is a good
     speed/ratio balance.
Run? [Y/n] y
...
```

```
$ # synguard catches a suspicious exec
[synguard] 🚨 ALERT [HIGH] pid=4821 (curl) evt=exec
           file=/tmp/.x/sh reason=exec from /tmp — AI: probable
           dropper execution pattern, confidence=0.91
```

```
$ # synui command bar (Super+Space)
> open my coding workspace
→ switches to workspace 3 "code"
→ AI suggests: editor left 60%, terminal right top 40%, terminal right bottom 40%
→ layout applied
```

---

## Key Design Decisions

**The AI runs in userspace, not the kernel.** `synapd` is a daemon, not an LSM hook. This makes it hot-reloadable, debuggable, and auditable. The tradeoff is that a root process can kill it — acceptable for the threat model (AI workstation/server, not adversarial kernel hardening).

**Graceful degradation everywhere.** If `synapd` dies, `synguard` falls back to rule-only enforcement. If `synapse_kmod` isn't loaded, `synguard` runs without kernel events. If `synapd` is offline, `synsh` runs as a normal shell. Nothing hard-depends on the AI being up.

**The model is embedded in the ISO.** `synapse-7b-q4_k_m.gguf` is downloaded during `build.sh` and baked into the squashfs. Boot to AI in under 60 seconds on modern hardware. If you skip `--no-model`, it downloads on first boot instead.

**AI_CTX is a real syscall family.** On the SynapseOS-patched Linux 7.0 kernel, processes can call `AI_CTX_SET(intent)`, `AI_CTX_GET()`, and `AI_CTX_QUERY(prompt)`. On stock kernels, `synapse_kmod` provides a kprobe shim that intercepts syscall numbers 451–453.

---

## Build

```bash
# Requires: Arch Linux (or Arch-based), root, ~22GB free
# All build deps (archiso, cmake, meson, etc.) are auto-installed

git clone https://github.com/synapseos/synapseos
cd synapseos/archiso
sudo ./build.sh
```

```bash
# Without the embedded model (~8GB, faster build)
sudo ./build.sh --no-model

# CPU-only llama.cpp (no GPU required)
sudo ./build.sh --no-gpu

# Both
sudo ./build.sh --no-model --no-gpu
```

Output: `out/SynapseOS-0.1.0-YYYYMMDD-x86_64.iso`

### Test in QEMU

```bash
# 8GB RAM recommended (leaves headroom for the 7B model)
QEMU_RAM=8G ./archiso/build_scripts/qemu-test.sh
```

### Write to USB

```bash
sudo dd if=out/SynapseOS-*.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

---

## Install

Boot the ISO (live user: `syn` / `synapse`), then:

```bash
sudo syninstall
```

Guided bash/dialog TUI. Covers locale, timezone, disk partitioning (auto Btrfs or ext4, or manual), user account, AI feature selection. Takes ~10 minutes.

---

## GPU Support

`build.sh` auto-detects and configures llama.cpp:

| Hardware | Backend | Notes |
|---|---|---|
| NVIDIA | CUDA | Detected via `lspci` |
| AMD | ROCm/HIP | Targets gfx1030, gfx1100 |
| Intel Arc | SYCL | Manual: `--gpu=sycl` |
| None | CPU AVX2 | Automatic fallback |

The 7B Q4_K_M model needs ~4.5GB VRAM for full GPU offload, or runs CPU-only at ~3 tokens/sec on a modern 8-core.

---

## Keybindings (synui)

| Binding | Action |
|---|---|
| `Super+Space` | AI command bar |
| `Super+A` | Toggle neural overlay |
| `Super+Enter` | Open terminal |
| `Super+Q` | Close focused window |
| `Super+L` | Cycle layout (tiling → monocle → AI → floating) |
| `Super+J/K` | Focus next/prev window |
| `Super+1..9` | Switch workspace |
| `Super+Shift+1..9` | Move window to workspace |
| `Super+Shift+Q` | Quit compositor |

---

## synsh Quick Reference

```bash
# Prefix syntax
?<query>     # force AI translation
!<command>   # force shell (skip AI)

# syn meta-command
syn ask <question>      # ask AI anything
syn do  <request>       # translate to command and run
syn status              # synapd connection status
syn history             # last 20 AI interactions

# Examples
? show disk usage by folder sorted by size
? what's listening on port 8080
? delete all node_modules directories under here
syn ask "explain what synapd is doing right now"
syn do "restart the web server"
```

---

## Security Modes (synguard)

```bash
# Default on install: audit (logs everything, never blocks)
# Switch to enforce once you've reviewed the baseline:

sudo systemctl edit synguard
# Add:
# ExecStart=
# ExecStart=/usr/bin/synguard --mode enforce --rules /etc/synguard/rules.d/
```

| Mode | Behavior |
|---|---|
| `audit` | Log all events, never block. Safe default. |
| `enforce` | SIGKILL on DENY verdicts. Requires baseline review. |
| `learning` | Build behavioral baseline, flag anomalies. |
| `lockdown` | Block everything not in explicit allowlist. |

Rules live in `/etc/synguard/rules.d/*.rules`. Hot-reload with `systemctl reload synguard` or `kill -HUP`.

---

## Status

This is pre-alpha research software. The kernel module requires Linux 6.8+ to compile. Full AI_CTX syscall support requires the SynapseOS kernel patch (not yet published separately). Everything degrades gracefully on stock kernels.

What works today:
- `synapd` — fully functional, tested
- `synsh` — fully functional, tested  
- `synguard` — functional in audit/enforce modes
- `synapse_kmod` — compiles on 6.8+, kprobes working
- `synui` — compiles against wlroots 0.18, layout engine working
- `archiso` — build pipeline functional on Arch

What's pending:
- SynapseOS kernel patch (AI_CTX syscalls) — in progress
- `synnet` — AI-assisted nftables policy (planned)
- Fine-tuned model — the current model is Mistral-7B-Instruct base; OS-specific fine-tuning on syscall analysis + shell translation is planned
- Full test suite

---

## Contributing

```bash
# Build a single component
cd synapd && meson setup build && ninja -C build

# Run synsh without installing
cd synsh && meson setup build && ./build/synsh

# Test synguard in audit mode (no kmod needed)
sudo ./synguard/build/synguard --mode audit --no-ai --debug
```

Issues, PRs, and kernel patch feedback welcome. The most useful contributions right now are testing on different GPU configurations and hardware.

---

## License

GPLv2. See [LICENSE](LICENSE).

The embedded model (`synapse-7b-q4_k_m.gguf`) is based on Mistral-7B-Instruct-v0.2 under the [Apache 2.0 License](https://huggingface.co/mistralai/Mistral-7B-Instruct-v0.2).

---

<div align="center">
<br/>
<sub>SynapseOS Project · <a href="https://synapseos.dev">synapseos.dev</a></sub>
<br/><br/>
</div>
