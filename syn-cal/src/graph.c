/* graph.c — Microsoft 365, which is not CalDAV and never will be again.
 *
 * Microsoft removed CalDAV. Outlook and 365 calendars are reachable only
 * through Graph, which speaks JSON about objects rather than iCalendar about
 * documents — so this backend is a translation as much as a transport.
 *
 * ── What the translation may and may not do ─────────────────────────────────
 *
 * ⛔ IT MAY LOSE NOTHING ON THE WAY BACK UP. Reading is forgiving: an event
 * this cannot fully express is still worth showing, and the worst outcome is a
 * calendar that is slightly less detailed than the web view. Writing is not.
 * An RRULE flattened on the way out DELETES a recurrence on somebody's work
 * calendar, and neither side would report anything wrong.
 *
 * So the rule is asymmetric, and deliberately so:
 *
 *   down   everything comes, and anything whose recurrence cannot be expressed
 *          arrives as a single occurrence carrying X-SYNCAL-LOSSY
 *
 *   up     single events are created and edited freely; anything carrying
 *          X-SYNCAL-LOSSY, and any recurring event, is REFUSED — the local file
 *          is left exactly as it is and the user is told which event and why
 *
 * A refusal is visible and recoverable. A silent flattening is neither.
 *
 * ── Times come back in UTC because we ask for them that way ─────────────────
 *
 * Graph returns a local time plus a Windows time-zone name — "W. Europe
 * Standard Time", which is not an IANA name and which libical cannot resolve
 * without the VTIMEZONE that is not being sent. `Prefer: outlook.timezone="UTC"`
 * makes the server do that conversion with its own tz data, which is the same
 * data that produced the appointment.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "graph.h"
#include "oauth.h"        /* json_str / json_num */

#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define GRAPH_BASE "https://graph.microsoft.com/v1.0"
#define PREFER_UTC "Prefer: outlook.timezone=\"UTC\""

typedef struct { char *url; http_auth_t auth; } graph_ctx_t;

/* ── small json helpers ─────────────────────────────────────────────────── */

