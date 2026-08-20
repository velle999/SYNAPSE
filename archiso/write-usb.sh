#!/usr/bin/env bash
# write-usb.sh — write a SynapseOS ISO to a USB stick AND verify the whole write.
#
# ⚠ THE VERIFY IS THE POINT. `dd` reporting "3954196480 bytes copied" does not
# mean the stick holds those bytes: the write can be interrupted, or the tool
# can return before the device's cache is flushed and the stick pulled early.
# What you get then is a stick whose FRONT is perfect and whose TAIL is
# whatever was there before — and every check anybody actually does lives in
# that good front:
#
#   * the partition table, the SYNAPSEOS_* label and the ISO 9660 UUID all read
#     correctly, so lsblk and the file manager say the right thing;
#   * `arch/pkglist.x86_64.txt` reads correctly, so the version looks right;
#   * the kernel and initramfs are early in the image, so they are usually fine.
#
# But mkarchiso lays the boot CONFIGS and the squashfs out late, so a corrupt
# tail means GRUB starts, reads a grub.cfg full of binary noise, and drops you
# at a `grub>` prompt — on a machine where the same ISO boots perfectly in
# qemu, because qemu reads the .iso file and never touches the stick. That is
# exactly what happened to 0.2.9.1 (2026-08-20) and to 0.1.7 before it
# (2026-07-18). Both times the ISO was blameless and hours went into the ISO.
#
# So this script does not finish until `cmp` has read the entire stick back and
# agreed with the image, byte for byte. It prints VERIFIED GOOD or it fails.
#
# Usage: sudo ./write-usb.sh /dev/sdX [VERSION]
#        sudo ./write-usb.sh /dev/sdX 0.2.9.1
#        sudo ./write-usb.sh /dev/sdX /path/to/some.iso
#
# With no version it takes the newest SynapseOS-*.iso in archiso/out.
set -euo pipefail

dev="${1:?usage: write-usb.sh /dev/sdX [VERSION|ISO]}"
want="${2:-}"

out="$(cd "$(dirname "$0")/out" && pwd)"

[[ $EUID -eq 0 ]] || { echo "write-usb: writing $dev needs root — use sudo" >&2; exit 1; }

# ── Which image ──────────────────────────────────────────────
if [[ -z $want ]]; then
    iso=$(ls -1t "$out"/SynapseOS-*-x86_64.iso 2>/dev/null | head -1) \
        || { echo "write-usb: no ISO in $out" >&2; exit 1; }
    [[ -n $iso ]] || { echo "write-usb: no ISO in $out" >&2; exit 1; }
elif [[ -f $want ]]; then
    iso=$want
else
    iso="$out/SynapseOS-${want}-x86_64.iso"
    [[ -f $iso ]] || { echo "write-usb: no such image: $iso" >&2; exit 1; }
fi

# ⚠ Verify the SOURCE first. Writing a corrupt image perfectly is a stick that
# fails in the same place every time and reads as a hardware fault.
if [[ -f "$iso.sha256" ]]; then
    echo "verifying the image itself ..."
    ( cd "$(dirname "$iso")" && sha256sum -c "$(basename "$iso").sha256" )
fi

size=$(stat -c%s "$iso")

# ── Which device, and is it one we may destroy ───────────────
[[ -b $dev ]] || { echo "write-usb: $dev is not a block device" >&2; exit 1; }

# A PARTITION IS THE CLASSIC MISTAKE, and it half-works: dd'ing a hybrid ISO to
# /dev/sdb1 leaves a filesystem the desktop will happily mount and a stick no
# firmware will boot, because the partition table and the boot sectors that
# belong at the START OF THE DISK went inside a partition instead.
if [[ $(lsblk -ndo TYPE "$dev") != 'disk' ]]; then
    echo "write-usb: $dev is a partition — write to the whole disk (e.g. ${dev%%[0-9]*})" >&2
    exit 1
fi

# Never the disk this system is running from. A stick is removable; the boot
# disk is not, and the two are one typo apart.
if [[ $(lsblk -ndo RM "$dev") != '1' ]]; then
    echo "write-usb: $dev is not a removable device — refusing" >&2
    echo "  (if you really mean it, dd it by hand and verify with cmp yourself)" >&2
    exit 1
fi
while read -r mnt; do
    [[ -z $mnt ]] && continue
    case "$mnt" in
        /|/boot|/boot/*|/home|/var|/usr)
            echo "write-usb: $dev holds $mnt — refusing" >&2; exit 1 ;;
    esac
done < <(lsblk -nro MOUNTPOINTS "$dev" | tr ',' '\n')

devsize=$(blockdev --getsize64 "$dev")
if (( devsize < size )); then
    echo "write-usb: $dev holds $devsize bytes, the image needs $size" >&2
    exit 1
fi

echo
echo "  image:  $iso ($(numfmt --to=iec "$size"))"
echo "  device: $dev  $(lsblk -ndo MODEL,SIZE "$dev")"
echo
read -r -p "This ERASES $dev. Type the device path to confirm: " confirm
[[ $confirm == "$dev" ]] || { echo "write-usb: not confirmed" >&2; exit 1; }

# ── Write ────────────────────────────────────────────────────
#
# Anything the desktop auto-mounted has to go first, or the page cache holds a
# view of a filesystem that no longer exists and the verify below reads it back
# instead of the stick.
while read -r part mnt; do
    [[ -n $mnt ]] || continue
    echo "unmounting $part ($mnt)"
    umount "$part" 2>/dev/null || udisksctl unmount -b "$part" >/dev/null 2>&1 || true
done < <(lsblk -nrpo NAME,MOUNTPOINTS "$dev" | tail -n +2 | tr ',' ' ')

echo "writing ..."
# oflag=direct bypasses the page cache on the way in; conv=fsync makes dd wait
# for the device before it exits. WITHOUT conv=fsync dd can report success with
# megabytes still in flight, which is one of the two ways this ends in a
# corrupt tail — the other is pulling the stick before the flush.
dd if="$iso" of="$dev" bs=4M oflag=direct conv=fsync status=progress
sync
blockdev --flushbufs "$dev"

# ── Verify ───────────────────────────────────────────────────
#
# The whole image, not a prefix, not a hash of the first N MB. cmp reads the
# stick back through the same block device and reports the FIRST differing
# byte, which is also the most useful number there is when it fails: where the
# write stopped being true.
echo "verifying the write (reads the whole stick back — this is the slow part) ..."
if cmp -n "$size" "$iso" "$dev"; then
    echo
    echo "VERIFIED GOOD — $dev matches $iso byte for byte."
    exit 0
fi

cat >&2 <<EOF

WRITE VERIFY FAILED — the stick does NOT match the image.

The offset above is where it stopped matching. Do NOT boot this stick: the
front of it is correct, so it will look right in lsblk and in a file manager,
and it will fail later and somewhere else — at a grub> prompt, or with an
initramfs that cannot mount the root filesystem.

Which of the two it is:

  * Read the divergence point again. A clean read means the stick simply holds
    the wrong bytes — an interrupted or unflushed write — and writing it again
    fixes it.
  * An I/O error there means the stick is failing. Use another one.

    dd if=$dev skip=<the offset above> bs=1 count=4096 of=/dev/null
EOF
exit 1
