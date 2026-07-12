#!/bin/bash
# synui-iso-mount — loop-mount a disc image without root, for Dolphin's
# right-click service menu (see config/synui-iso-mount.desktop).
#
# udisks2 does the privileged work. Its polkit actions loop-setup and
# filesystem-mount are allow_active=yes, so an active local session mounts with
# no password prompt; an inactive or remote one gets auth_admin and the polkit
# agent prompts. Nothing here needs sudo.
set -u

TITLE="Disc Image"

msg() { # msg <error|info> <text>
    zenity --"$1" --no-markup --title="$TITLE" --text="$2" 2>/dev/null \
        || printf '%s\n' "$2" >&2
}
die() { msg error "$1"; exit 1; }

action=${1:-}
image=${2:-}
if [ -z "$action" ] || [ -z "$image" ]; then
    printf 'usage: %s {mount|unmount} IMAGE\n' "${0##*/}" >&2
    exit 2
fi
[ -f "$image" ] || die "Not a file:\n$image"
image=$(readlink -f -- "$image") || die "Cannot resolve path:\n$image"

# The loop device already backed by this image, if any. sysfs backing_file is
# world-readable, so this needs no privileges.
loop_for_image() {
    local d backing
    for d in /sys/block/loop*; do
        [ -r "$d/loop/backing_file" ] || continue
        backing=$(cat "$d/loop/backing_file") || continue
        if [ "$backing" = "$image" ]; then
            printf '/dev/%s\n' "${d##*/}"
            return 0
        fi
    done
    return 1
}

# Where a loop device's filesystem is mounted, if anywhere. A hybrid ISO (any
# isohybrid one, ours included) carries an MBR as well as an ISO9660 filesystem,
# so udisks scans partitions out of it and the mount lands on loop0p1, not
# loop0 — check the whole device and its partitions.
mountpoint_of_loop() {
    local dev mp
    for dev in "$1" "$1"p*; do
        [ -b "$dev" ] || continue
        mp=$(findmnt -nro TARGET -S "$dev" 2>/dev/null | head -1)
        if [ -n "$mp" ]; then
            printf '%s\n' "$mp"
            return 0
        fi
    done
    return 1
}

case "$action" in
mount)
    ours=0
    loop=$(loop_for_image) || {
        out=$(udisksctl loop-setup --read-only --file "$image" 2>&1) \
            || die "Could not set up a loop device:\n\n$out"
        # "Mapped file <path> as /dev/loop0."
        loop=${out##* as }
        loop=${loop%.}
        [ -b "$loop" ] || die "Unexpected udisksctl output:\n\n$out"
        ours=1
    }

    mp=$(mountpoint_of_loop "$loop") || {
        for dev in "$loop" "$loop"p*; do
            [ -b "$dev" ] || continue
            out=$(udisksctl mount --block-device "$dev" 2>&1) && break
        done
        mp=$(mountpoint_of_loop "$loop") || {
            # Only tear down a loop device we just created; one that was
            # already there is someone else's.
            [ "$ours" = 1 ] && \
                udisksctl loop-delete --block-device "$loop" >/dev/null 2>&1
            die "Could not mount ${image##*/}:\n\n$out"
        }
    }

    # Hand the loop device back to the kernel once its last mount goes away.
    #
    # Detaching a loop device is Loop.Delete, and nothing but this script ever
    # calls it: Solid — and so the eject button beside the image in Dolphin's
    # Places panel — can unmount a filesystem but has no concept of the loop
    # device underneath it. So an eject used to unmount the image and leave the
    # loop attached, which kept the entry sitting in the panel looking mounted,
    # and only a second trip through this script's unmount action cleared it.
    #
    # Autoclear makes that impossible: the kernel drops the loop on the last
    # close, so *whoever* releases the mount gets a full teardown. It has to be
    # set after the mount, which holds the device open — set it on an idle loop
    # and it detaches under us immediately. Best-effort: the image is mounted
    # either way, and a stale loop is untidy, not fatal.
    if [ "$ours" = 1 ]; then
        busctl call org.freedesktop.UDisks2 \
            "/org/freedesktop/UDisks2/block_devices/${loop##*/}" \
            org.freedesktop.UDisks2.Loop SetAutoclear "ba{sv}" true 0 \
            >/dev/null 2>&1 || true
    fi

    exec dolphin "$mp"
    ;;
unmount)
    loop=$(loop_for_image) || die "${image##*/} is not mounted."

    # loop-delete refuses while anything on the device is still mounted, so
    # unmount the whole device and every partition udisks scanned out of it.
    for dev in "$loop" "$loop"p*; do
        [ -b "$dev" ] || continue
        [ -n "$(findmnt -nro TARGET -S "$dev" 2>/dev/null)" ] || continue
        out=$(udisksctl unmount --block-device "$dev" 2>&1) \
            || die "Could not unmount $dev:\n\n$out"
    done

    # Autoclear, set when we mounted it, means the unmount above has most
    # likely detached the loop device already. Deleting it a second time is not
    # merely redundant: once the backing file is gone udisks2 no longer sees the
    # device as one *we* set up, so the request escalates to loop-delete-others
    # and the user gets an admin password prompt for a teardown that has in fact
    # already happened.
    #
    # Note the node outlives the binding — /dev/loop0 still exists, unbound, so
    # a [ -b ] test proves nothing. What settles it is whether any loop device
    # is still backed by this image, which is exactly what loop_for_image asks.
    if loop=$(loop_for_image); then
        out=$(udisksctl loop-delete --block-device "$loop" 2>&1) \
            || die "Could not detach $loop:\n\n$out"
    fi
    ;;
*)
    die "Unknown action: $action"
    ;;
esac
