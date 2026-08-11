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

# Comments quote both the failure mode and the command that caused it, so these
# have to look at code lines only or they match the explanation.
#
# Read once into a variable and match from a here-string. NOT `strip | grep -q`:
# sourcing syn-install.sh turns on `set -o pipefail`, and `grep -q` exits the
# moment it matches, so the producer takes SIGPIPE and the pipeline reports 141.
# For a string early in the file that turns every "must be absent" check into
# "no" whether the string is there or not — the checks below would have passed
# over the exact commands they exist to forbid. -F because every pattern here is
# a literal, and $0 and "$@" do not need to be spelled as regex.
codetext=$(grep -vE '^[[:space:]]*#' "$here/../syn-install.sh")
in_code()    { grep -qF -- "$1" <<<"$codetext" && echo yes || echo no; }
count_code() { grep -cF -- "$1" <<<"$codetext"; }

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

echo "=== advanced (manual) partitioning ==="

# The editor list must degrade to what the image actually has. An empty list is
# not a menu, it is a dead end — and the ISO ships neither gparted nor gdisk.
eds="$(available_part_editors)"
check "at least one partition editor is available" "yes" \
    "$([ -n "$eds" ] && echo yes || echo no)"
check "cfdisk leads the list when present" "yes" \
    "$(case "$eds" in "cfdisk"*) echo yes ;; *) command -v cfdisk >/dev/null && echo no || echo yes ;; esac)"
check "the list contains only installed editors" "yes" \
    "$(ok=yes; for e in $eds; do command -v "$e" >/dev/null || ok=no; done; echo $ok)"

# part_usable is the whole safety net of the manual path: the user types a
# device name and every way that goes wrong is quiet. These are read-only —
# nothing is opened for writing, and the "whole disk" case uses whatever disk
# this machine actually has rather than a name baked into the test.
check "an empty device is rejected" "no" \
    "$(part_usable "" root 0 >/dev/null && echo yes || echo no)"
check "a nonexistent device is rejected" "no" \
    "$(part_usable /dev/synapse-no-such-device root 0 >/dev/null && echo yes || echo no)"
# The rest need a real device to look at, and a CI container has none — lsblk
# comes back empty there, "/dev/$(…)" collapses to "/dev/", and the rejection
# arrives for the wrong reason ("not a block device"), which is how these went
# red on a machine with no disks. Say they were skipped rather than passing
# vacuously or asserting hardware that isn't there.
somedisk="$(lsblk -dno NAME -e7 2>/dev/null | head -1)"
somedisk="${somedisk:+/dev/$somedisk}"
if [ -n "$somedisk" ] && [ -b "$somedisk" ]; then
    check "a whole disk is rejected as a partition" "no" \
        "$(part_usable "$somedisk" root 0 >/dev/null && echo yes || echo no)"
    check "and it says why" "yes" \
        "$(case "$(part_usable "$somedisk" root 0)" in *"whole disk"*) echo yes ;; *) echo no ;; esac)"
else
    printf '  skip  whole-disk rejection (no block devices on this host)\n'
fi
mountedpart="$(lsblk -rno NAME,MOUNTPOINT -e7 2>/dev/null | awk '$2=="/"{print "/dev/"$1; exit}')"
if [ -n "$mountedpart" ]; then
    check "a mounted partition is rejected" "no" \
        "$(part_usable "$mountedpart" root 0 >/dev/null && echo yes || echo no)"
else
    printf '  skip  mounted-partition rejection (nothing mounted at / here)\n'
fi
# Size: ask for more than the device has, using a real partition as the subject.
anypart="$(lsblk -rno NAME,TYPE -e7 2>/dev/null | awk '$2=="part"{print "/dev/"$1; exit}')"
if [ -n "$anypart" ]; then
    check "a too-small partition is rejected" "no" \
        "$(part_usable "$anypart" root 999999999999999 >/dev/null && echo yes || echo no)"
else
    printf '  skip  too-small rejection (no partitions on this host)\n'
fi

# The manual path must reuse the shared filesystem helpers, not reimplement
# them: that is what keeps LUKS and the btrfs subvolume layout identical to the
# automatic paths.
manual_arm=$(awk '/^elif \[ "\$INSTALL_MODE" = "manual" \]; then$/,/^elif \[ "\$BOOT_MODE" = "uefi" \]; then$/' \
             "$here/../syn-install.sh")
for needle in luks_format_root format_and_mount_root part_usable available_part_editors; do
    check "the manual arm calls $needle" "yes" \
        "$(grep -qF -- "$needle" <<<"$manual_arm" && echo yes || echo no)"
done
check "the manual arm never runs mklabel" "no" \
    "$(grep -qF -- 'mklabel' <<<"$manual_arm" && echo yes || echo no)"

