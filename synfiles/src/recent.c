/* recent.c — the Recently Modified view.
 *
 * ~/.local/share/recently-used.xbel is the freedesktop recent-files spec, and
 * it is the same file GTK and KDE applications both write, so this view is
 * populated on a fresh install without synfiles having recorded anything
 * itself.
 *
 * Entries are NOT filtered to files that still exist. A recent list that
 * silently drops the file you are looking for is worse than one that shows it
 * greyed out — "I know I opened it yesterday" is precisely the case this view
 * serves. `exists` travels as a column and the front-end decides.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	char  *href;     /* percent-encoded path, scheme already stripped */
	char  *mime;
	time_t modified;
	bool   exists;
} rec_t;

static char *recent_path(void)
{
	const char *env = getenv("SYNFILES_RECENT");
	if (env && *env)
		return xstrdup(env);
	char *data = xdg_data_home();
	char *p = xasprintf("%s/recently-used.xbel", data);
	free(data);
	return p;
}

static const char *find_in(const char *p, const char *end, const char *needle)
{
	if (!p || p >= end)
		return NULL;
	return memmem(p, (size_t)(end - p), needle, strlen(needle));
}

static char *attr_val(const char *p, const char *end, const char *attr)
{
	char *pat = xasprintf("%s=\"", attr);
	const char *a = find_in(p, end, pat);
	free(pat);
	if (!a)
		return NULL;
	a += strlen(attr) + 2;
	const char *q = memchr(a, '"', (size_t)(end - a));
	return q ? xstrndup(a, (size_t)(q - a)) : NULL;
}

/* "2026-07-06T21:48:03.819825Z" — always UTC in this file, so timegm rather
 * than mktime. Fractional seconds are ignored; nothing here needs microsecond
 * resolution and strptime cannot read them anyway. */
static time_t iso_to_epoch(const char *s)
{
	if (!s)
		return 0;
	struct tm tm;
	memset(&tm, 0, sizeof tm);
	if (!strptime(s, "%Y-%m-%dT%H:%M:%S", &tm))
		return 0;
	return timegm(&tm);
}

static int by_recent(const void *va, const void *vb)
{
	const rec_t *a = va, *b = vb;
	if (a->modified != b->modified)
		return a->modified < b->modified ? 1 : -1;   /* newest first */
	return strcmp(a->href, b->href);
}

int cmd_recent(int argc, char **argv)
{
	long limit = 200;
	bool only_existing = false;

	for (int i = 0; i < argc; i++) {
		if (!strncmp(argv[i], "--limit=", 8))
			limit = strtol(argv[i] + 8, NULL, 10);
		else if (!strcmp(argv[i], "--existing"))
			only_existing = true;
		else
			die("recent: unknown option '%s'", argv[i]);
	}
	if (limit <= 0)
		limit = 200;

	char *path = recent_path();
	char *text = slurp(path);
	free(path);

	if (g_out == OUT_REC)
		rec_row(4, "path", "mime", "mtime", "exists");

	if (!text)
		return 100;

	size_t cap = 256, n = 0;
	rec_t *recs = xmalloc(cap * sizeof *recs);

	const char *end = text + strlen(text);
	for (const char *p = text; (p = find_in(p, end, "<bookmark ")); ) {
		const char *b_end = find_in(p, end, "</bookmark>");
		if (!b_end)
			b_end = end;

		/* Attributes live on the opening tag; searching the whole element
		 * would find a modified= on a nested application record. */
		const char *tag_end = memchr(p, '>', (size_t)(b_end - p));
		if (!tag_end)
			break;

		char *href = attr_val(p, tag_end, "href");
		if (href && !strncmp(href, "file://", 7)) {
			char *mod = attr_val(p, tag_end, "modified");
			char *mime = NULL;
			const char *mt = find_in(p, b_end, "<mime:mime-type");
			if (mt)
				mime = attr_val(mt, b_end, "type");

			rec_t r = { 0 };
			/* Already a URI, so already percent-encoded — do not re-encode. */
			r.href = xstrdup(href + 7);
			r.mime = mime ? mime : xstrdup("");
			r.modified = iso_to_epoch(mod);

			if (n == cap) {
				cap *= 2;
				recs = xrealloc(recs, cap * sizeof *recs);
			}
			recs[n++] = r;
			free(mod);
		}
		free(href);
		p = b_end == end ? end : b_end + strlen("</bookmark>");
		if (p >= end)
			break;
	}

	qsort(recs, n, sizeof *recs, by_recent);

	/* Existence is checked only for the rows that will actually be shown.
	 * The file holds hundreds of entries and stat'ing all of them to print
	 * twenty is work nobody asked for — and on a disconnected network mount
	 * each one can block. */
	int shown = 0;
	for (size_t i = 0; i < n && shown < limit; i++) {
		char *real = pct_decode(recs[i].href);
		recs[i].exists = access(real, F_OK) == 0;

		if (only_existing && !recs[i].exists) {
			free(real);
			continue;
		}

		if (g_out == OUT_REC) {
			char *m = xasprintf("%lld", (long long)recs[i].modified);
			rec_row(4, recs[i].href, recs[i].mime, m, recs[i].exists ? "1" : "0");
			free(m);
		} else {
			char when[32] = "";
			struct tm tm;
			if (localtime_r(&recs[i].modified, &tm))
				strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);
			printf("%s%s%s  %s%s%s%s\n", C_DIM(), when, C_RESET(),
			       recs[i].exists ? "" : C_WARN(), real,
			       recs[i].exists ? "" : " [missing]", C_RESET());
		}
		free(real);
		shown++;
	}

	for (size_t i = 0; i < n; i++) {
		free(recs[i].href);
		free(recs[i].mime);
	}
	free(recs);
	free(text);

	return shown ? 0 : 100;
}
