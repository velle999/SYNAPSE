#!/usr/bin/env bash
#
# git-export.sh — each externally-installable component as its own git
# repository, holding the PKGBUILD, a generated .SRCINFO and a README.
#
# ── Why one repo per package ────────────────────────────────────────────────
#
# Because that is the shape everything downstream wants. `git clone <url> &&
# makepkg -si` is the whole install, with no release asset to look up by hand;
# and it is EXACTLY the AUR's layout, so submitting one later is adding a remote
# and pushing, not a conversion. Nothing here is AUR-specific — these push to
# any git host.
#
# ⛔ IT COPIES THE PKGBUILD, IT DOES NOT REWRITE IT. The tree's already works
# outside this checkout: `source=()` names the release asset and makepkg only
# prefers a local file when one is beside it. A generator that "adapted" each
# PKGBUILD for publication would be a second set of depends and install rules,
# free to drift, and the drift would surface as a package that builds here and
# not for the person who filed the bug.
#
# ⚠ .SRCINFO IS GENERATED, NEVER WRITTEN. `makepkg --printsrcinfo` expands the
# same variables makepkg itself will, so the metadata a host displays cannot
# disagree with what is built. It is regenerated on every run.
#
# ⚠ IDENTITY IS SET PER REPO, ON PURPOSE. ~/.gitconfig here carries credential
# helpers and no user.email, so a freshly created repo has none and commits
# either fail or land unattributed — GitHub matches commits to accounts by
# email and shows an unmatched one as an anonymous contributor.
#
# Usage:
#   packaging/git-export.sh                 # every external component
#   packaging/git-export.sh syn-play        # one of them
#   packaging/git-export.sh -o /srv/pkgrepos
#   packaging/git-export.sh --origin 'https://github.com/velle999/pkg-%s.git'
#
# `--origin` takes a printf template; %s becomes the package name, and it
# defaults to github.com/velle999/<pkgname> — the same repository that carries
# the package's source releases. The remote is only recorded; nothing here
# pushes. `tools/publish-sources.sh` is what creates the repository and pushes.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

BASE=$(cd "$(dirname "$0")/.." && pwd)
cd "$BASE"

GIT_NAME="Velle Sinclair"
GIT_EMAIL="brncomputerhelp@gmail.com"

out="$BASE/packaging/out"
# The package's own repository, which is also where its source releases live.
# tools/publish-sources.sh creates it and pushes here.
origin_tpl="https://github.com/velle999/%s.git"
only=()
while [ $# -gt 0 ]; do
    case "$1" in
        -o|--out)    out=$2; shift ;;
        --origin)    origin_tpl=$2; shift ;;
        -h|--help)   sed -n '2,40p' "$0"; exit 0 ;;
        -*) echo "git-export: unknown option $1" >&2; exit 2 ;;
        *)  only+=("$1") ;;
    esac
    shift
done

