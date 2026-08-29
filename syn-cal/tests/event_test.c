/* event_test.c — recurrence, overrides, and the clock change.
 *
 * ⛔ THE DST CASE IS THE REASON THIS FILE EXISTS. A weekly 09:00 London meeting
 * is 08:00 UTC through the summer and 09:00 UTC through the winter. An expander
 * that takes the first occurrence's offset as the rule's offset gives the right
 * answer for six months and the wrong one for the other six — and it is right
 * whenever anybody checks it in July. The fixture below crosses the last Sunday
 * of October 2026 deliberately.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, total = 0;
static void ok_(const char *what, bool cond)
{
	total++;
	if (cond) printf("  ok    %s\n", what);
	else { printf("  FAIL  %s\n", what); fails++; }
}

/* A real VTIMEZONE, because a real server sends one and libical resolves the
 * offset out of it rather than out of a guess. */
#define LONDON_TZ \
"BEGIN:VTIMEZONE\r\nTZID:Europe/London\r\n" \
"BEGIN:DAYLIGHT\r\nTZOFFSETFROM:+0000\r\nTZOFFSETTO:+0100\r\nTZNAME:BST\r\n" \
"DTSTART:19700329T010000\r\nRRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\r\nEND:DAYLIGHT\r\n" \
"BEGIN:STANDARD\r\nTZOFFSETFROM:+0100\r\nTZOFFSETTO:+0000\r\nTZNAME:GMT\r\n" \
"DTSTART:19701025T020000\r\nRRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nEND:STANDARD\r\n" \
"END:VTIMEZONE\r\n"

static const char *WEEKLY_LONDON =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\n" LONDON_TZ
"BEGIN:VEVENT\r\nUID:weekly@x\r\nDTSTAMP:20260101T000000Z\r\n"
"DTSTART;TZID=Europe/London:20261007T090000\r\n"
"DTEND;TZID=Europe/London:20261007T093000\r\n"
"RRULE:FREQ=WEEKLY;COUNT=6\r\nSUMMARY:Standup\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

static const char *WITH_EXDATE =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
"BEGIN:VEVENT\r\nUID:ex@x\r\nDTSTAMP:20260101T000000Z\r\n"
"DTSTART:20260907T100000Z\r\nDTEND:20260907T110000Z\r\n"
"RRULE:FREQ=DAILY;COUNT=4\r\nEXDATE:20260908T100000Z\r\n"
"SUMMARY:Daily\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

/* The master says 10:00 every day; one occurrence was moved to 15:00. Both
 * VEVENTs carry the same UID, which is how iCalendar says "this one instead". */
static const char *WITH_OVERRIDE =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
"BEGIN:VEVENT\r\nUID:ov@x\r\nDTSTAMP:20260101T000000Z\r\n"
"DTSTART:20260907T100000Z\r\nDTEND:20260907T110000Z\r\n"
"RRULE:FREQ=DAILY;COUNT=3\r\nSUMMARY:Daily\r\nEND:VEVENT\r\n"
"BEGIN:VEVENT\r\nUID:ov@x\r\nDTSTAMP:20260101T000000Z\r\n"
"RECURRENCE-ID:20260908T100000Z\r\n"
"DTSTART:20260908T150000Z\r\nDTEND:20260908T160000Z\r\n"
"SUMMARY:Daily (moved)\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

static const char *ALL_DAY =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
"BEGIN:VEVENT\r\nUID:allday@x\r\nDTSTAMP:20260101T000000Z\r\n"
"DTSTART;VALUE=DATE:20260915\r\nDTEND;VALUE=DATE:20260916\r\n"
"SUMMARY:Public holiday\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

static size_t count_summary(events_t *l, const char *s)
{
	size_t n = 0;
	for (size_t i = 0; i < l->n; i++)
		if (l->e[i].summary && strcmp(l->e[i].summary, s) == 0) n++;
	return n;
}

