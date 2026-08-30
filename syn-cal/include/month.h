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
 * ⚠ A COLUMN IS NOT A WEEKDAY. `first` and `col` are offsets from whichever day
 * the week starts on, which is a setting — so a Sunday is column 0 in one
 * configuration and column 6 in the next. Nothing may assume either: the record
 * carries `dow` (tm_wday, 0 = Sunday) alongside the column precisely so a front
 * end can label its headings without deciding the question a second time.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_MONTH_H
#define SYNCAL_MONTH_H

#include "event.h"

/* Which day a week is drawn from. Sunday is the default because it is what most
 * of the calendars people arrive from use; ISO 8601 and most of Europe start on
 * Monday, so it is a setting rather than a decision. */
typedef enum { WEEK_SUNDAY = 0, WEEK_MONDAY = 1 } week_start_t;

typedef struct {
	int year;
	int mon;        /* 0-11, as in struct tm */
	int days;       /* 28-31 */
	int first;      /* column the 1st falls in, 0-6, from `week_start` */
	int rows;       /* grid rows this month needs, 4-6 */
	int week_start; /* week_start_t: the day column 0 means */
	time_t start;   /* local midnight on the 1st */
	time_t end;     /* local midnight on the 1st of the next month */
} month_t;

/* The stored preference, WEEK_SUNDAY when nothing has been set. */
week_start_t week_start_get(void);
bool week_start_set(week_start_t ws, char **err);

/* "sun"/"sunday" and "mon"/"monday", case-insensitively. False on anything
 * else, so a caller reports the typo rather than silently picking one. */
bool week_start_parse(const char *s, week_start_t *out);
const char *week_start_name(week_start_t ws);

/* tm_wday (0 = Sunday) of a column in this month's grid. */
int month_dow_of_col(const month_t *m, int col);

/* Resolve a month against this machine's calendar. False only when the date is
 * one the machine cannot represent. `mon` is 0-11 and may be out of range: it
 * is normalised into `year` first, so callers can add and subtract freely. */
bool month_load(month_t *m, int year, int mon);

/* …starting the week where you say, rather than where the setting does. */
bool month_load_week(month_t *m, int year, int mon, week_start_t ws);

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

/* `syn-cal weekstart [sun|mon]` — prints it, or sets it. */
int cmd_weekstart(const char *value);

#endif /* SYNCAL_MONTH_H */
