#!/usr/bin/env bash
# ping_test.sh — the background check writes an answer a program can read
#
# `syn-update ping` is `check` with the report replaced by a small file the
# bar's update indicator reads. It runs from a systemd user timer, which is
# what most of the care below is about: there is no terminal, so anything that
# asks a question does not fail, it HANGS — and systemd skips a trigger whose
# service is still running, so a wedged ping means the indicator silently stops
# updating with nothing anywhere saying why.
#
#   1. it refuses, and RECORDS the refusal, when the tree is not usable
#   2. --every validates against systemd's grammar rather than a regex of ours
#   3. --every writes a drop-in, not the shipped unit
#   4. ⛔ the drop-in resets OnUnitActiveSec before setting it
#   5. ping takes no component names
#
# ⚠ WHAT IS NOT TESTED HERE, AND WHY. A real ping fetches from GitHub. A test
# that did would be a test that fails on a train, and one that passes for the
# wrong reason behind a proxy. The fetch is `check`'s code path and `check` is
# covered by using it; what is new in `ping` is the refusals, the state file and
# the timer plumbing, which is what this file is.
set -u

here=$(cd "$(dirname "$0")" && pwd)
E=${1:-$here/../syn-update.sh}
[ -f "$E" ] || { echo "  ABORT no syn-update.sh at $E"; exit 1; }

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else
    bad "$1 — wanted [$2], got [$3]"; fi; }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export XDG_CACHE_HOME="$T/cache" XDG_CONFIG_HOME="$T/config" HOME="$T"

# ── 1. an unusable tree is refused, and the refusal is RECORDED ─────────────
#
# ⛔ setup_src() can reach for sudo — it takes back a tree an older root-run
# left behind — and under a timer that prompt hangs for ever. ping refuses up
# front instead. The state file is written anyway, with status=error, so the
# indicator can say "could not check" rather than showing a stale count for
# ever: a checker that fails silently is worse than one that is switched off,
# because the bar goes on implying the machine is up to date.
out=$(SYN_UPDATE_SRC="$T/not-a-tree" bash "$E" ping 2>&1); rc=$?
check "an unusable source tree is a failure" "1" "$rc"
case "$out" in
    *"without asking for a password"*) ok "…and it says why, naming the fix" ;;
    *) bad "the refusal does not explain itself: $out" ;;
esac
state="$T/cache/syn-update/pending"
[ -f "$state" ] && ok "…and the refusal reached the state file" \
                || bad "no state file was written, so the indicator would go on
        showing whatever it last saw, for ever"
grep -q '^status=error$' "$state" 2>/dev/null \
    && ok "…as status=error, not as a zero count" \
    || bad "the state file does not record the error:
$(cat "$state" 2>/dev/null)"
grep -q '^reason=' "$state" 2>/dev/null \
    && ok "…with a reason the tooltip can show" \
    || bad "no reason= line"

# ── 2. the interval is systemd's grammar, not one invented here ─────────────
#
# ⚠ VALIDATED BY systemd-analyze, so "1week" and "90min" work without this
# script knowing they exist. A regex here would be a second, smaller grammar
# that drifts from the one actually parsing the unit — and the drift shows up
# as a timer that silently never fires.
out=$(bash "$E" ping --every "next tuesday" 2>&1); rc=$?
check "nonsense is refused" "1" "$rc"
case "$out" in
    *"not a time systemd understands"*) ok "…in systemd's own terms" ;;
    *) bad "unhelpful refusal: $out" ;;
esac

# ── 3. …and a good one lands in a DROP-IN ──────────────────────────────────
#
# ⛔ NOT IN THE SHIPPED UNIT. /usr/lib/systemd/user/syn-update-ping.timer
# belongs to the package and is replaced by every upgrade — and this package is
# the updater, so it upgrades often. An interval written there is one that
# silently reverts to 6h the first time syn-update updates itself, which is
# exactly the kind of setting-that-forgets nobody thinks to re-check.
bash "$E" ping --every 90min >/dev/null 2>&1
dropin="$T/config/systemd/user/syn-update-ping.timer.d/interval.conf"
[ -f "$dropin" ] && ok "the interval is written as a drop-in" \
                 || bad "no drop-in at $dropin"
