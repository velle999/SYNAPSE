#!/usr/bin/env bash
# apply_select_test.sh — `syn-update apply <component>` and its dependency guard
#
# The subset path is the one worth testing here, because it is the one that can
# be silently wrong: building synui while scenefx0.5 is also out of date links
# it against the library about to be replaced, and it looks like it worked.
#
# cmd_apply and component_deps are pulled out of the script and run against
# stubs. syn-update has no test seam of its own — it is a script that fetches,
# builds and installs from the first line of every command — so extracting the
# two pure-ish functions is the way to reach them without a machine to rebuild.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

here=$(cd "$(dirname "$0")" && pwd)
script="$here/../syn-update.sh"
repo=$(cd "$here/../.." && pwd)

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else
            printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fail=$((fail + 1)); fi }

# ── The stubbed world ───────────────────────────────────────────────────────
harness() {
    cat <<'STUB'
C_DIM=""; C_R=""
say()  { echo "$*"; }
info() { echo "info  $*"; }
ok()   { echo "ok    $*"; }
warn() { echo "warn  $*"; }
die()  { echo "fail  $*"; exit 1; }
need_not_root() { :; }; need_tools() { :; }; setup_src() { :; }; fetch_src() { :; }
local_rev() { echo aaaaaaa; }; remote_rev() { echo bbbbbbb; }
show_commits() { :; }; checkout_remote() { :; }; report() { :; }; scan() { :; }
refresh_local_repo() { echo "REFRESH built=[${BUILT[*]}]"; }
git() { echo shortrev; }
cd() { :; }
# ⚠ NOT optional. cmd_apply refreshes pacman's databases before it builds, and
# an unstubbed run of that would `sudo pacman -Syy` on whatever machine is
# running the suite.
sync_pacman_dbs() { echo "SYNC"; }
# Stands in for build-all.sh: records the filter it was handed.
build_all_recorder() { echo "BUILD [$*]"; }
STUB
    echo "SRC=$repo"
    sed -n '/^COMPONENTS=(/,/^)/p' "$script"
    echo 'BUILT=(); SELECT=()'
    sed -n '/^component_deps() {/,/^}/p' "$script"
    # cmd_apply shells out to ./build-all.sh; point that at the recorder.
    sed -n '/^cmd_apply() {/,/^}/p' "$script" \
        | sed 's|\./build-all\.sh "\${names\[@\]}"|build_all_recorder "${names[@]}"|'
}

run_apply() {   # run_apply <changed-csv> <new-csv> <selected...>
    local changed=$1 new=$2; shift 2
    {
        harness
        echo "CHANGED=($changed)"
        echo "NEW=($new)"
        # `printf '"%s" ' "$@"` with NO arguments still applies the format once
        # and emits an empty string, which made SELECT one element long and sent
        # the no-names case down the subset path. Emit the array only when there
        # is something to put in it.
        if [ $# -gt 0 ]; then
            printf 'SELECT=('; printf '"%s" ' "$@"; printf ')\n'
        else
            printf 'SELECT=()\n'
        fi
        echo 'cmd_apply'
    } > "$tmp/run.sh"
    bash "$tmp/run.sh" 2>&1
}

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

echo "=== component_deps reads the PKGBUILDs ==="
#
# Derived, never a list in the script: a hand-maintained edge list is wrong the
# first time a depends= line changes, and the failure is a component linked
# against a version about to be replaced.
deps_of() {
    { harness; echo "component_deps $1"; } > "$tmp/d.sh"
    bash "$tmp/d.sh"
}
case "$(deps_of synui)" in
    *scenefx0.5*) ok "synui depends on scenefx0.5" ;;
    *)            bad "synui's dependency on scenefx0.5 was not found" ;;
esac
case "$(deps_of vibe)" in
    *synapd*) ok "vibe depends on synapd" ;;
    *)        bad "vibe's dependency on synapd was not found" ;;
esac
check "a component with no component dependencies reports none" "" "$(deps_of synapd)"

# THE REGRESSION. `local c=$1 pk="$SRC/$c/PKGBUILD"` expands every word before
# local assigns any of them, so $c is empty and pk names a file that does not
# exist — component_deps then returns nothing for everything and the guard
# below can never fire. It passed review because the first test called it from
# a loop whose variable was also named c, so the expansion found the global.
{ harness; echo 'c=WRONG; component_deps synui'; } > "$tmp/shadow.sh"
case "$(bash "$tmp/shadow.sh")" in
    *scenefx0.5*) ok "component_deps ignores a caller variable named c" ;;
    *)            bad "component_deps reads \$c from its caller" ;;
esac

echo ""
echo "=== a named subset builds only what was named ==="
out=$(run_apply '"synui 1 2" "scenefx0.5 1 2" "synpkg 19 20"' '"syn-confine 0 1"' synpkg)
case "$out" in
    *"BUILD [synpkg]"*) ok "apply synpkg builds synpkg alone" ;;
    *)                  bad "apply synpkg did not filter: $out" ;;
esac
case "$out" in
    *"REFRESH built=[synpkg]"*) ok "the local repo publishes only what was built" ;;
    *) bad "refresh_local_repo was not given the built set: $out" ;;
esac

