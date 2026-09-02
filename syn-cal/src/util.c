/* util.c — allocation that cannot fail, buffers, paths, atomic writes.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syncal.h"
#include "i18n.h"
#include "config.h"

#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

out_mode_t g_out = OUT_HUMAN;
bool g_color = true;
bool g_verbose = false;

/* ── diagnostics ────────────────────────────────────────────────────────── */

void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("syn-cal: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("syn-cal: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

void info(const char *fmt, ...)
{
	if (!g_verbose) return;
	va_list ap;
	va_start(ap, fmt);
	fputs("  ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* ── allocation ─────────────────────────────────────────────────────────── */

void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) die(_("out of memory"));
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q) die(_("out of memory"));
	return q;
}

char *xstrdup(const char *s)
{
	size_t n = strlen(s);
	char *p = xmalloc(n + 1);
	memcpy(p, s, n + 1);
	return p;
}

char *xasprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *out = NULL;
	if (vasprintf(&out, fmt, ap) < 0) die(_("out of memory"));
	va_end(ap);
	return out;
}

/* ── buffers ────────────────────────────────────────────────────────────── */

void buf_init(buf_t *s) { s->b = NULL; s->len = s->cap = 0; }

void buf_add(buf_t *s, const void *p, size_t n)
{
	if (s->len + n + 1 > s->cap) {
		size_t want = s->cap ? s->cap : 256;
		while (want < s->len + n + 1) want *= 2;
		s->b = xrealloc(s->b, want);
		s->cap = want;
	}
	memcpy(s->b + s->len, p, n);
	s->len += n;
	s->b[s->len] = '\0';        /* always NUL-terminated, never counted */
}

void buf_addstr(buf_t *s, const char *p) { buf_add(s, p, strlen(p)); }

void buf_addf(buf_t *s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *tmp = NULL;
	if (vasprintf(&tmp, fmt, ap) < 0) die(_("out of memory"));
	va_end(ap);
	buf_addstr(s, tmp);
	free(tmp);
}

void buf_free(buf_t *s)
{
	free(s->b);
	buf_init(s);
}

/* ── percent encoding ───────────────────────────────────────────────────── */

static bool unreserved(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
}

char *pct_encode(const char *s, bool keep_slash)
{
	if (!s) return xstrdup("");
	buf_t out;
	buf_init(&out);
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (unreserved(*p) || (keep_slash && *p == '/')) buf_add(&out, p, 1);
		else buf_addf(&out, "%%%02X", *p);
	}
	if (!out.b) return xstrdup("");
	return out.b;                 /* ownership passes to the caller */
}

char *pct_decode(const char *s)
{
	if (!s) return xstrdup("");
	buf_t out;
	buf_init(&out);
	for (const char *p = s; *p; p++) {
		if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
			char hex[3] = { p[1], p[2], 0 };
			unsigned char c = (unsigned char)strtol(hex, NULL, 16);
			buf_add(&out, &c, 1);
			p += 2;
		} else {
			buf_add(&out, p, 1);
		}
	}
	if (!out.b) return xstrdup("");
	return out.b;
}

/* ── records ────────────────────────────────────────────────────────────── */

void rec_header(const char *fields)
{
	if (g_out == OUT_REC) printf("%s\n", fields);
}

void rec_row(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

/* ── paths ──────────────────────────────────────────────────────────────── */

bool ensure_dir(const char *path)
{
	char *copy = xstrdup(path);
	bool ok = true;
	for (char *p = copy + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(copy, 0700) != 0 && errno != EEXIST) { ok = false; break; }
		*p = '/';
	}
	if (ok) ok = (mkdir(copy, 0700) == 0 || errno == EEXIST);
	free(copy);
	return ok;
}

