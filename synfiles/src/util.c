/* util.c — allocation, output, and the percent-encoding the record format
 * depends on.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

out_mode_t g_out = OUT_HUMAN;
bool g_color = false;
bool g_verbose = false;

/* ── allocation ─────────────────────────────────────────────────────────── */

void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p)
		die("out of memory");
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q)
		die("out of memory");
	return q;
}

char *xstrdup(const char *s)
{
	char *p = strdup(s ? s : "");
	if (!p)
		die("out of memory");
	return p;
}

char *xstrndup(const char *s, size_t n)
{
	char *p = xmalloc(n + 1);
	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

char *xasprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *out = NULL;
	if (vasprintf(&out, fmt, ap) < 0)
		die("out of memory");
	va_end(ap);
	return out;
}

/* ── diagnostics ────────────────────────────────────────────────────────── */

void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "synfiles: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "synfiles: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

const char *C_RESET(void)  { return g_color ? "\033[0m"  : ""; }
const char *C_BOLD(void)   { return g_color ? "\033[1m"  : ""; }
const char *C_DIM(void)    { return g_color ? "\033[2m"  : ""; }
const char *C_ACCENT(void) { return g_color ? "\033[36m" : ""; }
const char *C_WARN(void)   { return g_color ? "\033[33m" : ""; }

/* ── percent-encoding ───────────────────────────────────────────────────────
 *
 * The unreserved set from RFC 3986. Everything else is escaped, which is
 * broader than strictly necessary — a space could legally travel unescaped
 * through a tab-separated field — but "escape everything that is not provably
 * safe" is the only version of this that stays correct when somebody later
 * changes the field separator or pipes a record through a shell.
 */
static bool unreserved(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
	    || (c >= '0' && c <= '9')
	    || c == '-' || c == '.' || c == '_' || c == '~';
}

char *pct_encode(const char *s, bool keep_slash)
{
	static const char hex[] = "0123456789ABCDEF";
	if (!s)
		return xstrdup("");

	size_t n = strlen(s);
	char *out = xmalloc(n * 3 + 1);   /* worst case: every byte escaped */
	char *w = out;

	for (const unsigned char *r = (const unsigned char *)s; *r; r++) {
		if (unreserved(*r) || (keep_slash && *r == '/')) {
			*w++ = (char)*r;
		} else {
			*w++ = '%';
			*w++ = hex[*r >> 4];
			*w++ = hex[*r & 0x0f];
		}
	}
	*w = '\0';
	return out;
}

static int unhex(unsigned char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

char *pct_decode(const char *s)
{
	if (!s)
		return xstrdup("");

	char *out = xmalloc(strlen(s) + 1);
	char *w = out;

	for (const char *r = s; *r; ) {
		if (*r == '%') {
			int hi = unhex((unsigned char)r[1]);
			int lo = hi >= 0 ? unhex((unsigned char)r[2]) : -1;
			if (lo >= 0) {
				/* A decoded NUL would truncate the string it is
				 * supposed to be part of, and no filename can contain
				 * one — treat it as corrupt input and keep the escape
				 * verbatim rather than silently shortening a path. */
				int byte = hi * 16 + lo;
				if (byte == 0) {
					*w++ = *r++;
					continue;
				}
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

/* ── records ────────────────────────────────────────────────────────────── */

static void rec_field(const char *s)
{
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++)
		if (*p != '\t' && *p != '\n' && *p != '\r')
			fputc(*p, stdout);
}

void rec_row(int nfields, ...)
{
	va_list ap;
	va_start(ap, nfields);
	for (int i = 0; i < nfields; i++) {
		if (i)
			fputc('\t', stdout);
		rec_field(va_arg(ap, const char *));
	}
	va_end(ap);
	fputc('\n', stdout);
}

/* ── odds and ends ──────────────────────────────────────────────────────── */

void strip_trailing_newline(char *s)
{
	if (!s)
		return;
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

char **split(char *text, char sep, size_t *n)
{
	size_t cap = 16, k = 0;
	char **out = xmalloc(cap * sizeof *out);

	for (char *p = text; ; ) {
		if (k == cap) {
			cap *= 2;
			out = xrealloc(out, cap * sizeof *out);
		}
		out[k++] = p;
		char *q = strchr(p, sep);
		if (!q)
			break;
		*q = '\0';
		p = q + 1;
	}

	*n = k;
	return out;
}

char *human_size(off_t bytes)
{
	static const char *unit[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB" };
	double v = (double)bytes;
	size_t i = 0;
	while (v >= 1024.0 && i + 1 < sizeof unit / sizeof *unit) {
		v /= 1024.0;
		i++;
	}
	return i == 0 ? xasprintf("%lld B", (long long)bytes)
	              : xasprintf("%.1f %s", v, unit[i]);
}

bool have_cmd(const char *name)
{
	const char *path = getenv("PATH");
	if (!path || !*path)
		return false;

	char *copy = xstrdup(path);
	bool found = false;
	for (char *p = copy; p && !found; ) {
		char *colon = strchr(p, ':');
		if (colon)
			*colon = '\0';
		if (*p) {
			char *full = xasprintf("%s/%s", p, name);
			found = access(full, X_OK) == 0;
			free(full);
		}
		p = colon ? colon + 1 : NULL;
	}
	free(copy);
	return found;
}

char *slurp(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;

	size_t cap = 8192, len = 0;
	char *text = xmalloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			text = xrealloc(text, cap);
		}
		size_t n = fread(text + len, 1, cap - len - 1, f);
		if (n == 0)
			break;
		len += n;
	}
	text[len] = '\0';
	fclose(f);
	return text;
}

char *run_capture(char *const argv[], int *status, bool quiet_stderr)
{
	int fds[2];
	if (pipe(fds) < 0) {
		if (status) *status = -1;
		return xstrdup("");
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		if (status) *status = -1;
		return xstrdup("");
	}
	if (pid == 0) {
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		if (quiet_stderr) {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDERR_FILENO);
				if (devnull > STDERR_FILENO)
					close(devnull);
			}
		}
		if (fds[1] > STDERR_FILENO)
			close(fds[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);

	size_t cap = 8192, len = 0;
	char *buf = xmalloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		ssize_t n = read(fds[0], buf + len, cap - len - 1);
		if (n <= 0)
			break;
		len += (size_t)n;
	}
	buf[len] = '\0';
	close(fds[0]);

	int st = 0;
	waitpid(pid, &st, 0);
	if (status)
		*status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
	return buf;
}

const char *home_dir(void)
{
	const char *h = getenv("HOME");
	if (!h || !*h)
		die("HOME is not set");
	return h;
}

char *xdg_data_home(void)
{
	const char *x = getenv("XDG_DATA_HOME");
	if (x && *x)
		return xstrdup(x);
	return xasprintf("%s/.local/share", home_dir());
}
