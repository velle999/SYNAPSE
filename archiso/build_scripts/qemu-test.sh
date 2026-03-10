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
# SynapseOS Project — GPLv2

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO="${1:-$(ls -t "${SCRIPT_DIR}/../out"/*.iso 2>/dev/null | head -1)}"
DISK="${SCRIPT_DIR}/../build/test-disk.qcow2"

[[ -z "$ISO" ]] && { echo "Usage: $0 <iso>"; exit 1; }
[[ -f "$ISO" ]] || { echo "ISO not found: $ISO"; exit 1; }

RAM="${QEMU_RAM:-4G}"
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
OVMF=""
for ovmf_path in \
    /usr/share/ovmf/OVMF.fd \
    /usr/share/OVMF/OVMF.fd \
    /usr/lib/ovmf/OVMF.fd
do
    if [[ -f "$ovmf_path" ]]; then
        OVMF="-drive if=pflash,format=raw,readonly=on,file=${ovmf_path}"
        echo "UEFI: $ovmf_path"
        break
    fi
done
[[ -z "$OVMF" ]] && echo "Warning: OVMF not found — using legacy BIOS boot"

echo ""
echo "⚡ Starting SynapseOS in QEMU"
echo "   ISO:  $ISO"
echo "   RAM:  $RAM"
echo "   CPUs: $CPUS"
echo "   Disk: $DISK"
echo ""

qemu-system-x86_64 \
    $KVM \
    $OVMF \
    -m "$RAM" \
    -smp "$CPUS" \
    -vga "$VGA" \
    -display gtk,zoom-to-fit=on \
    -cdrom "$ISO" \
    -drive file="$DISK",if=virtio,format=qcow2 \
    -boot order=dc \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -usb -device usb-tablet \
    -device intel-hda -device hda-duplex \
    -name "SynapseOS Test" \
    "$@"
