/* caldav.c — RFC 4791 over the HTTP in http.c.
 *
 * ── Discovery, because nobody knows their calendar's URL ────────────────────
 *
 * People know "fastmail.com" or "my nextcloud address". The path to a
 * collection is four hops away from that, and RFC 6764 exists precisely so a
 * client can walk it:
 *
 *   /.well-known/caldav        →  the DAV root, usually by redirect
 *   PROPFIND current-user-principal   →  who you are
 *   PROPFIND calendar-home-set        →  where your calendars live
 *   PROPFIND Depth: 1                 →  the calendars themselves
 *
 * Each hop is skipped when the thing it looks for is already in hand, so
 * pasting a collection URL straight in works and costs one request.
 *
 * ── The XML is parsed, not scanned ──────────────────────────────────────────
 *
 * ⛔ THIS IS UNTRUSTED INPUT FROM THE NETWORK. A multistatus body looks regular
 * enough to tempt a strstr() — and then a href containing an escaped angle
 * bracket, or a namespace prefix that is `D:` on one server and `d:` on the
 * next, silently returns the wrong path and the client writes an event over
 * something else. libxml2 is already in libical's dependency closure, so a real
 * parser costs nothing that has not already been paid.
 *
 * ⚠ AND ENTITY EXPANSION STAYS OFF. XML_PARSE_NONET without XML_PARSE_NOENT:
 * with NOENT a server can hand back a document that reads /etc/passwd into a
 * calendar name, or one that expands to a gigabyte.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "caldav.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define NS_DAV     "DAV:"
#define NS_CALDAV  "urn:ietf:params:xml:ns:caldav"
#define NS_CS      "http://calendarserver.org/ns/"
#define NS_APPLE   "http://apple.com/ns/ical/"

typedef struct { char *url; http_auth_t auth; } dav_ctx_t;

/* ── URLs ───────────────────────────────────────────────────────────────── */

/* Resolve `href` — which a server may give as a full URL, an absolute path, or
 * (rarely, and against the spec) a relative one — against `base`. */
static char *url_resolve(const char *base, const char *href)
{
	if (!href || !*href) return NULL;
	if (strncasecmp(href, "http://", 7) == 0 || strncasecmp(href, "https://", 8) == 0)
		return xstrdup(href);

	/* scheme://host[:port] out of the base */
	const char *p = strstr(base, "://");
	if (!p) return xstrdup(href);
	const char *slash = strchr(p + 3, '/');
	size_t rootlen = slash ? (size_t)(slash - base) : strlen(base);

	if (href[0] == '/') return xasprintf("%.*s%s", (int)rootlen, base, href);

	char *b = xstrdup(base);
	char *last = strrchr(b + rootlen, '/');
	if (last) *(last + 1) = '\0';
	char *out = xasprintf("%s%s", b, href);
	free(b);
	return out;
}

/* The path part, which is what a server compares an href against. */
static char *url_path(const char *url)
{
	const char *p = strstr(url, "://");
	const char *slash = p ? strchr(p + 3, '/') : NULL;
	return xstrdup(slash ? slash : "/");
}

/* ── XML ────────────────────────────────────────────────────────────────── */

typedef struct { xmlDoc *doc; xmlXPathContext *xp; } dav_xml_t;

