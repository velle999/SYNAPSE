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

# ⛔ EVERY GOLDEN OUTPUT BELOW IS IN ENGLISH, AND THE BINARY ANSWERS THE
# DESKTOP'S LANGUAGE. syn-disks' human path goes through gettext as of pkgrel
# 25, so an installed syn-disks on a German desktop fails every assertion that
# names one of its sentences — and passes on every English one, which is how
# this ships broken.
#
# ⚠ LANGUAGE is UNSET, not set. gettext reads LANGUAGE **before** LC_ALL, so a
# desktop with LANGUAGE=de still answers German to an LC_ALL=C.UTF-8 process
# and the pin does nothing at all.
#
# ⚠ THE RECORDS WOULD NOT HAVE NOTICED — they are never translated, and
# tests/i18n_test.sh proves it under a catalog that translates everything. It
# is the --no-color human assertions that need this.
export LC_ALL=C.UTF-8
unset LANGUAGE

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

# ⚠ says(), not a bare pipe, and for a SECOND reason on top of the one at the
# top of this file: `grep -q` exits at the first match and closes the pipe, and
# the binary is then killed by SIGPIPE partway through writing the rest. Under
# pipefail that is status 141 and a FAIL for a program that did exactly the
# right thing. It passes on an ordinary build — where the whole of --help
# reaches the pipe buffer before grep can act — and fails EVERY time under
# ASan, which is slow enough to lose the race. Three assertions here read FAIL
# on the sanitiser build alone, which is the most misleading place to have one.
says "$SD" --version | grep -q '^syn-disks '
check "--version prints a version" $?

says "$SD" --help | grep -q 'the SynapseOS disk utility'
check "--help prints usage" $?

"$SD" not-a-command >/dev/null 2>&1
[ $? -eq 2 ] && ok "unknown command exits 2" || bad "unknown command exits 2"

says "$SD" --help | grep -q 'no override'
check "--help states the format restriction" $?

# ── gui --format, the file manager's entry point ────────────────────────────
#
# Nothing here starts a window: every case below fails BEFORE the exec, which
# is the point — `gui` is the one command that hands control to quickshell, so
# what can be tested about it is exactly what it refuses to hand over.
#
# ⚠ Never add a case that reaches the exec. It would replace this shell.
# The display is unset for the same reason: with one, the next thing `gui`
# would do is start a window. It also pins the ORDER — arguments are checked
# before the environment is, so a mistyped device says so on a headless box.
out=$(env -u WAYLAND_DISPLAY -u DISPLAY "$SD" gui --format 2>&1)
[ $? -ne 0 ] && grep -q 'need a device' <<<"$out" \
    && ok "gui --format with no device is refused" \
    || bad "gui --format with no device is refused"

out=$(env -u WAYLAND_DISPLAY -u DISPLAY "$SD" gui --format /dev/definitely-not-a-disk 2>&1)
[ $? -ne 0 ] && grep -q 'not a block device' <<<"$out" \
    && ok "gui --format on a non-device is refused" \
    || bad "gui --format on a non-device is refused"

says "$SD" --help | grep -q 'gui --format'
check "--help documents gui --format" $?

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
#
# `start` is a real offset and not a constant, because the partition table and
# the free space in it are DERIVED from where the partitions sit. A fixture
# where every partition started at sector 2048 described four partitions all
# on top of each other, which is not a disk any kernel would produce and would
# have made `table` look correct while testing nothing.
mkpart() {  # disk name number sectors start
    mkdir -p "$S/$1/$2"
    echo "$3" > "$S/$1/$2/partition"
    echo "$4" > "$S/$1/$2/size"
    echo "${5:-2048}" > "$S/$1/$2/start"
    ln -sfn "$1/$2" "$S/$2"
}

mkdisk nvme1n1 500118192 0 0
mkpart nvme1n1 nvme1n1p1 1 8388608   2048
mkpart nvme1n1 nvme1n1p2 2 400000000 8390656

# The device-mapper volume over the LUKS container, both directions: dm-0 has
# nvme1n1p2 as a slave, nvme1n1p2 has dm-0 as a holder.
mkdir -p "$S/dm-0/slaves/nvme1n1p2" "$S/dm-0/dm" "$S/nvme1n1p2/holders/dm-0"
echo 491692032 > "$S/dm-0/size"
echo cryptroot > "$S/dm-0/dm/name"

# sdz is laid out with TWO gaps of different sizes, and the small one first.
# "the largest free space" and "the first free space that fits" are the same
# answer on a disk with one gap, and on a disk that has been repartitioned a
# few times they are a 20MB scrap and the 100GB at the end.
#
#   sdz1   2048        .. 500002048     (mounted at /mnt/data)
#   gap A  500002048   .. 500043008     ~20MB
#   sdz2   500043008   .. 700043008     (the only formattable thing here)
#   gap B  700043008   .. 900000000     ~95GB   <- the largest
#   sdz9   900000000   .. 900002048
#   sdz10  900002048   .. 900004096
#   gap C  900004096   .. 1000215216    ~47GB
mkdisk sdz 1000215216 1 0
mkpart sdz sdz1 1 500000000 2048
mkpart sdz sdz2 2 200000000 500043008
mkpart sdz sdz9 9 2048      900000000
mkpart sdz sdz10 10 2048    900002048

# The USB stick that lies about being a spinning disk. This is not a
# hypothetical: the SanDisk Cruzer Blade in the machine this was written on
# reports queue/rotational = 1.
mkdisk sdy 120225792 1 1

# A stick with its write-protect switch set. The kernel says so in `ro`, and
# every destructive command has to read it BEFORE it asks polkit for a
# password: the alternative is what shipped — mke2fs answering "Read-only file
# system while setting up superblock" after the confirmation, which reads like
# a dead stick and means a switch on the side of one.
mkdisk sdv 14336000 0 1
echo 1 > "$S/sdv/ro"

# The stick that LIES about that switch — the Generic Flash Disk in the machine
# this was written on. Its Mode Sense answers "Write Protect is off" at plug-in,
# so `ro` is 0 and every check there is passes; it then throws away every
# sector written to it, and only once mke2fs has failed does the kernel re-read
# it and say "Write Protect is on". Before the write there is nothing to find.
# The fake mkfs below flips `ro` exactly as the kernel does, which is what makes
# the after-the-fact answer testable at all.
mkdisk sdw 14336000 0 1

# The superfloppy: a filesystem written straight onto the drive with no
# partition table around it, which is how nearly every USB stick and camera
# card is sold. It is neither partitioned nor empty.
mkdisk sdx 14336000 0 1
# ...and a genuinely blank one, so "has a filesystem" and "has nothing" can be
# told apart. Without this pair, reporting either answer for both passes.
mkdisk sdu 14336000 0 1

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

# ── format asks guard.c, and does not carry its own copy of the rules ───────
#
# It used to carry a hand-written mount check and nothing else, so every rule
# guard.c had that the copy did not was a rule format did not apply. The
# write-protect flag was one of them: the dry run described the mkfs, the
# dialogue offered the button, polkit asked for a password, and mke2fs then
# said "Read-only file system while setting up superblock". Every one of those
# steps had the answer available and none of them looked.
#
# ⚠ These fail on a `-n` DRY RUN, which is the point. A refusal that only
# arrives at --yes is one the window has already offered a button for.

"$SD" format sdv --fs=ext4 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "format refuses a write-protected device" \
             || bad "format refuses a write-protected device"

says "$SD" --no-color format sdv --fs=ext4 -n | grep -q 'read-only'
check "...and says the kernel has it marked read-only" $?

# The way out is a physical switch, so the code names it rather than offering
# a flag: a front-end switching on `readonly` can say where to look.
# ⚠ Captured first: under pipefail `cmd | grep` reports the REFUSAL's status,
# which is 1 for every assertion in this file that tests one.
r=$("$SD" --rec format sdv --fs=ext4 -n 2>/dev/null)
echo "$r" | grep -q '^fix	readonly$'
check "...with a fix code a window can act on" $?

says "$SD" --no-color format sdv --fs=ext4 -n | grep -q 'write-protect switch'
check "...and the human hint points at the switch on the body" $?

# The rest of what format inherited by asking rather than copying — swap and
# fstab — is asserted in the partitioning section below, where the fixtures
# for both already exist.

# ── a FAILED format is an answer too, and it had none ───────────────────────
#
# 2026-08-14, the Generic Flash Disk: the guard cleared it (`ro` was 0, because
# the stick said so), polkit asked, mke2fs wrote nothing, and the window said
#
#   could not format /dev/sde — mke2fs 1.47.4 (6-Mar-2025) · Creating
#   filesystem with 1792000 4k blocks and 448800 inodes · Filesystem UUID: …
#
# — a version banner and some geometry, with the one line that said why cut off
# by the end of the status bar. Twice, because the first report was unreadable
# enough to look like nothing had happened.
#
# Two things are asserted here and they are separate: that the REASON leads,
# and that the device is asked again AFTER the write.

