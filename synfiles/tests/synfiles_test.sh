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
# else. Every record must have exactly 9 fields (the 9th is a .desktop
# launcher's own Icon=, empty for everything else).
widths=$("$SYNFILES" --rec list "$D" | awk -F'\t' '{print NF}' | sort -u | tr '\n' ' ')
[ "$widths" = "9 " ] && ok "every record has 9 fields" \
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

# ── du — what a folder actually holds ───────────────────────────────────────
#
# `info` answers with st_size, which for a directory is the size of the
# directory ENTRY: the SYNAPSE tree, ISO and all, reported 890 bytes. This is
# the command that answers the question a properties pane is really asking.
#
# Checked against coreutils, because that is the number anyone would sanity
# check against, and the two disagreeing is the whole bug.
eq() {   # eq <description> <expected> <actual>
    if [ "$2" = "$3" ]; then ok "$1"
    else bad "$1 — expected [$2], got [$3]"; fi
}

mkdir -p "$T/du/a/b"
head -c 5000 /dev/zero > "$T/du/a/f1"
head -c 3000 /dev/zero > "$T/du/a/b/f2"

n=$("$SYNFILES" --rec du "$T/du" | awk -F'\t' 'NR==1 {print NF}')
eq "du --rec is five columns" "5" "$n"

eq "du's byte total matches coreutils" \
   "$(du -sb "$T/du" | cut -f1)" \
   "$("$SYNFILES" --rec du "$T/du" | awk -F'\t' 'END {print $1}')"

eq "du's disk total matches coreutils" \
   "$(du -s --block-size=1 "$T/du" | cut -f1)" \
   "$("$SYNFILES" --rec du "$T/du" | awk -F'\t' 'END {print $2}')"

eq "du counts the files" "2" \
   "$("$SYNFILES" --rec du "$T/du" | awk -F'\t' 'END {print $3}')"

# A HARD LINK IS ONE FILE. A tree with a 4 GB file linked into it twice does
# not hold 8 GB, and a pacman cache or any backup tree is full of them. The
# total must not move when a second name for the same inode appears.
before=$("$SYNFILES" --rec du "$T/du" | awk -F'\t' 'END {print $1}')
ln "$T/du/a/f1" "$T/du/a/b/f1-again"
after=$("$SYNFILES" --rec du "$T/du" | awk -F'\t' 'END {print $1}')
eq "a hard link does not change the total" "$before" "$after"
eq "…and coreutils agrees" "$(du -sb "$T/du" | cut -f1)" "$after"

# The last record says it is the last, so a reader wanting only the answer
# waits for done=1 rather than guessing which row was final.
eq "the final record is marked done" "1" \
   "$("$SYNFILES" --rec du "$T/du" | awk -F'\t' 'END {print $5}')"

# A plain file answers too, so a caller need not know which it has.
eq "du on a file reports its size" "5000" \
   "$("$SYNFILES" --rec du "$T/du/a/f1" | awk -F'\t' 'END {print $1}')"

"$SYNFILES" du "$T/does-not-exist" >/dev/null 2>&1
[ $? -eq 1 ] && ok "du on a missing path exits 1" || bad "du on a missing path did not exit 1"

# ── resolution ──────────────────────────────────────────────────────────────
#
# Every fixture here is a HEADER, written by hand, with no pixel data behind
# it. That is deliberate: the parsers are only ever allowed to read a bounded
# prefix, so a file that is nothing but its header is the strictest version of
# the input they will meet, and any parser that wandered further would fail
# these instead of quietly reading a neighbouring file's bytes.
R="$T/res"
mkdir -p "$R"

res() { "$SYNFILES" --rec info "$1" | awk -F'\t' '$1=="resolution" {print $2}'; }

# PNG: IHDR is mandatory and first, so its offset is fixed. 300x200.
printf '\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR\x00\x00\x01\x2c\x00\x00\x00\xc8' > "$R/a.png"
[ "$(res "$R/a.png")" = "300x200" ] && ok "info reads a PNG's resolution" \
                                    || bad "PNG is '$(res "$R/a.png")', want 300x200"

# GIF: little-endian, unlike almost everything else here.
printf 'GIF89a\xc8\x00\x64\x00' > "$R/a.gif"
[ "$(res "$R/a.gif")" = "200x100" ] && ok "info reads a GIF's resolution" \
                                    || bad "GIF is '$(res "$R/a.gif")'"

# JPEG has to WALK: the frame header sits behind however many APP segments the
# camera wrote, so this one puts a JFIF segment in front of the SOF0 to prove
# the walk skips by declared length rather than scanning for 0xFFC0.
printf '\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00' > "$R/a.jpg"
printf '\xff\xc0\x00\x11\x08\x01\x90\x02\x8a\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01' >> "$R/a.jpg"
[ "$(res "$R/a.jpg")" = "650x400" ] && ok "info walks a JPEG's segments to the frame header" \
                                    || bad "JPEG is '$(res "$R/a.jpg")', want 650x400"

# A NEGATIVE BMP height is legal and means top-down rows, not a negative image.
printf 'BM\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$R/a.bmp"
printf '\x28\x00\x00\x00\x40\x00\x00\x00\xe0\xff\xff\xff' >> "$R/a.bmp"
[ "$(res "$R/a.bmp")" = "64x32" ] && ok "a top-down BMP's negative height reads as 32" \
                                 || bad "BMP is '$(res "$R/a.bmp")', want 64x32"

# WebP lossless packs both dimensions, minus one, into 28 bits.
printf 'RIFF\x18\x00\x00\x00WEBPVP8L\x0c\x00\x00\x00\x2f\x1f\xc0\x03\x00' > "$R/a.webp"
[ "$(res "$R/a.webp")" = "32x16" ] && ok "info reads a lossless WebP's resolution" \
                                   || bad "WebP is '$(res "$R/a.webp")'"

# TIFF in BIG-endian byte order, which is the half a reader that assumes
# little-endian still parses without ever looking wrong.
printf 'MM\x00\x2a\x00\x00\x00\x08\x00\x02' > "$R/a.tif"
printf '\x01\x00\x00\x03\x00\x00\x00\x01\x02\x80\x00\x00' >> "$R/a.tif"
printf '\x01\x01\x00\x03\x00\x00\x00\x01\x01\xe0\x00\x00\x00\x00\x00\x00' >> "$R/a.tif"
[ "$(res "$R/a.tif")" = "640x480" ] && ok "info reads a big-endian TIFF's resolution" \
                                    || bad "TIFF is '$(res "$R/a.tif")', want 640x480"

