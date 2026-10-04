/* util.c — output, paths, and the two sockets this program talks to.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synmouse.h"
#include "i18n.h"
#include "config.h"

#include <errno.h>
#include <glob.h>
#include <locale.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

out_mode_t g_out = OUT_HUMAN;

/*
 * ⛔ LC_NUMERIC STAYS AT C. Intervals are seconds with a decimal point in the
 * bindings file, in every record and in what the window sends back; a German
 * locale's "0,5" in a record is a window that has misread how often it is
 * about to press a key.
 */
void syn_mouse_i18n_init(void)
{
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	const char *dir = getenv("SYN_MOUSE_LOCALEDIR");
	bindtextdomain(SYN_MOUSE_GETTEXT_DOMAIN,
	               dir && *dir ? dir : SYNMOUSE_LOCALEDIR);
	bind_textdomain_codeset(SYN_MOUSE_GETTEXT_DOMAIN, "UTF-8");
	textdomain(SYN_MOUSE_GETTEXT_DOMAIN);
}

/* ⚠ THE "syn-mouse: " PREFIX IS FOR A TERMINAL, so --rec drops it — a front
 * end already knows what it ran and puts this text straight in front of
 * somebody. */
static void say_who(void)
{
	if (g_out != OUT_REC) fputs("syn-mouse: ", stderr);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	say_who();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void die(const char *fmt, ...)
{
	va_list ap;
	say_who();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void rec_header(const char *fields)
{
	if (g_out == OUT_REC) printf("%s\n", fields);
}

void rec_row(const char *fmt, ...)
{
	va_list ap;
	if (g_out != OUT_REC) return;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

char *xstrdup(const char *s)
{
	char *p = strdup(s ? s : "");
	if (!p) die("out of memory");
	return p;
}

char *xasprintf(const char *fmt, ...)
{
	va_list ap;
	char *p = NULL;
	va_start(ap, fmt);
	if (vasprintf(&p, fmt, ap) < 0) p = NULL;
	va_end(ap);
	if (!p) die("out of memory");
	return p;
}

/* ⛔ EVERY FIELD IN A --rec ROW IS PERCENT-ENCODED. Rows are tab-separated
 * and a window title may contain a tab — or a newline, which would end the
 * record early and hand the front end a row it reads as two.
 *
 * ⚠ ONLY WHAT WOULD BREAK THE ROW IS ENCODED: control characters, DEL and
 * the percent sign itself. A title's parentheses and its UTF-8 stay as they
 * are, so a record is still readable by a person and by grep, and the window's
 * decodeURIComponent() takes either form back. */
char *pct_encode(const char *s)
{
	static const char *hex = "0123456789ABCDEF";
	size_t n = strlen(s);
	char *out = malloc(n * 3 + 1);
	if (!out) die("out of memory");
	char *o = out;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (*p >= 0x20 && *p != 0x7f && *p != '%') {
			*o++ = (char)*p;
		} else {
			*o++ = '%'; *o++ = hex[*p >> 4]; *o++ = hex[*p & 15];
		}
	}
	*o = '\0';
	return out;
}

int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ── paths ───────────────────────────────────────────────────────────────── */

/* ⚠ SYNMOUSE_HOME EXISTS FOR THE TESTS. A suite that rewrote the bindings of
 * whoever ran it would change what their mouse does in their next game. */
char *config_dir(void)
{
	const char *h = getenv("SYNMOUSE_HOME");
	if (h && *h) return xstrdup(h);
	const char *x = getenv("XDG_CONFIG_HOME");
	if (x && *x) return xasprintf("%s/syn-mouse", x);
	const char *home = getenv("HOME");
	if (!home || !*home) die("no HOME set");
	return xasprintf("%s/.config/syn-mouse", home);
}

char *config_path(void)
{
	char *d = config_dir();
	char *p = xasprintf("%s/bindings.conf", d);
	free(d);
	return p;
}

char *control_socket_path(void)
{
	const char *s = getenv("SYNMOUSE_SOCKET");
	if (s && *s) return xstrdup(s);
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (!run || !*run) return NULL;
	return xasprintf("%s/syn-mouse.sock", run);
}

static bool can_connect(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return false;
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	bool ok = connect(fd, (struct sockaddr *)&a, sizeof a) == 0;
	close(fd);
	return ok;
}

/*
 * Where synui's control socket is.
 *
 * ⚠ THE SERVICE STARTS BEFORE THE COMPOSITOR. The user manager reaches
 * default.target while the login is still bringing synui up, so this process
 * may have been started with no WAYLAND_DISPLAY at all, and it is asked again
 * on every poll rather than once. SYNUI_SOCKET first (synui exports it to what
 * it spawns), then the socket named after WAYLAND_DISPLAY, then whichever
 * synui socket in the runtime directory answers — wayland-0's before any
 * other, because a nested test compositor is the other thing that leaves one
 * there.
 */
char *synui_socket_path(void)
{
	const char *s = getenv("SYNMOUSE_SYNUI_SOCKET");
	if (s) return *s ? xstrdup(s) : NULL;
	s = getenv("SYNUI_SOCKET");
	if (s && *s && can_connect(s)) return xstrdup(s);

	const char *run = getenv("XDG_RUNTIME_DIR");
	if (!run || !*run) return NULL;

	const char *disp = getenv("WAYLAND_DISPLAY");
	if (disp && *disp) {
		char *p = xasprintf("%s/synui-%s.sock", run, disp);
		if (can_connect(p)) return p;
		free(p);
	}
	char *p = xasprintf("%s/synui-wayland-0.sock", run);
	if (can_connect(p)) return p;
	free(p);

	char *pat = xasprintf("%s/synui-*.sock", run);
	glob_t g;
	char *hit = NULL;
	if (glob(pat, 0, NULL, &g) == 0) {
		for (size_t i = 0; i < g.gl_pathc && !hit; i++)
			if (can_connect(g.gl_pathv[i])) hit = xstrdup(g.gl_pathv[i]);
		globfree(&g);
	}
	free(pat);
	return hit;
}

/* ── JSON, as much of it as synui's replies need ─────────────────────────── */

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Unescape the JSON string starting just after its opening quote. Returns a
 * pointer past the closing quote, or NULL. */
static const char *json_unescape(const char *p, const char *end, char *out, size_t n)
{
	size_t o = 0;
	while (p < end && *p != '"') {
		unsigned cp = (unsigned char)*p++;
		if (cp == '\\' && p < end) {
			char c = *p++;
			switch (c) {
			case 'n': cp = '\n'; break;
			case 't': cp = '\t'; break;
			case 'r': cp = '\r'; break;
			case 'b': cp = '\b'; break;
			case 'f': cp = '\f'; break;
			case 'u': {
				if (end - p < 4) return NULL;
				int v = 0;
				for (int i = 0; i < 4; i++) {
					int h = hexval(p[i]);
					if (h < 0) return NULL;
					v = v * 16 + h;
				}
				p += 4;
				cp = (unsigned)v;
				/* Encode the code point; a lone surrogate becomes U+FFFD. */
				if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
				char u[4]; int k = 0;
				if (cp < 0x80) u[k++] = (char)cp;
				else if (cp < 0x800) { u[k++] = (char)(0xC0 | cp >> 6); u[k++] = (char)(0x80 | (cp & 63)); }
				else { u[k++] = (char)(0xE0 | cp >> 12); u[k++] = (char)(0x80 | ((cp >> 6) & 63)); u[k++] = (char)(0x80 | (cp & 63)); }
				for (int i = 0; i < k; i++) if (o + 1 < n) out[o++] = u[i];
				continue;
			}
			default: cp = (unsigned char)c; break;
			}
		}
		if (o + 1 < n) out[o++] = (char)cp;
	}
	if (n) out[o] = '\0';
	return p < end ? p + 1 : NULL;
}

bool json_string_member(const char *obj, size_t len, const char *key,
                        char *out, size_t n)
{
	const char *end = obj + len;
	size_t klen = strlen(key);
	int depth = 0;
	const char *p = obj;

	while (p < end) {
		char c = *p;
		if (c == '{' || c == '[') { depth++; p++; continue; }
		if (c == '}' || c == ']') { depth--; p++; continue; }
		if (c != '"') { p++; continue; }

		/* A string: a member name if a ':' follows it at depth 1. */
		char name[64];
		const char *after = json_unescape(p + 1, end, name, sizeof name);
		if (!after) return false;
		const char *q = after;
		while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
		if (depth == 1 && q < end && *q == ':' && strlen(name) == klen &&
		    !memcmp(name, key, klen)) {
			q++;
			while (q < end && (*q == ' ' || *q == '\t')) q++;
			if (q < end && *q == '"')
				return json_unescape(q + 1, end, out, n) != NULL;
			return false;     /* present, but not a string (null, a number) */
		}
		p = after;
	}
	return false;
}

/* ── talking to sockets ──────────────────────────────────────────────────── */

static int dial(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	if (strlen(path) >= sizeof a.sun_path) { close(fd); return -1; }
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
	return fd;
}

/* Send a line, read until the far end closes. Bounded in time and size: a
 * peer that never answers must not hang a command line or a window. */
static char *ask(const char *path, const char *cmd, int timeout_ms)
{
	int fd = dial(path);
	if (fd < 0) return NULL;

	size_t cl = strlen(cmd);
	if (write(fd, cmd, cl) != (ssize_t)cl || write(fd, "\n", 1) != 1) {
		close(fd);
		return NULL;
	}

	size_t cap = 4096, len = 0;
	char *buf = malloc(cap);
	if (!buf) die("out of memory");
	int64_t deadline = now_ms() + timeout_ms;

	for (;;) {
		int64_t left = deadline - now_ms();
		if (left <= 0) break;
		struct pollfd pf = { .fd = fd, .events = POLLIN };
		int r = poll(&pf, 1, (int)left);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		if (len + 1024 > cap) {
			if (cap >= (1u << 22)) break;
			cap *= 2;
			char *nb = realloc(buf, cap);
			if (!nb) die("out of memory");
			buf = nb;
		}
		ssize_t n = read(fd, buf + len, cap - len - 1);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		len += (size_t)n;
	}
	close(fd);
	buf[len] = '\0';
	return buf;
}

char *synui_ask(const char *cmd)
{
	char *path = synui_socket_path();
	if (!path) return NULL;
	char *r = ask(path, cmd, 2000);
	free(path);
	return r;
}

int daemon_ask(const char *cmd, FILE *out)
{
	char *path = control_socket_path();
	if (!path) return -1;
	char *r = ask(path, cmd, 3000);
	free(path);
	if (!r) return -1;
	if (out) fputs(r, out);
	free(r);
	return 0;
}
