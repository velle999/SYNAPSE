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

# ⚠ THE DEVELOPER'S OWN CONFIG MUST NOT REACH THESE ASSERTIONS. syntty reads
# $XDG_CONFIG_HOME/syntty/syntty.conf, so on the machine this is written on a
# `font_size = 24` would change every cell measurement and a `background =` line
# would change every probed pixel — and the suite would pass or fail depending
# on somebody's colour scheme. Pointed at an empty directory here, once, rather
# than remembering --no-config on ninety invocations.
export XDG_CONFIG_HOME="$T/xdg-config"
mkdir -p "$XDG_CONFIG_HOME"

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

#
# ⚠ THE FIXTURE USES SEQUENCES THAT WILL NEVER BE IMPLEMENTED, on purpose. It
# used `?1049h` — a real mode — and went red the day the alternate screen
# landed, which is a stale test rather than a regression. Anything genuinely
# useful in here becomes a false alarm the moment it is built.
n=$(printf '\033[?31337h\033[9999Z' | "$ST" dump - --stats 2>&1 >/dev/null \
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
benchout=$("$ST" bench "$T/bench.txt" --runs=2 2>&1)
if printf '%s' "$benchout" | grep -q 'MB/s'; then
    ok "bench reports a throughput number"
else
    bad "bench reports a throughput number"
fi

rate=$("$ST" bench "$T/bench.txt" --runs=3 | awk '/^best/{print $4}')
ok "throughput on this machine: ${rate} MB/s"

# ── the alternate screen ────────────────────────────────────────────────────
#
# ⚠ THE FEATURE THAT LOOKS LIKE A DIFFERENT BUG WHEN IT IS MISSING. Every
# full-screen program uses it — vim, less, htop, man — and without it their
# redraws pour into the scrollback, so ten minutes in an editor erases the
# commands the person actually ran. That reads as "the scrollback is broken",
# and nobody connects it to the alternate screen.

alt=$(python3 -c "
import sys
for i in range(1,6): sys.stdout.write('command %d\r\noutput %d\r\n' % (i,i))
sys.stdout.write('\033[?1049h')
for i in range(30): sys.stdout.write('EDITOR JUNK %d\r\n' % i)
sys.stdout.write('\033[?1049l')
sys.stdout.write('after\r\n')
" 2>/dev/null | "$ST" --cols=24 --rows=6 --scrollback-too dump - --stats 2>&1)

if [ -z "$alt" ]; then
    echo "  skip  the alternate screen (no python3)"
else
    printf '%s' "$alt" | grep -q 'EDITOR JUNK' \
        && bad "a full-screen program's redraws never reach the scrollback" \
        || ok "a full-screen program's redraws never reach the scrollback"

    printf '%s' "$alt" | grep -q 'command 1' \
        && ok "...and the real history is still there afterwards" \
        || bad "...and the real history is still there afterwards"

    # 30 lines of junk would have made it far bigger than this.
    n=$(printf '%s' "$alt" | awk '/^scrollback/{print $2}')
    [ -n "$n" ] && [ "$n" -lt 15 ] \
        && ok "...and the scrollback holds only what was really printed ($n rows)" \
        || bad "...and the scrollback holds only what was really printed ($n rows)"

    # Entering must give a BLANK screen every time. A second editor opening
    # onto the first one's leftovers is a memorable bug.
    blank=$(printf 'x\033[?1049hy\033[?1049l\033[?1049h' \
            | "$ST" --cols=10 --rows=3 dump - | tr -d ' \n')
    [ "$blank" = "" ] \
        && ok "entering the alternate screen always gives a blank one" \
        || bad "entering the alternate screen always gives a blank one ('$blank')"

    # And leaving restores what the shell had.
    back=$(printf 'shell text\033[?1049hEDITOR\033[?1049l' \
           | "$ST" --cols=14 --rows=3 dump - | head -1 | sed -e 's/[[:space:]]*$//')
    [ "$back" = "shell text" ] \
        && ok "leaving it restores the session underneath" \
        || bad "leaving it restores the session underneath (got '$back')"

    # The older forms are still emitted in the wild.
    back47=$(printf 'shell\033[?47hEDITOR\033[?47l' \
             | "$ST" --cols=14 --rows=3 dump - | head -1 | sed -e 's/[[:space:]]*$//')
    [ "$back47" = "shell" ] \
        && ok "...and so do the older ?47 and ?1047 forms" \
        || bad "...and so do the older ?47 and ?1047 forms (got '$back47')"
fi

# ── modes a program turns on that the input layer must obey ─────────────────
modes=$(printf '\033[?1000h\033[?1006h\033[?2004h\033[?25l' \
        | "$ST" --cols=10 --rows=2 dump - --stats 2>&1)
printf '%s' "$modes" | grep -q 'unhandled     0 CSI'
check "mouse, paste and cursor modes are understood, not counted as unknown" $?

# ⚠ AND THE MODE IS THE NUMBER IT SAYS IT IS. The check above passed for a
# whole release while the field holding it was a uint8_t: 1000 came back as
# 232, 234 for 1002, 235 for 1003 — non-zero, distinct from each other, and
# wrong. Nothing compared the value against 1000 until there was an input layer
# to send events, which is precisely when it would have started sending the
# wrong ones. A test that only asks "was it understood?" cannot see this.
printf '%s' "$modes" | grep -q 'mouse         1000, SGR coordinates'
check "...and ?1000 is recorded as 1000, not as a truncated byte" $?

m3=$(printf '\033[?1003h' | "$ST" --cols=10 --rows=2 dump - --stats 2>&1)
printf '%s' "$m3" | grep -q 'mouse         1003, 1984 coordinates'
check "...and ?1003 as 1003, with the old coordinates until ?1006 asks" $?

# ── what a pointer event becomes on the child's input ───────────────────────
#
# ⚠ THE SEAT IS WHAT CANNOT BE TESTED HERE. There is no pointer in a headless
# cage and input is never synthesised on a live session, so win.c's plumbing is
# exercised by a person and nothing else. Every RULE about what should be sent
# lives in mouse.c behind `syntty mouse`, which is why that subcommand exists.
mouse() { "$ST" mouse "$@" 2>&1; }

e=$(mouse --mode=1000 press:left@10,5)
case "$e" in *'ESC[M +&'*) ok "a left click is the 1984 encoding, offset by 32" ;;
             *) bad "a left click is the 1984 encoding, offset by 32 ($e)" ;; esac

# ⚠ X10 CANNOT SAY WHICH BUTTON CAME UP: every release is button 3. Reporting
# the real button would be a valid-looking sequence meaning a press of a button
# that does not exist on most mice.
e=$(mouse --mode=1000 release:right@10,5)
case "$e" in *'ESC[M#+&'*) ok "...and a release is button 3, because it cannot say which" ;;
             *) bad "...and a release is button 3, because it cannot say which ($e)" ;; esac

