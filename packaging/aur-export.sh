#!/usr/bin/env bash
#
# aur-export.sh — each externally-installable component in the shape the AUR
# wants: a directory holding the PKGBUILD and a generated .SRCINFO.
#
# ⛔ IT COPIES, IT DOES NOT REWRITE. The exported PKGBUILD is byte-for-byte the
# one in the tree, because the tree's already works outside this checkout —
# `source=()` names the release asset and makepkg only prefers a local file when
# one is there. A generator that "adapted" it for the AUR would be a second set
# of depends and install rules, free to drift, and the drift would show up as a
# package that builds here and not for the person who filed the bug.
#
# ⚠ .SRCINFO IS GENERATED, NOT WRITTEN. `makepkg --printsrcinfo` expands the
# same variables makepkg itself will, so the metadata the AUR shows cannot
# disagree with what is built. It is regenerated on every export; never edit it.
#
# Usage:
#   packaging/aur-export.sh              # every external component
#   packaging/aur-export.sh syn-play     # one of them
#   packaging/aur-export.sh -o /tmp/aur  # somewhere other than packaging/out
#
# Then, per package:
#   git clone ssh://aur@aur.archlinux.org/syn-play.git
#   cp packaging/out/syn-play/{PKGBUILD,.SRCINFO} syn-play/
#   cd syn-play && git commit -am 'syn-play 0.1.0-5' && git push
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

BASE=$(cd "$(dirname "$0")/.." && pwd)
cd "$BASE"

out="$BASE/packaging/out"
only=()
while [ $# -gt 0 ]; do
    case "$1" in
        -o|--out) out=$2; shift ;;
        -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
        -*) echo "aur-export: unknown option $1" >&2; exit 2 ;;
        *)  only+=("$1") ;;
    esac
    shift
done

# ⛔ THE SET IS READ, NOT RESTATED. tools/publish-sources.sh owns it; a copy here
# would let this script export a package whose source nothing publishes.
mapfile -t EXTERNAL < <(
    block=$(sed -n '/^EXTERNAL=(/,/^)/p' tools/publish-sources.sh)
    [ -n "$block" ] || exit 1
    ( set +u; eval "$block"; printf '%s\n' "${EXTERNAL[@]}" )
)
[ ${#EXTERNAL[@]} -gt 0 ] || { echo "aur-export: could not read EXTERNAL" >&2; exit 1; }

want() {
    [ ${#only[@]} -eq 0 ] && return 0
    local n; for n in "${only[@]}"; do [ "$n" = "$1" ] && return 0; done
    return 1
}

# ⚠ makepkg --printsrcinfo must run in the package's own directory: it sources
# the PKGBUILD, and several of ours read files beside them.
n=0
for name in "${EXTERNAL[@]}"; do
    want "$name" || continue
    [ -f "$BASE/$name/PKGBUILD" ] || { echo "aur-export: no $name/PKGBUILD" >&2; exit 1; }

    d="$out/$name"
    mkdir -p "$d"
    cp "$BASE/$name/PKGBUILD" "$d/PKGBUILD"
    # An .install scriptlet is named by the PKGBUILD and has to travel with it,
    # or the AUR package installs without ever running it.
    inst=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1; printf '%s' "${install:-}" )
    if [ -n "$inst" ] && [ -f "$BASE/$name/$inst" ]; then
        cp "$BASE/$name/$inst" "$d/$inst"
    fi
    ( cd "$BASE/$name" && makepkg --printsrcinfo ) > "$d/.SRCINFO"

    ver=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1; printf '%s-%s' "$pkgver" "$pkgrel" )
    printf '  %-14s %-10s %s\n' "$name" "$ver" "$d"
    n=$((n + 1))
done

echo ""
echo "$n package(s) exported to $out"
echo "⚠ Each needs its source published first — tools/publish-sources.sh --list"
