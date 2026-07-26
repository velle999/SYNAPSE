#!/usr/bin/env bash
# syn-install — SynapseOS disk installer
set -uo pipefail

VERSION="0.1.0-synapse"
COLS=$(tput cols 2>/dev/null || echo 80)

# ── UI helpers ────────────────────────────────────────────
cyan()  { printf '\033[1;36m%s\033[0m' "$*"; }
green() { printf '\033[1;32m%s\033[0m' "$*"; }
red()   { printf '\033[1;31m%s\033[0m' "$*"; }
yellow(){ printf '\033[1;33m%s\033[0m' "$*"; }
bold()  { printf '\033[1m%s\033[0m' "$*"; }
line()  { printf '%*s\n' "$COLS" '' | tr ' ' '─'; }

header() {
    clear
    cyan "
  ███████╗██╗   ██╗███╗   ██╗
  ██╔════╝╚██╗ ██╔╝████╗  ██║
  ███████╗ ╚████╔╝ ██╔██╗ ██║
  ╚════██║  ╚██╔╝  ██║╚██╗██║
  ███████║   ██║   ██║ ╚████║
  ╚══════╝   ╚═╝   ╚═╝  ╚═══╝
"
    echo "  $(bold "SynapseOS Installer $VERSION")"
    line
    echo ""
}

step()    { echo ""; cyan "  ▶ $*"; echo ""; }
success() { echo ""; green "  ✓ $*"; echo ""; }
fail()    { echo ""; red "  ✗ $*"; echo ""; }
warn()    { echo ""; yellow "  ⚠ $*"; echo ""; }
prompt()  { printf "  $(bold "$1") "; }

# Unmount the target on failure so a stale /mnt doesn't block a retry.
cleanup() { umount -R /mnt 2>/dev/null || true; }
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
[ "$(id -u)" = "0" ] || die "syn-install must be run as root"

# Best-effort unmount of the target area on any unexpected exit.
trap cleanup EXIT

# ── Welcome ───────────────────────────────────────────────
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
read -r

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
            read -r wifi_ans
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
read -r DISK
DISK="/dev/$DISK"

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
    echo ""
    prompt "Install mode [1/2]:"
    read -r _mode
    case "${_mode:-1}" in
        1) INSTALL_MODE="alongside" ;;
        *) INSTALL_MODE="erase" ;;
    esac
elif [ "$BOOT_MODE" = "uefi" ] && [ "$NUM_PARTS" -gt 0 ] && [ -n "$ESP_DEV" ]; then
    # Another OS is here, but not enough room to sit beside it.
    warn "This disk holds another OS but only $((FREE_MIB / 1024)) GiB is free —
  under the $((MIN_ROOT_MIB / 1024)) GiB SynapseOS needs. To dual-boot, shrink the
  existing OS first (Windows: Disk Management -> Shrink Volume), then re-run.
  Continuing now would ERASE the whole disk."
fi

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
if [ "$INSTALL_MODE" = "erase" ]; then
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
    read -r _enc
    case "${_enc,,}" in
        y|yes) ENCRYPT="yes" ;;
        *)     ENCRYPT="no" ;;
    esac
fi

if [ "$ENCRYPT" = "yes" ]; then
    command -v cryptsetup >/dev/null \
        || die "cryptsetup is not available on this installer image"

    # Read the passphrase twice, never echoed, and keep asking rather than
    # aborting a half-hour install over a typo. Empty is refused outright: an
    # empty LUKS passphrase is accepted by cryptsetup and would encrypt the
    # disk to nothing.
    while :; do
        prompt "Encryption passphrase:"
        read -rs LUKS_PASS; echo ""
        prompt "Repeat passphrase:"
        read -rs LUKS_PASS2; echo ""
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
            read -r _short
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
    read -r confirm
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

    echo "  Formatting new root ($PART_ROOT)..."
    mkfs.ext4 -F "$PART_ROOT" || die "Failed to format root partition"

    echo "  Mounting..."
    mount "$PART_ROOT" /mnt || die "Failed to mount root"
    mkdir -p /mnt/boot/efi
    # REUSE, never mkfs: this ESP carries Windows' bootloader.
    mount "$PART_EFI" /mnt/boot/efi || die "Failed to mount the existing ESP"

