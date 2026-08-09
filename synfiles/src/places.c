/* places.c — pinned folders, from ~/.local/share/user-places.xbel.
 *
 * That is Dolphin's file, and using it rather than inventing one is the whole
 * point: an evaluation build that starts with an EMPTY sidebar looks broken
 * even when it is working, and two file managers keeping two bookmark lists
 * would diverge the first time either one was used.
 *
 * XBEL hrefs are URIs, so the paths in this file arrive ALREADY
 * percent-encoded — the same encoding this program's own record format uses.
 * A path therefore travels from the bookmark file to the GUI without ever
 * being decoded, which is exactly the property that keeps a folder called
 * "Receipts 2024 (final)" from losing its identity somewhere in the middle.
 *
 * Writes are textual surgery on the existing file rather than parse-and-
 * regenerate. KDE stores per-bookmark state in there — IDs, isSystemItem,
 * isHidden, group visibility — and a regenerating writer would drop every
 * field it did not personally understand. Splicing one <bookmark> element in
 * or out leaves the other 95% of the file byte-identical.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* realpath(p, NULL) mallocs a buffer of the right size. The PATH_MAX form
 * cannot express a path longer than PATH_MAX, and deep trees reach that by
 * nesting even when every component is short. Callers free the result. */
static char *resolve(const char *path)
{
	char *real = realpath(path, NULL);
	if (!real)
		die("cannot resolve %s: %s", path, strerror(errno));
	return real;
}

static char *places_path(void)
{
	const char *env = getenv("SYNFILES_PLACES");
	if (env && *env)
		return xstrdup(env);
	char *data = xdg_data_home();
	char *p = xasprintf("%s/user-places.xbel", data);
	free(data);
	return p;
}

/* ── minimal XBEL scanning ──────────────────────────────────────────────── */

static const char *find_in(const char *p, const char *end, const char *needle)
{
	if (!p || p >= end)
		return NULL;
	return memmem(p, (size_t)(end - p), needle, strlen(needle));
}

/* Value of attr="..." within [p, end). */
static char *attr_val(const char *p, const char *end, const char *attr)
{
	char *pat = xasprintf("%s=\"", attr);
	const char *a = find_in(p, end, pat);
	free(pat);
	if (!a)
		return NULL;
	a += strlen(attr) + 2;
	const char *q = memchr(a, '"', (size_t)(end - a));
	return q ? xstrndup(a, (size_t)(q - a)) : NULL;
}

static char *tag_text(const char *p, const char *end, const char *tag)
{
	char *open = xasprintf("<%s>", tag);
	char *close = xasprintf("</%s>", tag);
	char *out = NULL;

	const char *a = find_in(p, end, open);
	if (a) {
		a += strlen(open);
		const char *b = find_in(a, end, close);
		if (b)
			out = xstrndup(a, (size_t)(b - a));
	}
	free(open);
	free(close);
	return out;
}

/* The five predefined XML entities. Titles are the only place these show up in
 * practice ("Tom &amp; Jerry"), and a title is display text, so an unknown
 * entity is passed through rather than guessed at. */
static void xml_unescape(char *s)
{
	static const struct { const char *ent; char ch; } ents[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
		{ "&quot;", '"' }, { "&apos;", '\'' },
	};
	char *w = s;
	for (const char *r = s; *r; ) {
		if (*r != '&') { *w++ = *r++; continue; }
		bool hit = false;
		for (size_t i = 0; i < sizeof ents / sizeof *ents && !hit; i++) {
			size_t n = strlen(ents[i].ent);
			if (!strncmp(r, ents[i].ent, n)) { *w++ = ents[i].ch; r += n; hit = true; }
		}
		if (!hit) *w++ = *r++;
	}
	*w = '\0';
}

static char *xml_escape(const char *s)
{
	size_t cap = strlen(s) * 6 + 1, n = 0;
	char *out = xmalloc(cap);
	for (const char *r = s; *r; r++) {
		const char *rep = NULL;
		switch (*r) {
		case '&': rep = "&amp;";  break;
		case '<': rep = "&lt;";   break;
		case '>': rep = "&gt;";   break;
		case '"': rep = "&quot;"; break;
		case '\'': rep = "&apos;"; break;
		default: break;
		}
		if (rep) { size_t l = strlen(rep); memcpy(out + n, rep, l); n += l; }
		else out[n++] = *r;
	}
	out[n] = '\0';
	return out;
}

/* ── listing ────────────────────────────────────────────────────────────── */

/* A place is not always a directory. KDE stores remote:/, trash:/, timeline:/
 * and search:/ in the same file, and a front-end that assumed every href was a
 * path would try to chdir into "trash:/". The scheme travels as its own column
 * so the GUI can route each kind to whatever handles it. */
static const char *kind_of_href(const char *href)
{
	if (!strncmp(href, "file://", 7))     return "path";
	if (!strncmp(href, "remote:", 7))     return "remote";
	if (!strncmp(href, "trash:", 6))      return "trash";
	if (!strncmp(href, "timeline:", 9))   return "timeline";
	if (!strncmp(href, "search:", 7))     return "search";
	if (!strncmp(href, "recentlyused:", 13)) return "recent";
	return "other";
}

