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

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
