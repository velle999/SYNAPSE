/* sync.c — two-way sync, decided from three facts rather than two.
 *
 * ── The rule the whole file exists to obey ──────────────────────────────────
 *
 * ⛔ AN EDIT IS NEVER LOST WITHOUT SAYING SO. Everything below follows from
 * that. A calendar that occasionally drops an appointment is worse than no
 * calendar at all, because you stop checking.
 *
 * ── Why three facts ─────────────────────────────────────────────────────────
 *
 * With only "what the server has" and "what the disk has", a difference is
 * unattributable: it looks identical whether they changed it, you changed it,
 * or one of you deleted it. The index supplies the third fact — what both sides
 * were at the end of the last sync — and every decision here is a comparison
 * against it:
 *
 *   remote etag == index etag   the server has not moved since we last agreed
 *   local hash  == index hash   the disk has not moved either
 *
 * Four combinations, and each has exactly one right answer:
 *
 *   neither moved   nothing to do
 *   remote only     pull it down
 *   local only      push it up
 *   both            a conflict, and the user's policy decides
 *
 * Presence is the same question one level up: an item in the index but on
 * neither side was deleted by both, which is agreement, not a problem.
 *
 * ── Every write is conditional ──────────────────────────────────────────────
 *
 * ⚠ If-Match ON EVERY PUT AND DELETE. The gap between deciding and writing is
 * real — a phone can save an event into it — and an unconditional PUT
 * overwrites whatever arrived in that gap without ever knowing. A 412 comes
 * back instead, and a 412 is not an error here: it means the decision was made
 * on stale information, and the item is left for the next run, which will see
 * the new ETag and decide again correctly.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "sync.h"
#include "i18n.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void seterr(char **err, const char *fmt, ...)
{
	if (!err || *err) return;         /* keep the first, which caused the rest */
	va_list ap;
	va_start(ap, fmt);
	char *s = NULL;
	if (vasprintf(&s, fmt, ap) < 0) s = NULL;
	va_end(ap);
	*err = s;
}

/* A UID for the copy that keeps a conflicted local edit alive.
 *
 * ⚠ DERIVED FROM THE ORIGINAL, NOT RANDOM. When you find two copies of the same
 * meeting in a fortnight, the only useful question is which one was yours, and
 * a UID that still contains the original answers it. */
static char *conflict_uid(const char *uid)
{
	return xasprintf("%s-syncal-local-%ld", uid, (long)time(NULL));
}

/* Pull one item down. Returns false only on a transport failure. */
static bool pull(remote_t *r, const sync_opts_t *o, index_t *ix,
                 const char *href, const char *etag, sync_stats_t *st, char **err)
{
	size_t len = 0;
	char *got_etag = NULL, *e = NULL;
	char *data = r->get(r, href, &len, &got_etag, &e);
	if (!data) {
		st->errors++;
		warn(_("cannot fetch %s: %s"), href, e ? e : "no reason given");
		free(e);
		return true;                  /* one bad item does not end the sync */
	}

	char *uid = ics_uid(data, len);
	if (!uid) {
		/* Addressable on the server, unusable here — and silently dropping it
		 * would be an event that never appears and never explains itself. */
		warn(_("%s has no UID; it cannot be filed and was skipped"), href);
		st->skipped++;
		free(data); free(got_etag); free(e);
		return true;
	}

	if (!o->dry_run) {
		if (!local_write(o->account, o->collection, uid, data, len)) {
			seterr(err, "cannot write %s into the local calendar", uid);
			free(data); free(uid); free(got_etag);
			return false;
		}
		char *hash = content_hash(data, len);
		idx_set(ix, uid, href, got_etag ? got_etag : etag, hash);
		free(hash);
	}

	free(data); free(uid); free(got_etag);
	return true;
}

/* Push one item up. `etag` is what we believe the server has, or NULL for a
 * create — which is sent with If-None-Match: * so two clients creating the same
 * event at once cannot silently overwrite one another. */
/* ⚠ NO `err` OUT-PARAMETER, UNLIKE pull(). A failed upload is always a
 * per-item outcome here: the local file is untouched, the index entry is left
 * alone, and the next run tries again. Nothing an upload can do makes the rest
 * of the calendar unsafe to sync, so there is no failure for it to report
 * upwards — and a signature that could report one would invite a caller to
 * abandon the run over a single unreachable event. */
