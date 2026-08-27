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
# else. Every record must have exactly 10 fields (the 9th is a .desktop
# launcher's own Icon=, empty for everything else; the 10th is the type's
# human description).
widths=$("$SYNFILES" --rec list "$D" | awk -F'\t' '{print NF}' | sort -u | tr '\n' ' ')
[ "$widths" = "10 " ] && ok "every record has 10 fields" \
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

# A SYMLINK IS NOT THE TREE IT POINTS AT. `du` reports the link itself, the way
# `list` shows it, and never walks through it — so the number can never be some
# other directory's total wearing this name.
ln -s "$T/du" "$T/du-link"
eq "du does not walk through a symlink to a directory" "$(printf '%s' "$T/du" | wc -c)" \
   "$("$SYNFILES" --rec du "$T/du-link" | awk -F'\t' 'END {print $1}')"

# …AND IT MUST NOT BE PERSUADED TO. The name was checked with lstat() and then
# opened again by name — two resolutions of one path, and a plain open() follows
# a symlink that lstat() would not. Swapping the directory for a symlink between
# the two sent the whole walk into another tree and reported ITS size as yours;
# a preloaded lstat that swapped the path won that race every time.
#
# The open is now the test: O_DIRECTORY refuses a non-directory, O_NOFOLLOW
# refuses the symlink, and the numbers come from fstat() on that descriptor. The
# recursion always worked this way, and CodeQL flagged the entry point that did
# not (cpp/toctou-race-condition, alert #12).
_l="$(dirname "$0")/../src/listing.c"
if [ -f "$_l" ]; then
    # THE ENTRY POINT SPECIFICALLY — open(path, …), not the openat() in the
    # recursion. Matching bare "O_NOFOLLOW" would find the recursion, which was
    # always right, and so would pass against the very bug this guards.
    grep -q 'open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW' "$_l" \
        && ok "du opens the top of the walk with O_NOFOLLOW" \
        || bad "du's entry-point open can follow a symlink again"
    # The old shape: a bare open() of the path with no O_NOFOLLOW.
    grep -qE 'open\([^,]*path[^)]*O_DIRECTORY \| O_CLOEXEC' "$_l" \
        && bad "the check-then-open pattern is back in cmd_du" \
        || ok "cmd_du no longer opens a path it only checked by name"
    # The recursion was already descriptor-relative and symlink-refusing. It
    # stays that way, or the walk can be redirected one level down instead.
    grep -q 'openat(dirfd, e->d_name,' "$_l" \
        && ok "the recursion still opens relative to its parent descriptor" \
        || bad "du_walk no longer opens relative to the directory it is reading"
    grep -q 'fstatat(dirfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW)' "$_l" \
        && ok "the recursion still stats without following symlinks" \
        || bad "du_walk's stat can follow a symlink"
fi

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
[ "$c" = 10 ] && ok "list gained no resolution column" || bad "list rows now have $c fields"

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

# The left panel. ON by default and it has to stay that way: the default IS the
# window everybody already has, and a "1" that ever became "0" would hide the
# places list on every existing install at once, with nothing said.
v=$(SYNFILES_CONFIG="$T/sidecfg" "$SYNFILES" config get sidebar)
[ "$v" = 1 ] && ok "the sidebar is on by default" || bad "sidebar defaulted to '$v'"

SYNFILES_CONFIG="$T/sidecfg" "$SYNFILES" config set sidebar 0 >/dev/null
v=$(SYNFILES_CONFIG="$T/sidecfg" "$SYNFILES" config get sidebar)
[ "$v" = 0 ] && ok "hiding the sidebar is remembered" || bad "sidebar came back '$v'"

# Boolean, not free text — the QML reads it as `=== "1"`, so anything that is
# neither 0 nor 1 would silently mean "hidden".
SYNFILES_CONFIG="$T/sidecfg" "$SYNFILES" config set sidebar maybe >/dev/null 2>&1
[ $? -ne 0 ] && ok "a non-boolean sidebar value is refused" || bad "sidebar accepted 'maybe'"

# The QML half of it: a setting the binary remembers and the window never reads
# is the shape of bug this pairing exists to prevent. Its own path, because
# $QML is not defined until the QML section far below.
_qml="$(dirname "$0")/../data/synfiles.qml"
grep -q 'case "sidebar":' "$_qml" \
    && ok "the window reads the sidebar setting back" \
    || bad "nothing in the QML reads the sidebar setting"
grep -q 'Qt.Key_F9' "$_qml" \
    && ok "F9 toggles the sidebar" || bad "F9 is not bound"

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
# Driven by piping commands at it. A pipe is not a terminal, so this drives the
# LINE PROTOCOL — which is the half that has to keep working unchanged, because
# it is what scripts use and what a machine whose desktop will not start gets
# over SSH. The arrow-key half only ever wakes up for a real terminal.
#
# The terminal half is checked further down by reading the source, not by
# driving a pty: a pty harness has to time out to avoid hanging this suite, and
# a test that can hang is a test that gets disabled.
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

# ── the tui moves things ────────────────────────────────────────────────────
#
# The move itself is cmd_move — the tui only picks the arguments. What is worth
# testing here is the picking: which row, which destination, and above all that
# a destination the mover would REFUSE does not take the browser down with it.
MV="$T/tuimove"
mkdir -p "$MV/dest"
echo hi > "$MV/one.txt"
echo yo > "$MV/two.txt"
echo x  > "$MV/notadir"

