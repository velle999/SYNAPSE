#!/bin/bash
# synui-iso-write — write a disc image to a USB stick, from Dolphin's
# right-click service menu (see config/synui-iso-write.desktop).
#
# Two stages. Stage one runs from Dolphin: it works out which USB sticks are
# safe to offer, asks which one, and opens a foot terminal. Stage two runs
# inside that terminal (__confirm, an internal action — not in the .desktop):
# it shows the proposed dd command, makes the user type the target device name,
# and only then writes. dd needs root, and root needs a tty to prompt on, which
# is the whole reason a terminal is in the picture at all.
#
# THE DEVICE IS NEVER HARDCODED. /dev/sdX is assigned in probe order and moves
# between boots: this box has a 7.3T archive on sda and the stick on sdd today,
# and nothing keeps it that way. dd does not ask twice, so the target is
# enumerated fresh every run and filtered down to removable USB disks only.
set -u

TITLE="Write Image to USB"
DD_BS="4M"

# Absolute path to this script, for the re-exec into foot below. KIO runs the
# service menu as bare `synui-iso-write`, so $0 can arrive with no directory at
# all; readlink -f on that would resolve against the cwd Dolphin happened to
# have and hand foot a path to nothing.
SELF=$0
case "$SELF" in
    */*) SELF=$(readlink -f -- "$SELF") ;;
    *)   SELF=$(command -v -- "$SELF") ;;
esac
[ -n "$SELF" ] && [ -x "$SELF" ] || SELF=/usr/bin/synui-iso-write

msg() { # msg <error|info|warning> <text>
    zenity --"$1" --no-markup --title="$TITLE" --text="$2" 2>/dev/null \
        || printf '%s\n' "$2" >&2
}
die() { msg error "$1"; exit 1; }

# ── The disks we must never offer ────────────────────────────────────────────
#
# The USB+removable filter below already excludes internal drives, but it is
# NOT sufficient on its own: SynapseOS ships this tool ON a live USB, and a
# live session's own boot stick is a removable USB disk like any other. Offering
# it means writing an image over the running system, so resolve what backs the
# live mounts and the root filesystem and strike those disks off by name.
disk_of() { # disk_of <path> -> parent disk name, or nothing
    local src pk
    src=$(findmnt -nro SOURCE --target "$1" 2>/dev/null) || return 0
    [ -n "$src" ] || return 0
    case "$src" in /dev/*) ;; *) return 0 ;; esac
    # A partition reports its parent disk; a whole-disk mount reports nothing,
    # in which case the source already IS the disk.
    pk=$(lsblk -nro PKNAME "$src" 2>/dev/null | head -1)
    printf '%s\n' "${pk:-$(basename "$src")}"
}

excluded=" "
for p in / /boot /boot/efi /run/archiso/bootmnt /run/archiso/cowspace; do
    d=$(disk_of "$p")
    [ -n "$d" ] && excluded="${excluded}${d} "
done

# ── Candidate USB sticks ─────────────────────────────────────────────────────
#
# -P (key="value") rather than -r: raw mode escapes spaces as \x20, so a
# "Cruzer Blade" arrives as "Cruzer\x20Blade" and reads as line noise in the
# picker. TRAN=usb and RM=1 together are the removable-USB test; TYPE=disk
# keeps partitions out.
candidates() {
    local NAME SIZE MODEL TRAN RM TYPE
    while IFS= read -r line; do
        NAME= SIZE= MODEL= TRAN= RM= TYPE=
        eval "$line"
        [ "${TYPE:-}" = "disk" ] || continue
        [ "${TRAN:-}" = "usb" ] || continue
        [ "${RM:-}" = "1" ]     || continue
        case "$excluded" in *" ${NAME} "*) continue ;; esac
        printf '%s\t%s\t%s\n' "$NAME" "$SIZE" "${MODEL:-USB device}"
    done < <(lsblk -dP -o NAME,SIZE,MODEL,TRAN,RM,TYPE 2>/dev/null)
}

human() { numfmt --to=iec --suffix=B "$1" 2>/dev/null || printf '%s bytes' "$1"; }

# ── Stage 2: confirm and write, inside foot ──────────────────────────────────
if [ "${1:-}" = "__confirm" ]; then
    image=${2:-}
    dev=${3:-}
    [ -n "$image" ] && [ -n "$dev" ] || { echo "internal: bad __confirm args" >&2; exit 2; }

    pause() { printf '\nPress Enter to close this window. '; read -r _ || true; }

    # Stage 1 filtered the device list, but re-run the system-disk check here
    # too. __confirm is reachable on its own — the script is on $PATH and this
    # is just an argument — so the check that matters must sit next to the dd,
    # not only next to the picker that usually precedes it.
    case "$excluded" in
        *" $(basename "$dev") "*)
            printf '\n  \033[1;31mRefusing to write to %s — it is a system disk.\033[0m\n' "$dev"
            pause; exit 1 ;;
    esac

    isize=$(stat -c %s "$image" 2>/dev/null || echo 0)
    # lsblk, not `blockdev --getsize64`: blockdev opens the device and so needs
    # root, which this stage does not have yet (only the dd below is sudo'd). It
    # failed with EACCES, the size read back as 0, and the "does it fit?" guard
    # below quietly skipped itself — dd then ran and died on ENOSPC halfway
    # through, leaving a half-written stick. lsblk reads sysfs and needs
    # nothing. A size of 0 here is now fatal rather than a licence to proceed.
    dsize=$(lsblk -bdno SIZE "$dev" 2>/dev/null | head -1)
    dsize=${dsize:-0}

    printf '\n  \033[1;36mWrite Image to USB\033[0m\n\n'
    printf '  Image:  %s\n' "${image##*/}"
    printf '          %s\n' "$(human "$isize")"
    printf '  Target: %s  —  %s, %s\n\n' "$dev" \
        "$(lsblk -dnro MODEL "$dev" 2>/dev/null | head -1)" \
        "$(lsblk -dnro SIZE "$dev" 2>/dev/null | head -1)"

    # Show what is on the stick now. This is the user's real chance to notice
    # they picked the wrong one — a label they recognise beats a device node
    # they don't.
    printf '  This device currently contains:\n'
    lsblk -nro NAME,SIZE,FSTYPE,LABEL "$dev" 2>/dev/null | sed 's/^/    /'
    printf '\n'

    if [ "$isize" -le 0 ] || [ "$dsize" -le 0 ]; then
        printf '  \033[1;31mCould not determine the image or device size — refusing to write.\033[0m\n'
        pause; exit 1
    fi
    if [ "$isize" -gt "$dsize" ]; then
        printf '  \033[1;31mThe image (%s) is larger than the device (%s). It cannot fit.\033[0m\n' \
            "$(human "$isize")" "$(human "$dsize")"
        pause; exit 1
    fi

    printf '  \033[1;31mEVERYTHING ON %s WILL BE DESTROYED.\033[0m\n' "$dev"
    printf '  This cannot be undone.\n\n'
    printf '  Proposed command:\n\n'
    printf '    \033[1msudo dd if=%s of=%s bs=%s status=progress oflag=sync\033[0m\n\n' \
        "$image" "$dev" "$DD_BS"

    # Type the device name, not "yes". A yes/no prompt is answered reflexively;
    # retyping "sdd" forces one more look at which disk this actually is.
    printf '  Type the device name (\033[1m%s\033[0m) to proceed, or Ctrl-C to cancel: ' \
        "${dev##*/}"
    read -r reply || { printf '\n  Cancelled.\n'; exit 1; }
    if [ "$reply" != "${dev##*/}" ] && [ "$reply" != "$dev" ]; then
        printf '\n  Cancelled — "%s" does not match.\n' "$reply"
        pause; exit 1
    fi

    # Unmount first. dd writes the raw device behind the filesystem's back, so
    # anything still mounted keeps a now-stale page cache over bytes that no
    # longer exist, and the kernel can flush that cache back over the fresh
    # image. udisksctl needs no root for a removable device the session owns.
    printf '\n'
    for part in "$dev"?*; do
        [ -b "$part" ] || continue
        [ -n "$(findmnt -nro TARGET -S "$part" 2>/dev/null)" ] || continue
        printf '  Unmounting %s...\n' "$part"
        udisksctl unmount --block-device "$part" >/dev/null 2>&1 \
            || { printf '  \033[1;31mCould not unmount %s — close anything using it.\033[0m\n' "$part"
                 pause; exit 1; }
    done

    printf '\n  Writing. Do not remove the device.\n\n'
    if ! sudo dd if="$image" of="$dev" bs="$DD_BS" status=progress oflag=sync; then
        printf '\n  \033[1;31mWrite failed.\033[0m\n'
        pause; exit 1
    fi

    # oflag=sync flushes dd's own writes, but the kernel may still hold the
    # device's buffer cache; settle it before telling anyone it is safe to pull.
    printf '\n  Flushing...\n'
    sync
    sudo blockdev --flushbufs "$dev" 2>/dev/null || true

    printf '\n  \033[1;32mDone. %s written to %s.\033[0m\n' "${image##*/}" "$dev"
    printf '  It is now safe to remove the device.\n'
    pause
    exit 0
