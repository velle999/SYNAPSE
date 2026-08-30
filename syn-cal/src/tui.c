/* tui.c — the calendar in a terminal.
 *
 * A month grid and the selected day's events, arrow-key driven. The third front
 * end over the same core: the CLI prints, the window draws, this one does both
 * and none of the three knows anything the others do not.
 *
 * ── What is and is not done to the terminal ─────────────────────────────────
 *
 * The same restraint synfiles' TUI documents at length, for the same reasons:
 *
 *   - ICANON and ECHO come off, so a keystroke arrives without Enter.
 *   - NO mouse reporting. A TUI killed mid-flight never sends the disable, and
 *     the shell underneath then reads every pointer movement as typed input —
 *     which lands in .bash_history.
 *   - NO alternate screen. What you looked at stays in the scrollback.
 *   - ISIG stays ON, so Ctrl+C still interrupts.
 *   - OPOST stays ON, so "\n" still carries a carriage return and every plain
 *     printf here goes on working.
 *
 * So the worst a hard kill leaves is a terminal with echo off, which `reset`
 * fixes and which spews nothing anywhere. Restoring is wired to atexit AND to
 * the four signals, so everything short of SIGKILL puts it back.
 *
 * ⚠ AND IT WORKS WITH NO TERMINAL AT ALL. Piped, it prints the month once and
 * exits — which is how it is tested, and what happens when somebody runs it
 * over ssh in a script.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "event.h"
#include "month.h"
#include "syncal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ── the terminal ───────────────────────────────────────────────────────── */

static struct termios g_saved;
static bool g_cbreak = false;

