#!/bin/sh
# syncalc.sh — syn-calc(1), the calculator with no compositor behind it.
#
# calc_test covers the parser itself in C. What this covers is the PROGRAM: the
# arguments, stdin, the exit status and — the one that matters in a script —
# that a refusal prints nothing to stdout. A calculator that answered "0" for a
# syntax error would be worse than useless in a pipeline.
#
# Usage: syncalc.sh /path/to/syn-calc
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

CALC=${1:?usage: syncalc.sh /path/to/syn-calc}
[ -x "$CALC" ] || { echo "not executable: $CALC" >&2; exit 1; }

fails=0
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1" >&2; fails=$((fails + 1)); }

want() {   # want <expected> <expression...>
    _want=$1; shift
    _got=$("$CALC" "$@" 2>/dev/null)
    if [ "$_got" = "$_want" ]; then ok "$* = $_want"
    else bad "$* gave '$_got', want '$_want'"; fi
}

want 1152 '1440 * 0.8'
want 4    '2+2'
want 1024 '2^10'
want 8    'sqrt(64)'
want -3   '1 - 4'

# Arguments are joined, so an unquoted expression works too — the docs still
# teach the quoted form, because a shell eats * and ( ) before this ever runs.
want 4 2 + 2

# A filter, with the newline echo adds. This is the shape a script uses.
got=$(echo '2^10' | "$CALC")
[ "$got" = 1024 ] && ok "reads an expression from stdin" \
                  || bad "stdin gave '$got', want 1024"

# A wrapped expression is one expression, not two.
got=$(printf '3 *\n(2+2)\n' | "$CALC")
[ "$got" = 12 ] && ok "a multi-line expression is one expression" \
                || bad "multi-line gave '$got', want 12"

# THE script-safety property: a refusal exits non-zero and says NOTHING on
# stdout, so `x=$(syn-calc "$expr")` cannot silently become an answer.
out=$("$CALC" '2 +' 2>/dev/null)
rc=$?
[ "$rc" != 0 ] && ok "a bad expression exits non-zero" \
               || bad "a bad expression exited 0"
[ -z "$out" ] && ok "a bad expression prints nothing to stdout" \
              || bad "a bad expression printed '$out'"
"$CALC" '2 +' 2>&1 >/dev/null | grep -q . \
    && ok "…and says why on stderr" || bad "a refusal explained nothing"

# --funcs is what makes the function list discoverable without the panel.
"$CALC" --funcs | grep -q sqrt && ok "--funcs lists the functions" \
                               || bad "--funcs did not list sqrt"

# No expression and no pipe is somebody looking for the usage, not a program
# that should hang waiting for input.
"$CALC" </dev/null >/dev/null 2>&1
[ $? -ne 0 ] && ok "no input is a usage error, not a hang" \
             || bad "empty input returned success"

# It must not have grown a compositor. calceval.c was split out of calc.c for
# exactly this, and a stray dependency is how that erodes.
if command -v ldd >/dev/null 2>&1; then
    if ldd "$CALC" | grep -qE 'wlroots|wayland|cairo|xkbcommon'; then
        bad "syn-calc links the compositor's libraries"
    else
        ok "syn-calc links no compositor libraries"
    fi
fi

[ "$fails" = 0 ] || { echo "syncalc: $fails failure(s)" >&2; exit 1; }
echo "syncalc: ok"