# ── swap ──
# It is offered only here, and the ordering is the part that silently breaks:
# genfstab writes a swap line for swap that is ON at that moment and nothing
# else, so a mkswap without a swapon BEFORE genfstab gives a machine that has
# swap for one session and forgets it at the next boot.
check "the manual arm offers swap" "yes" \
    "$(grep -qF -- 'Swap partition (blank for none)' <<<"$manual_arm" && echo yes || echo no)"
check "it makes and enables the swap" "yes" \
    "$(grep -qF -- 'mkswap' <<<"$manual_arm" && grep -qF -- 'swapon' <<<"$manual_arm" && echo yes || echo no)"
swapon_line=$(grep -n '^        swapon "\$PART_SWAP"' "$here/../syn-install.sh" | cut -d: -f1)
genfstab_line=$(grep -n '^genfstab -U /mnt' "$here/../syn-install.sh" | cut -d: -f1)
check "swapon comes before genfstab" "yes" \
    "$([ -n "$swapon_line" ] && [ -n "$genfstab_line" ] && [ "$swapon_line" -lt "$genfstab_line" ] \
       && echo yes || echo no)"
check "and fstab is checked for the swap line afterwards" "yes" \
    "$(in_code 'missing from the generated fstab')"
# Re-making an existing swap changes its UUID, which is what another system's
# fstab and its hibernation resume= point at. That must not be the default.
check "an existing swap is not re-made by default" "yes" \
    "$(grep -A2 'Re-make it?' <<<"$manual_arm" | grep -q '\*) FORMAT_SWAP="no"' && echo yes || echo no)"
check "cleanup turns the swap back off" "yes" \
    "$(in_code 'swapoff "$PART_SWAP"')"

# ── zram ──
# Unconditional, on every layout, because ERASE and ALONGSIDE have nowhere to
# put a partition. The package has to be installed AND the config written AND
# the result checked: a config zram-generator cannot parse makes it emit nothing
# and say nothing, so "no units" and "no zram" look identical to a working boot.
check "zram-generator is pacstrapped" "yes"      "$(in_code 'zram-generator \')"
check "the zram config is written"    "yes"      "$(in_code '/mnt/etc/systemd/zram-generator.conf')"
check "sized min(ram / 2, 8192)"      "yes"      "$(in_code 'zram-size = min(ram / 2, 8192)')"
check "compressed with zstd"          "yes"      "$(in_code 'compression-algorithm = zstd')"
# Ahead of a disk swap from fstab, which genfstab writes with no pri= (so -2).
check "zram outranks a disk swap"     "yes"      "$(in_code 'swap-priority = 100')"
check "the generator is actually run against the target" "yes" \
    "$(in_code 'ZRAM_GENERATOR_ROOT=/')"
check "and a config that yields no unit is fatal" "yes" \
    "$(in_code 'produced no swap unit from the config just written')"
check "zram-generator ships on the ISO" "yes" \
    "$(grep -qx 'zram-generator' "$here/../../archiso/packages.x86_64" && echo yes || echo no)"
# The package alone does nothing: with no config the generator reports "No
# configuration found" and emits zero units. The live root is an overlay in RAM
# running a compositor and pacstrap, which is exactly where a 4 GiB install dies.
check "the live ISO carries a zram config too" "yes" \
    "$(grep -q 'zram-size' "$here/../../archiso/airootfs/etc/systemd/zram-generator.conf" 2>/dev/null \
       && echo yes || echo no)"
check "the manual arm confirms before formatting" "yes" \
    "$(grep -qF -- "Type 'yes' to format these" <<<"$manual_arm" && echo yes || echo no)"

echo
echo "=== no runaway heredocs ==="
#
# A heredoc terminator only closes the heredoc when it is alone at column 0.
# `    EOF` does not close `<< EOF` — it is body text, and the heredoc runs on to
# the NEXT line that is exactly the terminator. That happened to the grub branch:
# an indented EOF swallowed 70 lines, including grub-install, grub-mkconfig, the
# whole `elif systemd-boot` arm and `mkdir -p /mnt/boot/loader/entries`. bash -n
# passes, because the file still parses — just not into the program anyone wrote.
# Every install from that ISO was affected: grub died on a systemd-boot check,
# and choosing systemd-boot silently installed limine.
#
# So: no line may be whitespace followed by a word used as a heredoc terminator.
terms=$(grep -oE '<<[ \t]*"?[A-Za-z_][A-Za-z0-9_]*"?' "$here/../syn-install.sh" \
        | sed 's/<<[ \t]*//; s/"//g' | sort -u)
