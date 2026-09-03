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
# Writes /etc/synapseos/components.conf through sudo. Stubbed so the assertions
# below can check WHEN it is called — after a successful build and never before
# one — without a password prompt or a write to the machine running the suite.
save_manifest() { echo "MANIFEST"; }
git() { echo shortrev; }
cd() { :; }
# ⚠ NOT optional. cmd_apply refreshes pacman's databases before it builds, and
# an unstubbed run of that would `sudo pacman -Syy` on whatever machine is
# running the suite.
sync_pacman_dbs() { echo "SYNC"; }
# ⚠ ALSO not optional, and for a sharper reason than the one above. Unstubbed,
# this is `sudo -v` — a real password prompt, from a test suite, on whatever
# machine is running it; and on a box where the credential happens to be cached
# it would instead fork a background loop holding root open past the test.
# Stubbed here so the ordering assertions below test WHERE it is called from,
# which is the part that can regress. The loop's own semantics — that it never
# creates a credential, and never outlives its parent — are tested separately
# at the end of this file, where sudo itself is a stub.
sudo_keepalive_start() { echo "KEEPALIVE"; }
# Stands in for build-all.sh: records the filter it was handed.
build_all_recorder() { echo "BUILD [$*]"; }
STUB
    echo "SRC=$repo"
    sed -n '/^COMPONENTS=(/,/^)/p' "$script"
    echo 'BUILT=(); SELECT=(); DECLINED=()'
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
        echo "DECLINED=($DECLINED_CSV)"
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

# What run_apply puts in DECLINED. A variable rather than another positional,
# because every existing call site passes the selection as "$@" and threading a
# fourth argument through them would mean editing tests this change must not
# touch.
DECLINED_CSV=""

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
echo "=== commits that install nothing say so ==="
#
# THE COMPLAINT THIS EXISTS FOR (velle, 2026-08-20): "updater is just listing
# it not trying to install". The tree was two commits ahead, `check` listed
# both, and then said "everything build-all.sh can update is already current"
# — which reads as an updater that can see an update and refuses to take it.
#
# It was right: one commit was a repo tool that is not in any package, the
# other was syn-install, which an installed system correctly does not have.
# COMMITS AND PACKAGES ARE NOT THE SAME THING, and the report let you think
# they were by showing one and then reporting on the other.
out=$(run_apply '' '')
case "$out" in
    *"nothing to build"*) ok "an apply with nothing to build still says so" ;;
    *) bad "the nothing-to-build line went missing: $out" ;;
esac
case "$out" in
    *"is now at"*)
        ok "…and says the source tree moved anyway" ;;
    *)  bad "an apply that advanced the tree said nothing about it — the
       repository's own tools live there and are not packaged: $out" ;;
esac
case "$out" in
    *"machine does not have"*|*"repository tooling"*)
        ok "…and why there was nothing in it to build" ;;
    *)  bad "no reason given for a commit that builds nothing: $out" ;;
esac

# ⚠ ONLY when the revision actually moved. A machine that is already at the
# tip has nothing to explain, and a paragraph about tooling on every no-op run
# is the noise that teaches people to stop reading the output.
same=$( { harness
          echo 'local_rev() { echo aaaaaaa; }; remote_rev() { echo aaaaaaa; }'
          echo "CHANGED=(); NEW=(); DECLINED=(); SELECT=()"
          echo 'cmd_apply'; } > "$tmp/same.sh"; bash "$tmp/same.sh" 2>&1)
case "$same" in
    *"is now at"*) bad "a machine already at the tip was told the tree moved: $same" ;;
    *)             ok "a machine already at the tip is told nothing extra" ;;
esac

echo ""
echo "=== the manifest decides what \"not installed\" MEANS ==="
#
# scan() has exactly one ambiguous state — a component that is in the tree and
# not on the disk — and before /etc/synapseos/components.conf there was nothing
# to disambiguate it with. These four cases are the whole rule.
scan_harness() {
    cat <<'STUB'
warn() { echo "warn $*"; }
buildable_names() { echo ""; }   # empty = scrape failed, block nothing
pkgfield() { echo 9; }           # every component reads as 9-9 available
# scan calls `pacman -Q <component>`; INSTALLED is the machine.
pacman() {
    case " $INSTALLED " in *" $2 "*) echo "$2 1-1"; return 0 ;; esac
    return 1
}
vercmp() { echo 1; }             # 9-9 is always newer than 1-1
STUB
    echo "SRC=$repo"
    sed -n '/^COMPONENTS=(/,/^)/p' "$script"
    sed -n '/^declare -A UNSUPPORTED=(/,/^)/p' "$script"
    sed -n '/^declare -A NEVER_ADD=(/,/^)/p' "$script"
    sed -n '/^MANIFEST=/p' "$script"
    echo 'declare -A COMP_KNOWN=(); MANIFEST_PRESENT=0'
    sed -n '/^load_manifest() {/,/^}/p' "$script"
    sed -n '/^scan() {/,/^}/p' "$script"
}

