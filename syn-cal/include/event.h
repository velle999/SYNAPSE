/* event.h — what an event is once somebody has to look at it.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_EVENT_H
#define SYNCAL_EVENT_H

#include "account.h"

typedef struct {
	char *uid;
	char *summary;
	char *location;
	char *account;
	char *calendar;
	time_t start;      /* UTC. An all-day event starts at 00:00 in its own day. */
	time_t end;
	bool all_day;
	bool recurring;    /* this instance came out of an RRULE */
	bool cancelled;
} event_t;

typedef struct { event_t *e; size_t n, cap; } events_t;

void events_init(events_t *l);
void events_free(events_t *l);
void events_sort(events_t *l);

/* Every occurrence between `from` and `to`, out of one .ics. */
bool event_expand(const char *ics, size_t len, time_t from, time_t to,
                  const char *account, const char *calendar, events_t *out);

/* …and out of every enabled calendar of every account. */
bool agenda_range(time_t from, time_t to, events_t *out, char **err);

/* tui.c */
int cmd_tui(void);

#endif /* SYNCAL_EVENT_H */
