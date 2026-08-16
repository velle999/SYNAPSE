/*
 * util.c — allocation, records, paths, and the one file writer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ── allocation ──────────────────────────────────────────────────────────── */

void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) {
		fputs("syn-arcade: out of memory\n", stderr);
		exit(EX_FAIL);
	}
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q) {
		fputs("syn-arcade: out of memory\n", stderr);
		exit(EX_FAIL);
	}
	return q;
}

char *xstrdup(const char *s)
{
	size_t n = strlen(s ? s : "") + 1;
	char *p = xmalloc(n);
	memcpy(p, s ? s : "", n);
	return p;
}

/* ── percent encoding ────────────────────────────────────────────────────── */

static int unhex(unsigned char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Everything outside a conservative unreserved set is escaped.
 *
 * A controller name is arbitrary bytes off a USB descriptor and a MangoHud
 * value can be anything a user typed, so "escape the ones that need it" means
 * keeping a list that will drift — and the day it drifts a tab inside a device
 * name shifts every column of a row. Escape by default instead. */
char *pct_encode(const char *s)
{
	if (!s) s = "";
	size_t n = strlen(s);
	char *out = xmalloc(n * 3 + 1);
	char *w = out;

	for (const char *r = s; *r; r++) {
		unsigned char c = (unsigned char)*r;
		if (isalnum(c) || strchr("-_.~/:@+ ", c))
			*w++ = (char)c;
		else
			w += sprintf(w, "%%%02X", c);
	}
	*w = '\0';
	return out;
}

char *pct_decode(const char *s)
{
	if (!s) s = "";
	char *out = xmalloc(strlen(s) + 1);
	char *w = out;

	for (const char *r = s; *r; ) {
		if (*r == '%') {
			int hi = unhex((unsigned char)r[1]);
			int lo = hi >= 0 ? unhex((unsigned char)r[2]) : -1;
			if (lo >= 0) {
				int byte = hi * 16 + lo;
				/* A decoded NUL would truncate the string it is
				 * part of. Corrupt input; keep the escape. */
				if (byte == 0) { *w++ = *r++; continue; }
				*w++ = (char)byte;
				r += 3;
				continue;
			}
		}
		*w++ = *r++;
	}
	*w = '\0';
	return out;
}

/* ── records ─────────────────────────────────────────────────────────────── */

/* Encoding happens HERE and not at the call sites, so a column added by
 * somebody who did not know the rule cannot arrive unencoded. */
void rec_row(int nfields, ...)
{
	va_list ap;
	va_start(ap, nfields);
	rec_vfrow(stdout, nfields, ap);
	va_end(ap);
}

/*
 * The same row, to any stream.
 *
 * Written for the two caches big screen mode keeps — the news headlines and
 * the media servers it found on the network. Those are stored as the RECORD
 * TEXT ITSELF rather than in some second format, so the cache is exactly what
 * `--rec` would have printed and the reader is `cat`. One encoding, one
 * escaping rule, one thing to get wrong instead of two.
 */
void rec_frow(FILE *f, int nfields, ...)
{
	va_list ap;
	va_start(ap, nfields);
	rec_vfrow(f, nfields, ap);
	va_end(ap);
}

void rec_vfrow(FILE *f, int nfields, va_list ap)
{
	for (int i = 0; i < nfields; i++) {
		if (i)
			fputc('\t', f);
		const char *s = va_arg(ap, const char *);
		char *enc = pct_encode(s ? s : "");
		fputs(enc, f);
		free(enc);
	}
	fputc('\n', f);
}

/* ── strings ─────────────────────────────────────────────────────────────── */

void strip_trailing_newline(char *s)
{
	if (!s) return;
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

char *trim(char *s)
{
	if (!s) return s;
	while (*s && isspace((unsigned char)*s)) s++;
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
	return s;
}

/* ── files ───────────────────────────────────────────────────────────────── */

char *read_file(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return NULL;

	size_t cap = 4096, len = 0;
	char *buf = xmalloc(cap);
	for (;;) {
		if (len + 4096 > cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		ssize_t r = read(fd, buf + len, cap - len - 1);
		if (r < 0) {
			if (errno == EINTR) continue;
			free(buf);
			close(fd);
			return NULL;
		}
		if (r == 0) break;
		len += (size_t)r;
	}
	buf[len] = '\0';
	close(fd);
	return buf;
}

int mkdir_parents(const char *path)
{
	char tmp[4096];
	if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp))
		return -ENAMETOOLONG;

	char *slash = strrchr(tmp, '/');
	if (!slash) return 0;            /* relative, no directory part */
	*slash = '\0';
	if (!tmp[0]) return 0;           /* the path was "/something" */

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
			return -errno;
		*p = '/';
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
		return -errno;
	return 0;
}

/*
 * Write a file IN PLACE: open(O_TRUNC), one write(2), close.
 *
 * ⚠ Not a temp file and a rename, which is what every other writer in this
 * suite does and what atomicity would normally demand. The reason is in
 * MangoHud's watcher (src/notify.cpp), which is the only reader that matters
 * here:
 *
 *   inotify_add_watch(fd, config_file_path, IN_MODIFY | IN_DELETE_SELF)
 *
 * The watch is on the FILE, so a rename over the path destroys the inode the
 * watch is attached to. MangoHud does handle that — IN_DELETE_SELF makes it
 * re-add the watch — but the re-add happens AFTER a 100ms sleep and a reparse,
 * and a second write landing inside that window is missed entirely. For a key
 * you can lean on, "sometimes nothing happens" is the one failure mode worth
 * designing out.
 *
 * An in-place write only ever raises IN_MODIFY, which never touches the watch.
 * And the partial-read risk that would normally argue for rename is covered by
 * MangoHud itself: it sleeps 100ms before reparsing, by which time a single
 * write of a file this size has long completed.
 *
 * The cost is that a crash between truncate and write loses the file, so
 * hud.c keeps a one-time .bak of anything it did not write itself.
 */
int write_file_inplace(const char *path, const char *text)
{
	int rc = mkdir_parents(path);
	if (rc < 0) return rc;

	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0) return -errno;

	size_t len = strlen(text), off = 0;
	while (off < len) {
		ssize_t w = write(fd, text + off, len - off);
		if (w < 0) {
			if (errno == EINTR) continue;
			int e = errno;
			close(fd);
			return -e;
		}
		off += (size_t)w;
	}
	if (close(fd) != 0) return -errno;
	return 0;
}