# The three modes differ ONLY in what motion does, and that is the whole point
# of keeping them apart: a program that asked for buttons and is sent 1003's
# motion gets a flood it never wanted and cannot switch off.
e=$(mouse --mode=1000 move@11,5)
case "$e" in *'reports buttons only'*) ok "?1000 sends no motion at all, and says why" ;;
             *) bad "?1000 sends no motion at all, and says why ($e)" ;; esac

e=$(mouse --mode=1002 move@11,5)
case "$e" in *'only while a button is held'*) ok "?1002 sends no motion with nothing held" ;;
             *) bad "?1002 sends no motion with nothing held ($e)" ;; esac

e=$(mouse --mode=1002 move:left@11,5)
case "$e" in *'ESC[M@,&'*) ok "...and motion while dragging carries the button, +32" ;;
             *) bad "...and motion while dragging carries the button, +32 ($e)" ;; esac

e=$(mouse --mode=1003 move@11,5)
case "$e" in *'ESC[MC,&'*) ok "?1003 sends motion with nothing held (button 3 + 32)" ;;
             *) bad "?1003 sends motion with nothing held ($e)" ;; esac

# SGR, which exists because the 1984 encoding puts a coordinate in one byte.
e=$(mouse --mode=1000 --sgr press:left@10,5)
case "$e" in *'ESC[<0;11;6M'*) ok "?1006 sends decimal coordinates, one-based" ;;
             *) bad "?1006 sends decimal coordinates, one-based ($e)" ;; esac

e=$(mouse --mode=1000 --sgr release:middle@10,5)
case "$e" in *'ESC[<1;11;6m'*) ok "...and its release is a lowercase m that names the button" ;;
             *) bad "...and its release is a lowercase m that names the button ($e)" ;; esac

# ⚠ THE COLUMN THAT BREAKS EVERYTHING. 32 + col + 1 has to fit in a byte, so
# the last cell the old encoding can name is 223 — which is an ordinary width
# on a wide monitor. Wrapping would report a confident click on a completely
# different cell, so the event is dropped and the reason is said out loud.
e=$(mouse --mode=1000 press:left@250,5)
case "$e" in *'beyond column 223'*) ok "past column 223 the old encoding refuses, out loud" ;;
             *) bad "past column 223 the old encoding refuses, out loud ($e)" ;; esac

e=$(mouse --mode=1000 --sgr press:left@250,5)
case "$e" in *'ESC[<0;251;6M'*) ok "...and ?1006 carries that column without trouble" ;;
             *) bad "...and ?1006 carries that column without trouble ($e)" ;; esac

# A wheel notch is a press with no release. Sending one gives every program a
# button-up for a button that was never down.
e=$(mouse --mode=1000 wheel:up@3,3)
case "$e" in *'ESC[M`$$'*) ok "a wheel notch is button 64" ;;
             *) bad "a wheel notch is button 64 ($e)" ;; esac

e=$(mouse --mode=1000 release:up@3,3)
case "$e" in *'no release'*) ok "...and it has no release event" ;;
             *) bad "...and it has no release event ($e)" ;; esac

# Modifiers ride in the button field, as the protocol's own bit values.
e=$(mouse --mode=1000 --ctrl --shift press:middle@10,5)
case "$e" in *'ESC[M5+&'*) ok "shift and ctrl are bits in the button field" ;;
             *) bad "shift and ctrl are bits in the button field ($e)" ;; esac

# With no mode set, nothing is sent whatever happens — a terminal that reports
# the pointer to a program that never asked types garbage into it.
e=$(mouse --mode=0 press:left@10,5)
case "$e" in *'no program asked'*) ok "with no mode set nothing is reported at all" ;;
             *) bad "with no mode set nothing is reported at all ($e)" ;; esac

# ── resizing, which nothing could test until there was a way to ─────────────
#
# ⚠ A WINDOW IS RESIZED BY THE COMPOSITOR, so every rule about what happens to
# the text and to the cursor lived on a path this file could not reach — and a
# bug sat there through three releases. `--resize` applies one to the grid after
# the stream, which is exactly what fit_grid does when a window changes size.
grew_screen=$(printf 'one\r\ntwo\r\nthree' | "$ST" --cols=10 --rows=3 dump - --resize=10x6 2>/dev/null)
grew=$(printf 'one\r\ntwo\r\nthree' | "$ST" --cols=10 --rows=3 dump - --resize=10x6 --stats 2>&1 >/dev/null)
[ "$(printf '%s' "$grew_screen" | sed -n 1p)" = "one" ] \
    && ok "growing the grid leaves the text where it was" \
    || bad "growing the grid leaves the text where it was"

# ⚠ THE ONE THE SCREENSHOT FOUND. Growing used to push the old rows DOWN by the
# number of rows gained while the cursor was merely clamped — so the cursor
# stayed at the top and the text slid to the bottom, and on a tiling compositor
# that is EVERY window: the shell's output sat at the bottom of the screen with
# the cursor blinking at the top, and the next command overwrote the top while
# its output appeared at the bottom.
printf '%s' "$grew" | grep -q '^cursor        5,2'
check "...and the cursor stays on the line it was on" $?

shrunk_screen=$(printf 'a\r\nb\r\nc\r\nd\r\ne\r\nf' | "$ST" --cols=10 --rows=6 dump - --resize=10x3 2>/dev/null)
shrunk=$(printf 'a\r\nb\r\nc\r\nd\r\ne\r\nf' | "$ST" --cols=10 --rows=6 dump - --resize=10x3 --stats 2>&1 >/dev/null)
[ "$(printf '%s' "$shrunk_screen" | sed -n 1p)" = "d" ] \
    && ok "shrinking keeps the BOTTOM — where a shell's prompt is" \
    || bad "shrinking keeps the BOTTOM — where a shell's prompt is"

printf '%s' "$shrunk" | grep -q '^cursor        1,2'
check "...and the cursor follows the text up" $?

# ── the config file ─────────────────────────────────────────────────────────
#
# Flags only is not how anybody runs a terminal: `--font=` on every launch is a
# shell alias somebody has to write, and it does not survive the compositor
# starting the terminal for them.
CONF="$T/syntty.conf"
cat > "$CONF" <<'EOF'
font_size = 16
rows = 30
scrollback = 5000
background = #123456
color1 = #00ff00
foreground = bright_white
EOF

c=$("$ST" --config="$CONF" config)
printf '%s' "$c" | grep -q 'errors       none'
check "a config with only good lines has no errors" $?

printf '%s' "$c" | grep -q '^rows         30'
check "...and the values it sets are the ones it says" $?

# ⚠ THE COMMENT CHARACTER AND THE COLOUR PREFIX ARE THE SAME CHARACTER. This
# was written the obvious way first — strip `#` to the end of the line — and
# the very first config with a colour in it came back as "no value: background",
# silently dropping every colour in the file. A comment is a WHOLE LINE here,
# and there are no trailing comments, because that rule has no exception to
# forget.
printf '%s' "$c" | grep -q '^background   #123456'
check "a colour survives the comment stripping (# is both)" $?