# scan_run <installed-list> <manifest-body|NOFILE> -> "CHANGED|NEW|DECLINED"
scan_run() {
    local installed=$1 manifest=$2
    if [ "$manifest" = NOFILE ]; then
        rm -f "$tmp/manifest.conf"
    else
        printf '%s\n' "$manifest" > "$tmp/manifest.conf"
    fi
    {
        scan_harness
        echo "MANIFEST=$tmp/manifest.conf"
        echo "INSTALLED='$installed'"
        cat <<'RUN'
scan
names() { local e; for e in "$@"; do set -- $e; printf '%s ' "$1"; done; }
printf '%s|%s|%s
' "$(names "${CHANGED[@]}")" "$(names "${NEW[@]}")" "$(names "${DECLINED[@]}")"
RUN
    } > "$tmp/scan.sh"
    bash "$tmp/scan.sh" 2>/dev/null | tail -1
}

# scan_run returns "CHANGED|NEW|DECLINED". Pull the fields apart by hand: the
# first cut of these assertions matched against ${out#*|}, which is NEW *and*
# DECLINED, so "was it queued for install" and "was it left alone" were the
# same test and both answered yes.
field() { local f=$1 o=$2; case $f in
    changed) printf ' %s ' "${o%%|*}" ;;
    new)     o=${o#*|}; printf ' %s ' "${o%%|*}" ;;
    dec)     printf ' %s ' "${o##*|}" ;;
esac; }
in_field() { case "$(field "$1" "$2")" in *" $3 "*) return 0 ;; esac; return 1; }

# 1. Named in the manifest and absent = the user's answer. Never built.
out=$(scan_run "synui vibe" "synui = selected
vibe = selected
cliamp = declined")
in_field dec "$out" cliamp \
    && ok "a component the manifest calls declined is DECLINED" \
    || bad "declined component not classified: [$out]"
in_field new "$out" cliamp \
    && bad "a declined component was queued for install: [$out]" \
    || ok "...and it was not queued as NEW"

# 2. NOT named in the manifest = never on offer here. Still NEW, still installed.
#    This is the 2026-08-08 fix and it has to survive the change.
out=$(scan_run "synui" "synui = selected
vibe = declined")
in_field new "$out" cliamp \
    && ok "a component the manifest has never heard of is NEW" \
    || bad "a genuinely new component was not queued: [$out]"
in_field dec "$out" vibe \
    && ok "...while the one it HAS heard of stays declined" \
    || bad "the declined component was not reported: [$out]"

# 3. Removed after the fact. `pacman -R vibe` is the same answer given the
#    other way round, and the manifest still says selected — what decides is
#    that the machine has been OFFERED it, not which word is on the line.
out=$(scan_run "synui" "synui = selected
vibe = selected")
in_field new "$out" vibe \
    && bad "a component removed with pacman -R was reinstalled: [$out]" \
    || ok "a component removed after the fact stays removed"

# 4. No file at all — every system installed before this existed. Read as
#    "everything missing was declined", because the other reading reinstalls
#    exactly the software this change exists to stop installing.
out=$(scan_run "synui" NOFILE)
[ -z "$(field new "$out" | tr -d ' ')" ] \
    && ok "with no manifest, nothing at all is queued for install" \
    || bad "a manifest-less system still force-installed: [$out]"
in_field dec "$out" vibe \
    && ok "...and the missing components are reported as declined" \
    || bad "a manifest-less system reported nothing at all: [$out]"

# The parser has to survive a hand-edited file: this is documented as plain
# text and safe to edit, and `vibe = declined` with spaces is what it shows.
out=$(scan_run "synui" "  vibe   =   declined   # took it off
# a comment
")
in_field new "$out" vibe \
    && bad "spaces and a trailing comment broke the parse: [$out]" \
    || ok "the manifest parses with spaces and comments"
in_field new "$out" cliamp \
    && ok "...and a name that is genuinely absent from it is still NEW" \
    || bad "the hand-edited file was not read as present at all: [$out]"

# NEVER_ADD outranks everything. syn-install is a disk partitioner and must not
# arrive as an update whatever any file says.
out=$(scan_run "synui" "synui = selected")
case "$out" in
    *syn-install*) bad "syn-install was queued: [$out]" ;;
    *)             ok "NEVER_ADD still outranks the manifest" ;;
esac

echo ""
echo "=== software the user did not pick is left alone ==="
#
# THE REGRESSION THIS SECTION EXISTS FOR. Every SynapseOS package is a checkbox
# in the installer, so "not installed" is an answer. scan() used to file every
# such component under NEW and apply installed it, handing back the whole suite
# to somebody who had deliberately taken a third of it — and the only escape
# was naming each component by hand on every single run.
DECLINED_CSV='"vibe 0 5" "cliamp 0 3"'

out=$(run_apply '"synui 1 2"' '""')
case "$out" in
    *vibe*|*cliamp*) bad "a bare apply built something the user declined: $out" ;;
    *)               ok "a bare apply builds nothing the user declined" ;;
