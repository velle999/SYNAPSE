#!/usr/bin/env bash
# syn-settings — smoke tests.
#
# These check the CONTRACT, not the values: a machine with no NVIDIA, no
# localectl or no compositor must still produce a well-formed table, because
# the whole design rests on "a missing tool is a value, not a failure". A test
# that asserted "keymap is us" would pass here and fail on the ISO.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

BIN=${1:?usage: syn_settings_test.sh /path/to/syn-settings}
fails=0

ok()   { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

# `((n++))` evaluates to the OLD value, so it returns 1 the first time and
# kills the script under `set -e`. Hence fails=$((fails + 1)) above.

check_table() {
    local pane=$1 out ncols nrows line
    out=$("$BIN" --rec "$pane") || { bad "$pane: exited non-zero"; return; }

    [ -n "$out" ] || { bad "$pane: printed nothing"; return; }

    ncols=$(head -1 <<<"$out" | awk -F'\t' '{print NF}')
    [ "$ncols" -ge 2 ] || { bad "$pane: header has $ncols columns"; return; }
    ok "$pane: header has $ncols columns"

    # Every row must have exactly as many fields as the header. This is the
    # test that catches an unescaped tab in a value, which would silently
    # shift every column to its right rather than fail.
    nrows=0
    while IFS= read -r line; do
        nrows=$((nrows + 1))
        local n
        n=$(awk -F'\t' '{print NF}' <<<"$line")
        if [ "$n" -ne "$ncols" ]; then
            bad "$pane: row $nrows has $n fields, header has $ncols"
            return
        fi
    done < <(tail -n +2 <<<"$out")
    ok "$pane: $nrows rows, all $ncols fields wide"
}

echo "syn-settings smoke tests"

for pane in display region power system; do
    check_table "$pane"
done

# An unknown pane must be refused, not silently empty.
if "$BIN" --rec nonesuch >/dev/null 2>&1; then
    bad "unknown pane was accepted"
else
    ok "unknown pane refused"
fi

# --help must work and must not need a display.
if "$BIN" --help | grep -q 'syn-settings'; then
    ok "--help prints usage"
else
    bad "--help printed nothing useful"
fi

# Writes must never run during a test. --dry-run is the guarantee, so it is
# itself tested: if this ever stopped being honoured the suite would start
# changing the machine it runs on.
if "$BIN" --dry-run set timezone UTC | grep -q '^would run: timedatectl set-timezone UTC$'; then
    ok "--dry-run shows the command and runs nothing"
else
    bad "--dry-run did not report the expected command"
fi

# Argument validation: a value that could be read as a flag must be refused
# before it reaches a tool that would honour it.
for bad_val in "--adjust" "a;b" 'x$(id)' "back\`tick\`"; do
    if "$BIN" --dry-run set timezone "$bad_val" >/dev/null 2>&1; then
        bad "accepted hostile value: $bad_val"
    else
        ok "refused: $bad_val"
    fi
done

if "$BIN" --dry-run unit enable not-a-unit >/dev/null 2>&1; then
    bad "accepted a name with no unit suffix"
else
    ok "refused a name with no unit suffix"
fi

if [ "$fails" -gt 0 ]; then
    printf '\n%d test(s) failed\n' "$fails"
    exit 1
fi
printf '\nall tests passed\n'