# Row 3 is one.txt (dest, notadir, one.txt, two.txt).
out=$(printf 'm 3 dest\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$MV" 2>&1)
[ -f "$MV/dest/one.txt" ] && ok "the tui moves a file into a folder" \
                          || bad "m did not move the file: $out"
[ -f "$MV/one.txt" ] && bad "the original is still there after a move" \
                     || ok "the original is gone after a move"

# RELATIVE TO WHAT IS BEING BROWSED, not to where synfiles was launched. The
# subshell cd is the whole test: realpath() would resolve "dest" against this
# script's working directory and fail on a folder that is plainly on screen.
mkdir -p "$MV/rel/inner"
echo r > "$MV/rel/movable.txt"
( cd / && printf 'm 2 inner\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$MV/rel" >/dev/null 2>&1 )
[ -f "$MV/rel/inner/movable.txt" ] \
    && ok "a relative destination resolves against the browsed folder" \
    || bad "a relative destination was resolved against the process cwd"

# Same rule for cd, which had the same bug: c <name> on a folder in the listing.
out=$( cd / && printf 'c inner\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$MV/rel" 2>&1 )
case "$out" in
    *"$MV/rel/inner"*) ok "c takes a path relative to the browsed folder" ;;
    *)                 bad "c did not follow a relative path into the listing" ;;
esac

# THE ONE THAT MATTERS. cmd_move reports a bad destination with die(), and
# die() is exit(1) — in a browser that would end the session over a typo. The
# discriminating part is the SECOND command: it only prints if the loop lived.
for badness in "m 3 $MV/notadir" "m 3 /nope/nowhere" "m 3" "m"; do
    out=$(printf '%s\na\nq\n' "$badness" | NO_COLOR=1 "$SYNFILES" tui "$MV" 2>&1)
    rc=$?
    case "$out" in
        *"items"*) ;;
        *) bad "the tui died on '$badness'"; continue ;;
    esac
    # `a` toggles hidden files, so a live loop prints two different counts.
    n=$(printf '%s' "$out" | grep -c "items")
    [ "$rc" = 0 ] && [ "$n" -ge 2 ] \
        && ok "the tui survives '$badness'" \
        || bad "the tui did not keep going after '$badness' (rc=$rc, $n listings)"
done

# A collision must not overwrite. cmd_move's default is CONFLICT_ERROR and the
# tui does not override it — clobbering is not a thing a browser picks for you.
mkdir -p "$MV/clash/dest"
echo original > "$MV/clash/dest/f.txt"
echo NEWER    > "$MV/clash/f.txt"
printf 'm 2 dest\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$MV/clash" >/dev/null 2>&1
case "$(cat "$MV/clash/dest/f.txt")" in
    original) ok "a name collision does not overwrite" ;;
    *)        bad "the tui overwrote a file it collided with" ;;
esac
[ -f "$MV/clash/f.txt" ] && ok "a refused move leaves the original alone" \
                         || bad "the source vanished on a refused move"

# The tui must not grow a mover of its own — cmd_move is where the
# cross-filesystem copy-verify-delete and the undo journal entry live.
_t="$(dirname "$0")/../src/tui.c"
if [ -f "$_t" ]; then
    grep -q 'cmd_move(' "$_t" && ok "the tui moves through cmd_move" \
                              || bad "the tui does not call cmd_move"
    grep -qE '(^|[^_[:alnum:]])rename\(' "$_t" \
        && bad "tui.c calls rename() itself instead of using cmd_move" \
        || ok "tui.c has no mover of its own"
fi

# ── the tui copies things ───────────────────────────────────────────────────
#
# `y`, not `c` — c is cd. Same guards as move, because cmd_copy die()s on a bad
# destination in exactly the same way, plus one that is copy's alone: a
# directory copied into its own subtree recurses until the disk fills.
CP="$T/tuicopy"
mkdir -p "$CP/dest" "$CP/tree/inner"
echo payload > "$CP/one.txt"
echo deep    > "$CP/tree/inner/f.txt"
echo x       > "$CP/plain"

# Rows sort directories first: 1 dest, 2 tree, 3 one.txt, 4 plain.
printf 'y 3 dest\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$CP" >/dev/null 2>&1
[ -f "$CP/dest/one.txt" ] && ok "the tui copies a file into a folder" \
                          || bad "y did not copy the file"
[ -f "$CP/one.txt" ] && ok "a copy leaves the original where it was" \
                     || bad "the original vanished — that is a move, not a copy"

# A copy recurses, and the original tree is untouched.
printf 'y 2 dest\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$CP" >/dev/null 2>&1
[ -f "$CP/dest/tree/inner/f.txt" ] && ok "copying a folder takes what is in it" \
                                   || bad "the nested file did not come along"
[ -f "$CP/tree/inner/f.txt" ] && ok "the copied folder is left alone" \
                              || bad "copying a folder consumed the original"

# COPY'S OWN HAZARD: a folder copied into itself, or into its own subtree,
# recurses forever. cmd_copy refuses on resolved paths; the tui must let it,
# and must still be running afterwards.
out=$(printf 'y 2 tree\ny 2 tree/inner\na\nq\n' | NO_COLOR=1 "$SYNFILES" tui "$CP" 2>&1)
case "$out" in
    *"inside the source"*) ok "copying a folder into itself is refused" ;;
    *)                     bad "the self-copy guard did not fire: $out" ;;
esac
[ "$(find "$CP/tree" | wc -l)" = 3 ] && ok "a refused self-copy changes nothing" \
                                     || bad "the tree grew: $(find "$CP/tree" | wc -l) entries"

# The same die() guards as move, and the same discriminating check: the second
# command only prints if the browser is still alive.
for badness in "y 3 $CP/plain" "y 3 /nope/nowhere" "y 3" "y"; do
    out=$(printf '%s\na\nq\n' "$badness" | NO_COLOR=1 "$SYNFILES" tui "$CP" 2>&1)
    n=$(printf '%s' "$out" | grep -c "items")
    [ "$n" -ge 2 ] && ok "the tui survives '$badness'" \
                   || bad "the tui died on '$badness'"
done

if [ -f "$_t" ]; then
    grep -q 'cmd_copy(' "$_t" && ok "the tui copies through cmd_copy" \
                              || bad "the tui does not call cmd_copy"
fi

# ── the redraw covers exactly what it printed ───────────────────────────────
#
# The arrow-key frame is redrawn by moving the cursor UP by the height of the
# last frame. That height was hand-counted, and was wrong three ways: the
# header emits five newlines and was counted as four, and both the path and the
# hint lines WRAP on a narrow terminal. The frame then walked down the screen a
# row per keypress until the terminal scrolled — and once it has scrolled, the
# rows the redraw wants are gone.
#
# Measured beats counted, so the test is that it is still measured.
if [ -f "$_t" ]; then
    grep -q 'drawn = frame_height()' "$_t" \
        && ok "the frame height is measured, not counted" \
        || bad "the redraw is back to a hand-counted height"
    grep -qE '^\s*before \+=' "$_t" \
        && bad "hand-counted frame arithmetic is back in tui.c" \
        || ok "no hand-counted line arithmetic remains"
    # The wrap is the part a line count cannot see.
    grep -q 'g_frame_col >= g_cols' "$_t" \
        && ok "the measurement accounts for wrapped lines" \
        || bad "wrapping is no longer counted — narrow terminals will drift"
fi

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

