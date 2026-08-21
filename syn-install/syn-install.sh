#!/usr/bin/env bash
# syn-install — SynapseOS disk installer
set -uo pipefail

# This SCRIPT's package version. Every SynapseOS component is 0.1.0-<pkgrel> by
# convention, so this identifies the installer and says nothing whatever about
# the release it installs — which is why the header printed "0.1.0-synapse" on
# a 0.2.8 ISO and looked like a stale image.
VERSION="0.1.0-synapse"

# What the header PRINTS: the SynapseOS release. archiso/build.sh stamps
# /etc/os-release from profiledef.sh's `iso_version` at build time (its "OS
# identity" block, which greps to prove the substitution landed), so VERSION_ID
# is the number on the media and the one worth quoting in a bug report.
#
# Read in a SUBSHELL: os-release defines VERSION itself, and sourcing it here
# would overwrite the line above. Guarded on ID, or running this script on a
# plain Arch box would print Arch's version as if it were ours; with no
# os-release at all — a bare `bash syn-install.sh` in a checkout — it falls
# back to the script version, which is at least true about the script.
RELEASE=$( . /etc/os-release 2>/dev/null
           [ "${ID:-}" = synapseos ] && printf '%s' "${VERSION_ID:-}" )
RELEASE="${RELEASE:-$VERSION}"

COLS=$(tput cols 2>/dev/null || echo 80)

# ── UI helpers ────────────────────────────────────────────
cyan()  { printf '\033[1;36m%s\033[0m' "$*"; }
green() { printf '\033[1;32m%s\033[0m' "$*"; }
red()   { printf '\033[1;31m%s\033[0m' "$*"; }
yellow(){ printf '\033[1;33m%s\033[0m' "$*"; }
bold()  { printf '\033[1m%s\033[0m' "$*"; }
line()  { printf '%*s\n' "$COLS" '' | tr ' ' '─'; }

# The dendrite mark, the same drawing as synui/data/logo.svg and the fastfetch
# mark in /usr/share/synapseos/logo.txt. Regenerate with
# `python3 archiso/mkasciilogo.py --compact --plain` and paste; it is pasted
# rather than read from /usr/share because the header draws on a live ISO, an
# installed system, and a bare `bash syn-install.sh`, and it must not go blank
# on any of them.
#
# SINGLE-quoted on purpose: the art contains backticks, and inside "..." those
# open a command substitution.
header() {
    clear
    cyan '
             oo
            `oo`
           `:oo:`
          `:+oo+:`
         .:++oo++:.
        .:+++oo+++:.
       .++ssooooss++.
     `.+soooossoooos+.`
    `ssooooos++soooooss`
   `oooooos++++++soooooo`
  `:ssssos++++++++sossss:`
'
    echo "  $(bold "SynapseOS $RELEASE Installer")"
    line
    echo ""
}

step()    { echo ""; cyan "  ▶ $*"; echo ""; }
success() { echo ""; green "  ✓ $*"; echo ""; }
fail()    { echo ""; red "  ✗ $*"; echo ""; }
warn()    { echo ""; yellow "  ⚠ $*"; echo ""; }
# Every interactive question in this script prints through here, which makes it
# the one place that can drop typeahead — and it has to.
#
# pacman-key --populate and pacstrap run for minutes with nothing to do but
# watch. A key pressed during them does not go anywhere: it waits in the
# terminal's input buffer until the next `read` consumes it. The next read after
# pacstrap is "Choice [1-4, default=2]", and a queued newline answers it
# instantly — the menu appears and is gone in the same frame, and the installer
# reports "Standard install selected" for a choice nobody made. Whatever else was
# typed then answers the prompts after it, in order.
#
# So: discard what is already queued, then ask. Only when stdin is a terminal —
# a piped or automated run has its answers on stdin legitimately and must not
# have them eaten.
prompt() {
    if [ -t 0 ]; then
        local _typeahead
        read -r -t 0.1 -N 4096 _typeahead 2>/dev/null || true
    fi
    # Show the cursor. The ISO boots `quiet splash`, and a console that came up
    # through a splash can have the text cursor turned off — so the question is
    # drawn with nothing blinking after it and there is no visible answer to
    # "where does what I type go?". That is not cosmetic: it is how a key gets
    # pressed twice, and until pick() landed a doubled key silently selected the
    # default. DECTCEM rather than `tput cnorm`, which needs a terminfo entry
    # this console may not have.
    printf '\033[?25h'
    printf "  $(bold "$1") "
}

# Unmount the target on failure so a stale /mnt doesn't block a retry — and turn
# off any swap the ADVANCED path enabled, or a retry finds the partition busy and
# the live session keeps paging to a disk the user is about to repartition.
cleanup() {
    umount -R /mnt 2>/dev/null || true
    [ -n "${PART_SWAP:-}" ] && swapoff "$PART_SWAP" 2>/dev/null || true
}
die() { fail "$*"; cleanup; exit 1; }

# ── Minimum target disk size (base system + SynapseOS + model headroom) ──
MIN_DISK_BYTES=$((8 * 1024 * 1024 * 1024))   # 8 GiB

# ── Pre-flight safety helpers ─────────────────────────────
# Connectivity: pacstrap downloads the base system, so being offline must
# fail *before* we touch the disk, not after.
have_net() {
    ping -c1 -W3 8.8.8.8 &>/dev/null || ping -c1 -W3 1.1.1.1 &>/dev/null
}

