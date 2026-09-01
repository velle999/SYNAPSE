#!/usr/bin/env bash
# shell_test.sh — synsh as a SHELL: the things every shell has to get right.
#
# Every case here was a live bug before the expansion/pipeline work, and every
# one of them was silent in a different way, which is why they lasted:
#
#   `seq 1 200000 | wc -l`   hung forever — stages ran one at a time, so the
#                            first filled the 64 KiB pipe and blocked writing to
#                            a reader that had not been forked yet
#   `yes | head -1`          same deadlock, from the other end
#   `echo $HOME`             printed the five characters "$HOME"
#   `ls *.c`                 asked for a file literally named "*.c"
#   `echo hi >f`             printed "hi >f" — redirection needed a space
#   `ls x >o 2>&1`           cut at the '&' into two commands, the second of
#                            them named "1"
#   `sleep 5 & echo done`    ran `sleep 5 echo done`
#   `false; echo $?`         printed the PREVIOUS line's status
#   `$(cmd)`                 was passed through as literal text — which is what
#                            made synsh's own remove-orphans intent answer "no
#                            orphaned packages" on every machine, always
#
# Run: tests/shell_test.sh [path-to-synsh]
# Default binary is build/synsh, so `meson compile -C build && tests/shell_test.sh`
# is the loop.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
SYNSH=${1:-$here/../build/synsh}
[ -x "$SYNSH" ] || { echo "no synsh at $SYNSH — build it first"; exit 2; }

TMP=$(mktemp -d /tmp/synsh-shell.XXXXXX)
trap 'rm -rf "$TMP"' INT TERM EXIT
cd "$TMP" || exit 2

fails=0
run() { timeout 10 "$SYNSH" --no-ai -c "$1" 2>&1; }

check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

status() {  # status <description> <expected-code> <command>
    timeout 10 "$SYNSH" --no-ai -c "$3" >/dev/null 2>&1
    check "$1" "$2" "$?"
}

echo "synsh shell behaviour — $SYNSH"

# ── Pipelines run concurrently ────────────────────────────
# 124 is timeout(1)'s "I killed it", i.e. the deadlock is back.
check "pipeline: large output does not deadlock" "200000" "$(run 'seq 1 200000 | wc -l')"
check "pipeline: reader exiting early does not deadlock" "y" "$(run 'yes | head -1')"
check "pipeline: three stages"       "HELLO"  "$(run 'echo hello | tr a-z A-Z | cat')"
status "pipeline: status is the LAST stage (false|true)" 0 'false | true'
status "pipeline: status is the LAST stage (true|false)" 1 'true | false'

# ── Expansion ─────────────────────────────────────────────
check "parameter: \$HOME"            "$HOME"      "$(run 'echo $HOME')"
check "parameter: \${HOME}/x"        "$HOME/x"    "$(run 'echo ${HOME}/x')"
check "parameter: unset is empty"    ""           "$(run 'echo $SYNSH_DEFINITELY_UNSET_XYZ')"
check "parameter: \$? follows the previous command" "1" "$(run 'false; echo $?')"
check "parameter: not expanded in single quotes" '$HOME' "$(run "echo '\$HOME'")"
check "parameter: expanded in double quotes" "$HOME" "$(run 'echo "$HOME"')"
check "substitution: \$( )"          "inner"      "$(run 'echo $(echo inner)')"
check "substitution: backticks"      "tick"       "$(run 'echo `echo tick`')"
check "substitution: nested pipeline inside \$( )" "2" "$(run 'echo $(printf "a\nb\n" | wc -l)')"
# One argument per field, or one argument holding the lot: the difference
# between `pacman -Rns $(pacman -Qtdq)` removing each orphan and asking for a
# single package whose name is every orphan joined by a newline.
check "substitution: field-split into separate arguments" "2" \
      "$(run 'printf "%s\n" $(printf "a b") | wc -l')"
check "substitution: NOT split inside double quotes" "1" \
      "$(run 'printf "%s\n" "$(printf "a b")" | wc -l')"

# ── Globbing ──────────────────────────────────────────────
: > g1.x; : > g2.x
check "glob: expands"                "g1.x g2.x"  "$(run 'echo *.x')"
check "glob: no match stays literal" "none-*.zzz" "$(run 'echo none-*.zzz')"
check "glob: quoted is literal"      "*.x"        "$(run 'echo "*.x"')"
check "glob: escaped is literal"     "*.x"        "$(run 'echo \*.x')"

