/* store_test.c — the vdir and the index, against a scratch store.
 *
 * ⛔ SYNCAL_HOME IS SET BEFORE ANYTHING ELSE HAPPENS. A calendar's test suite
 * that could reach the real store would be the most dangerous file in this
 * component; every path in here is under a mkdtemp that main() removes.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syncal.h"
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0, total = 0;

static void ok_(const char *what, bool cond)
{
	total++;
	if (cond) printf("  ok    %s\n", what);
	else { printf("  FAIL  %s\n", what); fails++; }
}

static void eq(const char *what, const char *got, const char *want)
{
	ok_(what, (!got && !want) || (got && want && strcmp(got, want) == 0));
	if (got && want && strcmp(got, want) != 0)
		printf("        got  [%s]\n        want [%s]\n", got, want);
}

static const char *EV1 =
    "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:simple@example.org\r\n"
    "SUMMARY:Standup\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

/* ⛔ A REAL GOOGLE UID SHAPE. Slashes and an '@' in an identity that is about to
 * become a filename is not a contrived case — it is what the biggest provider
 * issues, and writing it raw either escapes the directory or truncates. */
static const char *EV2 =
    "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:a/b/../c@google.com\r\n"
    "SUMMARY:Awkward\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

int main(void)
{
	char tmpl[] = "/tmp/syncal-store-XXXXXX";
	char *root = mkdtemp(tmpl);
	if (!root) { perror("mkdtemp"); return 2; }
	setenv("SYNCAL_HOME", root, 1);

	/* ── writing and reading back ───────────────────────────────────────── */

	ok_("an event writes", local_write("acct", "cal", "simple@example.org", EV1, strlen(EV1)));

	size_t len = 0;
	char *back = local_read("acct", "cal", "simple@example.org", &len);
	ok_("…and reads back byte for byte",
	    back && len == strlen(EV1) && memcmp(back, EV1, len) == 0);
	free(back);

	/* ── a UID that is hostile to being a filename ──────────────────────── */

	ok_("an event whose UID contains slashes writes",
	    local_write("acct", "cal", "a/b/../c@google.com", EV2, strlen(EV2)));

	back = local_read("acct", "cal", "a/b/../c@google.com", &len);
	ok_("…and reads back", back && len == strlen(EV2));
	free(back);

	/* ⛔ AND IT STAYED INSIDE THE COLLECTION. "a/b/../c" resolves to "a/c" if
	 * anything ever treats it as a path — the encoded name is what stops it. */
	char *p = item_path("acct", "cal", "a/b/../c@google.com");
	ok_("…without escaping the collection directory", strstr(p, "/../") == NULL);
	struct stat st;
	ok_("…and the file is where item_path says", stat(p, &st) == 0);
	free(p);

	/* ── the scan reads the UID out of the file, not off the filename ───── */

	local_list_t l;
	ok_("the collection scans", local_scan("acct", "cal", &l));
	ok_("…and finds both events", l.n == 2);
	ok_("…the simple one by its UID", local_find(&l, "simple@example.org") != NULL);
	ok_("…the awkward one too", local_find(&l, "a/b/../c@google.com") != NULL);

	local_item_t *it = local_find(&l, "simple@example.org");
	char *want_hash = content_hash(EV1, strlen(EV1));
	eq("…with the hash of its bytes", it ? it->hash : NULL, want_hash);
	free(want_hash);
	local_free(&l);

	/* A file the user dropped in by hand, named nothing like its UID. */
	char *dir = coll_dir("acct", "cal");
	char *odd = xasprintf("%s/whatever-i-called-it.ics", dir);
	FILE *f = fopen(odd, "wb");
	fwrite(EV1, 1, strlen(EV1), f);
	fclose(f);
	free(odd); free(dir);

	local_scan("acct", "cal", &l);
	ok_("a hand-dropped .ics is found by the UID INSIDE it, not its filename",
	    l.n == 3 && local_find(&l, "simple@example.org") != NULL);
	local_free(&l);

	/* ── deletion ───────────────────────────────────────────────────────── */

	ok_("an event deletes", local_delete("acct", "cal", "simple@example.org"));
	ok_("…deleting it again is not an error", local_delete("acct", "cal", "simple@example.org"));
	back = local_read("acct", "cal", "simple@example.org", &len);
	ok_("…and it is gone", back == NULL);
	free(back);

	/* ── the index survives a round trip ────────────────────────────────── */

	index_t ix;
	ok_("an absent index loads as empty, not as a failure",
	    idx_load(&ix, "acct", "cal") && ix.n == 0);

	/* ⚠ ETag SHAPES REAL SERVERS SEND: quotes, a weak validator, a slash. Every
	 * one of them would end a TSV field or a line if it were not encoded. */
	idx_set(&ix, "simple@example.org", "/cal/simple.ics", "\"abc123\"", "deadbeef");
	idx_set(&ix, "a/b/../c@google.com", "/cal/a%2Fb.ics", "W/\"x/y\"", "cafebabe");
	idx_set(&ix, "tabby", "/cal/t.ics", "has\ta tab\nand a newline", "0");
	ok_("the index saves", idx_save(&ix));
	idx_free(&ix);

	ok_("…and loads back", idx_load(&ix, "acct", "cal"));
	ok_("…with every entry", ix.n == 3);

	idx_entry_t *e = idx_find(&ix, "simple@example.org");
	eq("…href intact", e ? e->href : NULL, "/cal/simple.ics");
	eq("…a quoted ETag intact", e ? e->etag : NULL, "\"abc123\"");

	e = idx_find(&ix, "a/b/../c@google.com");
	eq("…a weak ETag with a slash intact", e ? e->etag : NULL, "W/\"x/y\"");

	e = idx_find(&ix, "tabby");
	eq("…an ETag containing a TAB and a NEWLINE intact",
	   e ? e->etag : NULL, "has\ta tab\nand a newline");

	e = idx_find_href(&ix, "/cal/simple.ics");
	eq("…and lookup by href finds the same entry", e ? e->uid : NULL, "simple@example.org");

	idx_remove(&ix, "tabby");
	ok_("an entry removes", idx_find(&ix, "tabby") == NULL && ix.n == 2);
	idx_free(&ix);

	/* ── the index is under state/, never in the vdir ───────────────────── */

	char *cd = coll_dir("acct", "cal");
	char *stray = xasprintf("%s/.idx", cd);
	ok_("no index file is left inside the vdir", stat(stray, &st) != 0);
	free(stray); free(cd);

	printf("\n%d/%d passed\n", total - fails, total);

	char *rm = xasprintf("rm -rf '%s'", root);
	if (system(rm) != 0) fprintf(stderr, "note: could not clean %s\n", root);
	free(rm);
	return fails ? 1 : 0;
}
