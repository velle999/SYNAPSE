/* util.c — allocation, output, percent-encoding, and display width.
 *
 * The first three are the suite's shared conventions, kept byte-compatible
 * with syn-disks and synfiles on purpose: every SynapseOS component emits the
 * same record format, so one QML parser reads all of them.
 *
 * The last is this program's own problem. A column in this editor is a BYTE
 * offset, because the buffer holds bytes and files are not always valid UTF-8.
 * What the user sees is a WIDTH, because a tab is eight columns wide and a
 * multi-byte character is one. Confusing the two puts the cursor somewhere
 * other than where the caret is drawn, which is the single most disorienting
 * bug an editor can have — so the conversion lives here, in two functions, and
 * nothing else computes it.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "syn-edit.h"
#include "i18n.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

out_mode_t g_out = OUT_HUMAN;
bool g_color = false;
bool g_verbose = false;

/* ── allocation ─────────────────────────────────────────────────────────── */

void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p)
		die(_("out of memory"));
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q)
		die(_("out of memory"));
	return q;
}

char *xstrdup(const char *s)
{
	char *p = strdup(s ? s : "");
	if (!p)
		die(_("out of memory"));
	return p;
}

char *xstrndup(const char *s, size_t n)
{
	char *p = xmalloc(n + 1);
	if (n)
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
		die(_("out of memory"));
	va_end(ap);
	return out;
}

/* ── diagnostics ────────────────────────────────────────────────────────── */

void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "syn-edit: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "syn-edit: ");
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

/* ── percent-encoding ───────────────────────────────────────────────────── */

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
	char *out = xmalloc(n * 3 + 1);
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
					/* A decoded NUL would truncate the string it is part
					 * of. Corrupt input; keep the escape. */
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

/* ── files and paths ────────────────────────────────────────────────────── */

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

/* execvp, never system(): a path in this editor comes from a file the user
 * opened or a name they typed, and a shell in the middle turns either into a
 * command line. */
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

char *expand_path(const char *p)
{
	if (!p)
		return xstrdup("");
	if (p[0] == '~' && (p[1] == '/' || p[1] == '\0')) {
		const char *home = getenv("HOME");
		if (home)
			return xasprintf("%s%s", home, p + 1);
	}
	return xstrdup(p);
}

char *config_dir(void)
{
	const char *x = getenv("XDG_CONFIG_HOME");
	if (x && *x)
		return xasprintf("%s/syn-edit", x);
	const char *home = getenv("HOME");
	return xasprintf("%s/.config/syn-edit", home ? home : ".");
}

char *data_dir(void)
{
	const char *x = getenv("XDG_DATA_HOME");
	if (x && *x)
		return xasprintf("%s/syn-edit", x);
	const char *home = getenv("HOME");
	return xasprintf("%s/.local/share/syn-edit", home ? home : ".");
}

bool mkdir_p(const char *path)
{
	char *copy = xstrdup(path);
	bool ok = true;
	for (char *p = copy + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(copy, 0700) < 0 && errno != EEXIST)
			ok = false;
		*p = '/';
	}
	if (ok && mkdir(copy, 0700) < 0 && errno != EEXIST)
		ok = false;
	free(copy);
	return ok;
}

/* ── display width ──────────────────────────────────────────────────────── */

size_t utf8_len(unsigned char c)
{
	if (c < 0x80) return 1;
	if ((c & 0xe0) == 0xc0) return 2;
	if ((c & 0xf0) == 0xe0) return 3;
	if ((c & 0xf8) == 0xf0) return 4;
	/* A stray continuation byte, or 0xfe/0xff. It is not the start of
	 * anything, but it IS a byte in the buffer and has to advance by one or
	 * every walk over the line becomes an infinite loop. */
	return 1;
}

/* Width of the first `bytes` bytes of s, with tabs snapping to the next
 * multiple of ts. Everything printable counts as one column, including
 * double-width CJK — getting that right needs a width table that would be the
 * largest file in this program, and getting it WRONG for CJK misplaces the
 * caret by one column, where getting it wrong for a control character
 * misplaces it by four. */
size_t disp_col(const char *s, size_t bytes, int ts)
{
	if (ts <= 0)
		ts = 8;
	size_t w = 0;
	for (size_t i = 0; i < bytes; ) {
		unsigned char c = (unsigned char)s[i];
		if (c == '\t') {
			w += (size_t)ts - (w % (size_t)ts);
			i++;
		} else if (c < 0x20 || c == 0x7f) {
			w += 2;             /* drawn as ^X */
			i++;
		} else {
			w += 1;
			i += utf8_len(c);
		}
	}
	return w;
}

size_t disp_width(const char *s, size_t len, int ts)
{
	return disp_col(s, len, ts);
}

/*
 * How many columns a terminal will spend drawing this string.
 *
 * ⛔ NOT disp_width(). That is the EDITOR'S cell model — one cell per
 * codepoint, which is what buffer positions and the cursor are counted in.
 * A terminal does not agree: "あり" is two codepoints and FOUR columns. Using
 * the editor's number to lay out CLI output left the `about` table two columns
 * out in Japanese and correct in German, which is the least useful kind of
 * wrong. Anything printed for a terminal to align wants this one.
 *
 * Falls back to the byte count when the string will not decode in the current
 * locale, which is the same thing printf's own width would have done.
 */
size_t term_cols(const char *s)
{
	size_t n = mbstowcs(NULL, s, 0);
	if (n == (size_t)-1)
		return strlen(s);

	wchar_t *w = malloc((n + 1) * sizeof *w);
	if (!w)
		return strlen(s);

	mbstowcs(w, s, n + 1);
	int cols = wcswidth(w, n);
	free(w);
	return cols < 0 ? strlen(s) : (size_t)cols;
}

/*
 * Bind the message catalog. Called once from main() before anything prints.
 *
 * ⛔ THE ENV OVERRIDE IS WHAT MAKES THIS TESTABLE. The compiled-in path is
 * under the install prefix, so an UNINSTALLED binary finds no catalog at all
 * and answers English in every locale — a test that runs it under two locales
 * and diffs would then pass on a real bug. synpkg 47 shipped that mistake and
 * only found it by deliberately mistranslating a record. Nothing changes for
 * an installed syn-edit: the variable is not set.
 */
void syn_edit_i18n_init(void)
{
	setlocale(LC_ALL, "");
	const char *dir = getenv("SYN_EDIT_LOCALEDIR");
	bindtextdomain(SYN_EDIT_GETTEXT_DOMAIN, dir && *dir ? dir : SYNEDIT_LOCALEDIR);
	bind_textdomain_codeset(SYN_EDIT_GETTEXT_DOMAIN, "UTF-8");
	textdomain(SYN_EDIT_GETTEXT_DOMAIN);
}
