/* compose.h — making an event, rather than only reading one.
 *
 * ⛔ THIS IS THE HALF THAT WAS MISSING. Everything else in syn-cal reads: it
 * syncs, expands, groups and draws, and there was no way to put an appointment
 * in — from the window or the command line — which is most of the point of a
 * calendar. Nothing here talks to a server: an event is written into the vdir
 * and the sync engine already knows how to push a local file up, which is the
 * same path a file dropped in by hand takes.
 *
 * ⚠ TIMES GO OUT IN UTC, deliberately. Writing a local time needs a VTIMEZONE
 * beside it — a whole DST ruleset, per event, and getting it wrong is wrong for
 * half the year and right whenever anybody checks it in July (see event_test).
 * A UTC instant has no such ambiguity, every server and client reads it, and
 * this machine's own zone is what turned the typed "13:15" into one. An all-day
 * event is the exception and must NOT be UTC: it is a date, and a date given an
 * instant lands on the day before for anybody west of the meridian.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_COMPOSE_H
#define SYNCAL_COMPOSE_H

#include "syncal.h"

typedef struct {
	const char *uid;       /* NULL to invent one */
	const char *summary;
	const char *location;  /* NULL or "" for none */
	const char *notes;
	time_t start;
	time_t end;
	bool all_day;
	int remind_min;        /* minutes before; 0 for no reminder */
	int sequence;          /* bumped on every edit, as the spec asks */
} draft_t;

/* A whole VCALENDAR holding one VEVENT. Caller frees. */
char *ics_compose(const draft_t *d, size_t *out_len);

/* "2026-09-21 13:15", "2026-09-21 1:15pm" or "2026-09-21" — the last of which
 * sets *all_day. Local time, because that is what a person types. */
bool parse_when(const char *s, time_t *out, bool *all_day);

/* "45m", "2h", "1h30m", "3d". Zero and negative are refused: an event that ends
 * before it starts is not a shorter event. */
bool parse_duration(const char *s, long *seconds);

/* Minutes before the start, from the same spelling as a duration. "0" and
 * "none" mean no reminder, which is why this is separate from parse_duration. */
bool parse_reminder(const char *s, int *minutes);

int cmd_new(int argc, char **argv);
int cmd_edit(int argc, char **argv);
int cmd_delete(const char *uid);

#endif /* SYNCAL_COMPOSE_H */
