/* open.c — quick open: find the thing without typing its path.
 *
 * ⚠ HISTORY FIRST, AND THAT IS THE WHOLE TRICK. What somebody wants to play is
 * overwhelmingly something they have played before, and that list is a few
 * hundred rows already in memory — so it is searched before a single directory
 * is opened, and a hit there needs no walk at all. The filesystem walk is the
 * fallback for the first time, and is bounded so a media library on a spinning
 * disk cannot turn "quick open" into a wait.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Kept in one place so the file manager, the TUI and the window agree about
 * what this program will even try to open. */
static const char *const MEDIA_EXT[] = {
	"mkv", "mp4", "avi", "mov", "webm", "m4v", "mpg", "mpeg", "wmv", "flv",
	"ts", "m2ts", "ogv", "3gp",
	"mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav", "wv", "ape",
	"wma", "mka", "aiff", "alac", "mpc",
	"m3u", "m3u8", "pls", "cue",
	NULL
};

bool sp_is_media(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot || !dot[1]) return false;
	char ext[16];
	size_t n = strlen(dot + 1);
	if (n >= sizeof ext) return false;
	for (size_t i = 0; i <= n; i++) ext[i] = (char)tolower((unsigned char)dot[1 + i]);
	for (int i = 0; MEDIA_EXT[i]; i++)
		if (!strcmp(ext, MEDIA_EXT[i])) return true;
	return false;
}

/*
 * Where to look. <config>/roots one per line if it is there, otherwise the
 * places a desktop actually keeps media.
 *
 * ⚠ NOT $HOME. A quick-open that walks a home directory reads a browser
 * profile, a mail store and a source tree looking for an .mp3 — seconds of
 * disk for a search that was supposed to be instant, every time.
 */
int sp_roots(char roots[][PATH_MAX], int max)
{
	int n = 0;

	char cfg[PATH_MAX];
	snprintf(cfg, sizeof cfg, "%s/roots", sp_config_dir());
	FILE *f = fopen(cfg, "r");
	if (f) {
		char line[PATH_MAX];
		while (n < max && sp_getline(f, line, sizeof line)) {
			char *s = line;
			while (*s == ' ' || *s == '\t') s++;
			if (!*s || *s == '#') continue;
			snprintf(roots[n++], PATH_MAX, "%s", s);
		}
		fclose(f);
		if (n) return n;
	}

	const char *home = getenv("SYNPLAY_HOME");
	if (!home || !*home) home = getenv("HOME");
	if (!home || !*home) return 0;

	static const char *const DEFAULTS[] = {
		"Videos", "Movies", "Music", "Downloads", NULL
	};
	for (int i = 0; DEFAULTS[i] && n < max; i++) {
		char p[PATH_MAX];
		snprintf(p, sizeof p, "%s/%s", home, DEFAULTS[i]);
		struct stat st;
		if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
			snprintf(roots[n++], PATH_MAX, "%s", p);
	}
	return n;
}

/* ── ranking ────────────────────────────────────────────────────────────── */

/*
 * A subsequence match, scored. `bhd` finds "Black Hawk Down"; so does "hawk".
 * Higher is better; 0 means it does not match at all.
 *
 * ⚠ THE BASENAME IS WHAT PEOPLE TYPE. Matching the whole path lets a directory
 * called "Music" supply half the letters of every query, so a search for "mus"
 * returns the entire library in whatever order it was walked. The basename is
 * scored, and the rest of the path only breaks ties.
 */
static int score(const char *hay, const char *needle)
{
	if (!*needle) return 1;

	char h[512], q[256];
	size_t hn = 0, qn = 0;
	for (const char *p = hay; *p && hn + 1 < sizeof h; p++)
		h[hn++] = (char)tolower((unsigned char)*p);
	h[hn] = '\0';
	for (const char *p = needle; *p && qn + 1 < sizeof q; p++)
		q[qn++] = (char)tolower((unsigned char)*p);
	q[qn] = '\0';
	if (!qn) return 1;

	/* A whole-word or prefix hit beats any scattered one, by enough that no
	 * amount of subsequence luck can overtake it. */
	const char *sub = strstr(h, q);
	if (sub) return (sub == h) ? 1000 : 700 - (int)(sub - h);

	int s = 0, run = 0;
	size_t qi = 0;
	for (size_t i = 0; i < hn && qi < qn; i++) {
		if (h[i] != q[qi]) { run = 0; continue; }
		qi++;
		run++;
		s += 10 + run * 5;                 /* adjacent letters are worth more */
		if (i == 0 || h[i - 1] == ' ' || h[i - 1] == '/' || h[i - 1] == '.'
		    || h[i - 1] == '-' || h[i - 1] == '_')
			s += 15;                       /* …and so is the start of a word */
	}
	return (qi == qn) ? s : 0;
}

