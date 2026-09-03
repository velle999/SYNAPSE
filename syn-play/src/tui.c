/* tui.c — the queue, the history and the playlists, with arrow keys.
 *
 * The third face over the same core: the CLI prints, the window draws, this one
 * does both, and none of the three knows anything the others do not.
 *
 * ── What is and is not done to the terminal ─────────────────────────────────
 *
 * The same restraint syn-cal's and synfiles' TUIs document, for the same
 * reasons:
 *
 *   - ICANON and ECHO come off, so a keystroke arrives without Enter.
 *   - NO mouse reporting. A TUI killed mid-flight never sends the disable, and
 *     the shell underneath then reads every pointer movement as typed input —
 *     which lands in .bash_history.
 *   - NO alternate screen. What you looked at stays in the scrollback.
 *   - ISIG stays ON, so Ctrl+C still interrupts.
 *   - OPOST stays ON, so "\n" still carries a carriage return.
 *
 * So the worst a hard kill leaves is a terminal with echo off, which `reset`
 * fixes. Restoring is wired to atexit AND to the signals, so everything short
 * of SIGKILL puts it back.
 *
 * ⚠ AND IT WORKS WITH NO TERMINAL AT ALL. Piped, it prints the queue once and
 * exits — which is how it is tested, and what happens when somebody runs it
 * over ssh in a script.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"
#include "i18n.h"

#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* ── the terminal ───────────────────────────────────────────────────────── */

static struct termios g_saved;
static bool g_cbreak = false;

static void tty_restore(void)
{
	if (!g_cbreak) return;
	g_cbreak = false;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
	fputs("\033[?25h", stdout);          /* only ever turns the cursor ON */
	fflush(stdout);
}

/* Restore, then die of the signal rather than exiting 0 — a caller waiting on
 * this process has to be able to tell it was interrupted. */
static void tty_signal(int sig)
{
	tty_restore();
	signal(sig, SIG_DFL);
	raise(sig);
}

static bool tty_raw(void)
{
	if (!isatty(STDIN_FILENO)) return false;
	if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return false;

	struct termios t = g_saved;
	t.c_lflag &= (tcflag_t)~(ICANON | ECHO);      /* ISIG stays on */
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) return false;

	/*
	 * ⛔ UNBUFFERED, BECAUSE THIS LOOP POLLS THE FILE DESCRIPTOR. getchar()
	 * on a buffered stream reads as much as is available and hands back one
	 * byte, leaving the rest inside libc where poll() cannot see it. An
	 * arrow key is THREE bytes that arrive together: the first getchar()
	 * would swallow all three, the 20ms poll for the tail would find nothing
	 * on the fd, and the `[` and `A` would come back later as two keystrokes
	 * that mean nothing — an arrow key that intermittently does nothing.
	 * Same fault as serve.c's, which cost a dropped file.
	 */
	setvbuf(stdin, NULL, _IONBF, 0);

	g_cbreak = true;
	atexit(tty_restore);
	signal(SIGINT, tty_signal);
	signal(SIGTERM, tty_signal);
	signal(SIGHUP, tty_signal);
	signal(SIGQUIT, tty_signal);
	fputs("\033[?25l", stdout);
	return true;
}

static int term_rows(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 6)
		return ws.ws_row;
	return 24;
}

static void fmt_time(double secs, char *out, size_t cap)
{
	if (secs < 0 || secs != secs) { snprintf(out, cap, "--:--"); return; }
	long t = (long)(secs + 0.5);
	long h = t / 3600, m = (t % 3600) / 60, s = t % 60;
	if (h) snprintf(out, cap, "%ld:%02ld:%02ld", h, m, s);
	else   snprintf(out, cap, "%ld:%02ld", m, s);
}

/* ── the view ───────────────────────────────────────────────────────────── */

