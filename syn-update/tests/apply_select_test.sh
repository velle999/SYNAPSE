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

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
