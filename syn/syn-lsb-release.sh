#!/usr/bin/env bash
# syn-lsb-release — make `lsb_release` answer SynapseOS instead of Arch Linux.
#
# WHY THIS EXISTS. lsb_release does NOT read /etc/os-release. It scans /etc for
# a *-release file with `find -type f`, which skips /etc/os-release outright
# because that path is a SYMLINK to ../usr/lib/os-release — so the only file it
# finds is the empty /etc/arch-release. Everything it then prints comes from
# sourcing /etc/lsb-release, which the Arch `lsb-release` package ships hardcoded
# to Arch:
#
#     Distributor ID:  Arch          Description:  Arch Linux
#
# on a machine whose os-release has said SynapseOS since it was installed.
# Deleting the file does not help — without it lsb_release answers "n/a" and
# "(none)", because there is no other source it consults. The override file is
# load-bearing, so the fix is to write a correct one.
#
# ⚠ AND IT HAS TO BE REWRITTEN, NOT EDITED ONCE. /etc/lsb-release is owned by
# the `lsb-release` package with `Backup Files : None`, and pacman overwrites a
# non-backup file on every upgrade with no .pacnew and no warning. A hand edit
# survives until the next upgrade of that package and then silently reverts. It
# is a dependency of steam and proton-cachyos-slr, so on a gaming box it does
# get upgraded. 75-syn-lsb-release.hook re-runs this PostTransaction whenever
# either lsb-release or syn moves, which is every occasion the file can change.
#
# ⚠ THE VALUES ARE SOURCED FROM os-release, NEVER GREPPED OUT OF IT. os-release
# fields are shell assignments and may quote or compose their values; scraping
# the literal line is how syn-update once read '${_pkgver}${_extver}' back as a
# version and bricked itself on limine-mkinitcpio-hook.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -uo pipefail

OS_RELEASE="${SYN_OS_RELEASE:-/etc/os-release}"
LSB_RELEASE="${SYN_LSB_RELEASE:-/etc/lsb-release}"

# ⛔ ONLY EVER REWRITE A FILE THAT IS ALREADY THERE. Creating /etc/lsb-release on
# a machine without the lsb-release package would leave an UNOWNED file at a path
# that package owns, and the next `pacman -S lsb-release` — pulled in by steam,
# by proton — would abort the whole transaction with "exists in filesystem".
# Absent means the tool is absent too, so there is nothing to correct.
[ -f "$LSB_RELEASE" ] || exit 0
[ -r "$OS_RELEASE" ]  || exit 0

# Subshell: os-release defines NAME and VERSION, which are ordinary names this
# script would otherwise inherit into. Same reasoning as syn-install's reader.
eval "$(
    . "$OS_RELEASE" 2>/dev/null || exit 0
    # Nothing to say without an identity. Better to leave whatever is there than
    # to blank the file out on a machine with a damaged os-release.
    [ -n "${NAME-}" ] || exit 0
    printf 'id=%q\nrelease=%q\ncodename=%q\ndescription=%q\n' \
        "$NAME" "${VERSION_ID-}" "${VERSION_CODENAME-}" "${PRETTY_NAME:-$NAME}"
)"
[ -n "${id-}" ] || exit 0

{
    printf 'DISTRIB_ID="%s"\n' "$id"
    printf 'DISTRIB_RELEASE="%s"\n' "$release"
    # Omitted rather than written empty when os-release has no VERSION_CODENAME:
    # lsb_release prints "n/a" for an unset codename either way, and an empty
    # assignment reads like a codename we failed to fill in.
    [ -n "$codename" ] && printf 'DISTRIB_CODENAME="%s"\n' "$codename"
    printf 'DISTRIB_DESCRIPTION="%s"\n' "$description"
} > "$LSB_RELEASE.syn.$$" || exit 1

# Atomic: a hook killed mid-write must not leave lsb_release sourcing half a
# file. mv over the same filesystem is the rename, so readers see old or new.
chmod 644 "$LSB_RELEASE.syn.$$"
mv -f "$LSB_RELEASE.syn.$$" "$LSB_RELEASE" || { rm -f "$LSB_RELEASE.syn.$$"; exit 1; }
