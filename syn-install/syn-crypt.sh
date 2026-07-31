#!/usr/bin/env bash
#
# syn-crypt — inspect and manage the encryption on an installed SynapseOS.
#
# Deliberately a CLI tool and not a synui panel. Changing a LUKS passphrase
# means holding the OLD and NEW secret in memory and handing them to
# cryptsetup; doing that inside the compositor would put both in the address
# space of a long-lived process that also renders the screen, talks to an AI
# daemon and keeps a control socket open. A short-lived setuid-free script the
# user runs under sudo is a far smaller thing to get right, and it is
# auditable in one screen.
#
# Everything here refuses to guess. A LUKS operation on the wrong device
# destroys a disk, so the device is either given explicitly or resolved from
# the actual mounted root — never from a pattern match on device names.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -euo pipefail

die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
ok()   { printf '\033[1;32m✓\033[0m %s\n' "$*"; }

command -v cryptsetup >/dev/null || die "cryptsetup is not installed"

usage() {
    cat <<'EOF'
syn-crypt — manage SynapseOS disk encryption

  syn-crypt status              show whether the root filesystem is encrypted
  syn-crypt add-key [DEVICE]    add another passphrase (up to the keyslot limit)
  syn-crypt change-key [DEVICE] replace an existing passphrase
  syn-crypt remove-key [DEVICE] remove a passphrase (never the last one)
  syn-crypt backup-header FILE [DEVICE]
                                save the LUKS header — without it, a corrupted
                                header means the data is gone even with the
                                right passphrase

DEVICE defaults to the LUKS container backing the mounted root filesystem.
EOF
}

