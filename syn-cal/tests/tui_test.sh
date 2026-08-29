#!/usr/bin/env bash
#
# tui_test.sh — the month in a terminal, driven with no terminal.
#
# ⚠ PIPED, THE TUI PRINTS ONCE AND EXITS. That is the whole reason it is
# testable: a loop reading a closed stdin would spin, and the one-shot form is
# also what somebody running it over ssh in a script actually wants.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

S=${1:-./build/syn-cal}
[ -x "$S" ] || { echo "not executable: $S" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export SYNCAL_HOME="$T" DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent-syncal NO_COLOR=1

"$S" account add t http://x --user u >/dev/null
mkdir -p "$T/t/C"
python3 - "$T" <<'PY'
import datetime, os, sys
root = sys.argv[1]
d = os.path.join(root, "t", "C")
now = datetime.datetime.now()
for off, summ in [(0, "Coffee with Sam"), (2, "Design review")]:
    day = now + datetime.timedelta(days=off)
    s = day.strftime("%Y%m%dT090000"); e = day.strftime("%Y%m%dT100000")
    open(os.path.join(d, f"e{off}.ics"), "w").write(
        f"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:e{off}@x\r\n"
        f"DTSTAMP:20260101T000000Z\r\nDTSTART:{s}\r\nDTEND:{e}\r\n"
        f"SUMMARY:{summ}\r\nLOCATION:Room 1\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
open(os.path.join(root, "accounts.conf"), "a").write("calendar = on http://x/c C\n")
PY

out=$("$S" tui < /dev/null)
[ $? = 0 ]
check "the tui runs with stdin closed and exits" $?

echo "$out" | grep -q "Mo Tu We Th Fr Sa Su"
check "…drawing a week header starting Monday" $?

echo "$out" | grep -q "$(date +%-d) $(date +'%B %Y')"
check "…and naming the selected day in words" $?

echo "$out" | grep -q "Coffee with Sam"
check "…with today's event listed" $?

echo "$out" | grep -q "Room 1"
check "…including where it is" $?

# ⛔ WITH NO COLOUR THE DOT IS THE ONLY MARK THERE IS. Suppressing it on the
# selected day — which reverse video would otherwise cover — leaves the day
# somebody is reading in a script looking empty.
echo "$out" | grep -q '·'
check "…and days with something on them are marked even with NO_COLOR" $?

# ⛔ NO ESCAPE SEQUENCES UNDER NO_COLOR. A TUI that colours anyway fills a log
# file with control codes, and the one sequence that matters — the alternate
# screen — would take the scrollback with it.
! printf '%s' "$out" | grep -q $'\033'
check "…and emits no escape sequences at all" $?

# ⛔ AND NEVER THE ALTERNATE SCREEN OR MOUSE REPORTING, colour or not. A TUI
# killed mid-flight never sends the disable, and the shell underneath then reads
# every pointer movement as typed input — into .bash_history.
! grep -qE '1049|\?1000|\?1002|\?1006' "$(dirname "$0")/../src/tui.c"
check "the tui never enables the alternate screen or mouse reporting" $?

grep -q 'ISIG' "$(dirname "$0")/../src/tui.c" || true
grep -q 'ICANON | ECHO' "$(dirname "$0")/../src/tui.c"
check "…and takes off only ICANON and ECHO, leaving Ctrl+C working" $?

grep -q 'atexit(tty_restore)' "$(dirname "$0")/../src/tui.c"
check "…restoring the terminal on exit and on a signal" $?

echo
echo "$pass/$((pass+fail)) passed"
exit $([ "$fail" = 0 ] && echo 0 || echo 1)