static void tty_restore(void)
{
	if (!g_cbreak) return;
	g_cbreak = false;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
	fputs("\033[?25h", stdout);      /* only ever turns the cursor back ON */
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

static bool tty_cbreak(void)
{
	if (!isatty(STDIN_FILENO)) return false;
	if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return false;

	struct termios raw = g_saved;
	raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;

	g_cbreak = true;
	atexit(tty_restore);
	signal(SIGINT, tty_signal);
	signal(SIGTERM, tty_signal);
	signal(SIGHUP, tty_signal);
	signal(SIGQUIT, tty_signal);
	return true;
}

/* ── the month ──────────────────────────────────────────────────────────── */

#define C_RESET  "\033[0m"
#define C_DIM    "\033[2m"
#define C_BOLD   "\033[1m"
#define C_ACC    "\033[36m"
#define C_SEL    "\033[7m"

static void draw(int year, int mon, int sel, events_t *ev, bool colour)
{
	const char *R = colour ? C_RESET : "";
	const char *D = colour ? C_DIM : "";
	const char *B = colour ? C_BOLD : "";
	const char *A = colour ? C_ACC : "";
	const char *S = colour ? C_SEL : "";

	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = year - 1900;
	t.tm_mon = mon;
	t.tm_mday = 1;
	char title[64];
	strftime(title, sizeof title, "%B %Y", &t);

	time_t now = time(NULL);
	struct tm tn;
	localtime_r(&now, &tn);
	int t_y = tn.tm_year + 1900, t_m = tn.tm_mon, t_d = tn.tm_mday;

	/* Which days have anything. One pass, so the grid below is a lookup. */
	bool busy[32] = { false };
	for (size_t i = 0; i < ev->n; i++) {
		struct tm lt;
		localtime_r(&ev->e[i].start, &lt);
		if (lt.tm_year + 1900 == year && lt.tm_mon == mon)
			busy[lt.tm_mday] = true;
	}

	printf("\n  %s%s%s\n\n", B, title, R);
	printf("  %sMo Tu We Th Fr Sa Su%s\n", D, R);

	month_t m;
	month_load(&m, year, mon);
	int first = m.first, dim = m.days;
	printf("  ");
	for (int i = 0; i < first; i++) printf("   ");

	for (int day = 1; day <= dim; day++) {
		bool is_today = (year == t_y && mon == t_m && day == t_d);
		bool is_sel = (day == sel);

		/* ⚠ THE MARK IS PART OF THE TWO-CHARACTER CELL, not an extra column.
		 * A dot appended after the number would push every later day one place
		 * right and the whole grid would stop lining up under its heading. */
		char cell[8];
		snprintf(cell, sizeof cell, "%2d", day);

		if (is_sel)        printf("%s%s%s", S, cell, R);
		else if (is_today) printf("%s%s%s", A, cell, R);
		else if (busy[day]) printf("%s%s%s", B, cell, R);
		else               printf("%s", cell);

		/* ⚠ A IS A VARIABLE, NOT A MACRO, so it cannot be concatenated with a
		 * string literal — the colour has to be a format argument. */
		/* ⚠ THE DOT DEFERS TO THE HIGHLIGHT ONLY WHEN THERE IS ONE. Piped, or
		 * under NO_COLOR, there is no reverse video for it to clash with — and
		 * suppressing it there leaves the selected day looking empty on exactly
		 * the output somebody is reading in a script. */
		if (busy[day] && (!is_sel || !colour)) printf("%s\u00b7%s", A, R);
		else                                   putchar(' ');

		if ((first + day) % 7 == 0 && day != dim) printf("\n  ");
	}
	printf("\n\n");

	/* The selected day, in full. */
	/* ⚠ AND THE HEADING NAMES THE SELECTED DAY IN WORDS. It is the only thing
	 * that says which day is selected when the grid has no colour to say it
	 * with, and it is useful even when it does. */
	printf("  %s%d %s%s\n", B, sel, title, R);
	int shown = 0;
	for (size_t i = 0; i < ev->n; i++) {
		struct tm lt;
		localtime_r(&ev->e[i].start, &lt);
		if (lt.tm_year + 1900 != year || lt.tm_mon != mon || lt.tm_mday != sel) continue;

		if (ev->e[i].all_day)
			printf("    %sall day%s  %s", D, R, ev->e[i].summary ? ev->e[i].summary : "(no title)");
		else
			printf("    %02d:%02d    %s", lt.tm_hour, lt.tm_min,
			       ev->e[i].summary ? ev->e[i].summary : "(no title)");
		if (ev->e[i].location && *ev->e[i].location) printf("  %s— %s%s", D, ev->e[i].location, R);
		putchar('\n');
		shown++;
	}
	if (!shown) printf("    %snothing on%s\n", D, R);

	printf("\n  %s←→ day · ↑↓ week · [ ] month · t today · q quit%s\n", D, R);
}

/* ── the loop ───────────────────────────────────────────────────────────── */

/* Whole months, clamping the selection: the 31st of March stepped back lands on
 * the 28th of February, not on a day that does not exist. */
static void tui_step_month(int *year, int *mon, int *sel, int delta)
{
	month_step(year, mon, delta);
	int dim = month_days_in(*year, *mon);
	if (*sel > dim) *sel = dim;
}

int cmd_tui(void)
{
	time_t now = time(NULL);
	struct tm t;
	localtime_r(&now, &t);
	int year = t.tm_year + 1900, mon = t.tm_mon, sel = t.tm_mday;

	bool interactive = tty_cbreak();
	/* The same answer main() worked out for every other command; asking the
	 * question a second time here is how the two drift apart. */
	bool colour = g_color;

	int loaded_year = -1, loaded_mon = -1;
	events_t ev;
	events_init(&ev);

	for (;;) {
		if (year != loaded_year || mon != loaded_mon) {
			events_free(&ev);
			char *err = NULL;
			month_t m;
			month_load(&m, year, mon);
			/* ⚠ TO THE 1ST OF THE NEXT MONTH, not the 1st plus 31 days: that
			 * over-read pulled in early events from the following month, which
			 * the day filter in draw() then had to drop. The month knows where
			 * it ends. */
			if (!agenda_range(m.start, m.end, &ev, &err)) {
				warn("%s", err ? err : "could not read the calendars");
				free(err);
			}
			loaded_year = year;
			loaded_mon = mon;
		}

		draw(year, mon, sel, &ev, colour);

		/* ⚠ PIPED, IT PRINTS ONCE AND STOPS. A loop reading a closed stdin
		 * would spin; and the one-shot form is what makes this testable and
		 * what somebody in a script actually wants. */
		if (!interactive) break;

		int c = getchar();
		if (c == EOF || c == 'q' || c == 'Q') break;

		if (c == '\033') {
			if (getchar() != '[') continue;
			switch (getchar()) {
			case 'C': month_step_day(&year, &mon, &sel, +1); break;   /* right */
			case 'D': month_step_day(&year, &mon, &sel, -1); break;   /* left */
			case 'B': month_step_day(&year, &mon, &sel, +7); break;   /* down */
			case 'A': month_step_day(&year, &mon, &sel, -7); break;   /* up */
			default: break;
			}
			continue;
		}

		switch (c) {
		case ']': case '.': tui_step_month(&year, &mon, &sel, +1); break;
		case '[': case ',': tui_step_month(&year, &mon, &sel, -1); break;
		case 't': case 'T':
			localtime_r(&now, &t);
			year = t.tm_year + 1900; mon = t.tm_mon; sel = t.tm_mday;
			break;
		default: break;
		}
	}

	events_free(&ev);
	tty_restore();
	return 0;
}