# ⚠ A BROKEN LINE MUST NOT STOP THE TERMINAL. A terminal that refuses to open
# over a typo in its config cannot be used to fix the typo.
cat > "$T/bad.conf" <<'EOF'
rows = 30
fnt = DejaVu
font_size = huge
EOF
c=$("$ST" --config="$T/bad.conf" config)
printf '%s' "$c" | grep -q '^rows         30'
check "a broken line does not stop the good lines around it" $?

# ⚠ AN UNKNOWN KEY IS REPORTED. Silently ignoring `fnt` is how a misspelling
# becomes half an hour of wondering why the font never changed.
printf '%s' "$c" | grep -q 'errors       2, first at line 2: unknown setting: fnt'
check "...and an unknown key is named, with its line number" $?

# A missing file is the normal case, not an error.
c=$("$ST" --config="$T/does-not-exist.conf" config)
printf '%s' "$c" | grep -q 'no file (not an error)'
check "no config file at all is not an error" $?

"$ST" config --example > "$T/example.conf"
[ -s "$T/example.conf" ] && "$ST" --config="$T/example.conf" config | grep -q 'errors       none'
check "the example it prints is a config it accepts" $?

# ── and the settings reach the pixels ───────────────────────────────────────
#
# A config that parses and changes nothing is the failure every one of the
# assertions above would still pass on.
if [ -n "${FC:-}" ] || command -v fc-match >/dev/null 2>&1; then
    FCC=${FC:-$T/fontcache}
    bgc=$(printf 'x' | XDG_CACHE_HOME="$FCC" "$ST" --cols=4 --rows=1 \
              --config="$CONF" render - --no-cursor --probe=2,0 2>/dev/null \
          | awk '{print $3}')
    [ "$bgc" = "123456" ] \
        && ok "the configured background is the colour actually painted" \
        || bad "the configured background is the colour actually painted ($bgc)"

    # A palette entry, which is a different path from the default colours: this
    # one goes through SGR 41 and the interned style.
    redc=$(printf '\033[41mA' | XDG_CACHE_HOME="$FCC" "$ST" --cols=4 --rows=1 \
               --config="$CONF" render - --no-cursor --probe=0,0 2>/dev/null \
           | awk '{print $3}')
    [ "$redc" = "00FF00" ] \
        && ok "...and a configured palette entry reaches SGR 41" \
        || bad "...and a configured palette entry reaches SGR 41 ($redc)"

    # ⚠ --no-config HAS TO WORK, because it is what this suite relies on to be
    # reproducible on a machine where somebody has a config.
    offc=$(printf 'x' | XDG_CACHE_HOME="$FCC" "$ST" --cols=4 --rows=1 \
               --config="$CONF" --no-config render - --no-cursor --probe=2,0 \
               2>/dev/null | awk '{print $3}')
    [ "$offc" = "1B1F26" ] \
        && ok "--no-config ignores the file entirely" \
        || bad "--no-config ignores the file entirely ($offc)"
else
    echo "  skip  the config reaching the pixels (no fontconfig)"
fi

# ⚠ FLAGS BEAT THE FILE, or a config makes the command line unusable.
rows_used=$(printf 'a\nb\nc\nd\ne\nf\ng\nh\ni\n' \
            | "$ST" --config="$CONF" --rows=7 dump - --stats 2>&1 \
            | awk '/^scrollback/{print $2}')
[ "$rows_used" = "3" ] \
    && ok "a flag beats the file (--rows=7 over rows = 30)" \
    || bad "a flag beats the file (--rows=7 over rows = 30; $rows_used scrolled)"

# ── include ─────────────────────────────────────────────────────────────────
#
# The one thing the format grew, and it grew for a reason: the desktop
# regenerates the terminal's palette on every theme switch, and a generated
# file and a hand-written one must not be the same file. See config.c.
INC="$T/inc"
mkdir -p "$INC"
printf 'background = #111111\nfont = Original\ninclude = palette.conf\n' > "$INC/main.conf"
printf 'background = #c0c0c0\nforeground = #000000\n' > "$INC/palette.conf"

c=$("$ST" --config="$INC/main.conf" config)
printf '%s\n' "$c" | grep -q 'errors       none'
check "an include is read" $?

# ⚠ LAST WINS, exactly as it does for two plain lines. This is what makes a
# theme switch change anything at all: an include at the bottom beats the line
# the user forgot they wrote at the top.
printf '%s\n' "$c" | grep -q 'background   #C0C0C0'
check "...and an include at the bottom beats the line above it" $?

# It is not an all-or-nothing takeover: a key the included file says nothing
# about keeps what the including file set.
printf '%s\n' "$c" | grep -q 'font         Original'
check "...while a key it does not mention is left alone" $?

# ⚠ NAMED IN THE OUTPUT. "why is my background not what I wrote" is answerable
# only if the file that overrode it can be found, and it is not a file the
# person went looking for.
printf '%s\n' "$c" | grep -q "included     $INC/palette.conf"
check "...and 'syntty config' names every file it read" $?

# Resolved against the DIRECTORY OF THE FILE IT APPEARS IN, not the working
# directory — a terminal is launched from wherever somebody happened to be, and
# the same config must not mean different things depending on that.
( cd / && "$ST" --config="$INC/main.conf" config | grep -q 'background   #C0C0C0' )
check "...resolved beside its parent, not against the working directory" $?

# ⚠ A MISSING INCLUDE IS AN ERROR. Every other absent file here is silence,
# because not writing a config is normal — but naming one that is not there is
# a typo, and the settings live somewhere the person is not looking.
#
# ⚠ CAPTURED BEFORE MATCHING, not piped. `syntty config` EXITS 1 when it has
# something to report, and under `pipefail` that makes the pipeline fail for a
# test whose grep matched — the same trap this file hits with `grep -q` on a
# producer, one release earlier, and it fails in the direction that looks like
# a broken feature rather than a broken test.
printf 'include = nowhere.conf\n' > "$INC/missing.conf"
c=$("$ST" --config="$INC/missing.conf" config || true)
printf '%s\n' "$c" | grep -q 'cannot read the included file: nowhere.conf'
check "a missing include is an error that says which file" $?

# kitty spells it without the equals, so this is the one wrong line somebody is
# actually going to write.
printf 'include palette.conf\n' > "$INC/bare.conf"
c=$("$ST" --config="$INC/bare.conf" config || true)
printf '%s\n' "$c" | grep -q "include takes an '=' here"
check "...and the kitty spelling is named rather than reported as a syntax error" $?