runaway=0
for t in $terms; do
    n=$(grep -cE "^[[:space:]]+$t\$" "$here/../syn-install.sh" || true)
    [ "$n" -gt 0 ] && { runaway=$((runaway + n)); echo "        (indented '$t' x$n)"; }
done
check "no heredoc terminator is indented" "0" "$runaway"

# And the consequence, asserted directly: strip every heredoc BODY, then require
# that the commands which actually install a bootloader are still code. This is
# what went to zero while the file still looked correct in an editor.
strip_heredocs() {
    awk '
        term != "" { if ($0 == term) term=""; next }
        /^[[:space:]]*#/ { print; next }
        match($0, /<<-?[ \t]*"?[A-Za-z_][A-Za-z0-9_]*"?/) {
            w = substr($0, RSTART, RLENGTH); sub(/<<-?[ \t]*/, "", w); gsub(/"/, "", w)
            term = w; print; next
        }
        { print }
    ' "$here/../syn-install.sh"
}
codeonly=$(strip_heredocs)
for needle in 'grub-install' 'grub-mkconfig' 'bootctl' 'mkdir -p /mnt/boot/loader/entries'; do
    check "'$needle' survives as executable code" "yes" \
        "$(grep -qF -- "$needle" <<<"$codeonly" && echo yes || echo no)"
done
check "the systemd-boot arm is reachable" "yes" \
    "$(grep -qF -- 'elif [ "$BOOTLOADER" = "systemd-boot" ]' <<<"$codeonly" && echo yes || echo no)"

# Files on the ESP do not make a machine boot: UEFI boots what NVRAM points at.
# GRUB registers itself (grub-install), limine has always been registered here
# explicitly — systemd-boot was trusting bootctl, which reports failure on
# stderr and exits 0, producing a perfect ESP the firmware has never heard of.
sdb_arm=$(awk '/^    # ── systemd-boot ─/,/^    # ── limine ─/' "$here/../syn-install.sh")
check "systemd-boot's firmware entry is created if bootctl skipped it" "yes" \
    "$(grep -qF -- 'efibootmgr --create' <<<"$sdb_arm" && echo yes || echo no)"
check "and only when bootctl did not already do it" "yes" \
    "$(grep -qF -- 'Linux Boot Manager' <<<"$sdb_arm" && echo yes || echo no)"
# Both UEFI loaders that need an explicit entry must create one.
check "both explicit registrations exist" "2" \
    "$(count_code '--label "SynapseOS" --unicode')"
check "every UEFI install is checked against NVRAM afterwards" "yes" \
    "$(in_code 'No EFI boot entry names this install')"
# It must NOT be fatal: removable media is a real fallback and some firmware
# refuses NVRAM writes from a chroot.
nvram_arm=$(awk '/^# ── The firmware has to know the disk is bootable ─/,/^# Hard-verify the encrypted boot path/' \
            "$here/../syn-install.sh")
check "and reported without aborting the install" "0" \
    "$(grep -c '^[[:space:]]*die ' <<<"$nvram_arm")"

# The updater's GUI runs `syn-update check` with no controlling terminal, so
# anything it touches must already exist: creating this on first use meant a
# sudo password prompt nobody could answer.
check "syn-update's source tree is pre-created for the user" "yes" \
    "$(grep -qF -- 'install -d -o "$NEW_USER" -g "$NEW_USER" /var/lib/synapse-src' <<<"$codeonly" \
       && echo yes || echo no)"

echo
echo "=== boot entries must name files that exist ==="

# The dead "(fallback initramfs)" entry passed every install-time check on both
# systemd-boot and limine because nothing read the entry files back. This is the
# check that would have caught it, so it gets a real fake ESP rather than a grep.
esp=$(mktemp -d)
trap 'rm -rf "$esp"' EXIT
mkdir -p "$esp/loader/entries"
: > "$esp/vmlinuz-linux"
: > "$esp/initramfs-linux.img"

cat > "$esp/loader/entries/synapseos.conf" << 'ENTRY'
title   SynapseOS
linux   /vmlinuz-linux
initrd  /initramfs-linux.img
options root=UUID=x rw
ENTRY
check "a complete systemd-boot entry passes" "no" \
    "$(esp_entry_missing_file "$esp" >/dev/null && echo yes || echo no)"

# The exact entry that shipped broken.
cat > "$esp/loader/entries/synapseos-fallback.conf" << 'ENTRY'
title   SynapseOS (fallback initramfs)
linux   /vmlinuz-linux
initrd  /initramfs-linux-fallback.img
options root=UUID=x rw
ENTRY
check "a fallback entry with no image is caught" "yes" \
    "$(esp_entry_missing_file "$esp" >/dev/null && echo yes || echo no)"
check "and it names the missing file" "synapseos-fallback.conf: /initramfs-linux-fallback.img" \
    "$(esp_entry_missing_file "$esp")"
rm -f "$esp/loader/entries/synapseos-fallback.conf"

# limine's own notation, including the nested snapshot entries the sync tool
# appends — those point into limine_history and must be checked the same way.
cat > "$esp/limine.conf" << 'LIMINE'
timeout: 5

/+SynapseOS
comment: machine-id=deadbeef

    //SynapseOS
    protocol: linux
    kernel_path: boot():/vmlinuz-linux
    module_path: boot():/initramfs-linux.img
    kernel_cmdline: root=UUID=x rw
LIMINE
check "a complete limine.conf passes" "no" \
    "$(esp_entry_missing_file "$esp" >/dev/null && echo yes || echo no)"

printf '     ////SynapseOS\n     kernel_path: boot():/limine_history/vmlinuz_gone\n' >> "$esp/limine.conf"
check "a limine entry with no kernel is caught" "yes" \
    "$(esp_entry_missing_file "$esp" >/dev/null && echo yes || echo no)"
check "and it strips boot():" "limine.conf: /limine_history/vmlinuz_gone" \
    "$(esp_entry_missing_file "$esp")"

rm -rf "$esp"
trap - EXIT

echo
echo "=== structural checks on the script itself ==="

# format_and_mount_root() must not call itself. An editing slip made it do
# exactly that once, and bash -n is perfectly happy with infinite recursion —
# it would have been discovered by a disk that never got formatted.
selfcalls=$(awk '/^format_and_mount_root\(\) \{/,/^\}/' "$here/../syn-install.sh" \
            | grep -c '^[[:space:]]*format_and_mount_root[[:space:]]*$')
check "format_and_mount_root does not call itself" "0" "$selfcalls"

# Every partitioning branch that lays down a chosen filesystem must go through
# the helper, or one of them formats without creating the subvolumes and btrfs
# snapshots silently do not work. That is three: UEFI erase, BIOS erase, and the
# ADVANCED (manual) path. The alongside path is deliberately NOT one of them — it
# writes ext4 into free space beside another OS and says so in its own comment.
calls=$(grep -c '^[[:space:]]\+format_and_mount_root\b' "$here/../syn-install.sh")
check "every filesystem-choosing branch calls the helper" "3" "$calls"

# Every mkfs of a ROOT device goes through fs_mkfs_cmd. A literal mkfs.ext4 on a
# root device would ignore the user's filesystem choice entirely.
badroot=$(grep -c 'mkfs\.ext4 -F "\$ROOT_FS_DEV"' "$here/../syn-install.sh")
check "no hardcoded mkfs.ext4 on the root device" "0" "$badroot"

# ── limine.conf must stay in the shape limine-snapper-sync accepts ──
#
# A flat "/SynapseOS" entry carrying kernel_path directly boots perfectly well,
# which is why this shipped broken: limine-snapper-sync rejects it with "Your OS
# entry has no kernel in /boot/limine.conf" and then exits 0, so the installer
# saw success and the snapshot menu was simply never populated. The branch plus
# nested "//" kernel entry is what gives it something to append to.
limineconf=$(awk '/^    cat > \/mnt\/boot\/limine\.conf/{f=1;next} f&&/^EOF$/{exit} f' \
             "$here/../syn-install.sh")
check "limine.conf writes a branch entry" "1" \
    "$(printf '%s\n' "$limineconf" | grep -c '^/+SynapseOS$')"
check "limine.conf nests the kernel entry under it" "1" \
    "$(printf '%s\n' "$limineconf" | grep -cE '^[[:space:]]+//SynapseOS$')"
check "limine.conf keeps the machine-id comment" "1" \
    "$(printf '%s\n' "$limineconf" | grep -c 'comment: machine-id=')"
# Arch's linux.preset ships PRESETS=('default') only, so no fallback image is
# ever generated and an entry naming one is a menu item that cannot boot.
check "limine.conf names no fallback initramfs" "0" \
    "$(printf '%s\n' "$limineconf" | grep -c 'initramfs-linux-fallback')"

# ── snapper must be configured, not merely installed ──
#
# `snapper create-config /` cannot work here: it insists on creating
# /.snapshots, which is already the mounted @snapshots subvolume, and fails with
# errno 17. Calling it and tolerating the failure produced installs whose
# `snapper list-configs` was empty — snapshots enabled, no snapshot possible.

# ── The plan must be confirmable while it is still free to change ──
#
# The point of the review prompt is that it sits between the last question and
# the first write. A later edit that moves a prompt below it, or moves the first
# parted call above it, would leave a "review" that reviews a disk already
# repartitioned — and would look completely fine in isolation.
confirm_line=$(grep -n 'Are these correct' "$here/../syn-install.sh" | cut -d: -f1)
write_line=$(grep -nE 'parted -s "\$DISK" (mkpart|mklabel)|wipefs|mkfs\.[a-z0-9]+ .*\$PART_' \
             "$here/../syn-install.sh" | head -1 | cut -d: -f1)
check "the plan is reviewed before anything is written" "yes" \
    "$([ -n "$confirm_line" ] && [ -n "$write_line" ] && [ "$confirm_line" -lt "$write_line" ] \
       && echo yes || echo no)"
# The passphrase costs the most to retype, so it must be asked after the review.
pass_line=$(grep -n 'prompt "Encryption passphrase:"' "$here/../syn-install.sh" | cut -d: -f1)
check "the passphrase is asked after the review" "yes" \
    "$([ -n "$pass_line" ] && [ "$confirm_line" -lt "$pass_line" ] && echo yes || echo no)"
# "no" restarts the process rather than looping in place: SEPARATE_BOOT and the
# free region are derived, and a loop that re-asks the prompts while a derived
# value keeps its old meaning installs a layout nobody chose.
check "answering no re-execs instead of looping" "yes" \
    "$(in_code 'exec "$0" "$@"')"

# ── The package selection must be confirmable, and re-askable safely ──
#
# This one loops in place (pacstrap has already run, so a re-exec would
# repartition the disk), which is only safe because the defaults are inside the
# loop. If SEL_APPS/SEL_CORE initialisation ever drifts above `while`, a second
# pass inherits the first one's appends and installs a set nobody picked — and
# the first pass would still look perfect.
sel_loop=$(grep -n '^while :; do' "$here/../syn-install.sh" | tail -1 | cut -d: -f1)
# Anchored on the ASSIGNMENT, not on the app list it happens to hold today:
# pinning the literal "chibi vibe" made this check fail the moment an app was
# added, which reads as "the loop invariant broke" when nothing of the sort
# happened. The position of the assignment is the whole point of the test.
sel_defaults=$(grep -n '^    SEL_APPS="' "$here/../syn-install.sh" | head -1 | cut -d: -f1)
sel_confirm=$(grep -n 'Install this selection' "$here/../syn-install.sh" | cut -d: -f1)
check "the selection defaults are re-initialised inside the loop" "yes" \
    "$([ -n "$sel_loop" ] && [ -n "$sel_defaults" ] && [ "$sel_loop" -lt "$sel_defaults" ] \
       && echo yes || echo no)"
check "the selection is confirmed after the defaults" "yes" \
    "$([ -n "$sel_confirm" ] && [ "$sel_defaults" -lt "$sel_confirm" ] && echo yes || echo no)"
# It must NOT re-exec here: that would repartition a disk that is already
# formatted and pacstrapped.
sel_block=$(sed -n "${sel_loop},\$p" "$here/../syn-install.sh" | sed -n '1,/^done$/p')
check "the selection loop does not re-exec the installer" "0" \
    "$(printf '%s\n' "$sel_block" | grep -c 'exec "\$0"')"
check "the selection loop can be exited" "1" \
    "$(printf '%s\n' "$sel_block" | grep -cE '^[[:space:]]+break$')"

# Neither loader may name it — the systemd-boot entry had the identical defect.
check "no loader entry names the fallback initramfs" "no" \
    "$(in_code 'initramfs-linux-fallback')"
check "both loaders read their entries back" "2" \
    "$(count_code 'esp_entry_missing_file /mnt/boot')"

check "no reliance on snapper create-config" "no" \
    "$(in_code 'create-config')"
check "the root config is written from snapper's template" "yes" \
    "$(in_code 'config-templates/default /mnt/etc/snapper/configs/root')"
check "the root config is registered in SNAPPER_CONFIGS" "yes" \
    "$(in_code 'SNAPPER_CONFIGS="root"')"
check "snapper itself is asked to confirm the config" "yes" \
    "$(in_code 'snapper --no-dbus list-configs')"

# limine-snapper-sync reads /proc/self/mounts, which inside arch-chroot is the
# installer's mount table, so it can only run on the booted target.
check "limine-snapper-sync is not run in the chroot" "no" \
    "$(in_code 'arch-chroot /mnt limine-snapper-sync')"
check "a first-boot sync unit is installed instead" "yes" \
    "$(in_code 'synapseos-limine-snapshot-sync.service << ')"

echo "=== GPU inference backend swap ==="

# The bug this section exists for: `pacman -S --noconfirm synapse-llama-cuda`
# can NEVER succeed, because synapse-llama-cuda conflicts with the synapse-llama
# that synapd's dependency already pulled in, and the conflict question defaults
# to no. It shipped, and every NVIDIA install that answered "yes" to "Enable GPU
# inference?" ran on the CPU anyway.
check "no bare --noconfirm install of the CUDA build" "no" \
    "$(in_code 'pacman -S --noconfirm synapse-llama-cuda')"
check "no bare --noconfirm install of the Vulkan build" "no" \
    "$(in_code 'pacman -S --noconfirm synapse-llama-vulkan')"
check "both GPU branches go through the swap helper" "2" \
    "$(count_code 'swap_llama_backend synapse-llama-')"
# --ask is undocumented, so it may not be the only way through.
check "the swap has an --ask-free fallback path" "yes" \
    "$(in_code 'pacman -Rdd --noconfirm "$old"')"

# The table above is shape only; it cannot see whether the fallback actually
# restores anything. So drive the real function against a fake pacman whose
# conflict semantics were measured against pacman 7.1.0:
#   -S --noconfirm <gpu-pkg>  with the CPU build installed  ->  exit 1
#   -S --noconfirm --ask=4 <gpu-pkg>                        ->  swaps, exit 0
fake=$(mktemp -d /tmp/syn-llama-swap.XXXXXX)
cat > "$fake/chroot" <<'FAKE'
#!/usr/bin/env bash
# Stand-in for `arch-chroot /mnt`. State = the one installed llama package.
st="$FAKE_STATE"
prov() { case "$(cat "$st")" in synapse-llama|synapse-llama-cuda|synapse-llama-vulkan) return 0;; *) return 1;; esac; }
case "$1" in
  sh)   [ "$FAKE_SO" = "1" ] ;;
  pacman)
    shift
    args="$*"
    case "$args" in
      *-Qq*)  prov && cat "$st"; exit 0 ;;
      *-Sw*)  exit 0 ;;
      *-Rdd*) : > "$st"; exit 0 ;;
      *-S*)
        pkg="${args##* }"
        case "$args" in *--ask=4*) ask=1 ;; *) ask=0 ;; esac
        # A pacman that dropped the undocumented flag rejects the whole command.
        [ "$ask" = 1 ] && [ "$FAKE_ASK" != "1" ] && \
            { echo "error: invalid option '--ask'" >&2; exit 1; }
        # The shipped failure: something else provides llama, the conflict
        # question defaults to N, --noconfirm takes that default, abort.
        if [ "$ask" = 0 ] && prov && [ "$pkg" != "$(cat "$st")" ]; then
            echo ":: $pkg and $(cat "$st") are in conflict. Remove $(cat "$st")? [y/N]" >&2
            echo "error: unresolvable package conflicts detected" >&2
            exit 1
        fi
        # Only installing a GPU build can be made to fail; putting the CPU
        # build back is the recovery path and must not be sabotaged.
        case "$pkg" in
          *-cuda|*-vulkan) [ "$FAKE_INSTALL_OK" = "1" ] || exit 1 ;;
        esac
        echo "$pkg" > "$st"; exit 0 ;;
    esac ;;