MKB="$T/mkbin"; mkdir -p "$MKB"
cat > "$MKB/mkfs.ext4" <<EOF
#!/bin/sh
# mke2fs on a stick that lies: it narrates, fails on its LAST line, and the
# kernel marks the device read-only a moment later.
echo "mke2fs 1.47.4 (6-Mar-2025)"
echo "Creating filesystem with 1792000 4k blocks and 448800 inodes"
echo "Filesystem UUID: 971feac3-707c-4fd3-ae6f-bae2231b10ac"
echo "Superblock backups stored on blocks:"
echo "	32768, 98304, 163840, 229376, 294912, 819200, 884736"
echo 1 > "$S/sdw/ro"
echo "mke2fs: Input/output error while writing out and closing file system" >&2
exit 1
EOF
chmod +x "$MKB/mkfs.ext4"

mkfail() { PATH="$MKB:$PATH" SYN_DISKS_NO_PKEXEC=1 "$SD" "$@"; }

echo 0 > "$S/sdw/ro"
mkfail format sdw --fs=ext4 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "a format the tool refuses exits 1" \
             || bad "a format the tool refuses exits 1"

# The stick passed the guard on the way in — otherwise the interesting failure
# never happens and the assertions below would be testing a refusal.
echo 0 > "$S/sdw/ro"
mkfail format sdw --fs=ext4 -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "...and it was ALLOWED before the write, as it must be" \
             || bad "...and it was ALLOWED before the write, as it must be"

echo 0 > "$S/sdw/ro"
r=$(mkfail --rec format sdw --fs=ext4 --yes 2>/dev/null)
echo "$r" | grep -q 'now%20marked%20it%20read-only'
check "a device that refuses every write is asked again afterwards" $?

# `latched`, NOT `readonly`, and the distinction is the whole point of having
# two codes: the flag found BEFORE a write is usually a switch on the body of a
# stick, and the same flag found after one cannot be — nobody flips a switch
# halfway through a format. This stick had no switch at all, and "check the
# write-protect switch" sent its owner looking for a part it does not have.
echo 0 > "$S/sdw/ro"
r=$(mkfail --rec format sdw --fs=ext4 --yes 2>/dev/null)
echo "$r" | awk -F'\t' 'NR > 1 && $4 == "latched" { found = 1 } END { exit !found }'
check "...with a fix code of its own, not the switch-on-the-body one" $?

# ...and the two never collapse into each other. The pre-write refusal keeps
# saying `readonly`, or the distinction exists only in the source.
r=$("$SD" --rec format sdv --fs=ext4 -n 2>/dev/null)
echo "$r" | grep -q '^fix	readonly$'
check "...while the flag found BEFORE a write still says readonly" $?

# ⚠ The one that the screenshot was: whatever else the detail holds, it may not
# OPEN with the tool's version banner. A status bar shows the front of this.
echo 0 > "$S/sdw/ro"
r=$(mkfail --rec format sdw --fs=ext4 --yes 2>/dev/null \
    | awk -F'\t' 'NR > 1 { print $3 }')
case "$r" in
    mke2fs%201.47.4*) bad "the failure still leads with the tool's banner" ;;
    *)                ok "the failure leads with the reason, not the banner" ;;
esac

# Nothing is dropped: a tool that puts its reason somewhere else must still be
# quoted in full, so the banner is still in there — just not first.
echo "$r" | grep -q 'mke2fs%201.47.4'
check "...and the tool's whole output is still there behind it" $?

# A device that fails for its OWN reasons must not be told it is
# write-protected. `ro` stays 0 here and the sentence must not appear.
cat > "$MKB/mkfs.ext4" <<'EOF'
#!/bin/sh
echo "mke2fs 1.47.4 (6-Mar-2025)"
echo "mke2fs: No space left on device while setting up superblock" >&2
exit 1
EOF
chmod +x "$MKB/mkfs.ext4"
echo 0 > "$S/sdw/ro"
r=$(mkfail --rec format sdw --fs=ext4 --yes 2>/dev/null)
echo "$r" | grep -q 'read-only' \
    && bad "a failure invents a write-protect switch that is not set" \
    || ok "a failure with no read-only flag says nothing about a switch"

echo "$r" | grep -q 'No%20space%20left' \
    && ok "...and still reports what the tool said" \
    || bad "...and still reports what the tool said"

# The terminal has room for all of it, and gets our sentence FIRST — the tool
# could not say this one, because by the time the kernel knew, it had exited.
cat > "$MKB/mkfs.ext4" <<EOF
#!/bin/sh
echo "mke2fs 1.47.4 (6-Mar-2025)"
echo 1 > "$S/sdw/ro"
echo "mke2fs: Input/output error while writing out and closing file system" >&2
exit 1
EOF
chmod +x "$MKB/mkfs.ext4"
echo 0 > "$S/sdw/ro"
says mkfail --no-color format sdw --fs=ext4 --yes | head -1 \
    | grep -q 'could not format'
check "the terminal names the failure before quoting the tool" $?

echo 0 > "$S/sdw/ro"
says mkfail --no-color format sdw --fs=ext4 --yes | grep -q 'switched itself'
check "...and says the drive did this to itself, not that a switch is set" $?

# ⚠ It must NOT reach for the switch here. The stick this came from has none,
# and being told to check one is a dead end dressed up as a way out.
echo 0 > "$S/sdw/ro"
says mkfail --no-color format sdw --fs=ext4 --yes | grep -q 'switch on the body' \
    && bad "a latched drive still sends its owner looking for a switch" \
    || ok "...and does not send its owner looking for a switch that is not there"

echo 0 > "$S/sdw/ro"
says mkfail --no-color format sdw --fs=ext4 --yes | grep -q 'Input/output error'
check "...and still quotes what the tool said" $?

# ── the new filesystem belongs to the person who formatted it ───────────────
#
# mkfs runs as root, so without this the root directory of a freshly formatted
# stick belongs to root and the person who formatted it cannot write to it.
# udisks2 has an option for exactly this (`take-ownership`) and this program
# does not format through udisks, so it arranges the same thing itself — AT
# CREATION, because a chown afterwards would mean a second privileged step:
# mount the new filesystem, change the owner, unmount.
#
# Only three of the six store ownership at all. vfat, exfat and ntfs keep none
# on disk — the mount options decide — so for those the right behaviour is to
# add nothing, and that is asserted here too. A flag that appeared for them
# would make mkfs.vfat refuse the command outright.
#
# ⚠ Run as root with nothing saying who asked, there is nobody to hand the
# filesystem to and the flags are correctly absent. That is a real case (a
# rescue shell), not a failure, so it is skipped rather than asserted.

OWN_UID=$(id -u); OWN_GID=$(id -g)
if [ "$OWN_UID" = 0 ]; then
    echo "  skip  filesystem ownership (running as root: nobody to hand it to)"
else
    # A runtime dir of our own. The scratch paths live under $XDG_RUNTIME_DIR
    # rather than /tmp — the path is in a command line that root then runs, and
    # a predictable name in a world-writable directory is a symlink waiting to
    # happen — so the test must control it to know what to look for.
    RT="$T/runtime"; mkdir -p "$RT"
    owns() { XDG_RUNTIME_DIR="$RT" SYN_DISKS_NO_PKEXEC=1 "$SD" "$@"; }

    says owns format sdz2 --fs=ext4 -n | grep -q -- "-E root_owner=$OWN_UID:$OWN_GID"
    check "ext4 is created owned by the user, not by root" $?

    says owns format sdz2 --fs=btrfs -n | grep -qE -- "--rootdir $RT/syn-disks-rootdir\.[0-9]+"
    check "btrfs is created owned by the user, not by root" $?

    says owns format sdz2 --fs=xfs -n | grep -qE -- "-p $RT/syn-disks-proto\.[0-9]+"
    check "xfs is created owned by the user, not by root" $?

    # ⚠ The scratch paths carry the PID. Two sticks formatted at once is two of
    # these processes, and with one fixed name the first to finish would delete
    # the second's protofile out from under a running mkfs.xfs.
    a=$(says owns format sdz2 --fs=xfs -n | grep -o 'syn-disks-proto\.[0-9]*')
    b=$(says owns format sdz2 --fs=xfs -n | grep -o 'syn-disks-proto\.[0-9]*')
    [ -n "$a" ] && [ "$a" != "$b" ] \
        && ok "two formats at once do not share a scratch file" \
        || bad "two formats at once do not share a scratch file"

    for f in vfat exfat ntfs; do
        says owns format sdz2 --fs=$f -n | grep -qE 'root_owner|--rootdir|-p /' \
            && bad "$f is left alone (it stores no ownership)"
    done
    ok "vfat, exfat and ntfs are left alone — they store no ownership"

    # A DRY RUN MUST LEAVE NOTHING BEHIND. Describing an operation is not doing
    # it, and the paths are decided when the command is built precisely so that
    # what --dry-run prints is what --yes runs.
    [ -z "$(ls -A "$RT")" ] && ok "a dry run writes no scratch file" \
                            || bad "a dry run writes no scratch file"

    # ── and the scratch file is really there WHEN mkfs runs ─────────────────
    #
    # The flag naming a file that does not exist is worse than no flag at all:
    # mkfs.xfs fails outright rather than quietly making a root-owned
    # filesystem. So this asserts the lifecycle from inside — the fake mkfs
    # records what it could see at the moment it was called — and not merely
    # that prepare() was reached.
    cat > "$MKB/mkfs.xfs" <<EOF