static bool push(remote_t *r, const sync_opts_t *o, index_t *ix,
                 const char *uid, const char *path, const char *href_in,
                 const char *etag, sync_stats_t *st)
{
	/* ⚠ READ BY THE PATH THE SCAN FOUND, not by one rebuilt from the UID. They
	 * differ for any file that arrived from outside this program, and
	 * reconstructing the name turns such an event into a permanent silent
	 * skip — it lists, and it never uploads. */
	size_t len = 0;
	char *data = path ? read_file(path, &len)
	                  : local_read(o->account, o->collection, uid, &len);
	if (!data) {
		/* Vanished between the scan and now — somebody deleted it while we
		 * were talking to the server. Not an error; the next run sees it. */
		st->skipped++;
		return true;
	}

	char *href = href_in ? xstrdup(href_in) : r->href_for(r, uid);
	char *new_etag = NULL, *new_href = NULL, *e = NULL;
	bool conflict = false;

	if (o->dry_run) {
		free(data); free(href);
		return true;
	}

	if (!r->put(r, href, data, len, etag, &new_etag, &new_href, &conflict, &e)) {
		st->errors++;
		warn(_("cannot upload %s: %s"), uid, e ? e : "no reason given");
		free(e); free(data); free(href); free(new_etag); free(new_href);
		return true;
	}

	if (conflict) {
		/* ⚠ NOT AN ERROR AND NOT A LOSS. Somebody wrote to this event between
		 * the listing and the upload. The local file is untouched and the index
		 * is left alone, so the next run sees the server's new ETag, sees the
		 * local change still pending, and takes the both-changed path — which
		 * keeps both. Retrying here would be the unconditional write this whole
		 * file exists to avoid. */
		info("%s changed on the server while we were uploading; left for the next run", uid);
		st->conflicts++;
		free(data); free(href); free(new_etag); free(new_href);
		return true;
	}

	char *hash = content_hash(data, len);
	/* A server may answer a PUT without an ETag. Recording an empty one would
	 * claim knowledge we do not have, so the entry is stored with no etag and
	 * the next listing supplies it.
	 *
	 * ⚠ AND THE href IS THE SERVER'S, IF IT NAMED ONE. Recording the guess
	 * instead means the next listing finds an item the index has never heard
	 * of, pulls it down as new, and uploads the local copy again — a duplicate
	 * per sync, for ever. */
	idx_set(ix, uid, new_href ? new_href : href, new_etag, hash);
	free(hash);

	free(data); free(href); free(new_etag); free(new_href);
	return true;
}

