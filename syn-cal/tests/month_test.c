/* month_test.c — the grid three front ends draw from.
 *
 * ⛔ THIS IS THE ARITHMETIC THAT USED TO BE COPIED. The month view exists in the
 * terminal, on the command line and in the window; the day it exists three
 * times is the day a leap year is fixed in one of them. What follows is the
 * whole of what the grid promises, so the copy that drifts fails here first.
 *
 * ⚠ THE CASES ARE CHOSEN, NOT SPRINKLED. Twenty-eight days fit in four rows
 * only when the month opens on the first column, and need five otherwise — so
 * February 2026 is four rows Sunday-first and five Monday-first, and the same
 * twenty-eight days answer differently depending on a setting. A `days / 7` is
 * wrong for both.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "month.h"

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

static void test_lengths(void)
{
	ok_("January has 31", month_days_in(2026, 0) == 31);
	ok_("April has 30", month_days_in(2026, 3) == 30);
	ok_("February 2026 has 28", month_days_in(2026, 1) == 28);
	ok_("…2028 has 29, being a leap year", month_days_in(2028, 1) == 29);
	/* ⛔ THE TWO EVERY WRONG LEAP RULE GETS WRONG. A `% 4` alone makes 2100 a
	 * leap year; a `% 4 && % 100` alone makes 2000 an ordinary one. */
	ok_("…2100 has 28: divisible by 100", month_days_in(2100, 1) == 28);
	ok_("…2000 had 29: divisible by 400", month_days_in(2000, 1) == 29);
}

/* ⛔ THE WHOLE GRID MOVES WITH THE SETTING. Every assertion about a column is an
 * assertion about which day the week starts on, so the ones below say which. */
static void test_week_start(void)
{
	month_t sun, mon;
	/* 1 August 2026 is a Saturday: the last column of a Sunday-first grid and
	 * the second-to-last of a Monday-first one. */
	ok_("August 2026 opens in column 6 when weeks start on Sunday",
	    month_load_week(&sun, 2026, 7, WEEK_SUNDAY) && sun.first == 6);
	ok_("…and in column 5 when they start on Monday",
	    month_load_week(&mon, 2026, 7, WEEK_MONDAY) && mon.first == 5);

	ok_("…and thirty-one days need six rows either way", sun.rows == 6 && mon.rows == 6);

	month_load_week(&sun, 2026, 1, WEEK_SUNDAY);   /* February 2026: a Sunday */
	month_load_week(&mon, 2026, 1, WEEK_MONDAY);
	ok_("February 2026 opens column 0 Sunday-first and 6 Monday-first",
	    sun.first == 0 && mon.first == 6);
	ok_("…so the same 28 days are four rows one way and five the other",
	    sun.rows == 4 && mon.rows == 5);

	/* A column is not a weekday, and this is the mapping that says so. */
	ok_("column 0 is Sunday when weeks start on Sunday",
	    month_dow_of_col(&sun, 0) == 0 && month_dow_of_col(&sun, 6) == 6);
	ok_("…and Monday when they start on Monday",
	    month_dow_of_col(&mon, 0) == 1 && month_dow_of_col(&mon, 6) == 0);

	week_start_t ws;
	ok_("'sun' and 'SUNDAY' both parse", week_start_parse("sun", &ws) && ws == WEEK_SUNDAY &&
	    week_start_parse("SUNDAY", &ws) && ws == WEEK_SUNDAY);
	ok_("'mon' and 'Monday' both parse", week_start_parse("mon", &ws) && ws == WEEK_MONDAY &&
	    week_start_parse("Monday", &ws) && ws == WEEK_MONDAY);
	/* ⛔ AND A TYPO IS REFUSED, NOT ROUNDED TO A DEFAULT. Silently picking one
	 * is how somebody's setting goes missing without a word. */
	ok_("…and anything else is refused", !week_start_parse("tuesday", &ws) &&
	    !week_start_parse("", &ws) && !week_start_parse(NULL, &ws));
}

static void test_rows(void)
{
	month_t m;

	ok_("February 2027 opens on a Monday",
	    month_load_week(&m, 2027, 1, WEEK_MONDAY) && m.first == 0);
	ok_("…so twenty-eight days need four rows", m.rows == 4);

	ok_("February 2026 opens on a Sunday",
	    month_load_week(&m, 2026, 1, WEEK_MONDAY) && m.first == 6);
	ok_("…so the same twenty-eight days need five", m.rows == 5);

	ok_("August 2026 opens on a Saturday",
	    month_load_week(&m, 2026, 7, WEEK_MONDAY) && m.first == 5);
	ok_("…and needs six rows", m.rows == 6);

	ok_("May 2027 opens on a Saturday with 31 days",
	    month_load_week(&m, 2027, 4, WEEK_MONDAY) && m.first == 5 && m.days == 31);
	ok_("…which is the widest a month gets: six rows", m.rows == 6);
}