typedef enum { TAB_QUEUE, TAB_HISTORY, TAB_PLAYLISTS, TAB_N } tab_t;
/* ⚠ MARKED HERE, TRANSLATED AT THE DRAW SITE. The tab's IDENTITY is the
 * tab_t enum, not this string — nothing compares against it — so one table is
 * enough and there is no id-and-label pair to keep apart. */
static const char *const TAB_NAME[TAB_N] = { N_("Queue"), N_("History"),
                                             N_("Playlists") };

static tab_t g_tab = TAB_QUEUE;
static int   g_sel[TAB_N];
static char  g_note[256];

static sp_entry_t g_queue[4096];
static int        g_qn;
static sp_hist_t  g_hist[2000];
static int        g_hn;
static char       g_pl[512][128];
static int        g_pn;

static void load_playlists(void)
{
	g_pn = 0;
	/* ⚠ The listing is `sp_playlist_list()`'s job and is not repeated here —
	 * a second directory walk is a second idea of what a playlist file is.
	 * Its --rec output is parsed back, which is the same contract the window
	 * uses. */
	sp_out_t save = g_out;
	g_out = OUT_REC;
	fflush(stdout);

	int pipefd[2];
	if (pipe(pipefd) != 0) { g_out = save; return; }
	int saved_out = dup(STDOUT_FILENO);
	dup2(pipefd[1], STDOUT_FILENO);
	close(pipefd[1]);
	sp_playlist_list();
	fflush(stdout);
	dup2(saved_out, STDOUT_FILENO);
	close(saved_out);
	g_out = save;

	FILE *f = fdopen(pipefd[0], "r");
	if (!f) { close(pipefd[0]); return; }
	char line[256];
	while (g_pn < 512 && sp_getline(f, line, sizeof line)) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		char *name = tab + 1;
		char *end = strchr(name, '\t');
		if (end) *end = '\0';
		snprintf(g_pl[g_pn++], sizeof g_pl[0], "%s", name);
	}
	fclose(f);
}

static void clamp(void)
{
	int max = (g_tab == TAB_QUEUE) ? g_qn : (g_tab == TAB_HISTORY) ? g_hn : g_pn;
	if (g_sel[g_tab] >= max) g_sel[g_tab] = max - 1;
	if (g_sel[g_tab] < 0)    g_sel[g_tab] = 0;
}

