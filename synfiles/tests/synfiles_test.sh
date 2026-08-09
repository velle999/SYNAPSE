#!/usr/bin/env bash
#
# synfiles_test.sh — checks against a scratch tree, never the user's files.
#
# Everything destructive happens inside a mktemp -d that the EXIT trap removes.
# Nothing here writes to $HOME, and the two commands that CAN modify a real
# file — places pin/unpin — are pointed at a fixture through SYNFILES_PLACES.
# A file manager's test suite that could touch real data would be the single
# most dangerous file in this repository.
#
# The bulk of these tests are about ONE property: a filename is arbitrary
# bytes, and every one of those bytes has to survive the trip to the GUI and
# back. That is not a hypothetical — a tab in a filename silently shifts every
# column of a naive TSV row, and a newline splits one file into two rows.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

SYNFILES=${1:-./build/synfiles}
[ -x "$SYNFILES" ] || { echo "not executable: $SYNFILES" >&2; exit 1; }
SYNFILES=$(readlink -f "$SYNFILES")

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# `((n++))` returns the OLD value, so a bare post-increment exits 1 the first
# time and kills the script under set -e. Hence $((n + 1)) above.

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

echo "synfiles tests — $SYNFILES"

# ── the binary answers ──────────────────────────────────────────────────────
"$SYNFILES" --version | grep -q '^synfiles '
check "--version prints a version" $?

"$SYNFILES" --help | grep -q 'the SynapseOS file browser'
check "--help prints usage" $?

"$SYNFILES" not-a-command >/dev/null 2>&1
[ $? -eq 2 ] && ok "unknown command exits 2" || bad "unknown command exits 2"

# ── the record format vs. real filenames ────────────────────────────────────
D="$T/names"
mkdir -p "$D"
touch "$D/plain.txt"
touch "$D/with space.txt"
touch "$D/tab	inside.txt"
touch "$D/quote'and\"dq.txt"
touch "$D/$(printf 'newline\nhere.txt')"
touch "$D/$(printf 'invalid\xff\xfeutf8.txt')"
touch "$D/percent%20literal.txt"
mkdir "$D/a dir"

# 8 entries + 1 header. A newline inside a filename is the case that breaks
# this: an unescaped one arrives as an extra record and the count goes up.
n=$("$SYNFILES" --rec list "$D" | wc -l)
[ "$n" = 9 ] && ok "one record per file, newlines included" \
             || bad "expected 9 lines, got $n (a newline in a name split a row?)"

# A tab inside a filename shifts every later column of a naive TSV row, and
# the damage is invisible — the row still parses, it just describes something
# else. Every record must have exactly 8 fields.
widths=$("$SYNFILES" --rec list "$D" | awk -F'\t' '{print NF}' | sort -u | tr '\n' ' ')
[ "$widths" = "8 " ] && ok "every record has 8 fields" \
                     || bad "field counts seen: $widths (a tab in a name shifted columns?)"

# Nothing may reach stdout that is not a record.
stray=$("$SYNFILES" --rec list "$D" | grep -cv $'\t')
[ "$stray" = 0 ] && ok "--rec writes only records to stdout" \
                 || bad "$stray non-record lines on stdout"

# The specific escapes, by name.
"$SYNFILES" --rec list "$D" | grep -q '^tab%09inside\.txt	'
check "a tab in a filename is escaped" $?

"$SYNFILES" --rec list "$D" | grep -q '^newline%0Ahere\.txt	'
check "a newline in a filename is escaped" $?

"$SYNFILES" --rec list "$D" | grep -q '^invalid%FF%FEutf8\.txt	'
check "invalid UTF-8 bytes survive as escapes" $?

"$SYNFILES" --rec list "$D" | grep -q '^with%20space\.txt	'
check "a space in a filename is escaped" $?

# THE round trip. A literal "%20" in a filename must encode to "%2520", or
# decoding gives back a SPACE and every action targets a different file — the
# failure that a naive encoder passes every other test while committing.
"$SYNFILES" --rec list "$D" | grep -q '^percent%2520literal\.txt	'
check "a literal %20 in a filename double-encodes" $?

# And the decoded form must name a file that actually exists. This is the test
# that would catch an encoder and decoder that are wrong in the same direction.
missing=0
while IFS=$'\t' read -r enc _; do
    dec=$(printf '%b' "${enc//%/\\x}")
    [ -e "$D/$dec" ] || missing=$((missing + 1))
done < <("$SYNFILES" --rec list "$D" | tail -n +2)
[ "$missing" = 0 ] && ok "every encoded name decodes back to a real file" \
                   || bad "$missing names did not decode to an existing file"

# ── listing behaviour ───────────────────────────────────────────────────────
touch "$D/.hidden"
n=$("$SYNFILES" --rec list "$D" | wc -l)
[ "$n" = 9 ] && ok "dotfiles are hidden by default" || bad "dotfile leaked into the listing"
n=$("$SYNFILES" --rec list --all "$D" | wc -l)
[ "$n" = 10 ] && ok "--all shows dotfiles" || bad "--all did not show the dotfile (got $n)"

# Directories float to the top and STAY there when the sort reverses — a
# reversed listing that buries the folders is not what "sort descending" asked
# for, and it is the easy bug to write.
first=$("$SYNFILES" --rec list "$D" | awk -F'\t' 'NR==2 {print $2}')
[ "$first" = "dir" ] && ok "directories sort first" || bad "first row is '$first', want dir"
first=$("$SYNFILES" --rec list --reverse "$D" | awk -F'\t' 'NR==2 {print $2}')
[ "$first" = "dir" ] && ok "directories stay first when reversed" \
                     || bad "reversed listing starts with '$first', want dir"

"$SYNFILES" --rec list --sort=nonsense "$D" >/dev/null 2>&1
[ $? -eq 1 ] && ok "an unknown sort key is refused" || bad "--sort=nonsense was accepted"

# An empty directory is "nothing to list" (100), not a failure.
mkdir "$T/empty"
"$SYNFILES" --rec list "$T/empty" >/dev/null 2>&1
[ $? -eq 100 ] && ok "an empty directory exits 100" || bad "empty directory did not exit 100"

"$SYNFILES" list "$T/does-not-exist" >/dev/null 2>&1
[ $? -eq 1 ] && ok "a missing directory exits 1" || bad "missing directory did not exit 1"