typedef struct { sp_entry_t e; int s; } cand_t;

static int by_score(const void *a, const void *b)
{
	const cand_t *x = a, *y = b;
	if (x->s != y->s) return y->s - x->s;
	return strcmp(x->e.title, y->e.title);
}

static void walk(const char *dir, int depth, const char *query,
                 cand_t *out, int *n, int max, int *budget)
{
	if (depth > 6 || *n >= max || *budget <= 0) return;

	DIR *d = opendir(dir);
	if (!d) return;

	struct dirent *de;
	while ((de = readdir(d)) && *n < max && *budget > 0) {
		if (de->d_name[0] == '.') continue;       /* and no hidden trees */
		(*budget)--;

		char path[PATH_MAX];
		if (snprintf(path, sizeof path, "%s/%s", dir, de->d_name) >= (int)sizeof path)
			continue;

		/* ⚠ ASKED, NOT ASSUMED. d_type is DT_UNKNOWN on some filesystems —
		 * a walk that trusts it silently skips every directory on them. */
		bool isdir;
		if (de->d_type == DT_DIR)       isdir = true;
		else if (de->d_type == DT_REG)  isdir = false;
		else {
			struct stat st;
			if (stat(path, &st) != 0) continue;
			isdir = S_ISDIR(st.st_mode);
		}

		if (isdir) { walk(path, depth + 1, query, out, n, max, budget); continue; }
		if (!sp_is_media(de->d_name)) continue;

		char title[512];
		sp_pretty_title(path, title, sizeof title);
		int s = score(title, query);
		if (!s) continue;

		cand_t *c = &out[(*n)++];
		memset(c, 0, sizeof *c);
		snprintf(c->e.path, sizeof c->e.path, "%s", path);
		snprintf(c->e.title, sizeof c->e.title, "%s", title);
		c->s = s;
	}
	closedir(d);
}

int sp_find(const char *query, sp_entry_t *out, int max)
{
	static cand_t cand[4096];
	int n = 0;

	/* ── history first ─────────────────────────────────────────────────── */
	static sp_hist_t hist[2000];
	int hn = sp_history_read(hist, 2000);
	for (int i = 0; i < hn && n < 4096; i++) {
		int s = score(hist[i].title, query);
		if (!s) s = score(hist[i].path, query) / 4;
		if (!s) continue;
		/*
		 * ⚠ RECENCY IS A BONUS, NOT THE ORDER. Sorting history by time and
		 * calling it a search gives the same three files for every query.
		 * It is worth about one word of a match — enough to break a tie
		 * between two equally good names, not enough to beat a better one.
		 */
		int recent = (i < 10) ? 120 : (i < 50) ? 60 : (i < 200) ? 20 : 0;
		cand_t *c = &cand[n++];
		memset(c, 0, sizeof *c);
		snprintf(c->e.path, sizeof c->e.path, "%s", hist[i].path);
		snprintf(c->e.title, sizeof c->e.title, "%s", hist[i].title);
		c->s = s + recent + 200;      /* something known beats something found */
	}

	/* ── then the roots ────────────────────────────────────────────────── */
	static char roots[16][PATH_MAX];
	int rn = sp_roots(roots, 16);
	/* A budget rather than a timeout: the same library gives the same answer
	 * every time, which a clock-based cutoff cannot promise. */
	int budget = 40000;
	for (int i = 0; i < rn && n < 4096; i++)
		walk(roots[i], 0, query, cand, &n, 4096, &budget);

	qsort(cand, (size_t)n, sizeof cand[0], by_score);

	/* One row per path: a file in history that is also on disk is one result,
	 * and it is the history one, because it sorted higher. */
	int outn = 0;
	for (int i = 0; i < n && outn < max; i++) {
		bool dup = false;
		for (int j = 0; j < outn; j++)
			if (!strcmp(out[j].path, cand[i].e.path)) { dup = true; break; }
		if (!dup) out[outn++] = cand[i].e;
	}
	return outn;
}
