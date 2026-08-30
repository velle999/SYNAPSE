#!/usr/bin/env bash
#
# preflight.sh — refuse a commit that cannot ship.
#
# Four bugs kept coming back, each of them silent, each of them costing a
# release or a day:
#
#   1. A source edit with no pkgrel bump. pacman compares pkgver-pkgrel; if the
#      installed release equals the tree's, nothing rebuilds and nothing
#      installs, however different the file is. The edit is committed, pushed,
#      applied — and absent.
#
#   2. A pkgver composed from another variable. syn-update used to scrape it
#      with sed and read back the LITERAL "${_pkgver}${_extver}", which vercmp
#      ranks ABOVE every real version, so the component was permanently out of
#      date and syn-update died on `unknown component` forever after.
#
#   3. A new component that never reached a list. There are four of them
#      (build-all.sh's KNOWN=, its build rules, syn-update's COMPONENTS,
#      archiso's PACKAGES) and no two are generated from each other — on
#      purpose, so a rename fails loudly in review. The cost of that choice is
#      that they drift, and they have drifted five times.
#
#   4. KNOWN= is not the build list. A name there with no build_component /
#      build_script_pkg / build_vendored_pkg rule is a legal argument that
#      builds nothing and exits 0, so syn-update reports success, publishes
#      nothing, and offers the identical update again on the next run, forever.
#
# Every one of them surfaces far from its cause — on velle's machine, after a
# push, as "syn-update keeps offering this" or "the menu row does nothing".
# This script answers all four before the commit exists.
#
# Usage:
#   tools/preflight.sh                # working tree + index, against HEAD
#   tools/preflight.sh --staged       # only what `git commit` would take
#   tools/preflight.sh --since <ref>  # audit a range: <ref>..HEAD
#   tools/preflight.sh --self-test    # prove the checks can still fail
#
# Exit 0 clean, 1 with findings.
#
# Every check is a fact about the repo, not a style opinion, so there is no
# suppression comment: a finding is fixed by bumping the pkgrel or by adding
# the name to the list it is missing from. The two tables below (EXEMPT_ISO,
# UNREGISTERED) are the exceptions, and each entry carries the reason in full.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

cd "$(dirname "$0")/.."

# ── Tables ───────────────────────────────────────────────────

# On the ISO but not a row in syn-install's component table, with the reason it
# still reaches an installed system — or why it should not.
#
# packages.x86_64 is what the LIVE environment gets; the component table is
# what the TARGET can be given, and nothing makes the two agree. That gap shipped an
# ISO carrying four Wallpaper Engine wallpapers whose picker found zero of them
# on every machine installed from it, because linux-wallpaperengine and
# synapse-wallpapers were in packages.x86_64 and in neither list here.
declare -A EXEMPT_INSTALLER=(
    [scenefx0.5]="a hard depends of synui, so pacman installs it with synui — naming it here as well would only be a second place to forget"
    [synapse-llama]="a hard depends of synapd, same as scenefx0.5 under synui"
    [limine-mkinitcpio-hook]="installed conditionally, inside the chroot, only on a limine install (syn-install.sh: arch-chroot /mnt pacman -S ... limine-mkinitcpio-hook). It must NOT arrive on a GRUB box — its hook shadows mkinitcpio's"
    [limine-snapper-sync]="limine + snapper only, chosen by the bootloader branch rather than the app list"
    # The one package whose absence from the target is the POINT. It ships
    # /usr/bin/syn-crypt, a disk partitioner; syn-update keeps it out of NEW[]
    # for the same reason, via its own NEVER_ADD.
    [syn-install]="the installer itself — it belongs to the ISO, and an installed system is expected not to have it"
)

# Components build-all.sh builds that are deliberately NOT on the ISO. Being
# buildable and being shipped are different questions and this is the only
# place that says which.
declare -A EXEMPT_ISO=(
    [samsung-m2020]="Samsung's EULA licenses the driver to the end user, not to us for redistribution — \`syn printer samsung\` installs it on demand instead. The PKGBUILD still exists and still builds; it just fetches from Samsung."
)

# Directories with a PKGBUILD that are in NEITHER syn-update's COMPONENTS nor
# its UNSUPPORTED map. Anything here is un-updatable on an installed system and
# nothing else in the repo records that, so an entry needs a reason that
# survives reading.
#
# This table was seeded with four when the script was written; three were
# genuine gaps and are gone. fetch and synapse-wallpapers are in COMPONENTS
# now — both are in packages.x86_64, so they were on every installed machine
# and updatable on none. limine-snapper-sync is in UNSUPPORTED, where
# syn-update reports the reason rather than skipping it in silence.
declare -A UNREGISTERED=(
    # RETIRED, not missing. The 0.4 fork, superseded by scenefx0.5 when synui
    # moved to wlroots 0.20; synui's depends names scenefx0.5 and nothing in
    # the tree names this one. It is in no list anywhere — not KNOWN=, not
    # COMPONENTS, not archiso PACKAGES, not packages.x86_64 — and the package
    # is not installed here, so it ships to nobody and updates for nobody,
    # which is correct for a retired tree.
    #
    # It is kept because it carries real local work (six wlroots 0.19.3 scene
    # TUs ported onto scenefx's own headers, see its PKGBUILD header), and git
    # history is a worse place to look for that than a directory. Deleting it
    # is a one-line change whenever that stops being true.
    [scenefx]="retired: the 0.4 fork superseded by scenefx0.5; in no build list and installed nowhere, kept for the ported scene TUs its PKGBUILD documents"

)

# Paths inside a component that do not reach the package, so editing them
# cannot require a pkgrel bump.
#
#   pkg/        makepkg's staging dir — not tracked source at all.
#   tests/      no PKGBUILD installs a tests/ tree. synpkg RUNS its tests in
#               check(), so an edit there can fail a build, but it still ships
#               nothing.
#
# ⚠ src/ IS NOT IN THIS LIST, AND USED TO BE — which quietly exempted the
# largest component in the repo from the one gate that exists to stop an edit
# that cannot ship.
#
# `src/` means two different things here. For a SCRIPT component it is
# makepkg's extraction dir (`git show HEAD:syn-update/src/syn-update.sh` fails;
# the tracked file is syn-update/syn-update.sh) — and those are all in
# .gitignore, so they never reach this check anyway. For a C component it is
# the SOURCE: synui/src has 93 tracked files, syn-arcade/src has 10. Excluding
# the pattern therefore excused every compositor change ever made, and the only
# reason it was ever caught was that most of them also touch a .qml or a
# header outside src/.
#
# So the split is made on what git says, one file at a time, in check_pkgrel:
# tracked means source, untracked means staging. No per-component list to keep
# in step.
NON_SHIPPING='(^|/)(pkg|tests)/'

# ── Output ───────────────────────────────────────────────────

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_RED=$'\033[31m'; C_YEL=$'\033[33m'; C_GRN=$'\033[32m'
    C_DIM=$'\033[2m';  C_B=$'\033[1m';    C_0=$'\033[0m'
else
    C_RED=; C_YEL=; C_GRN=; C_DIM=; C_B=; C_0=
fi

FINDINGS=0
NOTES=0

fail() {  # fail <check> <message...>
    local c=$1; shift
    printf '%sfail%s  %s%s%s  %s\n' "$C_RED" "$C_0" "$C_B" "$c" "$C_0" "$1" >&2
    shift
    local l
    for l in "$@"; do printf '        %s\n' "$l" >&2; done
    FINDINGS=$((FINDINGS + 1))
}