elif [ "$BOOT_MODE" = "uefi" ]; then
    warn "This will ERASE all data on $DISK"
    prompt "Type 'yes' to confirm:"
    read -r confirm
    [ "$confirm" = "yes" ] || die "Aborted"

    echo "  Creating GPT partition table..."
    parted -s "$DISK" mklabel gpt
    parted -s "$DISK" mkpart ESP fat32 1MiB 513MiB
    parted -s "$DISK" set 1 esp on
    if [ "$ENCRYPT" = "yes" ]; then
        # Encrypted: ESP, a plain /boot for the kernel + initramfs, then LUKS.
        parted -s "$DISK" mkpart boot ext4 513MiB 1537MiB
        parted -s "$DISK" mkpart root ext4 1537MiB 100%
        PART_EFI="$(part_name "$DISK" 1)"
        PART_BOOT="$(part_name "$DISK" 2)"
        PART_ROOT="$(part_name "$DISK" 3)"
    else
        parted -s "$DISK" mkpart root ext4 513MiB 100%
        PART_EFI="$(part_name "$DISK" 1)"
        PART_ROOT="$(part_name "$DISK" 2)"
    fi

    partprobe "$DISK" 2>/dev/null || true
    sleep 2

    echo "  Formatting EFI partition..."
    mkfs.fat -F32 "$PART_EFI" || die "Failed to format EFI partition"
    luks_format_root            # no-op unless encrypting; sets ROOT_FS_DEV
    echo "  Formatting root partition..."
    mkfs.ext4 -F "$ROOT_FS_DEV" || die "Failed to format root partition"

    echo "  Mounting..."
    mount "$ROOT_FS_DEV" /mnt || die "Failed to mount root"
    if [ "$ENCRYPT" = "yes" ]; then
        echo "  Formatting /boot partition..."
        mkfs.ext4 -F "$PART_BOOT" || die "Failed to format boot partition"
        mkdir -p /mnt/boot
        mount "$PART_BOOT" /mnt/boot || die "Failed to mount /boot"
    fi
    mkdir -p /mnt/boot/efi
    mount "$PART_EFI" /mnt/boot/efi || die "Failed to mount EFI"
else
    # BIOS/MBR whole-disk. Alongside is UEFI-only, so BIOS is always an erase.
    warn "This will ERASE all data on $DISK"
    prompt "Type 'yes' to confirm:"
    read -r confirm
    [ "$confirm" = "yes" ] || die "Aborted"

    echo "  Creating MBR partition table..."
    parted -s "$DISK" mklabel msdos
    if [ "$ENCRYPT" = "yes" ]; then
        # Same split as UEFI, minus the ESP: GRUB's core reads a plain /boot.
        parted -s "$DISK" mkpart primary ext4 1MiB 1025MiB
        parted -s "$DISK" set 1 boot on
        parted -s "$DISK" mkpart primary ext4 1025MiB 100%
        PART_BOOT="$(part_name "$DISK" 1)"
        PART_ROOT="$(part_name "$DISK" 2)"
    else
        parted -s "$DISK" mkpart primary ext4 1MiB 100%
        parted -s "$DISK" set 1 boot on
        PART_ROOT="$(part_name "$DISK" 1)"
    fi

    partprobe "$DISK" 2>/dev/null || true
    sleep 2

    luks_format_root            # no-op unless encrypting; sets ROOT_FS_DEV
    echo "  Formatting root partition..."
    mkfs.ext4 -F "$ROOT_FS_DEV" || die "Failed to format root partition"
    echo "  Mounting..."
    mount "$ROOT_FS_DEV" /mnt || die "Failed to mount root"
    if [ "$ENCRYPT" = "yes" ]; then
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
# dolphin is the file manager; it drags in Qt6 + KF6, so it is much the largest
# thing here. synui ships the /etc/xdg files that make KDE work outside Plasma
# (applications.menu, kdeglobals) — see its PKGBUILD.
# wine (+ wine-mono for .NET installers) makes .exe/.msi runnable out of the
# box: wine.desktop drives double-click and synui's "Run with Wine" right-click
# service menu drives the context menu. Matches the dev machine's local setup.
pacstrap /mnt \
    base linux linux-firmware linux-headers foot \
    grub efibootmgr os-prober ntfs-3g \
    networkmanager openssh sudo \
    seatd ttf-dejavu \
    xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk slurp \
    rtkit polkit-gnome xorg-xhost \
    mkinitcpio dkms dolphin \
    wine wine-mono \
    cryptsetup \
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
step "Step 4 — Installing SynapseOS"

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
arch-chroot /mnt pacman -Sy --noconfirm \
    synapd synsh synnet synguard synui synapse_kmod \
    syn syn-model syn-firstboot \
    chibi nexus-chat tepris vibe \
    samsung-m2020 shelly-bin \
    2>&1 || warn "Some SynapseOS packages failed to install — verifying below"

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
SYN_CORE="synapd synsh synnet synguard synui synapse_kmod syn syn-model syn-firstboot"
# Apps, not the OS. They ship in the live repo but were never installed to disk,
# so an installed SynapseOS had no chibi at all — you only got her on the ISO.
SYN_EXTRA="chibi nexus-chat tepris vibe samsung-m2020 shelly-bin"

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