esac
case "$out" in
    *"BUILD [synui"*) ok "...and still builds what IS installed and stale" ;;
    *)                 bad "the declined guard swallowed a real update: $out" ;;
esac

# Naming it IS the opt-in, and it has to work even when nothing else is stale —
# the early "nothing to build" return used to be computed from CHANGED and NEW
# alone and would have returned before ever looking at the name.
out=$(run_apply '' '' vibe)
case "$out" in
    *"BUILD [vibe]"*) ok "naming a declined component installs it" ;;
    *)                bad "apply <declined> built nothing: $out" ;;
esac
case "$out" in
    *MANIFEST*) ok "...and the new answer is written down" ;;
    *)          bad "the manifest was not rewritten after the opt-in: $out" ;;
esac

# One name is one opt-in. The others stay declined.
out=$(run_apply '' '' vibe)
case "$out" in
    *cliamp*) bad "opting into vibe dragged cliamp in too: $out" ;;
    *)        ok "opting into one declined component takes only that one" ;;
esac

# An apply that genuinely has nothing to do must still not ask for a password,
# and must not write the manifest — a run that built nothing has learned
# nothing about what anybody wants.
out=$(run_apply '' '')
case "$out" in
    *KEEPALIVE*|*MANIFEST*) bad "an apply with only declined components did work: $out" ;;
    *)                      ok "declined components alone are not work to do" ;;
esac

# NEW is untouched: a component the tree has gained is not a component anybody
# declined, and it still installs itself. This is the 2026-08-08 fix — synui
# 317 pointed two start-menu rows at a synpkg that had only just landed.
DECLINED_CSV='"vibe 0 5"'
out=$(run_apply '"synui 1 2"' '"synpkg 0 20"')
case "$out" in
    *"BUILD [synui synpkg]"*) ok "a genuinely new component still installs itself" ;;
    *) bad "the declined guard also blocked a new component: $out" ;;
esac

DECLINED_CSV=""

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

# ── and the one password is asked for in the same window ────────────────────
#
# ⚠ The keep-alive has to start BEFORE the refresh, not merely before the
# build. Both need root; if it started after, the refresh would raise the
# FIRST prompt itself and the "asked once, here is why" explanation would
# print after the thing it explains.
out=$(run_apply '"synui 1 2"' '""')
case "$out" in
    *KEEPALIVE*SYNC*BUILD*) ok "the password is asked for before the refresh AND the build" ;;
    *SYNC*KEEPALIVE*)       bad "the refresh prompted before the keep-alive did: $out" ;;
    *)                      bad "no keep-alive in an apply that builds: $out" ;;
esac
out=$(run_apply '' '')
case "$out" in
    *KEEPALIVE*) bad "an apply with nothing to build still asked for a password: $out" ;;
    *)           ok "an apply with nothing to build asks for no password" ;;
esac

# pacman_dbs_state against fixtures, which is the only way to reach these
# conditions without waiting a day or corrupting a real mirror copy.
#
# ⚠ It answers WHICH refresh, not merely whether one is needed, and the
# distinction is the whole point: -Syy re-downloads every database (~12-25 MB)
# while -Sy downloads only what changed. Paying the expensive one on every
# apply was the cost this replaced.
state_says() {   # state_says <dir>
    { harness; echo "SYNC_DIR=$1"; echo 'DB_FRESH_SECS=600'
      sed -n '/^pacman_dbs_state() {/,/^}/p' "$script"
      echo 'out=$(pacman_dbs_state); echo "${out:-CURRENT}"'
    } > "$tmp/state.sh"
    bash "$tmp/state.sh"
}
mkdir -p "$tmp/fresh" "$tmp/old" "$tmp/badsig" "$tmp/empty"
: > "$tmp/fresh/core.db"; : > "$tmp/fresh/core.db.sig"
: > "$tmp/old/core.db";   touch -d '2 hours ago' "$tmp/old/core.db"
# The signature written BEFORE the database it signs — a good signature over
# different content, which pacman calls a corrupted database. This is the ONE
# case a plain -Sy cannot clear, because -Sy keeps the cached signature.
: > "$tmp/badsig/core.db"; : > "$tmp/badsig/core.db.sig"
touch -d '2 days ago' "$tmp/badsig/core.db.sig"
check "a database synced a minute ago needs no refresh" CURRENT "$(state_says "$tmp/fresh")"
check "a database two hours old needs the CHEAP one"    sync    "$(state_says "$tmp/old")"
check "a signature older than its database forces -Syy" force   "$(state_says "$tmp/badsig")"
check "no databases at all forces -Syy"                 force   "$(state_says "$tmp/empty")"

