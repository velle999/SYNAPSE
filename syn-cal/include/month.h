/* month.h — the shape of a month, for the three front ends that draw one.
 *
 * ⛔ ONE OWNER FOR THE GRID. Which weekday a month opens on, how many days it
 * holds and therefore how many rows it needs is the same arithmetic in the
 * terminal, on the command line and in the window. It lived inside tui.c until
 * a second view needed it; copying it would have left three answers to a
 * question with one, and a leap-year or first-weekday fix landing in one of
 * them. The window does not compute it either — `syn-cal --rec month` hands it
 * the finished grid, one record per day, so QML places cells and never decides
 * which cell a day belongs in.
 *
 * ⚠ WEEKDAYS ARE 0 = MONDAY here, not tm_wday's 0 = Sunday. A calendar people
 * read starts on Monday, and converting once at the edge is safer than every
 * caller remembering which convention it is holding.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_MONTH_H
#define SYNCAL_MONTH_H

#include "event.h"

typedef struct {
	int year;
	int mon;        /* 0-11, as in struct tm */
	int days;       /* 28-31 */
	int first;      /* weekday of the 1st, 0 = Monday */
	int rows;       /* grid rows this month needs, 4-6 */
	time_t start;   /* local midnight on the 1st */
	time_t end;     /* local midnight on the 1st of the next month */
} month_t;

/* Resolve a month against this machine's calendar. False only when the date is
 * one the machine cannot represent. `mon` is 0-11 and may be out of range: it
 * is normalised into `year` first, so callers can add and subtract freely. */
bool month_load(month_t *m, int year, int mon);

int  month_days_in(int year, int mon);

/* Move by whole months, keeping year and month coherent. */
void month_step(int *year, int *mon, int delta);

/* Move a selected day by `delta` days, rolling into the neighbouring months. */
void month_step_day(int *year, int *mon, int *sel, int delta);

/* Local midnight on `day` of this month. Via mktime, so a day that a clock
 * change makes 23 or 25 hours long still starts where it really starts. */
time_t month_day_start(const month_t *m, int day);

/* How many of `ev` fall on each day. `counts` is indexed by day-of-month, so it
 * wants 32 entries and entry 0 is unused. Events outside the month are ignored,
 * which is what makes it safe to pass the over-wide range the callers load. */
void month_counts(const month_t *m, const events_t *ev, int *counts);

/* `syn-cal month [--from YYYY-MM[-DD]]` */
int cmd_month(const char *from_date);

#endif /* SYNCAL_MONTH_H */
