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
    sleep 3
    if have_net; then
        success "Network connected"
    else
        die "No network connection. SynapseOS install downloads the base
  system, so a connection is required. Connect (e.g. 'nmtui') and re-run."
    fi
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
step "Step 2 — Partitioning $DISK"

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
pacstrap /mnt \
    base linux linux-firmware linux-headers foot \
    grub efibootmgr \
    networkmanager openssh sudo \
    seatd ttf-dejavu \
    mkinitcpio dkms \
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

arch-chroot /mnt pacman -Sy --noconfirm \
    synapd synsh synnet synguard synui synapse_kmod \
    syn syn-model syn-firstboot \
    2>&1 || warn "Some SynapseOS packages failed to install"

# Copy llama.cpp shared libraries from live ISO
echo "  Copying AI runtime libraries..."
for lib in /run/archiso/airootfs/usr/lib/libllama* \
           /run/archiso/airootfs/usr/lib/libggml*; do
    [ -f "$lib" ] && cp "$lib" /mnt/usr/lib/ && echo "    $(basename "$lib")"
done
arch-chroot /mnt ldconfig

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
    *) success "SynapseUI selected (included)" ;;
esac

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

# locale
echo "en_US.UTF-8 UTF-8" >> /mnt/etc/locale.gen
arch-chroot /mnt locale-gen 2>/dev/null
echo "LANG=en_US.UTF-8" > /mnt/etc/locale.conf
echo "  Locale: en_US.UTF-8"

# timezone
arch-chroot /mnt ln -sf /usr/share/zoneinfo/UTC /etc/localtime 2>/dev/null || true
arch-chroot /mnt hwclock --systohc 2>/dev/null || true
echo "  Timezone: UTC"

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
for f in \
    /etc/systemd/system/synapd.service \
    /etc/systemd/system/synnet.service \
    /etc/systemd/system/synguard.service \
    /etc/systemd/system/synui.service \
    /etc/systemd/system/synui-foot.service \
    /etc/systemd/system/synapse-kmod-build.service \
    /etc/synguard/rules.d/; do
    [ -e "$f" ] && cp -r "$f" "/mnt$f" 2>/dev/null || true
done
[ -f /usr/bin/synapse-kmod-build ] && \
    cp /usr/bin/synapse-kmod-build /mnt/usr/bin/synapse-kmod-build 2>/dev/null || true

# ── Fix synguard: don't hard-require synapd ──────────────
# synguard handles missing synapd gracefully in code, but Requires=
# in the unit file kills it before it gets the chance.
if [ -f /mnt/etc/systemd/system/synguard.service ]; then
    sed -i 's/^Requires=synapd.service/Wants=synapd.service/' \
        /mnt/etc/systemd/system/synguard.service
    sed -i '/^PartOf=synapd.service/d' \
        /mnt/etc/systemd/system/synguard.service
fi

# ── Create user and groups ────────────────────────────────
echo "  Creating user '$NEW_USER'..."

arch-chroot /mnt bash -c "
    groupadd -r synapse  2>/dev/null || true
    groupadd -r synguard 2>/dev/null || true
    groupadd -r seat     2>/dev/null || true

    grep -qxF '/usr/bin/synsh' /etc/shells 2>/dev/null || echo '/usr/bin/synsh' >> /etc/shells

    useradd -m -G wheel,audio,video,input,synapse,seat \
        -s /bin/bash -c '$NEW_FULLNAME' '$NEW_USER' 2>/dev/null || true

    echo '%wheel ALL=(ALL:ALL) ALL' > /etc/sudoers.d/wheel
"

# Set password directly — pipe to chpasswd outside bash -c to avoid
# quoting/escaping issues and ensure errors are visible
printf '%s:%s' "$NEW_USER" "$NEW_PASS" | arch-chroot /mnt chpasswd \
    || { fail "Failed to set password"; }
success "Password set for '$NEW_USER'"

USER_UID=$(arch-chroot /mnt id -u "$NEW_USER" 2>/dev/null || echo 1000)
echo "  User '$NEW_USER' created (uid=$USER_UID)"

