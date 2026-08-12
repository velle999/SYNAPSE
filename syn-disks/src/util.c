/* util.c — allocation, output, and the percent-encoding the record format
 * depends on.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
	fprintf(stderr, "syn-disks: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "syn-disks: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

const char *C_RESET(void)  { return g_color ? "\033[0m"  : ""; }
const char *C_BOLD(void)   { return g_color ? "\033[1m"  : ""; }
const char *C_DIM(void)    { return g_color ? "\033[2m"  : ""; }
const char *C_ACCENT(void) { return g_color ? "\033[36m" : ""; }
const char *C_WARN(void)   { return g_color ? "\033[33m" : ""; }
const char *C_BAD(void)    { return g_color ? "\033[31m" : ""; }

/* ── percent-encoding ─────────────────────────────────────────────────────
 *
 * The unreserved set from RFC 3986. Escaping everything else is broader than
 * strictly necessary — a space could travel unescaped through a tab-separated
 * field — but "escape everything not provably safe" is the only version of
 * this that stays correct when somebody later changes the field separator or
 * pipes a record through a shell.
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
				int byte = hi * 16 + lo;
				if (byte == 0) {
					/* A decoded NUL would truncate the string it is
					 * part of. Corrupt input; keep the escape. */
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

/* ── records ────────────────────────────────────────────────────────────────
 *
 * Encoding happens HERE rather than at every call site. synfiles encodes at
 * the caller and has to remember which of ten arguments is a name; doing it
 * once, for every field, means a new column cannot be added unencoded by
 * somebody who did not know the rule.
 */
