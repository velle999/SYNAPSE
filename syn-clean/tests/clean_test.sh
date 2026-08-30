#!/usr/bin/env bash
#
# clean_test.sh — what syn-clean decides, inside a scratch home.
#
# ⛔ EVERY PATH THIS PROGRAM TOUCHES IS COMPOSED FROM SYNCLEAN_HOME, and the
# suite sets it to a temporary directory. That is not tidiness: this is a
# program that deletes directory trees, and a suite that could reach the real
# $HOME is one bad category string away from deleting the caches — or worse —
# of whoever ran it.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail

S=${1:-./build/syn-clean}
[ -x "$S" ] || { echo "not executable: $S" >&2; exit 1; }

T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
export SYNCLEAN_HOME="$T/home"

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
chk()  { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }
same() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want '$2', got '$3')"; fi; }

echo "clean"

mk() { mkdir -p "$(dirname "$1")"; head -c "$2" /dev/urandom > "$1"; }

mk "$SYNCLEAN_HOME/.cache/thumbnails/a.png"       20000
mk "$SYNCLEAN_HOME/.cache/thumbnails/b.png"       20000
mk "$SYNCLEAN_HOME/.cache/someapp/blob"           40000
mk "$SYNCLEAN_HOME/.local/share/Trash/files/old"  10000
mk "$SYNCLEAN_HOME/Documents/keep.txt"            5000

# ── scanning does not remove ────────────────────────────────────────────────
"$S" --rec scan >"$T/scan" 2>/dev/null
chk "scan runs" $?
[ -f "$SYNCLEAN_HOME/.cache/thumbnails/a.png" ]
chk "…and removes nothing" $?

grep -q '^thumbnails' "$T/scan"
chk "…and reports the thumbnails category" $?

# ⛔ THE TWO CACHE ROWS MUST NOT COUNT THE SAME BYTES. ~/.cache/thumbnails is
# inside ~/.cache, so a naive walk reports both and the total is a number that
# was never true.
thumbs=$(awk -F'\t' '$1=="thumbnails"{print $4}' "$T/scan")
user=$(awk -F'\t'   '$1=="usercache"{print $4}'  "$T/scan")
[ "$thumbs" -gt 0 ] && [ "$user" -gt 0 ]
chk "both cache rows report something" $?
# ⛔ AND IT MUST EXCLUDE EVERY SUBDIRECTORY ANOTHER ROW OWNS, not just
# thumbnails. ~/.cache/mozilla is inside ~/.cache, so a walker that took the
# whole directory reported the browser cache on two rows and the total at the
# bottom was a number that was never true — 2.5 GB of the first real run's
# 63 GB was counted twice.
mk "$SYNCLEAN_HOME/.cache/mozilla/big" 60000
"$S" --rec scan >"$T/scan2" 2>/dev/null
u2=$(awk -F'\t' '$1=="usercache"{print $4}'    "$T/scan2")
b2=$(awk -F'\t' '$1=="browsercache"{print $4}' "$T/scan2")
[ "$b2" -gt 50000 ]
chk "the browser cache is measured on its own row" $?
[ "$u2" -lt "$((u2 + b2))" ] && [ "$u2" = "$user" ]
chk "…and usercache did not grow by it — no double count" $?

# ── dry run ─────────────────────────────────────────────────────────────────
"$S" --rec --dry-run --yes clean thumbnails >/dev/null 2>&1
[ -f "$SYNCLEAN_HOME/.cache/thumbnails/a.png" ]
chk "--dry-run removes nothing" $?

# ── cleaning ────────────────────────────────────────────────────────────────
"$S" --rec --yes clean thumbnails >/dev/null 2>&1
[ ! -f "$SYNCLEAN_HOME/.cache/thumbnails/a.png" ]
chk "clean thumbnails removes them" $?
[ -f "$SYNCLEAN_HOME/.cache/someapp/blob" ]
chk "…and leaves the other caches alone" $?
[ -f "$SYNCLEAN_HOME/Documents/keep.txt" ]
chk "…and never touches documents" $?

# ⛔ NOT WITHOUT CONSENT. A front end pipes, and a pipe has nobody to answer a
# question — so silence must not be read as yes.
out=$(printf '' | "$S" --rec clean usercache 2>&1 >/dev/null)
[ -f "$SYNCLEAN_HOME/.cache/someapp/blob" ]
chk "clean on a pipe without --yes removes nothing" $?
grep -qi "pass --yes" <<<"$out"
chk "…and says what was missing" $?

