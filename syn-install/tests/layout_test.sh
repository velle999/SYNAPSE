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

echo "=== ESP mount point and size ==="

# GRUB reads a real filesystem, so the ESP is a side-car at /boot/efi.
# systemd-boot reads ONLY the ESP, so the kernels have to be on it — which
# means it is /boot itself. Getting this backwards produces a machine that
# installs cleanly and then finds nothing to boot.
check "grub mounts the ESP at /boot/efi"       "/boot/efi" "$(layout_esp_mount grub)"
check "systemd-boot mounts the ESP at /boot"   "/boot"     "$(layout_esp_mount systemd-boot)"

# systemd-boot stores every kernel and initramfs on the ESP; 512 MiB runs out
# on the second kernel, and running out looks like a failed pacman upgrade.
check "grub ESP is 512 MiB"                    "512"       "$(layout_esp_size_mib grub)"
check "systemd-boot ESP is larger"             "1024"      "$(layout_esp_size_mib systemd-boot)"

# The invariant tying the two together: whenever there is no separate /boot AND
# the loader is GRUB, the kernels live on the root filesystem — so GRUB must be
# able to read that filesystem. Every offered combination has to satisfy it.
for fs in $SYN_FILESYSTEMS; do
    for enc in yes no; do
        sep="$(layout_separate_boot "$fs" grub "$enc")"
        if [ "$sep" = "no" ]; then
            check "grub + $fs + encrypt=$enc: kernels on root, so /boot is NOT separate" \
                "no" "$sep"
        fi
    done
done

echo "=== limine ==="

check "limine is offered"                    "yes"    "$(case " $SYN_BOOTLOADERS " in *" limine "*) echo yes ;; *) echo no ;; esac)"
check "limine allowed on UEFI"               "yes"    "$(bootloader_supported limine uefi && echo yes || echo no)"
# Only the UEFI path is implemented, and the prompt is UEFI-only anyway.
check "limine REFUSED on BIOS"               "no"     "$(bootloader_supported limine bios && echo yes || echo no)"
check "limine CAN boot snapshots"            "yes"    "$(bootloader_supports_snapshots limine && echo yes || echo no)"
check "limine mounts the ESP at /boot"       "/boot"  "$(layout_esp_mount limine)"
check "limine needs no separate /boot"       "no"     "$(layout_separate_boot btrfs limine no)"
check "limine needs no separate /boot (enc)" "no"     "$(layout_separate_boot btrfs limine yes)"

# The sizing that actually matters. limine-snapper-sync copies EVERY retained
# snapshot's kernel and initramfs onto the ESP (~150 MB each) and stops adding
# entries at 85% full — so a snapshot install needs a far bigger ESP, and
# getting this wrong shows up months later as snapshots silently vanishing.
check "limine without snapshots: 1 GiB ESP"  "1024"   "$(layout_esp_size_mib limine no)"
check "limine WITH snapshots: 4 GiB ESP"     "4096"   "$(layout_esp_size_mib limine yes)"
# The snapshot answer must not inflate the other loaders' ESPs.
check "grub ESP unaffected by snapshots"     "512"    "$(layout_esp_size_mib grub yes)"
check "systemd-boot ESP unaffected"          "1024"   "$(layout_esp_size_mib systemd-boot yes)"

# Every loader that claims snapshots must have something that generates the
# entries, or the installer offers a menu that will not appear.
for bl in $SYN_BOOTLOADERS; do
    if bootloader_supports_snapshots "$bl"; then
        check "$bl claims snapshots and is grub or limine" "yes" \
            "$(case "$bl" in grub|limine) echo yes ;; *) echo no ;; esac)"
    fi
done

echo "=== structural checks on the script itself ==="

# format_and_mount_root() must not call itself. An editing slip made it do
# exactly that once, and bash -n is perfectly happy with infinite recursion —
# it would have been discovered by a disk that never got formatted.
selfcalls=$(awk '/^format_and_mount_root\(\) \{/,/^\}/' "$here/../syn-install.sh" \
            | grep -c '^[[:space:]]*format_and_mount_root[[:space:]]*$')
check "format_and_mount_root does not call itself" "0" "$selfcalls"

# Both partitioning branches must go through the helper, or one of them formats
# without creating subvolumes and btrfs snapshots silently do not work.
calls=$(grep -c '^[[:space:]]\+format_and_mount_root\b' "$here/../syn-install.sh")
check "both partitioning branches call the helper" "2" "$calls"

# Every mkfs of a ROOT device goes through fs_mkfs_cmd. A literal mkfs.ext4 on a
# root device would ignore the user's filesystem choice entirely.
badroot=$(grep -c 'mkfs\.ext4 -F "\$ROOT_FS_DEV"' "$here/../syn-install.sh")
check "no hardcoded mkfs.ext4 on the root device" "0" "$badroot"

echo
if [ "$fails" -gt 0 ]; then
    echo "$fails check(s) FAILED"
    exit 1
fi
echo "all checks passed"
