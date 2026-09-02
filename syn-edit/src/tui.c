/* tui.c — the terminal front-end.
 *
 * A renderer and a key decoder. It contains no editing logic at all: it turns
 * bytes from the terminal into engine keys, asks the engine to draw, and puts
 * the result on screen. Every command works here because it works in vim.c.
 *
 * ── Restoring the terminal ─────────────────────────────────────────────────
 *
 * Raw mode, the alternate screen and a hidden cursor are three things that
 * survive the process if it exits without putting them back — and a shell left
 * in raw mode with no echo looks like the machine has hung. So the restore is
 * on atexit() AND on the fatal signals, rather than at the end of the main
 * loop where a die() three frames deep would skip it.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "edit_internal.h"
#include "i18n.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_saved;
static bool g_raw = false;

static void tui_restore(void)
{
	if (!g_raw)
		return;
	g_raw = false;
	/* Show the cursor, leave the alternate screen, restore the mode. In that
	 * order: leaving the alternate screen first would put the "show cursor"
	 * escape on the user's shell line. */
	fputs("\033[?25h\033[?1049l", stdout);
	fflush(stdout);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
}

static void on_fatal(int sig)
{
	tui_restore();
	/* Re-raise with the default handler so the exit status still says the
	 * process died of a signal — a program that swallows SIGINT and exits 0
	 * lies to whatever ran it. */
	signal(sig, SIG_DFL);
	raise(sig);
}

static bool tui_raw(void)
{
	if (!isatty(STDIN_FILENO))
		return false;
	if (tcgetattr(STDIN_FILENO, &g_saved) != 0)
		return false;

	struct termios t = g_saved;
	t.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	t.c_oflag &= (tcflag_t)~OPOST;
	t.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0)
		return false;

	g_raw = true;
	atexit(tui_restore);
	signal(SIGINT, on_fatal);
	signal(SIGTERM, on_fatal);
	signal(SIGSEGV, on_fatal);
	signal(SIGABRT, on_fatal);

	fputs("\033[?1049h\033[2J", stdout);
	fflush(stdout);
	return true;
}

/* ── key decoding ───────────────────────────────────────────────────────── */

/* After an ESC, a real escape sequence arrives immediately and a user pressing
 * Escape does not. 25ms tells them apart without a perceptible delay on the
 * Escape key, which in a modal editor is pressed more than any other. */
static int read_more(int ms)
{
	struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
	if (poll(&p, 1, ms) <= 0)
		return -1;
	unsigned char c;
	if (read(STDIN_FILENO, &c, 1) != 1)
		return -1;
	return c;
}

static int tui_key(void)
{
	unsigned char c;
	ssize_t n = read(STDIN_FILENO, &c, 1);
	if (n != 1)
		return (n < 0 && errno == EINTR) ? K_NONE : -2;

	if (c != 27)
		return c;

	int a = read_more(25);
	if (a < 0)
		return K_ESC;
	if (a != '[' && a != 'O')
		/* Alt-<key> arrives as ESC then the key. Nothing here binds Alt, so
		 * it is reported as the key itself rather than being swallowed. */
		return a;

	int b = read_more(25);
	if (b < 0)
		return K_ESC;

	switch (b) {
	case 'A': return K_UP;
	case 'B': return K_DOWN;
	case 'C': return K_RIGHT;
	case 'D': return K_LEFT;
	case 'H': return K_HOME;
	case 'F': return K_END;
	}
	if (b >= '0' && b <= '9') {
		int num = b - '0';
		for (;;) {
			int d = read_more(25);
			if (d < 0)
				return K_ESC;
			if (d == '~')
				break;
			if (d >= '0' && d <= '9')
				num = num * 10 + (d - '0');
			else
				break;      /* a modifier form such as ESC[1;5C */
		}
		switch (num) {
		case 1: case 7:  return K_HOME;
		case 2:          return K_INS;
		case 3:          return K_DEL;
		case 4: case 8:  return K_END;
		case 5:          return K_PGUP;
		case 6:          return K_PGDN;
		}
	}
	return K_NONE;
}

/* ── drawing ────────────────────────────────────────────────────────────── */

static const char *tok_colour(tok_t t)
{
	switch (t) {
	case TK_KEYWORD:  return "\033[38;5;141m";
	case TK_TYPE:     return "\033[38;5;81m";
	case TK_CONSTANT: return "\033[38;5;209m";
	case TK_STRING:   return "\033[38;5;114m";
	case TK_CHAR:     return "\033[38;5;114m";
	case TK_NUMBER:   return "\033[38;5;209m";
	case TK_COMMENT:  return "\033[38;5;245m";
	case TK_PREPROC:  return "\033[38;5;176m";
	case TK_FUNC:     return "\033[38;5;111m";
	case TK_OPERATOR: return "\033[38;5;250m";
	case TK_HEADING:  return "\033[1;38;5;141m";
	case TK_ADDED:    return "\033[38;5;114m";
	case TK_REMOVED:  return "\033[38;5;203m";
	default:          return "\033[0m";
	}
}

typedef struct {
	char *s;
	size_t len, cap;
} sbuf;

