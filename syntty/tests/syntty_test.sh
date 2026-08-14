#!/usr/bin/env bash
#
# syntty_test.sh — the parser and the grid, with no window anywhere.
#
# ⚠ THE RULE FOR THIS FILE: it must run on a machine with no seat, no
# compositor, no display and no fonts. That is what stage 1 exists for. The
# moment a test here needs a screen, the thing it is testing has moved out of
# this stage and the test belongs somewhere else.
#
# Everything is a golden output: a byte stream in, a screen out, compared
# literally. A parser tested by asserting on its internal state passes while
# producing a screen nobody would accept; a parser tested on the screen it
# draws cannot.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

ST=${1:-./build/syntty}
[ -x "$ST" ] || { echo "not executable: $ST" >&2; exit 1; }
ST=$(readlink -f "$ST")

pass=0 fail=0
ok()    { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()   { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

echo "syntty tests — $ST"

# Feed a stream, print the screen with trailing blank lines removed so the
# expected output in each test is what a person would write down.
screen() {  # screen <cols> <rows> <stream-as-printf-format>
    local cols=$1 rows=$2; shift 2
    printf "$1" | "$ST" --cols="$cols" --rows="$rows" dump - | sed -e 's/[[:space:]]*$//' \
        | awk 'BEGIN{n=0} {L[n++]=$0} END{last=-1; for(i=0;i<n;i++) if(L[i]!="") last=i; for(i=0;i<=last;i++) print L[i]}'
}

expect() {  # expect <name> <expected> <actual>
    if [ "$2" = "$3" ]; then ok "$1"
    else
        bad "$1"
        printf '        expected: %s\n' "$(printf '%s' "$2" | tr '\n' '|')" >&2
        printf '        actual:   %s\n' "$(printf '%s' "$3" | tr '\n' '|')" >&2
    fi
}

# ── the plainest thing there is ─────────────────────────────────────────────

expect "plain text lands on the screen" \
    "hello" "$(screen 20 3 'hello')"

expect "a newline moves down, a carriage return moves back" \
    "$(printf 'one\ntwo')" "$(screen 20 3 'one\r\ntwo')"

# ⚠ LF alone does NOT return to column 0. A parser that treats it as CRLF looks
# right against shell output — which sends CR too — and staircases the moment a
# program sends a bare LF.
expect "a bare LF drops a line without returning to column 0" \
    "$(printf 'abc\n   def')" "$(screen 20 3 'abc\ndef')"

# ── wrapping ────────────────────────────────────────────────────────────────

expect "text wraps at the right edge" \
    "$(printf 'abcde\nfgh')" "$(screen 5 3 'abcdefgh')"

# ⚠ The one everybody gets wrong: a line that EXACTLY fills the width must not
# leave a blank row behind it. The cursor parks on the last column and the wrap
# happens when the next character arrives.
expect "a line that exactly fills the width leaves no blank row" \
    "$(printf 'abcde\nxyz')" "$(screen 5 3 'abcde\r\nxyz')"

# ...and the cursor really is still on that line until something else arrives:
# a CR after an exactly-full line returns to ITS column 0, not the next row's.
expect "an exactly-full line has not wrapped yet" \
    "xyzde" "$(screen 5 3 'abcde\rxyz')"

expect "an exactly-full line still wraps the next character" \
    "$(printf 'abcde\nf')" "$(screen 5 3 'abcdef')"

# ── cursor movement ─────────────────────────────────────────────────────────

expect "CUP puts the cursor where it says" \
    "$(printf '\n\n  X')" "$(screen 10 4 '\033[3;3HX')"

expect "CUP with no parameters homes the cursor" \
    "Xbc" "$(screen 10 3 'abc\033[HX')"

expect "CUF and CUB move by columns" \
    "a  b" "$(screen 10 3 'a\033[2Cb')"

expect "CUU cannot walk off the top" \
    "X" "$(screen 10 3 '\033[20AX')"

# A parameter of zero means ONE for a movement, and that is not the same rule
# as CUP's, where zero means the first row.
expect "CUB with a zero parameter still moves one column" \
    "abb" "$(screen 10 3 'abc\033[0Db')"

# ── erasing ─────────────────────────────────────────────────────────────────

expect "EL 0 erases from the cursor to the end of the line" \
    "ab" "$(screen 10 3 'abcdef\033[3G\033[0K')"

expect "EL 1 erases from the start to the cursor" \
    "$(printf '   def')" "$(screen 10 3 'abcdef\033[3G\033[1K')"

expect "ED 2 clears everything" \
    "" "$(screen 10 3 'one\r\ntwo\r\nthree\033[2J')"

# ── scrolling and the scrollback ────────────────────────────────────────────

expect "output scrolls the screen up" \
    "$(printf 'two\nthree')" "$(screen 10 2 'one\r\ntwo\r\nthree')"

# The row that left the top is not gone — it is history, and it is the reason
# the scrollback exists at all.
sb=$(printf 'one\r\ntwo\r\nthree' | "$ST" --cols=10 --rows=2 --scrollback-too dump - | sed -e 's/[[:space:]]*$//' | head -1)
expect "the row that scrolled off is in the scrollback" "one" "$sb"

# ⚠ A scroll REGION is a pane, not history. A full-screen program that scrolls
# its middle must not fill the user's scrollback with the pane's discards.
sb=$(printf '\033[2;3r\033[2;1Ha\r\nb\r\nc\r\nd' | "$ST" --cols=10 --rows=4 --scrollback-too --stats dump - 2>&1 >/dev/null | grep '^scrollback')
expect "a scroll region does not feed the scrollback" "scrollback    0 rows" "$sb"

expect "IL inserts a blank line and pushes the rest down" \
    "$(printf 'one\n\ntwo')" "$(screen 10 4 'one\r\ntwo\033[2;1H\033[L')"

expect "DL deletes a line and pulls the rest up" \
    "$(printf 'one\nthree')" "$(screen 10 4 'one\r\ntwo\r\nthree\033[2;1H\033[M')"

# ── insert and delete within a line ─────────────────────────────────────────

expect "ICH opens a gap at the cursor" \
    "$(printf 'ab  cd')" "$(screen 10 3 'abcd\033[3G\033[2@')"

expect "DCH closes the gap" \
    "ad" "$(screen 10 3 'abcd\033[2G\033[2P')"

expect "ECH blanks in place without moving the rest" \
    "$(printf 'a  d')" "$(screen 10 3 'abcd\033[2G\033[2X')"

# ── UTF-8 ───────────────────────────────────────────────────────────────────

expect "a two-byte codepoint is one cell" \
    "héllo" "$(screen 10 3 'h\303\251llo')"

expect "a four-byte codepoint survives" \
    "€𝄞" "$(screen 10 3 '\342\202\254\360\235\204\236')"

# A wide glyph takes two cells, so five of them fill ten columns exactly.
expect "a wide glyph occupies two columns" \
    "$(printf '日本\nX')" "$(screen 4 3 '\346\227\245\346\234\254X')"

# ⚠ A double-width glyph never straddles the edge: it wraps whole, or every
# consumer downstream has to understand half a character.
# In three columns after one narrow character there are exactly two left, so it
# FITS — the wrap case needs the edge actually reached, which is the next one.
expect "a wide glyph takes the last two columns it fits in" \
    "a日" "$(screen 3 3 'a\346\227\245')"

expect "a wide glyph wraps whole rather than splitting" \
    "$(printf 'ab\n日')" "$(screen 3 3 'ab\346\227\245')"

# A bad byte becomes one replacement character and the NEXT byte is
# reconsidered — a decoder that swallowed it turns one bad byte into a lost word.
expect "an invalid byte costs exactly one character" \
    "$(printf 'a\357\277\275b')" "$(screen 10 3 'a\377b')"

# ── the split-feed contract ─────────────────────────────────────────────────
#
# The parser must produce an identical screen however the stream is chopped up.
# At 256 KB reads a split lands mid-escape constantly; at 80 bytes it never
# does, which is why this is asserted rather than assumed.

cat > "$T/stream" <<'EOF'
EOF
printf 'plain \033[1;31mred bold\033[0m then \033[38;2;10;20;30mrgb\033[m done\r\n' > "$T/stream"
printf 'wide \346\227\245\346\234\254 and \303\251\303\250\r\n\033[3;5Hmoved\033[K\r\n' >> "$T/stream"
printf '\033]0;a title\007\033[2;4rregion\r\n\033[999;999Hclamped' >> "$T/stream"

whole=$("$ST" --cols=40 --rows=10 dump "$T/stream")
same=1
for n in 1 2 3 5 7 13 64; do
    part=$("$ST" --cols=40 --rows=10 --split="$n" dump "$T/stream")
    [ "$whole" = "$part" ] || { same=0; echo "        differs at chunk size $n" >&2; }
done
check "the screen is identical however the stream is split" $((1 - same))

# The same, for the one case a naive decoder always gets wrong: a split INSIDE
# a multi-byte codepoint.
a=$(printf 'x\346\227\245y' | "$ST" --cols=10 --rows=2 dump -)
b=$(printf 'x\346\227\245y' | "$ST" --cols=10 --rows=2 --split=2 dump -)
expect "a split inside a UTF-8 sequence changes nothing" "$a" "$b"

# ── styles ──────────────────────────────────────────────────────────────────

styles() { printf "$1" | "$ST" --cols=20 --rows=3 --stats dump - 2>&1 >/dev/null | awk '/^styles/{print $2}'; }

expect "plain text uses exactly one style" "1" "$(styles 'hello world')"
expect "a colour change interns a second style" "2" "$(styles 'a\033[31mb')"
# Interning is the whole reason a cell can be 8 bytes: the same style used
# twice must not become two entries.
expect "the same style twice is still one entry" "2" "$(styles 'a\033[31mb\033[0mc\033[31md')"
expect "SGR 0 returns to the default style, not a new one" "2" "$(styles '\033[31ma\033[0mb')"

# ⚠ 38;5;N and 38;2;R;G;B take their arguments from the same parameter list. A
# switch that ignored them reads 5 and 196 as "blink" and "bright white".
expect "256-colour and RGB do not leak into the next attribute" "3" \
    "$(styles 'a\033[38;5;196mb\033[38;2;1;2;3mc')"

# ── what it could not do ────────────────────────────────────────────────────
#
# Unhandled sequences are COUNTED. In a stage with no renderer, "drew nothing"
# and "was never asked" are different bugs and nothing else can tell them apart.

n=$(printf '\033[?1049h\033[>4;2m' | "$ST" dump - --stats 2>&1 >/dev/null \
    | awk '/^unhandled/{print $2}')
[ -n "$n" ] && [ "$n" -ge 2 ] \
    && ok "what the parser cannot do yet is counted, not ignored ($n)" \
    || bad "unhandled sequences are being silently dropped (got '${n:-nothing}')"

t=$(printf '\033]0;hello title\007' | "$ST" dump - --stats 2>&1 >/dev/null | grep '^title')
expect "an OSC title is kept" "title         hello title" "$t"

# ── the memory claim ────────────────────────────────────────────────────────
#
# The design says an 8-byte cell and a trimmed scrollback. Both are assertions
# a build can check, and neither survives being left to a comment.

sz=$("$ST" about | awk '/^  cell/{print $2}')
expect "a cell is 8 bytes" "8" "$sz"

# Ten thousand short lines through a 200-column grid. Untrimmed that is
# 200 x 10000 x 8 = 16 MB; trimmed to the ~6 columns each line uses it is well
# under 2 MB. The threshold is deliberately loose — what it is testing is that
# the trimming happens at all.
yes 'hi' | head -10000 | sed 's/$/\r/' > "$T/lines"
bytes=$("$ST" --cols=200 --rows=24 --scrollback=10000 dump "$T/lines" --stats 2>&1 >/dev/null \
        | awk '/^memory/{print $2}')
if [ "$bytes" -lt 4000000 ]; then
    ok "scrollback rows are trimmed to their used width ($((bytes / 1024)) KB for 10k lines)"
else
    bad "scrollback is storing full-width rows ($((bytes / 1024)) KB for 10k lines)"
fi

# ...and the scrollback is bounded. A ring that grows is a memory leak with a
# polite name.
b1=$("$ST" --cols=200 --scrollback=500 dump "$T/lines" --stats 2>&1 >/dev/null | awk '/^memory/{print $2}')
b2=$("$ST" --cols=200 --scrollback=500 dump "$T/lines" --stats 2>&1 >/dev/null | awk '/^memory/{print $2}')
[ "$b1" = "$b2" ] && [ "$b1" -lt 1000000 ] \
    && ok "the scrollback is bounded by its limit" \
    || bad "the scrollback grew past its limit ($b1 bytes)"

# ── the pty ─────────────────────────────────────────────────────────────────
#
# A real fork, a real pty, a real program. Nothing is mocked: the point of this
# subcommand is that the whole path from exec to screen is exercised without a
# window anywhere near it.

out=$("$ST" --cols=20 --rows=4 run -- /bin/echo hello | head -1 | sed -e 's/[[:space:]]*$//')
expect "a command runs on a real pty and its output reaches the grid" "hello" "$out"

"$ST" run -- /bin/sh -c 'exit 3' >/dev/null 2>&1
expect "the child's exit status is passed through" "3" "$?"

# The child gets a TERM it can act on, or every program that checks will decide
# it is talking to a teletype.
term=$("$ST" --cols=20 --rows=4 run -- /bin/sh -c 'printf %s "$TERM"' | head -1 | sed -e 's/[[:space:]]*$//')
expect "the child is told what terminal it is on" "xterm-256color" "$term"

# The window size reaches the child, which is what every full-screen program
# asks for before it draws anything.
size=$("$ST" --cols=77 --rows=9 run -- /bin/sh -c 'stty size' | head -1 | sed -e 's/[[:space:]]*$//')
expect "the child sees the grid's size" "9 77" "$size"

# ── the benchmark ───────────────────────────────────────────────────────────
#
# It is asserted to WORK, not to be fast: a threshold here would fail on
# somebody else's slower machine and teach everyone to ignore the suite. The
# number itself belongs in a run somebody reads.

seq 1 20000 > "$T/bench.txt"
if "$ST" bench "$T/bench.txt" --runs=2 | grep -q 'MB/s'; then
    ok "bench reports a throughput number"
else
    bad "bench reports a throughput number"
fi

rate=$("$ST" bench "$T/bench.txt" --runs=3 | awk '/^best/{print $4}')
ok "throughput on this machine: ${rate} MB/s"

# ── damage tracking draws the SAME PIXELS ───────────────────────────────────
#
# ⚠ THE TEST THIS FEATURE WAITED FOR. Damage tracking was recorded as "not
# done" for a whole stage precisely because it cannot be checked by looking at
# the finished screen: drawn fully, the screen is right either way. What goes
# wrong is a row that changed and was NOT marked, which keeps its old pixels —
# and stale pixels look like memory corruption, not like a missed update.
#
# `damage-check` draws the same stream twice: once fed whole and painted
# entirely, once fed in chunks and painted only where the grid reported damage.
# Then it compares the two buffers BYTE FOR BYTE. Any mutation site in grid.c
# that forgets to mark its row fails here and names the cell.
#
# ⚠ SMALL CHUNKS ARE HARSHER. A missed mark that happens to be covered because
# a later chunk redrew the same row passes at 64 KB and fails at 3 bytes.
dmg() { printf "$1" | "$ST" --cols="${2:-40}" --rows="${3:-12}" \
            damage-check - --split="${4:-3}" 2>&1; }

echo "$(seq 1 200)" | "$ST" --cols=80 --rows=24 damage-check - --split=7 2>&1 \
    | grep -q '^identical'
check "scrolling output paints the same pixels either way" $?

# Every kind of mutation, at three bytes a feed. Each of these is a different
# family of grid operation, and each is a different place to forget the mark.
dmg 'plain\r\n\033[31mred\033[0m\r\n\033[2;5H\033[42mjump\033[0m\r\n' | grep -q '^identical'
check "...and so do cursor jumps and colour changes" $?

dmg '\033[3;10r\033[5;1Hin a scroll region\r\nsecond\r\nthird\r\n' | grep -q '^identical'
check "...and a scroll region, which moves rows without changing them" $?

dmg 'aaaa\r\n\033[2J\033[1;1Hafter a clear\r\n\033[K\033[1Kboth erases\r\n' | grep -q '^identical'
check "...and erase-display and erase-line" $?

dmg '\033[3;1Hxxx\033[4L\033[2Minsert and delete lines\r\n' | grep -q '^identical'
check "...and inserting and deleting whole lines" $?

dmg 'abcdefgh\033[3D\033[4@\033[2Pinsert and delete chars\r\n' | grep -q '^identical'
check "...and inserting and deleting characters" $?

dmg '\xe6\x97\xa5\xe6\x9c\xac wide\r\n\033[2;3H\xe8\xaa\x9e over it\r\n' | grep -q '^identical'
check "...and wide glyphs, whose tail column belongs to the head" $?

# ⚠ THE CURSOR IS DAMAGE THE GRID CANNOT REPORT. Moving it changes no cell, so
# nothing in grid.c marks anything — but it changes two rows on screen, the one
# it left and the one it arrived at. Left out, the cursor smears a trail of
# itself down the window, which is the most visible form of this bug.
printf '\033[1;1Ha\033[5;1H\033[9;1H\033[3;1H' \
    | "$ST" --cols=20 --rows=12 damage-check - --split=1 2>&1 | grep -q '^identical'
check "a cursor that only MOVES still repaints the rows it touched" $?

# And it must actually be SAVING something, or it is complexity for nothing.
#
# ⚠ The percentage is the LAST field, and reading the wrong one made this pass
# vacuously: `$6` is the word "possible", `int("possible")` is 0, and 0 is
# comfortably under any threshold. A test that cannot fail is worse than no
# test, because it is counted. Hence the emptiness check on `pct` as well —
# a field that stops being numeric must break this, not satisfy it.
pct=$(dmg '\033[2;2Ha\033[4;2Hb\033[6;2Hc\033[8;2Hd\033[10;2He' 40 12 2 \
      | awk '/^repaints/{p=$NF; gsub(/[()%]/,"",p); print p}')
case "$pct" in
    ''|*[!0-9.]*) bad "scattered edits repaint a minority of rows (unparsable: '$pct')" ;;
    *) awk -v p="$pct" 'BEGIN{exit !(p > 0 && p < 60)}' \
           && ok "scattered edits repaint a minority of rows (${pct}%)" \
           || bad "scattered edits repaint a minority of rows (${pct}%)" ;;