note() {  # note <check> <message...>
    local c=$1; shift
    printf '%snote%s  %s%s%s  %s\n' "$C_YEL" "$C_0" "$C_B" "$c" "$C_0" "$1"
    shift
    local l
    for l in "$@"; do printf '        %s\n' "$l"; done
    NOTES=$((NOTES + 1))
}

ok() { printf '%s  ok%s  %s%s%s  %s\n' "$C_GRN" "$C_0" "$C_DIM" "$1" "$C_0" "$2"; }

# ── Reading the lists ────────────────────────────────────────
#
# The array declarations are EVALUATED, never scraped. That is the whole
# lesson of the composed-pkgver bug: a regex that reads a shell assignment is
# reading a string that shell would have expanded, and the difference between
# those two is where the bugs live. bash already knows how to read bash.
#
# It is our own repo, and makepkg sources these same files moments later.

read_array() {  # read_array <file> <NAME> [end-anchor]
    local f=$1 n=$2 end=${3:-)} block
    block=$(sed -n "/^\(declare -A \)\?${n}=(/,/^${end}\|${end}\$/p" "$f") || return 1
    [ -n "$block" ] || return 1
    ( set +u
      eval "$block" 2>/dev/null || exit 1
      eval "printf '%s\n' \"\${${n}[@]}\"" 2>/dev/null ) | grep -v '^$' || true
}

read_keys() {  # read_keys <file> <NAME> — keys of an associative array
    local f=$1 n=$2 block
    block=$(sed -n "/^declare -A ${n}=(/,/^)/p" "$f") || return 1
    [ -n "$block" ] || return 1
    ( set +u
      eval "$block" 2>/dev/null || exit 1
      eval "printf '%s\n' \"\${!${n}[@]}\"" 2>/dev/null ) | grep -v '^$' || true
}

# pkgfield <dir> <name> — the SAME contract syn-update's pkgfield() uses, and
# deliberately so: this check is only worth anything if it reads a PKGBUILD the
# way the updater will. Sourced in a subshell with set +e +u, because makepkg
# sources the same file. An unresolvable value returns empty and fails.
pkgfield() {
    local v
    v=$( set +e +u; cd "$1" 2>/dev/null && . ./PKGBUILD >/dev/null 2>&1 &&
         printf '%s' "${!2}" ) || return 1
    case "$v" in *'$'*|"") return 1 ;; esac
    printf '%s' "$v"
}

# depends= as separate names, one per line.
#
# NOT pkgfield: `${!name}` on an array yields its FIRST element only, so
# depends would read as 'wlroots0.20' and every check over it would pass
# vacuously. Sourcing also handles what the arrays here actually look like —
# several lines long with `#` comments between the entries.
#
# Version constraints are trimmed: a depend may be written 'foo>=1.2', and the
# name is the only part that names a sibling.
pkgdeps() {
    ( set +e +u; cd "$1" 2>/dev/null && . ./PKGBUILD >/dev/null 2>&1 &&
      printf '%s\n' "${depends[@]}" ) 2>/dev/null |
    sed -e 's/[<>=].*$//' -e '/^$/d'
}

# source= as separate entries, one per line, EXPANDED. Same subshell contract
# as pkgdeps: the specs are written `git+$url.git#tag=v$pkgver` and the clone
# directory makepkg creates is named from the resolved value, so a scrape reads
# `$url` and reports a directory that never existed. (It did, on eight
# components, the first time this check was written.)
pkgsources() {
    ( set +e +u; cd "$1" 2>/dev/null && . ./PKGBUILD >/dev/null 2>&1 &&
      printf '%s\n' "${source[@]}" ) 2>/dev/null | sed '/^$/d'
}

# What a sed-scraper would have read out of the same line. Not used to decide
# anything — used to say "a scraper would misread this", which is the shape of
# bug 2 and the reason limine-mkinitcpio-hook bricked the updater for a day.
pkgfield_scraped() {
    sed -n "s/^[[:space:]]*$2=[\"']\?\([^\"'#]*\)[\"']\?[[:space:]]*$/\1/p" \
        "$1/PKGBUILD" 2>/dev/null | head -n1
}

# ── Argument handling ────────────────────────────────────────

BASE=HEAD
MODE=worktree
SELFTEST=0
# Skip the pkgrel gate, which is the only check that reads pending edits.
# For a pathspec commit (`git commit -- <paths>`), which is how this repo
# commits safely while several sessions share one index, there is no staged set
# to read and the working tree contains other people's work — so the honest
# answer is to run everything that describes the repo AT REST and say plainly
# that the bump was not checked, rather than to pass vacuously or to block on
# an edit the committer did not make.
AT_REST=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --staged)    MODE=staged ;;
        --at-rest)   AT_REST=1 ;;
        --since)     shift; BASE=${1:?--since needs a ref}; MODE=range ;;
        --self-test) SELFTEST=1 ;;
        -h|--help)   sed -n '2,/^set -euo/p' "$0" | sed 's/^# \?//;$d'; exit 0 ;;
        *)           echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

# ── Self-test ────────────────────────────────────────────────
#
# A check that has stopped matching prints "ok" exactly like a check that
# passed, and the two are indistinguishable from the outside — which is how a
# guard rots without anyone noticing. This proves each detector still fires by
# handing it input that must fail.

if [ "$SELFTEST" -eq 1 ]; then
    t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
    st_fail=0
    st() {  # st <name> <expect-pass|expect-fail> <command...>
        local name=$1 want=$2; shift 2
        if "$@" >/dev/null 2>&1; then got=pass; else got=fail; fi
        if [ "$got" = "${want#expect-}" ]; then
            printf '%s  ok%s  self-test  %s\n' "$C_GRN" "$C_0" "$name"
        else
            printf '%sfail%s  self-test  %s: expected %s, got %s\n' \
                "$C_RED" "$C_0" "$name" "${want#expect-}" "$got" >&2
            st_fail=$((st_fail + 1))
        fi
    }

    mkdir -p "$t/good" "$t/composed" "$t/broken"
    printf 'pkgname=good\npkgver=1.2.3\npkgrel=4\n'                >"$t/good/PKGBUILD"
    printf 'pkgname=c\n_v=1.2\npkgver="${_v}.3"\npkgrel=1\n'       >"$t/composed/PKGBUILD"
    printf 'pkgname=b\npkgver="${undefined_var}"\npkgrel=1\n'      >"$t/broken/PKGBUILD"

    st "pkgfield reads a plain version"        expect-pass pkgfield "$t/good" pkgver
    st "pkgfield resolves a composed version"  expect-pass pkgfield "$t/composed" pkgver
    st "pkgfield REJECTS an unresolvable one"  expect-fail pkgfield "$t/broken" pkgver

    # The scraper disagreeing with the sourced value IS the composed-pkgver
    # signature. If this ever stops disagreeing, the note below is dead.
    st "scrape differs on a composed version"  expect-fail \
        [ "$(pkgfield_scraped "$t/composed" pkgver)" = "$(pkgfield "$t/composed" pkgver)" ]
    st "scrape agrees on a plain version"      expect-pass \
        [ "$(pkgfield_scraped "$t/good" pkgver)" = "$(pkgfield "$t/good" pkgver)" ]

    # Array reading, against a file shaped like the real ones.
    cat >"$t/lists.sh" <<'LISTS'
KNOWN=(alpha beta
       gamma)
