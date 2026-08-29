/* http.c — libcurl, with every default that matters set by hand.
 *
 * ⚠ THE DEFAULTS ARE WRITTEN OUT EVEN WHERE THEY ARE ALREADY THE DEFAULT.
 * Certificate verification, the protocol allow-list and credential handling
 * across a redirect are the three settings whose absence is invisible: the
 * program works identically with them wrong, right up until the moment it
 * matters. A reviewer should be able to see what this client will and will not
 * do without going to look up what libcurl assumes this year.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "http.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void http_global_init(void)   { curl_global_init(CURL_GLOBAL_DEFAULT); }
void http_global_cleanup(void) { curl_global_cleanup(); }

void http_resp_free(http_resp_t *r)
{
	buf_free(&r->body);
	free(r->etag); free(r->location); free(r->ctype);
	r->etag = r->location = r->ctype = NULL;
	r->status = 0;
}

static size_t on_body(char *p, size_t sz, size_t n, void *ud)
{
	buf_add((buf_t *)ud, p, sz * n);
	return sz * n;
}

/* Pull the three response headers anything here cares about. A header name is
 * case-insensitive and the value may carry leading space and a trailing CRLF. */
static size_t on_header(char *p, size_t sz, size_t n, void *ud)
{
	http_resp_t *r = ud;
	size_t len = sz * n;

	static const struct { const char *name; size_t nlen; size_t off; } want[] = {
		{ "etag:",         5, offsetof(http_resp_t, etag) },
		{ "location:",     9, offsetof(http_resp_t, location) },
		{ "content-type:", 13, offsetof(http_resp_t, ctype) },
	};

	for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
		if (len <= want[i].nlen || strncasecmp(p, want[i].name, want[i].nlen) != 0) continue;
		const char *v = p + want[i].nlen;
		size_t vlen = len - want[i].nlen;
		while (vlen && (*v == ' ' || *v == '\t')) { v++; vlen--; }
		while (vlen && (v[vlen - 1] == '\r' || v[vlen - 1] == '\n' || v[vlen - 1] == ' ')) vlen--;

		char **slot = (char **)((char *)r + want[i].off);
		free(*slot);
		*slot = xmalloc(vlen + 1);
		memcpy(*slot, v, vlen);
		(*slot)[vlen] = '\0';
		break;
	}
	return len;
}

bool http_do(const char *method, const char *url,
             const char *const *headers, size_t nheaders,
             const void *body, size_t bodylen,
             const http_auth_t *auth, http_resp_t *out, char **err)
{
	if (err) *err = NULL;
	memset(out, 0, sizeof *out);
	buf_init(&out->body);

	CURL *c = curl_easy_init();
	if (!c) { if (err) *err = xstrdup("curl would not start"); return false; }

	char errbuf[CURL_ERROR_SIZE] = "";
	struct curl_slist *hdrs = NULL;
	for (size_t i = 0; i < nheaders; i++) hdrs = curl_slist_append(hdrs, headers[i]);

	/* ⛔ NO Expect: 100-continue. libcurl adds it to any body over 1 KiB, and a
	 * good few CalDAV servers — Radicale among them — never send the 100, so
	 * every upload of a long event stalls for curl's full one-second wait
	 * before the body goes anyway. It reads as "syncing is slow". */
	hdrs = curl_slist_append(hdrs, "Expect:");

	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &out->body);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, out);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "syn-cal/" SYNCAL_VERSION);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");   /* gzip if offered */

	/* ⛔ https AND http ONLY. Without this a redirect to file:// or scp:// is a
	 * request libcurl will happily carry out, and the URL comes from a server. */
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https,http");
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https,http");
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
	/* ⛔ AND THE METHOD AND BODY SURVIVE THE REDIRECT. libcurl turns a 301 into
	 * a GET and drops the body by default, which is right for a browser and
	 * wrong here: RFC 6764 discovery is a PROPFIND to /.well-known/caldav, and
	 * every server answers it with a redirect. Without this the request that
	 * lands is a bodyless GET, the principal is never found, and the account
	 * "cannot be discovered" for no visible reason. */
	curl_easy_setopt(c, CURLOPT_POSTREDIR, (long)CURL_REDIR_POST_ALL);
	/* ⛔ CREDENTIALS DO NOT FOLLOW A REDIRECT TO ANOTHER HOST. This is libcurl's
	 * default and it is set anyway: an open redirect on a calendar server would
	 * otherwise hand your password to wherever it pointed. */
	curl_easy_setopt(c, CURLOPT_UNRESTRICTED_AUTH, 0L);
	curl_easy_setopt(c, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);

	long timeout = (auth && auth->timeout_s) ? auth->timeout_s : 30;
	curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
	/* A sync must not hang forever on a server that accepted the connection and
	 * then went quiet: under 100 bytes/s for 30s is a dead transfer. */
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);

	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, (auth && auth->insecure) ? 0L : 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, (auth && auth->insecure) ? 0L : 2L);

	if (auth && auth->bearer) {
		curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)CURLAUTH_BEARER);
		curl_easy_setopt(c, CURLOPT_XOAUTH2_BEARER, auth->bearer);
	} else if (auth && auth->user) {
		/* ⚠ BASIC AND DIGEST, NOT ANY. CURLAUTH_ANY includes NTLM and
		 * Negotiate, which will happily do a round trip against a hostile
		 * server; and it makes curl send an unauthenticated probe first, which
		 * doubles every request against servers that only ever wanted Basic. */
		curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)(CURLAUTH_BASIC | CURLAUTH_DIGEST));
		curl_easy_setopt(c, CURLOPT_USERNAME, auth->user);
		curl_easy_setopt(c, CURLOPT_PASSWORD, auth->pass ? auth->pass : "");
	}

	if (body && bodylen) {
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)bodylen);
	} else if (strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0) {
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, "");
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, 0L);
	}

	CURLcode rc = curl_easy_perform(c);
	if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &out->status);

	curl_slist_free_all(hdrs);
	curl_easy_cleanup(c);

	if (rc != CURLE_OK) {
		if (err) *err = xasprintf("%s", errbuf[0] ? errbuf : curl_easy_strerror(rc));
		http_resp_free(out);
		return false;
	}
	return true;
}
