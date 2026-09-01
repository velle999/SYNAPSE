#!/bin/bash
# Regenerate synui-$pkgver.tar.gz — the PKGBUILD's one and only source.
#
# This exists because the tarball used to be rolled by hand, and a `tar` over
# the whole tree swept in two things it must never contain:
#
#   src/synui-0.1.0/      makepkg's own extraction dir, which lives under src/
#                         because that is where the real sources are too. Ship
#                         it and the next build extracts a tarball containing
#                         last build's tree, so a stale render.c can win.
#   synui-0.1.0.tar.gz    the tarball inside itself, growing every release.
#
# Both are excluded below by name. Build dirs are excluded too — they hold
# object files and a configured meson prefix that mean nothing on another box.
#
# Run from the package root, then makepkg. Never `rm -rf src` to "clean up"
# here: src/ is the source tree, not a build artifact.
set -euo pipefail

cd "$(dirname "$(realpath "$0")")"

pkgver=$(sed -n 's/^pkgver=//p' PKGBUILD)
[[ -n $pkgver ]] || { echo "mktarball: no pkgver in PKGBUILD" >&2; exit 1; }

top="synui-$pkgver"
out="$top.tar.gz"

# What actually gets compiled and installed.
contents=(config data hooks meson.build po protocols quickshell quickshell-antiquity
          src systemd tests tools)

for c in "${contents[@]}"; do
    [[ -e $c ]] || { echo "mktarball: missing $c" >&2; exit 1; }
done

# ⛔ AND CROSS-CHECKED AGAINST meson.build, BECAUSE ASKING A HUMAN TO REMEMBER
# DID NOT WORK. This spot held a comment reading "adding a new top-level dir to
# meson.build means adding it here as well, or it silently will not ship" — and
# then po/ was added to meson.build and not to the list. Every check upstream of
# the build passed: git tracked all 17 files, preflight was green, the package
# built HERE because this working copy has the directory on disk regardless of
# what the tarball holds. It failed on the only machine that starts from the
# tarball, with meson saying "Nonexistent build file 'po/meson.build'", after
# the commit had already been pushed and published.
#
# A comment cannot fail a build. This can: whatever meson enters, the tarball
# carries. ⚠ cut -d/ because only the TOP-LEVEL name is what contents=() names.
while IFS= read -r d; do
    [[ " ${contents[*]} " == *" $d "* ]] && continue
    echo "mktarball: meson.build enters $d/ but contents=() does not carry it —" >&2
    echo "           the build would fail wherever the tarball is the whole tree" >&2
    exit 1
done < <(grep -oE "subdir\('[^']+'\)" meson.build \
         | sed "s/subdir('//; s/')//" | cut -d/ -f1 | sort -u)

tmp=$(mktemp -t "synui-tarball-XXXXXX.tar.gz")
trap 'rm -f "$tmp"' EXIT

tar czf "$tmp" \
    --transform "s,^,$top/," \
    --exclude="$top" \
    --exclude="$top.tar.gz" \
    --exclude='_build' \
    --exclude='_build*' \
    --exclude='build-*' \
    --exclude='_asan' \
    --exclude='_ci' \
    --exclude='pkg' \
    --exclude='*.o' \
    --exclude='*.pkg.tar.zst' \
    "${contents[@]}"

# A tarball that carries either poison entry is worse than no tarball: the
# build still succeeds, it just compiles the wrong code. Fail loudly instead.
if tar tzf "$tmp" | grep -qE "^$top/src/$top/|^$top/src/$top\.tar\.gz$"; then
    echo "mktarball: refusing to write — tarball contains its own extraction dir" >&2
    exit 1
fi

mv "$tmp" "$out"
trap - EXIT

printf 'mktarball: wrote %s (%s files, %s)\n' \
    "$out" "$(tar tzf "$out" | wc -l)" "$(du -h "$out" | cut -f1)"