declare -A UNSUPPORTED=(
    [delta]="a reason"
)
LISTS
    st "read_array finds a wrapped array"      expect-pass \
        [ "$(read_array "$t/lists.sh" KNOWN | tr '\n' ' ')" = "alpha beta gamma " ]
    st "read_keys finds assoc keys"            expect-pass \
        [ "$(read_keys "$t/lists.sh" UNSUPPORTED)" = "delta" ]
    st "read_array fails on a missing name"    expect-fail \
        [ -n "$(read_array "$t/lists.sh" NOSUCHLIST)" ]

    # The real lists must be readable, or every list check below silently
    # compares two empty sets and passes.
    st "build-all.sh KNOWN= is readable"       expect-pass \
        [ "$(read_array build-all.sh KNOWN | wc -l)" -gt 5 ]
    st "syn-update COMPONENTS is readable"     expect-pass \
        [ "$(read_array syn-update/syn-update.sh COMPONENTS | wc -l)" -gt 5 ]
    st "archiso PACKAGES is readable"          expect-pass \
        [ "$(read_array archiso/build.sh PACKAGES | wc -l)" -gt 5 ]
    st "build rules are findable"              expect-pass \
        [ "$(grep -cE '^build_(component|script_pkg|vendored_pkg) ' build-all.sh)" -gt 5 ]
    st "publish-sources EXTERNAL is readable"  expect-pass \
        [ "$(read_array tools/publish-sources.sh EXTERNAL | wc -l)" -gt 5 ]

    if [ "$st_fail" -gt 0 ]; then
        printf '\n%s%d self-test failure(s) — the checks below cannot be trusted%s\n' \
            "$C_RED" "$st_fail" "$C_0" >&2
        exit 1
    fi
    printf '\n%sself-test clean — every detector still fires%s\n' "$C_GRN" "$C_0"
    exit 0
fi

# ── Collect the lists ────────────────────────────────────────

mapfile -t KNOWN      < <(read_array build-all.sh KNOWN)
mapfile -t COMPONENTS < <(read_array syn-update/syn-update.sh COMPONENTS)
mapfile -t ISO_PKGS   < <(read_array archiso/build.sh PACKAGES)
mapfile -t UNSUP      < <(read_keys  syn-update/syn-update.sh UNSUPPORTED)
mapfile -t NEVER_ADD  < <(read_keys  syn-update/syn-update.sh NEVER_ADD)
mapfile -t RULES      < <(grep -oE '^build_(component|script_pkg|vendored_pkg) [A-Za-z0-9_.+-]+' \
                          build-all.sh | awk '{print $2}' | sort -u)
# The same rules in the order they RUN. RULES above is sorted and de-duplicated,
# which is what the set comparisons want and is exactly wrong for check_order.
mapfile -t RULES_ORDERED < <(grep -oE '^build_(component|script_pkg|vendored_pkg) [A-Za-z0-9_.+-]+' \
                          build-all.sh | awk '{print $2}')

# An empty list here would make every comparison below trivially pass. Refuse
# to run rather than print a screenful of ok.
for _pair in "${#KNOWN[@]}:build-all.sh's KNOWN=" \
             "${#COMPONENTS[@]}:syn-update's COMPONENTS" \
             "${#ISO_PKGS[@]}:archiso's PACKAGES" \
             "${#RULES[@]}:build-all.sh's build rules"; do
    [ "${_pair%%:*}" -gt 0 ] || {
        echo "preflight: could not read ${_pair#*:} — refusing to report ok" >&2
        exit 2
    }
done
unset _pair

has() {  # has <needle> <haystack...>
    local n=$1; shift
    local h
    for h in "$@"; do [ "$h" = "$n" ] && return 0; done
    return 1
}

# ── Check 1: the four lists agree ────────────────────────────

check_lists() {
    local c bad=$FINDINGS

    # KNOWN= with no build rule: a legal argument that builds nothing and exits
    # 0. build-all.sh's own DISPATCHED guard catches this too, but only when
    # the name is passed on a command line — this catches it at rest.
    for c in "${KNOWN[@]}"; do
        has "$c" "${RULES[@]}" || fail lists \
            "build-all.sh: '$c' is in KNOWN= with no build rule" \
            "It is a legal argument that builds NOTHING and exits 0, so syn-update" \
            "reports success, publishes nothing, and offers the same update forever." \
            "Add a build_component / build_script_pkg / build_vendored_pkg line."
    done

    # The mirror image: a rule for a name no argument can reach.
    for c in "${RULES[@]}"; do
        has "$c" "${KNOWN[@]}" || fail lists \
            "build-all.sh: '$c' has a build rule but is not in KNOWN=" \
            "\`./build-all.sh $c\` is rejected as an unknown component, so it can only" \
            "ever be built as part of a full run."
    done

    # COMPONENTS naming something build-all.sh will not accept is FATAL at
    # update time: syn-update dies on `unknown component` after the fetch,
    # naming neither list, and nothing can ship past it.
    for c in "${COMPONENTS[@]}"; do
        has "$c" "${KNOWN[@]}" || fail lists \
            "syn-update: COMPONENTS names '$c', absent from build-all.sh's KNOWN=" \
            "This BRICKS the updater: it dies on 'unknown component: $c' after the" \
            "fetch and every other component stops updating with it."
    done

    # A list entry with no directory is the same failure one step earlier.
    for c in "${KNOWN[@]}" "${COMPONENTS[@]}"; do
        [ -f "$c/PKGBUILD" ] || fail lists \
            "'$c' is named in a build list but $c/PKGBUILD does not exist"
    done

    [ "$FINDINGS" -eq "$bad" ] && ok lists \
        "${#KNOWN[@]} KNOWN, ${#RULES[@]} build rules, ${#COMPONENTS[@]} COMPONENTS — consistent"
    return 0
}

# ── Check 2: every component is registered somewhere ─────────

check_registration() {
    local d c bad=$FINDINGS untriaged=()

    for d in */PKGBUILD; do
        c=${d%/PKGBUILD}
        has "$c" "${COMPONENTS[@]}" && continue

        # UNSUPPORTED is keyed by PACKAGE name, and one directory disagrees
        # with its package (linux-wallpaperengine-pkg builds
        # linux-wallpaperengine), so resolve the name rather than assume it.
        local pkg
        pkg=$(pkgfield "$c" pkgname 2>/dev/null || true)
        has "$c" "${UNSUP[@]}" && continue
        [ -n "$pkg" ] && has "$pkg" "${UNSUP[@]}" && continue

        if [ -n "${UNREGISTERED[$c]:-}" ]; then
            untriaged+=("$c")
            continue
        fi

        fail registration \
            "'$c' has a PKGBUILD but is in neither COMPONENTS nor UNSUPPORTED" \
            "A component in no update list is frozen on every installed machine and" \
            "nothing says so. Add it to syn-update's COMPONENTS, or to UNSUPPORTED" \
            "with the reason it cannot be built there."
    done

    # The reverse: a list entry that is only a list entry.
    for c in "${NEVER_ADD[@]}"; do
        has "$c" "${COMPONENTS[@]}" || fail registration \
            "syn-update: NEVER_ADD names '$c', which is not in COMPONENTS" \
            "NEVER_ADD only takes effect on the not-installed branch of scan(), so" \
            "an entry outside COMPONENTS guards nothing."
    done

    if [ "${#untriaged[@]}" -gt 0 ]; then
        note registration \
            "${#untriaged[@]} component(s) knowingly unregistered: ${untriaged[*]}" \
            "Each is un-updatable on installed machines. Reasons are in this" \
            "script's UNREGISTERED table; resolving one means deleting its line."
    fi

    [ "$FINDINGS" -eq "$bad" ] && ok registration \
        "every PKGBUILD is in COMPONENTS, UNSUPPORTED, or declared"
    return 0
}

# ── Check 3: the ISO is a second, independent collector ──────

