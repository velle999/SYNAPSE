#!/usr/bin/env bash
#
# contract_test.sh — the columns syn-cal emits against the columns synui reads.
#
# ⛔ THIS SEAM BREAKS SILENTLY AND IN THE WORST WAY. synui's calendar popup
# parses `syn-cal --rec agenda` by FIELD INDEX. Insert a column in the middle of
# that record and the compositor keeps parsing, keeps drawing, and shows the
# wrong text at the wrong time — a calendar confidently displaying a location
# where a summary should be. Nothing errors, and neither component's own tests
# notice, because each is correct on its own.
#
# So the two are compared against each other here: the header syn-cal writes,
# and the indices synui reads out of it.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

# ⛔ THE PROGRAM UNDER TEST IS TRANSLATED NOW, AND THIS FILE ASSERTS ENGLISH.
# syn-cal's compiled-in localedir is /usr/share/locale, so on a machine where
# syn-cal is INSTALLED a freshly built binary loads the INSTALLED catalog and
# answers in the desktop's language — every assertion about a message then fails
# against a program that is working perfectly, and a failing `meson test` is a
# BUILD failure, so `syn-update` refuses to install it. synpkg 47 did exactly
# that on a Japanese desktop.
#
# ⚠ Running this under LANG=ja on a box where syn-cal is not installed does NOT
# catch it: with no catalog to find, gettext falls back to the msgid and it all
# passes in English. Reproduce with SYN_CAL_LOCALEDIR pointed at a built catalog.
#
# ⚠ LANGUAGE as well as LC_ALL — gettext reads LANGUAGE FIRST.
export LC_ALL=C.UTF-8
unset LANGUAGE


HERE=$(cd "$(dirname "$0")" && pwd)
MAIN="$HERE/../src/main.c"
CALEV="$HERE/../../synui/src/calevents.c"

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

if [ ! -f "$CALEV" ]; then
    echo "  skip  synui/src/calevents.c is not in this tree"
    exit 0
fi

# The header syn-cal actually writes for `agenda`.
hdr=$(sed -n 's/.*rec_header("\(start\\t[^"]*\)").*/\1/p' "$MAIN" | head -1)
[ -n "$hdr" ]
check "the agenda's --rec header is where it is expected to be" $?

# Turn it into a list of column names, in order.
cols=$(printf '%s' "$hdr" | sed 's/\\t/ /g')
idx_of() { printf '%s' "$cols" | tr ' ' '\n' | grep -n "^$1\$" | cut -d: -f1; }

n_start=$(( $(idx_of start) - 1 ))
n_allday=$(( $(idx_of all_day) - 1 ))
n_summary=$(( $(idx_of summary) - 1 ))
n_cols=$(printf '%s' "$cols" | wc -w)

[ "$n_cols" -ge 7 ]
check "…and has at least the seven columns synui requires ($n_cols)" $?

# What synui reads. Each of these must be the index of the column it names.
grep -q "e->start = (time_t)strtoll(f\[$n_start\]" "$CALEV"
check "synui reads 'start' from column $n_start" $?

grep -q "e->all_day = f\[$n_allday\]" "$CALEV"
check "…'all_day' from column $n_allday" $?

grep -q "pct_into(e->summary, sizeof e->summary, f\[$n_summary\]" "$CALEV"
check "…and 'summary' from column $n_summary" $?

# ⚠ AND IT REFUSES A SHORT ROW. A record with fewer fields than expected must
# be dropped, not read past the end of the array it was split into.
grep -q "if (nf >= 7 && cal->nev < CAL_EVENTS_MAX)" "$CALEV"
check "…and drops a row with too few fields rather than reading past it" $?

# ⛔ THE COMPOSITOR MUST NOT BLOCK ON THIS. A synchronous read on a key path
# freezes every window on the machine for as long as syn-cal takes.
#
# ⚠ COMMENTS STRIPPED FIRST. calevents.c explains at length why it does not
# popen, and a grep over the raw file therefore fails on the file's own account
# of why it passes. Second time this exact trap has been hit in this component —
# a check that reads source reads its prose too.
code=$(sed 's://.*::' "$CALEV" | perl -0777 -pe 's{/\*.*?\*/}{}gs' 2>/dev/null || sed 's://.*::' "$CALEV")
! printf '%s' "$code" | grep -q "popen(\|system("
check "synui never popen()s syn-cal — the fetch is asynchronous" $?

grep -q "wl_event_loop_add_fd" "$CALEV"
check "…it reads through the wl_event_loop" $?

# ⛔ THE HANGUP ARRIVES WITH THE LAST OF THE DATA. Acting on it before the read
# loses the final chunk — already written down for greeter.c and ipc.c.
sed -n '/static int read_cb/,/^}/p' "$CALEV" | grep -q "WL_EVENT_READABLE"
r=$?
sed -n '/static int read_cb/,/^}/p' "$CALEV" | awk '/WL_EVENT_READABLE/{r=NR} /WL_EVENT_HANGUP/{h=NR} END{exit !(r && h && r < h)}'
[ $r = 0 ] && [ $? = 0 ]
check "…and reads before it acts on the hangup, not instead of it" $?

# One fetch at a time, or holding an arrow key settles the panel on whichever
# month happened to answer last.
grep -q "calevents_cancel();" "$CALEV"
check "…and cancels the previous fetch before starting another" $?

echo
echo "$pass/$((pass+fail)) passed"
exit $([ "$fail" = 0 ] && echo 0 || echo 1)
