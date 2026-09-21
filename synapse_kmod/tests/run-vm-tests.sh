#!/usr/bin/env bash
# run-vm-tests.sh — boot a stock kernel under qemu with synapse_kmod and
# tests/kmod_vm_test.c as /init, once per mode, and report.
#
# Nothing here needs root and nothing touches the running system: the module
# is loaded, fuzzed and unloaded inside a throwaway VM whose only disk is an
# initramfs built in a temp directory. That is the point. Several of these
# cases crash an unfixed module — a zero-size ring divides by zero in a kprobe,
# and an unload after a runtime parameter flip leaves a device pointing into
# freed code — and a kernel module's bugs are the machine's bugs.
#
# Usage:
#   tests/run-vm-tests.sh                     # all modes, newest usable kernel
#   tests/run-vm-tests.sh main drop           # just these modes
#   KVER=7.2.6-arch2-1 tests/run-vm-tests.sh  # a specific installed kernel
#   KMOD_SRC=/path/to/synapse_kmod tests/run-vm-tests.sh   # test another tree
#
# Needs: qemu-system-x86_64, a kernel with both /usr/lib/modules/$KVER/vmlinuz
# and its build/ headers (the stock `linux` + `linux-headers` packages), gcc
# with a static libc, and cpio. /dev/kvm makes it take seconds; without it
# qemu falls back to TCG and it takes minutes.
#
# Exit status: 0 when every mode ends with KFT-END failures=0 and the kernel
# logged no BUG/Oops/Call Trace; 1 otherwise. GAP lines are documented limits
# and do not fail the run.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

here=$(cd "$(dirname "$(realpath "$0")")" && pwd)
src=${KMOD_SRC:-$(dirname "$here")}
modes=("$@")
[[ ${#modes[@]} -gt 0 ]] || modes=(main drop ring0 unload)

# A kernel we can both build against and boot.
pick_kernel() {
    local v
    for v in "${KVER:-}" "$(uname -r)" $(ls -1 /usr/lib/modules | sort -rV); do
        [[ -n $v && -r /usr/lib/modules/$v/vmlinuz && -d /usr/lib/modules/$v/build ]] \
            && { echo "$v"; return 0; }
    done
    return 1
}
kver=$(pick_kernel) || { echo "run-vm-tests: no kernel with both vmlinuz and build/ headers" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/kmod-vm.XXXXXX")
trap 'rm -rf "$work"' EXIT

echo "run-vm-tests: kernel $kver, module from $src"

# Build in a copy, so the tree is left exactly as it was.
mkdir -p "$work/kmod"
cp -r "$src/Makefile" "$src/src" "$src/include" "$work/kmod/"
find "$work/kmod" \( -name '*.o' -o -name '*.cmd' -o -name '*.ko' -o -name '*.mod*' \) -delete
make -s -C "/usr/lib/modules/$kver/build" M="$work/kmod" modules >"$work/build.log" 2>&1 \
    || { cat "$work/build.log" >&2; exit 2; }
if grep -E 'warning:' "$work/build.log" | grep -v 'pahole version' >&2; then
    echo "run-vm-tests: the module built with warnings (above)" >&2
fi

gcc -static -O1 -Wall -o "$work/init" "$here/kmod_vm_test.c"

root="$work/root"
mkdir -p "$root"/{proc,sys,dev,tmp,etc}
cp "$work/init" "$root/init"
cp "$work/kmod/synapse_kmod.ko" "$root/synapse_kmod.ko"
(cd "$root" && find . | cpio -o -H newc -R 0:0 --quiet) > "$work/initramfs.cpio"

accel=(-machine accel=tcg)
[[ -w /dev/kvm ]] && accel=(-enable-kvm -cpu host)

status=0
for mode in "${modes[@]}"; do
    log="$work/$mode.log" res="$work/$mode.results"
    : >"$log"; : >"$res"
    # ttyS0 is the kernel's console, ttyS1 the test's results — two files, so
    # a printk can never split a result line.
    timeout 180 qemu-system-x86_64 "${accel[@]}" -m 512 -smp 2 \
        -display none -no-reboot -nodefaults \
        -serial "file:$log" -serial "file:$res" \
        -kernel "/usr/lib/modules/$kver/vmlinuz" -initrd "$work/initramfs.cpio" \
        -append "console=ttyS0 panic=-1 loglevel=${LOGLEVEL:-5} kft=$mode" \
        >/dev/null 2>&1 || true

    echo
    echo "── $mode"
    tr -d '\r' <"$res" | grep -E '^(PASS|FAIL|GAP|INFO) ' | sed 's/^/  /' || true

    ok=1
    tr -d '\r' <"$res" | grep -q '^KFT-END failures=0' || ok=0
    if grep -E 'BUG:|Oops|Call Trace|divide error|general protection|Kernel panic' "$log" >/dev/null; then
        ok=0
        echo "  kernel:"
        grep -E -A3 'BUG:|Oops|divide error|general protection|Kernel panic' "$log" | head -12 | sed 's/^/    /'
    fi
    if (( ok )); then
        echo "  → $mode: ok"
    else
        tr -d '\r' <"$res" | grep -q '^KFT-END' || echo "  → $mode: the VM never reached KFT-END"
        echo "  → $mode: FAILED"
        status=1
        [[ -n ${KEEP_LOGS:-} ]] && cp "$log" "$res" "$KEEP_LOGS/"
    fi
done

exit $status
