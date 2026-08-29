# SynapseOS Build System

Builds a bootable ISO using [archiso](https://wiki.archlinux.org/title/Archiso).

## Prerequisites

```bash
# On Arch Linux
sudo pacman -S archiso base-devel git cmake meson ninja qemu ovmf

# Minimum disk space
# ~9GB   default (no model on the image)
# ~22GB  with --with-model
```

## Quick Build

```bash
# Full build — no AI model on the image (the default since 0.2.8;
# syn-install asks which model to download onto the target instead)
sudo ./build.sh

# Embed a 7B model in the image (~4.1GB bigger)
sudo ./build.sh --with-model

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

## Signing a release

A `.sha256` published in the same GitHub release as the ISO proves the download
was not truncated. It proves nothing about who built it: whoever could alter the
ISO could alter the checksum in the same breath. The signature is the only
artefact that answers "did this come from us", and it is worth something only
because the public key reaches people by a different road — soslinux.org, which
is a separate repository and a separate deploy.

### The key

One key, used for nothing but releases. Not a personal identity: a release key
can be rotated or revoked without touching anything else you sign, and a build
host that gets compromised then costs you the release key rather than your name.

```bash
gpg --quick-generate-key "SynapseOS Release Signing <releases@soslinux.org>" \
    ed25519 sign 3y
gpg --list-secret-keys --keyid-format=long     # note the fingerprint
```

Give it a passphrase. `--sign` will prompt for it once per build; a key with no
passphrase on a machine that builds ISOs is a key anyone with that disk has.

Export the public half for the website, and keep the revocation certificate
(gpg writes one to `~/.gnupg/openpgp-revocs.d/`) somewhere that is not this
machine:

```bash
gpg --armor --export <fingerprint> > synapseos-release-key.asc
```

### Building and publishing

```bash
SYNAPSE_SIGNING_KEY=<fingerprint> sudo -E ./build.sh --sign
./publish-release.sh 0.2.9.5
```

⛔ **`sudo -E`, not plain `sudo`.** The build needs root for mkarchiso, and
without `-E` the environment — including `SYNAPSE_SIGNING_KEY` — does not
survive into it.

`build.sh` signs as the **invoking** user, not as root: root's keyring holds no
release key, and a bare `gpg` under sudo would either fail or, on a machine
where root did have a key, quietly sign with the wrong one. It names the key
explicitly for the same reason — `gpg --detach-sign` with no `-u` picks whatever
secret key comes first, and a release accidentally signed with someone's
personal identity is not something you can quietly take back.

Both scripts **verify the signature they just made or are about to publish**.
An `.asc` that cannot verify is worse than none: it invites a stranger doing the
right thing to run a check that fails. `publish-release.sh` refuses to upload
one that does not verify, and warns loudly — without failing — when there is no
signature at all, because a test build is a legitimate thing to publish.

### What users run

```bash
gpg --import synapseos-release-key.asc          # once, from soslinux.org
gpg --verify SynapseOS-0.2.9.5-x86_64.iso.asc SynapseOS-0.2.9.5-x86_64.iso
```

Verified against a throwaway key before shipping: a good signature passes, a
**tampered ISO is refused**, and an unknown key id is an error rather than a
silent fallback to some other key.

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
                 └─ synui.service (Wayland compositor, launched by
                      │            systemd — the live ISO boots straight
                      │            into the desktop, no login prompt)
                      └─ synui-foot.service → foot (terminal, synsh)
```

Installed systems boot differently: greetd + tuigreet show a login
prompt on tty1 and launch synui as a PAM session for the user
(`/usr/local/bin/synui-session`, written by the installer). KDE and
GNOME installs use SDDM/GDM login screens.

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
  ├── 5. Model          (swept off the image; --with-model embeds one)
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

**The ISO ships without one.** It was ~4.1GB of an ~8GB image for a model the
live session can only run on the CPU, which in a VM is slow enough to be worse
than not offering it — so `syn-install` asks which model to download onto the
machine it installs (Mistral 7B recommended, Phi-3 Mini and Qwen2 0.5B offered
with what they cost in quality, and "None" allowed). synapd on the live ISO
therefore has nothing to load and runs in shell-assist mode only.

`--with-model` puts one back: `synapse.gguf`, currently
Mistral-7B-Instruct-v0.2 Q4_K_M. A gguf swept off the image is moved to
`archiso/model-cache/`, not deleted, so turning the flag back on costs no
download.

For production SynapseOS, this will be a model fine-tuned on:
- Linux syscall analysis (for synguard)
- Shell command translation (for synsh)
- Scheduling intent classification (for synapd scheduler)
- Window layout suggestions (for synui)

See `docs/model-finetuning.md` for the fine-tuning pipeline.

## First Boot

After installation, `synapseos-firstboot.service` runs once:
1. Downloads a model if the installer did not (~0.4-4.1GB, your pick)
2. Builds `synapse_kmod` via DKMS against the installed kernel
3. Starts `synapd` and verifies the model loads
4. Switches `synguard` to `audit` mode (safe default)

To enable enforcement after you've reviewed the baseline:
```bash
sudo systemctl edit synguard
# Add: ExecStart=
# Add: ExecStart=/usr/bin/synguard --mode enforce --rules /etc/synguard/rules.d/
```
