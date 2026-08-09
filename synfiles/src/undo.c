/* undo.c — the operation journal.
 *
 * Every mutation this program makes records how to reverse itself, so that the
 * answer to "that moved the wrong folder" is one keystroke rather than an
 * afternoon. The journal is an append-only record file next to the trash, in
 * the same percent-encoded format as everything else, so a filename with a
 * newline in it survives being journalled exactly as it survives being listed.
 *
 * What undo will and will not do, and why:
 *
 *   - move and rename are reversed by moving back. Exactly reversible.
 *   - trash is reversed by restoring, which the trash already knows how to do.
 *   - copy is reversed by TRASHING the copies, never by deleting them. The
 *     copy may have been edited in the meantime, and "undo" must not be a
 *     shorter path to destroying work than delete is.
 *   - mkdir is reversed only if the directory is still empty. Something may
 *     have been put in it.
 *   - `delete --yes` is NOT journalled, because nothing can reverse it. An
 *     undo entry that could not undo would be worse than none: it would look
 *     like a safety net.
 *
 * The rule that governs all of them: undo VERIFIES before it acts. If the file
 * is not where the journal says it should be, or something now occupies the
 * place it came from, undo refuses and says so. A "best effort" undo that
 * guessed would be a second mutation on top of a state nobody predicted.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Entries are kept from growing without bound. 400 is a few days of ordinary
 * use and a file measured in kilobytes; past that the oldest go, because an
 * undo from last month is not one anybody is going to take. */
#define JOURNAL_MAX 400

static char *journal_path(void)
{
	const char *env = getenv("SYNFILES_JOURNAL");
	if (env && *env)
		return xstrdup(env);
	char *data = xdg_data_home();
	char *dir = xasprintf("%s/synfiles", data);
	mkdir(dir, 0700);
	char *p = xasprintf("%s/journal", dir);
	free(dir);
	free(data);
	return p;
}

/* ── writing ────────────────────────────────────────────────────────────── */

/* One batch per process. A `move` of six files is ONE thing the user did, and
 * undoing it has to put all six back — not the last one and then, on a second
 * press, the fifth. */
static long g_batch;

static long last_batch(const char *text)
{
	long last = 0;
	if (!text)
		return 0;
	char *copy = xstrdup(text);
	size_t n = 0;
	char **lines = split(copy, '\n', &n);
	for (size_t i = 0; i < n; i++)
		if (*lines[i])
			last = strtol(lines[i], NULL, 10);
	free(lines);
	free(copy);
	return last;
}

void sf_journal(const char *op, const char *a, const char *b)
{
	char *path = journal_path();
	char *text = slurp(path);

	if (g_batch == 0)
		g_batch = last_batch(text) + 1;

	/* Trim from the front when the file gets long. Rewriting it whole is fine
	 * at this size and keeps the format a plain append everywhere else. */
	char *keep = NULL;
	if (text) {
		size_t n = 0;
		char *copy = xstrdup(text);
		char **lines = split(copy, '\n', &n);
		size_t real = 0;
		for (size_t i = 0; i < n; i++)
			if (*lines[i])
				real++;
		if (real > JOURNAL_MAX) {
			size_t drop = real - JOURNAL_MAX;
			size_t seen = 0;
			keep = xmalloc(strlen(text) + 1);
			keep[0] = '\0';
			for (size_t i = 0; i < n; i++) {
				if (!*lines[i])
					continue;
				if (seen++ < drop)
					continue;
				strcat(keep, lines[i]);
				strcat(keep, "\n");

			}
		}
		free(lines);
		free(copy);
	}

	char *ea = pct_encode(a ? a : "", true);
	char *eb = pct_encode(b ? b : "", true);

	FILE *f = fopen(path, keep ? "w" : "a");
	if (f) {
		if (keep)
			fputs(keep, f);
		fprintf(f, "%ld\t%lld\t%s\t%s\t%s\n", g_batch, (long long)time(NULL),
		        op, ea, eb);
		fclose(f);
		chmod(path, 0600);
	}

	free(ea);
	free(eb);
	free(keep);
	free(text);
	free(path);
}