# An ICO stores 256 as a zero, because the field is one byte wide.
printf '\x00\x00\x01\x00\x01\x00\x00\x00\x01\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$R/a.ico"
[ "$(res "$R/a.ico")" = "256x256" ] && ok "an ICO's zero dimension means 256" \
                                    || bad "ICO is '$(res "$R/a.ico")'"

# THE RULE THIS ALL RESTS ON: the format is decided by magic, not by the name.
# Both directions, because only checking one would pass on a reader that had
# quietly gone back to trusting the extension.
cp "$R/a.png" "$R/photo-of-a-cat.txt"
[ "$(res "$R/photo-of-a-cat.txt")" = "300x200" ] \
    && ok "a PNG named .txt still reports its resolution" \
    || bad "a PNG named .txt reported '$(res "$R/photo-of-a-cat.txt")'"

printf 'This is a text file that somebody named .png\n' > "$R/liar.png"
[ -z "$(res "$R/liar.png")" ] && ok "a text file named .png reports no resolution" \
                             || bad "a text file named .png reported '$(res "$R/liar.png")'"

# Nothing else grows a resolution row: a directory has none, and a truncated
# image must be answered or refused, never hang.
[ -z "$(res "$R")" ] && ok "a directory has no resolution" || bad "a directory reported one"

head -c 6 "$R/a.png" > "$R/trunc.png"
[ -z "$(timeout 5 "$SYNFILES" --rec info "$R/trunc.png" | awk -F'\t' '$1=="resolution"')" ] \
    && ok "a truncated image reports no resolution" \
    || bad "a truncated image reported one"

# The listing must NEVER pay for this. Opening 4000 files to draw 4000 rows is
# the whole reason mime.c matches globs instead of sniffing, so the column
# count is asserted here as well as in the field-count test above.
c=$("$SYNFILES" --rec list "$R" | awk -F'\t' 'NR==2 {print NF}')
[ "$c" = 9 ] && ok "list gained no resolution column" || bad "list rows now have $c fields"

# ── video: delegated, and only for video ────────────────────────────────────
# A fake ffprobe on PATH, the same shape as the fake lsblk further down: the
# real one's answer depends on what codecs are installed, and this has to
# assert the delegation, not ffmpeg.
mkdir -p "$T/fakebin"
cat > "$T/fakebin/ffprobe" <<'FFP'
#!/bin/sh
echo "$@" >> "$FFPROBE_CALLS"
echo "1280,720"
FFP
chmod +x "$T/fakebin/ffprobe"
export FFPROBE_CALLS="$T/ffprobe-calls"
: > "$FFPROBE_CALLS"

printf 'not really a matroska file\n' > "$R/clip.mkv"
r=$(PATH="$T/fakebin:$PATH" res "$R/clip.mkv")
[ "$r" = "1280x720" ] && ok "a video's resolution is delegated to ffprobe" \
                      || bad "delegated video resolution is '$r'"

# ...and ONLY for video. Forking a subprocess to open the properties of a text
# file is the cost this guard exists to refuse.
: > "$FFPROBE_CALLS"
PATH="$T/fakebin:$PATH" res "$M/a.txt" >/dev/null
[ ! -s "$FFPROBE_CALLS" ] && ok "ffprobe is not run for a text file" \
                          || bad "ffprobe was run for a text file"

# No ffprobe, no row — never a wrong one, and never an error.
r=$(PATH="$T/nonexistent-bin" "$SYNFILES" --rec info "$R/clip.mkv" \
    | awk -F'\t' '$1=="resolution" {print $2}')
[ -z "$r" ] && ok "without ffprobe a video simply has no resolution row" \
            || bad "without ffprobe the video reported '$r'"

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

# ⚠ A folder whose CHILD is pinned is not itself pinned.
#
# The existence check was a bare substring search over the whole file, and
# "file:///home/velle" is a substring of "file:///home/velle/Desktop" — so
# pinning Home reported "already pinned" and did nothing, for as long as
# anything under Home was in the list. The Pin button looked dead. It is the
# closing quote of the href attribute that makes the match exact, which unpin
# had and pin did not.
mkdir -p "$T/ancestor/inside"
"$SYNFILES" places pin "$T/ancestor/inside" >/dev/null 2>&1
"$SYNFILES" places pin "$T/ancestor" >/dev/null 2>&1
check "an ancestor of a pinned folder can still be pinned" $?
"$SYNFILES" --rec places | awk -F'\t' -v p="$T/ancestor" '$1 == p {found=1} END {exit !found}'
check "…and it lands in the list" $?

# The real duplicate still has to be refused, or Pin would grow a second row
# every time it was pressed.
"$SYNFILES" places pin "$T/ancestor" >/dev/null 2>&1
dupes=$("$SYNFILES" --rec places | awk -F'\t' -v p="$T/ancestor" '$1 == p' | wc -l)
[ "$dupes" = 1 ] && ok "pinning the same folder twice does not duplicate it" \
                 || bad "$dupes rows for one folder after pinning twice"

# And unpinning the ancestor must not take the child with it.
"$SYNFILES" places unpin "$T/ancestor" >/dev/null 2>&1
"$SYNFILES" --rec places | awk -F'\t' -v p="$T/ancestor/inside" '$1 == p {found=1} END {exit !found}'
check "unpinning a folder leaves the one inside it alone" $?
"$SYNFILES" places unpin "$T/ancestor/inside" >/dev/null 2>&1

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

# Which mount points survive the "boring" filter, against a FAKE lsblk — the
# one part of this that must not depend on what is plugged into the machine.
#
# /run/media/<user>/<label> is where udisks2 puts removable media, so a /run
# prefix match hid every USB stick the instant it was mounted. That did not
# read as a missing row: the drive was there, you clicked it, synfiles mounted
# it, and the row VANISHED — mounting was indistinguishable from ejecting.
B="$T/fakebin"
mkdir -p "$B"
cat > "$B/lsblk" <<'LB'
#!/usr/bin/env bash
cat <<'ROWS'
NAME="fakeusb1" PATH="/dev/fakeusb1" LABEL="STICKFIX" SIZE="8G" FSTYPE="vfat" MOUNTPOINT="/run/media/tester/STICKFIX" RM="1" TYPE="part" HOTPLUG="1" UUID="1111-1111"
NAME="fakeold1" PATH="/dev/fakeold1" LABEL="OLDSTYLE" SIZE="4G" FSTYPE="vfat" MOUNTPOINT="/media/OLDSTYLE" RM="1" TYPE="part" HOTPLUG="1" UUID="2222-2222"
NAME="fakesys1" PATH="/dev/fakesys1" LABEL="ESPFIX" SIZE="512M" FSTYPE="vfat" MOUNTPOINT="/boot" RM="0" TYPE="part" HOTPLUG="0" UUID="3333-3333"
NAME="fakerun1" PATH="/dev/fakerun1" LABEL="RUNFIX" SIZE="1G" FSTYPE="ext4" MOUNTPOINT="/run/somewhere-else" RM="0" TYPE="part" HOTPLUG="0" UUID="4444-4444"
ROWS
LB
chmod +x "$B/lsblk"

