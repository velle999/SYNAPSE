/* oauth.c — OAuth 2 with PKCE, for a program that cannot keep a secret.
 *
 * ── Why there is no client secret here ──────────────────────────────────────
 *
 * ⛔ A DESKTOP APPLICATION IS A PUBLIC CLIENT. Whatever "secret" it is issued
 * ships inside the binary on every machine that installs it, so it is not a
 * secret and treating it as one is theatre. Google and Microsoft both say so;
 * the answer is PKCE (RFC 7636), and it works without one.
 *
 * The verifier is 43 random characters this process invents. Its SHA-256 goes
 * out through the browser as the challenge, where a malicious app on the same
 * machine, a shoulder, or a logging proxy may see it — and that is fine,
 * because the verifier itself only ever travels to the token endpoint over TLS.
 * An intercepted redirect therefore yields a code nobody else can exchange.
 *
 * ⛔ S256, NEVER "plain". The spec permits sending the verifier itself as the
 * challenge and it is worth exactly nothing: anyone who can see the redirect
 * can then exchange the code. If a server ever refuses S256 the right answer is
 * to refuse the server.
 *
 * ── And the redirect comes back to this machine ─────────────────────────────
 *
 * The redirect URI is http://127.0.0.1:<port>/ — a loopback listener this
 * process opens, on a port the kernel picks. That is the flow both providers
 * document for installed applications, and it is http rather than https
 * deliberately: the connection never leaves the machine, and a self-signed
 * certificate on localhost would make the browser warn about the one hop that
 * is not carrying anything anyway.
 *
 * `state` is checked. Without it any page you visit during the flow can hand
 * your listener a code of its own choosing.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "oauth.h"
#include "http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <json-c/json.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ── providers ──────────────────────────────────────────────────────────── */

const oauth_provider_t *oauth_google(void)
{
	/* ⚠ THE CALENDAR SCOPE, NOT calendar.readonly. Two-way sync writes. */
	static const oauth_provider_t p = {
		"https://accounts.google.com/o/oauth2/v2/auth",
		"https://oauth2.googleapis.com/token",
		"https://www.googleapis.com/auth/calendar",
	};
	return &p;
}

const oauth_provider_t *oauth_microsoft(void)
{
	/* offline_access is what makes a refresh token come back at all; without it
	 * the flow succeeds and the account stops working in an hour. */
	static const oauth_provider_t p = {
		"https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
		"https://login.microsoftonline.com/common/oauth2/v2.0/token",
		"offline_access Calendars.ReadWrite",
	};
	return &p;
}

/* ── encoding ───────────────────────────────────────────────────────────── */

char *b64url(const void *data, size_t len)
{
	gchar *b64 = g_base64_encode(data, len);
	buf_t out;
	buf_init(&out);
	for (gchar *p = b64; *p; p++) {
		char c = *p == '+' ? '-' : (*p == '/' ? '_' : *p);
		if (c == '=') continue;                      /* padding is a separator */
		buf_add(&out, &c, 1);
	}
	g_free(b64);
	if (!out.b) out.b = xstrdup("");
	return out.b;
}

char *pkce_verifier(void)
{
	/* ⛔ FROM THE KERNEL, NOT FROM rand(). This value is the only thing standing
	 * between an intercepted redirect and a usable token. */
	unsigned char raw[32];
	FILE *f = fopen("/dev/urandom", "rb");
	if (!f || fread(raw, 1, sizeof raw, f) != sizeof raw) {
		if (f) fclose(f);
		die("cannot read /dev/urandom, and a guessable verifier is worse than none");
	}
	fclose(f);
	return b64url(raw, sizeof raw);      /* 43 unreserved characters */
}

char *pkce_challenge(const char *verifier)
{
	gsize len = 32;
	guchar digest[32];
	GChecksum *c = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(c, (const guchar *)verifier, (gssize)strlen(verifier));
	g_checksum_get_digest(c, digest, &len);
	g_checksum_free(c);
	return b64url(digest, len);
}