esac
FAKE
chmod +x "$fake/chroot"
export FAKE_STATE="$fake/state"
SYN_CHROOT="$fake/chroot"

swap_case() {  # swap_case <ask> <install_ok> <so>  -> "<rc>:<installed>"
    echo synapse-llama > "$FAKE_STATE"
    FAKE_ASK="$1" FAKE_INSTALL_OK="$2" FAKE_SO="$3"
    export FAKE_ASK FAKE_INSTALL_OK FAKE_SO
    swap_llama_backend synapse-llama-cuda libggml-cuda.so >/dev/null 2>&1
    echo "$?:$(cat "$FAKE_STATE")"
}

# First prove the fake reproduces the shipped bug, or the rest proves nothing.
echo synapse-llama > "$FAKE_STATE"
FAKE_ASK=1 FAKE_INSTALL_OK=1 FAKE_SO=1; export FAKE_ASK FAKE_INSTALL_OK FAKE_SO
"$fake/chroot" pacman -S --noconfirm synapse-llama-cuda >/dev/null 2>&1
check "the old command form still fails (fake matches pacman 7.1.0)" "1" "$?"

check "--ask=4 swaps in one transaction" "0:synapse-llama-cuda" "$(swap_case 1 1 1)"
check "a pacman without --ask still swaps, via -Rdd" "0:synapse-llama-cuda" "$(swap_case 0 1 1)"
# The one that matters: the fallback removed the CPU build, then could not
# install the GPU one. Leaving nothing installed means synapd cannot load a
# model AT ALL — strictly worse than the slow inference we were fixing.
check "a failed swap puts the CPU build back" "1:synapse-llama" "$(swap_case 0 0 1)"
check "installed-but-no-.so is a failure, not a success" "1:synapse-llama-cuda" "$(swap_case 1 1 0)"
rm -rf "$fake"