void rec_row(int nfields, ...)
{
	va_list ap;
	va_start(ap, nfields);
	for (int i = 0; i < nfields; i++) {
		if (i)
			fputc('\t', stdout);
		const char *s = va_arg(ap, const char *);
		char *enc = pct_encode(s ? s : "", true);
		fputs(enc, stdout);
		free(enc);
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

char *trim(char *s)
{
	if (!s)
		return NULL;
	char *p = s;
	while (*p && isspace((unsigned char)*p))
		p++;
	size_t n = strlen(p);
	while (n && isspace((unsigned char)p[n - 1]))
		p[--n] = '\0';
	if (p != s)
		memmove(s, p, n + 1);
	return s;
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

/* Powers of 1024 with the IEC names, matching what lsblk, du and the file
 * manager already print on this system. A disk utility that said "8.0 TB"
 * beside a file manager saying "7.3 TiB" for the same drive would look like
 * the two disagreed about the hardware. */
char *human_size(unsigned long long bytes)
{
	static const char *unit[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB" };
	double v = (double)bytes;
	size_t i = 0;
	while (v >= 1024.0 && i + 1 < sizeof unit / sizeof *unit) {
		v /= 1024.0;
		i++;
	}
	return i == 0 ? xasprintf("%llu B", bytes)
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

/* execvp, not system(): every argument this program passes to a tool comes
 * from somewhere a user can influence, and a shell in the middle turns a
 * filesystem label into a command line. It is also what lets the test suite
 * put a fake lsblk, udisksctl or smartctl on PATH and assert the argv. */
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
		} else {
			/* Merged, so a tool that explains its refusal on stderr is
			 * still quoted back to the user instead of vanishing. */
			dup2(fds[1], STDERR_FILENO);
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

/* run_capture with a string on the child's stdin.
 *
 * sfdisk takes its partition script that way and offers no argument form of
 * it, so without this the only way to drive it would be a shell — which is
 * precisely what run_capture exists to avoid. The script still travels as
 * data on a pipe, never as a command line, so nothing in it can be a command
 * however it was spelled.
 *
 * SIGPIPE is ignored for the duration: a tool that refuses the request and
 * exits before reading its input would otherwise kill this process with a
 * signal instead of letting it report the refusal. */
char *run_capture_in(char *const argv[], const char *input, int *status)
{
	int out[2], in[2];
	if (pipe(out) < 0) {
		if (status) *status = -1;
		return xstrdup("");
	}
	if (pipe(in) < 0) {
		close(out[0]);
		close(out[1]);
		if (status) *status = -1;
		return xstrdup("");
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(out[0]); close(out[1]);
		close(in[0]); close(in[1]);
		if (status) *status = -1;
		return xstrdup("");
	}
	if (pid == 0) {
		close(out[0]);
		close(in[1]);
		dup2(in[0], STDIN_FILENO);
		dup2(out[1], STDOUT_FILENO);
		/* Merged, so a tool that explains its refusal on stderr is quoted
		 * back to the user rather than vanishing. */
		dup2(out[1], STDERR_FILENO);
		if (in[0] > STDERR_FILENO)
			close(in[0]);
		if (out[1] > STDERR_FILENO)
			close(out[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(out[1]);
	close(in[0]);

	void (*old)(int) = signal(SIGPIPE, SIG_IGN);

	/* Written before anything is read. A partition script is a line or two,
	 * far below the pipe buffer, so this cannot deadlock against a child that
	 * reads its whole input before saying anything. */
	for (size_t off = 0, n = input ? strlen(input) : 0; off < n; ) {
		ssize_t w = write(in[1], input + off, n - off);
		if (w <= 0)
			break;
		off += (size_t)w;
	}
	close(in[1]);

	size_t cap = 8192, len = 0;
	char *buf = xmalloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		ssize_t n = read(out[0], buf + len, cap - len - 1);
		if (n <= 0)
			break;
		len += (size_t)n;
	}
	buf[len] = '\0';
	close(out[0]);

	int st = 0;
	waitpid(pid, &st, 0);
	signal(SIGPIPE, old);
	if (status)
		*status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
	return buf;
}

char *cmd_display(char *const argv[])
{
	char *line = NULL;
	for (int i = 0; argv[i]; i++) {
		bool quote = strchr(argv[i], ' ') != NULL || !*argv[i];
		char *piece = quote ? xasprintf("'%s'", argv[i]) : xstrdup(argv[i]);
		char *grown = line ? xasprintf("%s %s", line, piece) : xstrdup(piece);
		free(piece);
		free(line);
		line = grown;
	}
	return line ? line : xstrdup("");
}

bool parse_size(const char *s, unsigned long long *bytes)
{
	if (!s || !*s)
		return false;

	errno = 0;
	char *end = NULL;
	double v = strtod(s, &end);
	/* A leading '-' parses perfectly well and yields a negative size, which
	 * then wraps to something enormous on the way to unsigned long long. It is
	 * rejected here rather than clamped, because somebody who typed it meant
	 * something this program cannot work out. */
	if (end == s || errno == ERANGE || !(v >= 0.0))
		return false;

	while (*end == ' ')
		end++;

	/* IEC suffixes are 1024s and the two-letter SI ones are 1000s, which is
	 * the same split lsblk, du and every disk vendor's box already use. A bare
	 * number is bytes. */
	static const struct { const char *suffix; double mult; } U[] = {
		{ "",    1.0 },
		{ "B",   1.0 },
		{ "K",   1024.0 },            { "KiB", 1024.0 },
		{ "M",   1048576.0 },         { "MiB", 1048576.0 },
		{ "G",   1073741824.0 },      { "GiB", 1073741824.0 },
		{ "T",   1099511627776.0 },   { "TiB", 1099511627776.0 },
		{ "P",   1125899906842624.0 },{ "PiB", 1125899906842624.0 },
		{ "KB",  1e3 }, { "MB", 1e6 }, { "GB", 1e9 },
		{ "TB",  1e12 }, { "PB", 1e15 },
	};

	double mult = 0.0;
	for (size_t i = 0; i < sizeof U / sizeof *U; i++) {
		if (!strcasecmp(end, U[i].suffix)) {
			mult = U[i].mult;
			break;
		}
	}
	if (mult == 0.0)
		return false;          /* trailing rubbish, or a suffix not offered */

	double total = v * mult;
	if (!(total >= 0.0) || total >= 18446744073709549568.0)
		return false;

	*bytes = (unsigned long long)total;
	return true;
}

char *kv_val(const char *line, const char *key)
{
	size_t klen = strlen(key);
	for (const char *p = line; (p = strstr(p, key)); p += klen) {
		/* Both anchors are needed. Without the leading one, NAME matches
		 * inside PKNAME; without the trailing one, PTTYPE matches inside
		 * PARTTYPENAME on an lsblk that prints both. */
		if (p != line && p[-1] != ' ')
			continue;
		if (p[klen] != '=' || p[klen + 1] != '"')
			continue;
		const char *v = p + klen + 2;
		const char *q = strchr(v, '"');
		return q ? xstrndup(v, (size_t)(q - v)) : NULL;
	}
	return NULL;
}