static const char *obj_str(json_object *o, const char *key)
{
	json_object *v = NULL;
	if (!o || !json_object_object_get_ex(o, key, &v)) return NULL;
	return json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static json_object *obj_obj(json_object *o, const char *key)
{
	json_object *v = NULL;
	if (!o || !json_object_object_get_ex(o, key, &v)) return NULL;
	return json_object_is_type(v, json_type_object) ? v : NULL;
}

static int obj_int(json_object *o, const char *key, int fallback)
{
	json_object *v = NULL;
	if (!o || !json_object_object_get_ex(o, key, &v)) return fallback;
	return json_object_is_type(v, json_type_int) ? json_object_get_int(v) : fallback;
}

static bool obj_bool(json_object *o, const char *key)
{
	json_object *v = NULL;
	if (!o || !json_object_object_get_ex(o, key, &v)) return false;
	return json_object_is_type(v, json_type_boolean) && json_object_get_boolean(v);
}

/* "2026-09-10T14:00:00.0000000" → "20260910T140000Z".
 *
 * ⚠ THE Z IS OURS TO ADD, and only because Prefer: outlook.timezone="UTC" was
 * sent. Without that header Graph answers in the calendar's own zone and this
 * would relabel a local time as UTC — every appointment wrong by the offset,
 * which is the kind of error people notice as "the calendar is an hour out"
 * and never as "the client is lying about the zone". */
static bool graph_time(const char *in, bool date_only, char *out, size_t cap)
{
	int y, mo, d, h = 0, mi = 0, se = 0;
	if (!in) return false;
	if (sscanf(in, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3) return false;
	if (date_only) snprintf(out, cap, "%04d%02d%02d", y, mo, d);
	else snprintf(out, cap, "%04d%02d%02dT%02d%02d%02dZ", y, mo, d, h, mi, se);
	return true;
}

/* iCalendar escaping: a comma, a semicolon or a backslash in a summary ends the
 * value where it stands otherwise. */
static void ics_escape(buf_t *b, const char *s)
{
	for (const char *p = s ? s : ""; *p; p++) {
		switch (*p) {
		case '\\': buf_addstr(b, "\\\\"); break;
		case ';':  buf_addstr(b, "\\;");  break;
		case ',':  buf_addstr(b, "\\,");  break;
		case '\n': buf_addstr(b, "\\n");  break;
		default:   buf_add(b, p, 1);      break;
		}
	}
}

/* ── recurrence, Graph's shape to iCalendar's ───────────────────────────── */

static const char *DAY2[] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };

static const char *day_abbrev(const char *name)
{
	static const char *full[] = { "sunday", "monday", "tuesday", "wednesday",
	                              "thursday", "friday", "saturday" };
	for (int i = 0; i < 7; i++)
		if (name && strcasecmp(name, full[i]) == 0) return DAY2[i];
	return NULL;
}

static int index_num(const char *idx)
{
	if (!idx) return 0;
	if (!strcasecmp(idx, "first")) return 1;
	if (!strcasecmp(idx, "second")) return 2;
	if (!strcasecmp(idx, "third")) return 3;
	if (!strcasecmp(idx, "fourth")) return 4;
	if (!strcasecmp(idx, "last")) return -1;
	return 0;
}

/* Returns the RRULE value, or NULL when the pattern has no faithful iCalendar
 * form. NULL is not a failure: the caller marks the event lossy and shows it
 * once rather than showing it wrongly repeated. */
static char *graph_rrule(json_object *rec)
{
	json_object *pat = obj_obj(rec, "pattern");
	json_object *rng = obj_obj(rec, "range");
	if (!pat) return NULL;

	const char *type = obj_str(pat, "type");
	int interval = obj_int(pat, "interval", 1);
	if (!type || interval < 1) return NULL;

	buf_t r;
	buf_init(&r);

	/* The days list, for the two patterns that carry one. */
	char days[64] = "";
	json_object *dow = NULL;
	if (json_object_object_get_ex(pat, "daysOfWeek", &dow) &&
	    json_object_is_type(dow, json_type_array)) {
		for (size_t i = 0; i < json_object_array_length(dow); i++) {
			const char *ab = day_abbrev(json_object_get_string(
			                     json_object_array_get_idx(dow, i)));
			if (!ab) { buf_free(&r); return NULL; }
			if (*days) strncat(days, ",", sizeof days - strlen(days) - 1);
			strncat(days, ab, sizeof days - strlen(days) - 1);
		}
	}

	if (!strcasecmp(type, "daily")) {
		buf_addf(&r, "FREQ=DAILY;INTERVAL=%d", interval);
	} else if (!strcasecmp(type, "weekly")) {
		if (!*days) { buf_free(&r); return NULL; }
		buf_addf(&r, "FREQ=WEEKLY;INTERVAL=%d;BYDAY=%s", interval, days);
	} else if (!strcasecmp(type, "absoluteMonthly")) {
		buf_addf(&r, "FREQ=MONTHLY;INTERVAL=%d;BYMONTHDAY=%d",
		         interval, obj_int(pat, "dayOfMonth", 1));
	} else if (!strcasecmp(type, "relativeMonthly")) {
		int n = index_num(obj_str(pat, "index"));
		if (!n || !*days) { buf_free(&r); return NULL; }
		buf_addf(&r, "FREQ=MONTHLY;INTERVAL=%d;BYDAY=%d%s", interval, n, days);
	} else if (!strcasecmp(type, "absoluteYearly")) {
		buf_addf(&r, "FREQ=YEARLY;INTERVAL=%d;BYMONTH=%d;BYMONTHDAY=%d",
		         interval, obj_int(pat, "month", 1), obj_int(pat, "dayOfMonth", 1));
	} else if (!strcasecmp(type, "relativeYearly")) {
		int n = index_num(obj_str(pat, "index"));
		if (!n || !*days) { buf_free(&r); return NULL; }
		buf_addf(&r, "FREQ=YEARLY;INTERVAL=%d;BYMONTH=%d;BYDAY=%d%s",
		         interval, obj_int(pat, "month", 1), n, days);
	} else {
		buf_free(&r);
		return NULL;                 /* a pattern this does not know */
	}

	const char *rt = obj_str(rng, "type");
	if (rt && !strcasecmp(rt, "numbered")) {
		buf_addf(&r, ";COUNT=%d", obj_int(rng, "numberOfOccurrences", 1));
	} else if (rt && !strcasecmp(rt, "endDate")) {
		char until[32];
		if (graph_time(obj_str(rng, "endDate"), true, until, sizeof until))
			buf_addf(&r, ";UNTIL=%sT235959Z", until);
	}
	/* "noEnd" adds nothing, which is what an unbounded RRULE looks like. */

	return r.b;
}

/* ── one event, JSON to iCalendar ───────────────────────────────────────── */

char *graph_json_to_ics(const char *json, char **err)
{
	if (err) *err = NULL;
	json_object *o = json_tokener_parse(json);
	if (!o) { if (err) *err = xstrdup("the event was not JSON"); return NULL; }

	/* ⛔ iCalUId, NOT id. `id` is Graph's own handle and changes when an event
	 * is moved between calendars; `iCalUId` is the identity every other system
	 * knows the event by, which is exactly what a UID has to be for sync to
	 * match the same appointment on two sides. */
	const char *uid = obj_str(o, "iCalUId");
	if (!uid) uid = obj_str(o, "id");
	if (!uid) { json_object_put(o); if (err) *err = xstrdup("the event has no id"); return NULL; }

	bool all_day = obj_bool(o, "isAllDay");
	json_object *st = obj_obj(o, "start"), *en = obj_obj(o, "end");
	char ds[32] = "", de[32] = "";
	graph_time(obj_str(st, "dateTime"), all_day, ds, sizeof ds);
	graph_time(obj_str(en, "dateTime"), all_day, de, sizeof de);
	if (!*ds) { json_object_put(o); if (err) *err = xstrdup("the event has no start"); return NULL; }

	json_object *rec = obj_obj(o, "recurrence");
	char *rrule = rec ? graph_rrule(rec) : NULL;
	bool lossy = rec && !rrule;

	buf_t b;
	buf_init(&b);
	buf_addstr(&b, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//SynapseOS//syn-cal//EN\r\n");
	buf_addstr(&b, "BEGIN:VEVENT\r\n");
	buf_addf(&b, "UID:%s\r\n", uid);

	const char *changed = obj_str(o, "lastModifiedDateTime");
	char stamp[32];
	if (changed && graph_time(changed, false, stamp, sizeof stamp))
		buf_addf(&b, "DTSTAMP:%s\r\n", stamp);
	else
		buf_addstr(&b, "DTSTAMP:19700101T000000Z\r\n");

	if (all_day) {
		buf_addf(&b, "DTSTART;VALUE=DATE:%s\r\n", ds);
		if (*de) buf_addf(&b, "DTEND;VALUE=DATE:%s\r\n", de);
	} else {
		buf_addf(&b, "DTSTART:%s\r\n", ds);
		if (*de) buf_addf(&b, "DTEND:%s\r\n", de);
	}

	buf_addstr(&b, "SUMMARY:");
	ics_escape(&b, obj_str(o, "subject"));
	buf_addstr(&b, "\r\n");

	json_object *loc = obj_obj(o, "location");
	const char *locname = loc ? obj_str(loc, "displayName") : NULL;
	if (locname && *locname) {
		buf_addstr(&b, "LOCATION:");
		ics_escape(&b, locname);
		buf_addstr(&b, "\r\n");
	}

	if (rrule) buf_addf(&b, "RRULE:%s\r\n", rrule);

	/* ⛔ THE MARKER IS WHAT STOPS THIS GOING BACK UP WRONG. An event whose
	 * recurrence could not be expressed is shown ONCE — better than showing it
	 * repeating on the wrong days — and graph_ics_to_json refuses to upload
	 * anything carrying this, because doing so would replace a series on the
	 * server with the single occurrence we managed to render. */
	if (lossy)
		buf_addstr(&b, "X-SYNCAL-LOSSY:recurrence-not-representable\r\n");

	buf_addstr(&b, "END:VEVENT\r\nEND:VCALENDAR\r\n");
	free(rrule);
	json_object_put(o);
	return b.b;
}

/* ── one event, iCalendar to JSON ───────────────────────────────────────── */

static char *ics_unescape(const char *s)
{
	buf_t b;
	buf_init(&b);
	for (const char *p = s; p && *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			char c = *p == 'n' || *p == 'N' ? '\n' : *p;
			buf_add(&b, &c, 1);
		} else {
			buf_add(&b, p, 1);
		}
	}
	if (!b.b) b.b = xstrdup("");
	return b.b;
}

/* "20260910T140000Z" or "20260910" → Graph's dateTime. */
static bool ics_time(const char *v, char *out, size_t cap, bool *date_only)
{
	int y, mo, d, h = 0, mi = 0, se = 0;
	if (!v) return false;
	if (sscanf(v, "%4d%2d%2dT%2d%2d%2d", &y, &mo, &d, &h, &mi, &se) == 6) {
		if (date_only) *date_only = false;
		snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, h, mi, se);
		return true;
	}
	if (sscanf(v, "%4d%2d%2d", &y, &mo, &d) == 3) {
		if (date_only) *date_only = true;
		snprintf(out, cap, "%04d-%02d-%02dT00:00:00", y, mo, d);
		return true;
	}
	return false;
}

