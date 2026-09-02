/* store.c — the vdir on disk, and the sync index beside it.
 *
 * ── Two things, kept apart on purpose ───────────────────────────────────────
 *
 * THE VDIR IS THE DATA: `<account>/<collection>/<uid>.ics`, one event per file,
 * the layout vdirsyncer and khal already write. Delete the index and nothing is
 * lost; delete the vdir and everything is.
 *
 * THE INDEX IS A MEMORY OF THE LAST SYNC, and it is rebuildable by definition —
 * losing it costs one full re-sync, not one appointment. It records, per event,
 * the server's ETag and the local file's hash as they were when the two sides
 * last agreed. Without both, "they changed it" and "I changed it" are the same
 * observation, and a sync engine that cannot tell them apart deletes things.
 *
 * ⛔ THE FILENAME IS THE PERCENT-ENCODED UID. A UID is arbitrary text — RFC 5545
 * only asks that it be globally unique — and Google issues UIDs containing '/'
 * and '@'. Writing `<uid>.ics` raw either escapes the collection directory or
 * silently truncates at the slash, and the event that does it is somebody's
 * meeting rather than a test case. The encoded form is the identity on disk,
 * exactly as it is in synfiles' listings.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syncal.h"
#include "i18n.h"
#include "store.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── paths ──────────────────────────────────────────────────────────────── */

char *coll_dir(const char *account, const char *coll)
{
	char *a = pct_encode(account, false);
	char *c = pct_encode(coll, false);
	char *p = store_path("%s/%s", a, c);
	free(a); free(c);
	return p;
}

char *item_path(const char *account, const char *coll, const char *uid)
{
	char *dir = coll_dir(account, coll);
	char *name = pct_encode(uid, false);
	char *p = xasprintf("%s/%s.ics", dir, name);
	free(dir); free(name);
	return p;
}

static char *index_path(const char *account, const char *coll)
{
	char *a = pct_encode(account, false);
	char *c = pct_encode(coll, false);
	char *p = store_path("state/%s.%s.idx", a, c);
	free(a); free(c);
	return p;
}

/* ── the index ──────────────────────────────────────────────────────────── */

void idx_init(index_t *ix) { ix->e = NULL; ix->n = ix->cap = 0; ix->path = NULL; }

void idx_free(index_t *ix)
{
	for (size_t i = 0; i < ix->n; i++) {
		free(ix->e[i].uid); free(ix->e[i].href);
		free(ix->e[i].etag); free(ix->e[i].hash);
	}
	free(ix->e);
	free(ix->path);
	idx_init(ix);
}

idx_entry_t *idx_find(index_t *ix, const char *uid)
{
	for (size_t i = 0; i < ix->n; i++)
		if (strcmp(ix->e[i].uid, uid) == 0) return &ix->e[i];
	return NULL;
}

idx_entry_t *idx_find_href(index_t *ix, const char *href)
{
	for (size_t i = 0; i < ix->n; i++)
		if (ix->e[i].href && strcmp(ix->e[i].href, href) == 0) return &ix->e[i];
	return NULL;
}

void idx_set(index_t *ix, const char *uid, const char *href,
             const char *etag, const char *hash)
{
	idx_entry_t *e = idx_find(ix, uid);
	if (!e) {
		if (ix->n == ix->cap) {
			ix->cap = ix->cap ? ix->cap * 2 : 16;
			ix->e = xrealloc(ix->e, ix->cap * sizeof *ix->e);
		}
		e = &ix->e[ix->n++];
		memset(e, 0, sizeof *e);
		e->uid = xstrdup(uid);
	}
	free(e->href); e->href = href ? xstrdup(href) : NULL;
	free(e->etag); e->etag = etag ? xstrdup(etag) : NULL;
	free(e->hash); e->hash = hash ? xstrdup(hash) : NULL;
}

void idx_remove(index_t *ix, const char *uid)
{
	for (size_t i = 0; i < ix->n; i++) {
		if (strcmp(ix->e[i].uid, uid) != 0) continue;
		free(ix->e[i].uid); free(ix->e[i].href);
		free(ix->e[i].etag); free(ix->e[i].hash);
		memmove(&ix->e[i], &ix->e[i + 1], (ix->n - i - 1) * sizeof *ix->e);
		ix->n--;
		return;
	}
}

/* One TSV line per entry, every field percent-encoded.
 *
 * ⚠ AN ETag IS AN OPAQUE QUOTED STRING and servers put anything in it —
 * Fastmail's contain quotes, Google's contain slashes, and a weak validator
 * arrives as W/"...". Encoded, none of that can end a field or a line. */
