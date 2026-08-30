/* history.c — what has been played, and where it got to.
 *
 * One row per PATH, newest first, rewritten whole. A few thousand rows is tens
 * of kilobytes and this file is read and written by hand at human speed; an
 * append-only log with compaction would be faster at a size nothing here will
 * ever reach, and would need a second piece of code to be correct.
 *
 * ⚠ THE POSITION HERE IS FOR READING, NOT FOR SEEKING. mpv resumes from its own
 * watch-later files (--save-position-on-quit), and that is the number it acts
 * on. This one exists so a list can say "38 minutes in" without asking mpv
 * about a file it is not playing. Two readers of the same fact would be a bug;
 * one actor and one reporter is not.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SP_HISTORY_MAX 2000

/* epoch \t pos \t dur \t path% \t title%  — the two text fields
 * percent-encoded, because a path may contain a tab and a title may contain a
 * newline, and both are separators here. */
static int read_all(sp_hist_t *out, int max)
{
	FILE *f = fopen(sp_history_path(), "r");
	if (!f) return 0;

	int n = 0;
	char line[PATH_MAX * 3];
	while (n < max && sp_getline(f, line, sizeof line)) {
		char *save = NULL;
		char *when  = strtok_r(line, "\t", &save);
		char *pos   = strtok_r(NULL, "\t", &save);
		char *dur   = strtok_r(NULL, "\t", &save);
		char *path  = strtok_r(NULL, "\t", &save);
		char *title = strtok_r(NULL, "\t", &save);
		if (!when || !pos || !dur || !path) continue;

		sp_hist_t *h = &out[n];
		memset(h, 0, sizeof *h);
		h->when = (time_t)strtoll(when, NULL, 10);
		h->pos  = strtod(pos, NULL);
		h->dur  = strtod(dur, NULL);
		sp_dec(path, h->path, sizeof h->path);
		if (title) sp_dec(title, h->title, sizeof h->title);
		if (!h->title[0]) sp_pretty_title(h->path, h->title, sizeof h->title);
		if (!h->path[0]) continue;
		n++;
	}
	fclose(f);
	return n;
}

int sp_history_read(sp_hist_t *out, int max)
{
	return read_all(out, max);
}

static void write_all(sp_hist_t *rows, int n)
{
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.new", sp_history_path());

	FILE *f = fopen(tmp, "w");
	if (!f) return;
	for (int i = 0; i < n && i < SP_HISTORY_MAX; i++) {
		char p[PATH_MAX * 3], t[1536];
		sp_enc(rows[i].path, p, sizeof p);
		sp_enc(rows[i].title, t, sizeof t);
		fprintf(f, "%lld\t%.3f\t%.3f\t%s\t%s\n",
		        (long long)rows[i].when, rows[i].pos, rows[i].dur, p, t);
	}
	if (fclose(f) != 0) { unlink(tmp); return; }
	/* Renamed into place, so a history read while this is being written sees
	 * the old file or the new one and never half of either. */
	if (rename(tmp, sp_history_path()) != 0) unlink(tmp);
}

void sp_history_note(const char *path, const char *title, double pos, double dur)
{
	if (!path || !*path) return;

	static sp_hist_t rows[SP_HISTORY_MAX + 1];
	int n = read_all(rows, SP_HISTORY_MAX);

	/* Find it, and take it out of wherever it was. */
	sp_hist_t old;
	bool had = false;
	for (int i = 0; i < n; i++) {
		if (strcmp(rows[i].path, path)) continue;
		old = rows[i];
		had = true;
		memmove(&rows[i], &rows[i + 1], (size_t)(n - i - 1) * sizeof rows[0]);
		n--;
		break;
	}

	/* Shift down and put it at the front. */
	if (n > SP_HISTORY_MAX - 1) n = SP_HISTORY_MAX - 1;
	memmove(&rows[1], &rows[0], (size_t)n * sizeof rows[0]);
	n++;

	sp_hist_t *h = &rows[0];
	memset(h, 0, sizeof *h);
	h->when = time(NULL);
	snprintf(h->path, sizeof h->path, "%s", path);
	if (title && *title) snprintf(h->title, sizeof h->title, "%s", title);
	else sp_pretty_title(path, h->title, sizeof h->title);

	/*
	 * ⛔ A ZERO POSITION DOES NOT ERASE A REAL ONE. Every file starts at 0,
	 * and the note taken when playback begins arrives before the seek that
	 * resumes it — so writing 0 unconditionally means the row for a film
	 * somebody is 40 minutes into reads "just started" for as long as it
	 * takes them to reach a minute in. Same for a duration mpv has not
	 * worked out yet.
	 */
	h->pos = (pos > 0.5) ? pos : (had ? old.pos : 0.0);
	h->dur = (dur > 0.5) ? dur : (had ? old.dur : 0.0);

	write_all(rows, n);
}

void sp_history_clear(void)
{
	unlink(sp_history_path());
}