/* ── reading ────────────────────────────────────────────────────────────── */

typedef struct {
	long   batch;
	long long when;
	char  *op;
	char  *a;      /* decoded */
	char  *b;      /* decoded */
} entry_t;

static entry_t *load_journal(size_t *count, char **backing)
{
	*count = 0;
	*backing = NULL;

	char *path = journal_path();
	char *text = slurp(path);
	free(path);
	if (!text)
		return NULL;

	size_t nlines = 0;
	char **lines = split(text, '\n', &nlines);
	entry_t *out = xmalloc((nlines ? nlines : 1) * sizeof *out);
	size_t k = 0;

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;
		size_t nf = 0;
		char **f = split(lines[i], '\t', &nf);
		if (nf >= 5) {
			out[k].batch = strtol(f[0], NULL, 10);
			out[k].when = strtoll(f[1], NULL, 10);
			out[k].op = f[2];
			out[k].a = pct_decode(f[3]);
			out[k].b = pct_decode(f[4]);
			k++;
		}
		free(f);
	}

	free(lines);
	*backing = text;
	*count = k;
	return out;
}

static void free_journal(entry_t *e, size_t n, char *backing)
{
	for (size_t i = 0; i < n; i++) {
		free(e[i].a);
		free(e[i].b);
	}
	free(e);
	free(backing);
}

/* Drop every entry of `batch`, so an undone operation cannot be undone twice. */
static void forget_batch(long batch)
{
	char *path = journal_path();
	char *text = slurp(path);
	if (!text) {
		free(path);
		return;
	}

	size_t n = 0;
	char *copy = xstrdup(text);
	char **lines = split(copy, '\n', &n);

	FILE *f = fopen(path, "w");
	if (f) {
		for (size_t i = 0; i < n; i++) {
			if (!*lines[i])
				continue;
			if (strtol(lines[i], NULL, 10) == batch)
				continue;
			fprintf(f, "%s\n", lines[i]);
		}
		fclose(f);
		chmod(path, 0600);
	}

	free(lines);
	free(copy);
	free(text);
	free(path);
}

/* A one-line description, for a menu label or a confirmation. */
static char *describe(entry_t *e, size_t n, size_t first, size_t last)
{
	size_t count = last - first + 1;
	const char *op = e[first].op;
	(void)n;

	const char *verb = !strcmp(op, "move")   ? "Move"
	                 : !strcmp(op, "rename") ? "Rename"
	                 : !strcmp(op, "trash")  ? "Move to Trash"
	                 : !strcmp(op, "copy")   ? "Copy"
	                 : !strcmp(op, "mkdir")  ? "New Folder"
	                                         : op;

	if (count == 1) {
		const char *base = sf_basename(e[first].a);
		return xasprintf("%s of %s", verb, base);
	}
	return xasprintf("%s of %zu items", verb, count);
}

/* ── undoing ────────────────────────────────────────────────────────────── */

