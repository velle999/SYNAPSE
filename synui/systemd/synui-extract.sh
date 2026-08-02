#!/bin/bash
# synui-extract — extract .rar/.7z/.zip (and the tar family) from Dolphin's
# right-click service menu (see config/synui-extract.desktop).
#
# Nothing here needs root: extraction only ever writes inside a directory the
# user already has, and the destination is checked for writability up front
# rather than discovered halfway through a 4GB archive.
#
# One back end handles all three formats -- 7-Zip reads RAR and ZIP as well as
# its own -- but the others are kept as fallbacks so a box without the optional
# dependency still extracts something rather than showing a menu that fails.
# unrar goes FIRST for .rar, because 7-Zip's RAR support does not cover every
# variant in the wild and unrar is the reference implementation.
set -u

TITLE="Extract"

msg() { # msg <error|info|warning> <text>
    zenity --"$1" --no-markup --title="$TITLE" --text="$2" 2>/dev/null \
        || printf '%s\n' "$2" >&2
}
die() { msg error "$1"; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

action=${1:-}
shift || true
if [ -z "$action" ] || [ "$#" -eq 0 ]; then
    printf 'usage: %s {here|subdir|ask} ARCHIVE...\n' "${0##*/}" >&2
    exit 2
fi

if ! have 7zz && ! have unrar && ! have unzip && ! have bsdtar; then
    die "No extraction tool is installed.\n\nInstall 7zip (or unrar, unzip)."
fi

# ── naming ─────────────────────────────────────────────────────────────────

# The archive name with its extension taken off, including the double ones:
# foo.tar.gz -> foo, not foo.tar. Used for the subfolder name only, so getting
# an unusual one wrong costs a slightly ugly directory and nothing else.
strip_ext() {
    local n=${1##*/}
    case ${n,,} in
        *.tar.gz|*.tar.bz2|*.tar.xz|*.tar.zst|*.tar.lz4|*.tar.lzma)
            printf '%s\n' "${n%.*.*}" ;;
        *.*) printf '%s\n' "${n%.*}" ;;
        *)   printf '%s\n' "$n" ;;
    esac
}

# A directory that does not exist yet: name, then "name (2)", "name (3)"...
# Never returns a path that is already there, so an extraction cannot quietly
# merge into an unrelated folder that happens to share the archive's name.
unique_dir() {
    local base=$1 n=2 candidate=$1
    while [ -e "$candidate" ]; do
        candidate="$base ($n)"
        n=$((n + 1))
        [ "$n" -gt 999 ] && return 1
    done
    printf '%s\n' "$candidate"
}

# ── back ends ──────────────────────────────────────────────────────────────

# Each runs non-interactively and RENAMES on collision rather than overwriting.
# That matters more than it looks: these are launched from a file manager with
# no terminal attached, so a back end that stops to ask "overwrite? [y/n]" on
# stdin does not prompt -- it hangs, or takes the default, and either way the
# user is not told. Silent data loss is the failure this avoids.
#
# $pw is the password, empty for none. Passing an empty -p to 7-Zip is not the
# same as omitting it (it means "the password is the empty string"), so the
# argument is only added when there is one.
run_7zz() { # run_7zz <archive> <destdir>
    local args=(x -y -aou -bso0 -bsp0 -snld)
    [ -n "$pw" ] && args+=("-p$pw")
    7zz "${args[@]}" -o"$2" -- "$1"
}

run_unrar() { # run_unrar <archive> <destdir>
    # -or rename on collision, -y assume yes, -p- means "no password, do not
    # ask"; without it unrar blocks on a tty that is not there.
    if [ -n "$pw" ]; then
        unrar x -or -y -p"$pw" -- "$1" "$2/"
    else
        unrar x -or -y -p- -- "$1" "$2/"
    fi
}

run_unzip() { # run_unzip <archive> <destdir>
    # -n never overwrites. unzip has no rename-on-collision, so this is the one
    # back end that can skip a file; it is last in the zip order for that.
    if [ -n "$pw" ]; then
        unzip -qq -n -P "$pw" -- "$1" -d "$2"
    else
        unzip -qq -n -- "$1" -d "$2"
    fi
}

run_bsdtar() { # run_bsdtar <archive> <destdir>
    # -k keeps existing files. libarchive reads zip, 7z, rar and the tar family,
    # which is why it is a universal last resort -- and it is always present,
    # because pacman itself depends on libarchive.
    bsdtar -x -k -f "$1" -C "$2"
}

# Which back ends can try this archive, best first.
backends_for() { # backends_for <archive>
    local lower=${1,,}
    case $lower in
        *.rar|*.cbr)  printf '%s\n' unrar 7zz bsdtar ;;
        *.7z)         printf '%s\n' 7zz bsdtar ;;
        *.zip|*.cbz|*.jar|*.epub)
                      printf '%s\n' 7zz bsdtar unzip ;;
        *)            printf '%s\n' bsdtar 7zz ;;
    esac
}

