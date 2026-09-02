#!/usr/bin/env bash
#
# syn_edit_test.sh — the editing engine, driven with no terminal.
#
# ── Why this file can exist at all ──────────────────────────────────────────
#
# `syn-edit run --keys 'ggdG' file` applies a key sequence to a file and prints
# the result. That one subcommand is what makes a modal editor testable from a
# shell script: without it the only way to exercise `ciw` would be to drive a
# pty, which is awkward enough that in practice nobody does it and the editing
# engine ships on hope.
#
# So almost every test below is the same shape — some text in, some keys, the
# text that should come out — and it is the SAME code path the terminal and the
# window use. A test that passed here and failed in the window would mean the
# front-ends had grown editing logic of their own, which is the one thing the
# design forbids.
#
# ⚠ Everything happens inside a mktemp -d that the EXIT trap removes, and the
# only test that writes to a file at all is the one testing -w.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

# ⛔ THE PROGRAM UNDER TEST SPEAKS THIRTEEN LANGUAGES, AND THIS FILE ASSERTS
# ENGLISH. syn-edit's compiled-in localedir is /usr/share/locale, so on a
# machine where syn-edit is INSTALLED a freshly built binary loads the INSTALLED
# catalog and answers in the desktop's language — every assertion about a
# message then fails on a program that is working perfectly, and `meson test`
# failing is a BUILD failure, so `syn-update` refuses to install it. That is
# exactly what synpkg 47 did on a Japanese desktop.
#
# ⚠ Running this under LANG=ja on a box where syn-edit is not installed does NOT
# catch it: with no catalog to find, gettext falls back to the msgid and
# everything passes in English. Reproduce with SYN_EDIT_LOCALEDIR=build/po.
#
# ⚠ LANGUAGE as well as LC_ALL — gettext reads LANGUAGE FIRST, so an ambient
# LANGUAGE=ja survives an exported LC_ALL=C.UTF-8 on its own.
export LC_ALL=C.UTF-8
unset LANGUAGE

E=${1:-./build/syn-edit}
[ -x "$E" ] || { echo "not executable: $E" >&2; exit 1; }
E=$(readlink -f "$E")

