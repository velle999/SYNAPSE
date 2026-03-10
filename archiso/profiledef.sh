#!/usr/bin/env bash
#
# profiledef.sh — SynapseOS archiso profile definition
#
# This file is read by mkarchiso to configure the ISO build.
# Run the build with:
#   sudo ./build.sh
#
# Output: out/SynapseOS-0.1.0-x86_64.iso
#
# SynapseOS Project — GPLv2
# https://synapseos.dev

iso_name="SynapseOS"
iso_label="SYNAPSEOS_$(date +%Y%m)"
iso_publisher="SynapseOS Project <https://synapseos.dev>"
iso_application="SynapseOS — Where the kernel thinks."
iso_version="0.1.0"
iso_filename="${iso_name}-${iso_version}-$(date +%Y%m%d)-x86_64.iso"

# Architecture
arch="x86_64"

# Boot modes to support
bootmodes=(
    'bios.syslinux.mbr'
    'bios.syslinux.eltorito'
    'uefi-ia32.grub.esp'
    'uefi-x64.grub.esp'
    'uefi-x64.grub.eltorito'
)

# Compression for the squashfs root filesystem
# zstd level 15 gives good ratio with fast decompression on modern CPUs
# Use 'xz' for smaller ISO at the cost of slower boot
airootfs_image_type="squashfs"
airootfs_image_tool_options=(
    '-comp' 'zstd'
    '-Xcompression-level' '15'
    '-b' '1M'
    '-noappend'
)

# Install these packages into the live root filesystem
# Full package list is in packages.x86_64
# This array is for additional packages on top of base
declare -a extra_packages=(
    # SynapseOS core packages (built locally via PKGBUILD)
    # These are added to a local repo by build.sh before mkarchiso runs
    # 'synapseos-synapd'
    # 'synapseos-synsh'
    # 'synapseos-synguard'
    # 'synapseos-synui'
    # 'synapseos-kmod'
)

# File permissions — format: path:owner:group:mode
file_permissions=(
    ["/etc/shadow"]="0:0:400"
    ["/etc/gshadow"]="0:0:400"
    ["/etc/synapd/synapd.conf"]="0:0:640"
    ["/etc/synguard"]="0:65:750"
    ["/var/lib/synapd"]="0:0:750"
    ["/var/lib/synguard"]="0:65:750"
    ["/var/log/synguard"]="0:65:750"
    ["/usr/bin/synguard"]="0:0:755"
    ["/usr/bin/synapd"]="0:0:755"
    ["/usr/bin/synsh"]="0:0:755"
    ["/usr/bin/synui"]="0:0:755"
    ["/usr/bin/syn"]="0:0:755"
    ["/usr/bin/syn-kmod-status"]="0:0:755"
)
