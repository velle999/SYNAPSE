/* ex.c — the colon commands, search, and substitute.
 *
 * ── Why POSIX regcomp and not a hand-written matcher ───────────────────────
 *
 * regcomp/regexec are in libc, which means real regular expressions with no
 * dependency at all — and a regex engine is precisely the kind of thing that
 * looks like a weekend's work and is still growing corner cases a year later.
 *
 * It is compiled as a BASIC regular expression by default, not an extended
 * one. That is not nostalgia: vim's default "magic" level is BRE-shaped — `*`
 * repeats, `(` is a literal bracket, `\(` groups, `\|` alternates, `\+` and
 * `\?` are GNU extensions that behave exactly as vim's do. Somebody who types
 * a vim pattern gets what they meant. `\v` at the start of a pattern switches
 * to ERE, which is the same escape hatch vim's very-magic mode is.
 *
 * ── Ranges ─────────────────────────────────────────────────────────────────
 *
 * Parsed in ONE place, by ex_range(), and every command takes the result.
 * The alternative — each command parsing its own leading numbers — is how
 * `:12,15d` works and `:12,15>` silently does something else.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "edit_internal.h"

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define B(e) ed_cur(e)

/* ── patterns ───────────────────────────────────────────────────────────── */

static bool has_upper(const char *s)
{
	for (const char *p = s; *p; p++)
		if (isupper((unsigned char)*p))
			return true;
	return false;
}

/* The pattern as the user typed it, minus a leading \v. Returns the flags. */
static int compile_pat(const ed_t *e, const char *pat, regex_t *re, char **err)
{
	int flags = 0;
	const char *p = pat;
	if (p[0] == '\\' && p[1] == 'v') {
		flags |= REG_EXTENDED;
		p += 2;
	}
	/* smartcase only applies when ignorecase is on — otherwise a pattern in
	 * lower case would start matching case-insensitively for people who never
	 * asked for it. */
	if (e->o.ignorecase && !(e->o.smartcase && has_upper(p)))
		flags |= REG_ICASE;

	int rc = regcomp(re, p, flags);
	if (rc != 0) {
		char buf[256];
		regerror(rc, re, buf, sizeof buf);
		if (err)
			*err = xasprintf("bad pattern: %s", buf);
		return -1;
	}
	return 0;
}

bool ed_regex_ok(const char *pat, char **err)
{
	regex_t re;
	int rc = regcomp(&re, pat, 0);
	if (rc != 0) {
		char buf[256];
		regerror(rc, &re, buf, sizeof buf);
		if (err)
			*err = xstrdup(buf);
		regfree(&re);
		return false;
	}
	regfree(&re);
	return true;
}

/* Match within one line starting at byte `from`. REG_NOTBOL matters: without
 * it, a pattern anchored with ^ matches at every offset the search restarts
 * from, so /^foo finds "foo" in the middle of a line. */
static bool line_match(const regex_t *re, const char *s, size_t from,
                       regmatch_t *m, size_t nm)
{
	int flags = from > 0 ? REG_NOTBOL : 0;
	if (regexec(re, s + from, nm, m, flags) != 0)
		return false;
	for (size_t i = 0; i < nm; i++) {
		if (m[i].rm_so >= 0) {
			m[i].rm_so += (regoff_t)from;
			m[i].rm_eo += (regoff_t)from;
		}
	}
	return true;
}

bool ed_search(ed_t *e, const char *pat, int dir, char **err)
{
	buf_t *b = B(e);
	regex_t re;
	if (compile_pat(e, pat, &re, err) != 0)
		return false;

	regmatch_t m[1];
	bool found = false;
	size_t fy = 0, fx = 0;

	if (dir > 0) {
		/* Forward from just past the cursor, then round the end of the file
		 * and back to the cursor. Wrapping is what makes n usable; stopping
		 * at the last line means every search ends with a manual gg. */
		for (size_t step = 0; step <= b->n; step++) {
			size_t y = (e->cy + step) % b->n;
			size_t from = (step == 0) ? e->cx + 1 : 0;
			if (from > buf_linelen(b, y))
				continue;
			if (line_match(&re, buf_line(b, y), from, m, 1)) {
				fy = y;
				fx = (size_t)m[0].rm_so;
				found = true;
				break;
			}
		}
	} else {
		for (size_t step = 0; step <= b->n; step++) {
			size_t y = (e->cy + b->n - (step % b->n)) % b->n;
			size_t limit = (step == 0) ? e->cx : (size_t)-1;
			/* Backwards means the LAST match on the line that still starts
			 * before the cursor, not the first — otherwise N walks forwards
			 * through a line with several matches. */
			size_t at = 0, best = (size_t)-1;
			while (at <= buf_linelen(b, y)
			       && line_match(&re, buf_line(b, y), at, m, 1)) {
				if (limit != (size_t)-1 && (size_t)m[0].rm_so >= limit)
					break;
				best = (size_t)m[0].rm_so;
				at = (m[0].rm_eo > m[0].rm_so) ? (size_t)m[0].rm_eo
				                               : (size_t)m[0].rm_so + 1;
			}
			if (best != (size_t)-1) {
				fy = y;
				fx = best;
				found = true;
				break;
			}
		}
	}

	regfree(&re);
	if (!found) {
		if (err)
			*err = xasprintf("pattern not found: %s", pat);
		return false;
	}

	/* A jump sets the '' mark, so `` returns to where the search started. */
	b->mark[26] = e->cy + 1;
	b->markx[26] = e->cx;
	e->cy = fy;
	e->cx = fx;
	ed_clamp(e);
	return true;
}