esac

# ── the kitty keyboard protocol ─────────────────────────────────────────────
#
# Table stakes, not a win: programs assume it now, and it fixes something
# genuinely broken. In the legacy encoding Ctrl+I IS Tab, Ctrl+M IS Enter and
# Ctrl+[ IS Escape — the same bytes — and nothing can report a key release at
# all. Editors have guessed from timing for forty years.
#
# Only the PARSER half is testable here: what the program sends the terminal,
# and what the terminal answers. The other half — turning a real key press into
# the negotiated encoding — needs a seat and a person, and input is never
# synthesised in this suite.
#
# ⚠ kbd() CAPTURES, and is never piped straight into `grep -q`. That is the
# trap documented at the top of this file, and it caught this section too: grep
# -q exits at the first match and closes the pipe, the producer dies of SIGPIPE,
# and under `pipefail` the pipeline reports 141 for a test that MATCHED. It
# passes on the ordinary build, where all the output reaches the pipe buffer
# before grep can act, and fails EVERY time under ASan, which is slow enough to
# lose the race. One assertion here did exactly that.
kbd() { local out; out=$(printf "$1" | "$ST" dump - --stats 2>&1); printf '%s\n' "$out"; }

[ "$(kbd '\033[?u' | awk '/^kbd flags/{print $3}')" = 0 ] \
    && ok "the base state is the legacy encoding" \
    || bad "the base state is the legacy encoding"

