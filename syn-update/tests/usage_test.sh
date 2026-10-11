#!/usr/bin/env bash
# usage_test.sh — printing the help runs nothing
#
# usage() is an UNQUOTED heredoc, because its first line expands $VERSION. In
# an unquoted heredoc a backtick is a command substitution, and the help text
# quotes commands in backticks the way every comment here does. So `syn-update
# help` RAN `pacman -Sy` and `pacman -R`, and tried to run a command named
# `-Syy`: three errors on stderr, and three holes in the text where the
# commands should have been. Under `sudo syn-update help` the first one is a
# real database sync — the partial-upgrade setup Arch tells everyone to avoid.
#
# Stubs first on PATH log every call to pacman or anything that escalates, and
# refuse it. Printing the help must log nothing, and must SHOW the commands.
set -u

here=$(cd "$(dirname "$0")" && pwd)
E=${1:-$here/../syn-update.sh}
[ -f "$E" ] || { echo "  ABORT no syn-update.sh at $E"; exit 1; }

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir "$T/bin"
for c in pacman sudo run0 pkexec su doas; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/calls"\nexit 1\n' "$c" "$T" > "$T/bin/$c"
    chmod +x "$T/bin/$c"
done

for arg in help --help -h; do
    : > "$T/calls"
    PATH="$T/bin:$PATH" bash "$E" "$arg" > "$T/out" 2> "$T/err"
    rc=$?
    if [ "$rc" -eq 0 ]; then ok "$arg exits 0"; else bad "$arg exited $rc"; fi
    if [ -s "$T/calls" ]; then
        bad "$arg ran: $(tr '\n' ';' < "$T/calls")"
    else
        ok "$arg runs no pacman and no escalation"
    fi
    if [ -s "$T/err" ]; then
        bad "$arg wrote to stderr: $(head -3 "$T/err" | tr '\n' ';')"
    else
        ok "$arg writes nothing to stderr"
    fi
done

# The text itself, from the last run: each quoted command is on the page.
for want in '`pacman -Sy`' '`-Syy`' '`pacman -R`'; do
    if grep -qF -- "$want" "$T/out"; then ok "the help shows $want"
    else bad "the help lost $want"; fi
done

echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