# ⚠ The escalation is what makes preferring the cheap refresh safe: a stale
# signature that appears BETWEEN the check and the sync — the mirror updating
# mid-run, which is exactly how the case arises — is caught from what pacman
# says rather than paid for in advance by every other run.
sig_fail() {
    { harness
      sed -n '/^db_sig_failure() {/,/^}/p' "$script"
      echo "if db_sig_failure \"\$1\"; then echo ESCALATE; else echo GIVEUP; fi"
    } > "$tmp/sigfail.sh"
    bash "$tmp/sigfail.sh" "$1"
}
check "a signature complaint escalates to -Syy" ESCALATE \
    "$(sig_fail 'error: core: signature from "Arch Linux" is invalid')"
check "a corrupt database escalates too"        ESCALATE \
    "$(sig_fail 'error: failed to update core (invalid or corrupted database)')"
# ⚠ Not everything that fails a -Sy is fixed by downloading more. A mirror that
# is down, or no network at all, would have the expensive refresh fail in the
# same way a second time — for nothing, and slowly.
check "a mirror that is down does NOT escalate"  GIVEUP \
    "$(sig_fail 'error: failed retrieving file from mirror : Connection timed out')"

echo ""
echo "=== names are validated before anything is fetched ==="
out=$(bash "$script" apply not-a-component 2>&1)
case "$out" in
    *"is not a SynapseOS component"*) ok "an unknown name is refused by name" ;;
    *) bad "an unknown component name was not refused: $out" ;;
esac
# An unbuildable one is explained rather than called a typo.
# ⚠ NOT chibi any more. chibi was the example here until it moved into
# COMPONENTS — its payload is large but its SOURCE changes, and this assertion
# quietly became a test that the updater still refuses to update it.
out=$(bash "$script" apply synapse-llama 2>&1)
case "$out" in
    *"cannot be built on an installed system"*) ok "an unsupported component explains itself" ;;
    *) bad "synapse-llama was not explained: $out" ;;
esac
# ...and chibi is not one of them. It ships on the ISO and is rebuilt from its
# own repo like nexus-chat and tepris; leaving it out froze every chibi fix on
# every installed machine.
out=$(bash "$script" apply chibi 2>&1)
case "$out" in
    *"cannot be built on an installed system"*)
        bad "chibi is still refused by the updater: $out" ;;
    *"is not a SynapseOS component"*)
        bad "chibi is not registered with the updater at all: $out" ;;
    *) ok "chibi is updatable" ;;
esac
# Only apply takes names.
out=$(bash "$script" check synui 2>&1)
case "$out" in
    *"unknown argument: synui"*) ok "check takes no component names" ;;
    *) bad "check accepted a component name: $out" ;;
esac

echo ""
echo "=== the terminal the GUI hands the privileged build off to ==="
#
# ⚠ THE FLAG IS THE WHOLE POINT, and the terminal is chosen for it. `apply`
# drives build-all.sh, which runs `sudo pacman -U` mid-build — sudo with no
# controlling terminal cannot prompt, so this half has to leave the window and
# go somewhere a password can be typed. And it has to STAY there afterwards: a
# window that closes when the build ends takes the build log with it, which is
# the one thing anybody needs when an update fails.
#
# Asserted as a PAIR, because either half alone is a broken button. syntty
# without --hold is a window that vanishes at the moment it becomes useful;
# --hold on a terminal that has not got it is a launch that dies at parse, and
# a failed exec here is silent — the button simply does nothing.
qml="$here/../shell.qml"
if grep -q '"syntty", "--hold"' "$qml"; then
    ok "the GUI hands apply to syntty, held open"
else
    bad "the GUI does not launch syntty --hold: $(grep -n applyProc -A3 "$qml" | tr '\n' ' ')"
fi

if grep -q '"kitty"' "$qml"; then
    bad "something in the GUI is still pinned to kitty"