fi

# ── Stage 1: pick a target, from Dolphin ─────────────────────────────────────
image=${1:-}
[ -n "$image" ] || { printf 'usage: %s IMAGE\n' "${0##*/}" >&2; exit 2; }
[ -f "$image" ] || die "Not a file:\n$image"
image=$(readlink -f -- "$image") || die "Cannot resolve path:\n$image"
[ -r "$image" ] || die "Cannot read:\n$image"

mapfile -t found < <(candidates)
if [ "${#found[@]}" -eq 0 ]; then
    die "No USB stick found.\n\nInsert one and try again. Only removable USB devices are offered — internal drives are never listed."
fi

if [ "${#found[@]}" -eq 1 ]; then
    IFS=$'\t' read -r name size model <<< "${found[0]}"
    dev="/dev/$name"
else
    # zenity --list returns the first column of the selected row, so device
    # name leads and the human-readable columns follow to identify it.
    rows=()
    for f in "${found[@]}"; do
        IFS=$'\t' read -r name size model <<< "$f"
        rows+=("$name" "$size" "$model")
    done
    dev=$(zenity --list --title="$TITLE" \
        --text="Write ${image##*/} to which USB stick?" \
        --column="Device" --column="Size" --column="Model" \
        --hide-header=false "${rows[@]}" 2>/dev/null) || exit 0
    [ -n "$dev" ] || exit 0
    dev="/dev/${dev%%|*}"
fi

[ -b "$dev" ] || die "Not a block device:\n$dev"

# Re-check the chosen device against the exclusion list. The picker's list was
# built moments ago, but the decision to destroy a disk should not rest on a
# stale snapshot — and if zenity ever hands back something unexpected, this is
# what stops it reaching dd.
case "$excluded" in
    *" $(basename "$dev") "*) die "Refusing to write to $dev — it is a system disk." ;;
esac

exec foot -T "$TITLE" -- "$SELF" __confirm "$image" "$dev"
