# SynapseOS

> **Where the kernel thinks.**

SynapseOS is an Arch-based operating system where AI is woven into the kernel layer — not bolted on top. It boots directly into an AI-native shell, runs a local LLM daemon as a system service, and exposes AI scheduling hints through a custom kernel module.

---

## Boot Experience

SynapseOS boots to a branded TTY and auto-logs in as root, launching `synsh` — an AI-native shell where you can type naturally or use standard shell commands.
```
  ███████╗██╗   ██╗███╗   ██╗ █████╗ ██████╗ ███████╗███████╗
  ██╔════╝╚██╗ ██╔╝████╗  ██║██╔══██╗██╔══██╗██╔════╝██╔════╝
  ███████╗ ╚████╔╝ ██╔██╗ ██║███████║██████╔╝███████╗█████╗
  ╚════██║  ╚██╔╝  ██║╚██╗██║██╔══██║██╔═══╝ ╚════██║██╔══╝
  ███████║   ██║   ██║ ╚████║██║  ██║██║     ███████║███████╗
  ╚══════╝   ╚═╝   ╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝     ╚══════╝╚══════╝

  Where the kernel thinks.
```

---

## Components

| Component | Description | Status |
|-----------|-------------|--------|
| **synsh** | AI-native shell — type naturally or use shell commands | ✅ 0.1.0 |
| **synapd** | Local LLM inference daemon, Unix socket IPC | ✅ 0.1.0 |
| **synapse_kmod** | Kernel module — syscall monitoring, AI scheduling hints, sysfs interface | ✅ 0.1.0 |
| **synnet** | AI-assisted network policy daemon, nftables integration | ✅ 0.1.0 |
| **synguard** | AI security monitor — syscall event classification, threat scoring | ✅ 0.1.0 |
| **synui** | AI-aware Wayland compositor (wlroots 0.19) — tiling/monocle layouts, per-output workspaces, XWayland, layer-shell (see `synui/ROADMAP.md`) | ✅ 0.1.0 |

---

## Architecture
```
  User
   │
   ▼
 synsh  ──────────────────────────────┐
   │   natural language / commands    │
   ▼                                  │
 synapd  (local LLM — Mistral 7B)    │
   │   inference, socket IPC          │
   ├──► synguard  (security verdicts) │
   ├──► synnet    (network policy)    │
   └──► synapse_kmod  (kernel sysfs)  │
            │                         │
            ▼                         ▼
      /sys/kernel/synapse/        synui (Wayland)
      syscall_log, ai_hints,
      status, scheduler
```

---

## Services

All services start automatically on boot:
```bash
systemctl status synapd      # AI inference daemon
systemctl status synnet      # network policy
systemctl status synguard    # security monitor
lsmod | grep synapse_kmod    # kernel module
```

---

## Enabling AI

The default ISO build embeds Mistral 7B Instruct (Q4_K_M, ~4.1 GB) — AI is
live out of the box. ISOs built with `--no-model` are ~4 GB smaller and
download the model on first boot instead (`syn-firstboot`), or you can add
one manually:
```bash
# Copy a GGUF model (Mistral 7B recommended)
cp your-model.gguf /var/lib/synapd/models/synapse.gguf
systemctl restart synapd

# Confirm AI is loaded
synsh
# ⚡ AI online — type naturally or use shell commands
```

---

## Building

### Prerequisites
- Arch Linux host (or Arch-based)
- `archiso`, `base-devel`, `meson`, `ninja`, `wlroots0.19`, `qemu`, `ovmf`
- ~22 GB free disk space with the embedded model, ~9 GB without

### Build the ISO
`archiso/build.sh` runs the whole pipeline: it builds llama.cpp (pinned at
tag `b8272`, matching CI), packages all components via their PKGBUILDs into
a local pacman repo, downloads the model, and runs mkarchiso.
```bash
sudo archiso/build.sh              # full build, GPU auto-detected
sudo archiso/build.sh --no-gpu     # CPU-only llama.cpp (use for QEMU-targeted ISOs)
sudo archiso/build.sh --no-model   # slim ISO, model downloaded on first boot
sudo archiso/build.sh --no-clean   # keep previous llama.cpp build (faster rebuilds)
```
Output: `archiso/out/SynapseOS-0.1.0-x86_64.iso`

Package builds run as the `synbuild` user under `/var/tmp` — they must live
outside `/home` (mode 0700). A failed package build aborts the run
immediately rather than surfacing later as a pacstrap error.

### Component-only builds (dev loop)
```bash
bash build-all.sh    # builds every component against llama-staging/usr/
```

### Test in QEMU
```bash
QEMU_RAM=8G ./archiso/build_scripts/qemu-test.sh   # auto-detects latest ISO
```
The script enables KVM when available, boots UEFI via OVMF (falling back to
BIOS), and attaches a persistent 20 GB test disk. Use 8 GB+ RAM when the
model is embedded. Kernel/boot output is mirrored to the serial console
(`View → serial0` in the QEMU window).

---

## Protocol

`synsh`, `synui`, and the kernel module talk to `synapd` over a Unix socket
using the SYN binary protocol (`synapd/include/synapd.h`):
```c
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;        // 0x53594E41 "SYNA"
    uint8_t  version;      // SYNAPD_PROTOCOL_VER
    uint8_t  msg_type;     // QUERY, SYSCALL_EVENT, SCHED_HINT, CONTEXT_PUSH/GET,
                           // STATUS, RELOAD, SHUTDOWN; responses OR SYN_MSG_RESPONSE
    uint16_t flags;
    uint32_t payload_len;  // bytes following this header, max 1 MiB
    uint32_t request_id;   // echoed in response
    uint32_t client_pid;   // sender PID for privilege checks
    uint64_t timestamp_ns; // CLOCK_MONOTONIC_RAW
} syn_msg_header_t;        // 28 bytes
#pragma pack(pop)
```

---

## License

GPLv2 — SynapseOS Project
