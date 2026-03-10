# SynapseOS Build System

Builds a bootable ISO using [archiso](https://wiki.archlinux.org/title/Archiso).

## Prerequisites

```bash
# On Arch Linux
sudo pacman -S archiso base-devel git cmake meson ninja qemu ovmf

# Minimum disk space
# ~22GB with embedded model
# ~9GB  without
```

## Quick Build

```bash
# Full build (with 7B model embedded)
sudo ./build.sh

# Without model (faster — model downloaded on first boot)
sudo ./build.sh --no-model

# CPU-only llama.cpp (no GPU)
sudo ./build.sh --no-gpu

# Skip cleaning previous artifacts
sudo ./build.sh --no-clean
```

Output: `out/SynapseOS-0.1.0-YYYYMMDD-x86_64.iso`

## Test in QEMU

```bash
# Auto-detect latest ISO
./build_scripts/qemu-test.sh

# Specific ISO, more RAM for the model
QEMU_RAM=8G ./build_scripts/qemu-test.sh out/SynapseOS-*.iso
```

## Write to USB

```bash
sudo dd if=out/SynapseOS-*.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

## Install to Disk

Boot the ISO, then run:
```bash
sudo syninstall
```

The installer is a guided bash/dialog TUI. Takes ~10 minutes.

## Directory Structure

```
archiso/
├── profiledef.sh          ← archiso profile (name, version, bootmodes)
├── packages.x86_64        ← package list for the live ISO
├── build.sh               ← master build script
├── airootfs/              ← files copied into the live root filesystem
│   ├── customize_airootfs.sh  ← runs in chroot during build
│   ├── etc/
│   │   ├── modules-load.d/synapse.conf   ← auto-load synapse_kmod
│   │   ├── modprobe.d/synapse.conf       ← kmod options
│   │   ├── systemd/system/              ← service units
│   │   └── synapseos/firstboot.sh       ← first-boot setup
│   ├── usr/bin/syninstall ← guided installer
│   └── var/lib/synapd/models/           ← AI model (if embedded)
├── efiboot/loader/entries/synapseos.conf ← EFI boot entry
└── build_scripts/
    └── qemu-test.sh       ← QEMU test runner
```

## Boot Sequence

```
UEFI/BIOS
  └─ GRUB
       └─ Linux kernel + initramfs
            └─ systemd
                 ├─ synapse_kmod.ko       (modules-load.d)
                 ├─ synapd.service        (AI daemon)
                 ├─ synguard.service      (security monitor, audit mode)
                 ├─ NetworkManager
                 └─ greetd (display manager)
                      └─ synui (Wayland compositor, autologin: syn)
                           └─ foot (terminal)
```

## Live System Credentials

```
user:     syn
password: synapse
```

The `syn` user has passwordless sudo on the live ISO.

## Build Pipeline

```
build.sh
  ├── 1. Preflight      (deps, disk space, GPU detection)
  ├── 2. llama.cpp      (git clone + cmake, GPU auto-configured)
  ├── 3. SynapseOS pkgs (makepkg for each component)
  ├── 4. Local repo     (repo-add → synapseos.db)
  ├── 5. Model download (mistral-7b → synapse-7b-q4_k_m.gguf)
  ├── 6. mkarchiso      (squashfs + ISO assembly)
  └── 7. Checksums      (sha256, b2sum)
```

## GPU Support

`build.sh` auto-detects the host GPU and builds llama.cpp accordingly:

| GPU       | Backend  | Detection         |
|-----------|----------|-------------------|
| NVIDIA    | CUDA     | `lspci \| grep nvidia` |
| AMD       | ROCm/HIP | `lspci \| grep amd`    |
| Intel Arc | SYCL     | (manual: --gpu=sycl) |
| None      | CPU AVX2 | fallback          |

## Model

The embedded model is `synapse-7b-q4_k_m.gguf` — currently using
Mistral-7B-Instruct-v0.2 Q4_K_M as the base.

For production SynapseOS, this will be a model fine-tuned on:
- Linux syscall analysis (for synguard)
- Shell command translation (for synsh)
- Scheduling intent classification (for synapd scheduler)
- Window layout suggestions (for synui)

See `docs/model-finetuning.md` for the fine-tuning pipeline.

## First Boot

After installation, `synapseos-firstboot.service` runs once:
1. Downloads the model if not embedded (~4.1GB)
2. Builds `synapse_kmod` via DKMS against the installed kernel
3. Starts `synapd` and verifies the model loads
4. Switches `synguard` to `audit` mode (safe default)

To enable enforcement after you've reviewed the baseline:
```bash
sudo systemctl edit synguard
# Add: ExecStart=
# Add: ExecStart=/usr/bin/synguard --mode enforce --rules /etc/synguard/rules.d/
```