# A colliding DIRECTORY is renamed AS A WHOLE, and the existing one is left
# exactly as it was.
#
# This used to assert `dst/src/a (copy).txt` — that a second copy of a folder
# merged into the first and renamed the files inside it. That is what pasting a
# folder into the folder that CONTAINS it did: no new folder appeared, and the
# original filled up with `(copy)` duplicates while the run reported "4 done, 0
# skipped, 0 failed". The policy is now applied to the directory itself.
"$SYNFILES" copy --conflict=rename "$C/src" "$C/dst" >/dev/null 2>&1
[ -f "$C/dst/src (copy)/a.txt" ] && ok "--conflict=rename renames a colliding directory" \
                                 || bad "no 'src (copy)' directory was produced"
[ ! -e "$C/dst/src/a (copy).txt" ] && ok "...and leaves the existing directory alone" \
                                   || bad "the existing directory was merged into"

# The extension rule, on the collision it was written for: a FILE. "a.txt"
# becomes "a (copy).txt", never "a.txt (copy)".
mkdir -p "$C/ext"
echo one > "$C/ext/a.txt"
"$SYNFILES" copy --conflict=rename "$C/src/a.txt" "$C/ext" >/dev/null 2>&1
[ -f "$C/ext/a (copy).txt" ] && ok "--conflict=rename inserts before the extension" \
                             || bad "no 'a (copy).txt' was produced"

# Pasting into the folder something came FROM is the ordinary way to duplicate
# it, and it must produce a copy rather than walking the source into itself.
mkdir -p "$C/self/f"
echo x > "$C/self/f/inner.txt"
"$SYNFILES" copy --conflict=rename "$C/self/f" "$C/self" >/dev/null 2>&1
[ -f "$C/self/f (copy)/inner.txt" ] && ok "a folder pasted into its own parent is duplicated" \
                                    || bad "no 'f (copy)' — the source was merged into itself"
[ ! -e "$C/self/f/inner (copy).txt" ] && ok "...without polluting the original" \
                                      || bad "the original folder gained duplicates"

# THE DESTINATION CAN BE THE SOURCE. `copy --conflict=overwrite f.txt .` removed
# f.txt and then copied from the file it had just deleted — "0 done, 1 failed",
# and the only copy of the data gone. Unreachable from the GUI only for as long
# as the GUI never offers an overwrite, which is a promise about a caller.
mkdir -p "$C/same"
echo IRREPLACEABLE > "$C/same/f.txt"
"$SYNFILES" copy --conflict=overwrite "$C/same/f.txt" "$C/same" >/dev/null 2>&1
[ "$(cat "$C/same/f.txt" 2>/dev/null)" = IRREPLACEABLE ] \
    && ok "overwriting a file with itself is refused, not performed" \
    || bad "the file was deleted by being overwritten with itself"

# Worse in move, which has no second copy anywhere: it removed the destination
# and then had nothing left to rename onto it.
mkdir -p "$C/samemv"
echo IRREPLACEABLE > "$C/samemv/f.txt"
"$SYNFILES" move --conflict=overwrite "$C/samemv/f.txt" "$C/samemv" >/dev/null 2>&1
[ "$(cat "$C/samemv/f.txt" 2>/dev/null)" = IRREPLACEABLE ] \
    && ok "moving a file onto itself is refused, not performed" \
    || bad "the file was deleted by being moved onto itself"

# Merging is what --conflict=overwrite MEANS for two directories, and it still
# has to work: the colliding file is replaced, everything else survives.
mkdir -p "$C/mrg/src/x" "$C/mrg/dst/x"
echo new > "$C/mrg/src/x/f"
echo old > "$C/mrg/dst/x/f"
echo keep > "$C/mrg/dst/x/keep"
"$SYNFILES" copy --conflict=overwrite "$C/mrg/src/x" "$C/mrg/dst" >/dev/null 2>&1
[ "$(cat "$C/mrg/dst/x/f")" = new ] && [ "$(cat "$C/mrg/dst/x/keep")" = keep ] \
    && ok "--conflict=overwrite merges two directories" \
    || bad "the directory merge lost something"

# A recursive delete REPORTS AS IT GOES. Deleting one large folder is a single
# sf_rm_rf call, so `delete` and `trash empty` said nothing at all until the
# whole tree was gone — minutes for a big folder, and a GUI silence that long
# cannot be told from a hang.
mkdir -p "$C/tick/big/sub"
echo x > "$C/tick/big/f.txt"
echo y > "$C/tick/big/sub/g.txt"
n=$("$SYNFILES" --rec delete --yes "$C/tick/big" | grep -c 'removed$')
[ "$n" -ge 4 ] && ok "a recursive delete reports each entry as it removes it" \
               || bad "the recursive delete emitted $n progress records, want >= 4"

# ...but the removal inside an OVERWRITE must stay quiet: those records would
# be counted by the GUI as files copied.
mkdir -p "$C/tickq/src" "$C/tickq/dst/x"
echo new > "$C/tickq/src/x"
echo old > "$C/tickq/dst/x/inner"
"$SYNFILES" --rec copy --conflict=overwrite "$C/tickq/src/x" "$C/tickq/dst" \
    | grep -q 'removed$' \
    && bad "an overwrite emitted removal records" \
    || ok "the removal inside an overwrite stays quiet"

# ── how much there is, and stopping part-way ────────────────────────────────
#
# A count of files cannot say how far along a copy is when one of them is 8 GB,
# so there is no percentage and no estimate without a total. It is emitted once,
# before anything is copied, from a stat-only pre-pass.
mkdir -p "$C/tot/src/sub" "$C/tot/dst"
printf '%.0s.' $(seq 1 1000) > "$C/tot/src/a.bin"
printf '%.0s.' $(seq 1 2000) > "$C/tot/src/sub/b.bin"
tot=$("$SYNFILES" --rec copy "$C/tot/src" "$C/tot/dst" | awk -F'\t' '$1=="total"{print $3}')
[ "$tot" = 3000 ] && ok "copy reports the byte total before it starts" \
                  || bad "the pre-scan totalled $tot bytes, want 3000"

# Every byte figure is CUMULATIVE, so the last one equals the total — a reader
# that had to decide whether to add or replace would get big files wrong, since
# those report both a running figure and a final one.
last=$("$SYNFILES" --rec copy --conflict=rename "$C/tot/src" "$C/tot/dst" \
       | awk -F'\t' '$2=="done" && $4!="" {b=$4} END{print b}')
[ "$last" = 3000 ] && ok "the byte figures are cumulative, ending at the total" \
                   || bad "the last byte figure was $last, want 3000"