echo "=== ADVANCED: one partition cannot hold two roles ==="

# Pure, so it is asserted directly. The point is not that a duplicate would
# destroy anything — every mkfs and mkswap refuses a mounted device — but that
# without this it is only caught at format time, after the confirmation and
# after the root has been made and mounted, and reported as "Failed to format".
check "a free device is not taken" "no" \
    "$(part_role_taken /dev/sda3 "the root filesystem=/dev/sda2" >/dev/null && echo yes || echo no)"
check "a repeated device is taken" "yes" \
    "$(part_role_taken /dev/sda2 "the root filesystem=/dev/sda2" >/dev/null && echo yes || echo no)"
check "and it names the role that claimed it" "the root filesystem" \
    "$(part_role_taken /dev/sda2 "the root filesystem=/dev/sda2")"
check "the first claimant wins, not the last" "the EFI partition" \
    "$(part_role_taken /dev/sda1 "the EFI partition=/dev/sda1" "/boot=/dev/sda1")"

# A role not chosen yet is an EMPTY device, and must never match. Without the
# skip, an unset PART_EFI on a BIOS install would make "" == "" and report the
# swap partition as already being the EFI partition.
check "an unchosen role never matches" "no" \
    "$(part_role_taken "" "the EFI partition=" >/dev/null && echo yes || echo no)"
