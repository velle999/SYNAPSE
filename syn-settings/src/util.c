/* syn-settings — shared helpers.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

int g_dry_run = 0;

static int run_capture_impl(char *const argv[], char *out, size_t cap,
                           int silence_stderr);
static int run_or_show_impl(char *const argv[], int stream);

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
	/* A name with a slash in it is a PATH, and PATH is not consulted for one —
	 * exactly as execvp decides. Without this, "/usr/bin/syn-settings" was
	 * looked for at "/usr/bin//usr/bin/syn-settings" and every caller was told
	 * the program did not exist: run_quiet(), run_capture() and
	 * run_or_show_progress() all guard on this, so an absolute command was not
	 * refused loudly, it simply never ran. */
	if (strchr(cmd, '/')) return access(cmd, X_OK) == 0;

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
		/* Its own process group, so a command that has to be given up on can
		 * be killed WITH ITS CHILDREN. Killing just the one process leaves a
		 * grandchild holding the write end of this pipe, which is how a hang
		 * stops being visible here and turns into a pipe that never closes for
		 * whoever is reading us. */
		setpgid(0, 0);
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

	/* A DEADLINE ON THE ANSWER.
	 *
	 * Every caller of this is asking a QUESTION — `pacman -Q linux`,
	 * `localectl status`, `bluetoothctl show` — and a question that has not
	 * been answered in ten seconds is not going to be. Long work does not come
	 * through here; that is run_or_show_progress, which streams and is
	 * supposed to take minutes.
	 *
	 * Without this, one command that never returns takes everything with it.
	 * `bluetoothctl show` on a machine whose bluetooth.service is inactive
	 * blocks on D-Bus for ever — no adapter, nothing to answer — and it wedged
	 * the Bluetooth pane, and through the pane the test suite, and through the
	 * suite a package BUILD, which is where it was finally caught: 934 seconds
	 * and rising, zero I/O, stuck on this read.
	 *
	 * Polled rather than made non-blocking, so a command that is slow but
	 * alive still gets its whole ten seconds.
	 */
	/* Ten seconds, overridable ONLY so the suite can prove this works without
	 * spending ten seconds doing it. Not a user setting: a question that needs
	 * longer than this is a question this app should not be asking. */
	int budget_ms = 10000;
	{
		const char *ov = getenv("SYN_SETTINGS_CMD_TIMEOUT_MS");
		if (ov && *ov) {
			int v = atoi(ov);
			if (v > 0) budget_ms = v;
		}
	}
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);

	size_t used = 0;
	int timed_out = 0;
	for (;;) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed = (now.tv_sec - t0.tv_sec) * 1000
		             + (now.tv_nsec - t0.tv_nsec) / 1000000;
		if (elapsed >= budget_ms) { timed_out = 1; break; }

		struct pollfd pfd = { .fd = fds[0], .events = POLLIN, .revents = 0 };
		int pr = poll(&pfd, 1, (int)(budget_ms - elapsed));
		if (pr < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (pr == 0) { timed_out = 1; break; }

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

	if (timed_out) {
		/* Killed, not abandoned. An orphan still holding the other end of a
		 * pipe is how this stops being a hang here and becomes somebody
		 * else's mystery later. */
		kill(-pid, SIGKILL);      /* the group: the command and anything it started */
		kill(pid, SIGKILL);       /* and the process itself, if setpgid lost a race */
		waitpid(pid, NULL, 0);
		if (!silence_stderr)
			fprintf(stderr, "syn-settings: %s did not answer within %d seconds "
			                "— giving up on it\n", argv[0], budget_ms / 1000);
		if (cap) out[0] = '\0';
		return -1;
	}

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

/* ── Live progress ───────────────────────────────────────────────────────────
 *
 * run_quiet() is right for a write that finishes in the time it takes to let go
 * of the mouse — systemctl, localectl, wlr-randr. It is WRONG for the two that
 * do not: installing a kernel and making one bootable. Those hand off to
 * synpkg, which downloads a couple of hundred megabytes, runs mkinitcpio, and
 * on a machine whose repositories lack limine-mkinitcpio-hook falls through to
 * an AUR source build. With their output going to /dev/null, the whole thing
 * was several minutes of a greyed-out button and a status line that had not
 * changed since the click — indistinguishable from a hung app, which is exactly
 * how it was reported.
 *
 * The output already exists and is already live: synpkg writes its progress to
 * STDERR, flushed, one redraw per percent. Nothing needed inventing — it only
 * needed forwarding.
 *
 * Each line the child produces is re-emitted as a record:
 *
 *     progress<TAB>(2/9) linux-cachyos                      62%
 *
 * on our own stdout, flushed, so the GUI sees it as it happens. It is the same
 * TSV the readers print, so the window parses it with the parser it already
 * has, and anything else piping this binary gets a stream it can read too.
 */

/* Strip ANSI escape sequences in place.
 *
 * Belt and braces: synpkg only colours when its stdout is a terminal, and
 * through this pipe it is not. But grub-mkconfig and kernel-install are not
 * ours, a stray CSI would reach the status bar as literal "[0m", and the cost
 * of not finding out is one unreadable line in the middle of a kernel install.
 */
static void strip_ansi(char *s)
{
	char *w = s;
	for (const char *r = s; *r; ) {
		if (*r != 0x1b) { *w++ = *r++; continue; }
		r++;
		if (*r == '[') {              /* CSI: parameters, then a final byte */
			r++;
			while (*r && (*r < 0x40 || *r > 0x7e)) r++;
			if (*r) r++;
		} else if (*r == ']') {       /* OSC: runs to BEL or ST */
			r++;
			while (*r && *r != 0x07 && !(*r == 0x1b && r[1] == '\\')) r++;
			if (*r == 0x1b) r++;
			if (*r) r++;
		} else if (*r) {
			r++;                      /* a two-character escape */
		}
	}
	*w = '\0';
}

static void emit_progress(char *line)
{
	strip_ansi(line);
	tsv_clean(line);   /* a tab in it would invent a column */

	char *p = line;
	while (*p && isspace((unsigned char)*p)) p++;
	size_t n = strlen(p);
	while (n && isspace((unsigned char)p[n - 1])) p[--n] = '\0';
	if (!*p) return;

	printf("progress\t%s\n", p);
	/* Unmissable: our own stdout is a pipe when the GUI is the caller, so
	 * without this the records arrive in 4 KB lumps — which for an install
	 * that prints a few hundred bytes means they arrive when it is over. */
	fflush(stdout);
}

int run_progress(char *const argv[])
{
	if (!argv || !argv[0] || !have_cmd(argv[0])) return -1;

	/* On a terminal there is nothing to translate. Let the child write
	 * straight to it — carriage returns, colour and all — so `syn-settings
	 * pkg install linux-zen` looks like running synpkg, because it is. */
	int piped = !isatty(STDOUT_FILENO);

	int fds[2] = { -1, -1 };
	if (piped && pipe(fds) != 0) return -1;

	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0) {
		if (piped) { close(fds[0]); close(fds[1]); }
		return -1;
	}

	if (pid == 0) {
		if (piped) {
			close(fds[0]);
			/* Both onto the one pipe. synpkg's progress is on stderr and its
			 * records are on stdout; splitting them here would mean two
			 * readers and an ordering question with no answer. */
			dup2(fds[1], STDOUT_FILENO);
			dup2(fds[1], STDERR_FILENO);
			if (fds[1] > STDERR_FILENO) close(fds[1]);
		}
		execvp(argv[0], argv);
		_exit(127);
	}

	if (piped) {
		close(fds[1]);

		/* A FIXED line buffer, flushed when it fills. An accumulating one is
		 * how an unbounded capture ends up OOM-killing the session — a
		 * progress bar that redraws with no newline for an hour is a plausible
		 * thing for someone else's tool to do. */
		char line[512];
		size_t len = 0;
		char buf[512];
		ssize_t n;

		while ((n = read(fds[0], buf, sizeof buf)) > 0) {
			for (ssize_t i = 0; i < n; i++) {
				char c = buf[i];
				/* '\r' ends a line as surely as '\n' does: pacman-style
				 * progress redraws the SAME line, and treating only '\n' as a
				 * terminator would hold the whole download in the buffer and
				 * then emit it as one unreadable record. */
				if (c == '\n' || c == '\r' || len + 1 >= sizeof line) {
					line[len] = '\0';
					emit_progress(line);
					len = 0;
					if (c != '\n' && c != '\r') line[len++] = c;
					continue;
				}
				line[len++] = c;
			}
		}
		line[len] = '\0';
		emit_progress(line);
		close(fds[0]);
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
	return run_or_show_impl(argv, 0);
}

/* Same contract, except the child's output is forwarded as progress records
 * while it runs. For the writes that take minutes rather than milliseconds. */
int run_or_show_progress(char *const argv[])
{
	return run_or_show_impl(argv, 1);
}

static int run_or_show_impl(char *const argv[], int stream)
{
	if (g_dry_run) {
		fputs("would run:", stdout);
		for (int i = 0; argv[i]; i++) printf(" %s", argv[i]);
		putchar('\n');
		return 0;
	}
	int rc = stream ? run_progress(argv) : run_quiet(argv);
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


/* ── Configuration files this app WRITES ─────────────────────────────────────
 *
 * Almost everything here is read from a tool that owns the answer and written
 * by handing the change back to that tool. Two settings have no such tool: the
 * default application for a mime type, and the desktop's clock format. Those
 * are plain files, and these are the helpers for editing one safely — shared
 * rather than copied, because "take a backup, keep the mode, replace
 * atomically" is the part that is easy to get subtly different in two places
 * and only notice after somebody has lost a file.
 */

/* Whole file into a malloc'd buffer, NULL if it is not there. Capped: these
 * are configuration files, and an unbounded read of whatever happens to sit at
 * a path is how a settings app becomes the thing that OOM-kills the session
 * (reference_unbounded_shell_capture_oom_killed_audio). */
#define SLURP_CAP (1u << 20)

char *slurp(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return NULL;

	char *buf = malloc(SLURP_CAP);
	if (!buf) { close(fd); return NULL; }

	size_t used = 0;
	for (;;) {
		ssize_t n = read(fd, buf + used, SLURP_CAP - used - 1);
		if (n <= 0) break;
		used += (size_t)n;
		if (used + 1 >= SLURP_CAP) break;
	}
	buf[used] = '\0';
	close(fd);
	return buf;
}

/* The value of `key` inside `[group]`, or 0. Desktop-entry INI: a group runs
 * until the next line that starts with '['. */
int ini_get(const char *text, const char *group, const char *key,
                   char *out, size_t cap)
{
	if (cap) out[0] = '\0';
	if (!text) return 0;

	size_t klen = strlen(key);
	int in_group = 0;

	for (const char *p = text; p && *p; ) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);

		if (len && *p == '[') {
			char name[128];
			size_t nl = len - 1;
			const char *close = memchr(p, ']', len);
			if (close) nl = (size_t)(close - p) - 1;
			if (nl >= sizeof name) nl = sizeof name - 1;
			memcpy(name, p + 1, nl);
			name[nl] = '\0';
			in_group = !strcmp(name, group);
		} else if (in_group && len > klen && !strncmp(p, key, klen)
		           && p[klen] == '=') {
			size_t vlen = len - klen - 1;
			const char *v = p + klen + 1;
			while (vlen && (v[vlen - 1] == '\r' || v[vlen - 1] == ' '))
				vlen--;
			if (vlen >= cap) vlen = cap - 1;
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			return 1;
		}

		p = eol ? eol + 1 : NULL;
	}
	return 0;
}

