/* compose_test.c — the half that writes.
 *
 * ⛔ WHAT THIS FILE IS GUARDING IS SOMEBODY ELSE'S COPY. An event written wrong
 * does not fail here — it syncs, and the mistake is then on a server, in other
 * people's clients, in a colleague's week. Every case below is something that
 * looked right in the one zone and the one title it was written with.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "compose.h"
#include "event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0, total = 0;
static void ok_(const char *what, bool cond)
{
	total++;
	if (cond) printf("  ok    %s\n", what);
	else { printf("  FAIL  %s\n", what); fails++; }
}

static time_t at(int y, int mo, int d, int h, int mi)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
	t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
	return mktime(&t);
}

static void test_parse_when(void)
{
	time_t t;
	bool all_day = false;

	ok_("a date and time parses", parse_when("2026-09-21 13:15", &t, &all_day) &&
	    t == at(2026, 9, 21, 13, 15) && !all_day);

	ok_("…and so does the same one written with a T", parse_when("2026-09-21T13:15", &t, &all_day) &&
	    t == at(2026, 9, 21, 13, 15));

	/* ⚠ 1:15pm IS WHAT PEOPLE TYPE, and 13:15 is what a spec would ask for. */
	ok_("1:15pm is 13:15", parse_when("2026-09-21 1:15pm", &t, &all_day) &&
	    t == at(2026, 9, 21, 13, 15));
	ok_("…and 1:15am is 01:15", parse_when("2026-09-21 1:15am", &t, &all_day) &&
	    t == at(2026, 9, 21, 1, 15));
	/* ⛔ THE TWO EVERY 12-HOUR READER GETS WRONG. */
	ok_("12:30am is half past midnight", parse_when("2026-09-21 12:30am", &t, &all_day) &&
	    t == at(2026, 9, 21, 0, 30));
	ok_("…and 12:30pm is half past noon", parse_when("2026-09-21 12:30pm", &t, &all_day) &&
	    t == at(2026, 9, 21, 12, 30));
	ok_("an hour with no minutes works", parse_when("2026-09-21 9am", &t, &all_day) &&
	    t == at(2026, 9, 21, 9, 0));

	ok_("a bare date is an all-day event", parse_when("2026-09-21", &t, &all_day) &&
	    all_day && t == at(2026, 9, 21, 0, 0));

	ok_("nonsense is refused", !parse_when("next tuesday", &t, &all_day) &&
	    !parse_when("", &t, &all_day) && !parse_when(NULL, &t, &all_day));
	ok_("…and so is an impossible time", !parse_when("2026-09-21 25:00", &t, &all_day) &&
	    !parse_when("2026-13-01 10:00", &t, &all_day));
}

static void test_parse_spans(void)
{
	long s;
	ok_("30m is half an hour", parse_duration("30m", &s) && s == 1800);
	ok_("2h is two", parse_duration("2h", &s) && s == 7200);
	ok_("1h30m adds up", parse_duration("1h30m", &s) && s == 5400);
	ok_("3d is three days", parse_duration("3d", &s) && s == 3 * 86400);
	/* ⛔ ZERO IS NOT A SHORT EVENT. Most clients draw nothing for one. */
	ok_("zero is refused", !parse_duration("0", &s) && !parse_duration("0m", &s));
	ok_("…and so is a word", !parse_duration("ages", &s) && !parse_duration("", &s));

	int m;
	ok_("a reminder in minutes", parse_reminder("15m", &m) && m == 15);
	ok_("…in hours", parse_reminder("2h", &m) && m == 120);
	/* ⚠ A BARE NUMBER IS MINUTES, because --remind 15 is what gets typed. */
	ok_("…and a bare number means minutes", parse_reminder("15", &m) && m == 15);
	/* ⛔ UNLIKE A DURATION, ZERO IS MEANINGFUL HERE: it means no reminder. */
	ok_("'none' and 0 mean no reminder", parse_reminder("none", &m) && m == 0 &&
	    parse_reminder("0", &m) && m == 0);
	ok_("…and nonsense is still refused", !parse_reminder("soon", &m));
}

