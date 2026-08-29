/* sync_test.c — the engine, driven through every combination that can lose data.
 *
 * The remote is a list in memory. That is the point: the states worth testing
 * are the awkward ones — deleted here while edited there, a 412 landing between
 * the listing and the upload — and arranging those against a real server means
 * writing a server that can be driven into them. Three lines here instead.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
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

/* ── a remote made of a list ────────────────────────────────────────────── */

typedef struct { char *href, *etag, *data; bool gone; } fake_item_t;
typedef struct {
	fake_item_t it[64];
	size_t n;
	int etag_seq;
	/* When set, the next put on this href answers 412 — the "somebody wrote
	 * while you were deciding" case, which cannot otherwise be arranged. */
	char *fail_put_once;
} fake_t;

static fake_item_t *fake_get(fake_t *f, const char *href)
{
	for (size_t i = 0; i < f->n; i++)
		if (!f->it[i].gone && strcmp(f->it[i].href, href) == 0) return &f->it[i];
	return NULL;
}

static void fake_set(fake_t *f, const char *href, const char *data)
{
	fake_item_t *e = fake_get(f, href);
	if (!e) { e = &f->it[f->n++]; memset(e, 0, sizeof *e); e->href = xstrdup(href); }
	free(e->data); e->data = xstrdup(data);
	free(e->etag); e->etag = xasprintf("\"v%d\"", ++f->etag_seq);
	e->gone = false;
}

static bool f_list(remote_t *r, remote_list_t *out, char **err)
{
	(void)err;
	fake_t *f = r->ctx;
	for (size_t i = 0; i < f->n; i++)
		if (!f->it[i].gone) rlist_add(out, f->it[i].href, f->it[i].etag);
	return true;
}

static char *f_get(remote_t *r, const char *href, size_t *len, char **etag, char **err)
{
	(void)err;
	fake_item_t *e = fake_get(r->ctx, href);
	if (!e) return NULL;
	*len = strlen(e->data);
	*etag = xstrdup(e->etag);
	return xstrdup(e->data);
}

static bool f_put(remote_t *r, const char *href, const void *data, size_t len,
                  const char *if_match, char **new_etag, char **new_href,
                  bool *conflict, char **err)
{
	(void)err;
	fake_t *f = r->ctx;
	*conflict = false;
	*new_href = NULL;          /* this fake lets the client choose, like CalDAV */

	if (f->fail_put_once && strcmp(f->fail_put_once, href) == 0) {
		free(f->fail_put_once); f->fail_put_once = NULL;
		*conflict = true;
		return true;
	}

	fake_item_t *e = fake_get(f, href);
	/* If-Match against what we actually hold — the check a real server makes,
	 * and the whole reason the engine sends the header. */
	if (if_match && (!e || strcmp(e->etag, if_match) != 0)) { *conflict = true; return true; }
	if (!if_match && e) { *conflict = true; return true; }   /* If-None-Match: * */

	char *copy = xmalloc(len + 1);
	memcpy(copy, data, len); copy[len] = '\0';
	fake_set(f, href, copy);
	free(copy);
	*new_etag = xstrdup(fake_get(f, href)->etag);
	return true;
}

static bool f_del(remote_t *r, const char *href, const char *if_match,
                  bool *conflict, char **err)
{
	(void)err;
	*conflict = false;
	fake_item_t *e = fake_get(r->ctx, href);
	if (!e) return true;
	if (if_match && strcmp(e->etag, if_match) != 0) { *conflict = true; return true; }
	e->gone = true;
	return true;
}

/* ⚠ THE SAME NAMING THE REAL CLIENT USES. A fake that addressed items by the
 * percent-encoded UID would have kept passing after the live test found that a
 * %2F in an href is refused by real servers — the fake would have been
 * modelling the bug rather than the protocol. */
static char *f_href_for(remote_t *r, const char *uid)
{
	(void)r;
	char *name = ics_safe_name(uid);
	char *h = xasprintf("/cal/%s.ics", name);
	free(name);
	return h;
}

/* ── fixtures ───────────────────────────────────────────────────────────── */

/* The href the engine will choose for a UID, so assertions name it the same way
 * the code does rather than restating the rule and drifting from it. */
static char *href_of(const char *uid)
{
	static char buf[256];
	char *name = ics_safe_name(uid);
	snprintf(buf, sizeof buf, "/cal/%s.ics", name);
	free(name);
	return buf;
}

static char *ev(const char *uid, const char *summary)
{
	return xasprintf("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:%s\r\n"
	                 "SUMMARY:%s\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n", uid, summary);
}

static char *local_summary(const char *uid)
{
	size_t len = 0;
	char *d = local_read("a", "c", uid, &len);
	if (!d) return NULL;
	char *u = ics_unfold(d, len);
	char *s = ics_prop(u, "SUMMARY");
	free(u); free(d);
	return s;
}