static char *url_escape(const char *s)
{
	return pct_encode(s, false);
}

/* ── little parsers ─────────────────────────────────────────────────────── */

char *query_get(const char *query, const char *key)
{
	if (!query) return NULL;
	size_t klen = strlen(key);
	const char *p = query;
	while (p && *p) {
		const char *amp = strchr(p, '&');
		size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
		if (seglen > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
			const char *v = p + klen + 1;
			size_t vlen = seglen - klen - 1;
			char *raw = xmalloc(vlen + 1);
			memcpy(raw, v, vlen);
			raw[vlen] = '\0';
			/* A form-encoded value uses '+' for a space as well as %20. */
			for (char *c = raw; *c; c++) if (*c == '+') *c = ' ';
			char *out = pct_decode(raw);
			free(raw);
			return out;
		}
		p = amp ? amp + 1 : NULL;
	}
	return NULL;
}

/* ⚠ A REAL JSON PARSER FOR A TOKEN RESPONSE. It is untrusted input off a
 * network, and hand-rolling string unescaping is where that goes wrong — a
 * token containing a backslash is unremarkable and a naive reader truncates it,
 * producing an authentication failure that looks like a revoked grant. */
char *json_str(const char *json, const char *key)
{
	if (!json) return NULL;
	json_object *root = json_tokener_parse(json);
	if (!root) return NULL;
	json_object *v = NULL;
	char *out = NULL;
	if (json_object_object_get_ex(root, key, &v) && json_object_is_type(v, json_type_string))
		out = xstrdup(json_object_get_string(v));
	json_object_put(root);
	return out;
}

long json_num(const char *json, const char *key, long fallback)
{
	if (!json) return fallback;
	json_object *root = json_tokener_parse(json);
	if (!root) return fallback;
	json_object *v = NULL;
	long out = fallback;
	if (json_object_object_get_ex(root, key, &v) &&
	    (json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double)))
		out = (long)json_object_get_int64(v);
	json_object_put(root);
	return out;
}

void oauth_tokens_free(oauth_tokens_t *t)
{
	if (t->access_token) { memset(t->access_token, 0, strlen(t->access_token)); free(t->access_token); }
	if (t->refresh_token) { memset(t->refresh_token, 0, strlen(t->refresh_token)); free(t->refresh_token); }
	memset(t, 0, sizeof *t);
}

/* ── the loopback listener ──────────────────────────────────────────────── */

#define REDIRECT_PAGE \
    "<!doctype html><meta charset=utf-8><title>syn-cal</title>" \
    "<style>body{font:16px/1.6 system-ui,sans-serif;background:#16171c;color:#e9eaef;" \
    "display:grid;place-items:center;height:100vh;margin:0;text-align:center}" \
    "p{color:#9aa0ad}</style>" \
    "<div><h1>%s</h1><p>%s</p></div>"

static int listen_loopback(int *port, char **err)
{
	/* ⛔ 127.0.0.1, NOT 0.0.0.0. Binding this to every interface would put an
	 * endpoint that accepts an authorisation code on the local network for the
	 * length of the flow. */
	/* ⛔ SOCK_CLOEXEC. Without it this descriptor survives the exec below it and
	 * the browser inherits a listener that accepts authorisation codes, held
	 * open for as long as the browser runs — long after syn-cal has exited. */
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) { if (err) *err = xstrdup("cannot open a socket"); return -1; }

	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

	struct sockaddr_in a;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = 0;                              /* the kernel picks */

	/* ⚠ A BACKLOG OF ONE IS NOT ENOUGH. A Chromium browser opens speculative
	 * connections alongside the one carrying the redirect; a queue that only
	 * holds one drops the request that matters. */
	if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(fd, 8) != 0) {
		if (err) *err = xasprintf("cannot listen on the loopback: %s", strerror(errno));
		close(fd);
		return -1;
	}

	socklen_t alen = sizeof a;
	if (getsockname(fd, (struct sockaddr *)&a, &alen) != 0) {
		if (err) *err = xstrdup("cannot ask which port was chosen");
		close(fd);
		return -1;
	}
	*port = ntohs(a.sin_port);
	return fd;
}

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* The query string off one accepted connection: "" for a request that carried
 * none, NULL for a peer that said nothing at all. */