check_iso() {
    local c bad=$FINDINGS

    for c in "${KNOWN[@]}"; do
        has "$c" "${ISO_PKGS[@]}" && continue
        [ -n "${EXEMPT_ISO[$c]:-}" ] && continue
        fail iso \
            "'$c' is built by build-all.sh but is not in archiso's PACKAGES" \
            "archiso/build.sh collects independently of build-all.sh — that is the" \
            "fifth drift between the two. An installed system gets '$c' and the ISO" \
            "does not. Add it to PACKAGES, or to this script's EXEMPT_ISO with why."
    done

    for c in "${ISO_PKGS[@]}"; do
        [ -f "$c/PKGBUILD" ] || fail iso \
            "archiso PACKAGES names '$c' but $c/PKGBUILD does not exist" \
            "The ISO build dies at this package with 'aborting — packages.x86_64" \
            "needs every package'."
    done

    [ "$FINDINGS" -eq "$bad" ] && ok iso \
        "${#ISO_PKGS[@]} ISO packages — every buildable component accounted for"
    return 0
}

# ── Check 4: the ISO and the installer agree ─────────────────
#
# A third and fourth list, after KNOWN= and COMPONENTS: archiso's PACKAGES says
# what the live ISO carries, syn-install's component table says what a target
# can be given. They are written by hand, independently, and a package in the
# first and not the second is invisible on every installed machine while
# working perfectly on the media it came from — which is the hardest kind of
# gap to see, because testing the ISO does not test it.
#
# ⚠ IT IS THE TABLE NOW, NOT `SEL_CORE="…"`. Every SynapseOS package became a
# checkbox, so those two variables are BUILT by sel_packages() from
# SEL_COMPONENTS and no longer hold a literal list. Scraping the old
# assignments after that change found `core)` and `app)` — the arms of the
# case that builds them — and reported every real package as undelivered.
# Being a row on the page is what "an install path delivers it" means; whether
# the box is ticked by default is the std/full column, which select_test.sh
# covers.

check_installer() {
    local p bad=$FINDINGS
    local inst=syn-install/syn-install.sh
    [ -f "$inst" ] || { fail installer "$inst is missing"; return 0; }

    # Field 5 of every comp_ row: the packages that row installs. sw_ rows are
    # deliberately skipped — they name Arch packages, not ours.
    local sel
    sel=$(grep -oE '^[[:space:]]*"comp_[a-z0-9_]+\|[^"]*"' "$inst" \
          | tr -d '"' | cut -d'|' -f5 \
          | tr ' ' '\n' | grep -vE '^$' | sort -u)
    [ -n "$sel" ] || { fail installer "could not read the component table from $inst"; return 0; }

    # dir -> pkgname, because one disagrees (linux-wallpaperengine-pkg builds
    # linux-wallpaperengine) and a name check that assumed they matched would
    # report the working case as broken.
    declare -A ours=()
    local d c n
    for d in */PKGBUILD; do
        c=${d%/PKGBUILD}
        ours[$c]=1
        n=$(pkgfield "$c" pkgname 2>/dev/null || true)
        [ -n "$n" ] && ours[$n]=1
    done

    for p in $sel; do
        [ -n "${ours[$p]:-}" ] || fail installer \
            "syn-install's component table names '$p', which this repo does not build" \
            "A comp_ row may only name packages the ISO's local repo carries." \
            "pacstrap fails the install at this package."
    done

    # The direction that actually bit: on the ISO, delivered to nobody.
    for p in "${ISO_PKGS[@]}"; do
        n=$(pkgfield "$p" pkgname 2>/dev/null || true)
        n=${n:-$p}
        has "$p" $sel && continue
        has "$n" $sel && continue
        [ -n "${EXEMPT_INSTALLER[$p]:-}" ] && continue
        [ -n "${EXEMPT_INSTALLER[$n]:-}" ] && continue
        fail installer \
            "'$n' is on the ISO but is not a row in syn-install's component table" \
            "The live ISO carries it and no install path delivers it, so it works on" \
            "the media and is missing on every machine installed from that media —" \
            "which testing the ISO cannot show. Add a row for it to SEL_COMPONENTS, or" \
            "to this script's EXEMPT_INSTALLER with the reason it still arrives."
    done

    [ "$FINDINGS" -eq "$bad" ] && ok installer \
        "$(printf '%s\n' $sel | wc -l) installer packages — every ISO package reaches a target"
    return 0
}

# ── Check 5: every version field is readable ─────────────────

check_pkgver() {
    local c ver rel scraped bad=$FINDINGS composed=()

    for c in "${COMPONENTS[@]}"; do
        [ -f "$c/PKGBUILD" ] || continue   # already reported by check_lists

        if ! ver=$(pkgfield "$c" pkgver); then
            fail pkgver \
                "$c/PKGBUILD: pkgver does not resolve to a version" \
                "syn-update skips a component whose version cannot be read — a version" \
                "that cannot be read must never become an update. Fix the PKGBUILD."
            continue
        fi

        if ! rel=$(pkgfield "$c" pkgrel); then
            fail pkgver "$c/PKGBUILD: pkgrel does not resolve"
            continue
        fi

        case "$rel" in
            ''|*[!0-9]*) fail pkgver \
                "$c/PKGBUILD: pkgrel is '$rel', not a plain integer" ;;
        esac

        # makepkg's own rule, and vercmp's assumption.
        case "$ver" in
            *[:-]*|*' '*) fail pkgver \
                "$c/PKGBUILD: pkgver '$ver' contains a character makepkg forbids" \
                "pkgver may not contain ':', '-' or whitespace." ;;
        esac

        scraped=$(pkgfield_scraped "$c" pkgver)
        [ "$scraped" = "$ver" ] || composed+=("$c ($ver, written as '${scraped:-<no literal line>}')")
    done

    if [ "${#composed[@]}" -gt 0 ]; then
        note pkgver \
            "${#composed[@]} PKGBUILD(s) compose pkgver from another variable:" \
            "${composed[@]}" \
            "Legal, and resolved correctly here because this sources the file. But any" \
            "consumer that SCRAPES the line reads the literal '\${...}' back, and vercmp" \
            "ranks that above every real version — which is how syn-update bricked" \
            "itself on limine-mkinitcpio-hook. If you add a version reader, source it."
    fi

    [ "$FINDINGS" -eq "$bad" ] && ok pkgver \
        "${#COMPONENTS[@]} PKGBUILDs — every pkgver and pkgrel resolves"
    return 0
}

# ── Check 5: an edit that cannot ship ────────────────────────

changed_files() {
    case "$MODE" in
        staged)   git diff --cached --name-only --diff-filter=d ;;
        range)    git diff "$BASE"..HEAD --name-only --diff-filter=d ;;
        worktree) { git diff HEAD --name-only --diff-filter=d
                    git ls-files --others --exclude-standard; } | sort -u ;;
    esac
}

# Did <component>/PKGBUILD's pkgrel or pkgver change in the same set of edits?
# A pkgver bump counts: a new upstream version is a new package to pacman just
# as a pkgrel is.
version_moved() {
    local c=$1 diff
    case "$MODE" in
        staged)   diff=$(git diff --cached -U0 -- "$c/PKGBUILD") ;;
        range)    diff=$(git diff "$BASE"..HEAD -U0 -- "$c/PKGBUILD") ;;
        worktree) diff=$(git diff HEAD -U0 -- "$c/PKGBUILD") ;;
    esac
    printf '%s' "$diff" | grep -qE '^\+[[:space:]]*(pkgrel|pkgver|_pkgver)='
}

