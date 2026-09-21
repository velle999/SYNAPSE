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
# ⚠ AND SIGNED. Every tarball goes up with a detached `.sig` made with the
# update key (syn-update/synapseos-update.key), and the exported PKGBUILD names
# it with validpgpkeys, so makepkg refuses a tarball the project did not sign.
# A release published before that gets its signature on the next run — only if
# this tree makes the same files; see backfill_signature.
#
# Usage:
#   tools/publish-sources.sh --list           # every package and its state
#   tools/publish-sources.sh --dry-run        # what would be published
#   tools/publish-sources.sh                  # publish what is missing
#   tools/publish-sources.sh --force syn-play # re-upload one, replacing the asset
#
# Every run also re-asserts each repository's TOPICS from the table below —
# there is no separate command for it, and none is wanted: the table is the
# answer, so applying it is not an occasion.
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
    syn-cal syn-clean syn-disks syn-edit synfiles syn-play syn-scan syn-settings
    synsh synstudio syn-vault
    # packages, security and gaming — each installs and runs on plain Arch
    synpkg syn-arsenal syn-model syn-confine synguard syn-arcade syn-gfn
    syn-remote
    # the AI stack, and the package that lets a stranger satisfy it with the
    # distribution's own llama.cpp instead of a tree only an ISO build stages
    synapse-llama-system synapd synnet
)

# ⛔ AND THE ONES THAT ARE DELIBERATELY NOT PUBLISHED, WITH THE REASON.
#
# This table is the other half of EXTERNAL, and preflight.sh requires every
# component to be in one or the other. Without it a component is external by
# omission — which is how synpkg, the package manager, sat unpublishable for
# forty-three releases while the check above reported twelve components in
# perfect agreement with themselves. A set that only validates its members
# cannot see the one that was never added.
#
# Resolving an entry means DELETING its line, exactly as the registration
# table above works.
declare -A NOT_EXTERNAL=(
    [synapse-llama]="the pinned llama.cpp build itself — assembled from llama-staging/, which is a build output and not source; synapse-llama-system is what satisfies its name elsewhere"
    [synapse_kmod]="DKMS against a running kernel; it means something different on every machine and nothing on most"
    [syn-install]="installs SynapseOS onto a disk — it has no meaning on a machine that is not being turned into one"
    [syn-firstboot]="the first-boot wizard of a SynapseOS installation, for the same reason"
    [syn-update]="updates an installed SynapseOS from this very repository; publishing it would publish a second way to be out of date"
    [syn]="the unified CLI, which is a front end to the other syn-* tools and does nothing without them"
    [vibe]="its name is already taken: github.com/velle999/vibe is vibe's OWN upstream repository, a different tree with its own history, and a packaging export pushed there would overwrite it. Publishing needs a repo under another name"
    [synapse-wallpapers]="wallpaper assets with no source to build; they travel in the ISO"
    [scenefx]="retired fork, kept only for the scene TUs its PKGBUILD documents"
    [scenefx0.5]="already self-sufficient — its source=() fetches from wlrfx/scenefx upstream"
    [chibi]="builds from its own upstream repository already"
    [synapse-voice]="the same: a pinned commit of chibi-llm, plus model downloads"
    [tepris]="the same"
    [nexus-chat]="the same"
    [cliamp]="the same"
    [fetch]="the same"
    [linux-wallpaperengine-pkg]="third-party packaging; the source is somebody else's repository"
    [samsung-m2020]="the same — a printer driver packaged from Samsung's ULD"
    [limine-mkinitcpio-hook]="the same"
    [limine-snapper-sync]="the same"
)