static char *read_query(int c)
{
	/* The request line is all that is needed and it arrives first. A bounded
	 * read, because the peer is a browser and this is still a network socket. */
	char req[8192];
	size_t got = 0;
	while (got < sizeof req - 1) {
		struct pollfd p2 = { c, POLLIN, 0 };
		if (poll(&p2, 1, 5000) <= 0) break;
		ssize_t n = read(c, req + got, sizeof req - 1 - got);
		if (n <= 0) break;
		got += (size_t)n;
		req[got] = '\0';
		if (strstr(req, "\r\n\r\n") || strchr(req, '\n')) break;
	}
	if (!got) return NULL;
	req[got] = '\0';

	char *sp = strchr(req, ' ');
	if (!sp) return NULL;
	char *path = sp + 1;
	char *end = strpbrk(path, " \r\n");
	if (end) *end = '\0';
	char *q = strchr(path, '?');
	return xstrdup(q ? q + 1 : "");
}

/* Wait for the browser to arrive, take the query string off the request line,
 * answer with a page, and return the query.
 *
 * ⚠ THE FIRST CONNECTION IS NOT NECESSARILY THE REDIRECT. A Chromium browser
 * preconnects to a host before it fetches from it, and it asks for a favicon
 * afterwards; accepting exactly one connection and reading whatever it holds
 * means the flow can consume an empty socket and report that the browser never
 * came back — while the request carrying the code sits unanswered in the queue.
 * Every connection is answered, and only the one carrying `code` or `error`
 * ends the wait. */
static char *await_redirect(int fd, long timeout_s, char **err)
{
	long deadline = now_ms() + timeout_s * 1000L;

	for (;;) {
		long left = deadline - now_ms();
		if (left <= 0) {
			if (err) *err = xstrdup("timed out waiting for the browser to come back");
			return NULL;
		}

		struct pollfd pfd = { fd, POLLIN, 0 };
		int ready = poll(&pfd, 1, (int)(left > 60000 ? 60000 : left));
		if (ready < 0) {
			if (errno == EINTR) continue;
			if (err) *err = xasprintf("waiting for the browser failed: %s", strerror(errno));
			return NULL;
		}
		if (ready == 0) continue;                    /* not the deadline yet */

		int c = accept(fd, NULL, NULL);
		if (c < 0) {
			if (errno == EINTR || errno == ECONNABORTED) continue;
			if (err) *err = xstrdup("the browser's connection could not be accepted");
			return NULL;
		}

		char *query = read_query(c);
		char *code = query ? query_get(query, "code") : NULL;
		char *oerr = query ? query_get(query, "error") : NULL;

		if (!code && !oerr) {
			/* Something the browser opened on the way past. Answer it so it is
			 * not left hanging, and keep waiting for the real one. */
			static const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
			                         "Connection: close\r\n\r\n";
			ssize_t w = write(c, nf, sizeof nf - 1);
			(void)w;
			close(c);
			free(query);
			continue;
		}
		bool have_code = code != NULL;
		free(code); free(oerr);

		const char *title = "You can close this window";
		const char *sub = "syn-cal has what it needs.";
		if (!have_code) {
			title = "Something went wrong";
			sub = "syn-cal did not receive an authorisation code. Try again in the terminal.";
		}

		char *body = xasprintf(REDIRECT_PAGE, title, sub);
		char *head = xasprintf("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
		                       "Content-Length: %zu\r\nConnection: close\r\n\r\n", strlen(body));
		ssize_t w = write(c, head, strlen(head));
		if (w > 0) w = write(c, body, strlen(body));
		(void)w;
		free(head); free(body);
		close(c);
		return query;
	}
}