# Disks that must never be wiped: whatever backs the running live system
# (the boot media, the root fs, the archiso cow/airootfs mounts). Echoes
# one /dev/<disk> per line.
live_disks() {
    local mnt src disk
    for mnt in / /run/archiso/bootmnt /run/archiso/airootfs /run/archiso/cowspace /boot; do
        src=$(findmnt -no SOURCE --target "$mnt" 2>/dev/null) || continue
        [ -n "$src" ] || continue
        case "$src" in /dev/*) ;; *) continue ;; esac   # skip overlay/tmpfs
        # Map a partition back to its parent disk; if src is already a disk,
        # PKNAME is empty and we use src itself.
        disk=$(lsblk -no PKNAME "$src" 2>/dev/null | head -1)
        if [ -n "$disk" ]; then
            echo "/dev/$disk"
        else
            echo "$src"
        fi
    done | sort -u
}

is_live_disk() {
    local target="$1" d
    while read -r d; do
        [ -n "$d" ] && [ "$d" = "$target" ] && return 0
    done < <(live_disks)
    return 1
}

# Total size of a whole disk in bytes (empty if unknown).
disk_size_bytes() { lsblk -bdno SIZE "$1" 2>/dev/null | head -1; }

# True (0) if any partition of the disk is currently mounted.
disk_busy() {
    lsblk -nro MOUNTPOINT "$1" 2>/dev/null | grep -q .
}

# Partition device node for <disk> <num>, handling the 'p' infix on nvme/mmc
# (nvme0n1p2, mmcblk0p2) vs plain (sda2): the kernel inserts a 'p' when the
# disk name ends in a digit.
part_name() {
    local disk="$1" num="$2"
    if [[ "$disk" =~ [0-9]$ ]]; then echo "${disk}p${num}"; else echo "${disk}${num}"; fi
}

# The EFI System Partition type GUID — matched directly (not via a parted flag)
# so ESP detection is reliable regardless of which tool wrote the table.
ESP_TYPE_GUID="c12a7328-f81f-11d2-ba4b-00a0c93ec93b"

# Echo the disk's existing ESP (/dev/...), or nothing. The dual-boot path must
# REUSE this partition — it holds Windows'/other OSes' boot files and must never
# be reformatted.
find_esp() {
    local disk="$1" name ptype
    while read -r name ptype; do
        [ -n "$name" ] || continue
        [ "${ptype,,}" = "$ESP_TYPE_GUID" ] && { echo "/dev/$name"; return 0; }
    done < <(lsblk -rno NAME,PARTTYPE "$disk" 2>/dev/null)
    return 1
}

# Largest unallocated region on the disk as "START END SIZE" in whole MiB
# (empty if none). Read from parted's "Free Space" rows, which are stable across
# the parted versions shipped on the ISO. Used to place the alongside root.
largest_free_region() {
    parted -s "$1" unit MiB print free 2>/dev/null | awk '
        /Free Space/ {
            s=$1; e=$2; z=$3; gsub(/MiB/,"",s); gsub(/MiB/,"",e); gsub(/MiB/,"",z);
            if (z+0 > best+0) { best=z; bs=s; be=e }
        }
        END { if (best+0 > 0) printf "%d %d %d\n", bs+0, be+0, best+0 }'
}

# ── Must be root ──────────────────────────────────────────
# ── Filesystem and bootloader capability table ────────────
#
# Every one of these is a PURE function of its arguments — no globals, no side
# effects — because the interesting risk here is combinatorial, not procedural.
# Four filesystems by four bootloaders by encrypted-or-not by UEFI-or-BIOS is
# sixty-four combinations, and the ones that go wrong go wrong at FIRST BOOT, on
# someone else's machine, after the installer has already reported success.
# Pure functions can be asserted against a table in tests/layout_test.sh; the
# procedural install path cannot.
#
# WHAT IS AND IS NOT A VALID OPTION was measured, not assumed:
#
#   fs        kernel mounts it?   GRUB reads it?   verdict
#   ext4      builtin             ext2.mod         default, works everywhere
#   btrfs     builtin             btrfs.mod        subvolumes + snapshots
#   xfs       module              xfs.mod          no snapshots, cannot shrink
#   f2fs      module              f2fs.mod         flash-oriented
#   reiserfs  REMOVED from kernel  —               not offerable
#   bcachefs  not in this kernel  no module        fails both axes
#   zfs       not in this kernel  zfs.mod          needs archzfs: 3rd-party DKMS
#
# jfs and nilfs2 are technically available on both axes and are deliberately NOT
# offered: nobody should put a root filesystem on either in 2026, and a choice
# that is a trap is worse than no choice.

# Filesystems this installer can create. Order is the order they are offered.
SYN_FILESYSTEMS="ext4 btrfs xfs f2fs"

# Bootloaders. grub is the default and the only one that covers BIOS, Secure
# Boot as already built here, os-prober dual-boot detection AND bootable
# snapshots. See bootloader_supported() for the gating.
SYN_BOOTLOADERS="grub systemd-boot limine"

# mkfs invocation for a filesystem. Echoes the command; the caller runs it.
fs_mkfs_cmd() {
    case "$1" in
        ext4)  echo "mkfs.ext4 -F" ;;
        btrfs) echo "mkfs.btrfs -f" ;;
        # -f twice is not a typo: the first is mkfs.xfs's force, and -m
        # bigtime=1,inobtcount=1 are the defaults on current xfsprogs anyway —
        # stated explicitly so a future xfsprogs default change is visible here
        # rather than silently altering what we write to people's disks.
        xfs)   echo "mkfs.xfs -f" ;;
        f2fs)  echo "mkfs.f2fs -f" ;;
        *)     return 1 ;;
    esac
}

# Packages the TARGET needs for a root on this filesystem.
#
# This is not optional and it is not the same as what the ISO needs. `base`
# pulls in e2fsprogs and nothing else, so an xfs root on an installed system has
# no mkfs.xfs, no xfs_repair and no fsck.xfs — the filesystem cannot be checked
# or repaired on the one machine that most needs to. mkinitcpio's fsck hook also
# silently skips a filesystem whose fsck binary is missing.
fs_target_pkgs() {
    case "$1" in
        ext4)  echo "e2fsprogs" ;;
        btrfs) echo "btrfs-progs" ;;
        xfs)   echo "xfsprogs" ;;
        f2fs)  echo "f2fs-tools" ;;
        *)     return 1 ;;
    esac
}

# Default mount options written into fstab for the root filesystem.
fs_mount_opts() {
    case "$1" in
        # zstd:3 is a real win on a ~7 GB install and costs little CPU; noatime
        # keeps snapshots from churning on every read.
        btrfs) echo "noatime,compress=zstd:3,space_cache=v2" ;;
        ext4)  echo "noatime" ;;
        xfs)   echo "noatime" ;;
        f2fs)  echo "noatime,compress_algorithm=zstd" ;;
        *)     return 1 ;;
    esac
}

# Can this filesystem carry bootable snapshots?
fs_supports_snapshots() {
    [ "$1" = "btrfs" ]
}

# Is this bootloader usable in this firmware mode?
#
# systemd-boot is UEFI-only by construction — it is an EFI stub loader and there
# is no BIOS build of it. Offering it on a BIOS machine and failing at
# bootctl-install time would be a menu entry that lies.
bootloader_supported() {
    case "$1" in
        grub)         return 0 ;;                      # BIOS and UEFI
        systemd-boot) [ "$2" = "uefi" ] ;;
        # limine handles BIOS and UEFI, but only the UEFI path is implemented
        # here — the bootloader prompt is UEFI-only, so BIOS never reaches it.
        limine)       [ "$2" = "uefi" ] ;;
        refind)       [ "$2" = "uefi" ] ;;             # not offered yet
        *)            return 1 ;;
    esac
}

# Can this bootloader boot a btrfs snapshot?
#
# GRUB only, and only because grub-btrfs is in the official repos. limine's
# equivalent (limine-snapper-sync) is AUR, and systemd-boot has no equivalent at
# all. The installer warns rather than silently dropping the feature.
bootloader_supports_snapshots() {
    case "$1" in
        grub)   return 0 ;;   # grub-btrfs, official repos
        limine) return 0 ;;   # limine-snapper-sync, vendored in-tree
        *)      return 1 ;;
    esac
}

# Does this combination need /boot on its OWN plain ext4 partition?
#
# Three separate reasons converge here, which is exactly why it is one function
# and not three scattered conditionals:
#
#   1. GRUB + encryption. GRUB can only open a LUKS2 volume that uses PBKDF2,
#      not the argon2id cryptsetup defaults to — and argon2id is most of why
#      LUKS2 is worth having. A plain /boot keeps the initramfs the only thing
#      that unlocks anything.
#
#   2. systemd-boot needs NO separate /boot even when encrypted, which is a
#      genuine improvement rather than a preference: kernels live on the ESP and
#      the initramfs does the unlocking, so the argon2id compromise disappears.
#
#   3. xfs and f2fs. Both are readable by the GRUB we ship, but mkfs.xfs enables
#      on-disk features as xfsprogs advances and GRUB's f2fs support is thin —
#      a plain ext4 /boot sidesteps the entire question of whether this year's
#      mkfs wrote something this year's GRUB cannot read.
#
# btrfs deliberately keeps /boot INSIDE the root: a rollback that restores
# /usr/lib/modules but not the kernel that loads them is a mismatched pair.
# Where the EFI System Partition gets mounted.
#
# GRUB keeps the ESP at /boot/efi and the kernels on /boot — a real filesystem
# it can read. systemd-boot has no filesystem drivers at all: it reads the ESP
# and nothing else, so the kernels have to BE on the ESP, which means mounting
# it at /boot directly. This is why the two cannot share a partition layout.
layout_esp_mount() {
    case "$1" in
        # limine's own FAQ is explicit that it expects the kernel and modules on
        # a FAT32 partition rather than teaching the bootloader every
        # filesystem, and limine-snapper-sync copies snapshot kernels there too.
        # So, like systemd-boot, the ESP IS /boot.
        systemd-boot|limine) echo "/boot" ;;
        *)                   echo "/boot/efi" ;;
    esac
}

# How big to make the ESP, in MiB.
#
# 512 is plenty for GRUB, which puts one small binary there. systemd-boot has
# every installed kernel and initramfs on it — roughly 150 MiB per kernel with
# its fallback image — so 512 runs out on the second kernel, and running out
# looks like a pacman transaction failing halfway through an upgrade.
layout_esp_size_mib() {
    case "$1" in
        systemd-boot) echo 1024 ;;
        # limine with snapshots is the greedy case: limine-snapper-sync copies
        # EVERY retained snapshot's kernel and initramfs onto the ESP, ~150 MB
        # each, and simply stops adding entries once the partition passes 85%
        # full. A 1 GiB ESP therefore holds about four snapshots before the
        # feature quietly stops working, with nothing to explain why. 4 GiB is
        # the difference between a snapshot menu and a disappointment.
        limine)       [ "${2:-no}" = "yes" ] && echo 4096 || echo 1024 ;;
        *)            echo 512 ;;
    esac
}

layout_separate_boot() {
    local fs="$1" loader="$2" encrypt="$3"

    # systemd-boot and limine both keep the kernels ON the ESP, which is FAT32
    # and unencrypted by necessity — so the ESP already plays the role a plain
    # /boot plays for GRUB, and a second unencrypted partition would be pure
    # duplication. This is what lets an encrypted install keep argon2id: nothing
    # but the initramfs ever has to open the LUKS volume.
    case "$loader" in
        systemd-boot|limine) echo "no"; return 0 ;;
    esac
    if [ "$encrypt" = "yes" ]; then
        echo "yes"; return 0
    fi
    case "$fs" in
        xfs|f2fs) echo "yes" ;;
        *)        echo "no" ;;
    esac
}


# Read the boot entries back and report the first file they name that is not
# actually on the ESP. Prints "<entry>: <path>" and returns 0 when something is
# missing, returns 1 when every entry checks out — the shape a caller wants for
# `msg=$(...) && die`.
#
# WHY THIS EXISTS: the per-loader checks only ever looked for the ONE kernel and
# initramfs the default entry uses, so a second entry naming a file that is never
# generated passed every check and failed only at boot. That is precisely what
# the "(fallback initramfs)" entry did on both systemd-boot and limine, for as
# long as those loaders have been offered: Arch's linux.preset ships
# PRESETS=('default'), so initramfs-linux-fallback.img does not exist.
#
# Both formats name paths relative to the ESP — systemd-boot as bare "linux" and
# "initrd" keys, limine as "kernel_path:"/"module_path:" behind its boot():
# notation for "the partition this config is on".
# The partition editors this image can offer, best first. cfdisk is the one that
# is always there (util-linux, in base) and the only one a first-time user can
# drive without reading a manual; the rest are listed because someone who asked
# for ADVANCED may well want fdisk's expert commands or parted's resize. gparted
# is included for completeness and simply will not appear on an image without
# it — the ISO does not ship it today.
SYN_PART_EDITORS="cfdisk cgdisk fdisk gdisk parted gparted"

# Which of them exist here, in that order. Separate from the prompt so the test
# suite can assert the list without a disk.
available_part_editors() {
    local e out=""
    for e in $SYN_PART_EDITORS; do
        command -v "$e" >/dev/null 2>&1 && out="$out $e"
    done
    echo "${out# }"
}

# Is <dev> usable as the partition for <role>? Echoes the reason it is not and
# returns 1; silent and returns 0 when it is fine.
#
# This is the whole safety net of the ADVANCED path. The automatic paths know
# what they created; here the user types a device name, and the ways that goes
# wrong are all quiet: a typo naming the live USB, the disk instead of the
# partition, something already mounted, a root too small to hold the system.
part_usable() {   # part_usable <dev> <role> [min-bytes]
    local dev="$1" role="$2" min="${3:-0}" sz
    [ -n "$dev" ]            || { echo "no device given for $role"; return 1; }
    [ -b "$dev" ]            || { echo "$dev is not a block device"; return 1; }
    case "$(lsblk -dno TYPE "$dev" 2>/dev/null)" in
        part|crypt|lvm|raid1|raid0) ;;
        disk) echo "$dev is a whole disk, not a partition — partition it first"; return 1 ;;
        *)    echo "$dev is not a partition"; return 1 ;;
    esac
    if lsblk -nro MOUNTPOINT "$dev" 2>/dev/null | grep -q .; then
        echo "$dev is mounted — unmount it first"; return 1
    fi
    if is_live_disk "$(lsblk -no PKNAME "$dev" 2>/dev/null | head -1 | sed 's|^|/dev/|')"; then
        echo "$dev is on the live/boot device — that is the installer's own media"; return 1
    fi
    sz="$(lsblk -bdno SIZE "$dev" 2>/dev/null)"
    if [ "$min" -gt 0 ] && [[ "$sz" =~ ^[0-9]+$ ]] && [ "$sz" -lt "$min" ]; then
        echo "$dev is $((sz / 1024 / 1024)) MiB — $role needs at least $((min / 1024 / 1024)) MiB"
        return 1
    fi
    return 0
}

# Is <dev> already spoken for by another role? Echoes the role that claimed it
# and returns 0; silent and returns 1 when it is free.
#
# This is NOT a guardrail against something an advanced user might have meant.
# One partition cannot be both a root and a swap, so a repeated device is always
# a typo — there is no answer to honour.
#
# It is worth catching because of WHEN the collision otherwise surfaces, not
# whether. Every tool involved does refuse a mounted device ("%s is mounted;
# will not make a %s here!" from mkfs.ext4, "will not make swapspace" from
# mkswap, and the fat/btrfs equivalents), so nothing is ever corrupted. But the
# formatting order is ESP, root, /boot, swap — so a duplicate is discovered
# AFTER the "Type 'yes' to format these" confirmation and AFTER the root has
# been made and mounted, and it is reported as "Failed to format /dev/sdaN",
# which names the symptom and not the cause. Asked at the prompt it costs a
# retype; asked at format time it costs the root partition.
part_role_taken() {   # part_role_taken <dev> <role=dev>...
    local dev="$1" pair
    shift
    for pair in "$@"; do
        [ -n "${pair#*=}" ] || continue          # role not chosen (yet)
        [ "${pair#*=}" = "$dev" ] && { printf '%s' "${pair%%=*}"; return 0; }
    done
    return 1
}

esp_entry_missing_file() {   # esp_entry_missing_file <esp-root>
    local esp="$1" ent key val rest p

    for ent in "$esp"/loader/entries/*.conf; do
        [ -f "$ent" ] || continue
        while read -r key val rest; do
            case "$key" in
                linux|initrd)
                    [ -f "$esp/$val" ] || { echo "$(basename "$ent"): $val"; return 0; } ;;
            esac
        done < "$ent"
    done

    if [ -f "$esp/limine.conf" ]; then
        while read -r key val rest; do
            case "$key" in
                kernel_path:|module_path:|path:) ;;
                *) continue ;;
            esac

            # Only ESP-relative paths are ours to check. limine also accepts
            # uri()/tftp()/other roots, and a `path:` on a non-boot-entry line
            # (a background, a module on another device) is not a file this
            # verification knows how to locate. Skip anything without boot():.
            case "$val" in
                boot\(\):*) p="${val#boot():}" ;;
                *) continue ;;
            esac

            # ⚠ STRIP limine's INTEGRITY PIN. limine-entry-tool — installed just
            # above, and again by every future kernel's mkinitcpio hook — does
            # not point at /boot/vmlinuz-linux. It boots private per-kernel
            # copies under /boot/<machine-id>/<kernel>/ and pins each one in
            # limine.conf with a BLAKE2B-512 hash written as `path#<hash>`:
            #
            #   module_path: boot():/<machine-id>/linux/initramfs#08c20b3a…
            #
            # The `#<hash>` is limine SYNTAX, not part of the filename — the file
            # on disk is `…/linux/initramfs`. Without this strip, [ -f ] tests a
            # path that ends in a 128-hex-digit suffix and can NEVER exist, so
            # this check died on every install that reached the hook (the report
            # was "a limine entry names a file that is not on the ESP" over a
            # perfectly bootable system). See reference_limine_entry_dir_goes_stale.
            p="${p%%#*}"

            [ -f "$esp/$p" ] || { echo "limine.conf: $p"; return 0; }
        done < "$esp/limine.conf"
    fi

    return 1
}


# ── Swap the CPU llama build for a GPU one ────────────────
#
# synapse-llama-{cuda,vulkan} declare provides+conflicts on synapse-llama, so
# putting inference on the GPU is a REPLACEMENT, not an install. That
# distinction is a bug that shipped:
#
#   # pacman -S --noconfirm synapse-llama-cuda
#   :: synapse-llama-cuda and synapse-llama are in conflict. Remove synapse-llama? [y/N]
#   error: unresolvable package conflicts detected
#
# The conflict question defaults to NO, and --noconfirm answers every question
# with its default. So that command could never succeed while the CPU build was
# installed — and it always is, because synapd depends on synapse-llama and
# pacstrap resolved it minutes earlier. An NVIDIA install answered "Enable GPU
# inference?" with yes, printed a warning nobody saw under a wall of pacman
# output, and then ran the entire reason the distro exists on the CPU. Which is
# the *precise* failure the surrounding comments swore had been fixed: the code
# was written to prevent a silent CPU fallback and then fell back silently.
#
# Two ways through, in order of preference:
#
#  1. --ask=4. ALPM_QUESTION_CONFLICT_PKG is (1 << 2) in alpm.h, so this
#     auto-answers YES to the conflict question and to nothing else. It is one
#     transaction — the old package is removed only once the new one is staged.
#     But --ask is in neither `pacman --help` nor pacman(8): it works on pacman
#     7.1.0 and is not promised to keep working, so it cannot be the only path.
#  2. Explicit -Rdd then -S. Fully documented and will not rot, but there is a
#     window with NO llama installed, so a failure inside it must put the old
#     package back — a box where synapd cannot load a model at all is worse than
#     one where it loads slowly. Hence second, not first.
#
# Downloads happen up front, while the working CPU build is still installed, so
# the risky window in (2) contains no network.
#
# Returns 0 only if the backend's .so is really there afterwards. Never make
# this trust pacman's exit status alone: "reported success, offloaded nothing"
# is the failure mode this whole path exists to catch.
SYN_CHROOT="${SYN_CHROOT:-arch-chroot /mnt}"

swap_llama_backend() {   # swap_llama_backend <pkg> <so-name>
    local pkg="$1" so="$2" old restored=""

    # Whichever synapse-llama provider is installed now is what has to come
    # back if the swap dies halfway. -Q resolves provides, so this names the
    # real package (synapse-llama, or a GPU build on a re-run).
    old=$($SYN_CHROOT pacman -Qq synapse-llama 2>/dev/null | head -1)

    # Fetch the target and its dependencies (cuda is ~4.7 GiB) while the
    # working build is still in place and nothing is half-removed.
    $SYN_CHROOT pacman -Sw --noconfirm "$pkg" >/dev/null 2>&1 || true

    if ! $SYN_CHROOT pacman -S --noconfirm --ask=4 "$pkg"; then
        if [ -n "$old" ] && [ "$old" != "$pkg" ]; then
            warn "One-step swap to $pkg failed; removing $old and retrying."
            $SYN_CHROOT pacman -Rdd --noconfirm "$old" >/dev/null 2>&1
            if ! $SYN_CHROOT pacman -S --noconfirm "$pkg"; then
                $SYN_CHROOT pacman -S --noconfirm "$old" >/dev/null 2>&1 \
                    && restored="
  ($old put back — inference will work, on the CPU.)"
                warn "Could not install $pkg — synapd will run on the CPU.$restored
  Retry later with: sudo pacman -S $pkg"
                return 1
            fi
        else
            warn "Could not install $pkg — synapd will run on the CPU.
  Retry later with: sudo pacman -S $pkg"
            return 1
        fi
    fi

    if ! $SYN_CHROOT sh -c "[ -e /usr/lib/$so ]"; then
        warn "$pkg installed but $so is missing — synapd will run on the
  CPU. Report this."
        return 1
    fi

    success "GPU inference enabled ($pkg)"
    return 0
}

# ── Answer files (--config) ───────────────────────────────
#
# An install profile: every question this script asks, answered up front, so an
# install is reproducible instead of re-typed. Anything the file does NOT answer
# is still asked, which is what makes a partial profile useful — pin the disk
# layout and the package set, decide the hostname at the machine.
#
# TWO RULES SHAPE THE WHOLE THING.
#
# 1. Keys are SEMANTIC, never menu positions. A file saying `filesystem=2` is
#    unreadable, and it silently means something else the day a menu grows an
#    entry. So the config says `filesystem=btrfs` and `answer` maps that onto
#    the number the existing `case` already handles — not one case statement in
#    this script was rewritten for this.
#
# 2. A KEY THAT DOES NOTHING MUST SAY SO. That is how preseeding usually fails:
#    `bootlaoder=limine` is not an error, it is silence, and you find out when
#    the machine boots the wrong thing. Every consumed key is recorded, and
#    config_report_unused() at the end names the ones that were never read.
declare -A ANSWERS=()        # key -> value, from the config file
declare -A ANSWERS_USED=()   # key -> 1 once some answer() has consumed it
CONFIG_FILE=""

# Render a .nix profile to key=value lines. Needs nix, which the installed
# system has only if WANT_NIX was taken and the live ISO has only if it ships
# it — so this fails with an instruction, not a stack trace.
# NOTHING HERE MAY CALL die(). This runs inside $( ), where `exit` leaves only
# the subshell — the message would be captured as config text and parsed as
# answers, which is how the first cut of this reported "not a key=value line"
# quoting its own error. Failures go to stderr and come back as a status.
config_render_nix() {
    local f=$1 render=/usr/share/syn/nix/render.nix
    # Packaged path first; then the checkout, so `bash syn-install.sh --config
    # x.nix` works straight out of the repo. BASH_SOURCE, not $0: the test seam
    # sources this file, and $0 is then the shell.
    [ -f "$render" ] ||
        render="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../syn/nix-render.nix"

    if ! command -v nix-instantiate >/dev/null 2>&1; then
        fail "$f is a Nix profile, but nix is not installed here." >&2
        echo "  Render it on a machine that has nix and pass the result:" >&2
        echo "      syn nix profile $f > install.conf" >&2
        echo "      syn-install --config install.conf" >&2
        return 1
    fi
    if [ ! -f "$render" ]; then
        fail "render.nix is missing — the 'syn' package is not installed here." >&2
        return 1
    fi
    nix-instantiate --eval --strict --raw \
        --argstr profile "$(readlink -f "$f")" "$render" || return 1
}

config_load() {
    local f=$1 body line key val
    [ -f "$f" ] || die "config file not found: $f"

    # Passwords legitimately live in these files, so a world-readable one is
    # worth a word. Not fatal: an unattended install off a read-only ISO has
    # nowhere better to put it, and refusing would just push people to
    # chmod and forget.
    case "$f" in
        *.nix) body=$(config_render_nix "$f") ||
                   die "$f did not render — see above, or run 'syn nix profile $f'." ;;
        *)     body=$(cat "$f") ;;
    esac
    if [ -n "$(find "$f" -perm /0044 2>/dev/null)" ] &&
       grep -qE '^[[:space:]]*(password|luks_passphrase)[[:space:]]*=' <<<"$body"; then
        warn "$f holds a password and is readable by other users. chmod 600 it."
    fi

    while IFS= read -r line; do
        line="${line%%#*}"                       # trailing comments
        line="${line#"${line%%[![:space:]]*}"}"  # leading space
        [ -n "$line" ] || continue
        case "$line" in *=*) ;; *) die "config: not a key=value line: $line" ;; esac
        key="${line%%=*}"; val="${line#*=}"
        key="${key//[[:space:]]/}"
        val="${val#"${val%%[![:space:]]*}"}"     # leading space only — a
        val="${val%"${val##*[![:space:]]}"}"     # trailing space too
        # Quotes are optional and stripped, so a value with spaces (a full
        # name) can be written either way.
        case "$val" in
            \"*\") val="${val:1:${#val}-2}" ;;
            \'*\') val="${val:1:${#val}-2}" ;;
        esac
        ANSWERS[$key]="$val"
    done <<<"$body"

    CONFIG_FILE="$f"
}

# answer <key> <var> [-s] [--ack] [-m sym=raw,sym=raw]
#
# Drop-in for `read -r <var>`: uses the config when it has the key, asks
# otherwise. Keeps printing what it chose, so an unattended transcript reads
# like an attended one and a wrong answer is visible in the log.
answer() {
    local __key=$1 __var=$2; shift 2
    local __secret=0 __ack=0 __map="" __v __pair
    while [ $# -gt 0 ]; do
        case "$1" in
            -s)    __secret=1 ;;
            --ack) __ack=1 ;;
            -m)    __map="$2"; shift ;;
        esac
        shift
    done

    if [ -n "${ANSWERS[$__key]+set}" ]; then
        __v="${ANSWERS[$__key]}"
        ANSWERS_USED[$__key]=1
        # Symbolic -> whatever the case statement below expects. An answer
        # that is not in the map passes through untouched, so a raw menu
        # number still works and a free-text key needs no map at all.
        if [ -n "$__map" ]; then
            local __pairs; IFS=, read -ra __pairs <<<"$__map"
            for __pair in "${__pairs[@]}"; do
                if [ "${__pair%%=*}" = "${__v,,}" ]; then __v="${__pair#*=}"; break; fi
            done
        fi
        printf -v "$__var" '%s' "$__v"
        if [ "$__secret" = 1 ]; then echo "******** (--config)"; else echo "$__v  (--config)"; fi
        return 0
    fi

    # A bare acknowledgement — "press ENTER to continue". It carries no
    # information, so once a profile has been supplied there is nobody there to
    # press it. Destructive confirmations are NOT this: they take a real key.
    if [ "$__ack" = 1 ] && [ -n "$CONFIG_FILE" ]; then
        echo "(--config)"
        printf -v "$__var" '%s' ""
        return 0
    fi

    if [ "$__secret" = 1 ]; then read -rs "$__var"; else read -r "$__var"; fi
}

# pick <question> <key> <var> <default> <valid> [-m sym=raw,...]
#
# A numbered menu that REFUSES an answer that is not on it.
#
# Every menu in this script used to be `answer` followed by a case whose `*)`
# arm was the default, which means a typo is indistinguishable from pressing
# Enter: "33" — one keystroke repeated because the cursor was not visible —
# selected the default preset and the installer announced it as a choice. A
# wrong answer that LOOKS accepted is the worst outcome available here, because
# the next thing it does is partition a disk.
#
# Empty still means the default; anything else must be on the list or the
# question is asked again. <valid> is the space-separated set of RAW values,
# i.e. what the case statement below the call already matches on.
pick() {
    local __q=$1 __key=$2 __var=$3 __def=$4 __valid=$5; shift 5
    local __ans __v
    while :; do
        prompt "$__q"
        answer "$__key" __ans "$@" || true
        __ans="${__ans//[[:space:]]/}"
        __v="${__ans:-$__def}"
        case " $__valid " in
            *" $__v "*) printf -v "$__var" '%s' "$__v"; return 0 ;;
        esac
        # An unattended run has nobody to ask again — a loop here would spin
        # forever on a bad key. Name the key and the answers it accepts.
        [ -z "${ANSWERS[$__key]+set}" ] ||
            die "config: $__key=$__ans is not a valid answer (expected one of: $__valid)"
        fail "\"$__ans\" is not one of: $__valid"
    done
}

# ── Records, for a front end ──────────────────────────────
#
# `syn-install --list-disks` prints one TAB-separated record per whole disk:
#
#   dev  bytes  size  model  live  usable  reason
#
# It exists so the graphical installer renders a decision this script has
# already made rather than making its own. A GUI that runs its own lsblk and
# its own "is this the live USB" test is a second implementation of the only
# rule here that destroys data if it is wrong — and it would be the copy that
# nobody's test suite covers. live=1 marks the installer's own media.
#
# Above the test seam, so it is reachable from the tests, and printing only:
# nothing here touches a disk.
list_disks() {
    local dev bytes model live usable reason
    while read -r dev; do
        [ -n "$dev" ] || continue
        # zram is TYPE=disk to lsblk and is the compressed swap device the live
        # session is running on; ram/md are not install targets either. Offering
        # any of them is offering to install onto memory.
        case "$dev" in /dev/zram*|/dev/ram*|/dev/md*) continue ;; esac
        bytes=$(lsblk -bdno SIZE "$dev" 2>/dev/null | head -1)
        [ -n "$bytes" ] || continue
        model=$(lsblk -dno MODEL "$dev" 2>/dev/null | head -1)
        model="${model#"${model%%[![:space:]]*}"}"
        model="${model%"${model##*[![:space:]]}"}"
        live=0; usable=1; reason=""
        if is_live_disk "$dev"; then
            live=1; usable=0; reason="the installer's own media"
        elif [ "$bytes" -lt "$MIN_DISK_BYTES" ]; then
            usable=0
            reason="$((bytes / 1024 / 1024 / 1024)) GiB — SynapseOS needs at least $((MIN_DISK_BYTES / 1024 / 1024 / 1024)) GiB"
        fi
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$dev" "$bytes" "$(numfmt --to=iec --suffix=B "$bytes" 2>/dev/null || echo "$bytes")" \
            "${model:-unknown}" "$live" "$usable" "$reason"
    done < <(lsblk -dpno NAME,TYPE 2>/dev/null | awk '$2=="disk"{print $1}')
}

# ── The region tables, and the records a picker renders ────
#
# LOCALE_ROWS is deliberately short. A picker listing all 500 of glibc's locales
# is not more international, it is unusable — these cover the large majority,
# and Other takes any locale glibc has.
#
# Each row: label|locale|console-keymap|xkb-layout|font-package(s). The font
# column is the "language pack" part: noto-fonts covers Latin/Greek/Cyrillic and
# ships as a base, CJK needs noto-fonts-cjk (~130MB, which is why it is not
# simply always installed), and noto-fonts-extra carries the Indic/Arabic/Hebrew
# coverage.
#
# THE KEYBOARD IS TWO COLUMNS BECAUSE IT IS TWO NAMESPACES, and it used to be
# one. `KEYMAP=` in vconsole.conf names a file under /usr/share/kbd/keymaps that
# loadkeys must find; `xkb_layout` in synuirc names a layout in
# xkeyboard-config's rules that xkbcommon must compile. They overlap enough to
# look like the same thing and disagree on four of the fifteen rows below:
#
#   row                 was      console        XKB
#   English (UK)        uk       uk.map.gz ok   NO 'uk' layout — it is 'gb'
#   Japanese            jp106    jp106.map.gz   'jp106' is a MODEL, layout is 'jp'
#   Português (Brasil)  br       NO br.map.gz   'br' ok  (console is br-abnt2)
#   Korean              kr       NO kr.map.gz   'kr' ok  (kbd has no Korean map)
#
# Measured, not guessed: `xkbcli compile-keymap --layout uk` and `--layout jp106`
# both exit 1 and produce nothing, and neither br.map.gz nor kr.map.gz exists in
# kbd at all. Both halves fail SILENTLY — synui logs "failed to compile — using
# default" to a log nobody reads and hands the user a US desktop layout, and
# systemd-vconsole-setup fails and leaves the VT on its built-in US map. So a UK
# or Japanese user got the console they asked for and a US desktop, and a
# Brazilian or Korean user got the reverse. Which is the same "asked and then
# ignored" failure the region step swears it exists to prevent.
#
# tests/layout_test.sh asserts every cell against both namespaces.
#
# THE LABELS ARE ASCII ON PURPOSE. This menu is drawn on the Linux VT, whose
# font is ter-116n — the Latin-1 Terminus. Cyrillic came out as lookalike
# rubbish and every CJK/Devanagari/Arabic name came out as a row of boxes, so
# the entries that most needed to be readable were the only unreadable ones.
# A Unicode console font does not fix it either: a VT font holds at most 512
# glyphs, which cannot cover CJK by two orders of magnitude. The native name is
# a nice touch this screen cannot cash — the locale code carries the meaning.
#
# The table lives HERE, above the test seam, because the graphical installer
# renders it too: `--list-locales` is answered during argument parsing, and a
# front end with its own copy of these fifteen rows would be a second table to
# keep right — with the font column, the one that decides whether a Japanese
# install can draw Japanese, in the copy no test suite reads.
LOCALE_ROWS="
English (US)|en_US.UTF-8|us|us|
English (UK)|en_GB.UTF-8|uk|gb|
Deutsch|de_DE.UTF-8|de|de|
Français|fr_FR.UTF-8|fr|fr|
Español|es_ES.UTF-8|es|es|
Português (Brasil)|pt_BR.UTF-8|br-abnt2|br|
Italiano|it_IT.UTF-8|it|it|
Nederlands|nl_NL.UTF-8|nl|nl|
Polski|pl_PL.UTF-8|pl|pl|
Russian|ru_RU.UTF-8|ru|ru|
Japanese|ja_JP.UTF-8|jp106|jp|noto-fonts-cjk
Chinese (Simplified)|zh_CN.UTF-8|us|us|noto-fonts-cjk
Korean|ko_KR.UTF-8|us|kr|noto-fonts-cjk
Hindi|hi_IN.UTF-8|us|us|noto-fonts-extra
Arabic|ar_EG.UTF-8|us|us|noto-fonts-extra
"

# The common zones, with the label that says which one a person means. The
# timezone step explains the rest; the table is here so `--list-timezones` can
# put these at the top of the list, ahead of the 600 tzdata names.
TZ_ROWS="
America/New_York|US Eastern (EST/EDT)
America/Chicago|US Central (CST/CDT)
America/Denver|US Mountain (MST/MDT)
America/Phoenix|US Arizona (MST, no DST)
America/Los_Angeles|US Pacific (PST/PDT)
America/Anchorage|US Alaska
Pacific/Honolulu|US Hawaii
America/Toronto|Canada Eastern
America/Sao_Paulo|Brazil
Europe/London|UK (GMT/BST)
Europe/Berlin|Central Europe (CET/CEST)
Europe/Moscow|Moscow
Asia/Kolkata|India
Asia/Shanghai|China
Asia/Tokyo|Japan
Australia/Sydney|Australia Eastern
UTC|UTC — no local time
"

# `syn-install --list-locales` prints one TAB-separated record per row:
#
#   label  locale  console-keymap  xkb-layout  font-package(s)
#
# All five columns, including the fonts — a front end that picks a row is
# answering the same question the menu answers, so it has to be able to pass on
# the same font pack (see the lang_fonts key in the region step).
list_locales() {
    local row
    while IFS= read -r row; do
        [ -n "$row" ] || continue
        printf '%s\n' "${row//|/$'\t'}"
    done <<<"$LOCALE_ROWS"
}

# `--list-timezones` — `name  label`, the shortlist first (labelled) and then
# every zone tzdata ships (label empty), so a picker is not limited to the
# seventeen common ones. /usr/share/zoneinfo is the same authority the install
# validates the answer against.
#
# The name filter is one regex because every tzdata zone name starts with a
# capital and everything in that tree that is NOT a zone does not:
# leapseconds, posixrules, tzdata.zi, the *.tab files, and the posix/ and right/
# duplicate trees.
list_timezones() {
    local row name curated=""
    while IFS= read -r row; do
        [ -n "$row" ] || continue
        printf '%s\t%s\n' "${row%%|*}" "${row#*|}"
        curated="${curated}${row%%|*}"$'\n'
    done <<<"$TZ_ROWS"

    [ -d /usr/share/zoneinfo ] || return 0
    while IFS= read -r name; do
        [ -n "$name" ] || continue
        # Already printed with a label above.
        case $'\n'"$curated" in *$'\n'"$name"$'\n'*) continue ;; esac
        printf '%s\t\n' "$name"
    done < <( cd /usr/share/zoneinfo 2>/dev/null &&
              find . -type f -printf '%P\n' 2>/dev/null |
              grep -E '^[A-Z][A-Za-z0-9_+-]*(/[A-Za-z0-9_+-]+)*$' | sort )
}

# `--list-keymaps` — the console keymaps loadkeys can find on this image, which
# is exactly the check the install itself makes on the answer. Second column is
# empty: kbd ships no descriptions, and inventing them is how a picker starts
# disagreeing with the thing that loads the map.
list_keymaps() {
    [ -d /usr/share/kbd/keymaps ] || return 0
    find /usr/share/kbd/keymaps -name '*.map*' -printf '%f\n' 2>/dev/null |
        sed 's/\.map.*$//' | sort -u | sed 's/$/\t/'
}

# `--list-xkb-layouts` — `layout  description`, from xkeyboard-config's own
# rules list, which is where the names xkbcommon accepts are written down. The
# descriptions are the reason to read base.lst rather than list the symbols
# directory: "gb — English (UK)" is the answer to the question that made this
# two columns in the first place.
list_xkb_layouts() {
    local lst=/usr/share/X11/xkb/rules/base.lst
    if [ -f "$lst" ]; then
        awk '/^! layout/ { f = 1; next }
             /^!/        { f = 0 }
             f && NF > 1 { name = $1; $1 = ""; sub(/^[ \t]+/, ""); print name "\t" $0 }' \
            "$lst"
        return 0
    fi
    # No rules list: the symbols directory is what the install checks against,
    # and its file names ARE the layout names.
    [ -d /usr/share/X11/xkb/symbols ] || return 0
    find /usr/share/X11/xkb/symbols -maxdepth 1 -type f -printf '%f\t\n' 2>/dev/null | sort
}

config_report_unused() {
    [ -n "$CONFIG_FILE" ] || return 0
    local k unused=""
    for k in "${!ANSWERS[@]}"; do
        [ -n "${ANSWERS_USED[$k]+set}" ] || unused="$unused $k"
    done
    [ -n "$unused" ] || return 0
    warn "These keys in $CONFIG_FILE were never used:$unused
  Either they are misspelled, or they answer a question this install never
  reached (partition keys on an 'erase' install, for instance)."
}

# ── What to install: the pick lists ───────────────────────
#
# EVERY SynapseOS package is a checkbox, and so is a shelf of ordinary software
# people install on the first evening anyway. Two things were wrong before:
#
# 1. Eleven of our own packages were installed unconditionally — synpkg,
#    synfiles, syn-settings, syn-disks, syn-edit, syntty, syn, syn-model,
#    syn-firstboot, syn-confine, fetch — and Custom could not decline any of
#    them. "You get the whole suite or you hand-edit afterwards" is not a
#    choice, and the reasons each one was unconditional ("a desktop with no
#    file manager is broken") are arguments for a DEFAULT, not for a rule.
# 2. An installed SynapseOS had NO WEB BROWSER unless the Full preset happened
#    to pull one in as nexus-chat's dependency. The live ISO ships Firefox;
#    the machine installed from it did not.
#
# What stays forced is only what pacman would force anyway — synui depends on
# syntty, synnet and vibe depend on synapd, vibe depends on syn-confine,
# syn-firstboot depends on syn-model. sel_resolve_deps() turns those back on
# and SAYS SO, because a checkbox that silently un-ticks itself is worse than
# one that was never offered.
#
# ROW FORMAT — key|std|full|group|packages|label|description
#
#   key          the profile key, and what the graphical installer writes
#   std / full   ticked by default under Standard / Full (Minimal is `group`)
#   group        core  the spine a Minimal install still gets
#                app   ours, but an application — Minimal clears these
#                sw    not ours at all; from core/extra/multilib
#                flat  a FLATPAK from flathub, not a pacman package at all.
#                      `packages` is the app ID. Separate from sw because it is
#                      a different installer with a different failure mode, and
#                      because a pacman transaction must never be handed an app
#                      ID — `pacman -S org.localsend.localsend_app` is not a
#                      slow failure, it is a confusing one.
#   packages     what pacman is actually asked for; more than one is allowed
#                and they travel together (synguard + its module; the
#                wallpaper renderer + the wallpapers it plays)
#   label        <= 13 chars, description <= 15 — the two-column page below is
#                77 columns wide and a terminal is 80
#
# Keys are `comp_` for ours and `sw_` for everything else, so the vocabulary
# check in tests/config_test.sh can find them without a list of its own.
SEL_COMPONENTS=(
    "comp_synui|1|1|core|synui|SYNAPSE UI|the desktop"
    "comp_synapd|1|1|core|synapd|synapd|the AI daemon"
    "comp_synsh|1|1|core|synsh|synsh|the AI shell"
    "comp_synguard|1|1|core|synguard synapse_kmod|synguard|security watch"
    "comp_synnet|1|1|core|synnet|synnet|network policy"
    "comp_synpkg|1|1|core|synpkg|Software|package manager"
    "comp_synfiles|1|1|core|synfiles|Files|file manager"
    "comp_syntty|1|1|core|syntty|Terminal|the terminal"
    "comp_synsettings|1|1|core|syn-settings|Settings|system settings"
    "comp_syndisks|1|1|core|syn-disks|Disks|disks + format"
    "comp_synedit|1|1|core|syn-edit|Editor|the text editor"
    "comp_synupdate|1|1|core|syn-update|Updates|receives fixes"
    "comp_syn|1|1|core|syn|syn|top-level CLI"
    "comp_synmodel|1|1|core|syn-model|Models|fetch AI models"
    "comp_synfirstboot|1|1|core|syn-firstboot|First boot|first-run setup"
    "comp_synconfine|1|1|core|syn-confine|Sandbox|Landlock jail"
    "comp_fetch|1|1|core|fetch|About OS|the OS readout"
    "comp_arcade|1|1|app|syn-arcade|Arcade|pads + overlay"
    "comp_cliamp|1|1|app|cliamp|cliamp|music player"
    "comp_synstudio|1|1|app|synstudio|Studio|photo + video"
    "comp_arsenal|1|1|app|syn-arsenal|Arsenal|BlackArch tools"
    "comp_chibi|1|1|app|chibi|Chibi|voice companion"
    "comp_vibe|1|1|app|vibe|Vibe|AI coding help"
    "comp_wpengine|1|1|app|linux-wallpaperengine synapse-wallpapers|Wallpapers|animated, 317MB"
    "comp_nexus|0|1|app|nexus-chat|Nexus Chat|chat (+Firefox)"
    "comp_tepris|0|1|app|tepris|TEPRIS|game (+Firefox)"
)

# The shelf. Nothing here is ours and nothing here is required — the point is
# that the answer to "where is my browser" is a tick on the install screen
# rather than a search after the reboot.
#
# ⚠ EVERY NAME MUST BE IN core/extra/multilib. The target has our local repo
# and the Arch mirrors and nothing else at this point: [blackarch] is only
# added when WANT_BLACKARCH is taken and [cachyos] only with Steam, and the AUR
# is not a repository at all. A name pacman cannot resolve does not fail
# loudly — it loses the WHOLE transaction it is in, which is the mistake the
# Full preset made with samsung-m2020.
SEL_SW_WEB=(
    "sw_firefox|1|1|sw|firefox|Firefox|web browser"
    "sw_chromium|0|0|sw|chromium|Chromium|web browser"
    "sw_vivaldi|0|0|sw|vivaldi|Vivaldi|web browser"
    "sw_thunderbird|0|1|sw|thunderbird|Thunderbird|mail client"
    "sw_discord|0|0|sw|discord|Discord|voice + chat"
    "sw_telegram|0|0|sw|telegram-desktop|Telegram|messaging"
    "sw_signal|0|0|sw|signal-desktop|Signal|messaging"
    "sw_keepassxc|0|1|sw|keepassxc|KeePassXC|password vault"
    "sw_qbittorrent|0|0|sw|qbittorrent|qBittorrent|torrents"
    "sw_syncthing|0|0|sw|syncthing|Syncthing|file sync"
    "sw_localsend|0|1|flat|org.localsend.localsend_app|LocalSend|send to phone"
)
SEL_SW_MEDIA=(
    "sw_vlc|0|1|sw|vlc|VLC|plays anything"
    "sw_mpv|0|1|sw|mpv|mpv|minimal player"
    "sw_obs|0|0|sw|obs-studio|OBS Studio|record + stream"
    "sw_audacity|0|0|sw|audacity|Audacity|audio editor"
    "sw_kdenlive|0|0|sw|kdenlive|Kdenlive|video editor"
    "sw_handbrake|0|0|sw|handbrake|HandBrake|transcode video"
    "sw_spotify|0|0|sw|spotify-launcher|Spotify|streaming"
)
SEL_SW_OFFICE=(
    "sw_libreoffice|0|1|sw|libreoffice-fresh|LibreOffice|documents"
    "sw_gimp|0|1|sw|gimp|GIMP|image editing"
    "sw_inkscape|0|0|sw|inkscape|Inkscape|vector drawing"
    "sw_krita|0|0|sw|krita|Krita|painting"
    "sw_blender|0|0|sw|blender|Blender|3D"
    "sw_calibre|0|0|sw|calibre|Calibre|ebooks"
)
SEL_SW_DEV=(
    "sw_code|0|0|sw|code|VS Code|editor (OSS)"
    "sw_neovim|0|0|sw|neovim|Neovim|modal editor"
    "sw_gitlfs|0|0|sw|git-lfs|git-lfs|large files"
    "sw_docker|0|0|sw|docker|Docker|containers"
    "sw_virtmanager|0|0|sw|virt-manager|virt-manager|VMs (libvirt)"
    "sw_gparted|0|0|sw|gparted|GParted|partitions"
    "sw_btop|0|1|sw|btop|btop|process monitor"
    "sw_filezilla|0|0|sw|filezilla|FileZilla|FTP/SFTP"
    "sw_remmina|0|0|sw|remmina|Remmina|remote desktop"
    "sw_archivers|0|1|sw|7zip unrar|Archivers|7zip + unrar"
)
# Steam is NOT here — it is WANT_STEAM, because it is the only item on any of
# these pages that turns on a whole second architecture ([multilib], the lib32
# tree) and a third repository ([cachyos], for Proton). Everything below is an
# ordinary package from extra.
SEL_SW_GAMES=(
    "sw_lutris|0|0|sw|lutris|Lutris|game launcher"
    "sw_prism|0|0|sw|prismlauncher|Prism|Minecraft"
    "sw_retroarch|0|0|sw|retroarch|RetroArch|emulator front"
    "sw_dolphinemu|0|0|sw|dolphin-emu|Dolphin|GameCube/Wii"
    "sw_ppsspp|0|0|sw|ppsspp|PPSSPP|PSP"
    "sw_scummvm|0|0|sw|scummvm|ScummVM|point-and-click"
    "sw_pinball|0|1|flat|com.github.k4zmu2a.spacecadetpinball|Pinball|3D Space Cadet"
    "sw_dosbox|0|0|sw|dosbox|DOSBox|DOS games"
    "sw_mame|0|0|sw|mame|MAME|arcade"
    "sw_protontricks|0|0|sw|protontricks|Protontricks|per-game fixes"
    "sw_winetricks|0|0|sw|winetricks|Winetricks|Wine fixes"
    "sw_goverlay|0|0|sw|goverlay|GOverlay|MangoHud config"
    "sw_antimicrox|0|0|sw|antimicrox|AntiMicroX|pad remapping"
    "sw_openrgb|0|0|sw|openrgb|OpenRGB|lighting"
    "sw_corectrl|0|0|sw|corectrl|CoreCtrl|GPU/CPU tuning"
)

# Every list, so the walkers below need no second copy of the set.
SEL_ALL_LISTS=(SEL_COMPONENTS SEL_SW_WEB SEL_SW_MEDIA SEL_SW_OFFICE SEL_SW_DEV SEL_SW_GAMES)

declare -A PICKED=()     # key -> 0|1, the answer for every row above

# sel_reset <full|standard|minimal>
#
# Fills PICKED from the table. Presets are columns rather than three separate
# lists in three places, which is what let Full and Minimal drift apart from
# the Custom questionnaire before.
sel_reset() {
    local __preset=$1 __list __row key std full group rest
    for __list in "${SEL_ALL_LISTS[@]}"; do
        local -n __rows="$__list"
        for __row in "${__rows[@]}"; do
            IFS='|' read -r key std full group rest <<<"$__row"
            case "$__preset" in
                full)     PICKED[$key]=$full ;;
                minimal)  [ "$group" = core ] && PICKED[$key]=1 || PICKED[$key]=0 ;;
                *)        PICKED[$key]=$std ;;
            esac
        done
    done
}

# sel_on <key> — is that row ticked?
sel_on() { [ "${PICKED[$1]:-0}" = 1 ]; }

# sel_packages <group>… — the pacman names for every ticked row in those groups
sel_packages() {
    local __want=" $* " __list __row key std full group pkgs out=""
    for __list in "${SEL_ALL_LISTS[@]}"; do
        local -n __rows="$__list"
        for __row in "${__rows[@]}"; do
            IFS='|' read -r key std full group pkgs _ <<<"$__row"
            case "$__want" in *" $group "*) ;; *) continue ;; esac
            [ "${PICKED[$key]:-0}" = 1 ] && out="$out $pkgs"
        done
    done
    echo $out          # unquoted: collapses the runs of spaces
}

# sel_manifest — the record syn-update reads to know what was NOT wanted.
#
# Every SynapseOS package is a checkbox here, so an installed system can be
# missing two thirds of the suite on purpose. syn-update had no way to see that:
# "in the tree, not on the disk" read as "new, install it", and the first update
# handed back every component the user had unticked. The only escape was naming
# each component by hand on every run.
#
# So the picker writes its answer down. One line per PACKAGE — not per checkbox
# key — because pacman package names are what syn-update speaks in, and a row
# like comp_synguard covers two of them (synguard, synapse_kmod).
#
# Rows in the `sw` group are deliberately absent: those are Arch packages, and
# syn-update neither builds nor adds them, so a line about them would be a claim
# this file has no business making.
#
# Written from PICKED AFTER sel_resolve_deps(), so a package that was ticked
# back on because something else needs it is recorded as selected — which is
# what actually lands on the disk.
sel_manifest() {
    local __list __row key std full group pkgs p state
    echo "# SynapseOS components this machine has been offered."
    echo "#"
    echo "# selected = installed here.  declined = offered and not taken."
    echo "#"
    echo "# syn-update will not ADD a component named here: an unticked box at"
    echo "# install time, or a later \`pacman -R\`, is an answer and it is kept."
    echo "# A component that is NOT named here has never been on offer on this"
    echo "# machine, so a new one still installs itself on the next update."
    echo "#"
    echo "# To take something you declined:   syn-update apply <component>"
    echo "# Written by syn-install, and rewritten by every \`syn-update apply\`."
    for __list in "${SEL_ALL_LISTS[@]}"; do
        local -n __rows="$__list"
        for __row in "${__rows[@]}"; do
            IFS='|' read -r key std full group pkgs _ <<<"$__row"
            [ "$group" = core ] || [ "$group" = app ] || continue
            state=declined
            [ "${PICKED[$key]:-0}" = 1 ] && state=selected
            for p in $pkgs; do
                echo "$p = $state"
            done
        done
    done
}

# sel_label <key> — for the messages the dependency pass prints
sel_label() {
    local __list __row key std full group pkgs label
    for __list in "${SEL_ALL_LISTS[@]}"; do
        local -n __rows="$__list"
        for __row in "${__rows[@]}"; do
            IFS='|' read -r key std full group pkgs label _ <<<"$__row"
            [ "$key" = "$1" ] && { echo "$label"; return 0; }
        done
    done
    echo "$1"
}

# sel_resolve_deps — turn on what a ticked row cannot run without.
#
# These are the HARD depends in our own PKGBUILDs, and they are the reason
# "everything is optional" does not mean "any combination installs". pacman
# would pull each one in regardless; doing it here means the summary, the
# package count and the service enablement all agree with what lands on the
# disk, instead of the summary being the one place that lies.
#
# Adding a rule: check `depends=` in the PKGBUILD, not what you remember.
sel_resolve_deps() {
    local forced="" pair need by
    # need:because
    for pair in comp_syntty:comp_synui \
                comp_synapd:comp_synnet \
                comp_synapd:comp_vibe \
                comp_synconfine:comp_vibe \
                comp_synmodel:comp_synfirstboot; do
        need=${pair%%:*}; by=${pair#*:}
        if sel_on "$by" && ! sel_on "$need"; then
            PICKED[$need]=1
            forced="$forced
    $(sel_label "$need") — $(sel_label "$by") depends on it"
        fi
    done
    # A model with no daemon to load it, and a daemon with no model, are both
    # legal and both worth a word: the first is a gigabytes-long download that
    # nothing on the machine can read.
    if [ "${WANT_MODEL:-0}" = 1 ] && [ "${MODEL_CHOICE:-none}" != none ] && ! sel_on comp_synmodel; then
        PICKED[comp_synmodel]=1
        forced="$forced
    Models — an AI model was chosen and syn-model is what downloads it"
    fi
    [ -n "$forced" ] && warn "Added back, because something you kept requires it:$forced"
    return 0
}

# multi_select <title> <array name> — one page of checkboxes.
#
# Two columns because the component page is 25 rows and a terminal is 24 lines
# tall; the widths in the row format above are what keep it inside 80 columns.
#
# A profile answers these one row at a time (comp_synui=no), exactly like every
# other question here, so an unattended install never sees this page and a
# PARTIAL profile still gets asked about the rows it left out — which is the
# same contract `answer` has everywhere else in this script.
multi_select() {
    local __title=$1 __arr=$2
    local -n __rows="$__arr"
    local n=${#__rows[@]} i j idx rows line cell
    local -a keys=() labels=() descs=()
    local key std full group pkgs label desc unanswered=0 ans tok

    for ((i = 0; i < n; i++)); do
        IFS='|' read -r key std full group pkgs label desc <<<"${__rows[$i]}"
        keys+=("$key"); labels+=("$label"); descs+=("$desc")
        if [ -n "${ANSWERS[$key]+set}" ]; then
            ANSWERS_USED[$key]=1
            case "${ANSWERS[$key],,}" in
                y|yes|true|on|1)   PICKED[$key]=1 ;;
                n|no|false|off|0)  PICKED[$key]=0 ;;
                *) die "config: $key=${ANSWERS[$key]} is not yes or no" ;;
            esac
        else
            unanswered=1
        fi
    done

    rows=$(( (n + 1) / 2 ))
    while :; do
        echo ""
        echo "  $(bold "$__title")"
        echo ""
        for ((i = 0; i < rows; i++)); do
            line=""
            for j in 0 1; do
                idx=$(( i + j * rows ))
                [ "$idx" -lt "$n" ] || continue
                # ⚠ The PRECISION, not just the width. `%-15s` pads a short
                # description and does nothing at all to a long one, so a
                # sixteenth character pushes the row to 80 columns and the
                # terminal wraps the right-hand column onto its own line —
                # which reads as a layout bug in a page nobody edited.
                # `%-15.15s` truncates instead, so the grid cannot be broken by
                # a row added later.
                printf -v cell '[%s] %2d) %-13.13s %-15.15s' \
                    "$([ "${PICKED[${keys[$idx]}]:-0}" = 1 ] && echo x || echo ' ')" \
                    "$((idx + 1))" "${labels[$idx]}" "${descs[$idx]}"
                [ "$j" = 0 ] && line="  $cell " || line="$line$cell"
            done
            echo "${line%"${line##*[![:space:]]}"}"
        done
        echo ""

        # Answered in full by a profile: printed for the transcript, not asked.
        [ "$unanswered" = 1 ] || return 0

        prompt "Toggle [numbers, 'all', 'none', Enter = accept]:"
        read -r ans
        [ -n "${ans//[[:space:]]/}" ] || return 0
        for tok in $ans; do
            case "${tok,,}" in
                all)  for key in "${keys[@]}"; do PICKED[$key]=1; done ;;
                none) for key in "${keys[@]}"; do PICKED[$key]=0; done ;;
                ''|*[!0-9]*)
                    fail "\"$tok\" is not a number, 'all' or 'none'" ;;
                *)
                    idx=$((tok - 1))
                    if [ "$idx" -ge 0 ] && [ "$idx" -lt "$n" ]; then
                        key=${keys[$idx]}
                        [ "${PICKED[$key]:-0}" = 1 ] && PICKED[$key]=0 || PICKED[$key]=1
                    else
                        fail "$tok is not on this page (1-$n)"
                    fi ;;
            esac
        done
    done
}

# ── ILoveCandy ────────────────────────────────────────────
#
# pacman's Pac-Man progress bar, on by default on SynapseOS. Takes the file so
# the test can hand it a fixture; the installer passes /mnt/etc/pacman.conf.
#
# INSERTED RATHER THAN UNCOMMENTED, which is the opposite of the [multilib] rule
# further down and for a plain reason: ILoveCandy is undocumented and ships in
# no stock pacman.conf, so there is no commented line to turn on. It is a real
# option — `pacman-conf ILoveCandy` recognises it — and a bare key with no
# value, like UseSyslog beside it.
#
# The anchor is '# Misc options', the stock comment heading the block that holds
# Color, NoProgressBar and VerbosePkgLists: that is where this belongs and where
# a user goes looking for it. '[options]' is the fallback for a pacman.conf
# carrying no such comment — landing at the top of the section is untidy but
# correct, and the alternative is the option silently not being written.
#
# `0,/re/` bounds the substitution to the FIRST match, so a file with a stray
# later match cannot get a second copy, and '&' re-emits the anchor so this
# inserts AFTER it rather than replacing it.
#
# Returns non-zero if the line is not there afterwards. An anchor that moved
# makes sed write nothing and say nothing, and a default that quietly did not
# apply is the kind of thing nobody notices until they go looking for it.
pacman_conf_enable_candy() {  # pacman_conf_enable_candy <pacman.conf>
    _pcc_file=$1
    [ -f "$_pcc_file" ] || return 1
    if ! grep -q '^ILoveCandy' "$_pcc_file"; then
        if grep -q '^# Misc options$' "$_pcc_file"; then
            sed -i '0,/^# Misc options$/s//&\nILoveCandy/' "$_pcc_file"
        else
            sed -i '0,/^\[options\]$/s//&\nILoveCandy/' "$_pcc_file"
        fi
    fi
    grep -q '^ILoveCandy' "$_pcc_file"
}

# Test seam: sourcing this script with SYN_INSTALL_SOURCE_ONLY=1 defines the
# pure decision functions above and stops HERE, before the root check, the
# EXIT trap and the first blocking prompt. tests/layout_test.sh asserts the
# whole filesystem/bootloader table that way — the table is where the
# combinatorial risk lives, and it is the only part testable without a disk.
if [ "${SYN_INSTALL_SOURCE_ONLY:-}" = "1" ]; then
    return 0 2>/dev/null || exit 0
fi

# ── Arguments ─────────────────────────────────────────────
#
# Below the test seam on purpose: sourcing this script hands it the TEST's
# argv, and a test runner's flags are not install options.
while [ $# -gt 0 ]; do
    case "$1" in
        --config)  [ $# -ge 2 ] || die "--config needs a file"; _CONFIG_ARG="$2"; shift ;;
        --config=*) _CONFIG_ARG="${1#*=}" ;;
        -h|--help)
            cat << USAGE
syn-install $VERSION — install SynapseOS $RELEASE to disk

  syn-install                    ask every question (the normal way)
  syn-install --config FILE      answer them from an install profile
  syn-install --live             open with the live image's menu — install
                                 here, install graphically, or try the
                                 desktop. syn-firstboot passes this; you
                                 would not.

FILE is either key=value lines, or a .nix profile evaluated through
/usr/share/syn/nix/render.nix (needs nix). Questions the profile leaves
out are still asked at the machine, so a partial profile is useful.

  syn nix profile FILE.nix       render a Nix profile to key=value
  /usr/share/syn/nix/profile-example.nix   every key, documented

USAGE
            exit 0 ;;
        # Set by syn-firstboot on the live image, and by nothing else: it says
        # "you are the first thing the user sees", which is what turns the
        # welcome below into a three-way menu.
        --live)    LIVE_MENU=1 ;;
        # Records for the graphical front end. Prints and exits — it is a query,
        # so it runs before the root check below: reading a partition table
        # needs no privilege and a picker that demands root to LOOK is a picker
        # people run as root.
        --list-disks) list_disks; exit 0 ;;
        # The region pickers, for the same reason and on the same terms: the
        # tables and the "does this name exist on this image" tests live in one
        # place, and the window renders what they say. Queries, so they are
        # answered before the root check too.
        --list-locales)     list_locales; exit 0 ;;
        --list-timezones)   list_timezones; exit 0 ;;
        --list-keymaps)     list_keymaps; exit 0 ;;
        --list-xkb-layouts) list_xkb_layouts; exit 0 ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

[ "$(id -u)" = "0" ] || die "syn-install must be run as root"

[ -n "${_CONFIG_ARG:-}" ] && config_load "$_CONFIG_ARG"

# Best-effort unmount of the target area on any unexpected exit.
trap cleanup EXIT

# ── The live image's ONE menu ─────────────────────────────
#
# syn-firstboot used to draw a branded screen offering "1) Install to disk /
# 2) Try live session", then hand over to this script, which cleared the screen
# and drew the SAME branding again a second later: two nearly identical screens
# for one decision, with firstboot's background DKMS build running behind them
# making the gap between them slow. There is one screen now and this is it.
#
# The graphical path is a choice made HERE rather than only on the desktop,
# because the desktop is what has to start first and starting it is the slow
# part — asking after the compositor is up would mean paying for it before
# anyone who wants the text installer can decline it.
WELCOME_ACK=1
if [ "${LIVE_MENU:-0}" = 1 ]; then
    header
    echo "  SynapseOS is running from the live image."
    echo ""
    echo "    $(bold '1)') Install SynapseOS     — right here, in this terminal"
    echo "    $(bold '2)') Install graphically   — starts the desktop first"
    echo "    $(bold '3)') Try the live desktop  — look around; install later"
    echo ""
    pick "Choice [1-3, default=3]:" live_start _live 3 "1 2 3" \
         -m text=1,tui=1,gui=2,graphical=2,live=3,desktop=3
    case "$_live" in
        2)  # .bash_profile starts the desktop the moment this exits, and the
            # live session's synuirc autostarts syn-install-gui when this flag
            # is present. Nothing else creates it, so a plain live session
            # never gets an installer window it did not ask for.
            mkdir -p /run/synapseos && : > /run/synapseos/install-gui
            success "Starting the desktop — the installer opens with it."
            exit 0 ;;
        3)  exit 0 ;;
    esac
    # Falls through to the text installer. The ENTER-to-continue welcome is
    # skipped: picking "Install SynapseOS" off the menu above IS that
    # acknowledgement, and asking for it again is the second screen this whole
    # block exists to remove. Nothing destructive has moved — the disk plan
    # still has to be confirmed by typing "yes".
    WELCOME_ACK=0
fi

# ── Welcome ───────────────────────────────────────────────
if [ "$WELCOME_ACK" = 1 ]; then
    header
    echo "  This installer will:"
    echo "    1. Partition a disk"
    echo "    2. Install SynapseOS base system"
    echo "    3. Install SynapseOS packages"
    echo "    4. Create user account"
    echo "    5. Choose desktop environment"
    echo "    6. Configure system & bootloader"
    echo ""
    warn "ALL DATA ON THE TARGET DISK WILL BE ERASED"
    echo ""
    prompt "Press ENTER to continue or Ctrl+C to abort..."
    answer press_enter_start _ack --ack
fi

# ── Network pre-flight (before anything destructive) ──────
header
step "Checking network"
if have_net; then
    success "Network is up"
else
    echo "  No network detected. Starting NetworkManager..."
    systemctl start NetworkManager 2>/dev/null || true

    # NM needs a moment to bring a wired link up or reconnect a known network.
    for _ in 1 2 3 4 5; do
        have_net && break
        sleep 1
    done

    # Still nothing. On a laptop that almost always means "no cable, needs
    # Wi-Fi" — and this used to just die, telling the user to go run nmtui and
    # start the installer over. That made installing over Wi-Fi a two-step dance
    # you had to know about in advance. Offer the picker inline instead, and
    # loop: associating can easily take two tries (typo'd passphrase, weak AP).
    while ! have_net; do
        if ls /sys/class/net/*/wireless >/dev/null 2>&1 && \
           command -v nmtui >/dev/null 2>&1; then
            echo ""
            echo "  No connection — but this machine has Wi-Fi."
            prompt "Open the Wi-Fi picker (nmtui)? [Y/n]:"
            answer wifi_picker wifi_ans -m yes=y,no=n,true=y,false=n
            case "${wifi_ans:-y}" in
                [Nn]*) die "No network connection. SynapseOS downloads the base
  system during install, so a connection is required." ;;
            esac

            nmtui connect || true
            header
            step "Checking network"

            # nmtui returns as soon as it has *associated*, but DHCP may not
            # have finished — don't call it a failure until the lease has had a
            # chance to land, or we'd send the user back round for no reason.
            for _ in 1 2 3 4 5 6 7 8; do
                have_net && break
                sleep 1
            done
            have_net || echo "  Still offline — try again."
        else
            die "No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."
        fi
    done
    success "Network connected"