# ── symlinks ────────────────────────────────────────────────────────────────
L="$T/links"
mkdir -p "$L/target-dir"
echo hi > "$L/target-file"
ln -s target-dir "$L/to-dir"
ln -s target-file "$L/to-file"
ln -s nowhere "$L/dangling"

# A link to a directory must present AS a directory, or it neither opens nor
# sorts like one.
t=$("$SYNFILES" --rec list "$L" | awk -F'\t' '$1=="to-dir" {print $2"/"$6}')
[ "$t" = "dir/1" ] && ok "a symlink to a directory presents as a directory" \
                   || bad "to-dir is '$t', want dir/1"

t=$("$SYNFILES" --rec list "$L" | awk -F'\t' '$1=="to-file" {print $2"/"$6}')
[ "$t" = "file/1" ] && ok "a symlink to a file presents as a file" \
                    || bad "to-file is '$t', want file/1"

# A dangling link must be visible and marked, not silently dropped: it is
# usually the thing the user opened the folder to fix.
t=$("$SYNFILES" --rec list "$L" | awk -F'\t' '$1=="dangling" {print $2}')
[ "$t" = "broken" ] && ok "a dangling symlink is reported broken" \
                    || bad "dangling link is '$t', want broken"

tgt=$("$SYNFILES" --rec list "$L" | awk -F'\t' '$1=="dangling" {print $7}')
[ "$tgt" = "nowhere" ] && ok "a broken link still reports its target" \
                      || bad "broken link target is '$tgt'"

# ── mime detection ──────────────────────────────────────────────────────────
M="$T/mime"
mkdir -p "$M"
touch "$M/a.txt" "$M/b.c" "$M/meson.build" "$M/c.tar.gz" "$M/nothing"

m() { "$SYNFILES" --rec list "$M" | awk -F'\t' -v n="$1" '$1==n {print $8}'; }

[ "$(m a.txt)" = "text/plain" ] && ok "a .txt is text/plain" || bad ".txt is $(m a.txt)"

# .c vs .C is the case-sensitivity test, and it is a real trap rather than a
# pedantic one: update-mime-database emits every glob twice, once flagged "cs"
# and once not, so a matcher that ignores the flag lets the case-INSENSITIVE
# "*.C" swallow every C source file in the tree. Both directions are asserted,
# because only checking .c would pass on a matcher that simply preferred
# whichever entry came first.
touch "$M/upper.C"
[ "$(m b.c)" = "text/x-csrc" ] && ok "a .c is C, not C++" || bad ".c is $(m b.c)"
[ "$(m upper.C)" = "text/x-c++src" ] && ok "a .C is C++, not C" || bad ".C is $(m upper.C)"

# A whole-filename rule beats a pattern — "meson.build" is not just a .build.
[ "$(m meson.build)" = "text/x-meson" ] && ok "a literal filename rule wins" \
                                        || bad "meson.build is $(m meson.build)"

# The LONGEST pattern wins, or a .tar.gz reads as a bare gzip stream and opens
# in the wrong application.
[ "$(m c.tar.gz)" = "application/x-compressed-tar" ] && ok "the longest glob wins for .tar.gz" \
                                                     || bad "c.tar.gz is $(m c.tar.gz)"

[ "$(m nothing)" = "application/octet-stream" ] && ok "an unknown extension falls back" \
                                                || bad "nothing is $(m nothing)"

d=$("$SYNFILES" --rec list "$T" | awk -F'\t' '$1=="mime" {print $8}')
[ "$d" = "inode/directory" ] && ok "a directory is inode/directory" || bad "directory is $d"

# ── info ────────────────────────────────────────────────────────────────────
n=$("$SYNFILES" --rec info "$M/a.txt" | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 2 ] && ok "info --rec is two columns" || bad "info --rec has $n columns"

"$SYNFILES" --rec info "$M/a.txt" | grep -q '^mime	text/plain$'
check "info reports the mime type" $?

"$SYNFILES" --rec info "$D/tab	inside.txt" 2>/dev/null | grep -q '^name	tab%09inside.txt$'
check "info percent-encodes the name" $?

"$SYNFILES" info "$T/does-not-exist" >/dev/null 2>&1
[ $? -eq 1 ] && ok "info on a missing path exits 1" || bad "info on a missing path did not exit 1"

# ── places, against a fixture ───────────────────────────────────────────────
# SYNFILES_PLACES keeps every one of these off the real ~/.local/share file.
export SYNFILES_PLACES="$T/places.xbel"
cat > "$SYNFILES_PLACES" <<'XBEL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE xbel>
<xbel xmlns:bookmark="http://www.freedesktop.org/standards/desktop-bookmarks">
 <bookmark href="file:///home/someone/Docs">
  <title>Docs</title>
  <info><metadata owner="http://freedesktop.org">
   <bookmark:icon name="folder-documents"/>
  </metadata></info>
 </bookmark>
 <bookmark href="file:///home/someone/My%20Files">
  <title>My Files</title>
 </bookmark>
 <bookmark href="remote:/">
  <title>Network</title>
 </bookmark>
 <bookmark href="">
  <title>Empty Href</title>
 </bookmark>
 <bookmark href="file:///home/someone/Proj">
  <title>Project Folder</title>
  <info><metadata owner="http://www.kde.org">
   <OnlyInApp>kdenlive</OnlyInApp>
  </metadata></info>
 </bookmark>
 <bookmark href="file:///home/someone/Gone">
  <title>Hidden One</title>
  <info><metadata owner="http://www.kde.org">
   <isHidden>true</isHidden>
  </metadata></info>
 </bookmark>
</xbel>
XBEL

n=$("$SYNFILES" --rec places | tail -n +2 | wc -l)
[ "$n" = 4 ] && ok "places skips empty hrefs and app-scoped entries" \
             || bad "places listed $n rows, want 4"

# An href is ALREADY a URI. Re-encoding it turns %20 into %2520 and the
# sidebar entry stops pointing at the folder it names.
"$SYNFILES" --rec places | grep -q '^/home/someone/My%20Files	path	'
check "an href is not double-encoded" $?

"$SYNFILES" --rec places | grep -q '^remote:/	remote	'
check "a non-file scheme keeps its scheme and is labelled" $?

"$SYNFILES" --rec places | grep -q 'folder-documents'
check "places carries the bookmark icon" $?

