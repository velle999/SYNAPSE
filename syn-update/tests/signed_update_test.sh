#!/usr/bin/env bash
# signed_update_test.sh — syn-update builds only what the project signed.
#
# Every commit it would build has to carry a good signature from a key in
# SYN_UPDATE_KEYS_DIR; it builds up to the last commit before the first one
# that does not, and nothing past it. docs/THREAT-MODEL.md §4.
#
#   1. a signed upstream is taken whole, and a first clone checks out only that
#   2. an unsigned commit on top is left out, and apply does not move onto it
#   3. ⛔ a SIGNED commit on top of an unsigned one does not carry it in
#   4. re-signing the history and force-pushing is the way back
#   5. a signature from any other key counts as none, whatever the user's own
#      keyring trusts
#   6. a tree with nothing signed is refused, and nothing of it is checked out
#   7. no installed keys means nothing is built
#   8. SYN_UPDATE_ALLOW_UNSIGNED=1 is the one way past, and it says so
#   9. `verify-range` gives the same answer for a checkout (tools/pre-push)
#
# Every upstream is a local repository; nothing here touches the network.
set -u

here=$(cd "$(dirname "$0")" && pwd)
E=${1:-$here/../syn-update.sh}
[ -f "$E" ] || { echo "  ABORT no syn-update.sh at $E"; exit 1; }
for t in git gpg makepkg pacman vercmp; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed."; exit 77; }
done

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

T=$(mktemp -d)
export XDG_CACHE_HOME="$T/cache" XDG_CONFIG_HOME="$T/config" HOME="$T"
export GIT_CONFIG_GLOBAL="$T/gitconfig" GIT_CONFIG_SYSTEM=/dev/null
. "$here/signing_lib.sh"
signing_setup
trap 'signing_teardown; rm -rf "$T"' EXIT
git config --file "$GIT_CONFIG_GLOBAL" user.name t
git config --file "$GIT_CONFIG_GLOBAL" user.email t@e

UP="$T/upstream"
git init -q -b main "$UP"
commit() {  # commit <file> [git -c options…]
    local f=$1; shift
    echo "$f" > "$UP/$f"
    git -C "$UP" add -A && git "$@" -C "$UP" commit -qm "$f"
    git -C "$UP" rev-parse HEAD
}
unsigned() { commit "$1" -c commit.gpgsign=false; }

SRC="$T/src"; mkdir -p "$SRC"
run() {
    SYN_UPDATE_SRC="$SRC" SYN_UPDATE_REPO="file://$UP" SYN_UPDATE_MIRRORS="" \
        bash "$E" "$@" 2>&1
}
verified() { git -C "$SRC" rev-parse -q --verify refs/syn-update/verified 2>/dev/null; }
# apply refuses to run as root (makepkg does), and CI's container is root.
ROOT=0; [ "$(id -u)" = 0 ] && ROOT=1
head_of()  { git -C "$SRC" rev-parse HEAD 2>/dev/null; }

# ── 1. all signed ───────────────────────────────────────────────────────────
c1=$(commit one)
out=$(run check); rc=$?
[ "$rc" = 0 ] && [ "$(verified)" = "$c1" ] \
    && ok "a signed upstream is verified through its tip" \
    || bad "a signed upstream was not accepted (rc=$rc verified=$(verified)): $out"
[ "$(head_of)" = "$c1" ] && [ -f "$SRC/one" ] \
    && ok "…and the first clone checked out that revision" \
    || bad "the first clone's working tree is not the verified revision"

# ── 2. an unsigned commit on top ────────────────────────────────────────────
c2=$(unsigned two)
out=$(run check); rc=$?
[ "$rc" = 0 ] && [ "$(verified)" = "$c1" ] \
    && ok "an unsigned commit on top is left out" \
    || bad "the unsigned commit was accepted, or check failed (rc=$rc verified=$(verified)): $out"
grep -q '1 newer commit(s) .* not signed' <<<"$out" \
    && ok "…and check says so" \
    || bad "check did not report the commit it is leaving out: $out"
if [ "$ROOT" = 1 ]; then
    ok "(apply not run: running as root)"
else
    out=$(run apply); rc=$?
    [ "$rc" = 0 ] && [ "$(head_of)" = "$c1" ] && [ ! -e "$SRC/two" ] \
        && ok "apply stays on the last signed commit" \
        || bad "apply moved onto the unsigned commit (rc=$rc head=$(head_of)): $out"
fi