/* Builds the replacement for one match. & and \0 are the whole match, \1–\9
 * the groups, \r a line break, \\ a backslash. */
static char *expand_rep(const char *rep, const char *line, const regmatch_t *m,
                        size_t nm)
{
	size_t cap = strlen(rep) + 64, len = 0;
	char *out = xmalloc(cap);

	#define PUSH(p, n) do {                             \
		if (len + (n) + 1 > cap) {                      \
			while (len + (n) + 1 > cap) cap *= 2;       \
			out = xrealloc(out, cap);                   \
		}                                               \
		memcpy(out + len, (p), (n));                    \
		len += (n);                                     \
	} while (0)

	for (const char *p = rep; *p; p++) {
		int grp = -1;
		if (*p == '&') {
			grp = 0;
		} else if (*p == '\\' && p[1]) {
			p++;
			if (*p >= '0' && *p <= '9') {
				grp = *p - '0';
			} else if (*p == 'n' || *p == 'r') {
				char nl = '\n';
				PUSH(&nl, 1);
				continue;
			} else if (*p == 't') {
				char t = '\t';
				PUSH(&t, 1);
				continue;
			} else {
				/* \& is a literal ampersand, \\ a literal backslash, and
				 * anything else is the character itself — which is what makes
				 * \/ work inside a :s that used / as its delimiter. */
				PUSH(p, 1);
				continue;
			}
		} else {
			PUSH(p, 1);
			continue;
		}

		if (grp >= 0 && (size_t)grp < nm && m[grp].rm_so >= 0) {
			size_t n = (size_t)(m[grp].rm_eo - m[grp].rm_so);
			PUSH(line + m[grp].rm_so, n);
		}
	}
	#undef PUSH

	out[len] = '\0';
	return out;
}

long ed_substitute(ed_t *e, size_t y0, size_t y1, const char *pat,
                   const char *rep, bool global, bool icase, char **err)
{
	buf_t *b = B(e);
	regex_t re;

	/* An explicit I/i flag on the command overrides the option, so the
	 * compile cannot just call compile_pat. */
	int flags = 0;
	const char *p = pat;
	if (p[0] == '\\' && p[1] == 'v') {
		flags |= REG_EXTENDED;
		p += 2;
	}
	if (icase || (e->o.ignorecase && !(e->o.smartcase && has_upper(p))))
		flags |= REG_ICASE;
	int rc = regcomp(&re, p, flags);
	if (rc != 0) {
		char buf[256];
		regerror(rc, &re, buf, sizeof buf);
		if (err)
			*err = xasprintf("bad pattern: %s", buf);
		return -1;
	}

	long changed = 0;
	regmatch_t m[10];

	buf_group_begin(b);

	for (size_t y = y0; y <= y1 && y < b->n; y++) {
		const char *src = buf_line(b, y);
		size_t slen = buf_linelen(b, y);
		char *work = xstrndup(src, slen);
		size_t at = 0;
		bool any = false;

		for (;;) {
			if (at > strlen(work))
				break;
			if (!line_match(&re, work, at, m, 10))
				break;

			char *ins = expand_rep(rep, work, m, 10);
			size_t mstart = (size_t)m[0].rm_so;
			size_t mend = (size_t)m[0].rm_eo;
			size_t wlen = strlen(work);
			size_t ilen = strlen(ins);

			char *next = xmalloc(wlen - (mend - mstart) + ilen + 1);
			memcpy(next, work, mstart);
			memcpy(next + mstart, ins, ilen);
			memcpy(next + mstart + ilen, work + mend, wlen - mend);
			next[wlen - (mend - mstart) + ilen] = '\0';

			free(work);
			work = next;
			changed++;
			any = true;

			/* An empty match must still advance by one, or a pattern that can
			 * match nothing at all — anything ending in a star — never
			 * terminates: every position matches the empty string, forever.
			 * (Spelling the example out in a comment here is what closed this
			 * comment early the first time round.) */
			at = mstart + ilen + (mend == mstart ? 1 : 0);
			free(ins);

			if (!global)
				break;
		}

		if (any) {
			/* A replacement holding a newline splits the line, which is what
			 * makes :s/,/\r/g a usable way to break a list up. */
			if (strchr(work, '\n')) {
				size_t nseg = 1;
				for (char *q = work; *q; q++)
					if (*q == '\n')
						nseg++;
				char **segs = xmalloc(nseg * sizeof *segs);
				char *seg = work;
				for (size_t i = 0; i < nseg; i++) {
					char *nl = strchr(seg, '\n');
					segs[i] = xstrndup(seg, nl ? (size_t)(nl - seg) : strlen(seg));
					seg = nl ? nl + 1 : seg + strlen(seg);
				}
				buf_splice(b, y, 1, segs, nseg, e->cy, e->cx);
				for (size_t i = 0; i < nseg; i++)
					free(segs[i]);
				free(segs);
				y += nseg - 1;
				y1 += nseg - 1;
			} else {
				set_line(e, y, work);
			}
			e->cy = y;
		}
		free(work);
	}

	buf_group_end(b);
	regfree(&re);

	if (changed == 0) {
		if (err)
			*err = xasprintf("pattern not found: %s", pat);
		return 0;
	}
	ed_clamp(e);
	return changed;
}

