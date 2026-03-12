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

die() { fail "$*"; exit 1; }

# ── Must be root ──────────────────────────────────────────
[ "$(id -u)" = "0" ] || die "syn-install must be run as root"

# ── Welcome ───────────────────────────────────────────────
header
echo "  This installer will:"
echo "    1. Partition a disk"
echo "    2. Install SynapseOS base system"
echo "    3. Configure bootloader (GRUB)"
echo "    4. Set up all SynapseOS services"
echo ""
warn "ALL DATA ON THE TARGET DISK WILL BE ERASED"
echo ""
prompt "Press ENTER to continue or Ctrl+C to abort..."
read -r

# ── Disk selection ────────────────────────────────────────
header
step "Step 1/6 — Select Target Disk"

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

echo ""
echo "  $(bold 'Target:') $DISK"
lsblk "$DISK"
echo ""
warn "This will ERASE all data on $DISK"
prompt "Type 'yes' to confirm:"
read -r confirm
[ "$confirm" = "yes" ] || die "Aborted"

# ── Detect boot mode ──────────────────────────────────────
if [ -d /sys/firmware/efi ]; then
    BOOT_MODE="uefi"
    echo ""
    success "Boot mode: UEFI"
else
    BOOT_MODE="bios"
    echo ""
    success "Boot mode: BIOS/Legacy"
fi

# ── Partition ─────────────────────────────────────────────
header
step "Step 2/6 — Partitioning $DISK"

if [ "$BOOT_MODE" = "uefi" ]; then
    echo "  Creating GPT partition table..."
    parted -s "$DISK" mklabel gpt
    parted -s "$DISK" mkpart ESP fat32 1MiB 513MiB
    parted -s "$DISK" set 1 esp on
    parted -s "$DISK" mkpart root ext4 513MiB 100%

    # Partition names
    if [[ "$DISK" == *"nvme"* ]]; then
        PART_EFI="${DISK}p1"
        PART_ROOT="${DISK}p2"
    else
        PART_EFI="${DISK}1"
        PART_ROOT="${DISK}2"
    fi

    echo "  Formatting EFI partition..."
    mkfs.fat -F32 "$PART_EFI"
    echo "  Formatting root partition..."
    mkfs.ext4 -F "$PART_ROOT"

    echo "  Mounting..."
    mount "$PART_ROOT" /mnt
    mkdir -p /mnt/boot/efi
    mount "$PART_EFI" /mnt/boot/efi
else
    echo "  Creating MBR partition table..."
    parted -s "$DISK" mklabel msdos
    parted -s "$DISK" mkpart primary ext4 1MiB 100%
    parted -s "$DISK" set 1 boot on

    if [[ "$DISK" == *"nvme"* ]]; then
        PART_ROOT="${DISK}p1"
    else
        PART_ROOT="${DISK}1"
    fi

    echo "  Formatting root partition..."
    mkfs.ext4 -F "$PART_ROOT"
    echo "  Mounting..."
    mount "$PART_ROOT" /mnt
fi

success "Disk partitioned and mounted at /mnt"

# ── Install base system ───────────────────────────────────
header
step "Step 3/6 — Installing Base System"

echo "  This may take several minutes..."
echo ""

# Fix mirrorlist
echo 'Server = https://geo.mirror.pkgbuild.com/\$repo/os/\$arch' > /etc/pacman.d/mirrorlist
pacman -Sy --noconfirm 2>/dev/null || true

# Fix mirrorlist on both live system and target
MIRROR='Server = https://geo.mirror.pkgbuild.com/\$repo/os/\$arch'
echo "\$MIRROR" > /etc/pacman.d/mirrorlist
mkdir -p /mnt/etc/pacman.d
echo "\$MIRROR" > /mnt/etc/pacman.d/mirrorlist
pacman -Sy --noconfirm 2>/dev/null || true

# Use pacstrap to install base system
pacstrap /mnt     base linux linux-firmware     grub efibootmgr     networkmanager openssh sudo     seatd     mkinitcpio 2>&1
echo "pacstrap exit code: $?"

success "Base system installed"

# ── Install SynapseOS packages ────────────────────────────
header
step "Step 4/6 — Installing SynapseOS"