char *graph_ics_to_json(const char *ics, size_t len, char **err)
{
	if (err) *err = NULL;
	char *u = ics_unfold(ics, len);

	/* ⛔ THE TWO REFUSALS. Both leave the local file untouched and report which
	 * event could not go up — a refusal is visible and recoverable, and a
	 * silent flattening is neither. */
	char *lossy = ics_prop(u, "X-SYNCAL-LOSSY");
	if (lossy) {
		free(lossy); free(u);
		if (err) *err = xstrdup("this event's recurrence could not be read from "
		                        "Microsoft in the first place, so it is not sent "
		                        "back — edit it in Outlook instead");
		return NULL;
	}
	char *rrule = ics_prop(u, "RRULE");
	if (rrule) {
		free(rrule); free(u);
		if (err) *err = xstrdup("syn-cal does not write repeating events to "
		                        "Microsoft yet — the local copy is unchanged; "
		                        "edit the series in Outlook");
		return NULL;
	}

	char *dtstart = ics_prop(u, "DTSTART");
	char *dtend = ics_prop(u, "DTEND");
	char *summary = ics_prop(u, "SUMMARY");
	char *location = ics_prop(u, "LOCATION");
	char *uid = ics_prop(u, "UID");

	char s[40] = "", e[40] = "";
	bool date_only = false;
	if (!dtstart || !ics_time(dtstart, s, sizeof s, &date_only)) {
		free(dtstart); free(dtend); free(summary); free(location); free(uid); free(u);
		if (err) *err = xstrdup("the event has no start time this can read");
		return NULL;
	}
	if (!dtend || !ics_time(dtend, e, sizeof e, NULL)) snprintf(e, sizeof e, "%s", s);

	char *sum = ics_unescape(summary ? summary : "");
	char *loc = ics_unescape(location ? location : "");

	json_object *o = json_object_new_object();
	json_object_object_add(o, "subject", json_object_new_string(sum));

	json_object *jst = json_object_new_object();
	json_object_object_add(jst, "dateTime", json_object_new_string(s));
	json_object_object_add(jst, "timeZone", json_object_new_string("UTC"));
	json_object_object_add(o, "start", jst);

	json_object *jen = json_object_new_object();
	json_object_object_add(jen, "dateTime", json_object_new_string(e));
	json_object_object_add(jen, "timeZone", json_object_new_string("UTC"));
	json_object_object_add(o, "end", jen);

	json_object_object_add(o, "isAllDay", json_object_new_boolean(date_only));

	if (*loc) {
		json_object *jl = json_object_new_object();
		json_object_object_add(jl, "displayName", json_object_new_string(loc));
		json_object_object_add(o, "location", jl);
	}

	/* ⚠ SENT ON A CREATE SO THE TWO SIDES AGREE ON THE IDENTITY. Graph accepts
	 * iCalUId on POST and keeps it; without it the server invents one, the next
	 * listing does not recognise the event, and it is pulled back down as a
	 * second copy of what was just uploaded. */
	if (uid) json_object_object_add(o, "iCalUId", json_object_new_string(uid));

	const char *text = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
	char *out = xstrdup(text);
	json_object_put(o);

	free(sum); free(loc);
	free(dtstart); free(dtend); free(summary); free(location); free(uid); free(u);
	return out;
}

