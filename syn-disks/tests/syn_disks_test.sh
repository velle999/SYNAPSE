#!/usr/bin/env bash
#
# syn_disks_test.sh — against fixtures, never against the machine's real disks.
#
# ⚠ THE ONE RULE FOR THIS FILE: nothing in here may write to a block device.
#
# Every test drives the binary through SYN_DISKS_SYSFS and SYN_DISKS_MOUNTS,
# which point it at a described machine inside a mktemp -d that the EXIT trap
# removes. The commands that COULD write to hardware — format, mount, eject —
# are exercised either through --dry-run or against a fake udisksctl on PATH
# that records its arguments instead of doing anything. A disk utility whose
# test suite could reach real hardware would be the single most dangerous file
# in this repository, and "it only formats the fixture" is exactly the sentence
# that precedes somebody losing a disk.
#
# The fixture is a copy of the machine this was written on, because that
# machine is the interesting case: an encrypted root, so "/" is
# /dev/mapper/cryptroot over dm-0 over nvme1n1p2 over nvme1n1, and the safety
# check has to walk all four hops to notice that formatting the second one
# would destroy the running system.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

SD=${1:-./build/syn-disks}
[ -x "$SD" ] || { echo "not executable: $SD" >&2; exit 1; }
SD=$(readlink -f "$SD")

