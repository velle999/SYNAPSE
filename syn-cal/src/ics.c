/* ics.c — the line level of iCalendar, and nothing above it.
 *
 * ⛔ THIS IS NOT AN iCalendar PARSER AND MUST NOT BECOME ONE. Recurrence rules,
 * time zones and duration arithmetic are where iCalendar is genuinely hard, and
 * libical already does them correctly; event.c is where that lives. What is
 * here is the little that SYNC needs — the UID that identifies an event on both
 * sides, and the SEQUENCE that says which revision it is — and sync must work
 * whether or not the display half is built or linked.
 *
 * That split is the reason the engine has no libical dependency: moving an
 * opaque .ics blob between a folder and a server does not require understanding
 * it, and pretending otherwise is how a sync engine acquires a parser bug.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syncal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Unfold: RFC 5545 breaks a long line with CRLF followed by one space or tab,
 * and the break plus that one character are removed to rebuild the line.
 *
 * ⚠ LF ALONE COUNTS TOO. The spec says CRLF, and half the calendar servers in
 * the world send bare LF. A reader that insists on CRLF returns a UID with a
 * newline in the middle of it, which then does not match itself. */
char *ics_unfold(const char *data, size_t len)
{
	buf_t out;
	buf_init(&out);
	for (size_t i = 0; i < len; i++) {
		if (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n') {
			if (i + 2 < len && (data[i + 2] == ' ' || data[i + 2] == '\t')) { i += 2; continue; }
			buf_addstr(&out, "\n");
			i++;
			continue;
		}
		if (data[i] == '\n') {
			if (i + 1 < len && (data[i + 1] == ' ' || data[i + 1] == '\t')) { i++; continue; }
			buf_addstr(&out, "\n");
			continue;
		}
		buf_add(&out, data + i, 1);
	}
	if (!out.b) out.b = xstrdup("");
	return out.b;
}

/* The value of the first `NAME` property, or NULL.
 *
 * A property line is NAME, optional ;PARAM=..., then ':' and the value. The
 * parameters are skipped rather than parsed: nothing at this level needs them,
 * and a parameter value may legally contain a quoted ':' — which is exactly the
 * character a naive strchr() would stop at.
 */
char *ics_prop(const char *unfolded, const char *name)
{
	size_t nlen = strlen(name);
	const char *p = unfolded;

	while (p && *p) {
		const char *eol = strchr(p, '\n');
		size_t linelen = eol ? (size_t)(eol - p) : strlen(p);

		if (linelen > nlen && strncasecmp(p, name, nlen) == 0 &&
		    (p[nlen] == ':' || p[nlen] == ';')) {
			const char *q = p + nlen;
			bool quoted = false;
			while ((size_t)(q - p) < linelen) {
				if (*q == '"') quoted = !quoted;
				else if (*q == ':' && !quoted) break;
				q++;
			}
			if ((size_t)(q - p) < linelen && *q == ':') {
				q++;
				size_t vlen = linelen - (size_t)(q - p);
				/* A trailing CR survives when the input used bare CRLF and the
				 * unfolder kept the CR as ordinary text. */
				while (vlen && (q[vlen - 1] == '\r' || q[vlen - 1] == ' ')) vlen--;
				char *val = xmalloc(vlen + 1);
				memcpy(val, q, vlen);
				val[vlen] = '\0';
				return val;
			}
		}
		p = eol ? eol + 1 : NULL;
	}
	return NULL;
}

/* The UID an .ics identifies itself by.
 *
 * ⚠ VTIMEZONE HAS NO UID, which is what makes "the first UID in the file" safe
 * here — the alternative is tracking component nesting, and every extra rule in
 * this file is a rule that can disagree with libical. If a file ever arrives
 * with no UID at all it is not addressable on a CalDAV server either, so the
 * caller's only sane response is to skip it, loudly.
 */
char *ics_uid(const char *data, size_t len)
{
	char *unfolded = ics_unfold(data, len);
	char *uid = ics_prop(unfolded, "UID");
	free(unfolded);
	if (uid && !*uid) { free(uid); return NULL; }
	return uid;
}

/* The component type: VEVENT, VTODO, VJOURNAL — the first one that is not
 * VCALENDAR or VTIMEZONE. Used to keep a to-do out of a day grid. */
