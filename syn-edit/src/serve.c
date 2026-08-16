/* serve.c — the engine as a long-lived process the window talks to.
 *
 * ── Why the GUI is not its own editor ──────────────────────────────────────
 *
 * The obvious way to build a graphical editor on quickshell is to put a
 * TextEdit on screen and let Qt own the text. That would have been a second
 * editor: a second idea of what a word is, a second undo stack, and no modal
 * editing at all unless the whole vim layer were written again in QML.
 *
 * So the window owns no text. It sends keys and draws what comes back. `hjkl`,
 * `ciw`, `:%s/…/…/g`, undo, registers and macros all work in the window
 * because they are not implemented in the window.
 *
 * ── The protocol ───────────────────────────────────────────────────────────
 *
 * Commands arrive one per line on stdin. Every reply line is a TSV record
 * whose FIRST field is a one-letter tag, and every field is percent-encoded —
 * the same rule as everywhere else in the suite, and here it is load-bearing
 * twice over: a line of source code can contain tabs, and it can contain
 * bytes that are not valid UTF-8.
 *
 *   S  field value          one fact about the editor's state
 *   B  index name modified  a buffer in the list
 *   L  lineno text          one visible line
 *   H  lineno start len tok one highlight span within a visible line
 *   E  serial               end of frame — the window repaints on this
 *
 * The frame ends with E so that a renderer never draws a half-arrived screen.
 * Without it the window flickers through partial states on every keystroke,
 * which looks exactly like the editor being slow.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "edit_internal.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void s_row(const char *k, const char *v) { rec_row(3, "S", k, v); }

static void s_num(const char *k, size_t v)
{
	char n[32];
	snprintf(n, sizeof n, "%zu", v);
	s_row(k, n);
}

/* The carried syntax state at `upto`. Scanning from the top of the file on
 * every frame sounds wasteful and is not: it is a single pass over lines that
 * are already in memory, and the alternative — caching state per line and
 * invalidating it on edits — is a cache whose bugs look like "the colours are
 * wrong below where I just typed". */
static syn_state state_at(const buf_t *b, size_t upto)
{
	span_t scratch[64];
	syn_state st = 0;
	for (size_t y = 0; y < upto && y < b->n; y++)
		syn_scan(b->lang, buf_line(b, y), buf_linelen(b, y), &st, scratch, 64);
	return st;
}

/* Expands one line the way the screen will show it: a tab runs to the next
 * multiple of tabstop, a control character shows as ^X. colmap gets len+1
 * entries, colmap[i] being the display column byte i starts at.
 *
 * ⚠ This is why the window is sent DISPLAY COLUMNS and not byte offsets.
 * A renderer given byte offsets has to expand tabs itself to place a span,
 * and it has to know how many bytes a character is — after decoding, a QML
 * string is indexed in characters, not bytes, so every line with a non-ASCII
 * character in it would colour from the wrong place onwards. The engine
 * already knows both answers. It sends them.
 */
static char *expand_line(const char *s, size_t len, int ts, size_t **colmap)
{
	if (ts <= 0)
		ts = 8;
	size_t *map = xmalloc((len + 1) * sizeof *map);
	size_t cap = len * (size_t)ts + 1, w = 0, col = 0;
	char *out = xmalloc(cap + 1);

	for (size_t i = 0; i < len; i++) {
		map[i] = col;
		unsigned char c = (unsigned char)s[i];
		if (c == '\t') {
			size_t n = (size_t)ts - (col % (size_t)ts);
			for (size_t k = 0; k < n && w < cap; k++)
				out[w++] = ' ';
			col += n;
		} else if (c < 0x20 || c == 0x7f) {
			if (w + 2 <= cap) {
				out[w++] = '^';
				out[w++] = (c == 0x7f) ? '?' : (char)('@' + c);
			}
			col += 2;
		} else {
			if (w < cap)
				out[w++] = (char)c;
			/* A continuation byte is part of the character before it and
			 * occupies no column of its own. */
			if ((c & 0xc0) != 0x80)
				col++;
		}
	}
	map[len] = col;
	out[w] = '\0';
	*colmap = map;
	return out;
}