fi

# ── Disk selection ────────────────────────────────────────
header
step "Step 1 — Select Target Disk"

echo "  Available disks:"
echo ""
lsblk -d -o NAME,SIZE,TYPE,MODEL | grep disk | while read -r line; do
    echo "    $line"
done
echo ""
prompt "Target disk (e.g. sda, vda, nvme0n1):"
answer disk DISK
# Typed answers are bare names, because that is what the prompt asks for. A
# config file is written away from the machine and naturally says /dev/vda, so
# take either rather than building /dev//dev/vda and failing the check below
# with a path nobody wrote.
case "$DISK" in /dev/*) ;; *) DISK="/dev/$DISK" ;; esac

[ -b "$DISK" ] || die "Disk $DISK not found"

# Never wipe the disk we booted from / are running on.
if is_live_disk "$DISK"; then
    die "$DISK is the live/boot device — refusing to install onto it.
  Pick the disk you want SynapseOS installed to, not the install media."
fi

# Refuse a disk that's too small to hold the system.
DISK_BYTES=$(disk_size_bytes "$DISK")
if [[ "$DISK_BYTES" =~ ^[0-9]+$ ]] && [ "$DISK_BYTES" -lt "$MIN_DISK_BYTES" ]; then
    die "$DISK is too small ($((DISK_BYTES / 1024 / 1024 / 1024)) GiB).
  SynapseOS needs at least $((MIN_DISK_BYTES / 1024 / 1024 / 1024)) GiB."
fi

# Refuse a disk with mounted partitions (in use / risk of corruption).
if disk_busy "$DISK"; then
    echo ""
    warn "$DISK has mounted partitions:"
    lsblk -no NAME,MOUNTPOINT "$DISK" | sed 's/^/    /'
    die "Target disk is in use. Unmount its partitions and re-run."
fi

echo ""
echo "  $(bold 'Target:') $DISK"
lsblk "$DISK"
echo ""

# ── Detect boot mode ──────────────────────────────────────
# Determined before the install-mode choice: installing ALONGSIDE another OS is
# offered only under UEFI, which is how every modern Windows machine boots.
if [ -d /sys/firmware/efi/efivars ]; then
    BOOT_MODE="uefi"
    success "Boot mode: UEFI"
else
    BOOT_MODE="bios"
    success "Boot mode: BIOS/Legacy"
fi

# ── Install mode: erase whole disk vs install alongside ───
#
# ERASE wipes the disk (mklabel) — the clean-install path. ALONGSIDE installs
# into existing FREE SPACE without touching any current partition, reusing the
# machine's ESP: the dual-boot flow for a disk that already holds Windows (or
# another OS). Alongside is offered ONLY when it can actually work — UEFI, an
# existing ESP, and enough contiguous free space — so the option never appears
# when it would fail. It never resizes a partition: the user frees space first
# (Windows: Disk Management -> Shrink Volume), which is the safe, in-Windows way.
NUM_PARTS=$(lsblk -rno NAME "$DISK" | tail -n +2 | grep -c .)
FREE_REGION="$(largest_free_region "$DISK")"
FREE_MIB="${FREE_REGION##* }"; [[ "$FREE_MIB" =~ ^[0-9]+$ ]] || FREE_MIB=0
ESP_DEV="$(find_esp "$DISK" || true)"
MIN_ROOT_MIB=$((MIN_DISK_BYTES / 1024 / 1024))

INSTALL_MODE="erase"
if [ "$BOOT_MODE" = "uefi" ] && [ "$NUM_PARTS" -gt 0 ] \
   && [ -n "$ESP_DEV" ] && [ "$FREE_MIB" -ge "$MIN_ROOT_MIB" ]; then
    echo ""
    echo "  This disk already holds $NUM_PARTS partition(s), an EFI System"
    echo "  Partition ($ESP_DEV), and $((FREE_MIB / 1024)) GiB of free space."
    echo ""
    echo "    1) Install $(bold 'ALONGSIDE') — use the free space, keep everything else"
    echo "    2) $(bold 'ERASE') the whole disk — delete every partition and all data"
    echo "    3) $(bold 'ADVANCED') — partition this disk yourself, then pick the partitions"
    echo ""
    pick "Install mode [1-3]:" install_mode _mode 1 "1 2 3" \
         -m alongside=1,erase=2,manual=3
    case "${_mode:-1}" in
        1) INSTALL_MODE="alongside" ;;
        3) INSTALL_MODE="manual" ;;
        *) INSTALL_MODE="erase" ;;
    esac
else
    if [ "$BOOT_MODE" = "uefi" ] && [ "$NUM_PARTS" -gt 0 ] && [ -n "$ESP_DEV" ]; then
        # Another OS is here, but not enough room to sit beside it.
        warn "This disk holds another OS but only $((FREE_MIB / 1024)) GiB is free —
  under the $((MIN_ROOT_MIB / 1024)) GiB SynapseOS needs. To dual-boot, shrink the
  existing OS first (Windows: Disk Management -> Shrink Volume), then re-run.
  Continuing now would ERASE the whole disk."
    fi

    # ADVANCED is offered on every disk, not only the dual-boot-capable ones:
    # its whole point is the layouts this installer does not generate — a
    # separate /home, a reused ESP that the alongside path's conditions rejected,
    # a partition left over from another distro.
    echo ""
    echo "    1) $(bold 'ERASE') the whole disk — delete every partition and all data  (default)"
    echo "    2) $(bold 'ADVANCED') — partition this disk yourself, then pick the partitions"
    echo ""
    pick "Install mode [1/2]:" install_mode _mode 1 "1 2" \
         -m erase=1,manual=2
    case "${_mode:-1}" in
        2) INSTALL_MODE="manual" ;;
        *) INSTALL_MODE="erase" ;;
    esac
fi
case "$INSTALL_MODE" in
    manual)    success "Install mode: ADVANCED (manual partitioning)" ;;
    alongside) success "Install mode: alongside" ;;
    *)         success "Install mode: erase" ;;
esac

# ── Full-disk encryption ──────────────────────────────────
#
# LUKS2 on the root partition only. The ESP cannot be encrypted (firmware has
# to read it), and /boot is deliberately left as its own PLAIN ext4 partition
# rather than living inside the encrypted root.
#
# That last choice is the important one. Putting /boot inside LUKS means GRUB
# itself must unlock it, and GRUB can only open LUKS2 volumes that use PBKDF2
# — not the argon2id that cryptsetup defaults to and that makes LUKS2 worth
# having. The workarounds are to weaken the KDF or to fight GRUB's cryptodisk
# support, and on this distro GRUB is already the fragile part of the boot
# path (see the shim_lock notes). A separate plain /boot costs one partition
# and keeps the initramfs the only thing that has to unlock anything: GRUB
# reads the kernel in the clear, the initramfs prompts, root opens. Nothing on
# the unencrypted /boot is a secret; it is the same kernel everyone else ships.
#
# Only offered on the erase paths. Encrypting into free space beside an
# existing OS works in principle but needs its own dual-boot testing pass, and
# silently shipping an untested variant of the destructive path is worse than
# not offering it.
ENCRYPT="no"
CRYPT_NAME="cryptroot"
if [ "$INSTALL_MODE" = "erase" ] || [ "$INSTALL_MODE" = "manual" ]; then
    echo ""
    echo "  $(bold 'Encrypt this installation?')"
    echo ""
    echo "  Encrypts the root filesystem with LUKS2. You will be asked for the"
    echo "  passphrase at every boot, before the system starts."
    echo ""
    echo "  $(bold 'There is no recovery.') If you forget the passphrase the data is"
    echo "  gone — no password reset, no support call, nothing."
    echo ""
    prompt "Encrypt the disk? [y/N]:"
    answer encrypt _enc -m yes=y,no=n,true=y,false=n
    case "${_enc,,}" in
        y|yes) ENCRYPT="yes" ;;
        *)     ENCRYPT="no" ;;
    esac
fi

# ── Filesystem ────────────────────────────────────────────
#
# Offered on the erase paths only. The alongside path creates a root in free
# space beside another OS and is its own testing surface; adding four
# filesystems to it at the same time would multiply an already-delicate path.
#
# Each entry states its real trade-off rather than a marketing line. Someone
# choosing a root filesystem at install time cannot undo the choice without
# reinstalling, so "btrfs cannot be shrunk" belongs here, not in a wiki.
ROOT_FS="ext4"
SNAPSHOTS="no"
if [ "$INSTALL_MODE" = "erase" ] || [ "$INSTALL_MODE" = "manual" ]; then
    echo ""
    echo "  $(bold 'Root filesystem')"
    echo ""
    echo "    $(bold '1)') ext4   — the default. Boring, proven, repairable by anything."
    echo "    $(bold '2)') btrfs  — snapshots + zstd compression. Roll back a bad update"
    echo "                    from the boot menu. Uses more RAM and more CPU."
    echo "    $(bold '3)') xfs    — fast on large files. No snapshots, and it cannot be"
    echo "                    SHRUNK once created."
    echo "    $(bold '4)') f2fs   — built for flash. Good on SD cards and cheap SSDs;"
    echo "                    unusual enough that fewer rescue tools know it."
    echo ""
    pick "Filesystem [1-4, default 1]:" filesystem _fs 1 "1 2 3 4" \
         -m ext4=1,btrfs=2,xfs=3,f2fs=4
    case "${_fs:-1}" in
        2) ROOT_FS="btrfs" ;;
        3) ROOT_FS="xfs" ;;
        4) ROOT_FS="f2fs" ;;
        *) ROOT_FS="ext4" ;;
    esac
    success "Filesystem: $ROOT_FS"
fi

# ── Bootloader ────────────────────────────────────────────
#
# Gated by firmware: systemd-boot is an EFI stub loader with no BIOS build, so
# on a BIOS machine it is not offered at all rather than offered and then
# failing at install time.
BOOTLOADER="grub"
if { [ "$INSTALL_MODE" = "erase" ] || [ "$INSTALL_MODE" = "manual" ]; } && [ "$BOOT_MODE" = "uefi" ]; then
    echo ""
    echo "  $(bold 'Bootloader')"
    echo ""
    echo "    $(bold '1)') GRUB          — the default. Detects other operating systems,"
    echo "                          and the only one here that can boot a btrfs"
    echo "                          snapshot."
    echo "    $(bold '2)') systemd-boot  — minimal. No OS detection, no snapshot menu."
    if [ "$ENCRYPT" = "yes" ]; then
        echo "                          With encryption it is the BETTER choice: the"
        echo "                          kernel lives on the EFI partition and only the"
        echo "                          initramfs unlocks, so /boot needs no separate"
        echo "                          unencrypted partition."
    fi
    echo "    $(bold '3)') limine        — modern and fast, and it CAN boot snapshots."
    echo "                          It copies each snapshot's kernel onto the EFI"
    echo "                          partition, so that partition is made much"
    echo "                          larger when snapshots are enabled."
    echo ""
    pick "Bootloader [1-3, default 1]:" bootloader _bl 1 "1 2 3" \
         -m grub=1,systemd-boot=2,limine=3
    case "${_bl:-1}" in
        2) BOOTLOADER="systemd-boot" ;;
        3) BOOTLOADER="limine" ;;
        *) BOOTLOADER="grub" ;;
    esac
    success "Bootloader: $BOOTLOADER"
fi

bootloader_supported "$BOOTLOADER" "$BOOT_MODE" \
    || die "$BOOTLOADER cannot boot a $BOOT_MODE system"

# ── Snapshots ─────────────────────────────────────────────
#
# Only meaningful on btrfs: these are btrfs subvolume snapshots, and no other
# filesystem offered here has an equivalent — ext4, xfs and f2fs would need
# something like LVM thin snapshots, which is a different design and not one this
# installer implements.
#
# And only bootable under GRUB or limine, by different machinery: grub-btrfs
# generates GRUB's submenu, limine-snapper-sync copies each snapshot's kernel
# onto the ESP and writes limine's entries. systemd-boot has neither, so offering
# it there would promise a boot menu that cannot exist.
if fs_supports_snapshots "$ROOT_FS"; then
    if bootloader_supports_snapshots "$BOOTLOADER"; then
        echo ""
        echo "  $(bold 'Automatic snapshots?')"
        echo ""
        echo "  snapper takes a snapshot before and after every pacman"
        echo "  transaction, and $BOOTLOADER grows a menu to boot any of them. A"
        echo "  bad upgrade becomes a reboot instead of a rescue USB."
        echo ""
        echo "  Snapshots are cheap but not free: they hold the old copy of"
        echo "  anything that changes, so a disk near full stays near full."
        echo ""
        prompt "Enable snapshots? [Y/n]:"
        answer snapshots _sn -m yes=y,no=n,true=y,false=n
        case "${_sn,,}" in
            n|no) SNAPSHOTS="no" ;;
            *)    SNAPSHOTS="yes" ;;
        esac
        success "Snapshots: $SNAPSHOTS"
    else
        warn "Snapshots need GRUB or limine — $BOOTLOADER cannot boot them. Continuing without."
    fi
else
    # Say it out loud. The bootloader case above explains itself; this one used to
    # skip in silence, so choosing ext4 made the question vanish with no way to
    # tell a deliberate exclusion from a bug.
    warn "Snapshots need btrfs — $ROOT_FS has no snapshot mechanism. Not offered."
fi

# The layout follows from the three answers above; compute it once, here, so
# every partitioning branch reads the same decision instead of re-deriving it.
SEPARATE_BOOT="$(layout_separate_boot "$ROOT_FS" "$BOOTLOADER" "$ENCRYPT")"

# The chosen filesystem's mkfs has to exist ON THIS IMAGE, and this is the last
# moment it can be checked for free. Discovering it at format time means the
# partition table has already been rewritten: the difference between "pick
# another filesystem" and a disk that is now neither the old system nor a new
# one. mkfs.ext4 is checked too — a separate /boot needs it whatever the root is.
_mkfs_bin="$(fs_mkfs_cmd "$ROOT_FS" | awk '{print $1}')"
command -v "$_mkfs_bin" >/dev/null \
    || die "$_mkfs_bin is missing from this installer image — a $ROOT_FS root cannot be created here"
if [ "$SEPARATE_BOOT" = "yes" ]; then
    command -v mkfs.ext4 >/dev/null \
        || die "mkfs.ext4 is missing from this installer image — /boot cannot be created"
fi
if [ "$SNAPSHOTS" = "yes" ]; then
    command -v btrfs >/dev/null \
        || die "btrfs is missing from this installer image — subvolumes cannot be created"
fi

# ── Confirm the plan ──────────────────────────────────────
#
# Everything above this line is a question. Everything below it writes to the
# disk. So this is the last moment an answer can be taken back for free, and
# until now there was none: a mistyped filesystem or the wrong disk could only be
# undone by killing the installer and starting again — after the confirmations
# that follow have already been answered "yes".
#
# Answering no re-execs the installer rather than looping back over the questions
# in place. That is deliberate. Nothing has been written yet, so a restart is
# exactly equivalent to a loop, and it cannot leave one answer from the previous
# pass behind: eight of these are prompted and two more (SEPARATE_BOOT, the free
# region) are derived from them, and a loop that re-asks the eight while a
# derived value keeps its old meaning is the kind of bug that surfaces as a
# partition table nobody chose. exec also means no nesting, however many times
# the answer is no.
#
# The passphrase is asked AFTER this on purpose — no point typing it twice to
# find out the disk was wrong.
echo ""
echo "  $(bold 'Review the plan — nothing has been written yet:')"
echo ""
echo "    Disk          : $DISK"
case "$INSTALL_MODE" in
    erase)  echo "    Mode          : ERASE the whole disk" ;;
    manual) echo "    Mode          : ADVANCED — you partition, then pick the partitions" ;;
    *)      echo "    Mode          : install alongside, into $((FREE_MIB / 1024)) GiB of free space" ;;
esac
echo "    Firmware      : $BOOT_MODE"
echo "    Filesystem    : $ROOT_FS"
echo "    Bootloader    : $BOOTLOADER"
echo "    Separate /boot: $SEPARATE_BOOT"
echo "    Encryption    : $ENCRYPT"
echo "    Snapshots     : $SNAPSHOTS"
echo ""
prompt "Are these correct? [Y/n]:"
answer disk_plan_ok _plan_ok -m yes=y,no=n,true=y,false=n || true
case "${_plan_ok,,}" in
    n|no)
        echo ""
        warn "Starting the questions over — the disk has not been touched."
        exec "$0" "$@"
        ;;
esac

if [ "$ENCRYPT" = "yes" ]; then
    command -v cryptsetup >/dev/null \
        || die "cryptsetup is not available on this installer image"

    # Read the passphrase twice, never echoed, and keep asking rather than
    # aborting a half-hour install over a typo. Empty is refused outright: an
    # empty LUKS passphrase is accepted by cryptsetup and would encrypt the
    # disk to nothing.
    while :; do
        prompt "Encryption passphrase:"
        answer luks_passphrase LUKS_PASS -s; echo ""
        prompt "Repeat passphrase:"
        answer luks_passphrase LUKS_PASS2 -s; echo ""
        if [ -z "$LUKS_PASS" ]; then
            warn "Empty passphrase — that would leave the disk unprotected."
            continue
        fi
        if [ "$LUKS_PASS" != "$LUKS_PASS2" ]; then
            warn "Passphrases did not match — try again."
            continue
        fi
        if [ "${#LUKS_PASS}" -lt 8 ]; then
            warn "Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."
            prompt "Use it anyway? [y/N]:"
            answer short_passphrase_ok _short -m yes=y,no=n,true=y,false=n
            case "${_short,,}" in y|yes) ;; *) continue ;; esac
        fi
        break
    done
    unset LUKS_PASS2
    success "Encryption enabled — root will be LUKS2"
fi

#
# Turn $PART_ROOT into the device the root filesystem actually goes on, and
# leave it in $ROOT_FS_DEV. Without encryption that is just the partition; with
# it, the partition is LUKS2-formatted, opened, and ROOT_FS_DEV becomes the
# mapper node. Every partitioning path calls this so the mkfs/mount below never
# has to know which case it is in.
#
# The passphrase goes in on stdin, never as an argv (which is world-readable in
# /proc) and never through a temp file.
#
luks_format_root() {
    ROOT_FS_DEV="$PART_ROOT"
    [ "$ENCRYPT" = "yes" ] || return 0

    echo "  Encrypting $PART_ROOT (LUKS2)..."
    printf '%s' "$LUKS_PASS" | cryptsetup luksFormat \
        --type luks2 --batch-mode --key-file - "$PART_ROOT" \
        || die "cryptsetup luksFormat failed on $PART_ROOT"

    printf '%s' "$LUKS_PASS" | cryptsetup open \
        --key-file - "$PART_ROOT" "$CRYPT_NAME" \
        || die "cryptsetup open failed — the passphrase did not take"

    ROOT_FS_DEV="/dev/mapper/$CRYPT_NAME"
    [ -b "$ROOT_FS_DEV" ] || die "$ROOT_FS_DEV missing after cryptsetup open"

    # Needed by GRUB's cryptdevice= below. Read from the partition, not the
    # mapper node: it is the LUKS container's UUID that identifies the volume
    # to unlock, and it is stable across re-opens.
    LUKS_UUID="$(blkid -s UUID -o value "$PART_ROOT")"
    [ -n "$LUKS_UUID" ] || die "could not read the LUKS UUID of $PART_ROOT"

    success "Root encrypted (LUKS2, UUID $LUKS_UUID)"
}

# ── Root filesystem: format, mount, and lay out subvolumes ──
#
# Shared by the UEFI and BIOS partitioning branches. It exists as a function
# because the btrfs half is thirty lines of ordering that has to be identical in
# both: create the subvolumes on the bare filesystem, unmount, then mount @ as
# the root and everything else beneath it. Duplicating that is how the two
# branches drift until one of them produces a system snapper cannot roll back.
format_and_mount_root() {
    echo "  Formatting root partition ($ROOT_FS)..."
    $(fs_mkfs_cmd "$ROOT_FS") "$ROOT_FS_DEV" \
        || die "Failed to format root partition as $ROOT_FS"

    echo "  Mounting..."
    mount -o "$(fs_mount_opts "$ROOT_FS")" "$ROOT_FS_DEV" /mnt \
        || die "Failed to mount root"

    [ "$ROOT_FS" = "btrfs" ] || return 0

    # The names and the /.snapshots mount point are what `snapper rollback`
    # expects. A layout that merely looks similar gives you snapshots that
    # cannot be rolled back to, which is discovered on the day it matters.
    echo "  Creating btrfs subvolumes..."
    btrfs subvolume create /mnt/@          >/dev/null || die "btrfs: could not create @"
    btrfs subvolume create /mnt/@home      >/dev/null || die "btrfs: could not create @home"
    btrfs subvolume create /mnt/@snapshots >/dev/null || die "btrfs: could not create @snapshots"
    btrfs subvolume create /mnt/@var_log   >/dev/null || die "btrfs: could not create @var_log"
    btrfs subvolume create /mnt/@pkg       >/dev/null || die "btrfs: could not create @pkg"
    umount /mnt || die "could not remount the btrfs root onto @"

    local o
    o="$(fs_mount_opts btrfs)"
    mount -o "$o,subvol=@" "$ROOT_FS_DEV" /mnt || die "Failed to mount @"
    mkdir -p /mnt/home /mnt/.snapshots /mnt/var/log /mnt/var/cache/pacman/pkg
    mount -o "$o,subvol=@home"      "$ROOT_FS_DEV" /mnt/home       || die "Failed to mount @home"
    mount -o "$o,subvol=@snapshots" "$ROOT_FS_DEV" /mnt/.snapshots || die "Failed to mount @snapshots"
    mount -o "$o,subvol=@var_log"   "$ROOT_FS_DEV" /mnt/var/log    || die "Failed to mount @var_log"
    # The package cache is deliberately NOT compressed and NOT snapshotted: the
    # payload is already-compressed .zst, and rolling the cache back with the
    # system achieves nothing except holding dead downloads on disk.
    mount -o "noatime,subvol=@pkg"  "$ROOT_FS_DEV" /mnt/var/cache/pacman/pkg \
        || die "Failed to mount @pkg"
}

# Clean up any previous failed install attempt
umount -R /mnt 2>/dev/null || true

# ── Partition ─────────────────────────────────────────────
header
step "Step 2 — Partitioning $DISK ($INSTALL_MODE)"

if [ "$INSTALL_MODE" = "alongside" ]; then
    # Non-destructive: create ONE new root partition in the largest free region
    # and reuse the existing ESP. No mklabel, and nothing we did not just create
    # is ever formatted. Confirm the exact plan first.
    read -r FREE_START FREE_END _FREE_SIZE <<<"$FREE_REGION"
    R_START=$((FREE_START + 1))          # 1 MiB inset off the preceding partition
    R_END=$FREE_END
    echo ""
    echo "  Plan — $(bold 'nothing else is touched'):"
    echo "    • KEEP   all $NUM_PARTS existing partition(s), including Windows"
    echo "    • REUSE  $ESP_DEV as the EFI partition (mounted, $(bold 'not') formatted)"
    echo "    • CREATE a new ext4 root of ~$(( (R_END - R_START) / 1024 )) GiB in the free space"
    echo ""
    lsblk -o NAME,SIZE,FSTYPE,PARTTYPENAME,MOUNTPOINTS "$DISK" 2>/dev/null | sed 's/^/    /' \
        || lsblk "$DISK" | sed 's/^/    /'
    echo ""
    warn "This adds one partition in the free space. Back up anything irreplaceable first."
    prompt "Type 'yes' to install alongside:"
    answer confirm_alongside confirm -m yes=yes,true=yes
    [ "$confirm" = "yes" ] || die "Aborted"

    # Snapshot the partition list so we can identify the one parted creates — its
    # number is whatever GPT slot is free, not necessarily the highest.
    _before=$(lsblk -rno NAME "$DISK" | sort)
    echo "  Creating root partition in free space (${R_START}MiB–${R_END}MiB)..."
    parted -s "$DISK" mkpart SYNAPSE_ROOT ext4 "${R_START}MiB" "${R_END}MiB" \
        || die "Failed to create the root partition"
    partprobe "$DISK" 2>/dev/null || true
    sleep 2
    _after=$(lsblk -rno NAME "$DISK" | sort)
    _new=$(comm -13 <(echo "$_before") <(echo "$_after") | grep . | head -1)
    [ -n "$_new" ] || die "Could not identify the new partition after creating it"
    PART_ROOT="/dev/$_new"
    PART_EFI="$ESP_DEV"

    # ext4 unconditionally, and not fs_mkfs_cmd: the filesystem and bootloader
    # prompts are erase-only, so ROOT_FS is still its default here. Stated
    # rather than inherited, because a later change that offers the choice on
    # this path would otherwise silently start writing btrfs into free space
    # beside someone's Windows install with none of the layout below adjusted.
    echo "  Formatting new root ($PART_ROOT, ext4)..."
    mkfs.ext4 -F "$PART_ROOT" || die "Failed to format root partition"

    echo "  Mounting..."
    mount "$PART_ROOT" /mnt || die "Failed to mount root"
    mkdir -p /mnt/boot/efi
    # REUSE, never mkfs: this ESP carries Windows' bootloader.
    mount "$PART_EFI" /mnt/boot/efi || die "Failed to mount the existing ESP"

elif [ "$INSTALL_MODE" = "manual" ]; then
    # ── ADVANCED: the user partitions, the installer formats ──
    #
    # The installer stops deciding the LAYOUT here and starts asking. What it
    # does NOT hand over is the filesystem work: the mkfs, the btrfs subvolume
    # layout and the LUKS container still go through luks_format_root and
    # format_and_mount_root, exactly as the automatic paths do. A manual install
    # is a different partition table, not a second implementation of the parts
    # that are subtle.
    #
    # Nothing on the disk is touched until every partition has been named and
    # the summary confirmed, and only the partitions named here are formatted —
    # anything else on the disk is left alone, including a /home the user wants
    # to keep.
    FORMAT_ESP="no"   # set for real below; declared here so `set -u` holds on BIOS
    _editors="$(available_part_editors)"
    [ -n "$_editors" ] || die "no partition editor on this image (cfdisk, fdisk and parted are all missing)"

    echo ""
    echo "  $(bold 'Partition') $DISK $(bold 'now.') The installer will re-read the table when you exit."
    echo ""
    echo "  Available editors:$(printf ' %s' $_editors)"
    echo ""
    echo "  What this install needs:"
    echo "    • a root partition, at least $((MIN_DISK_BYTES / 1024 / 1024 / 1024)) GiB"
    [ "$BOOT_MODE" = "uefi" ] && \
    echo "    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"
    [ "$SEPARATE_BOOT" = "yes" ] && \
    echo "    • a separate /boot of ~1 GiB — $BOOTLOADER with this layout cannot read the root"
    echo ""
    _ed="${_editors%% *}"
    prompt "Editor to run [$_ed]:"
    answer partition_editor _ed_pick || true
    case " $_editors " in
        *" ${_ed_pick:-$_ed} "*) _ed="${_ed_pick:-$_ed}" ;;
        # skip/none are handled below and are not editor names, so they must
        # not be reported as an editor this machine happens not to have.
        *) case "${_ed_pick:-}" in ""|skip|none) ;;
               *) warn "$_ed_pick is not available here — using $_ed" ;;
           esac ;;
    esac

    echo ""
    # `partition_editor=skip` is the one answer that is not an editor name. The
    # ADVANCED path exists to use partitions that already exist, and a profile
    # that names part_root/part_efi is describing a disk somebody already laid
    # out — so launching a full-screen editor at it would hang the install on a
    # program waiting for a keystroke nobody is there to press.
    case "${_ed_pick:-}" in
        skip|none)
            echo "  Skipping the partition editor (--config)."
            ;;
        *)
            echo "  Starting $_ed on $DISK — write your changes before quitting."
            sleep 1
            "$_ed" "$DISK" || warn "$_ed exited non-zero — check the table below before continuing"
            ;;
    esac

    partprobe "$DISK" 2>/dev/null || true
    sleep 2

    echo ""
    echo "  $(bold 'Partitions now on') $DISK:"
    lsblk -o NAME,SIZE,FSTYPE,PARTTYPENAME,MOUNTPOINTS "$DISK" 2>/dev/null | sed 's/^/    /' \
        || lsblk "$DISK" | sed 's/^/    /'
    echo ""

    # Root. Asked in a loop rather than validated once and aborted: the user is
    # standing at the machine with the editor one keystroke away, so a typo
    # should cost a retry, not a re-run of the whole installer.
    while :; do
        prompt "Root partition (e.g. ${DISK}2):"
        answer part_root PART_ROOT || true
        [ -n "${PART_ROOT:-}" ] || continue
        case "$PART_ROOT" in /dev/*) ;; *) PART_ROOT="/dev/$PART_ROOT" ;; esac
        _why="$(part_usable "$PART_ROOT" "the root filesystem" "$MIN_DISK_BYTES")" && break
        warn "$_why"
    done

    PART_EFI=""
    if [ "$BOOT_MODE" = "uefi" ]; then
        while :; do
            prompt "EFI System Partition (e.g. ${DISK}1):"
            answer part_efi PART_EFI || true
            [ -n "${PART_EFI:-}" ] || continue
            case "$PART_EFI" in /dev/*) ;; *) PART_EFI="/dev/$PART_EFI" ;; esac
            _why="$(part_usable "$PART_EFI" "the EFI partition" $((256 * 1024 * 1024)))" || { warn "$_why"; continue; }
            _who="$(part_role_taken "$PART_EFI" "the root filesystem=$PART_ROOT")" \
                && { warn "$PART_EFI is already $_who — name a different partition."; continue; }
            # An ESP that already carries a bootloader is the dual-boot case:
            # formatting it would take Windows' with it. Ask, default to keeping.
            FORMAT_ESP="yes"
            if [ "$(blkid -s TYPE -o value "$PART_EFI" 2>/dev/null)" = "vfat" ]; then
                echo "    $PART_EFI is already FAT — it may hold another OS's bootloader."
                prompt "Format it? Everything on it is lost [y/N]:"
                answer format_esp _fmt -m yes=y,no=n,true=y,false=n || true
                case "${_fmt,,}" in y|yes) FORMAT_ESP="yes" ;; *) FORMAT_ESP="no" ;; esac
            fi
            break
        done
    fi

    PART_BOOT=""
    if [ "$SEPARATE_BOOT" = "yes" ]; then
        while :; do
            prompt "Separate /boot partition:"
            answer part_boot PART_BOOT || true
            [ -n "${PART_BOOT:-}" ] || continue
            case "$PART_BOOT" in /dev/*) ;; *) PART_BOOT="/dev/$PART_BOOT" ;; esac
            _why="$(part_usable "$PART_BOOT" "/boot" $((512 * 1024 * 1024)))" || { warn "$_why"; continue; }
            _who="$(part_role_taken "$PART_BOOT" "the root filesystem=$PART_ROOT" \
                                                 "the EFI partition=${PART_EFI:-}")" \
                && { warn "$PART_BOOT is already $_who — name a different partition."; continue; }
            break
        done
    fi

    # Swap. Optional, and only reachable here: the automatic paths give the root
    # 100% of what is left, so there is nowhere to put one without changing a
    # layout that is tested. Nothing else in this installer has ever made swap —
    # a swap partition created in the editor and not named here stays ignored.
    PART_SWAP=""; FORMAT_SWAP="no"
    while :; do
        echo ""
        prompt "Swap partition (blank for none):"
        answer part_swap _sw || true
        [ -n "${_sw:-}" ] || break
        case "$_sw" in /dev/*) ;; *) _sw="/dev/$_sw" ;; esac
        _why="$(part_usable "$_sw" "swap" $((128 * 1024 * 1024)))" || { warn "$_why"; continue; }
        _who="$(part_role_taken "$_sw" "the root filesystem=$PART_ROOT" \
                                       "the EFI partition=${PART_EFI:-}" \
                                       "/boot=${PART_BOOT:-}")" \
            && { warn "$_sw is already $_who — name a different partition, or leave this blank for no swap."; continue; }
        PART_SWAP="$_sw"; FORMAT_SWAP="yes"
        # An existing swap is likely shared with another Linux, and mkswap gives
        # it a new UUID — which is exactly what that system's fstab and its
        # hibernation resume= point at. Default to leaving it alone.
        if [ "$(blkid -s TYPE -o value "$PART_SWAP" 2>/dev/null)" = "swap" ]; then
            echo "    $PART_SWAP is already swap — another system may resume from it."
            prompt "Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"
            answer remake_swap _mk -m yes=y,no=n,true=y,false=n || true
            case "${_mk,,}" in y|yes) FORMAT_SWAP="yes" ;; *) FORMAT_SWAP="no" ;; esac
        fi
        break
    done

    # Say exactly which devices are about to be written, and nothing implied.
    echo ""
    echo "  $(bold 'These partitions will be FORMATTED') — everything on them is lost:"
    echo "    root : $PART_ROOT  → $ROOT_FS$([ "$ENCRYPT" = "yes" ] && echo " inside LUKS2")"
    [ -n "$PART_BOOT" ] && echo "    /boot: $PART_BOOT  → ext4"
    if [ -n "$PART_SWAP" ]; then
        if [ "$FORMAT_SWAP" = "yes" ]; then
            echo "    swap : $PART_SWAP  → mkswap"
        else
            echo "    swap : $PART_SWAP  → used as-is, not re-made"
        fi
    fi
    if [ -n "$PART_EFI" ]; then
        if [ "$FORMAT_ESP" = "yes" ]; then
            echo "    ESP  : $PART_EFI  → FAT32"
        else
            echo "    ESP  : $PART_EFI  → kept as-is, mounted only"
        fi
    fi
    echo ""
    echo "  Everything else on $DISK is left untouched."
    echo ""
    prompt "Type 'yes' to format these:"
    answer confirm_format confirm -m yes=yes,true=yes
    [ "$confirm" = "yes" ] || die "Aborted"

    if [ -n "$PART_EFI" ] && [ "$FORMAT_ESP" = "yes" ]; then
        echo "  Formatting EFI partition..."
        mkfs.fat -F32 "$PART_EFI" || die "Failed to format $PART_EFI"
    fi
    luks_format_root            # no-op unless encrypting; sets ROOT_FS_DEV
    format_and_mount_root       # mkfs + mount, and btrfs subvolumes if chosen

    if [ -n "$PART_BOOT" ]; then
        echo "  Formatting /boot partition..."
        mkfs.ext4 -F "$PART_BOOT" || die "Failed to format $PART_BOOT"
        mkdir -p /mnt/boot
        mount "$PART_BOOT" /mnt/boot || die "Failed to mount /boot"
    fi

    if [ -n "$PART_EFI" ]; then
        ESP_MOUNT="$(layout_esp_mount "$BOOTLOADER")"
        mkdir -p "/mnt${ESP_MOUNT}"
        mount "$PART_EFI" "/mnt${ESP_MOUNT}" || die "Failed to mount EFI at $ESP_MOUNT"
    fi

    # swapon, not just mkswap. fstab comes from `genfstab -U /mnt` further down,
    # and genfstab writes a swap line for swap that is ON at that moment and for
    # nothing else — so a swap made here but left off would work for this session
    # and be forgotten at the first boot, which is the failure nobody notices
    # until the machine starts OOM-killing.
    if [ -n "$PART_SWAP" ]; then
        if [ "$FORMAT_SWAP" = "yes" ]; then
            echo "  Making swap on $PART_SWAP..."
            mkswap "$PART_SWAP" >/dev/null || die "mkswap failed on $PART_SWAP"
        fi
        swapon "$PART_SWAP" || die "swapon $PART_SWAP failed"
        success "Swap enabled ($PART_SWAP, $(lsblk -dno SIZE "$PART_SWAP" 2>/dev/null))"
    fi

elif [ "$BOOT_MODE" = "uefi" ]; then
    warn "This will ERASE all data on $DISK"
    prompt "Type 'yes' to confirm:"
    answer confirm_erase confirm -m yes=yes,true=yes
    [ "$confirm" = "yes" ] || die "Aborted"

    echo "  Creating GPT partition table..."
    ESP_MIB="$(layout_esp_size_mib "$BOOTLOADER" "$SNAPSHOTS")"
    ESP_END=$((1 + ESP_MIB))
    parted -s "$DISK" mklabel gpt
    parted -s "$DISK" mkpart ESP fat32 1MiB "${ESP_END}MiB"
    parted -s "$DISK" set 1 esp on
    if [ "$SEPARATE_BOOT" = "yes" ]; then
        # ESP, a plain ext4 /boot for the kernel + initramfs, then the root.
        BOOT_END=$((ESP_END + 1024))
        parted -s "$DISK" mkpart boot ext4 "${ESP_END}MiB" "${BOOT_END}MiB"
        parted -s "$DISK" mkpart root "$ROOT_FS" "${BOOT_END}MiB" 100%
        PART_EFI="$(part_name "$DISK" 1)"
        PART_BOOT="$(part_name "$DISK" 2)"
        PART_ROOT="$(part_name "$DISK" 3)"
    else
        parted -s "$DISK" mkpart root "$ROOT_FS" "${ESP_END}MiB" 100%
        PART_EFI="$(part_name "$DISK" 1)"
        PART_ROOT="$(part_name "$DISK" 2)"
    fi

    partprobe "$DISK" 2>/dev/null || true
    sleep 2

    echo "  Formatting EFI partition (${ESP_MIB} MiB)..."
    mkfs.fat -F32 "$PART_EFI" || die "Failed to format EFI partition"
    luks_format_root            # no-op unless encrypting; sets ROOT_FS_DEV
    format_and_mount_root       # mkfs + mount, and btrfs subvolumes if chosen

    if [ "$SEPARATE_BOOT" = "yes" ]; then
        echo "  Formatting /boot partition..."
        mkfs.ext4 -F "$PART_BOOT" || die "Failed to format boot partition"
        mkdir -p /mnt/boot
        mount "$PART_BOOT" /mnt/boot || die "Failed to mount /boot"
    fi

    # systemd-boot reads the ESP and nothing else, so the ESP IS /boot and the
    # kernels install straight onto it. GRUB keeps it one level down.
    ESP_MOUNT="$(layout_esp_mount "$BOOTLOADER")"
    mkdir -p "/mnt${ESP_MOUNT}"
    mount "$PART_EFI" "/mnt${ESP_MOUNT}" || die "Failed to mount EFI at $ESP_MOUNT"
else
    # BIOS/MBR whole-disk. Alongside is UEFI-only, so BIOS is always an erase.
    warn "This will ERASE all data on $DISK"
    prompt "Type 'yes' to confirm:"
    answer confirm_erase confirm -m yes=yes,true=yes
    [ "$confirm" = "yes" ] || die "Aborted"

    echo "  Creating MBR partition table..."
    parted -s "$DISK" mklabel msdos
    if [ "$SEPARATE_BOOT" = "yes" ]; then
        # Same split as UEFI, minus the ESP: GRUB's core reads a plain /boot.
        parted -s "$DISK" mkpart primary ext4 1MiB 1025MiB
        parted -s "$DISK" set 1 boot on
        parted -s "$DISK" mkpart primary "$ROOT_FS" 1025MiB 100%
        PART_BOOT="$(part_name "$DISK" 1)"
        PART_ROOT="$(part_name "$DISK" 2)"
    else
        parted -s "$DISK" mkpart primary "$ROOT_FS" 1MiB 100%
        parted -s "$DISK" set 1 boot on
        PART_ROOT="$(part_name "$DISK" 1)"
    fi

    partprobe "$DISK" 2>/dev/null || true
    sleep 2

    luks_format_root            # no-op unless encrypting; sets ROOT_FS_DEV
    format_and_mount_root
    if [ "$SEPARATE_BOOT" = "yes" ]; then
        echo "  Formatting /boot partition..."
        mkfs.ext4 -F "$PART_BOOT" || die "Failed to format boot partition"
        mkdir -p /mnt/boot
        mount "$PART_BOOT" /mnt/boot || die "Failed to mount /boot"
    fi
fi

success "Disk partitioned and mounted at /mnt"

# ── Install base system ───────────────────────────────────
header
step "Step 3 — Installing Base System"

cat > /etc/pacman.d/mirrorlist << 'MIRROREOF'
Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch
Server = https://mirror.rackspace.com/archlinux/$repo/os/$arch
Server = https://mirrors.kernel.org/archlinux/$repo/os/$arch
MIRROREOF

pacman -Sy --noconfirm 2>/dev/null || true

echo "  Initializing pacman keyring..."
pacman-key --init
pacman-key --populate archlinux

echo "  Running pacstrap (this may take several minutes)..."
# Only things every install needs go here — this runs BEFORE step 4 asks what to
# install, so anything in this list is unconditional by construction.
#
# dolphin (Qt6 + KF6) and wine + wine-mono used to be on this line, which meant a
# Minimal install — the VM/small-disk/headless case the presets exist for — paid
# for the two largest optional dependency trees in the install with no way to say
# no. wine is WANT_WINE in step 4 now and is installed below; dolphin is not
# installed by any preset any more (2026-08-13) — synfiles is THE file manager,
# and `synpkg install dolphin` is one command for anyone who wants the KDE one.
# Filesystem tooling for the root that was actually chosen. `base` pulls in
# e2fsprogs and nothing else, so without this an xfs or btrfs root ships with no
# fsck and no repair tool on the one machine that will eventually need them —
# and mkinitcpio's fsck hook silently skips a filesystem whose fsck is absent.
FS_PKGS="$(fs_target_pkgs "$ROOT_FS")"

# Snapshot stack, only when asked for. grub-btrfs generates the boot entries and
# grub-btrfsd regenerates them when a snapshot appears; snap-pac is what makes
# every pacman transaction bracketed by a pre/post pair.
SNAP_PKGS=""
if [ "$SNAPSHOTS" = "yes" ]; then
    SNAP_PKGS="snapper snap-pac inotify-tools"
    # The thing that turns snapshots into BOOT ENTRIES differs per loader:
    # grub-btrfs generates a GRUB submenu, limine-snapper-sync copies each
    # snapshot's kernel onto the ESP and writes limine.conf entries.
    case "$BOOTLOADER" in
        grub)   SNAP_PKGS="$SNAP_PKGS grub-btrfs" ;;
        limine) SNAP_PKGS="$SNAP_PKGS limine limine-snapper-sync" ;;
    esac
fi
# limine itself is needed whether or not snapshots are on.
[ "$BOOTLOADER" = "limine" ] && SNAP_PKGS="$SNAP_PKGS limine"

# arch-audit and lynis are in the base set on purpose. They are ~6.5 MB
# together, and a security-focused OS that cannot tell you whether its packages
# have known CVEs until you first install a tool is telling on itself. lynis
# reads the skip-test profile synapse_kmod ships at /etc/lynis/custom.prf.
# (No inline comments below — the list is one backslash-continued command.)
pacstrap /mnt \
    base linux linux-firmware linux-headers kitty foot \
    grub efibootmgr os-prober ntfs-3g \
    $FS_PKGS $SNAP_PKGS \
    networkmanager wireless-regdb iw openssh sudo \
    seatd ttf-dejavu \
    xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk slurp \
    rtkit polkit-gnome xorg-xhost \
    mkinitcpio dkms \
    cryptsetup \
    zram-generator \
    arch-audit lynis \
    2>&1 || die "pacstrap failed — check network connection"

# Hard verify grub landed in the chroot
if ! arch-chroot /mnt which grub-install &>/dev/null; then
    warn "grub-install not found in chroot — attempting recovery..."
    arch-chroot /mnt pacman -S --noconfirm grub efibootmgr 2>&1 \
        || die "Could not install grub into target — check network"
fi

success "Base system installed"

# ── Install SynapseOS packages ────────────────────────────
header
step "Step 4 — Choose What to Install"

# Everything used to be installed unconditionally. The presets below keep that
# available (Full) and make it the thing you get by pressing Enter through the
# defaults you would have got anyway, while letting a small disk, a VM or a
# headless box drop the parts it does not want.
#
# Choices are ALL gathered here, before anything is installed, including the
# ones applied much later in the desktop step — an installer that asks a new
# question twenty minutes into a copy is one you cannot walk away from.
#
# The AI model is the single biggest item on the disk by an order of magnitude
# (~4.1 GB against ~1 GB for every SynapseOS package combined), which is why it
# is a question of its own rather than folded into a preset's package list —
# and since the ISO stopped carrying a gguf it is also a download of that size
# rather than a copy off the media, which is the other half of why it is asked
# on every preset instead of only under Custom.

# ── Choose, then confirm; "no" asks again ─────────────────
#
# The loop is in place rather than a re-exec like the disk plan: pacstrap has
# already run by here, so restarting the installer would repartition the disk.
# What makes looping safe is that the defaults below are re-initialised on every
# pass — WANT_* and every row in PICKED start from Standard again, so a second
# pass cannot inherit a package the first one ticked.
while :; do
    # Defaults = Standard, for the checkbox pages and the y/n groups alike.
    sel_reset standard
    WANT_MODEL=1          # fetch a gguf — WHICH one is asked below
    MODEL_CHOICE=mistral-7b
    MODEL_LABEL="Mistral 7B Instruct (~4.1 GB)"
    WANT_BLUETOOTH=1      # bluez + bluez-utils
    WANT_PRINTING=1       # cups + drivers
    WANT_WINE=1           # wine + wine-mono
    WANT_PHONE=1          # kdeconnect — pair a phone with the desktop
    # Steam is Full-only, not Standard. It is the only item here that needs a
    # whole second architecture on the disk ([multilib] plus the lib32 tree,
    # ~1.5 GB before a single game — plus ~1.6 GB for CachyOS Proton), so it is
    # opt-in the way the AI model is rather than something a default
    # Enter-through install pays for.
    WANT_STEAM=0          # steam + the 32-bit stack — see "Installing Steam"
    # BlackArch is a REPOSITORY, not a package set: enabling it installs the
    # keyring and nothing else, so the cost is a database sync rather than
    # thousands of tools. That is why it defaults ON where Steam does not —
    # SYNAPSE Arsenal is only useful if the repo it browses is there.
    WANT_BLACKARCH=1      # [blackarch] repo + keyring — see "Enabling BlackArch"
    # Nix is a SECOND package manager, and that is the whole reason it is
    # opt-in rather than Standard. Nothing on the system needs it: pacman owns
    # the compositor, the daemons and the drivers, and `syn nix` is a front end
    # for a layer the user chose to add on top. It also brings a /nix store on
    # the root filesystem that grows with every generation kept, which is not a
    # cost an Enter-through install should quietly take on.
    WANT_NIX=0            # nix + Home Manager — see "Configuring Nix"
    # Which SynapseOS packages and which shelf software: SEL_COMPONENTS and the
    # five SEL_SW_* tables above the test seam, one row per checkbox, with the
    # Standard and Full defaults as columns of the table rather than as three
    # separate lists that can drift apart. sel_reset() has just filled PICKED
    # from the Standard column; the preset arms below re-fill it, and Custom
    # draws the pages.
    #
    # linux-wallpaperengine and synapse-wallpapers are ONE row on purpose. The
    # renderer without our wallpapers is a player with nothing to play on a box
    # that has no Steam; our wallpapers without the renderer is worse — synui's
    # picker lists the rows and applying one does nothing, because wppick scans
    # /usr/share/synapse/wallpapers/431960 whether or not anything can render
    # what it finds. Same for synguard and its kernel module.

    echo "  What should be installed alongside the SynapseOS core?"
    echo ""
    # Name what DIFFERS, not what they share. Both of these lines used to list
    # the same six shared items and put the only difference — Steam — at the
    # far right, past 100 columns, where a terminal cut it off: Full and
    # Standard read as the same preset. Every line here stays under 80.
    echo "    $(bold '1)') Full      — Standard + Steam + Nix + more software"
    echo "    $(bold '2)') Standard  — the SynapseOS suite, Firefox, AI model,"
    echo "                   Bluetooth, printing, Wine, phone   (default)"
    echo "    $(bold '3)') Minimal   — core daemons only: none of the above"
    echo "    $(bold '4)') Custom    — tick every package yourself, ours and"
    echo "                   the ordinary software people install anyway"
    echo ""
    echo "  Every preset except Minimal then asks WHICH AI model to download,"
    echo "  and skipping it is one of the answers."
    echo ""
    pick "Choice [1-4, default=2]:" preset install_preset 2 "1 2 3 4" \
         -m full=1,standard=2,minimal=3,custom=4
    INSTALL_PRESET="$install_preset"

    case "$INSTALL_PRESET" in
        1)
            # samsung-m2020 is NOT here, and cannot be: its EULA forbids
            # redistribution, so archiso/build.sh deliberately keeps it out of
            # the ISO's local repo (see the comment there). Naming it in a
            # preset asked pacman for a target the media does not have, which
            # loses the WHOLE single transaction below — every SynapseOS
            # package with it — and is only rescued by the one-at-a-time retry
            # in the verify step. `syn printer samsung` installs the driver
            # from Samsung after the install, which is the supported route.
            sel_reset full
            WANT_MODEL=1; WANT_BLUETOOTH=1; WANT_PRINTING=1
            WANT_WINE=1; WANT_PHONE=1; WANT_STEAM=1
            WANT_BLACKARCH=1; WANT_NIX=1
            success "Full install selected"
            ;;
        3)
            sel_reset minimal
            WANT_MODEL=0; WANT_BLUETOOTH=0; WANT_PRINTING=0
            WANT_WINE=0; WANT_PHONE=0; WANT_STEAM=0
            WANT_BLACKARCH=0; WANT_NIX=0
            success "Minimal install selected"
            ;;
        4)
            echo ""
            echo "  Two kinds of question. First the packages, as pages of"
            echo "  checkboxes; then the handful of options that are a whole"
            echo "  subsystem rather than a package."
            echo ""

            # ── The checkbox pages ────────────────────────
            #
            # EVERY SynapseOS package is here, including the ones that used to
            # be installed whatever you answered. What keeps a nonsense
            # combination from reaching pacman is sel_resolve_deps() below,
            # which re-ticks what a kept package hard-depends on and names it.
            #
            # No M2020 question anywhere on these pages: the driver is not on
            # the media to install, and naming a package the local repo does
            # not carry loses the whole transaction it is in.
            # `syn printer samsung` fetches it from Samsung afterwards.
            multi_select "SynapseOS packages — everything the system is made of" \
                         SEL_COMPONENTS
            echo ""
            echo "  And the software people install on the first evening anyway."
            echo "  All of it is in the Arch repositories; none of it is ours."
            multi_select "Web and communication" SEL_SW_WEB
            multi_select "Audio and video"       SEL_SW_MEDIA
            multi_select "Office and graphics"   SEL_SW_OFFICE
            multi_select "Development and admin" SEL_SW_DEV
            multi_select "Games, launchers and helpers" SEL_SW_GAMES

            # ── The subsystem questions ───────────────────
            #
            # These stay one-at-a-time y/n rather than joining a page: each one
            # is a repository, an architecture or a service, not a name pacman
            # installs — and each has a sentence of consequence that does not
            # fit in a 15-column description.
            echo ""
            echo "  The rest is y/n. The default (shown in caps) is Standard."
            echo ""
            ask_opt() {   # ask_opt <varname> <default 0|1> <description>
                local __var=$1 __def=$2 __desc=$3 __hint __ans
                if [ "$__def" = 1 ]; then __hint="[Y/n]"; else __hint="[y/N]"; fi
                prompt "$__desc $__hint:"
                answer "${__var,,}" __ans -m yes=y,no=n,true=y,false=n || true
                case "${__ans,,}" in
                    y|yes) printf -v "$__var" '%s' 1 ;;
                    n|no)  printf -v "$__var" '%s' 0 ;;
                    *)     printf -v "$__var" '%s' "$__def" ;;
                esac
            }

            # No "AI model? y/n" here: the model question is asked once for
            # every preset below, and "None" is one of its answers.
            ask_opt WANT_BLUETOOTH  1 "Bluetooth support"
            ask_opt WANT_PRINTING   1 "Printing (CUPS) — a driver the OS cannot ship: 'syn printer <vendor>'"
            ask_opt WANT_WINE       1 "Wine — run Windows .exe/.msi (adds wine + wine-mono)"
            ask_opt WANT_PHONE      1 "KDE Connect — pair a phone (notifications, files, clipboard)"
            ask_opt WANT_STEAM      0 "Steam + game stack (mangohud/gamemode/gamescope) + CachyOS Proton — enables [multilib] and [cachyos] (~3.1 GB)"
            ask_opt WANT_BLACKARCH  1 "BlackArch security repo — ~5000 tools browsable in SYNAPSE Arsenal"
            ask_opt WANT_NIX        0 "Nix + Home Manager — a declarative user environment beside pacman ('syn nix')"

            # The two deselections worth stopping on. Both are allowed — they
            # were asked for — but each has a consequence that only shows up
            # weeks later, and by then the install screen is long forgotten.
            sel_on comp_synupdate || warn "syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."
            if ! sel_on comp_synui && ! sel_on comp_synapd; then
                warn "Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."
            fi
            success "Custom install configured"
            ;;
        *)
            success "Standard install selected"
            ;;
    esac

    # ── Which AI model ────────────────────────────────────
    #
    # Asked for every preset that wants one, because it is no longer a copy off
    # the media: the ISO ships without a gguf (it added ~4 GB to the image for a
    # model the live session can only run on the CPU), so this is a download of
    # gigabytes onto the new system and a download that size is a decision, not
    # something an Enter-through install starts on your behalf.
    #
    # The names are syn-model's own — `syn model download <name>` takes exactly
    # these three — so the pick made here is the pick available afterwards, and
    # "None" leaves the machine in the same state declining it always did.
    #
    # The quality warning is the point of the screen. Size is the only number a
    # first-time installer has to go on, and read alone it says "smaller is
    # thriftier"; what it actually costs is every AI feature in the OS at once.
    if [ "$WANT_MODEL" = 1 ]; then
        # Pre-picker profiles said `want.model = false`. Honour that as "None",
        # and read the key ONLY when it is present so nobody at a terminal is
        # asked the old yes/no question as well as the new one.
        if [ -n "${ANSWERS[want_model]+set}" ]; then
            # Consumed either way, so a profile carrying it is not told the key
            # went unused — but ai_model is the specific answer to the same
            # question, so where both are written the specific one wins.
            answer want_model _legacy_model -m yes=y,no=n,true=y,false=n || true
            if [ -z "${ANSWERS[ai_model]+set}" ]; then
                case "${_legacy_model,,}" in n|no) WANT_MODEL=0 ;; esac
            fi
        fi
    fi

    if [ "$WANT_MODEL" = 1 ]; then
        echo ""
        echo "  $(bold 'Which AI model should this machine run?')"
        echo ""
        echo "  synapd loads one model and everything AI in SynapseOS talks to it:"
        echo "  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,"
        echo "  over this connection, onto the disk you are installing to."
        echo ""
        echo "    $(bold '1)') Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"
        echo "    $(bold '2)') Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"
        echo "    $(bold '3)') Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"
        echo "    $(bold '4)') None                            skip it — nothing else changes"
        echo ""
        echo "  A smaller model is not just faster and lighter: it follows"
        echo "  instructions worse. synsh mistakes what you asked for, Vibe's code"
        echo "  needs more fixing, Chibi loses the thread. Take the default unless"
        echo "  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."
        echo ""
        echo "  Whatever you pick, it can be changed later: 'syn model download',"
        echo "  or Super+C ▸ System ▸ AI model on the desktop."
        echo ""
        pick "Choice [1-4, default=1]:" ai_model model_pick 1 "1 2 3 4" -m mistral-7b=1,mistral=1,phi3=2,phi=2,tiny=3,qwen=3,none=4,no=4,skip=4
        # Enter is the recommendation, so an empty answer and a typed 1 are the
        # same arm — pick() has already substituted the default by here.
        case "$model_pick" in
            1) MODEL_CHOICE=mistral-7b; MODEL_LABEL="Mistral 7B Instruct (~4.1 GB)" ;;
            2) MODEL_CHOICE=phi3;       MODEL_LABEL="Phi-3 Mini 4K (~2.2 GB)" ;;
            3) MODEL_CHOICE=tiny;       MODEL_LABEL="Qwen2 0.5B (~0.4 GB)" ;;
            4) MODEL_CHOICE=none;       WANT_MODEL=0 ;;
            # No catch-all arm: pick() cannot return anything else. A typo asks
            # again, and an ai_model= naming a model that does not exist stops
            # the install by name rather than standing in for the answer with a
            # multi-gigabyte download nobody chose.
        esac
        [ "$WANT_MODEL" = 1 ] && success "AI model: $MODEL_LABEL"
    else
        MODEL_CHOICE=none
    fi

    # Dependencies BEFORE the read-back, never after it: sel_resolve_deps re-ticks
    # what a kept package cannot run without, and the summary is the one place
    # the user finds out. Resolving it after the confirmation would make the
    # summary the only thing on screen that is not what gets installed.
    #
    # It also runs on every preset, not just Custom — Minimal drops the app
    # group wholesale, and Full turns everything on, so neither can produce a
    # broken pair, but a profile can name any combination it likes.
    sel_resolve_deps

    # The three lists downstream still speaks in. `core` is what decides service
    # enablement below; `sw` is not ours and is installed in its own transaction
    # after the SynapseOS one, so a name that has been renamed upstream cannot
    # take our packages down with it.
    SEL_CORE="$(sel_packages core)"
    SEL_APPS="$(sel_packages app)"
    SEL_SOFTWARE="$(sel_packages sw)"
    SEL_FLATPAKS="$(sel_packages flat)"

    # Read the selection back before touching the disk. A picker whose result you
    # only discover afterwards is worse than no picker.
    echo ""
    echo "  $(bold 'Installing:')"
    echo "    Core     : $(echo $SEL_CORE | wc -w) package(s)"
    echo "    Apps     : ${SEL_APPS:-none}"
    echo "    Software : ${SEL_SOFTWARE:-none}"
    echo "    AI model : $([ "$WANT_MODEL" = 1 ] && echo "$MODEL_LABEL — downloaded during the install" || echo 'none — the AI is inert until "syn model download"')"
    echo "    Bluetooth: $([ "$WANT_BLUETOOTH" = 1 ] && echo yes || echo no)"
    echo "    Printing : $([ "$WANT_PRINTING" = 1 ] && echo yes || echo no)"
    echo "    Wine     : $([ "$WANT_WINE" = 1 ] && echo yes || echo no)"
    echo "    Phone    : $([ "$WANT_PHONE" = 1 ] && echo 'yes (KDE Connect)' || echo no)"
    echo "    Steam    : $([ "$WANT_STEAM" = 1 ] && echo 'yes (+ mangohud/gamemode/gamescope + CachyOS Proton, enables multilib and [cachyos])' || echo no)"
    echo "    BlackArch: $([ "$WANT_BLACKARCH" = 1 ] && echo 'yes (repo + keyring only, no tools installed)' || echo no)"
    echo "    Nix      : $([ "$WANT_NIX" = 1 ] && echo 'yes (nix + Home Manager config, nothing built until "syn nix apply")' || echo no)"
    echo ""
    # ── Confirm the selection ─────────────────────────────
    #
    # Same rule as the disk plan: read it back, then ask. Nothing is installed
    # until Step 4b below, so "no" costs only the questions again.
    prompt "Install this selection? [Y/n]:"
    answer selection_ok _sel_ok -m yes=y,no=n,true=y,false=n || true
    case "${_sel_ok,,}" in
        n|no)
            echo ""
            warn "Choosing again — nothing has been installed yet."
            continue
            ;;
    esac
    break
done

step "Step 4b — Installing SynapseOS"

LIVE_REPO="/run/archiso/airootfs/local-repo"
CHROOT_REPO="/mnt/var/cache/synapseos"

mkdir -p "$CHROOT_REPO"
cp "$LIVE_REPO"/*.pkg.tar.zst "$CHROOT_REPO/" 2>/dev/null || true

arch-chroot /mnt bash -c "
    cd /var/cache/synapseos && \
    repo-add synapseos.db.tar.gz *.pkg.tar.zst 2>/dev/null && \
    ln -sf synapseos.db.tar.gz synapseos.db && \
    ln -sf synapseos.files.tar.gz synapseos.files
"

cat >> /mnt/etc/pacman.conf << REPOEOF

[synapseos]
SigLevel = Optional TrustAll
Server = file:///var/cache/synapseos
REPOEOF

# The Pac-Man progress bar, on by default. Cosmetic, so a failure warns and the
# install carries on — see pacman_conf_enable_candy() above the test seam.
pacman_conf_enable_candy /mnt/etc/pacman.conf \
    || warn "Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."

# ── [multilib], only when Steam was asked for ─────────────
#
# steam is a multilib package and half its dependency tree is lib32-*, so
# without this repo it is not "not installed", it is uninstallable — pacman
# cannot even resolve the name. This is why Steam was never an option here.
#
# Enabled conditionally rather than always: turning on a second architecture
# is a permanent property of the installed system, and a Minimal install has
# no use for it. Everything below is idempotent, so a re-run is harmless.
#
# Uncommenting the stock block is preferred over appending a new section so
# the file stays the canonical pacman.conf. The anchor is safe: '^#\[multilib\]'
# requires the literal ']', so it cannot match '#[multilib-testing]' above it.
# The result is verified rather than trusted — a silently-unenabled repo would
# surface much later as a confusing "target not found: steam".
if [ "$WANT_STEAM" = 1 ]; then
    if ! grep -q '^\[multilib\]' /mnt/etc/pacman.conf; then
        sed -i '/^#\[multilib\]/,/^#Include/ s/^#//' /mnt/etc/pacman.conf
    fi
    # Fallback for a pacman.conf that never carried the commented block.
    if ! grep -q '^\[multilib\]' /mnt/etc/pacman.conf; then
        printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /mnt/etc/pacman.conf
    fi
    if grep -q '^\[multilib\]' /mnt/etc/pacman.conf; then
        echo "  Enabling [multilib] (32-bit repo, needed by Steam)..."
        arch-chroot /mnt pacman -Sy --noconfirm 2>&1 \
            || warn "Could not sync the multilib database — Steam may fail to install"
    else
        warn "Could not enable [multilib]; Steam will be skipped."
        WANT_STEAM=0
    fi
fi

# synapse-llama carries libllama/libggml and is pulled in automatically as a
# dependency of synapd — do NOT copy those libraries in by hand.
#
# This used to `cp /run/archiso/airootfs/usr/lib/libllama* /mnt/usr/lib/`. Two
# bugs in one line: the files landed owned by no package (pacman could never
# upgrade them, and a stale set silently shadowed the real libraries in the
# ld.so cache — synapd ran a months-old CPU-only libllama while claiming GPU
# offload was on), and plain `cp` dereferenced the soname symlinks, writing
# three identical 4 MB regular files per library instead of a symlink chain,
# which is what made ldconfig warn "is not a symbolic link" on every pacman run.
#
# Guarded on being non-empty: every SynapseOS package is a checkbox now, so
# "none of them" is a legal answer, and `pacman -Sy` with no targets is a
# database refresh that reports success — which would read as a transaction
# that worked.
if [ -n "$SEL_CORE$SEL_APPS" ]; then
    arch-chroot /mnt pacman -Sy --noconfirm $SEL_CORE $SEL_APPS \
        2>&1 || warn "Some SynapseOS packages failed to install — verifying below"
else
    arch-chroot /mnt pacman -Sy --noconfirm 2>&1 || true
    warn "No SynapseOS packages were selected. This will be an Arch system."
fi

# ── Hard verify the SynapseOS packages landed ─────────────
#
# The install above is a SINGLE transaction, so one unresolvable dependency
# loses every package in it — and `|| warn` then let the installer carry on and
# still print "SynapseOS installation complete!" at the end. grub has been hard
# verified here for ages; the packages the distro exists for were not checked at
# all. A box without synapd/synguard/synapse_kmod is not SynapseOS, so say so
# instead of handing over a machine that quietly isn't one.
#
# Retry individually before giving up: that way one bad package fails alone
# instead of taking the other nine with it, and the error names the culprit.
# syn-update is core, not an app: without it an installed system can never
# receive another SynapseOS package again, which is the gap it exists to close.
#
# Verify what was SELECTED in step 4, not a fixed list. Hard-failing on a
# package the user deliberately declined would make Minimal and Custom
# impossible to complete — the guard has to enforce the user's choice, not
# override it.
SYN_CORE="$SEL_CORE"
# Apps, not the OS. They ship in the live repo but were never installed to disk,
# so an installed SynapseOS had no chibi at all — you only got her on the ISO.
SYN_EXTRA="$SEL_APPS"

syn_missing() {
    local out="" p
    for p in $1; do
        arch-chroot /mnt pacman -Q "$p" &>/dev/null || out="$out $p"
    done
    printf '%s' "$out"
}

missing=$(syn_missing "$SYN_CORE $SYN_EXTRA")
if [ -n "$missing" ]; then
    warn "Not installed:$missing — retrying each on its own..."
    for p in $missing; do
        arch-chroot /mnt pacman -S --noconfirm "$p" 2>&1 \
            || warn "  $p failed"
    done
fi

# These are apps, not the OS — a missing one is worth saying out loud but must
# not fail an otherwise good install.
missing_extra=$(syn_missing "$SYN_EXTRA")
[ -n "$missing_extra" ] && warn "App(s) not installed:$missing_extra"

missing_core=$(syn_missing "$SYN_CORE")
if [ -n "$missing_core" ]; then
    die "SynapseOS packages could not be installed:$missing_core
  This machine would boot without the daemons SynapseOS is built on.
  Usually a network problem, or a dependency the live repo cannot resolve.
  Check the pacman output above."
fi

if [ -n "$SYN_EXTRA" ]; then
    success "SynapseOS packages verified: $(echo $SYN_CORE | wc -w) core, \
$(( $(echo $SYN_EXTRA | wc -w) - $(echo $missing_extra | wc -w) ))/$(echo $SYN_EXTRA | wc -w) apps"
else
    success "SynapseOS packages verified: $(echo $SYN_CORE | wc -w) core, no apps selected"
fi

success "SynapseOS packages installed"

# ── Record what was picked, and what was not ──────────────
#
# syn-update reads this to tell a component the user DECLINED from one the tree
# has gained since. Without it, "in the tree and not on the disk" is a single
# undifferentiated state and the first `syn-update apply` reinstalls everything
# that was unticked here — undoing this whole screen, silently, on a machine
# whose owner already answered the question.
#
# Written even when nothing was selected: an empty selection is an answer too,
# and the file's ABSENCE is what means "never asked".
mkdir -p /mnt/etc/synapseos
sel_manifest > /mnt/etc/synapseos/components.conf
chmod 644 /mnt/etc/synapseos/components.conf
success "Component selection recorded in /etc/synapseos/components.conf"

# ── User account ─────────────────────────────────────────
header
step "Step 5 — Create User Account"

echo "  Create a user account for the installed system."
echo ""
prompt "Username [default: syn]:"
answer username NEW_USER || true
NEW_USER="${NEW_USER:-syn}"

prompt "Full name (optional):"
answer fullname NEW_FULLNAME || true

while true; do
    prompt "Password:"
    answer password NEW_PASS -s; echo
    prompt "Confirm password:"
    answer password NEW_PASS2 -s; echo
    if [ "$NEW_PASS" = "$NEW_PASS2" ] && [ -n "$NEW_PASS" ]; then
        break
    fi
    fail "Passwords do not match or are empty — try again"
done

success "User '$NEW_USER' will be created"

# ── Desktop environment ──────────────────────────────────
header
step "Step 6 — Desktop Environment"

echo "  Choose a desktop environment:"
echo ""
# Row 1 is only honest while synui is on the disk. Deselecting it in step 4 is
# now a supported answer, and offering "SynapseUI" afterwards would install
# greetd, quickshell and a session that has no compositor to start — a machine
# that boots to a login screen and cannot get past it.
if sel_on comp_synui; then
    echo "    $(bold '1)') SynapseUI  — AI-native Wayland compositor  (default)"
else
    echo "    $(bold '1)') SynapseUI  — NOT AVAILABLE: synui was not selected"
fi
echo "    $(bold '2)') KDE Plasma — Full-featured Wayland desktop"
echo "    $(bold '3)') GNOME      — Clean, modern Wayland desktop"
echo "    $(bold '4)') TTY only   — No GUI (headless/server)"
echo ""
if sel_on comp_synui; then
    pick "Choice [1-4, default=1]:" desktop de_choice 1 "1 2 3 4" \
         -m synui=1,kde=2,gnome=3,tty=4
else
    # A profile that asks for both is a contradiction, and quietly resolving it
    # either way would hand back a machine nobody asked for. Say which two keys
    # disagree and stop — before the desktop is installed, not after.
    case "${ANSWERS[desktop]:-}" in
        synui|1) die "config: desktop=synui, but comp_synui=no — the desktop cannot be
  installed without the compositor. Set one of the two." ;;
    esac
    pick "Choice [2-4, default=4]:" desktop de_choice 4 "2 3 4" \
         -m kde=2,gnome=3,tty=4
fi
DE_CHOICE="$de_choice"

case "$DE_CHOICE" in
    2)
        # kde-applications-meta contains dolphin, so choosing Plasma still gets
        # Dolphin — same shape as bluez arriving via synui's depends. That is
        # the KDE desktop's own file manager, not a SynapseOS default: nothing
        # else in the install pulls it in any more.
        echo "  Installing KDE Plasma..."
        arch-chroot /mnt pacman -S --noconfirm \
            plasma-meta sddm kde-applications-meta \
            2>&1 || warn "Some KDE packages failed to install"
        success "KDE Plasma installed"
        ;;
    3)
        echo "  Installing GNOME..."
        # NOT the `gnome` group. The group is 58 packages and roughly a third
        # of them are a second copy of something SynapseOS already ships, which
        # is what an app grid holding both Files and Nautilus, both Text Editor
        # and gnome-text-editor, and two calculators actually looks like. The
        # session is what is wanted from GNOME here; the applications are not.
        #
        # DROPPED because SynapseOS ships the same thing:
        #   nautilus→synfiles  gnome-text-editor→syn-edit  gnome-console→syntty
        #   gnome-disk-utility→syn-disks  gnome-software→synpkg
        #   gnome-calculator→synui's Calculator  gnome-system-monitor→synui's
        #   task manager  gnome-music/decibels/showtime→cliamp  epiphany→firefox
        #
        # DROPPED as tour/PIM/docs noise on a distro that is not a general
        # desktop: gnome-tour gnome-user-docs yelp gnome-maps gnome-weather
        #   gnome-contacts gnome-clocks gnome-calendar gnome-characters
        #   gnome-font-viewer gnome-logs gnome-connections gnome-remote-desktop
        #   gnome-user-share rygel snapshot sushi grilo-plugins gst-thumbnailers
        #
        # KEPT deliberately even though they are GNOME apps: loupe, papers,
        # simple-scan, baobab and gnome-color-manager fill gaps SynapseOS has
        # no answer for (image viewer, PDF, scanner, disk usage, colour
        # profiles). Dropping those would not be de-duplicating, it would be
        # removing the only thing that does the job.
        arch-chroot /mnt pacman -S --noconfirm --needed \
            gdm gnome-shell gnome-session gnome-settings-daemon \
            gnome-control-center gnome-keyring gnome-menus gnome-backgrounds \
            xdg-desktop-portal-gnome xdg-user-dirs-gtk tecla malcontent orca \
            gvfs gvfs-afc gvfs-mtp gvfs-gphoto2 gvfs-smb gvfs-nfs \
            loupe papers simple-scan baobab gnome-color-manager \
            2>&1 || warn "Some GNOME packages failed to install"
        success "GNOME installed (session only — SynapseOS apps, not GNOME's)"
        ;;
    4) echo "  No GUI will be installed." ;;
    *)
        echo "  Installing greetd (login screen) + desktop extras..."
        # wtype is not optional decoration: it is how the bar's start menu asks
        # synui to open the control panel (virtual-keyboard-v1 is the only IPC
        # into the compositor). Without it that menu entry silently does nothing.
        #
        # quickshell is the bar as of 2026-07-24, replacing waybar — synui
        # depends on it, but name it here too so what an install pulls in is
        # readable rather than inferred from someone else's depends.
        # greetd/quickshell/swaybg/wtype are the desktop itself and are not
        # optional. Bluetooth and printing are, and were chosen in step 4 —
        # a VM or a headless box has no use for either.
        #
        # Declining Bluetooth does not remove the bluez LIBRARY: synui depends
        # on it, so it arrives regardless (verified on a Minimal install —
        # `pacman -Qi bluez` reports "Required By: synui"). What declining does
        # is skip bluez-utils and leave bluetooth.service disabled, so the radio
        # stays down and synui's panel reports no adapter. That is the whole
        # user-visible effect, and it is what the option promises.
        #
        # There is no dolphin line here any more (2026-08-13). It was the SECOND
        # file manager, and nothing opened it: synfiles is in SEL_CORE, is the
        # distribution default for inode/directory, and is what the bar's Files
        # button, the desktop right-click menu and the ISO mounter reach through
        # synui-open-folder. `synpkg install dolphin` afterwards for anyone who
        # wants the KDE one. synui still ships the /etc/xdg files that make KDE
        # apps work outside Plasma (applications.menu, kdeglobals) — see its
        # PKGBUILD — and kdeconnect below is one of those apps.
        DESKTOP_PKGS="greetd greetd-tuigreet quickshell swaybg python wtype"
        [ "$WANT_BLUETOOTH" = 1 ] && DESKTOP_PKGS="$DESKTOP_PKGS bluez bluez-utils"
        [ "$WANT_PRINTING"  = 1 ] && DESKTOP_PKGS="$DESKTOP_PKGS cups cups-pdf ghostscript nss-mdns"
        # Phone pairing, and the one thing left that drags the KF6 tree in — on
        # a Minimal install that is the whole cost, which is why it stays a
        # question rather than something every install pays for.
        [ "$WANT_PHONE"     = 1 ] && DESKTOP_PKGS="$DESKTOP_PKGS kdeconnect"
        arch-chroot /mnt pacman -S --noconfirm $DESKTOP_PKGS \
            2>&1 || warn "greetd failed to install — boot falls back to getty login"
        success "SynapseUI selected (included)"
        ;;
esac

# ── Wine ──────────────────────────────────────────────────
#
# wine (+ wine-mono, so .NET installers do not stop to download Mono on first
# run) makes .exe/.msi runnable out of the box: wine.desktop registers the
# Windows mimetypes and drives double-click, and synui's "Run with Wine"
# service menu drives the right-click entry. Chosen in step 4, not pacstrapped:
# it is a large tree and the Minimal preset exists to skip exactly this.
#
# Outside the DE case above because it is an application, not part of any one
# desktop — synui's service menu is the only integration and it is an
# optdepend there, harmless when wine is absent (see synui/PKGBUILD).
if [ "$WANT_WINE" = 1 ]; then
    step "Installing Wine"
    if arch-chroot /mnt pacman -S --noconfirm --needed wine wine-mono 2>&1; then
        success "Wine installed"
    else
        warn "wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."
    fi
fi

# ── Video driver ──────────────────────────────────────────
header
step "Configuring Video Driver"

# synui needs a working EGL/Vulkan stack. On modern NVIDIA cards nouveau
# provides neither, so the renderer autocreate fails and the session dies
# — the driver has to be installed here, before mkinitcpio and grub run.
# Detect display controllers from sysfs; the live ISO may not ship lspci.
#
# When Steam was asked for, each branch below also installs the 32-bit half of
# whatever driver it picked, in the SAME pacman transaction as the 64-bit half.
# Two reasons it is here and not in the Steam step:
#
#   - nvidia-utils and lib32-nvidia-utils must be the same version. Installed
#     together they are resolved as one transaction and cannot mismatch; done
#     as a later separate -S they can land either side of an upstream bump.
#   - steam depends on the VIRTUAL packages lib32-vulkan-driver and lib32-libgl,
#     which have several providers. Under --noconfirm pacman takes the first
#     provider rather than asking, so an NVIDIA box could silently get
#     lib32-vulkan-radeon and no working Vulkan. Naming the concrete driver
#     here satisfies the virtual dep up front, so no provider choice is ever
#     made on the user's behalf.
GPU_KERNEL_PARAMS=""
LIB32_PKGS=""
HAS_NVIDIA="" NVIDIA_DEVID=0 HAS_AMD="" HAS_INTEL=""
for dev in /sys/bus/pci/devices/*; do
    case "$(cat "$dev/class" 2>/dev/null)" in 0x03*) ;; *) continue ;; esac
    case "$(cat "$dev/vendor" 2>/dev/null)" in
        0x10de) HAS_NVIDIA=1; NVIDIA_DEVID=$(( $(cat "$dev/device") )) ;;
        0x1002) HAS_AMD=1 ;;
        0x8086) HAS_INTEL=1 ;;
    esac
done

if grep -qiE 'VirtualBox|VMware|QEMU|KVM|Xen|innotek' \
        /sys/class/dmi/id/sys_vendor 2>/dev/null; then
    echo "  Virtual machine — installing mesa (synui uses pixman here)..."
    [ "$WANT_STEAM" = 1 ] && LIB32_PKGS="lib32-mesa lib32-vulkan-icd-loader"
    arch-chroot /mnt pacman -S --noconfirm --needed mesa $LIB32_PKGS 2>&1 \
        || warn "mesa failed to install"
elif [ -n "$HAS_NVIDIA" ]; then
    # nvidia-open supports Turing and newer (PCI device id >= 0x1e00);
    # older cards need the proprietary kernel module.
    NVIDIA_PKG="nvidia-dkms"
    [ "$NVIDIA_DEVID" -ge $((0x1e00)) ] && NVIDIA_PKG="nvidia-open-dkms"
    echo "  NVIDIA GPU detected — installing $NVIDIA_PKG (builds the module, takes a while)..."
    # lib32-nvidia-utils provides both lib32-libgl and lib32-vulkan-driver, so
    # it alone satisfies steam's 32-bit graphics deps on this card. lib32-mesa
    # rides along to mirror the 64-bit `mesa` on the same line — libglvnd
    # dispatches between them, and a game falling back to llvmpipe beats one
    # that will not start.
    [ "$WANT_STEAM" = 1 ] && LIB32_PKGS="lib32-nvidia-utils lib32-mesa lib32-vulkan-icd-loader"
    arch-chroot /mnt pacman -S --noconfirm --needed \
        "$NVIDIA_PKG" nvidia-utils egl-wayland mesa $LIB32_PKGS 2>&1 \
        || die "NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"

    # Keep nouveau off the card, including in early boot: the 'kms'
    # mkinitcpio hook loads nouveau from the initramfs, so blacklisting
    # alone isn't enough — drop the hook too.
    echo "blacklist nouveau" > /mnt/etc/modprobe.d/blacklist-nouveau.conf
    sed -i '/^HOOKS=/s/ kms//' /mnt/etc/mkinitcpio.conf
    # wlroots needs KMS on the nvidia driver
    GPU_KERNEL_PARAMS="nvidia_drm.modeset=1"
    success "NVIDIA driver installed ($NVIDIA_PKG, nouveau blacklisted)"

    # ── The sleep units ───────────────────────────────────────
    #
    # nvidia-utils ships nvidia-{suspend,resume,hibernate}.service DISABLED and
    # they are what run nvidia-sleep.sh, which performs the VRAM save on suspend
    # and the restore on resume. The system-sleep hook the package also ships
    # covers only `post` (resume) — there is no `pre` case in it — so with the
    # units off, the suspend half of that pair simply never happens.
    #
    # That is only inert while NVreg_PreserveVideoMemoryAllocations is unset.
    # Set the param with the units off and the driver is told to dump VRAM by a
    # mechanism that never runs, so the GPU resumes with lost framebuffers: the
    # unrecoverable black screen of 2026-07-17 on velle's box, and the state
    # that box was found in again on 2026-08-06 with its primary display dead
    # after a resume. Both times the param came from a hand-written
    # /etc/modprobe.d file owned by no package — which is exactly the kind of
    # thing that survives a reinstall in a backup while `systemctl enable` state
    # does not. The 2026-07-31 reinstall restored the file and not the enable.
    #
    # So enable them here rather than leaving it to whoever sets the param
    # later: they cost nothing when the param is unset (nvidia-sleep.sh checks
    # it and returns), and they are impossible to discover as missing when it is
    # set. `|| true` because a driver package that ever drops these units should
    # not fail an install.
    arch-chroot /mnt systemctl enable \
        nvidia-suspend.service nvidia-resume.service nvidia-hibernate.service \
        2>/dev/null \
        && success "NVIDIA sleep services enabled (VRAM save/restore)" \
        || warn "Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"

    # ── Put inference on the GPU ──────────────────────────────
    #
    # The driver alone does nothing for synapd. synapd depends on synapse-llama,
    # which the pacstrap above resolved to the CPU build the ISO ships — so
    # until this ran, a SynapseOS install detected your card, installed the
    # right driver, and then ran the entire reason the distro exists on the CPU.
    # Silently. On a 4090. Forever. Nothing said a word, because a CPU build is
    # not an error; it is just slow.
    #
    # synapse-llama-cuda declares provides+conflicts on synapse-llama, so this
    # is a straight swap and pacman does the right thing. It comes from the
    # [synapseos] repo copied to /var/cache/synapseos above; `cuda` and
    # nvidia-utils come from Arch's repos over the network, which the install
    # already required. It is a large download (cuda is ~4.7 GiB installed),
    # hence the prompt — but default yes: someone installing on an NVIDIA box
    # wants the GPU, and the failure mode of not asking loudly enough is a
    # machine that quietly never uses it.
    if arch-chroot /mnt pacman -Si synapse-llama-cuda &>/dev/null; then
        echo ""
        echo "  synapd can run inference on this GPU instead of the CPU."
        echo "  This downloads the CUDA runtime (~4.7 GiB installed)."
        prompt "Enable GPU inference? [Y/n]:"
        answer gpu_inference gpu_ans -m yes=y,no=n,true=y,false=n
        case "${gpu_ans:-y}" in
            [Nn]*)
                warn "Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda" ;;
            *)
                echo "  Installing synapse-llama-cuda (this takes a while)..."
                swap_llama_backend synapse-llama-cuda libggml-cuda.so ;;
        esac
    else
        # The ISO was built without a CUDA toolkit on the build host, so it
        # carries no GPU package. Say so — the whole bug being fixed here is
        # that this case used to be indistinguishable from success.
        warn "This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"
    fi
else
    GPU_PKGS="mesa"
    [ -n "$HAS_AMD" ]   && GPU_PKGS="$GPU_PKGS vulkan-radeon"
    [ -n "$HAS_INTEL" ] && GPU_PKGS="$GPU_PKGS vulkan-intel"
    if [ "$WANT_STEAM" = 1 ]; then
        # Mirror the 64-bit choice exactly — same ICD, 32-bit half.
        LIB32_PKGS="lib32-mesa lib32-vulkan-icd-loader"
        [ -n "$HAS_AMD" ]   && LIB32_PKGS="$LIB32_PKGS lib32-vulkan-radeon"
        [ -n "$HAS_INTEL" ] && LIB32_PKGS="$LIB32_PKGS lib32-vulkan-intel"
    fi
    echo "  Installing video stack: $GPU_PKGS $LIB32_PKGS..."
    arch-chroot /mnt pacman -S --noconfirm --needed $GPU_PKGS $LIB32_PKGS 2>&1 \
        || warn "Video driver install failed — synui may fall back to software rendering"
    success "Video drivers installed"

    # ── Put inference on the GPU (AMD/Intel, via Vulkan) ──────────
    #
    # The NVIDIA branch above swaps synapse-llama -> synapse-llama-cuda so an
    # NVIDIA box doesn't run the whole reason the distro exists on the CPU.
    # This is the same swap for AMD/Intel, using the portable Vulkan build:
    # the ICD (vulkan-radeon/vulkan-intel) was just installed, and
    # synapse-llama-vulkan carries the Vulkan llama backend so synapd offloads.
    #
    # No prompt (unlike CUDA's ~4.7 GiB download): the Vulkan runtime is the
    # loader + the mesa ICD already installed here, so this is a small swap.
    # Still verify rather than trust — a silent CPU fallback is exactly the
    # failure this whole path exists to prevent.
    if [ -n "$HAS_AMD" ] || [ -n "$HAS_INTEL" ]; then
        if arch-chroot /mnt pacman -Si synapse-llama-vulkan &>/dev/null; then
            echo "  Enabling GPU inference (synapse-llama-vulkan)..."
            swap_llama_backend synapse-llama-vulkan libggml-vulkan.so
        else
            warn "This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"
        fi
    fi
fi

# ── Steam ─────────────────────────────────────────────────
#
# The native multilib package, not Flatpak: it uses the system's own driver
# stack (the lib32 half installed with the 64-bit driver just above), so
# Proton sees the same Vulkan ICD synui does, and syn-update keeps it current
# with everything else instead of it being a second update mechanism.
#
# Deliberately AFTER the video driver step. steam's 32-bit graphics deps are
# already satisfied by the concrete lib32 driver installed there, so pacman
# never reaches a provider choice for lib32-vulkan-driver / lib32-libgl — see
# the comment on LIB32_PKGS. The other virtual dep, ttf-font, is satisfied by
# the ttf-dejavu that pacstrap installed.
#
# steam-devices (udev rules for controllers) is a hard dependency and arrives
# on its own; it is not named here so there is one place pacman is deciding.
#
# Non-fatal: a machine that boots to a working desktop without Steam is a far
# better outcome than a died-at-98% install, and the recovery is one command.
if [ "$WANT_STEAM" = 1 ]; then
    step "Installing Steam and the game stack"
    echo "  Installing steam and the 32-bit runtime libraries..."
    if arch-chroot /mnt pacman -S --noconfirm --needed steam 2>&1; then
        success "Steam installed (native multilib package)"
    else
        warn "steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."
    fi

    # ── The layers synui-game-run wraps a game in ─────────────
    #
    # synui ships /usr/bin/synui-game-run, which builds a stack of
    # gamemoderun -> mangohud -> game (and gamescope on request). Every layer is
    # an optdepend of synui and the wrapper drops whichever is missing, so none
    # of this was ever an install error — it just silently did nothing. The
    # result was a documented Super+G / overlay feature that could not work on
    # any SynapseOS install, because these packages reached the LIVE ISO at
    # most (gamescope) or were never packaged anywhere at all (mangohud,
    # gamemode). Installing Steam without them ships the launcher and none of
    # what it launches through.
    #
    # The lib32 halves matter as much as the 64-bit ones: a 32-bit game gets no
    # overlay from mangohud alone, which is the same "installed but does
    # nothing" failure one architecture down.
    #
    # Separate transaction from steam above so a failure here is reported as
    # itself — folded into one -S, a missing gamescope would read as "Steam
    # failed to install".
    echo "  Installing the game stack (overlay, governor, micro-compositor)..."
    if arch-chroot /mnt pacman -S --noconfirm --needed \
            mangohud lib32-mangohud gamemode lib32-gamemode gamescope 2>&1; then
        success "Game stack installed (mangohud, gamemode, gamescope)"
    else
        warn "The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
    fi

    # ── CachyOS Proton ────────────────────────────────────────
    #
    # proton-cachyos-slr is the Proton the CachyOS project ships: Valve's
    # experimental branch plus their patch set, built against the Steam Linux
    # Runtime exactly the way Valve's own Proton is. It installs into
    # /usr/share/steam/compatibilitytools.d/proton-cachyos-slr with a
    # compatibilitytool.vdf, which is the directory Steam scans at start, so it
    # appears in the per-game compatibility dropdown with no further setup —
    # nothing has to be copied into ~/.steam.
    #
    # The -native build is the same Proton OUTSIDE the runtime container, for
    # driving Proton without Steam. Wrong default here (Steam's own games run in
    # the container), and one pacman command away for anyone who wants it.
    #
    # This means adding a THIRD-PARTY REPOSITORY to the installed system. Two
    # rules keep that from turning SynapseOS into CachyOS, and both matter:
    #
    #  1. The generic [cachyos] repo only, appended LAST. CachyOS's own
    #     cachyos-repo.sh inserts [cachyos-v3]/[cachyos-core-v3]/
    #     [cachyos-extra-v3] ABOVE [core] and installs their build of pacman
    #     itself — which re-sources the whole system from CachyOS. pacman takes
    #     the FIRST repo in the file carrying a package name, so appending at the
    #     end means only names that exist nowhere else — proton-cachyos-* — ever
    #     come from here. core/extra keep every package they share.
    #  2. The signing key is PINNED by fingerprint and checked in the keyring
    #     FILE, before anything imports it. Same reasoning as the BlackArch
    #     block below: a substituted keyring is the actual risk, and the
    #     fingerprint is what catches it. Fetching the keyring over TLS is not
    #     on its own a reason to trust what is inside it.
    #
    # Non-fatal throughout, like the rest of this section: Steam without CachyOS
    # Proton is a working Steam, and Valve's own Proton is still there.
    step "Installing CachyOS Proton"

    # The CachyOS master key (uid "CachyOS <admin@cachyos.org>").
    CACHY_FPR="882DCFE48E2051D48E2562ABF3B607488DB35A47"
    CACHY_MIRROR="https://mirror.cachyos.org/repo/x86_64/cachyos"
    cachy_ok=0

    # Filenames carry the version, and mirrors keep only the current one — a
    # pinned URL is a 404 waiting for the next keyring bump. Ask the repo's own
    # database for the current name instead; the pinned pair upstream's script
    # uses is the fallback for when the database itself cannot be read.
    #
    # ⚠ bsdtar, NOT `tar -tzf`. `cachyos.db` is ZSTANDARD-compressed, and GNU
    # tar refuses it from a pipe ("Archive is compressed. Use --zstd option"),
    # so the lookup failed every single time and this function ALWAYS returned
    # the pinned fallback — silently, because a failed lookup is indistinguish-
    # able from a lookup that agrees with the pin. It only looked like it worked
    # because upstream has not rolled either package since March 2024. Measured
    # on a real install 2026-08-14, which came out with exactly
    # cachyos-keyring-20240331-1 and cachyos-mirrorlist-27-1.
    #
    # `--zstd` would work today and break the day upstream switches back;
    # bsdtar detects the compression itself and is already here (libarchive is
    # a pacman dependency, so it is on every ISO and every target).
    cachy_pkg_url() {
        local name=$1 fallback=$2 dir
        dir=$(curl -fsS --proto '=https' --tlsv1.2 --max-time 60 \
                  "$CACHY_MIRROR/cachyos.db" 2>/dev/null \
              | bsdtar -tf - 2>/dev/null | grep -m1 "^${name}-[0-9]") || true
        # The listing carries both "<pkg>-<ver>/" and "<pkg>-<ver>/desc"; take
        # the directory component either way rather than trusting which came
        # first out of the tar.
        dir=${dir%%/*}
        if [ -n "$dir" ]; then
            printf '%s/%s-any.pkg.tar.zst\n' "$CACHY_MIRROR" "$dir"
        else
            printf '%s/%s\n' "$CACHY_MIRROR" "$fallback"
        fi
    }

    if grep -q '^\[cachyos\]' /mnt/etc/pacman.conf; then
        cachy_ok=1          # already enabled (a re-run); nothing to add
    else
        echo "  Fetching the CachyOS keyring and mirrorlist..."
        cachy_keyring=$(cachy_pkg_url cachyos-keyring cachyos-keyring-20240331-1-any.pkg.tar.zst)
        cachy_mirrors=$(cachy_pkg_url cachyos-mirrorlist cachyos-mirrorlist-27-1-any.pkg.tar.zst)

        # ── TRUST THE KEY BEFORE FETCHING WHAT IT SIGNS ──────────────────────
        #
        # THE BOOTSTRAP IS A CHICKEN AND EGG, AND THIS BLOCK USED TO GET IT
        # BACKWARDS. cachyos-keyring is itself SIGNED by the CachyOS master key
        # (the .sig is on the mirror), `pacman -U` on a URL is governed by
        # RemoteFileSigLevel, and that defaults to Required. So on a target
        # whose trustdb has never seen this key, the -U below is refused for
        # unknown trust — every time, on every install. The old order ran the
        # -U FIRST and only trusted the key afterwards, so it could never have
        # succeeded on a fresh machine, and CachyOS Proton has never once been
        # installed by this installer.
        #
        # Proven, not deduced: on the 2026-08-14 T14 install the -U logged
        # `[PACMAN] Running` at 23:43:06 and installed nothing, and the byte-
        # identical command an hour later — after `synpkg cachyos enable-repo`
        # had recv+lsigned the key — installed both packages.
        #
        # --recv-keys takes the FULL fingerprint, so this is still a pin: gpg
        # matches the whole thing, and a keyserver that offers a different key
        # cannot satisfy it. lsign-key then trusts THAT key and nothing else;
        # --populate below still imports the rest of the keyring, which is why
        # the check on the delivered file stays.
        echo "  Trusting the CachyOS master key..."
        arch-chroot /mnt pacman-key --recv-keys "$CACHY_FPR" \
            --keyserver keyserver.ubuntu.com >/dev/null 2>&1 || true
        # Verify rather than believe the exit status: --recv-keys can report
        # success and leave no key behind (same reason synpkg's helper checks).
        if arch-chroot /mnt pacman-key --list-keys "$CACHY_FPR" >/dev/null 2>&1; then
            arch-chroot /mnt pacman-key --lsign-key "$CACHY_FPR" >/dev/null 2>&1 || true
        else
            warn "Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"
        fi

        # -U on a URL, because these two packages are the thing that makes the
        # repo usable — they cannot come FROM the repo.
        #
        # Its output is KEPT, not sent to /dev/null. This is the step most
        # likely to fail (two URLs, one network) and the warning at the bottom
        # of this block can only say "the repository could not be enabled" —
        # which names no cause at all, on the one machine where the cause was
        # visible for a moment.
        cachy_log=$(mktemp)
        if arch-chroot /mnt pacman -U --noconfirm --needed \
                "$cachy_keyring" "$cachy_mirrors" >"$cachy_log" 2>&1; then
            # Read the fingerprints out of the keyring without importing them.
            # --show-keys parses the file and touches no keyring of ours, so a
            # keyring signed by somebody else never reaches pacman's trustdb.
            if gpg --with-colons --show-keys \
                   /mnt/usr/share/pacman/keyrings/cachyos.gpg 2>/dev/null \
               | grep -q "^fpr:::::::::${CACHY_FPR}:"; then
                echo "  Master key pinned as expected — trusting it..."
                arch-chroot /mnt pacman-key --populate cachyos >/dev/null 2>&1 || true

                # Appended, so it lands after [core], [extra], [multilib] and
                # [synapseos]. See rule 1 above — the ORDER is the safety.
                # Saved first so the block below can take it back out if the
                # repo turns out not to sync.
                cp -a /mnt/etc/pacman.conf /mnt/etc/pacman.conf.precachy
                cat >> /mnt/etc/pacman.conf << 'CACHYEOF'

# CachyOS — deliberately LAST, so it can only supply packages no other
# repository has (proton-cachyos-*). Do not move it above [core]/[extra].
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
CACHYEOF

                # Usable, not merely present: a configured repo that lists
                # nothing is exactly the failure this check exists for.
                arch-chroot /mnt pacman -Sy --noconfirm >/dev/null 2>&1 || true
                cachy_count=$(arch-chroot /mnt pacman -Sl cachyos 2>/dev/null | wc -l)
                if [ "${cachy_count:-0}" -gt 0 ]; then
                    cachy_ok=1
                    echo "  [cachyos] enabled ($cachy_count packages available)"
                else
                    # TAKE THE SECTION BACK OUT. A repo pacman cannot sync is
                    # not a dormant extra — pacman aborts the WHOLE -Syu when
                    # any one database fails, so leaving this behind converts a
                    # skipped Proton into a machine that can take no upgrades at
                    # all (the failure BlackArch has caused here twice). Restore
                    # the file we saved before appending; synpkg's own
                    # enable-repo helper rolls back the same way.
                    warn "[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."
                    mv -f /mnt/etc/pacman.conf.precachy /mnt/etc/pacman.conf
                fi
            else
                warn "The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."
                arch-chroot /mnt pacman -Rdd --noconfirm \
                    cachyos-keyring cachyos-mirrorlist >/dev/null 2>&1 || true
            fi
        else
            warn "Could not install the CachyOS keyring and mirrorlist:
$(sed 's/^/    /' "$cachy_log" | tail -5)"
        fi
        rm -f "$cachy_log" /mnt/etc/pacman.conf.precachy
    fi

    if [ "$cachy_ok" = 1 ]; then
        echo "  Installing proton-cachyos-slr (~340 MB download)..."
        if arch-chroot /mnt pacman -S --noconfirm --needed proton-cachyos-slr 2>&1; then
            # The name in the dropdown is the vdf's display_name, which is
            # "proton-cachyos-<ver> (steam linux runtime)" — NOT "Proton-CachyOS",
            # which this used to say and which matches nothing on screen.
            success "CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."
        else
            warn "proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"
        fi
    else
        # ⛔ NOT a pointer at wiki.cachyos.org, which this used to say: that page
        # tells you to run cachyos-repo.sh, and that script inserts the v3/v4
        # repositories ABOVE [core] and installs CachyOS's build of pacman —
        # re-sourcing an Arch system. synpkg's helper does the same job the way
        # this installer does it: generic [cachyos], appended last, key pinned.
        warn "The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
    fi
fi

# ── BlackArch ─────────────────────────────────────────────
#
# Adds the REPOSITORY and its keyring. No tools are installed: [blackarch]
# carries ~5000 packages and pulling them in would dwarf the rest of the system
# several times over. SYNAPSE Arsenal (syn-arsenal / `syn arsenal`) is the
# front-end that turns the repo into something browsable by category.
#
# Upstream's strap.sh does the work because it owns the trust handling: it
# verifies the keyring tarball's SIGNATURE against BlackArch's master key,
# imports and locally signs it, then writes the repo section.
#
# We check the one thing that must not change before running it — that the
# script still pins the master key fingerprint we expect. A pinned sha1 of
# strap.sh would rot on every upstream edit and break installs that are fine;
# a substituted script that swaps in someone else's key is the actual risk, and
# the fingerprint is what catches it.
#
# Wholly non-fatal. This runs late, after a working desktop is already on disk,
# and a missing security repo is not worth failing an install over — especially
# as `sudo syn arsenal --enable-repo` does exactly this again, later, on demand.
if [ "$WANT_BLACKARCH" = 1 ]; then
    header
    step "Enabling BlackArch"

    BA_FPR="4345771566D76038C7FEB43863EC0ADBEA87E4E3"
    ba_ok=0
    echo "  Fetching the BlackArch bootstrap..."
    if curl -fsS --proto '=https' --tlsv1.2 -o /mnt/tmp/strap.sh \
            https://blackarch.org/strap.sh 2>/dev/null; then
        if grep -qF "$BA_FPR" /mnt/tmp/strap.sh; then
            chmod +x /mnt/tmp/strap.sh
            echo "  Master key pinned as expected — running bootstrap..."
            if arch-chroot /mnt /tmp/strap.sh >/dev/null 2>&1 \
               && grep -q '^\[blackarch\]' /mnt/etc/pacman.conf; then
                # Verify it is USABLE, not merely present: a configured repo
                # that lists nothing is the failure this check exists for.
                ba_count=$(arch-chroot /mnt pacman -Sl blackarch 2>/dev/null | wc -l)
                if [ "${ba_count:-0}" -gt 0 ]; then
                    # The keyring as a package, so key rotations arrive as an
                    # upgrade rather than never.
                    # --overwrite, scoped: strap.sh extracts the keyring
                    # tarball straight into /usr/share/pacman/keyrings, so those
                    # files are owned by no package and a plain -S dies with
                    # "exists in filesystem". Taking ownership is the point.
                    arch-chroot /mnt pacman -S --noconfirm --needed \
                        --overwrite '/usr/share/pacman/keyrings/blackarch*' \
                        blackarch-keyring >/dev/null 2>&1 \
                        || warn "blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."
                    ba_ok=1
                    success "BlackArch enabled ($ba_count packages available)"
                fi
            fi
        else
            warn "The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."
        fi
    fi
    rm -f /mnt/tmp/strap.sh
    [ "$ba_ok" = 1 ] || warn "BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."
fi

# ── The software shelf ────────────────────────────────────
#
# Everything ticked on the five SEL_SW_* pages: an ordinary pacman transaction
# from core/extra, nothing SynapseOS-specific about any of it.
#
# LAST, and in its OWN transaction, for two reasons that are really the same
# one. It is the only part of the install where the names are somebody else's
# to rename — a package that moved repositories or was dropped upstream would,
# in one shared transaction, take every SynapseOS package with it (the
# samsung-m2020 shape) — and a failure here costs the user a program they can
# install in one command afterwards, not a machine that will not boot. So it
# runs after the system is complete, warns rather than dies, and retries the
# failures one at a time so a single bad name fails alone and is NAMED.
#
# --needed because several of these are already on the disk by then: wine and
# winetricks, mangohud and goverlay, gamescope. Re-installing them would work
# and would waste the download.
if [ -n "${SEL_SOFTWARE:-}" ]; then
    header
    step "Installing software"
    echo "  $(echo $SEL_SOFTWARE | wc -w) package(s) from the Arch repositories:"
    echo "    $SEL_SOFTWARE"
    echo ""
    if ! arch-chroot /mnt pacman -S --noconfirm --needed $SEL_SOFTWARE 2>&1; then
        warn "That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."
        sw_failed=""
        for p in $SEL_SOFTWARE; do
            arch-chroot /mnt pacman -S --noconfirm --needed "$p" 2>&1 \
                || sw_failed="$sw_failed $p"
        done
        [ -n "$sw_failed" ] && warn "Not installed:$sw_failed
  Nothing else is affected. Install them later with 'synpkg install <name>'."
    fi
    # Docker's daemon is the one item on those pages that is inert until a unit
    # is on, and a Docker that is installed and does nothing reads as broken.
    # Enabled, not started: there is no running system here to start it in.
    case " $SEL_SOFTWARE " in
        *" docker "*) arch-chroot /mnt systemctl enable docker.service 2>/dev/null || true ;;
    esac
    case " $SEL_SOFTWARE " in
        *" syncthing "*) arch-chroot /mnt systemctl --global enable syncthing.service 2>/dev/null || true ;;
    esac
    success "Software installed"
fi

# ── Flatpak apps ──────────────────────────────────────────
#
# A second installer, for the things that are not in any Arch repo. Kept
# entirely separate from the pacman transaction above rather than folded into
# it: these are app IDs, not package names, and handing one to pacman produces a
# baffling error rather than a useful one.
#
# Runs AFTER the pacman software step so a failure here — flathub unreachable,
# a runtime that will not download — cannot cost the packages that were fine.
if [ -n "${SEL_FLATPAKS:-}" ]; then
    step "Installing Flatpak apps"
    echo "  $(echo $SEL_FLATPAKS | wc -w) app(s) from flathub:"
    echo "    $SEL_FLATPAKS"
    echo ""
    # flatpak itself is not in the base system and is not a `sw` row — nothing
    # would have pulled it in, and a flatpak app with no flatpak is nothing.
    if ! arch-chroot /mnt pacman -S --noconfirm --needed flatpak 2>&1; then
        warn "flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."
    else
        # --if-not-exists so re-running the installer over a half-finished
        # target is not an error. System-wide, not --user: there is no user
        # session in a chroot to install into.
        arch-chroot /mnt flatpak remote-add --if-not-exists \
            flathub https://dl.flathub.org/repo/flathub.flatpakrepo 2>&1 \
            || warn "Could not add the flathub remote"

        fp_failed=""
        for app in $SEL_FLATPAKS; do
            echo "  $app"
            # One at a time and never fatal. The first app pulls the freedesktop
            # runtime with it, so a slow link makes this the longest step in the
            # install — and an app that fails here leaves a working system that
            # is merely missing one program.
            arch-chroot /mnt flatpak install -y --noninteractive flathub "$app" 2>&1 \
                || fp_failed="$fp_failed $app"
        done
        [ -n "$fp_failed" ] && warn "Not installed:$fp_failed
  Install later with 'flatpak install flathub <app-id>'."
        success "Flatpak apps installed"
    fi
fi

# ── Configure system ──────────────────────────────────────
header
step "Configuring System"

# fstab
genfstab -U /mnt >> /mnt/etc/fstab
echo "  fstab generated"

# genfstab records the swap only because the ADVANCED path turned it on before
# reaching here. Check rather than assume: a swap the installer set up and fstab
# never mentions is one the machine has for this session and forgets at the next
# boot, and nothing about that looks wrong until it starts OOM-killing.
if [ -n "${PART_SWAP:-}" ]; then
    grep -qE '^[^#]*[[:space:]]swap[[:space:]]' /mnt/etc/fstab \
        || die "swap ($PART_SWAP) is missing from the generated fstab — it would not come back after a reboot"
    success "Swap recorded in fstab"
fi

# ── zram ──────────────────────────────────────────────────
#
# Compressed swap in RAM, on every install. Until now this installer made no
# swap of any kind: a 4 GiB VM running a 4.3 GB model plus a desktop had no
# cushion at all, and the first thing the OOM killer reaches for is synapd.
#
# zram rather than a partition because it is the only answer that fits every
# layout — ERASE and ALONGSIDE both give the root 100% of what is left, so there
# is nowhere to put a swap partition without changing a tested layout. The
# ADVANCED path still offers a real one, for the two things zram cannot do:
# hibernate (the image has to survive power-off) and add capacity that is not
# itself RAM.
#
# min(ram / 2, 8192) matches Fedora, which is where this default has had the
# most testing. priority 100 puts it ahead of a disk swap from fstab (which
# defaults to -2), so zram takes the pressure first and the disk is overflow.
#
# NOTE: llama.cpp mmaps the model, so those pages are file-backed and get
# evicted rather than swapped. This is a cushion for the rest of the desktop,
# not for the model.
cat > /mnt/etc/systemd/zram-generator.conf << 'EOF'
# Managed by syn-install. See zram-generator.conf(5).
[zram0]
zram-size = min(ram / 2, 8192)
compression-algorithm = zstd
swap-priority = 100
EOF

# Run the real generator against the target and check it produces the unit.
#
# This is not ceremony. A config the generator cannot parse makes it emit
# NOTHING and say nothing — verified here by feeding it a bad zram-size and
# watching it write zero units without an error — and the machine then boots
# with no swap, which looks exactly like a machine that was never configured.
# ZRAM_GENERATOR_ROOT is its documented test mode; running it inside the chroot
# means / is the target and /proc is the live kernel's, which is what it needs.
# ONE arch-chroot invocation, and this is not a style preference.
#
# arch-chroot mounts a FRESH tmpfs on the target's /tmp for every call and
# unmounts it again on exit (its chroot_setup mounts `tmp`, and line 1 of the
# script is a `trap chroot_teardown EXIT`). Split across calls — mkdir, run,
# test -f — each step got its own /tmp and the next one saw an empty one, so the
# check threw away the generator's output and then reported that the generator
# had produced nothing. It aborted a working install at "Configuring System".
#
# The generator's own stderr is captured and quoted in the failure, because
# "produced no swap unit" without its reason is exactly the blind message this
# check exists to prevent.
_zg=/usr/lib/systemd/system-generators/zram-generator
if arch-chroot /mnt test -x "$_zg"; then
    if _zout=$(arch-chroot /mnt env ZRAM_GENERATOR_ROOT=/ ZG="$_zg" sh -c '
            d=$(mktemp -d) || exit 2
            out=$("$ZG" "$d" 2>&1)
            if [ -f "$d/dev-zram0.swap" ]; then rm -rf "$d"; exit 0; fi
            printf "%s" "$out"; rm -rf "$d"; exit 1
        ' 2>&1); then
        success "zram configured (compressed swap, half of RAM up to 8 GiB)"
    else
        die "zram-generator produced no swap unit from the config just written — the system would boot with no swap: ${_zout:-no output from the generator}"
    fi
else
    warn "zram-generator is not installed in the target — no compressed swap"
fi

# hostname
echo "synapse" > /mnt/etc/hostname
cat > /mnt/etc/hosts << 'EOF'
127.0.0.1   localhost
::1         localhost
127.0.1.1   synapse.localdomain synapse
EOF
echo "  Hostname: synapse"

# ── Language, keyboard, timezone ──────────────────────────
#
# This used to hardcode en_US.UTF-8 and UTC with no keymap at all, so every
# SynapseOS install anywhere on earth came up in American English, on a US
# keyboard, with the clock wrong — and a CJK or Indic user got a system that
# rendered their own language as boxes, because the only fonts on the ISO are
# dejavu and terminus. "Installs in English" is a decision; "cannot display
# your language" is a bug.
#
# LOCALE_ROWS is defined ABOVE the test seam, with the notes that explain every
# column — the graphical installer renders the same table through
# `--list-locales`, and that query runs before this point in the script is ever
# reached. TZ_ROWS below is up there for the same reason.

header
step "Step 7 — Language & Region"

i=0
echo "$LOCALE_ROWS" | while IFS= read -r row; do
    [ -n "$row" ] || continue
    i=$((i + 1))
    printf '  %2d) %s\n' "$i" "${row%%|*}"
done
echo "   0) Other — enter a locale by hand"
echo ""
# Counted, not typed: a hardcoded range beside a list it is not derived from
# is wrong the first time anyone adds a language.
_n_locales=$(echo "$LOCALE_ROWS" | grep -c '|')
prompt "Language [1-${_n_locales}, default=1]:"
answer language lang_choice -m other=0
lang_choice="${lang_choice:-1}"

LOCALE="en_US.UTF-8"; KEYMAP="us"; XKB_LAYOUT="us"; LANG_FONTS=""
if [ "$lang_choice" = "0" ]; then
    prompt "Locale (e.g. sv_SE.UTF-8):"; answer locale LOCALE
    # Asked separately, and the examples differ on purpose: Swedish is
    # 'sv-latin1' to loadkeys and 'se' to XKB. One question answering both is
    # what broke four of the rows above, and typing a console keymap into
    # xkb_layout mostly produces a layout that does not exist — which synui
    # silently replaces with US.
    prompt "Console keymap (e.g. sv-latin1):"; answer keymap KEYMAP
    KEYMAP="${KEYMAP:-us}"
    prompt "Desktop keyboard layout (e.g. se) [$KEYMAP]:"; answer xkb_layout XKB_LAYOUT
    LOCALE="${LOCALE:-en_US.UTF-8}"; XKB_LAYOUT="${XKB_LAYOUT:-$KEYMAP}"
    # No idea what script that is, so cover as much as possible rather than
    # hand someone a system that cannot draw their own alphabet.
    LANG_FONTS="noto-fonts-extra"
    # ...unless whoever wrote the profile DOES know. The graphical installer
    # offers the LOCALE_ROWS table through --list-locales and then writes the row
    # back as locale/keymap/xkb_layout — which is language=other as far as this
    # branch is concerned, so without this key a Japanese install picked off that
    # list got noto-fonts-extra: the right locale, the right keyboard, and no
    # CJK font on the machine. Read only when present, so nobody at a terminal
    # is asked a question that has no prompt.
    #
    # It is a package list that goes to pacman unquoted, so it is checked here
    # rather than trusted: a name is letters, digits, and the four characters
    # Arch package names actually use.
    if [ -n "${ANSWERS[lang_fonts]+set}" ]; then
        answer lang_fonts _lang_fonts
        # In a variable, so the space in it is unmistakably part of the pattern.
        # The hyphen is last, where it is a literal and not a range.
        _pkglist_re='^[A-Za-z0-9._+@ -]*$'
        if [[ "$_lang_fonts" =~ $_pkglist_re ]]; then
            LANG_FONTS="$_lang_fonts"
        else
            warn "lang_fonts is not a package list: '$_lang_fonts' — installing
  noto-fonts-extra instead."
        fi
    fi
else
    row=$(echo "$LOCALE_ROWS" | sed -n "$((lang_choice + 1))p")
    if [ -n "$row" ]; then
        LOCALE=$(echo "$row" | cut -d'|' -f2)
        KEYMAP=$(echo "$row" | cut -d'|' -f3)
        XKB_LAYOUT=$(echo "$row" | cut -d'|' -f4)
        LANG_FONTS=$(echo "$row" | cut -d'|' -f5)
    fi
fi

# locale.gen needs every locale that will be used. en_US stays generated
# alongside: a great deal of software falls back to it, and a system with only
# one locale generated fails in odd ways when anything asks for C.UTF-8's
# neighbours.
echo "$LOCALE $(echo "$LOCALE" | cut -d'.' -f2)" >> /mnt/etc/locale.gen
[ "$LOCALE" = "en_US.UTF-8" ] || echo "en_US.UTF-8 UTF-8" >> /mnt/etc/locale.gen
arch-chroot /mnt locale-gen 2>&1 | sed 's/^/    /'
echo "LANG=$LOCALE" > /mnt/etc/locale.conf

# Check both names against the data files that will have to resolve them, on
# the image that has them. Neither failure reports itself later: loadkeys can't
# find a map and systemd-vconsole-setup leaves the VT on its built-in US map,
# xkbcommon can't compile a layout and synui swaps in the default — both to a
# log, on a machine whose keyboard is now wrong, which is the hardest possible
# state to go and read a log from.
#
# The files are the authority rather than `localectl list-keymaps` / xkbcli:
# they need no daemon and no extra package, and they agree with both (verified
# against all fifteen rows). Warn rather than die — a wrong keymap is not worth
# throwing away a finished install over, and 'us' is at least typeable.
# Guarded on the directory, not just the file: on an image carrying no kbd at
# all every install would warn about a keymap that is fine.
if [ -d /usr/share/kbd/keymaps ] \
   && ! find /usr/share/kbd/keymaps -name "$KEYMAP.map*" -print -quit 2>/dev/null | grep -q .; then
    warn "Console keymap '$KEYMAP' has no keymap file on this image — the text
  console will stay on US. Fix after boot with: localectl set-keymap <name>"
fi
echo "KEYMAP=$KEYMAP" > /mnt/etc/vconsole.conf

# synui reads its own layout from synuirc — the console keymap does not reach
# Wayland, so without this the desktop stays on a US layout no matter what was
# picked here. Written later with the rest of synuirc; recorded now.
SYNUI_XKB="$XKB_LAYOUT"
if [ -d /usr/share/X11/xkb/symbols ] \
   && [ ! -f "/usr/share/X11/xkb/symbols/$SYNUI_XKB" ]; then
    warn "Keyboard layout '$SYNUI_XKB' is not a layout xkbcommon knows — the
  DESKTOP would silently come up on US. Falling back to 'us'; set
  xkb_layout in ~/.config/synui/synuirc after boot."
    SYNUI_XKB="us"
fi
success "Locale: $LOCALE   Console: $KEYMAP   Desktop layout: $SYNUI_XKB"

# The language pack. Fonts are the whole point: without them the locale is set
# correctly and every glyph is a box, which looks far more broken than English
# would have.
FONT_PKGS="noto-fonts"
[ -n "$LANG_FONTS" ] && FONT_PKGS="$FONT_PKGS $LANG_FONTS"
echo "  Installing fonts ($FONT_PKGS)..."
arch-chroot /mnt pacman -S --noconfirm --needed $FONT_PKGS 2>&1 | tail -2 \
    || warn "Font install failed — $LOCALE may render as boxes"

# ── Timezone ──────────────────────────────────────────────
#
# This was a bare "Timezone (e.g. Europe/Berlin):" prompt, which assumed you
# knew that tzdata wants a Region/City pair. Typing the thing a US user
# actually knows their zone by — "cst" — silently fell through to UTC, and the
# screen offered no way to find out what it wanted instead. A wrong clock on a
# fresh install is a bad first five minutes.
#
# So: a short menu of the common zones, the same shape as the language picker
# above, plus an Other that still takes any tzdata name. The abbreviations are
# accepted because they are what people type; the full name is echoed back so
# the mapping is visible rather than magic. TZ_ROWS itself is defined above the
# test seam — `--list-timezones` prints it, and that query is answered long
# before this line runs.

# The abbreviations people type instead of a tzdata name. Deliberately only the
# unambiguous ones: "IST" is India, Ireland and Israel, so it is not here.
tz_from_abbrev() {
    case "$(echo "$1" | tr '[:lower:]' '[:upper:]')" in
        EST|EDT|ET)     echo "America/New_York" ;;
        CST|CDT|CT)     echo "America/Chicago" ;;
        MST|MDT|MT)     echo "America/Denver" ;;
        PST|PDT|PT)     echo "America/Los_Angeles" ;;
        AKST|AKDT)      echo "America/Anchorage" ;;
        HST)            echo "Pacific/Honolulu" ;;
        GMT|BST)        echo "Europe/London" ;;
        CET|CEST)       echo "Europe/Berlin" ;;
        JST)            echo "Asia/Tokyo" ;;
        AEST|AEDT)      echo "Australia/Sydney" ;;
        UTC|Z)          echo "UTC" ;;
        *)              echo "" ;;
    esac
}

echo ""
step "Step 8 — Timezone"
i=0
echo "$TZ_ROWS" | while IFS= read -r row; do
    [ -n "$row" ] || continue
    i=$((i + 1))
    printf '  %2d) %-22s %s\n' "$i" "${row%%|*}" "${row#*|}"
done
echo "   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"
echo ""
_n_tz=$(echo "$TZ_ROWS" | grep -c '|')

TZ_CHOICE=""
while [ -z "$TZ_CHOICE" ]; do
    prompt "Timezone [1-${_n_tz}, a name, or an abbreviation like CST; blank=UTC]:"
    answer timezone tz_input -m other=0
    tz_input="${tz_input:-UTC}"

    case "$tz_input" in
        0)
            prompt "tzdata name (Region/City):"
            answer timezone_name tz_input
            ;;
    esac

    # A number picks from the list; anything else is a name or an abbreviation.
    case "$tz_input" in
        ''|*[!0-9]*) ;;
        *)  row=$(echo "$TZ_ROWS" | sed -n "$((tz_input + 1))p")
            [ -n "$row" ] && tz_input="${row%%|*}"
            ;;
    esac

    # Abbreviation -> tzdata name, but only if it is not already a real zone.
    if [ ! -f "/mnt/usr/share/zoneinfo/$tz_input" ]; then
        mapped=$(tz_from_abbrev "$tz_input")
        if [ -n "$mapped" ]; then
            echo "    $tz_input -> $mapped"
            tz_input="$mapped"
        fi
    fi

    if [ -n "$tz_input" ] && [ -f "/mnt/usr/share/zoneinfo/$tz_input" ]; then
        TZ_CHOICE="$tz_input"
    else
        # Re-ask rather than silently installing UTC. Offer near misses: a
        # city name on its own ("Chicago") is the other thing people type.
        near=$(cd /mnt/usr/share/zoneinfo 2>/dev/null && \
               find . -type f -ipath "*${tz_input}*" 2>/dev/null \
               | sed 's|^\./||' | grep -vE '^(posix|right)/' | head -5)
        # Exactly one match is not a guess. "Chicago" is the other thing people
        # type after "CST", and making them retype it as America/Chicago when
        # nothing else could have been meant is the same unhelpfulness in a
        # politer voice.
        if [ "$(echo "$near" | grep -c .)" = "1" ]; then
            echo "    $tz_input -> $near"
            TZ_CHOICE="$near"
            continue
        fi
        warn "Not a timezone: '$tz_input'"
        if [ -n "$near" ]; then
            echo "  Did you mean:"
            echo "$near" | sed 's/^/    /'
        else
            echo "  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"
        fi
        echo ""
    fi
done
arch-chroot /mnt ln -sf "/usr/share/zoneinfo/$TZ_CHOICE" /etc/localtime 2>/dev/null || true
arch-chroot /mnt hwclock --systohc 2>/dev/null || true

# Dual-boot clock: Windows keeps the RTC in LOCAL time, Linux in UTC by default,
# so a naive dual-boot shows the wrong time in one OS after switching. Match the
# less-invasive side — put Linux on a local-time RTC too (writes LOCAL to
# /etc/adjtime). Only in alongside mode, where another OS is present.
if [ "$INSTALL_MODE" = "alongside" ]; then
    arch-chroot /mnt hwclock --systohc --localtime 2>/dev/null \
        && echo "  Dual-boot: set the hardware clock to local time (matches Windows)" \
        || echo "  (note: if the clock is wrong after booting Windows, set Windows to UTC"
fi
success "Timezone: $TZ_CHOICE"

# os-release — copy the live system's canonical file so the installed
# system's identity always matches the ISO (no drift). Fall back to the
# package-provided file if for some reason the live one is missing.
if [ -f /etc/os-release ]; then
    cp /etc/os-release /mnt/etc/os-release
    echo "  os-release: copied from live system"
fi

# issue — the same reasoning as os-release. Without this the installed system
# keeps the STOCK Arch `\S{PRETTY_NAME} \r (\l)` issue forever: the branded
# banner only ever existed in the ISO's airootfs, so it had no path onto a disk
# and nobody had seen it on an installed system.
#
# Note that on tty1 it still will not show, and that part is deliberate:
# synui.service declares Conflicts=getty@tty1.service, so no getty runs there.
# The banner is for tty2-tty6 and any serial console.
#
# ⛔ motd IS NOT COPIED WITH IT — see the motd written after the desktop case
# below. The live media's motd talks about the live media, and it does not stay
# on a console where only the person who typed a password reads it.
if [ -f /etc/issue ]; then
    cp /etc/issue /mnt/etc/issue
    echo "  issue: copied from live system"
fi

# Branding — the fastfetch console mark and the raster/vector logos beside it.
# The whole directory rather than logo.txt alone: the login-screen branding
# below reads logo.svg, and a distro that ships its mark on the ISO and not on
# the machine installed from it has the mark in the one place nobody keeps.
if [ -d /usr/share/synapseos ]; then
    mkdir -p /mnt/usr/share/synapseos
    cp -a /usr/share/synapseos/. /mnt/usr/share/synapseos/ 2>/dev/null || true
fi

# ── Copy service files from live ISO ─────────────────────
# Only units no package owns. synapd/synnet/synguard/synui/synui-foot are all
# shipped by their packages at /usr/lib/systemd/system, and `systemctl enable`
# below resolves them from there — so copying the ISO's /etc copies on top was
# not just redundant, it was harmful: /etc/systemd/system *overrides*
# /usr/lib/systemd/system, so every install permanently shadowed the packaged
# units with whatever stale copy the ISO happened to carry. That is how synapd
# and synnet ran with none of their hardening (NoNewPrivileges, ProtectSystem,
# CapabilityBoundingSet) for as long as they did: the hardening was added to the
# packaged units, and the /etc copies silently won.
#
# Anything genuinely unpackaged still has to be copied.
for f in \
    /etc/systemd/system/synapse-kmod-build.service \
    /etc/synguard/rules.d/; do
    [ -e "$f" ] && cp -r "$f" "/mnt$f" 2>/dev/null || true
done
# chmod after copy: if the live media shipped the script non-executable
# (broken file_permissions in an old ISO), the copy inherits that and the
# boot service dies with 203/EXEC.
if [ -f /usr/bin/synapse-kmod-build ]; then
    cp /usr/bin/synapse-kmod-build /mnt/usr/bin/synapse-kmod-build 2>/dev/null \
        && chmod 755 /mnt/usr/bin/synapse-kmod-build
fi

# The synguard Requires=synapd → Wants= sed that used to live here is gone: it
# patched the /etc copy we no longer make, and synguard's packaged unit (like
# synnet's) already says Wants=.

# ── Create user and groups ────────────────────────────────
# Everything in this section aborts the install on failure. A system that
# reaches "installation complete" with no working login is the worst
# outcome — the target fs can flip read-only mid-install (disk errors,
# errors=remount-ro) and a masked useradd/chpasswd here delivered exactly
# that: a locked-out "successful" install.
echo "  Creating user '$NEW_USER'..."

# The target fs may have remounted read-only since pacstrap; catch it now
# with a clear message instead of a cascade of masked failures.
arch-chroot /mnt sh -c 'touch /etc/.rw-check && rm -f /etc/.rw-check' \
    || die "Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"

# Fed on stdin as a QUOTED heredoc, not `bash -c "..."`. This block is mostly
# prose, and inside a double-quoted argument the outer shell still honours " $
# and ` — so an ordinary phrase in double quotes inside a COMMENT silently ends
# the string. That is not hypothetical: it shipped in pkgrel 20. A "bad
# permissions" in the comment below cut the block in half; the chmod and all
# three NOPASSWD drop-ins after it were handed to `bash -c` as unused
# positional parameters and never ran. Every install from that build got
# /etc/sudoers.d/wheel at 0644 and nothing else — game mode could not suspend
# synapd, the start menu could not reboot, the AI-backend row did nothing, and
# arch-chroot exited 0 throughout. <<'SYN_CHROOT' quotes the delimiter, so the
# outer shell neither expands nor parses anything in here and prose is safe.
arch-chroot /mnt bash <<'SYN_CHROOT'
    groupadd -r synapse  2>/dev/null || true
    groupadd -r synguard 2>/dev/null || true
    groupadd -r seat     2>/dev/null || true

    grep -qxF '/usr/bin/synsh' /etc/shells 2>/dev/null || echo '/usr/bin/synsh' >> /etc/shells

    echo '%wheel ALL=(ALL:ALL) ALL' > /etc/sudoers.d/wheel
    # 440 like every other drop-in: without it the file lands 0644 and
    # 'visudo -c' fails the whole ruleset with 'bad permissions', which is
    # the one check anyone would run to confirm this directory is sane.
    chmod 440 /etc/sudoers.d/wheel

    # ── Why every NOPASSWD drop-in below is named zz-* ───────────────────
    # sudo parses /etc/sudoers.d in SORTED LEXICAL ORDER, and when several
    # entries match a command the LAST match wins — not the most specific.
    # The blanket '%wheel ALL=(ALL:ALL) ALL' above matches every command and
    # carries no NOPASSWD, so any exception in a file sorting BEFORE 'wheel'
    # is silently overridden: sudo demands a password, 'sudo -n' fails, and
    # every caller here is fire-and-forget, so the refusal is invisible.
    #
    # That is exactly what shipped: power-menu, synapd-backend and
    # synapd-gamemode all sort before 'wheel' (p, s < w), so on a fresh
    # install game mode never suspended synapd, the start menu could not
    # reboot, and the AI backend row did nothing — while all three rules were
    # present and looked correct in 'sudo -l'. It went unnoticed because the
    # dev box carries a personal 'zz-*' NOPASSWD:ALL file that sorts last and
    # masked the whole problem.
    #
    # The zz- prefix is load-bearing. Do not rename these to something
    # tidier that sorts before 'wheel'.
    #
    # Clear the pre-zz names so an install over an older target can't leave
    # both copies behind.
    rm -f /etc/sudoers.d/power-menu /etc/sudoers.d/synapd-gamemode /etc/sudoers.d/synapd-backend

    # The SYNAPSE start menu's Reboot/Shut Down entries run with no
    # terminal to type a password into, and the target has no polkit —
    # allow exactly these two commands passwordless.
    echo '%wheel ALL=(ALL:ALL) NOPASSWD: /usr/bin/systemctl reboot, /usr/bin/systemctl poweroff' \
        > /etc/sudoers.d/zz-power-menu
    chmod 440 /etc/sudoers.d/zz-power-menu

    # Game mode stops synapd to hand the GPU to the game (synui/src/game.c).
    # synapd is a system unit, so the session user's plain 'systemctl stop'
    # is refused by polkit — and synui spawns it fire-and-forget, so the
    # refusal was silent: game mode said it had suspended synapd while
    # synapd carried on holding ~4GB of VRAM. sudo -n, scoped to these two.
    #
    # The SOCKET is named alongside the service because synapd is socket
    # activated (Requires=synapd.socket): stopping the service on its own
    # leaves the socket listening, and the next client to connect has systemd
    # start synapd again within the same second. That made game mode look
    # random — it held whenever nothing connected, and silently did nothing
    # whenever something did. These strings must stay character-for-character
    # in step with game_ai_{stop,start}_cmd in synui/src/config.c: sudoers
    # matches the whole command line, so a drift here is another silent no-op.
    echo '%wheel ALL=(ALL:ALL) NOPASSWD: /usr/bin/systemctl stop synapd.socket synapd.service, /usr/bin/systemctl start synapd.socket synapd.service' \
        > /etc/sudoers.d/zz-synapd-gamemode
    chmod 440 /etc/sudoers.d/zz-synapd-gamemode

    # Game mode can also quiet synapse_kmod's event capture while a game runs
    # (game_quiet_kmod, off by default). /sys/kernel/synapse/config is
    # root-owned 0644, so the session user cannot write it.
    #
    # Scoped to a helper with two fixed arguments rather than to `tee` or a
    # shell: a rule for tee on that path would allow writing ANY key to it, and
    # a shell rule allows everything. synui-kmod-events writes one known key and
    # reads it back.
    #
    # Same character-for-character warning as the block above — these strings
    # must match game_kmod_{quiet,restore}_cmd in synui/src/config.c exactly, or
    # sudo refuses and synui's fire-and-forget spawn never notices.
    echo '%wheel ALL=(ALL:ALL) NOPASSWD: /usr/lib/synui/synui-kmod-events on, /usr/lib/synui/synui-kmod-events off' \
        > /etc/sudoers.d/zz-synui-kmod-events
    chmod 440 /etc/sudoers.d/zz-synui-kmod-events

    # The 'AI backend' row (welcome menu / control panel) toggles synapd
    # between GPU/CPU/off via synui-ai-backend, which rewrites synapd's
    # systemd drop-in and restarts it. Under a greetd session synui runs as
    # the user, who cannot do that and has no polkit agent to prompt — so the
    # helper re-execs under sudo -n. Scope it to exactly this command.
    echo '%wheel ALL=(ALL:ALL) NOPASSWD: /usr/bin/synui-ai-backend gpu, /usr/bin/synui-ai-backend cpu, /usr/bin/synui-ai-backend off, /usr/bin/synui-ai-backend toggle' \
        > /etc/sudoers.d/zz-synapd-backend
    chmod 440 /etc/sudoers.d/zz-synapd-backend
SYN_CHROOT

# HARD VERIFY the drop-ins landed. The block above is masked (its failures do
# not stop the install), and it has now failed silently twice in two different
# ways: first the pre-zz names were parsed before the blanket wheel rule and
# lost to it, then the whole tail of the block was cut off by a stray quote.
# Both shipped a system where every sudo -n caller — all of them
# fire-and-forget — failed with nothing written anywhere. A missing file here
# is not cosmetic, so check for it rather than trusting that the block ran.
for f in zz-power-menu zz-synapd-gamemode zz-synapd-backend wheel; do
    [ -s "/mnt/etc/sudoers.d/$f" ] \
        || die "sudoers drop-in /etc/sudoers.d/$f was not written — privileged desktop actions (game mode, reboot, AI backend) would silently do nothing"
done
# Order matters as much as presence: sudo takes the LAST match, so every
# NOPASSWD file must sort after 'wheel'. Catch a rename that undoes that.
for f in zz-power-menu zz-synapd-gamemode zz-synapd-backend; do
    [ "$(printf '%s\nwheel\n' "$f" | sort | tail -n1)" = "$f" ] \
        || die "sudoers drop-in '$f' sorts before 'wheel' — the blanket wheel rule would override it"
done
# visudo -c parses the whole directory the way sudo will, and fails on a bad
# mode as well as bad syntax, so it covers the 0644 case too.
arch-chroot /mnt visudo -c >/dev/null \
    || die "sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"

# useradd runs outside the masked bash -c block: if it fails, nothing
# after it can produce a bootable login, so fail loudly and stop.
arch-chroot /mnt useradd -m -G wheel,audio,video,input,synapse,seat \
    -s /bin/bash -c "$NEW_FULLNAME" "$NEW_USER" \
    || die "useradd '$NEW_USER' failed — aborting (no login would be possible)"

# Set password directly — pipe to chpasswd outside bash -c to avoid
# quoting/escaping issues and ensure errors are visible
printf '%s:%s\n' "$NEW_USER" "$NEW_PASS" | arch-chroot /mnt chpasswd \
    || die "chpasswd failed for '$NEW_USER' — aborting (login would be impossible)"

# Verify the hash actually landed in /etc/shadow: chpasswd goes through
# PAM, and a module failure can leave the account locked ('!' from
# useradd) even when the command's own error output scrolled past.
SHADOW_HASH=$(grep "^${NEW_USER}:" /mnt/etc/shadow | cut -d: -f2)
case "$SHADOW_HASH" in
    \$*) ;;  # real hash — good
    *)  die "Password verification failed: shadow entry for '$NEW_USER' is '${SHADOW_HASH:-empty}' — login would be impossible" ;;
esac

# Root stays locked (pacstrap default, '*' in shadow) — admin goes
# through sudo. Say so, or the first `su` looks like a broken password.
success "Password set for '$NEW_USER' (verified in shadow)"

USER_UID=$(arch-chroot /mnt id -u "$NEW_USER" 2>/dev/null || echo 1000)
echo "  User '$NEW_USER' created (uid=$USER_UID)"

# syn-update's source tree, created HERE and owned by the user who will run it.
#
# syn-update clones into /var/lib/synapse-src on first use, and creating that
# directory needs root — so it shells out to `sudo install -d`. That works in a
# terminal and fails in the updater GUI, which Quickshell runs with no
# controlling terminal: sudo answers "a terminal is required to read the
# password", the window shows "cannot create /var/lib/synapse-src", and the only
# thing the user did was open Updates on a fresh install. Making the directory
# now means the read-only half of the updater stays read-only.
arch-chroot /mnt install -d -o "$NEW_USER" -g "$NEW_USER" /var/lib/synapse-src \
    2>/dev/null || warn "could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"

# ── Enable services ──────────────────────────────────────
arch-chroot /mnt systemctl enable NetworkManager seatd 2>/dev/null || true
# Bluetooth: bluez ships the unit but enables nothing. Without this the radio
# stays down and synui's panel (Super+B) correctly reports no adapter.
# Skipped entirely when step 4 declined it — the unit is not even installed.
[ "$WANT_BLUETOOTH" = 1 ] && \
    arch-chroot /mnt systemctl enable bluetooth 2>/dev/null || true
# Printing — the whole block is skipped when step 4 declined it, including the
# cups.socket drop-in and the nsswitch edit. Writing a drop-in for a unit that
# is not installed leaves a config file referring to nothing, and the mDNS entry
# would point glibc at a resolver that isn't there.
if [ "$WANT_PRINTING" = 1 ]; then
    # cups.socket, not cups.service: socket activation means no daemon runs
    # until something actually prints. avahi is what finds driverless
    # (IPP Everywhere / AirPrint) printers, i.e. most made since ~2015.
    arch-chroot /mnt systemctl enable cups.socket avahi-daemon 2>/dev/null || true
    # cups.socket binds only the unix socket, so the web admin UI at
    # localhost:631 — the whole printer story, opened from the start menu and
    # control panel — can never connect. Append a loopback TCP listener
    # (ListenStream is a list, so the unix socket stays); socket activation is
    # preserved, cupsd still idles until the port or a print job is touched.
    mkdir -p /mnt/etc/systemd/system/cups.socket.d
    cat > /mnt/etc/systemd/system/cups.socket.d/tcp.conf << 'CUPSTCP_EOF'
[Socket]
ListenStream=127.0.0.1:631
ListenStream=[::1]:631
CUPSTCP_EOF
    # The glibc half of mDNS. Without it cups discovers the printer and then
    # cannot resolve its .local name, so discovery works and printing fails.
    if [ -f /mnt/etc/nsswitch.conf ] && ! grep -q mdns_minimal /mnt/etc/nsswitch.conf; then
        sed -i '/^hosts:/s/ myhostname/ myhostname mdns_minimal [NOTFOUND=return]/' \
            /mnt/etc/nsswitch.conf
    fi
fi
# Only enable what was actually selected — `systemctl enable` on a unit whose
# package was declined fails silently, which would look identical to a broken
# install in the journal afterwards.
syn_selected() { case " $SEL_CORE " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
for svc in synapd synnet synguard; do
    syn_selected "$svc" && \
        arch-chroot /mnt systemctl enable "$svc" 2>/dev/null || true
done
syn_selected synapse_kmod && \
    arch-chroot /mnt systemctl enable synapse-kmod-build 2>/dev/null || true
# Module-signature policy: safe to enable — only enforces when Secure Boot is
# on and all modules are kernel-trusted, otherwise it's a no-op (assume SB on,
# fall back if not).
arch-chroot /mnt systemctl enable synapse-secureboot.service 2>/dev/null || true
arch-chroot /mnt systemctl enable vboxservice 2>/dev/null || true

# Audio (PipeWire). --global, not --enable: these are *user* units, and they
# have to be on for every user, before any user has logged in.
#
# Installing the pipewire packages does NOT enable them — nothing in Arch ships
# a preset or a sockets.target.wants symlink for them. Without this, chibi's
# audio is worse than absent: pipewire-alsa repoints ALSA's `default` PCM at a
# PipeWire daemon that never starts, so aplay/arecord fail and Chibi comes up
# mute and deaf with no error at all.
arch-chroot /mnt systemctl --global enable \
    pipewire.socket pipewire-pulse.socket wireplumber.service 2>/dev/null || true

# ── Desktop environment setup ────────────────────────────
mkdir -p /mnt/etc/synapseos
case "$DE_CHOICE" in
    2)
        echo "DE=kde" > /mnt/etc/synapseos/desktop.conf
        mkdir -p /mnt/etc/sddm.conf.d
        cat > /mnt/etc/sddm.conf.d/synapseos.conf << 'SDDMEOF'
[General]
DisplayServer=wayland
GreeterEnvironment=QT_WAYLAND_SHELL_INTEGRATION=layer-shell

[Theme]
Current=breeze
SDDMEOF
        arch-chroot /mnt systemctl enable sddm.service 2>/dev/null || true
        echo "  Desktop: KDE Plasma (SDDM login screen)"
        ;;
    3)
        echo "DE=gnome" > /mnt/etc/synapseos/desktop.conf
        arch-chroot /mnt systemctl enable gdm.service 2>/dev/null || true

        # Login-screen branding. Arch's gdm points org.gnome.login-screen's
        # `logo` at the Arch logo, so an installed SynapseOS greets you with
        # somebody else's mark on the first screen it ever shows you. A
        # system-db drop-in is the supported way to change it and does not
        # depend on what the default happens to be this release.
        #
        # banner-message-enable is pinned off alongside it. It is already the
        # default and it is NOT what put the live media's text on that screen
        # (that was pam_motd, fixed where the motd is written) — it is here so
        # that a future GNOME which does turn it on cannot put an unreviewed
        # wall of text back on the login screen.
        if [ -f /usr/share/synapseos/logo.svg ]; then
            install -Dm644 /usr/share/synapseos/logo.svg \
                /mnt/usr/share/pixmaps/synapseos-logo.svg
            mkdir -p /mnt/etc/dconf/db/gdm.d /mnt/etc/dconf/profile
            printf 'user-db:user\nsystem-db:gdm\n' > /mnt/etc/dconf/profile/gdm
            cat > /mnt/etc/dconf/db/gdm.d/01-synapseos << 'GDMDCONF'
[org/gnome/login-screen]
logo='/usr/share/pixmaps/synapseos-logo.svg'
banner-message-enable=false
GDMDCONF
            arch-chroot /mnt dconf update 2>/dev/null || true
            echo "  GDM: SynapseOS logo on the login screen"
        fi
        echo "  Desktop: GNOME (GDM login screen)"
        ;;
    4)
        echo "DE=tty" > /mnt/etc/synapseos/desktop.conf
        echo "  Desktop: TTY only"
        ;;
    *)
        echo "DE=synui" > /mnt/etc/synapseos/desktop.conf

        # Session wrapper — greetd runs this as the user after login.
        # Same VM detection as the .bash_profile fallback below.
        mkdir -p /mnt/usr/local/bin
        cat > /mnt/usr/local/bin/synui-session << 'SESSION_EOF'
#!/bin/sh
# synui picks its own renderer: it tries hardware GLES2 and falls back to
# software if that cannot come up. Forcing software here was wrong — a VM with
# 3D enabled has a working GPU, and llvmpipe is where the layer surfaces
# corrupt. Only the cursor and backend policy belongs at this level.
if [ -f /sys/class/dmi/id/sys_vendor ] && \
   grep -qiE 'VirtualBox|VMware|QEMU|KVM|Xen|innotek' /sys/class/dmi/id/sys_vendor 2>/dev/null; then
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
fi
# ~/.local/bin on PATH for synui and everything it spawns. greetd runs this
# through a login shell, so /etc/profile.d is already applied — but the ISO's
# profile.d/synapseos.sh is not installed onto disk, so this is where an
# installed system gets it. ~/.bashrc cannot serve: only interactive bash reads
# it, and the programs that need this are not bash (synsh walks PATH itself and
# says "no music player is installed" when it misses cliamp; synui's AI "CMD:"
# children exec via /bin/sh). Guarded against duplicates.
case ":${PATH}:" in
    *":$HOME/.local/bin:"*) ;;
    *) export PATH="$HOME/.local/bin:$PATH" ;;
esac

export XDG_SESSION_TYPE=wayland
# Portal backend routing (synui-portals.conf); unset ⇒ no screen sharing.
export XDG_CURRENT_DESKTOP=synui
# Qt matches XDG_CURRENT_DESKTOP to choose a platform theme, and synui matches
# nothing it knows, so it loaded none and Qt apps ran on the built-in light
# palette — which drew Dolphin file names in black on every dark theme.
export QT_QPA_PLATFORMTHEME=xdgdesktopportal
export LIBSEAT_BACKEND=seatd
# Pin cursor theme+size so Xwayland clients (Steam, games) match synui's own
# 24px cursor; without XCURSOR_SIZE libXcursor auto-picks a much larger size.
export XCURSOR_THEME=Adwaita
export XCURSOR_SIZE=24
# ...unless a theme was chosen with Super+Shift+P or synui-cursor(1), which
# writes exactly these two exports to cursor.env. Sourced after the defaults so
# the user's choice wins; absent on a fresh install, hence the -r test.
[ -r "$HOME/.config/synui/cursor.env" ] && . "$HOME/.config/synui/cursor.env"
# MangoHud's Vulkan implicit layer keys off MANGOHUD=1. A launcher wrapper only
# ever covers the path it wraps; the env var reaches Steam, Lutris, RetroArch and
# bare binaries alike. Hud starts hidden (no_display) — Shift_R+F12 toggles it
# live in-game, the only place it can work: an overlay cannot be injected into a
# process already up. Vulkan only; OpenGL needs synui-game-run's preload.
export MANGOHUD=1
# The three gaming shortcuts — super+F10 (big screen mode), F11 and F12 (the
# overlay) — live in ~/.config/synui/synuirc, and a package cannot write into a
# user's home. Nothing ever did: `binds refresh` deliberately does nothing when
# there is no block, so on a fresh install those keys existed in the defaults,
# in `binds show` and in the documentation, and did nothing when pressed.
#
# ⚠ HERE, synchronously, and not only in /etc/profile.d/syn-arcade.sh — that
# one backgrounds its work and would race synui reading its config, which on a
# first login means the keys are written but not live until the NEXT one.
# `ensure` writes nothing when the block is already right, and leaves a block
# somebody deliberately removed alone.
command -v syn-arcade >/dev/null 2>&1 && syn-arcade binds ensure --quiet >/dev/null 2>&1
exec synui
SESSION_EOF
        chmod 755 /mnt/usr/local/bin/synui-session

        # Greeter wrapper — greetd runs this as the `greeter` user to draw the
        # LOGIN screen, which is synui itself in --greeter mode: the same panel
        # as the lock screen. Same renderer detection as the session wrapper;
        # seatd so the compositor can take DRM/input on the seat. greetd exports
        # GREETD_SOCK into this process, which synui --greeter speaks to start
        # the session once the password checks out.
        cat > /mnt/usr/local/bin/synui-greeter << 'GREETER_EOF'
#!/bin/sh
if [ -f /sys/class/dmi/id/sys_vendor ] && \
   grep -qiE 'VirtualBox|VMware|QEMU|KVM|Xen|innotek' /sys/class/dmi/id/sys_vendor 2>/dev/null; then
    # synui picks its own renderer (hardware first, software on failure);
    # only cursor/backend policy belongs here. See synui-gfx-env.
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
fi
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=synui
# Qt loads no platform theme for an unknown desktop name; see the login script.
export QT_QPA_PLATFORMTHEME=xdgdesktopportal
export LIBSEAT_BACKEND=seatd
export XCURSOR_THEME=Adwaita
export XCURSOR_SIZE=24
exec synui --greeter
GREETER_EOF
        chmod 755 /mnt/usr/local/bin/synui-greeter

        # tuigreet is a TTY app; the synui greeter is a full wlroots compositor,
        # so the `greeter` user needs the seat's devices: video (GPU), input
        # (libinput), and seat (so seatd hands them over). Without these the
        # greeter can't take DRM and greetd loops on a black screen.
        arch-chroot /mnt usermod -aG video,input,seat greeter 2>/dev/null || true

        # greetd: the synui greeter on tty1 — the login screen IS the lock
        # screen. synui --greeter (via the wrapper) collects the password and
        # asks greetd to start /usr/local/bin/synui-session on success.
        # synui.service / synui-foot.service stay disabled on installs — the
        # session goes through PAM so logind owns XDG_RUNTIME_DIR (they remain
        # the no-login path for the live ISO only).
        mkdir -p /mnt/etc/greetd
        cat > /mnt/etc/greetd/config.toml << 'GREETD_EOF'
[terminal]
vt = 1

[default_session]
command = "/usr/local/bin/synui-greeter"
user = "greeter"
GREETD_EOF
        arch-chroot /mnt systemctl enable greetd.service 2>/dev/null || true
        echo "  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"
        ;;
esac

# ── User home setup ──────────────────────────────────────
# .bash_profile — launches synui on tty1 login as fallback
cat > "/mnt/home/$NEW_USER/.bash_profile" << 'PROFILEEOF'
if [ "$(tty)" = "/dev/tty1" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    if ! systemctl is-active --quiet synui.service 2>/dev/null; then
        export XDG_RUNTIME_DIR="/run/user/$(id -u)"
        mkdir -p "$XDG_RUNTIME_DIR"
        if [ -f /sys/class/dmi/id/sys_vendor ] && \
           grep -qiE 'VirtualBox|VMware|QEMU|KVM|Xen|innotek' /sys/class/dmi/id/sys_vendor 2>/dev/null; then
            # synui picks its own renderer (hardware first, software on
            # failure); only cursor/backend policy belongs here.
            export WLR_BACKENDS=drm,libinput
            export WLR_NO_HARDWARE_CURSORS=1
        fi
        export XDG_SESSION_TYPE=wayland
        export XDG_CURRENT_DESKTOP=synui
        # Qt loads no platform theme for an unknown desktop name; see above.
        export QT_QPA_PLATFORMTHEME=xdgdesktopportal
        export LIBSEAT_BACKEND=seatd
        export XCURSOR_THEME=Adwaita
        export XCURSOR_SIZE=24
        # ...unless synui-cursor(1) / Super+Shift+P wrote a choice. See the
        # synui-session heredoc above; this path must stay in step with it.
        [ -r "$HOME/.config/synui/cursor.env" ] && . "$HOME/.config/synui/cursor.env"
        # Vulkan overlay layer; see the synui-session heredoc above.
        export MANGOHUD=1
        # The gaming shortcuts; see the synui-session heredoc above.
        command -v syn-arcade >/dev/null 2>&1 && \
            syn-arcade binds ensure --quiet >/dev/null 2>&1
        exec synui
    fi
fi
PROFILEEOF

# ── motd ─────────────────────────────────────────────────
# WRITTEN, never copied from the live system, and the reason is a screenshot of
# a GDM login screen on an installed machine reading "No AI model ships on this
# media" and "To install to disk: syn-install" under an Arch Linux logo.
#
# ⚠ A GRAPHICAL LOGIN SHOWS THE motd. pam_motd emits it as a PAM_TEXT_INFO
# message, and GDM (and SDDM) render PAM's messages in the login dialog — so
# the motd is not the "shown once you are already in" file it is on a console.
# Every line of it is read by someone who has not logged in yet, on the first
# screen they ever see of the machine they just installed.
#
# So it says what is true of THIS machine: the desktop that was actually
# chosen, and nothing about install media. The live ISO's own motd stays as it
# is — it is correct there, and that is the only place it was ever correct.
case "$(cat /mnt/etc/synapseos/desktop.conf 2>/dev/null)" in
    DE=kde)   _motd_de="KDE Plasma" ;;
    DE=gnome) _motd_de="GNOME" ;;
    DE=tty)   _motd_de="none (console only)" ;;
    *)        _motd_de="synui" ;;
esac
{
    printf '  SynapseOS %s — Where the kernel thinks.\n\n' "$RELEASE"
    printf '  Desktop:   %s\n' "$_motd_de"
    printf '  Services:  synapd  synnet  synguard  synapse_kmod\n'
    printf '  Shell:     synsh    (type naturally or use shell commands)\n\n'
    printf '  Settings:  syn-settings        Software:  synpkg\n'
    printf '  Updates:   syn-update          Help:      syn --help\n'
} > /mnt/etc/motd
unset _motd_de
echo "  motd: written for this installation"

# synui config
mkdir -p "/mnt/home/$NEW_USER/.config/synui"
cat > "/mnt/home/$NEW_USER/.config/synui/synuirc" << 'SYNUIRC'
terminal = syntty
# SYNAPSE Prism — the house theme, and what a fresh install boots into.
#
# ⚠ WRITTEN HERE RATHER THAN COMPILED IN, and that is the whole point. synui's
# built-in default is still `synapse`, so an EXISTING machine that never picked
# a theme keeps the desktop it has: changing the compiled default would restyle
# every one of them on the next upgrade, silently, which is not something an
# update is allowed to do. A fresh install has no desktop to preserve.
#
# Prism is glass, and it takes its accent off the wallpaper — live, on every
# wallpaper change. A greyscale wallpaper has no colour to give and the theme's
# own cyan stands; the journal says which is happening.
theme = prism
# ── Glass: AUTO, and every word of that is deliberate ────────────────────────
#
# One slider for how much of the desktop you see through: 0 solid, 100 as clear
# as it goes, `auto` to let the theme answer. It drives the windows, synui's own
# panels and the bar together, each with the number that surface needs.
#
# ⚠ THIS USED TO SAY `glass_level = 55`, AND THAT IS WHY A FRESH INSTALL DID NOT
# FOLLOW ITS THEME. A number here is an explicit answer, and an explicit answer
# survives a theme switch: every one of the twelve non-glass themes was being
# handed Prism's level instead of the opacities it was tuned with, for ever,
# because the installer had written one down on the user's behalf.
#
# It was also redundant. synui already gives a GLASS theme
# SYN_GLASS_PANEL_DEFAULT — the same 55 — when nobody has set a level, precisely
# so a desktop that reached Prism through the theme manager is not the same
# theme with solid panels. The two were "the same decision written twice", and
# the copy in this file was the one that could be wrong.
#
# What changes visually on a fresh Prism install: the panels and the bar are
# identical (both paths resolve to 55). Windows go from 0.79 to Prism's own
# 0.90 — slightly less see-through, and 0.90 is the number the theme was
# designed with.
glass_level = auto

# The dock's body: `auto` is glass on a glass theme and solid elsewhere. It is
# already synui's compiled default and is written here to be visible rather than
# to change anything — a fresh install that wants to know what it was given
# should be able to read it in this file.
dock_style = auto

# ⚠ WIDGET GLASS IS `on` AND NOT `auto`, AND THIS IS THE ONE THAT NEEDS SAYING.
#
# `auto` means "follow the theme", and the desktop widgets cannot ask what the
# theme is. They are quickshell, and they read theme.state — a file
# synui-apply-theme writes when somebody PICKS a theme, and which deliberately
# refuses to create itself otherwise (creating it would hand theme.state
# precedence over settings.state's opacity keys on a desktop that never asked
# for that).
#
# A fresh install names `theme = prism` in this file and has never picked a
# theme, so theme.state does not exist. The compositor resolves its own chrome
# and the dock in-process and looks right; the widgets have nothing to read and
# come up solid on a glass desktop. `on` is the explicit answer for exactly that
# case, and it is what synui's own theme.c names as the answer.
#
# Set it back to `auto` after picking any theme from the manager and it will
# follow along like everything else.
widget_glass = on
# greetd launches synui after login.
# The bar is just the bar. The start menu it used to carry is synui's own
# panel (Super tap), which scans the installed .desktop files itself when it
# opens — so there is no generator to run before the bar starts. synui-bar
# starts quickshell against the QML tree synui packages to
# /usr/share/synui/quickshell.
autostart = synui-bar
# A synsh terminal used to be autostarted here, and the live ISO used to do the
# same through synui-foot.service. The live session dropped it — it is a desktop
# now, not a shell with a compositor around it — and an installed system has even
# less reason to open one: Super+Return is the terminal, and the start menu has
# it too. Uncomment to get a shell on every login.
# autostart = kitty synsh
# Any GUI app that needs root goes through polkit, and pkexec refuses to
# prompt on a terminal it doesn't have — without an authentication agent
# registered for the session it fails instantly and, launched from a menu,
# silently (gparted, for one). This is the agent that shows the dialog.
autostart = /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1
border_width    = 2
gap             = 8
master_factor   = 0.60

# The floating desktop's own tiler: windows you have not placed yourself are
# arranged into a grid that deliberately does NOT fill the screen. float_inset
# is the percentage of the screen kept clear at each edge and float_gap the
# space between the tiles, so the wallpaper shows through — that is the point
# of the layout, not a gap in it. Separate from `gap` above, which wants to be
# a hairline. Super+Shift+G re-arranges after you have dragged things about.
float_inset     = 8
float_gap       = 24

# The SYNAPSE wallpaper synui ships in /usr/share/synui. Drawn by the
# compositor itself (wallpaper.c), so nothing has to be autostarted to
# paint the desktop — a swaybg here would only cover it. Super+W picks a
# different one and records the choice in wallpaper.state, which from then
# on overrides this line. `wallpaper = none` gives a flat colour.
wallpaper       = default
wallpaper_mode  = fill

# "night drive" palette — matches kitty.conf/foot.ini and waybar style.css
border_color_norm  = #2a2a40
border_color_focus = #ff296d
border_color_ai    = #05d9e8
border_color_warn  = #ff3524

# CRT post-process (GPU only; VMs fall back to the plain look).
# OFF: a new desktop should look like a desktop, and scanlines over
# everything is a taste, not a default. Set `effects = on` here — or turn it
# on live in the Super+E filters panel, which saves to filters.state — and
# the strengths below take effect. Glitch fires on window close and,
# sustained, while synguard holds any window in ALERT/DENY.
effects           = off
# effect_scanline   = 0.35
# effect_curvature  = 0.25
# effect_aberration = 0.40
# effect_glitch     = 0.60

# ── Lock screen (Super+L) ──────────────────────────────────
# Offer the fingerprint reader beside the password. Both ways in are live at
# once, so you can type your password over a reader that is still waiting, and
# whichever answers first unlocks.
#
# On costs a machine WITHOUT a reader nothing: the helper is told there is no
# device, the lock stops asking and draws nothing. So this is not a "do I have
# one" switch — that is detected — it is for turning a working reader OFF.
#
# Needs the fprintd package (an optdepend) and at least one ENROLLED finger.
# Run the enroll as yourself, not root — prints are stored per account:
#   sudo pacman -S fprintd
#   fprintd-enroll        # swipe until it says enroll-completed
#   fprintd-list "$USER"  # confirm it took
# Your password always works regardless — five rejected swipes retire the
# reader for that lock rather than locking you out.
lock_fingerprint = on

ai_layout       = on
ai_ctx_decor    = on
start_overlay   = off
workspace_1_intent = main workspace
workspace_2_intent = web browsing and research
workspace_3_intent = writing code and running tests
workspace_4_intent = terminal and system administration
workspace_5_intent = media and entertainment
SYNUIRC

# The console keymap does not reach Wayland: synui reads its layout from its own
# config. Without this the installer asks which keyboard you have, sets it for
# the console, and then the desktop comes up on a US layout regardless — which
# is the same "asked and ignored" failure as setting the locale and shipping no
# fonts. Appended rather than put in the heredoc above, which is quoted so that
# nothing else in it expands.
echo "xkb_layout = $SYNUI_XKB" >> "/mnt/home/$NEW_USER/.config/synui/synuirc"

# KDE Connect, and only when it was actually installed — an autostart line
# naming a binary that is not there just makes sh log "command not found" on
# every login.
#
# The indicator, not kdeconnectd: starting it D-Bus-activates the daemon
# (org.kde.kdeconnect.service) *and* puts the tray icon in the bar, where
# starting the daemon alone would leave no way to see or pair a device.
#
# It has to be named here because the package's own
# /etc/xdg/autostart/org.kde.kdeconnect.daemon.desktop never runs: nothing in a
# synui session implements the XDG autostart spec. Without this the daemon
# starts only when something first talks to it, so a phone cannot reach the
# desktop until the user has opened the app by hand — which reads as "KDE
# Connect is broken" rather than "it is not running".
if [ "$WANT_PHONE" = 1 ]; then
    cat >> "/mnt/home/$NEW_USER/.config/synui/synuirc" << 'KDECONNECT_EOF'
# KDE Connect: pairs a phone (notifications, files, clipboard, remote input).
# The indicator starts the daemon and supplies the tray icon.
autostart = kdeconnect-indicator
KDECONNECT_EOF
fi

# kitty terminal — "night drive" palette (matches synuirc border colors).
#
# dynamic_background_opacity is NOT optional here. kitty's documentation is
# explicit that the option cannot be turned on by reloading the config, and that
# a background_opacity change on reload only takes effect if dynamic opacity was
# already enabled when kitty started. So a kitty.conf written without it leaves
# synui's transparency slider permanently inert for this user, and writing the
# line later does not repair it — kitty has to be restarted. It ships on.
# areofyl/fetch reads its logo only from ~/.config/fetch/logo.txt, so it gets a
# copy in the new user's home. Sourced from the live ISO -- the same file that
# landed in /mnt/usr/share/synapseos above -- so fetch and fastfetch cannot end
# up showing two different marks. Guarded: an ISO built without the asset skips
# it, and fetch then falls back to fastfetch's Arch logo as it always did.
if [ -f /usr/share/synapseos/logo.txt ]; then
    mkdir -p "/mnt/home/$NEW_USER/.config/fetch"
    cp /usr/share/synapseos/logo.txt "/mnt/home/$NEW_USER/.config/fetch/logo.txt"
fi

# Default applications.
#
# There used to be a block here writing inode/directory into the new user's own
# ~/.config/mimeapps.list, to stop kitty — which lists inode/directory among
# kitty-open.desktop's MimeTypes — winning "open this folder" by walkover and
# opening a TERMINAL at $HOME.
#
# The synfiles package owns that answer now: it ships
# /usr/share/applications/mimeapps.list, the DISTRIBUTION default, which beats
# a bare MimeType= declaration and loses to anything the user or the admin
# chooses. Two things get better by moving it there. It reaches every existing
# machine on the next upgrade instead of only the ones installed after the line
# was added; and it stops writing a default into the user's own file, where a
# default is indistinguishable from a choice they made.
#
# Nothing to do here. If this comment outlives the arrangement, the file to
# look at is synfiles/data/mimeapps.list.

mkdir -p "/mnt/home/$NEW_USER/.config/kitty"
cat > "/mnt/home/$NEW_USER/.config/kitty/kitty.conf" << 'KITTYEOF'
font_family              monospace
font_size                11
window_padding_width     8

background_opacity         0.92
dynamic_background_opacity yes

background               #0b0b14
foreground               #c8e3ee
cursor                   #05d9e8
cursor_text_color        #0b0b14

color0                   #16161e
color1                   #ff296d
color2                   #05ffa1
color3                   #ffd319
color4                   #2d9cee
color5                   #d817ff
color6                   #05d9e8
color7                   #94a3b8
color8                   #3b3b54
color9                   #ff5c8d
color10                  #57ffbe
color11                  #ffe14d
color12                  #5cb8ff
color13                  #e55cff
color14                  #4de8f4
color15                  #d6e5f5

selection_foreground     #0b0b14
selection_background     #05d9e8
url_color                #05d9e8
KITTYEOF

# foot, same palette — kept as the rescue terminal, not as a second default.
# kitty is GPU-accelerated and needs working OpenGL; foot renders on the CPU and
# works anywhere, which matters on a VM falling back to llvmpipe. It is 793 KiB
# against kitty's 65 MiB, so carrying it is what makes synui's
# `kitty || foot || alacritty || xterm` chain an actual rescue rather than a
# decorative one. Themed to match so the fallback does not look broken.
mkdir -p "/mnt/home/$NEW_USER/.config/foot"
cat > "/mnt/home/$NEW_USER/.config/foot/foot.ini" << 'FOOTEOF'
[main]
font=monospace:size=11
pad=8x8

# [colors] is deprecated in foot >= 1.17 and [cursor].color was removed
# outright — the palette and the cursor colour both live in [colors-dark].
[colors-dark]
alpha=0.92
background=0b0b14
foreground=c8e3ee
cursor=0b0b14 05d9e8
regular0=16161e
regular1=ff296d
regular2=05ffa1
regular3=ffd319
regular4=2d9cee
regular5=d817ff
regular6=05d9e8
regular7=94a3b8
bright0=3b3b54
bright1=ff5c8d
bright2=57ffbe
bright3=ffe14d
bright4=5cb8ff
bright5=e55cff
bright6=4de8f4
bright7=d6e5f5
selection-foreground=0b0b14
selection-background=05d9e8
urls=05d9e8
FOOTEOF

# The bar — clock + system telemetry in the same palette. quickshell as of
# 2026-07-24; the SYNAPSE badge is drawn by the compositor itself (launcher.c)
# over the bar's top-left corner, so the bar deliberately leaves that corner
# empty rather than competing for it.
#
# The tray module is load-bearing, not decoration: the bar is the only thing on
# SYNAPSE that owns org.kde.StatusNotifierWatcher. Drop it and an app that
# closes to tray (Steam, by default) unmaps its window and hands its icon to a
# bus name nobody holds — the window is then unreachable, since we have no
# taskbar either. Passive items are shown because Steam registers its item
# Passive first and only later marks it Active.
#
# NOTE: SNI clients do not re-register when the watcher changes owner, so a bar
# restart drops the icon of any client that does not watch NameOwnerChanged
# (norduserd does). It costs nothing at install
# time — the bar starts before any tray app — but it is why swapping bars on a
# running session loses icons until each app restarts.
#
# No bar config is written here any more. synui packages the whole QML tree to
# /usr/share/synui/quickshell and synui-bar points at it, so a new account gets
# a working bar with nothing copied into it, and there is one copy of the config
# instead of three. Heredocs here were the reason sync-installer.py/
# check-installer-sync.py had to exist: the embedded copies drifted from the
# repo's and shipped a start menu whose items launched the wrong application. A
# user who wants to customise the bar copies the tree into
# ~/.config/quickshell/synapse and owns it from then on — synui-bar hands over to
# that path by name, which is how XDG is meant to work.

# fastfetch config — reuse the branded one from the live ISO so `syn info`
# / fastfetch shows the SynapseOS logo on the installed system too.
if [ -f /home/syn/.config/fastfetch/config.jsonc ]; then
    mkdir -p "/mnt/home/$NEW_USER/.config/fastfetch"
    cp /home/syn/.config/fastfetch/config.jsonc \
       "/mnt/home/$NEW_USER/.config/fastfetch/config.jsonc"
fi

arch-chroot /mnt chown -R "$NEW_USER:$NEW_USER" "/home/$NEW_USER"

# ── The AI model ─────────────────────────────────────────
#
# A DOWNLOAD now, not a copy. The ISO stopped carrying a gguf — it made the
# image ~4 GB bigger to ship a model the live session can only run on the CPU,
# which in a VM is slow enough to be worse than not offering it — so the model
# chosen in step 4 is fetched here, into the target, by the same syn-model the
# installed system uses later. One downloader, one set of retries, one place
# where the URL for a name is written down.
#
# LAST of the long operations, and that is the whole reason it sits here rather
# than beside the packages that pulled it in. It is minutes of network; every
# question this installer asks has been answered by the time it starts, so it
# is the part you can walk away from instead of one that strands the username
# prompt behind a progress bar. Still BEFORE the Nix step below, which probes
# the disk to write facts.nix and would otherwise record a machine with no
# model on it.
#
# Older media DO carry one, and it is Mistral 7B. When that is what was asked
# for, copy it: it is already local and the network has nothing to add.
#
# Skipping is a supported answer (Minimal, or "None" at the picker), and a
# failed download is a warning rather than a die — it leaves the machine in
# exactly the state declining the model leaves it in, and that state boots.
step "AI model"

if [ "$WANT_MODEL" != 1 ] || [ "$MODEL_CHOICE" = none ]; then
    echo "  AI model skipped — install one later with: syn model download"
else
    MODEL_SRC=""
    if [ "$MODEL_CHOICE" = mistral-7b ]; then
        for f in /run/archiso/airootfs/var/lib/synapd/models/*.gguf \
                 /var/lib/synapd/models/*.gguf; do
            [ -f "$f" ] && MODEL_SRC="$f" && break
        done
    fi

    mkdir -p /mnt/var/lib/synapd/models
    if [ -n "$MODEL_SRC" ]; then
        echo "  Copying AI model off the live media ($(du -sh "$MODEL_SRC" | cut -f1))..."
        if cp "$MODEL_SRC" /mnt/var/lib/synapd/models/synapse.gguf; then
            success "AI model installed"
        else
            rm -f /mnt/var/lib/synapd/models/synapse.gguf
            warn "Could not copy the model to the target — usually no room left.
  Once the new system is up:  syn model download $MODEL_CHOICE"
        fi
    elif [ -x /mnt/usr/bin/syn-model ]; then
        echo "  Downloading the AI model ($MODEL_CHOICE) — this is the long part of"
        echo "  the install, and everything else on the disk is already done."
        echo ""
        # SYN_MODEL_NO_RESTART: there is no running synapd to restart in here,
        # and systemctl in a chroot would only print about it.
        if SYN_MODEL_NO_RESTART=1 arch-chroot /mnt syn-model download "$MODEL_CHOICE" -y; then
            success "AI model installed"
        else
            warn "Model download failed — the install itself is fine, the AI is not.
  Once the new system is online:  syn model download $MODEL_CHOICE"
        fi
    else
        warn "syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."
    fi
fi

# ── Nix ───────────────────────────────────────────────────
#
# The optional second layer: pacman keeps the system, Nix + Home Manager give
# the USER a declarative environment. The configurator is /etc/synapseos/nix —
# flake.nix, an editable home.nix, and a generated facts.nix describing this
# machine so the expressions branch on the install instead of being hand-edited
# per box.
#
# LAST among the optional groups, and it has to be. facts.nix is PROBED, not
# passed in: syn-nix-facts looks at what is actually on the disk. Run any
# earlier and it would record a machine without Steam, without the model,
# without whichever desktop the DE step had not written yet.
#
# NOTHING IS BUILT HERE. Setting up the config is seconds; realising it is a
# multi-gigabyte download from cache.nixos.org, and the nix daemon is not
# running in the chroot to do it. So the installer leaves a machine that is
# ready and tells the user the one command. An installer that hung for twenty
# minutes on a substituter would be a worse installer.
if [ "$WANT_NIX" = 1 ]; then
    step "Configuring Nix"

    if arch-chroot /mnt pacman -S --noconfirm --needed nix 2>&1 | tail -2; then
        # `syn nix init` is the ONLY implementation of this setup, and the
        # installer calls it rather than repeating the eight steps it does —
        # the copy that lives in two places is the copy that drifts. It works
        # in here because arch-chroot bind-mounts /sys and /proc, which is
        # what the GPU probe reads.
        #
        # Needs the syn package, which the core set installs above. It can be
        # declined ("Customise the core daemons too?"), so this is checked
        # rather than assumed.
        if [ -x /mnt/usr/bin/syn ]; then
            arch-chroot /mnt syn nix init 2>&1 | sed 's/^/  /'
            success "Nix configured — /etc/synapseos/nix"
            echo "  Nothing is built yet. As $NEW_USER, after the first boot:"
            echo "      syn nix apply"
            echo "  That is the download — a few hundred MB before any packages"
            echo "  you add to home.nix. 'syn nix edit' opens it."
        else
            warn "nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."
        fi
    else
        warn "nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."
    fi
fi

# ── Mark firstboot done ──────────────────────────────────
mkdir -p /mnt/var/lib/synapseos
echo "firstboot completed by installer: $(date)" > /mnt/var/lib/synapseos/firstboot.done

# tmpfiles for synapd — NOT written here any more, and the stale copy is removed.
#
# synapd ships /usr/lib/tmpfiles.d/synapd.conf itself, and has since it gained an
# unprivileged user. A file of the same name in /etc/tmpfiles.d SHADOWS it
# completely (systemd takes the first match by basename, /etc before /usr/lib) —
# so this block, written once at install time and never updated again, silently
# reverted every later ownership change the package made.
#
# What it cost: the package declares
#   d /var/lib/synapd 0750 synapd synapd
# and this said `0755 root root`. synapd runs as uid 964, so it could not create
# /var/lib/synapd/model.selected and logged "cannot record the model choice …
# Permission denied" on every switch. The remembered-model feature (synapd
# 0.1.0-28) has therefore never worked on ANY installed system — the daemon fell
# back to its --model flag after every restart, which is the exact bug that
# feature was written to fix.
#
# The removal is unconditional and idempotent: an upgraded box still carries the
# old file, and nothing else would ever take it away — it is unowned, so pacman
# will not, and `Z` in the package's own config cannot fix a directory whose
# ownership the shadowing file keeps re-asserting.
rm -f /mnt/etc/tmpfiles.d/synapd.conf
arch-chroot /mnt systemd-tmpfiles --create /usr/lib/tmpfiles.d/synapd.conf 2>/dev/null || true

# The encryption hook is what prompts for the passphrase at boot and opens the
# root volume. WHICH hook depends on how the initramfs is built, and getting it
# wrong boots to a rescue shell with no stated reason:
#
#   udev-based    (HOOKS=... udev ...)     -> `encrypt`,    cryptdevice=UUID=x:name
#   systemd-based (HOOKS=... systemd ...)  -> `sd-encrypt`, rd.luks.name=x=name
#
# They are not interchangeable: `encrypt` is a busybox hook that needs udev,
# and it does not read rd.luks.*; `sd-encrypt` ignores cryptdevice=. Arch has
# shipped both defaults over time, so detect rather than assume — the whole
# point of this block is that the failure is silent and happens at first boot.
#
# Position: after `block` (the device must exist) and before `filesystems`
# (which mounts what it produced). Inserting immediately before `filesystems`
# satisfies both, since `block` already precedes it in every stock HOOKS line.
CRYPT_HOOK=""
if [ "$ENCRYPT" = "yes" ]; then
    if grep -qE '^HOOKS=.*[( ]systemd[ )]' /mnt/etc/mkinitcpio.conf; then
        CRYPT_HOOK="sd-encrypt"
    else
        CRYPT_HOOK="encrypt"
    fi
    echo "  Adding the $CRYPT_HOOK hook to mkinitcpio..."

    grep -qE "^HOOKS=.*[( ]$CRYPT_HOOK[ )]" /mnt/etc/mkinitcpio.conf \
        || sed -i "/^HOOKS=/s/\bfilesystems\b/$CRYPT_HOOK filesystems/" \
                 /mnt/etc/mkinitcpio.conf
    grep -qE "^HOOKS=.*[( ]$CRYPT_HOOK[ )]" /mnt/etc/mkinitcpio.conf \
        || die "could not add the $CRYPT_HOOK hook — the installed system would not boot"

    # sd-encrypt resolves the volume from the kernel cmdline, but a
    # crypttab.initramfs makes the mapping explicit and survives someone later
    # regenerating the initramfs by hand.
    if [ "$CRYPT_HOOK" = "sd-encrypt" ]; then
        printf '%s UUID=%s none luks,discard\n' \
            "$CRYPT_NAME" "$LUKS_UUID" > /mnt/etc/crypttab.initramfs
    fi
fi

echo "  Generating initramfs..."
# /usr/bin/mkinitcpio by ABSOLUTE PATH, not `mkinitcpio`.
#
# limine-mkinitcpio-hook installs a wrapper at /usr/local/bin/mkinitcpio, which
# comes FIRST on the default PATH, and that wrapper ends in an interactive
# prompt when it is passed -P:
#
#     read -rp "==> Would you like to run 'limine-mkinitcpio' now? [Y/n]: "
#
# It is installed by pacstrap far above, so by the time this line runs the
# wrapper is already inside /mnt. Called by name this would block the installer
# forever on a question nobody is there to answer — or, worse, consume a
# keystroke intended for a later prompt, which is a failure this installer has
# already had once with pacstrap. The absolute path takes the real one.
arch-chroot /mnt /usr/bin/mkinitcpio -P 2>&1 | tail -5 \
    || die "mkinitcpio failed — the installed system would not boot"
[ -s /mnt/boot/initramfs-linux.img ] \
    || die "initramfs missing after mkinitcpio — the installed system would not boot"

success "System configured"

# ── Bootloader ────────────────────────────────────────────
header
step "Installing Bootloader"

# With an encrypted root the kernel needs to be told which volume to unlock and
# what the opened device will be called; the encrypt hook reads both from here.
# This goes in GRUB_CMDLINE_LINUX rather than _DEFAULT so it is also present on
# the recovery entry — a rescue boot that cannot open the root is useless.
GRUB_CRYPT_CMDLINE=""
if [ "$ENCRYPT" = "yes" ]; then
    if [ "$CRYPT_HOOK" = "sd-encrypt" ]; then
        GRUB_CRYPT_CMDLINE="rd.luks.name=$LUKS_UUID=$CRYPT_NAME root=/dev/mapper/$CRYPT_NAME"
    else
        GRUB_CRYPT_CMDLINE="cryptdevice=UUID=$LUKS_UUID:$CRYPT_NAME root=/dev/mapper/$CRYPT_NAME"
    fi
fi

if [ "$BOOTLOADER" = "grub" ]; then
    cat > /mnt/etc/default/grub << EOF
GRUB_DEFAULT=0
GRUB_TIMEOUT=5
GRUB_DISTRIBUTOR="SynapseOS"
GRUB_CMDLINE_LINUX_DEFAULT="$GPU_KERNEL_PARAMS"
GRUB_CMDLINE_LINUX="$GRUB_CRYPT_CMDLINE"
# os-prober ON so grub-mkconfig detects Windows / other OSes and adds their
# boot menu entries. Recent GRUB disables it by default for security; a
# single-OS install just finds nothing, so enabling it globally is safe and is
# what makes the alongside (dual-boot) install actually offer the other OS.
GRUB_DISABLE_OS_PROBER=false
EOF

    mkdir -p /mnt/boot/grub

    echo "  Installing GRUB ($BOOT_MODE)..."
    if [ "$BOOT_MODE" = "uefi" ]; then
        arch-chroot /mnt grub-install \
            --target=x86_64-efi \
            --efi-directory=/boot/efi \
            --bootloader-id=SynapseOS \
            --recheck \
            2>&1 || die "grub-install (UEFI) failed"
    else
        arch-chroot /mnt grub-install \
            --target=i386-pc \
            --recheck \
            "$DISK" \
            2>&1 || die "grub-install (BIOS) failed"
    fi

    echo "  Generating GRUB config..."
    arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg 2>&1 \
        || die "grub-mkconfig failed"

    [ -f /mnt/boot/grub/grub.cfg ] || die "grub.cfg missing after install"

    # ── And make it READABLE ──────────────────────────────
    #
    # grub-mkconfig writes its output inside an unconditional `umask 077` — the
    # temp file AND the final `cat grub.cfg.new > grub.cfg` — so a file it has
    # to create comes out 0600 root:root. That stays invisible until something
    # which is not root needs an answer out of it: syn-settings' Kernel pane
    # reads grub.cfg as the user, so on a fresh install every question it asked
    # came back "permission denied", and it recorded that as "no". The pane
    # reported NO BOOT ENTRY about the kernel the machine was RUNNING, and
    # never offered to change the default at all.
    #
    # Once is enough. grub-mkconfig's last act truncates the existing file in
    # place, which keeps whatever mode it already has, so every later
    # regeneration — the hook below, grub-btrfsd, syn-settings — preserves it.
    #
    # Skipped if a GRUB password is configured: that lives in grub.cfg as a
    # password_pbkdf2 hash and is nobody else's business. Nothing here sets
    # one, but this file is not the only thing that can.
    if grep -qE '^[[:space:]]*password(_pbkdf2)? ' /mnt/boot/grub/grub.cfg; then
        warn "grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"
    else
        chmod 0644 /mnt/boot/grub/grub.cfg
    fi

    # ── Make FUTURE kernels bootable ──────────────────────
    #
    # grub-mkconfig above runs ONCE, here. Arch ships no hook that runs it
    # again, so installing linux-lts next month gets an initramfs from
    # mkinitcpio's hook and no menu entry from anything — a complete, correct,
    # unbootable kernel that pacman reports as installed.
    #
    # grub-mkconfig is the right tool for this and needs no arguments: its
    # 10_linux script enumerates every kernel in /boot by itself. So the hook is
    # simply "run the same command the installer ran".
    #
    # NEEDED, not Exec-on-every-transaction: the trigger is narrow on purpose.
    # A pacman hook that regenerates the boot menu on every package would run
    # grub-mkconfig hundreds of times a year for no reason, and os-prober makes
    # it slow.
    mkdir -p /mnt/etc/pacman.d/hooks
    cat > /mnt/etc/pacman.d/hooks/95-grub-mkconfig.hook << 'EOF'
# Written by syn-install.
#
# Arch ships nothing that regenerates grub.cfg when a kernel is added or
# removed, so without this a second kernel is installed, gets an initramfs, and
# never appears in the boot menu. Triggers on the kernel module trees rather
# than on package names so it covers every kernel, including ones this OS does
# not ship a name for.
[Trigger]
Type = Path
Operation = Install
Operation = Upgrade
Operation = Remove
Target = usr/lib/modules/*/vmlinuz