r=$(kbd '\033[?u')
echo "$r" | grep -q 'reply *ESC\[?0u'
check "...and a query is answered, not ignored" $?

[ "$(kbd '\033[>15u\033[?u' | awk '/^kbd flags/{print $3}')" = 15 ] \
    && ok "a program can push the enhancements it wants" \
    || bad "a program can push the enhancements it wants"

[ "$(kbd '\033[>15u\033[<1u\033[?u' | awk '/^kbd flags/{print $3}')" = 0 ] \
    && ok "...and popping returns to what was there before" \
    || bad "...and popping returns to what was there before"

# ⚠ POPPING AN EMPTY STACK IS A NO-OP, NOT AN ERROR. It is exactly what a
# program does when it pops on exit having never pushed on entry — or when it
# is killed and the shell's own reset runs. The base state is the right place
# to land, and anything else strands the shell in a mode it cannot use.
[ "$(kbd '\033[<9u\033[?u' | awk '/^kbd flags/{print $3}')" = 0 ] \
    && ok "popping past the bottom lands on the legacy encoding" \
    || bad "popping past the bottom lands on the legacy encoding"

# `=` sets: mode 1 replaces, 2 adds bits, 3 clears them.
[ "$(kbd '\033[>4u\033[=1;2u\033[?u' | awk '/^kbd flags/{print $3}')" = 5 ] \
    && ok "mode 2 adds bits to what is already set" \
    || bad "mode 2 adds bits to what is already set"

