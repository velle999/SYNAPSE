/* tui.c — the terminal file browser.
 *
 * TWO INPUT MODES, and which one runs is decided by whether stdin is a
 * terminal.
 *
 *   PIPED — the original line protocol. `printf '1\nq\n' | synfiles tui` still
 *   works, which is how this is tested and how it is scripted. Nothing about a
 *   pipe changes.
 *
 *   A TERMINAL — arrow keys, with a highlighted row.
 *
 * WHAT IS AND IS NOT DONE TO THE TERMINAL. Arrow keys need each keystroke
 * delivered without waiting for Enter, so ICANON and ECHO come off. Nothing
 * else does:
 *
 *   - NO mouse reporting. That is the one that caused real harm — a TUI killed
 *     mid-flight never sends the disable, the shell underneath then reads every
 *     pointer movement as typed input, and it lands in .bash_history.
 *   - NO alternate screen. What you browsed stays in the scrollback.
 *   - ISIG is KEPT, so Ctrl+C still interrupts rather than being swallowed.
 *   - OPOST is KEPT, so "\n" still carries a carriage return and every plain
 *     printf in this file goes on working. Full raw mode would have needed
 *     "\r\n" everywhere, which is a lot of places to get wrong once.
 *
 * So the worst a hard kill can leave is a terminal with echo off, which `reset`
 * fixes and which does not spew anything anywhere. Restoring is wired to
 * atexit AND to SIGINT/SIGTERM/SIGHUP/SIGQUIT, so everything short of SIGKILL
 * puts it back.
 *
 * Redrawing moves the cursor up and clears below it — cursor movement, not a
 * mode change — so the list updates in place without an alternate screen.
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
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PAGE 20

/* ── the terminal ───────────────────────────────────────────────────────── */

static struct termios g_saved;
static bool g_cbreak = false;

static void tty_restore(void)
{
	if (!g_cbreak)
		return;
	g_cbreak = false;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
	/* Show the cursor. Nothing here ever hides it — this is insurance against
	 * a future redraw that does, and against arriving on a terminal some
	 * earlier program left it hidden on. It is the one sequence this file
	 * emits that is not a colour or a cursor move, and it only ever turns
	 * something back ON. */
	fputs("\033[?25h", stdout);
	fflush(stdout);
}

/* Restore, then die of the signal we were sent rather than exiting 0 — a
 * caller waiting on this process has to be able to tell it was interrupted. */
static void tty_signal(int sig)
{
	tty_restore();
	signal(sig, SIG_DFL);
	raise(sig);
}

static bool tty_cbreak(void)
{
	if (!isatty(STDIN_FILENO))
		return false;
	if (tcgetattr(STDIN_FILENO, &g_saved) != 0)
		return false;

	struct termios raw = g_saved;
	/* ICANON off: a keystroke arrives without Enter. ECHO off: it is not
	 * printed over the listing. ISIG stays ON so Ctrl+C still works, and the
	 * output flags are untouched so "\n" still emits a carriage return. */
	raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
		return false;

	g_cbreak = true;
	atexit(tty_restore);
	signal(SIGINT, tty_signal);
	signal(SIGTERM, tty_signal);
	signal(SIGHUP, tty_signal);
	signal(SIGQUIT, tty_signal);
	return true;
}

/* Run something that writes to the terminal itself — a child process, or a
 * command that prompts — with the terminal back in its normal state. */
static void tty_cooked(void)
{
	if (g_cbreak)
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
}

static void tty_uncooked(void)
{
	if (!g_cbreak)
		return;
	struct termios raw = g_saved;
	raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/* How many rows the listing may use. Derived from the window, so a tall
 * terminal shows more instead of paging at a constant nobody chose; the
 * subtraction is the header, the hint lines and the prompt. */
static size_t tty_page(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 12)
		return (size_t)ws.ws_row - 11;
	return PAGE;
}