check "an unchosen role does not swallow a real device" "no" \
    "$(part_role_taken /dev/sda2 "the EFI partition=" "/boot=" >/dev/null && echo yes || echo no)"

# The check is only meaningful if every later prompt names every earlier role.
# Root is asked first and so checks nothing; ESP checks root; /boot checks both;
# swap checks all three. A prompt that forgets one is a collision that still
# reaches mkfs.
manual_src=$(awk '/^elif \[ "\$INSTALL_MODE" = "manual" \]; then$/,/^elif \[ "\$BOOT_MODE" = "uefi" \]; then$/' \
             "$here/../syn-install.sh")
check "the ESP prompt checks the root" "1" \
    "$(grep -c 'part_role_taken "\$PART_EFI"' <<<"$manual_src")"
check "the /boot prompt checks the root" "yes" \
    "$(grep -A2 'part_role_taken "\$PART_BOOT"' <<<"$manual_src" \
       | grep -qF 'the root filesystem=$PART_ROOT' && echo yes || echo no)"
check "the /boot prompt checks the ESP" "yes" \
    "$(grep -A2 'part_role_taken "\$PART_BOOT"' <<<"$manual_src" \
       | grep -qF 'the EFI partition=${PART_EFI:-}' && echo yes || echo no)"
check "the swap prompt checks all three" "yes" \
    "$(grep -A3 'part_role_taken "\$_sw"' <<<"$manual_src" \
       | grep -qF 'the root filesystem=$PART_ROOT' \
       && grep -A3 'part_role_taken "\$_sw"' <<<"$manual_src" \
          | grep -qF 'the EFI partition=${PART_EFI:-}' \
       && grep -A3 'part_role_taken "\$_sw"' <<<"$manual_src" \
          | grep -qF '/boot=${PART_BOOT:-}' && echo yes || echo no)"

