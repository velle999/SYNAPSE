#!/usr/bin/env bash
# source-date-epoch.sh — the SOURCE_DATE_EPOCH a build of this tree should use.
#
#   tools/source-date-epoch.sh              # the tree: HEAD's commit time
#   tools/source-date-epoch.sh <component>  # the last commit that touched it
#
# makepkg stamps SOURCE_DATE_EPOCH into every package — the builddate in
# .PKGINFO and .BUILDINFO, and the mtime of every file it packs — and when the
# variable is unset it uses the CURRENT time. So two builds of one commit could
# never match, whatever the compiler did. This is the one place the date comes
# from, for build-all.sh, archiso/build.sh and tools/repro-check.sh alike.
#
# PER COMPONENT for a package, not HEAD: a package's bytes should change when
# its own source does, not every time anything else in the repository is
# committed. The ISO takes HEAD, because an image is a snapshot of all of it.
#
# ⚠ A DIRTY TREE STILL GETS THE COMMIT'S TIME, so a local build with uncommitted
# edits is stamped as if it were that commit. That is deliberate for build-all's
# inner loop, and it is why repro-check.sh builds a clean clone instead.
#
# Prints the epoch; exits 1 outside a git checkout (the caller then decides —
# build-all.sh falls back to makepkg's own behaviour).
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(git -C "$(dirname "$(realpath "$0")")/.." rev-parse --show-toplevel 2>/dev/null) || exit 1

if [ $# -gt 0 ] && [ -n "$1" ]; then
    t=$(git -C "$root" log -1 --format=%ct -- "$1" 2>/dev/null)
else
    t=$(git -C "$root" log -1 --format=%ct 2>/dev/null)
fi
[ -n "$t" ] || exit 1
echo "$t"
