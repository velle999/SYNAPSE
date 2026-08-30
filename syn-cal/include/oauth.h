/* oauth.h — OAuth 2 for a program with no server and no secret.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_OAUTH_H
#define SYNCAL_OAUTH_H

#include "syncal.h"

typedef struct {
	const char *auth_url;
	const char *token_url;
	const char *scope;
} oauth_provider_t;

const oauth_provider_t *oauth_google(void);
const oauth_provider_t *oauth_microsoft(void);

/* ── the pieces, exported so they can be tested ─────────────────────────── */

/* base64url with the padding removed — what every OAuth field uses, and what
 * plain base64 is not: '+' and '/' are not URL-safe and '=' is a separator. */
char *b64url(const void *data, size_t len);

/* 43 unreserved characters from the kernel's random source. */
char *pkce_verifier(void);

/* base64url(SHA-256(verifier)). The whole point of PKCE: the challenge goes out
 * over the browser where anything can see it, and the verifier only ever goes
 * to the token endpoint over TLS — so intercepting the redirect gets you a code
 * that cannot be exchanged. */
char *pkce_challenge(const char *verifier);

/* One value out of an application/x-www-form-urlencoded query string. */
char *query_get(const char *query, const char *key);

/* One string or number out of a flat JSON object. */
char *json_str(const char *json, const char *key);
long json_num(const char *json, const char *key, long fallback);

/* ── the flow ───────────────────────────────────────────────────────────── */

typedef struct {
	char *access_token;
	char *refresh_token;
	long expires_in;         /* seconds, as the server reported them */
} oauth_tokens_t;

void oauth_tokens_free(oauth_tokens_t *t);

/*
 * ⛔ GOOGLE REQUIRES A CLIENT SECRET FROM AN INSTALLED APPLICATION, AND SAYS SO.
 *
 * RFC 8252 argues that a desktop application cannot keep a secret, which is why
 * PKCE exists — and this file was written to that argument, sending no secret at
 * all. Google implemented something else: its Desktop-app clients are issued a
 * secret, its token endpoint refuses the exchange without one, and its own
 * documentation says the value "is not treated as a secret" for this client
 * type. PKCE is required IN ADDITION, not instead.
 *
 * The cost of believing the theory over the implementation was a sign-in that
 * got all the way through consent and then died on
 *
 *     invalid_request — client_secret is missing
 *
 * so `client_secret` is passed here and sent when it is non-NULL and non-empty.
 * Microsoft does not want one from a public client and must not be sent one:
 * pass NULL there. Nothing about this makes the value confidential — it ships
 * in the binary exactly as the client id does, for the same reason.
 */

/* Run the browser flow: start a loopback listener, open the browser, wait for
 * the redirect, exchange the code. Blocks until the user finishes or the
 * timeout runs out. `timeout_s` of 0 means the default.
 *
 * ⚠ `open_browser` false prints the URL instead of opening one, which is what
 * the tests use and what a machine with no browser needs. */
bool oauth_authorise(const oauth_provider_t *p, const char *client_id,
                     const char *client_secret,
                     bool open_browser, long timeout_s,
                     oauth_tokens_t *out, char **err);

/* Trade a refresh token for a new access token. */
bool oauth_refresh(const oauth_provider_t *p, const char *client_id,
                   const char *client_secret,
                   const char *refresh_token, oauth_tokens_t *out, char **err);

#endif /* SYNCAL_OAUTH_H */