else
    ok "...and nothing is still pinned to kitty for that flag"
fi

echo ""
echo "=== the one password is held for the build, and not a moment longer ==="
#
# Every property here is a safety property, so none of them can be tested
# against the real sudo: an unstubbed run would either prompt from a test
# suite or — worse, on a machine with a cached credential — leave a live
# root-refreshing loop behind after the suite exited. sudo is a function.
cat > "$tmp/ka_prelude.sh" <<PRE
C_DIM=""; C_R=""
say()  { echo "\$*"; }
info() { echo "info \$*"; }
CRED="$tmp/ka.cred"
CALLS="$tmp/ka.calls"
: > "\$CALLS"
# -n refreshes an EXISTING credential and can never create one; -v is the
# password prompt, and it says so out loud so a test can assert it did not
# happen.
sudo() {
    case "\$1" in
      -n) echo n >> "\$CALLS"; [ -f "\$CRED" ] ;;
      -v) echo PROMPTED; : > "\$CRED" ;;
       *) return 0 ;;
    esac
}
sleep() { command sleep 0.05; }
PRE
sed -n '/^SUDO_KEEPALIVE_PID=""/p'            "$script" >> "$tmp/ka_prelude.sh"
sed -n '/^sudo_keepalive_stop() {/,/^}/p'     "$script" >> "$tmp/ka_prelude.sh"
sed -n '/^sudo_keepalive_start() {/,/^}/p'    "$script" >> "$tmp/ka_prelude.sh"

ka_run() {   # ka_run <body> ; stdin is /dev/null — NOT a terminal
    { cat "$tmp/ka_prelude.sh"; printf '%s\n' "$1"; } > "$tmp/ka_case.sh"
    bash "$tmp/ka_case.sh" </dev/null 2>&1
}

# A credential already cached — the common case, since setup_src may have
# reached for sudo already. Nothing to ask for, so nothing should be asked.
: > "$tmp/ka.cred"
out=$(ka_run 'sudo_keepalive_start; echo "rc=$?"; sudo_keepalive_stop')
case "$out" in
    *PROMPTED*) bad "asked for a password it already had: $out" ;;
    *rc=0*)     ok  "a cached credential is held without asking again" ;;
    *)          bad "the keep-alive did not start: $out" ;;
esac

# ⚠ THE ONE THAT MATTERS. No credential and no terminal is the GUI's position,
# and a background `sudo -v` there blocks forever on a prompt nobody can see.
# It has to decline and let the caller decide.
rm -f "$tmp/ka.cred"
out=$(ka_run 'sudo_keepalive_start; echo "rc=$?"')
case "$out" in
    *PROMPTED*) bad "prompted for a password with no terminal to type into: $out" ;;
    *rc=1*)     ok  "no credential and no terminal declines instead of prompting" ;;
    *)          bad "expected a refusal, got: $out" ;;
esac

# The refresh has to actually repeat — a loop that runs once holds nothing open.
: > "$tmp/ka.cred"
out=$(ka_run 'sudo_keepalive_start; command sleep 0.4; sudo_keepalive_stop; wc -l < "$CALLS"')
n=$(printf '%s' "$out" | tail -1 | tr -d ' ')
if [ "${n:-0}" -ge 3 ]; then ok "the credential is refreshed repeatedly ($n times in 0.4s)"
else bad "the keep-alive refreshed $n time(s) — it is not holding anything open"; fi

# Stopping it has to actually reap it.
out=$(ka_run 'sudo_keepalive_start; pid=$SUDO_KEEPALIVE_PID; sudo_keepalive_stop
              kill -0 "$pid" 2>/dev/null && echo ALIVE || echo GONE')
case "$out" in
    *GONE*) ok "stopping the keep-alive kills the refresher" ;;
    *)      bad "the refresher survived its stop: $out" ;;
esac

# ⚠ AND THE BELT, TESTED WITHOUT THE BRACES. The EXIT trap cannot run when the
# parent is SIGKILLed — a crash, an OOM kill, a `kill -9` on a hung build — and
# that is precisely when an orphaned loop holding a passwordless root
# credential open would be worst. Only the loop's own parent check can save it,
# so the trap is taken away here to prove that check works on its own.
: > "$tmp/ka.cred"
{ cat "$tmp/ka_prelude.sh"
  echo "sudo_keepalive_start; echo \$SUDO_KEEPALIVE_PID > $tmp/ka.child; command sleep 30"
} > "$tmp/ka_orphan.sh"
bash "$tmp/ka_orphan.sh" </dev/null >/dev/null 2>&1 &
ka_parent=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do [ -s "$tmp/ka.child" ] && break; command sleep 0.1; done
ka_child=$(cat "$tmp/ka.child" 2>/dev/null)
kill -9 "$ka_parent" 2>/dev/null
# Reap it here, quietly. Left to be noticed asynchronously, bash prints its own
# "Killed" job line into the middle of the suite's output.
wait "$ka_parent" 2>/dev/null
command sleep 0.5
if [ -z "$ka_child" ]; then
    bad "the keep-alive never recorded a refresher pid"