static bool dav_parse(const char *body, size_t len, dav_xml_t *out, char **err)
{
	memset(out, 0, sizeof *out);
	if (!body || !len) { if (err) *err = xstrdup("the server sent an empty response"); return false; }

	/* NONET: never fetch a DTD. No NOENT: never expand an entity. */
	out->doc = xmlReadMemory(body, (int)len, "dav.xml", NULL,
	                         XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR |
	                         XML_PARSE_NOWARNING);
	if (!out->doc) { if (err) *err = xstrdup("the server's XML would not parse"); return false; }

	out->xp = xmlXPathNewContext(out->doc);
	if (!out->xp) { xmlFreeDoc(out->doc); out->doc = NULL; return false; }

	/* ⚠ THE PREFIXES ARE OURS, NOT THE DOCUMENT'S. A server is free to call the
	 * DAV namespace D:, d:, or nothing at all; binding our own prefixes to the
	 * URIs is what makes one XPath work against all of them. */
	xmlXPathRegisterNs(out->xp, BAD_CAST "d", BAD_CAST NS_DAV);
	xmlXPathRegisterNs(out->xp, BAD_CAST "c", BAD_CAST NS_CALDAV);
	xmlXPathRegisterNs(out->xp, BAD_CAST "cs", BAD_CAST NS_CS);
	xmlXPathRegisterNs(out->xp, BAD_CAST "a", BAD_CAST NS_APPLE);
	return true;
}

static void dav_xml_free(dav_xml_t *x)
{
	if (x->xp) xmlXPathFreeContext(x->xp);
	if (x->doc) xmlFreeDoc(x->doc);
	memset(x, 0, sizeof *x);
}

/* The text of the first node matching `expr`, relative to `node` when given. */
static char *dav_text(dav_xml_t *x, xmlNode *node, const char *expr)
{
	x->xp->node = node ? node : xmlDocGetRootElement(x->doc);
	xmlXPathObject *o = xmlXPathEvalExpression(BAD_CAST expr, x->xp);
	char *out = NULL;
	if (o && o->nodesetval && o->nodesetval->nodeNr > 0) {
		xmlChar *t = xmlNodeGetContent(o->nodesetval->nodeTab[0]);
		if (t) { out = xstrdup((const char *)t); xmlFree(t); }
	}
	if (o) xmlXPathFreeObject(o);
	return out;
}

static bool dav_has(dav_xml_t *x, xmlNode *node, const char *expr)
{
	x->xp->node = node ? node : xmlDocGetRootElement(x->doc);
	xmlXPathObject *o = xmlXPathEvalExpression(BAD_CAST expr, x->xp);
	bool yes = o && o->nodesetval && o->nodesetval->nodeNr > 0;
	if (o) xmlXPathFreeObject(o);
	return yes;
}

/* ── requests ───────────────────────────────────────────────────────────── */

static bool propfind(const char *url, const http_auth_t *auth, const char *depth,
                     const char *body, http_resp_t *resp, char **err)
{
	char *dh = xasprintf("Depth: %s", depth);
	const char *hdrs[] = { dh, "Content-Type: application/xml; charset=utf-8" };
	bool ok = http_do("PROPFIND", url, hdrs, 2, body, strlen(body), auth, resp, err);
	free(dh);
	if (!ok) return false;
	if (resp->status != 207 && resp->status != 200) {
		if (err) *err = xasprintf("the server answered %ld to PROPFIND %s", resp->status, url);
		return false;
	}
	return true;
}

#define PF_PRINCIPAL \
    "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop>" \
    "<d:current-user-principal/></d:prop></d:propfind>"
#define PF_HOMESET \
    "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\" " \
    "xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop>" \
    "<c:calendar-home-set/></d:prop></d:propfind>"
#define PF_COLLS \
    "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\" " \
    "xmlns:c=\"urn:ietf:params:xml:ns:caldav\" " \
    "xmlns:cs=\"http://calendarserver.org/ns/\" " \
    "xmlns:a=\"http://apple.com/ns/ical/\"><d:prop>" \
    "<d:resourcetype/><d:displayname/><cs:getctag/><a:calendar-color/>" \
    "<c:supported-calendar-component-set/></d:prop></d:propfind>"
#define PF_ETAGS \
    "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop>" \
    "<d:getetag/><d:resourcetype/></d:prop></d:propfind>"

/* ── discovery ──────────────────────────────────────────────────────────── */

static void colls_add(caldav_colls_t *c, caldav_coll_t v)
{
	if (c->n == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 8;
		c->e = xrealloc(c->e, c->cap * sizeof *c->e);
	}
	c->e[c->n++] = v;
}