/* ── the remote ─────────────────────────────────────────────────────────── */

static char *graph_url(graph_ctx_t *g, const char *tail)
{
	return tail && *tail ? xasprintf("%s/%s", g->url, tail) : xstrdup(g->url);
}

static bool graph_call(graph_ctx_t *g, const char *method, const char *url,
                       const char *body, const char *if_match,
                       http_resp_t *resp, char **err)
{
	char *cond = if_match ? xasprintf("If-Match: %s", if_match) : NULL;
	const char *hdrs[3];
	size_t n = 0;
	hdrs[n++] = PREFER_UTC;
	if (body) hdrs[n++] = "Content-Type: application/json";
	if (cond) hdrs[n++] = cond;

	bool ok = http_do(method, url, hdrs, n, body, body ? strlen(body) : 0,
	                  &g->auth, resp, err);
	free(cond);
	return ok;
}

/* Graph's own error text, which is more use than anything invented here:
 * "ErrorItemNotFound" and "InvalidAuthenticationToken" send you to different
 * places, and "the server said no" sends you nowhere. */
static char *graph_error(http_resp_t *r)
{
	json_object *root = r->body.b ? json_tokener_parse(r->body.b) : NULL;
	json_object *e = root ? obj_obj(root, "error") : NULL;
	const char *code = e ? obj_str(e, "code") : NULL;
	const char *msg = e ? obj_str(e, "message") : NULL;
	char *out = xasprintf("%ld%s%s%s%s", r->status,
	                      code ? " " : "", code ? code : "",
	                      msg ? " — " : "", msg ? msg : "");
	if (root) json_object_put(root);
	return out;
}