pass=0 fail=0
ok()    { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()   { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# `((n++))` returns the OLD value, so a bare post-increment exits 1 the first
# time and would kill this script under set -e. Hence $((n + 1)) above.
#
# ⚠ Output of a command EXPECTED to exit non-zero: `set -o pipefail` makes the
# status of `cmd | grep` the last non-zero status in the pipeline, which for a
# refusal is the refusal itself rather than grep's verdict. Capture first,
# match second — says() always exits 0.
says() { local out; out=$("$@" 2>&1); printf '%s\n' "$out"; }

# ⚠ ...and the same trap from the other end: `producer | gq PATTERN`.
#
# grep -q exits at its FIRST match and closes the pipe. A producer still
# writing then dies of SIGPIPE, and under `set -o pipefail` the pipeline
# reports THAT, so the test fails even though the match succeeded. It is
# timing-dependent, which is the worst version of it: every one of these
# passed against the ordinary build, and two of them failed against the
# sanitiser build purely because it is slower.
#
# gq reads all of stdin before matching, so the pipe is never closed early.
#
# ⚠ ...but "all of stdin" must be BOUNDED, or the cure is worse than the
# disease. This was `out=$(cat)` until 2026-08-12, and a producer that loops
# instead of exiting made every one of those captures grow without limit. Run
# under `timeout`, which kills the script but NOT the subshells it left mid-
# capture, the orphans keep buffering: ~1000 of them reached 17 GB of shell
# heap, took the box to 27.5G of 31G, and the kernel OOM-killer took out
# user@1000.service — which is pipewire, wireplumber and the whole user bus.
# Losing the desktop's audio is a steep price for a grep.
#
# So cap the bytes. Every real producer here emits a few lines; blowing past
# the cap means the thing under test is looping, and the right outcome is a
# failed test, not a dead session. Hitting the cap closes the pipe, so the
# producer takes the SIGPIPE it would have taken from `grep -q` anyway —
# acceptable, because by then the test has already lost.
#
# ⚠ GQ_TIMEOUT bounds how long we wait FOR DATA, not the producer. `a | gq b`
# makes bash wait on every member of the pipeline, so a producer that hangs
# without ever writing still hangs the suite — this cannot fix that from the
# consumer end. What it does guarantee is that the wait is FLAT: no capture
# grows while it happens. meson's `timeout: 120` is the backstop for the hang
# itself. Measured against `yes | gq`: 36ms bounded, versus unbounded growth
# until the allocator gives up.
GQ_MAX_BYTES=${GQ_MAX_BYTES:-1048576}
GQ_TIMEOUT=${GQ_TIMEOUT:-10}

# Bounded slurp. Mirrors `$(cat)`: command substitution eats trailing newlines
# and the printf puts one back, so matching behaviour is unchanged for any
# output that fits.
_slurp() {
	local out
	out=$(timeout "$GQ_TIMEOUT" head -c "$GQ_MAX_BYTES")
	printf '%s\n' "$out"
}
gq()  { _slurp | command grep -q -- "$1"; }
gqv() { _slurp | command grep -qv -- "$1"; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# Settings must not leak in from the machine running the tests: expandtab or a
# different tabstop in the developer's own config would change what `>>` and
# `cc` produce and make the suite pass or fail depending on whose box it is.
export SYN_EDIT_CONFIG="$T/settings"

echo "syn-edit tests — $E"

# t <desc> <keys> <input> <expected>   — \n escapes are honoured in both texts
t() {
	local desc=$1 keys=$2 input=$3 want=$4 got exp
	printf '%b' "$input" > "$T/f.txt"
	got=$("$E" run -k "$keys" "$T/f.txt" 2>/dev/null)
	exp=$(printf '%b' "$want")
	if [ "$got" = "$exp" ]; then
		ok "$desc"
	else
		bad "$desc"
		printf '          want: %s\n          got:  %s\n' \
		       "$(printf '%s' "$exp" | tr '\n' '|')" \
		       "$(printf '%s' "$got" | tr '\n' '|')" >&2
	fi
}

# x <desc> <ex-command> <input> <expected>
x() {
	local desc=$1 cmd=$2 input=$3 want=$4 got exp
	printf '%b' "$input" > "$T/f.txt"
	got=$("$E" run -c "$cmd" "$T/f.txt" 2>/dev/null)
	exp=$(printf '%b' "$want")
	if [ "$got" = "$exp" ]; then
		ok "$desc"
	else
		bad "$desc"
		printf '          want: %s\n          got:  %s\n' \
		       "$(printf '%s' "$exp" | tr '\n' '|')" \
		       "$(printf '%s' "$got" | tr '\n' '|')" >&2
	fi
}

THREE='alpha beta gamma\nsecond line here\nthird\n'

# ── the binary answers ──────────────────────────────────────────────────────

"$E" --version | gq '^syn-edit '
check "--version prints a version" $?

"$E" --help | gq 'the SynapseOS text editor'
check "--help prints usage" $?

"$E" --help | gq 'PERCENT-ENCODED'
check "--help states the record encoding" $?

says "$E" --nonsense | gq "unknown option"
check "an unknown option is refused" $?

# A name that is not a command is a FILE, not an error — see main.c.
says "$E" some-file-name | gq 'not a terminal'
check "a non-command argument is treated as a file" $?

# ── loading and saving are byte-faithful ────────────────────────────────────

printf 'one\ntwo\n' > "$T/lf.txt"
[ "$("$E" run -k '' "$T/lf.txt" | od -c | head -1)" = \
  "$(od -c < "$T/lf.txt" | head -1)" ]
check "a plain LF file round-trips unchanged" $?

printf 'one\r\ntwo\r\n' > "$T/crlf.txt"
"$E" run -k '' "$T/crlf.txt" | od -c | gq '\\r'
check "CRLF line endings survive a round trip" $?

printf 'no trailing newline' > "$T/noeol.txt"
[ "$("$E" run -k '' "$T/noeol.txt" | wc -c)" = 19 ]
check "a missing final newline is NOT invented" $?

printf 'one\ntwo\n' > "$T/eol.txt"
[ "$("$E" run -k '' "$T/eol.txt" | wc -c)" = 8 ]
check "a present final newline is kept" $?

printf 'a\0b\n' > "$T/bin.txt"
"$E" run --status "$T/bin.txt" 2>/dev/null | gq 'binary'
check "a file with a NUL opens and is reported as binary" $?

"$E" run -k '' "$T/does-not-exist-at-all" >/dev/null 2>&1
check "opening a name that does not exist yet is not an error" $?

says "$E" run -k '' "$T" | gq 'is a directory'
check "opening a directory is refused" $?

# -w is the ONLY thing here that writes.
printf 'keep\nme\n' > "$T/w.txt"
chmod 755 "$T/w.txt"
"$E" run -k 'ddZZ' -w "$T/w.txt" >/dev/null 2>&1
[ "$(cat "$T/w.txt")" = "me" ]
check "-w writes the change back" $?

[ "$(stat -c%a "$T/w.txt")" = 755 ]
check "saving preserves the file's permission bits" $?

printf 'untouched\n' > "$T/now.txt"
"$E" run -k 'dd' "$T/now.txt" >/dev/null 2>&1
[ "$(cat "$T/now.txt")" = "untouched" ]
check "without -w the file on disk is NOT modified" $?

# The temporary a save writes through must not be left behind.
ls "$T"/.syn-edit.* >/dev/null 2>&1
[ $? -ne 0 ] && ok "no temporary files are left after a save" \
             || bad "no temporary files are left after a save"

# ── motions ─────────────────────────────────────────────────────────────────

t "l moves right"            'lx'      'abcd\n'           'acd'
t "h moves left"             'llhx'    'abcd\n'           'acd'
t "3l takes a count"         '3lx'     'abcde\n'          'abce'
t "0 goes to the first byte" '$0x'     'abcd\n'           'bcd'
t "^ goes to the first non-blank" '$^x'  '  abcd\n'       '  bcd'
t "$ goes to the last byte"   '$x'      'abcd\n'           'abc'
t "w moves a word"           'wx'      'one two\n'        'one wo'
t "b moves back a word"      '$bx'     'one two\n'        'one wo'
t "e moves to a word end"    'ex'      'one two\n'        'on two'
t "W skips punctuation"      'Wx'      'a.b c\n'          'a.b '
t "gg goes to the top"       'Ggglx'   'aa\nbb\ncc\n'     'a\nbb\ncc'
t "G goes to the bottom"     'Gx'      'aa\nbb\ncc\n'     'aa\nbb\nc'
t "2G goes to a line"        '2Gx'     'aa\nbb\ncc\n'     'aa\nb\ncc'
t "j and k move by line"     'jjkx'    'aa\nbb\ncc\n'     'aa\nb\ncc'
t "j keeps the wanted column" '$jjx'   'abcd\ne\nabcd\n'  'abcd\ne\nabc'
t "} moves a paragraph"      '}x'      'a\n\nb\n'         'a\n\nb'
t "{ moves back a paragraph" 'G{x'     'a\n\nb\n'         'a\n\nb'
t "| goes to a column"       '3|x'     'abcde\n'          'abde'
t "f finds forward"          'fcx'     'abcd\n'           'abd'
t "F finds backward"         '$Fbx'    'abcd\n'           'acd'
t "t stops before"           'tcx'     'abcd\n'           'acd'
t "; repeats a find"         'f.;x'    'a.b.c\n'          'a.bc'
t ", reverses a find"        'f.f.,x'  'a.b.c\n'          'ab.c'
t "% matches a bracket"      '%x'      'a(bc)d\n'         'a(bcd'
t "empty buffer survives motions" 'wwbbjjkkx' '\n'        ''

# ── operators × motions ─────────────────────────────────────────────────────

t "dw deletes a word"        'dw'      "$THREE"  'beta gamma\nsecond line here\nthird'
t "d$ deletes to the end"    'wd$'     'one two three\n'  'one '
t "d0 deletes to the start"  '$d0'     'one two\n'        'o'
t "dd deletes a line"        'dd'      "$THREE"  'second line here\nthird'
t "2dd takes a count"        '2dd'     "$THREE"  'third'
t "d2w spans two words"      'd2w'     'a b c d\n'        'c d'
t "2dw is the same as d2w"   '2dw'     'a b c d\n'        'c d'
t "dj deletes two lines"     'dj'      "$THREE"  'third'
t "dG deletes to the end"    'jdG'     "$THREE"  'alpha beta gamma'
t "dgg deletes to the top"   'jdgg'    "$THREE"  'third'
t "df deletes through"       'dfb'     'a-b-c\n'          '-c'
t "dt stops before"          'dtb'     'a-b-c\n'          'b-c'
t "D deletes to the end"     'wD'      'one two\n'        'one '
t "x deletes a character"    'x'       'abc\n'            'bc'
t "3x takes a count"         '3x'      'abcde\n'          'de'
t "x at the end of a line does not join" 'x' 'ab\ncd\n'   'b\ncd'
t "X deletes backwards"      '$X'      'abc\n'            'ac'
t "cw changes a word"        'cwX<Esc>' 'one two\n'       'X two'
t "cw does NOT eat the space" 'cwX<Esc>' 'one two\n'      'X two'
t "ce changes a word"        'ceX<Esc>' 'one two\n'       'X two'
t "cc keeps the indent"      'ccX<Esc>' '    old\n'       '    X'
t "C changes to the end"     'wCX<Esc>' 'one two\n'       'one X'
t "s substitutes a char"     'sX<Esc>' 'abc\n'            'Xbc'
t "S substitutes a line"     'SX<Esc>' 'abc\ndef\n'       'X\ndef'
t "yyp duplicates a line"    'yyp'     'a\nb\n'           'a\na\nb'
t "yyP puts before"          'jyyP'    'a\nb\n'           'a\nb\nb'
t "J joins lines"            'J'       'a\nb\n'           'a b'
t "3J joins three"           '3J'      'a\nb\nc\n'        'a b c'
t "gJ joins without a space" 'gJ'      'a\nb\n'           'ab'
t "> indents"                '>>'      'a\n'              '\ta'
t "< outdents"               '>><<'    'a\n'              'a'
t ">j indents two lines"     '>j'      'a\nb\n'           '\ta\n\tb'
t "> leaves a blank line alone" '>j'   'a\n\n'            '\ta\n'
t "~ flips case"             '~'       'abc\n'            'Abc'
t "3~ flips three"           '3~'      'abc\n'            'ABC'
t "guw lowercases"           'guw'     'ABC def\n'        'abc def'
t "gUw uppercases"           'gUw'     'abc def\n'        'ABC def'
t "r replaces one"           'rZ'      'abc\n'            'Zbc'
t "3r replaces three"        '3rZ'     'abcd\n'           'ZZZd'
t "r refuses past the line end" '9rZ'  'abc\n'            'abc'

# ── insert mode ─────────────────────────────────────────────────────────────

t "i inserts"                'iX<Esc>' 'abc\n'            'Xabc'
t "a appends"                'aX<Esc>' 'abc\n'            'aXbc'
t "A appends at the end"     'AX<Esc>' 'abc\n'            'abcX'
t "I inserts at the first non-blank" 'IX<Esc>' '  abc\n'  '  Xabc'
t "o opens below"            'oX<Esc>' 'abc\n'            'abc\nX'
t "O opens above"            'OX<Esc>' 'abc\n'            'X\nabc'
t "o keeps the indent"       'oX<Esc>' '    a\n'          '    a\n    X'
t "3i repeats the insert"    '3iab<Esc>' 'Z\n'            'abababZ'
t "R overwrites"             'RXY<Esc>' 'abcd\n'          'XYcd'
t "R past the end extends"   'RXYZ<Esc>' 'a\n'            'XYZ'
t "Enter splits a line"      'a<CR><Esc>' 'ab\n'          'a\nb'
t "backspace joins lines"    'j0i<BS><Esc>' 'ab\ncd\n'    'abcd'
t "backspace in a line"      'lli<BS><Esc>' 'abc\n'       'ac'

# 3i repeats the TYPED text; the check above is deliberately odd-looking
# because the first copy is typed at the cursor and the rest follow it.
t "3iab then Escape leaves 3 copies" '3iab<Esc>' '\n'     'ababab'

# ── text objects ────────────────────────────────────────────────────────────

t "diw deletes a word"       'wdiw'    'one two three\n'  'one  three'
t "daw takes the space too"  'wdaw'    'one two three\n'  'one three'
t "ciw changes a word"       'wciwX<Esc>' 'one two three\n' 'one X three'
t 'di" empties a string'     'di"'     'x = "hi" ;\n'     'x = "" ;'
t 'da" takes the quotes'     'da"'     'x = "hi" ;\n'     'x =  ;'
t 'di" from outside finds its pair' 'di"' 'a "b" c\n'     'a "" c'
t "di( empties brackets"     'fadi('   'foo(a, b) end\n'  'foo() end'
t "da( takes the brackets"   'fada('   'foo(a, b) end\n'  'foo end'
t "di[ works"                'fadi['   'x[abc]\n'         'x[]'
t "di{ works"                'fadi{'   'x{abc}\n'         'x{}'
t "dip deletes a paragraph"  'dip'     'a\nb\n\nc\n'      '\nc'
t "cib is di( with a change" 'facibZ<Esc>' 'f(a)\n'       'f(Z)'

# ── registers ───────────────────────────────────────────────────────────────

t "a named register"         '"ayyj"ap' 'a\nb\n'          'a\nb\na'
t "the unnamed register"     'yyjp'    'a\nb\n'           'a\nb\na'
t "delete fills the unnamed register" 'ddp' 'a\nb\n'      'b\na'
t "an uppercase name appends" '"ayyj"Ayygg"ap' 'a\nb\n'   'a\na\nb\nb'
t 'the "1 register holds the last line delete' 'dd"1p' 'a\nb\n' 'b\na'
t "x fills the small-delete register" 'x$"-p'   'ab\n'     'ba'

# ── undo and redo ───────────────────────────────────────────────────────────

t "u undoes"                 'ddu'     "$THREE"  'alpha beta gamma\nsecond line here\nthird'
t "u undoes an insert as one" 'ihello<Esc>u' 'x\n'        'x'
t "Ctrl-R redoes"            'ddu<C-r>' 'a\nb\n'          'b'
t "u undoes a multi-line operator as ONE" '>ju' 'a\nb\n'  'a\nb'
t "u at the oldest change is safe" 'uuuu' 'a\n'           'a'
t "redo past the newest is safe" '<C-r><C-r>' 'a\n'       'a'
t "typing after an undo drops the redo branch" 'ddu<C-r>u' 'a\nb\n' 'a\nb'
t "2u undoes twice"          'xx2u'    'abc\n'            'abc'

# ── the dot register ────────────────────────────────────────────────────────

t "dot repeats a delete"     'dw.'     'a b c\n'          'c'
t "dot repeats an insert"    'iX<Esc>.' 'a\n'             'XXa'
t "dot repeats x"            'x..'     'abcd\n'           'd'
t "dot repeats a change"     'ciwZ<Esc>w.' 'aa bb\n'      'Z Z'
t "dot with no previous change is safe" '.' 'a\n'         'a'
t "dot repeats an operator with a count" '2dd.' 'a\nb\nc\nd\ne\n' 'e'

# ── macros ──────────────────────────────────────────────────────────────────

t "a macro records and replays" 'qaA!<Esc>jq@a' '1\n2\n3\n'  '1!\n2!\n3'
t "a macro takes a count"       'qaA!<Esc>jq2@a' '1\n2\n3\n' '1!\n2!\n3!'
t "@@ repeats the last macro"   'qaA!<Esc>jq@a@@' '1\n2\n3\n' '1!\n2!\n3!'
t "an empty register is safe"   '@z'    'a\n'             'a'
# A macro that plays itself must stop rather than hang.
timeout 5 "$E" run -k 'qaA!<Esc>@aq@a' <(printf 'x\n') >/dev/null 2>&1
[ $? -ne 124 ] && ok "a recursive macro terminates" \
               || bad "a recursive macro terminates"

# ── search ──────────────────────────────────────────────────────────────────

t "/ searches forward"       '/line<CR>x' "$THREE"  'alpha beta gamma\nsecond ine here\nthird'
t "n repeats a search"       '/e<CR>nx'   'e e e\n'       'e e '
t "? searches backward"      'G?alpha<CR>x' "$THREE" 'lpha beta gamma\nsecond line here\nthird'
t "a search wraps around"    'G/alpha<CR>x' "$THREE" 'lpha beta gamma\nsecond line here\nthird'
t "* searches for the word"  '*x'      'foo bar\nfoo baz\n' 'foo bar\noo baz'
t "* respects word boundaries" '*x'    'count\ncounter\ncount\n' 'count\ncounter\nount'
t "a search that finds nothing is safe" '/zzzz<CR>x' 'abc\n' 'bc'
t "^ anchors to the line start" '/^b<CR>x' 'ab\nbc\n'    'ab\nc'

# ── ex commands ─────────────────────────────────────────────────────────────

x "s replaces the first"     's/a/X/'      'aaa\n'        'Xaa'
x "s with g replaces all"    's/a/X/g'     'aaa\n'        'XXX'
x "%s covers the file"       '%s/a/X/g'    'aa\naa\n'     'XX\nXX'
x "a line range"             '2s/a/X/'     'aa\naa\n'     'aa\nXa'
x "a two-line range"         '1,2s/a/X/'   'aa\naa\naa\n' 'Xa\nXa\naa'
x "a \$ range"               '2,$s/a/X/'   'aa\naa\naa\n' 'aa\nXa\nXa'
x "an offset range"          '1,+1s/a/X/'  'aa\naa\naa\n' 'Xa\nXa\naa'
x "& is the whole match"     's/b*/[&]/'   'bbc\n'        '[bb]c'
x "a group reference"        's/\(a\)\(b\)/\2\1/' 'ab\n' 'ba'
x "alternation with \|"      's/\(a\|b\)/X/g' 'ab\n'  'XX'
x "\+ repeats"               's/a\+/X/'    'aaab\n'      'Xb'
x "\v switches to ERE"       's/\v(a|b)+/X/' 'abab c\n' 'X c'
x "a non-slash delimiter"    's#/usr#/opt#' '/usr/bin\n'  '/opt/bin'
x "an empty pattern is safe" 's/x*/-/'      'abc\n'       '-abc'
x "an empty pattern with g terminates" 's/x*/-/g' 'ab\n'  '-a-b-'
x "\r splits a line"         's/,/\r/g'    'a,b\n'      'a\nb'
x "d deletes a line"         '2d'          'a\nb\nc\n'    'a\nc'
x "a range delete"           '1,2d'        'a\nb\nc\n'    'c'
x "%d empties the file"      '%d'          'a\nb\n'       ''
x "m moves a line"           '1m$'         'a\nb\nc\n'    'b\nc\na'
x "m0 moves to the top"      '3m0'         'a\nb\nc\n'    'c\na\nb'
x "t copies a line"          '1t$'         'a\nb\n'       'a\nb\na'
x "a bare number jumps"      '2'           'a\nb\nc\n'    'a\nb\nc'
x "j joins"                  '1,2j'        'a\nb\nc\n'    'a b\nc'
x "> indents a range"        '1,2>'        'a\nb\nc\n'    '\ta\n\tb\nc'
x "sort orders lines"        '%sort'       'c\na\nb\n'    'a\nb\nc'
x "sort u removes duplicates" '%sort u'    'b\na\nb\n'    'a\nb'
x "normal runs keys"         '%normal A!'  'a\nb\n'       'a!\nb!'
x "g runs on matching lines" 'g/x/d'       'ax\nb\ncx\n'  'b'
x "v runs on non-matching"   'v/x/d'       'ax\nb\ncx\n'  'ax\ncx'
x "g with s"                 'g/a/s/a/X/'  'a\nb\na\n'    'X\nb\nX'
x "an unknown command is refused" 'frobnicate' 'a\n'      'a'
x "set changes an option"    'set expandtab' 'a\n'        'a'

printf 'a\n' > "$T/f.txt"
says "$E" run -c 'frobnicate' "$T/f.txt" | gq 'not an editor command'
check "an unknown ex command names itself" $?

# :g deleting several lines must not delete the WRONG ones — the reason it
# runs backwards. Forwards, the first delete shifts every later line number.
x "g/d deletes exactly the matches" 'g/^x/d' 'x1\nkeep\nx2\nkeep2\nx3\n' 'keep\nkeep2'

# ── quitting and refusals ───────────────────────────────────────────────────

printf 'a\n' > "$T/q.txt"
says "$E" run -k 'dd:q<CR>' "$T/q.txt" | gq 'unsaved changes'
check ":q refuses a modified buffer" $?

says "$E" run -k 'dd:q<CR>' "$T/q.txt" | gq 'q.txt'
check "the refusal NAMES the file" $?

"$E" run -k 'dd:q!<CR>' "$T/q.txt" >/dev/null 2>&1
check ":q! discards and quits" $?

printf 'a\n' > "$T/zz.txt"
"$E" run -k 'ddZZ' -w "$T/zz.txt" >/dev/null 2>&1
[ "$(cat "$T/zz.txt")" = "" ]
check "ZZ writes and quits" $?

# ── visual mode ─────────────────────────────────────────────────────────────

t "v then d deletes a span"  'vld'     'abcd\n'           'cd'
t "V selects a line"         'Vd'      'a\nb\n'           'b'
t "Vj selects two lines"     'Vjd'     'a\nb\nc\n'        'c'
t "v then y yanks"           'vlyP'    'abc\n'            'ababc'
t "visual > indents"         'Vj>'     'a\nb\n'           '\ta\n\tb'
t "visual ~ flips case"      'vll~'    'abc\n'            'ABC'
t "visual gu lowercases"     'vllgu'   'ABC\n'            'abc'
t "visual r replaces all"    'vllrZ'   'abcd\n'           'ZZZd'
t "visual c changes"         'vlcX<Esc>' 'abcd\n'         'Xcd'
# ⚠ gc needs a file whose LANGUAGE has a comment syntax, so these cannot go
# through t() — it writes f.txt, which is plain text and has none. Asserted
# against .c, or both tests would have passed while doing nothing at all.
printf 'int x;\nint y;\n' > "$T/gc.c"
[ "$("$E" run -k 'Vjgc' "$T/gc.c")" = "$(printf '// int x;\n// int y;')" ]
check "visual gc comments" $?

[ "$("$E" run -k 'VjgcggVjgc' "$T/gc.c")" = "$(printf 'int x;\nint y;')" ]
check "gc again uncomments" $?
t "Escape leaves visual"     'vll<Esc>x' 'abcd\n'         'abd'
t "visual iw selects a word" 'wviwd'   'one two three\n'  'one  three'
t "visual p replaces the selection" 'yyjVp' 'a\nb\n'      'a\na'

# ── comment toggling knows the language ─────────────────────────────────────

printf 'x = 1\ny = 2\n' > "$T/f.py"
[ "$("$E" run -k 'Vjgc' "$T/f.py")" = "$(printf '# x = 1\n# y = 2')" ]
check "gc uses # in Python" $?

printf 'a\nb\n' > "$T/f.unknown-ext"
says "$E" run -k 'Vjgc' "$T/f.unknown-ext" | gq 'no comment syntax'
check "gc refuses a file type it has no comment syntax for" $?

# ── syntax highlighting ─────────────────────────────────────────────────────

printf 'int main(void) { return 0; }\n' > "$T/h.c"
"$E" --rec highlight "$T/h.c" | gq 'keyword'
check "C keywords are highlighted" $?

"$E" --rec highlight "$T/h.c" | gq 'func'
check "a call is highlighted as a function" $?

printf '/* a\n   comment */ int x;\n' > "$T/h2.c"
[ "$("$E" --rec highlight "$T/h2.c" | awk -F'\t' '$1==2 && $4=="comment"' | wc -l)" -ge 1 ]
check "a block comment carries to the next line" $?

printf '"unterminated\nint x;\n' > "$T/h3.c"
"$E" --rec highlight "$T/h3.c" | awk -F'\t' '$1==2' | gqv 'string'
check "an unterminated string does NOT swallow the next line" $?

printf 'echo "a\\"b" done\n' > "$T/h.sh"
"$E" --rec highlight "$T/h.sh" | tail -1 | gqv 'string'
check "an escaped quote inside a shell string does not run away" $?

# The spans must cover every byte with no gaps — a renderer walks them and
# never asks what the bytes between two spans were.
printf 'int x = 1; /* c */\n' > "$T/h4.c"
"$E" --rec highlight "$T/h4.c" | awk -F'\t' '
	NR==1 { next }
	{ if ($2 != want) bad=1; want = $2 + $3 }
	END { exit (bad ? 1 : 0) }'
check "highlight spans cover the line with no gaps" $?

# Language detection.
for pair in "f.c:c" "f.h:c" "f.cpp:cpp" "f.py:python" "f.sh:sh" "f.qml:qml" \
            "f.json:json" "f.rs:rust" "f.go:go" "f.md:markdown" "f.yml:yaml" \
            "Makefile:makefile" "meson.build:meson" "f.diff:diff"; do
	name=${pair%%:*}; want=${pair##*:}
	printf 'x\n' > "$T/$name"
	got=$("$E" run --status "$T/$name" 2>/dev/null | \
	      awk -F'\t' '$1=="language"{print $2}')
	[ "$got" = "$want" ] && ok "$name is detected as $want" \
	                     || bad "$name is detected as $want (got '$got')"
done

# ".h" must not match inside ".hpp" — the boundary check in in_list().
printf 'x\n' > "$T/f.hpp"
[ "$("$E" run --status "$T/f.hpp" 2>/dev/null | awk -F'\t' '$1=="language"{print $2}')" = cpp ]
check ".hpp is C++, not C (the extension is matched whole)" $?

printf '#!/usr/bin/env python3\nx = 1\n' > "$T/noext"
[ "$("$E" run --status "$T/noext" 2>/dev/null | awk -F'\t' '$1=="language"{print $2}')" = python ]
check "a shebang identifies a file with no extension" $?

"$E" langs | gq '^  c$'
check "langs lists the languages" $?

# ── options ─────────────────────────────────────────────────────────────────

rm -f "$SYN_EDIT_CONFIG"
"$E" config set expandtab true >/dev/null
[ "$("$E" config get expandtab)" = true ]
check "config set then get round-trips" $?

[ -f "$SYN_EDIT_CONFIG" ]
check "the settings file is written where it was asked for" $?

says "$E" config set nonsense true | gq 'unknown option'
check "an unknown option is refused, not stored" $?

says "$E" config set expandtab maybe | gq 'true or false'
check "a bad boolean is refused" $?

"$E" config set tabstop 999 >/dev/null
[ "$("$E" config get tabstop)" = 16 ]
check "an out-of-range number is clamped, not refused" $?

"$E" config reset >/dev/null
[ "$("$E" config get expandtab)" = false ]
check "config reset restores the defaults" $?

# expandtab actually changes what is inserted.
"$E" config set expandtab true >/dev/null
"$E" config set shiftwidth 2 >/dev/null
[ "$("$E" run -k '>>' "$T/f.txt" 2>/dev/null | head -1 | cut -c1-2)" = "  " ]
check "expandtab makes >> insert spaces" $?
"$E" config reset >/dev/null

# A settings file full of rubbish must not stop the editor starting.
printf 'this is not = a setting\nnonsense\n' > "$SYN_EDIT_CONFIG"
"$E" run -k '' "$T/f.txt" >/dev/null 2>&1
check "a corrupt settings file does not stop the editor" $?
rm -f "$SYN_EDIT_CONFIG"

# ── the record protocol ─────────────────────────────────────────────────────

# A tab in a line MUST NOT shift the columns of a record.
printf 'a\tb\n' > "$T/tab.txt"
[ "$(printf 'render\nquit\n' | "$E" serve "$T/tab.txt" | \
     awk -F'\t' '$1=="L"' | head -1 | awk -F'\t' '{print NF}')" = 3 ]
check "a tab in a line does not add a field to its record" $?

# The window is sent the line as it will LOOK — tabs already expanded — and
# spans in display columns, so it never re-derives either. See expand_line().
printf 'render\nquit\n' | "$E" serve "$T/tab.txt" | gq '^L	1	a%20%20%20b$'
check "a tab is expanded for display, and the spaces are encoded" $?

# The encoding still has to be there: a PATH can hold a tab even when a
# displayed line no longer does.
printf 'x\n' > "$T/has	tab.txt"
printf 'quit\n' | "$E" serve "$T/has	tab.txt" | gq '^S	file	.*%09tab'
check "a tab in a FILE NAME is percent-encoded" $?

printf 'keys iX%%3Cq%%3E<Esc>\nquit\n' >/dev/null   # (notation, not run)

[ "$(printf 'quit\n' | "$E" serve "$T/tab.txt" | grep -c '^E	')" -ge 1 ]
check "a frame ends with an E record" $?

printf 'keys x\nquit\n' | "$E" serve "$T/tab.txt" | gq '^L	1	%20%20%20%20b$'
check "serve applies keys and re-renders" $?

printf 'ex %%25s%%2Fa%%2FZ%%2F\nquit\n' | "$E" serve "$T/tab.txt" | gq 'Z%20%20%20b'
check "serve runs a percent-encoded ex command" $?

printf 'quit\n' | "$E" serve "$T/tab.txt" | gq '^S	mode	NORMAL$'
check "serve reports the mode" $?

printf 'keys i\nquit\n' | "$E" serve "$T/tab.txt" | gq '^S	mode	INSERT$'
check "serve reports insert mode" $?

printf 'keys %%3A\nquit\n' | "$E" serve "$T/tab.txt" | gq '^S	cmdline	%3A$'
check "serve reports the command line being typed" $?

printf 'quit\n' | "$E" serve "$T/tab.txt" | gq '^B	1	'
check "serve lists the open buffers" $?

printf 'view 0 1\nquit\n' | "$E" serve <(printf 'a\nb\nc\n') >/dev/null 2>&1
check "serve accepts a viewport" $?

printf 'bogus-request\nquit\n' | "$E" serve "$T/tab.txt" | gq 'unknown%20request'
check "serve names an unknown request instead of dying" $?

# The dcol field exists so the window does not reimplement tab expansion.
printf 'keys l\nquit\n' | "$E" serve "$T/tab.txt" | gq '^S	dcol	'
check "serve reports the display column" $?

# ── naming a buffer that has never had a name ───────────────────────────────
#
# `named` exists so the window can tell "this has a path" from "this is called
# [No Name]" WITHOUT matching that string. Matching it would break the day the
# placeholder is reworded, and would call a real file named "[No Name]"
# unnamed. The window needs the answer to know that Save must ask first.
printf 'quit\n' | "$E" serve "$T/tab.txt" | gq '^S	named	1$'
check "serve reports a buffer that has a path as named" $?

printf 'quit\n' | "$E" serve | gq '^S	named	0$'
check "...and a fresh buffer as unnamed" $?

printf 'keys ihi\nsave\nquit\n' | "$E" serve | gq '^S	msg	no%20file%20name$'
check "saving an unnamed buffer is still refused by the engine" $?

# `save <path>` is a real Save As: it writes AND adopts the name, so the next
# plain Save goes to the same place rather than refusing again.
rm -f "$T/named-as.txt"
printf 'keys ihello\nsave %s\nquit\n' "$T/named-as.txt" | "$E" serve >/dev/null 2>&1
[ "$(cat "$T/named-as.txt" 2>/dev/null)" = "hello" ]
check "save <path> writes the buffer" $?

printf 'keys ihello\nsave %s\nquit\n' "$T/named-as.txt" | "$E" serve | gq '^S	named	1$'
check "...and the buffer adopts the name it was given" $?

# The route the WINDOW actually drives: it does not send `save <path>` for a
# new name — it opens the engine's command line on `:w <dir>/` and lets the
# editor collect the rest. Pinned here because it is the whole fix.
rm -f "$T/typed-in.txt"
printf 'keys ityped\nkeys %%1B\nkeys :w %s\nkeys <CR>\nquit\n' "$T/typed-in.txt" |
    "$E" serve >/dev/null 2>&1
[ "$(cat "$T/typed-in.txt" 2>/dev/null)" = "typed" ]
check ":w <name> from the command line names and writes it" $?

# ⚠ The <Esc> in front is load-bearing, and this is the assertion that proves
# it: WITHOUT leaving insert mode, ":w …" is text. If promptWrite ever stops
# escaping, the Save button types its own command into the document.
rm -f "$T/no-esc.txt"
printf 'keys ityped\nkeys :w %s\nkeys <CR>\nquit\n' "$T/no-esc.txt" |
    "$E" serve >/dev/null 2>&1
[ ! -f "$T/no-esc.txt" ]
check "...and without the Esc it is typed into the buffer instead" $?

# ── a button is not a keystroke ─────────────────────────────────────────────
#
# The same defect as the Save one above, in every other button: in INSERT mode
# the keys a toolbar sends are not commands. Find typed "/" into the document
# and Replace typed ":%s/", which is data loss dressed as a no-op — the caret
# does not move, so it reads as a button that did nothing.
printf 'a\n' > "$T/act.txt"
printf 'keys ihello\nkeys /\nquit\n' | "$E" serve "$T/act.txt" | gq '^L	1	hello/'
check "Find's key IS typed into the buffer when insert mode is not left" $?

printf 'keys ihello\nkeys <Esc>\nkeys /\nquit\n' | "$E" serve "$T/act.txt" |
    gq '^S	cmdline	/$'
check "...and opens the search prompt once it is" $?

printf 'keys ihello\nkeys <Esc>\nkeys :%%25s/\nquit\n' | "$E" serve "$T/act.txt" |
    gq '^S	cmdline	%3A%25s/$'
check "Replace opens the substitute prompt after an Esc" $?

# ⚠ AND NOT UNCONDITIONALLY. Copy and Cut are `"+y` / `"+d`, which act on the
# VISUAL selection — <Esc> drops it and the yank then waits for a motion and
# takes different text. This pair is the whole reason actKeys() is guarded on
# insert mode rather than always escaping, and it is the assertion that fails
# if somebody ever "tidies" that guard away.
printf 'abc def\n' > "$T/vis.txt"
printf 'keys vll\nkeys "ay\nkeys $\nkeys "ap\nquit\n' | "$E" serve "$T/vis.txt" |
    gq '^L	1	abc%20defabc$'
check "a visual yank takes the selection" $?

printf 'keys vll\nkeys <Esc>\nkeys "ay\nkeys $\nkeys "ap\nquit\n' |
    "$E" serve "$T/vis.txt" | gq '^L	1	abc%20defabc$'
[ $? != 0 ]
check "...and an Esc in front of it would take something else" $?

# ── what the WINDOW sends ───────────────────────────────────────────────────
#
# The graphical window has no selection, no scroll position and no idea what a
# word is: clicking, dragging, Shift+Arrow and the scrollbar are all translated
# into keys the engine already understands. That makes them testable from here,
# with no window — and it makes them BREAKABLE from here, which is the point.
# If `NG` or `N|` ever stopped extending a visual selection, nothing in the QML
# would error; dragging would just stop selecting.
# Forty lines, each WIDE ENOUGH to hold the columns these tests ask for. The
# first cut used `seq 1 40`, where line 3 is one character long — `5|` clamped
# to the end of it and the test read as "the caret does not follow a click"
# when the truth was that column 5 was not on that line.
awk 'BEGIN { for (i = 1; i <= 40; i++) printf "line %02d abcdefghij\n", i }' > "$T/nav.txt"

# A click: line N, display column C. Not `:N` — an ex command is not a motion,
# and the drag below needs one that extends the selection.
printf 'keys 3G5|\nquit\n' | "$E" serve "$T/nav.txt" | gq '^S	col	5$'
check "NG N| places the caret where a click was" $?

# A drag: v once, then keep moving. The anchor stays put because that is what
# visual mode does with it.
printf 'keys 3G\nkeys v\nkeys 5G3|\nquit\n' | "$E" serve "$T/nav.txt" | gq '^S	sel_y1	5$'
check "v then NG N| extends the selection — the drag" $?

# Shift+Arrow is the same trick with a different motion.
printf 'keys 3G\nkeys v\nkeys <Right>\nquit\n' | "$E" serve "$T/nav.txt" | gq '^S	mode	VISUAL$'
check "an arrow key extends a visual selection — Shift+Arrow" $?

printf 'keys ggVG\nquit\n' | "$E" serve "$T/nav.txt" | gq '^S	sel_y1	40$'
check "ggVG selects the whole file — Select All" $?

# ⚠ THE SCROLLBAR CONTRACT, and it is not obvious in either direction.
#
# `view` alone does nothing: the caret is kept on screen by the clamp at the
# end of this very command, so a scrollbar that only stated a new top would
# snap straight back to wherever the caret was. The window therefore moves the
# caret FIRST and states the top second.
printf 'view 0 5\nview 20 5\nquit\n' | "$E" serve "$T/nav.txt" | gq '^S	top	0$'
check "view alone cannot scroll away from the caret" $?

printf 'view 0 5\nkeys 21G\nview 20 5\nquit\n' | "$E" serve "$T/nav.txt" | gq '^S	top	20$'
check "...but the caret first, then view, does — the scrollbar" $?

# The context menu is registers, and the + register is the desktop clipboard.
# Tested with a NAMED register: the clipboard one shells out to wl-copy, and a
# test suite has no business overwriting the clipboard of whoever runs it.
printf 'keys "ayy\nkeys 3G\nkeys "ap\nquit\n' | "$E" serve "$T/nav.txt" | gq '^S	lines	41$'
check "a register yank and put — what Copy and Paste send" $?

# ── the run driver itself ───────────────────────────────────────────────────

printf 'a\nb\n' | "$E" run -k 'dd' - | grep -qx 'b'
check "run reads standard input for -" $?

"$E" run --status -k 'jj' <(printf 'a\nb\nc\n') 2>/dev/null | gq '^line	3$'
check "--status reports the cursor line" $?

"$E" run -k '' -q "$T/f.txt" | gq .
[ $? -ne 0 ] && ok "-q prints nothing" || bad "-q prints nothing"

says "$E" run -k '' | gq 'needs a file'
check "run with no file says so" $?

says "$E" run --frobnicate x | gq "unknown option"
check "run refuses an unknown option" $?

# -c and -k compose in the order given.
[ "$("$E" run -c '%s/a/b/g' -k 'A!<Esc>' <(printf 'aa\n') 2>/dev/null)" = 'bb!' ]
check "-c and -k are applied in order" $?

# ── about ───────────────────────────────────────────────────────────────────

"$E" about | gq 'GPL-2.0-or-later'
check "about states the licence" $?

"$E" about | gq 'buymeacoffee'
check "about carries the donate link" $?

"$E" --rec about | gq '^field	value	detail$'
check "about --rec names its columns" $?

# ── awkward input ───────────────────────────────────────────────────────────

printf 'caf\xc3\xa9 x\n' > "$T/utf.txt"
[ "$("$E" run -k 'A!<Esc>' "$T/utf.txt")" = "$(printf 'caf\xc3\xa9 x!')" ]
check "a UTF-8 line survives an append" $?

# Backspacing over a multi-byte character removes the CHARACTER, not one of
# its bytes — which would leave the line invalid, draw as a replacement glyph,
# and take another press to clear.
printf 'caf\xc3\xa9\n' > "$T/utf2.txt"
[ "$("$E" run -k 'A<BS><Esc>' "$T/utf2.txt")" = "caf" ]
check "backspace removes a whole multi-byte character" $?

[ "$("$E" run -k 'A<BS><Esc>' "$T/utf2.txt" | wc -c)" = 4 ]
check "...and leaves no orphaned continuation byte" $?

printf '\xff\xfe bad utf8\n' > "$T/bad.txt"
"$E" run -k 'x' "$T/bad.txt" >/dev/null 2>&1
check "a file that is not valid UTF-8 still edits" $?

printf 'a\n' > "$T/one.txt"
"$E" run -k 'dddddd' "$T/one.txt" >/dev/null 2>&1
check "deleting past the end of the buffer is safe" $?

t "deleting every line leaves one empty line" 'dddd' 'a\nb\n' ''

# A very long line must not be quadratic or crash.
python3 -c "print('x' * 200000)" > "$T/long.txt" 2>/dev/null || \
	awk 'BEGIN{s="";for(i=0;i<200000;i++)s=s "x";print s}' > "$T/long.txt"
timeout 10 "$E" run -k '$x' "$T/long.txt" >/dev/null 2>&1
check "a 200k-character line is handled promptly" $?

# ── the window ──────────────────────────────────────────────────────────────
#
# Static checks on the QML. They are greps, and they earn their place because
# the failures they catch are all SILENT: a window that draws, accepts keys,
# and quietly does one thing less than it did yesterday.
QML="$(dirname "$0")/../data/syn-edit.qml"
if [ -f "$QML" ]; then
    # The window owns no text, and that is the whole architecture. A TextEdit
    # or TextInput here would be a second editor with a second undo stack.
    n=$(grep -cE '^\s*(TextEdit|TextArea|TextInput)\s*\{' "$QML" || true)
    [ "$n" = 0 ] && ok "the window still owns no text" \
                 || bad "$n text-editing item(s) in the renderer"

    grep -q 'function beginVisual' "$QML" \
        && ok "selecting goes through the engine's visual mode" \
        || bad "syn-edit.qml no longer starts visual mode to select"

    # The scrollbar's two-message order is the subtle half — see the serve
    # tests above. A `view` with no caret move before it silently does nothing.
    # ⚠ CHECKED BY ORDER, NOT BY SPELLING. This used to grep for the literal
    # `"G")` — the `NG` motion scrollToLine used to send — and so it failed the
    # moment that motion became the `goto` verb, which does the same job and
    # does not type itself into the buffer in insert mode. What is load-bearing
    # is that the caret moves BEFORE `view` is stated; how it moves is not.
    if body=$(sed -n '/function scrollToLine/,/^    }/p' "$QML") &&
       printf '%s' "$body" | grep -q 'gotoPos' &&
       [ "$(printf '%s' "$body" | grep -n 'gotoPos' | head -1 | cut -d: -f1)" \
         -lt "$(printf '%s' "$body" | grep -n '"view "' | head -1 | cut -d: -f1)" ]
    then
        ok "the scrollbar moves the caret before it states the top"
    else
        bad "the scrollbar no longer moves the caret first — view alone cannot scroll"
    fi

    grep -q 'id: ctxMenu' "$QML" \
        && ok "there is a context menu" \
        || bad "syn-edit.qml lost its context menu"

    # ⚠ CODE ONLY, NEVER COMMENTS. The prose in this file names the key
    # sequences it stopped sending — `"+y`, `"+p`, `<Esc>viw` — so a grep over
    # the whole file matches the explanation of a bug as readily as the bug,
    # and every check below would pass on a window that had lost the lot.
    #
    # ⚠ INTO A FILE, not down a pipe. `set -o pipefail` is on, and `grep -q`
    # closes the pipe the moment it matches — the sed feeding it takes a
    # SIGPIPE, and the pipeline's status is that, so every check written as
    # `strip | grep -q` FAILS EXACTLY WHEN IT MATCHES.
    sed -e 's,^[[:space:]]*//.*,,' -e 's,^[[:space:]]*[*][^/]*,,' "$QML" > "$T/qml_code"

    # ⛔ CUT, COPY AND PASTE ARE VERBS, NOT REGISTER KEYS. `"+y`, `"+d` and
    # `"+p` are normal-mode commands, so every one of them needed the window to
    # leave INSERT first — which is what disarmed Backspace and Ctrl+V for
    # every key pressed afterwards. And `"+p` is vim's PUT: after the caret,
    # a whole line when the register happens to be linewise, caret left ON the
    # last character rather than past it. None of that is what Paste means in
    # a window.
    n=$(grep -cE '"\\?"[+*]' "$T/qml_code" || true)
    [ "$n" = 0 ] && ok "the clipboard entries are protocol verbs, not register keys" \
                 || bad "$n clipboard register key sequence(s) back in the QML"

    for v in "gui copy" "gui cut" "gui paste" "gui insert" "gui visual" "gui delsel"; do
        grep -q "\"$v\"" "$T/qml_code" \
            && ok "the window sends \`$v\`" \
            || bad "the window no longer sends \`$v\` — that operation is gone"
    done

    # Copy means the DESKTOP clipboard, and the engine is where the + register
    # is reached. Checked in serve.c rather than the QML, because that is where
    # it moved to.
    grep -q "reg_set(e, '+'" "$(dirname "$0")/../src/serve.c" \
        && ok "Copy still yanks to the desktop clipboard register" \
        || bad "gui_copy no longer writes the + register"

    # ⛔ NO Ctrl-<letter> FUNNEL. keyName() used to turn every Ctrl+letter into
    # the engine's `<C-x>` notation, which is why Ctrl+V had never pasted:
    # `<C-v>` is a BLOCK SELECTION. In insert mode it is worse — a control
    # BYTE, written into the document.
    grep -q 'String.fromCharCode(97' "$T/qml_code" \
        && bad "the Ctrl+letter funnel is back — Ctrl+V is a block selection again" \
        || ok "no Ctrl+letter reaches the engine's key table"

    # ⛔ AND NO WINDOW SHORTCUT MAY BE SENT AS ONE EITHER. Removing the funnel
    # is not enough on its own — a `sendKeys("<C-v>")` written by hand is the
    # same block selection by another route. The one control key this window
    # still sends is redo, which is not a shortcut: it IS the engine's redo,
    # run from a window that has no other name for it.
    n=$(grep -o '<C-[a-z]>' "$T/qml_code" | sort -u | tr '\n' ' ')
    [ "$n" = "<C-r> " ] \
        && ok "the only vim control key the window still sends is redo" \
        || bad "vim control keys back in the QML: $n"

    # The shortcuts every other program on the desktop has. A missing one here
    # is a key that silently does nothing.
    for pair in C:guiCopy X:guiCut V:guiPaste A:selectAll Z:undo Y:redo; do
        grep -q "Qt.Key_${pair%%:*}.*${pair##*:}" "$T/qml_code" \
            && ok "Ctrl+${pair%%:*} is wired to ${pair##*:}()" \
            || bad "Ctrl+${pair%%:*} does nothing — ${pair##*:}() is not reached"
    done

    # ⛔ AND NOTHING MAY BE WRITTEN BEFORE THE ENGINE'S FIRST FRAME. A Process
    # write made before the process spawns is dropped in silence, and the
    # window makes several `view` calls that early — so `sent` sat five ahead
    # of `acked` for the life of the window and the check above never trusted a
    # frame again. Held in an outbox until the first frame proves the pipe is
    # live. This one is invisible from outside: the window works, and one guard
    # inside it has quietly stopped guarding.
    sed -n '/function send(s)/,/^    }/p' "$T/qml_code" > "$T/sendfn"
    grep -q 'root.engineUp' "$T/sendfn" && grep -q 'outbox' "$T/sendfn" \
        && ok "the window holds its first writes until the engine can take them" \
        || bad "send() writes before the engine is up — those writes are dropped"

    # ── the layout ──────────────────────────────────────────────────────────
    #
    # ⛔ THE SIDEBAR IS FULL HEIGHT AND EVERYTHING ELSE IS A COLUMN BESIDE IT.
    # It used to be a strip wedged between a toolbar that did not act on it and
    # a status bar about a different pane. Anchoring is the whole of that
    # layout, and a wrong anchor does not error — it draws a window that is
    # merely arranged badly, which no test notices and nobody reports.
    for pane in toolbar tabstrip statusbar; do
        sed -n "/id: $pane\$/,/^            [A-Za-z]/p" "$T/qml_code" > "$T/pane"
        grep -q 'left: sidebar.right' "$T/pane" \
            && ok "the $pane starts where the sidebar ends" \
            || bad "the $pane spans the sidebar again — the list is back in a band"
    done
    sed -n '/id: sidebar$/,/color: Qt.rgba/p' "$T/qml_code" > "$T/side"
    grep -q 'top: parent.top; bottom: parent.bottom; left: parent.left' "$T/side" \
        && ok "the sidebar runs the full height of the window" \
        || bad "the sidebar no longer runs top to bottom"

    # A ListView, not a Column: MAXBUF is 64 and a Column of 64 rows in a 300px
    # panel runs off the bottom with no way to reach the rest.
    grep -q 'id: sideList' "$T/qml_code" && grep -q 'ListView' "$T/qml_code" \
        && ok "the document list scrolls" \
        || bad "the document list is not a view — a long one cannot be reached"

    # ⛔ AND CLOSING THE LAST DOCUMENT IS GUARDED IN THE FUNCTION, not only by
    # hiding the button. `:bd` on a single buffer sets quit (pinned above), so
    # the difference between the two is a stray click and a lost session.
    sed -n '/function doClose(/,/^    }/p' "$T/qml_code" > "$T/doclose"
    grep -q 'root.bufs.length <= 1' "$T/doclose" \
        && ok "closing the last document is refused in the code, not just hidden" \
        || bad "doClose no longer guards the last document — an ✕ closes the window"

    # ⚠ AND THE TAB STRIP HAS A SWITCH. It says the same thing the sidebar
    # says, and which of the two anybody wants is taste — but its only control
    # was `:set tabbar!`, typed, in a window that exists so nobody has to.
    grep -q 'set tabbar!' "$T/qml_code" \
        && ok "the tab strip can be turned off from the toolbar" \
        || bad "the tab strip has no control but a typed :set again"

    # The question, and both ways out of it.
    for f in closeBuffer discardAndClose saveAndClose; do
        grep -q "function $f" "$T/qml_code" \
            && ok "there is a $f()" \
            || bad "syn-edit.qml lost $f() — a modified document cannot be closed"
    done

    # ⚠ THE SAVE'S OWN FRAME IS THE VERDICT. Sending the close straight after
    # the save means a failed write is answered with bd's "unsaved changes",
    # which is the wrong message about the wrong problem.
    sed -n '/function saveAndClose/,/^    }/p' "$T/qml_code" > "$T/sac"
    grep -q 'closeSerial' "$T/sac" \
        && ok "save-and-close waits for the write to be answered" \
        || bad "save-and-close fires the close blind — a failed write reports the wrong error"

    # ⛔ A POSITIONER REFUSES TO LAY OUT AN ANCHORED CHILD, and the child then
    # adds NOTHING to the positioner's height. The unsaved-changes question
    # rendered its sentence and no buttons in a bar sized to fit them — not a
    # clipped button, an invisible one.
    grep -q 'property bool centered' "$T/qml_code" \
        && ok "ToolButton can drop its anchor for a Flow or a Column" \
        || bad "ToolButton always anchors — inside a positioner it vanishes"
    sed -n '/id: askCol/,/^                }/p' "$T/qml_code" > "$T/ask"
    n=$(grep -c 'centered: false' "$T/ask" || true)
    [ "$n" = 3 ] && ok "…and all three answers to the question do" \
                 || bad "$n of 3 buttons in the close question set centered: false"

    # ⛔ THE RULE THAT MAKES THE WINDOW MODELESS. Every path that ends in
    # NORMAL — an undo, a search finishing, a file opening, the engine's own
    # first frame — is answered by one `gui insert` in the frame handler. Take
    # it out and the window is modal again, silently, one gesture at a time.
    sed -n '/tag === "E"/,/^        }/p' "$T/qml_code" > "$T/eframe"
    grep -q 'guiInsert()' "$T/eframe" \
        && ok "a frame reporting NORMAL puts the window back into INSERT" \
        || bad "NORMAL is a resting state again — Backspace and Ctrl+V will stop working"

    # The Open dialogue lists with synfiles rather than reading a directory
    # itself, and falls back to :e when synfiles is not installed.
    grep -q '"synfiles", "--rec", "list"' "$QML" && grep -q 'haveFiles' "$QML" \
        && ok "Open browses through synfiles, and is probed for" \
        || bad "the Open dialogue no longer uses synfiles --rec list"

    # ── Save must be able to ask for a name ─────────────────────────────────
    #
    # The regression: a [No Name] buffer cannot be written, the engine says
    # "no file name", and the window had nothing that could clear it. New →
    # type → Save was unsaveable through the GUI, and the only way out was
    # knowing to type `:w name` at the command line the toolbar exists to
    # avoid. Every one of these is a SILENT loss if it comes back — a Save
    # button that reports a refusal and offers nothing.
    grep -q 'function saveNow' "$QML" && grep -q 'function saveAs' "$QML" \
        && ok "Save knows the difference between writing and naming" \
        || bad "syn-edit.qml lost saveNow/saveAs — Save cannot name a file"

    # Nothing may send a bare `save` any more: on an unnamed buffer that is
    # the dead end. Every route goes through saveNow(), which asks first.
    n=$(grep -cE 'send\("save"\)' "$QML" || true)
    [ "$n" = 1 ] && ok "only saveNow() sends a bare save" \
                 || bad "$n direct send(\"save\") call(s) — one of them cannot name a file"

    # The name is typed into the ENGINE's command line, like Find and Replace.
    # A TextInput here would be the second editor the whole architecture exists
    # to avoid — and the check above for that is still in force.
    grep -q 'function promptWrite' "$QML" && grep -q '":w "' "$QML" \
        && ok "naming goes through the engine's :w command line" \
        || bad "syn-edit.qml no longer opens :w to name a file"

    # ⚠ <Esc> before the colon. A toolbar button can be clicked in INSERT
    # mode, where `:` is a character — without this, Save types ":w /home/…"
    # INTO the document it was asked to save.
    grep -A3 'function promptWrite' "$QML" | grep -q 'sendKeys("<Esc>")' \
        && ok "naming leaves INSERT mode before typing the colon" \
        || bad "promptWrite no longer escapes insert mode — Save would type into the buffer"

    grep -q 'function showSave' "$QML" \
        && ok "the browser doubles as the Save As folder chooser" \
        || bad "syn-edit.qml lost the Save As browser mode"

    # Every button and menu entry routes its keys through actKeys, which leaves
    # INSERT mode first. A raw sendKeys on a button is the bug coming back.
    grep -q 'function actKeys' "$QML" \
        && ok "buttons send their keys through actKeys" \
        || bad "syn-edit.qml lost actKeys — a button in insert mode types into the file"

    grep -A2 'function actKeys' "$QML" | grep -q 'if (root.inserting)' \
        && ok "...which escapes INSERT mode and only insert mode" \
        || bad "actKeys no longer guards on insert mode — a blanket Esc breaks visual Copy"

    grep -q 'actKeys("/")' "$QML" && grep -q 'actKeys(":%s/")' "$QML" \
        && ok "Find and Replace go through it" \
        || bad "Find/Replace send raw keys again — they type into the buffer in insert mode"

    grep -q 'actKeys(m.keys)' "$QML" \
        && ok "...and so does every context-menu entry" \
        || bad "the context menu sends raw keys again"

    # ⛔ NO MOUSE GESTURE MAY ESCAPE TO NORMAL. This is the bug itself: a
    # double click sent `<Esc>viw` and a right click sent `<Esc>`, and nothing
    # ever sent the window back — so after one of either, Backspace was `h` and
    # would not join a line, and Ctrl+V was a block selection.
    #
    # actKeys() and promptWrite() are the two that legitimately escape, and
    # both are checked above; the assertion here is that the POINTER paths do
    # not. Counted rather than named, so a new one has to be looked at.
    n=$(sed -n '/id: textMa/,/^            }/p' "$T/qml_code" | grep -c '<Esc>' || true)
    [ "$n" = 0 ] && ok "no mouse gesture drops the window out of INSERT" \
                 || bad "$n mouse path(s) escape to NORMAL — Backspace stops joining lines"

    sed -n '/onDoubleClicked/,/^                }/p' "$T/qml_code" > "$T/dclick"
    grep -q 'beginVisual' "$T/dclick" \
        && ok "a double click selects through the engine's visual mode" \
        || bad "the double click no longer starts a selection the mode-safe way"

    # The POINTER paths are deliberately NOT converted: a click, a drag, the
    # wheel and the scrollbar send motions, and making those leave insert mode
    # is a change to what the editor DOES rather than a bug fix. They send raw
    # keys still — and they DO type into the buffer in insert mode, which is
    # recorded here as a known gap rather than pinned as correct.
    #
    # So the assertion is exact: the one remaining raw-key trigger is the
    # edge-scroll timer. Any OTHER one is a button that lost its guard.
    n=$(grep -cE 'onTriggered: root\.sendKeys\(' "$QML" || true)
    if [ "$n" = 1 ] && grep -qE 'onTriggered: root\.sendKeys\(edgeScroll' "$QML"; then
        ok "the only raw-key trigger left is the edge-scroll timer"
    else
        bad "$n raw-key trigger(s) — a button has lost its insert-mode guard"
    fi

    # See the note in synfiles' suite: this qmllint reports a parse failure
    # through its EXIT STATUS and prints nothing at all, so the status is the
    # verdict. syn-edit.qml carries no `pragma ComponentBehavior`, which is the
    # other thing that makes it exit non-zero in silence — if one is ever added
    # here, strip it from a copy the way synfiles' suite does.
    if command -v qmllint >/dev/null 2>&1; then
        if grep -q '^pragma ComponentBehavior' "$QML"; then
            sed '/^pragma ComponentBehavior/d' "$QML" > "$T/lint.qml"
        else
            cp "$QML" "$T/lint.qml"
        fi
        if qmllint "$T/lint.qml" > "$T/lint.txt" 2>&1; then
            ok "the QML parses cleanly"
        else
            bad "qmllint rejects syn-edit.qml — the window will not open"
            [ -s "$T/lint.txt" ] && sed 's/^/        /' "$T/lint.txt" >&2
        fi
    else
        echo "  skip  qmllint not installed"
    fi
else
    bad "syn-edit.qml not found beside the tests: $QML"
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

# ── task lists ──────────────────────────────────────────────────────────────
#
# The GUI's task panel reads the whole buffer and ticks a box by replacing ONE
# character. Both halves rest on engine behaviour that is easy to break without
# noticing, so both are pinned here rather than in the window — where they would
# need a compositor to test at all.

printf 'notes\n- [ ] first\nprose\n- [x] done\n  * [ ] indented\n' > "$T/tasks.md"

# 1. `view <top> 0` is the request for EVERY line: serve.c falls back to the
#    buffer length when view_rows is zero. The panel has no other way to see
#    past the window, and a scan that saw only the visible lines would quietly
#    list a fraction of the file.
[ "$(printf 'view 0 0\nquit\n' | "$E" serve "$T/tasks.md" | grep -c '^L	')" -ge 5 ]
check "view with zero rows emits the whole buffer" $?

# 2. ...and it must not MOVE anything. The caret re-clamp in serve.c is guarded
#    on view_rows, so a zero-row view is also the only one that cannot scroll
#    the window — which is what makes it safe for a panel that only reads.
printf 'keys 5G\nview 0 0\nquit\n' | "$E" serve "$T/tasks.md" \
    | awk -F'\t' '$1=="S" && $2=="line"' | tail -1 | gq 'line	5'
check "a whole-buffer scan leaves the caret where it was" $?

# 3. Ticking is an ex substitution on ONE line. No column arithmetic, which is
#    what keeps it right when the indent is a tab (see 6).
printf 'ex 2s%%2F%%5C%%5B%%20%%5C%%5D%%2F%%5Bx%%5D%%2F\nex w %s\nquit\n' "$T/t1.md" \
    | "$E" serve "$T/tasks.md" >/dev/null 2>&1
gq '^- \[x\] first$' < "$T/t1.md"
check "a task is ticked by a substitution on its own line" $?

# 4. The same in reverse. [xX] because a list edited elsewhere has both.
printf 'ex 4s%%2F%%5C%%5B%%5BxX%%5D%%5C%%5D%%2F%%5B%%20%%5D%%2F\nex w %s\nquit\n' "$T/t2.md" \
    | "$E" serve "$T/tasks.md" >/dev/null 2>&1
gq '^- \[ \] done$' < "$T/t2.md"
check "a finished task is unticked, in either letter case" $?

# 5. An indented task, where a column would no longer be a constant.
printf 'ex 5s%%2F%%5C%%5B%%20%%5C%%5D%%2F%%5Bx%%5D%%2F\nex w %s\nquit\n' "$T/t3.md" \
    | "$E" serve "$T/tasks.md" >/dev/null 2>&1
gq '^  \* \[x\] indented$' < "$T/t3.md"
check "an indented task ticks without knowing its column" $?

# 6. ⚠ THE ONE THAT BIT, AND THE REASON THERE IS NO COLUMN ABOVE.
#
#    serve.c sends lines with tabs EXPANDED (expand_line), so counting
#    characters in a record gives a DISPLAY column — while the engine's `|`
#    motion counts the RAW line. On "\t- [ ] tabbed" the box is display column 8
#    and raw column 5. The first version of the task panel computed the display
#    column and sent `2G8|rx`, which replaces the "t" of "tabbed": the box is
#    untouched, the task text is silently corrupted, and nothing reports
#    anything. Both halves are pinned here so neither number can drift.
printf 'x\n\t- [ ] tabbed\n' > "$T/tabtask.md"
printf 'view 0 0\nquit\n' | "$E" serve "$T/tabtask.md" \
    | gq '^L	2	\(%20\)\{4\}-%20%5B%20%5D'
check "a tab-indented task arrives with its tab expanded" $?

printf 'keys 2G8%%7Crx\nex w %s\nquit\n' "$T/tcol.md" \
    | "$E" serve "$T/tabtask.md" >/dev/null 2>&1
gq '^	- \[ \] xabbed$' < "$T/tcol.md"
check "a DISPLAY column aimed at the engine hits the wrong character" $?

printf 'ex 2s%%2F%%5C%%5B%%20%%5C%%5D%%2F%%5Bx%%5D%%2F\nex w %s\nquit\n' "$T/t4.md" \
    | "$E" serve "$T/tabtask.md" >/dev/null 2>&1
gq '^	- \[x\] tabbed$' < "$T/t4.md"
check "the substitution ticks a tab-indented task and keeps the tab" $?

# ── a click does not type its own coordinates ───────────────────────────────
#
# ⛔ THE WINDOW USED TO PLACE THE CARET BY SENDING KEYS. `12G34|` is a motion in
# normal mode and thirteen literal characters in insert mode, so clicking while
# typing filled the document with the mouse's own coordinates:
#
#     1G28|1G27|1G27|1G27|viw1G27|1G27|1G27|viw[ ] 1G6|1G11|1G11|1G11|viw
#
# — which is unexplainable from the outside, because none of those characters
# was pressed by anybody. Reported 2026-08-26 with a screenshot of exactly that.
#
# ⚠ ESCAPING FIRST IS NOT THE FIX. No editor throws you out of insert mode for
# clicking somewhere, and none puts you into it: the mode has to come out
# exactly as it went in. So `goto` moves the caret and says nothing about the
# mode, and these checks are as much about the MODE being untouched as about
# the buffer being unchanged.
printf 'abcdefghij\n' > "$T/goto.txt"

printf 'goto 1 5\nquit\n' | "$E" serve "$T/goto.txt" | gq '^S	col	5$'
check "goto moves the caret" $?

printf 'goto 1 5\nquit\n' | "$E" serve "$T/goto.txt" | gq '^S	mode	NORMAL$'
check "goto leaves NORMAL alone" $?

# THE REGRESSION. Insert mode, then a click: the caret moves, the mode is still
# INSERT, and — the whole point — the line is untouched.
printf 'keys i\ngoto 1 5\nquit\n' | "$E" serve "$T/goto.txt" | gq '^S	mode	INSERT$'
check "a click during INSERT stays in INSERT" $?

printf 'keys i\ngoto 1 5\nquit\n' | "$E" serve "$T/goto.txt" | gq '^S	col	5$'
check "…and still moves the caret" $?

printf 'keys i\ngoto 1 5\nquit\n' | "$E" serve "$T/goto.txt" | gq '^L	1	abcdefghij$'
check "…and types NOTHING into the line" $?

# The old spelling, to show what it did — `12G34|` as keys, in insert mode, IS
# the bug. Pinned so nobody reintroduces it thinking keys are equivalent.
printf 'keys i\nkeys 1G5%%7C\nquit\n' | "$E" serve "$T/goto.txt" | gqv '^L	1	abcdefghij$'
check "the old key spelling really did type into the buffer" $?

# A drag: visual mode, then goto EXTENDS rather than replacing the selection.
# That is why this sets the caret instead of calling a motion — the engine
# reads a selection as anchor→caret, so moving the caret is extending it.
# ⚠ sel_x0/sel_x1 are 0-BASED while `col` and `dcol` are 1-based — serve.c adds
# the +1 for the caret and not for the selection span. Column 7 is therefore
# sel_x1 6, and asserting 7 here fails against code that is working.
printf 'keys v\ngoto 1 7\nquit\n' | "$E" serve "$T/goto.txt" | gq '^S	sel_x1	6$'
check "goto extends a visual selection (a drag)" $?

printf 'keys v\ngoto 1 7\nquit\n' | "$E" serve "$T/goto.txt" | gq '^S	mode	VISUAL$'
check "…without leaving VISUAL" $?

# Out of range is clamped, not crashed: a click below the last line is an
# ordinary thing to do with a short file in a tall window.
printf 'goto 999 999\nquit\n' | "$E" serve "$T/goto.txt" | gq '^S	line	1$'
check "a click past the end of the file clamps" $?

# ── the window has no modes ─────────────────────────────────────────────────
#
# ⛔ TWO SYMPTOMS, ONE BUG, AND THE ENGINE WAS RIGHT ABOUT BOTH OF THEM.
# Reported 2026-08-27: Ctrl+Shift+V would not paste, and Backspace at the start
# of line 2 would not join it onto line 1 — the arrow keys had to be used to
# get there instead.
#
# Neither is a defect in vim.c. In NORMAL mode Backspace IS `h`, a motion that
# stops dead at column 0 and has never joined a line, and Ctrl+V IS `<C-v>`, a
# block selection. Both are the correct answers to a window that had silently
# left INSERT — which every mouse gesture did: a double click sent `<Esc>viw`,
# a right click sent `<Esc>`, and nothing ever sent the window back.
#
# The window is modeless now. These pin the verbs that make it so; the checks
# on the QML further up pin that the window actually uses them.
printf 'line one\nline two\nline three\n' > "$T/mless.txt"

# The resting state. `gui insert` is what the window sends whenever a frame
# reports NORMAL, and it must reach INSERT from every mode there is.
printf 'gui insert\nquit\n' | "$E" serve "$T/mless.txt" | gq '^S	mode	INSERT$'
check "gui insert reaches INSERT from NORMAL" $?

printf 'keys v\ngui insert\nquit\n' | "$E" serve "$T/mless.txt" | gq '^S	mode	INSERT$'
check "…and from VISUAL" $?

# ⚠ AND IT DROPS THE SELECTION WITHOUT DELETING IT. This is Escape, and a
# window whose Escape ate the selected text would be a window nobody could use.
printf 'keys vll\ngui insert\nquit\n' | "$E" serve "$T/mless.txt" |
    gq '^L	1	line%20one$'
check "…leaving the selected text alone — Escape is not a delete" $?

# THE REPORTED BUG, end to end: double-click a word on line 2, then Backspace
# until the lines join. The double click is `gui visual` + `iw`, and the
# Backspace that follows must be an INSERT backspace.
printf 'gui insert\ngoto 2 1\ngui visual\nkeys iw\ngui delsel\nkeys <BS>\nquit\n' |
    "$E" serve "$T/mless.txt" | gq '^L	1	line%20one%20two$'
check "Backspace joins line 2 onto line 1 after a double click" $?

# The same thing the old way, to show what was actually happening. `<Esc>viw`
# then Backspace is `h`: it moves the caret and changes nothing.
printf 'keys i\ngoto 2 1\nkeys <Esc>\nkeys viw\nkeys <BS>\nquit\n' |
    "$E" serve "$T/mless.txt" | gqv '^L	1	line%20one%20two$'
check "…and the old <Esc>viw spelling really did leave Backspace as a motion" $?

# `gui visual` anchors AT the caret. `<Esc>v` did not: leaving insert moves the
# caret one column left, so a selection begun while typing was off by one every
# time — and only at the moment a drag started, which is why nobody caught it.
printf 'gui insert\ngoto 1 5\ngui visual\nquit\n' | "$E" serve "$T/mless.txt" |
    gq '^S	sel_x0	4$'
check "gui visual anchors at the caret, not one column left of it" $?

printf 'keys i\ngoto 1 5\nkeys <Esc>\nkeys v\nquit\n' | "$E" serve "$T/mless.txt" |
    gqv '^S	sel_x0	4$'
check "…which the old <Esc>v spelling got wrong" $?

# Typing over a selection replaces it. The window sends `gui delsel` and then
# the key, so the engine decides what Backspace means rather than the QML.
printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui delsel\nkeys X\nquit\n' |
    "$E" serve "$T/mless.txt" | gq '^L	1	X%20one$'
check "typing over a selection replaces it" $?

printf 'gui insert\ngui delsel\nquit\n' | "$E" serve "$T/mless.txt" |
    gq '^L	1	line%20one$'
check "…and gui delsel with nothing selected changes nothing" $?

# ── cut, copy and paste ─────────────────────────────────────────────────────
#
# ⚠ RUN WITH AN EMPTY PATH, ON PURPOSE. The + register IS the desktop
# clipboard: vim.c shells out to wl-copy and wl-paste to reach it, and have_cmd
# needs PATH to find them. With no PATH neither is found, the documented
# fallback makes + an ordinary private register, and the verbs are exercised
# without a test suite overwriting the clipboard of whoever ran it.
CLIP="env PATH= $E"

printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui copy\nquit\n' |
    $CLIP serve "$T/mless.txt" | gq '^S	mode	VISUAL$'
check "Copy leaves the selection selected, the way every other program does" $?

printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui cut\nquit\n' |
    $CLIP serve "$T/mless.txt" | gq '^L	1	%20one$'
check "Cut takes the selection out" $?

printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui cut\nquit\n' |
    $CLIP serve "$T/mless.txt" | gq '^S	mode	INSERT$'
check "…and leaves the window able to type again" $?

# ⚠ AT THE END OF THE LINE, which is the case a mode change silently gets
# wrong: NORMAL clamps the caret to the last CHARACTER, so a paste that entered
# insert mode after placing the caret would land one column short. `line two`
# is eight characters, so column 9 is the end of it.
printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui cut\ngoto 2 9\ngui paste\nquit\n' |
    $CLIP serve "$T/mless.txt" | gq '^L	2	line%20twoline$'
check "Paste puts it back where the caret is" $?

# ⚠ WHERE THE CARET LANDS IS THE HALF THAT `"+p` GOT WRONG. Put is vim's: it
# goes AFTER the caret and leaves it on the last character pasted. A window's
# paste leaves the caret past the end of what it just inserted, or typing on
# does not continue from where you are looking.
printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui cut\ngoto 2 9\ngui paste\nkeys Z\nquit\n' |
    $CLIP serve "$T/mless.txt" | gq '^L	2	line%20twolineZ$'
check "…with the caret after it, so typing carries straight on" $?

# Pasting over a selection replaces it, and is ONE undo step rather than a
# delete the user has to undo twice.
printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui copy\ngoto 2 1\ngui visual\nkeys iw\ngui paste\nquit\n' |
    $CLIP serve "$T/mless.txt" | gq '^L	2	line%20two$'
check "Paste over a selection replaces it" $?

printf 'gui insert\ngoto 1 1\ngui visual\nkeys iw\ngui copy\ngoto 2 1\ngui visual\nkeys iw\ngui paste\nkeys <Esc>\nkeys u\nquit\n' |
    $CLIP serve "$T/mless.txt" | gq '^L	2	line%20two$'
check "…and undoes in one step" $?

# With nothing selected the line is the unit, which is what a menu entry with
# nothing selected has to pick anyway — better than a Copy that does nothing.
printf 'gui insert\ngoto 2 1\ngui cut\nquit\n' | $CLIP serve "$T/mless.txt" |
    gq '^S	lines	2$'
check "Cut with no selection takes the line" $?

# An empty clipboard must not leave the window somewhere it cannot type.
printf 'gui insert\ngui paste\nquit\n' | $CLIP serve "$T/mless.txt" |
    gq '^S	mode	INSERT$'
check "an empty clipboard still leaves the window in INSERT" $?

# Find and Replace put the caret on the ENGINE's command line, and that is
# where a paste has to land — pasting a search term into the document behind an
# open prompt is the same mistake as the mouse coordinates that used to be
# typed into it.
printf 'keys "+yy\nkeys <Esc>\nkeys /\ngui paste\nquit\n' | $CLIP serve "$T/mless.txt" |
    gq '^S	cmdline	/line%20one$'
check "Paste with a prompt open goes into the prompt" $?

printf 'keys "+yy\nkeys <Esc>\nkeys /\ngui paste\nquit\n' | $CLIP serve "$T/mless.txt" |
    gq '^S	mode	COMMAND$'
check "…and does not drag the window out of it" $?

# ⚠ AND THE NEWLINE A LINEWISE YANK CARRIES IS DROPPED, not typed. A newline on
# the command line SUBMITS it — a pasted pattern would run a substitution
# nobody had finished writing.
printf 'keys "+yy\nkeys <Esc>\nkeys /\ngui paste\nquit\n' | $CLIP serve "$T/mless.txt" |
    gq '^S	lines	3$'
check "…and a pasted newline does not run the command" $?

# An unknown gui op is reported rather than silently ignored — a verb added to
# the QML and not to serve.c would otherwise be a button that does nothing.
printf 'gui nonesuch\nquit\n' | "$E" serve "$T/mless.txt" | gq '^S	msgerr	1$'
check "an unknown gui verb is an error, not a silent no-op" $?

# ── the document sidebar ────────────────────────────────────────────────────
#
# The window grew a document list down the left with a header of its own, rows
# that can be closed, and a width you drag. Three of those need the engine to
# say something it did not say before, and all three are silent if they stop.

# ⚠ THE SIXTH FIELD OF A B RECORD IS "HAS A PATH". The sidebar needs it to know
# whether Close can offer to save, and it must NOT be answered by matching the
# "[No Name]" label — that is a message, it can be reworded, and a real file
# could be called that. Same reason the `named` state row exists.
printf 'x\n' > "$T/side.txt"
printf 'quit\n' | "$E" serve "$T/side.txt" | gq '^B	1	.*	0	1	1$'
check "a B record says the buffer has a path" $?

printf 'new\nquit\n' | "$E" serve "$T/side.txt" | gq '^B	2	%5BNo%20Name%5D	0	1	0$'
check "…and says when it does not" $?

# Closing. `:bd` is what the ✕ on a row runs, and its refusal on a modified
# buffer is what the window has to ask about rather than pass on.
printf 'b\n' > "$T/side2.txt"
printf 'ex bd\nquit\n' | "$E" serve "$T/side.txt" "$T/side2.txt" | gq '^S	lines	1$'
check "bd closes a document" $?

printf 'keys iz\nex bd\nquit\n' | "$E" serve "$T/side.txt" "$T/side2.txt" |
    gq '^S	msgerr	1$'
check "…and refuses one with unsaved changes" $?

printf 'keys iz\nex bd!\nquit\n' | "$E" serve "$T/side.txt" "$T/side2.txt" |
    gq '^S	file	.*side2.txt$'
check "…which bd! overrides — what Discard sends" $?

# ⛔ THE LAST DOCUMENT IS NOT CLOSEABLE, and the engine is the reason: bd on a
# single buffer sets quit. The window guards it in doClose() as well as by
# hiding the button, and this is what it is guarding against.
printf 'ex bd\nquit\n' | "$E" serve "$T/side.txt" | gq '^S	quit	1$'
check "bd on the only document would close the whole editor" $?

# The width the splitter drags.
"$E" config set treewidth 300 >/dev/null
[ "$("$E" config get treewidth)" = 300 ]
check "treewidth round-trips" $?

"$E" config set treewidth 9 >/dev/null
[ "$("$E" config get treewidth)" = 150 ]
check "…and is clamped to a width the rows still fit in" $?
"$E" config reset >/dev/null

printf 'quit\n' | "$E" serve "$T/side.txt" | gq '^S	treewidth	230$'
check "the frame carries it, so the window does not guess" $?

# ⛔ THE WINDOW'S `set` IS WRITTEN DOWN AND A TYPED `:set` IS NOT. A panel
# closed with a button and a sidebar dragged to a width are PREFERENCES; a
# window that forgets them on every launch has to be rearranged every morning.
# A typed :set is vim's, and vim's answer is "this session".
rm -f "$SYN_EDIT_CONFIG"
printf 'set treewidth=310\nquit\n' | "$E" serve "$T/side.txt" >/dev/null
grep -q '^treewidth = 310$' "$SYN_EDIT_CONFIG" 2>/dev/null
check "the window's set writes the setting down" $?

rm -f "$SYN_EDIT_CONFIG"
printf 'ex set treewidth=310\nquit\n' | "$E" serve "$T/side.txt" >/dev/null
[ ! -f "$SYN_EDIT_CONFIG" ]
check "…and a typed :set does not" $?
rm -f "$SYN_EDIT_CONFIG"

# ── report ──────────────────────────────────────────────────────────────────

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