check_pkgrel() {
    local f c bad=$FINDINGS
    declare -A touched=() witness=()

    while IFS= read -r f; do
        [ -n "$f" ] || continue
        c=${f%%/*}
        [ "$c" = "$f" ] && continue          # repo root: build-all.sh, README
        [ -f "$c/PKGBUILD" ] || continue     # archiso/, docs/, tools/
        printf '%s' "${f#*/}" | grep -qE "$NON_SHIPPING" && continue
        # src/ is source when git tracks it and makepkg's extraction dir when
        # it does not. See the note on NON_SHIPPING.
        case "${f#*/}" in
            src/*) git ls-files --error-unmatch "$f" >/dev/null 2>&1 || continue ;;
        esac
        touched[$c]=1
        [ -n "${witness[$c]:-}" ] || witness[$c]=$f
    done < <(changed_files)

    if [ "${#touched[@]}" -eq 0 ]; then
        ok pkgrel "no component source edits to gate"
        return 0
    fi

    for c in $(printf '%s\n' "${!touched[@]}" | sort); do
        # A component that did not exist at BASE has no released version to
        # beat, so pkgrel=1 is right and there is nothing to bump. Its real
        # risk is being in no build list, which check_registration owns.
        if ! git cat-file -e "$BASE:$c/PKGBUILD" 2>/dev/null; then
            ok pkgrel "$c — new component, no prior release to bump past"
            continue
        fi

        if version_moved "$c"; then
            ok pkgrel "$c — edited, version moved to $(pkgfield "$c" pkgver)-$(pkgfield "$c" pkgrel)"
            continue
        fi

        # syn-update is the amplifying case and says so: freezing the updater
        # freezes every OTHER component the edit would have taught it about.
        # It runs the COMPONENTS list from the INSTALLED copy, so a stale one
        # reports "already current" while printing the very commits you are
        # waiting on — those two outputs contradicting each other IS the
        # symptom, and it landed twice in one day.
        local extra=()
        [ "$c" = "syn-update" ] && extra=(
            "" \
            "syn-update especially: it runs COMPONENTS from the INSTALLED copy, so" \
            "leaving it here freezes every other component this edit would have" \
            "taught it about. Adopting a COMPONENTS change already takes two applies.")

        fail pkgrel \
            "$c: source edited (${witness[$c]}), pkgrel not bumped" \
            "pacman compares pkgver-pkgrel. At $(pkgfield "$c" pkgver 2>/dev/null || echo '?')-$(pkgfield "$c" pkgrel 2>/dev/null || echo '?') the installed release already equals" \
            "the tree's, so nothing rebuilds and nothing installs — the edit cannot" \
            "arrive. Bump pkgrel in $c/PKGBUILD, in this same commit." \
            "${extra[@]}"
    done
    return 0
}

# ── Check 7: a component is built before whatever depends on it ──
#
# Both collectors are ORDERED lists, and both install as they go: build-all.sh
# runs `pacman -U` on each package, and the ISO's makepkg -s resolves a
# component's depends out of the local repo it is filling. So a component that
# depends on a SIBLING built later fails — on build-all.sh with "could not
# satisfy dependencies", on the ISO at that package's makepkg.
#
# It fails only on a machine that has not got the sibling installed ALREADY,
# which is never the developer's box and always a fresh build host or the ISO
# runner. That is the whole reason for checking it here: locally it passes.
#
# Found when synui's `kitty` dependency became `syntty` — syntty was built near
# the end, next to syn-arcade, which had been correct for as long as nothing
# required it.
check_order() {
    local bad=$FINDINGS c dep pos_c pos_d

    # Position of a name in an ordered list; empty if absent.
    pos_in() {  # pos_in <needle> <haystack...>
        local n=$1 i=0; shift
        local h
        for h in "$@"; do
            [ "$h" = "$n" ] && { printf '%s' "$i"; return 0; }
            i=$((i + 1))
        done
        return 1
    }

    for c in "${KNOWN[@]}"; do
        [ -f "$c/PKGBUILD" ] || continue
        # Only SynapseOS siblings matter: anything else comes off a mirror and
        # is resolved by pacman long before either list is walked.
        while read -r dep; do
            [ -n "$dep" ] || continue
            has "$dep" "${KNOWN[@]}" || continue

            if pos_c=$(pos_in "$c" "${RULES_ORDERED[@]}") &&
               pos_d=$(pos_in "$dep" "${RULES_ORDERED[@]}") &&
               [ "$pos_d" -gt "$pos_c" ]; then
                fail order \
                    "build-all.sh builds '$c' before '$dep', which it depends on" \
                    "build-all.sh installs each package as it builds it, so this fails" \
                    "with 'could not satisfy dependencies' on any machine that has not" \
                    "got $dep already — a fresh build host, never yours. Move the" \
                    "build_component line for '$dep' above '$c'."
            fi

            if pos_c=$(pos_in "$c" "${ISO_PKGS[@]}") &&
               pos_d=$(pos_in "$dep" "${ISO_PKGS[@]}") &&
               [ "$pos_d" -gt "$pos_c" ]; then
                fail order \
                    "archiso PACKAGES builds '$c' before '$dep', which it depends on" \
                    "The ISO's makepkg -s resolves depends out of the local repo it is" \
                    "still filling, so '$c' fails at its own build. archiso/build.sh is a" \
                    "second, independent collector — fixing build-all.sh alone is half" \
                    "the fix. Move '$dep' above '$c' in PACKAGES."
            fi
        done < <(pkgdeps "$c")
    done

    [ "$FINDINGS" -eq "$bad" ] && ok order \
        "build order respects every inter-component dependency"
    return 0
}

# ── Check 8: an icon GTK cannot see ──────────────────────────
#
# gdk-pixbuf identifies an image by SNIFFING THE FIRST 256 BYTES of the file
# for a format signature, and librsvg registers the literal "<svg" as its own.
# An SVG whose root element starts after byte 256 — which is what a long
# explanatory header comment before <svg> does — is not recognised as an image
# at all:
#
#   gdk-pixbuf-error-quark: Couldn't recognize the image file format
#
# The failure is silent and desktop-specific, which is what made it expensive.
# Qt parses the XML properly instead of sniffing it, so on synui and on KDE
# every one of these icons drew perfectly; under GNOME the same file was a
# blank square in the app grid. Five shipped icons were in that state on ISO
# 0.2.9 — syn-settings, syn-arcade, syntty, syn-disks and syn-edit — and the
# house style of a documented header comment is exactly what put them there,
# so the rule needs enforcing rather than remembering.
#
# The fix is never to delete the comment: move it INSIDE <svg>, which is where
# synfiles.svg had it all along and why synfiles was the one that worked.
#
# Only icons that get INSTALLED are checked. A working-file SVG nothing ships
# is not a bug, and failing on one would teach people to skip this script.

check_icons() {
    local f off base bad=$FINDINGS n=0 icons=""

    # Exactly the SVGs some .desktop points at as its icon — not every SVG in
    # the tree. Two of them are installed from a PKGBUILD rather than living
    # under data/, so a path glob missed them; and the tree also holds SVGs
    # that are never icons at all, including one that is a QML file wearing a
    # .svg extension (synui's Qt VectorImage assets) and syn-arcade's tile art,
    # which quickshell renders directly. Failing on those would be noise, and
    # noise is what teaches people to skip this script.
    icons=" $(git ls-files '*.desktop' | xargs -r grep -h '^Icon=' \
              | cut -d= -f2- | tr -d '\r' | sort -u | tr '\n' ' ') "

    while IFS= read -r f; do
        base=${f##*/}; base=${base%.svg}
        case "$icons" in *" $base "*) ;; *) continue ;; esac
        n=$((n + 1))

        # The byte offset of "<svg", or nothing when the file has no root at
        # all (a gzipped .svg, or something that is not an SVG despite the
        # name — both of which gdk-pixbuf also declines).
        off=$(LC_ALL=C grep -abo -m1 '<svg' "$f" | cut -d: -f1)
        if [ -z "$off" ]; then
            fail icons "$f is named as an icon but has no <svg> element" \
                "Either it is gzipped or it is not an SVG. gdk-pixbuf declines" \
                "it either way and the app shows a blank icon on GTK desktops."
            continue
        fi
        [ "$off" -le 255 ] && continue
        fail icons "$f starts <svg> at byte $off — GTK will not read it" \
            "gdk-pixbuf sniffs only the first 256 bytes for the format" \
            "signature, so this file is 'unrecognized' and the icon is BLANK" \
            "in the GNOME app grid. It still draws on synui and KDE, because" \
            "Qt parses the XML rather than sniffing it — which is why this" \
            "shipped." \
            "Fix: move the header comment INSIDE the <svg> element, as" \
            "synfiles/data/synfiles.svg does. Do not delete it."
    done < <(git ls-files '*.svg')

    [ "$FINDINGS" -eq "$bad" ] && ok icons \
        "$n app icon(s) — every root element is inside gdk-pixbuf's sniff window"
    return 0
}