void caldav_colls_free(caldav_colls_t *c)
{
	for (size_t i = 0; i < c->n; i++) {
		free(c->e[i].url); free(c->e[i].name); free(c->e[i].ctag); free(c->e[i].color);
	}
	free(c->e);
	c->e = NULL; c->n = c->cap = 0;
}

/* Everything at Depth: 1 that says it is a calendar. */
static bool list_collections(const char *home, const http_auth_t *auth,
                             caldav_colls_t *out, char **err)
{
	http_resp_t resp;
	if (!propfind(home, auth, "1", PF_COLLS, &resp, err)) { http_resp_free(&resp); return false; }

	dav_xml_t x;
	if (!dav_parse(resp.body.b, resp.body.len, &x, err)) { http_resp_free(&resp); return false; }

	x.xp->node = xmlDocGetRootElement(x.doc);
	xmlXPathObject *o = xmlXPathEvalExpression(BAD_CAST "//d:response", x.xp);
	if (o && o->nodesetval) {
		for (int i = 0; i < o->nodesetval->nodeNr; i++) {
			xmlNode *r = o->nodesetval->nodeTab[i];
			if (!dav_has(&x, r, ".//d:resourcetype/c:calendar")) continue;

			char *href = dav_text(&x, r, "./d:href");
			if (!href) continue;

			caldav_coll_t v;
			memset(&v, 0, sizeof v);
			v.url = url_resolve(home, href);
			v.name = dav_text(&x, r, ".//d:displayname");
			v.ctag = dav_text(&x, r, ".//cs:getctag");
			v.color = dav_text(&x, r, ".//a:calendar-color");
			/* A server that does not say which components it holds holds both;
			 * that is what RFC 4791 means by the property being optional. */
			bool said = dav_has(&x, r, ".//c:supported-calendar-component-set/c:comp");
			v.events = !said || dav_has(&x, r, ".//c:comp[@name='VEVENT']");
			v.todos  = !said || dav_has(&x, r, ".//c:comp[@name='VTODO']");
			if (!v.name) v.name = xstrdup(href);
			colls_add(out, v);
			free(href);
		}
	}
	if (o) xmlXPathFreeObject(o);
	dav_xml_free(&x);
	http_resp_free(&resp);
	return true;
}

bool caldav_discover(const char *entered, const http_auth_t *auth,
                     caldav_colls_t *out, char **err)
{
	memset(out, 0, sizeof *out);
	if (err) *err = NULL;

	/* A bare domain becomes https, never http: the first request carries the
	 * password, and "upgrade to TLS afterwards" upgrades nothing. */
	char *url = (strncasecmp(entered, "http://", 7) == 0 ||
	             strncasecmp(entered, "https://", 8) == 0)
	          ? xstrdup(entered) : xasprintf("https://%s", entered);

	/* 1. Is it already a collection? One request, and pasting a URL from a
	 *    provider's help page just works. */
	if (list_collections(url, auth, out, NULL) && out->n > 0) { free(url); return true; }

	/* 2. RFC 6764. The redirect is followed by http.c. */
	char *path = url_path(url);
	char *well_known = (strcmp(path, "/") == 0)
	                 ? xasprintf("%s/.well-known/caldav", url) : xstrdup(url);
	free(path);

	http_resp_t resp;
	char *principal = NULL, *home = NULL;

	if (propfind(well_known, auth, "0", PF_PRINCIPAL, &resp, err)) {
		dav_xml_t x;
		if (dav_parse(resp.body.b, resp.body.len, &x, NULL)) {
			char *href = dav_text(&x, NULL, "//d:current-user-principal/d:href");
			if (href) { principal = url_resolve(well_known, href); free(href); }
			dav_xml_free(&x);
		}
	}
	http_resp_free(&resp);

	if (!principal) {
		if (err && !*err)
			*err = xasprintf("%s did not answer as a CalDAV server — check the address, "
			                 "and that the password is an app password if the account has "
			                 "two-factor authentication", entered);
		free(url); free(well_known);
		return false;
	}

	if (propfind(principal, auth, "0", PF_HOMESET, &resp, err)) {
		dav_xml_t x;
		if (dav_parse(resp.body.b, resp.body.len, &x, NULL)) {
			char *href = dav_text(&x, NULL, "//c:calendar-home-set/d:href");
			if (href) { home = url_resolve(principal, href); free(href); }
			dav_xml_free(&x);
		}
	}
	http_resp_free(&resp);

	bool ok = false;
	if (home) ok = list_collections(home, auth, out, err);
	else if (err && !*err) *err = xstrdup("the server named a principal but no calendar home");

	free(url); free(well_known); free(principal); free(home);
	return ok && out->n > 0;
}

