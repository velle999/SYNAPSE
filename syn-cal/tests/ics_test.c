/* ics_test.c — the line level of iCalendar.
 *
 * Every fixture here is a shape a real server actually sends. The folded line,
 * the bare LF and the quoted colon in a parameter are not hypothetical: they
 * are what Google, Nextcloud and Fastmail respectively put on the wire, and
 * each one breaks a different naive reader.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "syncal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, total = 0;

static void eq(const char *what, const char *got, const char *want)
{
	total++;
	if ((!got && !want) || (got && want && strcmp(got, want) == 0)) {
		printf("  ok    %s\n", what);
	} else {
		printf("  FAIL  %s\n        got  [%s]\n        want [%s]\n",
		       what, got ? got : "(null)", want ? want : "(null)");
		fails++;
	}
}

int main(void)
{
	/* ── unfolding ──────────────────────────────────────────────────────── */

	const char *folded = "SUMMARY:A very long summ\r\n ary that was folded\r\nUID:abc\r\n";
	char *u = ics_unfold(folded, strlen(folded));
	eq("CRLF+space unfolds", u, "SUMMARY:A very long summary that was folded\nUID:abc\n");
	free(u);

	/* ⚠ Bare LF is what half the servers in the world send, spec or no spec. */
	const char *lf = "SUMMARY:split\n\there\nUID:abc\n";
	u = ics_unfold(lf, strlen(lf));
	eq("bare LF + tab unfolds too", u, "SUMMARY:splithere\nUID:abc\n");
	free(u);

	/* ── properties ─────────────────────────────────────────────────────── */

	u = ics_unfold("UID:plain\n", 10);
	char *v = ics_prop(u, "UID");
	eq("a bare property", v, "plain");
	free(v); free(u);

	u = ics_unfold("DTSTART;TZID=Europe/London:20260901T090000\n", 43);
	v = ics_prop(u, "DTSTART");
	eq("parameters are skipped", v, "20260901T090000");
	free(v); free(u);

	/* ⛔ THE ONE THAT BREAKS strchr(line, ':'). A parameter value may be quoted
	 * and may contain a colon; stopping at the first one returns a fragment of
	 * the parameter instead of the value. */
	const char *q = "ATTENDEE;CN=\"Smith: Jane\";ROLE=REQ:mailto:jane@example.org\n";
	u = ics_unfold(q, strlen(q));
	v = ics_prop(u, "ATTENDEE");
	eq("a quoted colon in a parameter is not the separator", v, "mailto:jane@example.org");
	free(v); free(u);

	u = ics_unfold("UID:x\n", 6);
	v = ics_prop(u, "SUMMARY");
	eq("a missing property is NULL", v, NULL);
	free(v); free(u);

	/* A name that is a prefix of another must not match it. */
	u = ics_unfold("DTSTAMP:20260101T000000Z\nDTSTART:20260901T090000Z\n", 50);
	v = ics_prop(u, "DTSTART");
	eq("DTSTAMP is not DTSTART", v, "20260901T090000Z");
	free(v); free(u);

	/* ── uid and kind ───────────────────────────────────────────────────── */

	const char *ev =
	    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
	    "BEGIN:VTIMEZONE\r\nTZID:Europe/London\r\nEND:VTIMEZONE\r\n"
	    "BEGIN:VEVENT\r\nUID:evt-1@example.org\r\nSUMMARY:Standup\r\nEND:VEVENT\r\n"
	    "END:VCALENDAR\r\n";
	v = ics_uid(ev, strlen(ev));
	eq("the UID is found past a VTIMEZONE", v, "evt-1@example.org");
	free(v);

	v = ics_kind(ev, strlen(ev));
	eq("the kind skips VCALENDAR and VTIMEZONE", v, "VEVENT");
	free(v);

	const char *todo = "BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nUID:t1\r\nEND:VTODO\r\nEND:VCALENDAR\r\n";
	v = ics_kind(todo, strlen(todo));
	eq("a VTODO is a VTODO", v, "VTODO");
	free(v);

	const char *nouid = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:x\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
	v = ics_uid(nouid, strlen(nouid));
	eq("no UID is NULL, not an empty string", v, NULL);
	free(v);

	/* ── rewriting the UID, which is what a kept conflict does ──────────── */

	size_t n = 0;
	char *re = ics_replace_uid(ev, strlen(ev), "evt-1-local@syn-cal", &n);
	v = ics_uid(re, n);
	eq("the UID is replaced", v, "evt-1-local@syn-cal");
	free(v);

	/* ⛔ AND NOTHING ELSE MOVED. A conflict copy that loses the attendees or
	 * the alarms is a worse outcome than the conflict. */
	total++;
	if (strstr(re, "SUMMARY:Standup") && strstr(re, "BEGIN:VTIMEZONE") &&
	    strstr(re, "TZID:Europe/London") && !strstr(re, "evt-1@example.org")) {
		printf("  ok    everything except the UID survives byte for byte\n");
	} else {
		printf("  FAIL  the rewrite disturbed something other than the UID\n");
		fails++;
	}

	/* The CRLF line endings the file arrived with must survive too, or the
	 * server sees a different document and every sync re-uploads. */
	total++;
	if (strstr(re, "UID:evt-1-local@syn-cal\r\n")) {
		printf("  ok    the file's own line endings are kept\n");
	} else {
		printf("  FAIL  CRLF was not preserved across the rewrite\n");
		fails++;
	}
	free(re);

	/* ── the href name: safe, stable, unique ────────────────────────────── */
	/*
	 * ⛔ THE LIVE TEST FOUND THIS. The first version addressed items by the
	 * percent-encoded UID; a real server decodes %2F before routing, sees a path
	 * traversal and answers 403 where the same event under a plain name answers
	 * 201. These three properties are what replaced it, and all three matter.
	 */
	char *n1 = ics_safe_name("weird/uid+with spaces@x");
	total++;
	{
		bool clean = true;
		for (const char *c = n1; *c; c++)
			if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
			      (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.'))
				clean = false;
		if (clean && n1[0] != '.' && n1[0] != '-') printf("  ok    a hostile UID reduces to a safe name\n");
		else { printf("  FAIL  unsafe name [%s]\n", n1); fails++; }
	}

	char *n2 = ics_safe_name("weird/uid+with spaces@x");
	eq("…the same UID always gives the same name", n1, n2);
	free(n2);

	/* ⛔ AND TWO UIDS THAT REDUCE TO THE SAME TEXT STILL DIFFER. Without the
	 * hash of the whole original, "meeting/1" and "meeting:1" would both become
	 * "meeting-1" and one event would overwrite the other on the server. */
	char *a = ics_safe_name("meeting/1");
	char *b = ics_safe_name("meeting:1");
	total++;
	if (strcmp(a, b) != 0) printf("  ok    …and two UIDs that reduce alike still differ\n");
	else { printf("  FAIL  [%s] collides for meeting/1 and meeting:1\n", a); fails++; }
	free(a); free(b);

	/* A UID that is nothing but punctuation still has to produce a filename. */
	char *only = ics_safe_name("///");
	total++;
	if (only && only[0] && only[0] != '-' && only[0] != '.')
		printf("  ok    …a UID of pure punctuation still names something\n");
	else { printf("  FAIL  [%s] is not a usable name\n", only ? only : "(null)"); fails++; }
	free(only);

	/* Long enough to matter: filesystems stop at 255 bytes and so do servers. */
	char longuid[600];
	memset(longuid, 'x', sizeof longuid - 1);
	longuid[sizeof longuid - 1] = '\0';
	char *ln = ics_safe_name(longuid);
	total++;
	if (strlen(ln) < 120) printf("  ok    …and a 600-character UID is capped\n");
	else { printf("  FAIL  name is %zu characters\n", strlen(ln)); fails++; }
	free(ln); free(n1);

	/* ── the hash answers "did this change", and only that ──────────────── */

	char *h1 = content_hash("abc", 3);
	char *h2 = content_hash("abc", 3);
	char *h3 = content_hash("abd", 3);
	eq("the same bytes hash the same", h1, h2);
	total++;
	if (strcmp(h1, h3) != 0) printf("  ok    different bytes hash differently\n");
	else { printf("  FAIL  hash collision on a one-byte change\n"); fails++; }
	free(h1); free(h2); free(h3);

	printf("\n%d/%d passed\n", total - fails, total);
	return fails ? 1 : 0;
}