#!/bin/sh
# Records the protofile as it stood while mkfs was running.
while [ \$# -gt 0 ]; do
    if [ "\$1" = -p ]; then cp "\$2" "$T/seen-proto" 2>/dev/null; fi
    shift
done
exit 0
EOF
    chmod +x "$MKB/mkfs.xfs"

    rm -f "$T/seen-proto"
    PATH="$MKB:$PATH" owns format sdz2 --fs=xfs --yes >/dev/null 2>&1
    [ -f "$T/seen-proto" ] && ok "the protofile exists while mkfs is running" \
                           || bad "the protofile exists while mkfs is running"

    # The xfs protofile's third line is the ROOT DIRECTORY's own mode, uid and
    # gid. The other lines are a boot-block name nobody uses and a size; they
    # exist to carry this one.
    grep -q "^d--755 $OWN_UID $OWN_GID\$" "$T/seen-proto" 2>/dev/null
    check "...and names the user as the owner of the root directory" $?

    # CLEANED UP AFTERWARDS. It is a file in the user's runtime directory named
    # after this program; leaving one behind per format is litter that nothing
    # else will ever collect.
    [ -z "$(ls -A "$RT")" ] && ok "...and is removed once the write is done" \
                            || bad "...and is removed once the write is done"

    # btrfs's --rootdir is a directory whose CONTENTS are copied into the new
    # filesystem, so what matters is that mkfs is handed an EMPTY one.
    cat > "$MKB/mkfs.btrfs" <<EOF
#!/bin/sh
while [ \$# -gt 0 ]; do
    if [ "\$1" = --rootdir ]; then
        ls -A "\$2" > "$T/seen-rootdir" 2>/dev/null
        # Something inside it afterwards, so the cleanup asserted below runs
        # against a directory that is NOT empty. mkfs.btrfs itself leaves
        # nothing there; this stands in for a format killed halfway.
        echo stale > "\$2/leftover"
    fi
    shift
done
exit 0
EOF
    chmod +x "$MKB/mkfs.btrfs"

    rm -f "$T/seen-rootdir"
    PATH="$MKB:$PATH" owns format sdz2 --fs=btrfs --yes >/dev/null 2>&1
    [ -f "$T/seen-rootdir" ] && [ ! -s "$T/seen-rootdir" ] \
        && ok "btrfs is handed an EMPTY directory, never a stale one" \
        || bad "btrfs is handed an EMPTY directory, never a stale one"

    # ⚠ THE REGRESSION TEST of the pair. `rmdir` removes empty directories
    # only, so the first version of this left the directory behind the moment
    # anything was inside it — and a directory left behind is the one the next
    # format inherits. Reverting the cleanup to a bare rmdir() fails here and
    # nowhere else in this file.
    [ -z "$(ls -A "$RT")" ] \
        && ok "...and a directory with something in it is still cleaned up" \
        || bad "...and a directory with something in it is still cleaned up"

    # ⚠ NOT FATAL. With no runtime directory there is nowhere to put the
    # scratch file, and the answer is a filesystem owned by root — not a
    # refusal to format. ext4 needs no scratch file and is unaffected.
    r=$(env -u XDG_RUNTIME_DIR SYN_DISKS_NO_PKEXEC=1 "$SD" format sdz2 --fs=xfs -n 2>&1)
    [ $? -eq 0 ] && ok "with no runtime dir the format still goes ahead" \
                 || bad "with no runtime dir the format still goes ahead"
    echo "$r" | grep -q -- '-p ' \
        && bad "...naming no protofile it cannot write" \
        || ok "...naming no protofile it cannot write"
fi

# ── KILLING syn-disks must not stop the write ───────────────────────────────
#
# The window runs this binary as a child, and quickshell SIGKILLs its children
# when it exits. So closing the window killed syn-disks instantly, and the mkfs
# it had started — root, unkillable from here — died of SIGPIPE the next time it
# printed a line of progress into a pipe whose reader had gone. A format
# ABORTED PART WAY THROUGH WRITING A FILESYSTEM, from a window close, with
# nothing on screen that said an operation was in flight.
#
# Proven by driving the real window headlessly against a stub: parent killed at
# the moment of exit, child SIGPIPE'd in the same second. The fix is that a
# destructive tool writes to a FILE and not to a pipe this process holds, so
# nothing upstream can interrupt it. This asserts exactly that property: kill
# the binary mid-format and the tool still reaches the end of its work.

KLOG="$T/killlog"
cat > "$MKB/mkfs.ext4" <<EOF
#!/bin/sh
# Prints progress as mke2fs does — which is what made the pipe fatal.
i=0
while [ \$i -lt 6 ]; do
    echo "Writing inode tables: \$i"
    echo "tick \$i" >> "$KLOG"
    sleep 0.4
    i=\$((i + 1))
done
echo "FINISHED" >> "$KLOG"
EOF
chmod +x "$MKB/mkfs.ext4"

: > "$KLOG"
echo 0 > "$S/sdw/ro"
PATH="$MKB:$PATH" SYN_DISKS_NO_PKEXEC=1 "$SD" format sdw --fs=ext4 --yes >/dev/null 2>&1 &
victim=$!
sleep 0.8
kill -9 "$victim" 2>/dev/null
wait "$victim" 2>/dev/null
# Long enough for the tool to finish on its own after its parent is gone.
sleep 2.5

grep -q FINISHED "$KLOG" \
    && ok "killing syn-disks mid-format does not stop the write" \
    || bad "killing syn-disks left the filesystem half written"

# ...and it was genuinely still running when the kill landed, or the assertion
# above proves nothing: a tool that had already finished would pass it too.
[ "$(grep -c tick "$KLOG")" -ge 5 ] \
    && ok "...and the tool ran on past the death of its parent" \
    || bad "the tool had already finished — the kill proved nothing"

# A format that WORKS says so, and says nothing about a switch. The suite has
# never had a passing mkfs at all — every format assertion above is a refusal
# or a dry run, and "it reported a failure correctly" is only half the contract.
cat > "$MKB/mkfs.ext4" <<'EOF'
#!/bin/sh
echo "mke2fs 1.47.4 (6-Mar-2025)"
echo "Creating filesystem with 1792000 4k blocks and 448800 inodes"
echo "done"
EOF
chmod +x "$MKB/mkfs.ext4"
echo 0 > "$S/sdw/ro"
r=$(mkfail --rec format sdw --fs=ext4 --yes 2>/dev/null)
echo "$r" | awk -F'\t' 'NR > 1 && $2 == "ok" { found = 1 } END { exit !found }'
check "a format that works reports ok" $?

echo "$r" | awk -F'\t' 'NR > 1 && $4 == "none" { found = 1 } END { exit !found }'
check "...with no way out to offer, because nothing went wrong" $?

echo 0 > "$S/sdw/ro"

# ── can the kernel MOUNT what we would create ───────────────────────────────
#
# Creating and mounting are two capabilities, and only the first is a package
# dependency. A stick was formatted exFAT on this machine and then would not
# mount: a kernel upgrade with no reboot had deleted the running kernel's
# module tree, so exfat.ko was gone and nothing could load it. Every mkfs was
# installed, so `about` reported the machine fully capable.
#
# Both inputs are fixtures — nothing here reads the real /proc or /usr/lib.

KFS="$T/kfs"; mkdir -p "$KFS/mods" "$KFS/empty"
: > "$KFS/empty/modules.dep"
printf 'nodev\tsysfs\nnodev\tproc\n\text4\n\tvfat\n' > "$KFS/procfs"
printf 'kernel/fs/exfat/exfat.ko.zst:\nkernel/fs/ntfs3/ntfs3.ko.zst:\n' \
    > "$KFS/mods/modules.dep"

kwarn() {  # prints the number of `warn` rows for --fs=$1
    SYN_DISKS_FILESYSTEMS="$KFS/procfs" SYN_DISKS_MODULES="${2:-$KFS/mods}" \
        "$SD" --rec format sdz2 --fs="$1" --dry-run 2>&1 | grep -c '^warn'
}

[ "$(kwarn ext4)" = 0 ] && ok "no warning for a driver already in /proc/filesystems" \
                        || bad "no warning for a driver already in /proc/filesystems"

# Not loaded is not unavailable — the module may simply never have been asked
# for, and asking the kernel to load it needs root and would be a side effect
# of merely describing a plan.
[ "$(kwarn exfat)" = 0 ] && ok "no warning for a driver present as a module" \
                         || bad "no warning for a driver present as a module"

[ "$(kwarn xfs)" = 1 ] && ok "warns when neither /proc nor the module tree has the driver" \
                       || bad "warns when neither /proc nor the module tree has the driver"

# The filesystem's name is not its driver's name.
[ "$(kwarn ntfs)" = 0 ] && ok "NTFS counts as mountable via the ntfs3 driver" \
                        || bad "NTFS counts as mountable via the ntfs3 driver"

# A warning, never a refusal: a stick for a camera or a Windows machine is a
# good reason to write a filesystem this kernel cannot read.
"$SD" --rec format sdz2 --fs=xfs --dry-run >/dev/null 2>&1
[ $? -eq 0 ] && ok "...and an unmountable filesystem is still ALLOWED" \
             || bad "...and an unmountable filesystem is still ALLOWED"

# ⚠ Token-wise, never strstr: "ntfs" is a substring of "ntfs3". A substring
# match reports the old read-only driver present on every machine that has the
# new one, and matches nothing correctly in the other direction either.
for probe in exfat3 myexfat; do
    printf '\t%s\n' "$probe" > "$KFS/procfs"
    [ "$(kwarn exfat "$KFS/empty")" = 1 ] \
        || bad "'$probe' in /proc/filesystems must not satisfy exfat"
done
ok "a driver name is matched as a whole token, not as a substring"

# The suffix follows the kernel's module compression and is not ours to
# predict; the leading slash is what anchors the name.
printf 'nodev\tproc\n' > "$KFS/procfs"
for suf in .ko .ko.zst .ko.xz; do
    printf 'kernel/fs/exfat/exfat%s:\n' "$suf" > "$KFS/mods/modules.dep"
    [ "$(kwarn exfat)" = 0 ] || bad "modules.dep entry ending '$suf' must count"
done
ok "a module counts however the kernel compressed it"

# The two reasons are fixed by different things — one by a reboot, one not —
# so they are told apart rather than merged into "unavailable".
printf 'nodev\tproc\n' > "$KFS/procfs"
SYN_DISKS_FILESYSTEMS="$KFS/procfs" SYN_DISKS_MODULES="$KFS/no-such-tree" \
    says "$SD" format sdz2 --fs=exfat -n | grep -q 'reboot'
check "a missing module TREE says so, and says to reboot" $?

SYN_DISKS_FILESYSTEMS="$KFS/procfs" SYN_DISKS_MODULES="$KFS/empty" \
    says "$SD" format sdz2 --fs=exfat -n | grep -q 'cannot mount'
check "a kernel simply without the driver says that instead" $?

# about is the pane somebody reads when wondering why a mount failed.
SYN_DISKS_FILESYSTEMS="$KFS/procfs" SYN_DISKS_MODULES="$KFS/empty" \
    says "$SD" --rec about | grep -q '^mount%20exfat	no'
check "about reports what this kernel can mount, not just what it can create" $?

# ── partitioning ────────────────────────────────────────────────────────────
#
# ⚠ Everything below drives a FAKE sfdisk that appends its arguments and its
# stdin to a log. Not one of these tests can reach a partition table, and the
# fake is the only reason a `mktable --yes` may appear in a test suite at all.
#
# The partition table also needs a LABEL — "gpt" or "dos" — and that lives in
# the udev database rather than in sysfs, so this section is the one place a
# fake lsblk is provided. Everything above it still runs with no lsblk at all,
# which is the point: enrichment, not a dependency.

mkdir -p "$T/bin"

cat > "$T/bin/sfdisk-fake" <<EOF
#!/bin/sh
printf 'argv:%s\n' " \$*" >> "$T/sfdisk.log"
sed 's/^/stdin: /' >> "$T/sfdisk.log"
echo "The partition table has been altered."
EOF
chmod +x "$T/bin/sfdisk-fake"

# lsblk -P output for the fixture. sdz2 gets a UUID so the fstab rule below has
# something to match by — which is the case that matters, since a modern fstab
# names nothing by path.
cat > "$T/bin/lsblk-fake" <<'EOF'
#!/bin/sh
cat <<'ROWS'
NAME="nvme1n1" FSTYPE="" LABEL="" UUID="" PARTLABEL="" PARTTYPENAME="" PARTTYPE="" PARTUUID="" PTTYPE="gpt" MODEL="WD Blue SN570 1TB"
NAME="nvme1n1p1" FSTYPE="vfat" LABEL="BOOT" UUID="1234-ABCD" PARTLABEL="EFI" PARTTYPENAME="EFI System" PARTTYPE="c12a7328-f81f-11d2-ba4b-00a0c93ec93b" PARTUUID="aaaa-0001" PTTYPE="gpt" MODEL=""
NAME="nvme1n1p2" FSTYPE="crypto_LUKS" LABEL="" UUID="cccc-0002" PARTLABEL="" PARTTYPENAME="Linux filesystem" PARTTYPE="0fc63daf-8483-4772-8e79-3d69d8477de4" PARTUUID="aaaa-0002" PTTYPE="gpt" MODEL=""
NAME="sdz" FSTYPE="" LABEL="" UUID="" PARTLABEL="" PARTTYPENAME="" PARTTYPE="" PARTUUID="" PTTYPE="gpt" MODEL="Fixture Disk"
NAME="sdz1" FSTYPE="ext4" LABEL="data" UUID="dddd-0001" PARTLABEL="" PARTTYPENAME="Linux filesystem" PARTTYPE="" PARTUUID="bbbb-0001" PTTYPE="gpt" MODEL=""
NAME="sdz2" FSTYPE="ext4" LABEL="spare" UUID="dddd-0002" PARTLABEL="" PARTTYPENAME="Linux filesystem" PARTTYPE="" PARTUUID="bbbb-0002" PTTYPE="gpt" MODEL=""
NAME="sdz9" FSTYPE="ext4" LABEL="" UUID="dddd-0009" PARTLABEL="" PARTTYPENAME="Linux filesystem" PARTTYPE="" PARTUUID="bbbb-0009" PTTYPE="gpt" MODEL=""
NAME="sdz10" FSTYPE="ext4" LABEL="" UUID="dddd-0010" PARTLABEL="" PARTTYPENAME="Linux filesystem" PARTTYPE="" PARTUUID="bbbb-0010" PTTYPE="gpt" MODEL=""
NAME="sdy" FSTYPE="" LABEL="" UUID="" PARTLABEL="" PARTTYPENAME="" PARTTYPE="" PARTUUID="" PTTYPE="" MODEL="Cruzer Blade"
NAME="sdx" FSTYPE="exfat" LABEL="CAMERA" UUID="1234-5678" PARTLABEL="" PARTTYPENAME="" PARTTYPE="" PARTUUID="" PTTYPE="" MODEL="Generic Flash Disk"
NAME="sdu" FSTYPE="" LABEL="" UUID="" PARTLABEL="" PARTTYPENAME="" PARTTYPE="" PARTUUID="" PTTYPE="" MODEL="Generic Flash Disk"
NAME="sdv" FSTYPE="exfat" LABEL="LOCKED" UUID="1234-9999" PARTLABEL="" PARTTYPENAME="" PARTTYPE="" PARTUUID="" PTTYPE="" MODEL="Generic Flash Disk"
ROWS
EOF
chmod +x "$T/bin/lsblk-fake"

# Every partitioning call goes through these two, so no test can forget the
# fake and reach the real sfdisk on the machine running the suite.
sdp()   { PATH="$T/bin:$PATH" SYN_DISKS_LSBLK=lsblk-fake \
          SYN_DISKS_SFDISK=sfdisk-fake SYN_DISKS_NO_PKEXEC=1 "$SD" "$@"; }
saysp() { local out; out=$(sdp "$@" 2>&1); printf '%s\n' "$out"; }

# ── table: the layout, and the free space in it ─────────────────────────────

t=$(sdp --rec table sdz)

echo "$t" | tail -n +2 | cut -f1,5 | tr '\n' ' ' \
    | grep -q '/dev/sdz1	partition 	free /dev/sdz2	partition 	free /dev/sdz9	partition /dev/sdz10	partition 	free'
check "table reports partitions and gaps in on-disk order" $?

# Derived, because nothing stores it: the disk is 1000215216 sectors and the
# partitions account for 700006144 of them.
[ "$(echo "$t" | awk -F'\t' '$5 == "free"' | wc -l)" = 3 ]
check "the three gaps in the fixture are all found" $?

# sdz9 ends at 900002048 and sdz10 starts there. A gap between two adjacent
# partitions would mean the arithmetic is off by a partition.
echo "$t" | awk -F'\t' '$5 == "free" && $4 < 1048576' | grep -q .
[ $? -ne 0 ] && ok "no gap smaller than a megabyte is reported" \
             || bad "no gap smaller than a megabyte is reported"

# The HUMAN output, which every other assertion in this file ignores in favour
# of --rec. It shipped misaligned: the filesystem type was passed into the
# printf's COLOUR argument and the %-16s beside it padded an empty string, so
# every size moved by the length of the fstype. --rec was perfect throughout,
# because it does not use that format string at all.
#
# The size is right-aligned and ends at column 45: 2 spaces + 16 (device) + 1
# + 16 (fstype) + 1 + 9 (size).
sdp --no-color table sdz | tail -n +2 \
    | awk '{ if (substr($0, 37, 9) !~ /[0-9.]+ [KMGTP]i?B$/) bad = 1 }
           END { exit bad ? 1 : 0 }'
check "every row of the human table puts its size in the same column" $?

# ── table: the protected column ─────────────────────────────────────────────
#
# This column is the whole reason guard.c returns a sentence. The GUI greys a
# button out from it; if it disagreed with the binary the button would be
# enabled for something that is then refused — or, worse, the other way round.

echo "$t" | grep '/dev/sdz1	' | cut -f9 | grep -q 'mounted'
check "table marks a mounted partition as protected" $?

echo "$t" | grep '/dev/sdz2	' | awk -F'\t' '{exit ($9 == "") ? 0 : 1}'
check "...and leaves an idle one unprotected" $?

sdp --rec table nvme1n1 | grep '/dev/nvme1n1p2' | cut -f9 | grep -q 'running%20system'
check "the LUKS partition under / is protected in the table too" $?

# The reason a front-end must not re-derive this: the SAME sentence comes back
# from the command that refuses. Two implementations of one rule agree on the
# day they are written; the day they drift, the GUI offers a button the binary
# then refuses — or, far worse, offers one it does not.
p=$(sdp --rec table nvme1n1 | grep '/dev/nvme1n1p2' | cut -f9)
r=$(saysp rmpart nvme1n1p2 --yes | grep -o 'the running system is on it')
[ -n "$r" ] && [ "$p" = "the%20running%20system%20is%20on%20it" ] \
    && ok "the table's reason and the refusal's reason are the same sentence" \
    || bad "the table's reason and the refusal's reason are the same sentence"

# ── rmpart ──────────────────────────────────────────────────────────────────

for target in nvme1n1p1 nvme1n1p2; do
    sdp rmpart "$target" --yes >/dev/null 2>&1
    [ $? -eq 1 ] && ok "rmpart refuses $target (system disk)" \
                 || bad "rmpart refuses $target (system disk)"
done

sdp rmpart sdz1 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "rmpart refuses a mounted partition" \
             || bad "rmpart refuses a mounted partition"

saysp rmpart sdz1 --yes | grep -q '/mnt/data'
check "...and names where it is mounted" $?

saysp rmpart sdz --yes | grep -q 'whole drive'
check "rmpart on a whole drive says so rather than deleting something" $?

# The suite would be worthless if everything were refused.
sdp rmpart sdz2 -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "rmpart ALLOWS an idle partition on a non-system disk" \
             || bad "rmpart ALLOWS an idle partition on a non-system disk"

saysp rmpart sdz2 -n | grep -q 'sfdisk-fake --delete /dev/sdz 2'
check "...and would delete that partition, by number, from its disk" $?

sdp rmpart sdz2 >/dev/null 2>&1
[ $? -eq 2 ] && ok "rmpart without --yes exits 2 and does nothing" \
             || bad "rmpart without --yes exits 2 and does nothing"

# ── swap: the device nothing reports as mounted ─────────────────────────────
#
# /proc/swaps is not /proc/self/mounts. A guard built on mounts alone calls a
# live swap partition idle, and the machine dies at the next page-out.

cat > "$T/swaps" <<'EOF'
Filename				Type		Size	Used	Priority
/dev/sdz2                               partition	8388604	0	-2
EOF

SYN_DISKS_SWAPS="$T/swaps" sdp rmpart sdz2 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "rmpart refuses a partition holding live swap" \
             || bad "rmpart refuses a partition holding live swap"

SYN_DISKS_SWAPS="$T/swaps" saysp rmpart sdz2 --yes | grep -q 'swapoff'
check "...and says how to free it" $?

# A swap FILE is not a swap device, and reading it as one would protect
# whichever partition the file happens to live on.
cat > "$T/swaps-file" <<'EOF'
Filename				Type		Size	Used	Priority
/swapfile                               file		8388604	0	-2
EOF
SYN_DISKS_SWAPS="$T/swaps-file" sdp rmpart sdz2 -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "a swap FILE does not protect the partition under it" \
             || bad "a swap FILE does not protect the partition under it"

# ── fstab: what the next boot expects ───────────────────────────────────────
#
# /boot on a running machine is very often not mounted. Deleting it is not less
# destructive for that, and a modern fstab names it by UUID.

cat > "$T/fstab" <<'EOF'
# a comment, and a blank line follow

UUID=dddd-0002	/srv	ext4	defaults	0 2
PARTUUID=aaaa-0002	/boot	vfat	defaults	0 2
tmpfs	/tmp	tmpfs	defaults	0 0
EOF

SYN_DISKS_FSTAB="$T/fstab" sdp rmpart sdz2 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "rmpart refuses a partition fstab expects, matched by UUID" \
             || bad "rmpart refuses a partition fstab expects, matched by UUID"

SYN_DISKS_FSTAB="$T/fstab" saysp rmpart sdz2 --yes | grep -q '/srv'
check "...and says where the next boot expects it" $?

# The rule is skipped for a RESIZE, and only for a resize: growing a partition
# leaves its UUID alone, so the entry still resolves afterwards.
SYN_DISKS_FSTAB="$T/fstab" sdp resize sdz2 --size=150G -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "an fstab entry does not block a resize" \
             || bad "an fstab entry does not block a resize"

# ── the rules format gained by ASKING for them ──────────────────────────────
#
# format carried its own mount check and nothing else, so every rule it did not
# copy was a rule it did not have. Swap is the one that would have cost a
# machine: it is not in /proc/self/mounts, so the old check called a live swap
# partition idle and would have written a filesystem over it. The machine dies
# at the next page-out, minutes later, looking like unrelated hardware failure.

SYN_DISKS_SWAPS="$T/swaps" "$SD" format sdz2 --fs=ext4 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "format refuses a partition holding live swap" \
             || bad "format refuses a partition holding live swap"

r=$(SYN_DISKS_SWAPS="$T/swaps" "$SD" --rec format sdz2 --fs=ext4 -n 2>/dev/null)
echo "$r" | grep -q '^fix	swapoff$'
check "...and offers the same way out rmpart does" $?

# Through sdp, because a modern fstab names nothing by path and the UUID it
# matches on comes from lsblk.
SYN_DISKS_FSTAB="$T/fstab" sdp format sdz2 --fs=ext4 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "format refuses a partition /etc/fstab expects" \
             || bad "format refuses a partition /etc/fstab expects"

# ...and it is still the guard's OWN sentence, not one format composed. Two
# implementations of a rule agree on the day they are written; this asserts
# there is only one left to disagree with.
a=$("$SD" --rec format sdv --fs=ext4 -n 2>/dev/null | awk -F'\t' '$1 == "refused" {print $2}')
b=$(sdp --rec table sdv 2>/dev/null | awk -F'\t' 'NR > 1 {print $9}')
[ -n "$a" ] && [ "$a" = "$b" ] \
    && ok "format's refusal and the table's protected column are one sentence" \
    || bad "format's refusal and the table's protected column are one sentence"

# ── the superfloppy: a filesystem with no partition table around it ─────────
#
# How nearly every USB stick and camera card is sold. It has no partitions, so
# a layout derived from the partitions alone came out as ONE GAP the size of
# the drive — and the window drew a bar across the whole disk labelled "free
# space" over 6.8GB of somebody's photos. The bar is what gets clicked before
# New…, so the picture was an invitation to partition a drive that was full.

t=$(sdp --rec table sdx)
echo "$t" | tail -n +2 | awk -F'\t' '{exit ($5 == "whole" && $6 == "exfat") ? 0 : 1}'
check "a filesystem written over the whole drive is reported as such" $?

echo "$t" | grep -q '	free	'
[ $? -ne 0 ] && ok "...and is NOT reported as free space" \
             || bad "...and is NOT reported as free space"

echo "$t" | tail -n +2 | awk -F'\t' '{exit ($4 == "7340032000") ? 0 : 1}'
check "...spanning the whole drive, from byte zero" $?

# The discriminating half: a drive with nothing on it at all really is free
# space, and reporting either answer for both cases would pass a test that only
# looked at one of them.
sdp --rec table sdu | tail -n +2 | awk -F'\t' '{exit ($5 == "free") ? 0 : 1}'
check "a genuinely blank drive is still all free space" $?

# ── mkpart with no table: a refusal that a window can act on ────────────────
#
# This went to stderr as prose. The New… dialogue showed the sentence, had no
# `fix` field to switch on, and so offered no button — the only way out it
# named was a command line, to somebody holding a mouse.

r=$(sdp --rec mkpart sdx -n 2>/dev/null)
echo "$r" | grep -q '^refused	'
check "mkpart on a drive with no partition table refuses in records" $?

echo "$r" | grep -q '^fix	mktable$'
check "...naming mktable as the way out" $?

echo "$r" | grep -q '^device	/dev/sdx$'
check "...and the drive it is about" $?

saysp --no-color mkpart sdx -n | grep -q 'syn-disks mktable /dev/sdx'
check "...while a terminal still gets the command to run" $?

# A drive that cannot be written at all is told so BEFORE it is sent off to
# make a partition table it also could not write. GUARD_ADD checks nothing but
# this, and it has to come first or the way out leads to a second refusal.
r=$(sdp --rec mkpart sdv -n 2>/dev/null)
echo "$r" | grep -q '^fix	readonly$'
check "a write-protected drive says so rather than sending you to mktable" $?

# ── mkpart: the case the two-tier design exists for ─────────────────────────
#
# THE test in this section. Formatting refuses anything sharing a disk with
# "/", and if partitioning used that rule a laptop with one drive could never
# make a partition at all. Free space is not the running system.

sdp mkpart nvme1n1 --size=1G -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "mkpart ALLOWS free space on the disk holding /" \
             || bad "mkpart ALLOWS free space on the disk holding /"

# ...while everything that would touch what is already there still refuses.
sdp mktable nvme1n1 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "...and mktable on that same disk is still refused" \
             || bad "...and mktable on that same disk is still refused"

# The largest gap, not the first that fits. Gap A is ~20MB and comes first;
# gap B is ~95GB. Landing in A because it was first is a surprise nobody asked
# for, and it is what "first fit" does on any disk that has been repartitioned.
#
# 700043264 and not 700043008, which is where gap B actually begins: that is
# not a multiple of 2048 sectors, so the start is rounded UP to the next
# megabyte. An unaligned partition on an SSD or a 4Kn drive turns every write
# into a read-modify-write of the block underneath it, and the cost is
# invisible — it looks like a slow disk, for the life of the partition.
# ⚠ THESE THREE SAY WHAT THEY GOT, and the other assertions in this file do
# not, because these are the three that failed on a machine nobody could reach.
# A `grep -q` on an exact number answers "no" identically whether the geometry
# was off by one megabyte, whether the wrong gap was chosen, or whether the
# command printed an error instead of a script — and the first two of those are
# a bug in this program while the third is a bug in the fixture. On 2026-09-01
# two of them failed inside a VM during an update and the report was two lines
# with no numbers in them, against a suite that passes 217/217 here in English
# and in Japanese. The value is cheap to print and it is the whole diagnosis.
want_field() {  # want_field <label> <expected> <output>
    if printf '%s' "$3" | grep -q "$2"; then
        ok "$1"
    else
        bad "$1"
        printf '        wanted %s\n' "$2" >&2
        printf '        got    %s\n' \
            "$(printf '%s' "$3" | tr '\n' ' ' | cut -c1-160)" >&2
    fi
}

out=$(saysp mkpart sdz -n)
want_field "mkpart picks the largest free space, not the first one" \
           'start=700043264' "$out"

# ...and the size is rounded DOWN, out of the same gap. Rounded up, a partition
# placed at an aligned start would end one megabyte past the free space it was
# put in — which sfdisk refuses, after the confirmation.
want_field "...aligned at the start, and still inside the gap at the end" \
           'size=199956480' "$out"

# Aligned at both ends: the start rounded up to a megabyte, the size rounded
# down so it still fits in the gap afterwards.
want_field "a requested size is rounded down to a megabyte boundary" \
           'size=20971520' "$(saysp mkpart sdz --size=10GiB -n)"

sdp mkpart sdz --size=500G -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "mkpart refuses a size larger than the free space" \
             || bad "mkpart refuses a size larger than the free space"

saysp mkpart sdz --size=500G -n | grep -q 'largest free space'
check "...and says how much there actually is" $?

for size in 'not-a-size' '-5G' '10ZB' '4 4G' ''; do
    sdp mkpart sdz --size="$size" -n >/dev/null 2>&1
    [ $? -eq 1 ] || bad "mkpart refuses the size '$size'"
done
ok "a size that is not a size is refused rather than guessed at"

# A disk with no partition table has no free space to speak of — "the whole
# disk" is not a gap in a table that does not exist.
sdp mkpart sdy --size=1G -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "mkpart on a disk with no table says to make one first" \
             || bad "mkpart on a disk with no table says to make one first"

# --fs picks a row in the same fixed table format uses. Two lists would agree
# on the day they were written.
sdp mkpart sdz --fs=reiserfs -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "mkpart and format offer the same filesystems" \
             || bad "mkpart and format offer the same filesystems"

sdp mkpart sdz --fs=ext4 --label='a;rm -rf /' -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "mkpart checks a label exactly as format does" \
             || bad "mkpart checks a label exactly as format does"

# ── resize: grows, and will not shrink ──────────────────────────────────────

sdp resize sdz2 --size=50G --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "resize refuses to SHRINK a partition" \
             || bad "resize refuses to SHRINK a partition"

saysp resize sdz2 --size=50G --yes | grep -q 'filesystem inside'
check "...and says why, rather than offering a flag" $?

sdp resize sdz2 --size=300G -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "resize refuses to grow past the free space that follows" \
             || bad "resize refuses to grow past the free space that follows"

sdp resize sdz2 --size=150G -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "resize ALLOWS growing into the gap after the partition" \
             || bad "resize ALLOWS growing into the gap after the partition"

# The start is restated rather than left to sfdisk's merge behaviour: a
# partition that grew from the wrong start is one that moved.
saysp resize sdz2 --size=150G -n | grep -q 'start=500043008'
check "...from exactly where it already starts" $?

sdp resize sdz1 --size=300G --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "resize refuses a mounted partition" \
             || bad "resize refuses a mounted partition"

# ── mkpart --start: WHICH free space, not merely how much ───────────────────
#
# A front-end that draws the gaps has to be able to act on the one that was
# clicked. Without this it could only ask for "a partition" and get one in the
# largest gap — and the dry run would agree with itself all the way, because it
# would describe the same wrong gap the command was about to use.
#
# The offset is in BYTES, as `table` prints it: sysfs counts sectors and this
# program converts once, at the read.

gapA=$(sdp --rec table sdz | awk -F'\t' '$5 == "free" { print $3; exit }')
[ -n "$gapA" ] && ok "table names the offset of each gap" \
               || bad "table names the offset of each gap"

# Gap A is the ~20MB scrap, and it is the one mkpart would never pick on its
# own. 500002816 rather than 500002048 because the start is still rounded UP to
# a megabyte — naming a gap does not opt out of alignment.
saysp mkpart sdz --start="$gapA" -n | grep -q 'start=500002816'
check "--start puts the partition in the gap that was named, not the largest" $?

saysp mkpart sdz --start="$gapA" -n | grep -q 'size=38912'
check "...and fills exactly that gap, rounded down to stay inside it" $?

# An offset in no gap at all is refused rather than rounded to the nearest one:
# a stale offset from a layout that has changed since it was read names free
# space that is not there any more, and the partition it would produce is one
# nobody looked at. Byte 0 is inside the first megabyte, which holds the table
# itself and is never free space.
sdp mkpart sdz --start=0 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "--start with no free space at that offset is refused" \
             || bad "--start with no free space at that offset is refused"

saysp mkpart sdz --start=0 -n | grep -q 'syn-disks table'
check "...and says to re-read the layout" $?

# ── copypart ────────────────────────────────────────────────────────────────
#
# ⚠ Drives a FAKE dd that logs its arguments. Nothing here copies a byte.
#
# sdz9 and sdz10 are 1MiB each, adjacent, idle and in nothing's fstab — the
# only pair in the fixture that may legitimately be copied one onto the other.

cat > "$T/bin/dd-fake" <<EOF
#!/bin/sh
printf 'argv:%s\n' " \$*" >> "$T/dd.log"
echo "2048+0 records in"
echo "2048+0 records out"
EOF
chmod +x "$T/bin/dd-fake"

sdc()   { PATH="$T/bin:$PATH" SYN_DISKS_LSBLK=lsblk-fake \
          SYN_DISKS_DD=dd-fake SYN_DISKS_NO_PKEXEC=1 "$SD" "$@"; }
saysc() { local out; out=$(sdc "$@" 2>&1); printf '%s\n' "$out"; }

sdc copypart sdz9 sdz10 -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "copypart ALLOWS two idle partitions of the same size" \
             || bad "copypart ALLOWS two idle partitions of the same size"

# The HUMAN dry run, and not only --rec: `=` is percent-encoded in records, so
# a grep for 'if=/dev/sdz9' against --rec silently never matches. The command
# is printed verbatim here.
saysc copypart sdz9 sdz10 -n | grep -q 'if=/dev/sdz9 of=/dev/sdz10'
check "...and describes the copy in the direction it was asked for" $?

saysc copypart sdz9 sdz10 -n | grep -q 'conv=fsync'
check "...with the flush that makes 'done' mean the data is on the disk" $?

# A copy of a MOUNTED filesystem is a copy of one mid-write: it mounts, and it
# is corrupt. The source is read, not written, and it is still refused.
sdc copypart sdz1 sdz9 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "copypart refuses a MOUNTED source" \
             || bad "copypart refuses a MOUNTED source"

r=$(sdc --rec copypart sdz1 sdz10 -n 2>/dev/null)
echo "$r" | grep -q '^fix	unmount$'
check "...and offers the same way out every other refusal does" $?

sdc copypart sdz9 sdz1 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "copypart refuses a mounted DESTINATION" \
             || bad "copypart refuses a mounted DESTINATION"

sdc copypart nvme1n1p2 sdz9 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "copypart refuses to read the partition holding /" \
             || bad "copypart refuses to read the partition holding /"

# Short by a byte is not a small copy: it is a filesystem whose superblock
# describes blocks that are not there, and it will mount.
sdc copypart sdz2 sdz9 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "copypart refuses a destination smaller than the source" \
             || bad "copypart refuses a destination smaller than the source"

saysc copypart sdz2 sdz9 -n | grep -q '1.0 MiB'
check "...naming both sizes, so the shortfall is visible" $?

r=$(sdc --rec copypart sdz2 sdz9 -n 2>/dev/null)
echo "$r" | grep -q '^refused	'
check "...and reports it as records under --rec, not on stderr alone" $?

# Copying a partition over ITSELF zeroes it. The comparison is on kernel names
# for that reason: /dev/sdz9 and a by-uuid symlink to it are one device.
sdc copypart sdz9 sdz9 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "copypart refuses to copy a partition onto itself" \
             || bad "copypart refuses to copy a partition onto itself"

# A drive is not a partition. Copying one means copying its table too, which
# gives two disks the same identity — a different operation, not this one.
sdc copypart sdz sdz9 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "copypart refuses a whole drive as the source" \
             || bad "copypart refuses a whole drive as the source"

sdc copypart sdz9 sdz -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "...and as the destination" \
             || bad "...and as the destination"

# The UUID warning is unconditional and blocks NOTHING: copying a partition and
# then wiping the original is exactly what replacing a disk looks like. It is
# here so that a machine booting the wrong one of two identical filesystems is
# not a mystery afterwards.
saysc copypart sdz9 sdz10 -n | grep -q 'UUID'
check "a copy warns that it carries the source's filesystem UUID" $?

r=$(sdc --rec copypart sdz9 sdz10 -n 2>/dev/null)
echo "$r" | grep -q '^warn	'
check "...as a warn record, which leaves a front-end's button live" $?

echo "$r" | grep -q '^blocked	'
[ $? -ne 0 ] && ok "...and not as a blocked one" \
             || bad "...and not as a blocked one"

# A destination with room to spare is allowed, and told the truth: the
# filesystem inside the copy still ends where the source's did.
saysc copypart sdz9 sdz2 -n | grep -q 'still end where'
check "a larger destination is allowed, and says the filesystem does not grow" $?

# fstab is an objection to DESTROYING something and no objection at all to
# reading it. The same file, the same partition, opposite answers by role.
#
# By UUID, because that is what a real fstab holds — and because a fixture has
# no device nodes, so a spec naming /dev/sdz9 resolves to nothing here and
# would have passed this test by matching nobody.
cat > "$T/fstab-sdz9" <<'EOF'
UUID=dddd-0009	/srv	ext4	defaults	0 2
EOF

SYN_DISKS_FSTAB="$T/fstab-sdz9" sdc copypart sdz9 sdz10 -n >/dev/null 2>&1
[ $? -eq 0 ] && ok "an fstab entry does not stop a partition being COPIED" \
             || bad "an fstab entry does not stop a partition being COPIED"

SYN_DISKS_FSTAB="$T/fstab-sdz9" sdc copypart sdz10 sdz9 -n >/dev/null 2>&1
[ $? -eq 1 ] && ok "...and does stop it being overwritten" \
             || bad "...and does stop it being overwritten"

# The one call in this section that runs all the way through, against the fake.
: > "$T/dd.log"
sdc copypart sdz9 sdz10 --yes >/dev/null 2>&1
check "copypart runs when it is allowed to" $?

grep -q '^argv: if=/dev/sdz9 of=/dev/sdz10 bs=4M conv=fsync$' "$T/dd.log"
check "...with exactly the arguments the dry run showed" $?

# ── mktable ─────────────────────────────────────────────────────────────────

sdp mktable sdz --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "mktable refuses a disk with a mounted partition on it" \
             || bad "mktable refuses a disk with a mounted partition on it"

sdp mktable sdz1 --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "mktable refuses a partition — a table belongs to a drive" \
             || bad "mktable refuses a partition — a table belongs to a drive"

sdp mktable sdy --type=zfs --yes >/dev/null 2>&1
[ $? -eq 1 ] && ok "mktable refuses a table type that is not gpt or dos" \
             || bad "mktable refuses a table type that is not gpt or dos"

# ── the script reaches sfdisk on STDIN, never as a command line ─────────────
#
# sdy has nothing mounted, nothing in fstab and no swap, so this is the one
# call in the suite that runs all the way through — against the fake.

: > "$T/sfdisk.log"
sdp mktable sdy --type=gpt --yes >/dev/null 2>&1
check "mktable ALLOWS an idle USB stick" $?

grep -q '^argv: /dev/sdy$' "$T/sfdisk.log"
check "sfdisk is given the device and nothing else on its command line" $?

grep -q '^stdin: label: gpt$' "$T/sfdisk.log"
check "...and the script arrives on stdin, where a shell cannot reach it" $?

# ── a refusal is an ANSWER, not just a message on stderr ────────────────────
#
# Under --rec a refusal has to arrive as RECORDS. It used to go to stderr only,
# which is not the stream the format window's plan parser reads: the window saw
# an empty dry run, greyed its button out and said nothing at all. Worse, the
# one thing the user needed — that unmounting fixes it — was on a SECOND line
# that nothing looked at. Opening a stick from Files mounts it, so "mounted" is
# the state most people arrive in.

r=$("$SD" --rec format sdz1 --fs=ext4 -n 2>/dev/null)

echo "$r" | grep -q '^refused	'
check "a refusal under --rec is reported on STDOUT as records" $?

echo "$r" | grep -q '^fix	unmount$'
check "...with the way out as a field a front-end can switch on" $?

echo "$r" | grep -q '^device	/dev/sdz1$'
check "...naming the device it refused" $?

# The `fix` code is set beside the sentence at each return, never derived from
# it. A GUI matching prose to decide whether to offer an Unmount button would
# stop offering it the day the wording improved.
# ⚠ Captured first, matched second — every one of these EXITS 1, and under
# pipefail `cmd | grep` reports the refusal's status rather than grep's. Three
# of these read FAIL while the program was doing exactly the right thing, which
# is the same trap says() was written for at the top of this file.
r=$("$SD" --rec format nvme1n1p2 --fs=ext4 -n 2>/dev/null)
echo "$r" | grep -q '^fix	none$'
check "a refusal with no way out says so, rather than offering one" $?

r=$(SYN_DISKS_SWAPS="$T/swaps" sdp --rec rmpart sdz2 --yes 2>/dev/null)
echo "$r" | grep -q '^fix	swapoff$'
check "live swap reports its own way out" $?

r=$(SYN_DISKS_FSTAB="$T/fstab" sdp --rec rmpart sdz2 --yes 2>/dev/null)
echo "$r" | grep -q '^fix	fstab$'
check "an fstab entry reports its own way out" $?

# The human form must NOT turn into records — somebody reading a terminal wants
# the sentence and the hint, not a two-column table.
says "$SD" --no-color format sdz1 --fs=ext4 -n | grep -q 'Unmount it first'
check "the human refusal still prints the hint on its second line" $?

says "$SD" --no-color format sdz1 --fs=ext4 -n | grep -q '^field'
[ $? -ne 0 ] && ok "...and does not print records at a person" \
             || bad "...and does not print records at a person"

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

# ⚠ NO BACKSLASH BEFORE THE SLASHES. / is not special in a POSIX basic
# regular expression, so \/ is an undefined escape: GNU grep 3.8 and later print
# "warning: stray \\ before /" for each one, twice per run, into the stderr of
# every build that runs this suite. It matched anyway, which is why it survived.
"$SD" --no-color info dm-0 | grep -q 'physical disk */dev/nvme1n1'
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

    # ── the window and the binary describe the same operations ──────────────
    #
    # Static checks, because a window cannot be started from here: quickshell
    # needs a session, and a test suite that opened one would be a test suite
    # that drew on somebody's screen. What CAN be checked is that the two ends
    # still name the same commands — the failure this catches is a verb renamed
    # in C and left behind in QML, which is a dead button and no build error.
    for verb in mkpart rmpart resize copypart format mktable; do
        grep -q "\"$verb\"" "$QML" \
            || bad "the window never names the $verb command"
    done
    ok "every command the window offers is one the binary has"

    # ── every way out has a sentence at both ends ───────────────────────────
    #
    # A `fix` code is the contract between the guard and the window: the C side
    # sets one beside each refusal, and the window turns it into something to
    # read and — sometimes — a button. A code with no case in fixHint is a
    # dialogue that refuses and explains nothing, which is the exact state this
    # window shipped in for mkpart's missing partition table.
    #
    # guard_print_fix is the canonical list, because it is what the terminal
    # already handles. Nothing here can invent a code the CLI does not know.
    GUARD="$(dirname "$0")/../src/guard.c"
    for f in $(sed -n 's/.*strcmp(fix, "\([a-z]*\)").*/\1/p' "$GUARD"); do
        grep -q "case \"$f\":" "$QML" \
            || bad "the window has no sentence for the '$f' way out"
    done
    ok "every way out the binary names is one the window can explain"

    # ...and it is asked on BOTH sides of the write. A `fix` on a refusal is
    # answered by the dialogue; a `fix` on a FAILED operation is answered by
    # nothing at all unless the outcome handler reads the field too, which is
    # how a write-protected stick got mke2fs's version banner and no hint.
    grep -q 'r.fix' "$QML" \
        && ok "a failed operation is offered the same way out as a refusal" \
        || bad "the outcome handler ignores the fix code on a failure"

    # A close is a REQUEST, and it must not be granted in the middle of a write.
    # `onClosed: Qt.quit()` — which is what this had — tore down the window that
    # was reporting the operation, and used to take the operation with it.
    # ⚠ Asserted STATICALLY and on purpose: the real close path needs a
    # compositor, and the offscreen platform has no window manager to ask.
    # Exercised for real by calling closed() against a headless copy: with busy
    # set the window stays and says so, and with it clear the app still exits.
    grep -A 6 'onClosed: {' "$QML" | grep -q 'root.busy' \
        && ok "a close request is refused while a disk is being written to" \
        || bad "the window still quits on a close request mid-operation"

    # ...and it must still close normally. A window that cannot be closed at all
    # is a worse bug than the one being fixed.
    grep -A 6 'onClosed: {' "$QML" | grep -q 'Qt.quit()' \
        && ok "...and closes as usual when nothing is in flight" \
        || bad "the close handler never quits — the window cannot be closed"

    # ...and while a disk is being written to, the window has to LOOK like it.
    # Greying out four buttons and writing ten grey pixels at the bottom is not
    # a sign that a format is in flight, and somebody closed the window part way
    # through one because nothing said not to.
    grep -q 'visible: root.busy' "$QML" \
        && ok "the window says out loud that a write is in progress" \
        || bad "nothing in the window marks an operation in flight"

    # The bar has to be able to HOLD the answer. It was a fixed 22 pixels with
    # ElideRight, so the reason — however carefully composed — ran off the end
    # of it, and what stayed on screen was the first line of the tool's chatter.
    grep -q 'maximumLineCount' "$QML" \
        && ok "the status bar can hold more than one line of an answer" \
        || bad "the status bar elides everything after the first line"

    # ONE builder for the dry run and the confirm. The dialogue's whole claim is
    # that what it shows is what it runs, and that claim is only true while both
    # halves come from the same array.
    n=$(grep -c 'dlgArgs()' "$QML" || true)
    [ "$n" -ge 3 ] && ok "the dialogue builds its argv in one place" \
                   || bad "dlgArgs() is used $n time(s) — the plan and the confirm have drifted"

    # The gap is named by OFFSET. An index into the list would be stale the
    # moment anything else on the drive changed, and would name free space
    # nobody looked at.
    grep -qF -- '--start=" + root.selGap' "$QML" \
        && ok "a new partition names the free space it was pointed at" \
        || bad "the window does not pass --start"

    # A plan that arrives after the question changed is DROPPED. Probed
    # headlessly against a stub: without this the copy dialogue opened showing
    # `rmpart` as the command it was about to run, because the abandoned
    # dialogue's dry run finished afterwards.
    grep -q 'planAsked !== root.planStamp()' "$QML" \
        && ok "a stale dry run cannot land in the dialogue that replaced it" \
        || bad "nothing stamps the plan against the request it answers"
else
    bad "syn-disks.qml not found beside the tests: $QML"
fi

# ── the wallpaper's accent reaches this window ──────────────────────────────
#
# 387 gave the BAR the colour synui measures off the wallpaper, and only the
# bar: every app window beside it kept the preset's accent, so a desktop with
# the switch on wore two colours at once — the picture's on the bar, the
# theme's on Files, Software, Disks and the rest. These windows read
# ~/.config/synui/palette.state now, and this is the check that they do.
#
# ⚠ IT LOADS THE FILE IN A REAL ENGINE rather than grepping for the property.
# A duplicate property name is the trap this feature has sprung before: the
# file PARSES, qmllint is happy, and the type then refuses to LOAD, naming a
# line that is not the one at fault. Only running it can tell.
#
# Three cases, because two of them are the ones already got wrong once:
#   use=yes  the MEASURED colour;
#   use=no   the theme's own, because `use` is the SETTING and synui writes
#            the file whichever way it is set — reading the colour without
#            checking it is how the bar came to wear a wallpaper on themes
#            that never asked for one (386);
#   ok=no    the theme's own, the picture having no usable hue to give.
if [ -f "$QML" ] && command -v quickshell >/dev/null 2>&1; then
    WPT=$(mktemp -d)
    mkdir -p "$WPT/home/.config/synui" "$WPT/run"
    # A preset accent that is nothing like the measured one, so "it took the
    # wallpaper's" and "it kept the theme's" cannot be confused for each other.
    cat > "$WPT/home/.config/synui/theme.json" <<'WPJSON'
{ "scheme": "dark", "accent": [0,214,229], "glyph": [0,214,229],
  "bar": [25,28,35], "popup": [17,21,28], "fg": "#c8e3ee" }
WPJSON
    # A COPY with a probe timer appended INSIDE the root object — outside its
    # final brace the file is a syntax error and this would "fail" on a QML
    # that is perfectly good.
    awk 'BEGIN{RS="\0"} {
            n = match($0, /}[ \t\r\n]*$/)
            printf "%s\n    Timer { running: true; interval: 1200; repeat: false;\n             onTriggered: { console.log(\"WPACCENT=\" + root.cAccent); Qt.quit() } }\n%s", substr($0,1,n-1), substr($0,n)
         }' "$QML" > "$WPT/probe.qml"
    # ⚠ QT_ASSUME_STDERR_HAS_CONSOLE=1, or console.log() prints NOTHING at all
    # and every case below reads as an empty accent — a green suite that tested
    # the engine's silence. GSETTINGS_BACKEND=memory because the fake HOME has
    # no dconf for Qt's platform theme to find.
    wp_accent() {  # wp_accent <use> <ok> -> the colour the window resolves
        printf 'use=%s\nok=%s\naccent=#6479FF\naccent_dim=#37438C\nsecondary=#C68F14\n' \
               "$1" "$2" > "$WPT/home/.config/synui/palette.state"
        HOME="$WPT/home" XDG_RUNTIME_DIR="$WPT/run" QT_QPA_PLATFORM=offscreen \
        GSETTINGS_BACKEND=memory QT_ASSUME_STDERR_HAS_CONSOLE=1 \
        timeout 30 quickshell -p "$WPT/probe.qml" 2>&1 |
            sed -n 's/.*WPACCENT=\(#[0-9a-fA-F]*\).*/\1/p' | head -1
    }
    [ "$(wp_accent yes yes)" = "#6479ff" ] \
        && ok "the measured wallpaper accent reaches the window" \
        || bad "the window ignores palette.state and stays on the preset accent"
    [ "$(wp_accent no yes)" = "#00d6e5" ] \
        && ok "wallpaper_accent off leaves the theme's accent alone" \
        || bad "the window wears the wallpaper with use=no in palette.state"
    [ "$(wp_accent yes no)" = "#00d6e5" ] \
        && ok "a wallpaper with no usable hue falls back to the theme" \
        || bad "the window took a colour out of a palette.state saying ok=no"
    rm -rf "$WPT"
else
    echo "  skip  quickshell not installed, cannot check the wallpaper accent"
fi

# ── done ────────────────────────────────────────────────────────────────────

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