success "SynapseOS packages verified: $(echo $SYN_CORE | wc -w) core, \
$(( $(echo $SYN_EXTRA | wc -w) - $(echo $missing_extra | wc -w) ))/$(echo $SYN_EXTRA | wc -w) apps"

# Copy AI model if present on live ISO
MODEL_SRC=""
for f in /run/archiso/airootfs/var/lib/synapd/models/*.gguf \
         /var/lib/synapd/models/*.gguf; do
    [ -f "$f" ] && MODEL_SRC="$f" && break
done
if [ -n "$MODEL_SRC" ]; then
    echo "  Copying AI model ($(du -sh "$MODEL_SRC" | cut -f1))..."
    mkdir -p /mnt/var/lib/synapd/models
    cp "$MODEL_SRC" /mnt/var/lib/synapd/models/synapse.gguf
    success "AI model installed"
else
    warn "No AI model on live ISO — download later with: syn model download"
fi

success "SynapseOS packages installed"

# ── User account ─────────────────────────────────────────
header
step "Step 5 — Create User Account"

echo "  Create a user account for the installed system."
echo ""
prompt "Username [default: syn]:"
read -r NEW_USER || true
NEW_USER="${NEW_USER:-syn}"

prompt "Full name (optional):"
read -r NEW_FULLNAME || true

while true; do
    prompt "Password:"
    read -rs NEW_PASS; echo
    prompt "Confirm password:"
    read -rs NEW_PASS2; echo
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
echo "    $(bold '1)') SynapseUI  — AI-native Wayland compositor  (default)"
echo "    $(bold '2)') KDE Plasma — Full-featured Wayland desktop"
echo "    $(bold '3)') GNOME      — Clean, modern Wayland desktop"
echo "    $(bold '4)') TTY only   — No GUI (headless/server)"
echo ""
prompt "Choice [1-4, default=1]:"
read -r de_choice || true
DE_CHOICE="${de_choice:-1}"

case "$DE_CHOICE" in
    2)
        echo "  Installing KDE Plasma..."
        arch-chroot /mnt pacman -S --noconfirm \
            plasma-meta sddm kde-applications-meta \
            2>&1 || warn "Some KDE packages failed to install"
        success "KDE Plasma installed"
        ;;
    3)
        echo "  Installing GNOME..."
        arch-chroot /mnt pacman -S --noconfirm \
            gnome gdm \
            2>&1 || warn "Some GNOME packages failed to install"
        success "GNOME installed"
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
        arch-chroot /mnt pacman -S --noconfirm \
            greetd greetd-tuigreet quickshell swaybg python wtype \
            bluez bluez-utils \
            cups cups-pdf ghostscript nss-mdns \
            2>&1 || warn "greetd failed to install — boot falls back to getty login"
        success "SynapseUI selected (included)"
        ;;
esac

# ── Video driver ──────────────────────────────────────────
header
step "Configuring Video Driver"

# synui needs a working EGL/Vulkan stack. On modern NVIDIA cards nouveau
# provides neither, so the renderer autocreate fails and the session dies
# — the driver has to be installed here, before mkinitcpio and grub run.
# Detect display controllers from sysfs; the live ISO may not ship lspci.
GPU_KERNEL_PARAMS=""
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
    arch-chroot /mnt pacman -S --noconfirm --needed mesa 2>&1 \
        || warn "mesa failed to install"
elif [ -n "$HAS_NVIDIA" ]; then
    # nvidia-open supports Turing and newer (PCI device id >= 0x1e00);
    # older cards need the proprietary kernel module.
    NVIDIA_PKG="nvidia-dkms"
    [ "$NVIDIA_DEVID" -ge $((0x1e00)) ] && NVIDIA_PKG="nvidia-open-dkms"
    echo "  NVIDIA GPU detected — installing $NVIDIA_PKG (builds the module, takes a while)..."
    arch-chroot /mnt pacman -S --noconfirm --needed \
        "$NVIDIA_PKG" nvidia-utils egl-wayland mesa 2>&1 \
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
        read -r gpu_ans
        case "${gpu_ans:-y}" in
            [Nn]*)
                warn "Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda" ;;
            *)
                echo "  Installing synapse-llama-cuda (this takes a while)..."
                if arch-chroot /mnt pacman -S --noconfirm synapse-llama-cuda 2>&1; then
                    # Verify rather than trust: this is the exact spot where a
                    # silent CPU fallback would be indistinguishable from success.
                    if arch-chroot /mnt sh -c '[ -e /usr/lib/libggml-cuda.so ]'; then
                        success "GPU inference enabled (synapse-llama-cuda)"
                    else
                        warn "synapse-llama-cuda installed but libggml-cuda.so is missing —
  synapd will run on the CPU. Report this."
                    fi
                else
                    warn "Could not install synapse-llama-cuda — synapd will run on the
  CPU. Retry later with: sudo pacman -S synapse-llama-cuda"
                fi ;;
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
    echo "  Installing video stack: $GPU_PKGS..."
    arch-chroot /mnt pacman -S --noconfirm --needed $GPU_PKGS 2>&1 \
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
            if arch-chroot /mnt pacman -S --noconfirm synapse-llama-vulkan 2>&1; then
                if arch-chroot /mnt sh -c '[ -e /usr/lib/libggml-vulkan.so ]'; then
                    success "GPU inference enabled (synapse-llama-vulkan)"
                else
                    warn "synapse-llama-vulkan installed but libggml-vulkan.so is missing —
  synapd will run on the CPU. Report this."
                fi
            else
                warn "Could not install synapse-llama-vulkan — synapd will run on the
  CPU. Retry later with: sudo pacman -S synapse-llama-vulkan"
            fi
        else
            warn "This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"
        fi
    fi
fi

# ── Configure system ──────────────────────────────────────
header
step "Configuring System"

# fstab
genfstab -U /mnt >> /mnt/etc/fstab
echo "  fstab generated"

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
# LOCALE_ROWS is deliberately short. A picker listing all 500 of glibc's locales
# is not more international, it is unusable — these cover the large majority,
# and Other takes any locale glibc has.
#
# Each row: label|locale|keymap|font-package(s). The font column is the
# "language pack" part: noto-fonts covers Latin/Greek/Cyrillic and ships as a
# base, CJK needs noto-fonts-cjk (~130MB, which is why it is not simply always
# installed), and noto-fonts-extra carries the Indic/Arabic/Hebrew coverage.
#
# THE LABELS ARE ASCII ON PURPOSE. This menu is drawn on the Linux VT, whose
# font is ter-116n — the Latin-1 Terminus. Cyrillic came out as lookalike
# rubbish and every CJK/Devanagari/Arabic name came out as a row of boxes, so
# the entries that most needed to be readable were the only unreadable ones.
# A Unicode console font does not fix it either: a VT font holds at most 512
# glyphs, which cannot cover CJK by two orders of magnitude. The native name is
# a nice touch this screen cannot cash — the locale code carries the meaning.
LOCALE_ROWS="
English (US)|en_US.UTF-8|us|
English (UK)|en_GB.UTF-8|uk|
Deutsch|de_DE.UTF-8|de|
Français|fr_FR.UTF-8|fr|
Español|es_ES.UTF-8|es|
Português (Brasil)|pt_BR.UTF-8|br|
Italiano|it_IT.UTF-8|it|
Nederlands|nl_NL.UTF-8|nl|
Polski|pl_PL.UTF-8|pl|
Russian|ru_RU.UTF-8|ru|
Japanese|ja_JP.UTF-8|jp106|noto-fonts-cjk
Chinese (Simplified)|zh_CN.UTF-8|us|noto-fonts-cjk
Korean|ko_KR.UTF-8|kr|noto-fonts-cjk
Hindi|hi_IN.UTF-8|us|noto-fonts-extra
Arabic|ar_EG.UTF-8|us|noto-fonts-extra
"

header
step "Step 6 — Language & Region"

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
read -r lang_choice
lang_choice="${lang_choice:-1}"

LOCALE="en_US.UTF-8"; KEYMAP="us"; LANG_FONTS=""
if [ "$lang_choice" = "0" ]; then
    prompt "Locale (e.g. sv_SE.UTF-8):"; read -r LOCALE
    prompt "Console keymap (e.g. sv):"; read -r KEYMAP
    LOCALE="${LOCALE:-en_US.UTF-8}"; KEYMAP="${KEYMAP:-us}"
    # No idea what script that is, so cover as much as possible rather than
    # hand someone a system that cannot draw their own alphabet.
    LANG_FONTS="noto-fonts-extra"
else
    row=$(echo "$LOCALE_ROWS" | sed -n "$((lang_choice + 1))p")
    if [ -n "$row" ]; then
        LOCALE=$(echo "$row" | cut -d'|' -f2)
        KEYMAP=$(echo "$row" | cut -d'|' -f3)
        LANG_FONTS=$(echo "$row" | cut -d'|' -f4)
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
echo "KEYMAP=$KEYMAP" > /mnt/etc/vconsole.conf
success "Locale: $LOCALE   Keymap: $KEYMAP"

# synui reads its own layout from synuirc — the console keymap does not reach
# Wayland, so without this the desktop stays on a US layout no matter what was
# picked here. Written later with the rest of synuirc; recorded now.
SYNUI_XKB="$KEYMAP"

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
# the mapping is visible rather than magic.
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
step "Step 7 — Timezone"
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
    read -r tz_input
    tz_input="${tz_input:-UTC}"

    case "$tz_input" in
        0)
            prompt "tzdata name (Region/City):"
            read -r tz_input
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

# Branded fastfetch logo, if shipped on the live ISO.
if [ -f /usr/share/synapseos/logo.txt ]; then
    mkdir -p /mnt/usr/share/synapseos
    cp /usr/share/synapseos/logo.txt /mnt/usr/share/synapseos/logo.txt
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

arch-chroot /mnt bash -c "
    groupadd -r synapse  2>/dev/null || true
    groupadd -r synguard 2>/dev/null || true
    groupadd -r seat     2>/dev/null || true

    grep -qxF '/usr/bin/synsh' /etc/shells 2>/dev/null || echo '/usr/bin/synsh' >> /etc/shells

    echo '%wheel ALL=(ALL:ALL) ALL' > /etc/sudoers.d/wheel

    # The SYNAPSE start menu's Reboot/Shut Down entries run with no
    # terminal to type a password into, and the target has no polkit —
    # allow exactly these two commands passwordless.
    echo '%wheel ALL=(ALL:ALL) NOPASSWD: /usr/bin/systemctl reboot, /usr/bin/systemctl poweroff' \
        > /etc/sudoers.d/power-menu
    chmod 440 /etc/sudoers.d/power-menu

    # Game mode stops synapd to hand the GPU to the game (synui/src/game.c).
    # synapd is a system unit, so the session user's plain 'systemctl stop'
    # is refused by polkit — and synui spawns it fire-and-forget, so the
    # refusal was silent: game mode said it had suspended synapd while
    # synapd carried on holding ~4GB of VRAM. sudo -n, scoped to these two.
    echo '%wheel ALL=(ALL:ALL) NOPASSWD: /usr/bin/systemctl stop synapd, /usr/bin/systemctl start synapd' \
        > /etc/sudoers.d/synapd-gamemode
    chmod 440 /etc/sudoers.d/synapd-gamemode

    # The 'AI backend' row (welcome menu / control panel) toggles synapd
    # between GPU/CPU/off via synui-ai-backend, which rewrites synapd's
    # systemd drop-in and restarts it. Under a greetd session synui runs as
    # the user, who cannot do that and has no polkit agent to prompt — so the
    # helper re-execs under sudo -n. Scope it to exactly this command.
    echo '%wheel ALL=(ALL:ALL) NOPASSWD: /usr/bin/synui-ai-backend gpu, /usr/bin/synui-ai-backend cpu, /usr/bin/synui-ai-backend off, /usr/bin/synui-ai-backend toggle' \
        > /etc/sudoers.d/synapd-backend
    chmod 440 /etc/sudoers.d/synapd-backend
"

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

# ── Enable services ──────────────────────────────────────
arch-chroot /mnt systemctl enable NetworkManager seatd 2>/dev/null || true
# Bluetooth: bluez ships the unit but enables nothing. Without this the radio
# stays down and synui's panel (Super+B) correctly reports no adapter.
arch-chroot /mnt systemctl enable bluetooth 2>/dev/null || true
# Printing. cups.socket, not cups.service: socket activation means no daemon
# runs until something actually prints. avahi is what finds driverless
# (IPP Everywhere / AirPrint) printers, which is most printers made since ~2015.
arch-chroot /mnt systemctl enable cups.socket avahi-daemon 2>/dev/null || true
# cups.socket binds only the unix socket, so the web admin UI at localhost:631 —
# the whole printer story, opened from the start menu and control panel — can
# never connect. Append a loopback TCP listener (ListenStream is a list, so the
# unix socket stays); socket activation is preserved, cupsd still idles until
# the port or a print job is touched.
mkdir -p /mnt/etc/systemd/system/cups.socket.d
cat > /mnt/etc/systemd/system/cups.socket.d/tcp.conf << 'CUPSTCP_EOF'
[Socket]
ListenStream=127.0.0.1:631
ListenStream=[::1]:631
CUPSTCP_EOF
# The glibc half of mDNS. Without it cups discovers the printer and then cannot
# resolve its .local name, so discovery works and printing fails.
if [ -f /mnt/etc/nsswitch.conf ] && ! grep -q mdns_minimal /mnt/etc/nsswitch.conf; then
    sed -i '/^hosts:/s/ myhostname/ myhostname mdns_minimal [NOTFOUND=return]/' \
        /mnt/etc/nsswitch.conf
fi
arch-chroot /mnt systemctl enable synapd synnet synguard 2>/dev/null || true
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
if [ -f /sys/class/dmi/id/sys_vendor ] && \
   grep -qiE 'VirtualBox|VMware|QEMU|KVM|Xen|innotek' /sys/class/dmi/id/sys_vendor 2>/dev/null; then
    # fx_renderer is GLES2-only and ignores WLR_RENDERER, so these two are
    # what actually select llvmpipe; either alone still fails. See
    # archiso/airootfs/usr/local/bin/synui-gfx-env for the long version.
    export WLR_RENDERER_FORCE_SOFTWARE=1
    export WLR_RENDERER_ALLOW_SOFTWARE=1
    export WLR_RENDERER=pixman
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
else
    # nouveau has no usable EGL/Vulkan on modern cards — software-render
    # instead of dying at renderer autocreate.
    for drv in /sys/class/drm/card*/device/driver; do
        case "$(readlink "$drv" 2>/dev/null)" in
            *nouveau) export WLR_RENDERER_FORCE_SOFTWARE=1 \
                             WLR_RENDERER_ALLOW_SOFTWARE=1 \
                             WLR_RENDERER=pixman \
                             WLR_NO_HARDWARE_CURSORS=1 ;;
        esac
    done
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
    # fx_renderer is GLES2-only and ignores WLR_RENDERER, so these two are
    # what actually select llvmpipe; either alone still fails. See
    # archiso/airootfs/usr/local/bin/synui-gfx-env for the long version.
    export WLR_RENDERER_FORCE_SOFTWARE=1
    export WLR_RENDERER_ALLOW_SOFTWARE=1
    export WLR_RENDERER=pixman
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
else
    for drv in /sys/class/drm/card*/device/driver; do
        case "$(readlink "$drv" 2>/dev/null)" in
            *nouveau) export WLR_RENDERER_FORCE_SOFTWARE=1 \
                             WLR_RENDERER_ALLOW_SOFTWARE=1 \
                             WLR_RENDERER=pixman \
                             WLR_NO_HARDWARE_CURSORS=1 ;;
        esac
    done