elif kill -0 "$ka_child" 2>/dev/null; then
    kill -9 "$ka_child" 2>/dev/null
    bad "a SIGKILLed run left a root-refreshing loop behind (pid $ka_child)"
else
    ok "a SIGKILLed run leaves no refresher behind, trap or no trap"
fi


# ── the wallpaper's accent reaches this window ──────────────────────────────
#
# 387 gave the BAR the colour synui measures off the wallpaper, and only the
# bar: every app window beside it kept the preset's accent, so a desktop with
# the switch on wore two colours at once — the picture's on the bar, the
# theme's on Files, Software, Disks and the rest. These windows read
# ~/.config/synui/palette.state now, and this is the check that they do.
#
# ⚠ IT LOADS THE FILE IN A REAL ENGINE rather than grepping for the property.
# A duplicate property name is the trap this feature has sprung before: the
# file PARSES, qmllint is happy, and the type then refuses to LOAD, naming a
# line that is not the one at fault. Only running it can tell.
#
# Three cases, because two of them are the ones already got wrong once:
#   use=yes  the MEASURED colour;
#   use=no   the theme's own, because `use` is the SETTING and synui writes
#            the file whichever way it is set — reading the colour without
#            checking it is how the bar came to wear a wallpaper on themes
#            that never asked for one (386);
#   ok=no    the theme's own, the picture having no usable hue to give.
if [ -f "$qml" ] && command -v quickshell >/dev/null 2>&1; then
    WPT=$(mktemp -d)
    mkdir -p "$WPT/home/.config/synui" "$WPT/run"
    # A preset accent that is nothing like the measured one, so "it took the
    # wallpaper's" and "it kept the theme's" cannot be confused for each other.
    cat > "$WPT/home/.config/synui/theme.json" <<'WPJSON'
{ "scheme": "dark", "accent": [0,214,229], "glyph": [0,214,229],
  "bar": [25,28,35], "popup": [17,21,28], "fg": "#c8e3ee" }
WPJSON
    # A COPY with a probe timer appended INSIDE the root object — outside its
    # final brace the file is a syntax error and this would "fail" on a QML
    # that is perfectly good.
    awk 'BEGIN{RS="\0"} {
            n = match($0, /}[ \t\r\n]*$/)
            printf "%s\n    Timer { running: true; interval: 1200; repeat: false;\n             onTriggered: { console.log(\"WPACCENT=\" + root.cAccent); Qt.quit() } }\n%s", substr($0,1,n-1), substr($0,n)
         }' "$qml" > "$WPT/probe.qml"
    # ⚠ QT_ASSUME_STDERR_HAS_CONSOLE=1, or console.log() prints NOTHING at all
    # and every case below reads as an empty accent — a green suite that tested
    # the engine's silence. GSETTINGS_BACKEND=memory because the fake HOME has
    # no dconf for Qt's platform theme to find.
    wp_accent() {  # wp_accent <use> <ok> -> the colour the window resolves
        printf 'use=%s\nok=%s\naccent=#6479FF\naccent_dim=#37438C\nsecondary=#C68F14\n' \
               "$1" "$2" > "$WPT/home/.config/synui/palette.state"
        HOME="$WPT/home" XDG_RUNTIME_DIR="$WPT/run" QT_QPA_PLATFORM=offscreen \
        GSETTINGS_BACKEND=memory QT_ASSUME_STDERR_HAS_CONSOLE=1 \
        timeout 30 quickshell -p "$WPT/probe.qml" 2>&1 |
            sed -n 's/.*WPACCENT=\(#[0-9a-fA-F]*\).*/\1/p' | head -1
    }
    [ "$(wp_accent yes yes)" = "#6479ff" ] \
        && ok "the measured wallpaper accent reaches the window" \
        || bad "the window ignores palette.state and stays on the preset accent"
    [ "$(wp_accent no yes)" = "#00d6e5" ] \
        && ok "wallpaper_accent off leaves the theme's accent alone" \
        || bad "the window wears the wallpaper with use=no in palette.state"
    [ "$(wp_accent yes no)" = "#00d6e5" ] \
        && ok "a wallpaper with no usable hue falls back to the theme" \
        || bad "the window took a colour out of a palette.state saying ok=no"
    rm -rf "$WPT"
