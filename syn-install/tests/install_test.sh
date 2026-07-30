#!/usr/bin/env bash
# install_test.sh — drive a REAL install in QEMU, then check what it produced.
#
# layout_test.sh asserts the decision table and the shape of the script. It
# cannot catch a step that runs but does the wrong thing, and four of those
# shipped in one day: a heredoc that swallowed the bootloader section, a zram
# check that destroyed its own evidence, a systemd-boot install that never told
# the firmware it existed, and a guard that refused the directory the installer
# had just created. Every one of them needed an actual install to see, and every
# one was found by a person watching a screen.
#
# So this installs. It boots the ISO headless, answers the installer over ssh,
# shuts the machine down, and then inspects the disk FROM THE HOST — because the
# question is not "did the installer print success", it is "is the thing it left
# behind bootable".
#
#   ./install_test.sh                          # ext4 + grub, unencrypted
#   FS=btrfs LOADER=limine SNAPSHOTS=yes ./install_test.sh
#   FS=xfs LOADER=systemd-boot ENCRYPT=yes ./install_test.sh
#   ISO=/path/to.iso KEEP=1 ./install_test.sh  # keep the disk for poking at
#
# Needs: qemu, edk2-ovmf, sshpass, and sudo for qemu-nbd. Takes ~10 minutes,
# most of it pacstrap.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
ISO="${ISO:-$(ls -t "$here/../../archiso/out"/*.iso 2>/dev/null | head -1)}"
FS="${FS:-ext4}"
LOADER="${LOADER:-grub}"
ENCRYPT="${ENCRYPT:-no}"
SNAPSHOTS="${SNAPSHOTS:-no}"
PRESET="${PRESET:-3}"          # 3 = Minimal: no apps, no model, fastest install
DE="${DE:-4}"                  # 4 = TTY only: no KDE/GNOME download
PASSPHRASE="${PASSPHRASE:-synapse-test-passphrase}"
PORT="${PORT:-2299}"           # not 2222 — do not collide with a manual VM
RAM="${RAM:-4G}"
KEEP="${KEEP:-0}"

work=$(mktemp -d /tmp/syn-install-test.XXXXXX)
DISK="$work/disk.qcow2"
VARS="$work/OVMF_VARS.fd"
LOG="$work/install.log"
QPID=""
NBD=""

pass=0; fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  FAIL  %s\n' "$1"; fail=$((fail+1)); }
info() { printf '  ..    %s\n' "$1"; }
die()  { printf '\n  ABORT %s\n' "$1"; cleanup; exit 2; }

cleanup() {
    [ -n "$NBD" ] && { sudo umount "$work/esp" 2>/dev/null; sudo qemu-nbd -d "$NBD" >/dev/null 2>&1; }
    [ -n "$QPID" ] && kill "$QPID" 2>/dev/null
    if [ "$KEEP" = "1" ]; then
        printf '\n  artifacts kept in %s\n' "$work"
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT

# ── The answer sequence ───────────────────────────────────
#
# One line per read, in the order syn-install performs them. This is deliberately
# a flat list with the prompt written beside each answer rather than something
# clever: when a prompt is added or moved, this desyncs and the test fails, which
# is the correct outcome — a new question in a destructive script is exactly the
# thing a human should have to look at.
answers() {
    echo ""                     # Press ENTER to continue
    echo "vda"                  # Target disk
    echo "1"                    # Install mode: 1) ERASE  2) ADVANCED
    case "$ENCRYPT" in
        yes) echo "y" ;;        # Encrypt the disk? [y/N]
        *)   echo "n" ;;
    esac
    case "$FS" in               # Filesystem [1-4]
        ext4) echo "1" ;; btrfs) echo "2" ;; xfs) echo "3" ;; f2fs) echo "4" ;;
        *) die "unknown FS=$FS" ;;
    esac
    case "$LOADER" in           # Bootloader [1-3]  (UEFI only, which QEMU is)
        grub) echo "1" ;; systemd-boot) echo "2" ;; limine) echo "3" ;;
        *) die "unknown LOADER=$LOADER" ;;
    esac
    # Snapshots are offered only on btrfs with a loader that can boot them.
    if [ "$FS" = "btrfs" ] && [ "$LOADER" != "systemd-boot" ]; then
        case "$SNAPSHOTS" in yes) echo "y" ;; *) echo "n" ;; esac
    fi
    echo "y"                    # Review the plan — are these correct?
    if [ "$ENCRYPT" = "yes" ]; then
        echo "$PASSPHRASE"      # Encryption passphrase
        echo "$PASSPHRASE"      # Repeat passphrase
    fi
    echo "yes"                  # Type 'yes' to confirm  (erase)
    echo "$PRESET"              # What to install [1-4]
    echo "y"                    # Install this selection?
    echo "syn"                  # Username
    echo ""                     # Full name
    echo "synapse"              # Password
    echo "synapse"              # Confirm password
    echo "$DE"                  # Desktop environment [1-4]
    echo "n"                    # Enable GPU inference?  (asked when the CUDA
                                #   package is in the repo, which it is)
    echo "1"                    # Language
    echo ""                     # Timezone (blank = UTC)
    echo ""                     # Remove media and press ENTER to reboot
}