[ "$(kbd '\033[>5u\033[=4;3u\033[?u' | awk '/^kbd flags/{print $3}')" = 1 ] \
    && ok "mode 3 clears them" \
    || bad "mode 3 clears them"

[ "$(kbd '\033[>5u\033[=8;1u\033[?u' | awk '/^kbd flags/{print $3}')" = 8 ] \
    && ok "mode 1 replaces them outright" \
    || bad "mode 1 replaces them outright"

# ⚠ THE ANSWER IS WHAT WE IMPLEMENT, NEVER WHAT WAS ASKED FOR. A terminal that
# echoes the request back claims every enhancement, and the program then sends
# encodings it will never be able to read — which is worse than admitting to
# none, because the failure lands in the program's input handling rather than
# at the negotiation.
[ "$(kbd '\033[>255u\033[?u' | awk '/^kbd flags/{print $3}')" -le 31 ] \
    && ok "asking for enhancements we lack does not get them claimed back" \
    || bad "asking for enhancements we lack does not get them claimed back"

# ⚠ THE PREFIX IS THE WHOLE SEQUENCE. `CSI > 1 u`, `CSI < 1 u` and `CSI = 1 u`
# are three different operations one byte apart, and this is the same trap that
# made ESC[>4;2m apply underline and dim to everything — see csi_dispatch.
[ "$(kbd '\033[>1u\033[>2u\033[?u' | awk '/^kbd flags/{print $3}')" = 2 ] \
    && ok "a second push stacks rather than merging" \
    || bad "a second push stacks rather than merging"