else
    echo "  skip  quickshell not installed, cannot check the wallpaper accent"
fi

# ── holding a component back ────────────────────────────────────────────────
#
# `held` is a THIRD manifest state beside selected and declined, and the two
# ways it can go wrong are both silent:
#
#   - save_manifest recomputes the whole file from `pacman -Q`, which can only
#     see installed-or-not. A held component IS installed, so without the guard
#     it is written back as `selected` and quietly resumes updating.
#   - scan routes held out of CHANGED. Without the headline guard, a machine
#     whose only pending change is held reports "everything is already current"
#     — false, in the direction that hides the hold.

echo "=== holding a component back ==="

hold_tmp=$(mktemp -d); trap 'rm -rf "$tmp" "$hold_tmp"' EXIT

# save_manifest, run against a fixture manifest with a real `pacman -Q` stub.
# install(1) is left real: MANIFEST points inside a temp directory the test
# owns, so the writability branch takes the no-sudo path and nothing on the
# machine running this is touched.
manifest_after() {   # manifest_after <installed-csv> <prior-manifest-body>
    local installed=$1 prior=$2
    printf '%s\n' "$prior" > "$hold_tmp/in.conf"
    {
        echo 'say()  { echo "$*"; }'
        echo 'warn() { echo "warn  $*"; }'
        echo 'ok()   { echo "ok    $*"; }'
        echo "MANIFEST=$hold_tmp/out.conf"
        echo "SRC=$repo"
        echo "MANIFEST_PRESENT=1"
        # Only these three exist, so `declined` is reachable in the output.
        echo 'COMPONENTS=(synui vibe tepris)'
        echo "pacman() { case \" $installed \" in *\" \$2 \"*) echo \"\$2 1.0-1\";; *) return 1;; esac; }"
        sed -n '/^declare -A COMP_KNOWN=()/,/^MANIFEST_PRESENT=0/p' "$script"
        sed -n '/^load_manifest() {/,/^}/p' "$script"
        sed -n '/^install_manifest() {/,/^}/p' "$script"
        sed -n '/^save_manifest() {/,/^}/p' "$script"
        echo "MANIFEST=$hold_tmp/in.conf; load_manifest"
        echo "MANIFEST=$hold_tmp/out.conf; save_manifest >/dev/null"
        echo "grep -v '^#' $hold_tmp/out.conf | grep . | tr -d ' '"
    } > "$hold_tmp/m.sh"
    bash "$hold_tmp/m.sh" 2>/dev/null | tr '\n' ' ' | sed 's/ $//'
}

# THE REGRESSION. save_manifest asks pacman what is installed; a held component
# is installed, so the naive answer is `selected` and the hold evaporates on
# the first successful apply — the user's answer, silently discarded by the
# function whose whole job is to remember answers.
check "a held component survives save_manifest" \
      "synui=held vibe=selected tepris=declined" \
      "$(manifest_after "synui vibe" "synui = held
vibe = selected
tepris = declined")"

# The other direction: a hold is not sticky against removal. Uninstalled is
# uninstalled, and `declined` is the state that says so.
check "a held component that was removed becomes declined" \
      "synui=declined vibe=selected tepris=declined" \
      "$(manifest_after "vibe" "synui = held
vibe = selected
tepris = declined")"

# manifest_set_state edits ONE line and leaves the rest byte for byte. It is
# not save_manifest: that recomputes the file and is only correct after a
# successful build, so using it here would rewrite states this command was
# never asked about.
set_state() {   # set_state <name> <state> <prior>
    printf '%s\n' "$3" > "$hold_tmp/s.conf"
    {
        echo 'die() { echo "fail  $*"; exit 1; }'
        echo "MANIFEST=$hold_tmp/s.conf"
        sed -n '/^declare -A COMP_KNOWN=()/,/^MANIFEST_PRESENT=0/p' "$script"
        sed -n '/^load_manifest() {/,/^}/p' "$script"
        sed -n '/^install_manifest() {/,/^}/p' "$script"
        sed -n '/^manifest_set_state() {/,/^}/p' "$script"
        echo "manifest_set_state $1 $2"
        echo "cat $hold_tmp/s.conf"
    } > "$hold_tmp/s.sh"
    bash "$hold_tmp/s.sh" 2>/dev/null
}

check "setting a state rewrites only that line" \
      "# a comment
