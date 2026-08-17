/* progress.c — pacman's progress bar, ILoveCandy and all.
 *
 * ⚠ SETTING ILoveCandy IS NOT THE SAME AS SEEING IT, and the gap between the
 * two is this file. `ILoveCandy` in pacman.conf is read by pacman(8) and by
 * nothing else: it turns on ONE branch of pacman's own fill_progress(), inside
 * pacman's own callback, in pacman's own process. libalpm knows nothing about
 * it — it hands a front-end a percentage and a name, and what gets drawn is
 * entirely the front-end's business.
 *
 * synpkg is a front-end. So on a box where `pacman -Syu` chomps its way across
 * the terminal, `synpkg upgrade` printed
 *
 *     downloading linux-firmware-20260801.tar.zst        47%
 *
 * and the option looked broken when it was working perfectly, on a command
 * nobody was running. The system upgrade goes through synpkg (see syn-update),
 * which is where a user actually watches packages arrive, so this is where the
 * bar has to be drawn.
 *
 * It is drawn to pacman's proportions on purpose — same geometry, same eight
 * columns of numbers, same candy — because the point is not "a progress bar",
 * it is the one every Arch derivative has. A bar of our own invention would be
 * a different answer to a request that named this one.
 *
 * ── What is read from pacman.conf ───────────────────────────────────────────
 *
 *   ILoveCandy     the chomp. Undocumented in pacman.conf(5) and a real option
 *                  all the same — `pacman-conf ILoveCandy` prints its own name
 *                  when it is set and nothing when it is not, which is the only
 *                  test a bare directive supports.
 *   NoProgressBar  no bar at all, from anybody. Honoured for the same reason
 *                  the repo list is: a user who set it meant every front-end.
 *
 * Colour follows synpkg's own g_color rather than pacman's `Color`, and the
 * split is deliberate: `Color` is a statement about pacman's output, g_color is
 * this process asking whether its own stream is a terminal that wants escapes
 * (and honouring NO_COLOR, which pacman does not). Emitting pacman's hard-coded
 * yellow into a captured log because a config file two programs away said so is
 * how a GUI ends up displaying "\033[1;33mC\033[m".
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* pacman's own numbers, kept because the bar is meant to be recognisable:
 * 8 columns of furniture (" [", "] ", and four for "100%"), the label given
 * whatever is left up to this, and a floor under which there is no room for a
 * bar worth drawing. */
#define PROG_LABEL_MAX 40
#define PROG_FURNITURE 8
#define PROG_MIN_BAR   10

/* The chomp's two mouth positions and the pellets between them, straight out of
 * pacman's fill_progress(): every third cell of the untravelled bar is a pellet
 * and the rest is blank, so the mouth appears to eat its way along a row of
 * dots rather than across a solid gap. */
#define PROG_PELLET_EVERY 3

/* ── what pacman.conf says ──────────────────────────────────────────────── */

/*
 * Asked once and cached, because it is a fork+exec of pacman-conf and this is
 * called per percent per package — several thousand times in an upgrade.
 *
 * ⚠ Cached for the process, NOT for the transaction, and that is fine here:
 * pacman re-reads nothing mid-run either, and a user editing pacman.conf while
 * an upgrade is downloading has a bigger surprise coming than a stale bar.
 */
static bool prog_flag(const char *directive, int *cache)
{
	if (*cache < 0) {
		char *v = pconf(directive);
		*cache = v[0] != '\0';
		free(v);
	}
	return *cache != 0;
}

static bool prog_candy(void)
{
	static int cache = -1;
	return prog_flag("ILoveCandy", &cache);
}

static bool prog_disabled(void)
{
	static int cache = -1;
	return prog_flag("NoProgressBar", &cache);
}

/* ── the terminal ───────────────────────────────────────────────────────── */

/*
 * Usable columns, or 0 for "not a terminal, draw nothing".
 *
 * ⚠ 0 IS THE WHOLE GUARD AGAINST SCRIBBLING IN A LOG. This bar redraws itself
 * with a carriage return, which is a live terminal's idiom and a log file's
 * disaster — syn-update captures synpkg's output in places, and the GUI reads
 * it over a pipe. pacman answers the same question the same way (getcols), and
 * the TSV mode above the call sites is the second, independent guard.
 */
static int prog_cols(void)
{
	if (!isatty(STDERR_FILENO))
		return 0;

	struct winsize ws;
	if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return ws.ws_col;

	/* A terminal that will not say how wide it is. 80 is the answer every
	 * program has given to that question for forty years. */
	const char *env = getenv("COLUMNS");
	int c = env ? atoi(env) : 0;
	return c > 0 ? c : 80;
}

/* ── the bar ────────────────────────────────────────────────────────────── */

