/* graph.h — Microsoft 365 calendars, over Graph rather than CalDAV.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_GRAPH_H
#define SYNCAL_GRAPH_H

#include "caldav.h"      /* caldav_colls_t — the same shape serves both */
#include "http.h"
#include "remote.h"

/* GET /me/calendars, in the shape the CalDAV discovery returns, so the account
 * code and the CLI have one kind of answer to handle. */
bool graph_discover(const http_auth_t *auth, caldav_colls_t *out, char **err);

remote_t *graph_remote(const char *calendar_url, const http_auth_t *auth);
void graph_remote_free(remote_t *r);

/* Exported for the tests: the two conversions are where this backend can be
 * wrong in a way nothing notices. */
char *graph_json_to_ics(const char *json, char **err);
char *graph_ics_to_json(const char *ics, size_t len, char **err);

#endif /* SYNCAL_GRAPH_H */