bool sync_run(remote_t *r, const sync_opts_t *o, sync_stats_t *st, char **err)
{
	memset(st, 0, sizeof *st);
	if (err) *err = NULL;

	index_t ix;
	if (!idx_load(&ix, o->account, o->collection)) {
		seterr(err, "cannot read the sync index");
		return false;
	}

	remote_list_t remote;
	rlist_init(&remote);
	char *e = NULL;
	if (!r->list(r, &remote, &e)) {
		seterr(err, "%s", e ? e : "the server would not list the calendar");
		free(e);
		rlist_free(&remote);
		idx_free(&ix);
		return false;
	}

	local_list_t local;
	local_scan(o->account, o->collection, &local);

	bool ok = true;

	/* ── 1. everything the server has ───────────────────────────────────── */

	for (size_t i = 0; i < remote.n && ok; i++) {
		const char *href = remote.e[i].href;
		const char *etag = remote.e[i].etag;
		idx_entry_t *known = idx_find_href(&ix, href);

		if (!known) {
			/* Never seen. It is new to us — or it is an event we have under a
			 * different href, which a server is entitled to do after a move. In
			 * both cases fetching is right: the UID inside decides where it
			 * lands, and a re-file is the correct outcome of a move. */
			ok = pull(r, o, &ix, href, etag, st, err);
			st->pulled_new++;
			continue;
		}

		local_item_t *lo = local_find(&local, known->uid);
		bool remote_moved = !known->etag || !etag || strcmp(known->etag, etag) != 0;
		bool local_moved = lo && known->hash && strcmp(known->hash, lo->hash) != 0;

		if (!lo) {
			/* Deleted here since the last sync. If the server has not moved,
			 * that deletion is ours to propagate; if it has, both changed. */
			if (!remote_moved) {
				if (!o->dry_run) {
					bool conflict = false;
					char *de = NULL;
					if (r->del(r, href, known->etag, &conflict, &de)) {
						if (conflict) { st->conflicts++; }
						else { idx_remove(&ix, known->uid); st->pushed_deleted++; }
					} else {
						st->errors++;
						warn(_("cannot delete %s on the server: %s"), known->uid,
						     de ? de : "no reason given");
					}
					free(de);
				} else {
					st->pushed_deleted++;
				}
			} else {
				/* ⛔ DELETED HERE, EDITED THERE. The deletion is the weaker
				 * claim: it destroys, and the edit does not. The event comes
				 * back rather than being removed from the server. */
				info("%s was deleted here and edited on the server; keeping the server's", known->uid);
				ok = pull(r, o, &ix, href, etag, st, err);
				st->conflicts++;
			}
			continue;
		}

		if (remote_moved && local_moved) {
			st->conflicts++;
			if (o->on_conflict == CONFLICT_LOCAL_WINS) {
				ok = push(r, o, &ix, known->uid, lo ? lo->path : NULL, href, known->etag, st);
				continue;
			}
			if (o->on_conflict == CONFLICT_KEEP_BOTH && !o->dry_run) {
				/* Re-file the local edit under a new identity BEFORE the
				 * server's version overwrites the file it is sitting in. */
				size_t len = 0;
				char *mine = lo->path ? read_file(lo->path, &len)
				                      : local_read(o->account, o->collection, known->uid, &len);
				if (mine) {
					char *nuid = conflict_uid(known->uid);
					size_t nlen = 0;
					char *copy = ics_replace_uid(mine, len, nuid, &nlen);
					if (local_write(o->account, o->collection, nuid, copy, nlen))
						info("kept your version of %s as %s", known->uid, nuid);
					else
						warn(_("could not keep your version of %s — nothing was overwritten"), known->uid);
					free(copy); free(nuid);
				}
				free(mine);
			}
			ok = pull(r, o, &ix, href, etag, st, err);
			continue;
		}

		if (remote_moved) { ok = pull(r, o, &ix, href, etag, st, err); st->pulled_changed++; continue; }
		if (local_moved)  { ok = push(r, o, &ix, known->uid, lo ? lo->path : NULL, href, known->etag, st); st->pushed_changed++; continue; }
		/* Neither moved. */
	}

	/* ── 2. everything the disk has that the server did not list ────────── */

	for (size_t i = 0; i < local.n && ok; i++) {
		const char *uid = local.e[i].uid;
		idx_entry_t *known = idx_find(&ix, uid);

		if (!known) {                      /* new here */
			ok = push(r, o, &ix, uid, local.e[i].path, NULL, NULL, st);
			st->pushed_new++;
			continue;
		}
		if (known->href && rlist_find(&remote, known->href)) continue;  /* handled above */

		/* In the index, absent from the server: deleted there. */
		bool local_moved = known->hash && strcmp(known->hash, local.e[i].hash) != 0;
		if (!local_moved) {
			if (!o->dry_run) local_delete(o->account, o->collection, uid);
			idx_remove(&ix, uid);
			st->pulled_deleted++;
		} else {
			/* ⛔ DELETED THERE, EDITED HERE — the mirror of the case above, and
			 * the same answer: the edit survives. It is pushed back up as a new
			 * item, which is what "undelete" looks like over CalDAV. */
			info("%s was deleted on the server and edited here; putting it back", uid);
			idx_remove(&ix, uid);
			ok = push(r, o, &ix, uid, local.e[i].path, NULL, NULL, st);
			st->conflicts++;
		}
	}

	/* ── 3. index entries neither side has ──────────────────────────────── */

	for (size_t i = 0; i < ix.n; ) {
		idx_entry_t *en = &ix.e[i];
		bool on_server = en->href && rlist_find(&remote, en->href);
		bool on_disk = local_find(&local, en->uid) != NULL;
		if (!on_server && !on_disk) { idx_remove(&ix, en->uid); continue; }
		i++;
	}

	if (ok && !o->dry_run && !idx_save(&ix))
		seterr(err, "the sync finished but the index could not be written; the next run will redo it");

	rlist_free(&remote);
	local_free(&local);
	idx_free(&ix);
	return ok && (!err || !*err);
}

/* ── the remote list ────────────────────────────────────────────────────── */

void rlist_init(remote_list_t *l) { l->e = NULL; l->n = l->cap = 0; }

void rlist_add(remote_list_t *l, const char *href, const char *etag)
{
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->e = xrealloc(l->e, l->cap * sizeof *l->e);
	}
	l->e[l->n].href = xstrdup(href);
	l->e[l->n].etag = etag ? xstrdup(etag) : NULL;
	l->n++;
}

void rlist_free(remote_list_t *l)
{
	for (size_t i = 0; i < l->n; i++) { free(l->e[i].href); free(l->e[i].etag); }
	free(l->e);
	rlist_init(l);
}

remote_item_t *rlist_find(remote_list_t *l, const char *href)
{
	for (size_t i = 0; i < l->n; i++)
		if (strcmp(l->e[i].href, href) == 0) return &l->e[i];
	return NULL;
}
