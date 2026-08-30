/* oauth_test.c — PKCE, the little parsers, and the whole browser flow.
 *
 * With no argument it runs the parts that need nothing. With a token endpoint
 * it also runs the flow end to end: print the authorisation URL, wait on the
 * loopback for a redirect that tests/oauth_test.sh performs with curl, and
 * exchange the code against a fake provider.
 *
 * ⛔ NOTHING HERE TALKS TO GOOGLE. A test that needs a real account is a test
 * that runs on one machine, and the parts worth checking — that the challenge
 * is the SHA-256 of the verifier, that a mismatched state is refused, that a
 * refresh with no new refresh token keeps the old one — are all things a fake
 * provider can be driven into and a real one cannot.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "oauth.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, total = 0;
static void ok_(const char *what, bool cond)
{
	total++;
	if (cond) fprintf(stderr, "  ok    %s\n", what);
	else { fprintf(stderr, "  FAIL  %s\n", what); fails++; }
}
static void eq(const char *what, const char *got, const char *want)
{
	ok_(what, (!got && !want) || (got && want && strcmp(got, want) == 0));
	if (got && want && strcmp(got, want) != 0)
		fprintf(stderr, "        got  [%s]\n        want [%s]\n", got, want);
}

int main(int argc, char **argv)
{
	http_global_init();

	/* ── base64url ──────────────────────────────────────────────────────── */

	char *b = b64url("", 0);
	eq("empty input encodes to nothing", b, "");
	free(b);

	/* 0xFB 0xFF picks both characters plain base64 gets wrong: '+' and '/'. */
	const unsigned char tricky[] = { 0xfb, 0xff, 0xfe };
	b = b64url(tricky, 3);
	ok_("neither + nor / survives the encoding", !strchr(b, '+') && !strchr(b, '/'));
	ok_("…and the padding is gone", !strchr(b, '='));
	free(b);

	/* ── PKCE ───────────────────────────────────────────────────────────── */

	/* ⛔ THE KNOWN ANSWER. Computed independently:
	 *   base64url(sha256("test-verifier")) with the padding stripped.
	 * A challenge that is merely "some hash" passes every self-consistent
	 * test and is rejected by the provider. */
	char *ch = pkce_challenge("test-verifier");
	eq("the challenge is base64url(SHA-256(verifier))", ch,
	   "JBbiqONGWPaAmwXk_8bT6UnlPfrn65D32eZlJS-zGG0");
	free(ch);

	char *v1 = pkce_verifier();
	char *v2 = pkce_verifier();
	ok_("a verifier is 43 characters", strlen(v1) == 43);
	ok_("…and two of them differ", strcmp(v1, v2) != 0);
	{
		bool unreserved = true;
		for (char *c = v1; *c; c++)
			if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
			      (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.' || *c == '~'))
				unreserved = false;
		ok_("…made only of characters a URL does not have to escape", unreserved);
	}
	free(v1); free(v2);

	/* ── the query string off a redirect ────────────────────────────────── */

	const char *q = "code=4%2F0Ab_c-d&state=xyz&scope=a+b";
	char *code = query_get(q, "code");
	eq("a percent-escaped code decodes", code, "4/0Ab_c-d");
	free(code);
	char *scope = query_get(q, "scope");
	eq("a '+' in a form value is a space", scope, "a b");
	free(scope);
	ok_("a key that is not there is NULL", query_get(q, "nope") == NULL);
	/* A key that is a suffix of another must not match it. */
	ok_("'ode' does not match 'code'", query_get(q, "ode") == NULL);

	/* ── the token response ─────────────────────────────────────────────── */

	const char *json = "{\"access_token\":\"tok\\\\en\",\"expires_in\":3599,"
	                   "\"refresh_token\":\"r1\",\"token_type\":\"Bearer\"}";
	char *at = json_str(json, "access_token");
	/* ⛔ A BACKSLASH IN A TOKEN IS UNREMARKABLE, and a naive reader truncates
	 * there — producing an authentication failure that looks like a revoked
	 * grant and sends you to the wrong provider's support page. */
	eq("an escaped backslash survives", at, "tok\\en");
	free(at);
	ok_("a number reads as a number", json_num(json, "expires_in", 0) == 3599);
	ok_("a missing number falls back", json_num(json, "nope", 42) == 42);
	ok_("a number is not returned as a string", json_str(json, "expires_in") == NULL);
	ok_("malformed JSON yields nothing, rather than crashing",
	    json_str("{not json", "access_token") == NULL);

	/* ── the flow, against a fake provider ──────────────────────────────── */

	if (argc >= 2) {
		oauth_provider_t fake = { "http://127.0.0.1:1/never-opened", argv[1], "test.scope" };
		oauth_tokens_t t;
		char *err = NULL;

		/* stdout carries ONLY the URL, so the shell can read it. Everything
		 * else in this file goes to stderr. */
		bool got = oauth_authorise(&fake, "public-client", NULL, false, 30, &t, &err);
		ok_("the browser flow completes and exchanges the code", got);
		if (!got) fprintf(stderr, "        (%s)\n", err ? err : "no reason");
		free(err); err = NULL;

		if (got) {
			eq("…returning the access token", t.access_token, "fake-access-1");
			eq("…and the refresh token", t.refresh_token, "fake-refresh-1");
			ok_("…and the expiry the server gave", t.expires_in == 3599);
			oauth_tokens_free(&t);
		}

		/* ⚠ A REFRESH THAT ANSWERS WITHOUT A NEW REFRESH TOKEN — which is the
		 * common case — must keep the old one. Storing the empty answer over it
		 * logs the account out at the next expiry, an hour later, with nothing
		 * to connect the two events. */
		if (oauth_refresh(&fake, "public-client", NULL, "keep-me", &t, &err)) {
			eq("a refresh with no new refresh token keeps the old one",
			   t.refresh_token, "keep-me");
			eq("…and still yields a fresh access token", t.access_token, "fake-access-2");
			oauth_tokens_free(&t);
		} else {
			ok_("a refresh with no new refresh token keeps the old one", false);
			fprintf(stderr, "        (%s)\n", err ? err : "no reason");
		}
		free(err); err = NULL;

		/* A provider that refuses must say what it said. */
		oauth_provider_t bad = { fake.auth_url, argv[1], "test.scope" };
		if (!oauth_refresh(&bad, "public-client", NULL, "REFUSE", &t, &err)) {
			ok_("a refused refresh reports the provider's own error",
			    err && strstr(err, "invalid_grant") != NULL);
			if (err && !strstr(err, "invalid_grant")) fprintf(stderr, "        (%s)\n", err);
		} else {
			ok_("a refused refresh reports the provider's own error", false);
		}
		free(err);
		err = NULL;

		/*
		 * ⛔ THE SECRET REACHES THE TOKEN ENDPOINT.
		 *
		 * RFC 8252 says an installed app cannot keep a secret and this code was
		 * written to that argument, sending none. Google requires one anyway
		 * for its Desktop-app clients and refuses the exchange without it — so
		 * a sign-in passed consent, came back with a code, and died on
		 * "client_secret is missing" with the user watching. The fake provider
		 * demands one for `secret-client`, so omitting it fails here instead.
		 */
		if (oauth_refresh(&fake, "secret-client", "s3cr3t", "keep-me", &t, &err)) {
			ok_("a client that needs a secret gets one at the token endpoint", true);
			oauth_tokens_free(&t);
		} else {
			ok_("a client that needs a secret gets one at the token endpoint", false);
			fprintf(stderr, "        (%s)\n", err ? err : "no reason");
		}
		free(err); err = NULL;

		/* …and the failure is reported in the provider's own words, which is
		 * how tonight's cause was found at all. */
		if (!oauth_refresh(&fake, "secret-client", NULL, "keep-me", &t, &err)) {
			ok_("…and omitting it reports the provider's own complaint",
			    err && strstr(err, "client_secret is missing") != NULL);
			if (err && !strstr(err, "client_secret is missing"))
				fprintf(stderr, "        (%s)\n", err);
		} else {
			ok_("…and omitting it reports the provider's own complaint", false);
			oauth_tokens_free(&t);
		}
		free(err);
	}

	fprintf(stderr, "\n%d/%d passed\n", total - fails, total);
	http_global_cleanup();
	return fails ? 1 : 0;
}