static void open_in_browser(const char *url)
{
	pid_t pid = fork();
	if (pid == 0) {
		/* ⚠ DETACHED FROM THIS PROCESS'S PIPES. A browser inherits stdout and
		 * keeps it open long after it has opened the tab; leaving it attached
		 * makes anything reading syn-cal's output wait for the browser to
		 * quit. Same lesson as the detached child in quickshell. */
		int null = open("/dev/null", O_RDWR);
		if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); if (null > 2) close(null); }
		setsid();
		/* ⛔ AND FORKED AGAIN, BECAUSE xdg-open DOES NOT RETURN. Its generic
		 * fallback execs the browser and waits for it to quit, so waiting on
		 * this child parks the sign-in for as long as the browser is open —
		 * nothing ever calls accept(), the redirect sits unanswered in the
		 * listener's queue, and the page in the browser spins after consent.
		 * The grandchild is reparented to init; the intermediate exits now, so
		 * the wait below returns immediately and there is no zombie. */
		if (fork() == 0) {
			execlp("xdg-open", "xdg-open", url, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0) waitpid(pid, NULL, 0);   /* the intermediate, which exits at once */
}

/* ── the flow ───────────────────────────────────────────────────────────── */

/* "&client_secret=…", or "" when the provider does not want one. Always returns
 * a string the caller must free, so the two body builders below stay one
 * xasprintf each instead of branching. */
static char *secret_field(const char *client_secret)
{
	if (!client_secret || !*client_secret) return xstrdup("");
	char *e = url_escape(client_secret);
	char *f = xasprintf("&client_secret=%s", e);
	free(e);
	return f;
}

static bool exchange(const oauth_provider_t *p, const char *body,
                     oauth_tokens_t *out, char **err)
{
	http_auth_t auth = { NULL, NULL, NULL, 30, false };
	const char *hdrs[] = { "Content-Type: application/x-www-form-urlencoded" };
	http_resp_t resp;
	if (!http_do("POST", p->token_url, hdrs, 1, body, strlen(body), &auth, &resp, err)) {
		http_resp_free(&resp);
		return false;
	}

	if (resp.status < 200 || resp.status >= 300) {
		/* ⚠ THE PROVIDER'S OWN WORDS. "invalid_grant" against a 400 is the
		 * difference between a revoked account and a clock that is wrong, and
		 * inventing a friendlier message throws that away. */
		char *e = json_str(resp.body.b, "error");
		char *d = json_str(resp.body.b, "error_description");
		if (err) *err = xasprintf("the sign-in was refused (%ld): %s%s%s",
		                          resp.status, e ? e : "no reason given",
		                          d ? " — " : "", d ? d : "");
		free(e); free(d);
		http_resp_free(&resp);
		return false;
	}

	memset(out, 0, sizeof *out);
	out->access_token = json_str(resp.body.b, "access_token");
	out->refresh_token = json_str(resp.body.b, "refresh_token");
	out->expires_in = json_num(resp.body.b, "expires_in", 3600);
	http_resp_free(&resp);

	if (!out->access_token) {
		if (err) *err = xstrdup("the server's answer carried no access token");
		return false;
	}
	return true;
}