pass=0 fail=0
ok()    { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()   { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# `((n++))` returns the OLD value, so a bare post-increment exits 1 the first
# time and would kill this script under set -e. Hence $((n + 1)) above.

# ⚠ Output of a command that is EXPECTED to exit non-zero.
#
# `set -o pipefail` makes the status of `cmd | grep` the LAST non-zero status in
# the pipeline — which, for everything in this suite that tests a refusal, is
# the refusal itself rather than grep's verdict. Nine assertions here checked
# that a refusal printed the right explanation, and every one of them reported
# FAIL while the program was doing exactly the right thing: the message
# matched, and the pipeline still exited 1 because syn-disks had.
#
# Capture first, match second. `says` always exits 0, so the only status the
# caller sees is grep's.
says() { local out; out=$("$@" 2>&1); printf '%s\n' "$out"; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

echo "syn-disks tests — $SD"

# ── the binary answers ──────────────────────────────────────────────────────

"$SD" --version | grep -q '^syn-disks '
check "--version prints a version" $?

"$SD" --help | grep -q 'the SynapseOS disk utility'
check "--help prints usage" $?

"$SD" not-a-command >/dev/null 2>&1
[ $? -eq 2 ] && ok "unknown command exits 2" || bad "unknown command exits 2"

"$SD" --help | grep -q 'no override'
check "--help states the format restriction" $?

# ── the fixture ─────────────────────────────────────────────────────────────
#
# nvme1n1  the system disk
#   p1     /boot, vfat
#   p2     crypto_LUKS  <- dm-0 "cryptroot", btrfs, holds / and /home
# sdz      an unrelated disk
#   sdz1   mounted at /mnt/data
#   sdz2   not mounted        <- the only thing here that may be formatted
#   sdz9, sdz10               <- ordering
# sdy      a USB stick that claims to be rotational
# loop0, zram0, md0           <- must not appear

S="$T/sys/class/block"
mkdir -p "$S"

# A whole disk: size in 512-byte units, plus the queue attributes that only a
# whole disk has.
mkdisk() {  # name sectors rotational removable
    mkdir -p "$S/$1/queue" "$S/$1/device"
    echo "$2" > "$S/$1/size"
    echo "$3" > "$S/$1/queue/rotational"
    echo "$4" > "$S/$1/removable"
    echo 512  > "$S/$1/queue/logical_block_size"
}
# A partition lives INSIDE its disk's directory and is also visible flat in
# /sys/class/block, exactly as the kernel presents it.
mkpart() {  # disk name number sectors
    mkdir -p "$S/$1/$2"
    echo "$3" > "$S/$1/$2/partition"
    echo "$4" > "$S/$1/$2/size"
    echo 2048 > "$S/$1/$2/start"
    ln -sfn "$1/$2" "$S/$2"
}

mkdisk nvme1n1 500118192 0 0
mkpart nvme1n1 nvme1n1p1 1 8388608
mkpart nvme1n1 nvme1n1p2 2 491708416

# The device-mapper volume over the LUKS container, both directions: dm-0 has
# nvme1n1p2 as a slave, nvme1n1p2 has dm-0 as a holder.
mkdir -p "$S/dm-0/slaves/nvme1n1p2" "$S/dm-0/dm" "$S/nvme1n1p2/holders/dm-0"
echo 491692032 > "$S/dm-0/size"
echo cryptroot > "$S/dm-0/dm/name"

mkdisk sdz 1000215216 1 0
mkpart sdz sdz1 1 500000000
mkpart sdz sdz2 2 500000000
mkpart sdz sdz9 9 2048
mkpart sdz sdz10 10 2048

# The USB stick that lies about being a spinning disk. This is not a
# hypothetical: the SanDisk Cruzer Blade in the machine this was written on
# reports queue/rotational = 1.
mkdisk sdy 120225792 1 1

mkdisk loop0 1000 0 0
mkdisk zram0 1000 0 0
mkdisk md0   1000 0 0

cat > "$T/mounts" <<'EOF'
dm-0 / btrfs rw,subvol=/@ 0 0
dm-0 /home btrfs rw,subvol=/@home 0 0
nvme1n1p1 /boot vfat rw 0 0
sdz1 /mnt/data ext4 rw 0 0
EOF

# lsblk is deliberately NOT provided to most tests: everything below has to
# work without it, because it is enrichment and not a dependency of the truth.
export SYN_DISKS_SYSFS="$T/sys"
export SYN_DISKS_MOUNTS="$T/mounts"
export SYN_DISKS_LSBLK="definitely-not-a-real-command"

# ── enumerating ─────────────────────────────────────────────────────────────

out=$("$SD" --rec list)

echo "$out" | grep -q '/dev/nvme1n1' && echo "$out" | grep -q '/dev/sdz'
check "list finds the disks" $?

echo "$out" | grep -qE 'loop0|zram0|md0'
[ $? -ne 0 ] && ok "list hides loop, zram and md" || bad "list hides loop, zram and md"

echo "$out" | grep -q 'dm-0'
[ $? -ne 0 ] && ok "list hides dm-0 (a volume, not a drive)" \
             || bad "list hides dm-0 (a volume, not a drive)"

echo "$out" | grep -q '/dev/nvme1n1.*system'
check "list marks the disk holding /" $?

echo "$out" | grep '/dev/sdz' | grep -q 'system'
[ $? -ne 0 ] && ok "list does not mark an unrelated disk" \
             || bad "list does not mark an unrelated disk"

# 4 partitions on sdz, counted from sysfs with no lsblk anywhere.
echo "$out" | grep '/dev/sdz	' | awk -F'\t' '{exit ($8 == "4") ? 0 : 1}'
check "list counts partitions without lsblk" $?

# ── the flag that lies ──────────────────────────────────────────────────────

"$SD" --rec list | grep '/dev/sdy' | grep -q 'usb-stick'
check "a removable device is not called a hard disk (rotational=1)" $?

"$SD" --rec list | grep '/dev/sdz	' | grep -q 'hdd'
check "a non-removable rotational disk still reads as hdd" $?

# A partition has no queue/ of its own. Read there, "is it spinning" answers
# no by default and every partition of a spinning disk reported as an SSD.
"$SD" --no-color info sdz1 | grep -q 'kind *hdd'
check "a partition inherits its drive's kind" $?

# ── partition ordering ──────────────────────────────────────────────────────
#
# A string sort files sdz10 between sdz1 and sdz2. A partition table shown in
# the wrong order is one somebody will act on by position.
"$SD" --rec parts sdz | tail -n +2 | cut -f1 | tr '\n' ' ' \
    | grep -q '/dev/sdz1 /dev/sdz2 /dev/sdz9 /dev/sdz10'
check "partitions are ordered by number, not by name" $?

# ── the nested volume ───────────────────────────────────────────────────────

parts=$("$SD" --rec parts nvme1n1)

echo "$parts" | grep -q '/dev/dm-0'
check "parts shows the volume unlocked inside a partition" $?

echo "$parts" | grep '/dev/dm-0' | awk -F'\t' '{exit ($8 == "1") ? 0 : 1}'
check "the nested volume is reported one level deep" $?

# lsblk shows ONE mount point for a btrfs with subvolumes. This is the reason
# nothing in this program asks lsblk where anything is mounted.
# rec_row encodes EVERY byte of every field, and that includes the ", "
# joining two mount points — the comma arrives as %2C, not as a comma.
echo "$parts" | grep '/dev/dm-0' | awk -F'\t' '{exit ($5 ~ /%2C/) ? 0 : 1}'
check "every mount of a multi-subvolume filesystem is reported" $?

# ── format: the refusals that have no override ──────────────────────────────
#
# This is the block that matters. Each of these devices is one somebody could
# plausibly type, and formatting any of them destroys the running system.

for target in nvme1n1p2 nvme1n1p1 nvme1n1 dm-0; do
    "$SD" format "$target" --fs=ext4 --yes >/dev/null 2>&1
    [ $? -eq 1 ] && ok "format refuses $target (system disk)" \
                 || bad "format refuses $target (system disk)"
done

says "$SD" format nvme1n1p2 --fs=ext4 --yes | grep -q 'running system'
check "the refusal says why" $?

# The container itself reports nothing mounted — only the volume inside it
# does. A check that looked at the target alone would wave this through.
"$SD" --no-color info nvme1n1p2 | grep -q 'mounted at *not mounted'
check "the LUKS container itself reports no mounts" $?
"$SD" --no-color info nvme1n1p2 | grep -q 'holds this system *yes'
check "...and is still known to hold the system" $?

"$SD" format sdz1 --fs=ext4 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "format refuses a mounted partition" \
             || bad "format refuses a mounted partition"

# sdz itself is not mounted; sdz1 inside it is.
"$SD" format sdz --fs=ext4 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "format refuses a disk with a mounted partition" \
             || bad "format refuses a disk with a mounted partition"

says "$SD" format sdz --fs=ext4 --yes | grep -q '/mnt/data'
check "...and names what is mounted" $?

# ── format: the gate ────────────────────────────────────────────────────────

"$SD" format sdz2 --fs=ext4 >/dev/null 2>&1
[ $? -eq 2 ] && ok "format without --yes exits 2 and does nothing" \
             || bad "format without --yes exits 2 and does nothing"

says "$SD" format sdz2 --fs=ext4 | grep -q 'would run'
check "...and describes what it would have done" $?

"$SD" format sdz2 --fs=reiserfs -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "format refuses a filesystem not on the list" \
             || bad "format refuses a filesystem not on the list"

# --fs picks a row in a fixed table, never a program name. Were it used to
# build "mkfs.$fs", this would be a request to execute /bin/sh.
"$SD" format sdz2 --fs=../../bin/sh -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "--fs cannot name an arbitrary program" \
             || bad "--fs cannot name an arbitrary program"

for label in 'a;rm -rf /' '$(reboot)' 'x`id`' 'tab	inside' 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'; do
    "$SD" format sdz2 --fs=ext4 --label="$label" -n >/dev/null 2>&1
    [ $? -eq 1 ] || bad "format refuses the label '$label'"
done
ok "format refuses labels with shell characters, tabs, or over 32 bytes"

says "$SD" format sdz2 --fs=ext4 --label='My Stick' -n | grep -q "'My Stick'"
check "a label with a space is quoted in the command it shows" $?

# ── format: what IS allowed ─────────────────────────────────────────────────
#
# The suite would be worthless if everything were refused: a guard that says no
# to all input is indistinguishable from a broken command.

"$SD" format sdz2 --fs=ext4 -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "format ALLOWS an unmounted partition on a non-system disk" \
             || bad "format ALLOWS an unmounted partition on a non-system disk"

"$SD" format sdz2 --fs=ext4 -n | grep -q 'mkfs.ext4.*sdz2'
check "...and would run mkfs on exactly that device" $?

"$SD" format sdy --fs=vfat -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "format ALLOWS an unmounted USB stick" \
             || bad "format ALLOWS an unmounted USB stick"

# ── the record format ───────────────────────────────────────────────────────
#
# A filesystem label is arbitrary bytes and a mount point is a path. Either can
# hold a tab, and an unescaped one does not make a broken row — it makes a
# PLAUSIBLE row naming a different device, which the GUI then offers to format.

cat > "$T/mounts-nasty" <<EOF
sdz1 /mnt/tab$(printf '\t')here ext4 rw 0 0
sdz2 /mnt/with\040space ext4 rw 0 0
EOF

nasty=$(SYN_DISKS_MOUNTS="$T/mounts-nasty" "$SD" --rec parts sdz)

[ "$(echo "$nasty" | wc -l)" = 5 ]
check "a tab in a mount point does not add a row" $?

echo "$nasty" | awk -F'\t' 'NR>1 && NF != 12 { bad = 1 } END { exit bad ? 1 : 0 }'
check "every record has exactly 12 fields" $?

echo "$nasty" | grep -q '%09'
check "the tab survives, encoded" $?

# /proc/self/mounts writes a space as \040. Decoded wrongly, the mount point is
# truncated at the space — which is not a missing answer but a wrong one that
# happens to name a real directory.
echo "$nasty" | grep -q '/mnt/with%20space'
check "an octal-escaped space in a mount point is decoded" $?

# ── delegation, without touching anything ───────────────────────────────────
#
# A fake udisksctl that records its arguments. This asserts the argv rather
# than the effect, which is the only way to test a mount without mounting.

mkdir -p "$T/bin"
cat > "$T/bin/udisksctl" <<EOF
#!/bin/sh
echo "\$@" >> "$T/udisks.log"
echo "Mounted /dev/sdz2 at /run/media/test/x"
EOF
chmod +x "$T/bin/udisksctl"

PATH="$T/bin:$PATH" SYN_DISKS_UDISKSCTL=udisksctl "$SD" mount sdz2 >/dev/null 2>&1
grep -q '^mount -b /dev/sdz2$' "$T/udisks.log"
check "mount delegates to udisksctl with the canonical device path" $?

PATH="$T/bin:$PATH" SYN_DISKS_UDISKSCTL=udisksctl "$SD" unmount sdz2 >/dev/null 2>&1
grep -q '^unmount -b /dev/sdz2$' "$T/udisks.log"
check "unmount delegates the same way" $?

# power-off takes the DRIVE. Handed a partition, udisks fails with a message
# about the wrong object, which reads as "eject is broken".
PATH="$T/bin:$PATH" SYN_DISKS_UDISKSCTL=udisksctl "$SD" eject sdz2 >/dev/null 2>&1
grep -q '^power-off -b /dev/sdz$' "$T/udisks.log"
check "eject on a partition powers off the disk it is on" $?

"$SD" mount /dev/definitely-not-here >/dev/null 2>&1
[ $? -eq 1 ] && ok "a device that does not exist is refused" \
             || bad "a device that does not exist is refused"

# ── health ──────────────────────────────────────────────────────────────────
#
# A fake smartctl, so the parsing is exercised on a machine with no
# smartmontools and no failing drives — which is every machine this is likely
# to be built on, and the reason this parsing would otherwise ship untested.

cat > "$T/bin/smartctl" <<'EOF'
#!/bin/sh
case "$*" in
*sdy*)
  echo "SMART overall-health self-assessment test result: FAILED!"
  echo "Device Model:     FAILING DISK 1TB"
  echo "Rotation Rate:    7200 rpm"
  echo "ID# ATTRIBUTE_NAME          FLAG     VALUE WORST THRESH TYPE      UPDATED  WHEN_FAILED RAW_VALUE"
  echo "  5 Reallocated_Sector_Ct   0x0033   001   001   010    Pre-fail  Always   FAILING_NOW 2144"
  echo "  9 Power_On_Hours          0x0032   062   062   000    Old_age   Always       -       33489"
  echo "194 Temperature_Celsius     0x0022   030   045   000    Old_age   Always       -       30 (Min/Max 25/45)"
  exit 8 ;;
*nvme*)
  echo "Model Number:                       WD Blue SN570 1TB"
  echo "SMART/Health Information (NVMe Log 0x02)"
  echo "Critical Warning:                   0x00"
  echo "Temperature:                        41 Celsius"
  echo "Percentage Used:                    3%"
  echo "Data Units Written:                 22,377,371 [11.4 TB]"
  echo "Power On Hours:                     4,380"
  exit 0 ;;
*)
  echo "Smartctl open device: /dev/sdz failed: Permission denied"
  exit 2 ;;
