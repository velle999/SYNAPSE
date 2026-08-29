/* graph_test.c — the two conversions, which are where this backend can be
 * wrong in a way nothing notices.
 *
 * ⛔ THE ASYMMETRY IS THE POINT AND IS TESTED AS SUCH. Reading is forgiving:
 * an event whose recurrence cannot be expressed is still worth showing once.
 * Writing is not: an RRULE flattened on the way out DELETES a recurrence on
 * somebody's work calendar, and neither side reports anything wrong. So the
 * refusals below matter more than the conversions.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "graph.h"

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

static bool has(const char *hay, const char *needle)
{
	return hay && strstr(hay, needle) != NULL;
}

/* A single event, as Graph actually answers with Prefer: outlook.timezone="UTC". */
static const char *SINGLE =
"{\"id\":\"AAMkAD_abc\",\"iCalUId\":\"040000008200E00074C5B7101A82E008\","
"\"@odata.etag\":\"W/\\\"CQAAABYAAAA=\\\"\","
"\"subject\":\"Budget review; Q4, final\",\"isAllDay\":false,"
"\"lastModifiedDateTime\":\"2026-08-01T09:12:00.0000000\","
"\"start\":{\"dateTime\":\"2026-09-10T14:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"end\":{\"dateTime\":\"2026-09-10T15:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"location\":{\"displayName\":\"Room 2, floor 3\"}}";

static const char *WEEKLY =
"{\"id\":\"AAMkAD_w\",\"iCalUId\":\"weekly-uid\",\"subject\":\"Standup\","
"\"isAllDay\":false,"
"\"start\":{\"dateTime\":\"2026-09-07T09:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"end\":{\"dateTime\":\"2026-09-07T09:15:00.0000000\",\"timeZone\":\"UTC\"},"
"\"recurrence\":{\"pattern\":{\"type\":\"weekly\",\"interval\":1,"
"\"daysOfWeek\":[\"monday\",\"wednesday\"]},"
"\"range\":{\"type\":\"numbered\",\"numberOfOccurrences\":10}}}";

static const char *RELATIVE_MONTHLY =
"{\"id\":\"AAMkAD_m\",\"iCalUId\":\"monthly-uid\",\"subject\":\"Board\","
"\"isAllDay\":false,"
"\"start\":{\"dateTime\":\"2026-09-01T10:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"end\":{\"dateTime\":\"2026-09-01T11:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"recurrence\":{\"pattern\":{\"type\":\"relativeMonthly\",\"interval\":2,"
"\"index\":\"last\",\"daysOfWeek\":[\"friday\"]},"
"\"range\":{\"type\":\"noEnd\"}}}";

/* A pattern with no faithful iCalendar form. Graph will happily describe one. */
static const char *ODD =
"{\"id\":\"AAMkAD_o\",\"iCalUId\":\"odd-uid\",\"subject\":\"Odd\","
"\"isAllDay\":false,"
"\"start\":{\"dateTime\":\"2026-09-01T10:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"end\":{\"dateTime\":\"2026-09-01T11:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"recurrence\":{\"pattern\":{\"type\":\"someFuturePattern\",\"interval\":1},"
"\"range\":{\"type\":\"noEnd\"}}}";

static const char *ALLDAY =
"{\"id\":\"AAMkAD_a\",\"iCalUId\":\"allday-uid\",\"subject\":\"Holiday\","
"\"isAllDay\":true,"
"\"start\":{\"dateTime\":\"2026-09-15T00:00:00.0000000\",\"timeZone\":\"UTC\"},"
"\"end\":{\"dateTime\":\"2026-09-16T00:00:00.0000000\",\"timeZone\":\"UTC\"}}";

