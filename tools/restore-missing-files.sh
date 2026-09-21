#!/usr/bin/env bash
# restore-missing-files.sh — reinstall any first-party package whose files have
# gone missing from disk.
#
#     tools/restore-missing-files.sh <tree> <component>...
#
# build-all.sh runs this once, after its last install, over every component it
# knows — not only the ones it built this time.
#
# ⛔ WHY THIS EXISTS: A FILE THAT MOVES BETWEEN PACKAGES IS DELETED.
#
# build-all.sh installs each component in its OWN `pacman -U --overwrite '*'`,
# as soon as it is built, because later builds need earlier ones installed
# (scenefx before synui, syn-confine before vibe). --overwrite lets the new
# owner write a file, but does NOT take the file out of the old owner's file
# list — both now own it. When the old owner is later upgraded to a version
# without that file, pacman removes it from disk. Inside ONE transaction pacman
# tracks the move and keeps the file; across two it cannot.
#
# That happened: synapse-voice took faster-whisper's and piper's models over
# from chibi 26, and the chibi 27 upgrade eleven minutes later deleted them.
# `pacman -Qkk synapse-voice` said "6 altered files"; dictation on that machine
# heard nothing, and nothing anywhere reported an error.
#
# ⛔ THE DAMAGE LEAVES NO SIGNATURE TO LOOK FOR. The moment the old owner
# upgrades, the file is owned by the new package alone — the double ownership
# that caused it is gone. So what is checked is the result: a file pacman
# expects and the disk does not have. The deletion can also land a RUN later
# (the new owner installed on Monday, the old owner upgraded on Friday), which
# is why every installed component is checked and not just this run's.
#
# ⛔ ONLY "No such file or directory" COUNTS. As a user, `pacman -Qk synguard`
# reports every file under the root-only /etc/synguard/rules.d as missing with
# "Permission denied". Counting that would reinstall synguard on every run.
#
# Reinstalls from a package file of EXACTLY the installed version: the one the
# tree built, else the local [synapseos] repo, else pacman's cache. Never a
# newer or older build, which would be an upgrade or a downgrade nobody asked
# for. Nothing found, or still missing afterwards: it says so and carries on.
# A package with a hole in it is a reason to shout, not to fail an update that
# may be carrying a security fix.

set -u

tree=${1:?usage: restore-missing-files.sh <tree> <component>...}
shift

IFS=: read -ra PKG_DIRS <<< "${RESTORE_PKG_DIRS:-/var/cache/synapseos:/var/cache/pacman/pkg}"

# Paths pacman lists for package $1 that are ABSENT, one per line.
missing_files() {
    local n=$1 line p
    while IFS= read -r line; do
        case $line in
            "warning: $n: "*" (No such file or directory)")
                p=${line#"warning: $n: "}
                printf '%s\n' "${p% (No such file or directory)}" ;;
        esac
    done < <(LC_ALL=C pacman -Qk "$n" 2>&1)
}

# The package file for component $1, installed as $2 at version $3.
#
# The version is followed by "-" and the architecture, so syn 0.1.0-35 cannot
# match syn-update-0.1.0-35 (the name is anchored by the directory, then by the
# version right after it) nor syn-0.1.0-350.
package_file() {
    local comp=$1 name=$2 ver=$3 d f
    for d in "$tree/$comp" "${PKG_DIRS[@]}"; do
        for f in "$d/$name-$ver-"*.pkg.tar.zst; do
            [ -f "$f" ] && { printf '%s\n' "$f"; return 0; }
        done
    done
    return 1
}

restored=0
left=0
for comp in "$@"; do
    # `pacman -Q synapse-llama` answers for synapse-llama-cuda; use the name it
    # reports, which is the one the file list and the package file carry.
    read -r name ver < <(pacman -Q "$comp" 2>/dev/null) || continue
    [ -n "${name:-}" ] && [ -n "${ver:-}" ] || continue

    mapfile -t gone < <(missing_files "$name")
    [ ${#gone[@]} -gt 0 ] || continue

    echo "=== $name $ver: ${#gone[@]} file(s) missing from disk, e.g. ${gone[0]}"
    if ! pkg=$(package_file "$comp" "$name" "$ver"); then
        echo "!!! $name: no $name-$ver package file to reinstall from" >&2
        echo "    restore it with:  sudo pacman -S $name" >&2
        left=$((left + 1))
        continue
    fi

    echo "=== reinstalling $name from $pkg"
    ${SUDO:-sudo} pacman -U --noconfirm --overwrite '*' "$pkg" || true

    mapfile -t gone < <(missing_files "$name")
    if [ ${#gone[@]} -eq 0 ]; then
        echo "=== $name restored"
        restored=$((restored + 1))
    else
        echo "!!! $name: ${#gone[@]} file(s) still missing after reinstalling, e.g. ${gone[0]}" >&2
        left=$((left + 1))
    fi
done

[ "$restored" -gt 0 ] && echo "=== restored $restored package(s) with missing files"
[ "$left" -gt 0 ] && echo "!!! $left package(s) still have missing files — see above" >&2
exit 0