vols=$(PATH="$B:$PATH" "$SYNFILES" --rec volumes)
printf '%s' "$vols" | grep -q '/run/media/tester/STICKFIX' \
  && ok "a udisks mount under /run/media is listed" \
  || bad "a volume mounted at /run/media/... was filtered out"
printf '%s' "$vols" | grep -q '/media/OLDSTYLE' \
  && ok "a mount under /media is listed" || bad "/media was filtered out"
printf '%s' "$vols" | grep -q 'ESPFIX' \
  && bad "/boot is listed as a browsable volume" || ok "/boot is still filtered out"
printf '%s' "$vols" | grep -q 'RUNFIX' \
  && bad "an unrelated /run mount is listed" || ok "/run at large is still filtered out"

# The removable stick keeps its kind, or it lands under the wrong heading and
# is offered "Unmount" where it wants "Eject".
kind=$(printf '%s' "$vols" | awk -F'\t' '$3 == "STICKFIX" {print $2}')
[ "$kind" = "removable" ] && ok "a hotplug volume is kind=removable" \
                          || bad "STICKFIX came back as kind='$kind'"

# ── peek: what a folder has inside it, for its icon ─────────────────────────
# One call answers for every subdirectory, so the assertions are about what
# each subdirectory contributes — and about the bounds, because this runs on
# every navigation.
PK="$T/peek"
mkdir -p "$PK/pics/deeper" "$PK/clips" "$PK/docs" "$PK/empty"
: > "$PK/pics/one.png"; : > "$PK/pics/two.JPG"; : > "$PK/pics/notes.txt"
: > "$PK/pics/deeper/buried.png"
: > "$PK/clips/holiday.mp4"
: > "$PK/docs/manual.pdf"
: > "$PK/loose.png"                      # a file in the root is not a folder
ln -s "$PK/pics" "$PK/link-to-pics"

cols=$("$SYNFILES" --rec peek "$PK" | awk -F'\t' 'NR==1 {print NF}')
[ "$cols" = 3 ] && ok "peek --rec has 3 columns" || bad "peek --rec has $cols columns"

pk=$("$SYNFILES" --rec peek "$PK")
n=$(printf '%s' "$pk" | awk -F'\t' -v d="$PK/pics" 'NR>1 && $1==d' | wc -l)
[ "$n" = 2 ] && ok "peek reports the images in a folder" || bad "pics gave $n rows, want 2"

printf '%s' "$pk" | awk -F'\t' -v d="$PK/pics" 'NR>1 && $1==d {print $2}' | grep -q 'notes.txt' \
  && bad "peek offered a text file as a preview" || ok "peek skips what cannot be previewed"

printf '%s' "$pk" | grep -q 'buried.png' \
  && bad "peek descended a second level" || ok "peek never descends past one level"

printf '%s' "$pk" | awk -F'\t' -v d="$PK/clips" 'NR>1 && $1==d {print $2}' | grep -q 'holiday.mp4' \
  && ok "a video counts as previewable" || bad "peek skipped a video"

printf '%s' "$pk" | grep -q 'manual.pdf' \
  && bad "peek offered a PDF" || ok "peek leaves PDFs alone"

printf '%s' "$pk" | awk -F'\t' -v d="$PK/empty" 'NR>1 && $1==d' | grep -q . \
  && bad "an empty folder produced a row" || ok "an empty folder produces no rows"

printf '%s' "$pk" | grep -q 'loose.png' \
  && bad "peek reported a file sitting in the root" || ok "peek only looks inside subfolders"

# A symlinked directory is not followed — same rule as everywhere else here.
printf '%s' "$pk" | awk -F'\t' -v d="$PK/link-to-pics" 'NR>1 && $1==d' | grep -q . \
  && bad "peek followed a symlinked directory" || ok "peek does not follow a symlinked folder"

one=$("$SYNFILES" --rec peek "$PK" --limit=1 |
      awk -F'\t' -v d="$PK/pics" 'NR>1 && $1==d' | wc -l)
[ "$one" = 1 ] && ok "--limit caps the previews per folder" || bad "--limit=1 gave $one rows"

# The size is what lets the GUI refuse to read a 40MB photo to fill a 20px
# tile, so it has to be a number on every row.
bad_sz=$("$SYNFILES" --rec peek "$PK" |
         awk -F'\t' 'NR>1 && $3 !~ /^[0-9]+$/ {n++} END {print n+0}')
[ "$bad_sz" = 0 ] && ok "peek sizes are numeric" || bad "$bad_sz peek rows have no size"

# Filenames are bytes here too: the row is the identity, so it stays encoded.
mkdir -p "$PK/odd"
: > "$PK/odd/hol iday%20.png"
"$SYNFILES" --rec peek "$PK" | grep -q 'hol%20iday%2520.png' \
  && ok "peek percent-encodes an awkward filename" \
  || bad "peek did not encode a name with a space and a percent"

# Trailing slash must not produce a doubled separator: the GUI matches these
# strings against paths it already holds.
"$SYNFILES" --rec peek "$PK/" | grep -q '//' \
  && bad "a trailing slash produced a doubled separator" \
  || ok "a trailing slash is trimmed"

# "/" is the one directory whose name already ends in the separator.
"$SYNFILES" --rec peek / | grep -q '^//' \
  && bad "peek / produced doubled separators" || ok "peek / joins without doubling"

# ── .desktop launchers name their own icon ──────────────────────────────────
DT="$T/desktop"
mkdir -p "$DT"
cat > "$DT/game.desktop" <<'DE'
[Desktop Entry]
Type=Application
Name=A Game
Icon=steam_icon_1234
Exec=true

