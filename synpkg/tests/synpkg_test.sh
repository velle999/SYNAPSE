#!/usr/bin/env bash
#
# synpkg_test.sh — read-only checks against a real ALPM database.
#
# Everything here is a QUERY. The test suite runs under `meson test`, which may
# run on a live desktop or in a makepkg chroot, and a package manager test that
# can install or remove is one bad path away from mutating the machine that is
# building it. Nothing below takes the database lock or needs root.
#
# Environment-dependent facts (is BlackArch enabled, is anything upgradable) are
# checked for SHAPE, never for a specific answer: those differ between velle's
# box, the ISO build chroot, and a fresh install, and a test that only passes on
# one of them is a test that gets disabled.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

SYNPKG=${1:-./build/synpkg}
[ -x "$SYNPKG" ] || { echo "not executable: $SYNPKG" >&2; exit 1; }

# The catalogue lives beside the source during a build, not at SYNPKG_DATADIR.
export SYNPKG_CURATED="${SYNPKG_CURATED:-$(dirname "$0")/../data/curated.tsv}"

pass=0 fail=0

ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# `((n++))` evaluates to the OLD value, so a bare post-increment returns 1 the
# first time and kills the script under `set -e`. Hence $((n + 1)) above.

echo "synpkg tests — $SYNPKG"

# ── the binary answers at all ───────────────────────────────────────────────
"$SYNPKG" --version | grep -q '^synpkg '
check "--version prints a version" $?

"$SYNPKG" --help | grep -q 'the SynapseOS package manager'
check "--help prints usage" $?

"$SYNPKG" definitely-not-a-command >/dev/null 2>&1
[ $? -eq 2 ] && ok "unknown command exits 2" || bad "unknown command exits 2"

# ── TSV shape ───────────────────────────────────────────────────────────────
# The GUI splits on tab and trusts the column count. A row with the wrong
# number of fields renders as blank cells with no error anywhere, so the field
# count is the single most important invariant in this program.

tsv_cols() { awk -F'\t' 'NR==1 {print NF}'; }

n=$("$SYNPKG" --tsv status | tsv_cols)
[ "$n" = 4 ] && ok "status --tsv has 4 columns" || bad "status --tsv has 4 columns (got $n)"

n=$("$SYNPKG" --tsv installed --explicit | tsv_cols)
[ "$n" = 6 ] && ok "installed --tsv has 6 columns" || bad "installed --tsv has 6 columns (got $n)"

n=$("$SYNPKG" --tsv suggest | tsv_cols)
[ "$n" = 6 ] && ok "suggest --tsv has 6 columns" || bad "suggest --tsv has 6 columns (got $n)"

n=$("$SYNPKG" --tsv updates | tsv_cols)
[ "$n" = 5 ] && ok "updates --tsv has 5 columns" || bad "updates --tsv has 5 columns (got $n)"

# Every data row must carry the same field count as its header. This is what
# catches a description containing a literal tab — the exact bug the field
# stripping in tsv_row() exists to prevent, and one that only appears when some
# package upstream adds one.
ragged=$("$SYNPKG" --tsv installed | awk -F'\t' 'NR==1 {want=NF; next} NF!=want {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in installed --tsv" \
                  || bad "$ragged ragged rows in installed --tsv"

ragged=$("$SYNPKG" --tsv suggest | awk -F'\t' 'NR==1 {want=NF; next} NF!=want {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in suggest --tsv" \
                  || bad "$ragged ragged rows in suggest --tsv"

# ── TSV mode never writes anything but records to stdout ────────────────────
# A single stray progress line on stdout becomes a garbage row in the GUI.
stray=$("$SYNPKG" --tsv status 2>/dev/null | grep -cv $'\t')
[ "$stray" = 0 ] && ok "status --tsv writes only tab-separated rows" \
                 || bad "status --tsv wrote $stray non-record lines to stdout"

# ── the curated catalogue ───────────────────────────────────────────────────
# A malformed line warns rather than rendering an empty row, so the absence of
# warnings is the check.
warns=$("$SYNPKG" --tsv suggest 2>&1 >/dev/null | grep -c 'catalogue line')
[ "$warns" = 0 ] && ok "curated.tsv parses with no malformed lines" \
                 || bad "$warns malformed lines in curated.tsv"

"$SYNPKG" suggest categories | grep -q 'Browsers'
check "suggest categories lists Browsers" $?

# Filtering by a category must not return the whole catalogue.
all=$("$SYNPKG" --tsv suggest | wc -l)
one=$("$SYNPKG" --tsv suggest Browsers | wc -l)
[ "$one" -lt "$all" ] && ok "suggest <category> filters" \
                      || bad "suggest <category> returned everything ($one/$all)"

# Every catalogue field must be non-empty: a blank label renders as an unnamed
# button, which is worse than the entry being absent.
blank=$(awk -F'\t' '/^[^#]/ && NF>0 { for (i=1;i<=5;i++) if ($i=="") { n++; break } } END {print n+0}' \
        "$SYNPKG_CURATED")
[ "$blank" = 0 ] && ok "no empty fields in curated.tsv" || bad "$blank entries have an empty field"

# ── exit codes a poller depends on ──────────────────────────────────────────
# 100 means "nothing to do" and MUST be distinguishable from 1, or a status bar
# reports a failure every time the system is current.
"$SYNPKG" --tsv updates >/dev/null 2>&1
rc=$?
[ "$rc" = 0 ] || [ "$rc" = 100 ] && ok "updates exits 0 or 100" \
                                 || bad "updates exited $rc"

# ── arsenal degrades without the repo ───────────────────────────────────────
# status must answer on a machine with no BlackArch rather than failing: the
# whole point of the app on a fresh install is telling you it is missing.
"$SYNPKG" --tsv arsenal status >/dev/null 2>&1
rc=$?
case $rc in
    0|2|3) ok "arsenal status answers (rc=$rc)" ;;
    *)     bad "arsenal status exited $rc" ;;
esac

n=$("$SYNPKG" --tsv arsenal status | tsv_cols)
[ "$n" = 3 ] && ok "arsenal status --tsv has 3 columns" \
             || bad "arsenal status --tsv has 3 columns (got $n)"

# A non-blackarch group must be refused rather than listed: `packages base` on
# an unvalidated path would render a core group as security tooling.
"$SYNPKG" arsenal packages base >/dev/null 2>&1
[ $? -ne 0 ] && ok "arsenal packages rejects a non-blackarch group" \
             || bad "arsenal packages accepted a non-blackarch group"

# ── mutations refuse rather than assume ─────────────────────────────────────
# In TSV mode confirm() returns false, so a transaction without --noconfirm
# must decline. This is what stops a GUI click from installing silently if the
# --noconfirm flag is ever dropped from the QML.
"$SYNPKG" install >/dev/null 2>&1
[ $? -ne 0 ] && ok "install with no targets is an error" \
             || bad "install with no targets succeeded"

"$SYNPKG" remove >/dev/null 2>&1
[ $? -ne 0 ] && ok "remove with no targets is an error" \
             || bad "remove with no targets succeeded"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
