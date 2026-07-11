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
    xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk slurp \
    rtkit \
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
    *)
        echo "  Installing greetd (login screen) + desktop extras..."
        arch-chroot /mnt pacman -S --noconfirm \
            greetd greetd-tuigreet waybar swaybg python \
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
else
    GPU_PKGS="mesa"
    [ -n "$HAS_AMD" ]   && GPU_PKGS="$GPU_PKGS vulkan-radeon"
    [ -n "$HAS_INTEL" ] && GPU_PKGS="$GPU_PKGS vulkan-intel"
    echo "  Installing video stack: $GPU_PKGS..."
    arch-chroot /mnt pacman -S --noconfirm --needed $GPU_PKGS 2>&1 \
        || warn "Video driver install failed — synui may fall back to software rendering"
    success "Video drivers installed"
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
# chmod after copy: if the live media shipped the script non-executable
# (broken file_permissions in an old ISO), the copy inherits that and the
# boot service dies with 203/EXEC.
if [ -f /usr/bin/synapse-kmod-build ]; then
    cp /usr/bin/synapse-kmod-build /mnt/usr/bin/synapse-kmod-build 2>/dev/null \
        && chmod 755 /mnt/usr/bin/synapse-kmod-build
fi

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
arch-chroot /mnt systemctl enable synapd synnet synguard 2>/dev/null || true
arch-chroot /mnt systemctl enable synapse-kmod-build 2>/dev/null || true
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
    export WLR_RENDERER=pixman
    export WLR_BACKENDS=drm,libinput
    export WLR_NO_HARDWARE_CURSORS=1
else
    # nouveau has no usable EGL/Vulkan on modern cards — software-render
    # instead of dying at renderer autocreate.
    for drv in /sys/class/drm/card*/device/driver; do
        case "$(readlink "$drv" 2>/dev/null)" in
            *nouveau) export WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 ;;
        esac
    done
fi
export XDG_SESSION_TYPE=wayland
# Portal backend routing (synui-portals.conf); unset ⇒ no screen sharing.
export XDG_CURRENT_DESKTOP=synui
export LIBSEAT_BACKEND=seatd
# Pin cursor theme+size so Xwayland clients (Steam, games) match synui's own
# 24px cursor; without XCURSOR_SIZE libXcursor auto-picks a much larger size.
export XCURSOR_THEME=Adwaita
export XCURSOR_SIZE=24
exec synui
SESSION_EOF
        chmod 755 /mnt/usr/local/bin/synui-session

        # greetd: tuigreet login prompt on tty1, session = synui.
        # synui.service / synui-foot.service stay disabled on installs —
        # the session goes through PAM so logind owns XDG_RUNTIME_DIR
        # (they remain the no-login path for the live ISO only).
        mkdir -p /mnt/etc/greetd
        cat > /mnt/etc/greetd/config.toml << 'GREETD_EOF'
[terminal]
vt = 1

[default_session]
command = "tuigreet --time --remember --theme 'border=magenta;text=cyan;prompt=green;time=magenta;action=cyan;button=yellow;container=black;input=magenta' --cmd /usr/local/bin/synui-session"
user = "greeter"
GREETD_EOF
        arch-chroot /mnt systemctl enable greetd.service 2>/dev/null || true
        echo "  Desktop: SynapseUI (greetd login as $NEW_USER)"
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
        else
            for drv in /sys/class/drm/card*/device/driver; do
                case "$(readlink "$drv" 2>/dev/null)" in
                    *nouveau) export WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 ;;
                esac
            done
        fi
        export XDG_SESSION_TYPE=wayland
        export XDG_CURRENT_DESKTOP=synui
        export LIBSEAT_BACKEND=seatd
        export XCURSOR_THEME=Adwaita
        export XCURSOR_SIZE=24
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
autostart = swaybg -c '#0b0b14'
# synapse-menu-gen.py refreshes the Applications submenu from installed
# .desktop files before each waybar launch; run with ';' (not '&&') so a
# generator failure still leaves waybar starting with the prior menu.
autostart = python3 $HOME/.config/waybar/synapse-menu-gen.py; exec waybar
autostart = foot synsh
border_width    = 2
gap             = 8
master_factor   = 0.60