# ⚠ A CYCLE TERMINATES. Three separate caps make it harmless (already-read,
# depth, total files) — this asserts the program comes back at all, which is
# the property that matters and the one a recursive reader loses.
printf 'include = b.conf\n' > "$INC/a.conf"
printf 'include = a.conf\nbackground = #abcdef\n' > "$INC/b.conf"
timeout 5 "$ST" --config="$INC/a.conf" config | grep -q 'background   #ABCDEF'
check "an include cycle terminates instead of recursing" $?

# ── what a paste becomes on the way to the child ────────────────────────────
#
# ⚠ A PASTE IS NOT TYPING, and a terminal that treats it as typing has turned
# the clipboard into an execution channel: copy a command off a web page, and
# the page can append a newline so the shell runs it before it has been read.
# The rules below are safety, not convenience, and this is the half of
# copy-and-paste that can be tested without a seat.
p=$("$ST" paste "$(printf 'ls -la\nrm -rf /tmp/x')")
[ "$p" = "ls -laCRrm -rf /tmp/x" ] \
    && ok "a pasted newline becomes CR, which is what Enter sends" \
    || bad "a pasted newline becomes CR, which is what Enter sends ('$p')"

# ⚠ \r\n IS ONE LINE ENDING. Text that has been near Windows carries both, and
# sending both submits the line and then an empty one — a two-line paste runs
# three commands.
p=$("$ST" paste "$(printf 'a\r\nb')")
[ "$p" = "aCRb" ] && ok "...and \\r\\n is one line ending, not two" \
                  || bad "...and \\r\\n is one line ending, not two ('$p')"

p=$("$ST" paste --bracketed "$(printf 'a\nb')")
[ "$p" = "ESC[200~aCRbESC[201~" ] \
    && ok "bracketed paste wraps it so the shell knows it was pasted" \
    || bad "bracketed paste wraps it so the shell knows it was pasted ('$p')"

# ⚠ THE END MARKER CANNOT BE FORGED. A clipboard containing ESC[201~ would
# close the bracket early and have everything after it treated as typed —
# which is the whole attack bracketed paste exists to stop. Stripping ESC is
# what makes it impossible, and it is why the stripping is not optional.
p=$("$ST" paste --bracketed "$(printf 'safe\033[201~rm -rf /')")
case "$p" in
    "ESC[200~safe[201~rm -rf /ESC[201~") ok "a pasted end marker cannot close the bracket early" ;;
    *) bad "a pasted end marker cannot close the bracket early ('$p')" ;;
esac

p=$("$ST" paste "$(printf 'a\tb\001c')")
[ "$p" = "aTABbc" ] \
    && ok "control bytes are dropped from a paste, tab excepted" \
    || bad "control bytes are dropped from a paste, tab excepted ('$p')"

# ── OSC 52: the child asking for the clipboard ──────────────────────────────
#
# How vim, tmux and anything over ssh copy to the clipboard of the machine the
# person is sitting at. There is no other channel from the far end of a pty.
c=$(printf '\033]52;c;aGVsbG8gY2xpcA==\007' | "$ST" --cols=10 --rows=2 dump - --stats 2>&1)
printf '%s' "$c" | grep -q 'clipboard     1 set, 10 bytes waiting for the clipboard'
check "a child can put text on the clipboard (OSC 52)" $?

c=$(printf '\033]52;p;aGk=\007' | "$ST" --cols=10 --rows=2 dump - --stats 2>&1)
printf '%s' "$c" | grep -q 'waiting for the primary'
check "...and can name the primary selection instead" $?

# ⚠ THE READ FORM IS REFUSED, AND THAT IS THE POINT. `52;c;?` asks the terminal
# to type the clipboard back at whatever is running — which may be a program on
# the far end of an ssh session, and the clipboard is regularly a password. The
# terminal cannot tell who is asking, so nobody gets an answer, and the refusal
# is counted so it is visible that something asked.
c=$(printf '\033]52;c;?\007' | "$ST" --cols=10 --rows=2 dump - --stats 2>&1)
printf '%s' "$c" | grep -q 'READ request(s) refused'
check "a child asking to READ the clipboard is refused, out loud" $?
printf '%s' "$c" | grep -q 'reply  '
if [ $? = 0 ]; then bad "...and is sent nothing back at all"
else ok "...and is sent nothing back at all"; fi

# ⚠ AN OVERLONG OSC IS DROPPED WHOLE, NOT TRUNCATED. Keeping the first N bytes
# is harmless for a window title and dangerous here: half a base64 payload
# decodes cleanly into DIFFERENT text, and the clipboard would silently hold
# something nobody copied.
big=$(python3 -c "print('A'*9000, end='')" 2>/dev/null)
if [ -z "$big" ]; then
    echo "  skip  an overlong OSC is dropped whole (no python3)"
else
    c=$(printf '\033]52;c;%s\007' "$big" | "$ST" --cols=10 --rows=2 dump - --stats 2>&1)
    printf '%s' "$c" | grep -q 'dropped for running past'
    check "an OSC longer than the buffer is dropped whole, not truncated" $?
    printf '%s' "$c" | grep -q 'clipboard     1 set'
    if [ $? = 0 ]; then bad "...and sets no clipboard from the part that fitted"
    else ok "...and sets no clipboard from the part that fitted"; fi
fi

# ── selecting with the pointer, without a pointer ───────────────────────────
selfile="$T/sel.txt"
printf 'hello /usr/lib/libfoo.so world\r\nsecond line\r\n' > "$selfile"

s=$("$ST" --cols=40 --rows=6 dump "$selfile" --click=0,0 --drag=4,0)
[ "$s" = "hello" ] && ok "a press and a drag select what lies between them" \
                   || bad "a press and a drag select what lies between them ('$s')"

# ⚠ A DOUBLE-CLICK MUST TAKE THE WHOLE PATH. A word set of "letters and digits"
# gives back `so`, which is the version of this feature nobody can use: the
# reason to double-click in a terminal is almost always a path, a URL or a flag.
s=$("$ST" --cols=40 --rows=6 dump "$selfile" --click=10,0,word)
[ "$s" = "/usr/lib/libfoo.so" ] \
    && ok "a double-click takes the whole path, not the part between the dots" \
    || bad "a double-click takes the whole path, not the part between the dots ('$s')"

# Clicking in the gap between two words selects the gap, not one of them.
s=$("$ST" --cols=40 --rows=6 dump "$selfile" --click=5,0,word)
[ "$s" = "" ] && ok "...and a double-click on whitespace selects the whitespace" \
              || bad "...and a double-click on whitespace selects the whitespace ('$s')"

s=$("$ST" --cols=40 --rows=6 dump "$selfile" --click=3,0,line)
[ "$s" = "hello /usr/lib/libfoo.so world" ] \
    && ok "a triple-click takes the line, trailing padding trimmed" \
    || bad "a triple-click takes the line, trailing padding trimmed ('$s')"

