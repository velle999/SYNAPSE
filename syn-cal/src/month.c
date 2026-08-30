/* month.c — the month grid, and the command that prints one.
 *
 * See month.h for why this is not three copies of the same arithmetic.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "month.h"
#include "syncal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── the grid ───────────────────────────────────────────────────────────── */

int month_days_in(int year, int mon)
{
	static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (mon < 0 || mon > 11) return 0;
	if (mon != 1) return d[mon];
	bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	return leap ? 29 : 28;
}

void month_step(int *year, int *mon, int delta)
{
	*mon += delta;
	while (*mon < 0)  { *mon += 12; (*year)--; }
	while (*mon > 11) { *mon -= 12; (*year)++; }
}

/* Weekday of the 1st, 0 = Monday. Via mktime rather than by hand: it knows
 * about the calendar's own history, and this is not a place to be clever. */
static int first_weekday(int year, int mon)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = year - 1900;
	t.tm_mon = mon;
	t.tm_mday = 1;
	t.tm_hour = 12;              /* midday: no clock change can move the day */
	t.tm_isdst = -1;
	if (mktime(&t) == (time_t)-1) return 0;
	return (t.tm_wday + 6) % 7;
}

static time_t day_start(int year, int mon, int day)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = year - 1900;
	t.tm_mon = mon;
	t.tm_mday = day;
	t.tm_isdst = -1;
	return mktime(&t);
}

bool month_load(month_t *m, int year, int mon)
{
	month_step(&year, &mon, 0);          /* normalise before anything reads it */
	memset(m, 0, sizeof *m);
	m->year = year;
	m->mon = mon;
	m->days = month_days_in(year, mon);
	m->first = first_weekday(year, mon);
	/* ⚠ CEILING, NOT days/7. February starting on a Monday is exactly four
	 * rows and every other shape needs the partial one. */
	m->rows = (m->first + m->days + 6) / 7;
	m->start = day_start(year, mon, 1);

	int ny = year, nm = mon;
	month_step(&ny, &nm, +1);
	m->end = day_start(ny, nm, 1);

	return m->start != (time_t)-1 && m->end != (time_t)-1;
}

time_t month_day_start(const month_t *m, int day)
{
	return day_start(m->year, m->mon, day);
}

void month_step_day(int *year, int *mon, int *sel, int delta)
{
	*sel += delta;
	while (*sel < 1) {
		month_step(year, mon, -1);
		*sel += month_days_in(*year, *mon);
	}
	int dim;
	while (*sel > (dim = month_days_in(*year, *mon))) {
		*sel -= dim;
		month_step(year, mon, +1);
	}
}

void month_counts(const month_t *m, const events_t *ev, int *counts)
{
	for (int i = 0; i < 32; i++) counts[i] = 0;
	for (size_t i = 0; i < ev->n; i++) {
		struct tm lt;
		localtime_r(&ev->e[i].start, &lt);
		if (lt.tm_year + 1900 != m->year || lt.tm_mon != m->mon) continue;
		if (lt.tm_mday < 1 || lt.tm_mday > 31) continue;
		counts[lt.tm_mday]++;
	}
}

/* ── the command ────────────────────────────────────────────────────────── */

/* The month a `--from` names, or the one containing today.
 *
 * ⚠ THE DAY IS OPTIONAL AND IGNORED. `--from 2026-09` and `--from 2026-09-14`
 * both mean September, because a month view has no use for the day and
 * refusing the longer form would mean the flag behaves differently here than
 * it does on `agenda`. */
static bool month_from(const char *from_date, int *year, int *mon)
{
	time_t now = time(NULL);
	struct tm lt;
	localtime_r(&now, &lt);
	*year = lt.tm_year + 1900;
	*mon = lt.tm_mon;

	if (!from_date) return true;

	int y = 0, m = 0, d = 0;
	int got = sscanf(from_date, "%d-%d-%d", &y, &m, &d);
	if (got < 2 || m < 1 || m > 12 || y < 1 || y > 9999) {
		warn("--from wants a month like 2026-09, or a date in it");
		return false;
	}
	*year = y;
	*mon = m - 1;
	return true;
}

int cmd_month(const char *from_date)
{
	int year, mon;
	if (!month_from(from_date, &year, &mon)) return 2;

	month_t m;
	if (!month_load(&m, year, mon)) {
		warn("that is not a month this machine can represent");
		return 2;
	}

	events_t ev;
	events_init(&ev);
	char *err = NULL;
	if (!agenda_range(m.start, m.end, &ev, &err)) {
		warn("%s", err ? err : "could not read the calendars");
		free(err);
		events_free(&ev);
		return 1;
	}

	int counts[32];
	month_counts(&m, &ev, counts);

	time_t now = time(NULL);
	struct tm tn;
	localtime_r(&now, &tn);
	int t_y = tn.tm_year + 1900, t_m = tn.tm_mon, t_d = tn.tm_mday;

	if (g_out == OUT_REC) {
		/* ⛔ ONE RECORD PER DAY, CARRYING ITS CELL. The window draws the grid
		 * from this and does no calendar arithmetic of its own — which is the
		 * whole reason `row` and `col` are here rather than left for a front
		 * end to infer from the first day's weekday. */
		rec_header("date\tstart\trow\tcol\ttoday\tcount");
		for (int day = 1; day <= m.days; day++) {
			int cell = m.first + day - 1;
			rec_row("%04d-%02d-%02d\t%ld\t%d\t%d\t%d\t%d",
			        m.year, m.mon + 1, day, (long)month_day_start(&m, day),
			        cell / 7, cell % 7,
			        (m.year == t_y && m.mon == t_m && day == t_d) ? 1 : 0,
			        counts[day]);
		}
		events_free(&ev);
		return 0;
	}

	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = m.year - 1900;
	t.tm_mon = m.mon;
	t.tm_mday = 1;
	char title[64];
	strftime(title, sizeof title, "%B %Y", &t);

	const char *R = g_color ? "\033[0m"  : "";
	const char *D = g_color ? "\033[2m"  : "";
	const char *B = g_color ? "\033[1m"  : "";
	const char *A = g_color ? "\033[36m" : "";

	printf("\n  %s%s%s\n\n", B, title, R);
	printf("  %sMo Tu We Th Fr Sa Su%s\n", D, R);
	printf("  ");
	for (int i = 0; i < m.first; i++) printf("   ");

	int total = 0;
	for (int day = 1; day <= m.days; day++) {
		bool is_today = (m.year == t_y && m.mon == t_m && day == t_d);
		total += counts[day];

		/* ⚠ THE MARK IS PART OF THE TWO-CHARACTER CELL, not an extra column.
		 * A dot appended after the number would push every later day one place
		 * right and the whole grid would stop lining up under its heading. */
		char cell[8];
		snprintf(cell, sizeof cell, "%2d", day);
		if (is_today)          printf("%s%s%s", A, cell, R);
		else if (counts[day])  printf("%s%s%s", B, cell, R);
		else                   printf("%s", cell);

		if (counts[day]) printf("%s·%s", A, R);
		else             putchar(' ');

		if ((m.first + day) % 7 == 0 && day != m.days) printf("\n  ");
	}
	printf("\n\n");

	if (total == 0)
		printf("  %snothing on in %s%s\n\n", D, title, R);
	else
		printf("  %s%d event%s — syn-cal agenda --from %04d-%02d-01 --days %d%s\n\n",
		       D, total, total == 1 ? "" : "s", m.year, m.mon + 1, m.days, R);

	events_free(&ev);
	return 0;
}