# ── leavings: a built tree must not look like an edited one ──
#
# ⛔ THE FAILURE THIS CATCHES IS AN UPDATE THAT NEVER ARRIVES. syn-update builds
# in its own clone (/var/lib/synapse-src) and refuses to fetch when that clone
# is dirty — and its dirt check is `git status --porcelain`, which counts
# UNTRACKED files. So a component that has only ever been BUILT there looks
# exactly like one somebody EDITED, and every later check dies with "local
# modifications" until a human clears it by hand. Nothing was edited.
#
# makepkg drops two kinds of directory next to a PKGBUILD, and both need a
# .gitignore rule in the same commit as the package:
#
#   <pkg>/src/        the srcdir, for any package without a tracked src/ of
#                     its own (synui, synguard, synapd, synsh, synnet and
#                     synapse_kmod have real ones — a blanket src/ rule would
#                     hide those, which is why the rules are per package)
#   <pkg>/<clone>/    makepkg's cached bare clone, for a git+ source, where
#                     <clone> is the source's `name::` prefix or the basename
#                     of its URL — NOT the package name. Assuming otherwise
#                     reports three false positives on this tree.
#
# Hit twice: cliamp (2026-08-16) and syn-gfn (2026-08-25), the second of which
# stopped the update checker on a laptop that had built it.
check_uifont() {
    local f bad=$FINDINGS n=0 bare fam ln

    # Every STANDALONE app window follows the desktop font. One file,
    # ~/.config/synui/font.state, carries the family and a percent `scale`, and
    # a window that does not read it keeps whatever face and size Qt resolved
    # at startup — which is what "the theming missed those apps" has meant
    # every time it has been reported. velle, 2026-08-25: "font size isn't
    # system wide. it's supposed to be. menus apps everywhere."
    #
    # ⚠ BOTH HALVES OR NEITHER. Qt resolves an application's default font ONCE
    # at startup and QML cannot change it afterwards, so the family has to be
    # named on every Text and every size has to go through ui(). Doing one and
    # not the other gives a window that follows the desktop until somebody
    # changes it — which looks fixed at exactly the moment it is tested.
    #
    # ⚠ ONLY STANDALONE APPS. Everything under synui/quickshell*/ imports
    # synui's Theme singleton, which reads the file on their behalf, so those
    # legitimately mention neither. src/ and pkg/ are build leavings, and
    # archiso/ is a filesystem image.
    #
    # A file with no pixelSize at all draws no text of its own and is skipped.
    while IFS= read -r f; do
        case "$f" in
            synui/quickshell*|*/src/*|*/pkg/*|archiso/*|*/tests/*) continue ;;
        esac
        grep -q 'pixelSize' "$f" || continue

        # ⚠ NAMED, NOT INFERRED. syn-install runs off the ISO, before there is
        # a ~/.config/synui to read, and the big screen sizes every glyph off
        # the SCREEN because it is a ten-foot UI — multiplying that by a scale
        # chosen for a desk monitor would break the one relationship its layout
        # depends on. Both still name the family; only the scale is excused,
        # and each is listed so the exemption cannot spread by accident.
        case "$f" in
            syn-install/syn-install-gui.qml)  continue ;;
        esac

        # ⚠ A BAR PLUGIN IS NOT AN APP WINDOW, and holding it to this rule
        # would be holding it to the wrong one. A widget draws inside the bar,
        # so it takes its font FROM the bar — `bar.fontFamily`, alongside the
        # bar.barSize its size already comes off — and reading font.state
        # itself would be a second reader of a file the host has already read.
        # It still has to name a family, which is checked below like any other.
        case "$f" in
            synui/data/plugins/*)
                if grep -qE 'font\.family' "$f"; then
                    n=$((n + 1))
                else
                    fail uifont "$f names no font family" \
                        "A bar plugin takes the desktop font from its host —" \
                        "\`bar.fontFamily\`, the same place bar.barSize comes" \
                        "from. Naming none leaves the widget in whatever face" \
                        "Qt resolved at startup, beside modules that followed" \
                        "the picked font."
                fi
                continue ;;
        esac

        n=$((n + 1))
        if ! grep -q 'font\.state' "$f"; then
            fail uifont "$f reads font.state nowhere" \
                "It keeps whatever face and size Qt resolved at startup, so a" \
                "font picked for the desktop reaches every app but this one." \
                "Copy the FileView + ui() block from syn-disks/data/syn-disks.qml."
            continue
        fi

        # The size half. A bare `pixelSize: 12` is the silent failure: the
        # window draws perfectly, at the wrong size, beside windows at the
        # right one. syn-arcade-big is excused HERE and only here.
        case "$f" in
            syn-arcade/data/syn-arcade-big.qml) bare=0 ;;
            *) bare=$(grep -cE 'pixelSize:[[:space:]]*[0-9]' "$f" || true) ;;
        esac
        if [ "${bare:-0}" -gt 0 ]; then
            fail uifont "$f has $bare pixelSize literal(s) outside ui()" \
                "Those sizes ignore font.state's scale, so this window draws" \
                "at 100% beside windows at whatever the user chose."
            continue
        fi

        # The family half, on every font block.
        #
        # ⚠ THE WINDOW IS TAKEN OVER CODE, NOT OVER LINES. The first draft read
        # two raw lines either side and reported syn-update/shell.qml, whose
        # size and family are three lines apart with a two-line comment between
        # them explaining why that family is the literal "monospace". A check
        # that fails on a file for having a comment in it is a check people
        # learn to route around, so full-line comments and blanks are dropped
        # from the window before it is read, and the span is widened to match.
        # …and the window stops at the element's own braces, so a family on the
        # NEXT Text cannot vouch for this one — a false negative is the
        # dangerous direction for a gate, and widening the span to see past
        # comments is exactly what would open one.
        fam=$(awk '
            { line[NR] = $0 }
            END {
                miss = 0
                for (i = 1; i <= NR; i++) {
                    if (line[i] !~ /font\.pixelSize:/) continue
                    found = 0
                    for (d = -1; d <= 1; d += 2) {
                        for (j = i + d; j >= 1 && j <= NR; j += d) {
                            t = line[j]
                            sub(/^[ \t]+/, "", t); sub(/[ \t]+$/, "", t)
                            if (t == "" || t ~ /^(\/\/|\/\*|\*)/) continue
                            if (t ~ /[{}]/) break          # element boundary
                            if (t ~ /font\.family/) { found = 1; break }
                        }
                        if (found) break
                    }
                    # A family declared on the SAME line counts too.
                    if (!found && line[i] ~ /font\.family/) found = 1
                    if (!found) miss++
                }
                print miss
            }' "$f")
        if [ "$fam" -gt 0 ]; then
            fail uifont "$f sets a size without a family $fam time(s)" \
                "Qt cannot restyle an application's font after startup, so a" \
                "Text that names no family keeps the startup face for ever."
            continue
        fi
    done < <(git ls-files '*.qml')

    [ "$FINDINGS" -eq "$bad" ] && ok uifont \
        "$n app window(s) — every one follows the desktop font"
    return 0
}

check_scrollbar() {
    local f bad=$FINDINGS n=0 views

    # ⛔ A VIEW THAT SCROLLS SHOWS THAT IT SCROLLS. velle, 2026-08-28: "add
    # scrollbar to assistant chat window, you keep making windows without
    # scrollbars and thats dumb. […] you did the same thing with software
    # manager and it will list long lines of software with no quick way to
    # scroll, annoying."
    #
    # A wheel is not a substitute for a bar. Without one there is nothing on
    # screen saying there is anything past the edge of the view, nothing saying
    # how much, nothing saying where in it you are, and no way to cross it in
    # one gesture — which on a list of every package on the system is the
    # difference between a window and a chore.
    #
    # ⚠ THE RULE IS PER FILE, NOT PER VIEW. A file may hold a scroller that
    # cannot overflow — a Flickable used purely to clip — and counting those
    # would make this a nuisance rather than a rule. One bar in a file whose
    # views scroll is the evidence that whoever wrote it thought about it.
    #
    # ⚠ A HAND-ROLLED BAR COUNTS. synfiles predates the shared type and carries
    # its own VScroll/HScroll — a full implementation with a draggable handle,
    # click-to-page, and hiding itself when everything fits. Grepping for the
    # Controls type alone called that file a gap and it was not; a rule that
    # cannot see a correct implementation teaches people to satisfy the grep.
    #
    # ⚠ ScrollBar NEEDS `import QtQuick.Controls`. synpkg had a ListView of
    # thousands of rows and no such import at all — the type was not merely
    # unused, it was unavailable, which is how this goes unnoticed.

    while IFS= read -r f; do
        # Build leavings, the ISO image, and test fixtures are not app windows.
        case "$f" in
            */src/*|*/pkg/*|archiso/*|*/tests/*) continue ;;
        esac

        # ⚠ `|| true` ON BOTH, because this script runs under `set -e` and grep
        # exits 1 when it matches nothing — which here is the ordinary case,
        # not an error. Without it this gate took the whole preflight down at
        # the first QML file with no scrolling view in it, and every check
        # after it silently stopped running.
        views=$(grep -cE '^[[:space:]]*(ListView|GridView|TableView|Flickable|ScrollView)[[:space:]]*\{' "$f" || true)
        [ "${views:-0}" -gt 0 ] || continue
        n=$((n + 1))

        if grep -qE 'ScrollBar|VScroll|HScroll' "$f"; then continue; fi

        fail scrollbar "$f has $views scrolling view(s) and no scrollbar" \
            "A wheel does not tell a reader there is more, how much more, or" \
            "where in it they are. Attach one — SynScrollBar in the synui and" \
            "antiquity trees, an inline component elsewhere."
    done < <(git ls-files '*.qml')

    [ "$FINDINGS" -eq "$bad" ] && ok scrollbar \
        "$n scrolling window(s) — every one has a scrollbar"
    return 0
}

check_leavings() {
    local f pkg src line spec clone bad=$FINDINGS n=0

    while IFS= read -r f; do
        pkg=${f%/PKGBUILD}
        n=$((n + 1))

        # A package that TRACKS files under src/ keeps its src/; makepkg
        # unpacks under it into src/<name>-<ver>/, which the generic rule
        # already covers.
        if [ -z "$(git ls-files "$pkg/src" | head -1)" ]; then
            git check-ignore -q "$pkg/src/" || fail leavings \
                "$pkg/src/ is not ignored — a build there dirties the tree" \
                "makepkg creates it for every package with a source array, and" \
                "syn-update counts untracked files as local modifications: the" \
                "first machine to BUILD $pkg stops being able to UPDATE, with" \
                "\"refusing to overwrite local changes\" and nothing edited." \
                "Fix: add '$pkg/src/' to .gitignore in this commit."
        fi

        # …and the cached clone of a git+ source, whose directory is named by
        # the source spec rather than by the package.
        while IFS= read -r spec; do
            case "$spec" in
                *::git+*) clone=${spec%%::git+*} ;;
                git+*)    clone=${spec#git+}
                          clone=${clone%%#*}; clone=${clone%%\?*}
                          clone=${clone##*/}; clone=${clone%.git} ;;
                *)        continue ;;
            esac
            [ -n "$clone" ] || continue
            git check-ignore -q "$pkg/$clone/" || fail leavings \
                "$pkg/$clone/ is not ignored — makepkg caches the clone there" \
                "SRCDEST is the PKGBUILD's own directory for a git+ source, so" \
                "the bare clone lands beside it and untracked. Same consequence" \
                "as src/ above: the machine that builds it cannot update again." \
                "Fix: add '$pkg/$clone/' to .gitignore in this commit."
        done < <(pkgsources "$pkg")
    done < <(git ls-files '*/PKGBUILD')

    [ "$FINDINGS" -eq "$bad" ] && ok leavings \
        "$n component(s) — a build leaves nothing that looks like an edit"
    return 0
}