/* ── the remote ─────────────────────────────────────────────────────────── */

static bool dav_list(remote_t *r, remote_list_t *out, char **err)
{
	dav_ctx_t *d = r->ctx;
	http_resp_t resp;
	if (!propfind(d->url, &d->auth, "1", PF_ETAGS, &resp, err)) { http_resp_free(&resp); return false; }

	dav_xml_t x;
	if (!dav_parse(resp.body.b, resp.body.len, &x, err)) { http_resp_free(&resp); return false; }

	char *self = url_path(d->url);
	size_t selflen = strlen(self);

	x.xp->node = xmlDocGetRootElement(x.doc);
	xmlXPathObject *o = xmlXPathEvalExpression(BAD_CAST "//d:response", x.xp);
	if (o && o->nodesetval) {
		for (int i = 0; i < o->nodesetval->nodeNr; i++) {
			xmlNode *n = o->nodesetval->nodeTab[i];
			char *href = dav_text(&x, n, "./d:href");
			if (!href) continue;

			/* ⚠ SKIP THE COLLECTION ITSELF. Depth: 1 includes the directory,
			 * and it has no ETag — treating it as an item makes every sync try
			 * to GET the calendar as though it were an event. The trailing
			 * slash is optional in what a server returns, so both are checked. */
			bool is_self = strncmp(href, self, selflen) == 0 &&
			               (href[selflen] == '\0' ||
			                (href[selflen] == '/' && href[selflen + 1] == '\0'));
			char *etag = dav_text(&x, n, ".//d:getetag");
			if (!is_self && etag) rlist_add(out, href, etag);
			free(etag);
			free(href);
		}
	}
	if (o) xmlXPathFreeObject(o);
	free(self);
	dav_xml_free(&x);
	http_resp_free(&resp);
	return true;
}

static char *dav_get(remote_t *r, const char *href, size_t *len, char **etag, char **err)
{
	dav_ctx_t *d = r->ctx;
	char *url = url_resolve(d->url, href);
	http_resp_t resp;
	bool ok = http_do("GET", url, NULL, 0, NULL, 0, &d->auth, &resp, err);
	free(url);
	if (!ok) { http_resp_free(&resp); return NULL; }
	if (resp.status < 200 || resp.status >= 300) {
		if (err) *err = xasprintf("the server answered %ld", resp.status);
		http_resp_free(&resp);
		return NULL;
	}
	*len = resp.body.len;
	*etag = resp.etag ? xstrdup(resp.etag) : NULL;
	char *data = resp.body.b;
	resp.body.b = NULL;                      /* ownership moves to the caller */
	http_resp_free(&resp);
	return data;
}

