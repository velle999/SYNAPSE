/* caldav.h — discovery, and a remote_t over HTTPS.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_CALDAV_H
#define SYNCAL_CALDAV_H

#include "http.h"
#include "remote.h"

typedef struct {
	char *url;          /* absolute, ready to request */
	char *name;         /* displayname, for a person to choose from */
	char *ctag;         /* getctag: "has anything in here changed" in one word */
	char *color;        /* apple:calendar-color, when the server offers one */
	bool events, todos;
} caldav_coll_t;

typedef struct { caldav_coll_t *e; size_t n, cap; } caldav_colls_t;
void caldav_colls_free(caldav_colls_t *c);
void colls_add_public(caldav_colls_t *c, caldav_coll_t v);

/* From whatever the user typed to the list of their calendars.
 *
 * Accepts a bare domain, a server root, a principal URL or a collection URL —
 * because those are the four things people paste, and telling them apart is
 * this function's job rather than theirs. */
bool caldav_discover(const char *entered, const http_auth_t *auth,
                     caldav_colls_t *out, char **err);

remote_t *caldav_remote(const char *collection_url, const http_auth_t *auth);
void caldav_remote_free(remote_t *r);

#endif /* SYNCAL_CALDAV_H */
