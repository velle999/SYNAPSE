#!/usr/bin/env bash
# layout_test.sh — the filesystem/bootloader decision table
#
# syn-install cannot be unit-tested as a whole: it partitions disks. But the
# part most likely to be wrong is not the procedure, it is the TABLE — four
# filesystems by four bootloaders by encrypted-or-not by UEFI-or-BIOS, where a
# wrong answer produces an install that formats, reports success, and then fails
# at first boot on someone else's machine.
#
# So the decisions are pure functions in syn-install.sh, and this asserts them
# exhaustively. It sources the script with SYN_INSTALL_SOURCE_ONLY=1, which stops
# it before it touches anything.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
export SYN_INSTALL_SOURCE_ONLY=1
# shellcheck source=/dev/null
. "$here/../syn-install.sh"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

echo "=== filesystem table ==="

# Every offered filesystem must answer every question. A filesystem that is
# offered but has no mkfs command, no target package or no mount options would
# fail partway through an install with the disk already wiped.
for fs in $SYN_FILESYSTEMS; do
    check "$fs has an mkfs command"   "yes" "$(fs_mkfs_cmd    "$fs" >/dev/null 2>&1 && echo yes || echo no)"
    check "$fs has target packages"   "yes" "$(fs_target_pkgs "$fs" >/dev/null 2>&1 && echo yes || echo no)"
    check "$fs has mount options"     "yes" "$(fs_mount_opts  "$fs" >/dev/null 2>&1 && echo yes || echo no)"
done

# An unknown filesystem must FAIL, not fall through to a default. Silently
# formatting ext4 because someone typed "brtfs" is the worst possible outcome.
check "an unknown fs is rejected by mkfs_cmd"   "no" "$(fs_mkfs_cmd    zfs >/dev/null 2>&1 && echo yes || echo no)"
check "an unknown fs is rejected by target_pkgs" "no" "$(fs_target_pkgs zfs >/dev/null 2>&1 && echo yes || echo no)"

# The one filesystem that carries snapshots.
check "btrfs supports snapshots" "yes" "$(fs_supports_snapshots btrfs && echo yes || echo no)"
for fs in ext4 xfs f2fs; do
    check "$fs does NOT claim snapshots" "no" "$(fs_supports_snapshots "$fs" && echo yes || echo no)"
done

# btrfs must actually ask for compression — the whole point of choosing it.
check "btrfs mounts with zstd compression" "yes" \
    "$(case "$(fs_mount_opts btrfs)" in *compress=zstd*) echo yes ;; *) echo no ;; esac)"

echo
echo "=== bootloader gating ==="

check "grub works on BIOS"              "yes" "$(bootloader_supported grub bios         && echo yes || echo no)"
check "grub works on UEFI"              "yes" "$(bootloader_supported grub uefi         && echo yes || echo no)"
# The gate that matters: systemd-boot is an EFI stub loader and has no BIOS
# build at all. Offering it on a BIOS machine would be a menu entry that lies.
check "systemd-boot REFUSED on BIOS"    "no"  "$(bootloader_supported systemd-boot bios && echo yes || echo no)"
check "systemd-boot allowed on UEFI"    "yes" "$(bootloader_supported systemd-boot uefi && echo yes || echo no)"
check "refind REFUSED on BIOS"          "no"  "$(bootloader_supported refind bios       && echo yes || echo no)"
check "an unknown bootloader is refused" "no" "$(bootloader_supported burg uefi         && echo yes || echo no)"

# Every bootloader we OFFER must be supported in at least one firmware mode,
# or it is dead code in a menu.
for bl in $SYN_BOOTLOADERS; do
    ok=no
    bootloader_supported "$bl" uefi && ok=yes
    bootloader_supported "$bl" bios && ok=yes
    check "$bl is usable somewhere" "yes" "$ok"
done

# Snapshot booting is GRUB-only, and only because grub-btrfs is in the official
# repos. If this ever reports yes for another loader, the installer is promising
# a boot menu that will not exist.
check "grub can boot snapshots"          "yes" "$(bootloader_supports_snapshots grub         && echo yes || echo no)"
check "systemd-boot canNOT boot snapshots" "no" "$(bootloader_supports_snapshots systemd-boot && echo yes || echo no)"

echo
echo "=== /boot layout, all combinations ==="

# systemd-boot never needs a separate /boot — kernels live on the ESP and the
# initramfs unlocks. This is the case that REMOVES the argon2id compromise, so
# it must hold even when encrypting.
for fs in $SYN_FILESYSTEMS; do
    for enc in yes no; do
        check "systemd-boot + $fs + encrypt=$enc → no separate /boot" \
            "no" "$(layout_separate_boot "$fs" systemd-boot "$enc")"
    done
done

# GRUB + encryption always needs one, whatever the filesystem: GRUB cannot open
# an argon2id LUKS2 volume.
for fs in $SYN_FILESYSTEMS; do
    check "grub + $fs + encrypted → separate /boot" \
        "yes" "$(layout_separate_boot "$fs" grub yes)"
done

# Unencrypted GRUB: btrfs keeps /boot inside the root so a rollback restores the
# kernel alongside its modules. xfs and f2fs get a plain ext4 /boot to sidestep
# GRUB's filesystem-feature lag entirely.
check "grub + btrfs unencrypted → /boot INSIDE the root" "no"  "$(layout_separate_boot btrfs grub no)"
check "grub + ext4  unencrypted → no separate /boot"     "no"  "$(layout_separate_boot ext4  grub no)"
check "grub + xfs   unencrypted → separate /boot"        "yes" "$(layout_separate_boot xfs   grub no)"
check "grub + f2fs  unencrypted → separate /boot"        "yes" "$(layout_separate_boot f2fs  grub no)"

echo
if [ "$fails" -gt 0 ]; then
    echo "$fails check(s) FAILED"
    exit 1
fi
echo "all checks passed"
