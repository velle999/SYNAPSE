/* mime.c — file type from name, using the database that already exists.
 *
 * /usr/share/mime/globs2 is shared-mime-info's compiled glob table, kept
 * current by update-mime-database, and it is 1500 lines of
 * "weight:mimetype:glob[:flags]". Parsing it is a morning's work and gets the
 * same answers every other file manager gets, because it is the same data.
 *
 * Content sniffing (libmagic, or shared-mime-info's own magic table) is
 * deliberately NOT done here. A directory listing calls this once per entry,
 * and sniffing means opening and reading every file to draw its icon — which
 * is fine on a local disk and turns a listing of a network share into a stall.
 * Dolphin makes the same trade for the same reason: the icon comes from the
 * name, and content is only consulted when something actually opens the file.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int   weight;
	char *mime;
	char *glob;
	bool  cased;     /* the "cs" flag: match case-sensitively */
	bool  literal;   /* no wildcard — a whole-filename match like "meson.build" */
	size_t globlen;
} glob_t_;

static glob_t_ *g_globs;
static size_t   g_nglobs;
static char    *g_backing;
static bool     g_loaded;

static const char *globs_path(void)
{
	/* Overridable so the test suite can pin behaviour against a fixture
	 * instead of whatever shared-mime-info this machine happens to ship. */
	const char *env = getenv("SYNFILES_GLOBS");
	return env && *env ? env : "/usr/share/mime/globs2";
}

static void load_globs(void)
{
	if (g_loaded)
		return;
	g_loaded = true;

	g_backing = slurp(globs_path());
	if (!g_backing)
		return;   /* no database: everything falls back to a generic icon */

	size_t nlines = 0;
	char **lines = split(g_backing, '\n', &nlines);

	g_globs = xmalloc((nlines ? nlines : 1) * sizeof *g_globs);

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i] || lines[i][0] == '#')
			continue;

		size_t nf = 0;
		char **f = split(lines[i], ':', &nf);
		if (nf >= 3) {
			glob_t_ *e = &g_globs[g_nglobs++];
			e->weight  = atoi(f[0]);
			e->mime    = f[1];
			e->glob    = f[2];
			e->cased   = nf >= 4 && !strcmp(f[3], "cs");
			e->globlen = strlen(f[2]);
			e->literal = strpbrk(f[2], "*?[") == NULL;
		}
		free(f);
	}

	free(lines);
}

/* Freedesktop's precedence, in the order the spec gives it:
 *   1. a literal filename match beats any pattern;
 *   2. a CASE-SENSITIVE match beats a case-insensitive one;
 *   3. otherwise the LONGEST pattern wins ("*.tar.gz" over "*.gz");
 *   4. weight breaks a remaining tie.
 *
 * Rule 3 is what stops an archive reading as a bare gzip stream.
 *
 * Rule 2 is not optional and is easy to miss, because update-mime-database
 * emits every glob TWICE — once flagged "cs" and once not:
 *
 *     50:text/x-c++src:*.C:cs
 *     50:text/x-c++src:*.C
 *     50:text/x-csrc:*.c:cs
 *     50:text/x-csrc:*.c
 *
 * Without rule 2 the uncased "*.C" is matched case-insensitively, so it hits
 * "hello.c", ties with "*.c" on length and weight, and wins purely by
 * appearing earlier in the file — every C source file in the tree reports as
 * C++. Caught by the test suite on 2026-08-09. */
static bool better(const glob_t_ *a, const glob_t_ *best)
{
	if (!best)
		return true;
	if (a->literal != best->literal)
		return a->literal;
	if (a->cased != best->cased)
		return a->cased;
	if (a->globlen != best->globlen)
		return a->globlen > best->globlen;
	return a->weight > best->weight;
}

const char *mime_for(const char *name, bool is_dir)
{
	if (is_dir)
		return "inode/directory";

	load_globs();
	if (!g_nglobs)
		return "application/octet-stream";

	const glob_t_ *best = NULL;
	for (size_t i = 0; i < g_nglobs; i++) {
		const glob_t_ *e = &g_globs[i];
		int flags = e->cased ? 0 : FNM_CASEFOLD;
		if (fnmatch(e->glob, name, flags) == 0 && better(e, best))
			best = e;
	}

	return best ? best->mime : "application/octet-stream";
}

/* The icon naming spec turns "text/plain" into "text-plain". Front-ends fall
 * back from there to "<media>-x-generic" and finally to a blank page, which is
 * why the mime type travels in the row alongside the icon name — the fallback
 * chain is derivable from it and does not need a second column.
 *
 * Directories are the exception worth hardcoding: the icon is "folder", not
 * "inode-directory", and every theme has it. */
const char *icon_for(const char *mime, bool is_dir)
{
	static char buf[128];

	if (is_dir)
		return "folder";
	if (!mime || !*mime)
		return "text-x-generic";

	size_t n = 0;
	for (const char *p = mime; *p && n + 1 < sizeof buf; p++, n++)
		buf[n] = (*p == '/') ? '-' : *p;
	buf[n] = '\0';
	return buf;
}
