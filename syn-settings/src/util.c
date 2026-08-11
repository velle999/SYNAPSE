/* syn-settings — shared helpers.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

int g_dry_run = 0;

static int run_capture_impl(char *const argv[], char *out, size_t cap,
                           int silence_stderr);

void rec_header(const char *cols)
{
	printf("%s\n", cols);
}

void rec_row(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

void tsv_clean(char *s)
{
	if (!s) return;
	for (; *s; s++)
		if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

int have_cmd(const char *cmd)
{
	const char *path = getenv("PATH");
	if (!path || !*path) path = "/usr/bin:/bin";

	char *dup = strdup(path);
	if (!dup) return 0;

	int found = 0;
	char *save = NULL;
	for (char *dir = strtok_r(dup, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		char full[512];
		if (snprintf(full, sizeof full, "%s/%s", dir, cmd) >= (int)sizeof full)
			continue;
		if (access(full, X_OK) == 0) { found = 1; break; }
	}
	free(dup);
	return found;
}

/* fork+exec rather than popen: popen goes through /bin/sh, which means every
 * argument would have to be quoted correctly forever. Nothing here is ever a
 * shell string. */
/* Same as run_capture, but the child's stderr goes to /dev/null.
 *
 * For the callers where a NON-ZERO exit is an expected answer rather than a
 * fault: `pacman -Q linux-zen` on a machine without it prints "error: package
 * not found" and that is simply the word "no". Left alone, those lines reach
 * the terminal — and, through the GUI's stderr collector, the status bar,
 * where the app would report an error for a question it answered correctly. */
int run_capture_quiet(char *const argv[], char *out, size_t cap)
{
	return run_capture_impl(argv, out, cap, 1);
}

int run_capture(char *const argv[], char *out, size_t cap)
{
	return run_capture_impl(argv, out, cap, 0);
}

static int run_capture_impl(char *const argv[], char *out, size_t cap,
                            int silence_stderr)
{
	if (cap) out[0] = '\0';
	if (!argv || !argv[0] || !have_cmd(argv[0])) return -1;

	int fds[2];
	if (pipe(fds) != 0) return -1;

	pid_t pid = fork();
	if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }

	if (pid == 0) {
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		close(fds[1]);
		if (silence_stderr) {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDERR_FILENO);
				if (devnull > STDERR_FILENO) close(devnull);
			}
		}
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fds[1]);
	size_t used = 0;
	for (;;) {
		if (cap == 0 || used + 1 >= cap) {
			/* Keep draining, or the child blocks on a full pipe and we
			 * wait on a process that is waiting on us. */
			char sink[512];
			if (read(fds[0], sink, sizeof sink) <= 0) break;
			continue;
		}
		ssize_t n = read(fds[0], out + used, cap - used - 1);
		if (n <= 0) break;
		used += (size_t)n;
	}
	if (cap) out[used] = '\0';
	close(fds[0]);

	int st = 0;
	if (waitpid(pid, &st, 0) < 0) return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int run_quiet(char *const argv[])
{
	if (!argv || !argv[0] || !have_cmd(argv[0])) return -1;

	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO) close(devnull);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	int st = 0;
	if (waitpid(pid, &st, 0) < 0) return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Run argv, or — under --dry-run — print what would have run and change
 * nothing. Shared rather than duplicated: a second copy is how a dry run stops
 * being dry for exactly one caller, which is the caller you find out about
 * afterwards. */
int run_or_show(char *const argv[])
{
	if (g_dry_run) {
		fputs("would run:", stdout);
		for (int i = 0; argv[i]; i++) printf(" %s", argv[i]);
		putchar('\n');
		return 0;
	}
	int rc = run_quiet(argv);
	if (rc == -1) {
		fprintf(stderr, "syn-settings: could not run %s\n", argv[0]);
		return 1;
	}
	if (rc != 0)
		fprintf(stderr, "syn-settings: %s exited %d "
		                "(authorisation refused, or the value was rejected)\n",
		        argv[0], rc);
	return rc;
}

const char *read_line_file(const char *path, char *buf, size_t cap)
{
	FILE *f = fopen(path, "re");
	if (!f) return NULL;
	if (!fgets(buf, (int)cap, f)) { fclose(f); return NULL; }
	fclose(f);
	buf[strcspn(buf, "\n")] = '\0';
	return buf;
}

int scrape_field(const char *text, const char *key, char *out, size_t cap)
{
	if (cap) out[0] = '\0';
	if (!text || !key) return 0;

	size_t klen = strlen(key);
	for (const char *p = text; p && *p; ) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);

		/* Skip the leading whitespace these tools indent with. */
		const char *s = p;
		while (len && (*s == ' ' || *s == '\t')) { s++; len--; }

		if (len > klen && strncmp(s, key, klen) == 0 && s[klen] == ':') {
			const char *v = s + klen + 1;
			size_t vlen = len - klen - 1;
			while (vlen && (*v == ' ' || *v == '\t')) { v++; vlen--; }
			while (vlen && (v[vlen - 1] == ' ' || v[vlen - 1] == '\r')) vlen--;
			if (vlen >= cap) vlen = cap - 1;
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			tsv_clean(out);
			return 1;
		}
		p = eol ? eol + 1 : NULL;
	}
	return 0;
}