echo "=== language table: keymaps resolve in BOTH namespaces ==="

# LOCALE_ROWS lives past the SYN_INSTALL_SOURCE_ONLY guard, so it is read out of
# the file rather than inherited. Each row is
# label|locale|console-keymap|xkb-layout|font-package(s).
#
# The column that needs asserting is the keyboard, and it needs asserting TWICE,
# because it is two namespaces that look like one. `KEYMAP=` goes to loadkeys,
# which wants a file under /usr/share/kbd/keymaps; `xkb_layout` goes to
# xkbcommon, which wants a layout in xkeyboard-config. They agree often enough
# to seem interchangeable and disagreed on four of these fifteen rows: 'uk' and
# 'jp106' are real keymaps and not layouts ('gb', 'jp'), while 'br' and 'kr' are
# real layouts and not keymaps (br-abnt2, and kbd has no Korean map at all).
#
# Both failures are silent at install time and only show up as a keyboard that
# types the wrong characters, so nothing but a check like this catches them.
# File existence is the oracle: no daemon, no extra package, and it agrees with
# `localectl list-keymaps` and `xkbcli compile-keymap` on every row here.
locale_rows=$(awk '/^LOCALE_ROWS="$/{f=1;next} f&&/^"$/{exit} f' "$here/../syn-install.sh")
check "the language table was found" "yes" \
    "$([ -n "$locale_rows" ] && echo yes || echo no)"