static int undo_one(entry_t *e)
{
	/* move and rename: put it back, but only if it is still where we left it
	 * and nothing has taken the place it came from. */
	if (!strcmp(e->op, "move") || !strcmp(e->op, "rename")) {
		if (faccessat(AT_FDCWD, e->a, F_OK, AT_SYMLINK_NOFOLLOW) != 0) {
			warn("%s is no longer there — not undoing", e->a);
			return 1;
		}
		if (faccessat(AT_FDCWD, e->b, F_OK, AT_SYMLINK_NOFOLLOW) == 0) {
			warn("%s already exists — not undoing over it", e->b);
			return 1;
		}
		if (rename(e->a, e->b) != 0) {
			warn("cannot put %s back: %s", e->b, strerror(errno));
			return 1;
		}
		if (g_out == OUT_REC) {
			char *x = pct_encode(e->b, true);
			rec_row(3, x, "done", "restored");
			free(x);
		} else {
			printf("moved back to %s\n", e->b);
		}
		return 0;
	}

	/* trash: the trash already knows how to put something back, and going
	 * through it means the .trashinfo is cleaned up the same way. */
	if (!strcmp(e->op, "trash")) {
		/* cmd_trash reads argv[0] as the SUBCOMMAND, not as the program name.
		 * Passing "trash" here made it treat that word as a path and try to
		 * trash a file called "trash". */
		char *enc = pct_encode(e->b, false);
		char *argv[] = { (char *)"restore", enc, NULL };
		int rc = cmd_trash(2, argv);
		free(enc);
		return rc;
	}

	/* copy: the copies go to the TRASH, not to unlink(). They may have been
	 * edited since, and undo must never be a shorter road to losing work than
	 * deleting is. */
	if (!strcmp(e->op, "copy")) {
		if (faccessat(AT_FDCWD, e->a, F_OK, AT_SYMLINK_NOFOLLOW) != 0)
			return 0;   /* already gone; nothing to undo, not an error */
		char *argv[] = { e->a, NULL };
		return cmd_trash(1, argv);
	}

	/* mkdir: only if still empty. Something may have been put in it since,
	 * and removing that is not what "undo the folder I just made" means. */
	if (!strcmp(e->op, "mkdir")) {
		if (faccessat(AT_FDCWD, e->a, F_OK, AT_SYMLINK_NOFOLLOW) != 0)
			return 0;
		if (rmdir(e->a) != 0) {
			if (errno == ENOTEMPTY)
				warn("%s is not empty any more — leaving it", e->a);
			else
				warn("cannot remove %s: %s", e->a, strerror(errno));
			return 1;
		}
		if (g_out == OUT_HUMAN)
			printf("removed %s\n", e->a);
		return 0;
	}

	warn("nothing knows how to undo '%s'", e->op);
	return 1;
}

int cmd_undo(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "";

	size_t n = 0;
	char *backing = NULL;
	entry_t *e = load_journal(&n, &backing);

	if (!strcmp(sub, "clear")) {
		char *path = journal_path();
		unlink(path);
		free(path);
		free_journal(e, n, backing);
		if (g_out == OUT_HUMAN)
			printf("undo history cleared\n");
		return 0;
	}

	if (!strcmp(sub, "list")) {
		if (g_out == OUT_REC)
			rec_row(4, "batch", "when", "op", "what");
		if (n == 0) {
			free_journal(e, n, backing);
			return 100;
		}
		/* Newest batch first — the order somebody would undo them in. */
		size_t i = n;
		while (i > 0) {
			size_t last = i - 1;
			size_t first = last;
			while (first > 0 && e[first - 1].batch == e[last].batch)
				first--;

			char *what = describe(e, n, first, last);
			if (g_out == OUT_REC) {
				char *bt = xasprintf("%ld", e[last].batch);
				char *wt = xasprintf("%lld", e[last].when);
				rec_row(4, bt, wt, e[last].op, what);
				free(bt);
				free(wt);
			} else {
				printf("  %s\n", what);
			}
			free(what);
			i = first;
		}
		free_journal(e, n, backing);
		return 0;
	}

	if (*sub)
		die("undo: unknown subcommand '%s' — try list, clear, or no argument", sub);

	if (n == 0) {
		if (g_out == OUT_REC)
			rec_row(3, "path", "status", "detail");
		else
			printf("nothing to undo\n");
		free_journal(e, n, backing);
		return 100;
	}

	/* The most recent batch, unwound in REVERSE order. A batch may contain
	 * steps that depend on each other, and doing them forwards would put a
	 * file back before the directory it belongs in. */
	size_t last = n - 1;
	size_t first = last;
	while (first > 0 && e[first - 1].batch == e[last].batch)
		first--;

	char *what = describe(e, n, first, last);
	if (g_out == OUT_REC)
		rec_row(3, "path", "status", "detail");
	else
		printf("undoing: %s\n", what);

	int rc = 0;
	size_t i = last + 1;
	while (i > first) {
		i--;
		if (undo_one(&e[i]) != 0)
			rc = 1;
	}

	/* Forgotten either way. A batch that half-undid is not one to offer
	 * again — the second attempt would act on a state nobody predicted. */
	forget_batch(e[last].batch);

	free(what);
	free_journal(e, n, backing);
	return rc;
}