# isHidden entries still travel — the front-end decides. Dropping them here
# would make them unrecoverable from the GUI.
"$SYNFILES" --rec places | grep -q 'Hidden One'
check "a hidden place is still emitted, flagged" $?

# pin/unpin round trip, on the fixture.
mkdir -p "$T/pinme"
"$SYNFILES" places pin "$T/pinme" "Pin Me" >/dev/null 2>&1
"$SYNFILES" --rec places | grep -q 'Pin Me'
check "pin adds a bookmark" $?

# The other bookmarks must survive a write. A regenerating writer would drop
# the icon metadata it does not understand.
"$SYNFILES" --rec places | grep -q 'folder-documents'
check "pin preserves the bookmarks it did not touch" $?

"$SYNFILES" places unpin "$T/pinme" >/dev/null 2>&1
"$SYNFILES" --rec places | grep -q 'Pin Me' && bad "unpin left the bookmark behind" \
                                            || ok "unpin removes the bookmark"

n=$("$SYNFILES" --rec places | tail -n +2 | wc -l)
[ "$n" = 4 ] && ok "unpin leaves the other bookmarks intact" \
             || bad "after unpin there are $n rows, want 4"

# A backup is taken before the first modification of a file synfiles did not
# create — this is user data that predates the program.
[ -f "$SYNFILES_PLACES.pre-synfiles" ] && ok "the first write backs the file up" \
                                       || bad "no .pre-synfiles backup was written"
unset SYNFILES_PLACES

# ── recent, against a fixture ───────────────────────────────────────────────
export SYNFILES_RECENT="$T/recent.xbel"
cat > "$SYNFILES_RECENT" <<'XBEL'
<?xml version="1.0" encoding="UTF-8"?>
<xbel version="1.0">
 <bookmark href="file:///tmp/older.txt" added="2020-01-01T00:00:00Z" modified="2020-01-01T00:00:00Z">
  <info><metadata owner="http://freedesktop.org"><mime:mime-type type="text/plain"/></metadata></info>
 </bookmark>
 <bookmark href="file:///tmp/newer.txt" added="2026-01-01T00:00:00Z" modified="2026-01-01T00:00:00Z">
  <info><metadata owner="http://freedesktop.org"><mime:mime-type type="text/plain"/></metadata></info>
 </bookmark>
</xbel>
XBEL

first=$("$SYNFILES" --rec recent | awk -F'\t' 'NR==2 {print $1}')
[ "$first" = "/tmp/newer.txt" ] && ok "recent is newest first" || bad "recent starts with $first"

"$SYNFILES" --rec recent | grep -q '	text/plain	'
check "recent carries the mime type" $?

# Entries whose file is gone are shown and FLAGGED, not dropped: "I know I
# opened it yesterday" is exactly what this view is for.
"$SYNFILES" --rec recent | grep -q '/tmp/older.txt	text/plain	[0-9]*	0'
check "a missing recent file is flagged rather than dropped" $?

n=$("$SYNFILES" --rec recent --limit=1 | tail -n +2 | wc -l)
[ "$n" = 1 ] && ok "--limit caps the row count" || bad "--limit=1 returned $n rows"
unset SYNFILES_RECENT

# ── volumes ─────────────────────────────────────────────────────────────────
# Shape only. What is mounted differs between velle's box, a build chroot and
# the ISO, and a test that asserted a specific drive would be a test that gets
# disabled on the first machine that does not have it.
n=$("$SYNFILES" --rec volumes | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 10 ] && ok "volumes --rec has 10 columns" || bad "volumes --rec has $n columns"