/* ── keys ───────────────────────────────────────────────────────────────── */

enum {
	K_NONE = 0,
	K_UP = 256, K_DOWN, K_LEFT, K_RIGHT,
	K_HOME, K_END, K_PGUP, K_PGDN, K_DEL,
	K_ENTER, K_ESC, K_BACKSPACE
};

/* One keypress, with escape sequences decoded.
 *
 * After ESC the terminal is asked for the rest with a 100ms timeout, which is
 * what separates a real arrow key from someone pressing Escape: an arrow
 * arrives as one burst, a bare Escape has nothing behind it. */
static int read_key(void)
{
	unsigned char c;
	if (read(STDIN_FILENO, &c, 1) != 1)
		return K_NONE;

	if (c == '\r' || c == '\n')
		return K_ENTER;
	if (c == 127 || c == 8)
		return K_BACKSPACE;
	if (c != 27)
		return c;

	struct termios t;
	if (tcgetattr(STDIN_FILENO, &t) != 0)
		return K_ESC;
	struct termios timed = t;
	timed.c_cc[VMIN] = 0;
	timed.c_cc[VTIME] = 1;              /* 100ms */
	tcsetattr(STDIN_FILENO, TCSANOW, &timed);

	unsigned char seq[4] = {0};
	ssize_t got = read(STDIN_FILENO, seq, 1);
	int key = K_ESC;

	if (got == 1 && (seq[0] == '[' || seq[0] == 'O')) {
		if (read(STDIN_FILENO, &seq[1], 1) == 1) {
			switch (seq[1]) {
			case 'A': key = K_UP;    break;
			case 'B': key = K_DOWN;  break;
			case 'C': key = K_RIGHT; break;
			case 'D': key = K_LEFT;  break;
			case 'H': key = K_HOME;  break;
			case 'F': key = K_END;   break;
			default:
				/* ESC [ <digit> ~ — Home/End/PgUp/PgDn/Delete on the
				 * keyboards that send the numeric form. */
				if (seq[1] >= '0' && seq[1] <= '9') {
					unsigned char tilde = 0;
					if (read(STDIN_FILENO, &tilde, 1) == 1 && tilde == '~') {
						switch (seq[1]) {
						case '1': case '7': key = K_HOME; break;
						case '4': case '8': key = K_END;  break;
						case '5': key = K_PGUP; break;
						case '6': key = K_PGDN; break;
						case '3': key = K_DEL;  break;
						}
					}
				}
				break;
			}
		}
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &t);
	return key;
}

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

/* `sel` is the highlighted row, or -1 when nothing is (the piped mode has no
 * cursor — there is nothing to move it with). */