static void emit_frame(ed_t *e, unsigned long serial)
{
	buf_t *b = ed_buf(e);

	size_t top = e->view_top;
	size_t rows = e->view_rows ? e->view_rows : b->n;
	if (top >= b->n)
		top = b->n ? b->n - 1 : 0;
	size_t last = top + rows;
	if (last > b->n)
		last = b->n;

	/* buffers */
	for (size_t i = 0; i < e->nbuf; i++) {
		char idx[32];
		snprintf(idx, sizeof idx, "%zu", i + 1);
		rec_row(5, "B", idx, buf_name(e->buf[i]),
		        e->buf[i]->modified ? "1" : "0", i == e->cur ? "1" : "0");
	}

	/* lines and their spans */
	span_t spans[1024];
	syn_state st = state_at(b, top);
	for (size_t y = top; y < last; y++) {
		char n[32];
		snprintf(n, sizeof n, "%zu", y + 1);

		size_t len = buf_linelen(b, y);
		size_t *map = NULL;
		char *shown = expand_line(buf_line(b, y), len, e->o.tabstop, &map);
		rec_row(3, "L", n, shown);
		free(shown);

		size_t ns = syn_scan(b->lang, buf_line(b, y), len, &st, spans, 1024);
		for (size_t i = 0; i < ns; i++) {
			/* Plain text is the default the renderer already draws; sending
			 * a span for it would roughly double the frame for nothing. */
			if (spans[i].tok == TK_TEXT)
				continue;
			size_t s0 = spans[i].start;
			size_t s1 = s0 + spans[i].len;
			if (s0 > len) s0 = len;
			if (s1 > len) s1 = len;
			if (map[s1] <= map[s0])
				continue;
			char a[32], c[32];
			snprintf(a, sizeof a, "%zu", map[s0]);
			snprintf(c, sizeof c, "%zu", map[s1] - map[s0]);
			rec_row(5, "H", n, a, c, syn_tok_name(spans[i].tok));
		}
		free(map);
	}

	/* state */
	s_row("mode", ed_mode_name(e));
	s_num("line", e->cy + 1);
	s_num("col", e->cx + 1);
	/* The DISPLAY column, so the window can place the caret without
	 * reimplementing tab expansion — and get a different answer. */
	s_num("dcol", disp_col(buf_line(b, e->cy), e->cx, e->o.tabstop) + 1);
	s_num("lines", b->n);
	s_num("top", top);
	s_row("file", buf_name(b));
	/* Whether that name is a PATH or the placeholder. buf_name() answers
	 * "[No Name]" for a buffer that has never been written, and a window
	 * that string-matched for it would be a window that breaks the day the
	 * placeholder is reworded — or worse, one that treats a real file
	 * called "[No Name]" as unnamed. The window needs this to know that
	 * Save has to ask for a name first. */
	s_row("named", (b->path && *b->path) ? "1" : "0");
	s_row("lang", syn_lang_name(b->lang));
	s_row("modified", b->modified ? "1" : "0");
	s_row("readonly", b->readonly ? "1" : "0");
	s_row("eol", b->eol == EOL_CRLF ? "crlf" : "lf");
	s_row("binary", b->binary ? "1" : "0");
	s_num("tabstop", (size_t)e->o.tabstop);
	s_row("number", e->o.number ? "1" : "0");
	s_row("tabbar", e->o.showtabs ? "1" : "0");
	s_row("tree", e->o.tree ? "1" : "0");
	s_num("text_scale", (size_t)e->o.text_scale);
	s_row("msg", e->msg);
	s_row("msgerr", e->msg_err ? "1" : "0");
	s_row("search", e->search ? e->search : "");
	s_row("hlsearch", e->o.hlsearch ? "1" : "0");

	/* The command line as it is being typed, so the window shows what the
	 * engine actually has rather than echoing its own key handling. */
	if (e->mode == M_CMDLINE) {
		char shown[sizeof e->cmd + 2];
		snprintf(shown, sizeof shown, "%c%s", (char)e->cmdchar, e->cmd);
		s_row("cmdline", shown);
	} else {
		s_row("cmdline", "");
	}

	size_t y0, x0, y1, x1;
	if (ed_selection(e, &y0, &x0, &y1, &x1)) {
		/* Display columns, like the spans and dcol — the window highlights a
		 * rectangle and must not have to work out where a byte lands. */
		s_num("sel_y0", y0 + 1);
		s_num("sel_x0", disp_col(buf_line(b, y0), x0, e->o.tabstop));
		s_num("sel_y1", y1 + 1);
		s_num("sel_x1", disp_col(buf_line(b, y1), x1, e->o.tabstop));
		s_row("sel_line", e->mode == M_VISUAL_LINE ? "1" : "0");
	} else {
		s_row("sel_y0", "");
	}

	s_row("quit", e->quit ? "1" : "0");

	char n[32];
	snprintf(n, sizeof n, "%lu", serial);
	rec_row(2, "E", n);
	fflush(stdout);
}

