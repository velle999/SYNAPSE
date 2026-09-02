/* month.c — the month grid, and the command that prints one.
 *
 * See month.h for why this is not three copies of the same arithmetic.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "month.h"
#include "settings.h"
#include "syncal.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <time.h>

/* ── which day the week starts on ───────────────────────────────────────── */

/* ⛔ ONE LINE, ONE KEY, AND ITS OWN FILE. Not accounts.conf: that file is a list
 * of accounts and every reader of it walks sections. A preference that is not
 * about any account has no section to live in, and inventing one would mean an
 * account could be called [settings]. */
#define WEEK_KEY "week_start"

bool week_start_parse(const char *s, week_start_t *out)
{
	if (!s || !*s) return false;
	if (!strcasecmp(s, "sun") || !strcasecmp(s, "sunday")) { *out = WEEK_SUNDAY; return true; }
	if (!strcasecmp(s, "mon") || !strcasecmp(s, "monday")) { *out = WEEK_MONDAY; return true; }
	return false;
}

const char *week_start_name(week_start_t ws)
{
	return ws == WEEK_MONDAY ? "monday" : "sunday";
}

week_start_t week_start_get(void)
{
	char *v = settings_get(WEEK_KEY);
	week_start_t ws = WEEK_SUNDAY;
	if (v) { week_start_parse(v, &ws); free(v); }
	return ws;
}

bool week_start_set(week_start_t ws, char **err)
{
	return settings_set(WEEK_KEY, week_start_name(ws), err);
}

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

/* tm_wday of the 1st, 0 = Sunday. Via mktime rather than by hand: it knows about
 * the calendar's own history, and this is not a place to be clever. */
static int first_wday(int year, int mon)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = year - 1900;
	t.tm_mon = mon;
	t.tm_mday = 1;
	t.tm_hour = 12;              /* midday: no clock change can move the day */
	t.tm_isdst = -1;
	if (mktime(&t) == (time_t)-1) return 0;
	return t.tm_wday;
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
	return month_load_week(m, year, mon, week_start_get());
}

int month_dow_of_col(const month_t *m, int col)
{
	return (m->week_start + col) % 7;
}

bool month_load_week(month_t *m, int year, int mon, week_start_t ws)
{
	month_step(&year, &mon, 0);          /* normalise before anything reads it */
	memset(m, 0, sizeof *m);
	m->year = year;
	m->mon = mon;
	m->week_start = ws;
	m->days = month_days_in(year, mon);
	/* ⚠ THE COLUMN, NOT THE WEEKDAY. Sunday is column 0 of a Sunday-first grid
	 * and column 6 of a Monday-first one; everything below counts columns. */
	m->first = (first_wday(year, mon) - (int)ws + 7) % 7;
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
		warn(_("--from wants a month like 2026-09, or a date in it"));
		return false;
	}
	*year = y;
	*mon = m - 1;
	return true;
}

int cmd_weekstart(const char *value)
{
	if (!value) {
		if (g_out == OUT_REC) {
			rec_header("week_start");
			rec_row("%s", week_start_name(week_start_get()));
		} else {
			printf("%s\n", week_start_name(week_start_get()));
		}
		return 0;
	}

	week_start_t ws;
	if (!week_start_parse(value, &ws)) {
		warn(_("weekstart wants 'sun' or 'mon', not '%s'"), value);
		return 2;
	}
	char *err = NULL;
	if (!week_start_set(ws, &err)) {
		warn("%s", err ? err : _("could not write the setting"));
		free(err);
		return 1;
	}
	if (g_out != OUT_REC)
		printf("Weeks start on %s.\n", ws == WEEK_MONDAY ? "Monday" : "Sunday");
	return 0;
}

int cmd_month(const char *from_date)
{
	int year, mon;
	if (!month_from(from_date, &year, &mon)) return 2;

	month_t m;
	if (!month_load(&m, year, mon)) {
		warn(_("that is not a month this machine can represent"));
		return 2;
	}

	events_t ev;
	events_init(&ev);
	char *err = NULL;
	if (!agenda_range(m.start, m.end, &ev, &err)) {
		warn("%s", err ? err : _("could not read the calendars"));
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
		/* ⚠ AND `dow` BESIDE `col`, BECAUSE THEY ARE NOT THE SAME THING.
		 * Which column a Sunday is depends on where the week starts, so a
		 * front end labelling its headings Mon..Sun from the column number
		 * would be right for one setting and silently wrong for the other.
		 * It reads the weekday off the record instead. */
		rec_header("date\tstart\trow\tcol\tdow\ttoday\tcount");
		for (int day = 1; day <= m.days; day++) {
			int cell = m.first + day - 1;
			rec_row("%04d-%02d-%02d\t%ld\t%d\t%d\t%d\t%d\t%d",
			        m.year, m.mon + 1, day, (long)month_day_start(&m, day),
			        cell / 7, cell % 7, month_dow_of_col(&m, cell % 7),
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
	/* ⚠ HEADED FROM THE SETTING, NOT FROM A LITERAL. A grid drawn
	 * Sunday-first under a heading that starts at Monday is off by one all
	 * month, and reads as the grid being wrong rather than the heading. */
	static const char *const abbr[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
	printf("  %s", D);
	for (int c = 0; c < 7; c++) printf("%s%s", c ? " " : "", abbr[month_dow_of_col(&m, c)]);
	printf("%s\n", R);
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

	if (total == 0) {
		/* ⚠ THE MONTH'S NAME IS INSIDE THE SENTENCE, not concatenated onto
		 * the end of it: a translator needs to put it where their language
		 * puts it, and a bare "nothing on in" is not a phrase anyone can
		 * translate. Split across three printfs so the colour escapes stay
		 * out of the msgid.  */
		printf("  %s", D);
		printf(_("nothing on in %s"), title);
		printf("%s\n\n", R);
	} else
		printf(P_("  %s%d event — syn-cal agenda --from %04d-%02d-01 --days %d%s\n\n",
		          "  %s%d events — syn-cal agenda --from %04d-%02d-01 --days %d%s\n\n",
		          total),
		       D, total, m.year, m.mon + 1, m.days, R);

	events_free(&ev);
	return 0;
}