static void sb_add(sbuf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		while (b->len + n + 1 > b->cap)
			b->cap = b->cap ? b->cap * 2 : 8192;
		b->s = xrealloc(b->s, b->cap);
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = '\0';
}

static void sb_str(sbuf *b, const char *s) { sb_add(b, s, strlen(s)); }

static void sb_fmt(sbuf *b, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n > 0)
		sb_add(b, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

/* One buffer byte, expanded the way disp_col measured it — the two must agree
 * or the caret lands in the wrong place. */
static void put_byte(sbuf *out, unsigned char ch, size_t *w, int ts)
{
	if (ch == '\t') {
		size_t n = (size_t)ts - (*w % (size_t)ts);
		for (size_t i = 0; i < n; i++)
			sb_str(out, " ");
		*w += n;
	} else if (ch < 0x20 || ch == 0x7f) {
		sb_fmt(out, "\033[7m^%c\033[27m", ch == 0x7f ? '?' : (char)('@' + ch));
		*w += 2;
	} else {
		sb_add(out, (const char *)&ch, 1);
		if ((ch & 0xc0) != 0x80)
			(*w)++;
	}
}

static void draw(ed_t *e, size_t rows, size_t cols)
{
	buf_t *b = ed_buf(e);

	size_t textrows = rows > 2 ? rows - 2 : 1;
	e->view_rows = textrows;

	if (e->cy < e->view_top)
		e->view_top = e->cy;
	else if (e->cy >= e->view_top + textrows)
		e->view_top = e->cy - textrows + 1;
	if (e->view_top >= b->n)
		e->view_top = b->n ? b->n - 1 : 0;

	int gutter = 0;
	if (e->o.number) {
		size_t max = b->n;
		gutter = 1;
		while (max >= 10) {
			max /= 10;
			gutter++;
		}
		gutter += 1;                  /* a space between number and text */
		if (gutter < 4)
			gutter = 4;
	}

	size_t textcols = cols > (size_t)gutter ? cols - (size_t)gutter : 1;

	/* Horizontal scroll, so a long line does not push the caret off screen.
	 * Kept in whole columns of the CURSOR line only — scrolling to fit the
	 * longest visible line would move the text sideways as you scroll down. */
	size_t curw = disp_col(buf_line(b, e->cy), e->cx, e->o.tabstop);
	static size_t hscroll = 0;
	if (curw < hscroll)
		hscroll = curw;
	else if (curw >= hscroll + textcols)
		hscroll = curw - textcols + 1;
	if (!e->o.wrap && b->n && curw < textcols)
		hscroll = 0;

	sbuf out = { NULL, 0, 0 };
	sb_str(&out, "\033[?25l\033[H");

	syn_state st = 0;
	span_t spans[1024];
	{
		span_t scratch[64];
		for (size_t y = 0; y < e->view_top && y < b->n; y++)
			syn_scan(b->lang, buf_line(b, y), buf_linelen(b, y), &st,
			         scratch, 64);
	}

	size_t sy0 = 0, sx0 = 0, sy1 = 0, sx1 = 0;
	bool has_sel = ed_selection(e, &sy0, &sx0, &sy1, &sx1);
	bool sel_lines = (e->mode == M_VISUAL_LINE);

	for (size_t r = 0; r < textrows; r++) {
		size_t y = e->view_top + r;
		sb_str(&out, "\033[2K");

		if (y >= b->n) {
			sb_str(&out, "\033[38;5;240m~\033[0m\r\n");
			continue;
		}

		if (e->o.number) {
			bool cur = (y == e->cy);
			sb_fmt(&out, "%s%*zu \033[0m",
			       cur ? "\033[38;5;250m" : "\033[38;5;240m",
			       gutter - 1, y + 1);
		}

		const char *line = buf_line(b, y);
		size_t len = buf_linelen(b, y);
		size_t ns = syn_scan(b->lang, line, len, &st, spans, 1024);

		size_t w = 0;
		sbuf row = { NULL, 0, 0 };
		for (size_t i = 0; i < ns; i++) {
			bool selected_span = false;
			(void)selected_span;
			sb_str(&row, tok_colour(spans[i].tok));
			for (size_t j = 0; j < spans[i].len; j++) {
				size_t bx = spans[i].start + j;
				bool sel = false;
				if (has_sel && y >= sy0 && y <= sy1) {
					if (sel_lines)
						sel = true;
					else if (y == sy0 && y == sy1)
						sel = (bx >= sx0 && bx <= sx1);
					else if (y == sy0)
						sel = (bx >= sx0);
					else if (y == sy1)
						sel = (bx <= sx1);
					else
						sel = true;
				}
				if (sel)
					sb_str(&row, "\033[48;5;238m");
				put_byte(&row, (unsigned char)line[bx], &w, e->o.tabstop);
				if (sel)
					sb_str(&row, "\033[49m");
			}
			sb_str(&row, "\033[0m");
		}

		/* The horizontal window is applied to the RENDERED row, after the
		 * escapes are in it, so it has to be cut by counting printable
		 * columns rather than bytes. Simpler and correct: rebuild the visible
		 * slice by walking the source again. */
		if (hscroll == 0 && w <= textcols) {
			sb_add(&out, row.s ? row.s : "", row.len);
		} else {
			sbuf clip = { NULL, 0, 0 };
			size_t cw = 0;
			for (size_t i = 0; i < ns; i++) {
				sb_str(&clip, tok_colour(spans[i].tok));
				for (size_t j = 0; j < spans[i].len; j++) {
					size_t bx = spans[i].start + j;
					size_t before = cw;
					sbuf one = { NULL, 0, 0 };
					put_byte(&one, (unsigned char)line[bx], &cw,
					         e->o.tabstop);
					if (before >= hscroll && before < hscroll + textcols)
						sb_add(&clip, one.s ? one.s : "", one.len);
					free(one.s);
				}
				sb_str(&clip, "\033[0m");
			}
			sb_add(&out, clip.s ? clip.s : "", clip.len);
			free(clip.s);
		}
		free(row.s);
		sb_str(&out, "\033[0m\r\n");
	}

	/* ── status line ────────────────────────────────────────────────── */
	sb_str(&out, "\033[2K");
	const char *mode = ed_mode_name(e);
	const char *mcol = (e->mode == M_INSERT || e->mode == M_REPLACE)
	                   ? "\033[48;5;114;38;5;232m"
	                   : (e->mode == M_VISUAL || e->mode == M_VISUAL_LINE
	                      || e->mode == M_VISUAL_BLOCK)
	                     ? "\033[48;5;209;38;5;232m"
	                     : "\033[48;5;141;38;5;232m";
	sb_fmt(&out, "%s %s \033[0m\033[48;5;236m %s%s \033[0m",
	       mcol, mode, buf_name(b), b->modified ? " [+]" : "");
	sb_fmt(&out, "\033[48;5;236m%s\033[0m", "");
	sb_fmt(&out, " \033[38;5;245m%s  %zu:%zu  %zu%%\033[0m",
	       syn_lang_name(b->lang), e->cy + 1, e->cx + 1,
	       b->n ? (e->cy + 1) * 100 / b->n : 0);
	if (b->readonly)
		sb_str(&out, " \033[38;5;209m[RO]\033[0m");
	if (b->eol == EOL_CRLF)
		sb_str(&out, " \033[38;5;245mCRLF\033[0m");
	sb_str(&out, "\r\n");

	/* ── message / command line ─────────────────────────────────────── */
	sb_str(&out, "\033[2K");
	if (e->mode == M_CMDLINE) {
		sb_fmt(&out, "%c%s", (char)e->cmdchar, e->cmd);
	} else if (e->msg[0]) {
		sb_fmt(&out, "%s%s\033[0m",
		       e->msg_err ? "\033[38;5;203m" : "\033[38;5;245m", e->msg);
	} else if (e->rec_reg) {
		sb_fmt(&out, "\033[38;5;209mrecording @%c\033[0m", (char)e->rec_reg);
	}

	/* ── caret ──────────────────────────────────────────────────────── */
	if (e->mode == M_CMDLINE) {
		sb_fmt(&out, "\033[%zu;%zuH", rows, (size_t)e->ncmd + 2);
	} else {
		size_t sr = e->cy - e->view_top + 1;
		size_t sc = curw - hscroll + (size_t)gutter + 1;
		sb_fmt(&out, "\033[%zu;%zuH", sr, sc);
	}
	sb_str(&out, "\033[?25h");

	/* One write. Drawing incrementally makes the cursor visibly travel across
	 * the screen on a slow terminal, and over SSH it flickers. */
	ssize_t ignored = write(STDOUT_FILENO, out.s ? out.s : "", out.len);
	(void)ignored;
	free(out.s);
}

/* ── the loop ───────────────────────────────────────────────────────────── */

int cmd_tui(int argc, char **argv)
{
	ed_t *e = ed_new();
	opts_load(&e->o);

	int rc = 0;
	for (int i = 0; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1]) {
			ed_free(e);
			die(_("unknown option '%s'"), argv[i]);
		}
		char *err = NULL;
		if (ed_open(e, argv[i], &err) < 0) {
			warn("%s", err ? err : "could not open");
			free(err);
			rc = 1;
		}
	}
	e->cur = 0;
	e->cy = e->cx = 0;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		ed_free(e);
		die(_("not a terminal — use `syn-edit gui` for a window, "
		    "or `syn-edit run` to script an edit"));
	}
	if (!tui_raw()) {
		ed_free(e);
		die(_("could not put the terminal into raw mode"));
	}

	if (e->nbuf == 1 && !ed_buf(e)->path)
		ed_message(e, false,
		           _("syn-edit — :help is :h, :q quits, i inserts"));

	for (;;) {
		struct winsize ws;
		size_t rows = 24, cols = 80;
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
			rows = ws.ws_row;
			cols = ws.ws_col;
		}
		draw(e, rows, cols);

		int k = tui_key();
		if (k == -2)
			break;                  /* stdin closed */
		if (k == K_NONE)
			continue;               /* a resize interrupted the read */
		ed_key(e, k);
		if (e->quit)
			break;
	}

	tui_restore();
	ed_free(e);
	return rc;
}