# ⛔ THE SOURCE TARBALL IS AN ALLOWLIST, AND WHAT IT OMITS IS SILENT. build-all.sh
# names the files it collects; anything else is simply absent from the tarball
# makepkg unpacks. A component therefore builds perfectly in its own tree and
# fails inside makepkg, on a machine that is mid-update, with an error naming
# something the developer never touched.
#
# meson_options.txt is the one that bites: meson.build calls get_option() for an
# option the tarball no longer defines, and `meson setup` stops with
# `Unknown option`. syn-cal shipped an OAuth client id that way and every
# installed machine's syn-update died on it.
check_tarball() {
    local c f bad=$FINDINGS n=0
    # ⚠ THE ALLOWLIST MOVED, AND THIS CHECK HAS TO MOVE WITH IT. It lived inline
    # in build-all.sh until tools/publish-sources.sh needed the same tarball for
    # people without this checkout; both callers now go through
    # tools/collect-source.sh, which is the one place the list exists.
    for f in meson_options.txt meson.options; do
        grep -q "name/$f" tools/collect-source.sh || continue
        n=$((n + 1))
    done
    if [ "$n" -eq 0 ]; then
        fail tarball \
            "tools/collect-source.sh collects no meson options file" \
            "A component with meson_options.txt builds in its tree and fails" \
            "inside makepkg with \"Unknown option\", mid-update." \
            "Fix: add it to the dirs list in tools/collect-source.sh."
        return 0
    fi

    n=0
    while IFS= read -r c; do
        c=$(dirname "$c")
        n=$((n + 1))
        # A meson.build that reads a custom option needs the file that defines
        # it to travel too. get_option() also serves the builtins (prefix,
        # datadir…), which need no options file — so the test is the FILE's
        # presence, not the call's.
        # ⚠ ONE FILE AT A TIME. `git ls-files --error-unmatch a b` fails when
        # EITHER path is untracked, so asking about both at once reports every
        # component that has one of them and not the other — which is all of
        # them, since a project writes one name or the other, never both.
        for f in meson_options.txt meson.options; do
            [ -f "$c/$f" ] || continue
            git ls-files --error-unmatch "$c/$f" >/dev/null 2>&1 || fail tarball \
                "$c/$f is not tracked by git" \
                "build-all.sh tars the working tree, so it builds here. But" \
                "syn-update rebuilds from a fresh clone, where the file does not" \
                "exist and meson stops on \"Unknown option\" — on somebody else's" \
                "machine, mid-update." \
                "Fix: git add it in this commit."
        done
    done < <(git ls-files '*/meson.build')

    [ "$FINDINGS" -eq "$bad" ] && ok tarball \
        "$n meson component(s) — every options file reaches the tarball"
    return 0
}