# CANCELLING has to leave a filesystem somebody can reason about: no fragment
# of a half-written file, and a record saying it stopped.
#
# SKIPPED rather than failed when the copy wins the race, which on tmpfs it
# usually does — 30 MB is copied in about the time it takes the shell to run
# `kill`. A fixture big enough to win reliably would be a gigabyte, which is
# not a thing to allocate inside somebody's package build, and a check that
# sometimes fails for timing is worse than one that sometimes says nothing.
# The behaviour it guards was verified directly: SIGTERM to a 3000-file copy
# gave exit 130, a `cancelled` record, 897 files copied and every one of them
# whole.
mkdir -p "$C/cxl/src" "$C/cxl/dst"
i=0
while [ $i -lt 120 ]; do head -c 262144 /dev/zero > "$C/cxl/src/f$i"; i=$((i+1)); done
"$SYNFILES" --rec copy "$C/cxl/src" "$C/cxl/dst" > "$C/cxl/out.txt" 2>&1 &
cxlpid=$!
kill -TERM "$cxlpid" 2>/dev/null
wait "$cxlpid"; cxlrc=$?
if [ "$cxlrc" = 130 ]; then
    grep -q '^cancelled' "$C/cxl/out.txt" \
        && ok "a cancelled copy says so" \
        || bad "a cancelled copy exited 130 without saying it was cancelled"
    stray=$(find "$C/cxl/dst" -type f ! -size 262144c | head -1)
    [ -z "$stray" ] && ok "a cancelled copy leaves no half-written file" \
                    || bad "cancel left a partial file: $stray"
else
    echo "  skip  the copy finished before the cancel landed (exit $cxlrc)"
fi

# A DELETE is measured in ENTRIES, not bytes. It costs one unlink each whatever
# they weigh, so a folder of empty files is minutes and no bytes at all — a
# byte-shaped estimate would call it finished before it started. The byte column
# is deliberately zero, which is how the GUI knows which unit to draw.
mkdir -p "$C/eta/d/sub"
i=0; while [ $i -lt 5 ]; do : > "$C/eta/d/f$i"; : > "$C/eta/d/sub/g$i"; i=$((i+1)); done
line=$("$SYNFILES" --rec delete --yes "$C/eta/d" | awk -F'\t' '$1=="total"')
n=$(printf '%s' "$line" | cut -f2)
b=$(printf '%s' "$line" | cut -f3)
[ "$n" = 12 ] && ok "delete counts its entries before starting" \
              || bad "delete totalled $n entries, want 12"
[ "$b" = 0 ] && ok "...and reports no byte total, so the estimate is in items" \
             || bad "delete reported a byte total of $b, which would mis-scale the bar"

# Emptying the trash gets the same, from a readdir walk that is far cheaper
# than the unlinks it is estimating.
tfx=$(mktemp -d)
SYNFILES_TRASH="$tfx" mkdir -p "$tfx/files" "$tfx/info"
mkdir -p "$C/eta/t/folder"
: > "$C/eta/t/folder/inner"
SYNFILES_TRASH="$tfx" "$SYNFILES" --rec trash "$C/eta/t/folder" >/dev/null 2>&1
tn=$(SYNFILES_TRASH="$tfx" "$SYNFILES" --rec trash empty --yes | awk -F'\t' '$1=="total"{print $2}')
[ "${tn:-0}" -ge 2 ] && ok "emptying the trash counts what it is about to remove" \
                     || bad "trash empty totalled ${tn:-nothing}, want at least 2"
rm -rf "$tfx"

# ── collisions ──────────────────────────────────────────────────────────────
#
# What the GUI asks BEFORE it pastes, so that "overwrite?" is only asked when
# there is something to overwrite. One stat per source, no traversal.
K="$T/collide"
mkdir -p "$K/dst/folder" "$K/src/folder"
echo x > "$K/src/lone.txt"
echo y > "$K/dst/dup.txt"
echo z > "$K/src/dup.txt"

out=$("$SYNFILES" collisions "$K/src/folder" "$K/src/lone.txt" "$K/src/dup.txt" "$K/dst")
grep -q '^folder	dir'   <<<"$out" && ok "collisions reports a colliding directory" \
                                   || bad "the colliding directory was not reported"
grep -q '^dup.txt	file' <<<"$out" && ok "collisions reports a colliding file" \
                                   || bad "the colliding file was not reported"
grep -q 'lone'          <<<"$out" && bad "a name that does not collide was reported" \
                                  || ok "collisions stays quiet about names that are free"

# Exit 0 either way: this ANSWERS a question, and a non-zero exit would make
# "yes, two of them" indistinguishable from "the destination does not exist".
"$SYNFILES" collisions "$K/src/dup.txt" "$K/dst" >/dev/null 2>&1
[ $? -eq 0 ] && ok "collisions exits 0 when things collide" \
             || bad "collisions used its exit status to mean 'found some'"
"$SYNFILES" collisions "$K/src/lone.txt" "$K/dst" >/dev/null 2>&1
[ $? -eq 0 ] && ok "collisions exits 0 when nothing collides" \
             || bad "collisions failed on a clean destination"

# The source can BE the entry it collides with — pasting into the folder it
# came from. The GUI needs to know, because overwriting there would delete the
# original, so that button must not be offered.
out=$("$SYNFILES" --rec collisions "$K/dst/dup.txt" "$K/dst")
awk -F'\t' '$1!="path" && $4=="yes"' <<<"$out" | grep -q . \
    && ok "collisions flags a source that IS the destination entry" \
    || bad "the duplicate-in-place case was not flagged"
out=$("$SYNFILES" --rec collisions "$K/src/dup.txt" "$K/dst")
awk -F'\t' '$1!="path" && $4=="no"' <<<"$out" | grep -q . \
    && ok "...and does not flag an ordinary collision as that" \
    || bad "an ordinary collision was called same"

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

# The IMAGE associations, added when synui grew a viewer. The .desktop that has
# to agree with these lines is synui's, not ours — two packages cannot both own
# /usr/share/applications/mimeapps.list, so the one vendor file carries an
# association for somebody else's entry. synui's own suite checks the other half
# (that synui-view.desktop declares these types); all this can check is that the
# lines are here and name the file they mean to.
grep -q '^image/png=synui-view.desktop$' data/mimeapps.list
check "the vendor mimeapps.list opens PNGs in the SynapseOS image viewer" $?