[Desktop Action Play]
Icon=not-this-one
DE
: > "$DT/plain.txt"

icon=$("$SYNFILES" --rec list "$DT" | awk -F'\t' '$1 == "game.desktop" {print $9}')
[ "$icon" = "steam_icon_1234" ] && ok "a .desktop row carries its Icon=" \
                               || bad "launcher icon came back as '$icon'"

icon2=$("$SYNFILES" --rec list "$DT" | awk -F'\t' '$1 == "plain.txt" {print $9}')
[ -z "$icon2" ] && ok "an ordinary file has no icon field" || bad "plain.txt got icon '$icon2'"

# The view mode is a setting like the others, and "auto" has to survive being
# the default: it is what keeps an existing window the shape it already was.
v=$(SYNFILES_CONFIG="$T/viewcfg" "$SYNFILES" config get view)
[ "$v" = auto ] && ok "view defaults to auto" || bad "view defaulted to '$v'"

SYNFILES_CONFIG="$T/viewcfg" "$SYNFILES" config set view compact >/dev/null
v=$(SYNFILES_CONFIG="$T/viewcfg" "$SYNFILES" config get view)
[ "$v" = compact ] && ok "a view mode is remembered" || bad "view came back '$v'"

SYNFILES_CONFIG="$T/viewcfg" "$SYNFILES" config set view sideways >/dev/null 2>&1
[ $? -ne 0 ] && ok "an invented view mode is refused" || bad "view accepted 'sideways'"

# The text slider is a percentage with a floor and a ceiling, like icon_size:
# a settings file that accepts 0 or 5000 is a window that opens unreadable.
SYNFILES_CONFIG="$T/textcfg" "$SYNFILES" config set text_scale 5000 >/dev/null
v=$(SYNFILES_CONFIG="$T/textcfg" "$SYNFILES" config get text_scale)
[ "$v" = 175 ] && ok "text_scale clamps to its maximum" || bad "text_scale went to '$v'"
SYNFILES_CONFIG="$T/textcfg" "$SYNFILES" config set text_scale 10 >/dev/null
v=$(SYNFILES_CONFIG="$T/textcfg" "$SYNFILES" config get text_scale)
[ "$v" = 75 ] && ok "text_scale clamps to its minimum" || bad "text_scale went to '$v'"

# ── tui ─────────────────────────────────────────────────────────────────────
#
# Driven by piping commands at it, which is the whole point of it being
# LINE-ORIENTED: no raw mode, no alternate screen, nothing to restore. A TUI
# that needed a pty to test is also a TUI that leaves a terminal broken when it
# is killed, and this is the front-end for a machine whose desktop will not
# start.
TU="$T/tui"
mkdir -p "$TU/sub"
echo hi > "$TU/one.txt"
echo there > "$TU/two.txt"
echo x > "$TU/.dotted"
ln -s one.txt "$TU/alink.txt"
ln -s nowhere "$TU/dead.txt"

out=$(printf 'q\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"one.txt"*) ok "tui lists the directory" ;;
    *)           bad "tui printed no listing" ;;
esac
case "$out" in
    *"5 items"*) ok "tui hides dotfiles by default" ;;
    *)           bad "tui item count is wrong: $(printf '%s' "$out" | grep -o '[0-9]* items')" ;;
esac

# `a` toggles them, and the count is the observable.
out=$(printf 'a\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"6 items"*) ok "tui shows dotfiles on 'a'" ;;
    *)           bad "'a' did not reveal the dotfile" ;;
esac

# A bare number opens: into a directory, and the header follows.
out=$(printf '1\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"/sub"*) ok "a number opens a folder" ;;
    *)        bad "opening row 1 did not enter sub" ;;
esac

# 'u' comes back up.
out=$(printf '1\nu\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1 | grep -c "$TU$")
[ "$out" -ge 2 ] && ok "'u' returns to the parent" || bad "'u' did not go back up"

# The filter narrows the SAME scan rather than re-reading with a glob.
out=$(printf '/two\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"1 item ·"*) ok "the filter narrows the listing" ;;
    *)            bad "the filter did not narrow to one row" ;;
esac

# Sort cycles through four and says which is on, so a keystroke is never silent.
out=$(printf 's\ns\ns\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1 | grep -c "sort type")
[ "$out" -ge 1 ] && ok "sort cycles to type" || bad "sort did not reach type"

# Row commands delegate rather than reimplementing: info is cmd_info's output.
out=$(printf 'i 2\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"mime"*) ok "'i' shows the properties cmd_info prints" ;;
    *)        bad "'i' printed no info block" ;;
esac

# Nonsense must not crash or act. A row number out of range is the likeliest
# typo and has to be refused by NAME, not clamped to some other file.
out=$(printf '99\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"no row 99"*) ok "an out-of-range row is refused" ;;
    *)             bad "row 99 was not refused" ;;
esac
out=$(printf 'X\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"unrecognised"*) ok "an unknown key says so" ;;
    *)                bad "an unknown key was swallowed" ;;
esac

# EOF is a clean exit: the TUI is pipeable, and a pipe ends.
NO_COLOR=1 "$SYNFILES" tui "$TU" </dev/null >/dev/null 2>&1
[ $? -eq 0 ] && ok "tui exits 0 on EOF" || bad "tui did not exit cleanly on EOF"

# A control byte in a name is REPLACED for display, never stripped: stripping
# prints a name that is not the file's, which is the whole reason records are
# percent-encoded everywhere else.
printf 'x' > "$TU/$(printf 'ctl\tname.txt')"
out=$(printf 'q\n' | NO_COLOR=1 "$SYNFILES" tui "$TU" 2>&1)
case "$out" in
    *"ctl?name.txt"*) ok "a control byte in a name is shown as ?" ;;
    *)                bad "the tab in a filename was not neutralised" ;;
esac