# Skipped rather than passed vacuously where the data files are absent — a
# container without kbd or xkeyboard-config must not report this table as sound.
if [ -d /usr/share/kbd/keymaps ]; then
    while IFS='|' read -r label locale km xkb fonts; do
        [ -n "$label" ] || continue
        check "console keymap '$km' exists ($label)" "yes" \
            "$(find /usr/share/kbd/keymaps -name "$km.map*" -print -quit 2>/dev/null \
               | grep -q . && echo yes || echo no)"
    done <<<"$locale_rows"
else
    printf '  skip  console keymaps (kbd is not installed on this host)\n'
fi

if [ -d /usr/share/X11/xkb/symbols ]; then
    while IFS='|' read -r label locale km xkb fonts; do
        [ -n "$label" ] || continue
        check "xkb layout '$xkb' exists ($label)" "yes" \
            "$([ -f "/usr/share/X11/xkb/symbols/$xkb" ] && echo yes || echo no)"
    done <<<"$locale_rows"
else
    printf '  skip  xkb layouts (xkeyboard-config is not installed on this host)\n'
fi

# Every row must fill all five columns. A missing xkb column would make cut -f4
# return the font package and cut -f5 return nothing — the desktop layout would
# become "noto-fonts-cjk" and no font would be installed, and both halves fail
# quietly in exactly the way this section exists to prevent.
while IFS= read -r row; do
    [ -n "$row" ] || continue
    check "row '${row%%|*}' has 5 columns" "5" "$(awk -F'|' '{print NF}' <<<"$row")"
done <<<"$locale_rows"

# The two namespaces must stay SEPARATE variables all the way to the files they
# are written into. Collapsing them back to one is the original bug.
check "the console keymap is what reaches vconsole.conf" "yes" \
    "$(in_code 'echo "KEYMAP=$KEYMAP" > /mnt/etc/vconsole.conf')"
check "the xkb layout, not the keymap, reaches synuirc" "yes" \
    "$(in_code 'SYNUI_XKB="$XKB_LAYOUT"')"
check "the keymap is no longer fed straight to xkb" "no" \
    "$(in_code 'SYNUI_XKB="$KEYMAP"')"

# ── A SECOND KERNEL MUST BE BOOTABLE ON ALL THREE LOADERS ───────────────────
#
# Installing a kernel builds its initramfs from mkinitcpio's pacman hook and
# nothing else happens. Without a per-loader generator wired in, `pacman -S
# linux-lts` yields a complete, correct, UNBOOTABLE kernel while every surface
# reports success — the trap syn-settings' Kernel pane could only describe.
#
# Each loader needs a different mechanism, so each is asserted separately. This
# is structural (does the installer wire it?) rather than behavioural; only a
# real install proves the entry appears.
check "grub regenerates grub.cfg when a kernel changes" "yes" \
    "$(in_code '95-grub-mkconfig.hook')"
check "the grub hook runs grub-mkconfig" "yes" \
    "$(in_code 'Exec = /usr/bin/grub-mkconfig -o /boot/grub/grub.cfg')"
check "the grub hook triggers on kernel module trees" "yes" \
    "$(in_code 'Target = usr/lib/modules/*/vmlinuz')"

check "systemd-boot delegates new entries to kernel-install" "yes" \
    "$(in_code '95-systemd-boot-entries.hook')"
check "systemd-boot writes /etc/kernel/cmdline for it" "yes" \
    "$(in_code '/mnt/etc/kernel/cmdline')"

check "limine installs the entry generator" "yes" \
    "$(in_code 'limine-mkinitcpio-hook')"
check "limine gets an /etc/default/limine with a real cmdline" "yes" \
    "$(in_code 'KERNEL_CMDLINE[default]=')"

# THE SHIM. limine-mkinitcpio-hook puts a wrapper at /usr/local/bin/mkinitcpio
# which comes first on PATH and ends in an interactive `read -rp` when passed
# -P. pacstrap installs it long before the initramfs is generated, so calling
# `mkinitcpio` by NAME would block the installer forever on a prompt nobody is
# there to answer — or eat a keystroke meant for a later one, which this
# installer has already suffered once via pacstrap typeahead.
check "the initramfs run calls mkinitcpio by ABSOLUTE path" "yes" \
    "$(in_code 'arch-chroot /mnt /usr/bin/mkinitcpio -P')"
check "and never by bare name, which would hit the prompting shim" "no" \
    "$(in_code 'arch-chroot /mnt mkinitcpio -P')"

echo
if [ "$fails" -gt 0 ]; then
    echo "$fails check(s) FAILED"
    exit 1
fi
echo "all checks passed"