# ── Boot the ISO ──────────────────────────────────────────
[ -n "$ISO" ] && [ -f "$ISO" ] || die "no ISO found — pass ISO=/path/to.iso"
command -v sshpass >/dev/null || die "sshpass is required to drive the installer"

OVMF_CODE=""
for d in /usr/share/edk2/x64 /usr/share/edk2-ovmf/x64 /usr/share/OVMF; do
    for c in OVMF_CODE.4m.fd OVMF_CODE.fd; do
        [ -f "$d/$c" ] && { OVMF_CODE="$d/$c"; cp "$d/$(basename "$c" | sed 's/CODE/VARS/')" "$VARS" 2>/dev/null; break 2; }
    done
done
[ -n "$OVMF_CODE" ] && [ -f "$VARS" ] || die "no OVMF firmware — install edk2-ovmf (UEFI is what we are testing)"

echo "═══ install test: $FS + $LOADER + encrypt=$ENCRYPT ═══"
info "iso   $(basename "$ISO")"
info "disk  20G at $DISK"

qemu-img create -f qcow2 "$DISK" 20G >/dev/null || die "qemu-img failed"

qemu-system-x86_64 \
    -enable-kvm -cpu host -m "$RAM" -smp 4 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE",unit=0 \
    -drive if=pflash,format=raw,file="$VARS",unit=1 \
    -drive file="$DISK",if=virtio,format=qcow2 \
    -cdrom "$ISO" -boot order=dc \
    -netdev user,id=n0,hostfwd=tcp::"$PORT"-:22 -device virtio-net-pci,netdev=n0 \
    -display none -serial file:"$work/serial.log" &
QPID=$!

info "waiting for the live session's sshd (up to 5 min)"
for i in $(seq 1 100); do
    sshpass -p synapse ssh -p "$PORT" -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null -o ConnectTimeout=3 \
        root@localhost true 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || die "QEMU exited before the ISO booted — see $work/serial.log"
    sleep 3
done
sshpass -p synapse ssh -p "$PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    root@localhost true 2>/dev/null || die "live session never came up"
ok "live ISO booted and reachable"

# ── Run the installer ─────────────────────────────────────
#
# stdin is a pipe, not a terminal, which is exactly the case prompt()'s
# typeahead flush is guarded against — answers fed this way survive.
info "installing (this is pacstrap, expect several minutes)"
answers | sshpass -p synapse ssh -p "$PORT" -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null root@localhost \
    'syn-install 2>&1' > "$LOG" 2>&1
rc=$?

if grep -q "SynapseOS installation complete" "$LOG"; then
    ok "installer reported success"
else
    bad "installer did not complete (exit $rc) — last lines:"
    tail -12 "$LOG" | sed 's/^/        /'
fi

# Anything the installer called out as a failure is a failure here too, even if
# it carried on: warnings are how a half-configured system reports itself.
if grep -qE "^\s*✗|ABORT|failed" "$LOG"; then
    bad "the transcript contains failures:"
    grep -E "^\s*✗|ABORT|failed" "$LOG" | head -5 | sed 's/^/        /'
else
    ok "no failure lines in the transcript"
fi

# ── Does the FIRMWARE boot it? ────────────────────────────
#
# The strongest assertion available, and the one that maps onto the failure that
# prompted this test: an install can be complete, correct and unbootable because
# nothing in NVRAM points at it, in which case the firmware quietly boots the
# installer media again.
#
# syn-install's last act is `umount -R /mnt; reboot`, so the target filesystem is
# cleanly unmounted before this happens — which is what makes it safe to stop the
# machine once the choice is visible. OVMF names its choice on the serial console:
#
#     BdsDxe: starting Boot0009 "SynapseOS" from HD(1,GPT,...)/\EFI\...
#     BdsDxe: starting Boot0005 "UEFI QEMU DVD-ROM ..." from PciRoot(...)
#
# The first is a pass. The second is the bug, with the ISO still attached exactly
# as it would be on a real machine someone forgot to unplug.
# The serial log already contains the FIRST boot selection — the ISO, made
# before the installer ever ran. Counting from a baseline and classifying the
# LAST line is the difference between "which did it pick this time" and "has it
# ever picked a DVD", and the naive version of this check reported the bug it
# was written to catch on a machine that did not have it.
base=$(grep -ac 'BdsDxe: starting Boot' "$work/serial.log" 2>/dev/null || echo 0)
info "waiting for the reboot, to see what the firmware picks (first boot was #$base)"
booted=""
for i in $(seq 1 60); do
    now=$(grep -ac 'BdsDxe: starting Boot' "$work/serial.log" 2>/dev/null || echo 0)
    if [ "$now" -gt "$base" ]; then
        last=$(grep -a 'BdsDxe: starting Boot' "$work/serial.log" | tail -1 | tr -d '\r')
        case "$last" in
            *"from HD("*) booted="disk" ;;
            *DVD*|*CD*)   booted="media" ;;
            *)            booted="other" ;;
        esac
        break
    fi
    sleep 3
