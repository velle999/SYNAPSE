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

# Clean up any previous failed install attempt
umount -R /mnt 2>/dev/null || true

# ── Detect boot mode ──────────────────────────────────────
if [ -d /sys/firmware/efi/efivars ]; then
    BOOT_MODE="uefi"
    success "Boot mode: UEFI"
else
    BOOT_MODE="bios"
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

    if [[ "$DISK" == *"nvme"* ]]; then
        PART_EFI="${DISK}p1"
        PART_ROOT="${DISK}p2"
    else
        PART_EFI="${DISK}1"
        PART_ROOT="${DISK}2"
    fi

    partprobe "$DISK" 2>/dev/null || true
    sleep 2

    echo "  Formatting EFI partition..."
    mkfs.fat -F32 "$PART_EFI" || die "Failed to format EFI partition"
    echo "  Formatting root partition..."
    mkfs.ext4 -F "$PART_ROOT" || die "Failed to format root partition"

    echo "  Mounting..."
    mount "$PART_ROOT" /mnt || die "Failed to mount root"
    mkdir -p /mnt/boot/efi
    mount "$PART_EFI" /mnt/boot/efi || die "Failed to mount EFI"
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

    partprobe "$DISK" 2>/dev/null || true
    sleep 2

    echo "  Formatting root partition..."
    mkfs.ext4 -F "$PART_ROOT" || die "Failed to format root partition"
    echo "  Mounting..."
    mount "$PART_ROOT" /mnt || die "Failed to mount root"
fi

success "Disk partitioned and mounted at /mnt"

# ── Install base system ───────────────────────────────────
header
step "Step 3/6 — Installing Base System"

# Fix mirrorlist on live system (single quotes preserve $repo/$arch literals
# which pacman expands itself — do NOT use a variable here)
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
pacstrap /mnt \
    base linux linux-firmware \
    grub efibootmgr \
    networkmanager openssh sudo \
    seatd \
    mkinitcpio \
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
step "Step 4/6 — Installing SynapseOS"

# Copy local-repo into chroot and install via pacman
LIVE_REPO="/run/archiso/airootfs/local-repo"
CHROOT_REPO="/mnt/var/cache/synapseos"

mkdir -p "$CHROOT_REPO"
cp "$LIVE_REPO"/*.pkg.tar.zst "$CHROOT_REPO/" 2>/dev/null || true

# Build repo db inside chroot
arch-chroot /mnt bash -c "
    cd /var/cache/synapseos && \
    repo-add synapseos.db.tar.gz *.pkg.tar.zst 2>/dev/null && \
    ln -sf synapseos.db.tar.gz synapseos.db && \
    ln -sf synapseos.files.tar.gz synapseos.files
"

# Add synapseos repo to chroot pacman.conf
cat >> /mnt/etc/pacman.conf << REPOEOF

[synapseos]
SigLevel = Optional TrustAll
Server = file:///var/cache/synapseos
REPOEOF

# Install all SynapseOS packages
arch-chroot /mnt pacman -Sy --noconfirm \
    synapd synsh synnet synguard synui synapse_kmod \
    syn syn-model syn-firstboot \
    2>&1 || warn "Some SynapseOS packages failed to install"

# Copy llama.cpp shared libraries from live ISO to installed system
echo "  Copying AI runtime libraries..."
for lib in /run/archiso/airootfs/usr/lib/libllama* \
           /run/archiso/airootfs/usr/lib/libggml*; do
    [ -f "$lib" ] && cp "$lib" /mnt/usr/lib/ && echo "    $(basename $lib)"
done
arch-chroot /mnt ldconfig

success "SynapseOS packages installed"

# ── Configure system ──────────────────────────────────────
header
step "Step 5/6 — Configuring System"

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

# locale
echo "en_US.UTF-8 UTF-8" >> /mnt/etc/locale.gen
arch-chroot /mnt locale-gen 2>/dev/null
echo "LANG=en_US.UTF-8" > /mnt/etc/locale.conf
echo "  Locale configured"

# timezone
arch-chroot /mnt ln -sf /usr/share/zoneinfo/UTC /etc/localtime 2>/dev/null || true
arch-chroot /mnt hwclock --systohc 2>/dev/null || true
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

# Copy SynapseOS service files from live ISO
for f in \
    /etc/systemd/system/synapd.service \
    /etc/systemd/system/synnet.service \
    /etc/systemd/system/synguard.service \
    /etc/synguard/rules.d/; do
    [ -e "$f" ] && cp -r "$f" "/mnt$f" 2>/dev/null || true
done

# Enable services
arch-chroot /mnt systemctl enable \
    NetworkManager synapd synnet synguard seatd vboxservice \
    2>/dev/null || true
echo "  Services enabled"

# Auto-login to synsh on tty1
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

# tmpfiles for synapd runtime dirs
cat > /mnt/etc/tmpfiles.d/synapd.conf << 'EOF'
d /run/synapd 0755 root root -
d /var/lib/synapd 0755 root root -
d /var/lib/synapd/models 0755 root root -
EOF

echo "  Generating initramfs..."
arch-chroot /mnt mkinitcpio -P 2>&1 | tail -5 || warn "mkinitcpio had errors"

success "System configured"

# ── Bootloader ────────────────────────────────────────────
header
step "Step 6/6 — Installing Bootloader"

# Write grub defaults BEFORE grub-mkconfig runs
cat > /mnt/etc/default/grub << 'EOF'
GRUB_DEFAULT=0
GRUB_TIMEOUT=5
GRUB_DISTRIBUTOR="SynapseOS"
GRUB_CMDLINE_LINUX_DEFAULT="quiet"
GRUB_CMDLINE_LINUX=""
GRUB_DISABLE_OS_PROBER=true
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
echo "  On first boot, syn-firstboot will guide you through"
echo "  downloading an AI model and configuring your system."
echo ""

prompt "Remove installation media and press ENTER to reboot..."
read -r
umount -R /mnt 2>/dev/null || true
reboot
