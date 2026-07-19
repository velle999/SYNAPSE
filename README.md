<div align="center">

```
  ███████╗██╗   ██╗███╗   ██╗ █████╗ ██████╗ ███████╗███████╗
  ██╔════╝╚██╗ ██╔╝████╗  ██║██╔══██╗██╔══██╗██╔════╝██╔════╝
  ███████╗ ╚████╔╝ ██╔██╗ ██║███████║██████╔╝███████╗█████╗
  ╚════██║  ╚██╔╝  ██║╚██╗██║██╔══██║██╔═══╝ ╚════██║██╔══╝
  ███████║   ██║   ██║ ╚████║██║  ██║██║     ███████║███████╗
  ╚══════╝   ╚═╝   ╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝     ╚══════╝╚══════╝
```

# SynapseOS

**Where the kernel thinks.**

An Arch-based operating system with a local LLM wired into the system layer — not bolted on top.

[![Build](https://github.com/velle999/SYNAPSE/actions/workflows/build.yml/badge.svg)](https://github.com/velle999/SYNAPSE/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/velle999/SYNAPSE)](https://github.com/velle999/SYNAPSE/releases/latest)
[![License](https://img.shields.io/badge/license-GPLv2-blue.svg)](#license)
![Platform](https://img.shields.io/badge/platform-x86__64-1793d1)

<img src="docs/screenshots/synui-desktop.jpg" alt="synui — the SynapseOS compositor" width="900">

<sub><i>synui, the wlroots compositor: waybar status bar, auto-hiding dock, CRT post-process filters.</i></sub>
<img src="docs/screenshots//synapse-20260717-215931.png" alt="synui — amber crt effects on" width="900">

<sub><i>CRT post-process filters example in amber phosphor.</i></sub>
</div>

---

SynapseOS runs a local LLM daemon as a system service and lets the rest of the
system talk to it over a Unix socket: the shell, the compositor, the security
monitor, the network filter, and a kernel module that exports syscall telemetry
and AI scheduling hints through sysfs. No network calls, no API keys — the model
lives on the machine.

It boots to `synsh`, a shell where you can type a command or just say what you
want, and into `synui`, a wlroots compositor that knows the AI daemon exists.

> **Status: alpha.** Version 0.1.x. This is a real, actively developed system —
> the author daily-drives it — but it is early, moves fast, and will break.
> Try it in a VM before you give it a disk.

---

## Quick start

Grab the latest ISO from [Releases](https://github.com/velle999/SYNAPSE/releases/latest)
and boot it. The default ISO embeds Mistral 7B Instruct (Q4_K_M, ~4.1 GB), so
the AI is live on first boot with nothing to configure.

To take it for a spin without touching hardware:

```bash
git clone https://github.com/velle999/SYNAPSE.git && cd SYNAPSE
QEMU_RAM=8G ./archiso/build_scripts/qemu-test.sh   # auto-detects the newest ISO
```

The script uses KVM when available, boots UEFI via OVMF (falling back to BIOS),
and attaches a persistent 20 GB test disk. Give it 8 GB+ of RAM when the model
is embedded. Kernel and boot output are mirrored to the serial console —
`View → serial0` in the QEMU window.

---

## Components

Each lives in its own directory with its own `PKGBUILD`.

| Component | What it does |
|---|---|
| **`synapd`** | Local LLM inference daemon (llama.cpp). Owns the model; serves every other component over a Unix socket. |
| **`synsh`** | AI-native shell. Type naturally, or use it as a normal shell. |
| **`synui`** | Wayland compositor on wlroots 0.19 — tiling and monocle layouts, per-output workspaces, XWayland, layer-shell. See [`synui/ROADMAP.md`](synui/ROADMAP.md). |
| **`synguard`** | Security monitor. Classifies syscall events, scores threats, publishes verdicts on a feed that `synui` subscribes to. |
| **`synnet`** | Network policy daemon with nftables integration. |
| **`synapse_kmod`** | Kernel module (DKMS). Syscall monitoring and AI scheduling hints, exposed via sysfs. |

### Apps

| App | What it does |
|---|---|
| **`vibe`** | Local AI coding assistant — an agentic read/write/edit/bash/grep loop. Reuses the model already resident in `synapd` (no second model, no extra VRAM), and confirms before destructive tools. `vibe` in a terminal; `VIBE_BACKEND=ollama` to swap backends. |
| **`chibi`** | Voice-interactive AI companion with a security-sentinel aspect over `synguard`'s verdict feed. See the [Chibi wiki page](https://github.com/velle999/SYNAPSE/wiki/Chibi). |
| **`nexus-chat`**, **`tepris`** | Bundled web apps (Firefox app-mode packages). |

Supporting pieces: `syn-install`, `syn-firstboot`, `syn-model`, `waybar/`
(status bar config), and `archiso/` (install media).

---

## Architecture

```
  User
   │
   ▼
 synsh ─── natural language / commands ──┐
   │                                     │
   ▼                                     │
 synapd  (local LLM — Mistral 7B)        │
   │  inference over SYN socket protocol │
   ├──► synguard      security verdicts ─┤
   ├──► synnet        network policy     │
   └──► synapse_kmod  kernel sysfs       │
            │                            ▼
            ▼                    synui (Wayland)
     /sys/kernel/synapse/
     syscall_log, ai_hints, stats,
     status, config, version
```

---

## The desktop

`synui` is a compositor written for this system rather than adapted to it. It
draws its own display-settings panel, wallpaper picker, and dock; it renders an
optional CRT-style post-process pass; and it holds a live subscription to
`synguard`'s verdict feed and `synapd`'s activity.

Defaults (override in `~/.config/synui/synuirc` or `/etc/synui/synuirc`):

| Key | Action |
|---|---|
| `Super` (tapped alone) | Start menu (the bar's SYNAPSE badge) |
| `Super`+`C` | Control panel — every shortcut, plus the settings, in one place |
| `Super`+`Return` | Open a terminal |
| `Super`+`Space` | Command bar |
| `Super`+`Backspace` | Ask the AI |
| `Super`+`A` | Neural activity overlay |
| `Super`+`D` | Display settings |
| `Super`+`W` / `Super`+`Shift`+`W` | Wallpaper picker / reload the wallpaper |
| `Super`+`E` | Visual effects panel (per-filter strength sliders) |
| `Super`+`T` | Theme manager (SYNAPSE / Dark / XP / 95) |
| `Super`+`Shift`+`T` | Calendar |
| `Super`+`Escape` | Menu |
| `Super`+`Tab` | Cycle layout |
| `Alt`+`Tab` / `Alt`+`Shift`+`Tab` | Most-recently-used window switch |
| `Super`+`Q` / `Super`+`Shift`+`Q` | Close window / quit compositor |
| `Super`+`J` / `Super`+`K` | Focus next / previous |
| `Super`+`Shift`+`J` / `Super`+`Shift`+`K` | Move window down / up the stack |
| `Super`+`H` / `Super`+`Shift`+`L` | Shrink / grow master area |
| `Super`+`F` / `Super`+`M` / `Super`+`N` | Float / maximize / minimize |
| `Super`+`Shift`+`N` | Restore a minimized window |
| `Super`+`Shift`+`F` | Fullscreen (forces it — for games that only do "borderless") |
| `Super`+`Shift`+`D` | Show/hide titlebars |
| `Super`+`O` / `Super`+`Shift`+`O` | Move window to next / previous monitor |
| `Super`+`P` | Power saving panel |
| `Ctrl`+`Alt`+`Delete` | Task manager (processes, CPU/RAM/GPU) |
| `Super`+`G` | Game mode |
| `Super`+`L` | Lock screen |
| `Super`+`B` | Bluetooth |
| `Super`+`Shift`+`B` | Night light (blue-light filter) |
| `Super`+`I` | Network / Wi-Fi |
| `Super`+`V` | Clipboard history |
| `Super`+`R` | News (Hacker News, Arch, LWN, Phoronix, …) |
| `Super`+`Shift`+`R` | Start/stop screen recording |
| `Super`+`Shift`+`C` | Cat mode |
| `Print` | Screenshot the monitor you're on |
| `Shift`+`Print` / `Super`+`Shift`+`S` | Screenshot an area (drag it out with slurp) |
| `Ctrl`+`Print` | Screenshot every monitor at once |
| Volume keys | Raise / lower / mute (also the USB volume knob) |
| Brightness keys | Screen brightness up / down |
| `Super`+`1`–`9` | Switch workspace |
| `Super`+`Shift`+`1`–`9` | Move window to workspace |

`Super`+`Shift`+`A` is deliberately unbound — the theme manager moved to
`Super`+`T`, so it is free for a `bind =` line of your own.

Screenshots land in `~/Pictures/Screenshots` *and* on the clipboard, so you can
paste one straight into a chat without opening the file.

---

## Services

Everything starts on boot:

```bash
systemctl status synapd      # AI inference daemon
systemctl status synguard    # security monitor
systemctl status synnet      # network policy
lsmod | grep synapse_kmod    # kernel module
cat /sys/kernel/synapse/status
```

---

## Commands

### Command-line tools

Every tool is prefixed `syn` and self-documents with `--help` (or `help`).

| Command | What it does |
|---|---|
| `syn` | Top-level CLI — `syn status`, `syn info`, `syn model/net/guard …`, `syn shell`, `syn ui`, `syn install` |
| `synsh` | Natural-language shell — type plain English or normal commands; `--no-ai` for pure shell, `--intent-check` to test an intent |
| `syn-model` | Model manager — `download [mistral-7b\|phi3\|tiny]`, `list`, `status`, `remove` |
| `syn-install` | Install SynapseOS to disk (the live-ISO installer) |
| `synctl` | Talk to the running `synui` compositor over its control socket — `synctl clients`, `workspaces`, `outputs`, `activewindow`, `dispatch <action> [arg]` |
| `synui-ai-backend` | Switch `synapd`'s inference device — `gpu` / `cpu` / `off` / `toggle` / `status` (drives the "AI backend" row; see below) |
| `synapd` / `synguard` / `synnet` / `synui` | The daemons and compositor themselves — normally started by systemd, not by hand |

### Privileged desktop actions

`synui` runs as the **session user** (under a greetd session it is not root), and
the target has **no polkit agent** to prompt for a password. So the handful of
menu/desktop actions that genuinely need root are granted passwordless through
tightly-scoped `/etc/sudoers.d` rules (written by `syn-install`). These are the
*only* commands `%wheel` may run without a password — each helper self-escalates
with `sudo -n`:

| Command | Rule file | Triggered by |
|---|---|---|
| `sudo -n systemctl reboot` · `poweroff` | `power-menu` | Start-menu Reboot / Shut Down |
| `sudo -n systemctl stop synapd` · `start synapd` | `synapd-gamemode` | Game mode (`Super`+`G`) frees the GPU |
| `sudo -n synui-ai-backend gpu\|cpu\|off\|toggle` | `synapd-backend` | "AI backend" row (control panel / `Super`+`Escape`) |

Anything else still prompts for a password (`%wheel ALL=(ALL:ALL) ALL`). When
`synui` instead runs as root via `synui.service`, the `sudo -n` re-exec is a
no-op — the helpers already have the privilege they need.

---

## The model

ISOs built with `--no-model` are ~4 GB smaller and fetch the model on first boot
via `syn-firstboot`. You can also drop one in by hand:

```bash
cp your-model.gguf /var/lib/synapd/models/synapse.gguf
systemctl restart synapd

synsh
# ⚡ AI online — type naturally or use shell commands
```

Any GGUF that llama.cpp can load will work; Mistral 7B Instruct is what the
prompts are tuned against.

### GPU inference

`synapd` runs on the CPU by default and offloads to the GPU when a matching
`synapse-llama-*` build is installed. The installer picks the right one for your
hardware; you can also switch at any time from the desktop's **AI backend** row
(or `synui-ai-backend gpu`).

| Package | Hardware | Backend | Runtime cost |
|---|---|---|---|
| `synapse-llama` | any | CPU | — (the default) |
| `synapse-llama-cuda` | NVIDIA | CUDA | large (`cuda` ~4.7 GiB) |
| `synapse-llama-vulkan` | AMD / Intel | Vulkan | tiny (loader + mesa ICD) |

The **Vulkan** build is deliberately portable — one package runs on any AMD
(GCN/RDNA/APU) or Intel GPU with no per-card compile, which is why the ISO can
offer it broadly. ROCm/HIP is an opt-in high-performance path for a known AMD
card: build it with `sudo archiso/build.sh --gpu=rocm`.

---

## Building

**Prerequisites** — an Arch (or Arch-based) host with `archiso`, `base-devel`,
`meson`, `ninja`, `wlroots0.19`, `qemu`, and `ovmf`. Budget ~22 GB of free disk
with the embedded model, ~9 GB without.

`archiso/build.sh` runs the whole pipeline: builds llama.cpp (pinned at tag
`b8272`, matching CI), packages every component through its `PKGBUILD` into a
local pacman repo, fetches the model, and invokes `mkarchiso`.

```bash
sudo archiso/build.sh              # full build, GPU auto-detected
sudo archiso/build.sh --no-gpu     # CPU-only llama.cpp — use for QEMU-targeted ISOs
sudo archiso/build.sh --gpu=vulkan # AMD/Intel GPU backend (portable; needs shaderc)
sudo archiso/build.sh --gpu=cuda   # NVIDIA GPU backend (needs the cuda toolkit)
sudo archiso/build.sh --no-model   # slim ISO, model downloaded on first boot
sudo archiso/build.sh --no-clean   # reuse the previous llama.cpp build
```

Regardless of the ISO's own backend, a release build **also** packages a GPU
build into the repo when the host has the toolchain — `synapse-llama-cuda` if
`nvcc` is present, `synapse-llama-vulkan` if `glslc` (from `shaderc`) is — so an
installed machine can switch onto its GPU. Add `shaderc` + `vulkan-headers` to
the build host to ship the AMD/Intel package.

Output lands in `archiso/out/SynapseOS-<version>-x86_64.iso`.

Package builds run as the `synbuild` user under `/var/tmp`, because they must
live outside `/home` (mode 0700). A failed package build aborts the run
immediately rather than resurfacing later as a confusing pacstrap error.

For the inner-loop, skip the ISO entirely:

```bash
bash build-all.sh   # builds every component against llama-staging/usr/
```

### Cutting a release

Bump **`iso_version`** in `archiso/profiledef.sh` and nothing else — in
particular, leave `SYNAPSEOS_VERSION` in `archiso/build.sh` alone, as it tracks
the component series rather than the image. Then run `archiso/publish-release.sh`.

---

## Protocol

`synsh`, `synui`, `synguard`, `synnet`, and the kernel module all speak to
`synapd` over a Unix socket using a fixed 28-byte binary header
([`synapd/include/synapd.h`](synapd/include/synapd.h)):

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;        // 0x53594E41 "SYNA"
    uint8_t  version;      // SYNAPD_PROTOCOL_VER
    uint8_t  msg_type;     // QUERY, SYSCALL_EVENT, SCHED_HINT, CONTEXT_PUSH/GET,
                           // STATUS, RELOAD, SHUTDOWN; responses OR SYN_MSG_RESPONSE
    uint16_t flags;
    uint32_t payload_len;  // bytes following this header, max 1 MiB
    uint32_t request_id;   // echoed in the response
    uint32_t client_pid;   // sender PID, for privilege checks
    uint64_t timestamp_ns; // CLOCK_MONOTONIC_RAW
} syn_msg_header_t;        // 28 bytes
#pragma pack(pop)
```

---

## License

GPLv2 — the SynapseOS Project.