fi
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=synui
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
            # See synui-gfx-env: fx_renderer ignores WLR_RENDERER, these two
            # are what actually select llvmpipe, and either alone fails.
            export WLR_RENDERER_FORCE_SOFTWARE=1
            export WLR_RENDERER_ALLOW_SOFTWARE=1
            export WLR_RENDERER=pixman
            export WLR_BACKENDS=drm,libinput
            export WLR_NO_HARDWARE_CURSORS=1
        else
            for drv in /sys/class/drm/card*/device/driver; do
                case "$(readlink "$drv" 2>/dev/null)" in
                    *nouveau) export WLR_RENDERER_FORCE_SOFTWARE=1 \
                                     WLR_RENDERER_ALLOW_SOFTWARE=1 \
                                     WLR_RENDERER=pixman \
                                     WLR_NO_HARDWARE_CURSORS=1 ;;
                esac
            done
        fi
        export XDG_SESSION_TYPE=wayland
        export XDG_CURRENT_DESKTOP=synui
        export LIBSEAT_BACKEND=seatd
        export XCURSOR_THEME=Adwaita
        export XCURSOR_SIZE=24
        # ...unless synui-cursor(1) / Super+Shift+P wrote a choice. See the
        # synui-session heredoc above; this path must stay in step with it.
        [ -r "$HOME/.config/synui/cursor.env" ] && . "$HOME/.config/synui/cursor.env"
        # Vulkan overlay layer; see the synui-session heredoc above.
        export MANGOHUD=1
        exec synui
    fi