# Does this failure look like a locked archive rather than a broken one?
needs_password() { # needs_password <output>
    printf '%s' "$1" | grep -qiE \
        'wrong password|password is incorrect|encrypted|requires a password|incorrect password|is encrypted'
}

# ── the work ───────────────────────────────────────────────────────────────

# Try each back end in turn. Returns 0 on the first success; on failure leaves
# the last back end's output in $last_out so the caller can report something
# more useful than "it did not work".
extract_with_backends() { # extract_with_backends <archive> <destdir>
    local be rc
    last_out=""
    for be in $(backends_for "$1"); do
        have "$be" || continue
        last_out=$("run_$be" "$1" "$2" 2>&1)
        rc=$?
        [ "$rc" -eq 0 ] && return 0
        # A locked archive is not a reason to try the next back end: they will
        # all fail the same way. Report it so the caller can ask for a password.
        needs_password "$last_out" && return 2
    done
    return 1
}

# Where "Extract Here" should really put things.
#
# An archive with one top-level entry unpacks into the current directory; one
# with several would strew them across it -- the tarbomb -- so it gets a folder
# of its own. This is what a file manager is expected to do, and doing it by
# LOOKING rather than by guessing from the name is the only way to be right.
# If the listing cannot be read, assume many: a needless folder is a nuisance,
# a strewn directory is a mess to clean up.
single_root() { # single_root <archive>
    local out roots
    if have 7zz; then
        out=$(7zz l -ba -slt -p- -- "$1" 2>/dev/null) || return 1
        roots=$(printf '%s\n' "$out" | sed -n 's/^Path = //p' \
            | sed 's#/.*##' | sort -u | wc -l)
    elif have bsdtar; then
        out=$(bsdtar -tf "$1" 2>/dev/null) || return 1
        roots=$(printf '%s\n' "$out" | sed 's#/.*##' | sort -u | wc -l)
    else
        return 1
    fi
    [ "$roots" = "1" ]
}

pw=""
rc_any=0
ask_dir=""

for archive in "$@"; do
    [ -f "$archive" ] || { msg error "Not a file:\n$archive"; rc_any=1; continue; }
    archive=$(readlink -f -- "$archive") || { rc_any=1; continue; }
    parent=${archive%/*}
    stem=$(strip_ext "$archive")

    case $action in
    here)
        if single_root "$archive"; then
            dest=$parent
        else
            dest=$(unique_dir "$parent/$stem") || { msg error "Too many folders named\n$stem"; rc_any=1; continue; }
        fi
        ;;
    subdir)
        dest=$(unique_dir "$parent/$stem") || { msg error "Too many folders named\n$stem"; rc_any=1; continue; }
        ;;
    ask)
        # Asked once for a whole selection, not once per archive.
        if [ -z "$ask_dir" ]; then
            ask_dir=$(zenity --file-selection --directory --title="$TITLE to…" \
                --filename="$parent/" 2>/dev/null) || exit 0
        fi
        dest=$ask_dir
        ;;
    *)
        printf 'unknown action: %s\n' "$action" >&2
        exit 2
        ;;
    esac

    # Checked before anything is created, and on the PARENT when the folder is
    # ours to make -- a read-only medium is the common case here (an archive
    # sitting on a mounted .iso) and finding out mid-extraction leaves a
    # half-unpacked mess.
    if [ -d "$dest" ]; then
        [ -w "$dest" ] || { msg error "Cannot write to:\n$dest"; rc_any=1; continue; }
    else
        [ -w "${dest%/*}" ] || { msg error "Cannot write to:\n${dest%/*}"; rc_any=1; continue; }
        mkdir -p -- "$dest" || { msg error "Cannot create:\n$dest"; rc_any=1; continue; }
        made_dir=$dest
    fi

    extract_with_backends "$archive" "$dest"
    rc=$?

    if [ "$rc" -eq 2 ]; then
        pw=$(zenity --password --title="$TITLE" 2>/dev/null) || pw=""
        if [ -n "$pw" ]; then
            extract_with_backends "$archive" "$dest"
            rc=$?
        fi
    fi

    if [ "$rc" -ne 0 ]; then
        # Remove a folder we created and then failed to fill, so a failed
        # extraction does not litter. Only ever rmdir: it refuses on a
        # non-empty directory, which is exactly the guard wanted here.
        [ -n "${made_dir:-}" ] && rmdir -- "$made_dir" 2>/dev/null
        detail=$(printf '%s' "$last_out" | tail -3)
        msg error "Could not extract:\n${archive##*/}\n\n${detail:-No extraction back end succeeded.}"
        rc_any=1
    fi
    made_dir=""
done

exit "$rc_any"
