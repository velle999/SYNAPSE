/* compose.c — writing an event into the vdir. See compose.h.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "compose.h"
#include "account.h"
#include "settings.h"
#include "event.h"
#include "store.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* ── text that has to survive being a property value ────────────────────── */

/* RFC 5545 §3.3.11: a backslash, a comma, a semicolon and a newline all mean
 * something in a property value.
 *
 * ⛔ AND A TITLE CONTAINING A COMMA IS ORDINARY. "Lunch, then dentist" written
 * raw is two values to a parser, so the event comes back truncated at the comma
 * — on the server, where it is somebody else's copy too. */
static char *ics_escape(const char *s)
{
	buf_t out;
	buf_init(&out);
	for (const char *p = s ? s : ""; *p; p++) {
		switch (*p) {
		case '\\': buf_addstr(&out, "\\\\"); break;
		case ',':  buf_addstr(&out, "\\,");  break;
		case ';':  buf_addstr(&out, "\\;");  break;
		case '\n': buf_addstr(&out, "\\n");  break;
		case '\r': break;
		default:   buf_add(&out, p, 1);      break;
		}
	}
	if (!out.b) out.b = xstrdup("");
	return out.b;
}

/* The inverse, for a value read back out of a file.
 *
 * ⛔ WITHOUT THIS, EVERY EDIT ESCAPES WHAT WAS ALREADY ESCAPED. ics_prop returns
 * the raw property text, so a location of "Clinic; room 2" comes back as
 * "Clinic\; room 2", goes through ics_escape again as "Clinic\\; room 2", and
 * each edit doubles the backslashes — visible to the person only after the
 * second one, by which point their own text has been corrupted on the server. */
static char *ics_unescape(const char *s)
{
	buf_t out;
	buf_init(&out);
	for (const char *p = s ? s : ""; *p; p++) {
		if (*p != '\\' || !p[1]) { buf_add(&out, p, 1); continue; }
		p++;
		switch (*p) {
		case 'n': case 'N': buf_addstr(&out, "\n"); break;
		default:            buf_add(&out, p, 1);    break;
		}
	}
	if (!out.b) out.b = xstrdup("");
	return out.b;
}

/* ⚠ FOLDED AT 75 OCTETS, and folded on a character boundary. A line longer than
 * that is legal to write and refused by strict parsers; splitting it mid-UTF-8
 * puts half a character on each line, which every parser then reads as
 * mojibake. Continuation lines begin with one space. */
static void add_folded(buf_t *out, const char *line)
{
	size_t n = strlen(line), i = 0, col = 0;
	while (i < n) {
		size_t take = 0;
		while (i + take < n && col + take < 74) {
			/* Never break before a UTF-8 continuation byte. */
			size_t next = take + 1;
			while (i + next < n && ((unsigned char)line[i + next] & 0xC0) == 0x80) next++;
			if (col + next > 74) break;
			take = next;
		}
		if (take == 0) take = 1;
		buf_add(out, line + i, take);
		i += take;
		if (i < n) { buf_addstr(out, "\r\n "); col = 1; }
	}
	buf_addstr(out, "\r\n");
}

static void add_prop(buf_t *out, const char *name, const char *value)
{
	if (!value || !*value) return;
	char *v = ics_escape(value);
	char *line = xasprintf("%s:%s", name, v);
	add_folded(out, line);
	free(line);
	free(v);
}

/* ── time ───────────────────────────────────────────────────────────────── */

static char *utc_stamp(time_t t)
{
	struct tm g;
	gmtime_r(&t, &g);
	char b[32];
	strftime(b, sizeof b, "%Y%m%dT%H%M%SZ", &g);
	return xstrdup(b);
}

static char *date_stamp(time_t t)
{
	struct tm lt;
	localtime_r(&t, &lt);
	char b[16];
	strftime(b, sizeof b, "%Y%m%d", &lt);
	return xstrdup(b);
}

