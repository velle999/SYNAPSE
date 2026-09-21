#!/usr/bin/env bash
# repro-check.sh — build a component twice from one commit; are the packages
# the same bytes?
#
# docs/SECURITY-ROADMAP.md §4. Signing says who built an image; this says the
# source in this repository is what is in it — which only holds if building
# the same commit twice gives the same package.
#
# Usage:
#   tools/repro-check.sh synguard syn-settings    # these components
#   tools/repro-check.sh --all                    # every component build-all.sh builds
#   tools/repro-check.sh --all --skip synapse-llama   # …but not these
#   KEEP=1 tools/repro-check.sh synui             # keep the work dir to look at
#
# What it does, per component:
#   1. clones HEAD — the COMMITTED tree, never the working tree, so an edit you
#      have not committed cannot make a build look reproducible or not;
#   2. prepares it exactly as build-all.sh does (mktarball.sh, or
#      tools/collect-source.sh, or nothing for a script package) — the mapping
#      is READ from build-all.sh, not kept a second time here;
#   3. builds it twice with makepkg, SOURCE_DATE_EPOCH set to the time of the
#      last commit that touched the component (tools/source-date-epoch.sh);
#   4. compares the packages, and when they differ, says which files do.
#
# Nothing is installed and nothing needs root: makepkg runs without -s, so a
# component whose dependencies are not installed here is reported as not
# buildable rather than fetched. Builds happen in the clone, never in ~/SYNAPSE.
#
# Exit: 0 when every component built was identical, 1 when any differed, 2 on
# a usage or setup error. "not built here" does not fail the run.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

root=$(git -C "$(dirname "$(realpath "$0")")/.." rev-parse --show-toplevel)

comps=() skip=() all=0
while [ $# -gt 0 ]; do
    case $1 in
        --all)  all=1 ;;
        --skip) shift; skip+=("$1") ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        -*) echo "repro-check: unknown option $1" >&2; exit 2 ;;
        *)  comps+=("$1") ;;
    esac
    shift
done

# name → how build-all.sh builds it, from build-all.sh's own call lines.
declare -A how
while read -r fn name; do
    how[$name]=$fn
done < <(sed -n -E 's/^[[:space:]]*(build_component|build_script_pkg|build_vendored_pkg)[[:space:]]+([A-Za-z0-9_.-]+).*/\1 \2/p' "$root/build-all.sh")

if (( all )); then
    mapfile -t comps < <(printf '%s\n' "${!how[@]}" | sort)