fi
PROFILEEOF

# synui config
mkdir -p "/mnt/home/$NEW_USER/.config/synui"
cat > "/mnt/home/$NEW_USER/.config/synui/synuirc" << 'SYNUIRC'
terminal = foot
# greetd launches synui after login; the synsh terminal is autostarted
# here (synui-foot.service is only used on the live ISO).
# The bar is just the bar. The start menu it used to carry is synui's own
# panel (Super tap), which scans the installed .desktop files itself when it
# opens — so there is no generator to run before the bar starts. synui-bar
# starts quickshell against the QML tree synui packages to
# /usr/share/synui/quickshell.
autostart = synui-bar
autostart = foot synsh
# Any GUI app that needs root goes through polkit, and pkexec refuses to
# prompt on a terminal it doesn't have — without an authentication agent
# registered for the session it fails instantly and, launched from a menu,
# silently (gparted, for one). This is the agent that shows the dialog.
autostart = /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1
border_width    = 2
gap             = 8
master_factor   = 0.60

# The SYNAPSE wallpaper synui ships in /usr/share/synui. Drawn by the
# compositor itself (wallpaper.c), so nothing has to be autostarted to
# paint the desktop — a swaybg here would only cover it. Super+W picks a
# different one and records the choice in wallpaper.state, which from then
# on overrides this line. `wallpaper = none` gives a flat colour.
wallpaper       = default
wallpaper_mode  = fill