# "night drive" palette — matches foot.ini and waybar style.css
border_color_norm  = #2a2a40
border_color_focus = #ff296d
border_color_ai    = #05d9e8
border_color_warn  = #ff3524

# CRT post-process (GPU only; VMs fall back to the plain look).
# Glitch fires on window close and, sustained, while synguard holds
# any window in ALERT/DENY — the screen corrupts under attack.
effects           = on
effect_scanline   = 0.35
effect_curvature  = 0.25
effect_aberration = 0.40
effect_glitch     = 0.60
ai_layout       = on
ai_ctx_decor    = on
start_overlay   = off
workspace_1_intent = main workspace
workspace_2_intent = web browsing and research
workspace_3_intent = writing code and running tests
workspace_4_intent = terminal and system administration
workspace_5_intent = media and entertainment
SYNUIRC

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

# waybar HUD — clock + system telemetry in the same palette.
# The SYNAPSE badge doubles as the start menu (waybar's built-in GTK
# dropdown; synui renders layer-shell xdg popups, so it just works).
# Heredoc is unquoted so $NEW_USER lands in menu-file — keep the rest
# of the config free of $ and backticks.
mkdir -p "/mnt/home/$NEW_USER/.config/waybar"
# NB: no comments in this seed. It is JSONC and waybar would accept them, but
# menu-actions below is rewritten in place by synapse-menu-gen.py, which reads
# the file back with json — and json.dump would drop any comment it did not
# choke on first. The repo copy (SYNAPSE/waybar/config.jsonc) is the annotated
# one; this stays plain.
cat > "/mnt/home/$NEW_USER/.config/waybar/config.jsonc" << WAYBAREOF
{
    "layer": "top",
    "position": "top",
    "height": 28,
    "modules-left": ["custom/synapse"],
    "modules-center": ["clock"],
    "modules-right": ["custom/gamemode", "cpu", "memory", "network"],
    "custom/synapse": {
        "format": "◢ SYNAPSE",
        "tooltip": false,
        "menu": "on-click",
        "menu-file": "/home/$NEW_USER/.config/waybar/synapse-menu.xml",
        "menu-actions": {
            "terminal": "foot",
            "aishell": "foot synsh",
            "status": "foot --hold syn status",
            "network": "foot -e nmtui",
            "monitor": "foot -e top",
            "logout": "pkill -x synui",
            "reboot": "sudo systemctl reboot",
            "poweroff": "sudo systemctl poweroff"
        }
    },
    "custom/gamemode": {
        "exec": "/usr/bin/synui-game-status",
        "return-type": "json",
        "interval": 2,
        "tooltip": true
    },
    "clock": { "format": "{:%H:%M:%S  %Y-%m-%d}", "interval": 1 },
    "cpu": { "format": "CPU {usage}%", "interval": 2 },
    "memory": { "format": "MEM {percentage}%", "interval": 5 },
    "network": {
        "format-wifi": "NET {essid} {signalStrength}%",
        "format-ethernet": "NET {ipaddr}",
        "format-disconnected": "NET offline",
        "format-disabled": "NET wifi off",
        "tooltip-format-disabled": "Wi-Fi is switched off - click to turn it back on",
        "tooltip-format-disconnected": "Disconnected - click for network options",
        "menu": "on-click",
        "menu-file": "/home/$NEW_USER/.config/waybar/network-menu.xml",
        "menu-actions": {
            "wifi_on": "nmcli radio wifi on",
            "wifi_off": "nmcli radio wifi off",
            "wifi_pick": "foot -e nmtui-connect",
            "wifi_rescan": "nmcli device wifi rescan",
            "settings": "foot -e nmtui",
            "info": "foot --hold nmcli device show"
        },
        "interval": 5
    }
}
WAYBAREOF