# No names at all is the old behaviour and stays the default.
out=$(run_apply '"synui 1 2" "synpkg 19 20"' '""')
case "$out" in
    *"BUILD [synui synpkg"*) ok "apply with no names still builds everything changed" ;;
    *) bad "apply with no names changed behaviour: $out" ;;
esac

echo ""
echo "=== the dependency guard ==="
out=$(run_apply '"synui 1 2" "scenefx0.5 1 2"' '""' synui)
case "$out" in
    *"depends on scenefx0.5, which is ALSO out of date"*)
        ok "building synui alone warns that scenefx0.5 is also stale" ;;
    *)  bad "no warning when a stale dependency was left out: $out" ;;
esac
case "$out" in
    *"syn-update apply synui scenefx0.5"*)
        ok "the warning names the command that does both" ;;
    *)  bad "the warning does not say how to fix it: $out" ;;
esac
# It WARNS and proceeds. Refusing would make the button useless in exactly the
# case where avoiding the full build is the point.
case "$out" in
    *"BUILD [synui]"*) ok "the warning does not stop the build" ;;
    *)                 bad "a stale dependency blocked the build: $out" ;;
esac

out=$(run_apply '"synui 1 2" "scenefx0.5 1 2"' '""' synui scenefx0.5)
case "$out" in
    *"ALSO out of date"*) bad "warned even though both were named: $out" ;;
    *)                    ok "naming both does not warn" ;;
esac

echo ""
echo "=== a component with nothing to build is not an error ==="
# A GUI re-lists after a build and will ask for something now current; failing
# there would turn a no-op into a red banner.
out=$(run_apply '"synui 1 2"' '""' syn-disks)
case "$out" in
    *"already current, nothing to build: syn-disks"*)
        ok "a current component says so by name" ;;
    *)  bad "a current component was not reported: $out" ;;
esac
case "$out" in
    *"BUILD ["*) bad "a current component still ran a build: $out" ;;
    *)           ok "a current component runs no build" ;;
esac

echo ""
echo "=== the database refresh runs before the build, and only when needed ==="
#
# `pacman -Syy` re-downloads every database (~12 MB here). Doing it on an apply
# with nothing to build would ask for a password for no reason, and doing it on
# the syn-update apply that `synpkg upgrade` runs a second after refreshing as
# root would pay for the same download twice.
out=$(run_apply '"synui 1 2"' '""')
case "$out" in
    *SYNC*BUILD*) ok "the refresh happens BEFORE the build" ;;
    *BUILD*SYNC*) bad "the refresh ran after the build: $out" ;;
    *)            bad "no refresh in an apply that builds: $out" ;;
esac
# Genuinely empty arrays — NOT the '""' the tests above pass, which is one
# empty-string element and does not reach the "nothing to build" return.
out=$(run_apply '' '')
case "$out" in
    *SYNC*) bad "an apply with nothing to build still asked for a password: $out" ;;
    *)      ok "an apply with nothing to build does not refresh" ;;
esac

# pacman_dbs_stale against fixtures, which is the only way to reach the two
# conditions without waiting a day or corrupting a real mirror copy.
stale_says() {   # stale_says <dir>
    { harness; echo "SYNC_DIR=$1"; echo 'DB_FRESH_SECS=600'
      sed -n '/^pacman_dbs_stale() {/,/^}/p' "$script"
      echo 'if pacman_dbs_stale; then echo STALE; else echo FRESH; fi'
    } > "$tmp/stale.sh"
    bash "$tmp/stale.sh"
}
mkdir -p "$tmp/fresh" "$tmp/old" "$tmp/badsig" "$tmp/empty"
: > "$tmp/fresh/core.db"; : > "$tmp/fresh/core.db.sig"
: > "$tmp/old/core.db";   touch -d '2 hours ago' "$tmp/old/core.db"
# The signature written BEFORE the database it signs — a good signature over
# different content, which pacman calls a corrupted database.
: > "$tmp/badsig/core.db"; : > "$tmp/badsig/core.db.sig"
touch -d '2 days ago' "$tmp/badsig/core.db.sig"
check "a database synced a minute ago is fresh"       FRESH "$(stale_says "$tmp/fresh")"
check "a database two hours old is stale"             STALE "$(stale_says "$tmp/old")"
check "a signature older than its database is stale"  STALE "$(stale_says "$tmp/badsig")"
check "no databases at all counts as stale"           STALE "$(stale_says "$tmp/empty")"

echo ""
echo "=== names are validated before anything is fetched ==="
out=$(bash "$script" apply not-a-component 2>&1)
case "$out" in
    *"is not a SynapseOS component"*) ok "an unknown name is refused by name" ;;
    *) bad "an unknown component name was not refused: $out" ;;
esac
# An unbuildable one is explained rather than called a typo.
out=$(bash "$script" apply chibi 2>&1)
case "$out" in
    *"cannot be built on an installed system"*) ok "an unsupported component explains itself" ;;
    *) bad "chibi was not explained: $out" ;;
esac
# Only apply takes names.
out=$(bash "$script" check synui 2>&1)
case "$out" in
    *"unknown argument: synui"*) ok "check takes no component names" ;;
    *) bad "check accepted a component name: $out" ;;
esac

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