grep -q '^image/jpeg=synui-view.desktop$' data/mimeapps.list
check "the vendor mimeapps.list opens JPEGs in the SynapseOS image viewer" $?

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
    #
    # `submenu` is the one act that is deliberately NOT in the switch: it names
    # a row that opens a flyout on hover and does nothing on click, so it
    # returns before the switch is reached. Checked below on its own terms
    # rather than waved through here.
    missing=""
    for a in $(grep -oE 'act: *"[a-z0-9:_-]+"' "$QML" |
               sed 's/.*"\(.*\)"/\1/' | sort -u); do
        case "$a" in
            view:*|sort:*)
                grep -q "act.indexOf(\"${a%%:*}:\")" "$QML" && continue ;;
            submenu) continue ;;
        esac
        grep -q "case \"$a\":" "$QML" || missing="$missing $a"
    done
    [ -z "$missing" ] && ok "every menu act is handled somewhere" \
                      || bad "menu act(s) that nothing acts on:$missing"

    # ── the Open With submenu ───────────────────────────────────────────────
    #
    # Six "Open with …" rows used to sit in the middle of this menu for an
    # ordinary PNG — most of its height, with the longest and most-elided
    # labels in it. They are one row and a flyout now.
    grep -q 'act: "submenu"' "$QML" \
        && ok "Open with is one row with a submenu" \
        || bad "synfiles.qml lost the Open With submenu row"

    # ⚠ The row must RETURN before the switch. Without that, clicking the
    # parent row falls through to `ctxMenu.open = false` and the whole menu
    # closes under the flyout it just opened — which reads as "Open with does
    # nothing", the exact failure the act-coverage check above exists for.
    grep -q 'if (ctxItem.modelData.act === "submenu")' "$QML" \
        && grep -q 'return$' "$QML" \
        && ok "clicking the submenu row does not close the menu" \
        || bad "the submenu row falls through to the act switch"

    # ⚠ And it is only added when there is something behind it. An arrow that
    # opens onto an empty panel is worse than the flat rows it replaced.
    grep -q 'if (opens.length > 0)' "$QML" \
        && ok "no applications means no Open with row" \
        || bad "the Open With row is added unconditionally"

    # The flyout is a SIBLING of the menu, not a child: ctxFlick clips, so a
    # child would be sliced off at the menu's own right edge.
    grep -q 'id: ctxSub' "$QML" \
        && ok "the flyout exists" \
        || bad "synfiles.qml has no Open With flyout"

    # ⚠ And its hover is a HoverHandler, NOT a MouseArea filling the panel.
    # Qt hands the hover enter/exit pair to exactly ONE item — the topmost
    # under the pointer — so a panel-filling MouseArea is `exited` the moment
    # the pointer reaches an ENTRY inside the flyout, because the entry's own
    # MouseArea takes the hover. That restarted the close timer under a hand
    # that had just ARRIVED, and the flyout went away 300ms later while the
    # pointer sat still on it: "it closes by itself before I can get to it".
    # tests/ctx_flyout_hover.qml drives the pointer path that proves it.
    # ⚠ Matched in a variable, not `awk … | grep -q`: under `set -o pipefail`
    # grep -q exits the moment it matches, awk dies of SIGPIPE, and the
    # PIPELINE reports 141 — a passing check that reads as a failure.
    ctxsub=$(awk '/id: ctxSub$/,/^        \}$/' "$QML")
    case "$ctxsub" in
        *HoverHandler*) ok "the flyout tracks hover across its whole subtree" ;;
        *) bad "ctxSub lost its HoverHandler — hover over an entry reads as a leave" ;;
    esac

    grep -q 'onExited: subCloseTimer.restart()' "$QML" \
        && bad "the flyout closes on a MouseArea exit an ENTRY inside it causes" \
        || ok "no panel-filling MouseArea decides when the flyout leaves"

    # The FIRING is guarded too, not only the restarts: where the pointer is
    # now is a fact, a restart scheduled 300ms ago is only a guess, and one
    # stale guess is all it takes to shut the flyout under a resting hand.
    grep -q 'onTriggered: if (!ctxSubHover.hovered)' "$QML" \
        && ok "the close timer re-checks the pointer before it closes" \
        || bad "subCloseTimer closes without asking where the pointer is"

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

    # ── ...and that it LOADS ────────────────────────────────────────────────
    #
    # qmllint parses. It does not resolve properties on this file's own inline
    # components, so `ToggleChip { ignoreBusy: true }` — a property that
    # belongs to a different app's button — passed the lint above and then
    # failed the whole window with "Cannot assign to non-existent property".
    # A QML that cannot load is not a cosmetic defect: nothing opens.
    #
    # ONLY a load failure fails this check. Anything else — no quickshell, no
    # runtime dir, a crash on some machine without a GPU — skips, because a
    # test that can break a package build for reasons unrelated to the code is
    # worse than no test. (The build has been broken that way once already.)
    if command -v quickshell >/dev/null 2>&1; then
        # A COPY with a quit timer appended, for two reasons: a QML that loads
        # successfully RUNS — without this the check would sit until its
        # timeout and add that to every build — and the timer has to go INSIDE
        # the root object, before its final brace, or the file is a syntax
        # error and the check would "fail" on a perfectly good QML.
        qsq="$T/loadcheck.qml"
        awk 'BEGIN{RS="\0"} {
                n = match($0, /}[ \t\r\n]*$/)
                printf "%s\n    Timer { running: true; interval: 1200; repeat: false;\n             onTriggered: Qt.quit() }\n%s", substr($0,1,n-1), substr($0,n)
             }' "$QML" > "$qsq"
        qsrun="$T/qsrun"
        mkdir -p "$qsrun"
        qsout=$(XDG_RUNTIME_DIR="$qsrun" QT_QPA_PLATFORM=offscreen \
                timeout 30 quickshell -p "$qsq" 2>&1 | head -40)
        if printf '%s' "$qsout" | grep -q "Failed to load configuration"; then
            bad "synfiles.qml does not LOAD — the window will not open"
            printf '%s\n' "$qsout" | grep -A2 "Failed to load" | sed 's/^/        /' >&2
        else
            ok "the QML loads in a real engine"
        fi
    else
        echo "  skip  quickshell not installed, cannot check that the QML loads"
    fi
else
    bad "synfiles.qml not found beside the tests: $QML"
fi