# ── Check 12: buildable by somebody without this checkout ────
#
# Every in-house PKGBUILD builds from `$pkgname-$pkgver.tar.gz`, which
# build-all.sh assembles from the working tree and .gitignore keeps out of the
# repo. For a component nobody outside is meant to build, that is correct and
# invisible. For one that IS meant to be built elsewhere it is fatal and equally
# invisible: `makepkg` stops at "Retrieving sources" on a stock Arch box, and
# nothing here can tell, because on THIS machine the file is always there.
#
# So the external set (tools/publish-sources.sh's EXTERNAL) and the source=()
# lines have to agree, in both directions:
#
#   - a name in EXTERNAL whose source=() does not carry the release URL is an
#     asset published where no PKGBUILD looks;
#
# ⛔ AND THE URL NAMES THE PACKAGE'S OWN REPOSITORY, NOT THIS ONE. These went to
# SYNAPSE's releases first: twelve component tarballs buried the ISO downloads,
# and the newest of them became GitHub's "Latest release" for the whole project,
# so the README badge and every /releases/latest link pointed at a source
# tarball instead of the operating system.
#   - a PKGBUILD carrying the URL that is not in EXTERNAL is a package that
#     404s for everybody outside this repo, because nothing publishes its
#     source.
#
# ⛔ AND THE URL MUST CARRY THE pkgrel. The tag is <pkgname>-<pkgver>-<pkgrel>;
# written any other way it goes stale on the next bump and silently serves the
# previous source to everyone who is not us.
#
# ⚠ THE CLOSURE IS PART OF IT. synui depends on syntty, so publishing synui
# alone gives an outsider a package they cannot install. A depend that is a
# component of this repo has to be reachable too — either in EXTERNAL, or
# self-sufficient already because its own source=() is an upstream URL
# (scenefx0.5, which fetches from wlrfx/scenefx and needs nothing from here).
check_external() {
    local bad=$FINDINGS ext n=0 name ver rel want got dep
    ext=$(read_array tools/publish-sources.sh EXTERNAL) || {
        fail external             "tools/publish-sources.sh EXTERNAL is unreadable"             "Every check below would then compare two empty sets and pass."             "Fix: keep it a plain array literal."
        return 0
    }

    for name in $ext; do
        n=$((n + 1))
        [ -f "$name/PKGBUILD" ] && continue
        fail external "$name is in EXTERNAL but has no PKGBUILD"             "publish-sources.sh would fail on it, having already published"             "the components before it in the list."             "Fix: remove the name, or add the component."
    done

    for name in $ext; do
        [ -f "$name/PKGBUILD" ] || continue
        ver=$(pkgfield "$name" pkgver) || continue
        rel=$(pkgfield "$name" pkgrel) || continue
        want="https://github.com/velle999/$name/releases/download/$ver-$rel/$name-$ver.tar.gz"
        got=$(pkgsources "$name" | grep -F "releases/download" || true)

        if [ -z "$got" ]; then
            fail external "$name/PKGBUILD has no release source"                 "It is in EXTERNAL, so its tarball is published — but the"                 "PKGBUILD names only the local filename, which exists on this"                 "machine and nowhere else. makepkg stops at \"Retrieving"                 "sources\" for everybody who is not us."                 "Fix: source=(\"\$pkgname-\$pkgver.tar.gz::$want\")"
            continue
        fi
        case "$got" in
            *"::$want") ;;
            *) fail external "$name/PKGBUILD's release URL is not the one it needs"                    "want: $want"                    "got:  ${got#*::}"                    "A URL that does not carry pkgrel goes stale on the next"                    "bump and serves the PREVIOUS source, silently." ;;
        esac
        case "$got" in
            "$name-$ver.tar.gz::"*) ;;
            *) fail external "$name/PKGBUILD renames its local tarball"                    "The filename before :: is what makepkg looks for on disk,"                    "and collect-source.sh writes $name-$ver.tar.gz."                    "Written any other way, build-all.sh downloads instead of"                    "using the tree it just collected — so a local edit builds"                    "the RELEASED source and the change appears to do nothing." ;;
        esac

        # The closure.
        for dep in $(pkgdeps "$name"); do
            [ -f "$dep/PKGBUILD" ] || continue          # not ours; pacman's problem
            printf '%s\n' $ext | grep -qx "$dep" && continue
            pkgsources "$dep" | grep -qE '^[^:]*::https?://|^https?://' && continue
            fail external "$name depends on $dep, which nothing publishes"                 "$dep is a component of this repo, is not in EXTERNAL, and its"                 "source=() is a local tarball — so an outsider can install"                 "$name's dependency from nowhere."                 "Fix: add $dep to EXTERNAL, or drop $name from it."
        done
    done

    # The other direction.
    local c
    while IFS= read -r c; do
        c=$(dirname "$c")
        pkgsources "$c" | grep -qF "/$c/releases/download/" || continue
        printf '%s\n' $ext | grep -qx "$c" && continue
        fail external "$c/PKGBUILD points at a release nothing publishes"             "Its source=() names a SYNAPSE release asset, but $c is not in"             "publish-sources.sh's EXTERNAL, so that asset is never created."             "Every build outside this checkout 404s."             "Fix: add $c to EXTERNAL."
    done < <(git ls-files '*/PKGBUILD')

    [ "$FINDINGS" -eq "$bad" ] && ok external         "$n component(s) buildable without this checkout — set, URLs and closure agree"
    return 0
}

# ── Run ──────────────────────────────────────────────────────

case "$MODE" in
    staged)   printf '%spreflight — staged changes%s\n\n' "$C_DIM" "$C_0" ;;
    range)    printf '%spreflight — %s..HEAD%s\n\n' "$C_DIM" "$BASE" "$C_0" ;;
    worktree) printf '%spreflight — working tree against HEAD%s\n\n' "$C_DIM" "$C_0" ;;
esac

check_lists
check_registration
check_iso
check_installer
check_pkgver
check_order
check_icons
check_uifont
check_scrollbar
check_leavings
check_tarball
check_external
if [ "$AT_REST" -eq 1 ]; then
    note pkgrel "not checked — no staged set to read (--at-rest)" \
        "A pathspec commit carries no index, so the bump cannot be verified here." \
        "Run ./tools/preflight.sh yourself if you edited a component."
else
    check_pkgrel
fi

echo
if [ "$FINDINGS" -gt 0 ]; then
    printf '%s%d finding(s) — this commit cannot ship as it stands%s\n' \
        "$C_RED$C_B" "$FINDINGS" "$C_0" >&2
    exit 1
fi
printf '%sclean%s' "$C_GRN$C_B" "$C_0"
[ "$NOTES" -gt 0 ] && printf ' (%d note(s) above)' "$NOTES"
printf '\n'
exit 0