# ── what the tui is allowed to do to a terminal ─────────────────────────────
#
# Arrow keys need ICANON and ECHO off, which is a terminal mode change in a
# program whose whole premise was not making one. These check the boundary that
# makes it safe, by reading the source: a pty harness would have to time out to
# avoid hanging this suite, and a test that can hang is one that gets disabled.
TSRC="$(dirname "$0")/../src/tui.c"
if [ -f "$TSRC" ]; then
    # THE ONE THAT CAUSED REAL HARM. A TUI killed mid-flight never sends the
    # disable, the shell underneath reads every pointer movement as typed
    # input, and it lands in .bash_history as one enormous line.
    grep -qE '\?100[0-9]|\?101[0-9]' "$TSRC" \
        && bad "tui.c turns mouse reporting on" \
        || ok "tui.c never enables mouse reporting"

    # No alternate screen: what you browsed stays in the scrollback.
    grep -qE '\?1049|\?47[hl]' "$TSRC" \
        && bad "tui.c switches to the alternate screen" \
        || ok "tui.c leaves the scrollback alone"

    # ISIG must survive, or Ctrl+C stops working and the only way out of a
    # wedged browser is another terminal.
    grep -q 'c_lflag &= (tcflag_t)~(ICANON | ECHO)' "$TSRC" \
        && ok "only ICANON and ECHO are cleared" \
        || bad "the termios mask changed — check ISIG is still set"

    # Everything short of SIGKILL has to put the terminal back.
    grep -q 'atexit(tty_restore)' "$TSRC" \
        && ok "the restore is wired to atexit" \
        || bad "nothing restores the terminal at exit"
    n=$(grep -cE 'signal\(SIG(INT|TERM|HUP|QUIT), tty_signal\)' "$TSRC")
    [ "$n" = 4 ] && ok "INT, TERM, HUP and QUIT all restore" \
                 || bad "only $n of the 4 signals restore the terminal"

    # …and die of the signal rather than exiting 0, or a caller cannot tell it
    # was interrupted.
    grep -q 'raise(sig)' "$TSRC" \
        && ok "an interrupted tui dies of its signal" \
        || bad "the signal handler swallows the signal"

    # The mode change happens ONLY for a terminal. This is what keeps the line
    # protocol — and every piped test above — working unchanged.
    grep -q 'if (!isatty(STDIN_FILENO))' "$TSRC" \
        && ok "the mode change is gated on stdin being a terminal" \
        || bad "tui.c may change modes on a pipe"

    # A keystroke must not reach the permanent delete. cmd_delete is gated
    # behind --yes for a reason and one key in a browser is not that.
    # A CALL, not the word: the line that chooses cmd_trash carries a comment
    # saying "never cmd_delete", and the first cut of this check matched that
    # and failed against the very code it was written to protect.
    grep -q 'cmd_delete(' "$TSRC" \
        && bad "the tui can reach the permanent delete" \
        || ok "the tui trashes, never deletes"
else
    bad "tui.c not found beside the tests: $TSRC"
fi

# ── the trash on ANOTHER filesystem ─────────────────────────────────────────
#
# The tests below override SYNFILES_TRASH so they can never touch the real one
# — which also means they never exercise topdir_of(), the code that decides
# WHICH trash a file belongs in. That gap hid a real bug: a mount made directly
# under / (a tmpfs /tmp, a separate /home, /boot) resolved to the root
# filesystem, so trashing a file on one built "//.Trash-$uid" and failed with
# "Permission denied" on /. Mounts further down were fine, which is why it went
# unnoticed — a USB stick at /run/media/user/X has a parent that is not "/".
#
# Needs a second filesystem to mean anything, so it SKIPS rather than lying
# when /tmp is on the root device.
if [ "$(stat -c %d /tmp)" != "$(stat -c %d /)" ]; then
    vt=$(mktemp -d /tmp/synfiles-vt.XXXXXX)
    echo payload > "$vt/onvolume.txt"
    # Deliberately WITHOUT the override: this is the path being tested.
    ( unset SYNFILES_TRASH; "$SYNFILES" trash "$vt/onvolume.txt" ) >/dev/null 2>&1
    vtrash="/tmp/.Trash-$(id -u)/files/onvolume.txt"
    if [ -e "$vtrash" ]; then
        ok "a file on another filesystem reaches that volume's trash"
        rm -f "$vtrash" "/tmp/.Trash-$(id -u)/info/onvolume.txt.trashinfo"
    else
        bad "trash on a top-level mount did not land in /tmp/.Trash-$(id -u)"
    fi
    [ -e "$vt/onvolume.txt" ] && bad "the original survived the volume trash" \
                             || ok "…and the original is gone from the volume"
    rm -rf "$vt"
else
    ok "skipped: /tmp is on the root filesystem, so there is no volume to test"
fi

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

# ── the launched application must SURVIVE this process ──────────────────────
# The bug this pins: `Open with Text Editor` did nothing from the GUI while
# working perfectly from a terminal. cmd_action called setsid() but left the
# child holding synfiles' own stdout and stderr — which under quickshell are
# pipes it closes the instant synfiles exits, and synfiles exits immediately by
# design. The application's first line of startup logging then went into a pipe
# with no reader and SIGPIPE killed it before it mapped a window: right argv,
# exit status 0, nothing in any log.
#
# So the fixture chatters BEFORE it does its work, and the pipeline's reader is
# gone before it gets there. `head -n 0` exits at once, which is precisely the
# closed read end quickshell leaves behind.
cat > "$T/launch-marker.sh" <<'LM'
#!/bin/sh
# Wait for the caller to be gone, then write where a real toolkit writes.
sleep 1
echo "starting up" >&2
echo "starting up"
: > "$1.launched"
LM
chmod +x "$T/launch-marker.sh"

cat > "$A/applications/fixture-launch.desktop" <<DE
[Desktop Entry]
Type=Application
Name=Fixture Launcher
Exec=$T/launch-marker.sh %F
DE

rm -f "$AM/a.txt.launched"
"$SYNFILES" action fixture-launch.desktop -- "$AM/a.txt" 2>&1 | head -n 0
# Longer than the fixture's own sleep, or this asserts the timing not the fix.
sleep 3
[ -f "$AM/a.txt.launched" ] \
    && ok "a launched application outlives synfiles' closed stdio" \
    || bad "the launched application was killed with synfiles' pipes"
rm -f "$AM/a.txt.launched"

# And a missing Exec= must still SAY so. The child reports on a close-on-exec
# duplicate of the original stderr, which exists only on the failed-exec path —
# an application that actually started never inherits it. Silence here made an
# uninstalled helper look identical to a working one.
cat > "$A/applications/fixture-missing.desktop" <<'DE'
[Desktop Entry]
Type=Application
Name=Not Installed
Exec=synfiles-no-such-program-exists %F
DE

"$SYNFILES" action fixture-missing.desktop -- "$AM/a.txt" 2>"$T/launch-err" >/dev/null
sleep 1
grep -q 'cannot run synfiles-no-such-program-exists' "$T/launch-err"
check "an Exec= that is not installed reports instead of failing silently" $?