int main(void)
{
	events_t l;
	/* 1 Sep 2026 to 1 Dec 2026, in UTC. */
	time_t from = 1788220800;   /* 2026-09-01T00:00:00Z */
	time_t to   = 1796083200;   /* 2026-12-01T00:00:00Z */

	/* ── the clock change ───────────────────────────────────────────────── */

	events_init(&l);
	event_expand(WEEKLY_LONDON, strlen(WEEKLY_LONDON), from, to, "a", "c", &l);
	events_sort(&l);
	ok_("a weekly rule yields its COUNT of occurrences", l.n == 6);

	bool before = false, after = false;
	for (size_t i = 0; i < l.n; i++) {
		if (l.e[i].start == 1792569600) before = true;   /* Wed 21 Oct, 08:00Z */
		if (l.e[i].start == 1793178000) after = true;    /* Wed 28 Oct, 09:00Z */
	}
	/* ⛔ BOTH, OR THE EXPANSION IS WRONG FOR HALF THE YEAR. 09:00 London is
	 * 08:00 UTC on the 21st and 09:00 UTC on the 28th — the clocks went back on
	 * the 25th. An expander that carries one offset through gets exactly one of
	 * these right. */
	ok_("…at 08:00 UTC before the clocks go back (09:00 BST)", before);
	ok_("…and at 09:00 UTC after them (09:00 GMT, the same local time)", after);

	ok_("…every instance is marked as coming from a rule",
	    l.n && l.e[0].recurring);
	ok_("…and carries the account and calendar it came from",
	    l.n && l.e[0].account && strcmp(l.e[0].account, "a") == 0 &&
	    l.e[0].calendar && strcmp(l.e[0].calendar, "c") == 0);
	events_free(&l);

	/* ── a range that clips ─────────────────────────────────────────────── */

	events_init(&l);
	/* Just the fortnight around the change. */
	event_expand(WEEKLY_LONDON, strlen(WEEKLY_LONDON), 1792310400, 1793520000, "a", "c", &l);
	ok_("a narrower range yields fewer occurrences", l.n > 0 && l.n < 6);
	events_free(&l);

	/* ── EXDATE ─────────────────────────────────────────────────────────── */

	events_init(&l);
	event_expand(WITH_EXDATE, strlen(WITH_EXDATE), from, to, "a", "c", &l);
	ok_("an EXDATE removes exactly one of four", l.n == 3);
	bool has_excluded = false;
	for (size_t i = 0; i < l.n; i++)
		if (l.e[i].start == 1788861600) has_excluded = true;   /* 2026-09-08T10:00Z */
	ok_("…and it is the one that was named", !has_excluded);
	events_free(&l);

	/* ── an override ────────────────────────────────────────────────────── */

	events_init(&l);
	event_expand(WITH_OVERRIDE, strlen(WITH_OVERRIDE), from, to, "a", "c", &l);
	ok_("a master plus an override still yields COUNT occurrences", l.n == 3);

	/* ⛔ ONCE, AT THE NEW TIME. Expanding the master and appending the override
	 * shows the moved meeting twice — at the time it is not, and the time it
	 * is — which is worse than showing neither. */
	ok_("…the moved occurrence appears exactly once", count_summary(&l, "Daily (moved)") == 1);
	ok_("…and the one it replaced is gone", count_summary(&l, "Daily") == 2);

	bool at_new = false, at_old = false;
	for (size_t i = 0; i < l.n; i++) {
		if (l.e[i].start == 1788879600) at_new = true;   /* 2026-09-08T15:00Z */
		if (l.e[i].start == 1788861600) at_old = true;   /* 2026-09-08T10:00Z */
	}
	ok_("…at the time the override gives", at_new);
	ok_("…and not at the time the rule would have", !at_old);
	events_free(&l);

	/* ── all day ────────────────────────────────────────────────────────── */

	events_init(&l);
	event_expand(ALL_DAY, strlen(ALL_DAY), from, to, "a", "c", &l);
	ok_("an all-day event is found", l.n == 1);
	ok_("…and marked as all-day, so a front end does not print 01:00",
	    l.n == 1 && l.e[0].all_day);
	ok_("…and is not marked as recurring", l.n == 1 && !l.e[0].recurring);
	events_free(&l);

	/* ── rubbish in ─────────────────────────────────────────────────────── */

	events_init(&l);
	const char *junk = "this is not a calendar at all";
	event_expand(junk, strlen(junk), from, to, "a", "c", &l);
	ok_("a file that is not iCalendar yields nothing, and does not crash", l.n == 0);
	events_free(&l);

	events_init(&l);
	const char *empty = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nEND:VCALENDAR\r\n";
	event_expand(empty, strlen(empty), from, to, "a", "c", &l);
	ok_("an empty VCALENDAR yields nothing", l.n == 0);
	events_free(&l);

	printf("\n%d/%d passed\n", total - fails, total);
	return fails ? 1 : 0;
}
