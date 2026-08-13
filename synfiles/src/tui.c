/* tui.c — the terminal file browser.
 *
 * LINE-ORIENTED ON PURPOSE: no ncurses, no alternate screen, no raw mode, no
 * mouse reporting. The same decision synpkg's TUI documents, and it matters
 * more here, not less: this is the front-end someone reaches for over SSH into
 * a machine whose desktop will not start, and a full-screen TUI killed
 * mid-flight never sends the sequences that turn mouse reporting off — the
 * shell underneath then reads every pointer movement as typed input. Nothing
 * in this file changes a terminal mode, so nothing it can do on the way out
 * leaves one broken. Everything here degrades to a scrollback log.
 *
 * IT DOES NOT REIMPLEMENT ANYTHING. The listing comes from sf_scan(), the same
 * scan `list` prints; properties from cmd_info(); sizes from cmd_du(); deletes
 * from cmd_trash(). A browser that walked directories itself would be a second
 * set of answers about symlinks, broken links and sort order, and the two
 * would drift on the first bug fixed in one of them.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PAGE 20

/* ── state ──────────────────────────────────────────────────────────────── */

typedef struct {
	char  cwd[PATH_MAX];
	bool  all;          /* show dotfiles */
	bool  reverse;
	sf_sort_t sort;
	size_t page;
} tui_t;

static char *tui_prompt(const char *text)
{
	static char line[PATH_MAX + 64];
	fputs(text, stdout);
	fflush(stdout);
	if (!fgets(line, sizeof line, stdin))
		return NULL;
	strip_trailing_newline(line);
	return line;
}

static const char *sort_name(sf_sort_t s)
{
	switch (s) {
	case SF_SORT_SIZE:  return "size";
	case SF_SORT_MTIME: return "date";
	case SF_SORT_TYPE:  return "type";
	default:            return "name";
	}
}

/* Join the current directory and a name into an absolute path.
 *
 * The result is what every command below is handed, and it is built from the
 * RAW name — never from anything that has been through the display path. A
 * percent-encoded name handed back to the disk names a different file, or no
 * file at all, and the encoded form is the identity everywhere else in this
 * program. */
static char *path_join(const char *dir, const char *name)
{
	if (!strcmp(dir, "/"))
		return xasprintf("/%s", name);
	return xasprintf("%s/%s", dir, name);
}

/* ── drawing ────────────────────────────────────────────────────────────── */

static void draw_header(const tui_t *t, size_t n, size_t pages)
{
	printf("\n%s╭─ SYNAPSE Files ", C_ACCENT());
	for (int i = 0; i < 44; i++)
		fputs("─", stdout);
	printf("╮%s\n", C_RESET());

	/* The path can be longer than the box; it is the one thing here worth
	 * showing in full, so it wraps rather than being cut. */
	printf("%s│%s  %s%s%s\n", C_ACCENT(), C_RESET(), C_BOLD(), t->cwd, C_RESET());
	printf("%s╰", C_ACCENT());
	for (int i = 0; i < 60; i++)
		fputs("─", stdout);
	printf("╯%s\n", C_RESET());

	printf("  %s%zu item%s · sort %s%s · page %zu/%zu%s\n",
	       C_DIM(), n, n == 1 ? "" : "s", sort_name(t->sort),
	       t->reverse ? " (reversed)" : "",
	       pages ? t->page + 1 : 1, pages ? pages : 1, C_RESET());
}

static void draw_rows(const tui_t *t, sf_entry_t *ents, size_t n)
{
	size_t start = t->page * PAGE;

	for (size_t i = start; i < n && i < start + PAGE; i++) {
		sf_entry_t *e = &ents[i];
		char *sz = e->is_dir ? xstrdup("") : human_size(e->size);

		char when[32] = "";
		struct tm tm;
		if (localtime_r(&e->mtime, &tm))
			strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);

		/* The name is printed as the kernel gave it. A terminal is a display,
		 * and a filename holding an escape sequence could repaint the screen —
		 * but stripping bytes out of it would print a name that is not the
		 * file's, which is the mistake the whole encoding rule in synfiles.h
		 * exists to prevent. Control bytes are replaced one-for-one so the
		 * column still lines up and the name stays the same length. */
		char *shown = xstrdup(e->name);
		for (char *p = shown; *p; p++)
			if ((unsigned char)*p < 0x20 || *p == 0x7f)
				*p = '?';

		printf("  %s%3zu%s %s%s%-32.32s%s %s%9s  %s%s\n",
		       C_DIM(), i + 1, C_RESET(),
		       e->is_dir ? C_ACCENT() : "",
		       e->broken ? "!" : (e->is_dir ? "/" : (e->is_link ? "@" : " ")),
		       shown, C_RESET(),
		       C_DIM(), sz, when, C_RESET());

		free(shown);
		free(sz);
	}
	if (n == 0)
		printf("  %sempty%s\n", C_DIM(), C_RESET());
}