# ── mime aliases and subclasses ─────────────────────────────────────────────
# THE bug that hid Mount ISO. globs2 answers application/vnd.efi.iso for *.iso,
# synui's service menu declares application/x-cd-image, and /usr/share/mime/
# aliases records the second as an alias of the first. Comparing the strings
# directly finds nothing and the menu silently never appears — Dolphin resolves
# aliases, so it showed the entry and synfiles did not.
MM="$T/mimedb"
mkdir -p "$MM"
cat > "$MM/aliases" <<'AL'
application/x-cd-image application/vnd.efi.iso
application/x-iso9660-image application/vnd.efi.iso
AL
cat > "$MM/subclasses" <<'SC'
text/x-csrc text/plain
SC

cat > "$A/kio/servicemenus/test-iso.desktop" <<'SM'
[Desktop Entry]
Type=Application
Name=Disc Image
MimeType=application/x-cd-image;
Actions=mount;

[Desktop Action mount]
Name=Mount Image
Exec=fixture-mount %F
SM

# A menu declared for a subclass's PARENT must apply to the child too, which is
# what makes "open with a text editor" work on source code.
cat > "$A/kio/servicemenus/test-text.desktop" <<'SM'
[Desktop Entry]
Type=Application
Name=Text Tools
MimeType=text/plain;
Actions=count;

[Desktop Action count]
Name=Count Lines
Exec=fixture-count %F
SM

export SYNFILES_MIME_DIR="$MM"
touch "$AM/disc.iso" "$AM/prog.c"

"$SYNFILES" --rec actions "$AM/disc.iso" | grep -q 'Mount Image'
check "a service menu declared for a mime ALIAS still matches" $?

"$SYNFILES" --rec actions "$AM/prog.c" | grep -q 'Count Lines'
check "a service menu declared for a PARENT type matches a subclass" $?

# The equivalence must not become "everything matches everything".
"$SYNFILES" --rec actions "$AM/disc.iso" | grep -q 'Count Lines' \
    && bad "alias resolution matched an unrelated mime type" \
    || ok "alias resolution does not over-match"

unset SYNFILES_MIME_DIR

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

# ── config ──────────────────────────────────────────────────────────────────
export SYNFILES_CONFIG="$T/settings"

n=$("$SYNFILES" --rec config list | awk -F'\t' 'NR==1 {print NF}')
[ "$n" = 2 ] && ok "config list --rec has 2 columns" || bad "config list --rec has $n columns"

# With no file at all, every key still answers with its declared default —
# a GUI reading settings on a fresh install must get values, not blanks.
v=$("$SYNFILES" config get icon_size)
[ "$v" = "20" ] && ok "an unset setting returns its default" || bad "icon_size default is '$v'"

"$SYNFILES" config set icon_size 96 >/dev/null 2>&1
v=$("$SYNFILES" config get icon_size)
[ "$v" = "96" ] && ok "a set value is remembered" || bad "icon_size came back '$v'"

# Written through the binary rather than by the GUI, because quickshell's
# FileView silently drops setText() on a path that does not exist yet — the
# exact state a fresh install is in.
[ -f "$SYNFILES_CONFIG" ] && ok "the settings file is created on first write" \
                          || bad "no settings file was written"

# Numbers CLAMP rather than fail: a slider reporting a value outside the range
# should land at the edge, not refuse.
"$SYNFILES" config set icon_size 9999 >/dev/null 2>&1
v=$("$SYNFILES" config get icon_size)
[ "$v" = "128" ] && ok "an out-of-range number clamps" || bad "icon_size clamped to '$v'"

# Enums and unknown keys do NOT get that treatment. A settings file that
# accepts anything silently accumulates typos that never read back.
"$SYNFILES" config set sort nonsense >/dev/null 2>&1
[ $? -eq 1 ] && ok "an invalid choice is refused" || bad "sort accepted 'nonsense'"
"$SYNFILES" config set bogus 1 >/dev/null 2>&1
[ $? -eq 1 ] && ok "an unknown key is refused" || bad "an unknown key was written"

"$SYNFILES" config set previews notabool >/dev/null 2>&1
[ $? -eq 1 ] && ok "an invalid boolean is refused" || bad "previews accepted a non-boolean"

# Writing one key must not lose another — the file is rewritten whole, and two
# settings changed in quick succession would otherwise clobber each other.
"$SYNFILES" config set sort mtime >/dev/null 2>&1
"$SYNFILES" config set tree 1 >/dev/null 2>&1
{ [ "$("$SYNFILES" config get icon_size)" = "128" ] \
  && [ "$("$SYNFILES" config get sort)" = "mtime" ] \
  && [ "$("$SYNFILES" config get tree)" = "1" ]; }
check "writing one setting preserves the others" $?

# A stored value that no longer validates falls back rather than propagating —
# an enum member that was removed, or a range that shrank.
printf 'icon_size\t99999\nsort\tgone\n' > "$SYNFILES_CONFIG"
v=$("$SYNFILES" config get sort)
[ "$v" = "name" ] && ok "an invalid stored value falls back to the default" \
                  || bad "stored 'gone' came back as '$v'"

# Split view. Off by default — one pane is what a file manager is until you
# ask for two — and remembered, because it is a way of working rather than a
# momentary choice.
v=$(SYNFILES_CONFIG="$T/splitcfg" "$SYNFILES" config get split)
[ "$v" = "0" ] && ok "split view is off by default" || bad "split defaults to '$v'"
SYNFILES_CONFIG="$T/splitcfg" "$SYNFILES" config set split 1 >/dev/null
v=$(SYNFILES_CONFIG="$T/splitcfg" "$SYNFILES" config get split)
[ "$v" = "1" ] && ok "split view is remembered" || bad "split came back '$v'"
SYNFILES_CONFIG="$T/splitcfg" "$SYNFILES" config set split sometimes >/dev/null 2>&1
[ $? -eq 1 ] && ok "split refuses a non-boolean" || bad "split accepted 'sometimes'"

# The divider's position is deliberately NOT a setting: it is a gesture about
# what is on screen right now, and a remembered one would come back wrong.
SYNFILES_CONFIG="$T/splitcfg" "$SYNFILES" config set split_ratio 0.7 >/dev/null 2>&1
[ $? -eq 1 ] && ok "the divider position is not a stored setting" \
             || bad "split_ratio was written"