# A stack has a bottom AND a top. Pushing past it must not scribble past the
# array; the overflow is counted so it cannot hide.
kbd "$(printf '\033[>1u%.0s' $(seq 1 40))\033[?u" >/dev/null 2>&1
[ $? -eq 0 ] && ok "forty pushes do not run off the end of the stack" \
             || bad "forty pushes do not run off the end of the stack"

# The reply is bytes owed to the CHILD, so nothing may reach the screen.
[ -z "$(printf '\033[?u' | "$ST" dump - | tr -d ' \n')" ] \
    && ok "an answer to the program is not drawn on the screen" \
    || bad "an answer to the program is not drawn on the screen"

# ── the font lookup, and the cache that exists to skip it ───────────────────
#
# ⚠ THE ONE PART OF THIS FILE THAT NEEDS SOMETHING INSTALLED. The rule at the
# top says these tests run with no fonts, and that rule is kept by SKIPPING
# here rather than by failing: a machine with no monospace font is a real
# machine (a container, a build host), and a suite that goes red on it teaches
# people to ignore it. What is asserted is the CACHE's behaviour, which is the
# part with logic in it.
#
# XDG_CACHE_HOME is redirected into the temp dir for every one of these. A test
# suite that writes to the developer's real cache is one that behaves
# differently the second time it is run, and that is exactly the bug the cache
# itself can have.
FC="$T/cache"
mkdir -p "$FC"
fontrun() { XDG_CACHE_HOME="$FC" "$ST" font "$@" 2>&1; }

if ! fontrun >/dev/null 2>&1; then
    echo "  skip  font lookup (no font on this machine — see the rule at the top)"