static bool has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void test_compose(void)
{
	draft_t d;
	memset(&d, 0, sizeof d);
	d.summary = "Lunch, then dentist";
	d.location = "Clinic; room 2";
	d.notes = "Bring the \\ form";
	d.start = at(2026, 9, 21, 13, 15);
	d.end = at(2026, 9, 21, 14, 0);
	d.remind_min = 15;

	size_t len = 0;
	char *ics = ics_compose(&d, &len);

	ok_("it is a calendar with one event",
	    has(ics, "BEGIN:VCALENDAR") && has(ics, "BEGIN:VEVENT") && has(ics, "END:VCALENDAR"));
	ok_("…with a uid", has(ics, "UID:"));

	/* ⛔ THE ESCAPES, WHICH ARE THE WHOLE REASON THIS IS NOT sprintf. A comma is
	 * a value separator; a title carrying one comes back truncated. */
	ok_("a comma in the title is escaped", has(ics, "SUMMARY:Lunch\\, then dentist"));
	ok_("…a semicolon in the location too", has(ics, "LOCATION:Clinic\\; room 2"));
	ok_("…and a backslash is doubled", has(ics, "DESCRIPTION:Bring the \\\\ form"));

	ok_("the reminder is before the event, not after", has(ics, "TRIGGER:-PT15M"));
	ok_("…and it is an alarm a client will show", has(ics, "BEGIN:VALARM") &&
	    has(ics, "ACTION:DISPLAY"));

	/* ⚠ CRLF, because RFC 5545 says so and some servers enforce it. */
	ok_("lines end CRLF", has(ics, "\r\n") && !has(ics, "\n\n"));

	/* ⚠ AND THE TIME WENT OUT AS AN INSTANT. A local time with no VTIMEZONE
	 * beside it is a different appointment on every machine that reads it. */
	ok_("a timed event goes out in UTC", has(ics, "DTSTART:") && has(ics, "Z\r\n") &&
	    !has(ics, "DTSTART;VALUE=DATE"));
	free(ics);

	/* An all-day event is the opposite: a date, and never an instant. */
	memset(&d, 0, sizeof d);
	d.summary = "Birthday";
	d.start = at(2026, 9, 22, 0, 0);
	d.end = at(2026, 9, 23, 0, 0);
	d.all_day = true;
	ics = ics_compose(&d, &len);
	ok_("an all-day event is a DATE", has(ics, "DTSTART;VALUE=DATE:20260922"));
	/* ⛔ AND ITS END IS THE DAY AFTER. An all-day event ending on its own date
	 * is zero days long and most clients draw nothing at all. */
	ok_("…ending the day after, because the end is exclusive",
	    has(ics, "DTEND;VALUE=DATE:20260923"));
	ok_("…and carries no UTC stamp for its times", !has(ics, "DTSTART:2026"));
	free(ics);
}

/* Written, then read back by the same code the agenda uses. A composer that
 * only satisfies its own assertions has proved nothing. */
static void test_round_trip(void)
{
	draft_t d;
	memset(&d, 0, sizeof d);
	d.summary = "Lunch, then dentist";
	d.location = "Clinic; room 2";
	d.start = at(2026, 9, 21, 13, 15);
	d.end = at(2026, 9, 21, 14, 0);

	size_t len = 0;
	char *ics = ics_compose(&d, &len);

	events_t l;
	events_init(&l);
	bool got = event_expand(ics, len, at(2026, 9, 20, 0, 0), at(2026, 9, 23, 0, 0),
	                        "acct", "cal", &l);
	ok_("what was written reads back", got && l.n == 1);
	if (l.n == 1) {
		ok_("…at the time it was given", l.e[0].start == at(2026, 9, 21, 13, 15));
		/* ⛔ AND THE ESCAPES CAME BACK OFF. If they did not, the title on screen
		 * would carry the backslashes the file needs and the person never typed. */
		ok_("…with the comma back and no backslash",
		    l.e[0].summary && !strcmp(l.e[0].summary, "Lunch, then dentist"));
		ok_("…and the semicolon too",
		    l.e[0].location && !strcmp(l.e[0].location, "Clinic; room 2"));
		ok_("…and it is not marked as repeating", !l.e[0].recurring);
	}
	events_free(&l);
	free(ics);

	/* ⛔ THE ALL-DAY CASE, WHICH IS THE ONE THAT WAS WRONG. libical resolves a
	 * VALUE=DATE to midnight UTC and every view here formats with localtime_r,
	 * so west of the meridian a birthday on the 22nd drew on the 21st — in the
	 * agenda, the month grid and the terminal at once. */
	memset(&d, 0, sizeof d);
	d.summary = "Birthday";
	d.start = at(2026, 9, 22, 0, 0);
	d.end = at(2026, 9, 23, 0, 0);
	d.all_day = true;
	ics = ics_compose(&d, &len);

	events_init(&l);
	event_expand(ics, len, at(2026, 9, 1, 0, 0), at(2026, 10, 1, 0, 0), "a", "c", &l);
	ok_("an all-day event reads back", l.n == 1);
	if (l.n == 1) {
		ok_("…marked as all day", l.e[0].all_day);
		struct tm lt;
		localtime_r(&l.e[0].start, &lt);
		ok_("…on the date it was written, in local time, not the day before",
		    lt.tm_year + 1900 == 2026 && lt.tm_mon == 8 && lt.tm_mday == 22);
		ok_("…starting at local midnight", lt.tm_hour == 0 && lt.tm_min == 0);
	}
	events_free(&l);
	free(ics);
}

int main(void)
{
	/* ⚠ A ZONE WEST OF UTC, DELIBERATELY. The all-day bug above is invisible at
	 * UTC and invisible east of it: the date only slips backwards where the
	 * local day starts after the UTC one. A suite that ran in London would have
	 * passed throughout. */
	setenv("TZ", "America/Chicago", 1);
	tzset();

	printf("compose\n");
	test_parse_when();
	test_parse_spans();
	test_compose();
	test_round_trip();

	printf("%d/%d passed\n", total - fails, total);
	return fails ? 1 : 0;
}