fi
[ ${#comps[@]} -gt 0 ] || { sed -n '9,14p' "$0" >&2; exit 2; }
for s in "${skip[@]}"; do
    comps=("${comps[@]/#$s}")
done

work=$(mktemp -d "${TMPDIR:-/tmp}/repro.XXXXXX")
# chmod before rm: a package's extracted sources can be read-only, and rm alone
# then leaves the whole work dir behind.
if [ -z "${KEEP:-}" ]; then trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
else echo "repro-check: work dir $work"; fi
git clone -q --no-hardlinks "$root" "$work/tree" || { echo "repro-check: clone failed" >&2; exit 2; }
tree=$work/tree
echo "repro-check: HEAD $(git -C "$tree" rev-parse --short HEAD)"

# One download cache for both builds of everything: a component that fetches
# an upstream tarball or a model fetches it once, and both builds read the
# same bytes — which is also what makes a download-dependent package
# comparable at all.
export SRCDEST=$work/srcdest
mkdir -p "$SRCDEST"

# Everything the last build left, and nothing else: git clean removes what is
# UNTRACKED in the component and cannot touch a tracked file. That matters,
# because src/ is makepkg's extraction dir for a script package and the actual
# SOURCE for a C one (never rm -rf src in synui). chmod first: a package's own
# sources can extract read-only (samsung-m2020's driver tarball does), and then
# neither makepkg's second extraction nor a plain rm can replace them.
clean() {
    local c=$1
    chmod -R u+w "$tree/$c" 2>/dev/null
    git -C "$tree" clean -xdfq -- "$c"
}

prepare() {
    local c=$1
    case ${how[$c]} in
        build_component)
            if [ -x "$tree/$c/mktarball.sh" ]; then
                (cd "$tree/$c" && ./mktarball.sh >/dev/null)
            else
                "$tree/tools/collect-source.sh" "$c" >/dev/null
            fi ;;
    esac
}

build_once() {   # $1 component, $2 output dir, $3 log
    local c=$1
    clean "$c"
    prepare "$c" || return 1
    (cd "$tree/$c" && SOURCE_DATE_EPOCH=$sde makepkg -f --nocheck --noconfirm) >"$3" 2>&1 \
        || return 1
    mkdir -p "$2"
    mv "$tree/$c"/*.pkg.tar.zst "$2"/ 2>/dev/null || return 1
}

# Why two packages differ, file by file.
explain() {   # $1 pkg A, $2 pkg B
    local a=$work/xa b=$work/xb
    rm -rf "$a" "$b"; mkdir -p "$a" "$b"
    bsdtar -xf "$1" -C "$a" 2>/dev/null
    bsdtar -xf "$2" -C "$b" 2>/dev/null
    (cd "$a" && find . -type f -o -type l | sort) >"$work/la"
    (cd "$b" && find . -type f -o -type l | sort) >"$work/lb"
    if ! cmp -s "$work/la" "$work/lb"; then
        echo "      the file LISTS differ:"
        diff "$work/la" "$work/lb" | grep '^[<>]' | head -10 | sed 's/^/        /'
    fi
    local f
    while read -r f; do
        [ -e "$b/$f" ] || continue
        cmp -s "$a/$f" "$b/$f" && continue
        echo "      differs: ${f#./}"
        case $f in
            ./.BUILDINFO|./.PKGINFO)
                diff "$a/$f" "$b/$f" | grep '^[<>]' | head -6 | sed 's/^/          /' ;;
        esac
    done <"$work/la"
}

identical=0 differed=0 unbuilt=0
declare -a bad=() nob=()
for c in "${comps[@]}"; do
    [ -n "$c" ] || continue
    if [ -z "${how[$c]:-}" ]; then
        echo "── $c: not a component build-all.sh builds"; unbuilt=$((unbuilt+1)); continue
    fi
    sde=$("$root/tools/source-date-epoch.sh" "$c") || { echo "── $c: no commit time"; continue; }
    printf '── %s  (%s, SOURCE_DATE_EPOCH=%s)\n' "$c" "${how[$c]#build_}" "$sde"
    if ! build_once "$c" "$work/out/$c/1" "$work/$c.1.log"; then
        echo "   not built here — $(grep -m1 -E 'ERROR|error:|Missing dependencies' "$work/$c.1.log" | cut -c1-110)"
        unbuilt=$((unbuilt+1)); nob+=("$c"); continue
    fi
    if ! build_once "$c" "$work/out/$c/2" "$work/$c.2.log"; then
        echo "   the SECOND build failed — $(grep -m1 -E 'ERROR|error:' "$work/$c.2.log" | cut -c1-110)"
        differed=$((differed+1)); bad+=("$c"); continue
    fi
    same=1
    for p in "$work/out/$c/1"/*.pkg.tar.zst; do
        q=$work/out/$c/2/$(basename "$p")
        if [ ! -e "$q" ]; then
            echo "   $(basename "$p"): only the first build made it"; same=0; continue
        fi
        if cmp -s "$p" "$q"; then
            echo "   identical  $(basename "$p")  $(sha256sum "$p" | cut -c1-16)"
        else
            echo "   DIFFERS    $(basename "$p")"
            explain "$p" "$q"
            same=0
        fi
    done
    if (( same )); then identical=$((identical+1)); else differed=$((differed+1)); bad+=("$c"); fi
done

echo
echo "repro-check: $identical identical, $differed differ, $unbuilt not built here"
[ ${#bad[@]} -eq 0 ] || echo "  differ: ${bad[*]}"
[ ${#nob[@]} -eq 0 ] || echo "  not built here: ${nob[*]}"
[ "$differed" -eq 0 ]
