#!/usr/bin/env bash
#
# collect-source.sh — the source tarball a component is built from.
#
# ⛔ ONE OWNER, BECAUSE THERE ARE NOW TWO CALLERS. build-all.sh assembles this
# tarball so `makepkg` has something to extract; tools/publish-sources.sh
# assembles the identical thing to attach to a GitHub release, so that anybody
# NOT holding this checkout can build the same component from the same bytes.
# Those two producing subtly different trees is exactly the failure this repo
# has already paid for elsewhere — a package that builds here and not there,
# discovered by somebody who cannot see either script.
#
# ⚠ A SCRIPT COMPONENT IS ITS TOP-LEVEL FILES, and those are collected from
# what git TRACKS rather than from the allowlist below — see the loop at the
# bottom of the selection. The same question decides src/: tracked means it is
# the component's source, untracked means it is makepkg's extraction dir.
#
# ⚠ THE DIRECTORY LIST IS AN ALLOWLIST. A top-level directory it does not name
# is simply absent from the tarball, and nothing warns: makepkg is green, the
# package installs, the files are not there. quickshell-antiquity/ sat fully
# written in synui's working copy for three days that way. Adding a top-level
# dir to a component means adding it HERE as well as to the PKGBUILD's install
# rules. (⛔ meson_options.txt / meson.options travel with meson.build for the
# same reason: syn-cal once shipped without its options file and the build died
# on `Unknown option` inside makepkg, long after the tree it was tested in built
# fine.)
#
# ⚠ A component that ships its own mktarball.sh owns its list instead, and this
# script refuses to second-guess it — that script knows which top-level dirs the
# component actually compiles.
#
# Usage:
#   tools/collect-source.sh <component> [outfile]
#
# With no outfile the tarball lands at <component>/<component>-<pkgver>.tar.gz,
# which is where the PKGBUILD's source=() line looks for it first.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

BASE=$(cd "$(dirname "$0")/.." && pwd)
name=${1:-}
[ -n "$name" ] || { echo "usage: tools/collect-source.sh <component> [outfile]" >&2; exit 2; }
[ -f "$BASE/$name/PKGBUILD" ] || { echo "collect-source: no such component: $name" >&2; exit 2; }

if [ -x "$BASE/$name/mktarball.sh" ]; then
    echo "collect-source: $name ships its own mktarball.sh — run that instead" >&2
    exit 2
fi

# ⛔ SOURCED, NOT SCRAPED. A pkgver composed from another variable reads back as
# the literal ${_pkgver} when a script greps for it, and vercmp ranks that above
# every real version — which is how syn-update once bricked itself.
pkgver=$( set +u; . "$BASE/$name/PKGBUILD" >/dev/null 2>&1; printf '%s' "$pkgver" )
[ -n "$pkgver" ] || { echo "collect-source: $name has no pkgver" >&2; exit 2; }

out=${2:-$BASE/$name/$name-$pkgver.tar.gz}
prefix="$name-$pkgver"

cd "$BASE"

# ⛔ GIT HAS TO ANSWER, OR THIS SHIPS AN EMPTY TARBALL IN SILENCE. Two of the
# selections below ask git which files are source: whether src/ is tracked, and
# which loose top-level files a script component is made of. A git that REFUSES
# — "detected dubious ownership", the checkout owned by another uid, which is
# the normal state inside a CI container — makes both answer "nothing", and the
# tarball comes out with a meson.build and no code. tar's own `|| true` below
# then hides the rest. Ask once, loudly, before believing any of it.
if ! git -C "$BASE" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "collect-source: git cannot read $BASE — refusing to guess at what is source" >&2
    echo "  (in a container this is usually: git config --global --add safe.directory $BASE)" >&2
    exit 1
fi

# Collect directories that exist.
dirs=()
# ⛔ src/ IS SOURCE FOR ONE KIND OF COMPONENT AND MAKEPKG'S STAGING FOR THE
# OTHER, AND ONLY GIT KNOWS WHICH.
#
# A meson component keeps its C in src/ and git tracks it. A SCRIPT component
# keeps its files loose at the top level, and its src/ is where makepkg
# extracted the last build — untracked, and full of copies. Sweeping that in
# ships "whatever you built last" as the source somebody else compiles, which
# is the opposite of the reproducibility this tarball exists to promise. It is
# also how a tarball for a script component came out holding a stale copy of
# its own scripts and nothing else.
#
# ⚠ preflight.sh's pkgrel gate makes the same split for the same reason, one
# file at a time: tracked means source, untracked means staging.
if [ -d "$name/src" ] && [ -n "$(git -C "$BASE" ls-files "$name/src" | head -n1)" ]; then
    dirs+=("$name/src/")