/* ── ranges ─────────────────────────────────────────────────────────────── */

/* One address: a number, ., $, 'x, /pat/, or nothing. Returns true when one
 * was present and advances *pp. */
static bool ex_addr(ed_t *e, const char **pp, size_t *out)
{
	const buf_t *b = e->buf[e->cur];
	const char *p = *pp;
	size_t v;
	bool got = false;

	while (*p == ' ')
		p++;

	if (isdigit((unsigned char)*p)) {
		v = 0;
		while (isdigit((unsigned char)*p))
			v = v * 10 + (size_t)(*p++ - '0');
		if (v > 0)
			v--;                 /* ex counts from 1 */
		got = true;
	} else if (*p == '.') {
		v = e->cy;
		p++;
		got = true;
	} else if (*p == '$') {
		v = b->n - 1;
		p++;
		got = true;
	} else if (*p == '\'') {
		p++;
		int idx = -1;
		if (*p >= 'a' && *p <= 'z')
			idx = *p - 'a';
		else if (*p == '<')
			idx = 27;
		else if (*p == '>')
			idx = 28;
		else if (*p == '\'')
			idx = 26;
		if (idx < 0 || b->mark[idx] == 0)
			return false;
		v = b->mark[idx] - 1;
		p++;
		got = true;
	} else {
		v = e->cy;
	}

	/* +N / -N offsets, which is what makes .,+5 and 'a,+2 work. */
	while (*p == '+' || *p == '-') {
		int sign = (*p == '+') ? 1 : -1;
		p++;
		size_t n = 0;
		if (isdigit((unsigned char)*p)) {
			while (isdigit((unsigned char)*p))
				n = n * 10 + (size_t)(*p++ - '0');
		} else {
			n = 1;
		}
		if (sign > 0)
			v += n;
		else
			v = n > v ? 0 : v - n;
		got = true;
	}

	if (v >= b->n)
		v = b->n - 1;
	*out = v;
	*pp = p;
	return got;
}

/* Fills y0/y1 with the range and leaves *pp on the command name. */
static void ex_range(ed_t *e, const char **pp, size_t *y0, size_t *y1,
                     bool *had_range)
{
	const buf_t *b = e->buf[e->cur];
	const char *p = *pp;
	*had_range = false;
	*y0 = *y1 = e->cy;

	while (*p == ' ')
		p++;

	if (*p == '%') {
		*y0 = 0;
		*y1 = b->n - 1;
		*had_range = true;
		p++;
		*pp = p;
		return;
	}

	size_t a;
	if (ex_addr(e, &p, &a)) {
		*y0 = *y1 = a;
		*had_range = true;
	}
	while (*p == ' ')
		p++;
	if (*p == ',' || *p == ';') {
		p++;
		size_t c;
		if (ex_addr(e, &p, &c))
			*y1 = c;
		else
			*y1 = e->cy;
		*had_range = true;
	}
	if (*y0 > *y1) {
		size_t t = *y0;
		*y0 = *y1;
		*y1 = t;
	}
	*pp = p;
}

/* ── the command table ──────────────────────────────────────────────────── */

/* File scope rather than a nested function inside :sort — a nested one is a
 * GCC extension that builds a trampoline on the stack, and on a hardened
 * toolchain an executable stack is exactly what is not available. */