const char *env_or(const char *name, const char *fallback)
{
	const char *v = getenv(name);
	return (v && *v) ? v : fallback;
}

/* $HOME-relative default for the XDG single-value variables. "/root" rather
 * than "" as the fallback: an unset HOME must not turn a config path into an
 * absolute path at the filesystem root. */
const char *home_sub(const char *sub, char *buf, size_t cap)
{
	const char *home = env_or("HOME", "/root");
	snprintf(buf, cap, "%s%s", home, sub);
	return buf;
}

/* $XDG_CONFIG_HOME, or its default. Its own function so the two callers that
 * build a path under it start from a buffer the compiler can prove fits. */
void config_home(char *out, size_t cap)
{
	const char *ch = getenv("XDG_CONFIG_HOME");
	if (ch && *ch)
		snprintf(out, cap, "%s", ch);
	else
		home_sub("/.config", out, cap);
}

/* One-time backup before this app first touches a file it did not create.
 * ~/.config/mimeapps.list and synuirc are both hand-edited by real people;
 * O_EXCL is what makes this happen once, ever, rather than overwriting the
 * pre-syn-settings state with a post-syn-settings one on the second run. */
void backup_once(const char *path)
{
	char bak[PATH_CAP + 32];
	if (snprintf(bak, sizeof bak, "%s.pre-syn-settings", path) >= (int)sizeof bak)
		return;

	int in = open(path, O_RDONLY | O_CLOEXEC);
	if (in < 0) return;

	int out = open(bak, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (out < 0) { close(in); return; }

	char buf[4096];
	ssize_t n;
	while ((n = read(in, buf, sizeof buf)) > 0) {
		ssize_t off = 0;
		while (off < n) {
			ssize_t w = write(out, buf + off, (size_t)(n - off));
			if (w <= 0) break;
			off += w;
		}
	}
	close(in);
	close(out);
}

int write_atomic(const char *path, const char *text)
{
	char tmp[PATH_CAP + 16];
	if (snprintf(tmp, sizeof tmp, "%s.new", path) >= (int)sizeof tmp)
		return 1;

	/* The existing file's mode, copied from the FILE not the path, so a
	 * replaced file keeps the permissions it had. A fresh one takes 0644
	 * rather than whatever the umask happens to be
	 * (reference_temp_rename_takes_the_umask_mode). */
	mode_t mode = 0644;
	int old = open(path, O_RDONLY | O_CLOEXEC);
	if (old >= 0) {
		struct stat st;
		if (fstat(old, &st) == 0) mode = st.st_mode & 07777;
		close(old);
	}

	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		fprintf(stderr, "syn-settings: cannot write %s: %s\n",
		        tmp, strerror(errno));
		return 1;
	}
	if (fchmod(fd, mode) != 0) { /* best effort; the content matters more */ }

	size_t len = strlen(text), off = 0;
	while (off < len) {
		ssize_t w = write(fd, text + off, len - off);
		if (w <= 0) {
			close(fd);
			unlink(tmp);
			fprintf(stderr, "syn-settings: short write to %s\n", tmp);
			return 1;
		}
		off += (size_t)w;
	}
	if (fsync(fd) != 0) { /* tmpfs and some filesystems; not fatal */ }
	close(fd);

	if (rename(tmp, path) != 0) {
		unlink(tmp);
		fprintf(stderr, "syn-settings: cannot replace %s: %s\n",
		        path, strerror(errno));
		return 1;
	}
	return 0;
}

/* mkdir -p for the parent of `path`. */
void ensure_parent(const char *path)
{
	char dir[PATH_CAP];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (!slash || slash == dir) return;
	*slash = '\0';

	for (char *p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	mkdir(dir, 0755);
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
