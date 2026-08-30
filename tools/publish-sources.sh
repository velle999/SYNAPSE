#!/usr/bin/env bash
#
# publish-sources.sh — put each component's source tarball where somebody
# without this checkout can reach it.
#
# ── Why this exists ─────────────────────────────────────────────────────────
#
# Every in-house PKGBUILD builds from `$pkgname-$pkgver.tar.gz`, a file
# build-all.sh assembles from the working tree and .gitignore keeps out of the
# repo. That is right for the inner loop and it means the packages are
# unbuildable by anybody else: `makepkg` in syn-play/ on a stock Arch box fails
# at "Retrieving sources" and there is nowhere for it to look.
#
# So the same tarball is attached to a GitHub release, and the PKGBUILD's
# source=() line names both: the bare filename first, the release URL after.
# makepkg uses a file that is already there and downloads only when it is not —
# so this changes nothing for anyone building from the checkout.
#
# ⛔ THE TAG CARRIES THE pkgrel: `<pkgname>-<pkgver>-<pkgrel>`. A component's
# pkgrel is its real version here (synui is 0.1.0-560), preflight.sh refuses a
# source edit that does not bump it, and the PKGBUILD composes its own URL from
# it — so a released asset can never be the wrong source for the PKGBUILD that
# points at it, and re-running this after a bump publishes exactly what changed.
#
# ⚠ REPRODUCIBLE, SO IT CAN BE CHECKED. collect-source.sh sorts entries and
# zeroes timestamps and ownership, so anybody can re-derive the published bytes
# from the tagged commit and compare. (The PKGBUILDs still carry
# sha256sums=('SKIP') — see packaging/README.md for why they must.)
#
# Usage:
#   tools/publish-sources.sh --dry-run        # what would be published
#   tools/publish-sources.sh                  # publish what is missing
#   tools/publish-sources.sh --force syn-play # re-upload one, replacing the asset
#   tools/publish-sources.sh --list           # the external set and its state
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

BASE=$(cd "$(dirname "$0")/.." && pwd)
cd "$BASE"

# ⛔ THE EXTERNAL SET, AND THE ONLY PLACE IT IS WRITTEN DOWN. A name here must
# have a PKGBUILD whose source=() names the release URL, and vice versa;
# preflight.sh checks both directions, because a component in one and not the
# other is either an asset nobody can use or a PKGBUILD that 404s for everybody
# outside this repo.
#
# ⚠ Deliberately NOT every component. synapd needs a pinned llama.cpp build,
# synapse_kmod is DKMS against a running kernel, and syn-install / syn-update
# manage a SynapseOS installation — each of those means something different, or
# nothing, on a machine this project does not own. scenefx0.5 is absent for the
# opposite reason: it already builds from an upstream tarball, so it needs
# nothing from here.
EXTERNAL=(
    # the desktop
    synui syntty
    # the applications
    syn-cal syn-clean syn-disks syn-edit synfiles syn-play syn-settings
    synsh synstudio syn-vault
)

dry=0; force=0; list=0; only=()
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run|-n) dry=1 ;;
        --force|-f)   force=1 ;;
        --list|-l)    list=1 ;;
        -h|--help)    sed -n '2,40p' "$0"; exit 0 ;;
        -*)           echo "publish-sources: unknown option $1" >&2; exit 2 ;;
        *)            only+=("$1") ;;
    esac
    shift
done

want() {
    [ ${#only[@]} -eq 0 ] && return 0
    local n; for n in "${only[@]}"; do [ "$n" = "$1" ] && return 0; done
    return 1
}

if [ "$list" -eq 0 ] && [ "$dry" -eq 0 ]; then
    command -v gh >/dev/null || { echo "publish-sources: gh is not installed" >&2; exit 1; }
    gh auth status >/dev/null 2>&1 || {
        echo "publish-sources: gh is not logged in — run \`gh auth login\`" >&2; exit 1; }
fi

# ⚠ A DIRTY TREE PUBLISHES SOMETHING NO COMMIT CONTAINS. The tarball is built
# from the WORKING TREE, so an uncommitted edit would ship inside an asset
# nobody can re-derive from the tag — which is the one property this format
# promises.
if [ "$list" -eq 0 ] && [ "$dry" -eq 0 ] && [ -n "$(git status --porcelain)" ]; then
    echo "publish-sources: the tree has uncommitted changes." >&2
    echo "                 Commit and push first — an asset built from an" >&2
    echo "                 unpublished edit cannot be re-derived from its tag." >&2
    exit 1
fi

pkgfield() {  # pkgfield <component> <field>
    ( set +u; . "$BASE/$1/PKGBUILD" >/dev/null 2>&1; printf '%s' "${!2}" )
}

rc=0
for name in "${EXTERNAL[@]}"; do
    want "$name" || continue
    [ -f "$BASE/$name/PKGBUILD" ] || { echo "  MISSING  $name has no PKGBUILD"; rc=1; continue; }

    pkgver=$(pkgfield "$name" pkgver)
    pkgrel=$(pkgfield "$name" pkgrel)
    tag="$name-$pkgver-$pkgrel"
    asset="$name-$pkgver.tar.gz"

    have=""
    if command -v gh >/dev/null 2>&1; then
        gh release view "$tag" >/dev/null 2>&1 && have=yes
    fi

    if [ "$list" -eq 1 ]; then
        printf '  %-14s %-10s %s\n' "$name" "$pkgver-$pkgrel" \
               "$([ -n "$have" ] && echo published || echo 'not published')"
        continue
    fi

    if [ -n "$have" ] && [ "$force" -eq 0 ]; then
        printf '  ok       %-14s %s already published\n' "$name" "$tag"
        continue
    fi

    if [ "$dry" -eq 1 ]; then
        printf '  would    %-14s %s  <- %s\n' "$name" "$tag" "$asset"
        continue
    fi

    # ⚠ synui owns its own list of what goes in the tarball (its mktarball.sh
    # knows which top-level dirs it compiles), so it is asked rather than
    # second-guessed. Everything else goes through the shared collector.
    if [ -x "$BASE/$name/mktarball.sh" ]; then
        ( cd "$BASE/$name" && ./mktarball.sh >/dev/null )
    else
        "$BASE/tools/collect-source.sh" "$name" >/dev/null
    fi
    src="$BASE/$name/$asset"
    [ -s "$src" ] || { echo "  FAILED   $name produced no tarball"; rc=1; continue; }

    notes="Source tarball for \`$name $pkgver-$pkgrel\`, so it can be built without a
checkout of this repository.

\`\`\`bash
curl -LO https://github.com/velle999/SYNAPSE/raw/main/$name/PKGBUILD
makepkg -si
\`\`\`

The PKGBUILD composes this URL from its own \`pkgver\`/\`pkgrel\`, so it can only
ever fetch the source it was written against. Rebuild the same bytes from the
checkout with \`tools/collect-source.sh $name\`.

See [packaging/README.md](https://github.com/velle999/SYNAPSE/blob/main/packaging/README.md)."

    if [ -n "$have" ]; then
        gh release upload "$tag" "$src" --clobber
        printf '  updated  %-14s %s\n' "$name" "$tag"
    else
        gh release create "$tag" "$src" \
            --title "$name $pkgver-$pkgrel (source)" \
            --notes "$notes" >/dev/null
        printf '  created  %-14s %s\n' "$name" "$tag"
    fi
done

exit $rc