static int sort_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool word_is(const char *tok, const char *full, size_t minlen)
{
	size_t n = strlen(tok);
	if (n < minlen || n > strlen(full))
		return false;
	return strncmp(tok, full, n) == 0;
}

static void close_current(ed_t *e)
{
	if (e->nbuf <= 1) {
		e->quit = true;
		return;
	}
	buf_free(e->buf[e->cur]);
	for (size_t i = e->cur; i + 1 < e->nbuf; i++)
		e->buf[i] = e->buf[i + 1];
	e->nbuf--;
	if (e->cur >= e->nbuf)
		e->cur = e->nbuf - 1;
	e->cy = e->cx = 0;
	ed_clamp(e);
}

static bool any_modified(const ed_t *e)
{
	for (size_t i = 0; i < e->nbuf; i++)
		if (e->buf[i]->modified)
			return true;
	return false;
}

/* Splits ":s/a/b/g" on its delimiter, which is whatever follows the s. Using
 * the first character rather than assuming / is what lets :s#/usr#/opt# hold
 * a path without every slash being escaped. */
static bool split_subst(const char *p, char **pat, char **rep, char **flags)
{
	char delim = *p;
	if (!delim || isalnum((unsigned char)delim) || delim == ' '
	           || delim == '\\' || delim == '"' || delim == '|')
		return false;
	p++;

	const char *parts[2];
	size_t lens[2];
	for (int i = 0; i < 2; i++) {
		const char *start = p;
		while (*p && *p != delim) {
			if (*p == '\\' && p[1])
				p++;
			p++;
		}
		parts[i] = start;
		lens[i] = (size_t)(p - start);
		if (*p == delim)
			p++;
		else if (i == 0)
			return false;       /* :s/pat with no second delimiter */
	}

	*pat = xstrndup(parts[0], lens[0]);
	*rep = xstrndup(parts[1], lens[1]);
	*flags = xstrdup(p);
	return true;
}