# Copy local repo to target
mkdir -p /mnt/var/cache/synapseos-repo
cp /run/archiso/bootmnt/arch/pkgs/*.pkg.tar.zst /mnt/var/cache/synapseos-repo/ 2>/dev/null || true

# Find packages from the live ISO local repo
SYNAPSE_PKGS=(
    synapd synsh synnet synguard synui
    syn syn-model syn-firstboot
    mkinitcpio-archiso archiso
)

for pkg in "${SYNAPSE_PKGS[@]}"; do
    PKG_FILE=$(find /run/archiso/bootmnt /run/archiso/airootfs \
        -name "${pkg}-*.pkg.tar.zst" 2>/dev/null | head -1)
    if [ -n "$PKG_FILE" ]; then
        echo "  Installing $pkg..."
        cp "$PKG_FILE" /mnt/tmp/
        arch-chroot /mnt pacman -U "/tmp/$(basename "$PKG_FILE")" --noconfirm 2>/dev/null || true
    fi
done

success "SynapseOS packages installed"

# ── Configure system ──────────────────────────────────────
header
step "Step 5/6 — Configuring System"

# fstab
genfstab -U /mnt >> /mnt/etc/fstab
echo "  fstab generated"

# hostname
echo "synapse" > /mnt/etc/hostname
echo "  Hostname: synapse"

# locale
echo "en_US.UTF-8 UTF-8" >> /mnt/etc/locale.gen
arch-chroot /mnt locale-gen 2>/dev/null
echo "LANG=en_US.UTF-8" > /mnt/etc/locale.conf
echo "  Locale configured"

# timezone
arch-chroot /mnt ln -sf /usr/share/zoneinfo/UTC /etc/localtime 2>/dev/null || true
echo "  Timezone: UTC"

# os-release
cat > /mnt/etc/os-release << 'EOF'
NAME="SynapseOS"
PRETTY_NAME="SynapseOS 0.1.0"
ID=synapseos
ID_LIKE=arch
BUILD_ID=0.1.0
ANSI_COLOR="1;36"
HOME_URL="https://synapseos.dev"
EOF

# Copy SynapseOS configs from live ISO
for f in /etc/systemd/system/synapd.service \
          /etc/systemd/system/synnet.service \
          /etc/systemd/system/synguard.service \
          /etc/synguard/rules.d/; do
    [ -e "$f" ] && cp -r "$f" "/mnt$f" 2>/dev/null || true
done

# Enable services
arch-chroot /mnt systemctl enable NetworkManager synapd synnet synguard seatd 2>/dev/null || true
echo "  Services enabled"

# Auto-login to synsh
mkdir -p /mnt/etc/systemd/system/getty@tty1.service.d/
cat > /mnt/etc/systemd/system/getty@tty1.service.d/autologin.conf << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF

cat > /mnt/root/.bash_profile << 'EOF'
if [ "$(tty)" = "/dev/tty1" ]; then
    if [ ! -f /var/lib/synguard/.firstboot_done ]; then
        exec /usr/bin/syn-firstboot
    else
        exec /usr/bin/synsh
    fi
fi
EOF

# tmpfiles for synapd
cat > /mnt/etc/tmpfiles.d/synapd.conf << 'EOF'
d /run/synapd 0755 root root -
d /var/lib/synapd 0755 root root -
d /var/lib/synapd/models 0755 root root -
EOF

# mkinitcpio
arch-chroot /mnt mkinitcpio -P 2>&1 | tail -3 || true

success "System configured"

# ── Bootloader ────────────────────────────────────────────
header
step "Step 6/6 — Installing Bootloader"

if [ "$BOOT_MODE" = "uefi" ]; then
    arch-chroot /mnt grub-install \
        --target=x86_64-efi \
        --efi-directory=/boot/efi \
        --bootloader-id=SynapseOS 2>&1
else
    arch-chroot /mnt grub-install \
        --target=i386-pc "$DISK" 2>&1
fi

# SynapseOS grub config
cat > /mnt/etc/default/grub << 'EOF'
GRUB_DEFAULT=0
GRUB_TIMEOUT=5
GRUB_DISTRIBUTOR="SynapseOS"
GRUB_CMDLINE_LINUX_DEFAULT="quiet"
GRUB_CMDLINE_LINUX=""
EOF

arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -3

success "Bootloader installed"

# ── Done ──────────────────────────────────────────────────
header
green "  SynapseOS installation complete!"
echo ""
echo "  $(bold 'Disk:')     $DISK"
echo "  $(bold 'Boot:')     $BOOT_MODE"
echo "  $(bold 'Hostname:') synapse"
echo ""
line
echo ""
echo "  On first boot, the setup wizard will guide you through"
echo "  downloading an AI model and configuring your system."
echo ""
# Verify GRUB was installed
if [ ! -f /mnt/boot/grub/grub.cfg ]; then
    warn "GRUB config missing — reinstalling..."
    if [ "$BOOT_MODE" = "uefi" ]; then
        arch-chroot /mnt grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=SynapseOS --recheck 2>&1
    else
        arch-chroot /mnt grub-install --target=i386-pc "$DISK" --recheck 2>&1
    fi
    arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg 2>&1
fi

ls /mnt/boot/grub/grub.cfg && success "GRUB verified" || fail "GRUB still missing!"

prompt "Remove installation media and press ENTER to reboot..."
read -r
umount -R /mnt
reboot