esac
EOF
chmod +x "$T/bin/smartctl"
export PATH="$T/bin:$PATH"

s=$(says "$SD" --rec smart sdy)
echo "$s" | grep -q '^health	FAILING$'
check "a failing SATA drive is reported as failing" $?

# The raw value is the LAST field, not a fixed offset: WHEN_FAILED is "-" on a
# healthy drive and "FAILING_NOW" on this one, so counting from the left lands
# on the wrong column exactly when the answer matters.
echo "$s" | grep -q '^reallocated%20sectors	2144$'
check "the raw attribute value survives a shifted WHEN_FAILED column" $?

echo "$s" | grep -q '^temperature	30%20%C2%B0C$'
check "a temperature with parenthesised detail is trimmed to the number" $?

echo "$s" | grep -q 'about%203.8%20years'
check "power-on hours are also given in years" $?

"$SD" smart sdy >/dev/null 2>&1
[ $? -eq 1 ] && ok "a failing drive exits non-zero" || bad "a failing drive exits non-zero"

# smartctl's exit status is a BITMASK — a failing disk exits non-zero with a
# perfectly good report attached, and treating that as an error hides the one
# result worth seeing. The fake above exits 8 for exactly this reason.
n=$("$SD" --rec smart nvme1n1)
echo "$n" | grep -q '^health	healthy$'
check "an NVMe drive with no overall-health line is read from Critical Warning" $?