[Action]
Description = Updating the GRUB boot menu...
When = PostTransaction
Exec = /usr/bin/grub-mkconfig -o /boot/grub/grub.cfg
EOF

elif [ "$BOOTLOADER" = "systemd-boot" ]; then
    # ── systemd-boot ──────────────────────────────────────
    #
    # No filesystem drivers, no config generator, no OS prober: it reads the ESP
    # and boots what it finds. That is the whole appeal, and it is why the ESP is
    # mounted at /boot here — the kernels have to live where it can see them.
    echo "  Installing systemd-boot..."
    arch-chroot /mnt bootctl --esp-path=/boot install 2>&1 \
        || die "bootctl install failed"

    # bootctl is supposed to register the loader with the FIRMWARE as well as
    # copy it onto the ESP, and when it cannot it says so on stderr and exits 0.
    # The result is an ESP that looks perfect — systemd-bootx64.efi, the
    # removable \EFI\BOOT\BOOTX64.EFI, loader.conf, a correct entry, the kernel
    # and initramfs all present — attached to a machine whose NVRAM has never
    # heard of it. The next boot goes to whatever else the firmware knows about,
    # which with the install media still attached is the installer, and the whole
    # thing reads as "the install did not happen".
    #
    # Seen exactly that in a UEFI VM: every file in place, no "Linux Boot
    # Manager" entry in NVRAM, next boot went to the DVD. GRUB does not have this
    # problem because grub-install writes its own entry, and the limine branch
    # below has always created one explicitly. This is the one loader that was
    # trusting someone else to do it.
    if ! arch-chroot /mnt efibootmgr 2>/dev/null | grep -qi "Linux Boot Manager"; then
        _esp_disk="$(lsblk -no PKNAME "$PART_EFI" 2>/dev/null | head -1)"
        _esp_num="$(lsblk -no PARTN "$PART_EFI" 2>/dev/null | head -1)"
        if [ -n "$_esp_disk" ] && [ -n "$_esp_num" ]; then
            echo "  Registering systemd-boot with the firmware..."
            arch-chroot /mnt efibootmgr --create --disk "/dev/$_esp_disk" \
                --part "$_esp_num" --loader '\EFI\systemd\systemd-bootx64.efi' \
                --label "SynapseOS" --unicode >/dev/null 2>&1 \
                || warn "efibootmgr entry not created — the removable-media path still applies"
        fi
    fi

    # The root has to be named on the kernel command line. GRUB derives this
    # itself from grub-mkconfig; here it is written by hand, so it is written
    # from the same facts the fstab was: the LUKS mapper when encrypting, the
    # filesystem UUID otherwise, plus the subvolume when the root is btrfs.
    SDB_ROOT=""
    if [ "$ENCRYPT" = "yes" ]; then
        if [ "$CRYPT_HOOK" = "sd-encrypt" ]; then
            SDB_ROOT="rd.luks.name=$LUKS_UUID=$CRYPT_NAME root=/dev/mapper/$CRYPT_NAME"
        else
            SDB_ROOT="cryptdevice=UUID=$LUKS_UUID:$CRYPT_NAME root=/dev/mapper/$CRYPT_NAME"
        fi
    else
        _ruuid="$(blkid -s UUID -o value "$ROOT_FS_DEV")"
        [ -n "$_ruuid" ] || die "could not read the root filesystem UUID"
        SDB_ROOT="root=UUID=$_ruuid"
    fi
    [ "$ROOT_FS" = "btrfs" ] && SDB_ROOT="$SDB_ROOT rootflags=subvol=@"

    mkdir -p /mnt/boot/loader/entries
    cat > /mnt/boot/loader/loader.conf << EOF