s=$("$ST" --cols=40 --rows=6 dump "$selfile" --click=6,0 --drag=5,1)
[ "$s" = "$(printf '/usr/lib/libfoo.so world\nsecond')" ] \
    && ok "a drag across rows keeps the newline the program really sent" \
    || bad "a drag across rows keeps the newline the program really sent ('$s')"

# ⚠ THE ONE THE ABSOLUTE ANCHOR EXISTS FOR. Somebody highlights a filename
# while a build is running; the build keeps printing and pushes it up the
# window. A selection stored as "row 0, columns 0 to 4" stays lit on row 0 over
# whatever has since scrolled into it, and copies that instead. Three lines of
# output arrive here, so a selection kept in screen coordinates comes back
# empty — the rows it named are blank by then.
s=$("$ST" --cols=40 --rows=6 dump "$selfile" --click=0,0 --drag=4,0 --scroll-after=3)
[ "$s" = "hello" ] \
    && ok "output arriving underneath a selection does not move it" \
    || bad "output arriving underneath a selection does not move it ('$s')"

# ── selection: what would actually land on the clipboard ────────────────────
sel=$(printf 'hello world\r\nsecond line\r\n' | "$ST" --cols=20 --rows=4 dump - --select=0,0,4,0)
[ "$sel" = "hello" ] && ok "a selection inside one line is that text" \
                     || bad "a selection inside one line is that text (got '$sel')"

sel=$(printf 'hello world\r\nsecond line\r\n' | "$ST" --cols=20 --rows=4 dump - --select=6,0,5,1)
[ "$sel" = "$(printf 'world\nsecond')" ] \
    && ok "...and across lines it keeps the newline between them" \
    || bad "...and across lines it keeps the newline between them (got '$sel')"

# ⚠ THE ONE MOST TERMINALS GET WRONG. A long line the terminal broke across two
# rows is ONE line. Copy it with a newline in the middle, paste it into a
# shell, and half a command runs on its own.
sel=$(printf 'aaaaaaaaaabbbbbbbbbb' | "$ST" --cols=10 --rows=4 dump - --select=0,0,9,1)
[ "$sel" = "aaaaaaaaaabbbbbbbbbb" ] \
    && ok "a soft-wrapped line is copied as ONE line, not two" \
    || bad "a soft-wrapped line is copied as ONE line, not two (got '$sel')"

# Dragging backwards selects the same text as dragging forwards.
a=$(printf 'hello world' | "$ST" --cols=20 --rows=2 dump - --select=0,0,4,0)
b=$(printf 'hello world' | "$ST" --cols=20 --rows=2 dump - --select=4,0,0,0)
[ "$a" = "$b" ] && ok "dragging backwards selects the same text" \
                || bad "dragging backwards selects the same text"

# Padding to the right of the text is not spaces anybody typed.
sel=$(printf 'hi' | "$ST" --cols=20 --rows=2 dump - --select=0,0,19,0)
[ "$sel" = "hi" ] && ok "trailing padding is not copied as spaces" \
                  || bad "trailing padding is not copied as spaces (got '$sel')"

# ── semantic marks, the scrollback view, and jump-to-prompt ─────────────────
#
# OSC 133 is how a shell tells the terminal where its prompts are, when a
# command started and how it ended. ⚠ IT IS THE STANDARD'S MARKS, NOT OURS,
# even though we ship the shell at the other end — a terminal that only
# understands its own shell gets none of this from bash, zsh or a remote ssh
# session, and a shell that emits something private is useless everywhere else.
# Owning both ends is worth using to make the marks RELIABLE, not private.
sess() {
    python3 -c "
import sys
for i in range(1,13):
    sys.stdout.write('\033]133;A\007\$ \033]133;B\007cmd%d\r\n\033]133;C\007out %d\r\n\033]133;D;%d\007' % (i,i,i%2))
" 2>/dev/null
}

if [ -z "$(sess)" ]; then
    echo "  skip  semantic marks (no python3 to build a session)"
else
    r=$(sess | "$ST" --cols=20 --rows=6 dump - --stats 2>&1)

    echo "$r" | grep -q 'commands      12 recorded'
    check "every command the shell announced is recorded" $?

    # ⚠ THE STATUS IS THE POINT. A terminal that records the timing but loses
    # whether it worked cannot colour a failed command, which is the single
    # most useful thing these marks buy.
    echo "$r" | grep -q 'status 1'
    check "...with the exit status the shell reported" $?

    # ⚠ AND UNKNOWN IS NOT ZERO. A bare `D` says "finished" and nothing else;
    # reporting that as 0 is how a failing command comes back looking green.
    u=$(printf '\033]133;A\007$ \033]133;B\007x\r\n\033]133;C\007\033]133;D\007' \
        | "$ST" --cols=20 --rows=4 dump - --stats 2>&1)
    printf '%s' "$u" | grep -q 'status unknown'
    check "a command that finished without saying how reports unknown, not 0" $?

    # ── the scrollback viewport ─────────────────────────────────────────────
    for i in $(seq 1 40); do printf 'line %d\r\n' $i; done > "$T/lines"

    # ⚠ The expected line depends on the WINDOW HEIGHT — four rows shows 38..40
    # plus the cursor's blank row, three rows shows 39..40. Getting that wrong
    # is a test bug, not a terminal bug, and it is worth stating because the
    # first version of these two assertions was written against a four-row
    # window and run against a three-row one.
    [ "$("$ST" --cols=12 --rows=4 dump - < "$T/lines" | head -1)" = "line 38" ] \
        && ok "the live view shows the newest lines" \
        || bad "the live view shows the newest lines"

    [ "$("$ST" --cols=12 --rows=4 --view=10 dump - < "$T/lines" | head -1)" = "line 28" ] \
        && ok "...and scrolling back shows history" \
        || bad "...and scrolling back shows history"

    # ⚠ CLAMPED TO WHAT IS KEPT, not to what was ever written. Scrolling past
    # the oldest retained line must stop, not wrap onto whatever is in the ring.
    top=$("$ST" --cols=12 --rows=4 --view=99999 dump - < "$T/lines" | head -1)
    [ "$top" = "line 1" ] \
        && ok "...and stops at the oldest line still kept" \
        || bad "...and stops at the oldest line still kept (got '$top')"

    # ── jump-to-prompt ──────────────────────────────────────────────────────
    #
    # The thing owning both ends actually buys: the shell SAID where its
    # prompts are, so this is exact rather than a heuristic over blank lines.
    j1=$(sess | "$ST" --cols=20 --rows=4 --jump=1 dump - | head -1)
    j2=$(sess | "$ST" --cols=20 --rows=4 --jump=2 dump - | head -1)
    [ "$j1" = '$ cmd11' ] && [ "$j2" = '$ cmd10' ] \
        && ok "jumping back lands each previous prompt at the top" \
        || bad "jumping back lands each previous prompt at the top ($j1 / $j2)"

    # ⚠ THE CURSOR IS NOT DRAWN OVER HISTORY. It is in a position that does not
    # exist there, and it would slide around as the view scrolled.
    live=$(printf 'abc' | "$ST" --cols=8 --rows=2 render - --probe=3,0 2>/dev/null | awk '{print $3}')
    for i in $(seq 1 30); do printf 'x\r\n'; done \
        | "$ST" --cols=8 --rows=2 --view=10 render - --probe=0,0 2>/dev/null \
        | awk '{print $3}' | grep -qv "$live"
    check "the cursor is not painted over scrollback" $?