bool graph_discover(const http_auth_t *auth, caldav_colls_t *out, char **err)
{
	memset(out, 0, sizeof *out);
	graph_ctx_t g = { (char *)GRAPH_BASE "/me/calendars", *auth };

	http_resp_t resp;
	if (!graph_call(&g, "GET", GRAPH_BASE "/me/calendars", NULL, NULL, &resp, err)) {
		http_resp_free(&resp);
		return false;
	}
	if (resp.status < 200 || resp.status >= 300) {
		if (err) *err = graph_error(&resp);
		http_resp_free(&resp);
		return false;
	}

	json_object *root = json_tokener_parse(resp.body.b);
	json_object *arr = root ? obj_obj(root, "value") : NULL;
	if (!arr) {
		json_object *v = NULL;
		if (root && json_object_object_get_ex(root, "value", &v) &&
		    json_object_is_type(v, json_type_array)) arr = v;
	}

	if (arr && json_object_is_type(arr, json_type_array)) {
		for (size_t i = 0; i < json_object_array_length(arr); i++) {
			json_object *c = json_object_array_get_idx(arr, i);
			const char *id = obj_str(c, "id");
			if (!id) continue;
			caldav_coll_t v;
			memset(&v, 0, sizeof v);
			v.url = xasprintf(GRAPH_BASE "/me/calendars/%s", id);
			const char *nm = obj_str(c, "name");
			v.name = xstrdup(nm ? nm : id);
			const char *col = obj_str(c, "hexColor");
			if (col && *col && strcmp(col, "auto") != 0) v.color = xstrdup(col);
			v.events = true;
			v.todos = false;      /* Graph keeps tasks somewhere else entirely */
			colls_add_public(out, v);
		}
	}
	if (root) json_object_put(root);
	http_resp_free(&resp);
	return out->n > 0;
}

