/* http.h — one request, over libcurl, with the defaults set explicitly.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_HTTP_H
#define SYNCAL_HTTP_H

#include "syncal.h"

typedef struct {
	char *user, *pass;     /* Basic — Nextcloud, Fastmail, iCloud app passwords */
	char *bearer;          /* OAuth 2 — Google, Microsoft */
	long timeout_s;
	/* ⛔ FOR A LAB SERVER AND NOTHING ELSE. Never set from a config file, never
	 * offered in the GUI: a calendar carries where you are and who with, and a
	 * client that can be told to stop checking certificates is one setting away
	 * from handing that to whoever is on the same wifi. tests/ set it against
	 * 127.0.0.1 and that is the only caller. */
	bool insecure;
} http_auth_t;

typedef struct {
	long status;
	buf_t body;
	char *etag;
	char *location;
	char *ctype;
} http_resp_t;

void http_resp_free(http_resp_t *r);

/* Returns false only when the request never completed — DNS, TLS, timeout,
 * connection refused. Any HTTP status at all, 404 and 500 included, is a
 * completed request and returns true with .status set: they are answers, and
 * the caller is the only thing that knows which of them are bad news. */
bool http_do(const char *method, const char *url,
             const char *const *headers, size_t nheaders,
             const void *body, size_t bodylen,
             const http_auth_t *auth, http_resp_t *out, char **err);

void http_global_init(void);
void http_global_cleanup(void);

#endif /* SYNCAL_HTTP_H */