fi

# ── the kitty GRAPHICS protocol ─────────────────────────────────────────────
#
# ⚠ NOTHING TO DO WITH THE GPU, despite the name — it is escape sequences for
# putting an IMAGE on the screen (icat, file previews, plots), composited on
# the CPU like everything else here. Table stakes, same as the keyboard half.
#
# The transport is APC: ESC _ G <control> ; <base64> ESC \.
gfx() { local out; out=$(printf "$1" | "$ST" dump - --stats 2>&1); printf '%s\n' "$out"; }

# A 2x2 image: red, green / blue, white. Small enough to reason about, and
# every quadrant a different colour so a flipped axis cannot pass.
IMG=$(python3 -c "import base64,sys; sys.stdout.write(base64.b64encode(bytes([255,0,0, 0,255,0, 0,0,255, 255,255,255])).decode())" 2>/dev/null)

if [ -z "$IMG" ]; then
    echo "  skip  the graphics protocol (no python3 to build a test image)"
else
    # THE SUPPORT PROBE. Programs send a=q and decide from the answer whether
    # to send images at all, so answering it is most of what "supports the
    # protocol" means in practice.
    r=$(gfx '\033_Gi=31,s=1,v=1,a=q;AAAA\033\\\\')
    echo "$r" | grep -q 'ESC_Gi=31;OKESC'
    check "a graphics query is answered OK" $?

    # ⚠ AND WHAT IS NOT IMPLEMENTED IS REFUSED OUT LOUD. A program told "OK"
    # for a format we ignore draws nothing and has no way to find out why —
    # the failure moves from the negotiation, where it is legible, into the
    # program's own rendering, where it is not.
    echo "$(gfx '\033_Gi=7,f=100,a=T,s=2,v=2;iVBOR\033\\\\')" | grep -q 'ENOTSUPP'
    check "...and an unimplemented format is refused, not silently dropped" $?

    echo "$(gfx '\033_Gi=8,f=32,t=f,a=T,s=2,v=2;L3RtcC94\033\\\\')" | grep -q 'ENOTSUPP'
    check "...as is a transmission medium we do not implement" $?

    # The whole path: transmit, display, and check the PIXELS. Four probes,
    # one per quadrant — an image that renders at all but transposed passes
    # every check except this one.
    img_probe() {  # img_probe <col,row>
        printf "\033_Ga=T,f=24,s=2,v=2,i=5,c=8,r=4;%s\033\\\\" "$IMG" \
            | "$ST" --cols=24 --rows=6 render - --no-cursor --probe="$1" 2>/dev/null \
            | awk '{print $3}'
    }
    q1=$(img_probe 1,1); q2=$(img_probe 6,1)
    q3=$(img_probe 1,3); q4=$(img_probe 6,3)
    [ "$q1" = FF0000 ] && [ "$q2" = 00FF00 ] && [ "$q3" = 0000FF ] && [ "$q4" = FFFFFF ] \
        && ok "an image is drawn, scaled to its cells, the right way up" \
        || bad "an image is drawn, scaled to its cells, the right way up ($q1 $q2 $q3 $q4)"

    # ⚠ CHUNKED TRANSMISSION IS THE NORMAL CASE, not an edge one: senders split
    # at 4096 bytes of base64, so anything bigger than a thumbnail arrives in
    # pieces. The geometry rides on the FIRST chunk only.
    h1=$(printf '%s' "$IMG" | cut -c1-8)
    h2=$(printf '%s' "$IMG" | cut -c9-)
    chunked=$(printf "\033_Ga=T,f=24,s=2,v=2,i=9,c=8,r=4,m=1;%s\033\\\\\033_Gm=0;%s\033\\\\" "$h1" "$h2" \
              | "$ST" --cols=24 --rows=6 render - --no-cursor --probe=1,1 2>/dev/null | awk '{print $3}')
    [ "$chunked" = FF0000 ] \
        && ok "an image split across chunks is reassembled" \
        || bad "an image split across chunks is reassembled (got $chunked)"

    # ⚠ THE CHILD IS NOT TRUSTED. `s=65535,v=65535,f=32` is a 17 GB allocation
    # request in eleven bytes. A refused image is a visible missing picture; an
    # unbounded one is a machine that stops.
    echo "$(gfx '\033_Ga=T,f=32,s=65535,v=65535,i=11;AAAA\033\\\\')" | grep -q 'EINVAL'
    check "an implausibly large image is refused rather than allocated" $?

    # A payload shorter than the geometry claims must not be believed, or the
    # blit reads whatever follows it in memory.
    echo "$(gfx '\033_Ga=T,f=24,s=64,v=64,i=12;AAAA\033\\\\')" | grep -q 'EINVAL'
    check "a payload shorter than its declared size is refused" $?

    # Deleting removes the placement — the image stops being drawn.
    after=$(printf "\033_Ga=T,f=24,s=2,v=2,i=5,c=8,r=4;%s\033\\\\\033_Ga=d\033\\\\" "$IMG" \
            | "$ST" --cols=24 --rows=6 render - --no-cursor --probe=1,1 2>/dev/null | awk '{print $3}')
    [ "$after" != FF0000 ] \
        && ok "deleting an image stops it being drawn" \
        || bad "deleting an image stops it being drawn"

    # An APC that is not ours is left alone, not guessed at.
    other=$(printf '\033_Xsomething else\033\\\\hello' | "$ST" --cols=20 --rows=2 dump -)
    printf '%s' "$other" | head -1 | grep -q 'hello'
    check "an APC belonging to something else is ignored, not eaten" $?

    # ⚠ AN UNTERMINATED SEQUENCE MUST NOT GROW A BUFFER FOREVER. This is the
    # denial-of-service shape of the feature: bytes arriving from the child
    # with no terminator in sight.
    apcout=$(python3 -c "import sys; sys.stdout.write('\033_G' + 'A'*40000 + '\033\\\\' + 'survived')" \
        | "$ST" --cols=20 --rows=2 dump -)
    printf '%s' "$apcout" | head -1 | grep -q 'survived'
    check "an over-long APC is dropped and the stream stays in step" $?
