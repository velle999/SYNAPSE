#!/usr/bin/env bash
#
# cleanroom.sh — can somebody who does not have this repository build these?
#
# ⛔ THE FAILURE THIS EXISTS FOR IS SILENT AND ONE-SIDED. A component builds
# here from a working tree that has everything in it; what ships to an outsider
# is the SOURCE TARBALL, and that is an allowlist
# (tools/collect-source.sh, or the component's own mktarball.sh). A top-level
# directory missing from that list is simply absent, and nothing warns: makepkg
# is green here, the package installs here, and the build fails only on a
# machine nobody in this repo is sitting at. synui's quickshell-antiquity/ sat
# fully written and unshippable for three days exactly that way.
#
# So: for each externally-installable component, assemble its tarball, take it
# and the PKGBUILD to an empty directory with nothing else in it, and build.
#
# ⚠ IT IS NOT THE SAME AS `makepkg` IN THE COMPONENT'S OWN DIRECTORY. That one
# extracts into src/ beside the real sources, so a file the tarball forgot is
# still on disk one level up. The whole point here is that it is not.
#
# ⚠ WHAT THIS DOES NOT PROVE: that the release asset EXISTS. The URL is only
# reached when the file is absent, and this puts it there on purpose — see
# `tools/publish-sources.sh --list` for what is actually published.
#
# Usage:
#   packaging/cleanroom.sh              # every external component
#   packaging/cleanroom.sh syn-play     # one of them
#   packaging/cleanroom.sh --check      # run each component's test suite too
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -uo pipefail

BASE=$(cd "$(dirname "$0")/.." && pwd)
cd "$BASE"

check=0; only=()
while [ $# -gt 0 ]; do
    case "$1" in
        --check|-c) check=1 ;;
        -h|--help)  sed -n '2,30p' "$0"; exit 0 ;;
        -*) echo "cleanroom: unknown option $1" >&2; exit 2 ;;
        *)  only+=("$1") ;;
    esac
    shift
done

mapfile -t EXTERNAL < <(
    block=$(sed -n '/^EXTERNAL=(/,/^)/p' tools/publish-sources.sh)
    [ -n "$block" ] || exit 1
    ( set +u; eval "$block"; printf '%s\n' "${EXTERNAL[@]}" )
)
[ ${#EXTERNAL[@]} -gt 0 ] || { echo "cleanroom: could not read EXTERNAL" >&2; exit 1; }

want() {
    [ ${#only[@]} -eq 0 ] && return 0
    local n; for n in "${only[@]}"; do [ "$n" = "$1" ] && return 0; done
    return 1
}

T=$(mktemp -d -p "${TMPDIR:-/var/tmp}" cleanroom-XXXXXX)
trap 'rm -rf "$T"' EXIT

fails=0; n=0
echo "cleanroom — building each component from its tarball alone"
echo ""

for name in "${EXTERNAL[@]}"; do
    want "$name" || continue
    n=$((n + 1))

    pkgver=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1; printf '%s' "$pkgver" )
    asset="$name-$pkgver.tar.gz"

    if [ -x "$BASE/$name/mktarball.sh" ]; then
        ( cd "$BASE/$name" && ./mktarball.sh ) >/dev/null 2>&1
    else
        "$BASE/tools/collect-source.sh" "$name" >/dev/null 2>&1
    fi
    if [ ! -s "$BASE/$name/$asset" ]; then
        printf '  FAIL  %-14s no source tarball was produced\n' "$name"
        fails=$((fails + 1)); continue
    fi

    d="$T/$name"
    mkdir -p "$d"
    cp "$BASE/$name/PKGBUILD" "$d/"
    cp "$BASE/$name/$asset"   "$d/"
    inst=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1; printf '%s' "${install:-}" )
    [ -n "$inst" ] && [ -f "$BASE/$name/$inst" ] && cp "$BASE/$name/$inst" "$d/"

    args=(--noconfirm --noarchive)
    [ "$check" -eq 1 ] || args+=(--nocheck)

    if ( cd "$d" && timeout 1800 makepkg "${args[@]}" ) >"$d/log" 2>&1; then
        printf '  ok    %-14s %s\n' "$name" "$pkgver"
    else
        printf '  FAIL  %-14s see below\n' "$name"
        sed -n '/error\|Error\|No such file\|not found\|ERROR/p' "$d/log" | head -6 | sed 's/^/        /'
        tail -4 "$d/log" | sed 's/^/        /'
        fails=$((fails + 1))
    fi
done

echo ""
if [ "$fails" -eq 0 ]; then
    echo "$n component(s) build from their tarball alone"
else
    echo "$fails of $n component(s) cannot be built outside this checkout"
    exit 1
fi
