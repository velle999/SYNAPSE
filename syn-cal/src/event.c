/* event.c — recurrence and time zones, which is the whole reason libical is here.
 *
 * ⛔ THIS IS THE PART NOT TO WRITE BY HAND. An RRULE can say "the last weekday
 * of every second month, except these four dates, until a count is reached",
 * and a DTSTART carries a TZID whose UTC offset changes twice a year — so a
 * weekly 09:00 meeting is 08:00 UTC for half the year and 09:00 for the other
 * half, and an expander that treats the first occurrence's offset as the rule's
 * offset is wrong for six months at a time and right when anybody checks it in
 * summer. libical has the tz database and thirty years of other people's bugs
 * fixed in it.
 *
 * ── Overrides ───────────────────────────────────────────────────────────────
 *
 * A recurring event that has had one occurrence moved is stored as TWO VEVENTs
 * with the same UID: the master with its RRULE, and an override carrying a
 * RECURRENCE-ID naming which occurrence it replaces. libical's
 * icalcomponent_foreach_recurrence expands the master and knows nothing about
 * the override, so an expander that stops there shows the moved meeting at its
 * old time AND at its new one — which is worse than showing neither.
 *
 * So the master is expanded, each generated instance is looked up against the
 * overrides by its RECURRENCE-ID, and any override that falls in range without
 * having replaced a generated instance is added on its own.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "event.h"
#include "store.h"

#include <libical/ical.h>
#include <stdlib.h>
#include <string.h>

void events_init(events_t *l) { l->e = NULL; l->n = l->cap = 0; }

void events_free(events_t *l)
{
	for (size_t i = 0; i < l->n; i++) {
		free(l->e[i].uid); free(l->e[i].summary); free(l->e[i].location);
		free(l->e[i].account); free(l->e[i].calendar);
	}
	free(l->e);
	events_init(l);
}

static event_t *events_add(events_t *l)
{
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 32;
		l->e = xrealloc(l->e, l->cap * sizeof *l->e);
	}
	memset(&l->e[l->n], 0, sizeof l->e[l->n]);
	return &l->e[l->n++];
}

static int by_start(const void *a, const void *b)
{
	const event_t *x = a, *y = b;
	if (x->start != y->start) return x->start < y->start ? -1 : 1;
	/* A stable second key, so two events at the same minute do not swap places
	 * between runs and make a diff of the agenda meaningless. */
	return strcmp(x->summary ? x->summary : "", y->summary ? y->summary : "");
}

void events_sort(events_t *l) { if (l->n) qsort(l->e, l->n, sizeof *l->e, by_start); }

/* ── one file ───────────────────────────────────────────────────────────── */

static char *dup_or_null(const char *s) { return s ? xstrdup(s) : NULL; }

static bool is_cancelled(icalcomponent *c)
{
	icalproperty *p = icalcomponent_get_first_property(c, ICAL_STATUS_PROPERTY);
	return p && icalproperty_get_status(p) == ICAL_STATUS_CANCELLED;
}

static void fill(event_t *ev, icalcomponent *c, const char *account, const char *calendar)
{
	ev->uid = dup_or_null(icalcomponent_get_uid(c));
	ev->summary = dup_or_null(icalcomponent_get_summary(c));
	ev->location = dup_or_null(icalcomponent_get_location(c));
	ev->account = dup_or_null(account);
	ev->calendar = dup_or_null(calendar);
	ev->all_day = icaltime_is_date(icalcomponent_get_dtstart(c));
	ev->cancelled = is_cancelled(c);
}

/* ⚠ icalcomponent_foreach_recurrence CALLS BACK FOR A NON-RECURRING EVENT TOO,
 * once, for its single occurrence — which is the right behaviour and makes one
 * code path serve both. But it means the callback cannot infer "this came out
 * of a rule" from being called: that has to be asked of the component, or every
 * ordinary appointment is reported as recurring and a front end offers to edit
 * "this occurrence or the series" for something that has no series. */
static bool has_rule(icalcomponent *c)
{
	return icalcomponent_get_first_property(c, ICAL_RRULE_PROPERTY) ||
	       icalcomponent_get_first_property(c, ICAL_RDATE_PROPERTY);
}

typedef struct {
	events_t *out;
	icalcomponent **over;      /* the overrides, by RECURRENCE-ID */
	time_t *over_at;
	size_t nover;
	bool *over_used;
	const char *account, *calendar;
	icalcomponent *master;
	bool recurring;
} expand_ctx_t;