bool idx_load(index_t *ix, const char *account, const char *coll)
{
	idx_init(ix);
	ix->path = index_path(account, coll);

	size_t len = 0;
	char *text = read_file(ix->path, &len);
	if (!text) return true;                  /* no index yet is not an error */

	char *save = NULL;
	for (char *line = strtok_r(text, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		if (!*line || *line == '#') continue;
		char *f[4] = { NULL, NULL, NULL, NULL };
		int n = 0;
		char *p = line;
		while (n < 4) {
			f[n++] = p;
			char *tab = strchr(p, '\t');
			if (!tab) break;
			*tab = '\0';
			p = tab + 1;
		}
		if (n < 4) continue;                 /* a short line is a stale format */
		char *uid = pct_decode(f[0]), *href = pct_decode(f[1]);
		char *etag = pct_decode(f[2]), *hash = pct_decode(f[3]);
		idx_set(ix, uid, href, etag, hash);
		free(uid); free(href); free(etag); free(hash);
	}
	free(text);
	return true;
}

bool idx_save(index_t *ix)
{
	buf_t out;
	buf_init(&out);
	buf_addstr(&out, "# syn-cal sync index — rebuildable. uid\thref\tetag\thash\n");
	for (size_t i = 0; i < ix->n; i++) {
		char *u = pct_encode(ix->e[i].uid, false);
		char *h = pct_encode(ix->e[i].href ? ix->e[i].href : "", false);
		char *e = pct_encode(ix->e[i].etag ? ix->e[i].etag : "", false);
		char *c = pct_encode(ix->e[i].hash ? ix->e[i].hash : "", false);
		buf_addf(&out, "%s\t%s\t%s\t%s\n", u, h, e, c);
		free(u); free(h); free(e); free(c);
	}

	char *dir = store_path("state");
	bool ok = ensure_dir(dir);
	free(dir);
	if (ok) ok = write_file_atomic(ix->path, out.b, out.len, 0600);
	buf_free(&out);
	return ok;
}

/* ── the vdir ───────────────────────────────────────────────────────────── */

void local_init(local_list_t *l) { l->e = NULL; l->n = l->cap = 0; }

void local_free(local_list_t *l)
{
	for (size_t i = 0; i < l->n; i++) { free(l->e[i].uid); free(l->e[i].hash); free(l->e[i].path); }
	free(l->e);
	local_init(l);
}

static void local_add(local_list_t *l, const char *uid, const char *hash, const char *path)
{
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->e = xrealloc(l->e, l->cap * sizeof *l->e);
	}
	l->e[l->n].uid = xstrdup(uid);
	l->e[l->n].hash = xstrdup(hash);
	l->e[l->n].path = xstrdup(path);
	l->n++;
}

local_item_t *local_find(local_list_t *l, const char *uid)
{
	for (size_t i = 0; i < l->n; i++)
		if (strcmp(l->e[i].uid, uid) == 0) return &l->e[i];
	return NULL;
}

/* Every .ics in the collection, by the UID inside it and the hash of its bytes.
 *
 * ⛔ THE UID COMES FROM THE FILE, NOT FROM THE FILENAME. They agree for anything
 * this program wrote, and they do not have to: a vdir is a public format and the
 * user may have dropped an .ics in by hand, or restored one from a backup under
 * a different name. Trusting the filename would then upload an event under an
 * identity the file itself contradicts, and the server would keep both. */
bool local_scan(const char *account, const char *coll, local_list_t *out)
{
	local_init(out);
	char *dir = coll_dir(account, coll);
	DIR *d = opendir(dir);
	if (!d) { free(dir); return true; }       /* nothing synced yet */

	struct dirent *ent;
	while ((ent = readdir(d))) {
		size_t nlen = strlen(ent->d_name);
		if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".ics") != 0) continue;
		if (ent->d_name[0] == '.') continue;

		char *path = xasprintf("%s/%s", dir, ent->d_name);
		size_t len = 0;
		char *data = read_file(path, &len);
		if (data && len) {
			char *uid = ics_uid(data, len);
			if (uid) {
				char *hash = content_hash(data, len);
				local_add(out, uid, hash, path);
				free(hash);
				free(uid);
			} else {
				/* Loudly: an .ics with no UID cannot be addressed on a CalDAV
				 * server, so skipping it silently would mean an event that
				 * never syncs and never explains why. */
				warn(_("%s has no UID and cannot be synced"), path);
			}
		}
		free(data);
		free(path);
	}
	closedir(d);
	free(dir);
	return true;
}

char *local_read(const char *account, const char *coll, const char *uid, size_t *len)
{
	char *p = item_path(account, coll, uid);
	char *data = read_file(p, len);
	free(p);
	return data;
}

bool local_write(const char *account, const char *coll, const char *uid,
                 const void *data, size_t len)
{
	char *dir = coll_dir(account, coll);
	bool ok = ensure_dir(dir);
	free(dir);
	if (!ok) return false;

	char *p = item_path(account, coll, uid);
	ok = write_file_atomic(p, data, len, 0600);
	free(p);
	return ok;
}

bool local_delete(const char *account, const char *coll, const char *uid)
{
	char *p = item_path(account, coll, uid);
	bool ok = (unlink(p) == 0 || errno == ENOENT);
	free(p);
	return ok;
}
