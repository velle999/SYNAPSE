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
| **synui** | Wayland compositor skeleton (wlroots 0.19) | ✅ 0.1.0 |

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

The live ISO ships without a model to keep the image size manageable. To enable full AI features:
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
- Arch Linux build VM
- `archiso`, `mkinitcpio-archiso`, `meson`, `ninja`, `wlroots0.19`
- llama.cpp built at `llama-staging/usr/`

### Build
```bash
# Build all components
LLAMA_INC=llama-staging/usr/include \
LLAMA_LIB=llama-staging/usr/lib \
bash build-all.sh

# Build ISO
sudo mkarchiso -v \
  -w /tmp/synapse-work \
  -o /tmp/synapse-out \
  archiso/
```

### Test in QEMU
```bash
qemu-system-x86_64 -m 4G -smp 2 -machine q35 \
  -drive file=SynapseOS-0.1.0-x86_64.iso,media=cdrom,readonly=on \
  -boot d -vga virtio -display sdl
```

---

## Protocol

`synsh` ↔ `synapd` communicate over a Unix socket using the SYN binary protocol:
```c
typedef struct {
    uint32_t magic;        // 0x53594E41 "SYNA"
    uint8_t  version;      // 1
    uint8_t  msg_type;     // SYN_MSG_QUERY | SYN_MSG_RESPONSE
    uint16_t flags;
    uint32_t payload_len;
    uint32_t request_id;
    uint32_t client_pid;
    uint64_t timestamp_ns;
} syn_hdr_t;               // 28 bytes
```

---

## Roadmap

- [ ] First-boot model downloader
- [ ] synui — full Wayland compositor with AI-aware window management
- [ ] synnet — active connection monitoring and AI verdict enforcement
- [ ] synguard — ENFORCE mode with process isolation
- [ ] os-release / neofetch integration
- [ ] SynapseOS installer

---

## License

GPLv2 — SynapseOS Project