/* The mouth's state, the hash count it was last drawn at, and which key owns
 * the line currently on screen. pacman's rule for the first two: the mouth only
 * CHEWS when the bar has actually advanced, so a stalled download shows a mouth
 * frozen mid-bite rather than one chewing on nothing.
 *
 * `prog_open` is the third thing and is not pacman's — it is what lets anything
 * else in synpkg print without landing on top of a half-drawn bar. See
 * progress_end(). */
static int  prog_last_hash = -1;
static int  prog_mouth     = 0;
static bool prog_open      = false;
static char prog_key[256]  = "";

/*
 * One redraw of `label` at `percent`.
 *
 * `key` identifies WHAT is being drawn — a package name, a filename — and is
 * what resets the animation. The chomp carries state between calls (which way
 * the mouth is facing, and whether the bar has actually moved since the last
 * one), and alpm interleaves parallel downloads: without a key, two files
 * progressing at once would hand the same mouth back and forth and the pellets
 * would flicker rather than travel.
 */
void progress_draw(const char *key, const char *label, int percent)
{
	if (g_out == OUT_TSV || prog_disabled())
		return;

	int cols = prog_cols();
	if (cols <= 0)
		return;

	if (percent < 0)   percent = 0;
	if (percent > 100) percent = 100;

	if (strncmp(key ? key : "", prog_key, sizeof prog_key - 1) != 0) {
		snprintf(prog_key, sizeof prog_key, "%s", key ? key : "");
		prog_last_hash = -1;
		prog_mouth     = 0;
	}

	/* Label first, bar with what is left. A narrow terminal loses bar before
	 * it loses name: the name is the information and the bar is the comfort. */
	int label_w = cols / 2;
	if (label_w > PROG_LABEL_MAX) label_w = PROG_LABEL_MAX;
	if (label_w < 1) label_w = 1;

	int bar_w = cols - label_w - PROG_FURNITURE - 1;
	if (bar_w < PROG_MIN_BAR) {
		/* No room for a bar. The percentage still has to arrive, or a
		 * narrow terminal would show a package manager doing nothing. */
		fprintf(stderr, "\r%-*.*s %3d%%", label_w, label_w,
		        label ? label : "", percent);
		prog_open = true;
		if (percent >= 100) progress_end();
		fflush(stderr);
		return;
	}

	int hash = bar_w * percent / 100;

	fprintf(stderr, "\r%-*.*s [", label_w, label_w, label ? label : "");

	if (prog_candy()) {
		/*
		 * pacman's chomp, cell for cell. Counting DOWN from bar_w is not a
		 * stylistic choice: the mouth sits at the boundary between travelled
		 * and untravelled, `i == bar_w - hash`, and the pellet phase is
		 * `i % PROG_PELLET_EVERY`, so the dots are pinned to the bar's right
		 * end and stay put as the mouth advances through them. Counting up
		 * would slide the whole pellet field along with the mouth, which
		 * reads as static rather than as eating.
		 */
		if (prog_last_hash != hash) {
			prog_mouth = !prog_mouth;
			prog_last_hash = hash;
		}
		for (int i = bar_w; i > 0; --i) {
			if (i > bar_w - hash) {
				fputc('-', stderr);
			} else if (i == bar_w - hash) {
				fprintf(stderr, "%s%c%s", g_color ? "\033[1;33m" : "",
				        prog_mouth ? 'C' : 'c', g_color ? "\033[0m" : "");
			} else if (i % PROG_PELLET_EVERY == 0) {
				fprintf(stderr, "%so%s", g_color ? "\033[0;37m" : "",
				        g_color ? "\033[0m" : "");
			} else {
				fputc(' ', stderr);
			}
		}
	} else {
		for (int i = 0; i < bar_w; i++)
			fputc(i < hash ? '#' : '-', stderr);
	}

	fprintf(stderr, "] %3d%%", percent);
	prog_open = true;

	/* The line is redrawn in place until it is finished, and finished means
	 * the caller gets to print the next thing on a line of its own. */
	if (percent >= 100)
		progress_end();
	fflush(stderr);
}

/*
 * Close whatever line is on screen, if there is one.
 *
 * ⚠ IT HAS TO BE IDEMPOTENT AND IT HAS TO KNOW. Every caller that prints
 * anything after a transfer has to end the bar — a download that finished, one
 * that failed, an error, a question — and none of them can tell whether a bar
 * was ever drawn (a whole install can arrive from cache with no transfer at
 * all). A blind newline from each of them is a run pockmarked with blank lines;
 * `prog_open` is the one place that knows, so they can all just ask.
 */
void progress_end(void)
{
	if (!prog_open)
		return;
	prog_open      = false;
	prog_key[0]    = '\0';
	prog_last_hash = -1;
	fputc('\n', stderr);
	fflush(stderr);
}