static bool g_list(remote_t *r, remote_list_t *out, char **err)
{
	graph_ctx_t *g = r->ctx;
	/* ⚠ $select KEEPS THE ANSWER SMALL AND THE ETags PRESENT. Without it Graph
	 * sends the body, the attendees and the attachments of every event in the
	 * calendar, which on a work account is megabytes per sync.
	 *
	 * ⚠ AND OCCURRENCES ARE NOT ASKED FOR. /events returns series MASTERS and
	 * single events; /calendarView expands every occurrence, which would file
	 * one .ics per instance of a daily meeting and never stop growing. */
	char *url = xasprintf("%s/events?$select=id,iCalUId,lastModifiedDateTime&$top=500",
	                      g->url);
	http_resp_t resp;
	bool ok = graph_call(g, "GET", url, NULL, NULL, &resp, err);
	free(url);
	if (!ok) { http_resp_free(&resp); return false; }
	if (resp.status < 200 || resp.status >= 300) {
		if (err) *err = graph_error(&resp);
		http_resp_free(&resp);
		return false;
	}

	json_object *root = json_tokener_parse(resp.body.b);
	json_object *arr = NULL;
	if (root) json_object_object_get_ex(root, "value", &arr);
	if (arr && json_object_is_type(arr, json_type_array)) {
		for (size_t i = 0; i < json_object_array_length(arr); i++) {
			json_object *e = json_object_array_get_idx(arr, i);
			const char *id = obj_str(e, "id");
			if (!id) continue;
			/* Graph's ETag lives under @odata.etag. Where it is absent, the
			 * last-modified stamp is a serviceable stand-in: it changes when
			 * the event does, which is the only property the engine needs. */
			const char *tag = obj_str(e, "@odata.etag");
			const char *mod = obj_str(e, "lastModifiedDateTime");
			rlist_add(out, id, tag ? tag : (mod ? mod : ""));
		}
	}
	if (root) json_object_put(root);
	http_resp_free(&resp);
	return true;
}

static char *g_get(remote_t *r, const char *href, size_t *len, char **etag, char **err)
{
	graph_ctx_t *g = r->ctx;
	char *tail = xasprintf("events/%s", href);
	char *url = graph_url(g, tail);
	free(tail);

	http_resp_t resp;
	bool ok = graph_call(g, "GET", url, NULL, NULL, &resp, err);
	free(url);
	if (!ok) { http_resp_free(&resp); return NULL; }
	if (resp.status < 200 || resp.status >= 300) {
		if (err) *err = graph_error(&resp);
		http_resp_free(&resp);
		return NULL;
	}

	char *ics = graph_json_to_ics(resp.body.b, err);
	if (ics) {
		*len = strlen(ics);
		char *tag = json_str(resp.body.b, "@odata.etag");
		if (!tag) tag = json_str(resp.body.b, "lastModifiedDateTime");
		*etag = tag;
	}
	http_resp_free(&resp);
	return ics;
}

