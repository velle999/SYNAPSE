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

# ⛔ THE LOCALE THIS SUITE ASSERTS IN IS PINNED. Every assertion below looks for
# an English phrase, and once syn-clean is installed the binary answers the
# desktop's language — so on a German box they fail for a program that is
# working exactly as intended.
# ⚠ LANGUAGE is UNSET, not set: gettext reads it before LC_ALL, so an ambient
# LANGUAGE=de wins over LC_ALL=C and the pin does nothing.
export LC_ALL=C.UTF-8
unset LANGUAGE


S=${1:-./build/syn-clean}
[ -x "$S" ] || { echo "not executable: $S" >&2; exit 1; }

# ⛔ NEVER AS root, AND THAT IS NOT CAUTION EITHER.
#
# Since 0.1.0-5 the root categories are DONE rather than refused — `clean --all`
# with root empties /var/cache/pacman/pkg and vacuums the journal — and this
# suite runs `clean --all`. As root that is the package cache and the system
# log of whoever typed `meson test`, which is the same bug SYNCLEAN_TMPDIRS was
# added for, one directory over.
#
# ⚠ Loudly, not as a skip. makepkg refuses to run as root at all, so the build
# never lands here; anyone who does is running it by hand and needs to know why
# it stopped rather than to see a green suite that asserted nothing.
if [ "$(id -u)" = 0 ]; then
    echo "clean_test.sh: refusing to run as root — it would clean this machine" >&2
    exit 1
fi

T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
export SYNCLEAN_HOME="$T/home"

# ⛔ AND THE TWO ROOTS SYNCLEAN_HOME CANNOT REACH. The `tmp` category sweeps
# /tmp and /var/tmp, which are not under $HOME — so `clean --all` below, which
# this suite runs, was deleting the REAL /tmp files of whoever typed
# `meson test`, anything of theirs older than a day. The guarantee at the top
# of this file was true of every path except these two.
export SYNCLEAN_TMPDIRS="$T/tmproot"
mkdir -p "$T/tmproot"

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
# ⛔ AND THE PROOF THAT THE /tmp SEAM HOLDS, checked around the one command
# that used to escape it. An old file outside the fixture must survive `clean
# --all`; before SYNCLEAN_TMPDIRS existed this deleted it, and everything else
# of yours in /tmp with it.
mkdir -p "$T/nottmp"
: > "$T/nottmp/precious"
touch -d '30 days ago' "$T/nottmp/precious" 2>/dev/null || touch "$T/nottmp/precious"
: > "$T/tmproot/sweepable"
touch -d '30 days ago' "$T/tmproot/sweepable" 2>/dev/null || true

"$S" --rec --yes clean --all >/dev/null 2>&1
[ -f "$T/nottmp/precious" ]
chk "⛔ --all sweeps only \$SYNCLEAN_TMPDIRS, not the real /tmp" $?
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

# ── ⛔ WHAT WENT, NOT WHAT WAS FOUND ────────────────────────────────────────
#
# The count used to be added before the unlink was attempted, so a tree this
# user cannot write was reported as freed with every byte of it still on the
# disk: 13.1 GB of root-owned rootfs trees sitting in a trash folder, "Freed
# 13.1 GB", and nothing but the exit status saying otherwise.
#
# A directory with no write permission is the same refusal as somebody else's
# files, reachable without root.
mkdir -p "$SYNCLEAN_HOME/.local/share/Trash/files/locked"
mk "$SYNCLEAN_HOME/.local/share/Trash/files/locked/stuck" 30000
mk "$SYNCLEAN_HOME/.local/share/Trash/files/loose"        10000
mkdir -p "$SYNCLEAN_HOME/.local/share/Trash/info"
: > "$SYNCLEAN_HOME/.local/share/Trash/info/loose.trashinfo"
chmod 0500 "$SYNCLEAN_HOME/.local/share/Trash/files/locked"

out=$("$S" --yes clean trash 2>&1)
chmod 0700 "$SYNCLEAN_HOME/.local/share/Trash/files/locked"
[ -f "$SYNCLEAN_HOME/.local/share/Trash/files/locked/stuck" ]
chk "a file that refuses to go stays" $?
grep -qE "Freed (0 B|1[0-9]\.[0-9] KB)" <<<"$out"
chk "…and is not counted as freed" $?
grep -qi "could not be removed" <<<"$out"
chk "…and the refusal is said out loud" $?

# ⛔ AND THE RESTORE RECORDS OUTLIVE IT. Trash/info holds one .trashinfo per
# item — the original path, which is the whole of what "restore" means. Emptying
# it while Trash/files refused leaves the space still used AND nothing
# restorable, which is how a real trash folder came to hold 13 GB that no
# longer belonged to anything.
[ -f "$SYNCLEAN_HOME/.local/share/Trash/info/loose.trashinfo" ]
chk "…and the restore records are kept when the files would not go" $?

# ⚠ AND THE TRASH IS EMPTIED, NOT REMOVED. The desktop puts the next deleted
# file straight into this directory.
[ -d "$SYNCLEAN_HOME/.local/share/Trash/files" ]
chk "the trash directory itself survives being emptied" $?

rm -rf "$SYNCLEAN_HOME/.local/share/Trash"