# ── 3. the blessing ─────────────────────────────────────────────────────────
#
# A signature covers the whole tree, so somebody signing on top of a commit
# they never noticed would vouch for it. Every commit is checked, not the tip.
c3=$(commit three)
out=$(run check); rc=$?
[ "$(verified)" = "$c1" ] \
    && ok "a signed commit on top does not carry the unsigned one in" \
    || bad "the signed tip blessed the unsigned commit under it: verified=$(verified)"
grep -q '2 newer commit(s)' <<<"$out" \
    && ok "…and both are reported as waiting" \
    || bad "the report does not count both commits: $out"

# ── 4. the way back: re-sign and force-push ─────────────────────────────────
git -C "$UP" reset -q --hard "$c1"
git -C "$UP" cherry-pick -q "$c2" "$c3" >/dev/null 2>&1
c3b=$(git -C "$UP" rev-parse HEAD)
if [ "$ROOT" = 1 ]; then
    out=$(run check); rc=$?
    [ "$rc" = 0 ] && [ "$(verified)" = "$c3b" ] \
        && ok "re-signed and force-pushed, the same changes are verified" \
        || bad "the re-signed history was not verified (rc=$rc verified=$(verified)): $out"
else
    out=$(run apply); rc=$?
    [ "$rc" = 0 ] && [ "$(verified)" = "$c3b" ] && [ "$(head_of)" = "$c3b" ] \
        && ok "re-signed and force-pushed, the same changes are taken" \
        || bad "the re-signed history was not taken (rc=$rc verified=$(verified)): $out"
fi

# ── 5. somebody else's key ──────────────────────────────────────────────────
other=$(new_signing_key "$T/gnupg-other" "not the project")
c5=$(GNUPGHOME="$T/gnupg-other" commit five -c user.signingkey="$other")
[ "$(GNUPGHOME="$T/gnupg-other" git -C "$UP" log -1 --format=%G?)" = G ] ||
    bad "fixture: the other key's commit is not signed"
out=$(GNUPGHOME="$T/gnupg-other" run check); rc=$?
[ "$(verified)" = "$c3b" ] \
    && ok "a commit signed by another key is not built, even with that key trusted in the user's keyring" \
    || bad "a signature from a key that is not installed was accepted: $out"

# ── 6. nothing signed at all ────────────────────────────────────────────────
BARE="$T/unsigned-upstream"
git init -q -b main "$BARE"
echo x > "$BARE/stranger"
git -C "$BARE" add -A && git -c commit.gpgsign=false -C "$BARE" commit -qm stranger
SRC6="$T/src6"; mkdir -p "$SRC6"
out=$(SYN_UPDATE_SRC="$SRC6" SYN_UPDATE_REPO="file://$BARE" SYN_UPDATE_MIRRORS="" \
      bash "$E" check 2>&1); rc=$?
[ "$rc" != 0 ] && grep -q 'refusing to build' <<<"$out" \
    && ok "a tree with nothing signed is refused" \
    || bad "an unsigned tree was accepted (rc=$rc): $out"
[ ! -e "$SRC6/stranger" ] \
    && ok "…and none of it was checked out" \
    || bad "the refused tree is sitting in the working tree, where syn printer builds from"

# ── 7. no keys installed ────────────────────────────────────────────────────
mkdir -p "$T/no-keys"
out=$(SYN_UPDATE_KEYS_DIR="$T/no-keys" run check); rc=$?
[ "$rc" != 0 ] && grep -q 'no update-signing keys' <<<"$out" \
    && ok "with no keys installed nothing is built" \
    || bad "no keys did not stop the update (rc=$rc): $out"

# ── 8. the override ─────────────────────────────────────────────────────────
out=$(SYN_UPDATE_ALLOW_UNSIGNED=1 run check); rc=$?
[ "$rc" = 0 ] && [ "$(verified)" = "$c5" ] && grep -q 'WITHOUT checking' <<<"$out" \
    && ok "SYN_UPDATE_ALLOW_UNSIGNED=1 takes the tip, and warns" \
    || bad "the override did not work, or did not say so (rc=$rc): $out"

# ── 9. verify-range, for tools/pre-push ─────────────────────────────────────
bash "$E" verify-range "$UP" "$c3b" >/dev/null 2>&1 \
    && ok "verify-range passes a signed history" \
    || bad "verify-range refused a signed history"
bash "$E" verify-range "$UP" "$c5" >/dev/null 2>&1 \
    && bad "verify-range passed a commit signed by another key" \
    || ok "verify-range fails one signed by another key"

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