# ── a file with no themed icon still looks like a file ─────────────────────
#
# ⛔ EVERY NON-IMAGE FILE WAS A BLANK SQUARE (61). iconFor() ends at
# Quickshell.iconPath(name, true), which answers "" for an icon the theme has
# not got, and on SynapseOS the theme has not got any of them — Qt never learns
# an icon theme name here, because it picks its base platform theme from
# XDG_CURRENT_DESKTOP and ours says `SynapseOS`/`synui`, neither of which Qt
# knows. An empty source is Image.Null, which draws nothing and reports no
# error, so the window looked fine and the files were simply gone.
#
# ⚠ THIS CANNOT BE TESTED BY LOOKING AT iconFor(). It was correct before and is
# correct now; what was missing is a fallback for when it comes back empty. The
# check is therefore on the DELEGATES: each of the two must draw a FileIcon
# when its Image has nothing (Null) or could not decode what it had (Error).
# Deliberately NOT on Loading — a thumbnail on its way must not flash a page
# first.
if [ -f "$QML" ]; then
    if grep -q "component FileIcon:" "$QML"; then
        ok "there is a drawn file icon to fall back to"
    else
        bad "no FileIcon component — a file the icon theme cannot name draws
        nothing at all, which is the bug 61 fixed"
    fi

    n=$(grep -c "Image.Null" "$QML")
    if [ "$n" -ge 2 ]; then
        ok "both delegates fall back when the icon source is empty ($n)"
    else
        bad "only $n delegate(s) test for Image.Null — the list and the grid
        each need it, or one view shows blanks and the other does not"
    fi

    if grep -q "|| cellIcon.status === Image.Error" "$QML" &&
       grep -q "|| rowIcon.status === Image.Error" "$QML"; then
        ok "a source that will not decode falls back too"
    else
        bad "a delegate treats only the empty case, so a themed icon that
        fails to decode still leaves a blank"
    fi

    # The lettering is what makes the drawn icon better than a generic page,
    # and the one case it must NOT letter is a dotfile: ".bashrc" is not a
    # BASHRC file.
    if grep -q "function extOf(row)" "$QML" && grep -q "dot <= 0" "$QML"; then
        ok "a dotfile is not lettered with its own name"
    else
        bad "extOf does not guard the leading dot — every dotfile would be
        lettered with whatever follows it"
    fi
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
if [ -f "$QML" ] && command -v quickshell >/dev/null 2>&1; then
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
         }' "$QML" > "$WPT/probe.qml"
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

# ── a rename cannot silently eat the extension ─────────────────────────────
#
# Renaming `tux95.png` to `tux95` in the window produced an EXTENSIONLESS PNG.
# The bytes were fine and every listing looked normal; what broke was
# everything downstream that dispatches on the suffix — synui's wallpaper
# picker filters the folder by extension (wppick.c) and its thumbnailer chooses
# a decoder the same way (wpthumb.c), so the picture left the wallpaper list
# and previewed as nothing, with no error raised anywhere.
#
# Two halves, and the second is the one that makes it a rule: stemLen() keeps
# the extension out of the initial SELECTION so typing does not overwrite it,
# and keepExt() puts it back at COMMIT for a name that arrived without one —
# a menu rename, a paste, or a hand-extended selection never touched stemLen.
#
# ⚠ RUN IN A REAL ENGINE, not grepped. These are two functions on root, and a
# grep proves the text is present, not that the file still loads or that the
# functions return anything in particular. ⚠ console.log is INVISIBLE without
# QT_ASSUME_STDERR_HAS_CONSOLE.
if [ -f "$QML" ] && command -v quickshell >/dev/null 2>&1; then
    rq="$T/renameext.qml"
    awk 'BEGIN{RS="\0"} {
            n = match($0, /}[ \t\r\n]*$/)
            printf "%s\n    Timer { running: true; interval: 1; repeat: false; onTriggered: {\n", substr($0,1,n-1)
            printf "        var r = [];\n"
            printf "        function t(label, got, want) { r.push((got === want ? \"PASS \" : \"FAIL \") + label + \" got[\" + got + \"] want[\" + want + \"]\") }\n"
            printf "        t(\"stem-stops-at-ext\", root.stemLen(\"tux95.png\", false), 5);\n"
            printf "        t(\"stem-dir-is-whole\", root.stemLen(\"My.Stuff\", true), 8);\n"
            printf "        t(\"stem-dotfile-is-whole\", root.stemLen(\".bashrc\", false), 7);\n"
            printf "        t(\"stem-no-ext-is-whole\", root.stemLen(\"README\", false), 6);\n"
            printf "        t(\"stem-last-dot-wins\", root.stemLen(\"archive.tar.gz\", false), 11);\n"
            printf "        t(\"keep-untyped-ext\", root.keepExt(\"tux95.png\", \"tux95\", false), \"tux95.png\");\n"
            printf "        t(\"keep-on-new-stem\", root.keepExt(\"tux95.png\", \"penguin\", false), \"penguin.png\");\n"
            printf "        t(\"typed-ext-wins\", root.keepExt(\"tux95.png\", \"tux95.jpg\", false), \"tux95.jpg\");\n"
            printf "        t(\"dir-untouched\", root.keepExt(\"My.Stuff\", \"Other\", true), \"Other\");\n"
            printf "        t(\"no-ext-to-keep\", root.keepExt(\"README\", \"NOTES\", false), \"NOTES\");\n"
            printf "        console.log(\"RENAMEEXT \" + r.join(\" | \"));\n"
            printf "        Qt.quit() } }\n%s", substr($0,n)
         }' "$QML" > "$rq"
    rqrun="$T/rqrun"; mkdir -p "$rqrun"
    rqout=$(XDG_RUNTIME_DIR="$rqrun" QT_QPA_PLATFORM=offscreen \
            QT_ASSUME_STDERR_HAS_CONSOLE=1 \
            timeout 30 quickshell -p "$rq" 2>&1 | grep 'RENAMEEXT' | head -1)
    if [ -z "$rqout" ]; then
        bad "the rename-extension checks did not run at all"
    else
        # ⚠ Assert on the ABSENCE of FAIL *and* the presence of PASS. A run that
        # printed the marker and nothing else would satisfy a bare `grep -qv
        # FAIL` while proving nothing.
        if printf '%s' "$rqout" | grep -q 'FAIL '; then
            bad "a rename would still lose or mangle the extension"
            printf '%s\n' "$rqout" | tr '|' '\n' | grep 'FAIL ' | sed 's/^/        /' >&2
        else
            n=$(printf '%s' "$rqout" | grep -o 'PASS ' | wc -l | tr -d ' ')
            [ "$n" -eq 10 ] \
                && ok "a rename keeps the extension unless one is typed ($n checks)" \
                || bad "expected 10 rename-extension checks, the engine ran $n"
        fi
    fi
