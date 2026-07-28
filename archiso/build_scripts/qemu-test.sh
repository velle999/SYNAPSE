#!/usr/bin/env bash
# scripts/qemu-test.sh — Test SynapseOS ISO in QEMU
#
# Usage:
#   ./qemu-test.sh path/to/SynapseOS.iso
#   ./qemu-test.sh                            # auto-detect latest ISO
#
# Requirements: qemu, ovmf (for UEFI)
#
# Options (env vars):
#   QEMU_RAM=8G      RAM (default: 4G — 8G+ recommended for 7B model)
#   QEMU_CPUS=4      CPU count (default: 4)
#   QEMU_VGA=virtio  VGA type (default: virtio)
#   QEMU_KVM=1       Enable KVM (default: auto-detect)
#   QEMU_DISK=20G    Create a virtual disk of this size (default: 20G)
#   QEMU_AUDIO=1     Enable audio passthrough
#   QEMU_NET=user    Network mode (default: user)
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO="${1:-$(ls -t "${SCRIPT_DIR}/../out"/*.iso 2>/dev/null | head -1)}"
shift || true   # Remove ISO from $@ so it isn't passed to QEMU as a bare arg
DISK="${SCRIPT_DIR}/../out/test-disk.qcow2"

[[ -z "$ISO" ]] && { echo "Usage: $0 <iso>"; exit 1; }
[[ -f "$ISO" ]] || { echo "ISO not found: $ISO"; exit 1; }

# Build creates files as root — fix ownership so QEMU can open them
if [[ -f "$ISO" && "$(stat -c %U "$ISO")" != "$(whoami)" ]]; then
    echo "ISO owned by root — fixing ownership with sudo..."
    sudo chown "$(whoami):$(id -gn)" "$ISO"
fi

RAM="${QEMU_RAM:-8G}"
CPUS="${QEMU_CPUS:-4}"
VGA="${QEMU_VGA:-virtio}"

# Auto-detect KVM
KVM=""
if [[ "${QEMU_KVM:-auto}" != "0" ]] && [[ -e /dev/kvm ]]; then
    KVM="-enable-kvm -cpu host"
    echo "KVM enabled"
else
    KVM="-cpu max"
    echo "KVM not available — running in emulation mode (slow)"
fi

# Create test disk
DISK_SIZE="${QEMU_DISK:-20G}"
if [[ ! -f "$DISK" ]]; then
    echo "Creating ${DISK_SIZE} test disk: $DISK"
    qemu-img create -f qcow2 "$DISK" "$DISK_SIZE"
fi

# OVMF (UEFI firmware)
#
# The paths this used to search — /usr/share/ovmf/OVMF.fd and friends — do not
# exist on a current Arch host: edk2-ovmf ships /usr/share/edk2/x64/. So the
# loop fell through on every machine it was meant to serve, printed one line of
# warning, and booted BIOS. Every "tested in QEMU" for a distro whose boot path
# is GRUB + Secure Boot was therefore a BIOS test. Split CODE/VARS is tried
# first (the modern layout, and the only one that can persist boot entries or
# an enrolled key), then the single-file images, then the legacy paths.
OVMF=""
OVMF_VARS_SRC=""
for d in /usr/share/edk2/x64 /usr/share/edk2-ovmf/x64 /usr/share/OVMF /usr/share/ovmf/x64; do
    for code in OVMF_CODE.4m.fd OVMF_CODE.fd; do
        if [[ -f "$d/$code" ]]; then
            for vars in OVMF_VARS.4m.fd OVMF_VARS.fd; do
                [[ -f "$d/$vars" ]] && OVMF_VARS_SRC="$d/$vars" && break
            done
            [[ -n "$OVMF_VARS_SRC" ]] && OVMF="$d/$code" && break 2
        fi
    done
done

OVMF_ARGS=""
if [[ -n "$OVMF" ]]; then
    # VARS is per-VM and writable, so the firmware can keep its boot entries.
    # Beside the test disk, and only seeded once, or every run would silently
    # discard whatever the last boot wrote.
    OVMF_VARS="${DISK%.qcow2}-OVMF_VARS.fd"
    [[ -f "$OVMF_VARS" ]] || cp "$OVMF_VARS_SRC" "$OVMF_VARS"
    OVMF_ARGS="-drive if=pflash,format=raw,readonly=on,file=${OVMF},unit=0"
    OVMF_ARGS+=" -drive if=pflash,format=raw,file=${OVMF_VARS},unit=1"
    echo "UEFI: $OVMF (vars: $OVMF_VARS)"
else
    for single in /usr/share/edk2/x64/OVMF.4m.fd /usr/share/ovmf/OVMF.fd \
                  /usr/share/OVMF/OVMF.fd /usr/lib/ovmf/OVMF.fd; do
        if [[ -f "$single" ]]; then
            OVMF_ARGS="-drive if=pflash,format=raw,readonly=on,file=${single}"
            echo "UEFI: $single (single-image; boot entries will not persist)"
            break
        fi
    done
fi

if [[ -z "$OVMF_ARGS" ]]; then
    # Loud, because the thing being tested boots differently in this mode and
    # nothing later in the run will say so again.
    echo ""
    echo "  ############################################################"
    echo "  #  NO UEFI FIRMWARE FOUND — THIS IS A LEGACY BIOS BOOT     #"
    echo "  #  It does NOT test GRUB's UEFI path or Secure Boot.       #"
    echo "  #  Install edk2-ovmf to test the way real machines boot.   #"
    echo "  ############################################################"
    echo ""
fi

echo ""
echo "⚡ Starting SynapseOS in QEMU"
echo "   ISO:  $ISO"
echo "   RAM:  $RAM"
echo "   CPUs: $CPUS"
echo "   Disk: $DISK"
echo "  SSH:    ssh -p 2222 root@localhost   (the ISO autologins root; sshd is enabled)"
echo "          — use this rather than the QEMU window when you need paste or scrollback."
echo ""

qemu-system-x86_64 \
    $KVM \
    $OVMF_ARGS \
    -m "$RAM" \
    -smp "$CPUS" \
    -vga "$VGA" \
    -display gtk,zoom-to-fit=on \
    -drive file="$DISK",if=virtio,format=qcow2 \
    -cdrom "$ISO" \
    -boot order=dc \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -usb -device usb-tablet \
    -device intel-hda -device hda-duplex \
    -name "SynapseOS Test" \
    "$@"