echo "$n" | grep -q '^wear%20used	3%25$'
check "NVMe wear is reported" $?

says "$SD" --rec smart sdz | grep -q '^retry	elevate$'
check "a permission failure asks to be retried with authorisation" $?

"$SD" smart sdz >/dev/null 2>&1
[ $? -eq 3 ] && ok "unavailable health exits 3, not 0" \
             || bad "unavailable health exits 3, not 0"

says env SYN_DISKS_SMARTCTL=no-such-smartctl "$SD" --rec smart sdz | grep -q 'not%20installed'
check "a missing smartmontools says so rather than guessing" $?

# ── info, and the walk down to hardware ─────────────────────────────────────

"$SD" --no-color info dm-0 | grep -q 'physical disk *\/dev\/nvme1n1'
check "info walks a volume down to the disk underneath it" $?

"$SD" --no-color info sdz2 | grep -q 'holds this system *no'
check "info says when a device does NOT hold the system" $?

"$SD" parts sdz2 >/dev/null 2>&1
[ $? -eq 1 ] && ok "asking for the partitions of a partition is refused" \
             || bad "asking for the partitions of a partition is refused"

says "$SD" parts sdz2 | grep -q 'is a partition of /dev/sdz'
check "...and says which disk it is on" $?

# ── about ───────────────────────────────────────────────────────────────────