static void draw(int fd, bool interactive)
{
	char path[PATH_MAX] = "", title[512] = "";
	double pos = 0, dur = 0, vol = 0;
	bool paused = false;

	if (fd >= 0) {
		sp_get_str(fd, "path", path, sizeof path);
		sp_now_title(fd, path, title, sizeof title);
		sp_get_num(fd, "time-pos", &pos);
		sp_get_num(fd, "duration", &dur);
		sp_get_num(fd, "volume", &vol);
		sp_get_bool(fd, "pause", &paused);
	}

	int rows = interactive ? term_rows() : 1000;
	/* header 4 lines, tab bar 1, keys 1, and one spare so the last row is
	 * never written into the bottom-right cell (which scrolls on some
	 * terminals and pushes the whole frame up by one). */
	int room = rows - 7;
	if (room < 3) room = 3;

	if (interactive) fputs("\033[H\033[2J", stdout);

	if (!path[0]) {
		printf(_("syn-play — nothing playing\n"));
	} else {
		char a[16], b[16];
		fmt_time(pos, a, sizeof a);
		fmt_time(dur, b, sizeof b);
		/* Two lines rather than "[paused] " in a %s slot: a fragment in
		 * brackets is a word no language can place for itself. */
		if (paused) printf(_("syn-play — [paused] %s\n"), title);
		else        printf(_("syn-play — %s\n"), title);

		int width = 40;
		int filled = (dur > 0.5) ? (int)(pos / dur * width) : 0;
		if (filled < 0) filled = 0;
		if (filled > width) filled = width;
		fputs("  [", stdout);
		for (int i = 0; i < width; i++) fputc(i < filled ? '#' : '-', stdout);
		printf(_("]  %s / %s   vol %.0f\n"), a, b, vol);
	}
	fputc('\n', stdout);

	for (int i = 0; i < TAB_N; i++) {
		bool on = ((tab_t)i == g_tab);
		printf("%s%s%s  ", on ? "[" : " ", _(TAB_NAME[i]), on ? "]" : " ");
	}
	fputc('\n', stdout);

	int max = (g_tab == TAB_QUEUE) ? g_qn : (g_tab == TAB_HISTORY) ? g_hn : g_pn;
	/* The selection is kept on screen by scrolling the window around it,
	 * rather than by clamping it to the first page — a 400-row history whose
	 * cursor stops at line 17 is a list you cannot reach the bottom of. */
	int first = g_sel[g_tab] - room / 2;
	if (first > max - room) first = max - room;
	if (first < 0) first = 0;

	for (int i = first; i < max && i < first + room; i++) {
		bool cur = (i == g_sel[g_tab]);
		if (g_tab == TAB_QUEUE) {
			printf("%s%s%3d  %.90s\n", cur ? ">" : " ",
			       g_queue[i].current ? "*" : " ", i + 1, g_queue[i].title);
		} else if (g_tab == TAB_HISTORY) {
			char at[16];
			fmt_time(g_hist[i].pos, at, sizeof at);
			bool part = g_hist[i].pos > 30 &&
			            (g_hist[i].dur <= 0 || g_hist[i].pos < g_hist[i].dur - 30);
			printf("%s  %-58.58s %s\n", cur ? ">" : " ", g_hist[i].title,
			       part ? at : "");
		} else {
			printf("%s  %s\n", cur ? ">" : " ", g_pl[i]);
		}
	}
	if (max == 0) {
		if (g_tab == TAB_QUEUE)        printf(_("   the queue is empty\n"));
		else if (g_tab == TAB_HISTORY) printf(_("   nothing played yet\n"));
		else                           printf(_("   no playlists saved\n"));
	}

	if (!interactive) return;

	fputc('\n', stdout);
	if (g_note[0]) { printf("  %s\n", g_note); g_note[0] = '\0'; }
	else if (g_tab == TAB_QUEUE)
		printf(_("  space pause  n/p track  ↑↓ move  enter play  x remove  "
		         "s shuffle  S save  tab view  q quit\n"));
	else if (g_tab == TAB_HISTORY)
		printf(_("  enter play  a queue  ↑↓ move  tab view  q quit\n"));
	else
		printf(_("  enter load  a append  D delete  ↑↓ move  tab view  q quit\n"));
	fflush(stdout);
}

static void refresh(int fd)
{
	g_qn = (fd >= 0) ? sp_queue(fd, g_queue, 4096) : 0;
	if (g_qn < 0) g_qn = 0;
	g_hn = sp_history_read(g_hist, 2000);
	load_playlists();
	clamp();
}

/* ── the loop ───────────────────────────────────────────────────────────── */

static void play_selected(int *fd, bool append)
{
	const char *target = NULL;
	if (g_tab == TAB_HISTORY && g_hn) target = g_hist[g_sel[TAB_HISTORY]].path;
	if (!target) return;

	if (*fd < 0) *fd = sp_connect_or_start();
	if (*fd < 0) { snprintf(g_note, sizeof g_note, "%s", _("could not start mpv")); return; }

	if (sp_load(*fd, target, append ? "append-play" : "replace")) {
		char title[512];
		sp_pretty_title(target, title, sizeof title);
		sp_history_note(target, title, 0, 0);
		/* Two whole notes; "queued" in a slot cannot agree with the title. */
		if (append) snprintf(g_note, sizeof g_note, _("queued %.200s"), title);
		else        snprintf(g_note, sizeof g_note, _("playing %.200s"), title);
	} else {
		snprintf(g_note, sizeof g_note, "%s", _("mpv would not take that"));
	}
}

/* A one-line prompt on the bottom row, with the terminal put back to cooked
 * mode for the duration — so backspace, Ctrl+U and a UTF-8 paste all behave
 * the way they do everywhere else instead of being re-implemented badly. */
