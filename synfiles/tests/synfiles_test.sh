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
[ "$n" = 8 ] && ok "volumes --rec has 8 columns" || bad "volumes --rec has $n columns"

ragged=$("$SYNFILES" --rec volumes | awk -F'\t' 'NR==1 {w=NF; next} NF!=w {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in volumes --rec" || bad "$ragged ragged volume rows"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