default synapseos.conf
timeout 5
console-mode keep
editor no
EOF

    cat > /mnt/boot/loader/entries/synapseos.conf << EOF
title   SynapseOS
linux   /vmlinuz-linux
initrd  /initramfs-linux.img
options $SDB_ROOT rw $GPU_KERNEL_PARAMS
EOF

    # There is deliberately no "(fallback initramfs)" entry. Arch's linux.preset
    # ships PRESETS=('default') — `pacman -Qkk linux` confirms that file
    # unmodified — so /boot/initramfs-linux-fallback.img is never generated, and
    # the entry that named it for years could only ever fail to boot. Adding the
    # fallback back means adding 'fallback' to the preset and rerunning
    # mkinitcpio -P, not writing the entry; the loop below now refuses the
    # entry-without-image combination outright.

    # The kernel must actually be ON the ESP. If pacstrap ran before the ESP was
    # mounted at /boot, these entries would point at files that are not there and
    # the machine would drop to the boot menu with nothing to boot.
    [ -f /mnt/boot/vmlinuz-linux ] \
        || die "vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"
    [ -f /mnt/boot/initramfs-linux.img ] \
        || die "initramfs is not on the ESP — systemd-boot would find nothing to boot"
    [ -f /mnt/boot/EFI/BOOT/BOOTX64.EFI ] || [ -f /mnt/boot/EFI/systemd/systemd-bootx64.efi ] \
        || die "systemd-boot did not install its EFI binary"

    # And EVERY entry must name files that exist, not just the default one. The
    # two checks above pass whatever else the menu claims — which is exactly how
    # a dead fallback entry survived: nothing here ever read the entry files
    # back. This does, so the next entry added without its image fails the
    # install instead of the boot.
    _miss="$(esp_entry_missing_file /mnt/boot)" \
        && die "a boot entry names a file that is not on the ESP — $_miss"

    # ── Make FUTURE kernels bootable ──────────────────────
    #
    # synapseos.conf above names /vmlinuz-linux and nothing else, so a kernel
    # installed later gets an initramfs from mkinitcpio's hook and no entry
    # from anything. systemd-boot has no config generator by design — it boots
    # what it finds on the ESP — but systemd DOES ship kernel-install for
    # exactly this, and Arch's mkinitcpio ships the 50-mkinitcpio.install
    # plugin it needs. So the entry writing is delegated, not reimplemented.
    #
    # kernel-install reads the command line from /etc/kernel/cmdline. Written
    # here from the same facts as synapseos.conf rather than left to fall back
    # to /proc/cmdline, which during an install is the live ISO's command line
    # — on an encrypted install that is the difference between booting and a
    # rescue shell.
    mkdir -p /mnt/etc/kernel
    printf '%s rw %s\n' "$SDB_ROOT" "$GPU_KERNEL_PARAMS" > /mnt/etc/kernel/cmdline

    # THE STOCK KERNEL IS DELIBERATELY SKIPPED. synapseos.conf already boots
    # it and loader.conf names that file as the default, so letting
    # kernel-install handle it too would add a second menu entry for the same
    # kernel AND a second copy of vmlinuz+initramfs on the ESP — around 150 MB
    # on a partition sized at 1 GiB. This hook exists for the kernels
    # synapseos.conf does not cover, which is all the others.
    mkdir -p /mnt/etc/pacman.d/hooks
    cat > /mnt/etc/pacman.d/hooks/95-systemd-boot-entries.hook << 'EOF'