# Network menu behind the NET module. A machine that boots with the Wi-Fi radio
# switched off shows "NET wifi off" and has, without this, no way back short of
# a terminal — "nmcli radio wifi on" also clears an rfkill soft block and needs
# no root, so the bar alone is enough to recover.
cat > "/mnt/home/$NEW_USER/.config/waybar/network-menu.xml" << 'NETMENUEOF'
<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <object class="GtkMenu" id="menu">
    <child>
      <object class="GtkMenuItem" id="wifi_pick">
        <property name="label">Wi-Fi Networks…</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="wifi_rescan">
        <property name="label">Rescan</property>
      </object>
    </child>
    <child>
      <object class="GtkSeparatorMenuItem" id="netsep1"/>
    </child>
    <child>
      <object class="GtkMenuItem" id="wifi_on">
        <property name="label">Turn Wi-Fi On</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="wifi_off">
        <property name="label">Turn Wi-Fi Off</property>
      </object>
    </child>
    <child>
      <object class="GtkSeparatorMenuItem" id="netsep2"/>
    </child>
    <child>
      <object class="GtkMenuItem" id="settings">
        <property name="label">Network Settings</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="info">
        <property name="label">Connection Info</property>
      </object>
    </child>
  </object>
</interface>
NETMENUEOF

# Start menu behind the SYNAPSE badge — GtkBuilder XML; the object ids
# map to menu-actions in config.jsonc. Entries only reference programs
# the installer actually puts on the target (foot, synsh, syn, nmtui,
# top from procps-ng). synapse-menu-gen.py (written below) rewrites both
# this file and config.jsonc's menu-actions on every boot, adding an
# "Applications" submenu built from installed .desktop files — this
# heredoc is just the seed used before the generator ever runs.
cat > "/mnt/home/$NEW_USER/.config/waybar/synapse-menu.xml" << 'MENUEOF'
<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <object class="GtkMenu" id="menu">
    <child>
      <object class="GtkMenuItem" id="terminal">
        <property name="label">Terminal</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="aishell">
        <property name="label">AI Shell (synsh)</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="status">
        <property name="label">System Status</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="network">
        <property name="label">Network Setup</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="monitor">
        <property name="label">Process Monitor</property>
      </object>
    </child>
    <child>
      <object class="GtkSeparatorMenuItem" id="sep1"/>
    </child>
    <child>
      <object class="GtkMenuItem" id="logout">
        <property name="label">Log Out</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="reboot">
        <property name="label">Reboot</property>
      </object>
    </child>
    <child>
      <object class="GtkMenuItem" id="poweroff">
        <property name="label">Shut Down</property>
      </object>
    </child>
  </object>
</interface>
MENUEOF

# Regenerates synapse-menu.xml + config.jsonc's menu-actions from installed
# .desktop files on every waybar launch (see the autostart line above) —
# keep this in sync with SYNAPSE/waybar/synapse-menu-gen.py in the repo.
cat > "/mnt/home/$NEW_USER/.config/waybar/synapse-menu-gen.py" << 'GENEOF'
#!/usr/bin/env python3
# Regenerates synapse-menu.xml and config.jsonc's custom/synapse.menu-actions
# from installed .desktop files, so the SYNAPSE waybar menu's "Applications"
# submenu always reflects what's actually installed. Run before waybar
# starts (see synuirc's autostart line) — never crashes the caller: any
# failure leaves the existing files untouched and exits 0, so waybar still
# launches with whatever menu it had last.
#
# The two files are a matched pair: waybar looks a clicked item's GtkMenuItem
# id ("app_7") up in menu-actions to find the command to run. Let them drift
# apart and the menu misfires in silence — ids past the end of menu-actions do
# nothing at all, and ids still in range launch whatever app now sits at that
# index. So: parse tolerantly, write both files or neither, and refuse to write
# a pair whose ids don't line up.

import configparser
import glob
import json
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

WAYBAR_DIR = os.path.expanduser("~/.config/waybar")
CONFIG_PATH = os.path.join(WAYBAR_DIR, "config.jsonc")
MENU_PATH = os.path.join(WAYBAR_DIR, "synapse-menu.xml")

# XDG application dirs, in precedence order (first match for a given
# .desktop basename wins, per the XDG spec).
APP_DIRS = [
    os.path.expanduser("~/.local/share/applications"),
    "/usr/local/share/applications",
    "/usr/share/applications",
]

FIELD_CODE_RE = re.compile(r"%[fFuUdDnNickvm]")

STATIC_ITEMS = [
    ("terminal", "Terminal", "foot"),
    ("aishell", "AI Shell (synsh)", "foot synsh"),
    ("status", "System Status", "foot --hold syn status"),
    ("network", "Network Setup", "foot -e nmtui"),
    ("monitor", "Process Monitor", "foot -e top"),
]
POWER_ITEMS = [
    ("logout", "Log Out", "pkill -x synui"),
    ("reboot", "Reboot", "sudo systemctl reboot"),
    ("poweroff", "Shut Down", "sudo systemctl poweroff"),
]


