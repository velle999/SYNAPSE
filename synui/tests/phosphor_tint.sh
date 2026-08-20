#!/bin/sh
# phosphor_tint.sh — the hot core must saturate toward the PHOSPHOR's spectrum,
# not toward white.
#
# velle, 2026-08-19, with colours sampled from a photograph of a real amber tube
# and from a capture of this filter:
#
#                    G/R     B/R
#     real tube hot  0.933   0.079      saturates to YELLOW; no blue to emit
#     ours      hot  0.934   0.737      CREAM
#
# The cause was `mix(u_tint, vec3(1.0), ...)`: 75% toward white takes amber's
# blue from 0.12 to 0.78. A phosphor driven hard does not begin emitting light
# it has no phosphor for.
#
# ⚠ WHAT THIS DOES AND DOES NOT PROVE. There is no GL harness here — the shader
# is a string compiled by a live compositor, and replaying it needs the REAL
# driver (llvmpipe renders this pass correctly and so proves nothing). So this
# checks the two halves that CAN be checked without a GPU: that the shader still
# says what it is meant to, and that the arithmetic that says lands where the
# measurements want. An end-to-end render check would need a capture on real
# hardware, which is velle's eye and not this file.
set -u

SRC=${1:?usage: phosphor_tint.sh /path/to/effects.c /path/to/phosphor_replay.py}
REPLAY=${2:?usage: phosphor_tint.sh /path/to/effects.c /path/to/phosphor_replay.py}

pass=0 fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

# ⚠ The anchor is asserted FIRST and on its own. A test that only grepped for
# the good expression would pass silently if the block were renamed or removed
# — matching nothing prints ok. This fails loudly instead.
grep -q 'smoothstep(PH_HOT, 1.0, e) \* PH_HOTMAX' "$SRC" \
    && ok "the hot-core block is still where this test thinks it is" \
    || bad "the hot-core block moved or changed shape — re-read effects.c"

grep -q 'mix(u_tint, vec3(1.0, 1.0, u_tint.b)' "$SRC" \
    && ok "the hot core saturates toward the phosphor's own spectrum" \
    || bad "the hot core no longer targets vec3(1.0, 1.0, u_tint.b)"

grep -q 'mix(u_tint, vec3(1.0),' "$SRC" \
    && bad "the hot core whitens again — this is the cream-highlight regression" \
    || ok "...and not toward white"

out=$("$REPLAY" 2>&1) || { echo "$out"; echo "  FAIL  the replay did not run" >&2; exit 1; }

# Amber's hot core, as the shipping shader computes it. B/R is the assertion:
# the whitening version puts it at 0.780 and the tube measures 0.079.
echo "$out" | grep -q 'predicted, proposed  G/R 0.925  B/R 0.120' \
    && ok "amber's hot core lands at B/R 0.120, against a tube's 0.079" \
    || bad "amber's hot core is not where the replay says it should be"

echo "$out" | grep -q 'predicted, current   G/R 0.925  B/R 0.780' \
    && ok "...and the whitening version is still reproduced, at B/R 0.780" \
    || bad "the replay no longer reproduces the fault it was written to explain"

# The other two phosphors ride on the same expression and must not regress.
echo "$out" | grep -q 'green hot  current #d4ffcc   proposed #d4ff33' \
    && ok "green saturates yellow-green rather than minty white" \
    || bad "green's hot core changed"

echo "$out" | grep -q 'white hot  current #fffdfa   proposed #fffdeb' \
    && ok "white barely moves, its blue already being 0.92" \
    || bad "white's hot core changed"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
