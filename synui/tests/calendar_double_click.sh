#!/bin/sh
# calendar_double_click.sh — double-clicking a day in the bar's calendar opens
# syn-cal on that day, with a new event ready.
#
# ⛔ WHY THIS IS A SOURCE TEST AND NOT A CAPTURE. Everything else that proves a
# click does something drives synthetic input at a nested synui — and this
# particular click SPAWNS AN APPLICATION. A capture test for it would open a
# real syn-cal window on whatever seat the suite ran on, which on a developer's
# machine is their desktop. The bar_enabled-style rigs are worth their weight
# for pixels; they are the wrong tool for a side effect that escapes the
# compositor.
#
# So this pins the three things that were actually easy to get wrong, all of
# which are visible in the source and none of which a screenshot would show.
#
# SynapseOS Project — GPL-2.0-or-later
set -u

SRC=${1:-src}
pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

CLOCK="$SRC/clock.c"
[ -f "$CLOCK" ] || { echo "no such file: $CLOCK" >&2; exit 1; }

echo "calendar double click"

# ── 1. It happens at all ────────────────────────────────────────────────────

grep -q 'syn-cal gui %04d-%02d-%02d' "$CLOCK"
chk "a double click runs syn-cal on the day that was clicked" $?

# ⛔ BUILT FROM INTEGERS, NEVER PASTED. synui_spawn() runs /bin/sh -c, so a
# date assembled with %s from anything at all is a shell injection waiting for
# the right calendar state; assembled with %04d-%02d-%02d it cannot be one.
! grep -q 'syn-cal gui %s' "$CLOCK"
chk "…with the date built from integers, not interpolated as a string" $?

# ── 2. The ordering bug ─────────────────────────────────────────────────────
#
# ⛔ THE DOUBLE CLICK MUST BE TESTED BEFORE THE `day == cal->sel` EARLY RETURN.
# The FIRST click of the pair selects the day, so the SECOND always lands on the
# already-selected day — which is exactly what that return discards. Written the
# other way round, a double click works on every day except the one you just
# clicked, and the one you just clicked is every double click.
awk '/^int calendar_click/ { inf = 1 }
     inf && /last_click_day == day/  { dbl = NR }
     inf && /day == cal->sel/        { sel = NR }
     inf && /^}/ && inf++ > 0 && dbl && sel { exit !(dbl < sel) }
     END { if (!dbl || !sel) exit 1 }' "$CLOCK"
chk "…decided before the early return that discards a repeat click" $?

# ── 3. What it leaves behind ────────────────────────────────────────────────

# The popup closes: leaving it up puts the calendar over the window it just
# opened, and the new-event form comes up behind it.
awk '/^int calendar_click/ { inf = 1 }
     inf && /synui_spawn\(cmd\)/ { spawn = NR }
     inf && /calendar_hide\(s\)/ && spawn { hide = NR }
     END { exit !(spawn && hide && hide > spawn) }' "$CLOCK"
chk "…and the popup closes rather than covering it" $?

# ── 4. The other end of it ──────────────────────────────────────────────────
#
# A command that opens nothing is the whole feature failing silently, so the
# receiving side is pinned here too rather than only in syn-cal's own suite.
CAL=${2:-../syn-cal}
if [ -f "$CAL/src/main.c" ]; then
    grep -q 'SYNCAL_NEW_ON' "$CAL/src/main.c"
    chk "syn-cal gui passes the day on to the window" $?

    grep -q 'unsetenv("SYNCAL_NEW_ON")' "$CAL/src/main.c"
    chk "…and clears it when there is none, so a stale one cannot be inherited" $?

    grep -q 'SYNCAL_NEW_ON' "$CAL/data/syn-cal.qml"
    chk "…and the window acts on it" $?
else
    echo "  --    syn-cal tree not beside this one; skipping the receiving end"
fi

echo "$pass/$((pass + fail)) passed"
[ "$fail" = 0 ]