static bool dav_put(remote_t *r, const char *href, const void *data, size_t len,
                    const char *if_match, char **new_etag, bool *conflict, char **err)
{
	dav_ctx_t *d = r->ctx;
	*conflict = false;
	char *url = url_resolve(d->url, href);

	/* ⛔ ALWAYS CONDITIONAL. If-Match when we believe a version exists,
	 * If-None-Match: * when we believe none does. An unconditional PUT is how
	 * two clients creating the same event silently keep only one of them. */
	char *cond = if_match ? xasprintf("If-Match: %s", if_match)
	                      : xstrdup("If-None-Match: *");
	const char *hdrs[] = { "Content-Type: text/calendar; charset=utf-8", cond };

	http_resp_t resp;
	bool ok = http_do("PUT", url, hdrs, 2, data, len, &d->auth, &resp, err);
	free(cond); free(url);
	if (!ok) { http_resp_free(&resp); return false; }

	if (resp.status == 412 || resp.status == 409) { *conflict = true; http_resp_free(&resp); return true; }
	if (resp.status < 200 || resp.status >= 300) {
		if (err) *err = xasprintf("the server answered %ld to the upload", resp.status);
		http_resp_free(&resp);
		return false;
	}
	/* ⚠ AN ETag IS OPTIONAL ON A PUT and plenty of servers omit it. NULL here
	 * means "we do not know the new version", which the engine records as such
	 * rather than inventing one — the next listing supplies it. */
	*new_etag = resp.etag ? xstrdup(resp.etag) : NULL;
	http_resp_free(&resp);
	return true;
}

static bool dav_del(remote_t *r, const char *href, const char *if_match,
                    bool *conflict, char **err)
{
	dav_ctx_t *d = r->ctx;
	*conflict = false;
	char *url = url_resolve(d->url, href);
	char *cond = if_match ? xasprintf("If-Match: %s", if_match) : NULL;
	const char *hdrs[] = { cond ? cond : "" };

	http_resp_t resp;
	bool ok = http_do("DELETE", url, cond ? hdrs : NULL, cond ? 1 : 0, NULL, 0,
	                  &d->auth, &resp, err);
	free(cond); free(url);
	if (!ok) { http_resp_free(&resp); return false; }

	if (resp.status == 412) { *conflict = true; http_resp_free(&resp); return true; }
	/* 404 is success: it is gone, which is what was asked for. */
	bool good = (resp.status >= 200 && resp.status < 300) || resp.status == 404;
	if (!good && err) *err = xasprintf("the server answered %ld to the delete", resp.status);
	http_resp_free(&resp);
	return good;
}

/* ⛔ NOT THE PERCENT-ENCODED UID. That was the first version and a real server
 * rejected it: most stacks decode %2F before routing, see a path traversal and
 * answer 403 — while the identical event under a plain name answers 201. The
 * href is the server's address for the resource and nothing more; ics_safe_name
 * explains what replaces it. */
static char *dav_href_for(remote_t *r, const char *uid)
{
	dav_ctx_t *d = r->ctx;
	char *path = url_path(d->url);
	char *name = ics_safe_name(uid);
	size_t plen = strlen(path);
	char *href = (plen && path[plen - 1] == '/')
	           ? xasprintf("%s%s.ics", path, name)
	           : xasprintf("%s/%s.ics", path, name);
	free(path); free(name);
	return href;
}

remote_t *caldav_remote(const char *collection_url, const http_auth_t *auth)
{
	dav_ctx_t *d = xmalloc(sizeof *d);
	memset(d, 0, sizeof *d);
	d->url = xstrdup(collection_url);
	d->auth.user = auth->user ? xstrdup(auth->user) : NULL;
	d->auth.pass = auth->pass ? xstrdup(auth->pass) : NULL;
	d->auth.bearer = auth->bearer ? xstrdup(auth->bearer) : NULL;
	d->auth.timeout_s = auth->timeout_s;
	d->auth.insecure = auth->insecure;

	remote_t *r = xmalloc(sizeof *r);
	r->list = dav_list; r->get = dav_get; r->put = dav_put;
	r->del = dav_del; r->href_for = dav_href_for; r->ctx = d;
	return r;
}

void caldav_remote_free(remote_t *r)
{
	if (!r) return;
	dav_ctx_t *d = r->ctx;
	free(d->url); free(d->auth.user); free(d->auth.pass); free(d->auth.bearer);
	free(d);
	free(r);
}