#
# Which block device holds the LUKS container for "/"?
#
# Resolved from the mount table, not by scanning for LUKS volumes: a machine
# can have several, and picking the wrong one here is how you wipe the keyslot
# of a backup drive. If / is not on a mapper node, it is not encrypted and we
# say so rather than offering to operate on something.
#
root_luks_device() {
    local src parent
    # --nofsroot is not a refinement, it is the difference between this tool
    # working and not. findmnt appends the subvolume as "[/@]" on a btrfs mount
    # (and "[/dir]" on a bind mount), so plain SOURCE returns
    # "/dev/mapper/cryptroot[/@]" — a string no lsblk, blkid or cryptsetup can
    # resolve. On an encrypted BTRFS root, which is the installer's own layout
    # whenever snapshots are chosen, resolution failed and `status` therefore
    # reported a genuinely LUKS-encrypted disk as NOT ENCRYPTED, while every
    # key operation refused with "the root filesystem is not encrypted — pass a
    # device explicitly". Verified on this machine: root is /dev/mapper/cryptroot
    # on /dev/nvme1n1p2 (crypto_LUKS), and status said NOT ENCRYPTED.
    src="$(findmnt -no SOURCE --nofsroot / 2>/dev/null || true)"
    [ -n "$src" ] || die "could not determine the root device"

    case "$src" in
        /dev/mapper/*) ;;
        *) return 1 ;;
    esac

    # The mapper node's single slave is the underlying partition.
    #
    # -r is load-bearing. `lsblk -s` renders a TREE, and it does so whether or
    # not stdout is a terminal, so line 2 came back as "└─nvme0n1p2" — and the
    # `tr -d ' '` this used to rely on strips ASCII spaces, not the U+2514/U+2500
    # box-drawing characters. Every default-device operation then resolved to
    # "/dev/└─nvme0n1p2": status printed that as the container with a blank UUID,
    # and add-key/change-key/remove-key/backup-header all died on "is not a block
    # device". Since DEVICE is optional and the installer's closing screen tells
    # people to run all four WITHOUT one, that was the whole tool on an encrypted
    # install. -r prints one raw name per line instead.
    parent="$(lsblk -nsro NAME "$src" 2>/dev/null | sed -n '2p')"
    [ -n "$parent" ] || return 1
    parent="/dev/$parent"

    # A mapper node is not by itself proof of encryption — an LVM logical volume
    # is one too, and this function's answer is what `status` prints ENCRYPTED
    # from. Ask what the parent actually holds rather than inferring it from the
    # name. lsblk reads udev's database, so this still works without root, which
    # `blkid` and `cryptsetup isLuks` do not.
    [ "$(lsblk -dno FSTYPE "$parent" 2>/dev/null)" = "crypto_LUKS" ] || return 1

    printf '%s' "$parent"
}

# Resolve the device an operation should act on: an explicit argument if given,
# otherwise the root container. Verified to actually be LUKS before returning,
# so no caller can hand cryptsetup a plain partition by accident.
resolve_device() {
    local dev="${1:-}"
    if [ -z "$dev" ]; then
        dev="$(root_luks_device)" \
            || die "the root filesystem is not encrypted — pass a device explicitly"
    fi
    [ -b "$dev" ] || die "$dev is not a block device"
    cryptsetup isLuks "$dev" || die "$dev is not a LUKS volume"
    printf '%s' "$dev"
}

need_root() {
    [ "$(id -u)" -eq 0 ] || die "this needs root — try: sudo syn-crypt $*"
}

cmd_status() {
    local dev
    if ! dev="$(root_luks_device 2>/dev/null)"; then
        printf 'Root filesystem: \033[1;33mNOT ENCRYPTED\033[0m\n'
        printf '  %s\n' "$(findmnt -no SOURCE,FSTYPE / 2>/dev/null || true)"
        echo ""
        echo "Encryption can only be set up at install time — it is not something"
        echo "that can be turned on in place without reformatting."
        return 0
    fi

    printf 'Root filesystem: \033[1;32mENCRYPTED\033[0m\n'
    printf '  container : %s\n' "$dev"
    printf '  mapper    : %s\n' "$(findmnt -no SOURCE --nofsroot /)"
    # lsblk, not blkid: blkid has to read the device itself and returns nothing
    # at all to a non-root user, so this line printed an empty UUID in exactly
    # the case the "(run as root for … detail)" branch below says is supported.
    printf '  UUID      : %s\n' "$(lsblk -dno UUID "$dev" 2>/dev/null)"

    # luksDump needs root; without it, report what we can rather than failing.
    if [ "$(id -u)" -eq 0 ]; then
        printf '  version   : %s\n' \
            "$(cryptsetup luksDump "$dev" | awk -F': *' '/^Version/{print $2; exit}')"
        printf '  cipher    : %s\n' \
            "$(cryptsetup luksDump "$dev" | awk -F': *' '/^\s*cipher:/{print $2; exit}')"
        printf '  KDF       : %s\n' \
            "$(cryptsetup luksDump "$dev" | awk '/PBKDF:/{print $2; exit}')"
        printf '  keyslots  : %s in use\n' \
            "$(cryptsetup luksDump "$dev" | grep -cE '^\s+[0-9]+: luks2' || true)"
    else
        echo "  (run as root for cipher/KDF/keyslot detail)"
    fi

    # /boot must be its own filesystem; if it is not, the kernel is inside the
    # volume that has not been unlocked yet, and this system did not boot the
    # way the installer builds it.
    if ! mountpoint -q /boot; then
        warn "/boot is not a separate mount — unexpected for an encrypted install"
    fi
}

cmd_add_key() {
    need_root add-key
    local dev; dev="$(resolve_device "${1:-}")"
    echo "Adding a passphrase to $dev."
    echo "You will be asked for an EXISTING passphrase first, then the new one."
    cryptsetup luksAddKey "$dev" || die "luksAddKey failed"
    ok "passphrase added"
}

cmd_change_key() {
    need_root change-key
    local dev; dev="$(resolve_device "${1:-}")"
    echo "Changing a passphrase on $dev."
    echo "You will be asked for the passphrase to replace, then the new one."
    cryptsetup luksChangeKey "$dev" || die "luksChangeKey failed"
    ok "passphrase changed"
}

cmd_remove_key() {
    need_root remove-key
    local dev; dev="$(resolve_device "${1:-}")"

    # Removing the only keyslot leaves a volume nobody can ever open again.
    # cryptsetup will happily do it; we will not.
    local slots
    slots="$(cryptsetup luksDump "$dev" | grep -cE '^\s+[0-9]+: luks2' || true)"
    [ "${slots:-0}" -gt 1 ] \
        || die "$dev has only $slots keyslot — removing it would make the disk unopenable"

    echo "Removing a passphrase from $dev ($slots keyslots in use)."
    echo "You will be asked for the passphrase to REMOVE."
    cryptsetup luksRemoveKey "$dev" || die "luksRemoveKey failed"
    ok "passphrase removed"
}

cmd_backup_header() {
    need_root backup-header
    local out="${1:-}"; shift || true
    [ -n "$out" ] || die "usage: syn-crypt backup-header FILE [DEVICE]"
    [ -e "$out" ] && die "$out already exists — refusing to overwrite a header backup"
    local dev; dev="$(resolve_device "${1:-}")"

    cryptsetup luksHeaderBackup "$dev" --header-backup-file "$out" \
        || die "luksHeaderBackup failed"
    chmod 600 "$out"
    ok "header saved to $out"
    warn "This file can unlock the disk with any passphrase that was valid when
  it was taken — including ones you later removed. Store it like a key,
  not like a backup, and keep it OFF this machine."
}

# Test seam, the same shape as syn-install.sh's SYN_INSTALL_SOURCE_ONLY: sourcing
# this with SYN_CRYPT_SOURCE_ONLY=1 defines the functions and stops HERE, before
# the dispatch below runs a command. tests/crypt_test.sh drives root_luks_device
# against fake findmnt/lsblk, which is the only way to exercise the device
# resolution without a real LUKS volume and root.
if [ "${SYN_CRYPT_SOURCE_ONLY:-}" = "1" ]; then
    return 0 2>/dev/null || exit 0
fi

case "${1:-}" in
    status)        shift; cmd_status "$@" ;;
    add-key)       shift; cmd_add_key "$@" ;;
    change-key)    shift; cmd_change_key "$@" ;;
    remove-key)    shift; cmd_remove_key "$@" ;;
    backup-header) shift; cmd_backup_header "$@" ;;
    ""|-h|--help|help) usage ;;
    *) usage; exit 1 ;;
esac
