#!/usr/bin/env bash
# run-vm-bpf-tests.sh — synguard's BPF-LSM suites, as root, in a VM.
#
# bpf_failsafe_test, bpf_policy_test and bpf_report_test need root and a
# kernel with BPF-LSM active, so under `meson test` on a desktop they SKIP.
# This boots a stock kernel under qemu with an initramfs holding the three
# test binaries, the shared libraries they link, and tests/vm_init.c as /init,
# and runs them there. Nothing needs root and nothing touches the running
# system — the same approach as synapse_kmod/tests/run-vm-tests.sh.
#
# Usage:
#   tests/run-vm-bpf-tests.sh                      # newest installed kernel with BTF
#   KVER=7.2.6-arch2-1 tests/run-vm-bpf-tests.sh
#   BUILD=/path/to/meson/builddir tests/run-vm-bpf-tests.sh   # reuse a build
#
# Needs: qemu-system-x86_64, gcc with a static libc, cpio, meson, and a kernel
# at /usr/lib/modules/$KVER/vmlinuz with CONFIG_BPF_LSM and BTF. /dev/kvm makes
# it take seconds.
#
# Exit: 0 when every suite passed (a SKIP counts as a failure here — in this VM
# nothing should need to skip), 1 otherwise, 2 on a setup error.
#
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/.." && pwd)
for t in qemu-system-x86_64 gcc cpio meson ldd; do
    command -v "$t" >/dev/null || { echo "run-vm-bpf-tests: needs $t" >&2; exit 2; }
done

kver=${KVER:-}
if [ -z "$kver" ]; then
    for d in $(ls -1d /usr/lib/modules/*/ 2>/dev/null | sort -rV); do
        k=$(basename "$d")
        [ -f "$d/vmlinuz" ] && [ -f "$d/build/.config" ] &&
            grep -q '^CONFIG_BPF_LSM=y' "$d/build/.config" && { kver=$k; break; }
    done
fi
[ -n "$kver" ] && [ -f "/usr/lib/modules/$kver/vmlinuz" ] ||
    { echo "run-vm-bpf-tests: no kernel with BPF-LSM found (set KVER)" >&2; exit 2; }

work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
build=${BUILD:-$work/build}
if [ -z "${BUILD:-}" ]; then
    meson setup "$build" "$src" -Dbpf_lsm=enabled >"$work/setup.log" 2>&1 ||
        { cat "$work/setup.log" >&2; exit 2; }
fi
meson compile -C "$build" bpf_failsafe_test bpf_policy_test bpf_report_test \
    >"$work/compile.log" 2>&1 || { tail -30 "$work/compile.log" >&2; exit 2; }

gcc -static -O1 -Wall -o "$work/init" "$here/vm_init.c"

root="$work/root"
mkdir -p "$root"/{proc,sys,dev,tmp,etc,t,usr/lib,usr/bin,lib64}
ln -s usr/lib "$root/lib"
ln -s usr/bin "$root/bin"
cp "$work/init" "$root/init"
# bpf_failsafe_test writes its stand-in kernel cmdline into the BUILD directory
# (the path is compiled in), so that path has to exist in here too.
mkdir -p "$root$build"

# Every shared library a binary links, and the dynamic loader at the path the
# binaries name for it.
libs_of() {
    ldd "$1" | awk '/=> \//{print $3} /^\s*\/lib64\/ld-linux/{print $1}' |
    while read -r lib; do
        case "$lib" in
            */ld-linux-x86-64.so.2) cp -L "$lib" "$root/lib64/ld-linux-x86-64.so.2" ;;
            *) cp -L -n "$lib" "$root/usr/lib/" ;;
        esac
    done
}

: >"$root/tests.list"
for t in bpf_failsafe_test bpf_policy_test bpf_report_test; do
    cp "$build/$t" "$root/t/$t"
    echo "/t/$t" >>"$root/tests.list"
    libs_of "$build/$t"
done
# bpf_policy_test stages an exec target with system("cp /usr/bin/true …").
for b in sh cp true; do
    p=$(type -P "$b") && p=$(readlink -f "$p") || { echo "run-vm-bpf-tests: needs $b" >&2; exit 2; }
    cp -L "$p" "$root/usr/bin/$b"
    libs_of "$p"
done
(cd "$root" && find . | cpio -o -H newc -R 0:0 --quiet) >"$work/initramfs.cpio"

accel=(-machine accel=tcg)
[[ -w /dev/kvm ]] && accel=(-enable-kvm -cpu host)

log="$work/console.log" res="$work/results.log"
: >"$log"; : >"$res"
echo "run-vm-bpf-tests: booting $kver"
timeout 600 qemu-system-x86_64 "${accel[@]}" -m 1024 -smp 2 \
    -display none -no-reboot -nodefaults \
    -serial "file:$log" -serial "file:$res" \
    -kernel "/usr/lib/modules/$kver/vmlinuz" -initrd "$work/initramfs.cpio" \
    -append "console=ttyS0 panic=-1 loglevel=4 lsm=landlock,lockdown,yama,integrity,bpf" \
    >/dev/null 2>&1 || true

tr -d '\r' <"$res" | sed 's/^/  /'
status=0
tr -d '\r' <"$res" | grep -q '^SGVM-END failures=0' || status=1
tr -d '\r' <"$res" | grep -qE '^SGVM-EXIT .* 77$' && { echo "  a suite SKIPPED in the VM"; status=1; }
if grep -E 'BUG:|Oops|Call Trace|general protection|Kernel panic' "$log" >/dev/null; then
    status=1
    echo "  kernel:"
    grep -E -A4 'BUG:|Oops|general protection|Kernel panic' "$log" | head -16 | sed 's/^/    /'
fi
[ "$status" = 0 ] && echo "run-vm-bpf-tests: ok" || echo "run-vm-bpf-tests: FAILED"
exit "$status"