bool oauth_authorise(const oauth_provider_t *p, const char *client_id,
                     const char *client_secret,
                     bool open_browser, long timeout_s, oauth_tokens_t *out, char **err)
{
	if (err) *err = NULL;
	if (!client_id || !*client_id) {
		if (err) *err = xstrdup("no OAuth client id is set for this account");
		return false;
	}
	if (timeout_s <= 0) timeout_s = 300;

	int port = 0;
	int fd = listen_loopback(&port, err);
	if (fd < 0) return false;

	char *verifier = pkce_verifier();
	char *challenge = pkce_challenge(verifier);
	char *state = pkce_verifier();          /* same source, same strength */
	char *redirect = xasprintf("http://127.0.0.1:%d/", port);

	char *e_client = url_escape(client_id);
	char *e_redir = url_escape(redirect);
	char *e_scope = url_escape(p->scope);

	char *url = xasprintf(
	    "%s?response_type=code&client_id=%s&redirect_uri=%s&scope=%s"
	    "&code_challenge=%s&code_challenge_method=S256&state=%s"
	    /* offline + consent, or Google returns a refresh token on the FIRST
	     * authorisation only and the account silently stops working an hour
	     * after the second one. */
	    "&access_type=offline&prompt=consent",
	    p->auth_url, e_client, e_redir, e_scope, challenge, state);
	free(e_client); free(e_redir); free(e_scope);

	if (open_browser) {
		fprintf(stderr, "Opening your browser to sign in…\n");
		open_in_browser(url);
		fprintf(stderr, "If nothing opened, visit:\n\n  %s\n\n", url);
	} else {
		printf("%s\n", url);
		fflush(stdout);
	}

	char *query = await_redirect(fd, timeout_s, err);
	close(fd);

	bool ok = false;
	if (query) {
		char *code = query_get(query, "code");
		char *got_state = query_get(query, "state");
		char *oerr = query_get(query, "error");

		if (oerr) {
			if (err) *err = xasprintf("the sign-in was cancelled or refused: %s", oerr);
		} else if (!code) {
			if (err) *err = xstrdup("the browser came back with no authorisation code");
		} else if (!got_state || strcmp(got_state, state) != 0) {
			/* ⛔ A MISMATCHED state IS AN ATTACK, NOT A GLITCH. Some other page
			 * handed this listener a code; exchanging it would attach somebody
			 * else's calendar to this account. */
			if (err) *err = xstrdup("the redirect did not match the request this "
			                        "process started; nothing was exchanged");
		} else {
			char *e_code = url_escape(code);
			char *e_client2 = url_escape(client_id);
			char *e_redir2 = url_escape(redirect);
			char *e_ver = url_escape(verifier);
			char *e_sec = secret_field(client_secret);
			char *body = xasprintf("grant_type=authorization_code&code=%s&client_id=%s"
			                       "&redirect_uri=%s&code_verifier=%s%s",
			                       e_code, e_client2, e_redir2, e_ver, e_sec);
			ok = exchange(p, body, out, err);
			memset(body, 0, strlen(body));
			memset(e_sec, 0, strlen(e_sec));
			free(body); free(e_code); free(e_client2); free(e_redir2); free(e_ver);
			free(e_sec);
		}
		free(code); free(got_state); free(oerr);
	}

	memset(verifier, 0, strlen(verifier));
	free(verifier); free(challenge); free(state); free(redirect); free(url); free(query);
	return ok;
}

bool oauth_refresh(const oauth_provider_t *p, const char *client_id,
                   const char *client_secret,
                   const char *refresh_token, oauth_tokens_t *out, char **err)
{
	char *e_client = url_escape(client_id);
	char *e_tok = url_escape(refresh_token);
	/* ⚠ THE REFRESH NEEDS IT TOO. Sending the secret only on the first exchange
	 * buys a sign-in that works once and then stops an hour later, which is a
	 * far worse failure than one that never worked. */
	char *e_sec = secret_field(client_secret);
	char *body = xasprintf("grant_type=refresh_token&refresh_token=%s&client_id=%s%s",
	                       e_tok, e_client, e_sec);
	bool ok = exchange(p, body, out, err);
	memset(body, 0, strlen(body));
	memset(e_sec, 0, strlen(e_sec));
	free(body); free(e_client); free(e_tok); free(e_sec);

	/* ⚠ A REFRESH OFTEN ANSWERS WITHOUT A NEW REFRESH TOKEN, which means the
	 * old one is still good. Storing the empty answer over it would log the
	 * account out at the next expiry. */
	if (ok && !out->refresh_token) out->refresh_token = xstrdup(refresh_token);
	return ok;
}
