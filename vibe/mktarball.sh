#!/usr/bin/env bash
# Build the source tarball vibe's PKGBUILD expects.
#
# Without this, vibe-0.1.0.tar.gz was a committed artefact that somebody had to
# remember to regenerate. That is the failure syn-update exists to catch, in a
# different disguise: the package builds, reports success, and ships whatever
# the tarball happened to contain the last time a human rebuilt it.
#
# build-all.sh's build_component() runs this in preference to its generic tar
# when it is executable, so the tarball is now derived from the tree on every
# build.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
name=vibe
ver=$(sed -n 's/^pkgver=//p' PKGBUILD | head -1)
[ -n "$ver" ] || { echo "mktarball: cannot read pkgver from PKGBUILD" >&2; exit 1; }

stage="$name-$ver"
tarball="$stage.tar.gz"

# Only what package() reads: the entry point, the package, the launcher, and
# the chat window's QML. requirements.txt, setup.sh and the dev launcher are
# deliberately not shipped.
for p in main.py vibe packaging/vibe-launcher.sh data/vibe.qml data/vibe.desktop \
         systemd/vibe-wake.service; do
    [ -e "$p" ] || { echo "mktarball: missing $p" >&2; exit 1; }
done

# Never into the source dir itself -- an extraction dir inside the tarball is
# what caused the nested src/vibe-0.1.0/src/vibe-0.1.0/... recursion elsewhere.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/$stage/packaging" "$tmp/$stage/data" "$tmp/$stage/systemd"
cp -a main.py vibe "$tmp/$stage/"
cp -a packaging/vibe-launcher.sh "$tmp/$stage/packaging/"
cp -a data/vibe.qml data/vibe.desktop "$tmp/$stage/data/"
cp -a systemd/vibe-wake.service "$tmp/$stage/systemd/"

# Build-host droppings and runtime session state must never reach a package.
rm -rf "$tmp/$stage/vibe/__pycache__" "$tmp/$stage/.vibe"
find "$tmp/$stage" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true

rm -f "$tarball"
tar czf "$tarball" -C "$tmp" "$stage"
echo "mktarball: wrote $tarball ($(du -h "$tarball" | cut -f1))"