bool ed_ex(ed_t *e, const char *line)
{
	buf_t *b = B(e);
	const char *p = line;
	while (*p == ' ' || *p == ':')
		p++;

	if (!*p)
		return true;

	size_t y0, y1;
	bool had_range;
	ex_range(e, &p, &y0, &y1, &had_range);
	while (*p == ' ')
		p++;

	/* A bare range is "go to that line", which is how :42 works. */
	if (!*p) {
		if (had_range) {
			e->cy = y1;
			e->cx = line_first_nonblank(b, e->cy);
			ed_clamp(e);
		}
		return true;
	}

	/* The command name, then a bang, then the argument. */
	char cmdbuf[32];
	size_t ci = 0;
	if (*p == '&' || *p == '<' || *p == '>' || *p == '=' || *p == '!') {
		cmdbuf[ci++] = *p++;
		while (ci < sizeof cmdbuf - 1 && *p == cmdbuf[0])
			cmdbuf[ci++] = *p++;
	} else {
		while (ci < sizeof cmdbuf - 1 && isalpha((unsigned char)*p))
			cmdbuf[ci++] = *p++;
	}
	cmdbuf[ci] = '\0';

	bool bang = false;
	if (*p == '!') {
		bang = true;
		p++;
	}
	while (*p == ' ')
		p++;
	const char *arg = p;

	/* ── writing and quitting ─────────────────────────────────────────── */

	if (!strcmp(cmdbuf, "w") || !strcmp(cmdbuf, "write")
	 || !strcmp(cmdbuf, "wq") || !strcmp(cmdbuf, "x") || !strcmp(cmdbuf, "xit")
	 || !strcmp(cmdbuf, "wa") || !strcmp(cmdbuf, "wall")
	 || !strcmp(cmdbuf, "wqa") || !strcmp(cmdbuf, "xa")) {
		bool all = (cmdbuf[1] == 'a' || (cmdbuf[0] == 'w' && cmdbuf[1] == 'q'
		            && cmdbuf[2] == 'a') || !strcmp(cmdbuf, "xa"));
		bool quit = (!strcmp(cmdbuf, "wq") || !strcmp(cmdbuf, "x")
		          || !strcmp(cmdbuf, "xit") || !strcmp(cmdbuf, "wqa")
		          || !strcmp(cmdbuf, "xa"));

		if (b->readonly && !bang && !all) {
			ed_message(e, true, "%s: read-only (use :w! to override)",
			           buf_name(b));
			return false;
		}

		char *err = NULL;
		if (all) {
			size_t n = 0;
			for (size_t i = 0; i < e->nbuf; i++) {
				if (!e->buf[i]->modified || !e->buf[i]->path)
					continue;
				if (!buf_save(e->buf[i], NULL, &err)) {
					ed_message(e, true, "%s", err ? err : "write failed");
					free(err);
					return false;
				}
				n++;
			}
			ed_message(e, false, "%zu file%s written", n, n == 1 ? "" : "s");
		} else {
			const char *to = *arg ? arg : NULL;
			if (!to && !b->path) {
				ed_message(e, true, "no file name (use :w <name>)");
				return false;
			}
			if (!buf_save(b, to, &err)) {
				ed_message(e, true, "%s", err ? err : "write failed");
				free(err);
				return false;
			}
			ed_message(e, false, "\"%s\" %zu lines written", buf_name(b), b->n);
		}
		if (quit) {
			if (all)
				e->quit = true;
			else
				close_current(e);
		}
		return true;
	}

	if (!strcmp(cmdbuf, "q") || !strcmp(cmdbuf, "quit")
	 || !strcmp(cmdbuf, "qa") || !strcmp(cmdbuf, "qall")) {
		bool all = (cmdbuf[1] == 'a');
		if (!bang) {
			/* Refusing NAMES the file. "No write since last change" without
			 * saying which of nine buffers is a message that costs a hunt. */
			if (all && any_modified(e)) {
				for (size_t i = 0; i < e->nbuf; i++)
					if (e->buf[i]->modified) {
						ed_message(e, true,
						           "%s has unsaved changes (:qa! to discard)",
						           buf_name(e->buf[i]));
						return false;
					}
			} else if (!all && b->modified) {
				ed_message(e, true,
				           "%s has unsaved changes (:q! to discard)",
				           buf_name(b));
				return false;
			}
		}
		if (all)
			e->quit = true;
		else
			close_current(e);
		return true;
	}

	/* ── buffers and files ────────────────────────────────────────────── */

	if (!strcmp(cmdbuf, "e") || !strcmp(cmdbuf, "edit")) {
		if (!*arg) {
			if (!b->path) {
				ed_message(e, true, "no file name");
				return false;
			}
			if (b->modified && !bang) {
				ed_message(e, true, "unsaved changes (:e! to discard)");
				return false;
			}
			char *err = NULL;
			char *path = xstrdup(b->path);
			bool ok = buf_load(b, path, &err);
			free(path);
			if (!ok) {
				ed_message(e, true, "%s", err ? err : "read failed");
				free(err);
				return false;
			}
			e->cy = e->cx = 0;
			ed_message(e, false, "reloaded \"%s\"", buf_name(b));
			return true;
		}
		char *err = NULL;
		if (ed_open(e, arg, &err) < 0) {
			ed_message(e, true, "%s", err ? err : "could not open");
			free(err);
			return false;
		}
		free(e->want_open);
		e->want_open = xstrdup(arg);
		ed_message(e, false, "\"%s\" %zu lines", buf_name(B(e)), B(e)->n);
		return true;
	}

	if (word_is(cmdbuf, "bnext", 2) || !strcmp(cmdbuf, "bn")) {
		e->cur = (e->cur + 1) % e->nbuf;
		e->cy = e->cx = 0;
		ed_clamp(e);
		return true;
	}
	if (word_is(cmdbuf, "bprevious", 2) || !strcmp(cmdbuf, "bp")) {
		e->cur = (e->cur + e->nbuf - 1) % e->nbuf;
		e->cy = e->cx = 0;
		ed_clamp(e);
		return true;
	}
	if (!strcmp(cmdbuf, "bd") || word_is(cmdbuf, "bdelete", 2)) {
		if (b->modified && !bang) {
			ed_message(e, true, "unsaved changes (:bd! to discard)");
			return false;
		}
		close_current(e);
		return true;
	}
	if (!strcmp(cmdbuf, "b") || !strcmp(cmdbuf, "buffer")) {
		if (isdigit((unsigned char)*arg)) {
			size_t i = (size_t)atol(arg);
			if (i == 0 || i > e->nbuf) {
				ed_message(e, true, "no buffer %s", arg);
				return false;
			}
			e->cur = i - 1;
			e->cy = e->cx = 0;
			ed_clamp(e);
			return true;
		}
		/* By name: the first buffer whose path CONTAINS the argument, which
		 * is what makes :b main enough when the path is long. */
		for (size_t i = 0; i < e->nbuf; i++) {
			if (e->buf[i]->path && strstr(e->buf[i]->path, arg)) {
				e->cur = i;
				e->cy = e->cx = 0;
				ed_clamp(e);
				return true;
			}
		}
		ed_message(e, true, "no buffer matching %s", arg);
		return false;
	}
	if (!strcmp(cmdbuf, "ls") || word_is(cmdbuf, "buffers", 3)) {
		char out[1024];
		size_t w = 0;
		for (size_t i = 0; i < e->nbuf && w < sizeof out - 64; i++) {
			w += (size_t)snprintf(out + w, sizeof out - w, "%s%zu %s%s  ",
			                      i == e->cur ? ">" : " ", i + 1,
			                      buf_name(e->buf[i]),
			                      e->buf[i]->modified ? " [+]" : "");
		}
		ed_message(e, false, "%s", out);
		return true;
	}
	if (!strcmp(cmdbuf, "enew")) {
		if (e->nbuf >= MAXBUF) {
			ed_message(e, true, "too many open files");
			return false;
		}
		e->buf[e->nbuf++] = buf_new();
		e->cur = e->nbuf - 1;
		e->cy = e->cx = 0;
		return true;
	}
	if (!strcmp(cmdbuf, "r") || !strcmp(cmdbuf, "read")) {
		if (!*arg) {
			ed_message(e, true, "read: needs a file name");
			return false;
		}
		buf_t *tmp = buf_new();
		char *err = NULL;
		if (!buf_load(tmp, arg, &err)) {
			ed_message(e, true, "%s", err ? err : "read failed");
			free(err);
			buf_free(tmp);
			return false;
		}
		char **lines = xmalloc(tmp->n * sizeof *lines);
		for (size_t i = 0; i < tmp->n; i++)
			lines[i] = (char *)buf_line(tmp, i);
		buf_group_begin(b);
		buf_splice(b, y1 + 1, 0, lines, tmp->n, e->cy, e->cx);
		buf_group_end(b);
		free(lines);
		ed_message(e, false, "\"%s\" %zu lines", arg, tmp->n);
		buf_free(tmp);
		return true;
	}

	/* ── editing ──────────────────────────────────────────────────────── */

	if (!strcmp(cmdbuf, "s") || word_is(cmdbuf, "substitute", 1)) {
		char *pat = NULL, *rep = NULL, *flags = NULL;
		if (!split_subst(p, &pat, &rep, &flags)) {
			ed_message(e, true, "usage: :[range]s/pattern/replacement/[g]");
			return false;
		}
		/* An empty pattern reuses the last search, which is what makes
		 * /foo<CR> then :%s//bar/g the ordinary way to work. */
		if (!*pat) {
			free(pat);
			if (!e->search || !*e->search) {
				free(rep);
				free(flags);
				ed_message(e, true, "no previous pattern");
				return false;
			}
			pat = xstrdup(e->search);
		} else {
			free(e->search);
			e->search = xstrdup(pat);
			reg_set(e, '/', pat, false);
		}

		bool global = strchr(flags, 'g') != NULL;
		bool icase = strchr(flags, 'i') != NULL;
		char *err = NULL;
		long n = ed_substitute(e, y0, y1, pat, rep, global, icase, &err);
		if (n <= 0) {
			ed_message(e, true, "%s", err ? err : "pattern not found");
			free(err);
		} else {
			ed_message(e, false, "%ld substitution%s", n, n == 1 ? "" : "s");
		}
		free(pat);
		free(rep);
		free(flags);
		return n > 0;
	}

	/* :g/pat/cmd and :v/pat/cmd — run an ex command on every matching line.
	 *
	 * Executed from the LAST match backwards. Forwards, the first :d shifts
	 * every line number after it and the rest of the list points at the wrong
	 * lines — the classic way a :g/…/d deletes a scattering of innocent
	 * lines. Backwards, the numbers still ahead are untouched. */
	if (!strcmp(cmdbuf, "g") || !strcmp(cmdbuf, "global")
	 || !strcmp(cmdbuf, "v") || !strcmp(cmdbuf, "vglobal")) {
		bool invert = (cmdbuf[0] == 'v');
		char delim = *p;
		if (!delim || isalnum((unsigned char)delim)) {
			ed_message(e, true, "usage: :[range]g/pattern/command");
			return false;
		}
		p++;
		const char *ps = p;
		while (*p && *p != delim) {
			if (*p == '\\' && p[1])
				p++;
			p++;
		}
		char *pat = xstrndup(ps, (size_t)(p - ps));
		if (*p == delim)
			p++;
		const char *sub = *p ? p : "d";

		if (!had_range) {
			y0 = 0;
			y1 = b->n - 1;
		}

		regex_t re;
		char *err = NULL;
		if (compile_pat(e, pat, &re, &err) != 0) {
			ed_message(e, true, "%s", err ? err : "bad pattern");
			free(err);
			free(pat);
			return false;
		}

		size_t *hits = xmalloc((y1 - y0 + 1) * sizeof *hits);
		size_t nh = 0;
		for (size_t y = y0; y <= y1 && y < b->n; y++) {
			bool m = regexec(&re, buf_line(b, y), 0, NULL, 0) == 0;
			if (m != invert)
				hits[nh++] = y;
		}
		regfree(&re);
		free(pat);

		char *subcmd = xstrdup(sub);
		buf_group_begin(b);
		for (size_t i = nh; i > 0; i--) {
			if (hits[i - 1] >= b->n)
				continue;
			e->cy = hits[i - 1];
			e->cx = 0;
			ed_ex(e, subcmd);
		}
		buf_group_end(b);
		free(subcmd);
		free(hits);
		ed_message(e, false, "%zu line%s", nh, nh == 1 ? "" : "s");
		ed_clamp(e);
		return true;
	}

	if (!strcmp(cmdbuf, "d") || word_is(cmdbuf, "delete", 1)) {
		buf_group_begin(b);
		char *text = range_text(e, y0, 0, y1, 0, true);
		reg_set(e, *arg ? *arg : '"', text, true);
		free(text);
		range_delete(e, y0, 0, y1, 0, true);
		buf_group_end(b);
		e->cy = y0 < b->n ? y0 : b->n - 1;
		e->cx = line_first_nonblank(b, e->cy);
		ed_clamp(e);
		return true;
	}

	if (!strcmp(cmdbuf, "y") || word_is(cmdbuf, "yank", 1)) {
		char *text = range_text(e, y0, 0, y1, 0, true);
		reg_set(e, *arg ? *arg : '"', text, true);
		free(text);
		ed_message(e, false, "%zu lines yanked", y1 - y0 + 1);
		return true;
	}

	if (!strcmp(cmdbuf, "m") || word_is(cmdbuf, "move", 1)
	 || !strcmp(cmdbuf, "t") || !strcmp(cmdbuf, "co")
	 || word_is(cmdbuf, "copy", 2)) {
		bool copy = (cmdbuf[0] == 't' || cmdbuf[0] == 'c');
		const char *ap = arg;
		size_t dest;
		bool got = ex_addr(e, &ap, &dest);
		if (!got && *arg != '0') {
			ed_message(e, true, "%s: needs a destination line", cmdbuf);
			return false;
		}
		/* :m0 means "to the very top", and address 0 is the only one that is
		 * not a line — it is the gap before line one. */
		bool to_top = (*arg == '0' && !isdigit((unsigned char)arg[1]));

		size_t n = y1 - y0 + 1;
		char **lines = xmalloc(n * sizeof *lines);
		for (size_t i = 0; i < n; i++)
			lines[i] = xstrdup(buf_line(b, y0 + i));

		buf_group_begin(b);
		size_t at = to_top ? 0 : dest + 1;
		if (!copy) {
			/* Remove first, then correct the destination for the hole that
			 * leaves — inserting first would need the same correction in the
			 * other direction and one of the two always gets forgotten. */
			buf_splice(b, y0, n, NULL, 0, e->cy, e->cx);
			if (at > y0)
				at = at > n ? at - n : 0;
		}
		buf_splice(b, at, 0, lines, n, e->cy, e->cx);
		buf_group_end(b);

		for (size_t i = 0; i < n; i++)
			free(lines[i]);
		free(lines);
		e->cy = at + n - 1;
		ed_clamp(e);
		return true;
	}

	if (!strcmp(cmdbuf, ">") || !strcmp(cmdbuf, ">>")
	 || !strcmp(cmdbuf, "<") || !strcmp(cmdbuf, "<<")) {
		int dir = cmdbuf[0] == '>' ? 1 : -1;
		size_t times = strlen(cmdbuf);
		buf_group_begin(b);
		for (size_t t = 0; t < times; t++) {
			for (size_t y = y0; y <= y1 && y < b->n; y++) {
				/* shift_lines is static in vim.c; the same effect through the
				 * public path keeps one implementation of the indent rules. */
				size_t save_y = e->cy, save_x = e->cx;
				e->cy = y;
				ed_keys(e, dir > 0 ? ">>" : "<<");
				e->cy = save_y;
				e->cx = save_x;
			}
		}
		buf_group_end(b);
		ed_clamp(e);
		return true;
	}

	if (!strcmp(cmdbuf, "j") || word_is(cmdbuf, "join", 1)) {
		buf_group_begin(b);
		e->cy = y0;
		size_t count = y1 > y0 ? y1 - y0 + 1 : 2;
		char keys[32];
		snprintf(keys, sizeof keys, "%zuJ", count);
		ed_keys(e, keys);
		buf_group_end(b);
		return true;
	}

	if (word_is(cmdbuf, "normal", 4)) {
		/* :normal takes the rest of the line VERBATIM — including spaces,
		 * which is why arg is used rather than a re-tokenised copy.
		 *
		 * ⚠ An INCOMPLETE command is terminated after each line, exactly as
		 * vim does. `:%normal A!` ends its first line still in insert mode,
		 * and without this the second line receives "A!" as literal text
		 * instead of as a command — so line one gets an exclamation mark and
		 * every line after it gets the keystrokes spelled out. */
		if (had_range) {
			for (size_t y = y0; y <= y1 && y < b->n; y++) {
				e->cy = y;
				e->cx = 0;
				ed_keys(e, arg);
				if (e->mode != M_NORMAL)
					ed_key(e, K_ESC);
			}
		} else {
			ed_keys(e, arg);
			if (e->mode != M_NORMAL)
				ed_key(e, K_ESC);
		}
		ed_clamp(e);
		return true;
	}

	/* ── options and odds and ends ────────────────────────────────────── */

	if (!strcmp(cmdbuf, "set") || !strcmp(cmdbuf, "se")) {
		if (!*arg) {
			char out[1024];
			size_t w = 0;
			for (size_t i = 0; i < opts_count() && w < sizeof out - 48; i++) {
				char val[64];
				const char *k = opts_key_at(i);
				opts_get(&e->o, k, val, sizeof val);
				w += (size_t)snprintf(out + w, sizeof out - w, "%s=%s  ", k, val);
			}
			ed_message(e, false, "%s", out);
			return true;
		}
		/* Each of "number", "nonumber", "number!" and "tabstop=4" is a
		 * separate spelling of the same thing; they are normalised HERE so
		 * that opts_set only ever sees a key and a value. */
		char key[64], val[128];
		const char *eq = strchr(arg, '=');
		bool ok = true;
		char *err = NULL;
		if (eq) {
			size_t kl = (size_t)(eq - arg);
			if (kl >= sizeof key)
				kl = sizeof key - 1;
			memcpy(key, arg, kl);
			key[kl] = '\0';
			snprintf(val, sizeof val, "%s", eq + 1);
		} else {
			snprintf(key, sizeof key, "%s", arg);
			char *bangp = strchr(key, '!');
			bool toggle = bangp != NULL;
			if (toggle)
				*bangp = '\0';
			bool off = false;
			if (strncmp(key, "no", 2) == 0 && opts_get(&e->o, key + 2, val, sizeof val)) {
				memmove(key, key + 2, strlen(key + 2) + 1);
				off = true;
			}
			char cur[64];
			if (!opts_get(&e->o, key, cur, sizeof cur)) {
				ed_message(e, true, "unknown option: %s", arg);
				return false;
			}
			if (toggle)
				snprintf(val, sizeof val, "%s",
				         strcmp(cur, "true") == 0 ? "false" : "true");
			else
				snprintf(val, sizeof val, "%s", off ? "false" : "true");
		}
		ok = opts_set(&e->o, key, val, &err);
		if (!ok) {
			ed_message(e, true, "%s", err ? err : "bad option");
			free(err);
			return false;
		}
		char now[64];
		opts_get(&e->o, key, now, sizeof now);
		ed_message(e, false, "%s=%s", key, now);
		return true;
	}

	if (word_is(cmdbuf, "nohlsearch", 3)) {
		/* Clearing the message IS the effect: the front-ends stop drawing
		 * search highlighting when there is nothing to report. */
		e->msg[0] = '\0';
		e->msg_err = false;
		return true;
	}

	if (!strcmp(cmdbuf, "sort")) {
		/* Ordinary byte order, with -u for unique. Locale-aware collation
		 * would make the result depend on who ran it. */
		size_t n = y1 - y0 + 1;
		char **lines = xmalloc(n * sizeof *lines);
		for (size_t i = 0; i < n; i++)
			lines[i] = xstrdup(buf_line(b, y0 + i));
		qsort(lines, n, sizeof *lines, sort_cmp);
		size_t out = n;
		if (strstr(arg, "u")) {
			out = 0;
			for (size_t i = 0; i < n; i++) {
				if (i == 0 || strcmp(lines[i], lines[out - 1]) != 0)
					lines[out++] = lines[i];
				else
					free(lines[i]);
			}
		}
		buf_group_begin(b);
		buf_splice(b, y0, n, lines, out, e->cy, e->cx);
		buf_group_end(b);
		for (size_t i = 0; i < out; i++)
			free(lines[i]);
		free(lines);
		ed_clamp(e);
		return true;
	}

	if (!strcmp(cmdbuf, "noh"))
		return true;

	if (word_is(cmdbuf, "version", 3) || !strcmp(cmdbuf, "about")) {
		ed_message(e, false, "syn-edit — the SynapseOS editor");
		return true;
	}

	ed_message(e, true, "not an editor command: %s", cmdbuf);
	return false;
}
