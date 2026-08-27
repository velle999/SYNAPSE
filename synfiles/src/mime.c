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

#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
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

/* ── the human name for a type ───────────────────────────────────────────────
 *
 * "Plain text document", not "text/plain". shared-mime-info ships one XML file
 * per type and the FIRST <comment> in it is the untranslated one; every later
 * one carries an xml:lang. There is no compiled table for this the way globs2
 * is compiled for the globs, so the file is read.
 *
 * ⚠ CACHED, because a listing asks about the same handful of types over and
 * over: a folder of photographs has two, /usr/bin has three, and a directory
 * of ten thousand entries would otherwise be ten thousand opens.
 *
 * ⚠ AND ONLY THE HEAD OF THE FILE IS READ. text/plain.xml is 25 KB of
 * translations and the answer is on line four; slurping every type in a
 * listing would be megabytes to produce a dozen short strings.
 */
typedef struct { char *mime, *desc; } desc_t;
static desc_t *g_descs;
static size_t  g_ndescs;

static const char *mime_dir(void)
{
	/* Overridable so the suite can pin behaviour against a fixture rather
	 * than whatever shared-mime-info this machine happens to ship — the same
	 * escape SYNFILES_GLOBS gives the table above. */
	const char *env = getenv("SYNFILES_MIMEDIR");
	return env && *env ? env : "/usr/share/mime";
}

/* The first <comment> with no attributes. Parsed by hand rather than with an
 * XML library: this is one element in a file generated by
 * update-mime-database, and linking a parser to read four lines of it would be
 * a dependency on every machine that lists a directory. */
static char *first_comment(const char *head)
{
	const char *p = strstr(head, "<comment>");
	if (!p)
		return NULL;
	p += strlen("<comment>");
	const char *end = strstr(p, "</comment>");
	if (!end || end == p)
		return NULL;
	return xstrndup(p, (size_t)(end - p));
}

const char *mime_desc(const char *mime)
{
	if (!mime || !*mime)
		return "";

	for (size_t i = 0; i < g_ndescs; i++)
		if (strcmp(g_descs[i].mime, mime) == 0)
			return g_descs[i].desc;

	/* ⛔ THE TYPE BECOMES PART OF A PATH, so it is checked before it is used.
	 * It arrives from globs2 or from this file's own strings today, and a
	 * "type" of "../../etc/passwd" would be read from disk the day one of
	 * those comes from somewhere else. Exactly one slash, and nothing that
	 * can climb. */
	const char *slash = strchr(mime, '/');
	bool ok = slash && slash != mime && slash[1]
	          && !strchr(slash + 1, '/') && !strstr(mime, "..");
	for (const char *p = mime; ok && *p; p++)
		if (!(isalnum((unsigned char)*p) || *p == '/' || *p == '.'
		      || *p == '-' || *p == '+' || *p == '_'))
			ok = false;

	char *desc = NULL;
	if (ok) {
		char *path = xasprintf("%s/%s.xml", mime_dir(), mime);
		FILE *f = fopen(path, "re");
		free(path);
		if (f) {
			char head[8192];
			size_t n = fread(head, 1, sizeof head - 1, f);
			head[n] = '\0';
			fclose(f);
			desc = first_comment(head);
		}
	}

	/* No database entry is not an error — an unknown type is described by its
	 * own name, which is still better than an empty column. */
	if (!desc)
		desc = xstrdup(mime);

	g_descs = xrealloc(g_descs, (g_ndescs + 1) * sizeof *g_descs);
	g_descs[g_ndescs].mime = xstrdup(mime);
	g_descs[g_ndescs].desc = desc;
	return g_descs[g_ndescs++].desc;
}