# ⚠ WHAT GITHUB CALLS TOPICS, AND THE ONLY PLACE THEY ARE WRITTEN DOWN.
#
# A repository holding one PKGBUILD is invisible without them: nobody browses
# to `velle999/syn-vault`, they search for `gocryptfs` or `file-manager`. The
# description is generated from pkgdesc and the homepage points back here;
# these are the third thing GitHub indexes and the only one nothing else sets.
#
# ⛔ SET, NOT ADDED. This list replaces a repository's topics outright, so this
# file stays the answer to "what is that repo tagged as" — `--add-topic` would
# accumulate whatever anybody ever tried and never remove it.
#
# ⚠ GitHub's rules: lower case, digits and hyphens, starting with a letter or
# digit, 35 characters each and at most 20 per repository. Anything else is
# rejected for the WHOLE call, so a typo here loses every topic on that repo
# rather than one.
TOPICS_COMMON="synapseos linux arch-linux pkgbuild"
declare -A TOPICS=(
    [synui]="wayland wayland-compositor wlroots compositor desktop-environment scenefx"
    [syntty]="terminal terminal-emulator wayland pty freetype"
    [syn-cal]="calendar caldav google-calendar icalendar scheduling"
    [syn-clean]="disk-cleanup cleaner shred secure-delete"
    [syn-scan]="antivirus malware-scanner clamav rkhunter security"
    [syn-disks]="disk-utility partitioning filesystem smart udisks"
    [syn-edit]="text-editor modal-editor vim-like syntax-highlighting"
    [synfiles]="file-manager file-browser thumbnails gvfs"
    [syn-play]="mpv media-player video-player music-player playlist"
    [syn-settings]="settings control-panel system-configuration"
    [synsh]="shell natural-language llm ai command-line"
    [synstudio]="photo-editing video-editing raw-image color-grading darkroom"
    [syn-vault]="encryption encrypted-filesystem gocryptfs fuse privacy"
    [synpkg]="package-manager pacman aur flatpak flathub"
    [syn-arsenal]="security-tools blackarch pentesting"
    [syn-model]="llm model-manager ai downloader"
    [syn-confine]="sandbox landlock security hardening"
    [synguard]="security ebpf intrusion-detection monitoring"
    [syn-arcade]="gaming game-overlay controller mangohud"
    [syn-gfn]="cloud-gaming geforce-now gaming"
    [syn-remote]="remote-desktop vnc wayland wlroots wayvnc"
    [synapd]="llm llama-cpp inference-server daemon ai"
    [synnet]="firewall nftables network-security daemon"
    [synapse-llama-system]="llama-cpp ggml meta-package ai"
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
#
# ⚠ SCOPED TO WHAT IS BEING PUBLISHED. A tarball is built from one component's
# subtree, so that subtree is what has to be committed for the asset to be
# re-derivable. Checking the whole repo means an unrelated component being
# worked on in the same checkout blocks a release that is itself clean — which
# it did, while `syn-update` on an installed machine was waiting for it. Naming
# no component still checks everything, because then everything is published.
if [ "$list" -eq 0 ] && [ "$dry" -eq 0 ]; then
    if [ ${#only[@]} -eq 0 ]; then dirty=$(git status --porcelain)
    else                           dirty=$(git status --porcelain -- "${only[@]}"); fi
    if [ -n "$dirty" ]; then
        echo "publish-sources: uncommitted changes in what you are publishing:" >&2
        echo "$dirty" | sed 's/^/                 /' >&2
        echo "                 Commit and push first — an asset built from an" >&2
        echo "                 unpublished edit cannot be re-derived from its tag." >&2
        exit 1
    fi
fi

pkgfield() {  # pkgfield <component> <field>
    ( set +u; . "$BASE/$1/PKGBUILD" >/dev/null 2>&1; printf '%s' "${!2}" )
}

# ── signing ─────────────────────────────────────────────────────────────────
#
# The update key signs, and it has no passphrase so that this runs unattended.
# Signatures are written to a temp dir, never beside the tarball: the tree has
# to stay clean for the next run's check above.
SIGN_FPR=$(gpg --batch --with-colons --show-keys "$BASE/syn-update/synapseos-update.key" 2>/dev/null |
           awk -F: '$1 == "fpr" { print $10; exit }')
[ -n "$SIGN_FPR" ] || { echo "publish-sources: cannot read syn-update/synapseos-update.key" >&2; exit 1; }
if [ "$list" -eq 0 ] && [ "$dry" -eq 0 ]; then
    gpg --batch --list-secret-keys "$SIGN_FPR" >/dev/null 2>&1 || {
        echo "publish-sources: the update-signing key $SIGN_FPR is not on this machine" >&2
        exit 1; }
fi
SIGDIR=$(mktemp -d); trap 'rm -rf "$SIGDIR"' EXIT

sign_file() {  # sign_file <file> → $SIGDIR/<basename>.sig
    local out="$SIGDIR/$(basename "$1").sig"
    rm -f "$out"
    gpg --batch --yes --quiet --local-user "$SIGN_FPR" --detach-sign --output "$out" "$1"
}

make_tarball() {  # make_tarball <component>
    if [ -x "$BASE/$1/mktarball.sh" ]; then
        ( cd "$BASE/$1" && ./mktarball.sh >/dev/null )
    else
        "$BASE/tools/collect-source.sh" "$1" >/dev/null
    fi
}

# The same member names, types, modes and contents — everything but the
# timestamps and owners an older, non-reproducible pack recorded.
same_files() {  # same_files <a.tar.gz> <b.tar.gz>
    local a b r=1
    a=$(mktemp -d); b=$(mktemp -d)
    if tar -xpzf "$1" -C "$a" && tar -xpzf "$2" -C "$b" &&
       diff -r --no-dereference "$a" "$b" >/dev/null &&
       [ "$(cd "$a" && find . -printf '%m %y %P\n' | sort)" = \
         "$(cd "$b" && find . -printf '%m %y %P\n' | sort)" ]; then
        r=0
    fi
    rm -rf "$a" "$b"
    return $r
}

# ⛔ A RELEASE PUBLISHED BEFORE SIGNING BEGAN IS SIGNED ONLY IF THIS TREE MAKES
# IT. Signing the asset GitHub serves would put the project's name on whatever
# is there now — the one thing this signature exists to stop taking on trust.
# So the tarball is rebuilt here and compared: the same bytes are signed as
# they are; the same files packed differently (an asset from before the packing
# was reproducible) are replaced by the reproducible pack and signed; anything
# else is refused and reported — an edit that never bumped pkgrel, or an asset
# somebody changed. The exported PKGBUILD then stays unsigned until it is fixed.
backfill_signature() {  # backfill_signature <name> <repo> <tag> <asset>
    local name=$1 repo=$2 tag=$3 asset=$4 src tmp
    make_tarball "$name"
    src="$BASE/$name/$asset"
    [ -s "$src" ] || { echo "  FAILED    $name produced no tarball"; return 1; }
    tmp=$(mktemp -d)
    if ! gh release download "$tag" --repo "$repo" --pattern "$asset" -D "$tmp" >/dev/null 2>&1; then
        rm -rf "$tmp"
        echo "  FAILED    $name: could not download the published $asset"
        return 1
    fi
    if cmp -s "$src" "$tmp/$asset"; then
        sign_file "$src"
        gh release upload "$tag" "$SIGDIR/$asset.sig" --repo "$repo" --clobber >/dev/null
        printf '  signed    %-13s %s %s\n' "$name" "$repo" "$tag"
    elif same_files "$src" "$tmp/$asset"; then
        sign_file "$src"
        gh release upload "$tag" "$src" "$SIGDIR/$asset.sig" --repo "$repo" --clobber >/dev/null
        printf '  signed    %-13s %s %s  (same files, repacked reproducibly)\n' "$name" "$repo" "$tag"
    else
        printf '  REFUSED   %-13s %s: the published %s holds different files from this tree — not signed\n' \
               "$name" "$tag" "$asset"
        rm -rf "$tmp"
        return 1
    fi
    rm -rf "$tmp"
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

    have_repo=""; have_rel=""; have_sig=""; assets=""
    if command -v gh >/dev/null 2>&1; then
        gh repo view "$repo" >/dev/null 2>&1 && have_repo=yes
        [ -n "$have_repo" ] &&
            assets=$(gh release view "$tag" --repo "$repo" --json assets --jq '.assets[].name' 2>/dev/null) &&
            have_rel=yes
        grep -qxF "$asset.sig" <<<"$assets" && have_sig=yes
    fi
    has_src=""; [ -n "$(pkgfield "$name" source)" ] && has_src=yes

    if [ "$list" -eq 1 ]; then
        printf '  %-14s %-10s %-9s %-13s %s\n' "$name" "$tag" \
               "$([ -n "$have_repo" ] && echo 'repo ok' || echo 'NO REPO')" \
               "$([ -n "$have_rel" ] && echo published || echo 'not published')" \
               "$([ -z "$has_src" ] && echo '' || { [ -n "$have_sig" ] && echo signed || echo UNSIGNED; })"
        continue
    fi

    if [ "$dry" -eq 1 ]; then
        if [ -n "$have_rel" ] && [ -n "$has_src" ] && [ -z "$have_sig" ] && [ "$force" -eq 0 ]; then
            printf '  would     %-13s %s  <- sign %s, if this tree makes the same files\n' \
                   "$name" "$repo" "$tag"
        elif [ -n "$have_rel" ] && [ "$force" -eq 0 ]; then
            printf '  would     %-13s %s  <- the repository; %s is published already\n' \
                   "$name" "$repo" "$tag"
        else
            printf '  would     %-13s %s %s  <- %s%s\n' "$name" "$repo" "$tag" "$asset" \
                   "$([ -n "$have_repo" ] || echo '  (creating the repository)')"
        fi
        continue
    fi

    # ── the package repository, ON EVERY RUN ────────────────────────────────
    #
    # ⚠ REGENERATED FIRST, ALWAYS. The exported repo holds a COPY of the
    # PKGBUILD; publishing a release for a version whose PKGBUILD was never
    # pushed would give somebody a clone that fetches a tarball its own
    # PKGBUILD does not name.
    #
    # ⛔ AND PUSHED BEFORE THE RELEASE IS CONSIDERED, not after it is missing.
    # This sat below the "already published" early return, so the tree only
    # ever reached GitHub on a run that also had a release to upload — and a
    # change to the tree ALONE has no release to upload by definition. Adding
    # a README to all twelve therefore did nothing at all: every one printed
    # `ok` and pushed nothing, and the repositories kept showing a bare
    # PKGBUILD. Anything that is not the source tarball — the README, the
    # .SRCINFO, an .install scriptlet — is published by this line or not at
    # all.
    #
    # ⚠ THE SIGNATURE COMES FIRST, WHEN THE RELEASE IS OLDER THAN SIGNING. The
    # export names the .sig under --signed, and a PKGBUILD naming a file that
    # is not there fails for everybody who builds it. A release this run is
    # about to create is created with its signature, so it counts as signed.
    if [ -n "$have_rel" ] && [ -n "$has_src" ] && [ -z "$have_sig" ] && [ "$force" -eq 0 ]; then
        if backfill_signature "$name" "$repo" "$tag" "$asset"; then have_sig=yes; else rc=1; fi
    fi
    signed=()
    if [ -n "$have_sig" ] || [ -z "$have_rel" ] || [ "$force" -eq 1 ]; then signed=(--signed); fi
    "$BASE/packaging/git-export.sh" "${signed[@]}" "$name" >/dev/null

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
    #
    # ⚠ The repository is already current by here; this is only the asset.
    #
    # ⚠ AND SOME PACKAGES HAVE NO ASSET. synapse-llama-system installs no files;
    # it is a name and a list of dependencies, so its repository is complete
    # with the PKGBUILD alone and there is no tarball to attach. Publishing an
    # empty one would be publishing a promise nothing keeps.
    if [ -z "$has_src" ]; then
        printf '  ok        %-13s %s  (no sources — the PKGBUILD is the package)\n' \
               "$name" "$repo"
        continue
    fi

    if [ -n "$have_rel" ] && [ "$force" -eq 0 ]; then
        printf '  ok        %-13s %s %s\n' "$name" "$repo" "$tag"
        continue
    fi
    make_tarball "$name"
    src="$BASE/$name/$asset"
    [ -s "$src" ] || { echo "  FAILED    $name produced no tarball"; rc=1; continue; }
    sign_file "$src"

    notes="Source for \`$name $tag\`, so it can be built without a checkout of
[the SynapseOS monorepo](https://github.com/velle999/SYNAPSE).

\`\`\`bash
git clone https://github.com/$repo
cd $name && makepkg -si
\`\`\`

The PKGBUILD composes this URL from its own \`pkgver\`/\`pkgrel\`, so it can only
ever build the source it was written against. The tarball is reproducible:
\`tools/collect-source.sh $name\` at the matching commit re-derives it byte for
byte.

\`$asset.sig\` is its signature, made with the SynapseOS update key
(\`$SIGN_FPR\`), and makepkg checks it. Import the key once:

\`\`\`bash
curl -sL https://soslinux.org/synapseos-update-key.asc | gpg --import
\`\`\`"

    if [ -n "$have_rel" ]; then
        gh release upload "$tag" "$src" "$SIGDIR/$asset.sig" --repo "$repo" --clobber
        printf '  updated   %-13s %s %s\n' "$name" "$repo" "$tag"
    else
        gh release create "$tag" "$src" "$SIGDIR/$asset.sig" --repo "$repo" \
            --title "$name $tag" --notes "$notes" >/dev/null
        printf '  published %-13s %s %s\n' "$name" "$repo" "$tag"
    fi
done

# ── topics, once every repository that needs one exists ─────────────────────
#
# ⚠ A PASS OF ITS OWN, because the loop above returns early for a component
# whose release is already published — which is every component on almost
# every run, and exactly the ones whose topics would then never be revisited.
if [ "$list" -eq 0 ]; then
    for name in "${EXTERNAL[@]}"; do
        want "$name" || continue
        repo="$OWNER/$name"
        topics="$TOPICS_COMMON ${TOPICS[$name]:-}"

        if [ "$dry" -eq 1 ]; then
            printf '  topics    %-13s %s\n' "$name" "$topics"
            continue
        fi

        gh repo view "$repo" >/dev/null 2>&1 || continue

        args=()
        for t in $topics; do args+=(-f "names[]=$t"); done
        if gh api --method PUT "repos/$repo/topics" "${args[@]}" >/dev/null 2>&1; then
            printf '  topics    %-13s %s\n' "$name" "$topics"
        else
            printf '  FAILED    %-13s topics rejected: %s\n' "$name" "$topics"
            rc=1
        fi
    done
fi

exit $rc
