#!/bin/bash
# syn-boot-seed — does the boot loader that started this system read a random
# seed from the ESP? The ExecCondition of systemd-boot-random-seed.service
# (syn-boot-seed.conf): exit 0 runs `bootctl random-seed`, exit 1 skips it.
#
# The unit's own conditions ask only whether LoaderFeatures or StubFeatures
# EXISTS, which assumes any loader that sets them is systemd-boot. limine sets
# LoaderFeatures too (12.9.3: 0x200f — timeouts, default and one-shot entries,
# menu-disable) without the random-seed bit, and never opens
# /boot/loader/random-seed. So on a limine install bootctl wrote a seed nothing
# reads, every boot, and logged two "world accessible, which is a security
# hole" errors about it.
#
# The bits are systemd's (src/fundamental/efivars.h, v262):
#   LoaderFeatures  EFI_LOADER_FEATURE_RANDOM_SEED  1 << 6
#   StubFeatures    EFI_STUB_FEATURE_RANDOM_SEED    1 << 4
# An efivarfs file is 4 attribute bytes, then the little-endian uint64.
#
# Unsure is yes: a variable that exists but cannot be read leaves the unit to
# run as upstream wrote it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

vars=${SYN_EFIVARS:-/sys/firmware/efi/efivars}
guid=4a67b082-0a4c-41cf-b6c7-440b29bb8c4f

# feature <variable> <bit> — 0: set, 1: clear or absent, 2: present but unreadable
feature() {
    local f="$vars/$1-$guid" b
    [ -e "$f" ] || return 1
    b=$(od -An -tu1 -j $((4 + $2 / 8)) -N1 "$f" 2>/dev/null | tr -d ' \n')
    [ -n "$b" ] || return 2
    (( (b >> ($2 % 8)) & 1 )) && return 0
    return 1
}

feature LoaderFeatures 6; l=$?
feature StubFeatures 4;   s=$?
if [ "$l" -ne 1 ] || [ "$s" -ne 1 ]; then
    exit 0
fi
echo "the boot loader does not read a random seed from the ESP; not writing one"
exit 1