static void draw_rows(const tui_t *t, sf_entry_t *ents, size_t n,
                      size_t page_rows, long sel)
{
	size_t start = t->page * page_rows;

	for (size_t i = start; i < n && i < start + page_rows; i++) {
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

		/* The highlight is a ">" and the accent colour, never a reversed
		 * background: a background colour on a row is the one thing that
		 * looks broken on a terminal whose palette does not match the
		 * assumptions, and this program has no idea what palette it is on. */
		bool here = ((long)i == sel);
		printf("%s%s%s%3zu%s %s%s%-32.32s%s %s%9s  %s%s\n",
		       here ? C_ACCENT() : "  ", here ? "> " : "",
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

/* The graphical browser, on this folder. Detached for the same reason opening a
 * file is: it runs until the user closes it, and this browser has to stay
 * usable meanwhile. */
static void open_gui(const char *dir)
{
	if (!have_cmd("synfiles")) {
		warn("synfiles is not on PATH");
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		warn("cannot fork: %s", strerror(errno));
		return;
	}
	if (pid == 0) {
		if (fork() != 0)
			_exit(0);
		setsid();
		execlp("synfiles", "synfiles", "gui", dir, (char *)NULL);
		_exit(127);
	}
	waitpid(pid, NULL, 0);
	printf("  %sopened the window on%s %s\n", C_DIM(), C_RESET(), dir);
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

/* ── row actions, shared by both input modes ────────────────────────────── */
//
// Both loops end up here, so a key and a typed command do exactly the same
// thing. Having each mode call cmd_info/cmd_trash itself is how the two would
// come to disagree about which one trashes and which one deletes.
static void row_action(tui_t *t, sf_entry_t *e, char what)
{
	char *full = path_join(t->cwd, e->name);
	char *one[] = { full, NULL };

	/* Every one of these writes to the terminal, and cmd_du keeps printing
	 * for as long as the walk takes. Hand the terminal back first so the
	 * output lands normally and Ctrl+C reaches the child. */
	tty_cooked();
	printf("\n");
	switch (what) {
	case 'i': cmd_info(1, one); break;
	case 'z': cmd_du(1, one); break;
	case 'e': cmd_actions(1, one); break;
	case 't': cmd_trash(1, one); break;   /* never cmd_delete: that is permanent */
	default: break;
	}
	free(full);

	if (g_cbreak) {
		printf("\n  %spress any key%s", C_DIM(), C_RESET());
		fflush(stdout);
		tty_uncooked();
		read_key();
		printf("\n");
	}
}

/* Ask for a line of text with the terminal in its normal state, so the user
 * can see what they are typing and use backspace. */
static char *ask_line(const char *label)
{
	static char buf[PATH_MAX];
	tty_cooked();
	printf("  %s%s%s ", C_ACCENT(), label, C_RESET());
	fflush(stdout);
	if (!fgets(buf, sizeof buf, stdin)) {
		tty_uncooked();
		return NULL;
	}
	strip_trailing_newline(buf);
	tty_uncooked();
	return buf;
}

/* ── the arrow-key loop ─────────────────────────────────────────────────── */

static int run_keys(tui_t *t)
{
	char filter[256] = "";
	long sel = 0;
	int drawn = 0;                 /* lines the last frame used */

	for (;;) {
		sf_sort_set(t->sort, t->reverse, true);

		size_t n = 0;
		sf_entry_t *ents = sf_scan(t->cwd, t->all, &n);
		if (!ents) {
			printf("  %scannot read %s: %s%s\n", C_WARN(), t->cwd,
			       strerror(errno), C_RESET());
			return 1;
		}
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

		if (sel >= (long)n)
			sel = n ? (long)n - 1 : 0;
		if (sel < 0)
			sel = 0;

		size_t rows = tty_page();
		t->page = n ? (size_t)sel / rows : 0;
		size_t pages = n ? (n + rows - 1) / rows : 1;

		/* Redraw IN PLACE: up over the last frame, then clear from the cursor
		 * down. Cursor movement and an erase — no alternate screen, so what
		 * came before is still in the scrollback where it belongs. */
		if (drawn > 0)
			printf("\033[%dA\033[J", drawn);

		int before = 0;
		draw_header(t, n, pages);          before += 4;
		if (*filter) {
			printf("  %sfilter: %s%s\n", C_WARN(), filter, C_RESET());
			before += 1;
		}
		size_t shown = n > (t->page * rows) ? n - (t->page * rows) : 0;
		if (shown > rows)
			shown = rows;
		draw_rows(t, ents, n, rows, sel);
		before += (int)(shown ? shown : 1);
		printf("\n  %s↑↓ move · → / Enter open · ← up · PgUp/PgDn page%s\n",
		       C_DIM(), C_RESET());
		printf("  %si info · z size · t trash · e actions · / filter · c cd%s\n",
		       C_DIM(), C_RESET());
		printf("  %sa hidden · s sort · r reverse · g gui · h home · q quit%s\n",
		       C_DIM(), C_RESET());
		before += 4;
		drawn = before;

		int k = read_key();
		bool redraw_fresh = false;      /* actions scroll; start a new frame */

		switch (k) {
		case K_NONE: case 'q': case K_ESC:
			sf_entries_free(ents, n);
			printf("\n");
			return 0;

		case K_UP:   if (sel > 0) sel--; break;
		case K_DOWN: if (sel + 1 < (long)n) sel++; break;
		case K_PGUP: sel -= (long)rows; if (sel < 0) sel = 0; break;
		case K_PGDN:
			sel += (long)rows;
			if (sel >= (long)n) sel = n ? (long)n - 1 : 0;
			break;
		case K_HOME: sel = 0; break;
		case K_END:  sel = n ? (long)n - 1 : 0; break;

		case K_LEFT: case K_BACKSPACE: {
			/* Up a level, landing ON the folder just left, which is where the
			 * eye already is. */
			char *leaving = xstrdup(sf_basename(t->cwd));
			char *up = path_join(t->cwd, "..");
			*filter = '\0';
			go(t, up);
			free(up);
			size_t m = 0;
			sf_entry_t *back = sf_scan(t->cwd, t->all, &m);
			if (back) {
				for (size_t i = 0; i < m; i++)
					if (!strcmp(back[i].name, leaving)) { sel = (long)i; break; }
				sf_entries_free(back, m);
			}
			free(leaving);
			break;
		}

		case K_RIGHT: case K_ENTER:
			if (!n) break;
			{
				char *full = path_join(t->cwd, ents[sel].name);
				if (ents[sel].is_dir) {
					*filter = '\0';
					go(t, full);
					sel = 0;
				} else {
					tty_cooked();
					open_detached(full);
					tty_uncooked();
					redraw_fresh = true;
				}
				free(full);
			}
			break;

		case 'a': t->all = !t->all; break;
		case 'r': t->reverse = !t->reverse; break;
		case 's': t->sort = (sf_sort_t)((t->sort + 1) % 4); break;
		case 'h': *filter = '\0'; go(t, home_dir()); sel = 0; break;

		case 'i': case 'z': case 't': case 'e':
			if (n) {
				row_action(t, &ents[sel], (char)k);
				redraw_fresh = true;
			}
			break;

		case '/': {
			char *f = ask_line("filter (empty clears):");
			if (f) snprintf(filter, sizeof filter, "%s", f);
			sel = 0;
			redraw_fresh = true;
			break;
		}
		case 'c': {
			char *p = ask_line("cd to:");
			if (p && *p) { *filter = '\0'; go(t, p); sel = 0; }
			redraw_fresh = true;
			break;
		}
		case 'g':
			tty_cooked();
			open_gui(t->cwd);
			tty_uncooked();
			redraw_fresh = true;
			break;

		default:
			break;                    /* an unbound key does nothing, quietly */
		}

		sf_entries_free(ents, n);
		if (redraw_fresh)
			drawn = 0;               /* something else wrote; do not overdraw it */
	}
}

int cmd_tui(int argc, char **argv)
{
	tui_t t = { .all = false, .reverse = false, .sort = SF_SORT_NAME, .page = 0 };

	const char *start = argc > 0 && *argv[0] ? argv[0] : ".";
	char resolved[PATH_MAX];
	if (!realpath(start, resolved))
		die("cannot resolve %s: %s", start, strerror(errno));
	snprintf(t.cwd, sizeof t.cwd, "%s", resolved);

	/* WHICH LOOP. A terminal gets arrow keys; anything else keeps the line
	 * protocol exactly as it was, because that is what a pipe can drive and
	 * what the tests use. tty_cbreak() answers both questions at once: it
	 * returns false when stdin is not a terminal, and false when the terminal
	 * will not take the mode change — in which case the line loop is a working
	 * browser rather than a failure. */
	if (tty_cbreak()) {
		int rc = run_keys(&t);
		tty_restore();
		return rc;
	}

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
		draw_rows(&t, ents, n, PAGE, -1);
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
			open_gui(t.cwd);
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