else
    echo "  skip  quickshell not installed, cannot check rename extensions"
fi

# ── the Open With flyout survives a pointer resting on it ───────────────────
#
# The static greps above check the WIRING; this drives a real pointer path
# through a replica of it — open the flyout, graze the row below the trigger
# on the way across (unavoidable when the entry sits lower than the row that
# opened it), rest on an entry for 600ms, then click.
#
# ── the type's human name ───────────────────────────────────────────────────
#
# "Plain text document", not "text/plain". Read from shared-mime-info's own
# per-type XML, which is the same string every other file manager on this
# desktop shows — a table written in a front-end would be a second, worse copy
# of a database that is already installed.
DESCD="$T/desc"; mkdir -p "$DESCD"
printf 'x' > "$DESCD/a.txt"
printf 'x' > "$DESCD/b.png"
mkdir -p "$DESCD/sub"

desc_of() {  # desc_of <name>
    "$SYNFILES" --rec list "$DESCD" |
        awk -F'\t' -v n="$1" 'NR>1 && $1==n {print $10}'
}
[ "$(desc_of a.txt)" = "Plain%20text%20document" ] \
    && ok "a listing says what a type IS, not just its mime name" \
    || bad "the description column is '$(desc_of a.txt)'"

[ "$(desc_of sub)" = "Folder" ] \
    && ok "…for a folder too" \
    || bad "a folder's description is '$(desc_of sub)'"

"$SYNFILES" --rec info "$DESCD/b.png" | grep -q '^desc	PNG%20image$'
check "info carries it as well, encoded like every other field with a space" $?

# ⚠ AND IT IS PERCENT-ENCODED LIKE EVERY OTHER FIELD. Descriptions hold spaces
# and parentheses, and in a localised database any byte at all — a raw space is
# harmless in a TSV and a raw TAB is the bug the field-count test exists for,
# so the encoding is asserted rather than assumed from one of them.
raw=$("$SYNFILES" --rec list "$DESCD" | awk -F'\t' 'NR>1 {print $10}' | grep -c ' ' || true)
[ "$raw" = 0 ] && ok "the description column is percent-encoded like the rest" \
              || bad "$raw description(s) carry a raw space"

# ⛔ THE TYPE BECOMES PART OF A PATH. It comes from globs2 today, and a "type"
# of "../../etc/passwd" would be read off disk the day it comes from somewhere
# else. Pointed at a fixture directory that holds nothing, the answer is the
# type's own name — never a file from outside it.
MIMED="$T/mimedb"; mkdir -p "$MIMED/text"
printf '<mime-type type="text/plain"><comment>Fixture text</comment></mime-type>\n' \
    > "$MIMED/text/plain.xml"
[ "$(SYNFILES_MIMEDIR="$MIMED" "$SYNFILES" --rec list "$DESCD" |
     awk -F'\t' 'NR>1 && $1=="a.txt" {print $10}')" = "Fixture%20text" ] \
    && ok "the description database is the one it was pointed at" \
    || bad "SYNFILES_MIMEDIR is ignored — the test cannot pin this"

[ "$(SYNFILES_MIMEDIR="$MIMED" "$SYNFILES" --rec list "$DESCD" |
     awk -F'\t' 'NR>1 && $1=="b.png" {print $10}')" = "image%2Fpng" ] \
    && ok "…and a type it has never heard of is described by its own name" \
    || bad "a missing description is not falling back to the mime type"

# ── what a folder costs on disk ─────────────────────────────────────────────
#
# `du --rec` reports bytes AND disk, and they are different numbers: disk is
# st_blocks * 512, what the tree costs on THIS filesystem. A directory of tiny
# files costs a block each, so the two diverge by any factor you like — which
# is why the hover panel and the Properties dialog lead with the disk figure.
DUD="$T/dudisk"; mkdir -p "$DUD/sub"
for i in 1 2 3 4 5 6 7 8 9 10; do printf 'x' > "$DUD/sub/f$i"; done
du_col() { "$SYNFILES" --rec du "$DUD" | awk -F'\t' 'END {print $'"$1"'}'; }
[ "$(du_col 1)" = 10 ] && ok "du counts the bytes a tree contains" \
                       || bad "du says the tree holds $(du_col 1) bytes, expected 10"
[ "$(du_col 2)" -gt "$(du_col 1)" ] \
    && ok "…and separately what it costs on disk, which is more" \
    || bad "disk ($(du_col 2)) is not above bytes ($(du_col 1)) for ten one-byte files"
[ "$(du_col 3)" = 10 ] && ok "…and how many files that was" \
                       || bad "du counted $(du_col 3) files, expected 10"

