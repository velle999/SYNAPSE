#!/bin/sh
# hdr_probe_is_read_only.sh — the HDR probe must never change the screen.
#
# ⛔ THE WHOLE SAFETY PROPERTY IS ONE FUNCTION NAME. wlr_output_test_state()
# asks the kernel "would this commit succeed" via DRM's TEST_ONLY and changes
# nothing; wlr_output_commit_state() does it. A probe that reached for the
# second to answer the first would put a display into HDR in order to find out
# whether it can be — and on hardware that half-supports it, that is a black
# screen on somebody's only monitor, produced by a query.
#
# There is no way to catch that at runtime except on the hardware it would
# break, so it is caught here instead.
#
# SynapseOS Project — GPL-2.0-or-later
set -u

SRC=${1:-src}
F="$SRC/hdrprobe.c"
[ -f "$F" ] || { echo "no such file: $F" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

echo "hdr probe"

grep -q "wlr_output_test_state" "$F"
chk "the probe asks with a test commit" $?

! grep -qE "wlr_output_commit_state|wlr_output_commit\(" "$F"
chk "…and never commits one" $?

# ⚠ EVERY STATE IT OPENS MUST BE FINISHED. This runs once per output per query,
# and a machine that polls it would leak a state's worth of buffers for as long
# as the compositor lives.
# ⚠ try_state() FINISHES ON THE CALLER'S BEHALF, so counting finish calls alone
# calls correct code a leak. What must hold is that every state opened either
# reaches try_state or is finished directly.
inits=$(grep -c "wlr_output_state_init" "$F")
fins=$(grep -c "wlr_output_state_finish" "$F")
tries=$(grep -c "try_state(o, &st)" "$F")
[ "$inits" -le $((fins + tries)) ]
chk "…and every state it opens is finished or handed to one that does ($inits opened, $fins finished, $tries tested)" $?

# Colour transforms are reference-counted; the same argument applies.
#
# ⚠ COMMENT LINES ARE STRIPPED FIRST. This file explains itself at length and
# names these functions in prose; counting those mentions called correct code a
# leak. Every comment line in this tree opens with `*` or `/*`.
code() { grep -vE '^[[:space:]]*(\*|/\*)' "$F"; }
refs=$(code | grep -c "wlr_color_transform_init")
unrefs=$(code | grep -c "wlr_color_transform_unref")
[ "$refs" -le "$unrefs" ]
chk "…and every transform it makes is released ($refs made, $unrefs freed)" $?

# ⛔ THE TWO HALVES ARE REPORTED SEPARATELY, because they fail separately:
# signalling HDR10 is a different capability from mapping SDR into it, and a
# machine that takes the first and refuses the second shows a washed-out
# desktop rather than an error. Collapsing them into one yes/no would hide the
# only interesting answer.
grep -q "image_description=" "$F"
chk "the report says whether HDR10 can be signalled" $?

grep -q "pipeline=" "$F"
chk "…and separately whether SDR can be mapped into it" $?

# ── hdr.c: the same property, on a file that DOES commit ──────────────────
#
# ⛔ hdr.c IS ALLOWED TO COMMIT — driving a display in HDR is what it is for —
# so the blanket rule above cannot be applied to it. What must still hold is
# that ASKING never commits: hdr_probe() runs on every connector at startup and
# again on every `synctl hdr`, on monitors that will refuse, and a commit in
# there would put a display into HDR to find out whether it can be. The
# capability answer must cost nothing.
H="$SRC/hdr.c"
[ -f "$H" ] || { echo "no such file: $H" >&2; exit 1; }

echo "hdr mode"

# The body of hdr_probe(), from its opening brace to the closing brace in
# column 1 — the file's own layout, which every function here follows.
probe_body() {
    awk '/^void hdr_probe\(/ { in_fn = 1 } in_fn { print } in_fn && /^}/ { exit }' "$H"
}

[ -n "$(probe_body)" ]
chk "hdr_probe() is where the capability question is asked" $?

! probe_body | grep -qE "wlr_output_commit_state|wlr_output_commit\("
chk "…and it never commits, only tests" $?

probe_body | grep -q "hdr_lut_ok\|hdr_sdr_ok"
chk "…and it asks about the curve and the way back out, not just the signal" $?

# ⛔ AND THE WAY OUT IS PART OF THE CAPABILITY. An output state that leaves the
# image description unset leaves the connector carrying whatever the last commit
# gave it, so a display put into PQ has to be explicitly taken back to sRGB. A
# connector that will not accept that must never be offered the mode: a mode you
# cannot leave is worse than one you never had.
probe_body | grep -q "hdr_capable = 1"
chk "…and capable is only set once all three have answered yes" $?

awk '/hdr_sdr_ok = /,/hdr_capable = 1/' "$H" | grep -q "return"
chk "…with an early return when the way out is refused" $?

# Reference counting, on the same terms as the probe file above.
hcode() { grep -vE '^[[:space:]]*(\*|/\*)' "$H"; }
hrefs=$(hcode | grep -c "wlr_color_transform_init")
hunrefs=$(hcode | grep -c "wlr_color_transform_unref")
[ "$hrefs" -le "$hunrefs" ]
chk "every transform hdr.c makes is released ($hrefs made, $hunrefs freed)" $?

echo "$pass/$((pass + fail)) passed"
[ "$fail" = 0 ]
