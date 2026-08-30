#!/usr/bin/env bash
#
# publish-sources.sh — put each component where somebody without this checkout
# can install it: its own repository, holding its PKGBUILD and its sources.
#
# ── Why this exists ─────────────────────────────────────────────────────────
#
# Every in-house PKGBUILD builds from `$pkgname-$pkgver.tar.gz`, a file
# build-all.sh assembles from the working tree and .gitignore keeps out of the
# repo. That is right for the inner loop and it means the packages are
# unbuildable by anybody else: `makepkg` in syn-play/ on a stock Arch box fails
# at "Retrieving sources" and there is nowhere for it to look.
#
# So the same tarball is attached to a release, and the PKGBUILD's source=()
# line names both: the bare filename first, the release URL after. makepkg uses
# a file that is already there and downloads only when it is not — so this
# changes nothing for anyone building from the checkout.
#
# ⛔ ONE REPOSITORY PER PACKAGE, AND *NOT* THIS ONE. These went to SYNAPSE's
# releases first, and that was wrong twice over: twelve component tarballs per
# release round buried the ISO downloads, and because GitHub calls the newest
# release "Latest", the project's release badge and every /releases/latest link
# resolved to a source tarball instead of the operating system. A package's
# sources belong with its PKGBUILD, at github.com/velle999/<pkgname>.
#
# ⛔ THE TAG IS <pkgver>-<pkgrel>. A component's pkgrel is its real version here
# (synui is 0.1.0-561), preflight.sh refuses a source edit that does not bump
# it, and the PKGBUILD composes its own URL from it — so a released asset can
# never be the wrong source for the PKGBUILD that points at it, and re-running
# this after a bump publishes exactly what changed.
#
# ⚠ REPRODUCIBLE, SO IT CAN BE CHECKED. collect-source.sh sorts entries and
# zeroes timestamps and ownership, so anybody can re-derive the published bytes
# from the tagged commit and compare. (The PKGBUILDs still carry
# sha256sums=('SKIP') — see packaging/README.md for why they must.)
#
# Usage:
#   tools/publish-sources.sh --list           # every package and its state
#   tools/publish-sources.sh --dry-run        # what would be published
#   tools/publish-sources.sh                  # publish what is missing
#   tools/publish-sources.sh --force syn-play # re-upload one, replacing the asset
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

BASE=$(cd "$(dirname "$0")/.." && pwd)
cd "$BASE"

OWNER=velle999

# ⛔ THE EXTERNAL SET, AND THE ONLY PLACE IT IS WRITTEN DOWN. A name here must
# have a PKGBUILD whose source=() names its release URL, and vice versa;
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
        -h|--help)    sed -n '2,45p' "$0"; exit 0 ;;
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

if [ "$dry" -eq 0 ]; then
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
    repo="$OWNER/$name"
    tag="$pkgver-$pkgrel"
    asset="$name-$pkgver.tar.gz"

    have_repo=""; have_rel=""
    if command -v gh >/dev/null 2>&1; then
        gh repo view "$repo" >/dev/null 2>&1 && have_repo=yes
        [ -n "$have_repo" ] && gh release view "$tag" --repo "$repo" >/dev/null 2>&1 && have_rel=yes
    fi

    if [ "$list" -eq 1 ]; then
        printf '  %-14s %-10s %-9s %s\n' "$name" "$tag" \
               "$([ -n "$have_repo" ] && echo 'repo ok' || echo 'NO REPO')" \
               "$([ -n "$have_rel" ] && echo published || echo 'not published')"
        continue
    fi

    if [ -n "$have_rel" ] && [ "$force" -eq 0 ]; then
        printf '  ok        %-13s %s %s\n' "$name" "$repo" "$tag"
        continue
    fi

    if [ "$dry" -eq 1 ]; then
        printf '  would     %-13s %s %s  <- %s%s\n' "$name" "$repo" "$tag" "$asset" \
               "$([ -n "$have_repo" ] || echo '  (creating the repository)')"
        continue
    fi

    # ── the package repository ──────────────────────────────────────────────
    #
    # ⚠ REGENERATED FIRST, ALWAYS. The exported repo holds a COPY of the
    # PKGBUILD; publishing a release for a version whose PKGBUILD was never
    # pushed would give somebody a clone that fetches a tarball its own
    # PKGBUILD does not name.
    "$BASE/packaging/git-export.sh" "$name" >/dev/null

    d="$BASE/packaging/out/$name"
    if [ -z "$have_repo" ]; then
        desc=$(pkgfield "$name" pkgdesc)
        gh repo create "$repo" --public \
            --description "${desc:-$name — a SynapseOS component}" \
            --homepage "https://github.com/velle999/SYNAPSE" >/dev/null
        printf '  created   %-13s %s\n' "$name" "$repo"
    fi
    git -C "$d" push -q --set-upstream origin main

    # ── the source release, in that repository ──────────────────────────────
    if [ -x "$BASE/$name/mktarball.sh" ]; then
        ( cd "$BASE/$name" && ./mktarball.sh >/dev/null )
    else
        "$BASE/tools/collect-source.sh" "$name" >/dev/null
    fi
    src="$BASE/$name/$asset"
    [ -s "$src" ] || { echo "  FAILED    $name produced no tarball"; rc=1; continue; }

    notes="Source for \`$name $tag\`, so it can be built without a checkout of
[the SynapseOS monorepo](https://github.com/velle999/SYNAPSE).

\`\`\`bash
git clone https://github.com/$repo
cd $name && makepkg -si
\`\`\`

The PKGBUILD composes this URL from its own \`pkgver\`/\`pkgrel\`, so it can only
ever build the source it was written against. The tarball is reproducible:
\`tools/collect-source.sh $name\` at the matching commit re-derives it byte for
byte."

    if [ -n "$have_rel" ]; then
        gh release upload "$tag" "$src" --repo "$repo" --clobber
        printf '  updated   %-13s %s %s\n' "$name" "$repo" "$tag"
    else
        gh release create "$tag" "$src" --repo "$repo" \
            --title "$name $tag" --notes "$notes" >/dev/null
        printf '  published %-13s %s %s\n' "$name" "$repo" "$tag"
    fi
done

exit $rc