static int places_list(void)
{
	char *path = places_path();
	char *text = slurp(path);
	if (!text) {
		/* Not an error: a user who has never opened a file manager has no
		 * bookmark file, and the right answer is an empty sidebar with the
		 * defaults the front-end supplies. */
		if (g_out == OUT_REC)
			rec_row(6, "href", "kind", "title", "icon", "hidden", "system");
		else
			printf("%sno places file at %s%s\n", C_DIM(), path, C_RESET());
		free(path);
		return 100;
	}
	free(path);

	if (g_out == OUT_REC)
		rec_row(6, "href", "kind", "title", "icon", "hidden", "system");

	int n = 0;
	const char *end = text + strlen(text);
	for (const char *p = text; (p = find_in(p, end, "<bookmark ")); ) {
		const char *b_end = find_in(p, end, "</bookmark>");
		if (!b_end)
			break;

		char *href = attr_val(p, b_end, "href");

		/* Two entries in a real user-places.xbel that must not reach a
		 * sidebar, both found in velle's on 2026-08-09:
		 *
		 *   - href="" — an empty bookmark. Rendering it gives a clickable row
		 *     that navigates nowhere.
		 *   - <OnlyInApp>kdenlive</OnlyInApp> — KDE scopes some places to a
		 *     single application. kdenlive's "Project Folder" is meaningless
		 *     in a general file browser, and honouring the field is also what
		 *     stops synfiles putting ITS entries in Dolphin's sidebar later. */
		bool foreign = find_in(p, b_end, "<OnlyInApp>") != NULL;
		if (href && (!*href || foreign)) {
			free(href);
			href = NULL;
		}

		if (href) {
			char *title = tag_text(p, b_end, "title");
			if (title)
				xml_unescape(title);

			/* <bookmark:icon name="..."/> — the attribute is on a self-closing
			 * element, so attr_val over the whole bookmark finds it. */
			const char *ic = find_in(p, b_end, "<bookmark:icon");
			char *icon = ic ? attr_val(ic, b_end, "name") : NULL;

			bool hidden = find_in(p, b_end, "<isHidden>true</isHidden>") != NULL;
			bool system = find_in(p, b_end, "<isSystemItem>true</isSystemItem>") != NULL;

			/* href is already a URI and therefore already percent-encoded.
			 * Re-encoding it here would double every escape and turn
			 * "%20" into "%2520". Strip the scheme, keep the encoding. */
			const char *kind = kind_of_href(href);
			const char *val = !strcmp(kind, "path") ? href + 7 : href;

			if (g_out == OUT_REC) {
				rec_row(6, val, kind, title ? title : "", icon ? icon : "",
				        hidden ? "1" : "0", system ? "1" : "0");
			} else if (!hidden) {
				char *shown = pct_decode(val);
				printf("%s%-24s%s %s%s%s\n", C_ACCENT(),
				       title ? title : shown, C_RESET(), C_DIM(), shown, C_RESET());
				free(shown);
			}
			n++;

			free(title);
			free(icon);
			free(href);
		}
		p = b_end + strlen("</bookmark>");
	}

	free(text);
	return n ? 0 : 100;
}

/* ── pin / unpin ────────────────────────────────────────────────────────── */

/* Write via a temporary file in the same directory and rename() over the
 * target. rename() within a filesystem is atomic, so a reader — Dolphin, or
 * another copy of this program — sees either the old file or the new one and
 * never a half-written one. Writing in place is how a bookmark list gets
 * truncated to zero bytes by an unlucky crash. */
static int write_atomic(const char *path, const char *text)
{
	char *tmp = xasprintf("%s.synfiles-XXXXXX", path);
	int fd = mkstemp(tmp);
	if (fd < 0) {
		warn("cannot create a temporary file beside %s: %s", path, strerror(errno));
		free(tmp);
		return 1;
	}

	size_t len = strlen(text);
	ssize_t w = write(fd, text, len);
	if (w < 0 || (size_t)w != len || fsync(fd) != 0) {
		warn("cannot write %s: %s", tmp, strerror(errno));
		close(fd);
		unlink(tmp);
		free(tmp);
		return 1;
	}
	close(fd);

	/* mkstemp makes it 0600; the bookmark file is not a secret and Dolphin
	 * ships it 0644. Match, or the next tool to read it may not be able to. */
	chmod(tmp, 0644);

	if (rename(tmp, path) != 0) {
		warn("cannot replace %s: %s", path, strerror(errno));
		unlink(tmp);
		free(tmp);
		return 1;
	}

	free(tmp);
	return 0;
}

/* One backup, ever, before this program first modifies a file it did not
 * create. Dolphin's bookmark list is user data that predates synfiles, and
 * while synfiles is being evaluated it is worth being able to put it back. */