# ── --all leaves the two that surprise people ───────────────────────────────
#
# ⚠ Cookies sign you out of every site; root categories cannot be done as this
# user at all. A sweep that swallowed either is a sweep people learn not to run.
mk "$SYNCLEAN_HOME/.mozilla/firefox/abc.default/cookies.sqlite" 8000
"$S" --rec --yes clean --all >/dev/null 2>&1
[ -f "$SYNCLEAN_HOME/.mozilla/firefox/abc.default/cookies.sqlite" ]
chk "--all does NOT delete cookies" $?
[ ! -f "$SYNCLEAN_HOME/.cache/someapp/blob" ]
chk "…but does clear the caches" $?

# Named explicitly, it goes — UNLESS a browser has the database open.
#
# ⛔ AND THAT SECOND CASE IS NOT A FLAKY TEST, IT IS THE FEATURE. A cookie jar
# is sqlite with a write-ahead log; deleting one under a live browser leaves the
# -wal and -shm pointing at a database that is gone, and the next start is a
# profile it cannot read. Which branch runs depends on whether the person
# running the suite has a browser open, so the suite asserts the right one
# rather than assuming an empty desktop.
out=$("$S" --rec --yes clean cookies 2>&1 >/dev/null)
if grep -qi "is running" <<<"$out"; then
    [ -f "$SYNCLEAN_HOME/.mozilla/firefox/abc.default/cookies.sqlite" ]
    chk "a running browser blocks the cookie jar rather than corrupting it" $?
    grep -qiE "firefox|chromium|vivaldi|chrome|brave" <<<"$out"
    chk "…and names which one to close" $?
else
    [ ! -f "$SYNCLEAN_HOME/.mozilla/firefox/abc.default/cookies.sqlite" ]
    chk "cookies go when asked for by name" $?
    ok "…no browser was running, so nothing had to be blocked"
fi

# ── shred ───────────────────────────────────────────────────────────────────
mk "$T/secret.txt" 4096
printf 'the quick brown fox jumps over the lazy dog' > "$T/secret.txt"
"$S" --yes shred "$T/secret.txt" >/dev/null 2>&1
[ ! -e "$T/secret.txt" ]
chk "shred removes the file" $?

# ⛔ AND IT SAYS WHAT GROUND IT WAS ON. Overwriting is only destruction on a
# filesystem that rewrites in place; on btrfs it is not, and a program that
# stayed quiet about that would be making a promise it cannot keep.
mk "$T/g.txt" 512
out=$("$S" --rec --dry-run --yes shred "$T/g.txt" 2>/dev/null)
head -1 <<<"$out" | grep -q "fstype"
chk "shred reports the filesystem it is working on" $?
[ -e "$T/g.txt" ]
chk "…and --dry-run still destroyed nothing" $?

# A directory, and only what is inside it.
mkdir -p "$T/tree/sub"
mk "$T/tree/one" 1000
mk "$T/tree/sub/two" 1000
mk "$T/bystander" 1000
"$S" --yes shred "$T/tree" >/dev/null 2>&1
[ ! -e "$T/tree" ]
chk "shred takes a folder and everything in it" $?
[ -e "$T/bystander" ]
chk "…and nothing beside it" $?

# ⛔ A SYMLINK IS A NAME, NOT THE FILE IT POINTS AT. Following one destroys
# something the user never selected.
mk "$T/real.txt" 900
ln -s "$T/real.txt" "$T/link"
"$S" --yes shred "$T/link" >/dev/null 2>&1
[ ! -e "$T/link" ]
chk "shredding a symlink removes the link" $?
[ -e "$T/real.txt" ]
chk "…and leaves its target alone" $?

# ── saying no clearly ───────────────────────────────────────────────────────
"$S" --rec list | grep -q "^id" ; chk "list has a header for a front end" $?
"$S" nonsense >/dev/null 2>&1; [ $? = 2 ]
chk "an unknown command is refused" $?
"$S" --nonsense >/dev/null 2>&1; [ $? = 2 ]
chk "an unknown option is refused" $?
"$S" --help | grep -qi "copy-on-write\|btrfs"
chk "--help carries the warning about what shred cannot do" $?

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
echo "syn-clean: PASS"