# ── the hover panel ─────────────────────────────────────────────────────────
#
# Static checks on the wiring; tests/item_hover_info.qml drives the pointer.
if [ -f "$QML" ]; then
    # ⚠ AT THE WINDOW LEVEL, NOT INSIDE A PANE. A panel parented to the row it
    # describes is clipped by the view that row is in — at the bottom of a list
    # that is a two-pixel sliver — and in split view it is cut off at the
    # pane's edge as well.
    tipline=$(grep -n 'id: tipPanel' "$QML" | head -1 | cut -d: -f1)
    paneline=$(grep -n '^    component Pane:' "$QML" | head -1 | cut -d: -f1)
    if [ -n "$tipline" ] && [ -n "$paneline" ] && [ "$tipline" -lt "$paneline" ]; then
        ok "the hover panel is a window-level item, not a child of a row"
    else
        bad "the hover panel moved inside the pane — a view will clip it"
    fi

    # Both views ask, and both let go. One of the two being wired is the shape
    # this arrives in: the grid is where it matters and the list is where it
    # gets forgotten.
    for ma in rowMa cellMa; do
        blk=$(sed -n "/id: $ma\$/,/^                }/p" "$QML")
        if printf '%s' "$blk" | grep -q 'root.askInfo' &&
           printf '%s' "$blk" | grep -q 'root.dropInfo'; then
            ok "$ma raises and drops the hover panel"
        else
            bad "$ma no longer drives the hover panel"
        fi
    done

    # ⛔ AND EVERY VIEW TAKES IT DOWN WHEN ITS CONTENT MOVES. The panel is
    # anchored to a place on SCREEN; a small scroll keeps the same item under
    # the pointer, so nothing is entered or exited and nothing else would ever
    # close it. tests/item_hover_info.qml scrolls ten pixels for that reason.
    n=$(grep -c 'onContentYChanged: root.hideInfo()' "$QML" || true)
    [ "$n" = 2 ] && ok "both views take the panel down when they scroll" \
                 || bad "$n of 2 views hide the panel on a scroll"

    # A delay, or crossing the grid strobes.
    sed -n '/id: tipDelay/,/^    }/p' "$QML" | grep -qE 'interval: [1-9][0-9][0-9]' \
        && ok "the panel waits before it appears" \
        || bad "the hover panel has no delay — crossing the grid would strobe"

    # ── a folder's size is a WALK ────────────────────────────────────────────
    #
    # ⛔ st_size FOR A DIRECTORY IS THE SIZE OF THE DIRECTORY ENTRY — "890 B"
    # for a tree holding an ISO. The panel's Size row for a folder has to come
    # from `du`, and showing r.size there instead would be a plausible number
    # that is wrong by any factor you like.
    sed -n '/function startTipSize/,/^    }/p' "$QML" > "$T/tipdu"
    grep -q '"du"' "$T/tipdu" \
        && ok "a folder's size on the panel comes from the walk" \
        || bad "the panel is reporting a directory entry's own size again"

    # ⚠ ITS OWN PROCESS. Properties owns duProc and stops it when it closes:
    # sharing means a hover killing the walk behind an open Properties panel,
    # and an open Properties panel stealing the hover's.
    grep -q 'duProc.running' "$T/tipdu" \
        && bad "the hover panel drives Properties' walk — the two will fight" \
        || ok "the hover panel walks in a process of its own"

    # ⛔ AND THE WALK STOPS WITH THE PANEL. A du over a big tree runs for
    # seconds after the panel is gone otherwise, and its records land in
    # whatever the pointer reached next.
    for f in hideInfo dropInfo; do
        sed -n "/function $f/,/^    }/p" "$QML" | grep -q 'stopTipSize' \
            && ok "$f() stops the folder walk" \
            || bad "$f() leaves a du running with no panel to report to"
    done

    # ⚠ AND A RELOAD THROWS THE MEASURED SIZES AWAY. A size cached across a
    # copy, a delete or an F5 is a confident wrong number.
    sed -n '/^        function reload()/,/^        }/p' "$QML" > "$T/tipreload"
    grep -q 'root.tipSizes = ({})' "$T/tipreload" \
        && ok "measured folder sizes go when the listing is re-read" \
        || bad "a measured size can outlive the folder it measured"

    # ⚠ THE DISK FIGURE LEADS. A tree of small files takes far more room than
    # it contains, and that difference is the reason to ask at all.
    grep -q 'root.fmtSize(t.disk, false) + " on disk"' "$QML" \
        && ok "the panel leads with what the folder costs on disk" \
        || bad "the folder size no longer says what it costs on disk"

    # ⚠ "1 files in 1 folders" is the kind of wrong that makes a careful number
    # look careless. Both counters go through fmtMany, which is where the s is.
    n=$(grep -c 'fmtCount(t\.\(files\|dirs\))' "$QML" || true)
    [ "$n" = 0 ] && ok "both places that count files and folders pluralise" \
                 || bad "$n counter(s) print a bare number and a fixed plural"
fi

# ── the hover panel, with a real pointer ────────────────────────────────────
#
# ⚠ Qt 6's qmltestrunner, not /usr/bin/qmltestrunner, which is Qt 5's: it
# rejects the unversioned imports and prints the reason to a stderr the runner
# discards, so a Qt 5 run looks exactly like a test that found nothing wrong.
HOVER2_QML="$(dirname "$0")/item_hover_info.qml"
for c in /usr/lib/qt6/bin/qmltestrunner /usr/lib/qt6/bin/qmltestrunner6; do
    [ -x "$c" ] && { QMLTEST2=$c; break; }
done
if [ -n "${QMLTEST2:-}" ] && [ -f "$HOVER2_QML" ]; then
    h2run="$T/hoverrun2"; mkdir -p "$h2run"
    h2out=$(XDG_RUNTIME_DIR="$h2run" QT_QPA_PLATFORM=offscreen \
            timeout 90 "$QMLTEST2" -input "$HOVER2_QML" 2>&1)
    h2pass=$(printf '%s' "$h2out" | grep -c '^PASS ' || true)
    if printf '%s' "$h2out" | grep -q '^FAIL'; then
        bad "the hover panel misbehaves under a real pointer"
        printf '%s\n' "$h2out" | grep -A2 '^FAIL' | sed 's/^/        /' >&2
    elif [ "$h2pass" -lt 8 ]; then
        bad "the hover panel test did not run ($h2pass passes)"
        printf '%s\n' "$h2out" | tail -5 | sed 's/^/        /' >&2
    else
        ok "the hover panel waits, follows and lets go ($h2pass checks)"
    fi
else
    echo "  skip  Qt 6 qmltestrunner not installed, cannot check the hover panel"
fi

# ⚠ Qt 6's qmltestrunner, not /usr/bin/qmltestrunner, which is Qt 5's: it
# rejects the unversioned imports and prints the reason to a stderr the runner
# discards, so a Qt 5 run looks exactly like a test that found nothing wrong.
HOVER_QML="$(dirname "$0")/ctx_flyout_hover.qml"
QMLTEST=""
for c in /usr/lib/qt6/bin/qmltestrunner /usr/lib/qt6/bin/qmltestrunner6; do
    [ -x "$c" ] && { QMLTEST=$c; break; }
done
if [ -n "$QMLTEST" ] && [ -f "$HOVER_QML" ]; then
    hrun="$T/hoverrun"; mkdir -p "$hrun"
    hout=$(XDG_RUNTIME_DIR="$hrun" QT_QPA_PLATFORM=offscreen \
           timeout 60 "$QMLTEST" -input "$HOVER_QML" 2>&1)
    hpass=$(printf '%s' "$hout" | grep -c '^PASS ' || true)
    if printf '%s' "$hout" | grep -q '^FAIL'; then
        bad "the Open With flyout closes under a resting pointer"
        printf '%s\n' "$hout" | grep -A2 '^FAIL' | sed 's/^/        /' >&2
    elif [ "$hpass" -lt 5 ]; then
        # A Qt 5 runner, or an import error, exits without running anything.
        bad "the flyout hover test did not run ($hpass passes)"
        printf '%s\n' "$hout" | tail -5 | sed 's/^/        /' >&2
    else
        ok "the flyout stays open under a resting pointer ($hpass checks)"
    fi
else
    echo "  skip  Qt 6 qmltestrunner not installed, cannot check flyout hover"
fi

unset SYNFILES_CONFIG

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