static void backup_once(const char *path)
{
	char *bak = xasprintf("%s.pre-synfiles", path);

	/* O_EXCL IS the "once, ever" test. Asking access() whether the backup
	 * exists and then opening it is two answers to one question, and the file
	 * can appear — or be replaced with a link to something else — in between;
	 * the open would then truncate whatever the link pointed at. Create-or-
	 * fail says it in one call. */
	int fd = open(bak, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd >= 0) {
		char *text = slurp(path);
		if (text) {
			size_t len = strlen(text);
			if (write(fd, text, len) != (ssize_t)len)
				warn("could not write %s", bak);
			free(text);
		}
		close(fd);
	}
	free(bak);
}

/* The href to match on. Takes a real path with real bytes and produces the
 * URI form the file stores. */
static char *href_for(const char *path)
{
	char *enc = pct_encode(path, true);
	char *href = xasprintf("file://%s", enc);
	free(enc);
	return href;
}

static int places_pin(const char *path, const char *title)
{
	char *real = resolve(path);
	char *file = places_path();
	char *text = slurp(file);
	if (!text) {
		/* Bootstrapping an empty file is fine — the schema is fixed and this
		 * is the same skeleton KDE writes. */
		text = xstrdup("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		               "<!DOCTYPE xbel>\n"
		               "<xbel xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\""
		               " xmlns:kdepriv=\"http://www.kde.org/kdepriv\""
		               " xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
		               "</xbel>\n");
	} else {
		backup_once(file);
	}

	char *href = href_for(real);
	if (strstr(text, href)) {
		warn("%s is already pinned", real);
		free(href); free(text); free(file); free(real);
		return 0;
	}

	const char *base = strrchr(real, '/');
	const char *name = title ? title : (base && base[1] ? base + 1 : real);
	char *etitle = xml_escape(name);

	char *block = xasprintf(
		" <bookmark href=\"%s\">\n"
		"  <title>%s</title>\n"
		"  <info>\n"
		"   <metadata owner=\"http://freedesktop.org\">\n"
		"    <bookmark:icon name=\"folder\"/>\n"
		"   </metadata>\n"
		"  </info>\n"
		" </bookmark>\n", href, etitle);

	char *close = strstr(text, "</xbel>");
	if (!close) {
		warn("%s has no </xbel> — refusing to guess where a bookmark goes", file);
		free(block); free(etitle); free(href); free(text); free(file); free(real);
		return 1;
	}

	size_t head = (size_t)(close - text);
	char *out = xmalloc(strlen(text) + strlen(block) + 1);
	memcpy(out, text, head);
	strcpy(out + head, block);
	strcat(out, close);

	int rc = write_atomic(file, out);
	if (rc == 0 && g_out == OUT_HUMAN)
		printf("pinned %s\n", real);

	free(out); free(block); free(etitle); free(href); free(text); free(file); free(real);
	return rc;
}

static int places_unpin(const char *path)
{
	char *real = resolve(path);
	char *file = places_path();
	char *text = slurp(file);
	if (!text) {
		warn("no places file to remove from");
		free(real);
		free(file);
		return 1;
	}
	backup_once(file);

	char *href = href_for(real);

	/* Find the <bookmark ...> whose href matches, then cut from the start of
	 * that element to the end of its </bookmark>. Searching for the href
	 * first and walking BACKWARDS to the element start is what keeps this
	 * from cutting the wrong bookmark when one path is a prefix of another. */
	int rc = 1;
	char *hit = strstr(text, href);
	while (hit) {
		/* Guard against /home/a matching inside /home/abc: the href must be
		 * followed by the closing quote of the attribute. */
		if (hit[strlen(href)] != '"') {
			hit = strstr(hit + 1, href);
			continue;
		}

		char *start = hit;
		while (start > text && strncmp(start, "<bookmark ", 10))
			start--;
		char *stop = strstr(hit, "</bookmark>");
		if (strncmp(start, "<bookmark ", 10) || !stop) {
			warn("could not find the bookmark element around %s", real);
			break;
		}
		stop += strlen("</bookmark>");
		while (*stop == '\n')
			stop++;

		/* Take the indentation on the line the element starts on with it,
		 * or every removal leaves a ragged blank-ish line behind. */
		while (start > text && (start[-1] == ' ' || start[-1] == '\t'))
			start--;

		size_t head = (size_t)(start - text);
		char *out = xmalloc(strlen(text) + 1);
		memcpy(out, text, head);
		strcpy(out + head, stop);

		rc = write_atomic(file, out);
		if (rc == 0 && g_out == OUT_HUMAN)
			printf("unpinned %s\n", real);
		free(out);
		break;
	}

	if (rc != 0 && !hit)
		warn("%s is not pinned", real);

	free(href); free(text); free(file); free(real);
	return rc;
}

int cmd_places(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "list";

	if (!strcmp(sub, "list"))
		return places_list();
	if (!strcmp(sub, "pin")) {
		if (argc < 2)
			die("places pin: need a path");
		return places_pin(argv[1], argc >= 3 ? argv[2] : NULL);
	}
	if (!strcmp(sub, "unpin")) {
		if (argc < 2)
			die("places unpin: need a path");
		return places_unpin(argv[1]);
	}

	die("places: unknown subcommand '%s' — try list, pin <path>, unpin <path>", sub);
}
