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

# ── report ──────────────────────────────────────────────────────────────────

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
