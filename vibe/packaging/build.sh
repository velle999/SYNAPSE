#!/bin/bash
# Build the SynapseOS `vibe` package from the local working tree.
#
# Vibe's synapd backend is not upstream yet, so — unlike chibi, which clones a
# pinned commit — this packages whatever is checked out here. It tars the app
# (entry point + vibe/ package + launcher) into the source tarball the PKGBUILD
# expects, then runs makepkg.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."   # → the vibe/ component root

VER=0.1.0
TARBALL="vibe-$VER.tar.gz"

# Clean prior makepkg droppings so the tarball never re-ingests an extraction.
rm -rf "pkg" "src/vibe-$VER" ./*.pkg.tar.zst "$TARBALL"

# Only the files the package installs. Exclude dev-only and generated cruft.
tar czf "$TARBALL" \
    --transform "s|^|vibe-$VER/|" \
    --exclude='__pycache__' \
    --exclude='.venv' \
    --exclude='.git*' \
    --exclude='*.pyc' \
    main.py vibe packaging/vibe-launcher.sh

makepkg -sf --noconfirm "$@"