# ── Tilde ─────────────────────────────────────────────────
check "tilde: bare"        "$HOME"      "$(run 'echo ~')"
check "tilde: with a path" "$HOME/x"    "$(run 'echo ~/x')"
check "tilde: mid-word is a filename, not a home" "a~b" "$(run 'echo a~b')"

# ── Redirection ───────────────────────────────────────────
run 'echo hi >out1' >/dev/null
check "redirect: > with no space" "hi" "$(cat out1 2>&1)"
run 'echo a >out2; echo b >>out2' >/dev/null
check "redirect: >> appends" "a b" "$(tr '\n' ' ' < out2 | sed 's/ $//')"
run 'ls /nonexistent-xyz 2>err1' >/dev/null
check "redirect: 2> takes stderr only" "1" "$(test -s err1 && echo 1 || echo 0)"
run 'ls /nonexistent-xyz >both1 2>&1'
check "redirect: 2>&1 after > sends both to the file" "1" \
      "$(test -s both1 && echo 1 || echo 0)"
run 'ls /nonexistent-xyz &>both2'
check "redirect: &> sends both to the file" "1" \
      "$(test -s both2 && echo 1 || echo 0)"
check "redirect: '&' inside 2>&1 is not a background separator" "0" \
      "$(run 'ls /nonexistent-xyz >o3 2>&1'; test -e 1 && echo 1 || echo 0)"
check "redirect: < reads the file" "hi" "$(run 'cat <out1')"
check "redirect: target is expanded" "hi" "$(run 'echo hi >$PWD/out4'; cat out4)"
# ⚠ EXPANDED ONCE. A quoted target is already literal by the time the operator
# has been read; expanding it a second time on the way out turned a file
# honestly called `a$b` into a file called `a`.
run "echo hi > 'a\$b'" >/dev/null
check "redirect: a quoted target is not expanded twice" "hi" "$(cat 'a$b' 2>&1)"
# A target with a space in it is one file, not two — see finish_word_as().
check "redirect: a target holding a space is one file" "hi" \
      "$(run 'T="two words"; echo hi >$T'; cat "two words" 2>&1)"

# ── Lists and background ──────────────────────────────────
check "list: ; runs both"      "a b"  "$(run 'echo a; echo b' | tr '\n' ' ' | sed 's/ $//')"
check "list: && short-circuits" "b"   "$(run 'false && echo a; echo b')"
check "list: || short-circuits" "a"   "$(run 'false || echo a')"
check "background: & separates the two commands" "done" \
      "$(run 'sleep 0.1 & echo done' | grep -v '^\[')"
status "background: the shell does not wait" 0 'sleep 5 & true'

# ── Quoting ───────────────────────────────────────────────
check "quote: spaces held together"  "a  b c" "$(run 'echo "a  b" c')"
check "quote: quotes removed mid-word" "x=a b" "$(run 'echo x="a b"')"
check "quote: empty argument survives" "1"  "$(run 'printf "%s\n" "" | wc -l')"

# ── Built-ins ─────────────────────────────────────────────
# ── Assignments ───────────────────────────────────────────
check "assign: NAME=value sets it for the shell" "v" "$(run 'X=v; echo $X')"
check "assign: quoted value keeps its spaces"    "a b" "$(run 'X="a b"; echo $X')"
check "assign: expanded on the right-hand side"  "$HOME/x" "$(run 'X=$HOME/x; echo $X')"
# ⚠ IN FRONT OF A COMMAND IT IS THAT COMMAND'S ONLY. `LANG=C ls` must not leave
# the shell in C afterwards, which is the half a naive implementation gets wrong.
check "assign: in front of a command reaches it" "v" \
      "$(run 'X=v sh -c "echo \$X"')"
check "assign: in front of a command does NOT leak into the shell" "" \
      "$(run 'X=v true; echo $X')"

check "builtin: cd changes the shell's own directory" "/etc" "$(run 'cd /etc && pwd')"
check "builtin: export expands its value" "$HOME:/x" "$(run 'export P=$HOME:/x; echo $P')"
check "builtin: alias expands to a built-in" "/" "$(run "alias up='cd /'; up; pwd")"
check "builtin: redirection applies to a built-in" "1" \
      "$(run 'alias >alias.out'; test -e alias.out && echo 1 || echo 0)"

# ── Exit status ───────────────────────────────────────────
status "exit: true"                  0   'true'
status "exit: false"                 1   'false'
status "exit: passed through"       42   'sh -c "exit 42"'
status "exit: command not found"   127   'definitely-not-a-command-xyz'

echo
if [ "$fails" -eq 0 ]; then echo "all shell checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
