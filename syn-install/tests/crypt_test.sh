#!/usr/bin/env bash
# crypt_test.sh — syn-crypt's device resolution
#
# syn-crypt picks the block device that LUKS operations are aimed at. Getting
# that wrong is not a cosmetic failure: luksRemoveKey or luksChangeKey on the
# wrong device damages a disk, and resolving to something that does not exist
# breaks every command that takes no explicit DEVICE — which, per the
# installer's closing screen, is how all of them are documented to be run.
#
# It cannot be tested against a real LUKS volume here (that needs root and a
# loop device), so findmnt and lsblk are faked on PATH and the pure resolution
# function is driven directly through SYN_CRYPT_SOURCE_ONLY=1.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

# ── Fakes ────────────────────────────────────────────────
# lsblk reproduces the real thing's two behaviours that matter: `-s` DRAWS A
# TREE (with box-drawing characters, and it does so even when stdout is a pipe),
# and `-r` turns that off. That tree is the bug this file exists for, so the fake
# has to render it or the test proves nothing.
fake=$(mktemp -d)
trap 'rm -rf "$fake"' EXIT
mkdir -p "$fake/bin"

# findmnt appends the subvolume as "[/@]" on a btrfs mount unless --nofsroot is
# passed. The fake reproduces exactly that, because it is the second bug this
# file exists for: without the flag the caller gets a device string nothing can
# resolve, and an encrypted btrfs root reports as NOT ENCRYPTED.
cat > "$fake/bin/findmnt" <<'EOF'
#!/usr/bin/env bash
src="${FAKE_ROOT_SRC:-}"
case " $* " in
    *" --nofsroot "*|*" -v "*) ;;
    *) [ -n "$src" ] && src="$src${FAKE_FSROOT:-}" ;;
esac
printf '%s\n' "$src"
EOF

cat > "$fake/bin/lsblk" <<'EOF'
#!/usr/bin/env bash
raw=0; nodeps=0; want=""
for a in "$@"; do
    case "$a" in
        -*r*) raw=1 ;;&
        -*d*) nodeps=1 ;;&
        /dev/*) want="$a" ;;
    esac
done
case " $* " in *" FSTYPE "*) fstype=1 ;; *) fstype=0 ;; esac
case " $* " in *" UUID "*) uuid=1 ;; *) uuid=0 ;; esac

# Real lsblk cannot resolve a name carrying findmnt's "[/@]" fsroot suffix:
# "lsblk: /dev/mapper/cryptroot[/@]: No such file or directory", rc 32, no
# output. The fake must do the same or the btrfs checks pass vacuously — the
# whole point there is that the unstripped name reaches lsblk and dies.
case "$want" in
    *'['*|'') printf 'lsblk: %s: No such file or directory\n' "$want" >&2; exit 32 ;;
esac

if [ "$nodeps" = 1 ] && [ "$fstype" = 1 ]; then
    printf '%s\n' "${FAKE_PARENT_FSTYPE-crypto_LUKS}"; exit 0
fi
if [ "$nodeps" = 1 ] && [ "$uuid" = 1 ]; then
    printf '%s\n' "fake-uuid-1234"; exit 0
fi
# -s: the device, then its parents. Tree-drawn unless -r.
printf '%s\n' "${want##*/}"
if [ "$raw" = 1 ]; then
    printf '%s\n' "${FAKE_PARENT:-sda2}"
else
    printf '└─%s\n' "${FAKE_PARENT:-sda2}"
fi
EOF
chmod 755 "$fake/bin/findmnt" "$fake/bin/lsblk"
PATH="$fake/bin:$PATH"

export SYN_CRYPT_SOURCE_ONLY=1
# shellcheck source=/dev/null
. "$here/../syn-crypt.sh"

echo "=== root_luks_device ==="

# The regression. lsblk -s draws "└─sda2"; `tr -d ' '` does not remove U+2514 or
# U+2500, so the old parse produced "/dev/└─sda2" — not a block device, so every
# command that takes no explicit DEVICE died on it, and status printed it as the
# container. -r is what makes line 2 a bare name.
FAKE_ROOT_SRC="/dev/mapper/cryptroot"
export FAKE_ROOT_SRC
check "an encrypted root resolves to the backing partition" "/dev/sda2" \
    "$(root_luks_device)"
check "no box-drawing character survives into the device path" "no" \
    "$(case "$(root_luks_device)" in *[!/a-zA-Z0-9-]*) echo yes ;; *) echo no ;; esac)"
check "the resolved device is a plausible /dev path" "yes" \
    "$(case "$(root_luks_device)" in /dev/[a-z]*) echo yes ;; *) echo no ;; esac)"

FAKE_PARENT="nvme0n1p3"; export FAKE_PARENT
check "nvme naming survives resolution" "/dev/nvme0n1p3" "$(root_luks_device)"
unset FAKE_PARENT

# THE BTRFS CASE. findmnt reports a btrfs mount's source as
# "/dev/mapper/cryptroot[/@]", and "@" is the installer's own root subvolume —
# so this is not an exotic layout, it is what every encrypted install with
# snapshots gets. Passing that string on unresolved made status report a
# genuinely LUKS-encrypted disk as NOT ENCRYPTED, and made every key operation
# refuse with "the root filesystem is not encrypted".
FAKE_FSROOT="[/@]"; export FAKE_FSROOT
check "an encrypted BTRFS root still resolves (subvolume suffix stripped)" "/dev/sda2" \
    "$(root_luks_device)"
check "an encrypted BTRFS root is not reported unencrypted" "0" \
    "$( (root_luks_device) >/dev/null 2>&1; echo $? )"
# Bind mounts get the same "[/dir]" treatment from findmnt.
FAKE_FSROOT="[/some/dir]"
check "a bind-mounted root resolves too" "/dev/sda2" "$(root_luks_device)"
FAKE_FSROOT=""

# A mapper node is not proof of encryption: an LVM logical volume is one too, and
# root_luks_device's success is what cmd_status prints ENCRYPTED from. Reporting
# an unencrypted LVM root as encrypted is the kind of wrong answer this tool's
# header says it refuses to give.
FAKE_PARENT_FSTYPE="LVM2_member"; export FAKE_PARENT_FSTYPE
check "an LVM root is NOT reported as encrypted" "1" \
    "$(root_luks_device >/dev/null 2>&1; echo $?)"
FAKE_PARENT_FSTYPE="crypto_LUKS"

# A root that is not a mapper node at all is simply unencrypted.
FAKE_ROOT_SRC="/dev/sda2"
check "a plain root is not encrypted" "1" \
    "$(root_luks_device >/dev/null 2>&1; echo $?)"

# An empty mount table must not resolve to /dev/ or to the empty string. This one
# `die`s rather than returning, so it needs its own subshell — otherwise the exit
# takes the $( ) with it and `echo $?` never runs.
FAKE_ROOT_SRC=""
check "an unknown root device does not resolve" "1" \
    "$( (root_luks_device) >/dev/null 2>&1; echo $? )"
check "and it says so rather than printing a device" "" \
    "$( (root_luks_device) 2>/dev/null )"

echo
if [ "$fails" -gt 0 ]; then
    echo "$fails check(s) FAILED"
    exit 1
fi
echo "all checks passed"