# ── Enable services ──────────────────────────────────────
arch-chroot /mnt systemctl enable NetworkManager seatd 2>/dev/null || true
arch-chroot /mnt systemctl enable synapd synnet synguard 2>/dev/null || true
arch-chroot /mnt systemctl enable synapse-kmod-build 2>/dev/null || true
arch-chroot /mnt systemctl enable vboxservice 2>/dev/null || true

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

[Autologin]
Session=plasma
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

        # synui.service override — run as user, not root
        mkdir -p /mnt/etc/systemd/system/synui.service.d
        cat > /mnt/etc/systemd/system/synui.service.d/user.conf << SYNUI_EOF
[Unit]
Conflicts=getty@tty1.service

[Service]
User=$NEW_USER
Environment=XDG_RUNTIME_DIR=/run/user/$USER_UID
ExecStartPre=
ExecStartPre=+/bin/mkdir -p /run/user/$USER_UID
ExecStartPre=+/bin/chown $NEW_USER:$NEW_USER /run/user/$USER_UID
ExecStartPre=+/bin/sh -c 'if [ -f /sys/class/dmi/id/sys_vendor ] && grep -qiE "VirtualBox|VMware|QEMU|KVM|Xen|innotek" /sys/class/dmi/id/sys_vendor; then mkdir -p /run/synui; printf "WLR_RENDERER=pixman\nWLR_BACKENDS=drm,libinput\nWLR_NO_HARDWARE_CURSORS=1\n" > /run/synui/env; else mkdir -p /run/synui; : > /run/synui/env; fi'
SYNUI_EOF

        # synui-foot override — run terminal as user
        mkdir -p /mnt/etc/systemd/system/synui-foot.service.d
        cat > /mnt/etc/systemd/system/synui-foot.service.d/user.conf << FOOT_EOF
[Service]
User=$NEW_USER
Environment=XDG_RUNTIME_DIR=/run/user/$USER_UID
FOOT_EOF

        arch-chroot /mnt systemctl enable synui.service 2>/dev/null || true
        echo "  Desktop: SynapseUI (as $NEW_USER)"
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
            export WLR_RENDERER=pixman
            export WLR_BACKENDS=drm,libinput
            export WLR_NO_HARDWARE_CURSORS=1
        fi
        export XDG_SESSION_TYPE=wayland
        export LIBSEAT_BACKEND=seatd
        exec synui
    fi
fi
PROFILEEOF

# synui config
mkdir -p "/mnt/home/$NEW_USER/.config/synui"
cat > "/mnt/home/$NEW_USER/.config/synui/synuirc" << 'SYNUIRC'
terminal = foot
autostart = foot
border_width    = 2
gap             = 8
master_factor   = 0.60
ai_layout       = on
ai_ctx_decor    = on
start_overlay   = off
workspace_1_intent = main workspace
workspace_2_intent = web browsing and research
workspace_3_intent = writing code and running tests
workspace_4_intent = terminal and system administration
workspace_5_intent = media and entertainment
SYNUIRC

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

echo "  Generating initramfs..."
arch-chroot /mnt mkinitcpio -P 2>&1 | tail -5 || warn "mkinitcpio had errors"

success "System configured"

# ── Bootloader ────────────────────────────────────────────
header
step "Installing Bootloader"

cat > /mnt/etc/default/grub << 'EOF'
GRUB_DEFAULT=0
GRUB_TIMEOUT=5
GRUB_DISTRIBUTOR="SynapseOS"
GRUB_CMDLINE_LINUX_DEFAULT=""
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
DE_NAME="SynapseUI"
case "$DE_CHOICE" in
    2) DE_NAME="KDE Plasma" ;;
    3) DE_NAME="GNOME" ;;
    4) DE_NAME="TTY only" ;;
esac
echo "  $(bold 'Disk:')     $DISK"
echo "  $(bold 'Boot:')     $BOOT_MODE"
echo "  $(bold 'Desktop:')  $DE_NAME"
echo "  $(bold 'User:')     $NEW_USER"
echo "  $(bold 'Hostname:') synapse"
echo ""
line
echo ""
echo "  Log in as '$(bold "$NEW_USER")' after reboot."
echo "  Type '$(bold "syn ask anything")' to get started."
echo ""

prompt "Remove installation media and press ENTER to reboot..."
read -r
umount -R /mnt 2>/dev/null || true
reboot
