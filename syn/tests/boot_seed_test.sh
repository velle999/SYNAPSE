#!/usr/bin/env bash
# boot_seed_test.sh — the ESP random seed is written only for a loader that
# reads one.
#
# syn-boot-seed is the ExecCondition syn adds to systemd's
# systemd-boot-random-seed.service. Two halves:
#
#   the decision — fake efivarfs files (4 attribute bytes, then a little-endian
#     uint64) for limine, systemd-boot and a UKI booted by systemd-stub. limine
#     12.9.3's real LoaderFeatures (0x200f) must skip; a loader with the
#     random-seed bit must run.
#   the wiring — the drop-in is only worth anything if systemd applies it and
#     the path it names is where package() installs the script. A fake root
#     with the host's real unit, the drop-in and the script goes through
#     `systemd-analyze verify --root`, which reports a command it cannot find;
#     without the script, the same verify must fail.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
syn="$here/.."
script="$syn/syn-boot-seed.sh"
dropin="$syn/syn-boot-seed.conf"
pkgb="$syn/PKGBUILD"
guid=4a67b082-0a4c-41cf-b6c7-440b29bb8c4f

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# efivar <dir> <name> <uint64> — write one efivarfs file
efivar() {
    local v=$3 i bytes='\x06\x00\x00\x00'
    for i in 0 1 2 3 4 5 6 7; do
        bytes+=$(printf '\\x%02x' $(( (v >> (8 * i)) & 0xff )))
    done
    printf "$bytes" > "$1/$2-$guid"
}

# decide <dir> — the script's exit status against that efivars dir
decide() { SYN_EFIVARS=$1 bash "$script" >/dev/null 2>&1; echo $?; }

echo "=== files ==="
check "syn-boot-seed.sh exists" yes "$([ -f "$script" ] && echo yes || echo no)"
check "the script parses" yes "$(bash -n "$script" 2>/dev/null && echo yes || echo no)"
check "the drop-in exists" yes "$([ -f "$dropin" ] && echo yes || echo no)"

echo "=== the decision ==="
d=$tmp/limine; mkdir -p "$d"; efivar "$d" LoaderFeatures $((0x200f))
check "limine 12.9.3 (LoaderFeatures 0x200f, no random-seed bit): skip" 1 "$(decide "$d")"

d=$tmp/sdboot; mkdir -p "$d"; efivar "$d" LoaderFeatures $((0x200f | 1 << 6))
check "a loader with EFI_LOADER_FEATURE_RANDOM_SEED (1<<6): run" 0 "$(decide "$d")"

d=$tmp/sdboot-only-seed; mkdir -p "$d"; efivar "$d" LoaderFeatures $((1 << 6))
check "…the bit alone is enough" 0 "$(decide "$d")"

d=$tmp/stub; mkdir -p "$d"; efivar "$d" StubFeatures $((1 << 4 | 1))
check "a UKI started by systemd-stub with EFI_STUB_FEATURE_RANDOM_SEED (1<<4): run" 0 "$(decide "$d")"

d=$tmp/stub-noseed; mkdir -p "$d"; efivar "$d" StubFeatures $((0x7 | 1 << 6))
check "a stub without the random-seed bit: skip" 1 "$(decide "$d")"

d=$tmp/limine-stub; mkdir -p "$d"
efivar "$d" LoaderFeatures $((0x200f)); efivar "$d" StubFeatures $((1 << 4))
check "limine starting a stub that reads the seed: run" 0 "$(decide "$d")"

d=$tmp/short; mkdir -p "$d"; printf '\x06\x00\x00\x00' > "$d/LoaderFeatures-$guid"
check "a variable that cannot be read: run, as upstream would" 0 "$(decide "$d")"

d=$tmp/none; mkdir -p "$d"
check "neither variable: skip" 1 "$(decide "$d")"

echo "=== the wiring ==="
# The drop-in names an absolute path; package() is the only thing that puts the
# script there.
named=$(sed -n 's/^ExecCondition=//p' "$dropin")
check "the drop-in runs /usr/lib/syn/syn-boot-seed" /usr/lib/syn/syn-boot-seed "$named"
check "package() installs the script at that path" yes \
    "$(grep -qF 'syn-boot-seed.sh" "$pkgdir/usr/lib/syn/syn-boot-seed"' "$pkgb" && echo yes || echo no)"
check "package() installs the drop-in beside systemd's unit" yes \
    "$(grep -qF '"$pkgdir/usr/lib/systemd/system/systemd-boot-random-seed.service.d/syn-boot-seed.conf"' "$pkgb" && echo yes || echo no)"
for f in syn-boot-seed.sh syn-boot-seed.conf; do
    check "$f is in source=()" yes "$(grep -q "^ *\"$f\"" "$pkgb" && echo yes || echo no)"
done

unit=/usr/lib/systemd/system/systemd-boot-random-seed.service
if command -v systemd-analyze >/dev/null && [ -f "$unit" ]; then
    r=$tmp/root
    mkdir -p "$r/usr/lib/systemd/system/systemd-boot-random-seed.service.d" \
             "$r/usr/lib/syn" "$r/usr/bin"
    cp "$unit" "$r/usr/lib/systemd/system/"
    cp "$dropin" "$r/usr/lib/systemd/system/systemd-boot-random-seed.service.d/syn-boot-seed.conf"
    printf '#!/bin/sh\n' > "$r/usr/bin/bootctl"; chmod 755 "$r/usr/bin/bootctl"
    install -m755 "$script" "$r/usr/lib/syn/syn-boot-seed"
    out=$(systemd-analyze verify --man=no --root="$r" systemd-boot-random-seed.service 2>&1)
    check "systemd-analyze verify passes with the script installed" "" "$out"
    rm "$r/usr/lib/syn/syn-boot-seed"
    out=$(systemd-analyze verify --man=no --root="$r" systemd-boot-random-seed.service 2>&1)
    check "…and fails without it, so systemd does read the drop-in" yes \
        "$(grep -q '/usr/lib/syn/syn-boot-seed is not executable' <<<"$out" && echo yes || echo no)"
else
    echo "  skip  systemd-analyze or $unit not on this box: the wiring was not verified"
fi

echo
if [ "$fails" -eq 0 ]; then echo "all passed"; exit 0; fi
echo "$fails FAILED"; exit 1