# ── ⛔ THE ROOT ROWS ARE A QUESTION ABOUT THE PROCESS ───────────────────────
#
# `needs_root` was read as "this can never be done": `sudo syn-clean clean
# pkgcache` answered "needs root: run it with sudo" to somebody who had. All
# three root categories were unreachable by any command line while the scan
# offered their bytes in the total at the bottom.
out=$("$S" --yes clean pkgcache 2>&1)
grep -qi "needs root" <<<"$out"
chk "without root, a root category says which command does it" $?

out=$("$S" --yes --dry-run clean --all 2>&1)
grep -q "pkgcache" <<<"$out"
chk "…and --all names the rows it skipped rather than going quiet" $?

# ⛔ AND THE TOTAL SAYS HOW MUCH OF ITSELF IS OUT OF REACH. 44.4 GB could be
# freed, of which 27 GB needed a sudo the line never mentioned.
root_bytes=$("$S" --rec scan 2>/dev/null |
             awk -F'\t' '$6=="1" {s+=$4} END{print s+0}')
if [ "$root_bytes" -gt 0 ]; then
    "$S" scan 2>/dev/null | grep -qi "needs sudo"
    chk "the scan total says how much of it needs sudo" $?
else
    printf '  skip  no root-owned bytes on this machine to report\n'
fi

# ── ⛔ AND WITH ROOT IT DOES THEM ───────────────────────────────────────────
#
# ⚠ In a user namespace, which is geteuid() == 0 without being able to harm
# anything outside this fixture. $SYNCLEAN_PKGCACHE is the seam that keeps the
# real /var/cache/pacman/pkg out of reach — the same shape as SYNCLEAN_HOME,
# and added for the same reason the moment this category stopped refusing.
if unshare -r true 2>/dev/null; then
    mkdir -p "$T/pkgcache"
    mk "$T/pkgcache/some-1.0-1-x86_64.pkg.tar.zst" 50000
    out=$(unshare -r env SYNCLEAN_PKGCACHE="$T/pkgcache" \
              SYNCLEAN_HOME="$SYNCLEAN_HOME" SYNCLEAN_TMPDIRS="$SYNCLEAN_TMPDIRS" \
              LC_ALL=C.UTF-8 "$S" --yes clean pkgcache 2>&1)
    grep -qi "needs root" <<<"$out"
    [ $? != 0 ]
    chk "⛔ as root the package cache is not refused" $?
    [ ! -f "$T/pkgcache/some-1.0-1-x86_64.pkg.tar.zst" ]
    chk "…and the packages actually go" $?
    # ⚠ pacman owns this directory; emptying it is the job, removing it is a
    # surprise for the next download.
    [ -d "$T/pkgcache" ]
    chk "…while the cache directory itself stays" $?
else
    printf '  skip  no unprivileged user namespaces (the root half is unasserted)\n'
fi

# ── ⛔ ORPHANS ARE UNINSTALLED, AND THAT IS NOT A SWEEP ─────────────────────
#
# The row counted them and the clean did nothing at all — roots_for() has no
# entry for this id, so `clean orphans` walked an empty list and printed
# "Freed 0 B" over packages that were still installed.
#
# ⚠ AGAINST A FAKE pacman ON PATH. The real one would uninstall software from
# the machine running the suite, and what is being asserted is this program's
# half: that it asks, that it reports, and that a failure is not reported as
# success. Prepending PATH substitutes a binary; it cannot hide one, which is
# why nothing below tests the absent case.
mkdir -p "$T/bin"
cat > "$T/bin/pacman" <<'FAKE'
#!/bin/sh
case "$*" in
    *-Qtdq*) printf 'libfoo\nlibbar\n' ;;
    *-Rns*)  exit "${FAKE_PACMAN_RC:-0}" ;;
esac
FAKE
chmod +x "$T/bin/pacman"

n=$(PATH="$T/bin:$PATH" "$S" --rec scan orphans 2>/dev/null |
    awk -F'\t' '$1=="orphans"{print $5}')
same "the orphan row counts what pacman lists" "2" "$n"

# ⚠ AND THE REST OF IT NEEDS root, because this category does. Same namespace
# as above: geteuid() == 0, and a fake pacman so nothing real is uninstalled.
if unshare -r true 2>/dev/null; then
    ns() { unshare -r env PATH="$T/bin:$PATH" SYNCLEAN_HOME="$SYNCLEAN_HOME" \
               SYNCLEAN_TMPDIRS="$SYNCLEAN_TMPDIRS" SYNCLEAN_PKGCACHE="$T/pkgcache" \
               LC_ALL=C.UTF-8 "$@"; }

    out=$(ns "$S" --yes --dry-run clean orphans 2>&1)
    grep -qi "would remove 2" <<<"$out"
    chk "a dry run says how many it would uninstall" $?

    out=$(ns "$S" --yes clean orphans 2>&1)
    grep -qi "removed 2" <<<"$out"
    chk "clean orphans says what it uninstalled" $?

    out=$(ns env FAKE_PACMAN_RC=1 "$S" --yes clean orphans 2>&1)
    grep -qi "could not remove" <<<"$out"
    chk "⛔ a pacman that failed is not reported as a removal" $?

    # ⛔ AND `--all` NEVER TAKES IT, root or not. Every other category deletes
    # files that come back on their own; this one uninstalls software, which is
    # chosen by name for the same reason cookies are.
    out=$(ns "$S" --yes --dry-run clean --all 2>&1)
    grep -qi "orphan" <<<"$out"
    [ $? != 0 ]
    chk "--all does NOT uninstall packages, even with root" $?
else
    printf '  skip  no unprivileged user namespaces (the orphan half is unasserted)\n'
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