static void draw_help(void)
{
	printf("\n  %s<num> open · u up · h home · c <path> cd · / <text> filter%s\n",
	       C_DIM(), C_RESET());
	printf("  %si <num> info · z <num> size · t <num> trash · e <num> actions%s\n",
	       C_DIM(), C_RESET());
	printf("  %sn/p page · a hidden · s sort · r reverse · g gui · q quit%s\n",
	       C_DIM(), C_RESET());
}

/* ── opening things ─────────────────────────────────────────────────────── */

/* DETACHED, and it has to be.
 *
 * xdg-open blocks for the entire life of the application it starts — measured,
 * not assumed — so running it in the foreground would freeze the browser until
 * the user closed the editor. The double fork is what stops the intermediate
 * child becoming a zombie this program then has to reap.
 *
 * QS_APP_ID is UNSET in the child. This process may itself have been started
 * with one, and everything it spawns inherits the environment: that is how an
 * editor opened from the file list ended up claiming to BE synfiles and got no
 * dock entry of its own. */
static void open_detached(const char *path)
{
	if (!have_cmd("xdg-open")) {
		warn("xdg-open is not installed — cannot open %s", path);
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		warn("cannot fork: %s", strerror(errno));
		return;
	}
	if (pid == 0) {
		if (fork() != 0)
			_exit(0);                  /* intermediate exits at once */
		unsetenv("QS_APP_ID");
		/* Detach from the terminal so the child cannot write over the
		 * browser's own output, and cannot be killed by a Ctrl+C aimed here. */
		setsid();
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO)
				close(devnull);
		}
		execlp("xdg-open", "xdg-open", path, (char *)NULL);
		_exit(127);
	}
	waitpid(pid, NULL, 0);             /* reaps the intermediate, not the app */
	printf("  %sopened%s %s\n", C_DIM(), C_RESET(), path);
}

/* ── the loop ───────────────────────────────────────────────────────────── */

static void go(tui_t *t, const char *path)
{
	char resolved[PATH_MAX];
	if (!realpath(path, resolved)) {
		printf("  %scannot go to %s: %s%s\n", C_WARN(), path,
		       strerror(errno), C_RESET());
		return;
	}

	/* Probe it before committing: landing in a directory that cannot be read
	 * and then showing an empty listing is indistinguishable from an empty
	 * folder, which is the wrong answer to "why is nothing here". */
	size_t probe_n = 0;
	sf_entry_t *probe = sf_scan(resolved, t->all, &probe_n);
	if (!probe) {
		printf("  %scannot read %s: %s%s\n", C_WARN(), resolved,
		       strerror(errno), C_RESET());
		return;
	}
	sf_entries_free(probe, probe_n);

	snprintf(t->cwd, sizeof t->cwd, "%s", resolved);
	t->page = 0;
}

