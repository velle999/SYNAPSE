/* util.c — allocation, process plumbing, and the two output renderers.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

out_mode_t g_out       = OUT_HUMAN;
/* See the note on the super search in synpkg.h. Off unless `search --all`
 * turned it on, so every other command emits exactly what it always did. */
bool       g_super     = false;
bool       g_color     = false;
bool       g_noconfirm = false;
bool       g_verbose   = false;

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

char *xasprintf(const char *fmt, ...)
{
	va_list ap;
	char *out = NULL;
	va_start(ap, fmt);
	if (vasprintf(&out, fmt, ap) < 0)
		die("out of memory");
	va_end(ap);
	return out;
}

/* ── diagnostics ────────────────────────────────────────────────────────── */

void die(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%ssynpkg:%s ", C_ERR(), C_RESET());
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%swarning:%s ", C_WARN(), C_RESET());
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* Progress chatter belongs on stderr even in human mode: `synpkg search foo |
 * grep bar` must not receive it, and in TSV mode a stray line on stdout would
 * be parsed as a malformed record by the GUI. */
void info(const char *fmt, ...)
{
	va_list ap;
	if (g_out == OUT_TSV && !g_verbose)
		return;
	fprintf(stderr, "%s::%s ", C_ACCENT(), C_RESET());
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* ── colour ─────────────────────────────────────────────────────────────── */

#define COLOUR(fn, code) \
	const char *fn(void) { return g_color ? code : ""; }

COLOUR(C_RESET,  "\033[0m")
COLOUR(C_BOLD,   "\033[1m")
COLOUR(C_DIM,    "\033[2m")
COLOUR(C_ACCENT, "\033[38;5;44m")
COLOUR(C_WARN,   "\033[33m")
COLOUR(C_ERR,    "\033[31m")
COLOUR(C_OK,     "\033[32m")

/* ── TSV ────────────────────────────────────────────────────────────────── */

static void tsv_field(const char *s)
{
	if (!s)
		return;
	for (; *s; s++)
		fputc((*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s, stdout);
}

void tsv_row(int nfields, ...)
{
	va_list ap;
	va_start(ap, nfields);
	for (int i = 0; i < nfields; i++) {
		if (i)
			fputc('\t', stdout);
		tsv_field(va_arg(ap, const char *));
	}
	va_end(ap);
	fputc('\n', stdout);
}

/* ── processes ──────────────────────────────────────────────────────────── */

int run(char *const argv[], bool quiet)
{
	pid_t pid = fork();
	if (pid < 0) {
		warn("fork: %s", strerror(errno));
		return -1;
	}
	if (pid == 0) {
		if (quiet) {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDOUT_FILENO);
				dup2(devnull, STDERR_FILENO);
				if (devnull > STDERR_FILENO)
					close(devnull);
			}
		}
		execvp(argv[0], argv);
		_exit(127);
	}

	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
	if (WIFEXITED(st))
		return WEXITSTATUS(st);
	return WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1;
}

char *run_capture(char *const argv[], int *status, bool quiet_stderr)
{
	int fds[2];
	if (pipe(fds) < 0) {
		warn("pipe: %s", strerror(errno));
		if (status)
			*status = -1;
		return xstrdup("");
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		warn("fork: %s", strerror(errno));
		if (status)
			*status = -1;
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

	size_t cap = 4096, len = 0;
	char *buf = xmalloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		ssize_t n = read(fds[0], buf + len, cap - len - 1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;
		len += (size_t)n;
	}
	buf[len] = '\0';
	close(fds[0]);

	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
	if (status)
		*status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
	return buf;
}

bool have_cmd(const char *name)
{
	char *path = getenv("PATH");
	if (!path || !*path)
		return false;
	char *copy = xstrdup(path);
	bool found = false;
	for (char *save = NULL, *dir = strtok_r(copy, ":", &save);
	     dir && !found; dir = strtok_r(NULL, ":", &save)) {
		char *full = xasprintf("%s/%s", *dir ? dir : ".", name);
		found = access(full, X_OK) == 0;
		free(full);
	}
	free(copy);
	return found;
}

/* ── misc ───────────────────────────────────────────────────────────────── */

/* Can the question be put at all? A caller that must distinguish "the user
 * said no" from "there was nobody to ask" asks this — the two look identical
 * in confirm()'s return value and mean opposite things about the exit code. */
bool confirm_possible(void)
{
	return g_noconfirm || (g_out != OUT_TSV && isatty(STDIN_FILENO));
}

bool confirm(const char *fmt, ...)
{
	if (g_noconfirm)
		return true;
	/* A GUI front-end has no terminal to answer in. Refusing is the safe
	 * default: the GUI always passes --noconfirm when the user has already
	 * clicked through its own dialog.
	 *
	 * But SAY SO, and — via confirm_possible() at the transaction call sites —
	 * EXIT NON-ZERO. This used to refuse in silence, and a caller that forgot
	 * --noconfirm got a transaction that authenticated through polkit and then
	 * declined itself — the callers treat a declined transaction as success,
	 * so nothing was printed, nothing was installed, and the only symptom was
	 * a button that went quiet after the password prompt. That was
	 * syn-settings' kernel installer for its whole life, and then, until
	 * 2026-08-12, its "Make bootable" button. A refusal nobody can see is
	 * indistinguishable from a hang. */
	if (!confirm_possible()) {
		warn("declined: there is no terminal to confirm on "
		     "(a front-end must pass --noconfirm once the user has agreed)");
		return false;
	}

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputs(" [Y/n] ", stderr);
	fflush(stderr);

	char line[16];
	if (!fgets(line, sizeof line, stdin))
		return false;
	return line[0] == '\n' || line[0] == 'y' || line[0] == 'Y';
}

char *human_size(off_t bytes)
{
	static const char *unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	double v = (double)bytes;
	size_t i = 0;
	while (v >= 1024.0 && i + 1 < sizeof unit / sizeof *unit) {
		v /= 1024.0;
		i++;
	}
	return i == 0 ? xasprintf("%.0f %s", v, unit[i])
	              : xasprintf("%.2f %s", v, unit[i]);
}

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
	size_t cap = 16, count = 0;
	char **out = xmalloc(cap * sizeof *out);

	if (text && *text) {
		char *p = text;
		out[count++] = p;
		for (; *p; p++) {
			if (*p != sep)
				continue;
			*p = '\0';
			if (count + 1 >= cap) {
				cap *= 2;
				out = xrealloc(out, cap * sizeof *out);
			}
			out[count++] = p + 1;
		}
		/* A trailing separator yields an empty final field; drop it so
		 * callers iterating rows do not get a phantom blank record. */
		if (count && !*out[count - 1])
			count--;
	}

	*n = count;
	return out;
}