char *ics_kind(const char *data, size_t len)
{
	char *unfolded = ics_unfold(data, len);
	const char *p = unfolded;
	char *kind = NULL;

	while (p && *p) {
		const char *eol = strchr(p, '\n');
		size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
		if (linelen > 7 && strncasecmp(p, "BEGIN:V", 7) == 0) {
			const char *v = p + 6;
			size_t vlen = linelen - 6;
			while (vlen && (v[vlen - 1] == '\r' || v[vlen - 1] == ' ')) vlen--;
			if (!(vlen == 9 && strncasecmp(v, "VCALENDAR", 9) == 0) &&
			    !(vlen == 9 && strncasecmp(v, "VTIMEZONE", 9) == 0)) {
				kind = xmalloc(vlen + 1);
				memcpy(kind, v, vlen);
				kind[vlen] = '\0';
				for (char *c = kind; *c; c++) *c = (char)toupper((unsigned char)*c);
				break;
			}
		}
		p = eol ? eol + 1 : NULL;
	}
	free(unfolded);
	return kind;
}

/* Replace the UID, keeping everything else byte for byte.
 *
 * This is what "keep both" does to the losing side of a conflict: the event
 * survives in full, under an identity that cannot collide with the one the
 * server kept. Rewriting rather than regenerating matters — a round trip
 * through a parser and back is where the attendee list and the alarms go. */
char *ics_replace_uid(const char *data, size_t len, const char *uid, size_t *out_len)
{
	buf_t out;
	buf_init(&out);
	bool done = false;
	size_t i = 0;

	while (i < len) {
		size_t j = i;
		while (j < len && data[j] != '\n') j++;
		size_t linelen = j - i;                 /* without the newline */

		bool is_uid = !done && linelen >= 4 && strncasecmp(data + i, "UID:", 4) == 0;
		if (is_uid) {
			buf_addf(&out, "UID:%s", uid);
			/* Keep whatever line ending the file was already using. */
			if (linelen && data[i + linelen - 1] == '\r') buf_addstr(&out, "\r");
			done = true;
		} else {
			buf_add(&out, data + i, linelen);
		}
		if (j < len) buf_addstr(&out, "\n");
		i = j + 1;
	}

	if (out_len) *out_len = out.len;
	if (!out.b) out.b = xstrdup("");
	return out.b;
}

/* A filename-safe, stable, unique name for an event.
 *
 * ⛔ THE href IS NOT THE UID, AND MUST NOT BE DERIVED FROM IT NAIVELY. CalDAV
 * never required the two to match — the href is the server's address for the
 * resource, the UID is the event's identity — and treating them as one thing
 * breaks on the UIDs real providers issue. Percent-encoding is not a way out:
 * a `%2F` in a path is decoded before routing by most servers, which then see a
 * path traversal and refuse it. Measured against Radicale: `%2F` in the href
 * answers 403 where the identical event under a plain name answers 201.
 *
 * So the UID is reduced to characters no server or filesystem can object to,
 * and a hash of the WHOLE original is appended. The hash is what makes it
 * safe to be lossy:
 *
 *   stable   the same UID always produces the same name, so a re-sync finds
 *            the event it already uploaded instead of creating a second one
 *   unique   two UIDs that reduce to the same text still differ, so
 *            "meeting/1" and "meeting:1" cannot collide
 */
char *ics_safe_name(const char *uid)
{
	buf_t out;
	buf_init(&out);

	size_t kept = 0;
	for (const unsigned char *p = (const unsigned char *)uid; *p && kept < 80; p++) {
		bool safe = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
		            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.';
		char c = safe ? (char)*p : '-';
		/* A leading dot would make it a hidden file in the vdir, and a run of
		 * dashes is noise. */
		if (c == '-' && (out.len == 0 || out.b[out.len - 1] == '-')) continue;
		if (c == '.' && out.len == 0) continue;
		buf_add(&out, &c, 1);
		kept++;
	}
	while (out.len && out.b[out.len - 1] == '-') out.b[--out.len] = '\0';

	char *hash = content_hash(uid, strlen(uid));
	char *name = out.len ? xasprintf("%s-%s", out.b, hash) : xasprintf("event-%s", hash);
	free(hash);
	buf_free(&out);
	return name;
}