fi

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
# ⚠ SMALL CHUNKS ARE HARSHER FOR A MISSED MARK. One that happens to be covered
# because a later chunk redrew the same row passes at 64 KB and fails at 3 bytes.
#
# ⚠⚠ AND THEY ARE THE WRONG TOOL FOR A MISSED **ROW**, which is the other half
# of this and the half that was missing for three releases. The cursor forces a
# repaint of the row it left and the row it arrived at; feed a stream three
# bytes at a time and the cursor visits nearly every row between paints, doing
# the marking the grid failed to do. A whole-stream feed hands it only the first
# row and the last — which is what a pty delivers, because a read is 256 KB.
#
# So every case below is run at BOTH ends: chopped fine, and whole.
dmg() { local out; out=$(printf "$1" | "$ST" --cols="${2:-40}" --rows="${3:-12}" \
            damage-check - --split="${4:-3}" 2>&1); printf '%s\n' "$out"; }

# ── the whole-stream feed, which is the one a real terminal gets ────────────
#
# ⚠ THIS IS THE TEST THAT WAS MISSING, and its absence cost three releases of a
# terminal that dropped plain text on the floor. `put_run` — the printable
# ASCII fast path, i.e. what all ordinary output goes through — wrote cells and
# never set the row's dirty flag. Nothing caught it because every damage case
# here fed 3 bytes at a time, and at 3 bytes the cursor marks the rows for it.
#
# Under a compositor it looked like this: `syntty about` prints nineteen lines,
# the cursor touches the first and the last, and the seventeen in between were
# written and never drawn. They appeared the moment anything else dirtied them
# — a keystroke, a scroll, a drag-select — so the text seemed to be hiding
# rather than missing, and it read as a colour bug rather than a paint bug.
#
# Plain text, no escapes at all: the one thing a terminal must not lose.
dmg 'first line\r\nsecond line\r\nthird line\r\nfourth line\r\nfifth line\r\n' \
    40 12 4096 | grep -q '^identical'
check "a block of plain text fed WHOLE paints every row it wrote" $?

# The same, without carriage returns — a bare LF leaves the column alone, so
# each line starts further right and no two rows share a column. A row that is
# never painted cannot be excused by the one above it having covered it.
dmg 'aaa\nbbb\nccc\nddd\n' 40 12 4096 | grep -q '^identical'
check "...and so does a staircase, where no row covers for another" $?