grep -q '^OnUnitActiveSec=90min$' "$dropin" 2>/dev/null \
    && ok "…carrying the interval asked for" \
    || bad "the drop-in does not set the interval:
$(cat "$dropin" 2>/dev/null)"

# ── 4. ⛔ and it RESETS the list before setting it ──────────────────────────
#
# systemd's list-valued settings ACCUMULATE across drop-ins: without an empty
# assignment first, the shipped 6h and the user's 90min are both live and the
# timer fires on whichever comes round first. The symptom is a machine that
# checks more often than it was told to and no file that says why.
n=$(grep -c '^OnUnitActiveSec=' "$dropin" 2>/dev/null || echo 0)
check "the drop-in has both the reset and the value" "2" "$n"
head -n -1 "$dropin" 2>/dev/null | grep -q '^OnUnitActiveSec=$' \
    && ok "…the empty assignment coming first" \
    || bad "the reset is missing or in the wrong place — the shipped interval
        stays live and the timer fires on whichever comes first:
$(cat "$dropin" 2>/dev/null)"

# ── 5. ping is not an apply ────────────────────────────────────────────────
# `apply`, `ignore` and `unignore` take component names; ping does not, and a
# name after it would otherwise be swallowed by the same catch-all and silently
# ignored — a command that looks like it did something narrower than it did.
out=$(bash "$E" ping synui 2>&1); rc=$?
check "a component name after ping is refused" "1" "$rc"
case "$out" in
    *"takes no component names"*) ok "…saying what to type instead" ;;
    *) bad "unhelpful refusal: $out" ;;
esac

# ── 6. `apply` refreshing the badge can never break `apply` ─────────────────
#
# ⚠ THE BADGE DOES NOT CLEAR ITSELF. The bar reads the state file and nothing
# else — deliberately, so no bar ever does a git fetch — so until `apply` wrote
# it, a machine that had just been updated by hand went on showing "3 updates"
# until the six-hourly timer next fired. That reads exactly like an updater
# that did not update anything, which is what it was reported as.
#
# ⛔ AND IT MUST NEVER BE FATAL. It runs at the END of a successful build, after
# packages are installed and the manifest is saved. A failure to rewrite an
# indicator's cache turning a completed update into a red error would be far
# worse than a stale badge, which the next timer tick corrects anyway. So the
# hardest case is checked here: the same unusable tree that makes `ping` refuse
# outright must make this return quietly.
# A LOCAL git repository standing in for GitHub. `apply` against a tree with no
# components takes the "nothing to build" path, which is deliberately BEFORE the
# sudo keep-alive and before build-all.sh — so the whole thing runs with no
# network, no password and no build, and still reaches the refresh.
if command -v git >/dev/null 2>&1; then
    remote="$T/remote"
    mkdir -p "$remote"
    (
        cd "$remote" || exit 1
        git init -q -b main .
        git config user.email t@t; git config user.name t
        echo "not a component" > README.md
        git add -A && git commit -qm "empty tree"
    ) >/dev/null 2>&1

    # Created up front: setup_src reaches for sudo to make a source directory
    # it does not own, and refuses outright when it cannot ask — which is the
    # right behaviour and not what this phase is about.
    mkdir -p "$T/src"

    rm -f "$state"
    out=$(SYN_UPDATE_REPO="$remote" SYN_UPDATE_SRC="$T/src" \
          bash "$E" apply 2>&1); rc=$?
    check "apply against an empty tree succeeds" "0" "$rc"
    if [ -f "$state" ]; then
        ok "…and it REWROTE the badge's state file"
        grep -q '^updates=0$' "$state" 2>/dev/null \
            && ok "…saying nothing is pending, which is what just became true" \
            || bad "the refreshed state does not read updates=0:
$(cat "$state" 2>/dev/null)"
    else
        bad "apply left no state file — the bar would go on showing whatever it
        last saw until the six-hourly timer fired, which reads as an updater
        that did not update anything"
    fi
else
    ok "(skipped the apply-refresh phase: no git)"
fi

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