synui = held
vibe = selected" \
      "$(set_state synui held "# a comment
synui = selected
vibe = selected")"

# A machine installed before this component existed has no line for it. The
# hold is still an answer and still has to be recorded.
check "a component the manifest never named is appended" \
      "vibe = selected
synui = held" \
      "$(set_state synui held "vibe = selected")"

# The headline. HELD is deliberately kept out of CHANGED so nothing downstream
# has to remember to exclude it — which is exactly why the summary line has to
# know about it separately.
report_headline() {   # report_headline <changed-csv> <held-csv>
    {
        echo 'C_DIM=""; C_R=""'
        echo 'say() { echo "$*"; }; info() { echo "info  $*"; }'
        echo 'ok() { echo "ok    $*"; }; warn() { echo "warn  $*"; }'
        echo "CHANGED=($1)"; echo "HELD=($2)"
        echo 'NEW=(); DECLINED=(); BLOCKED=(); SKIPPED=()'
        echo 'declare -A UNSUPPORTED=(); MANIFEST_PRESENT=1; MANIFEST=/dev/null'
        sed -n '/^report() {/,/^}/p' "$script"
        echo 'report'
    } > "$hold_tmp/r.sh"
    bash "$hold_tmp/r.sh" 2>&1
}

case "$(report_headline "" "\"synui 1.0-1 1.0-2\"")" in
    *"already current"*)
        bad "the report says everything is current while holding an update back" ;;
    *"HELD BACK"*)
        ok "a held update is reported instead of being called current" ;;
    *)  bad "a held update was neither reported nor explained" ;;
esac

case "$(report_headline "" "\"synui 1.0-1 1.0-2\"")" in
    *"1.0-1"*"1.0-2"*)
        ok "the report names the version it is refusing" ;;
    *)  bad "the held row does not say what is being held back" ;;
esac

# Nothing held: the wording must not change for everybody else.
case "$(report_headline "" "")" in
    *"already current"*) ok "with nothing held, the summary is unchanged" ;;
    *)                   bad "the held guard changed the ordinary summary" ;;
esac

# ── NEVER_ADD: what an update must not bring, and what to type instead ───────
#
# The guard runs at argument-parsing time, so it is extracted the same way
# everything else here is. `pacman` is a FUNCTION, which beats PATH — the
# suite must never consult the packages actually installed on the machine
# running it, or it would pass or fail depending on whose desk it is on.
never_add_guard() {   # never_add_guard <name> <installed:yes|no>
    local tmp; tmp=$(mktemp -d)
    {
        echo 'die() { echo "fail  $*"; exit 1; }'
        echo "pacman() { [ '$2' = yes ]; }"
        sed -n '/^declare -A NEVER_ADD=(/,/^)/p' "$script"
        sed -n '/^declare -A NEVER_ADD_HOW=(/,/^)/p' "$script"
        echo 'declare -A UNSUPPORTED=()'
        sed -n '/^COMPONENTS=(/,/^)/p' "$script"
        echo "SELECT=($1)"
        sed -n '/^for _s in "\${SELECT\[@\]}"; do/,/^done$/p' "$script"
        echo 'echo "reached the command"'
    } > "$tmp/g.sh"
    bash "$tmp/g.sh" 2>&1
    rm -rf "$tmp"
}

echo ""
echo "=== a component that must never arrive on its own ==="

case "$(never_add_guard samsung-m2020 no)" in
    *"syn printer samsung"*)
        ok "naming an un-installed NEVER_ADD component says where it comes from" ;;
    *"reached the command"*)
        bad "apply accepted a component it would silently build nothing for" ;;
    *)  bad "the refusal does not say how to install it" ;;
esac

case "$(never_add_guard samsung-m2020 yes)" in
    *"reached the command"*)
        ok "once installed, the same name updates like any other component" ;;
    *)  bad "an installed NEVER_ADD component can no longer be updated by name" ;;
esac

case "$(never_add_guard synui no)" in
    *"reached the command"*)
        ok "an ordinary component is not caught by the guard" ;;
    *)  bad "the guard refuses a component that is only declined" ;;
esac

# Two rosters. A future entry added to one and not the other would print
# "installed by hand" at somebody rather than a command they can run.
rosters=$(
    { sed -n '/^declare -A NEVER_ADD=(/,/^)/p' "$script"
      sed -n '/^declare -A NEVER_ADD_HOW=(/,/^)/p' "$script"
      echo 'for k in "${!NEVER_ADD[@]}"; do [ -n "${NEVER_ADD_HOW[$k]:-}" ] || echo "$k"; done'
    } | bash)
if [ -z "$rosters" ]; then
    ok "every NEVER_ADD component says how it is meant to arrive"
else
    bad "no install path for: $rosters"
fi

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