bool file_exists(const char *path)
{
	struct stat st;
	return path && *path && stat(path, &st) == 0;
}

/*
 * Could we write this path?
 *
 * ⚠ "Absent is writable if its directory is" is not enough, and the shortfall
 * is a FIRST-RUN bug rather than an edge case. write_file_inplace() calls
 * mkdir_parents(), so it happily creates ~/.config/MangoHud/MangoHud.conf on
 * an account that has no ~/.config/MangoHud yet — but a check that stops at the
 * immediate parent finds that directory missing, answers "no", and `hud ensure`
 * refuses to create the very file it exists to create. That leaves MangoHud's
 * inotify watch unattached for the whole session, which is every overlay
 * keybind silently dead in every game.
 *
 * So walk UP to the nearest ancestor that exists and ask about that one — which
 * is the question mkdir_parents() will actually face.
 */
bool file_writable(const char *path)
{
	if (!path || !*path) return false;
	if (access(path, W_OK) == 0) return true;
	if (errno != ENOENT) return false;

	char tmp[4096];
	if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp))
		return false;

	for (;;) {
		char *slash = strrchr(tmp, '/');
		if (!slash) return access(".", W_OK) == 0;
		*slash = '\0';
		if (!tmp[0]) return access("/", W_OK) == 0;

		if (access(tmp, F_OK) == 0)
			return access(tmp, W_OK) == 0;
		/* That ancestor is missing too; mkdir_parents would create it,
		 * so the question moves up a level. */
	}
}

bool home_path(char *buf, size_t n, const char *rel)
{
	const char *home = getenv("HOME");
	if (!home || !*home) return false;
	return snprintf(buf, n, "%s/%s", home, rel) < (int)n;
}

bool config_path(char *buf, size_t n, const char *rel)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		return snprintf(buf, n, "%s/%s", xdg, rel) < (int)n;

	const char *home = getenv("HOME");
	if (!home || !*home) return false;
	return snprintf(buf, n, "%s/.config/%s", home, rel) < (int)n;
}

/*
 * The user half of XDG_DATA_DIRS — where `fit` writes the menu entry it makes.
 *
 * XDG_DATA_HOME is honoured for the same reason config_path honours
 * XDG_CONFIG_HOME: it is what lets the test suite redirect every file this
 * binary writes into a temporary directory, and applications/ is a directory
 * whose contents appear in the live desktop's menu the moment they are written.
 */
bool data_path(char *buf, size_t n, const char *rel)
{
	const char *xdg = getenv("XDG_DATA_HOME");
	if (xdg && *xdg)
		return snprintf(buf, n, "%s/%s", xdg, rel) < (int)n;

	const char *home = getenv("HOME");
	if (!home || !*home) return false;
	return snprintf(buf, n, "%s/.local/share/%s", home, rel) < (int)n;
}