else
    CACHEFILE="$FC/syntty/fonts.v1"

    # COLD then WARM. The claim is that the second start does not ask
    # fontconfig; if it does, every start pays the lookup and the cache is
    # decoration. Asserted on which SOURCE answered, not on a duration —
    # a timing threshold here would be flaky on a loaded machine.
    rm -rf "$FC"; mkdir -p "$FC"
    fontrun | grep -q 'via fontconfig'
    check "the first start asks fontconfig" $?

    fontrun | grep -q 'via cache'
    check "...and the second start does not" $?

    # The glyphs must be the SAME ones. A cache that answers instantly with a
    # different font is worse than no cache, and both runs print the file.
    a=$(fontrun | awk '/^file/{print $2}')
    rm -rf "$FC"; mkdir -p "$FC"
    b=$(fontrun | awk '/^file/{print $2}')
    [ -n "$a" ] && [ "$a" = "$b" ] \
        && ok "...and answers with the same font the lookup would have" \
        || bad "...and answers with the same font the lookup would have"

    # ⚠ THE INVALIDATION, which is what makes the cache honest rather than
    # merely fast. A blindly-trusted cache survives the font being upgraded,
    # renamed or removed, and the failure lands weeks after the package upgrade
    # that caused it, looking like a broken terminal.
    fontrun >/dev/null           # ensure it is warm and valid
    real=$(fontrun | awk '/^file/{print $2}')

    printf 'syntty-fonts-1\nmonospace\t0\t596428\t1785569937\t%s/gone.ttf\n' "$T" > "$CACHEFILE"
    fontrun | grep -q 'via fontconfig'
    check "a cache naming a font that is gone is not believed" $?

    printf 'syntty-fonts-1\nmonospace\t0\t999999\t1785569937\t%s\n' "$real" > "$CACHEFILE"
    fontrun | grep -q 'via fontconfig'
    check "a cache whose font changed size is not believed" $?

    echo "not a cache at all" > "$CACHEFILE"
    fontrun | grep -q 'via fontconfig'
    check "a cache with the wrong header is not believed" $?

    # ...and having disbelieved it, it must REWRITE it. A cache that is
    # correctly rejected every time is a cache that never works again.
    fontrun | grep -q 'via cache'
    check "...and every rejection repairs the cache" $?

    # A face that loads and rasterises nothing looks identical, from the
    # outside, to one that works. `font` fails rather than reporting success
    # with an all-zero atlas — see cmd_font.
    fontrun | grep -qE '^ink +[1-9]'
    check "the rasteriser produced actual ink, not an empty atlas" $?

    # The cell box is what the whole grid is laid out on. Zero or negative
    # would divide by zero in the renderer, far from here.
    fontrun | awk '/^cell/{split($2,d,"x"); exit !(d[1] > 0 && d[2] > 0)}'
    check "the cell box has a positive width and height" $?

    # A font nobody has must not silently become a font somebody has WITHOUT
    # saying so — fontconfig always matches something, so this asserts the
    # request reached it rather than that it failed.
    fontrun --font=NoSuchFontExistsAnywhere1234 | grep -q '^file'
    check "an unknown family still resolves (fontconfig always substitutes)" $?

    # ── the renderer ────────────────────────────────────────────────────────
    #
    # Stage 1 could assert on text because its output WAS text. A renderer's
    # output is pixels, and "it returned without crashing" passes on an
    # all-black screen — so `render` prints what is actually in the buffer:
    # the share of pixels that differ from the background, the number of
    # distinct colours, and (with --probe) the colour of one named cell.
    #
    # ⚠ Everything here goes through --probe or those counters rather than
    # through an image library, because this file may not gain a dependency
    # that a build host will not have.
    rend() { XDG_CACHE_HOME="$FC" "$ST" "$@" render - --no-cursor 2>&1; }

    ink() { rend "$@" | awk '/^ink/{print int($2)}'; }

    # The buffer is exactly the cells times the cell box. A renderer that gets
    # this wrong draws a correct screen into the wrong-sized window, which on
    # a real compositor is a protocol error rather than a visible bug.
    cw=$(fontrun | awk '/^cell/{split($2,d,"x"); print d[1]}')
    chh=$(fontrun | awk '/^cell/{split($2,d,"x"); print d[2]}')
    got=$(printf 'x' | XDG_CACHE_HOME="$FC" "$ST" --cols=10 --rows=3 render - --no-cursor 2>&1 \
          | awk '/^size/{print $2}')
    [ "$got" = "$((cw * 10))x$((chh * 3))" ] \
        && ok "the buffer is exactly cols x rows of the cell box" \
        || bad "the buffer is exactly cols x rows of the cell box (got $got)"

    # EVERY cell is painted, including the blank ones. `width == 0` means both
    # "erased" and "the tail of a wide glyph", and skipping on width alone
    # skipped every blank cell on the screen — they kept whatever was in the
    # buffer, which showed up as black rectangles rather than background.
    n=$(printf 'ab' | XDG_CACHE_HOME="$FC" "$ST" --cols=10 --rows=2 render - --no-cursor 2>&1 \
        | awk '/^cells/{print $2}')
    [ "$n" = 20 ] \
        && ok "every cell is painted, blanks included" \
        || bad "every cell is painted, blanks included (drew $n of 20)"

    # ...and a WIDE glyph paints two columns from one cell, so its tail is
    # skipped and the count drops by exactly one per wide character.
    n=$(printf '\xe6\x97\xa5' | XDG_CACHE_HOME="$FC" "$ST" --cols=10 --rows=1 render - --no-cursor 2>&1 \
        | awk '/^cells/{print $2}')
    [ "$n" = 9 ] \
        && ok "...and a wide glyph's tail column is not painted twice" \
        || bad "...and a wide glyph's tail column is not painted twice (drew $n of 9)"

    # An empty screen has no ink.
    #
    # ⚠ THIS ONE PROVES LESS THAN IT LOOKS. `ink` is measured against the
    # buffer's first pixel, so a renderer that paints NOTHING AT ALL scores a
    # perfect 0% here — which is exactly what happened while blank cells were
    # being skipped. It stays because it catches garbage being painted into
    # cells nothing asked for; the two cell COUNTS above are what actually
    # caught that bug, and they are the ones to trust.
    [ "$(printf '' | ink --cols=20 --rows=4)" = 0 ] \
        && ok "an empty screen is entirely background" \
        || bad "an empty screen is entirely background"

    # ...and text has some. Both directions matter: the first check alone
    # passes on a renderer that draws nothing at all.
    [ "$(printf 'Hello' | ink --cols=20 --rows=4)" -gt 0 ] \
        && ok "...and text puts ink on it" \
        || bad "...and text puts ink on it"

    probe() { XDG_CACHE_HOME="$FC" "$ST" --cols=8 --rows=1 render - --no-cursor \
                  --probe="$1" 2>/dev/null | awk '{print $3}'; }

    # THE COLOUR SEMANTICS, asserted on named cells. An aggregate check passes
    # on a screen that reversed the wrong cell.
    dfl=$(printf 'A' | probe 0,0)
    rev=$(printf '\033[7mA' | probe 0,0)
    red=$(printf '\033[41mA' | probe 0,0)

    [ -n "$dfl" ] && [ "$dfl" != "$rev" ] \
        && ok "reverse video swaps the cell's colours" \
        || bad "reverse video swaps the cell's colours"

    [ "$red" = "CC5555" ] \
        && ok "an indexed background reaches the pixels (ESC[41m)" \
        || bad "an indexed background reaches the pixels (ESC[41m, got $red)"

    # 24-bit colour is a different encoding path from the 256 indexed ones,
    # and it is the one that passes the value through untouched.
    tru=$(printf '\033[48;2;18;52;86mA' | probe 0,0)
    [ "$tru" = "123456" ] \
        && ok "a 24-bit background reaches the pixels exactly" \
        || bad "a 24-bit background reaches the pixels exactly (got $tru)"

    # The cursor is drawn, and --no-cursor really turns it off — every
    # assertion above depends on that flag working.
    a=$(printf '' | ink --cols=8 --rows=1)
    b=$(printf '' | XDG_CACHE_HOME="$FC" "$ST" --cols=8 --rows=1 render - 2>&1 | awk '/^ink/{print int($2)}')
    [ "$a" = 0 ] && [ "$b" -gt 0 ] \
        && ok "the cursor is drawn, and --no-cursor turns it off" \
        || bad "the cursor is drawn, and --no-cursor turns it off"

    # A PPM a person can open, and that `cmp` can compare.
    XDG_CACHE_HOME="$FC" "$ST" --cols=8 --rows=2 render /dev/null --out="$T/r.ppm" >/dev/null 2>&1
    head -c 2 "$T/r.ppm" | grep -q 'P6'
    check "--out writes a PPM" $?

    sz=$(stat -c %s "$T/r.ppm" 2>/dev/null)
    want=$(( cw * 8 * chh * 2 * 3 ))
    [ -n "$sz" ] && [ "$sz" -gt "$want" ] \
        && ok "...whose payload is three bytes per pixel" \
        || bad "...whose payload is three bytes per pixel"

    # ⚠ FALLBACK. The chosen monospace font covers Latin and little else — on
    # this machine "monospace" is Noto Sans Mono, which has no CJK at all, so
    # 日本語 rasterised as three empty boxes: correctly SPACED and completely
    # invisible, which is the worst shape a bug can have. Skipped rather than
    # failed where no font on the machine covers it, for the same reason as
    # the rest of this section.
    if [ "$(printf '\xe6\x97\xa5' | ink --cols=4 --rows=1)" -gt 0 ]; then
        ok "a glyph the chosen font lacks is drawn from a fallback font"
    else
        echo "  skip  fallback (no installed font covers U+65E5)"
    fi