# ⛔ THE SET IS READ, NOT RESTATED. tools/publish-sources.sh owns it; a copy here
# would let this export a package whose source nothing publishes — a repo that
# clones fine and cannot build.
mapfile -t EXTERNAL < <(
    block=$(sed -n '/^EXTERNAL=(/,/^)/p' tools/publish-sources.sh)
    [ -n "$block" ] || exit 1
    ( set +u; eval "$block"; printf '%s\n' "${EXTERNAL[@]}" )
)
[ ${#EXTERNAL[@]} -gt 0 ] || { echo "git-export: could not read EXTERNAL" >&2; exit 1; }

want() {
    [ ${#only[@]} -eq 0 ] && return 0
    local n; for n in "${only[@]}"; do [ "$n" = "$1" ] && return 0; done
    return 1
}

n=0
for name in "${EXTERNAL[@]}"; do
    want "$name" || continue
    [ -f "$BASE/$name/PKGBUILD" ] || { echo "git-export: no $name/PKGBUILD" >&2; exit 1; }

    d="$out/$name"
    mkdir -p "$d"

    cp "$BASE/$name/PKGBUILD" "$d/PKGBUILD"
    # An .install scriptlet is named BY the PKGBUILD and has to travel with it,
    # or the package installs without ever running it — silently, since pacman
    # does not check that a named scriptlet exists until build time.
    inst=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1; printf '%s' "${install:-}" )
    if [ -n "$inst" ] && [ -f "$BASE/$name/$inst" ]; then
        cp "$BASE/$name/$inst" "$d/$inst"
    fi

    # ⚠ makepkg --printsrcinfo must run in the package's own directory: it
    # sources the PKGBUILD, and several of ours read files beside them.
    ( cd "$BASE/$name" && makepkg --printsrcinfo ) > "$d/.SRCINFO"

    ver=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1; printf '%s-%s' "$pkgver" "$pkgrel" )

    # What a build leaves behind. Without this the first `makepkg -si` somebody
    # runs turns the clone dirty, and the next `git pull` refuses.
    cat > "$d/.gitignore" <<'IGN'
pkg/
src/
*.pkg.tar.zst
*.tar.gz
IGN

    # ── the README that repository shows ────────────────────────────────────
    #
    # ⚠ ASSEMBLED, NOT COPIED. The prose is written once in
    # packaging/readme/<name>.md; the version, the licence and the install
    # lines are generated here — those are exactly the parts that go stale the
    # moment a pkgrel moves and nobody thinks to re-read the paragraph around
    # them. A component with no prose file still gets a README, off its
    # pkgdesc, rather than the export failing over documentation.
    #
    # ⛔ AND THE PROSE LIVES IN packaging/, NOT BESIDE THE PKGBUILD.
    # preflight.sh gates every edit inside <name>/ on a pkgrel bump, and it is
    # right to: cliamp and fetch really do install a top-level README.md into
    # /usr/share/doc, so for them a README edit DOES change the package. A
    # README in the component directory would therefore either demand twelve
    # version bumps for a typo, or need a blanket exemption that is wrong for
    # those two. Keeping it here means the gate never has to be taught a
    # special case.
    # ⚠ Comma-joined: synui carries four licences and "GPL MIT Apache CC" run
    # together reads as one long identifier rather than as a list.
    lic=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1
           IFS=,; printf '%s' "${license[*]}" )
    lic=${lic//,/, }
    {
        if [ -f "$BASE/packaging/readme/$name.md" ]; then
            cat "$BASE/packaging/readme/$name.md"
        else
            desc=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1
                    printf '%s' "${pkgdesc:-}" )
            printf '# %s\n\n%s\n' "$name" "${desc:-A SynapseOS component.}"
        fi

        printf '\n## Install\n\n'
        printf '%s\n' '```bash'
        printf 'git clone https://github.com/velle999/%s\n' "$name"
        printf 'cd %s && makepkg -si\n' "$name"
        printf '%s\n' '```'
        printf '\n%s\n' 'makepkg fetches the source for this PKGBUILD'"'"'s exact version from this'
        printf '%s\n' 'repository'"'"'s releases, so a clone can only ever build the source it was'
        printf '%s\n' 'written against. `.SRCINFO` lists what it needs.'

        printf '\n## Where this comes from\n\n'
        printf '%s\n' 'Developed in [the SynapseOS monorepo](https://github.com/velle999/SYNAPSE),'
        printf 'in `%s/`. **This repository is generated from it** — the PKGBUILD, a\n' "$name"
        printf '%s\n' 'generated `.SRCINFO` and this README — so issues and patches belong there.'
        printf '\n%s %s · %s\n' "$name" "$ver" "${lic:-GPL-2.0-or-later}"
    } > "$d/README.md"

    if [ ! -d "$d/.git" ]; then
        git -C "$d" init -q -b main
        git -C "$d" config user.name  "$GIT_NAME"
        git -C "$d" config user.email "$GIT_EMAIL"
    fi
    # Reasserted rather than only set at init: a repo created before this script
    # existed, or copied from somewhere, would otherwise keep the wrong one.
    git -C "$d" config user.name  "$GIT_NAME"
    git -C "$d" config user.email "$GIT_EMAIL"

    # The AUR's own URL, recorded and not pushed. Whenever submissions open,
    # `git push aur main` is the whole of it.
    git -C "$d" remote get-url aur >/dev/null 2>&1 \
        || git -C "$d" remote add aur "ssh://aur@aur.archlinux.org/$name.git"
    if [ -n "$origin_tpl" ]; then
        # shellcheck disable=SC2059
        url=$(printf "$origin_tpl" "$name")
        if git -C "$d" remote get-url origin >/dev/null 2>&1; then
            git -C "$d" remote set-url origin "$url"
        else
            git -C "$d" remote add origin "$url"
        fi
    fi

    git -C "$d" add -A
    if git -C "$d" diff --cached --quiet 2>/dev/null; then
        printf '  %-14s %-10s unchanged\n' "$name" "$ver"
    else
        git -C "$d" commit -q -m "$name $ver" \
            -m "Built from https://github.com/velle999/SYNAPSE — see packaging/README.md.

The source tarball is fetched from that repository's \`$name-$ver\` release;
the PKGBUILD composes the URL from its own pkgver/pkgrel, so it can only ever
build the source it was written against."
        printf '  %-14s %-10s committed\n' "$name" "$ver"
    fi
    n=$((n + 1))
done

echo ""
echo "$n repository(ies) in $out"
echo ""
echo "  Push one:   git -C $out/<name> push origin main"
echo "  To the AUR: git -C $out/<name> push aur main      (remote already set)"
echo ""
echo "⚠ Each needs its source published — tools/publish-sources.sh --list"