# Written by syn-install.
#
# systemd-boot has no config generator, so nothing would give a kernel
# installed after the OS a boot entry. kernel-install is systemd's own tool for
# this and mkinitcpio ships the plugin it needs.
#
# The stock `linux` kernel is skipped: syn-install wrote synapseos.conf for it
# and loader.conf makes that the default, so generating a second entry would
# duplicate both the menu item and the ~150 MB of kernel+initramfs on the ESP.
[Trigger]
Type = Path
Operation = Install
Operation = Upgrade
Target = usr/lib/modules/*/pkgbase

[Action]
Description = Adding boot entries for newly installed kernels...
When = PostTransaction
NeedsTargets
Exec = /bin/sh -c 'while read -r t; do rel=${t#usr/lib/modules/}; rel=${rel%/pkgbase}; [ -f "/$t" ] || continue; [ "$(cat "/$t")" = linux ] && continue; [ -f "/usr/lib/modules/$rel/vmlinuz" ] || continue; /usr/bin/kernel-install add "$rel" "/usr/lib/modules/$rel/vmlinuz" || true; done'
EOF

else
    # ── limine ────────────────────────────────────────────
    #
    # Two files and a text config. limine ships the EFI binary as a data file in
    # /usr/share/limine; installing it means copying that onto the ESP, which is
    # why the ESP is mounted at /boot for this loader — limine expects the
    # kernels on FAT32 beside it rather than reading the root filesystem.
    echo "  Installing limine..."
    mkdir -p /mnt/boot/EFI/BOOT
    cp /usr/share/limine/BOOTX64.EFI /mnt/boot/EFI/BOOT/BOOTX64.EFI \
        || die "could not copy limine's EFI binary to the ESP"

    LIM_ROOT=""
    if [ "$ENCRYPT" = "yes" ]; then
        if [ "$CRYPT_HOOK" = "sd-encrypt" ]; then
            LIM_ROOT="rd.luks.name=$LUKS_UUID=$CRYPT_NAME root=/dev/mapper/$CRYPT_NAME"
        else
            LIM_ROOT="cryptdevice=UUID=$LUKS_UUID:$CRYPT_NAME root=/dev/mapper/$CRYPT_NAME"
        fi
    else
        _ruuid="$(blkid -s UUID -o value "$ROOT_FS_DEV")"
        [ -n "$_ruuid" ] || die "could not read the root filesystem UUID"
        LIM_ROOT="root=UUID=$_ruuid"
    fi
    [ "$ROOT_FS" = "btrfs" ] && LIM_ROOT="$LIM_ROOT rootflags=subvol=@"

    # boot():/ is limine's own notation for "the partition this config is on",
    # which here is the ESP. The machine-id comment is not decoration:
    # limine-snapper-sync uses it to find which entry to hang snapshots under,
    # and without it the tool matches on the OS name and silently does nothing
    # when that name does not line up.
    #
    # THE NESTING IS LOAD-BEARING. limine's config format distinguishes a branch
    # ("/name", "/+name" for one that opens expanded) from a kernel entry nested
    # under it ("//name"). A flat "/SynapseOS" carrying kernel_path directly is a
    # perfectly bootable menu, and it is what this wrote until snapshots were
    # tested end to end: limine-snapper-sync refuses it with "Your OS entry has
    # no kernel in /boot/limine.conf. Please add at least a kernel entry
    # '//<kernel name>'" — and then EXITS 0, so nothing upstream of it notices.
    # It needs a branch it can append its own "//Snapshots" subtree to. This is
    # the shape of upstream's own documented example.
    cat > /mnt/boot/limine.conf << EOF
timeout: 5

/+SynapseOS
comment: machine-id=$(cat /mnt/etc/machine-id 2>/dev/null)

    //SynapseOS
    protocol: linux
    kernel_path: boot():/vmlinuz-linux
    kernel_cmdline: $LIM_ROOT rw $GPU_KERNEL_PARAMS
    module_path: boot():/initramfs-linux.img
EOF

    # ── Make FUTURE kernels bootable ──────────────────────
    #
    # The entry above covers the kernel this install ships and nothing else.
    # Install linux-lts later and mkinitcpio's pacman hook builds its initramfs
    # — so /boot fills with a complete, correct, entirely unbootable kernel,
    # and pacman reports success. GRUB regenerates grub.cfg and systemd-boot
    # has kernel-install; limine has no generator in the base system at all.
    #
    # limine-mkinitcpio-hook is that generator, from the same upstream as the
    # limine-snapper-sync installed alongside it. It replaces Arch's
    # 90-mkinitcpio-install.hook from /etc (which shadows the one in
    # /usr/share/libalpm/hooks) with one that writes the limine entry too.
    #
    # /etc/default/limine is written HERE, from the same facts as the entry
    # above, rather than left for the tool to guess. Unset, it falls back to
    # /etc/kernel/cmdline or /proc/cmdline — and /proc/cmdline during an install
    # is the ISO's command line, which would give every future kernel the live
    # medium's root instead of this machine's. On an encrypted install that is
    # the difference between booting and a rescue shell.
    cat > /mnt/etc/default/limine << EOF
# Written by syn-install. See /etc/limine-entry-tool.conf for every option.
#
# The kernel command line for entries limine-entry-tool generates. It is the
# same one syn-install wrote into the SynapseOS entry in limine.conf, taken
# from this machine's own root device rather than from /proc/cmdline — which
# at install time belongs to the live ISO.
KERNEL_CMDLINE[default]="$LIM_ROOT rw $GPU_KERNEL_PARAMS"

# Target the entry syn-install wrote, by machine-id. limine.conf already
# carries a matching "comment: machine-id=" line, which is also what
# limine-snapper-sync uses to find where to hang snapshots.
ESP_PATH="/boot"
EOF

    # Not fatal. A machine that boots the kernel it shipped with is a working
    # install; what it loses is the ability to add a second one later without
    # editing limine.conf by hand, and syn-settings' Kernel pane says so
    # plainly when the generator is absent.
    arch-chroot /mnt pacman -S --noconfirm --needed limine-mkinitcpio-hook \
        >/dev/null 2>&1 \
        || warn "limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"

    # limine does not register itself with the firmware; without this the
    # machine falls back to the removable-media path and may not boot at all on
    # firmware that ignores it.
    if [ -n "${PART_EFI:-}" ]; then
        _esp_disk="$(lsblk -no PKNAME "$PART_EFI" 2>/dev/null | head -1)"
        _esp_num="$(lsblk -no PARTN "$PART_EFI" 2>/dev/null | head -1)"
        if [ -n "$_esp_disk" ] && [ -n "$_esp_num" ]; then
            arch-chroot /mnt efibootmgr --create --disk "/dev/$_esp_disk" \
                --part "$_esp_num" --loader '\EFI\BOOT\BOOTX64.EFI' \
                --label "SynapseOS" --unicode >/dev/null 2>&1 \
                || warn "efibootmgr entry not created — the removable-media path still applies"
        fi
    fi

    [ -f /mnt/boot/vmlinuz-linux ] \
        || die "vmlinuz-linux is not on the ESP — limine would find nothing to boot"
    [ -f /mnt/boot/EFI/BOOT/BOOTX64.EFI ] \
        || die "limine's EFI binary is not on the ESP"
    grep -q 'kernel_path' /mnt/boot/limine.conf \
        || die "limine.conf has no kernel entry"

    # The same read-it-back check the systemd-boot branch does. This is what
    # would have caught the fallback entry naming an initramfs that mkinitcpio
    # never generates.
    _miss="$(esp_entry_missing_file /mnt/boot)" \
        && die "a limine entry names a file that is not on the ESP — $_miss"
fi

# ── The firmware has to know the disk is bootable ─────────
#
# Everything above this line puts files on the ESP. None of it makes the machine
# boot them: UEFI boots what its NVRAM points at, and an install whose loader is
# perfect but unregistered boots the installer media instead — silently, and
# looking exactly like an install that never ran.
#
# Not fatal. Removable media (\EFI\BOOT\BOOTX64.EFI) is a real fallback that a
# lot of firmware honours, and some firmware refuses NVRAM writes from a chroot
# outright, so failing the install here would be worse than saying so. But it is
# said LOUDLY, with the fix, because the symptom names nothing.
if [ "$BOOT_MODE" = "uefi" ]; then
    if arch-chroot /mnt efibootmgr 2>/dev/null \
         | grep -qiE "SynapseOS|Linux Boot Manager"; then
        success "Registered with the firmware ($(arch-chroot /mnt efibootmgr 2>/dev/null \
                 | grep -iE 'SynapseOS|Linux Boot Manager' | head -1 | cut -c1-40))"
    else
        warn "No EFI boot entry names this install.

  The bootloader IS on the EFI partition, but the firmware has not been told
  about it, so it will boot the install media or the network first — which
  looks exactly like the install did not happen. Remove the install media and
  try again; if it still does not boot, pick the disk from the firmware's boot
  menu once, or add the entry by hand:

      efibootmgr --create --disk $DISK --part 1 --label SynapseOS \\
          --loader '\\EFI\\BOOT\\BOOTX64.EFI'"
    fi
fi

# Hard-verify the encrypted boot path. Every one of these is a way to end up
# with an install that partitions, formats and reports success, then drops to a
# rescue shell on first boot — which is the worst possible time to find out.
if [ "$ENCRYPT" = "yes" ]; then
    echo "  Verifying the encrypted boot path..."

    cryptsetup isLuks "$PART_ROOT" \
        || die "$PART_ROOT is not a LUKS volume after install"

    # The bootloader has to name the LUKS volume, and where that is written
    # differs: grub-mkconfig bakes it into grub.cfg, systemd-boot takes it from
    # the entry file written above. Checking the wrong file would pass
    # vacuously — grep on a path that does not exist is simply a failure — so
    # this asks the loader that is actually installed.
    case "$BOOTLOADER" in
        grub)
            grep -q "$LUKS_UUID" /mnt/boot/grub/grub.cfg \
                || die "grub.cfg never mentions $LUKS_UUID — it would not unlock at boot" ;;
        systemd-boot)
            grep -q "$LUKS_UUID" /mnt/boot/loader/entries/synapseos.conf \
                || die "the systemd-boot entry never mentions $LUKS_UUID — it would not unlock at boot" ;;
        limine)
            grep -q "$LUKS_UUID" /mnt/boot/limine.conf \
                || die "limine.conf never mentions $LUKS_UUID — it would not unlock at boot" ;;
    esac

    grep -qE "^HOOKS=.*[( ]$CRYPT_HOOK[ )]" /mnt/etc/mkinitcpio.conf \
        || die "the $CRYPT_HOOK hook is missing from mkinitcpio.conf"

    # The hook being named in the config is not the same as it being IN the
    # image — a hook whose package is absent is skipped with a warning that
    # scrolls past, and mkinitcpio still exits 0.
    lsinitcpio /mnt/boot/initramfs-linux.img 2>/dev/null \
        | grep -qE "(hooks/$CRYPT_HOOK|cryptsetup)" \
        || die "$CRYPT_HOOK is not in the initramfs — the system would not unlock"

    # /boot must be its own partition, or the kernel GRUB needs to read is
    # sitting inside the volume it has not unlocked yet.
    mountpoint -q /mnt/boot \
        || die "/boot is not a separate mount — an encrypted root needs a plain /boot"

    grep -qE '^[^#]*\s/boot\s' /mnt/etc/fstab \
        || die "/boot is missing from fstab — it would not be mounted after boot"

    success "Encrypted boot path verified"
fi

# ── Snapshots ─────────────────────────────────────────────
#
# snapper's own `create-config` CANNOT run here, and this is not a maybe:
#
#   create subvolume failed, btrfs_util_create_subvolume_fd() failed, errno:17
#   Creating config failed (creating btrfs subvolume .snapshots failed since it
#   already exists).
#
# It insists on creating /.snapshots itself, and /.snapshots is already the
# @snapshots subvolume mounted there — by design, because that is the layout
# `snapper rollback` needs. This used to call it anyway with `2>/dev/null ||
# true`, guard the tuning on a config file that therefore never appeared, and
# still print "Snapshots enabled". The result was an install with snapper
# installed, snapper-cleanup enabled, limine-snapper-sync running — and
# `snapper list-configs` EMPTY, so not one snapshot could ever be taken.
#
# So the config is written directly from snapper's own template, which already
# carries SUBVOLUME="/" and FSTYPE="btrfs", and the existing subvolume is kept.
if [ "$SNAPSHOTS" = "yes" ]; then
    step "Configuring snapshots"

    [ -f /mnt/usr/share/snapper/config-templates/default ] \
        || die "snapper's config template is missing — snapshots cannot be configured"
    mkdir -p /mnt/etc/snapper/configs
    cp /mnt/usr/share/snapper/config-templates/default /mnt/etc/snapper/configs/root \
        || die "could not write /etc/snapper/configs/root"

    # The defaults keep hourly timeline snapshots forever and are how a btrfs
    # system quietly fills its own disk. Timeline off, and a bounded number of
    # pacman snapshots: the ones worth having are the pre/post pairs around an
    # upgrade, not a photograph of every hour the machine was idle.
    sed -i \
        -e 's/^TIMELINE_CREATE=.*/TIMELINE_CREATE="no"/' \
        -e 's/^NUMBER_CLEANUP=.*/NUMBER_CLEANUP="yes"/' \
        -e 's/^NUMBER_LIMIT=.*/NUMBER_LIMIT="12"/' \
        -e 's/^NUMBER_LIMIT_IMPORTANT=.*/NUMBER_LIMIT_IMPORTANT="6"/' \
        /mnt/etc/snapper/configs/root

    # A config file nothing lists is still not a config: snapper-cleanup and
    # snapper's own tooling read the set from SNAPPER_CONFIGS, and the package
    # ships it empty.
    if grep -q '^SNAPPER_CONFIGS=' /mnt/etc/conf.d/snapper 2>/dev/null; then
        sed -i 's/^SNAPPER_CONFIGS=.*/SNAPPER_CONFIGS="root"/' /mnt/etc/conf.d/snapper
    else
        echo 'SNAPPER_CONFIGS="root"' >> /mnt/etc/conf.d/snapper
    fi

    # Ask snapper itself rather than trusting the file we just wrote — a
    # template that changes shape upstream, or a sed that stops matching, both
    # look exactly like success from here.
    arch-chroot /mnt snapper --no-dbus list-configs 2>/dev/null | grep -qE '^root\s' \
        || die "snapper does not see the 'root' config — snapshots would never be taken"
    grep -q '^TIMELINE_CREATE="no"' /mnt/etc/snapper/configs/root \
        || die "snapper's root config was not tuned — timeline snapshots would fill the disk"

    arch-chroot /mnt systemctl enable snapper-cleanup.timer 2>/dev/null || true

    # What turns a snapshot into a boot entry is loader-specific.
    if [ "$BOOTLOADER" = "grub" ]; then
        # grub-btrfsd watches /.snapshots and regenerates the menu when one
        # appears; without it the entries only refresh on a manual
        # grub-mkconfig.
        arch-chroot /mnt systemctl enable grub-btrfsd.service 2>/dev/null \
            || warn "could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"
        # Regenerate now, so the submenu exists on the very first boot rather
        # than only after the first upgrade.
        arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg 2>/dev/null || true
        success "Snapshots enabled (snapper + snap-pac, bootable from GRUB)"
    else
        # limine-snapper-sync copies each snapshot's kernel and initramfs onto
        # the ESP and writes the entries under the OS branch in limine.conf.
        arch-chroot /mnt systemctl enable limine-snapper-sync.service 2>/dev/null \
            || warn "could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"

        # The desktop entry limine-snapper-sync ships ("Limine-snapper-restore")
        # opens a terminal to show the restore running. Its own fallback chain
        # checks foot BEFORE kitty, so on SynapseOS the one third-party menu item
        # that opens a terminal would open the rescue one. TERMINAL takes a
        # command with its exec flag, the same shape as its konsole example.
        if ! grep -q '^TERMINAL=' /mnt/etc/limine-snapper-sync.conf 2>/dev/null; then
            printf '\n# SynapseOS: syntty is the default terminal; upstream probes foot first.\nTERMINAL="syntty -e"\n' \
                >> /mnt/etc/limine-snapper-sync.conf
        fi

        # One snapshot to restore TO. snap-pac only fires on a pacman
        # transaction, so without this a fresh install has an empty snapshot
        # list and the desktop's "Limine-snapper-restore" entry has nothing to
        # work with. Soft: an install is not worth failing over a snapshot.
        arch-chroot /mnt snapper --no-dbus -c root create -d "post-install" 2>/dev/null \
            || warn "could not take the post-install snapshot"

        # This CANNOT be `arch-chroot /mnt limine-snapper-sync`. The tool reads
        # /proc/self/mounts to find the root subvolume, and inside arch-chroot
        # that is the installer's own mount table — it answers "No root
        # subvolume found in /proc/self/mounts", and it EXITS 0 doing it, so
        # `|| true` never caught anything. It has to run on the booted target.
        #
        # And it cannot be left to the watcher either: limine-snapper-sync.service
        # is inotify-only. Restarting it does NOT sync snapshots that already
        # exist (verified — the Snapshots subtree stays absent across a restart),
        # so the entries would first appear only after the next snapshot event,
        # i.e. after the user's first pacman transaction.
        cat > /mnt/etc/systemd/system/synapseos-limine-snapshot-sync.service << 'EOF'