/* A UID nothing else will pick. The random half comes from the kernel: two
 * events created in the same second on the same machine must not collide, and
 * a UID collision on a server silently overwrites somebody's appointment. */
static char *new_uid(void)
{
	unsigned char raw[8];
	FILE *f = fopen("/dev/urandom", "rb");
	if (f) {
		if (fread(raw, 1, sizeof raw, f) != sizeof raw) memset(raw, 0, sizeof raw);
		fclose(f);
	} else {
		memset(raw, 0, sizeof raw);
	}
	char hex[17];
	for (int i = 0; i < 8; i++) snprintf(hex + i * 2, 3, "%02x", raw[i]);
	return xasprintf("%ld-%s@syn-cal", (long)time(NULL), hex);
}

char *ics_compose(const draft_t *d, size_t *out_len)
{
	buf_t o;
	buf_init(&o);

	char *uid = d->uid && *d->uid ? xstrdup(d->uid) : new_uid();
	char *stamp = utc_stamp(time(NULL));

	buf_addstr(&o, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n");
	buf_addstr(&o, "PRODID:-//SynapseOS//syn-cal//EN\r\n");
	buf_addstr(&o, "CALSCALE:GREGORIAN\r\nBEGIN:VEVENT\r\n");

	add_prop(&o, "UID", uid);
	buf_addf(&o, "DTSTAMP:%s\r\n", stamp);
	buf_addf(&o, "LAST-MODIFIED:%s\r\n", stamp);
	buf_addf(&o, "SEQUENCE:%d\r\n", d->sequence);

	if (d->all_day) {
		/* ⛔ VALUE=DATE, AND DTEND IS THE DAY AFTER. The end of an all-day
		 * event is exclusive in RFC 5545, so a one-day event ending on its own
		 * date is zero days long and vanishes from most clients. */
		char *s = date_stamp(d->start);
		char *e = date_stamp(d->end);
		buf_addf(&o, "DTSTART;VALUE=DATE:%s\r\n", s);
		buf_addf(&o, "DTEND;VALUE=DATE:%s\r\n", e);
		free(s); free(e);
	} else {
		char *s = utc_stamp(d->start);
		char *e = utc_stamp(d->end);
		buf_addf(&o, "DTSTART:%s\r\n", s);
		buf_addf(&o, "DTEND:%s\r\n", e);
		free(s); free(e);
	}

	add_prop(&o, "SUMMARY", d->summary);
	add_prop(&o, "LOCATION", d->location);
	add_prop(&o, "DESCRIPTION", d->notes);

	if (d->remind_min > 0) {
		/* ⚠ THE TRIGGER IS NEGATIVE, meaning "before the start". A positive one
		 * is legal and fires after the event, which is not what anybody asking
		 * for a fifteen-minute reminder means. */
		buf_addstr(&o, "BEGIN:VALARM\r\nACTION:DISPLAY\r\n");
		add_prop(&o, "DESCRIPTION", d->summary && *d->summary ? d->summary : "Reminder");
		buf_addf(&o, "TRIGGER:-PT%dM\r\n", d->remind_min);
		buf_addstr(&o, "END:VALARM\r\n");
	}

	buf_addstr(&o, "END:VEVENT\r\nEND:VCALENDAR\r\n");
	free(uid);
	free(stamp);

	if (out_len) *out_len = o.len;
	return o.b;
}

/* ── what a person types ────────────────────────────────────────────────── */

bool parse_when(const char *s, time_t *out, bool *all_day)
{
	if (!s || !*s) return false;

	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_isdst = -1;                 /* let mktime decide, per that date */

	/* The date is required; the time is what decides whether this is all day. */
	int y = 0, mo = 0, d = 0, h = 0, mi = 0;
	char rest[32] = "";
	int got = sscanf(s, "%d-%d-%d %31s", &y, &mo, &d, rest);
	if (got < 3) {
		got = sscanf(s, "%d-%d-%dT%31s", &y, &mo, &d, rest);
		if (got < 3) return false;
	}
	if (mo < 1 || mo > 12 || d < 1 || d > 31 || y < 1970 || y > 9999) return false;

	t.tm_year = y - 1900;
	t.tm_mon = mo - 1;
	t.tm_mday = d;

	/* ⚠ THE ISO 'T' IS PART OF THE SEPARATOR, NOT OF THE TIME. "%d-%d-%d %s"
	 * matches "2026-09-21T13:15" too — the %s simply starts at the T — so the
	 * time then fails to parse and a perfectly ordinary spelling is refused. */
	const char *time_part = rest;
	if (*time_part == 'T' || *time_part == 't') time_part++;

	if (got == 3 || !*time_part) {
		if (all_day) *all_day = true;
	} else {
		if (all_day) *all_day = false;
		/* "13:15", "1:15pm", "1pm" */
		char suffix[8] = "";
		int n = sscanf(time_part, "%d:%d%7s", &h, &mi, suffix);
		if (n < 2) {
			mi = 0;
			n = sscanf(time_part, "%d%7s", &h, suffix);
			if (n < 1) return false;
		}
		if (*suffix) {
			if (!strcasecmp(suffix, "pm") && h < 12) h += 12;
			else if (!strcasecmp(suffix, "am") && h == 12) h = 0;
			else if (strcasecmp(suffix, "pm") && strcasecmp(suffix, "am")) return false;
		}
		if (h < 0 || h > 23 || mi < 0 || mi > 59) return false;
		t.tm_hour = h;
		t.tm_min = mi;
	}

	time_t r = mktime(&t);
	if (r == (time_t)-1) return false;
	*out = r;
	return true;
}

/* Shared by parse_duration and parse_reminder: "1h30m" is 90 minutes either
 * way, and having one reader means they cannot disagree about "2d". */
static bool parse_span(const char *s, long *seconds)
{
	if (!s || !*s) return false;
	long total = 0;
	bool any = false;
	const char *p = s;
	while (*p) {
		while (*p == ' ') p++;
		if (!*p) break;
		char *end = NULL;
		long n = strtol(p, &end, 10);
		if (end == p) return false;
		long mult;
		switch (tolower((unsigned char)*end)) {
		case 'd': mult = 86400; break;
		case 'h': mult = 3600;  break;
		case 'm': mult = 60;    break;
		case 's': mult = 1;     break;
		/* A bare number is minutes: "--remind 15" is the thing people type. */
		case '\0': mult = 60; end--; break;
		default: return false;
		}
		if (n < 0) return false;
		total += n * mult;
		any = true;
		p = end + 1;
	}
	if (!any) return false;
	*seconds = total;
	return true;
}

bool parse_duration(const char *s, long *seconds)
{
	long v;
	if (!parse_span(s, &v)) return false;
	/* ⛔ ZERO IS REFUSED. An event that ends when it starts is not a short
	 * event; most clients draw nothing at all for it. */
	if (v <= 0) return false;
	*seconds = v;
	return true;
}

bool parse_reminder(const char *s, int *minutes)
{
	if (!s || !*s) return false;
	if (!strcasecmp(s, "none") || !strcasecmp(s, "off") || !strcmp(s, "0")) {
		*minutes = 0;
		return true;
	}
	long v;
	if (!parse_span(s, &v)) return false;
	if (v < 0) return false;
	*minutes = (int)(v / 60);
	return true;
}

/* One DTSTART or DTEND straight out of the file.
 *
 * ⛔ NOT THROUGH THE RECURRENCE EXPANDER. Asking event_expand for "every
 * occurrence between the epoch and forever" to recover one event's own start
 * quietly returned nothing, and the edit then wrote the event at 1970 — a
 * silent time travel that only shows up when the appointment stops appearing.
 * A non-repeating event has exactly one DTSTART, and edit refuses the repeating
 * kind, so reading the property is both simpler and the whole answer.
 *
 * ⚠ THE PARAMETERS MATTER: `DTSTART;VALUE=DATE:20260921` is an all-day event and
 * `DTSTART:20260921T181500Z` is an instant, and they are the same property. */
static bool read_dt(const char *unfolded, const char *name, time_t *out, bool *is_date)
{
	size_t nlen = strlen(name);
	const char *p = unfolded;
	while (p && *p) {
		const char *eol = strchr(p, '\n');
		size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
		if (!strncmp(p, name, nlen) && (p[nlen] == ':' || p[nlen] == ';')) {
			const char *colon = memchr(p, ':', linelen);
			if (!colon) return false;
			bool date_only = memmem(p, (size_t)(colon - p), "VALUE=DATE", 10) != NULL;
			char val[32];
			size_t vlen = linelen - (size_t)(colon + 1 - p);
			while (vlen && (colon[vlen] == '\r' || colon[vlen] == ' ')) vlen--;
			if (vlen >= sizeof val) vlen = sizeof val - 1;
			memcpy(val, colon + 1, vlen);
			val[vlen] = '\0';

			struct tm t;
			memset(&t, 0, sizeof t);
			t.tm_isdst = -1;
			size_t l = strlen(val);
			if (date_only || l == 8) {
				if (!strptime(val, "%Y%m%d", &t)) return false;
				if (is_date) *is_date = true;
				*out = mktime(&t);
			} else if (l && val[l - 1] == 'Z') {
				if (!strptime(val, "%Y%m%dT%H%M%SZ", &t)) return false;
				if (is_date) *is_date = false;
				*out = timegm(&t);
			} else {
				/* A floating time means "whatever the clock says here". */
				if (!strptime(val, "%Y%m%dT%H%M%S", &t)) return false;
				if (is_date) *is_date = false;
				*out = mktime(&t);
			}
			return *out != (time_t)-1;
		}
		p = eol ? eol + 1 : NULL;
	}
	return false;
}

/* ── which calendar ─────────────────────────────────────────────────────── */

/* The calendar a new event goes to when nobody says. "<account>/<calendar>". */
#define DEFAULT_CAL_KEY "default_calendar"

static bool split_in(const char *in, char **acct, char **cal);

/* Is this account/calendar pair switched on right now? */
static bool calendar_enabled(accounts_t *a, const char *acct, const char *cal)
{
	for (size_t i = 0; i < a->n; i++) {
		if (strcmp(a->e[i].name, acct)) continue;
		for (size_t c = 0; c < a->e[i].ncals; c++)
			if (a->e[i].cals[c].enabled && a->e[i].cals[c].name &&
			    !strcmp(a->e[i].cals[c].name, cal))
				return true;
	}
	return false;
}

/* Where an event goes when nobody said: the remembered one, or the only one.
 *
 * ⛔ AND STILL A REFUSAL WHEN NEITHER APPLIES. Picking the first of several
 * would put an appointment on a calendar the person did not name and may not
 * even look at — which they find out about when somebody does not turn up.
 *
 * ⚠ THE REMEMBERED ONE IS CHECKED, NOT TRUSTED. A calendar that has since been
 * switched off, renamed, or removed with its account is still a name in
 * settings.conf; writing to it would put the event somewhere nothing syncs. */
static bool default_calendar(accounts_t *a, char **acct, char **cal, char **err)
{
	char *saved = settings_get(DEFAULT_CAL_KEY);
	if (saved) {
		char *sa = NULL, *sc = NULL;
		if (split_in(saved, &sa, &sc) && calendar_enabled(a, sa, sc)) {
			free(saved);
			*acct = sa;
			*cal = sc;
			return true;
		}
		free(sa); free(sc); free(saved);
	}

	const char *fa = NULL, *fc = NULL;
	int n = 0;
	for (size_t i = 0; i < a->n; i++)
		for (size_t c = 0; c < a->e[i].ncals; c++)
			if (a->e[i].cals[c].enabled) {
				if (!n) { fa = a->e[i].name; fc = a->e[i].cals[c].name; }
				n++;
			}

	if (n == 1) { *acct = xstrdup(fa); *cal = xstrdup(fc); return true; }
	if (n == 0) {
		if (err) *err = xstrdup("no calendar is switched on — run: syn-cal discover <account>");
		return false;
	}
	if (err) *err = xasprintf("there are %d calendars switched on, so say which:\n"
	                          "  --in <account>/<calendar>   just this once\n"
	                          "  syn-cal default <account>/<calendar>   from now on", n);
	return false;
}

static bool split_in(const char *in, char **acct, char **cal)
{
	const char *slash = strrchr(in, '/');
	if (!slash || slash == in || !slash[1]) return false;
	size_t n = (size_t)(slash - in);
	char *a = xmalloc(n + 1);
	memcpy(a, in, n);
	a[n] = '\0';
	*acct = a;
	*cal = xstrdup(slash + 1);
	return true;
}

/* ── finding an event that already exists ───────────────────────────────── */

typedef struct { char *account; char *calendar; char *path; char *data; size_t len; } found_t;

static void found_free(found_t *f)
{
	free(f->account); free(f->calendar); free(f->path); free(f->data);
	memset(f, 0, sizeof *f);
}

static bool find_event(accounts_t *a, const char *uid, found_t *out, char **err)
{
	memset(out, 0, sizeof *out);
	for (size_t i = 0; i < a->n; i++) {
		for (size_t c = 0; c < a->e[i].ncals; c++) {
			if (!a->e[i].cals[c].enabled) continue;
			const char *acct = a->e[i].name, *cal = a->e[i].cals[c].name;
			size_t len = 0;
			char *data = local_read(acct, cal, uid, &len);
			if (!data) continue;
			out->account = xstrdup(acct);
			out->calendar = xstrdup(cal);
			out->data = data;
			out->len = len;
			return true;
		}
	}
	if (err) *err = xasprintf("no event here with the id '%s'", uid);
	return false;
}

/* ── the commands ───────────────────────────────────────────────────────── */

typedef struct {
	const char *title, *at, *dur, *remind, *where, *notes, *in;
	bool all_day, have_all_day;
} opts_t;

static bool take_opts(int argc, char **argv, opts_t *o, const char **positional)
{
	*positional = NULL;
	for (int i = 0; i < argc; i++) {
		const char *v = argv[i];
		const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (!strcmp(v, "--at") && next)          { o->at = next; i++; }
		else if (!strncmp(v, "--at=", 5))        { o->at = v + 5; }
		else if (!strcmp(v, "--for") && next)    { o->dur = next; i++; }
		else if (!strncmp(v, "--for=", 6))       { o->dur = v + 6; }
		else if (!strcmp(v, "--remind") && next) { o->remind = next; i++; }
		else if (!strncmp(v, "--remind=", 9))    { o->remind = v + 9; }
		else if (!strcmp(v, "--where") && next)  { o->where = next; i++; }
		else if (!strncmp(v, "--where=", 8))     { o->where = v + 8; }
		else if (!strcmp(v, "--notes") && next)  { o->notes = next; i++; }
		else if (!strncmp(v, "--notes=", 8))     { o->notes = v + 8; }
		else if (!strcmp(v, "--title") && next)  { o->title = next; i++; }
		else if (!strncmp(v, "--title=", 8))     { o->title = v + 8; }
		else if (!strcmp(v, "--in") && next)     { o->in = next; i++; }
		else if (!strncmp(v, "--in=", 5))        { o->in = v + 5; }
		else if (!strcmp(v, "--all-day"))        { o->all_day = true; o->have_all_day = true; }
		/* The global flags this command is reached before main() can read. */
		else if (!strcmp(v, "--rec"))            { g_out = OUT_REC; g_color = false; }
		else if (!strcmp(v, "--verbose") || !strcmp(v, "-v")) { g_verbose = true; }
		else if (v[0] == '-')                    { warn("unknown option '%s'", v); return false; }
		else if (!*positional)                   { *positional = v; }
		else { warn("too many things to call this event: '%s'", v); return false; }
	}
	return true;
}

/* `syn-cal default [<account>/<calendar>]` — where new events go. */
int cmd_default(const char *value)
{
	accounts_t a;
	accounts_load(&a);

	if (!value) {
		char *acct = NULL, *cal = NULL, *err = NULL;
		bool have = default_calendar(&a, &acct, &cal, &err);
		if (g_out == OUT_REC) {
			rec_header("account\tcalendar");
			if (have) {
				char *pa = pct_encode(acct, false), *pc = pct_encode(cal, false);
				rec_row("%s\t%s", pa, pc);
				free(pa); free(pc);
			}
		} else if (have) {
			printf("%s / %s\n", acct, cal);
		} else {
			warn("%s", err ? err : "no default calendar");
		}
		free(acct); free(cal); free(err);
		accounts_free(&a);
		return have ? 0 : 1;
	}

	char *acct = NULL, *cal = NULL;
	if (!split_in(value, &acct, &cal)) {
		warn("default wants <account>/<calendar>");
		accounts_free(&a);
		return 2;
	}

	/* ⛔ REFUSED IF IT IS NOT SWITCHED ON. Remembering a calendar nothing syncs
	 * would send every later event somewhere it never leaves this machine. */
	if (!calendar_enabled(&a, acct, cal)) {
		warn("'%s / %s' is not a calendar that is switched on — syn-cal calendars %s",
		     acct, cal, acct);
		free(acct); free(cal);
		accounts_free(&a);
		return 1;
	}

	char *err = NULL;
	bool ok = settings_set(DEFAULT_CAL_KEY, value, &err);
	if (!ok) { warn("%s", err ? err : "could not save it"); free(err); }
	else if (g_out != OUT_REC) printf("New events go to %s / %s.\n", acct, cal);

	free(acct); free(cal);
	accounts_free(&a);
	return ok ? 0 : 1;
}

int cmd_new(int argc, char **argv)
{
	opts_t o;
	memset(&o, 0, sizeof o);
	const char *pos = NULL;
	if (!take_opts(argc, argv, &o, &pos)) return 2;

	const char *title = o.title ? o.title : pos;
	if (!title || !*title) {
		warn("what is it called?  syn-cal new \"Dentist\" --at \"2026-09-21 13:15\"");
		return 2;
	}
	if (!o.at) {
		warn("when is it?  --at \"2026-09-21 13:15\", or --at 2026-09-21 for all day");
		return 2;
	}

	draft_t d;
	memset(&d, 0, sizeof d);
	bool dated_all_day = false;
	if (!parse_when(o.at, &d.start, &dated_all_day)) {
		warn("--at wants a date and time like \"2026-09-21 13:15\"");
		return 2;
	}
	d.all_day = o.have_all_day ? o.all_day : dated_all_day;

	long span = d.all_day ? 86400 : 3600;
	if (o.dur && !parse_duration(o.dur, &span)) {
		warn("--for wants a length like 30m, 1h or 1h30m");
		return 2;
	}
	d.end = d.start + span;

	if (o.remind && !parse_reminder(o.remind, &d.remind_min)) {
		warn("--remind wants a length like 15m or 1h, or 'none'");
		return 2;
	}

	d.summary = title;
	d.location = o.where;
	d.notes = o.notes;

	accounts_t a;
	accounts_load(&a);

	char *acct = NULL, *cal = NULL;
	if (o.in) {
		if (!split_in(o.in, &acct, &cal)) {
			warn("--in wants <account>/<calendar>");
			accounts_free(&a);
			return 2;
		}
	} else {
		char *err = NULL;
		if (!default_calendar(&a, &acct, &cal, &err)) {
			warn("%s", err);
			free(err);
			accounts_free(&a);
			return 1;
		}
	}

	size_t len = 0;
	char *ics = ics_compose(&d, &len);
	char *uid = ics_uid(ics, len);

	bool ok = local_write(acct, cal, uid, ics, len);
	if (!ok) warn("could not write the event into %s / %s", acct, cal);

	if (ok && g_out == OUT_REC) {
		rec_header("uid\taccount\tcalendar");
		char *u = pct_encode(uid, false), *ac = pct_encode(acct, false), *c = pct_encode(cal, false);
		rec_row("%s\t%s\t%s", u, ac, c);
		free(u); free(ac); free(c);
	} else if (ok) {
		/* ⚠ AND IT SAYS IT IS NOT ON THE SERVER YET. Written locally is not the
		 * same as saved, and a calendar that implies otherwise is lying about
		 * where somebody's appointment is. */
		printf("Added '%s' to %s / %s.\n\n  syn-cal sync   (it is only on this machine so far)\n",
		       title, acct, cal);
	}

	free(ics); free(uid); free(acct); free(cal);
	accounts_free(&a);
	return ok ? 0 : 1;
}

int cmd_edit(int argc, char **argv)
{
	opts_t o;
	memset(&o, 0, sizeof o);
	const char *uid = NULL;
	if (!take_opts(argc, argv, &o, &uid)) return 2;
	if (!uid) { warn("which event?  syn-cal edit <id> --title \"...\""); return 2; }

	accounts_t a;
	accounts_load(&a);

	found_t f;
	char *err = NULL;
	if (!find_event(&a, uid, &f, &err)) {
		warn("%s", err);
		free(err);
		accounts_free(&a);
		return 1;
	}

	char *unfolded = ics_unfold(f.data, f.len);

	/* ⛔ A REPEATING EVENT IS REFUSED, and says so. Rewriting one from the few
	 * fields this understands would flatten the rule and replace a whole series
	 * with a single occurrence — the same reason Microsoft's backend refuses
	 * them. Somebody's weekly meeting is not a thing to guess at. */
	char *rrule = ics_prop(unfolded, "RRULE");
	if (rrule) {
		warn("'%s' repeats, and syn-cal will not edit one occurrence of a series "
		     "without losing the rest of it. Change it where it was made.", uid);
		free(rrule); free(unfolded); found_free(&f); accounts_free(&a);
		return 1;
	}

	draft_t d;
	memset(&d, 0, sizeof d);
	d.uid = uid;

	/* Unescaped on the way in, because ics_compose escapes on the way out. */
	char *raw_sum = ics_prop(unfolded, "SUMMARY");
	char *raw_loc = ics_prop(unfolded, "LOCATION");
	char *raw_notes = ics_prop(unfolded, "DESCRIPTION");
	char *old_sum = raw_sum ? ics_unescape(raw_sum) : NULL;
	char *old_loc = raw_loc ? ics_unescape(raw_loc) : NULL;
	char *old_notes = raw_notes ? ics_unescape(raw_notes) : NULL;
	free(raw_sum); free(raw_loc); free(raw_notes);
	char *old_seq = ics_prop(unfolded, "SEQUENCE");

	/* The existing times, so an edit that does not mention them keeps them. */
	bool was_date = false;
	if (!read_dt(unfolded, "DTSTART", &d.start, &was_date)) {
		warn("'%s' has no start this can read, so editing it would move it", uid);
		free(old_sum); free(old_loc); free(old_notes); free(old_seq);
		free(unfolded); found_free(&f); accounts_free(&a);
		return 1;
	}
	d.all_day = was_date;
	if (!read_dt(unfolded, "DTEND", &d.end, NULL))
		d.end = d.start + (d.all_day ? 86400 : 3600);

	d.summary = o.title ? o.title : old_sum;
	d.location = o.where ? o.where : old_loc;
	d.notes = o.notes ? o.notes : old_notes;
	d.sequence = old_seq ? atoi(old_seq) + 1 : 1;

	bool changed = o.title || o.where || o.notes || o.at || o.dur || o.remind || o.have_all_day;
	if (!changed) {
		warn("nothing to change — pass --title, --at, --for, --remind, --where or --notes");
		free(old_sum); free(old_loc); free(old_notes); free(old_seq);
		free(unfolded); found_free(&f); accounts_free(&a);
		return 2;
	}

	long span = d.end > d.start ? (long)(d.end - d.start) : 3600;
	if (o.at) {
		bool dated_all_day = false;
		if (!parse_when(o.at, &d.start, &dated_all_day)) {
			warn("--at wants a date and time like \"2026-09-21 13:15\"");
			free(old_sum); free(old_loc); free(old_notes); free(old_seq);
			free(unfolded); found_free(&f); accounts_free(&a);
			return 2;
		}
		d.all_day = o.have_all_day ? o.all_day : dated_all_day;
	}
	if (o.have_all_day) d.all_day = o.all_day;
	if (o.dur && !parse_duration(o.dur, &span)) {
		warn("--for wants a length like 30m, 1h or 1h30m");
		free(old_sum); free(old_loc); free(old_notes); free(old_seq);
		free(unfolded); found_free(&f); accounts_free(&a);
		return 2;
	}
	d.end = d.start + span;

	/* ⚠ AN EXISTING REMINDER SURVIVES AN EDIT THAT DOES NOT MENTION IT. */
	char *trig = ics_prop(unfolded, "TRIGGER");
	if (trig) {
		int mins = 0;
		if (sscanf(trig, "-PT%dM", &mins) == 1) d.remind_min = mins;
		else if (sscanf(trig, "-PT%dH", &mins) == 1) d.remind_min = mins * 60;
		free(trig);
	}
	if (o.remind && !parse_reminder(o.remind, &d.remind_min)) {
		warn("--remind wants a length like 15m or 1h, or 'none'");
		free(old_sum); free(old_loc); free(old_notes); free(old_seq);
		free(unfolded); found_free(&f); accounts_free(&a);
		return 2;
	}

	size_t len = 0;
	char *ics = ics_compose(&d, &len);
	bool ok = local_write(f.account, f.calendar, uid, ics, len);
	if (!ok) warn("could not write the event back");
	else if (g_out != OUT_REC)
		printf("Changed '%s'.\n\n  syn-cal sync   (the server has the old one until then)\n",
		       d.summary ? d.summary : uid);

	free(ics);
	free(old_sum); free(old_loc); free(old_notes); free(old_seq);
	free(unfolded);
	found_free(&f);
	accounts_free(&a);
	return ok ? 0 : 1;
}

int cmd_delete(const char *uid)
{
	if (!uid || !*uid) { warn("which event?  syn-cal delete <id>"); return 2; }

	accounts_t a;
	accounts_load(&a);

	found_t f;
	char *err = NULL;
	if (!find_event(&a, uid, &f, &err)) {
		warn("%s", err);
		free(err);
		accounts_free(&a);
		return 1;
	}

	char *unfolded = ics_unfold(f.data, f.len);
	char *sum = ics_prop(unfolded, "SUMMARY");

	/* ⚠ REMOVED HERE, AND THE SYNC TAKES IT OFF THE SERVER. Deleting the file
	 * is the whole mechanism — the engine already knows a file that was in the
	 * index and is gone from disk is a deletion to push, which is the path a
	 * file removed by hand takes too. */
	bool ok = local_delete(f.account, f.calendar, uid);
	if (!ok) warn("could not remove the event");
	else if (g_out != OUT_REC)
		printf("Removed '%s'.\n\n  syn-cal sync   (it is still on the server until then)\n",
		       sum && *sum ? sum : uid);

	free(sum);
	free(unfolded);
	found_free(&f);
	accounts_free(&a);
	return ok ? 0 : 1;
}