static bool run(fake_t *f, conflict_t policy, sync_stats_t *st)
{
	remote_t r = { f_list, f_get, f_put, f_del, f_href_for, f };
	sync_opts_t o = { "a", "c", policy, false };
	char *err = NULL;
	bool ok = sync_run(&r, &o, st, &err);
	if (err) { printf("        (error: %s)\n", err); free(err); }
	return ok;
}

int main(void)
{
	char tmpl[] = "/tmp/syncal-sync-XXXXXX";
	char *root = mkdtemp(tmpl);
	if (!root) { perror("mkdtemp"); return 2; }
	setenv("SYNCAL_HOME", root, 1);

	fake_t f;
	memset(&f, 0, sizeof f);
	sync_stats_t st;

	/* ── an empty sync is not an error ──────────────────────────────────── */
	ok_("an empty calendar syncs cleanly", run(&f, CONFLICT_KEEP_BOTH, &st));
	ok_("…and does nothing", st.pulled_new == 0 && st.pushed_new == 0 && st.errors == 0);

	/* ── down: the server has something new ─────────────────────────────── */
	char *e1 = ev("one@x", "Standup");
	fake_set(&f, "/cal/one.ics", e1);
	run(&f, CONFLICT_KEEP_BOTH, &st);
	char *s = local_summary("one@x");
	ok_("a new remote event lands on disk", s && strcmp(s, "Standup") == 0);
	free(s);
	ok_("…and is counted as pulled", st.pulled_new == 1);

	/* Nothing changed since: a second run must be a no-op, not a re-upload. */
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("a second sync with nothing changed does nothing",
	    st.pulled_new == 0 && st.pulled_changed == 0 &&
	    st.pushed_new == 0 && st.pushed_changed == 0);

	/* ── up: the disk has something new ─────────────────────────────────── */
	char *e2 = ev("two@x", "Dentist");
	local_write("a", "c", "two@x", e2, strlen(e2));
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("a new local event reaches the server", fake_get(&f, href_of("two@x")) != NULL);
	ok_("…and is counted as pushed", st.pushed_new == 1);

	/* ── down: the server changed it ────────────────────────────────────── */
	char *e1b = ev("one@x", "Standup moved");
	fake_set(&f, "/cal/one.ics", e1b);
	run(&f, CONFLICT_KEEP_BOTH, &st);
	s = local_summary("one@x");
	ok_("a remote edit reaches the disk", s && strcmp(s, "Standup moved") == 0);
	free(s);
	ok_("…counted as a change, not as new", st.pulled_changed == 1 && st.pulled_new == 0);

	/* ── up: the disk changed it ────────────────────────────────────────── */
	char *e2b = ev("two@x", "Dentist rescheduled");
	local_write("a", "c", "two@x", e2b, strlen(e2b));
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("a local edit reaches the server",
	    strstr(fake_get(&f, href_of("two@x"))->data, "Dentist rescheduled") != NULL);
	ok_("…counted as a change", st.pushed_changed == 1);

	/* ── both changed: keep both, lose nothing ──────────────────────────── */
	char *mine = ev("one@x", "MY version");
	char *theirs = ev("one@x", "THEIR version");
	local_write("a", "c", "one@x", mine, strlen(mine));
	fake_set(&f, "/cal/one.ics", theirs);
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("a double edit is reported as a conflict", st.conflicts == 1);
	s = local_summary("one@x");
	ok_("…the server's version keeps the UID", s && strcmp(s, "THEIR version") == 0);
	free(s);

	/* ⛔ AND MINE IS STILL SOMEWHERE. This is the assertion the whole engine
	 * exists for: an edit is never lost without saying so. */
	local_list_t l;
	local_scan("a", "c", &l);
	bool kept = false;
	for (size_t i = 0; i < l.n; i++) {
		char *sm = local_summary(l.e[i].uid);
		if (sm && strcmp(sm, "MY version") == 0) kept = true;
		free(sm);
	}
	ok_("…and MY version survives under a new UID", kept);
	local_free(&l);

	/* ── remote-wins discards it, and only when asked ───────────────────── */
	char *mine2 = ev("two@x", "MY second");
	char *theirs2 = ev("two@x", "THEIR second");
	local_write("a", "c", "two@x", mine2, strlen(mine2));
	fake_set(&f, "/cal/two%40x.ics", theirs2);
	run(&f, CONFLICT_REMOTE_WINS, &st);
	s = local_summary("two@x");
	ok_("remote-wins takes the server's copy", s && strcmp(s, "THEIR second") == 0);
	free(s);
	local_scan("a", "c", &l);
	bool duplicated = false;
	for (size_t i = 0; i < l.n; i++) {
		char *sm = local_summary(l.e[i].uid);
		if (sm && strcmp(sm, "MY second") == 0) duplicated = true;
		free(sm);
	}
	ok_("…and does NOT leave a copy, because that is what was asked for", !duplicated);
	local_free(&l);

	/* ── deletion, both directions ──────────────────────────────────────── */
	char *e3 = ev("three@x", "Gone soon");
	local_write("a", "c", "three@x", e3, strlen(e3));
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("a third event syncs up", fake_get(&f, href_of("three@x")) != NULL);

	local_delete("a", "c", "three@x");
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("deleting it here deletes it there", fake_get(&f, href_of("three@x")) == NULL);
	ok_("…counted as a pushed deletion", st.pushed_deleted == 1);

	char *e4 = ev("four@x", "Server will drop this");
	fake_set(&f, "/cal/four.ics", e4);
	run(&f, CONFLICT_KEEP_BOTH, &st);
	fake_get(&f, "/cal/four.ics")->gone = true;
	run(&f, CONFLICT_KEEP_BOTH, &st);
	size_t len = 0;
	char *gone = local_read("a", "c", "four@x", &len);
	ok_("deleting it there deletes it here", gone == NULL);
	ok_("…counted as a pulled deletion", st.pulled_deleted == 1);
	free(gone);

	/* ⛔ DELETED HERE, EDITED THERE — the deletion must NOT win. It destroys;
	 * the edit does not. */
	char *e5 = ev("five@x", "original");
	fake_set(&f, "/cal/five.ics", e5);
	run(&f, CONFLICT_KEEP_BOTH, &st);
	local_delete("a", "c", "five@x");
	char *e5b = ev("five@x", "edited on the server");
	fake_set(&f, "/cal/five.ics", e5b);
	run(&f, CONFLICT_KEEP_BOTH, &st);
	s = local_summary("five@x");
	ok_("deleted here but edited there: the edit wins and it comes back",
	    s && strcmp(s, "edited on the server") == 0);
	free(s);

	/* ⛔ …AND THE MIRROR. Deleted there, edited here: the edit goes back up. */
	char *e6 = ev("six@x", "original");
	local_write("a", "c", "six@x", e6, strlen(e6));
	run(&f, CONFLICT_KEEP_BOTH, &st);
	fake_get(&f, href_of("six@x"))->gone = true;
	char *e6b = ev("six@x", "edited here");
	local_write("a", "c", "six@x", e6b, strlen(e6b));
	run(&f, CONFLICT_KEEP_BOTH, &st);
	fake_item_t *back = fake_get(&f, href_of("six@x"));
	ok_("deleted there but edited here: it is put back on the server",
	    back && strstr(back->data, "edited here") != NULL);

	/* ── a 412 between deciding and writing ─────────────────────────────── */
	char *e7 = ev("seven@x", "v1");
	local_write("a", "c", "seven@x", e7, strlen(e7));
	run(&f, CONFLICT_KEEP_BOTH, &st);
	char *e7b = ev("seven@x", "v2 from here");
	local_write("a", "c", "seven@x", e7b, strlen(e7b));
	f.fail_put_once = xstrdup(href_of("seven@x"));
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("a 412 is counted as a conflict, not an error", st.conflicts >= 1 && st.errors == 0);
	s = local_summary("seven@x");
	ok_("…and the local edit is still on disk, untouched",
	    s && strcmp(s, "v2 from here") == 0);
	free(s);
	/* The next run must resolve it rather than looping. */
	run(&f, CONFLICT_KEEP_BOTH, &st);
	ok_("…and the next run pushes it",
	    strstr(fake_get(&f, href_of("seven@x"))->data, "v2 from here") != NULL);

	/* ── dry run decides everything and changes nothing ─────────────────── */
	char *e8 = ev("eight@x", "not really");
	local_write("a", "c", "eight@x", e8, strlen(e8));
	{
		remote_t r = { f_list, f_get, f_put, f_del, f_href_for, &f };
		sync_opts_t o = { "a", "c", CONFLICT_KEEP_BOTH, true };
		char *err = NULL;
		sync_run(&r, &o, &st, &err);
		free(err);
	}
	ok_("a dry run reports what it would push", st.pushed_new == 1);
	ok_("…and pushes nothing", fake_get(&f, href_of("eight@x")) == NULL);

	printf("\n%d/%d passed\n", total - fails, total);
	free(e1); free(e1b); free(e2); free(e2b); free(e3); free(e4);
	free(e5); free(e5b); free(e6); free(e6b); free(e7); free(e7b); free(e8);
	free(mine); free(theirs); free(mine2); free(theirs2);
	char *rm = xasprintf("rm -rf '%s'", root);
	if (system(rm) != 0) fprintf(stderr, "note: could not clean %s\n", root);
	free(rm);
	return fails ? 1 : 0;
}