[Unit]
Description=Populate limine's snapshot boot entries once, on the first boot
ConditionPathExists=!/var/lib/synapseos/limine-snapshots.synced
After=local-fs.target
RequiresMountsFor=/boot /.snapshots

[Service]
Type=oneshot
ExecStart=/usr/bin/limine-snapper-sync
ExecStartPost=/usr/bin/sh -c 'mkdir -p /var/lib/synapseos && touch /var/lib/synapseos/limine-snapshots.synced'

[Install]
WantedBy=multi-user.target
EOF
        arch-chroot /mnt systemctl enable synapseos-limine-snapshot-sync.service 2>/dev/null \
            || warn "could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"

        success "Snapshots enabled (snapper + snap-pac, bootable from limine)"
    fi
fi

success "Bootloader installed"

# ── Done ──────────────────────────────────────────────────
header
green "  SynapseOS installation complete!"
echo ""
DE_NAME="SynapseUI"
case "$DE_CHOICE" in
    2) DE_NAME="KDE Plasma" ;;
    3) DE_NAME="GNOME" ;;
    4) DE_NAME="TTY only" ;;
esac
echo "  $(bold 'Disk:')     $DISK"
echo "  $(bold 'Boot:')     $BOOT_MODE"
if [ "$ENCRYPT" = "yes" ]; then
    echo "  $(bold 'Encrypted:') yes — LUKS2 on $PART_ROOT"
