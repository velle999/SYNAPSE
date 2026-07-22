#!/bin/bash
# Regenerate synguard-$pkgver.tar.gz — the PKGBUILD's one and only source.
#
# synguard had no such script, and that cost a real bug: a fixed daemon was
# built, packaged, installed and reported success while still containing the
# OLD binary, because `makepkg` had happily reused the stale tarball sitting in
# this directory. Editing src/ and running makepkg is NOT enough — the tree is
# the source, but the tarball is what gets compiled.
#
# The one entry that must never end up inside is:
#
#   src/synguard-0.1.0/   makepkg's own extraction dir, which lives under src/
#                         because that is where the real sources are too. Ship
#                         it and the next build extracts a tarball containing
#                         last build's tree, so a stale synguard_main.c wins.
#
# Mirrors synui/mktarball.sh deliberately — two packages solving the same
# problem two different ways is how one of them drifts.
#
# Run from the package root, then makepkg. Never `rm -rf src` to "clean up"
# here: src/ is the source tree, not a build artifact.
set -euo pipefail

cd "$(dirname "$(realpath "$0")")"

pkgver=$(sed -n 's/^pkgver=//p' PKGBUILD)
[[ -n $pkgver ]] || { echo "mktarball: no pkgver in PKGBUILD" >&2; exit 1; }

top="synguard-$pkgver"
out="$top.tar.gz"

# What actually gets compiled and installed. Adding a new top-level dir to
# meson.build means adding it here as well, or it silently will not ship.
contents=(include meson.build rules src systemd tests)

for c in "${contents[@]}"; do
    [[ -e $c ]] || { echo "mktarball: missing $c" >&2; exit 1; }
done

tmp=$(mktemp -t "synguard-tarball-XXXXXX.tar.gz")
trap 'rm -f "$tmp"' EXIT

tar czf "$tmp" \
    --transform "s,^,$top/," \
    --exclude="$top" \
    --exclude="$top.tar.gz" \
    --exclude='build' \
    --exclude='_build' \
    --exclude='pkg' \
    --exclude='*.o' \
    --exclude='*.pkg.tar.zst' \
    "${contents[@]}"

# A tarball that carries the extraction dir is worse than no tarball: the build
# still succeeds, it just compiles the wrong code. Fail loudly instead.
if tar tzf "$tmp" | grep -qE "^$top/src/$top/|^$top/src/$top\.tar\.gz$"; then
    echo "mktarball: refusing to write — tarball contains its own extraction dir" >&2
    exit 1
fi

mv "$tmp" "$out"
trap - EXIT

printf 'mktarball: wrote %s (%s files, %s)\n' \
    "$out" "$(tar tzf "$out" | wc -l)" "$(du -h "$out" | cut -f1)"
