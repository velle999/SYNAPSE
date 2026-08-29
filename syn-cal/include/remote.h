/* remote.h — what the sync engine needs from a server, and nothing more.
 *
 * ⛔ AN INTERFACE, SO THE ALGORITHM CAN BE TESTED WITHOUT A NETWORK. Two-way
 * sync is the part of a calendar that loses data when it is wrong, and its bugs
 * are combinatorial: both sides changed, both deleted, one deleted while the
 * other edited, the same event arriving under a second href. Testing that
 * through real HTTP means every case needs a server that can be driven into it,
 * and the cases that go untested are the ones that are awkward to arrange —
 * which is the same set as the ones that lose an appointment.
 *
 * caldav.c fills this in over HTTPS. tests/sync_test.c fills it in with a hash
 * table, and can therefore arrange any of those states in three lines.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_REMOTE_H
#define SYNCAL_REMOTE_H

#include "syncal.h"

typedef struct { char *href; char *etag; } remote_item_t;
typedef struct { remote_item_t *e; size_t n, cap; } remote_list_t;

void rlist_init(remote_list_t *l);
void rlist_add(remote_list_t *l, const char *href, const char *etag);
void rlist_free(remote_list_t *l);
remote_item_t *rlist_find(remote_list_t *l, const char *href);

/* Every call returns false on a transport or protocol failure and leaves *err
 * pointing at a malloc'd sentence fit to show a person. A precondition failure
 * — 412, the "somebody else changed it first" answer — is NOT a failure: it
 * sets *conflict and returns true, because it is an ordinary outcome of two
 * people using one calendar and the engine has a plan for it.
 */
typedef struct remote {
	bool (*list)(struct remote *r, remote_list_t *out, char **err);
	char *(*get)(struct remote *r, const char *href, size_t *len,
	             char **etag, char **err);
	bool (*put)(struct remote *r, const char *href, const void *data, size_t len,
	            const char *if_match, char **new_etag, bool *conflict, char **err);
	bool (*del)(struct remote *r, const char *href, const char *if_match,
	            bool *conflict, char **err);
	/* Where a new event should live. CalDAV lets the client choose, and the
	 * convention every server understands is <collection>/<uid>.ics. */
	char *(*href_for)(struct remote *r, const char *uid);
	void *ctx;
} remote_t;

#endif /* SYNCAL_REMOTE_H */