"$SYNFILES" config reset >/dev/null 2>&1
v=$("$SYNFILES" config get icon_size)
[ "$v" = "20" ] && ok "reset restores the defaults" || bad "after reset icon_size is '$v'"

# ── the folder association ──────────────────────────────────────────────────
#
# Two files have to agree or synfiles is not the file manager, and the failure
# is SILENT both ways: a MimeType line with no mimeapps.list entry makes it a
# candidate that never wins, and a mimeapps.list entry naming a .desktop that
# does not declare the type is ignored by some implementations and honoured by
# others — which is the worst kind of "it works on my machine".
grep -q '^MimeType=inode/directory;$' data/synfiles.desktop
check "the .desktop claims inode/directory" $?

grep -q '^inode/directory=synfiles.desktop$' data/mimeapps.list
check "the vendor mimeapps.list makes synfiles the default for folders" $?

grep -q '^\[Default Applications\]$' data/mimeapps.list
check "mimeapps.list has the [Default Applications] group" $?

# The name in mimeapps.list is resolved against the applications directories,
# so it has to be the .desktop's FILENAME. Naming it after the app, or after
# the binary, silently resolves to nothing.
[ -f "data/synfiles.desktop" ] && ok "the .desktop is named synfiles.desktop" \
                               || bad "mimeapps.list names a file that is not there"

# Exec has to take a path, or the association is a launcher that always opens
# $HOME no matter which folder was clicked.
grep -q '^Exec=synfiles gui %f$' data/synfiles.desktop
check "the handler is passed the folder that was opened" $?