scrollout=$(echo "$(seq 1 200)" | "$ST" --cols=80 --rows=24 damage-check - --split=7 2>&1)
printf '%s' "$scrollout" | grep -q '^identical'
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
# ⚠ CAPTURES, for the reason at the top of this file: a direct
# `producer | grep -q` dies of SIGPIPE when grep matches and exits early, and
# under `pipefail` that is 141 for a test that PASSED. ASan is slow enough to
# lose that race every time.
fontrun() { local out; out=$(XDG_CACHE_HOME="$FC" "$ST" font "$@" 2>&1); printf '%s\n' "$out"; }

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

    # ── the highlight, in pixels ────────────────────────────────────────────
    #
    # A selection that is remembered and not DRAWN is a selection nobody can
    # see, and every check above it — the text it copies, the anchor it keeps —
    # passes on a terminal that highlights nothing at all.
    #
    # It is an inversion rather than a colour on purpose: whatever a program
    # has painted underneath, inverting it contrasts with it by construction,
    # with no palette entry that has to be checked against every theme.
    hi=$(printf 'hello' | XDG_CACHE_HOME="$FC" "$ST" --cols=8 --rows=1 render - \
             --no-cursor --click=0,0 --drag=2,0 --probe=1,0 2>/dev/null | awk '{print $3}')
    off=$(printf 'hello' | XDG_CACHE_HOME="$FC" "$ST" --cols=8 --rows=1 render - \
              --no-cursor --click=0,0 --drag=2,0 --probe=4,0 2>/dev/null | awk '{print $3}')
    [ -n "$hi" ] && [ "$hi" = "$dfl" ] \
        && bad "a selected cell is drawn differently from an unselected one" \
        || ok "a selected cell is drawn differently from an unselected one"
    [ -n "$off" ] && [ "$off" = "$dfl" ] \
        && ok "...and the cell past the end of the selection is untouched" \
        || bad "...and the cell past the end of the selection is untouched ($off vs $dfl)"

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

    # ⚠ WHAT THIS SECTION CANNOT TEST, STATED SO NOBODY ASSUMES IT WAS.
    #
    # THE CLIPBOARD ROUND TRIP. Taking a selection needs a SERIAL from a real
    # input event — the compositor uses it to check that the client claiming
    # the clipboard was the one being used, which is what stops a background
    # window taking it. A headless cage has no keyboard and no pointer, so no
    # serial ever exists there and the claim never happens. Everything up to
    # that point is asserted above (`syntty paste`, OSC 52 through --stats);
    # the handover itself was verified by hand on a real compositor, against
    # wl-paste in another process, and there is no way to automate it here.
    #
    # PASTING likewise: it starts with somebody pressing Ctrl+Shift+V, and
    # input is never synthesised on a live session.
    #
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

    # ── `-e`, which is what makes this usable as THE terminal ───────────────
    #
    # Everything that launches "a terminal running something" emits -e: KDE's
    # KTerminalLauncherJob, xdg-terminal-exec, a Terminal=true .desktop through
    # xdg-open, and every script anyone has written. Until this shipped, syntty
    # answered `-e` with "unknown option" and exited 1 — so the desktop could
    # name it as the default terminal and every launch through one of those
    # paths would fail. No subcommand needed: -e implies the window.
    caged 30 -e /bin/sh -c 'exit 3' >/dev/null 2>&1
    [ $? -eq 3 ] && ok "-e runs the command in a window, no subcommand needed" \
                 || bad "-e runs the command in a window, no subcommand needed"

    # And after the subcommand too, because somebody who knows both conventions
    # writes it that way. Without it the child would be argv "-e /bin/sh …" and
    # the exec would fail on a program named -e.
    caged 30 win -e /bin/sh -c 'exit 3' >/dev/null 2>&1
    [ $? -eq 3 ] && ok "...and 'win -e CMD' means the same thing" \
                 || bad "...and 'win -e CMD' means the same thing"

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

    # ── tabs ────────────────────────────────────────────────────────────────
    #
    # ⚠ OPENING ONE IS A KEYSTROKE, WHICH THIS FILE CANNOT MAKE. Ctrl+Shift+T,
    # the bar, clicking a tab and switching between them all need a person, and
    # a headless cage has no keyboard at all. `--tabs=N` is not a test hook: it
    # is how somebody starts a window with several sessions in it, and it is
    # also the only door the suite has to any of this.
    #
    # What is asserted here is everything that does NOT need input: that the
    # sessions are real and separate, that one exiting does not take the window
    # with it, that the status is still the first tab's, and that the bar takes
    # a row from the grid the CHILD is told about.
    out=$(caged 30 --stats --tabs=3 win -- /bin/sh -c 'exit 0')
    echo "$out" | grep -q '^tabs          3 opened'
    check "--tabs=3 opens three sessions in one window" $?

    # ⚠ A TAB EXITING IS NOT THE WINDOW EXITING. Before tabs, any child hanging
    # up ended the loop — with four sessions open that would throw three of them
    # away, and the person would lose whatever was running in them.
    t0=$(date +%s%N)
    caged 30 --tabs=2 win -- /bin/sh -c \
        'test "$SYNTTY_TAB" = 1 && exit 3; sleep 1; exit 4' >/dev/null 2>&1
    rc=$?
    t1=$(date +%s%N)
    ms=$(( (t1 - t0) / 1000000 ))
    [ "$ms" -ge 900 ] \
        && ok "one tab exiting leaves the window open for the others (${ms} ms)" \
        || bad "one tab exiting leaves the window open for the others (${ms} ms)"

    # ⚠ AND THE STATUS IS STILL THE FIRST TAB'S. `syntty win -- make` asked
    # about make; opening a second tab to read something while it built must not
    # change what lands in $?. Here the first tab exits 3 immediately and the
    # second exits 4 a second later — the LAST to close is not the answer.
    [ "$rc" = 3 ] \
        && ok "...and the window still exits with the FIRST tab's status" \
        || bad "...and the window still exits with the FIRST tab's status (got $rc)"

    # SYNTTY_TAB is a stable serial, so a shell can say which session it is in.
    caged 30 --tabs=2 win -- /bin/sh -c "echo \$SYNTTY_TAB >> $T/serials" \
        >/dev/null 2>&1
    [ "$(sort -u "$T/serials" 2>/dev/null | tr -d '\n')" = "12" ] \
        && ok "each tab's child is told which tab it is (SYNTTY_TAB)" \
        || bad "each tab's child is told which tab it is (got '$(tr -d '\n' < "$T/serials" 2>/dev/null)')"

    # ⚠ THE BAR TAKES A ROW, AND THE CHILD HAS TO BE TOLD. Asked of `stty`
    # rather than of our own statistics, because what matters is the size the
    # PROGRAM believes it has: a bar drawn over a grid that still thinks it owns
    # the whole window puts the top line of every full-screen program underneath
    # the tabs.
    caged 30 --tabs=1 win -- /bin/sh -c "sleep 0.4; stty size > $T/size1" \
        >/dev/null 2>&1
    # ⚠ BOTH TABS HAVE TO STILL BE OPEN when the size is read. The first
    # version let tab 2 exit immediately — the bar went away with it, the grid
    # grew back by a row, and the measurement came out identical to the
    # one-tab case. The feature was working; the test was measuring a window
    # that no longer had a bar.
    caged 30 --tabs=2 win -- /bin/sh -c \
        "sleep 0.4; test \$SYNTTY_TAB = 1 && stty size > $T/size2; sleep 0.4" \
        >/dev/null 2>&1
    r1=$(awk '{print $1}' "$T/size1" 2>/dev/null)
    r2=$(awk '{print $1}' "$T/size2" 2>/dev/null)
    if [ -n "$r1" ] && [ -n "$r2" ]; then
        [ "$r2" = "$((r1 - 1))" ] \
            && ok "the tab bar costs the child exactly one row ($r1 -> $r2)" \
            || bad "the tab bar costs the child exactly one row ($r1 -> $r2)"
    else
        bad "the tab bar costs the child exactly one row (no size reported)"
    fi

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

    # ── the desktop switches theme while a terminal is open ─────────────────
    #
    # The whole point of the config watch, and the only place it can be proved:
    # it is a claim about PIXELS ON A SCREEN changing without a restart, so
    # nothing headless can answer it. `grim` works inside a headless cage
    # because cage implements screencopy, which is what makes this testable at
    # all rather than a thing verified once by eye and hoped for afterwards.
    #
    # ⚠ THE PALETTE IS RENAMED OVER, NOT WRITTEN IN PLACE, because that is what
    # the desktop helper does and it is the case a naive watch gets wrong: a
    # watch on the FILE follows the inode and stays attached to the old
    # contents, so it fires once and never again. Writing in place here would
    # pass with that bug present.
    if ! command -v grim >/dev/null 2>&1; then
        echo "  skip  the live recolour (grim is not installed)"
    else
        RC="$T/reload"
        mkdir -p "$RC/syntty"
        printf 'include = palette.conf\n' > "$RC/syntty/syntty.conf"
        printf 'background = #1b1f26\n' > "$RC/syntty/palette.conf"

        # The socket cage creates is the NEXT free one; the live desktop, if
        # there is one, already owns wayland-0. Taken from cage's own
        # environment rather than guessed — see
        # reference_wayland_connect_falls_back_to_live_socket for what guessing
        # costs: a client with no display connects to the REAL session.
        rm -f "$RC/sock"
        WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 \
        XDG_CONFIG_HOME="$RC" XDG_CACHE_HOME="$FC" \
        timeout 40 cage -- /bin/sh -c \
            "printf '%s' \"\$WAYLAND_DISPLAY\" > $RC/sock; \
             exec $ST --cols=40 --rows=6 win -- /bin/sh -c 'sleep 20'" \
            >/dev/null 2>&1 &
        cagepid=$!

        # Wait for the window rather than sleeping a guessed amount: a fixed
        # sleep is a test that passes on this machine and flakes on a slower one.
        shot=""
        for _ in $(seq 1 40); do
            [ -s "$RC/sock" ] && shot=$(cat "$RC/sock") && break
            sleep 0.25
        done

        first=""
        if [ -n "$shot" ]; then
            for _ in $(seq 1 40); do
                WAYLAND_DISPLAY="$shot" grim -t ppm "$RC/a.ppm" 2>/dev/null \
                    && first=1 && break
                sleep 0.25
            done
        fi

        if [ -z "$first" ]; then
            echo "  skip  the live recolour (no window came up under cage)"
        else
            # The theme switch, exactly as synui-apply-theme performs it.
            printf 'background = #c0c0c0\n' > "$RC/syntty/palette.tmp"
            mv -f "$RC/syntty/palette.tmp" "$RC/syntty/palette.conf"

            got=""
            for _ in $(seq 1 40); do
                WAYLAND_DISPLAY="$shot" grim -t ppm "$RC/b.ppm" 2>/dev/null || break
                # The new background as three raw bytes, anywhere in the image.
                if printf '\300\300\300' | grep -qaF -f - "$RC/b.ppm"; then
                    got=1; break
                fi
                sleep 0.25
            done
            [ -n "$got" ]
            check "a theme written under a RUNNING window recolours it in place" $?

            # ...and it really was a change, not a window that was silver all
            # along: the old background has to be gone.
            printf '\033\037\046' | grep -qaF -f - "$RC/b.ppm" \
                && bad "...and the old background is gone, not merely covered" \
                || ok "...and the old background is gone, not merely covered"
        fi
        kill "$cagepid" 2>/dev/null || true
        wait "$cagepid" 2>/dev/null || true
    fi
fi

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
