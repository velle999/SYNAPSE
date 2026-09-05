#!/usr/bin/env bash
# mirror_failover_test.sh — an unreachable host must not stop an update.
#
# THE FAILURE THIS EXISTS FOR is not a bug that happened; it is one that can
# only happen once. Every SynapseOS machine updates by cloning the monorepo, so
# for as long as one host is the only copy of it, that host is the single point
# of failure for updating every installation in existence — including out of a
# security hole. syn-update can fall back to further sources now.
#
# ⚠ THERE IS NO SECOND HOST YET, and this suite is still the point. The default
# mirror list is EMPTY (Codeberg was the candidate and asks not to be used as a
# mirror of a project hosted elsewhere), so what is proven here is the
# MECHANISM — the order, the ref it lands on, and the switch that turns it off.
# Naming a host later is one line in syn-update.sh, and this is what says that
# line will work. Every source below is a local `file://` repository, so none of
# it depends on which host is eventually chosen.
#
#   1. a first-run CLONE tries every source, not just the first
#   2. a FETCH falls back too, and writes the ref origin would have written
#   3. ⛔ the fallback is skipped by the URL git ACTUALLY has, not by name
#   4. mirrors can be switched off, and then a dead origin is simply dead
#
# ⚠ NOTHING HERE TOUCHES THE NETWORK, the same reason ping_test.sh does not
# fetch: a test that reached a real host would fail on a train and pass behind a
# proxy for the wrong reason.
set -u

here=$(cd "$(dirname "$0")" && pwd)
E=${1:-$here/../syn-update.sh}
[ -f "$E" ] || { echo "  ABORT no syn-update.sh at $E"; exit 1; }
for t in git makepkg pacman vercmp; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export XDG_CACHE_HOME="$T/cache" XDG_CONFIG_HOME="$T/config" HOME="$T"
export GIT_CONFIG_GLOBAL="$T/gitconfig" GIT_CONFIG_SYSTEM=/dev/null

# The stand-in for the mirror: a real repository with a real branch.
UP="$T/upstream"
git init -q -b main "$UP"
git -C "$UP" config user.name  t
git -C "$UP" config user.email t@e
echo one > "$UP/file"
git -C "$UP" add -A && git -C "$UP" commit -qm one

DEAD="file://$T/there-is-no-repo-here"
LIVE="file://$UP"

# ── 1. the clone tries every source ─────────────────────────────────────────
#
# `check` does much more than clone and is expected to fail afterwards on a
# tree with no PKGBUILDs in it. The clone is what is being asserted, so the
# assertion is on the tree, not on the exit status.
# Pre-created and user-owned, the way syn-install leaves it on a real machine —
# setup_src() reaches for sudo when it has to make the directory itself, and a
# test that let it would hang waiting for a password nobody can type.
SRC1="$T/src1"; mkdir -p "$SRC1"
SYN_UPDATE_SRC="$SRC1" SYN_UPDATE_REPO="$DEAD" SYN_UPDATE_MIRRORS="$LIVE" \
    bash "$E" check >"$T/clone.log" 2>&1
if [ -d "$SRC1/.git" ]; then
    ok "a dead primary still produced a source tree, from the mirror"
else
    bad "no tree was cloned at all — a first run during an outage of the
        primary host would leave the machine with no way to ever update:
$(tail -5 "$T/clone.log")"
fi
[ "$(git -C "$SRC1" remote get-url origin 2>/dev/null)" = "$LIVE" ] \
    && ok "…and origin is the host that actually answered" \
    || bad "origin is not the mirror that served the clone"

# ── 2. a fetch falls back, into the ref origin would have written ───────────
#
# ⚠ THE REF IS THE POINT. remote_rev() and checkout_remote() both read
# `origin/<ref>` and must not learn which host answered; a fallback that
# fetched to FETCH_HEAD instead would leave both of them reading the stale
# value and reporting the machine up to date.
SRC2="$T/src2"
git clone -q --branch main "$LIVE" "$SRC2"
echo two > "$UP/file"
git -C "$UP" commit -qam two
want=$(git -C "$UP" rev-parse main)