# ── the window follows the desktop font ─────────────────────────────────────
# ~/.config/synui/font.state carries the desktop's family AND its text scale.
# Qt resolves an application's default font ONCE at startup, so both have to be
# BINDINGS on every Text — a bare `font.pixelSize: 13` or a literal family is
# the regression, and it fails silently: the window just stops moving when the
# control panel changes the font, which is how syn-arsenal and synpkg behaved
# until 2026-08-11.
QML="$(dirname "$0")/../data/synfiles.qml"
if [ -f "$QML" ]; then
    grep -q 'config/synui/font.state' "$QML" \
        && ok "the desktop font file is watched" \
        || bad "synfiles.qml does not read font.state"

    # synfiles is the ONE app whose scale has two sources, because it owned a
    # text_scale slider before the setting was desktop-wide. font.state wins by
    # a FLAG and not by arrival order: it reads its own config through a Process
    # and font.state through a FileView, so which lands first is a race, and
    # "last writer wins" would make the size depend on disk timing. Losing
    # scaleFromDesktop is how the config silently starts overriding the desktop
    # again — the exact bug that had synfiles drawing at 115% beside siblings
    # stuck at 100.
    grep -q 'scaleFromDesktop' "$QML" \
        && ok "font.state wins the scale by a flag, not by arrival order" \
        || bad "synfiles.qml lost scaleFromDesktop; the config can override the desktop"

    # awk rather than `grep -c ... | grep -vc ...`: grep exits 1 on no matches,
    # and under pipefail a correct zero would be read as a failed check.
    n=$(awk '/pixelSize: *[0-9]/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "no pixel size bypasses ui()" \
                 || bad "$n pixel size(s) bypass ui()"

    n=$(awk '/family: *"/ && !/family: *"monospace"/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "every literal family is the deliberate monospace" \
                 || bad "$n literal font family/families are not monospace"

    # Qt.application.font.family is the STARTUP font: as a fallback it freezes
    # the very thing this is fixing.
    n=$(grep -c 'Qt.application.font' "$QML" || true)
    [ "$n" = 0 ] && ok "no fallback pins the startup font" \
                 || bad "$n use(s) of Qt.application.font"

    # ── every menu entry does something ─────────────────────────────────────
    #
    # The menus are built from models: an entry is a label and an `act`, and
    # the act is matched in a switch somewhere else in the file. Mistype one
    # and the entry still draws, still highlights, still closes the menu on
    # click — and does nothing. This window has shipped that bug more than
    # once (Rename in icon view, drag-and-drop in the grid), and it is exactly
    # the kind that no runtime test catches because nothing errors.
    #
    # view: and sort: are handled by applyViewAction on their PREFIX, so they
    # are checked against that instead of against a case label.
    missing=""
    for a in $(grep -oE 'act: *"[a-z0-9:_-]+"' "$QML" |
               sed 's/.*"\(.*\)"/\1/' | sort -u); do
        case "$a" in
            view:*|sort:*)
                grep -q "act.indexOf(\"${a%%:*}:\")" "$QML" && continue ;;
        esac
        grep -q "case \"$a\":" "$QML" || missing="$missing $a"
    done
    [ -z "$missing" ] && ok "every menu act is handled somewhere" \
                      || bad "menu act(s) that nothing acts on:$missing"

    # ── the empty-space menu ────────────────────────────────────────────────
    grep -q 'if (!ctxMenu.row)' "$QML" \
        && ok "right-clicking the empty space has its own menu" \
        || bad "synfiles.qml lost the background context menu"

    # It is the SAME menu with no row, not a second one to keep in step.
    n=$(grep -c 'id: ctxMenu' "$QML" || true)
    [ "$n" = 1 ] && ok "there is one context menu, not two" \
                 || bad "$n context menus — the background one was copied, not shared"

    # ── the rubber band hands rows back ─────────────────────────────────────
    #
    # The band sits ON TOP of the views so the Flickable cannot take the press
    # first, which only works because it declines every press that lands on a
    # row. Without that decline it swallows row clicks, row drags and
    # double-click-to-open — i.e. the whole view.
    grep -q 'indexAtPoint' "$QML" && grep -q 'mouse.accepted = false' "$QML" \
        && ok "the band declines presses that land on a row" \
        || bad "the band no longer hands row presses back to the delegates"

    # ── keyboard selection ──────────────────────────────────────────────────
    grep -q 'Qt.Key_Down' "$QML" \
        && ok "the arrow keys move a cursor" \
        || bad "synfiles.qml has no arrow-key navigation"

    grep -q '"extend"' "$QML" && grep -q 'cursorName' "$QML" \
        && ok "Shift extends the selection from the anchor" \
        || bad "Shift+Arrow no longer extends a range"

    # ── formatting goes through syn-disks ───────────────────────────────────
    #
    # ⚠ The one rule for this window and block devices: it does not write to
    # them. What may be formatted is decided by syn-disks' guard.c, and a
    # mkfs, dd or wipefs spawned from here would be a second set of rules
    # about erasing a disk — with no dry run, no refusal text and no
    # confirmation dialogue behind it.
    n=$(grep -cE '"(mkfs|mkfs\.[a-z0-9]+|wipefs|dd|sgdisk|parted)"' "$QML" || true)
    [ "$n" = 0 ] && ok "nothing here writes to a block device itself" \
                 || bad "$n direct block-device command(s) in the file manager"

    grep -q 'syn-disks", "gui", "--format"' "$QML" \
        && ok "Format… opens the disk utility's own dialogue" \
        || bad "the Format entry no longer goes through syn-disks gui --format"

    # syn-disks is an optdepend and `gui --format` is newer than the package
    # itself, so "the binary exists" is not the question. An older syn-disks
    # reads --format as a device name and exits — into nothing, since the
    # window is launched detached. The entry is gated on the FLAG being
    # there, which is a grep of its own --help.
    grep -q "haveFormat" "$QML" && grep -q "grep -q -- 'gui --format'" "$QML" \
        && ok "Format… is offered only where syn-disks understands it" \
        || bad "the Format entry is no longer gated on syn-disks supporting --format"

    # ── launching an app must be DETACHED ───────────────────────────────────
    #
    # ⚠ `xdg-open` DOES NOT RETURN until the application it started exits.
    # Measured: `xdg-open notes.txt` with Kate as the handler ran for the whole
    # of a six-second timeout and Kate was still up. On a shared quickshell
    # Process that holds the command for the lifetime of the app, and assigning
    # `command` to a running Process does nothing — so the second file you
    # opened did nothing, silently, until the first app was closed. Same for
    # the terminal, which obviously outlives the click that opened it.
    #
    # The rule this pins: an external application is launched with
    # execDetached, never through a Process object.
    grep -n '"xdg-open"' "$QML" | grep -qv execDetached \
        && bad "xdg-open is launched through a Process — it will block until the app exits" \
        || ok "opening a file is detached from the file manager"

    n=$(grep -cE 'id: (openProc|termProc)' "$QML" || true)
    [ "$n" = 0 ] && ok "no shared Process is left holding an app launch" \
                 || bad "$n shared launch Process(es) — the queueing bug is back"

    # And when something else IS holding the operation lock, say so rather than
    # returning in silence — a menu entry that does nothing looks broken, which
    # is precisely how the bug above was reported.
    grep -q 'root.busy) {' "$QML" \
        && ok "a busy refusal reaches the status line" \
        || bad "runOp refuses silently again"

    # ── the last tab still closes something ────────────────────────────────
    #
    # The × used to be hidden at one tab (`visible: pane.tabs.length > 1`) and
    # closeTab() refused it, so the only way out of the last tab was the
    # window's own close button — which is not where anyone looks, and made the
    # tab bar behave unlike every other tabbed application on the machine.
    grep -qE 'visible: pane\.tabs\.length > 1' "$QML" \
        && bad "the tab × is hidden again when only one tab is open" \
        || ok "the tab × is shown even on the last tab"

    # What it closes depends on what is left, and the split case is the one
    # worth pinning: the last tab of ONE pane must fold the split, not quit the
    # window because half of it ran out of tabs.
    grep -q 'function closeTabOrQuit' "$QML" \
        && ok "closing a tab goes through closeTabOrQuit" \
        || bad "closeTabOrQuit is gone — the last tab probably refuses again"
    grep -q 'if (!root.split) { Qt.quit(); return }' "$QML" \
        && ok "the last tab of the last pane quits" \
        || bad "the last tab no longer quits the window"

    # Ctrl+W is the same action and must not drift back to the refusing one.
    grep -q 'Key_W) { root.closeTabOrQuit' "$QML" \
        && ok "Ctrl+W closes the last tab too" \
        || bad "Ctrl+W still calls the closeTab that refuses"

    # ── a folder's size is its CONTENTS ────────────────────────────────────
    #
    # `info` reports st_size, which for a directory is the size of the
    # directory entry — the SYNAPSE folder, ISO and all, read "890 B". The walk
    # is a separate command so the panel does not freeze on it.
    grep -q 'root.startFolderSize' "$QML" \
        && ok "the properties panel asks for a folder's real size" \
        || bad "nothing starts the recursive size walk"

    # SplitParser, not StdioCollector: `du` prints a RUNNING total and a
    # collector fires once at the end, which would leave the row saying
    # "calculating…" for the whole walk and then jump to the answer.
    awk '/id: duProc/,/^        }/' "$QML" | grep -q 'SplitParser' \
        && ok "the running total is read line by line" \
        || bad "duProc no longer streams — the total cannot update while it runs"

    # A walk over a big tree outliving its panel lands its records in whatever
    # folder is asked about next.
    grep -q 'onShowPropsChanged: if (!root.showProps) root.stopFolderSize()' "$QML" \
        && ok "closing the panel stops the walk" \
        || bad "the size walk is not stopped when properties closes"

    # ── it still parses ─────────────────────────────────────────────────────
    #
    # A syntax error in this file is invisible to everything above: the C core
    # builds, every record test passes, and the only symptom is a window that
    # does not open. qmllint settles it in a second — but two things about it
    # have to be handled or the check is worse than none.
    #
    # 1. THE VERDICT IS THE EXIT STATUS. This qmllint prints NOTHING on a
    #    syntax error in this file — not a line, not a position — and simply
    #    exits non-zero. The first cut of this check read its output and so
    #    passed happily on a file with `((( ` appended to it.
    # 2. `pragma ComponentBehavior: Bound` makes it exit non-zero the same
    #    silent way on any file carrying it, which would have made the check
    #    fail forever. It is stripped from the COPY being checked: it decides
    #    what Qt binds inside delegates, not whether the file parses.
    if command -v qmllint >/dev/null 2>&1; then
        sed '/^pragma ComponentBehavior/d' "$QML" > "$T/lint.qml"
        if qmllint "$T/lint.qml" > "$T/lint.txt" 2>&1; then
            ok "the QML parses cleanly"
        else
            bad "qmllint rejects synfiles.qml — the window will not open"
            [ -s "$T/lint.txt" ] && sed 's/^/        /' "$T/lint.txt" >&2
        fi
    else
        echo "  skip  qmllint not installed"
    fi
else
    bad "synfiles.qml not found beside the tests: $QML"
fi

unset SYNFILES_CONFIG

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