# "night drive" palette — matches foot.ini and waybar style.css
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

# foot terminal — "night drive" palette (matches synuirc border colors)
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
# (norduserd does; shelly-notifications does not). It costs nothing at install
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

# ── Mark firstboot done ──────────────────────────────────
mkdir -p /mnt/var/lib/synapseos
echo "firstboot completed by installer: $(date)" > /mnt/var/lib/synapseos/firstboot.done

# tmpfiles for synapd
cat > /mnt/etc/tmpfiles.d/synapd.conf << 'EOF'
d /run/synapd 0755 root root -
d /var/lib/synapd 0755 root root -
d /var/lib/synapd/models 0755 root root -
EOF

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
arch-chroot /mnt mkinitcpio -P 2>&1 | tail -5 \
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

# Hard-verify the encrypted boot path. Every one of these is a way to end up
# with an install that partitions, formats and reports success, then drops to a
# rescue shell on first boot — which is the worst possible time to find out.
if [ "$ENCRYPT" = "yes" ]; then
    echo "  Verifying the encrypted boot path..."

    cryptsetup isLuks "$PART_ROOT" \
        || die "$PART_ROOT is not a LUKS volume after install"

    grep -q "$LUKS_UUID" /mnt/boot/grub/grub.cfg \
        || die "grub.cfg never mentions $LUKS_UUID — it would not unlock at boot"

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

prompt "Remove installation media and press ENTER to reboot..."
read -r
umount -R /mnt 2>/dev/null || true
reboot