done
case "$booted" in
    disk)  ok "the firmware booted the INSTALLED DISK — $(sed 's/.*starting //; s/ from .*//' <<<"$last")" ;;
    media) bad "the firmware booted the INSTALL MEDIA again — nothing in NVRAM points at the disk" ;;
    other) bad "the firmware booted something unexpected: $last" ;;
    *)     bad "no second boot seen on the serial console within 180s" ;;
esac

info "stopping the machine (the target was unmounted before the reboot)"
kill "$QPID" 2>/dev/null; sleep 3
kill -0 "$QPID" 2>/dev/null && { kill -9 "$QPID" 2>/dev/null; sleep 2; }
QPID=""

# ── Inspect what it left behind ───────────────────────────
#
# From the host, on the powered-off image. "The installer said it worked" is the
# claim under test, not the evidence.
sudo modprobe nbd max_part=8 2>/dev/null
for n in /dev/nbd0 /dev/nbd1 /dev/nbd2 /dev/nbd3; do
    sudo qemu-nbd --read-only -c "$n" "$DISK" 2>/dev/null && { NBD="$n"; break; }
done
[ -n "$NBD" ] || die "could not attach the disk image for inspection"
sleep 2

parts=$(lsblk -rno NAME,FSTYPE "$NBD" | tail -n +2)
echo "$parts" | grep -q vfat && ok "an EFI System Partition exists" \
                             || bad "no FAT partition — the ESP was never created"

if [ "$ENCRYPT" = "yes" ]; then
    echo "$parts" | grep -q crypto_LUKS && ok "root is LUKS" \
                                        || bad "encryption was requested and the root is not LUKS"
fi

espdev=$(lsblk -rno NAME,FSTYPE "$NBD" | awk '$2=="vfat"{print "/dev/"$1; exit}')
mkdir -p "$work/esp"
if [ -n "$espdev" ] && sudo mount -o ro "$espdev" "$work/esp" 2>/dev/null; then
    case "$LOADER" in
        grub)         boot_bin="EFI/SynapseOS/grubx64.efi" ;;
        systemd-boot) boot_bin="EFI/systemd/systemd-bootx64.efi" ;;
        limine)       boot_bin="EFI/BOOT/BOOTX64.EFI" ;;
    esac
    sudo test -f "$work/esp/$boot_bin" && ok "$LOADER is on the ESP ($boot_bin)" \
                                       || bad "$LOADER's EFI binary is missing from the ESP"
    sudo test -f "$work/esp/EFI/BOOT/BOOTX64.EFI" \
        && ok "the removable-media fallback exists" \
        || info "no \\EFI\\BOOT\\BOOTX64.EFI (only matters if NVRAM is lost)"

    # Every file a boot entry names must exist. This is the check that would
    # have caught an entry pointing at an initramfs mkinitcpio never generated.
    missing=""
    while read -r key val _; do
        case "$key" in
            linux|initrd)   sudo test -f "$work/esp/$val" || missing="$missing $val" ;;
            kernel_path:|module_path:) p="${val#boot():}"
                            sudo test -f "$work/esp/$p" || missing="$missing $p" ;;
        esac
    done < <(sudo cat "$work/esp"/loader/entries/*.conf "$work/esp"/limine.conf 2>/dev/null)
    [ -z "$missing" ] && ok "every boot entry names a file that exists" \
                      || bad "boot entries name missing files:$missing"
    sudo umount "$work/esp" 2>/dev/null
else
    bad "could not mount the ESP"
fi

# The firmware has to know. An unregistered install boots the media instead and
# looks exactly like an install that never happened.
if strings -el "$VARS" 2>/dev/null | grep -qiE "SynapseOS|Linux Boot Manager"; then
    ok "an EFI boot entry was written to NVRAM"
else
    bad "NVRAM has no entry for this install — it would boot the installer again"
fi

sudo qemu-nbd -d "$NBD" >/dev/null 2>&1; NBD=""

echo
if [ "$fail" -gt 0 ]; then
    echo "  $fail check(s) FAILED, $pass passed — transcript: $LOG"
    KEEP=1
    exit 1
fi
echo "  all $pass checks passed"