fi
echo "  $(bold 'Desktop:')  $DE_NAME"
echo "  $(bold 'User:')     $NEW_USER"
echo "  $(bold 'Hostname:') synapse"
echo ""
line
echo ""
echo "  Log in as '$(bold "$NEW_USER")' after reboot."
echo "  Type '$(bold "syn ask anything")' to get started."
echo ""
echo "  Admin: use $(bold "sudo") with your user password."
echo "  The root account is locked (no root login / su)."
echo "  Note: 3 wrong password attempts lock the account for 10 minutes."
echo ""

if [ "$ENCRYPT" = "yes" ]; then
    warn "You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."
    echo ""
    echo "  Manage it later with $(bold 'syn-crypt'):"
    echo "    syn-crypt status                    is this disk encrypted, and how"
    echo "    sudo syn-crypt change-key           replace the passphrase"
    echo "    sudo syn-crypt add-key              add a second one"
    echo "    sudo syn-crypt backup-header FILE   save the LUKS header"
    echo ""
    echo "  $(bold 'Back up the header to another machine.') A damaged LUKS header"
    echo "  means the data is unrecoverable even with the right passphrase."
    echo ""
fi

# Last thing before the reboot, so a misspelled key is on screen next to the
# summary rather than scrolled off behind twenty minutes of pacstrap.
config_report_unused

prompt "Remove installation media and press ENTER to reboot..."
answer press_enter_reboot _ack --ack
umount -R /mnt 2>/dev/null || true
reboot