def strip_jsonc(text):
    """Drop // and /* */ comments and trailing commas from JSONC.

    waybar's config is JSONC and ours carries comments, but json.load only
    speaks strict JSON. It used to be handed the raw file: the first comment to
    land in config.jsonc made the load raise, the blanket except in main()
    swallowed it, and the menu quietly stopped regenerating — which is how the
    XML and menu-actions drifted apart to begin with.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':                    # copy strings verbatim, escapes and all
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                nl = text.find("\n", i)
                if nl < 0:
                    break
                i = nl
                continue
            if text[i + 1] == "*":
                end = text.find("*/", i + 2)
                i = n if end < 0 else end + 2
                continue
        out.append(c)
        i += 1
    # Legal in JSONC, fatal in JSON.
    return re.sub(r",(\s*[}\]])", r"\1", "".join(out))


def clickable_ids(menu_xml):
    """Ids of GtkMenuItems that actually dispatch an action when clicked.

    A menu item holding a <child type="submenu"> only opens that submenu, so it
    needs no entry in menu-actions; every other item does.
    """
    root = ET.fromstring(menu_xml)
    ids = set()
    for obj in root.iter("object"):
        if obj.get("class") != "GtkMenuItem":
            continue
        if any(c.get("type") == "submenu" for c in obj.findall("child")):
            continue
        ids.add(obj.get("id"))
    return ids


def write_atomic(path, data):
    """Write to a temp file in the same dir, then rename over the target.

    waybar may be starting while we run; a rename is atomic, so it reads one
    whole version or the other and never a half-written menu.
    """
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".menu-gen-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def find_apps():
    seen_ids = set()
    apps = []
    for d in APP_DIRS:
        for path in sorted(glob.glob(os.path.join(d, "*.desktop"))):
            entry_id = os.path.basename(path)
            if entry_id in seen_ids:
                continue
            seen_ids.add(entry_id)

            cp = configparser.RawConfigParser(strict=False)
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    cp.read_file(f)
            except (OSError, configparser.Error):
                continue
            if "Desktop Entry" not in cp:
                continue
            e = cp["Desktop Entry"]

            if e.get("Type", "Application") != "Application":
                continue
            if e.getboolean("NoDisplay", fallback=False):
                continue
            if e.getboolean("Hidden", fallback=False):
                continue

            name = e.get("Name")
            exec_ = e.get("Exec")
            if not name or not exec_:
                continue

            cmd = FIELD_CODE_RE.sub("", exec_)
            cmd = re.sub(r"\s+", " ", cmd).strip()
            if not cmd:
                continue
            if e.getboolean("Terminal", fallback=False):
                cmd = f"foot -e {cmd}"

            apps.append((name, cmd))

    apps.sort(key=lambda a: a[0].lower())
    return apps


def build_menu_xml(apps):
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        "<interface>",
        '  <object class="GtkMenu" id="menu">',
    ]

    def item(indent, id_, label):
        pad = "  " * indent
        lines.append(f"{pad}<child>")
        lines.append(f'{pad}  <object class="GtkMenuItem" id="{id_}">')
        lines.append(f'{pad}    <property name="label">{escape(label)}</property>')
        lines.append(f"{pad}  </object>")
        lines.append(f"{pad}</child>")

    def separator(indent, id_):
        pad = "  " * indent
        lines.append(f"{pad}<child>")
        lines.append(f'{pad}  <object class="GtkSeparatorMenuItem" id="{id_}"/>')
        lines.append(f"{pad}</child>")

    for id_, label, _ in STATIC_ITEMS:
        item(2, id_, label)
    separator(2, "sep1")

    app_actions = {}
    lines.append("    <child>")
    lines.append('      <object class="GtkMenuItem" id="applications">')
    lines.append('        <property name="label">Applications</property>')
    lines.append('        <child type="submenu">')
    lines.append('          <object class="GtkMenu" id="applications_menu">')
    for i, (name, cmd) in enumerate(apps):
        aid = f"app_{i}"
        app_actions[aid] = cmd
        item(6, aid, name)
    lines.append("          </object>")
    lines.append("        </child>")
    lines.append("      </object>")
    lines.append("    </child>")

    separator(2, "sep2")
    for id_, label, _ in POWER_ITEMS:
        item(2, id_, label)

    lines.append("  </object>")
    lines.append("</interface>")
    return "\n".join(lines) + "\n", app_actions


def main():
    try:
        apps = find_apps()
        menu_xml, app_actions = build_menu_xml(apps)

        with open(CONFIG_PATH, encoding="utf-8") as f:
            config = json.loads(strip_jsonc(f.read()))

        actions = {}
        for id_, _, cmd in STATIC_ITEMS:
            actions[id_] = cmd
        actions.update(app_actions)
        for id_, _, cmd in POWER_ITEMS:
            actions[id_] = cmd

        # Every item the XML can emit needs a command behind it, or clicking it
        # is a silent no-op. Cheap check; the bug it catches is invisible at
        # runtime. Items that only open a submenu ("Applications") are skipped —
        # they are containers, and GTK never dispatches an action for them.
        missing = clickable_ids(menu_xml) - set(actions)
        if missing:
            raise ValueError(f"menu items with no action: {sorted(missing)}")

        config["custom/synapse"]["menu-actions"] = actions

        # Both files or neither — they only mean anything as a matched pair.
        write_atomic(MENU_PATH, menu_xml)
        write_atomic(CONFIG_PATH, json.dumps(config, indent=4) + "\n")
    except Exception as exc:  # noqa: BLE001 - never block waybar's launch
        print(f"synapse-menu-gen: {exc}", file=sys.stderr)
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
GENEOF
chmod +x "/mnt/home/$NEW_USER/.config/waybar/synapse-menu-gen.py"

cat > "/mnt/home/$NEW_USER/.config/waybar/style.css" << 'WAYBARCSS'
* {
    font-family: monospace;
    font-size: 12px;
    min-height: 0;
    border: none;
}
window#waybar {
    background: rgba(11, 11, 20, 0.85);
    color: #c8e3ee;
    border-bottom: 2px solid #ff296d;
}
#custom-synapse {
    color: #05d9e8;
    font-weight: bold;
    padding: 0 12px;
}
#custom-synapse:hover {
    background: rgba(5, 217, 232, 0.15);
}
/* Start menu dropdown (GtkMenu spawned by the badge) */
menu {
    background: rgba(11, 11, 20, 0.97);
    border: 1px solid #ff296d;
    color: #c8e3ee;
}
menu menuitem {
    padding: 6px 14px;
}
menu menuitem:hover {
    background: #ff296d;
    color: #0b0b14;
}
menu separator {
    background: #2a2a40;
    min-height: 1px;
}
#clock {
    color: #ffd319;
}
#cpu, #memory, #network, #custom-gamemode {
    color: #05d9e8;
    padding: 0 10px;
}
#network.disconnected {
    color: #ff296d;
}
/* A switched-off radio is not the same failure as "no route" — do not make the
   user guess which one the bar is showing. */
#network.disabled {
    color: #ffd319;
}
/* Game mode. Always on the bar, so "off" reads as off rather than as a module
   that failed to load. */
#custom-gamemode.inactive {
    color: #3a4a52;
}
#custom-gamemode.active {
    color: #0b0b14;
    background: #ffd319;
    font-weight: bold;
}
WAYBARCSS

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
arch-chroot /mnt mkinitcpio -P 2>&1 | tail -5 \
    || die "mkinitcpio failed — the installed system would not boot"
[ -s /mnt/boot/initramfs-linux.img ] \
    || die "initramfs missing after mkinitcpio — the installed system would not boot"

success "System configured"

# ── Bootloader ────────────────────────────────────────────
header
step "Installing Bootloader"

cat > /mnt/etc/default/grub << EOF
GRUB_DEFAULT=0
GRUB_TIMEOUT=5
GRUB_DISTRIBUTOR="SynapseOS"
GRUB_CMDLINE_LINUX_DEFAULT="$GPU_KERNEL_PARAMS"
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
echo "  Admin: use $(bold "sudo") with your user password."
echo "  The root account is locked (no root login / su)."
echo "  Note: 3 wrong password attempts lock the account for 10 minutes."
echo ""

prompt "Remove installation media and press ENTER to reboot..."
read -r
umount -R /mnt 2>/dev/null || true
reboot