static void test_bounds(void)
{
	month_t m;
	ok_("August 2026 loads", month_load(&m, 2026, 7));

	/* ⛔ THE END IS THE 1ST OF THE NEXT MONTH, not the start plus 31 days.
	 * Over-reading is how a September event turned up in August's grid. */
	month_t sep;
	ok_("…and ends exactly where September begins",
	    month_load(&sep, 2026, 8) && m.end == sep.start);

	ok_("the 1st starts where the month starts", month_day_start(&m, 1) == m.start);
	ok_("…and the last day is inside it", month_day_start(&m, m.days) < m.end);

	/* A month is not 30 * 86400 seconds even when it has 30 days: a clock
	 * change makes one of them 23 or 25 hours long. Asserting only that the
	 * days land in order keeps this true in every zone. */
	bool ordered = true;
	for (int d = 2; d <= m.days; d++)
		if (month_day_start(&m, d) <= month_day_start(&m, d - 1)) ordered = false;
	ok_("…and every day starts after the one before it", ordered);
}

static void test_stepping(void)
{
	int y = 2026, mo = 11;
	month_step(&y, &mo, +1);
	ok_("December steps into January of the next year", y == 2027 && mo == 0);

	month_step(&y, &mo, -1);
	ok_("…and back again", y == 2026 && mo == 11);

	y = 2026; mo = 0;
	month_step(&y, &mo, -14);
	ok_("stepping back more than a year lands right", y == 2024 && mo == 10);

	/* month_load normalises too, so a caller may hand it an out-of-range month
	 * rather than doing this itself. */
	month_t m;
	ok_("a month of 12 is January of the next year",
	    month_load(&m, 2026, 12) && m.year == 2027 && m.mon == 0);
	ok_("…and -1 is the December before",
	    month_load(&m, 2026, -1) && m.year == 2025 && m.mon == 11);
}

static void test_stepping_days(void)
{
	int y = 2026, mo = 7, sel = 31;      /* 31 August */
	month_step_day(&y, &mo, &sel, +1);
	ok_("the day after the 31st of August is the 1st of September",
	    y == 2026 && mo == 8 && sel == 1);

	month_step_day(&y, &mo, &sel, -1);
	ok_("…and stepping back returns to it", y == 2026 && mo == 7 && sel == 31);

	y = 2026; mo = 0; sel = 1;
	month_step_day(&y, &mo, &sel, -1);
	ok_("the day before the 1st of January is New Year's Eve",
	    y == 2025 && mo == 11 && sel == 31);

	/* A week back from the 3rd crosses a month boundary AND a short month. */
	y = 2026; mo = 2; sel = 3;           /* 3 March 2026 */
	month_step_day(&y, &mo, &sel, -7);
	ok_("a week before the 3rd of March is the 24th of February",
	    y == 2026 && mo == 1 && sel == 24);

	/* Far enough to cross a year in one go, which the while-loops must survive. */
	y = 2026; mo = 5; sel = 15;
	month_step_day(&y, &mo, &sel, +365);
	ok_("a year of days from 15 June 2026 is 15 June 2027",
	    y == 2027 && mo == 5 && sel == 15);
}

/* An events_t built by hand: month_counts only reads `start`, and going through
 * the store would be testing the store. */
static void add_at(events_t *l, int year, int mon, int day, int hour)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = year - 1900;
	t.tm_mon = mon;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_isdst = -1;
	event_t e;
	memset(&e, 0, sizeof e);
	e.start = mktime(&t);
	e.end = e.start + 3600;
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 8;
		l->e = realloc(l->e, l->cap * sizeof *l->e);
	}
	l->e[l->n++] = e;
}

static void test_counts(void)
{
	month_t m;
	month_load(&m, 2026, 7);             /* August 2026 */

	events_t l;
	events_init(&l);
	add_at(&l, 2026, 7, 29, 10);
	add_at(&l, 2026, 7, 29, 14);         /* same day, twice */
	add_at(&l, 2026, 7, 31, 9);
	add_at(&l, 2026, 8, 1, 9);           /* September: not this month */
	add_at(&l, 2026, 6, 31, 9);          /* July: nor this one */

	int c[32];
	month_counts(&m, &l, c);

	ok_("a day with two events counts two", c[29] == 2);
	ok_("a day with one counts one", c[31] == 1);
	ok_("a day with none counts none", c[30] == 0);
	/* ⛔ THE FILTER IS WHAT MAKES AN OVER-WIDE RANGE SAFE. Every caller loads
	 * more than the month and relies on this to drop the rest. */
	ok_("an event in the next month is not counted", c[1] == 0);
	ok_("…and the first of this month is genuinely empty", c[1] == 0);

	int sum = 0;
	for (int i = 0; i < 32; i++) sum += c[i];
	ok_("…so only the three in August are counted at all", sum == 3);

	free(l.e);
}

int main(void)
{
	/* ⚠ A FIXED ZONE. The grid is local-time arithmetic, so a suite that reads
	 * the builder's zone asserts something different on every machine — and
	 * passes on the one where it was written. */
	setenv("TZ", "Europe/London", 1);
	tzset();

	printf("month\n");
	test_lengths();
	test_week_start();
	test_rows();
	test_bounds();
	test_stepping();
	test_stepping_days();
	test_counts();

	printf("%d/%d passed\n", total - fails, total);
	return fails ? 1 : 0;
}