int cmd_serve(int argc, char **argv)
{
	ed_t *e = ed_new();
	opts_load(&e->o);

	for (int i = 0; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1]) {
			ed_free(e);
			die("serve: unknown option '%s'", argv[i]);
		}
		char *err = NULL;
		if (ed_open(e, argv[i], &err) < 0) {
			ed_message(e, true, "%s", err ? err : "could not open");
			free(err);
		}
	}
	e->cur = 0;
	e->cy = e->cx = 0;

	/* The GUI parses records, so this process must never be in colour mode
	 * however it was started. */
	g_out = OUT_REC;
	g_color = false;

	unsigned long serial = 0;
	emit_frame(e, serial);

	char *line = NULL;
	size_t cap = 0;
	while (getline(&line, &cap, stdin) > 0) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';

		char *sp = strchr(line, ' ');
		char *verb = line;
		char *rest = sp ? sp + 1 : (char *)"";
		if (sp)
			*sp = '\0';

		serial++;

		if (!strcmp(verb, "quit")) {
			break;
		} else if (!strcmp(verb, "keys")) {
			char *k = pct_decode(rest);
			ed_keys(e, k);
			free(k);
		} else if (!strcmp(verb, "ex")) {
			char *c = pct_decode(rest);
			ed_ex(e, c);
			free(c);
		} else if (!strcmp(verb, "open")) {
			char *p = pct_decode(rest);
			char *err = NULL;
			if (ed_open(e, p, &err) < 0) {
				ed_message(e, true, "%s", err ? err : "could not open");
				free(err);
			}
			free(p);
		} else if (!strcmp(verb, "new")) {
			ed_ex(e, "enew");
		} else if (!strcmp(verb, "save")) {
			char *p = pct_decode(rest);
			char *err = NULL;
			if (!buf_save(ed_buf(e), *p ? p : NULL, &err))
				ed_message(e, true, "%s", err ? err : "write failed");
			else
				ed_message(e, false, "\"%s\" written", buf_name(ed_buf(e)));
			free(err);
			free(p);
		} else if (!strcmp(verb, "set")) {
			/* The same option change as :set, but SILENT.
			 *
			 * A toolbar button is not a typed command. Clicking Documents
			 * went through `ex set tree!` and left "tree=true" sitting in
			 * the message line, which reads as the window reporting
			 * something rather than as a panel having been toggled. The TUI
			 * keeps the echo, because there it is the only feedback a :set
			 * gets.
			 *
			 * An ERROR still speaks: clearing the message unconditionally
			 * would turn a rejected option into a button that does nothing
			 * and says nothing about it. */
			char cmd[512];
			snprintf(cmd, sizeof cmd, "set %s", rest);
			if (ed_ex(e, cmd) && !e->msg_err)
				e->msg[0] = '\0';
		} else if (!strcmp(verb, "buf")) {
			size_t i = (size_t)atol(rest);
			if (i >= 1 && i <= e->nbuf) {
				e->cur = i - 1;
				e->cy = e->cx = 0;
				ed_clamp(e);
			}
		} else if (!strcmp(verb, "view")) {
			char *sp2 = strchr(rest, ' ');
			e->view_top = (size_t)atol(rest);
			e->view_rows = sp2 ? (size_t)atol(sp2 + 1) : 0;
		} else if (!strcmp(verb, "render")) {
			/* nothing to do — every command ends in a frame */
		} else if (*verb) {
			ed_message(e, true, "unknown request: %s", verb);
		}

		/* The cursor must stay on screen, or the window scrolls somewhere the
		 * engine does not think it is. Done HERE rather than in the window so
		 * that the TUI and the GUI scroll identically. */
		if (e->view_rows) {
			if (e->cy < e->view_top)
				e->view_top = e->cy;
			else if (e->cy >= e->view_top + e->view_rows)
				e->view_top = e->cy - e->view_rows + 1;
		}

		emit_frame(e, serial);
		if (e->quit)
			break;
	}

	free(line);
	ed_free(e);
	return 0;
}