static bool prompt(const char *label, char *out, size_t cap)
{
	tty_restore();
	printf("\n%s", label);
	fflush(stdout);
	bool ok = sp_getline(stdin, out, cap) && out[0];
	tty_raw();
	return ok;
}

int sp_tui(void)
{
	int fd = sp_connect();

	if (!tty_raw()) {
		/* No terminal: print the queue once and go. This is the path the
		 * test suite takes, and the one a script gets. */
		refresh(fd);
		draw(fd, false);
		if (fd >= 0) close(fd);
		return 0;
	}

	refresh(fd);
	for (;;) {
		draw(fd, true);

		struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
		/* Redraws twice a second so the position moves, and immediately on
		 * a keystroke. A tighter loop would be a wakeup per frame for a
		 * clock that ticks in seconds. */
		int ready = poll(&pfd, 1, 500);
		if (ready <= 0) {
			if (fd < 0) fd = sp_connect();
			continue;
		}

		int c = getchar();
		if (c == EOF || c == 'q') break;

		/* Arrow keys arrive as ESC [ A. ⚠ A BARE ESC IS ALSO A KEY, and
		 * waiting for the rest of a sequence that is not coming hangs the
		 * TUI until the next keypress — so the tail is only read when it
		 * is already there. */
		if (c == 27) {
			struct pollfd e = { .fd = STDIN_FILENO, .events = POLLIN };
			if (poll(&e, 1, 20) <= 0) continue;
			if (getchar() != '[') continue;
			c = getchar();
			if (c == 'A') c = 'k';
			else if (c == 'B') c = 'j';
			else if (c == 'C') c = 'l';
			else if (c == 'D') c = 'h';
			else continue;
		}

		switch (c) {
		case '\t': g_tab = (tab_t)((g_tab + 1) % TAB_N); clamp(); break;
		case 'j':  g_sel[g_tab]++; clamp(); break;
		case 'k':  g_sel[g_tab]--; clamp(); break;
		case 'g':  g_sel[g_tab] = 0; break;
		case 'G':  g_sel[g_tab] = 1 << 30; clamp(); break;

		case ' ':
			if (fd >= 0) sp_cmd(fd, "\"cycle\",\"pause\"", NULL, 0);
			break;
		case 'n':
			if (fd >= 0) sp_cmd(fd, "\"playlist-next\",\"force\"", NULL, 0);
			refresh(fd);
			break;
		case 'p':
			if (fd >= 0) sp_cmd(fd, "\"playlist-prev\",\"force\"", NULL, 0);
			refresh(fd);
			break;
		case 'l':
			if (fd >= 0) sp_cmd(fd, "\"seek\",10,\"relative\"", NULL, 0);
			break;
		case 'h':
			if (fd >= 0) sp_cmd(fd, "\"seek\",-10,\"relative\"", NULL, 0);
			break;

		case 's':
			if (fd >= 0) sp_expand_queue_dirs(fd);
			if (fd >= 0 && sp_cmd(fd, "\"playlist-shuffle\"", NULL, 0))
				snprintf(g_note, sizeof g_note, "%s", _("shuffled"));
			refresh(fd);
			break;
		case 'u':
			if (fd >= 0 && sp_cmd(fd, "\"playlist-unshuffle\"", NULL, 0))
				snprintf(g_note, sizeof g_note, "%s",
				         _("unshuffled — back to the order added"));
			refresh(fd);
			break;

		case '\n':
		case '\r':
			if (g_tab == TAB_QUEUE && g_qn && fd >= 0) {
				char a[64];
				snprintf(a, sizeof a, "\"playlist-play-index\",%d", g_sel[TAB_QUEUE]);
				sp_cmd(fd, a, NULL, 0);
			} else if (g_tab == TAB_HISTORY) {
				play_selected(&fd, false);
			} else if (g_tab == TAB_PLAYLISTS && g_pn) {
				if (fd < 0) fd = sp_connect_or_start();
				if (fd >= 0) {
					char path[PATH_MAX], quoted[PATH_MAX * 2], args[PATH_MAX * 2 + 64];
					if (sp_playlist_path(g_pl[g_sel[TAB_PLAYLISTS]], path, sizeof path)) {
						sp_json_quote(path, quoted, sizeof quoted);
						snprintf(args, sizeof args, "\"loadlist\",%s,\"replace\"", quoted);
						sp_cmd(fd, args, NULL, 0);
						snprintf(g_note, sizeof g_note, _("playing %s"),
						         g_pl[g_sel[TAB_PLAYLISTS]]);
					}
				}
			}
			refresh(fd);
			break;

		case 'a':
			if (g_tab == TAB_HISTORY) play_selected(&fd, true);
			else if (g_tab == TAB_PLAYLISTS && g_pn) {
				if (fd < 0) fd = sp_connect_or_start();
				if (fd >= 0) sp_playlist_load(fd, g_pl[g_sel[TAB_PLAYLISTS]], true);
			}
			refresh(fd);
			break;

		case 'x':
			if (g_tab == TAB_QUEUE && g_qn && fd >= 0) {
				char a[64];
				snprintf(a, sizeof a, "\"playlist-remove\",%d", g_sel[TAB_QUEUE]);
				sp_cmd(fd, a, NULL, 0);
				refresh(fd);
			}
			break;

		case 'D':
			if (g_tab == TAB_PLAYLISTS && g_pn) {
				char path[PATH_MAX];
				if (sp_playlist_path(g_pl[g_sel[TAB_PLAYLISTS]], path, sizeof path)) {
					unlink(path);
					snprintf(g_note, sizeof g_note, _("deleted %s"),
					         g_pl[g_sel[TAB_PLAYLISTS]]);
				}
				refresh(fd);
			}
			break;

		case 'S': {
			char name[128];
			/* ⚠ Guarded here because sp_playlist_save() dies on an empty
			 * queue — correct for a command line, and in a TUI it would be
			 * the whole program exiting under somebody's hands. */
			if (g_qn == 0) { snprintf(g_note, sizeof g_note, "%s",
			                          _("nothing in the queue to save")); break; }
			if (fd >= 0 && prompt(_("save the queue as: "), name, sizeof name)) {
				char path[PATH_MAX];
				if (!sp_playlist_path(name, path, sizeof path))
					snprintf(g_note, sizeof g_note,
					         _("'%s' is not a name a playlist can have"), name);
				else {
					/* sp_playlist_save prints and may die(); the TUI wants
					 * neither, so the queue is written here through the
					 * same file format. */
					sp_out_t save = g_out;
					g_out = OUT_REC;
					fflush(stdout);
					int devnull = dup(STDOUT_FILENO);
					FILE *sink = freopen("/dev/null", "w", stdout);
					sp_playlist_save(fd, name);
					if (sink) { fflush(stdout); dup2(devnull, STDOUT_FILENO); }
					close(devnull);
					g_out = save;
					snprintf(g_note, sizeof g_note, _("saved %s"), name);
				}
			}
			refresh(fd);
			break;
		}

		case '/': {
			char q[256];
			if (prompt(_("open: "), q, sizeof q)) {
				static sp_entry_t hit[1];
				if (sp_find(q, hit, 1) == 1) {
					if (fd < 0) fd = sp_connect_or_start();
					if (fd >= 0) {
						sp_load(fd, hit[0].path, "replace");
						sp_history_note(hit[0].path, hit[0].title, 0, 0);
						snprintf(g_note, sizeof g_note, _("playing %.200s"), hit[0].title);
					}
				} else {
					snprintf(g_note, sizeof g_note, _("nothing matches '%.200s'"), q);
				}
			}
			refresh(fd);
			break;
		}

		case 'r': refresh(fd); break;
		}
	}

	tty_restore();
	fputc('\n', stdout);
	if (fd >= 0) close(fd);
	return 0;
}