fi
[ -d "$name/include" ] && dirs+=("$name/include/")
[ -f "$name/meson.build" ] && dirs+=("$name/meson.build")
[ -f "$name/meson_options.txt" ] && dirs+=("$name/meson_options.txt")
[ -f "$name/meson.options" ]     && dirs+=("$name/meson.options")
[ -d "$name/data" ]    && dirs+=("$name/data/")
[ -d "$name/config" ]  && dirs+=("$name/config/")
[ -d "$name/systemd" ] && dirs+=("$name/systemd/")
[ -d "$name/sysusers" ] && dirs+=("$name/sysusers/")
[ -d "$name/tmpfiles" ] && dirs+=("$name/tmpfiles/")
[ -d "$name/rules" ]   && dirs+=("$name/rules/")
[ -d "$name/protocols" ] && dirs+=("$name/protocols/")
[ -d "$name/tests" ]   && dirs+=("$name/tests/")
# ⛔ MESSAGE CATALOGS, AND THEY ARE ENTERED BY meson. A component whose
# meson.build says subdir('po') and whose tarball does not carry po/ builds
# perfectly HERE — the directory is on disk either way — and fails on the first
# machine that starts from the tarball, with "Nonexistent build file
# 'po/meson.build'", after the commit has shipped. preflight's subdir check is
# what catches it, and it caught this one.
#
# ⚠ po-bar/ TOO, and now po-antiquity/. synui has three: po/ for the
# compositor's C, po-bar/ for the SYNAPSE bar's QML and po-antiquity/ for the
# Antiquity shell's — separate domains with separate last miles, each entered
# by its own subdir(). ⛔ synui does NOT come through this script (it ships
# synui/mktarball.sh and this one refuses it by name), so the list that has to
# stay right for synui is that file's contents=(), which cross-checks itself
# against every subdir() in meson.build. These two lines are the rule for
# every other component.
[ -d "$name/po" ]      && dirs+=("$name/po/")
[ -d "$name/po-bar" ]  && dirs+=("$name/po-bar/")
# synapse_kmod extras
[ -f "$name/Makefile" ]  && dirs+=("$name/Makefile")
[ -f "$name/dkms.conf" ] && dirs+=("$name/dkms.conf")
[ -d "$name/hooks" ]     && dirs+=("$name/hooks/")
[ -d "$name/tools" ]     && dirs+=("$name/tools/")
[ -f "$name/synapse_kmod.install" ] && dirs+=("$name/synapse_kmod.install")
# A pure-Python component (entry point, package, launcher) rather than the
# src/include/meson layout the C packages use. vibe is the only one and it owns
# its own mktarball.sh, so these are here to keep the rule rather than the case.
# ⚠ GATED ON main.py, not on the directory existing. `<name>/<name>/` is also
# what makepkg's GIT CACHE is called for a component sourced from a repo
# (chibi/chibi, tepris/tepris), and sweeping that into a source tarball would
# ship a clone of somebody else's project inside ours.
if [ -f "$name/main.py" ]; then
    dirs+=("$name/main.py")
    [ -d "$name/$name" ]     && dirs+=("$name/$name/")
    [ -d "$name/packaging" ] && dirs+=("$name/packaging/")
fi
# ⚠ GLOBBED, NOT NAMED. PKGBUILDs install top-level docs — synapse_kmod ships
# HARDENING.md to /usr/share/doc — and package() fails with "cannot stat" when
# one is missing. This was a named HARDENING.md line here and a glob in
# archiso/build.sh's copy, which is the drift that copy's own comments describe.
for _md in "$name"/*.md; do
    [ -f "$_md" ] && dirs+=("$_md")
done

# ⚠ AND THE LOOSE FILES A SCRIPT COMPONENT IS MADE OF. syn-gfn, syn-model and
# syn-arsenal have no src/ and no meson.build — the shell scripts, .desktop,
# .svg, units and polkit rules sit at the top level, and the allowlist above
# named none of them. Their tarballs came out holding tests/ and nothing else.
#
# ⛔ TRACKED ONLY, and never a dotfile, the PKGBUILD or an .install scriptlet:
# the first two are published beside the tarball by git-export.sh, and a
# .gitignore is not source. A meson component has no such files, so this adds
# nothing to the twelve that were already published — checked, not assumed.
while IFS= read -r _f; do
    case "$_f" in
        */*|.*|PKGBUILD|*.install|*.md|meson.build|meson_options.txt|meson.options) continue ;;
    esac
    [ -f "$name/$_f" ] && dirs+=("$name/$_f")
done < <(git -C "$BASE" ls-files "$name" | sed "s#^$name/##")

[ ${#dirs[@]} -gt 0 ] || { echo "collect-source: $name has nothing to collect" >&2; exit 2; }

# ⚠ SORTED, WITH THE CLOCK AND THE BUILD USER TAKEN OUT. Two runs at the same
# commit then produce the same bytes, so a released asset can be re-derived and
# compared rather than trusted. It costs nothing here and it is the only way an
# outsider can check that the tarball they downloaded is this tree.
#
# ⛔ AND YET sha256sums STAYS 'SKIP' IN THE PKGBUILDs — see packaging/README.md.
# A real checksum would make every local build fail the moment somebody edits a
# source file, because build-all.sh regenerates this tarball from the WORKING
# TREE and makepkg would then reject it.
tar czf "$out" \
    --transform "s|^$name/|$prefix/|" \
    --sort=name --mtime="UTC 2020-01-01" \
    --owner=0 --group=0 --numeric-owner \
    --exclude="$name/src/$name-$pkgver" \
    --exclude="$name/pkg" \
    --exclude="$name/src/pkg" \
    --exclude="$name/*.pkg.tar*" \
    --exclude="$name/*.tar.gz" \
    --exclude="$name/*.ko" \
    --exclude="$name/*.o" \
    --exclude="$name/*.mod*" \
    --exclude="$name/modules.order" \
    --exclude="$name/Module.symvers" \
    "${dirs[@]}" 2>/dev/null || true

[ -s "$out" ] || { echo "collect-source: $name produced no tarball" >&2; exit 1; }
echo "$out"