int main(void)
{
	char *err = NULL;

	/* ── down ───────────────────────────────────────────────────────────── */

	char *ics = graph_json_to_ics(SINGLE, &err);
	ok_("a single event converts", ics != NULL);
	if (!ics) { printf("        (%s)\n", err ? err : "?"); return 1; }

	/* ⛔ iCalUId, NOT id. `id` is Graph's own handle and changes when an event
	 * moves between calendars; `iCalUId` is what every other system knows the
	 * appointment by, and sync matches on it. */
	ok_("…under its iCalUId, not Graph's internal id",
	    has(ics, "UID:040000008200E00074C5B7101A82E008") && !has(ics, "AAMkAD_abc"));
	ok_("…with the time as UTC", has(ics, "DTSTART:20260910T140000Z"));
	ok_("…and the end", has(ics, "DTEND:20260910T150000Z"));
	ok_("…and the location", has(ics, "LOCATION:Room 2"));

	/* ⛔ A SEMICOLON AND A COMMA END AN iCalendar VALUE WHERE THEY STAND. An
	 * unescaped subject truncates the summary and can invent a property. */
	ok_("…and a subject containing ; and , is escaped",
	    /* ⚠ "\;" IN C IS THE TWO CHARACTERS \; — which is what iCalendar
	     * wants. Written "\;" it is an invalid escape that the compiler folds
	     * to a bare ';', and the expectation quietly stops testing the thing
	     * it names. Second time this exact slip has cost a red test here. */
	    has(ics, "SUMMARY:Budget review\\; Q4\\, final"));
	free(ics);

	ics = graph_json_to_ics(WEEKLY, &err);
	ok_("a weekly pattern becomes an RRULE",
	    has(ics, "RRULE:FREQ=WEEKLY;INTERVAL=1;BYDAY=MO,WE;COUNT=10"));
	ok_("…and is not marked lossy", !has(ics, "X-SYNCAL-LOSSY"));
	free(ics);

	ics = graph_json_to_ics(RELATIVE_MONTHLY, &err);
	ok_("'last Friday of every second month' becomes BYDAY=-1FR",
	    has(ics, "RRULE:FREQ=MONTHLY;INTERVAL=2;BYDAY=-1FR"));
	ok_("…with no COUNT or UNTIL, because it does not end",
	    !has(ics, "COUNT=") && !has(ics, "UNTIL="));
	free(ics);

	ics = graph_json_to_ics(ALLDAY, &err);
	ok_("an all-day event uses DATE values",
	    has(ics, "DTSTART;VALUE=DATE:20260915") && has(ics, "DTEND;VALUE=DATE:20260916"));
	free(ics);

	/* ⛔ THE CASE THAT DECIDES WHETHER THIS BACKEND IS HONEST. A pattern with
	 * no iCalendar form must come down as ONE occurrence and say so — showing
	 * it repeating on the wrong days would be worse, and dropping it would hide
	 * a real appointment. */
	ics = graph_json_to_ics(ODD, &err);
	ok_("an unrepresentable pattern still yields an event", ics != NULL);
	ok_("…with no RRULE at all", !has(ics, "RRULE:"));
	ok_("…marked so it can never be written back", has(ics, "X-SYNCAL-LOSSY"));

	/* ── up ─────────────────────────────────────────────────────────────── */

	free(err); err = NULL;
	char *json = graph_ics_to_json(ics, strlen(ics), &err);
	ok_("…and the upload of that event is REFUSED", json == NULL);
	ok_("…with a reason naming Outlook as where to edit it",
	    err && strstr(err, "Outlook") != NULL);
	free(json); free(err); err = NULL;
	free(ics);

	/* A recurring event this CAN read is still not written back, because
	 * writing recurrence to Graph is not built — and a flattened series
	 * replaces the real one silently. */
	ics = graph_json_to_ics(WEEKLY, &err);
	json = graph_ics_to_json(ics, strlen(ics), &err);
	ok_("a repeating event is not written back either", json == NULL);
	ok_("…and says the local copy is unchanged",
	    err && strstr(err, "unchanged") != NULL);
	free(json); free(err); err = NULL;
	free(ics);

	/* ── the round trip that is allowed ─────────────────────────────────── */

	ics = graph_json_to_ics(SINGLE, &err);
	json = graph_ics_to_json(ics, strlen(ics), &err);
	ok_("a single event converts back to JSON", json != NULL);
	if (json) {
		ok_("…keeping the subject, unescaped again",
		    has(json, "Budget review; Q4, final"));
		ok_("…the start, as UTC", has(json, "2026-09-10T14:00:00") && has(json, "UTC"));
		ok_("…and not marked all-day", has(json, "\"isAllDay\":false"));
		/* ⛔ THE UID GOES UP ON A CREATE. Without it Graph invents one, the next
		 * listing does not recognise the event, and it comes back down as a
		 * second copy of what was just uploaded. */
		ok_("…and carries the iCalUId so the two sides agree on the identity",
		    has(json, "040000008200E00074C5B7101A82E008"));
	}
	free(json); free(ics); free(err);

	/* Rubbish in. */
	err = NULL;
	ok_("JSON that is not an event is refused, not crashed on",
	    graph_json_to_ics("{\"nothing\":1}", &err) == NULL);
	free(err);

	printf("\n%d/%d passed\n", total - fails, total);
	return fails ? 1 : 0;
}