"$SD" --rec about | grep -q 'Support'
check "about carries the donate link" $?

"$SD" --rec about | grep -q 'system%20disk	protected'
check "about states that the system disk is protected" $?

# ── the window follows the desktop font ─────────────────────────────────────
# ~/.config/synui/font.state carries the desktop's family AND its text scale.
# It is not a key in theme.json on purpose: the font outlives a theme switch.
#
# Qt resolves an application's default font ONCE at startup, so both have to be
# BINDINGS on every Text — a bare `font.pixelSize: 13` or a literal family is
# the regression, and it fails silently: the window simply stops moving when the
# control panel changes the font. That is exactly how syn-arsenal and synpkg
# behaved until 2026-08-11, while this window and syn-settings moved correctly.
QML="$(dirname "$0")/../data/syn-disks.qml"
if [ -f "$QML" ]; then
    grep -q 'config/synui/font.state' "$QML" \
        && ok "the desktop font file is watched" \
        || bad "syn-disks.qml does not read font.state"
    grep -q 'root.textScale = s' "$QML" \
        && ok "the scale is read from the same file" \
        || bad "syn-disks.qml reads the family but not the scale"

    # awk rather than `grep -c ... | grep -vc ...`: grep exits 1 on no matches,
    # and under pipefail a correct zero would be read as a failed check.
    n=$(awk '/pixelSize: *[0-9]/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "no pixel size bypasses ui()" \
                 || bad "$n pixel size(s) bypass ui()"

    # The SMART table's monospace is exempt from the FAMILY rule — columns of
    # figures are not prose — but not from the size rule above.
    n=$(awk '/family: *"/ && !/family: *"monospace"/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "every literal family is the deliberate monospace" \
                 || bad "$n literal font family/families are not monospace"

    # Qt.application.font.family is the STARTUP font. As a fallback it freezes
    # the very thing this is fixing — synpkg shipped one and had a row that kept
    # the old face while the pane around it moved.
    n=$(grep -c 'Qt.application.font' "$QML" || true)
    [ "$n" = 0 ] && ok "no fallback pins the startup font" \
                 || bad "$n use(s) of Qt.application.font"
else
    bad "syn-disks.qml not found beside the tests: $QML"
fi

# ── done ────────────────────────────────────────────────────────────────────

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