fi

# ── the window ──────────────────────────────────────────────────────────────
#
# ⚠ THE ONE SECTION THAT NEEDS A COMPOSITOR, and it brings its own: a HEADLESS
# cage, which is what produced every startup number this project quotes. It
# never touches the machine's real seat or display.
#
# Skipped without cage, for the same reason the font section is skipped without
# fonts. The rule at the top of this file still holds: nothing here requires a
# screen that was already running.
if ! command -v cage >/dev/null 2>&1; then
    echo "  skip  the window (cage is not installed)"
elif [ -z "${XDG_RUNTIME_DIR:-}" ]; then
    echo "  skip  the window (no XDG_RUNTIME_DIR for a compositor socket)"
else
    caged() {  # caged <timeout> <args...>
        local t=$1; shift
        WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 \
        XDG_CACHE_HOME="$FC" timeout "$t" cage -- "$ST" "$@" 2>&1
    }

    # It opens a window, paints, and exits with the child's status. Every
    # earlier assertion in this file is about a terminal that cannot be seen;
    # this is the first one that says the whole thing runs.
    out=$(caged 30 --stats win -- /bin/sh -c 'exit 0')
    echo "$out" | grep -qE '^first frame +[0-9]'
    check "the window opens and paints a first frame" $?

    echo "$out" | grep -qE '^frames +[1-9]'
    check "...and commits at least one buffer" $?

    # THE STARTUP CLAIM. Not a threshold — a machine slower than this one must
    # not go red — but the number is printed, because a claim nothing prints is
    # a claim nobody checks. kitty is 230.3 ms and foot 24.9 ms on this machine.
    ff=$(echo "$out" | awk '/^first frame/{print $3}')
    ok "first frame on this machine: ${ff} ms  (kitty 230.3, foot 24.9)"

    # The child's status is the window's status. A terminal that always exits 0
    # breaks every script that runs one.
    caged 30 win -- /bin/sh -c 'exit 3' >/dev/null 2>&1
    [ $? -eq 3 ] && ok "the child's exit status is the window's" \
                 || bad "the child's exit status is the window's"

    # ⚠ THE OUTPUT MUST SURVIVE THE HANGUP. POLLHUP arrives on the same
    # revents that carry the child's last write, so a loop that checks for
    # hangup before draining throws away everything the child printed as it
    # exited — which is most of what a short command ever prints.
    out=$(caged 30 --stats win -- /bin/sh -c 'printf "the-last-line-before-exit"')
    echo "$out" | grep -qE '^frames +[1-9]'
    check "output written just before the child exits is not lost" $?

    # No display at all is a sentence, not a crash. This is what running it
    # from a tty looks like, and it is a thing people do by accident.
    #
    # ⚠ XDG_RUNTIME_DIR IS REDIRECTED, and unsetting WAYLAND_DISPLAY is not
    # enough on its own. wl_display_connect falls back to the socket named
    # `wayland-0` inside XDG_RUNTIME_DIR, so with the real one still in the
    # environment this "no compositor" test connected to the DEVELOPER'S LIVE
    # DESKTOP and briefly opened a window on it — while reporting success,
    # because connecting is exactly what it did. An empty directory has no
    # socket to find.
    EMPTY="$T/no-compositor"
    mkdir -p "$EMPTY"
    out=$(env -u WAYLAND_DISPLAY -u DISPLAY \
          XDG_RUNTIME_DIR="$EMPTY" XDG_CACHE_HOME="$FC" \
          timeout 20 "$ST" win -- /bin/true 2>&1)
    rc=$?
    [ $rc -ne 0 ] && echo "$out" | grep -qi 'wayland' \
        && ok "with no compositor it says so and exits non-zero" \
        || bad "with no compositor it says so and exits non-zero"

    # ── latency, via wp_presentation ────────────────────────────────────────
    #
    # ⚠ WHAT A HEADLESS COMPOSITOR CAN AND CANNOT TELL YOU. Nothing here is
    # scanned out — there is no display and no refresh cycle — so most frames
    # come back `discarded` rather than `presented`, and the handful of
    # commit->photon figures that do arrive are NOT a measurement of a real
    # display. A number from this environment must never be quoted as one.
    #
    # What IS worth asserting here is the machinery: that the protocol was
    # bound, that the compositor's clock was checked against ours, that
    # feedback arrives and is accounted for, and — most of all — that a
    # latency nobody measured is reported as unmeasured instead of as zero.
    out=$(caged 30 --stats win -- /bin/sh -c 'echo x; sleep 0.4')

    echo "$out" | grep -qE 'commit->photon|discarded'
    check "wp_presentation is bound and its clock matched ours" $?

    echo "$out" | grep -q 'latency       not measured'
    if [ $? -eq 0 ]; then
        bad "wp_presentation is bound and its clock matched ours"
    fi

    # ⚠ THE ASSERTION THAT MATTERS MOST IN THIS BLOCK. Nothing typed means
    # nothing to report, and a latency figure invented out of an empty sample
    # is the single most damaging thing this program could print — it would be
    # quoted, and it would be a lie. "0.00 ms" must be impossible to reach by
    # accident.
    echo "$out" | grep -q 'input->photon  nothing was typed'
    check "an input latency nobody measured is reported as unmeasured" $?

    echo "$out" | grep -qE 'input->photon +[0-9]' \
        && bad "...and never as a number" \
        || ok "...and never as a number"

    # Every committed frame is accounted for: shown, or superseded. A frame
    # that produced neither event means a leaked feedback object.
    fr=$(echo "$out" | awk '/^frames/{print $2}')
    dis=$(echo "$out" | awk '/^discarded/{print $2}')
    pres=$(echo "$out" | sed -n 's/.*n=\([0-9]*\).*/\1/p' | head -1)
    : "${dis:=0}" "${pres:=0}"
    [ -n "$fr" ] && [ "$((dis + pres))" -ge 1 ] && [ "$((dis + pres))" -le "$fr" ] \
        && ok "every committed frame is accounted for (${pres} shown, ${dis} superseded of ${fr})" \
        || bad "every committed frame is accounted for (${pres}+${dis} vs ${fr})"

    # ── deadline rendering ──────────────────────────────────────────────────
    #
    # ⚠ WHAT CANNOT BE TESTED HERE, stated so nobody assumes it was. The whole
    # POINT of deadline rendering — that a keystroke arriving after the frame
    # callback still makes it into that frame — needs a real display (something
    # must actually be scanned out) and a real person (input is never
    # synthesised). Neither exists in this file. A headless cage reports no
    # constant refresh rate at all, so what runs here is the FALLBACK.
    #
    # The fallback is worth every one of these assertions, because it is what
    # runs whenever the prediction is unavailable, and getting it wrong makes
    # the terminal WORSE than one that never tried.
    echo "$out" | grep -q '^deadline      on'
    check "deadline rendering is on by default" $?

    out_off=$(caged 30 --stats --no-deadline win -- /bin/sh -c 'echo x; sleep 0.3')
    echo "$out_off" | grep -q 'deadline      off'
    check "--no-deadline turns it off, for an A/B against it" $?

    # ⚠ NO CADENCE IS INVENTED. A client that assumes 60 Hz because it was
    # given nothing is wrong on every 144 Hz and every variable-refresh
    # monitor, and being wrong here does not degrade gently: it paints after
    # the deadline and lands a whole frame late, which is the exact problem
    # deadline rendering exists to fix.
    echo "$out" | grep -q 'no constant refresh rate'
    check "with no refresh rate reported it paints immediately and says so" $?

    echo "$out" | grep -qE 'deadline      on, 1[0-9]\.[0-9]+ ms refresh' \
        && bad "...and never invents 60 Hz to fill the gap" \
        || ok "...and never invents 60 Hz to fill the gap"
fi

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