ragged=$("$SYNFILES" --rec volumes | awk -F'\t' 'NR==1 {w=NF; next} NF!=w {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in volumes --rec" || bad "$ragged ragged volume rows"

# ── trash, against a fixture ────────────────────────────────────────────────
# SYNFILES_TRASH is an unconditional override: without it the device check
# would route a scratch file to a volume trash and these tests could touch the
# user's real one. Nothing below may reach ~/.local/share/Trash.
export SYNFILES_TRASH="$T/Trash"
W="$T/work"
mkdir -p "$W"

echo "content one" > "$W/notes.txt"
printf 'payload' > "$W/$(printf 'weird\tname\nhere.txt')"
echo "has-a-percent" > "$W/50%off.txt"
mkdir -p "$W/subdir/deep" && echo deep > "$W/subdir/deep/f.txt"

"$SYNFILES" trash "$W/notes.txt" >/dev/null 2>&1
[ -e "$W/notes.txt" ] && bad "trash left the original in place" \
                      || ok "trash moves the file out of its directory"

# Both halves must exist. A file in files/ with no info/ entry is unrestorable
# litter, and an info/ entry with no file is a phantom row in every trash
# viewer on the system.
[ -f "$SYNFILES_TRASH/files/notes.txt" ] && ok "the data lands in files/" \
                                         || bad "nothing in files/"
[ -f "$SYNFILES_TRASH/info/notes.txt.trashinfo" ] && ok "the info lands in info/" \
                                                  || bad "nothing in info/"

grep -q '^\[Trash Info\]$' "$SYNFILES_TRASH/info/notes.txt.trashinfo"
check "trashinfo has the spec's section header" $?
grep -q "^Path=$W/notes.txt$" "$SYNFILES_TRASH/info/notes.txt.trashinfo"
check "trashinfo records an absolute Path for the home trash" $?
grep -qE '^DeletionDate=[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}$' \
     "$SYNFILES_TRASH/info/notes.txt.trashinfo"
check "trashinfo records a spec-shaped DeletionDate" $?

# Path= is percent-encoded per the spec. Unencoded, a filename containing a
# newline would make the .trashinfo unparseable and the file unrestorable.
"$SYNFILES" trash "$W/$(printf 'weird\tname\nhere.txt')" >/dev/null 2>&1
grep -q 'Path=.*weird%09name%0Ahere\.txt$' "$SYNFILES_TRASH"/info/*.trashinfo
check "trashinfo percent-encodes Path" $?

# Reserving the name with O_EXCL is what makes a second file of the same name
# become notes.txt.2 rather than overwriting the first.
echo "content two" > "$W/notes.txt"
"$SYNFILES" trash "$W/notes.txt" >/dev/null 2>&1
[ -f "$SYNFILES_TRASH/files/notes.txt.2" ] && ok "a second file of the same name gets a suffix" \
                                           || bad "no notes.txt.2 — the first was overwritten?"
grep -q '^content one$' "$SYNFILES_TRASH/files/notes.txt" \
    && ok "the first trashed file is untouched by the second" \
    || bad "trashing a same-named file clobbered the first"

n=$("$SYNFILES" --rec trash list | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 5 ] && ok "trash list --rec has 5 columns" || bad "trash list --rec has $n columns"

# THE round trip, and the reason `restore` decodes its argument: the name in
# the listing is percent-encoded, so handing it straight back has to work.
"$SYNFILES" trash "$W/50%off.txt" >/dev/null 2>&1
enc=$("$SYNFILES" --rec trash list | awk -F'\t' '$2 ~ /50%25off/ {print $1}')
[ "$enc" = "50%25off.txt" ] && ok "a % in a trashed name is double-encoded in the listing" \
                            || bad "listed name is '$enc', want 50%25off.txt"

"$SYNFILES" trash restore "$enc" >/dev/null 2>&1
[ -f "$W/50%off.txt" ] && ok "restore takes the name exactly as listed" \
                       || bad "restore did not put 50%off.txt back"
grep -q '^has-a-percent$' "$W/50%off.txt" 2>/dev/null
check "restored content is intact" $?

# A name with a tab and a newline has to survive the same round trip, and it
# is the case where an encode/decode mismatch actually loses the file.
enc=$("$SYNFILES" --rec trash list | awk -F'\t' '$1 ~ /weird/ {print $1}')
"$SYNFILES" trash restore "$enc" >/dev/null 2>&1
[ -f "$W/$(printf 'weird\tname\nhere.txt')" ] \
    && ok "a name with a tab and a newline survives trash and restore" \
    || bad "the hostile name did not come back"

# Restoring must never overwrite: something newer may occupy the old path.
echo "newer" > "$W/notes.txt"
"$SYNFILES" trash restore notes.txt >/dev/null 2>&1
grep -q '^newer$' "$W/notes.txt"
check "restore refuses to overwrite what is already there" $?

# Directories go to the trash whole.
"$SYNFILES" trash "$W/subdir" >/dev/null 2>&1
[ -f "$SYNFILES_TRASH/files/subdir/deep/f.txt" ] && ok "a directory is trashed with its contents" \
                                                 || bad "the directory tree did not survive trashing"

"$SYNFILES" trash empty >/dev/null 2>&1
[ $? -eq 1 ] && ok "trash empty refuses without --yes" || bad "trash empty ran unconfirmed"

"$SYNFILES" trash empty --yes >/dev/null 2>&1
n=$(ls -A "$SYNFILES_TRASH/files" 2>/dev/null | wc -l)
[ "$n" = 0 ] && ok "trash empty --yes clears files/" || bad "$n items left in files/"
n=$(ls -A "$SYNFILES_TRASH/info" 2>/dev/null | wc -l)
[ "$n" = 0 ] && ok "trash empty --yes clears info/" || bad "$n items left in info/"

unset SYNFILES_TRASH

# ── copy ────────────────────────────────────────────────────────────────────
C="$T/copy"
mkdir -p "$C/src/deep" "$C/dst"
echo alpha > "$C/src/a.txt"
echo beta > "$C/src/deep/b.txt"
chmod 700 "$C/src/a.txt"
ln -s a.txt "$C/src/lnk"
mkfifo "$C/src/afifo"

"$SYNFILES" copy "$C/src" "$C/dst" >/dev/null 2>&1
[ -f "$C/dst/src/deep/b.txt" ] && ok "copy recurses into subdirectories" \
                               || bad "copy did not recurse"

m=$(stat -c '%a' "$C/dst/src/a.txt" 2>/dev/null)
[ "$m" = "700" ] && ok "copy preserves permissions" || bad "copied mode is $m, want 700"

# A symlink must be RECREATED, not chased. Following it turns one 40-byte link
# into a second copy of whatever it points at.
[ -L "$C/dst/src/lnk" ] && ok "copy recreates a symlink as a symlink" \
                        || bad "copy followed the symlink"

# Opening a fifo blocks until somebody writes to it, so a copy that tried
# would hang with no diagnosis.
[ ! -e "$C/dst/src/afifo" ] && ok "copy skips a fifo rather than hanging on it" \
                            || bad "copy created something for the fifo"

# The default conflict policy refuses and says so. Silently overwriting is
# unrecoverable and silently skipping hides work that did not happen.
"$SYNFILES" copy "$C/src" "$C/dst" >/dev/null 2>&1
[ $? -eq 1 ] && ok "the default conflict policy refuses" || bad "a second copy did not refuse"

"$SYNFILES" copy --conflict=skip "$C/src" "$C/dst" >/dev/null 2>&1
[ $? -eq 0 ] && ok "--conflict=skip succeeds" || bad "--conflict=skip returned failure"

"$SYNFILES" copy --conflict=rename "$C/src" "$C/dst" >/dev/null 2>&1
[ -f "$C/dst/src/a (copy).txt" ] && ok "--conflict=rename inserts before the extension" \
                                 || bad "no 'a (copy).txt' was produced"

"$SYNFILES" copy --conflict=nonsense "$C/src" "$C/dst" >/dev/null 2>&1
[ $? -eq 1 ] && ok "an unknown conflict policy is refused" || bad "--conflict=nonsense accepted"

# Copying a directory into its own subtree recurses until the disk fills.
"$SYNFILES" copy "$C/src" "$C/src/deep" >/dev/null 2>&1
[ $? -eq 1 ] && ok "copy refuses a destination inside the source" \
             || bad "copy accepted a destination inside the source"

# ── move ────────────────────────────────────────────────────────────────────
M="$T/move"
mkdir -p "$M/a" "$M/b"
echo moveme > "$M/a/f.txt"
mkdir -p "$M/a/sub" && echo s > "$M/a/sub/g.txt"

"$SYNFILES" move "$M/a/f.txt" "$M/b" >/dev/null 2>&1
{ [ -f "$M/b/f.txt" ] && [ ! -e "$M/a/f.txt" ]; }
check "move relocates a file" $?

"$SYNFILES" move "$M/a/sub" "$M/b" >/dev/null 2>&1
{ [ -f "$M/b/sub/g.txt" ] && [ ! -e "$M/a/sub" ]; }
check "move relocates a directory with its contents" $?

# ── rename ──────────────────────────────────────────────────────────────────
"$SYNFILES" rename "$M/b/f.txt" renamed.txt >/dev/null 2>&1
{ [ -f "$M/b/renamed.txt" ] && [ ! -e "$M/b/f.txt" ]; }
check "rename renames in place" $?

# A "/" in a new name would make rename a silent move — and "../.." a silent
# move somewhere surprising.
"$SYNFILES" rename "$M/b/renamed.txt" ../escaped.txt >/dev/null 2>&1
[ $? -eq 1 ] && ok "rename refuses a path as the new name" || bad "rename accepted a path"

"$SYNFILES" rename "$M/b/renamed.txt" .. >/dev/null 2>&1
[ $? -eq 1 ] && ok "rename refuses '..' as a name" || bad "rename accepted '..'"

echo occupied > "$M/b/taken.txt"
"$SYNFILES" rename "$M/b/renamed.txt" taken.txt >/dev/null 2>&1
grep -q '^occupied$' "$M/b/taken.txt"
check "rename does not overwrite an existing name" $?

# ── permanent delete ────────────────────────────────────────────────────────
# The gate is the whole point: the GUI's Delete key reaches `trash`, and this
# is the one that cannot be undone.
"$SYNFILES" delete "$M/b/taken.txt" >/dev/null 2>&1
{ [ $? -eq 1 ] && [ -f "$M/b/taken.txt" ]; }
check "delete refuses without --yes, and changes nothing" $?

"$SYNFILES" delete --yes "$M/b/taken.txt" >/dev/null 2>&1
[ ! -e "$M/b/taken.txt" ] && ok "delete --yes removes the file" || bad "delete --yes did nothing"

"$SYNFILES" delete --yes / >/dev/null 2>&1
[ $? -eq 1 ] && ok "delete refuses the root directory" || bad "delete did not refuse /"

# A recursive delete must never descend THROUGH a symlink — it unlinks the
# link. Following one into a directory outside the tree would remove files the
# user never selected.
DL="$T/dellink"
mkdir -p "$DL/keep" "$DL/target"
echo precious > "$DL/target/precious.txt"
ln -s "$DL/target" "$DL/keep/pointer"
"$SYNFILES" delete --yes "$DL/keep" >/dev/null 2>&1
[ -f "$DL/target/precious.txt" ] && ok "delete unlinks a symlink without following it" \
                                 || bad "delete followed a symlink and destroyed the target"

# ── mount / unmount ─────────────────────────────────────────────────────────
# Shape and guards only. Actually mounting needs real hardware and a polkit
# prompt, and a test that mounted one of velle's disks would be a test nobody
# could run twice.
"$SYNFILES" mount >/dev/null 2>&1
[ $? -eq 1 ] && ok "mount with no device is an error" || bad "mount accepted no argument"

# A bare name is refused rather than passed through. udisksctl is a mount
# helper, and handing it a caller-supplied string that was never checked is
# where a surprising argument does damage.
"$SYNFILES" mount sdc1 >/dev/null 2>&1
[ $? -eq 1 ] && ok "mount refuses a bare device name" || bad "mount accepted 'sdc1'"

"$SYNFILES" unmount relative/path >/dev/null 2>&1
[ $? -eq 1 ] && ok "unmount refuses a non-/dev path" || bad "unmount accepted a relative path"

# ── the thumbnail URI contract ──────────────────────────────────────────────
# The GUI finds a cached thumbnail at ~/.cache/thumbnails/<size>/<md5 of the
# file's URI>.png, and computes that md5 itself from "file://" + the encoded
# path in the record. That only works if this program's encoding is byte-for-
# byte the one every other implementation hashed: escape everything outside
# the RFC 3986 unreserved set, and leave "/" alone.
#
# If this ever drifts, thumbnails do not break loudly — they just silently
# never hit the cache, which reads as "synfiles has no thumbnails".
U="$T/uri"
mkdir -p "$U"
touch "$U/plain.png" "$U/with space.png" "$U/mixed-CASE_1.9~x.png"

enc() { "$SYNFILES" --rec list "$U" | awk -F'\t' -v n="$1" '$1==n {print $1}'; }

"$SYNFILES" --rec list "$U" | grep -q '^with%20space\.png	'
check "a space becomes %20, as the URI form requires" $?

# Unreserved characters must NOT be escaped, or the hash differs from the one
# GTK and KDE computed for the same file.
"$SYNFILES" --rec list "$U" | grep -q '^mixed-CASE_1\.9~x\.png	'
check "unreserved characters are left alone" $?

# And the full path form keeps its slashes, which is what makes
# "file://" + path a valid URI rather than one long escaped blob.
"$SYNFILES" --rec info "$U/plain.png" | grep -q "^path	$U/plain.png\$"
check "info emits a path with its slashes intact" $?

# ── about ───────────────────────────────────────────────────────────────────
n=$("$SYNFILES" --rec about | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 4 ] && ok "about --rec has 4 columns" || bad "about --rec has $n columns"

ragged=$("$SYNFILES" --rec about | awk -F'\t' 'NR==1 {w=NF; next} NF!=w {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in about --rec" || bad "$ragged ragged about rows"

# The donate link. The GUI decides between "open in a browser" and "run in a
# terminal" purely on this prefix, so the row has to keep it.
"$SYNFILES" --rec about | grep -q '^Support	info	Buy me a coffee	https://'
check "about carries a Support row with an https link" $?

# Every state the front-ends colour must be one they know about.
unknown=$("$SYNFILES" --rec about |
          awk -F'\t' 'NR>1 && $2!="ok" && $2!="off" && $2!="missing" && $2!="info" {n++} END {print n+0}')
[ "$unknown" = 0 ] && ok "about states are all known" || bad "$unknown about rows have an unknown state"

# ── actions: Open With and service menus ────────────────────────────────────
# Against a fixture, not the machine's real menus: what is installed differs
# between velle's box, a build chroot and the ISO, and a test that asserted
# "Extract" exists would be a test that gets disabled on the first machine
# without synui.
# $M belongs to the move tests by this point in the file — these get their own.
AM="$T/actions-files"
mkdir -p "$AM"
touch "$AM/a.txt" "$AM/archive.zip"
A="$T/xdg"
mkdir -p "$A/kio/servicemenus" "$A/applications"

cat > "$A/kio/servicemenus/test-extract.desktop" <<'SM'
[Desktop Entry]
Type=Application
Name=Extract
Icon=application-x-archive
MimeType=application/zip;application/x-tar;
Actions=here;subdir;

[Desktop Action here]
Name=Extract Here
Icon=archive-extract
Exec=fixture-extract here %F

[Desktop Action subdir]
Name=Extract to Subfolder
Exec=fixture-extract subdir %F
SM

cat > "$A/applications/fixture-editor.desktop" <<'DE'
[Desktop Entry]
Type=Application
Name=Fixture Editor
Icon=text-editor
Exec=fixture-editor %F
MimeType=text/plain;
DE

cat > "$A/applications/fixture-hidden.desktop" <<'DE'
[Desktop Entry]
Type=Application
Name=Hidden Helper
NoDisplay=true
Exec=fixture-hidden %F
MimeType=text/plain;
DE

cat > "$A/applications/mimeinfo.cache" <<'MC'
[MIME Cache]
text/plain=fixture-editor.desktop;fixture-hidden.desktop;
MC

export SYNFILES_DATA_DIRS="$A"
export HOME="$T/fakehome"      # so xdg_data_home cannot reach the real one
mkdir -p "$HOME"

n=$("$SYNFILES" --rec actions "$AM/a.txt" | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 5 ] && ok "actions --rec has 5 columns" || bad "actions --rec has $n columns"

"$SYNFILES" --rec actions "$AM/a.txt" | grep -q '^open-with	fixture-editor.desktop		Fixture Editor	'
check "actions lists an Open With candidate" $?

# NoDisplay is how a helper .desktop hides itself from menus; offering it puts
# something in front of the user that nobody meant to be there.
"$SYNFILES" --rec actions "$AM/a.txt" | grep -q 'Hidden Helper' \
    && bad "a NoDisplay application reached the menu" \
    || ok "a NoDisplay application is left out"

# The service menu must match on mime and expand to one row per action.
touch "$AM/archive.zip"
n=$("$SYNFILES" --rec actions "$AM/archive.zip" | grep -c '^service	')
[ "$n" = 2 ] && ok "a service menu expands to one row per action" \
             || bad "expected 2 service rows, got $n"

"$SYNFILES" --rec actions "$AM/archive.zip" | grep -q '^service	test-extract.desktop	here	Extract Here	archive-extract$'
check "a service action carries its id, label and icon" $?

# A .txt is not an archive. Whole-entry mime matching is what keeps Extract off
# every file in the folder.
"$SYNFILES" --rec actions "$AM/a.txt" | grep -q '^service	' \
    && bad "a service menu matched the wrong mime type" \
    || ok "a service menu does not match an unrelated mime type"

# A mixed selection has no single type, so nothing that acts on "the selection"
# can be offered — running Extract over half a folder of photographs is exactly
# the outcome this prevents.
n=$("$SYNFILES" --rec actions "$AM/archive.zip" "$AM/a.txt" | tail -n +2 | wc -l)
[ "$n" = 0 ] && ok "a mixed-type selection offers no actions" \
             || bad "a mixed selection offered $n actions"

# ── running one ─────────────────────────────────────────────────────────────
# The .desktop argument is a NAME. Accepting a path would let a caller point
# this at any file on the system and run whatever Exec= line it found there.
"$SYNFILES" action ../../../etc/passwd here -- "$AM/a.txt" >/dev/null 2>&1
[ $? -eq 1 ] && ok "action refuses a path as the desktop name" \
             || bad "action accepted a path traversal"

"$SYNFILES" action >/dev/null 2>&1
[ $? -eq 1 ] && ok "action with no arguments is an error" || bad "action accepted no arguments"

"$SYNFILES" action no-such-thing.desktop -- "$AM/a.txt" >/dev/null 2>&1
[ $? -eq 1 ] && ok "action refuses an unknown desktop entry" || bad "action accepted a missing entry"

unset SYNFILES_DATA_DIRS

# ── automounts ──────────────────────────────────────────────────────────────
# An autofs target is browsable and must not also be offered as an unmounted
# device — taking that offer would mount the disk a second time under
# /run/media while the automount still owns its own path. Fixture units, since
# the real ones are velle's.
U="$T/units"
mkdir -p "$U"
cat > "$U/fixture.mount" <<'MU'
[Unit]
Description=fixture

[Mount]
What=/dev/disk/by-uuid/FIXTURE-UUID
Where=/mnt/fixture-target
Type=ext4
MU

# Shape only: whether this machine HAS an automount differs everywhere, so the
# assertion is that reading the unit directory changes nothing it should not.
before=$("$SYNFILES" --rec volumes | wc -l)
after=$(SYNFILES_UNIT_DIRS="$U" "$SYNFILES" --rec volumes | wc -l)
[ "$before" = "$after" ] && ok "a unit for an absent device adds no volume rows" \
                         || bad "unit parsing changed the row count ($before -> $after)"

# used/total are numbers the meter divides. A non-numeric cell renders as NaN,
# and 0 total must mean "unknown" rather than "empty" — an untriggered
# automount is deliberately not measured.
bad_nums=$("$SYNFILES" --rec volumes |
           awk -F'\t' 'NR>1 && ($9 !~ /^[0-9]+$/ || $10 !~ /^[0-9]+$/) {n++} END {print n+0}')
[ "$bad_nums" = 0 ] && ok "volume used/total are numeric" \
                    || bad "$bad_nums volume rows have non-numeric usage"

over=$("$SYNFILES" --rec volumes |
       awk -F'\t' 'NR>1 && $10 > 0 && $9 > $10 {n++} END {print n+0}')
[ "$over" = 0 ] && ok "no volume reports more used than it has" \
               || bad "$over volumes report used > total"

# ── undo ────────────────────────────────────────────────────────────────────
# Its own journal and its own trash, so nothing here can reach the real ones.
export SYNFILES_JOURNAL="$T/journal"
export SYNFILES_TRASH="$T/undo-trash"
UW="$T/undo"
mkdir -p "$UW/src" "$UW/dst"
echo one > "$UW/src/f1.txt"
echo two > "$UW/src/f2.txt"

"$SYNFILES" undo >/dev/null 2>&1
[ $? -eq 100 ] && ok "undo with an empty journal exits 100" || bad "empty undo did not exit 100"

# A move of two files is ONE thing the user did, so it undoes as one — not the
# second file now and the first on a second press.
"$SYNFILES" move "$UW/src/f1.txt" "$UW/src/f2.txt" "$UW/dst" >/dev/null 2>&1
"$SYNFILES" undo >/dev/null 2>&1
{ [ -f "$UW/src/f1.txt" ] && [ -f "$UW/src/f2.txt" ] && [ -z "$(ls -A "$UW/dst")" ]; }
check "a multi-file move undoes as one batch" $?

# And is gone from the journal afterwards, so it cannot be undone twice.
"$SYNFILES" undo >/dev/null 2>&1
[ $? -eq 100 ] && ok "an undone batch is forgotten" || bad "the batch could be undone twice"

"$SYNFILES" rename "$UW/src/f1.txt" newname.txt >/dev/null 2>&1
"$SYNFILES" undo >/dev/null 2>&1
{ [ -f "$UW/src/f1.txt" ] && [ ! -e "$UW/src/newname.txt" ]; }
check "undo reverses a rename" $?

"$SYNFILES" trash "$UW/src/f1.txt" >/dev/null 2>&1
"$SYNFILES" undo >/dev/null 2>&1
grep -q '^one$' "$UW/src/f1.txt" 2>/dev/null
check "undo restores from the trash with content intact" $?

"$SYNFILES" mkdir "$UW/made" >/dev/null 2>&1
"$SYNFILES" undo >/dev/null 2>&1
[ ! -d "$UW/made" ] && ok "undo removes a folder it made" || bad "the folder was left behind"

# Only if it is still empty — something may have been put in it since, and
# removing that is not what "undo the folder I just made" means.
"$SYNFILES" mkdir "$UW/kept" >/dev/null 2>&1
echo x > "$UW/kept/something.txt"
"$SYNFILES" undo >/dev/null 2>&1
[ -f "$UW/kept/something.txt" ] && ok "undo leaves a folder that is no longer empty" \
                                || bad "undo removed a folder with a file in it"

# THE one that matters. Undoing a copy must TRASH the copy, never unlink it:
# the copy may have been edited since, and undo must not be a shorter road to
# losing work than deleting is.
"$SYNFILES" copy "$UW/src/f1.txt" "$UW/dst" >/dev/null 2>&1
[ -f "$UW/dst/f1.txt" ] && ok "copy lands where expected" || bad "copy did not produce dst/f1.txt"
"$SYNFILES" undo >/dev/null 2>&1
[ ! -e "$UW/dst/f1.txt" ] && ok "undo removes the copy" || bad "the copy is still there"
"$SYNFILES" --rec trash list | grep -q 'f1.txt'
check "the undone copy went to the TRASH, not to unlink" $?
grep -q '^one$' "$UW/src/f1.txt"
check "undoing a copy leaves the original alone" $?

# Undo VERIFIES. If something now occupies the place a file came from,
# refusing is the only safe answer — overwriting it would destroy a file the
# user created after the operation being undone.
mkdir -p "$UW/a" "$UW/b"
echo original > "$UW/a/c.txt"
"$SYNFILES" move "$UW/a/c.txt" "$UW/b" >/dev/null 2>&1
echo blocker > "$UW/a/c.txt"
"$SYNFILES" undo >/dev/null 2>&1
{ [ $? -eq 1 ] && grep -q '^blocker$' "$UW/a/c.txt"; }
check "undo refuses rather than overwriting what took the old place" $?

# A permanent delete is deliberately NOT journalled: an undo entry that could
# not undo would look like a safety net and not be one.
"$SYNFILES" undo clear >/dev/null 2>&1
echo gone > "$UW/doomed.txt"
"$SYNFILES" delete --yes "$UW/doomed.txt" >/dev/null 2>&1
"$SYNFILES" undo >/dev/null 2>&1
[ $? -eq 100 ] && ok "a permanent delete records nothing to undo" \
               || bad "delete --yes left an undo entry it cannot honour"

# The journal has to survive the same filenames everything else does.
printf 'x' > "$UW/src/$(printf 'j\tk\nl.txt')"
"$SYNFILES" move "$UW/src/$(printf 'j\tk\nl.txt')" "$UW/dst" >/dev/null 2>&1
"$SYNFILES" undo >/dev/null 2>&1
[ -f "$UW/src/$(printf 'j\tk\nl.txt')" ] && ok "undo survives a name with a tab and a newline" \
                                         || bad "the hostile name did not come back"

n=$("$SYNFILES" --rec undo list | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 4 ] && ok "undo list --rec has 4 columns" || bad "undo list --rec has $n columns"

"$SYNFILES" undo clear >/dev/null 2>&1
"$SYNFILES" undo list >/dev/null 2>&1
[ $? -eq 100 ] && ok "undo clear empties the journal" || bad "the journal survived a clear"

unset SYNFILES_JOURNAL SYNFILES_TRASH

# ── find ────────────────────────────────────────────────────────────────────
F="$T/find"
mkdir -p "$F/one/two" "$F/other"
echo "the word needle is here" > "$F/one/haystack.txt"
echo "nothing of note"          > "$F/one/two/deep.txt"
echo "needle again"             > "$F/other/second.txt"
printf 'binary\000needle\000data' > "$F/one/blob.bin"
touch "$F/.hidden-needle.txt"

n=$("$SYNFILES" --rec find "$F" --name=deep | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 9 ] && ok "find --rec has 9 columns" || bad "find --rec has $n columns"

# A bare word means "contains". Making somebody type *deep* would be a quiz
# about fnmatch, not a search box.
"$SYNFILES" --rec find "$F" --name=deep | grep -q '^deep\.txt	'
check "a pattern with no wildcards means contains" $?

# And where it was found, or a result list is unusable — the same filename
# appears in three places in this fixture.
d=$("$SYNFILES" --rec find "$F" --name=deep | awk -F'\t' 'NR==2 {print $9}')
[ "$d" = "one/two" ] && ok "a hit reports the directory it is in" || bad "dir is '$d', want one/two"

"$SYNFILES" --rec find "$F" --name='*.bin' | grep -q '^blob\.bin	'
check "an explicit glob is used as given" $?

# Case-insensitive, because a search box that cares is a search box that finds
# nothing half the time.
"$SYNFILES" --rec find "$F" --name=HAYSTACK | grep -q '^haystack\.txt	'
check "name matching is case-insensitive" $?

n=$("$SYNFILES" --rec find "$F" --content=needle | tail -n +2 | wc -l)
[ "$n" = 2 ] && ok "content search finds both text files" || bad "content search found $n, want 2"

# The NUL heuristic: blob.bin contains "needle" but is binary, and matching it
# would put an unopenable file in the results of a text search.
"$SYNFILES" --rec find "$F" --content=needle | grep -q 'blob\.bin' \
    && bad "content search matched a binary file" \
    || ok "content search skips binaries"

# Both together means BOTH, which is what makes "*.txt containing needle" a
# useful question rather than two unrelated ones.
n=$("$SYNFILES" --rec find "$F" --name='second*' --content=needle | tail -n +2 | wc -l)
[ "$n" = 1 ] && ok "--name and --content are ANDed" || bad "combined search returned $n, want 1"

"$SYNFILES" --rec find "$F" --name=needle | grep -q 'hidden' \
    && bad "a dotfile leaked into search results" \
    || ok "find hides dotfiles by default"
"$SYNFILES" --rec find "$F" --all --name=needle | grep -q 'hidden'
check "--all searches dotfiles" $?

n=$("$SYNFILES" --rec find "$F" --name='*' --limit=2 | tail -n +2 | wc -l)
[ "$n" -le 2 ] && ok "--limit caps the results" || bad "--limit=2 returned $n"

"$SYNFILES" find "$F" --name=nothing-matches-this >/dev/null 2>&1
[ $? -eq 100 ] && ok "find with no matches exits 100" || bad "empty find did not exit 100"

"$SYNFILES" find "$F" >/dev/null 2>&1
[ $? -eq 1 ] && ok "find with no pattern is an error" || bad "find ran with no pattern"

# THE one that would hang. A symlink pointing at its own ancestor is a loop,
# and one pointing at / turns a search of a project folder into a search of the
# whole machine. Links are reported but never descended through.
ln -s "$F" "$F/one/loop"
ln -s / "$F/one/toroot"
timeout 20 "$SYNFILES" --rec find "$F" --name=deep >/dev/null 2>&1
rc=$?
[ "$rc" != 124 ] && ok "a symlink loop does not hang the search" \
                 || bad "the search hung on a symlink loop"

# Exactly one hit, not one per trip around the loop.
n=$(timeout 20 "$SYNFILES" --rec find "$F" --name=deep.txt | tail -n +2 | wc -l)
[ "$n" = 1 ] && ok "a symlink loop yields no duplicate hits" || bad "got $n hits for one file"

# ── compress ────────────────────────────────────────────────────────────────
export SYNFILES_JOURNAL="$T/cjournal"
export SYNFILES_TRASH="$T/ctrash"
CZ="$T/comp"
mkdir -p "$CZ/proj/sub" "$CZ/elsewhere"
echo alpha > "$CZ/proj/a.txt"
echo beta  > "$CZ/proj/sub/b.txt"
echo gamma > "$CZ/elsewhere/c.txt"

"$SYNFILES" compress "$CZ/proj" >/dev/null 2>&1
[ -f "$CZ/proj.tar.gz" ] && ok "compress defaults to tar.gz beside the input" \
                         || bad "no proj.tar.gz was produced"

# THE thing that makes an archive safe to unpack anywhere: relative paths. An
# archive full of absolute paths or "../" scatters files across the filesystem
# on extraction, which is the tarbomb problem from the other end.
tar tzf "$CZ/proj.tar.gz" 2>/dev/null | grep -qE '^(/|\.\./)' \
    && bad "the archive contains absolute or parent-relative paths" \
    || ok "archive paths are relative to the containing folder"
tar tzf "$CZ/proj.tar.gz" 2>/dev/null | grep -q '^proj/sub/b.txt$'
check "compress recurses into subdirectories" $?

# Not overwriting matters here specifically: "compress" is not a command
# anybody expects to destroy an existing backup.
"$SYNFILES" compress "$CZ/proj" >/dev/null 2>&1
[ $? -eq 1 ] && ok "compress refuses to overwrite an existing archive" \
             || bad "compress overwrote an existing archive"

# Inputs from different folders would force absolute paths into the archive.
"$SYNFILES" compress "$CZ/proj/a.txt" "$CZ/elsewhere/c.txt" >/dev/null 2>&1
[ $? -eq 1 ] && ok "compress refuses inputs from different folders" \
             || bad "compress accepted inputs with no common parent"

"$SYNFILES" compress --format=nonsense "$CZ/proj" >/dev/null 2>&1
[ $? -eq 1 ] && ok "an unknown format is refused" || bad "--format=nonsense was accepted"

# --name is a NAME. A path would put the archive somewhere the user did not
# choose while the tool still ran in the input's folder.
"$SYNFILES" compress --name=../escape "$CZ/proj" >/dev/null 2>&1
[ $? -eq 1 ] && ok "--name refuses a path" || bad "--name accepted a path"

"$SYNFILES" compress --name=backup "$CZ/proj" >/dev/null 2>&1
[ -f "$CZ/backup.tar.gz" ] && ok "--name is used, with the extension added" \
                           || bad "backup.tar.gz was not produced"

# And not added twice.
"$SYNFILES" compress --name=twice.tar.gz "$CZ/proj" >/dev/null 2>&1
[ -f "$CZ/twice.tar.gz" ] && ok "--name keeps an extension it already has" \
                          || bad "the extension was doubled"

# zip falls back to 7z where zip is not installed, rather than telling somebody
# to install a second tool for a format the one they have can write.
if command -v zip >/dev/null 2>&1 || command -v 7z >/dev/null 2>&1; then
    "$SYNFILES" compress --format=zip --name=z "$CZ/proj" >/dev/null 2>&1
    [ -f "$CZ/z.zip" ] && ok "zip is produced by whichever tool is present" \
                       || bad "no z.zip was produced"
else
    ok "zip is produced by whichever tool is present (skipped: no zip or 7z)"
fi

# An archive is journalled, so the undo that follows a mistaken compress
# trashes it rather than leaving clutter.
"$SYNFILES" undo >/dev/null 2>&1
[ ! -e "$CZ/z.zip" ] && ok "undo removes a created archive" || bad "the archive survived undo"
"$SYNFILES" --rec trash list | grep -q 'z.zip'
check "the undone archive went to the trash" $?

unset SYNFILES_JOURNAL SYNFILES_TRASH

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
