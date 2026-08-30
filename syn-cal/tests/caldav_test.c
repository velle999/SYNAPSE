/* caldav_test.c — the client against a real CalDAV server.
 *
 * ⛔ THE IN-MEMORY REMOTE IN sync_test.c PROVES THE ALGORITHM AND NOTHING ELSE.
 * It answers exactly what this code expects, which is the one thing a real
 * server will not reliably do: namespace prefixes differ, Depth: 1 includes the
 * collection itself, ETags come back quoted or weak or not at all, and a PUT
 * may answer 201 or 204. Every one of those has to be met by something that did
 * not write the client.
 *
 * Radicale is that something. Started by tests/caldav_test.sh on a loopback
 * port with its own storage; this binary is handed the base URL.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "caldav.h"
#include "sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails = 0, total = 0;
static void ok_(const char *what, bool cond)
{
	total++;
	if (cond) printf("  ok    %s\n", what);
	else { printf("  FAIL  %s\n", what); fails++; }
}

static char *ev(const char *uid, const char *summary)
{
	return xasprintf("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//syn-cal//test//EN\r\n"
	                 "BEGIN:VEVENT\r\nUID:%s\r\nDTSTAMP:20260101T000000Z\r\n"
	                 "DTSTART:20260901T090000Z\r\nDTEND:20260901T093000Z\r\n"
	                 "SUMMARY:%s\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n", uid, summary);
}

static char *summary_of(const char *uid)
{
	size_t len = 0;
	char *d = local_read("live", "cal", uid, &len);
	if (!d) return NULL;
	char *u = ics_unfold(d, len);
	char *s = ics_prop(u, "SUMMARY");
	free(u); free(d);
	return s;
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: caldav_test <base-url>\n"); return 2; }
	const char *base = argv[1];

	char tmpl[] = "/tmp/syncal-live-XXXXXX";
	char *root = mkdtemp(tmpl);
	if (!root) { perror("mkdtemp"); return 2; }
	setenv("SYNCAL_HOME", root, 1);

	/* ── what a failure says, before anything talks to a server ─────────── */
	//
	// ⛔ THE REASON IS IN THE BODY. Google answered a 403 naming the switch that
	// was off and the URL that turns it on, and syn-cal printed the status and
	// threw the rest away — so a project with the CalDAV API disabled looked
	// exactly like a bad token.
	{
		http_resp_t r;
		memset(&r, 0, sizeof r);

		r.body.b = (char *)"<?xml version=\"1.0\"?><errors xmlns=\"http://schemas.google.com/g/2005\">"
		                   "<error><domain>GData</domain><code>accessNotConfigured</code>"
		                   "<internalReason>CalDAV API has not been used in project 1234 "
		                   "before or it is disabled.</internalReason></error></errors>";
		char *w = server_said_public(&r);
		ok_("a GData body gives up its reason",
		    strstr(w, "CalDAV API has not been used") != NULL);
		free(w);

		/* ⚠ WITH A NAMESPACE PREFIX TOO. Every real DAV server sends one, and
		 * which letter it uses is its own business — D:, d:, or none. */
		r.body.b = (char *)"<D:error xmlns:D=\"DAV:\">"
		                   "<D:responsedescription>Quota exceeded</D:responsedescription></D:error>";
		w = server_said_public(&r);
		ok_("…and so does a prefixed DAV one", strcmp(w, " — Quota exceeded") == 0);
		free(w);

		r.body.b = (char *)"Forbidden: this calendar is read-only";
		w = server_said_public(&r);
		ok_("a short plain-text body is the server talking too",
		    strstr(w, "read-only") != NULL);
		free(w);

		/* ⛔ AND A PAGE IS NOT A REASON. An HTML error page appended to a warn()
		 * scrolls the actual message off the screen. */
		r.body.b = (char *)"<html><head><title>Error</title></head><body><p>oh dear</p></body></html>";
		w = server_said_public(&r);
		ok_("…but an HTML page is not repeated", strcmp(w, "") == 0);
		free(w);

		r.body.b = NULL;
		w = server_said_public(&r);
		ok_("…and an empty body adds nothing", strcmp(w, "") == 0);
		free(w);
	}

	http_global_init();
	/* The credentials tests/caldav_test.sh gives radicale. Basic auth, which is
	   what every CalDAV provider except Google and Microsoft actually uses. */
	http_auth_t auth = { (char *)"tester", (char *)"secret", NULL, 20, false };

	/* ── make a calendar to work in ─────────────────────────────────────── */

	char *coll = xasprintf("%s/tester/work/", base);
	const char *mk_hdr[] = { "Content-Type: application/xml; charset=utf-8" };
	const char *mk_body =
	    "<?xml version=\"1.0\"?><c:mkcalendar xmlns:d=\"DAV:\" "
	    "xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:set><d:prop>"
	    "<d:displayname>Work</d:displayname></d:prop></d:set></c:mkcalendar>";
	http_resp_t resp;
	char *err = NULL;
	bool made = http_do("MKCALENDAR", coll, mk_hdr, 1, mk_body, strlen(mk_body),
	                    &auth, &resp, &err);
	ok_("a calendar can be created on the server",
	    made && (resp.status == 201 || resp.status == 405));
	if (!made) printf("        (%s)\n", err ? err : "no reason given");
	free(err); err = NULL;
	http_resp_free(&resp);

	/* ── discovery, from the server root ────────────────────────────────── */

	caldav_colls_t colls;
	bool found = caldav_discover(base, &auth, &colls, &err);
	ok_("discovery walks well-known → principal → home → calendars", found);
	if (!found) printf("        (%s)\n", err ? err : "no reason given");
	free(err); err = NULL;

	bool saw_work = false;
	for (size_t i = 0; i < colls.n; i++)
		if (colls.e[i].name && strcmp(colls.e[i].name, "Work") == 0) saw_work = true;
	ok_("…and finds the calendar by its displayname", saw_work);
	caldav_colls_free(&colls);

	/* ⚠ AND FROM A COLLECTION URL PASTED STRAIGHT IN, which is what a provider's
	 * help page gives you and therefore what people actually paste. */
	memset(&colls, 0, sizeof colls);
	ok_("discovery also accepts a collection URL directly",
	    caldav_discover(coll, &auth, &colls, &err) && colls.n >= 1);
	free(err); err = NULL;
	caldav_colls_free(&colls);

	/* ── sync, both ways, against the real thing ────────────────────────── */

	remote_t *r = caldav_remote(coll, &auth);
	sync_opts_t o = { "live", "cal", CONFLICT_KEEP_BOTH, false };
	sync_stats_t st;

	ok_("an empty calendar syncs", sync_run(r, &o, &st, &err) && st.errors == 0);
	free(err); err = NULL;

	/* up */
	char *e1 = ev("live-1@syn-cal", "Design review");
	local_write("live", "cal", "live-1@syn-cal", e1, strlen(e1));
	ok_("a local event uploads", sync_run(r, &o, &st, &err) && st.pushed_new == 1 && st.errors == 0);
	if (err) printf("        (%s)\n", err);
	free(err); err = NULL;

	/* The server really has it — asked of the server, not of our own index. */
	remote_list_t rl;
	rlist_init(&rl);
	ok_("…and the server lists it back", r->list(r, &rl, &err) && rl.n == 1);
	free(err); err = NULL;
	ok_("…with an ETag", rl.n == 1 && rl.e[0].etag && *rl.e[0].etag);

	/* ⛔ THE COLLECTION ITSELF MUST NOT APPEAR AS AN ITEM. Depth: 1 includes it,
	 * and a client that does not filter it tries to GET the calendar as an
	 * event on every single sync. */
	bool self_listed = false;
	for (size_t i = 0; i < rl.n; i++) {
		size_t hl = strlen(rl.e[i].href);
		if (hl && rl.e[i].href[hl - 1] == '/') self_listed = true;
	}
	ok_("…and the collection itself is not listed as an event", !self_listed);
	rlist_free(&rl);

	/* down: change it on the server behind our back */
	char *e1b = ev("live-1@syn-cal", "Design review MOVED");
	rlist_init(&rl);
	r->list(r, &rl, NULL);
	char *href = rl.n ? xstrdup(rl.e[0].href) : NULL;
	char *etag = (rl.n && rl.e[0].etag) ? xstrdup(rl.e[0].etag) : NULL;
	rlist_free(&rl);

	bool conflict = false;
	char *ne = NULL, *nh = NULL;
	ok_("an If-Match upload with the right ETag succeeds",
	    href && r->put(r, href, e1b, strlen(e1b), etag, &ne, &nh, &conflict, &err) && !conflict);
	free(err); err = NULL; free(ne); ne = NULL;

	ok_("the change comes down", sync_run(r, &o, &st, &err) && st.pulled_changed == 1);
	free(err); err = NULL;
	char *s = summary_of("live-1@syn-cal");
	ok_("…and the disk has the server's text", s && strcmp(s, "Design review MOVED") == 0);
	free(s);

	/* ⛔ A STALE If-Match MUST BE REFUSED BY THE SERVER, or every guarantee in
	 * sync.c rests on a header the server ignores. This is the one assertion
	 * that cannot be made against a fake. */
	conflict = false;
	ok_("a stale If-Match is refused with a conflict",
	    href && r->put(r, href, e1b, strlen(e1b), "\"definitely-not-the-etag\"",
	                   &ne, &nh, &conflict, &err) && conflict);
	free(err); err = NULL; free(ne); ne = NULL;

	/* delete, upward */
	local_delete("live", "cal", "live-1@syn-cal");
	ok_("deleting locally deletes on the server",
	    sync_run(r, &o, &st, &err) && st.pushed_deleted == 1 && st.errors == 0);
	free(err); err = NULL;

	rlist_init(&rl);
	r->list(r, &rl, NULL);
	ok_("…and the server has nothing left", rl.n == 0);
	rlist_free(&rl);

	/* An event whose UID needs encoding before it can be a path. */
	char *e2 = ev("weird/uid+with spaces@x", "Awkward identity");
	local_write("live", "cal", "weird/uid+with spaces@x", e2, strlen(e2));
	ok_("an event whose UID contains a slash and a space uploads",
	    sync_run(r, &o, &st, &err) && st.pushed_new == 1 && st.errors == 0);
	if (err) printf("        (%s)\n", err);
	free(err); err = NULL;

	/* And comes back intact through a fresh store — a full download from cold. */
	char *fresh = xasprintf("%s/fresh", root);
	setenv("SYNCAL_HOME", fresh, 1);
	ok_("a cold client downloads it", sync_run(r, &o, &st, &err) && st.pulled_new == 1);
	free(err); err = NULL;
	s = summary_of("weird/uid+with spaces@x");
	ok_("…under the same UID, with the same text", s && strcmp(s, "Awkward identity") == 0);
	free(s);
	free(fresh);

	printf("\n%d/%d passed\n", total - fails, total);

	free(href); free(etag); free(e1); free(e1b); free(e2); free(coll);
	caldav_remote_free(r);
	http_global_cleanup();
	char *rm = xasprintf("rm -rf '%s'", root);
	if (system(rm) != 0) fprintf(stderr, "note: could not clean %s\n", root);
	free(rm);
	return fails ? 1 : 0;
}
