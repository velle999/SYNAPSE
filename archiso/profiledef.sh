#!/usr/bin/env bash
# shellcheck disable=SC2034
iso_name="SynapseOS"
iso_label="SYNAPSEOS_$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y%m)"
iso_publisher="SynapseOS Project <https://github.com/velle999/SYNAPSE>"
iso_application="SynapseOS — Where the kernel thinks."
iso_version="0.2.9.4"
install_dir="arch"
buildmodes=('iso')
bootmodes=('bios.syslinux' 'uefi.grub')
pacman_conf="pacman.conf"
arch="x86_64"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'zstd' '-Xcompression-level' '15' '-b' '1M' '-noappend')
bootstrap_tarball_compression=('zstd' '-c' '-T0' '--auto-threads=logical' '--long' '-19')
# No `declare -A` here: mkarchiso sources this file inside a function, so
# `declare` would create a function-local array that shadows mkarchiso's
# global — every entry below would be silently ignored. Plain assignment
# writes into the global mkarchiso already declared.
file_permissions=(
    ["/etc/shadow"]="0:0:400"
    ["/etc/gshadow"]="0:0:400"
    ["/var/lib/synapd"]="0:0:750"
    ["/usr/bin/synapd"]="0:0:755"
    ["/usr/bin/synsh"]="0:0:755"
    ["/usr/bin/synapse-kmod-build"]="0:0:755"
    ["/usr/local/bin/synui-gfx-env"]="0:0:755"
    ["/root/.ssh"]="0:0:700"
    ["/root/.ssh/authorized_keys"]="0:0:600"
)