static bool g_put(remote_t *r, const char *href, const void *data, size_t len,
                  const char *if_match, char **new_etag, char **new_href,
                  bool *conflict, char **err)
{
	graph_ctx_t *g = r->ctx;
	*conflict = false;
	*new_href = NULL;

	char *json = graph_ics_to_json(data, len, err);
	if (!json) return false;          /* refused, with the reason already set */

	/* ⛔ POST TO THE COLLECTION FOR A CREATE, PATCH THE EVENT FOR AN EDIT.
	 * Graph has no upsert: a PATCH to an id that does not exist is a 404, and a
	 * POST of something that does exist is a duplicate. `if_match` is the
	 * engine saying which of the two it believes this is. */
	bool creating = (if_match == NULL);
	char *url;
	if (creating) {
		url = graph_url(g, "events");
	} else {
		char *tail = xasprintf("events/%s", href);
		url = graph_url(g, tail);
		free(tail);
	}

	http_resp_t resp;
	bool ok = graph_call(g, creating ? "POST" : "PATCH", url, json,
	                     creating ? NULL : if_match, &resp, err);
	free(url);
	free(json);
	if (!ok) { http_resp_free(&resp); return false; }

	if (resp.status == 412 || resp.status == 409) {
		*conflict = true;
		http_resp_free(&resp);
		return true;
	}
	if (resp.status < 200 || resp.status >= 300) {
		if (err) *err = graph_error(&resp);
		http_resp_free(&resp);
		return false;
	}

	/* ⛔ THE id COMES BACK FROM THE CREATE, and it is the only chance to learn
	 * it. Recording the guess instead means the next listing finds an item the
	 * index has never heard of, pulls it down as new, and uploads the local
	 * copy again — a duplicate per sync, for ever. */
	if (creating) *new_href = json_str(resp.body.b, "id");

	char *tag = json_str(resp.body.b, "@odata.etag");
	if (!tag) tag = json_str(resp.body.b, "lastModifiedDateTime");
	*new_etag = tag;

	http_resp_free(&resp);
	return true;
}

static bool g_del(remote_t *r, const char *href, const char *if_match,
                  bool *conflict, char **err)
{
	graph_ctx_t *g = r->ctx;
	*conflict = false;
	char *tail = xasprintf("events/%s", href);
	char *url = graph_url(g, tail);
	free(tail);

	http_resp_t resp;
	bool ok = graph_call(g, "DELETE", url, NULL, if_match, &resp, err);
	free(url);
	if (!ok) { http_resp_free(&resp); return false; }

	if (resp.status == 412) { *conflict = true; http_resp_free(&resp); return true; }
	/* 404 is success: it is gone, which is what was asked for. */
	bool good = (resp.status >= 200 && resp.status < 300) || resp.status == 404;
	if (!good && err) *err = graph_error(&resp);
	http_resp_free(&resp);
	return good;
}

/* ⛔ NULL, BECAUSE GRAPH ASSIGNS THE id. The engine treats that as "you cannot
 * know until you have asked", and g_put answers through new_href. */
static char *g_href_for(remote_t *r, const char *uid)
{
	(void)r; (void)uid;
	return NULL;
}

remote_t *graph_remote(const char *calendar_url, const http_auth_t *auth)
{
	graph_ctx_t *g = xmalloc(sizeof *g);
	memset(g, 0, sizeof *g);
	g->url = xstrdup(calendar_url);
	g->auth.bearer = auth->bearer ? xstrdup(auth->bearer) : NULL;
	g->auth.timeout_s = auth->timeout_s;

	remote_t *r = xmalloc(sizeof *r);
	r->list = g_list; r->get = g_get; r->put = g_put;
	r->del = g_del; r->href_for = g_href_for; r->ctx = g;
	return r;
}

void graph_remote_free(remote_t *r)
{
	if (!r) return;
	graph_ctx_t *g = r->ctx;
	free(g->url);
	if (g->auth.bearer) { memset(g->auth.bearer, 0, strlen(g->auth.bearer)); free(g->auth.bearer); }
	free(g);
	free(r);
}