# origin is now a dead URL on disk; the live repo is only reachable as a mirror.
git -C "$SRC2" remote set-url origin "$DEAD"
out=$(SYN_UPDATE_SRC="$SRC2" SYN_UPDATE_REPO="$DEAD" SYN_UPDATE_MIRRORS="$LIVE" \
      bash "$E" ping 2>&1)
got=$(git -C "$SRC2" rev-parse origin/main 2>/dev/null)
if [ "$got" = "$want" ]; then
    ok "a dead origin fell back to the mirror, and origin/main moved with it"
else
    bad "origin/main is $got, wanted $want — the fallback either did not run
        or landed on a ref nothing downstream reads:
$out"
fi

# ── 3. the skip is by the URL git HAS, not by the name of the primary ───────
#
# ⛔ A machine first set up while the primary was down was CLONED FROM a mirror,
# so its origin is the mirror and the primary is the fallback. Skipping "the
# primary" by name would re-try the remote that just failed and skip the one
# that works. Here origin is dead and the PRIMARY is the live URL — the reverse
# of case 2 — and it must still be reached.
SRC3="$T/src3"
git clone -q --branch main "$LIVE" "$SRC3"
git -C "$SRC3" update-ref -d refs/remotes/origin/main
git -C "$SRC3" remote set-url origin "$DEAD"
SYN_UPDATE_SRC="$SRC3" SYN_UPDATE_REPO="$LIVE" SYN_UPDATE_MIRRORS="" \
    bash "$E" ping >/dev/null 2>&1
[ "$(git -C "$SRC3" rev-parse origin/main 2>/dev/null)" = "$want" ] \
    && ok "a tree whose origin is the mirror still reaches the primary" \
    || bad "the fallback skipped the live URL because it was named primary —
        this is the machine that was installed during an outage"

# ── 4. mirrors off means mirrors off ────────────────────────────────────────
#
# The empty value has to be distinguishable from "unset", or a site that turns
# the fallback off silently keeps the built-in one. That is why REPO_MIRRORS
# uses ${VAR-default} and not ${VAR:-default}.
SRC4="$T/src4"
git clone -q --branch main "$LIVE" "$SRC4"
git -C "$SRC4" remote set-url origin "$DEAD"
out=$(SYN_UPDATE_SRC="$SRC4" SYN_UPDATE_REPO="$DEAD" SYN_UPDATE_MIRRORS="" \
      bash "$E" ping 2>&1); rc=$?
[ "$rc" -ne 0 ] \
    && ok "with mirrors switched off, a dead origin is a clean failure" \
    || bad "SYN_UPDATE_MIRRORS= did not switch the fallback off (rc=$rc)"

# ⛔ AND IT HAS TO REACH THE STATE FILE. This is what caught the silent exit:
# fetch_src ends in die(), die() exits, and called directly it took the whole
# ping down with its message swallowed by the redirect beside it — so the
# indicator kept showing its last count for ever with nothing saying the check
# had died. "Exited non-zero" is NOT enough to assert here; a timer's exit
# status is seen by nobody.
state="$T/cache/syn-update/pending"
if grep -q '^status=error$' "$state" 2>/dev/null; then
    ok "…recorded as status=error, so the indicator stops claiming a count"
else
    bad "no status=error in the state file — the bar would go on showing the
        last count it saw, which is the failure ping exists to prevent:
        rc=$rc out=[$out] state=[$(cat "$state" 2>/dev/null)]"
fi
grep -q '^reason=.*could not reach' "$state" 2>/dev/null \
    && ok "…with a reason naming the hosts that were tried" \
    || bad "the recorded reason does not say what was unreachable:
        $(cat "$state" 2>/dev/null)"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
