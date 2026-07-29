#!/bin/bash
# Regenerate synapse_kmod-$pkgver.tar.gz — the PKGBUILD's one and only source.
#
# synguard grew this script after a fixed daemon was built, packaged, installed
# and reported success while still containing the OLD binary, because makepkg
# reused the stale tarball sitting in the package root. synapse_kmod had the
# same source= line and no such script, so it had the same bug waiting: on
# 2026-07-29 the tarball here was 13 hours older than src/synapse_probe.c, and
# `makepkg` would have cheerfully shipped a kmod without the fix in it.
#
# For a DKMS package the failure is quieter still. The .ko is compiled on the
# TARGET at install time from whatever sources the package dropped in
# /usr/src, so a stale tarball produces a module that builds cleanly against
# the right kernel, loads with the right vermagic, and simply does not contain
# the change. Nothing in the build or install output says so.
#
# The one entry that must never end up inside is:
#
#   src/synapse_kmod-0.1.0/   makepkg's own extraction dir, which lives under
#                             src/ because that is where the real sources are
#                             too. Ship it and the next build extracts a
#                             tarball containing last build's tree.
#
# In-tree build leavings (*.o, *.ko, *.mod.c, Module.symvers, modules.order)
# are excluded as well: package() deletes them from $pkgdir, but DKMS reuses
# any that reach /usr/src and silently produces a module with the wrong
# vermagic — the reason dkms.conf builds in build/ rather than source/.
#
# Mirrors synguard/mktarball.sh deliberately — two packages solving the same
# problem two different ways is how one of them drifts.
#
# Run from the package root, then makepkg. Never `rm -rf src` to "clean up"
# here: src/ is the source tree, not a build artifact.
set -euo pipefail

cd "$(dirname "$(realpath "$0")")"

pkgver=$(sed -n 's/^pkgver=//p' PKGBUILD)
[[ -n $pkgver ]] || { echo "mktarball: no pkgver in PKGBUILD" >&2; exit 1; }

top="synapse_kmod-$pkgver"
out="$top.tar.gz"

# What actually gets compiled and installed. Adding a new top-level dir that
# PKGBUILD reads means adding it here as well, or it silently will not ship.
contents=(config dkms.conf HARDENING.md hooks include Makefile src
          synapse_kmod.install systemd tools)

for c in "${contents[@]}"; do
    [[ -e $c ]] || { echo "mktarball: missing $c" >&2; exit 1; }
done

tmp=$(mktemp -t "synapse_kmod-tarball-XXXXXX.tar.gz")
trap 'rm -f "$tmp"' EXIT

tar czf "$tmp" \
    --transform "s,^,$top/," \
    --exclude="$top" \
    --exclude="$top.tar.gz" \
    --exclude='build' \
    --exclude='_build' \
    --exclude='pkg' \
    --exclude='*.o' \
    --exclude='*.ko' \
    --exclude='*.mod' \
    --exclude='*.mod.c' \
    --exclude='*.cmd' \
    --exclude='modules.order' \
    --exclude='Module.symvers' \
    --exclude='*.pkg.tar.zst' \
    "${contents[@]}"

# A tarball that carries the extraction dir is worse than no tarball: the build
# still succeeds, it just compiles the wrong code. Fail loudly instead.
if tar tzf "$tmp" | grep -qE "^$top/src/$top/|^$top/src/$top\.tar\.gz$"; then
    echo "mktarball: refusing to write — tarball contains its own extraction dir" >&2
    exit 1
fi

# DKMS compiles what is in here, so a build artifact that slipped through
# would be reused on the target. Cheap to assert, expensive to debug.
if tar tzf "$tmp" | grep -qE '\.(o|ko|mod|mod\.c|cmd)$|/(modules\.order|Module\.symvers)$'; then
    echo "mktarball: refusing to write — tarball contains build artifacts" >&2
    exit 1
fi

mv "$tmp" "$out"
trap - EXIT

printf 'mktarball: wrote %s (%s files, %s)\n' \
    "$out" "$(tar tzf "$out" | wc -l)" "$(du -h "$out" | cut -f1)"