static void on_instance(icalcomponent *comp, const struct icaltime_span *span, void *data)
{
	(void)comp;
	expand_ctx_t *cx = data;

	/* ⚠ AN OVERRIDE REPLACES THE INSTANCE IT NAMES. Without this the moved
	 * occurrence appears twice: once where the rule says it would have been,
	 * once where it actually is. */
	icalcomponent *use = cx->master;
	for (size_t i = 0; i < cx->nover; i++) {
		if (cx->over_at[i] != span->start) continue;
		cx->over_used[i] = true;
		use = cx->over[i];
		break;
	}

	event_t *ev = events_add(cx->out);
	fill(ev, use, cx->account, cx->calendar);
	if (use == cx->master) {
		ev->start = span->start;
		ev->end = span->end;
		ev->recurring = cx->recurring;
	} else {
		/* The override carries its own times, which are the point of it. */
		ev->start = icaltime_as_timet_with_zone(icalcomponent_get_dtstart(use), icaltimezone_get_utc_timezone());
		icaltimetype e = icalcomponent_get_dtend(use);
		ev->end = icaltime_is_null_time(e) ? ev->start
		        : icaltime_as_timet_with_zone(e, icaltimezone_get_utc_timezone());
		/* An override only exists because there is a series to override. */
		ev->recurring = true;
	}
}

bool event_expand(const char *ics, size_t len, time_t from, time_t to,
                  const char *account, const char *calendar, events_t *out)
{
	char *text = xmalloc(len + 1);
	memcpy(text, ics, len);
	text[len] = '\0';

	icalcomponent *root = icalparser_parse_string(text);
	free(text);
	if (!root) return false;

	/* ⚠ THE VTIMEZONEs IN THIS FILE ARE REGISTERED FIRST. A server may send a
	 * TZID that is not an IANA name — Exchange sends "W. Europe Standard Time"
	 * — and libical can only resolve it from the VTIMEZONE travelling beside
	 * it. Parsing the whole VCALENDAR rather than the VEVENT alone is what
	 * makes that work. */
	icalcomponent *master = NULL;
	icalcomponent *over[64];
	time_t over_at[64];
	bool over_used[64];
	size_t nover = 0;

	for (icalcomponent *c = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT);
	     c; c = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
		icaltimetype rid = icalcomponent_get_recurrenceid(c);
		if (icaltime_is_null_time(rid)) {
			if (!master) master = c;
		} else if (nover < 64) {
			over[nover] = c;
			over_at[nover] = icaltime_as_timet_with_zone(rid, icaltimezone_get_utc_timezone());
			over_used[nover] = false;
			nover++;
		}
	}

	/* A VCALENDAR with only overrides is legal and does happen — a single
	 * moved occurrence, synced on its own. */
	if (!master && nover == 0) { icalcomponent_free(root); return true; }

	if (master) {
		expand_ctx_t cx = { out, over, over_at, nover, over_used,
		                    account, calendar, master, has_rule(master) };
		icaltimetype a = icaltime_from_timet_with_zone(from, 0, icaltimezone_get_utc_timezone());
		icaltimetype b = icaltime_from_timet_with_zone(to, 0, icaltimezone_get_utc_timezone());
		icalcomponent_foreach_recurrence(master, a, b, on_instance, &cx);
	}

	/* Any override the expansion did not consume — because the master's rule no
	 * longer generates that instant, or because there is no master here. */
	for (size_t i = 0; i < nover; i++) {
		if (over_used[i]) continue;
		time_t s = icaltime_as_timet_with_zone(icalcomponent_get_dtstart(over[i]),
		                                       icaltimezone_get_utc_timezone());
		if (s < from || s >= to) continue;
		event_t *ev = events_add(out);
		fill(ev, over[i], account, calendar);
		ev->start = s;
		icaltimetype e = icalcomponent_get_dtend(over[i]);
		ev->end = icaltime_is_null_time(e) ? s
		        : icaltime_as_timet_with_zone(e, icaltimezone_get_utc_timezone());
		ev->recurring = true;
	}

	icalcomponent_free(root);
	return true;
}

/* ── every calendar ─────────────────────────────────────────────────────── */

bool agenda_range(time_t from, time_t to, events_t *out, char **err)
{
	if (err) *err = NULL;
	events_init(out);

	accounts_t a;
	accounts_load(&a);

	for (size_t i = 0; i < a.n; i++) {
		account_t *acc = &a.e[i];
		for (size_t c = 0; c < acc->ncals; c++) {
			if (!acc->cals[c].enabled) continue;
			const char *label = acc->cals[c].name ? acc->cals[c].name : acc->cals[c].url;

			local_list_t l;
			local_scan(acc->name, label, &l);
			for (size_t k = 0; k < l.n; k++) {
				size_t len = 0;
				char *data = read_file(l.e[k].path, &len);
				if (!data) continue;
				/* ⚠ ONE UNREADABLE FILE IS NOT A FAILED AGENDA. A calendar with
				 * one malformed event still has the rest of the week in it. */
				if (!event_expand(data, len, from, to, acc->name, label, out))
					warn("could not read %s in %s / %s", l.e[k].uid, acc->name, label);
				free(data);
			}
			local_free(&l);
		}
	}

	accounts_free(&a);
	events_sort(out);
	return true;
}