int cmd_tui(int argc, char **argv)
{
	tui_t t = { .all = false, .reverse = false, .sort = SF_SORT_NAME, .page = 0 };

	const char *start = argc > 0 && *argv[0] ? argv[0] : ".";
	char resolved[PATH_MAX];
	if (!realpath(start, resolved))
		die("cannot resolve %s: %s", start, strerror(errno));
	snprintf(t.cwd, sizeof t.cwd, "%s", resolved);

	/* A filter, not a search: it narrows what is already on screen. `find` is
	 * the command that walks a tree, and this browser can call it. */
	char filter[256] = "";

	for (;;) {
		sf_sort_set(t.sort, t.reverse, true);

		size_t n = 0;
		sf_entry_t *ents = sf_scan(t.cwd, t.all, &n);
		if (!ents) {
			printf("  %scannot read %s: %s%s\n", C_WARN(), t.cwd,
			       strerror(errno), C_RESET());
			return 1;
		}

		/* Filtering AFTER the scan keeps one code path for reading a
		 * directory. The rows are compacted in place; the freed names belong
		 * to the entries that go, and the array is freed as one either way. */
		if (*filter) {
			size_t keep = 0;
			for (size_t i = 0; i < n; i++) {
				if (strcasestr(ents[i].name, filter)) {
					ents[keep++] = ents[i];
				} else {
					free(ents[i].name);
					free(ents[i].target);
					free(ents[i].icon);
				}
			}
			n = keep;
		}

		size_t pages = n ? (n + PAGE - 1) / PAGE : 1;
		if (t.page >= pages)
			t.page = pages - 1;

		draw_header(&t, n, pages);
		if (*filter)
			printf("  %sfilter: %s%s  (/ alone clears it)\n",
			       C_WARN(), filter, C_RESET());
		draw_rows(&t, ents, n);
		draw_help();

		char *in = tui_prompt("\n  > ");
		if (!in) {                       /* EOF — a pipe ended, or Ctrl+D */
			printf("\n");
			sf_entries_free(ents, n);
			return 0;
		}
		while (*in == ' ')
			in++;

		if (!strcmp(in, "q") || !strcmp(in, "quit")) {
			sf_entries_free(ents, n);
			return 0;
		}
		if (!*in) { sf_entries_free(ents, n); continue; }

		/* A bare number is the common case and gets no verb. */
		if (in[0] >= '0' && in[0] <= '9') {
			long num = strtol(in, NULL, 10);
			if (num < 1 || (size_t)num > n) {
				printf("  %sno row %ld%s\n", C_WARN(), num, C_RESET());
				sf_entries_free(ents, n);
				continue;
			}
			sf_entry_t *e = &ents[num - 1];
			char *full = path_join(t.cwd, e->name);
			if (e->is_dir) {
				*filter = '\0';
				go(&t, full);
			} else {
				open_detached(full);
			}
			free(full);
			sf_entries_free(ents, n);
			continue;
		}

		char verb = in[0];
		const char *rest = in + 1;
		while (*rest == ' ')
			rest++;

		switch (verb) {
		case 'u':
			*filter = '\0';
			{ char *up = path_join(t.cwd, ".."); go(&t, up); free(up); }
			break;
		case 'h':
			*filter = '\0';
			go(&t, home_dir());
			break;
		case 'c':
			if (!*rest) { printf("  %sc needs a path%s\n", C_WARN(), C_RESET()); break; }
			*filter = '\0';
			go(&t, rest);
			break;
		case 'n':
			if (t.page + 1 < pages) t.page++;
			break;
		case 'p':
			if (t.page) t.page--;
			break;
		case 'a':
			t.all = !t.all;
			t.page = 0;
			break;
		case 'r':
			t.reverse = !t.reverse;
			break;
		case 's':
			/* Cycles, rather than asking: four values and a keystroke each is
			 * faster than a prompt, and the header always says which is on. */
			t.sort = (sf_sort_t)((t.sort + 1) % 4);
			break;
		case '/':
			snprintf(filter, sizeof filter, "%s", rest);
			t.page = 0;
			break;
		case 'g':
			/* The graphical browser, on this folder. Detached for the same
			 * reason opening a file is: it runs until the user closes it. */
			{
				char *self = xasprintf("%s", "synfiles");
				if (have_cmd(self)) {
					pid_t pid = fork();
					if (pid == 0) {
						if (fork() != 0) _exit(0);
						setsid();
						execlp("synfiles", "synfiles", "gui", t.cwd, (char *)NULL);
						_exit(127);
					}
					if (pid > 0) waitpid(pid, NULL, 0);
					printf("  %sopened the window on%s %s\n", C_DIM(), C_RESET(), t.cwd);
				} else {
					warn("synfiles is not on PATH");
				}
				free(self);
			}
			break;
		case 'i': case 'z': case 't': case 'e': {
			long num = strtol(rest, NULL, 10);
			if (num < 1 || (size_t)num > n) {
				printf("  %s%c needs a row number%s\n", C_WARN(), verb, C_RESET());
				break;
			}
			char *full = path_join(t.cwd, ents[num - 1].name);
			char *one[] = { full, NULL };
			printf("\n");
			if (verb == 'i')
				cmd_info(1, one);
			else if (verb == 'z')
				cmd_du(1, one);
			else if (verb == 'e')
				cmd_actions(1, one);
			else
				/* Trash, never delete. cmd_delete is the permanent one and is
				 * gated behind --yes; a single keystroke in a browser must not
				 * reach it. */
				cmd_trash(1, one);
			free(full);
			break;
		}
		default:
			printf("  %sunrecognised — see the keys above%s\n", C_WARN(), C_RESET());
			break;
		}

		sf_entries_free(ents, n);
	}
}
