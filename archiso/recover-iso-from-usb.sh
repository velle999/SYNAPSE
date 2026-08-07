#!/usr/bin/env bash
# One-off: pull the built SynapseOS ISO back off the USB it was dd'd to.
#
# The 0.2.6 ISO was built, written to /dev/sdd, and then lost with the rest of
# archiso/out/ when the box was reinstalled. The stick is the only copy left.
# A dd'd hybrid ISO sits at the very start of the device, so the image is
# recoverable byte-for-byte -- the only thing we have to get right is how many
# bytes to take. That number is declared by the ISO itself, in the primary
# volume descriptor, so we read it rather than guess it.
set -euo pipefail

dev="${1:-/dev/sdd}"
ver="${2:-0.2.6}"
out="$(cd "$(dirname "$0")/out" && pwd)"
iso="$out/SynapseOS-${ver}-x86_64.iso"

[[ $EUID -eq 0 ]] || { echo "run me with sudo -- reading $dev needs root" >&2; exit 1; }
[[ -b $dev ]] || { echo "$dev is not a block device" >&2; exit 1; }

# --- the ISO9660 primary volume descriptor lives at LBA 16 (byte 32768) ------
# Byte 0 is the descriptor type (1 = primary), bytes 1-5 are the magic "CD001".
# If those don't match, this device was never dd'd an ISO and we stop before
# writing 7.9 GB of whatever it actually holds.
# Compared as hex, because `od -c` renders these bytes as space-padded columns
# and any attempt to match that text is really a bet about whitespace.
magic="$(dd if="$dev" bs=1 skip=32768 count=6 status=none | od -An -tx1 | tr -d ' \n')"
if [[ $magic != 014344303031 ]]; then
    echo "no ISO9660 primary volume descriptor at $dev+32768" >&2
    echo "expected 014344303031 (0x01 'CD001'), got $magic" >&2
    echo "refusing to guess -- is $dev really the SynapseOS stick?" >&2
    exit 1
fi

# Volume Space Size: PVD offset 80, both-endian, little-endian half first.
# Units are logical blocks, and the ISO's logical block size is at offset 128
# (also both-endian) -- read it rather than assume 2048.
blocks="$(dd if="$dev" bs=1 skip=32848 count=4 status=none | od -An -tu4 | tr -d ' ')"
bsize="$(dd if="$dev" bs=1 skip=32896 count=2 status=none | od -An -tu2 | tr -d ' ')"
bytes=$(( blocks * bsize ))

# Cross-check against the partition table the kernel already parsed: the image
# has to be at least long enough to contain its own appended EFI partition.
# This is the check that would catch a PVD we misread -- a too-small count
# would truncate the tail, which is exactly the failure that still mounts,
# still boots, and only breaks late.
p2_end=0
for p in /sys/class/block/"$(basename "$dev")"?*; do
    [[ -r $p/start ]] || continue
    end=$(( $(cat "$p/start") + $(cat "$p/size") ))
    (( end > p2_end )) && p2_end=$end
done
min=$(( p2_end * 512 ))

echo "declared volume : $blocks blocks x $bsize = $bytes bytes"
echo "partitions end  : $min bytes"
(( bytes >= min )) || { echo "declared size is SHORTER than the partition table -- aborting" >&2; exit 1; }

devsz="$(blockdev --getsize64 "$dev")"
(( bytes <= devsz )) || { echo "declared size exceeds the device -- aborting" >&2; exit 1; }

echo
echo "recovering $bytes bytes from $dev -> $iso"
dd if="$dev" of="$iso" bs=4M count="$bytes" iflag=count_bytes status=progress

# build.sh chowns both the dir and the files because publish-release.sh runs
# unprivileged and has to write the split parts next to the ISO.
chown "$(stat -c '%u:%g' "$out/..")" "$out" "$iso"

echo
echo "recovered: $(stat -c '%s' "$iso") bytes"
echo "next: verify the payload against the checksum baked into the image,"
echo "      then regenerate $iso.sha256 / .b2sum (the originals died with out/)."