char *store_root(void)
{
	/* ⚠ SYNCAL_HOME FIRST, and it exists for the test suite. A calendar's test
	 * suite that could write to the real store would be the most dangerous file
	 * in this component — see the same note at the top of synfiles' tests. */
	const char *override = getenv("SYNCAL_HOME");
	if (override && *override) return xstrdup(override);

	const char *xdg = getenv("XDG_DATA_HOME");
	if (xdg && *xdg) return xasprintf("%s/syn-cal", xdg);

	const char *home = getenv("HOME");
	if (!home || !*home) die(_("neither $HOME nor $XDG_DATA_HOME is set"));
	return xasprintf("%s/.local/share/syn-cal", home);
}

char *store_path(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *tail = NULL;
	if (vasprintf(&tail, fmt, ap) < 0) die(_("out of memory"));
	va_end(ap);

	char *root = store_root();
	char *full = xasprintf("%s/%s", root, tail);
	free(root);
	free(tail);
	return full;
}

/* ── files ──────────────────────────────────────────────────────────────── */

char *read_file(const char *path, size_t *len)
{
	if (len) *len = 0;
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;

	buf_t b;
	buf_init(&b);
	char chunk[65536];
	size_t n;
	while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) buf_add(&b, chunk, n);
	fclose(f);

	if (!b.b) b.b = xstrdup("");
	if (len) *len = b.len;
	return b.b;
}

bool write_file_atomic(const char *path, const void *data, size_t len, int mode)
{
	/* ⛔ mkstemp, NOT a name built from the pid. The lesson is synfiles'
	 * thumb.c, CodeQL #16-#19: a temp whose name can be worked out in advance,
	 * beside a target whose path is known, is one symlink away from writing
	 * somewhere else. Six unguessable characters and O_EXCL instead. */
	char *tmp = xasprintf("%s.XXXXXX", path);
	int fd = mkstemp(tmp);
	if (fd < 0) { free(tmp); return false; }

	bool ok = true;
	if (fchmod(fd, mode) != 0) ok = false;

	const char *p = data;
	size_t left = len;
	while (ok && left) {
		ssize_t w = write(fd, p, left);
		if (w <= 0) { ok = false; break; }
		p += w;
		left -= (size_t)w;
	}
	/* ⚠ fsync BEFORE the rename. rename() is atomic with respect to a reader,
	 * not with respect to a power cut: without this the directory entry can
	 * reach the disk ahead of the bytes it names, and the file comes back
	 * zero-length. A calendar that loses an appointment to a reboot is worse
	 * than one that is slow to save. */
	if (ok && fsync(fd) != 0) ok = false;
	if (close(fd) != 0) ok = false;

	if (ok && rename(tmp, path) != 0) ok = false;
	if (!ok) unlink(tmp);
	free(tmp);
	return ok;
}

char *content_hash(const void *data, size_t len)
{
	/* FNV-1a, 64-bit. Not a security hash and never used as one: this answers
	 * "are these bytes the same bytes as last time", which is a question about
	 * accident rather than about an adversary. */
	uint64_t h = 1469598103934665603ULL;
	const unsigned char *p = data;
	for (size_t i = 0; i < len; i++) {
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return xasprintf("%016llx", (unsigned long long)h);
}

/*
 * Bind the message catalog. Called once from main() before anything prints.
 *
 * ⛔ THE ENV OVERRIDE IS WHAT MAKES THIS TESTABLE. The compiled-in path is under
 * the install prefix, so an UNINSTALLED binary finds no catalog at all and
 * answers English in every locale — a test that runs it under two locales and
 * diffs would then pass on a real bug, and PASS IS EXACTLY WHAT IT DID: synpkg
 * verified a release that way and shipped a suite that failed on every
 * translated desktop. Nothing changes for an installed syn-cal; the variable is
 * not set.
 */
void syn_cal_i18n_init(void)
{
	setlocale(LC_ALL, "");
	const char *dir = getenv("SYN_CAL_LOCALEDIR");
	bindtextdomain(SYN_CAL_GETTEXT_DOMAIN, dir && *dir ? dir : SYNCAL_LOCALEDIR);
	bind_textdomain_codeset(SYN_CAL_GETTEXT_DOMAIN, "UTF-8");
	textdomain(SYN_CAL_GETTEXT_DOMAIN);
}
