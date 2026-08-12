/* vim.c — the modal engine. Both front-ends are renderers over this file.
 *
 * ── How a key becomes a change ─────────────────────────────────────────────
 *
 * ed_key() is the whole interface. It is a state machine over four things
 * that accumulate before a command can run: a count, a register, an operator,
 * and a prefix (g, z, ", f, m, …). "2d3w" is two counts, an operator and a
 * motion, and the counts multiply. Nothing is executed until enough has
 * arrived to be unambiguous, which is why the pending state lives in ed_t and
 * not in local variables — a front-end may hand over one key an hour.
 *
 * ── Operators and motions are separate ─────────────────────────────────────
 *
 * A motion answers "where would the cursor go", and says whether the span it
 * covers is exclusive, inclusive or linewise. An operator answers "what do I
 * do with a span". Every combination therefore works without being written
 * down: `d` and `}` were implemented once each and `d}` came free. The whole
 * point of a modal editor is this multiplication, and an implementation that
 * hard-codes `dw`, `db`, `dd` as three commands has thrown it away.
 *
 * ── Dot ────────────────────────────────────────────────────────────────────
 *
 * `.` is not a saved edit, it is the saved KEYS of the last change, replayed
 * through this same function. That is the only version that stays correct as
 * commands are added: anything that changes the buffer is repeatable the day
 * it is written, including commands nobody thought about when `.` was built.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "edit_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

buf_t *ed_cur(ed_t *e) { return e->buf[e->cur]; }
#define B(e) ed_cur(e)

/* ── lifecycle ──────────────────────────────────────────────────────────── */

ed_t *ed_new(void)
{
	ed_t *e = xmalloc(sizeof *e);
	memset(e, 0, sizeof *e);
	e->buf[0] = buf_new();
	e->nbuf = 1;
	e->cur = 0;
	e->mode = M_NORMAL;
	e->search_dir = 1;
	e->view_rows = 0;          /* 0 = "the whole buffer"; see view_bottom() */
	opts_defaults(&e->o);
	return e;
}

void ed_free(ed_t *e)
{
	if (!e)
		return;
	for (size_t i = 0; i < e->nbuf; i++)
		buf_free(e->buf[i]);
	for (size_t i = 0; i < NREG; i++)
		free(e->regs[i].text);
	free(e->search);
	free(e->want_open);
	free(e);
}

buf_t *ed_buf(ed_t *e) { return B(e); }

void ed_message(ed_t *e, bool err, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(e->msg, sizeof e->msg, fmt, ap);
	va_end(ap);
	e->msg_err = err;
}

const char *ed_mode_name(const ed_t *e)
{
	switch (e->mode) {
	case M_INSERT:       return "INSERT";
	case M_REPLACE:      return "REPLACE";
	case M_VISUAL:       return "VISUAL";
	case M_VISUAL_LINE:  return "V-LINE";
	case M_VISUAL_BLOCK: return "V-BLOCK";
	case M_CMDLINE:      return "COMMAND";
	default:             return "NORMAL";
	}
}

int ed_open(ed_t *e, const char *path, char **err)
{
	/* A file already open is switched to rather than opened twice: two
	 * buffers over one file have two undo stacks, and whichever is saved
	 * second silently discards the other's work. */
	char *full = expand_path(path);
	for (size_t i = 0; i < e->nbuf; i++) {
		if (e->buf[i]->path && strcmp(e->buf[i]->path, full) == 0) {
			e->cur = i;
			e->cy = e->cx = 0;
			free(full);
			return (int)i;
		}
	}
	free(full);

	if (e->nbuf >= MAXBUF) {
		if (err)
			*err = xasprintf("too many open files (max %d)", MAXBUF);
		return -1;
	}

	buf_t *b = buf_new();
	if (!buf_load(b, path, err)) {
		buf_free(b);
		return -1;
	}

	/* The first buffer is replaced when it is an untouched scratch — starting
	 * syn-edit with a file should not leave [No Name] in the buffer list. */
	if (e->nbuf == 1 && !e->buf[0]->path && !e->buf[0]->modified
	    && e->buf[0]->n == 1 && buf_linelen(e->buf[0], 0) == 0) {
		buf_free(e->buf[0]);
		e->buf[0] = b;
		e->cur = 0;
		e->cy = e->cx = 0;
		return 0;
	}

	e->buf[e->nbuf++] = b;
	e->cur = e->nbuf - 1;
	e->cy = e->cx = 0;
	return (int)e->cur;
}

/* ── cursor ─────────────────────────────────────────────────────────────── */

size_t line_first_nonblank(const buf_t *b, size_t y)
{
	const char *s = buf_line(b, y);
	size_t len = buf_linelen(b, y), i = 0;
	while (i < len && (s[i] == ' ' || s[i] == '\t'))
		i++;
	return (i < len) ? i : (len ? len - 1 : 0);
}

void ed_clamp(ed_t *e)
{
	buf_t *b = B(e);
	if (b->n == 0)
		return;
	if (e->cy >= b->n)
		e->cy = b->n - 1;
	size_t len = buf_linelen(b, e->cy);
	/* Insert mode may sit one past the last byte — that is where a character
	 * typed at the end of a line goes. Normal mode may not, or `x` at the end
	 * of a line deletes nothing and the caret is drawn off the text. */
	size_t maxx = (e->mode == M_INSERT || e->mode == M_REPLACE)
	              ? len : (len ? len - 1 : 0);
	if (e->cx > maxx)
		e->cx = maxx;
}

static size_t view_bottom(const ed_t *e)
{
	const buf_t *b = e->buf[e->cur];
	if (e->view_rows == 0)
		return b->n ? b->n - 1 : 0;
	size_t bot = e->view_top + e->view_rows - 1;
	return bot >= b->n ? (b->n ? b->n - 1 : 0) : bot;
}

/* ── registers ──────────────────────────────────────────────────────────── */

/* 0–25 a–z, 26–35 "0–"9, 36 unnamed, 37 small-delete, 38 last search,
 * 39 last insert, 40 the system clipboard. */
#define R_UNNAMED 36
#define R_SMALL   37
#define R_SEARCH  38
#define R_INSERT  39
#define R_CLIP    40

int reg_index(int c)
{
	if (c >= 'a' && c <= 'z') return c - 'a';
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= '0' && c <= '9') return 26 + (c - '0');
	if (c == '"') return R_UNNAMED;
	if (c == '-') return R_SMALL;
	if (c == '/') return R_SEARCH;
	if (c == '.') return R_INSERT;
	if (c == '+' || c == '*') return R_CLIP;
	return -1;
}

/* The clipboard registers are the desktop's, not this program's. wl-copy and
 * wl-paste are how everything else on SynapseOS reaches it, and shelling out
 * to them costs a fork on an explicit "+y — where holding a Wayland data
 * source open for the lifetime of the editor would mean the TUI needed a
 * display connection. Absent tools fall back to an ordinary register, so "+y
 * still works, just privately. */
static void clip_write(const char *text)
{
	if (!have_cmd("wl-copy"))
		return;
	int st = 0;
	/* The text travels as an ARGUMENT, not through a shell — wl-copy takes it
	 * that way, and every other route from here to the clipboard would need a
	 * quoting rule that a yanked line of code eventually breaks. */
	char *argv[] = { (char *)"wl-copy", (char *)"--", (char *)text, NULL };
	free(run_capture(argv, &st, true));
}

static char *clip_read(void)
{
	if (!have_cmd("wl-paste"))
		return NULL;
	int st = 0;
	char *argv[] = { (char *)"wl-paste", (char *)"--no-newline", NULL };
	char *out = run_capture(argv, &st, true);
	if (st != 0) {
		free(out);
		return NULL;
	}
	return out;
}

void reg_set(ed_t *e, int c, const char *text, bool linewise)
{
	if (c == 0)
		c = '"';
	int i = reg_index(c);
	if (i < 0)
		return;

	if (i == R_CLIP)
		clip_write(text);

	/* An uppercase name appends to the register rather than replacing it —
	 * "Ayy three times collects three lines. */
	if (c >= 'A' && c <= 'Z' && e->regs[i].text) {
		/* ⚠ A linewise register's text ALREADY ends in a newline — that
		 * trailing newline is what makes it linewise. Joining two of them
		 * with another one inserts a blank line between them, so "Ayy twice
		 * put back a blank line in the middle of what was collected. */
		size_t have = strlen(e->regs[i].text);
		bool need_nl = (e->regs[i].linewise || linewise)
		               && have > 0 && e->regs[i].text[have - 1] != '\n';
		char *joined = need_nl
		               ? xasprintf("%s\n%s", e->regs[i].text, text)
		               : xasprintf("%s%s", e->regs[i].text, text);
		free(e->regs[i].text);
		e->regs[i].text = joined;
		e->regs[i].linewise = e->regs[i].linewise || linewise;
	} else {
		free(e->regs[i].text);
		e->regs[i].text = xstrdup(text);
		e->regs[i].linewise = linewise;
	}

	/* Every yank and delete also lands in the unnamed register, which is what
	 * a bare p pastes. Without this, "ayy followed by p pastes whatever was
	 * there before, which reads as the yank having failed. */
	if (i != R_UNNAMED) {
		free(e->regs[R_UNNAMED].text);
		e->regs[R_UNNAMED].text = xstrdup(e->regs[i].text);
		e->regs[R_UNNAMED].linewise = e->regs[i].linewise;
	}
}

const reg_t *reg_get(ed_t *e, int c)
{
	if (c == 0)
		c = '"';
	int i = reg_index(c);
	if (i < 0)
		return NULL;
	if (i == R_CLIP) {
		char *sys = clip_read();
		if (sys) {
			free(e->regs[i].text);
			e->regs[i].text = sys;
			/* Text ending in a newline came from a line-oriented copy. */
			size_t n = strlen(sys);
			e->regs[i].linewise = n > 0 && sys[n - 1] == '\n';
		}
	}
	return e->regs[i].text ? &e->regs[i] : NULL;
}

/* ── text primitives ────────────────────────────────────────────────────── */

void set_line(ed_t *e, size_t y, const char *text)
{
	char *one[1];
	one[0] = (char *)text;
	buf_splice(B(e), y, 1, one, 1, e->cy, e->cx);
}

char *range_text(ed_t *e, size_t y0, size_t x0, size_t y1, size_t x1,
                 bool linewise)
{
	buf_t *b = B(e);
	if (y1 >= b->n)
		y1 = b->n - 1;

	size_t total = 0;
	if (linewise) {
		for (size_t y = y0; y <= y1; y++)
			total += buf_linelen(b, y) + 1;
	} else if (y0 == y1) {
		size_t len = buf_linelen(b, y0);
		if (x1 > len) x1 = len;
		if (x0 > x1) x0 = x1;
		total = x1 - x0;
	} else {
		total = buf_linelen(b, y0) - (x0 < buf_linelen(b, y0) ? x0 : buf_linelen(b, y0)) + 1;
		for (size_t y = y0 + 1; y < y1; y++)
			total += buf_linelen(b, y) + 1;
		total += x1 < buf_linelen(b, y1) ? x1 : buf_linelen(b, y1);
	}

	char *out = xmalloc(total + 2);
	size_t w = 0;

	if (linewise) {
		for (size_t y = y0; y <= y1; y++) {
			size_t len = buf_linelen(b, y);
			memcpy(out + w, buf_line(b, y), len);
			w += len;
			out[w++] = '\n';
		}
	} else if (y0 == y1) {
		size_t len = buf_linelen(b, y0);
		if (x1 > len) x1 = len;
		if (x0 > x1) x0 = x1;
		memcpy(out + w, buf_line(b, y0) + x0, x1 - x0);
		w += x1 - x0;
	} else {
		size_t len = buf_linelen(b, y0);
		size_t s = x0 < len ? x0 : len;
		memcpy(out + w, buf_line(b, y0) + s, len - s);
		w += len - s;
		out[w++] = '\n';
		for (size_t y = y0 + 1; y < y1; y++) {
			size_t l = buf_linelen(b, y);
			memcpy(out + w, buf_line(b, y), l);
			w += l;
			out[w++] = '\n';
		}
		size_t l1 = buf_linelen(b, y1);
		size_t t = x1 < l1 ? x1 : l1;
		memcpy(out + w, buf_line(b, y1), t);
		w += t;
	}
	out[w] = '\0';
	return out;
}

void range_delete(ed_t *e, size_t y0, size_t x0, size_t y1, size_t x1,
                  bool linewise)
{
	buf_t *b = B(e);
	if (y1 >= b->n)
		y1 = b->n - 1;

	if (linewise) {
		buf_splice(b, y0, y1 - y0 + 1, NULL, 0, e->cy, e->cx);
		return;
	}

	size_t len0 = buf_linelen(b, y0);
	size_t len1 = buf_linelen(b, y1);
	if (x0 > len0) x0 = len0;
	if (x1 > len1) x1 = len1;
	if (y0 == y1 && x0 > x1) x0 = x1;

	size_t headn = x0;
	size_t tailn = len1 - x1;
	char *joined = xmalloc(headn + tailn + 1);
	memcpy(joined, buf_line(b, y0), headn);
	memcpy(joined + headn, buf_line(b, y1) + x1, tailn);
	joined[headn + tailn] = '\0';

	char *one[1] = { joined };
	buf_splice(b, y0, y1 - y0 + 1, one, 1, e->cy, e->cx);
	free(joined);
}

/* Splits `text` on newlines and stitches it into line y at byte x. Reports
 * where the inserted text ended, which is where p leaves the cursor and where
 * insert mode resumes typing. */
void insert_text_at(ed_t *e, size_t y, size_t x, const char *text,
                    size_t *endy, size_t *endx)
{
	buf_t *b = B(e);
	size_t len = buf_linelen(b, y);
	if (x > len)
		x = len;
	const char *cur = buf_line(b, y);

	size_t nseg = 1;
	for (const char *p = text; *p; p++)
		if (*p == '\n')
			nseg++;

	char **out = xmalloc(nseg * sizeof *out);
	const char *seg = text;
	for (size_t i = 0; i < nseg; i++) {
		const char *nl = strchr(seg, '\n');
		size_t sl = nl ? (size_t)(nl - seg) : strlen(seg);
		size_t pre = (i == 0) ? x : 0;
		size_t post = (i + 1 == nseg) ? len - x : 0;
		out[i] = xmalloc(pre + sl + post + 1);
		if (pre)
			memcpy(out[i], cur, pre);
		memcpy(out[i] + pre, seg, sl);
		if (post)
			memcpy(out[i] + pre + sl, cur + x, post);
		out[i][pre + sl + post] = '\0';
		if (i + 1 == nseg) {
			if (endy) *endy = y + i;
			if (endx) *endx = pre + sl;
		}
		seg = nl ? nl + 1 : seg + sl;
	}

	buf_splice(b, y, 1, out, nseg, e->cy, e->cx);
	for (size_t i = 0; i < nseg; i++)
		free(out[i]);
	free(out);
}

/* ── character classes and word motions ─────────────────────────────────── */

static int cclass(unsigned char c)
{
	if (c == ' ' || c == '\t')
		return 0;
	if (isalnum(c) || c == '_' || c >= 0x80)
		return 1;
	return 2;
}

static int class_at(const buf_t *b, size_t y, size_t x, bool big)
{
	size_t len = buf_linelen(b, y);
	if (x >= len)
		return 0;
	int c = cclass((unsigned char)buf_line(b, y)[x]);
	/* WORD motions know only "blank" and "not blank". */
	return (big && c == 2) ? 1 : c;
}

static bool pos_next(const buf_t *b, size_t *y, size_t *x)
{
	if (*x + 1 < buf_linelen(b, *y)) {
		(*x)++;
		return true;
	}
	if (*y + 1 < b->n) {
		(*y)++;
		*x = 0;
		return true;
	}
	/* One past the last byte of the last line is a real position for an
	 * operator's end, so advancing there is allowed exactly once. */
	if (*x < buf_linelen(b, *y)) {
		(*x)++;
		return true;
	}
	return false;
}

static bool pos_prev(const buf_t *b, size_t *y, size_t *x)
{
	if (*x > 0) {
		(*x)--;
		return true;
	}
	if (*y == 0)
		return false;
	(*y)--;
	size_t len = buf_linelen(b, *y);
	*x = len ? len - 1 : 0;
	return true;
}

static bool at_eol(const buf_t *b, size_t y, size_t x)
{
	return x >= buf_linelen(b, y);
}

static void word_fwd(const buf_t *b, size_t *y, size_t *x, bool big)
{
	int c = class_at(b, *y, *x, big);
	if (c != 0) {
		while (class_at(b, *y, *x, big) == c && !at_eol(b, *y, *x))
			if (!pos_next(b, y, x))
				return;
		if (at_eol(b, *y, *x) && *y + 1 < b->n) {
			/* An empty line is a word, so landing on one stops here. */
			(*y)++;
			*x = 0;
			if (buf_linelen(b, *y) == 0)
				return;
		}
	} else if (at_eol(b, *y, *x)) {
		if (*y + 1 < b->n) {
			(*y)++;
			*x = 0;
			if (buf_linelen(b, *y) == 0)
				return;
		}
	}
	while (class_at(b, *y, *x, big) == 0) {
		if (at_eol(b, *y, *x)) {
			if (*y + 1 >= b->n)
				return;
			(*y)++;
			*x = 0;
			if (buf_linelen(b, *y) == 0)
				return;
			continue;
		}
		if (!pos_next(b, y, x))
			return;
	}
}

static void word_end(const buf_t *b, size_t *y, size_t *x, bool big)
{
	/* e always moves at least one character, or `e` on the last letter of a
	 * word would never leave it. */
	if (!pos_next(b, y, x))
		return;
	while (class_at(b, *y, *x, big) == 0) {
		if (at_eol(b, *y, *x)) {
			if (*y + 1 >= b->n)
				return;
			(*y)++;
			*x = 0;
			continue;
		}
		if (!pos_next(b, y, x))
			return;
	}
	int c = class_at(b, *y, *x, big);
	while (!at_eol(b, *y, *x)) {
		size_t ny = *y, nx = *x;
		if (!pos_next(b, &ny, &nx))
			break;
		if (ny != *y || class_at(b, ny, nx, big) != c)
			break;
		*y = ny;
		*x = nx;
	}
}

static void word_back(const buf_t *b, size_t *y, size_t *x, bool big)
{
	if (!pos_prev(b, y, x))
		return;
	while (class_at(b, *y, *x, big) == 0) {
		if (*x == 0 && buf_linelen(b, *y) == 0)
			return;         /* an empty line is a word */
		if (!pos_prev(b, y, x))
			return;
	}
	int c = class_at(b, *y, *x, big);
	while (*x > 0 && class_at(b, *y, *x - 1, big) == c)
		(*x)--;
}

/* ── paragraphs and brackets ────────────────────────────────────────────── */

static bool blank_line(const buf_t *b, size_t y)
{
	size_t len = buf_linelen(b, y);
	const char *s = buf_line(b, y);
	for (size_t i = 0; i < len; i++)
		if (s[i] != ' ' && s[i] != '\t')
			return false;
	return true;
}

static size_t para_fwd(const buf_t *b, size_t y)
{
	size_t i = y;
	while (i + 1 < b->n && blank_line(b, i))
		i++;
	while (i + 1 < b->n && !blank_line(b, i))
		i++;
	return i;
}

static size_t para_back(const buf_t *b, size_t y)
{
	size_t i = y;
	while (i > 0 && blank_line(b, i))
		i--;
	while (i > 0 && !blank_line(b, i))
		i--;
	return i;
}

static const char OPENS[] = "([{";
static const char CLOSES[] = ")]}";

static bool match_bracket(const buf_t *b, size_t y, size_t x,
                          size_t *my, size_t *mx)
{
	size_t len = buf_linelen(b, y);
	const char *s = buf_line(b, y);

	/* % on a line with no bracket under the cursor searches forward on the
	 * line for one first — the vim behaviour, and the one that makes % useful
	 * from the start of a line. */
	size_t sx = x;
	while (sx < len && !strchr(OPENS, s[sx]) && !strchr(CLOSES, s[sx]))
		sx++;
	if (sx >= len)
		return false;

	char c = s[sx];
	const char *o = strchr(OPENS, c);
	int dir = o ? 1 : -1;
	char want = o ? CLOSES[o - OPENS] : OPENS[strchr(CLOSES, c) - CLOSES];

	int depth = 0;
	size_t cy = y, cx = sx;
	for (;;) {
		char ch = buf_line(b, cy)[cx];
		if (ch == c)
			depth++;
		else if (ch == want) {
			depth--;
			if (depth == 0) {
				*my = cy;
				*mx = cx;
				return true;
			}
		}
		if (dir > 0) {
			if (cx + 1 < buf_linelen(b, cy)) {
				cx++;
			} else if (cy + 1 < b->n) {
				cy++;
				cx = 0;
				while (cy < b->n && buf_linelen(b, cy) == 0) {
					cy++;
					if (cy >= b->n)
						return false;
				}
				if (cy >= b->n)
					return false;
			} else {
				return false;
			}
		} else {
			if (cx > 0) {
				cx--;
			} else if (cy > 0) {
				cy--;
				while (buf_linelen(b, cy) == 0) {
					if (cy == 0)
						return false;
					cy--;
				}
				cx = buf_linelen(b, cy) - 1;
			} else {
				return false;
			}
		}
	}
}

/* ── motions ────────────────────────────────────────────────────────────── */

typedef enum { MK_EXCL, MK_INCL, MK_LINE } motkind;

typedef struct {
	size_t y, x;
	motkind kind;
	bool ok;
	bool keep_col;      /* j/k keep the desired column */
	bool to_first_nb;   /* linewise motions that land on the first non-blank */
} mot_t;

/* `repeat` is true only for ; and , — never for the first f/F/t/T.
 *
 * ⚠ The difference is the whole correctness of `t`. Repeated, it must not
 * stall on the character it is already sitting before, so the scan starts one
 * further along. On the FIRST use it must not do that, or `dt,` with the
 * cursor on the character just before the comma skips to the NEXT comma —
 * and when there is no next comma the motion fails and deletes nothing, which
 * is exactly what it did. */
static bool find_char(ed_t *e, int cmd, int target, long count, bool repeat,
                      size_t *outx)
{
	buf_t *b = B(e);
	const char *s = buf_line(b, e->cy);
	size_t len = buf_linelen(b, e->cy);
	long n = count > 0 ? count : 1;

	if (cmd == 'f' || cmd == 't') {
		size_t x = e->cx;
		if (repeat && cmd == 't' && x + 2 <= len && s[x + 1] == (char)target)
			x++;
		for (long k = 0; k < n; k++) {
			size_t i = x + 1;
			while (i < len && s[i] != (char)target)
				i++;
			if (i >= len)
				return false;
			x = i;
		}
		*outx = (cmd == 't') ? x - 1 : x;
		return true;
	}

	size_t x = e->cx;
	if (repeat && cmd == 'T' && x >= 1 && x - 1 < len && s[x - 1] == (char)target)
		x--;
	for (long k = 0; k < n; k++) {
		if (x == 0)
			return false;
		size_t i = x - 1;
		for (;;) {
			if (s[i] == (char)target)
				break;
			if (i == 0)
				return false;
			i--;
		}
		x = i;
	}
	*outx = (cmd == 'T') ? x + 1 : x;
	return true;
}

/* Returns false when `k` is not a motion at all. */
static bool do_motion(ed_t *e, int k, long count, int arg, mot_t *m)
{
	buf_t *b = B(e);
	long n = count > 0 ? count : 1;
	m->ok = true;
	m->kind = MK_EXCL;
	m->keep_col = false;
	m->to_first_nb = false;
	m->y = e->cy;
	m->x = e->cx;

	switch (k) {
	case 'h': case K_LEFT: case K_BS:
		m->x = (size_t)n > e->cx ? 0 : e->cx - (size_t)n;
		return true;
	case 'l': case K_RIGHT: case ' ': {
		size_t len = buf_linelen(b, e->cy);
		m->x = e->cx + (size_t)n;
		if (m->x > len)
			m->x = len;
		return true;
	}
	case 'j': case K_DOWN: case 14:      /* Ctrl-N */
		m->kind = MK_LINE;
		m->keep_col = true;
		m->y = e->cy + (size_t)n;
		if (m->y >= b->n)
			m->y = b->n - 1;
		return true;
	case 'k': case K_UP: case 16:        /* Ctrl-P */
		m->kind = MK_LINE;
		m->keep_col = true;
		m->y = (size_t)n > e->cy ? 0 : e->cy - (size_t)n;
		return true;
	case '0': case K_HOME:
		m->x = 0;
		return true;
	case '^':
		m->x = line_first_nonblank(b, e->cy);
		return true;
	case '$': case K_END: {
		m->y = e->cy + (size_t)(n - 1);
		if (m->y >= b->n)
			m->y = b->n - 1;
		size_t len = buf_linelen(b, m->y);
		m->x = len ? len - 1 : 0;
		m->kind = MK_INCL;
		return true;
	}
	case '|':
		m->x = (size_t)(n - 1);
		return true;
	case 'w': case 'W': {
		size_t y = e->cy, x = e->cx;
		for (long i = 0; i < n; i++)
			word_fwd(b, &y, &x, k == 'W');
		m->y = y;
		m->x = x;
		return true;
	}
	case 'e': case 'E': {
		size_t y = e->cy, x = e->cx;
		for (long i = 0; i < n; i++)
			word_end(b, &y, &x, k == 'E');
		m->y = y;
		m->x = x;
		m->kind = MK_INCL;
		return true;
	}
	case 'b': case 'B': {
		size_t y = e->cy, x = e->cx;
		for (long i = 0; i < n; i++)
			word_back(b, &y, &x, k == 'B');
		m->y = y;
		m->x = x;
		return true;
	}
	case 'G':
		m->kind = MK_LINE;
		m->to_first_nb = true;
		m->y = count > 0 ? (size_t)(count - 1) : b->n - 1;
		if (m->y >= b->n)
			m->y = b->n - 1;
		return true;
	case '{':
		m->y = e->cy;
		for (long i = 0; i < n; i++)
			m->y = para_back(b, m->y);
		m->x = 0;
		return true;
	case '}':
		m->y = e->cy;
		for (long i = 0; i < n; i++)
			m->y = para_fwd(b, m->y);
		m->x = 0;
		return true;
	case '%': {
		size_t my, mx;
		if (!match_bracket(b, e->cy, e->cx, &my, &mx)) {
			m->ok = false;
			return true;
		}
		m->y = my;
		m->x = mx;
		m->kind = MK_INCL;
		return true;
	}
	case 'H':
		m->kind = MK_LINE;
		m->to_first_nb = true;
		m->y = e->view_top + (size_t)(n - 1);
		if (m->y >= b->n)
			m->y = b->n - 1;
		return true;
	case 'L': {
		m->kind = MK_LINE;
		m->to_first_nb = true;
		size_t bot = view_bottom(e);
		m->y = (size_t)(n - 1) > bot ? 0 : bot - (size_t)(n - 1);
		return true;
	}
	case 'M':
		m->kind = MK_LINE;
		m->to_first_nb = true;
		m->y = e->view_top + (view_bottom(e) - e->view_top) / 2;
		if (m->y >= b->n)
			m->y = b->n - 1;
		return true;
	case '+': case '\r': case '\n':
		m->kind = MK_LINE;
		m->to_first_nb = true;
		m->y = e->cy + (size_t)n;
		if (m->y >= b->n)
			m->y = b->n - 1;
		return true;
	case '-':
		m->kind = MK_LINE;
		m->to_first_nb = true;
		m->y = (size_t)n > e->cy ? 0 : e->cy - (size_t)n;
		return true;
	case '_':
		m->kind = MK_LINE;
		m->to_first_nb = true;
		m->y = e->cy + (size_t)(n - 1);
		if (m->y >= b->n)
			m->y = b->n - 1;
		return true;
	case 'f': case 'F': case 't': case 'T': {
		size_t x;
		if (!find_char(e, k, arg, n, false, &x)) {
			m->ok = false;
			return true;
		}
		m->x = x;
		m->kind = (k == 'f' || k == 't') ? MK_INCL : MK_EXCL;
		return true;
	}
	case ';': case ',': {
		if (!e->last_ft_cmd) {
			m->ok = false;
			return true;
		}
		int c = e->last_ft_cmd;
		if (k == ',') {
			/* , is ; in the other direction, so the command is mirrored
			 * rather than the search being written twice. */
			c = (c == 'f') ? 'F' : (c == 'F') ? 'f'
			  : (c == 't') ? 'T' : 't';
		}
		size_t x;
		if (!find_char(e, c, e->last_ft, n, true, &x)) {
			m->ok = false;
			return true;
		}
		m->x = x;
		m->kind = (c == 'f' || c == 't') ? MK_INCL : MK_EXCL;
		return true;
	}
	default:
		return false;
	}
}

/* ── text objects ───────────────────────────────────────────────────────── */

static bool textobj(ed_t *e, bool around, int obj,
                    size_t *y0, size_t *x0, size_t *y1, size_t *x1,
                    bool *linewise)
{
	buf_t *b = B(e);
	*linewise = false;

	if (obj == 'w' || obj == 'W') {
		size_t len = buf_linelen(b, e->cy);
		if (len == 0)
			return false;
		bool big = (obj == 'W');
		size_t x = e->cx < len ? e->cx : len - 1;
		int c = class_at(b, e->cy, x, big);
		size_t s = x, t = x;
		while (s > 0 && class_at(b, e->cy, s - 1, big) == c)
			s--;
		while (t + 1 < len && class_at(b, e->cy, t + 1, big) == c)
			t++;
		if (around) {
			/* aw takes the trailing whitespace, or the leading kind when
			 * there is none after — which is what makes `daw` on the last
			 * word of a line not leave a dangling space. */
			size_t t2 = t;
			while (t2 + 1 < len && class_at(b, e->cy, t2 + 1, big) == 0)
				t2++;
			if (t2 == t) {
				while (s > 0 && class_at(b, e->cy, s - 1, big) == 0)
					s--;
			}
			t = t2;
		}
		*y0 = *y1 = e->cy;
		*x0 = s;
		*x1 = t + 1;              /* exclusive */
		return true;
	}

	if (obj == 'p') {
		size_t s = e->cy, t = e->cy;
		bool blank = blank_line(b, e->cy);
		while (s > 0 && blank_line(b, s - 1) == blank)
			s--;
		while (t + 1 < b->n && blank_line(b, t + 1) == blank)
			t++;
		if (around) {
			size_t t2 = t;
			while (t2 + 1 < b->n && blank_line(b, t2 + 1) != blank)
				t2++;
			t = t2;
		}
		*y0 = s;
		*y1 = t;
		*x0 = *x1 = 0;
		*linewise = true;
		return true;
	}

	/* quotes — within the current line only, as in vim */
	if (obj == '"' || obj == '\'' || obj == '`') {
		const char *s = buf_line(b, e->cy);
		size_t len = buf_linelen(b, e->cy);
		size_t open = (size_t)-1, close = (size_t)-1;
		/* Scan from the start of the line pairing quotes, so the cursor being
		 * anywhere inside the string finds ITS pair rather than the nearest
		 * quote in either direction — which for `a "b" c "d"` with the cursor
		 * on c would otherwise select from the second quote to the third. */
		for (size_t i = 0; i < len; i++) {
			if (s[i] == '\\') {
				i++;
				continue;
			}
			if (s[i] != (char)obj)
				continue;
			size_t j = i + 1;
			while (j < len && s[j] != (char)obj) {
				if (s[j] == '\\')
					j++;
				j++;
			}
			if (j >= len)
				break;
			if (e->cx <= j) {
				open = i;
				close = j;
				break;
			}
			i = j;
		}
		if (open == (size_t)-1)
			return false;
		*y0 = *y1 = e->cy;
		if (around) {
			*x0 = open;
			*x1 = close + 1;
		} else {
			*x0 = open + 1;
			*x1 = close;
		}
		return true;
	}

	/* brackets */
	{
		char oc = 0, cc = 0;
		switch (obj) {
		case '(': case ')': case 'b': oc = '('; cc = ')'; break;
		case '{': case '}': case 'B': oc = '{'; cc = '}'; break;
		case '[': case ']': oc = '['; cc = ']'; break;
		case '<': case '>': oc = '<'; cc = '>'; break;
		default: return false;
		}

		/* Walk out from the cursor to the enclosing pair. */
		size_t oy = e->cy, ox = e->cx;
		int depth = 0;
		bool found = false;
		for (;;) {
			char ch = ox < buf_linelen(b, oy) ? buf_line(b, oy)[ox] : '\0';
			if (ch == cc && !(oy == e->cy && ox == e->cx))
				depth++;
			else if (ch == oc) {
				if (depth == 0) {
					found = true;
					break;
				}
				depth--;
			}
			if (ox == 0) {
				if (oy == 0)
					break;
				oy--;
				size_t l = buf_linelen(b, oy);
				ox = l ? l - 1 : 0;
				if (l == 0)
					continue;
			} else {
				ox--;
			}
		}
		if (!found)
			return false;

		size_t my, mx;
		if (!match_bracket(b, oy, ox, &my, &mx))
			return false;

		if (around) {
			*y0 = oy; *x0 = ox;
			*y1 = my; *x1 = mx + 1;
		} else {
			*y0 = oy; *x0 = ox + 1;
			*y1 = my; *x1 = mx;
			/* i{ over a brace that opens a block takes whole lines: the
			 * charwise reading leaves the newline after { and the indent
			 * before }, so `di{` on a C function empties the body but leaves
			 * two ragged lines. */
			if (oy != my && ox + 1 >= buf_linelen(b, oy) && mx == line_first_nonblank(b, my)) {
				if (oy + 1 <= my - 1) {
					*y0 = oy + 1;
					*y1 = my - 1;
					*x0 = *x1 = 0;
					*linewise = true;
				}
			}
		}
		if (*y0 > *y1 || (*y0 == *y1 && *x0 > *x1))
			return false;
		return true;
	}
}

/* ── operators ──────────────────────────────────────────────────────────── */

static char *indent_string(const ed_t *e, int width)
{
	if (width <= 0)
		return xstrdup("");
	if (e->o.expandtab) {
		char *s = xmalloc((size_t)width + 1);
		memset(s, ' ', (size_t)width);
		s[width] = '\0';
		return s;
	}
	int tabs = width / e->o.tabstop;
	int rest = width % e->o.tabstop;
	char *s = xmalloc((size_t)(tabs + rest) + 1);
	int i = 0;
	for (; i < tabs; i++)
		s[i] = '\t';
	for (int j = 0; j < rest; j++)
		s[i++] = ' ';
	s[i] = '\0';
	return s;
}

static int leading_width(const ed_t *e, size_t y)
{
	const buf_t *b = e->buf[e->cur];
	const char *s = buf_line(b, y);
	size_t len = buf_linelen(b, y), i = 0;
	int w = 0;
	while (i < len && (s[i] == ' ' || s[i] == '\t')) {
		w += (s[i] == '\t') ? e->o.tabstop - (w % e->o.tabstop) : 1;
		i++;
	}
	return w;
}

static void shift_lines(ed_t *e, size_t y0, size_t y1, int dir)
{
	buf_t *b = B(e);
	for (size_t y = y0; y <= y1 && y < b->n; y++) {
		const char *s = buf_line(b, y);
		size_t len = buf_linelen(b, y);
		size_t i = 0;
		while (i < len && (s[i] == ' ' || s[i] == '\t'))
			i++;
		/* A blank line is left alone. Indenting whitespace-only lines fills a
		 * diff with changes to lines nobody touched. */
		if (i >= len)
			continue;
		int w = leading_width(e, y) + dir * e->o.shiftwidth;
		if (w < 0)
			w = 0;
		char *ind = indent_string(e, w);
		char *line = xasprintf("%s%s", ind, s + i);
		set_line(e, y, line);
		free(line);
		free(ind);
	}
}

static void case_range(ed_t *e, int how, size_t y0, size_t x0,
                       size_t y1, size_t x1, bool linewise)
{
	buf_t *b = B(e);
	for (size_t y = y0; y <= y1 && y < b->n; y++) {
		size_t len = buf_linelen(b, y);
		size_t s = (!linewise && y == y0) ? x0 : 0;
		size_t t = (!linewise && y == y1) ? x1 : len;
		if (t > len) t = len;
		if (s > t) continue;
		char *copy = xstrndup(buf_line(b, y), len);
		for (size_t i = s; i < t; i++) {
			unsigned char c = (unsigned char)copy[i];
			if (how == 'u')
				copy[i] = (char)tolower(c);
			else if (how == 'U')
				copy[i] = (char)toupper(c);
			else
				copy[i] = isupper(c) ? (char)tolower(c)
				        : islower(c) ? (char)toupper(c) : (char)c;
		}
		set_line(e, y, copy);
		free(copy);
	}
}

/* gc — toggle comments. Not a vim built-in, but this is a Kate replacement
 * as much as a vim one, and "comment out what I selected" is the single most
 * used command in that editor. The whole range is UNcommented only when every
 * non-blank line in it is already a comment; otherwise it is commented. A
 * per-line toggle would uncomment half a block and comment the other half. */
static void comment_toggle(ed_t *e, size_t y0, size_t y1)
{
	buf_t *b = B(e);
	const char *pfx = syn_comment_prefix(b->lang);
	if (!pfx || !*pfx) {
		ed_message(e, true, "no comment syntax for this file type");
		return;
	}
	size_t plen = strlen(pfx);

	bool all_commented = true;
	int min_ind = -1;
	for (size_t y = y0; y <= y1 && y < b->n; y++) {
		if (blank_line(b, y))
			continue;
		size_t i = line_first_nonblank(b, y);
		const char *s = buf_line(b, y);
		if (strncmp(s + i, pfx, plen) != 0)
			all_commented = false;
		int w = leading_width(e, y);
		if (min_ind < 0 || w < min_ind)
			min_ind = w;
	}
	if (min_ind < 0)
		return;

	for (size_t y = y0; y <= y1 && y < b->n; y++) {
		if (blank_line(b, y))
			continue;
		const char *s = buf_line(b, y);
		size_t i = line_first_nonblank(b, y);
		char *line;
		if (all_commented) {
			size_t skip = i + plen;
			/* The space this program inserted after the prefix comes back
			 * off, and one the user wrote does not — indistinguishable, so
			 * exactly one is removed. */
			if (buf_line(b, y)[skip] == ' ')
				skip++;
			char *head = xstrndup(s, i);
			line = xasprintf("%s%s", head, s + skip);
			free(head);
		} else {
			char *ind = indent_string(e, min_ind);
			size_t at = 0;
			int w = 0;
			size_t len = buf_linelen(b, y);
			while (at < len && w < min_ind) {
				w += (s[at] == '\t') ? e->o.tabstop - (w % e->o.tabstop) : 1;
				at++;
			}
			line = xasprintf("%s%s %s", ind, pfx, s + at);
			free(ind);
		}
		set_line(e, y, line);
		free(line);
	}
}

static void apply_op(ed_t *e, int op, int opg, size_t y0, size_t x0,
                     size_t y1, size_t x1, bool linewise, int reg)
{
	buf_t *b = B(e);
	if (y1 >= b->n)
		y1 = b->n - 1;

	buf_group_begin(b);

	if (op == 'y') {
		char *text = range_text(e, y0, x0, y1, x1, linewise);
		reg_set(e, reg ? reg : '0', text, linewise);
		if (!reg) {
			/* A yank fills "0 as well as the unnamed register, so that a
			 * later delete does not cost you the thing you yanked. */
			reg_set(e, '"', text, linewise);
		}
		free(text);
		e->cy = y0;
		e->cx = linewise ? e->cx : x0;
		ed_clamp(e);
		buf_group_end(b);
		return;
	}

	if (op == '>' || op == '<') {
		shift_lines(e, y0, y1, op == '>' ? 1 : -1);
		e->cy = y0;
		e->cx = line_first_nonblank(b, y0);
		buf_group_end(b);
		return;
	}

	if (op == 'g' && (opg == 'u' || opg == 'U' || opg == '~')) {
		case_range(e, opg, y0, x0, y1, x1, linewise);
		e->cy = y0;
		e->cx = linewise ? e->cx : x0;
		ed_clamp(e);
		buf_group_end(b);
		return;
	}

	if (op == 'g' && opg == 'c') {
		comment_toggle(e, y0, y1);
		e->cy = y0;
		ed_clamp(e);
		buf_group_end(b);
		return;
	}

	/* d and c */
	char *text = range_text(e, y0, x0, y1, x1, linewise);
	/* A delete of less than a line goes to the small-delete register, and
	 * anything larger shifts "1–"9 along. That is what makes "1p …"2p able to
	 * recover the last nine line deletions. */
	if (linewise || y0 != y1) {
		/* Only an unnamed delete shifts the ring along. "add into "a should
		 * not silently push "1 down to "2 — the numbered registers are the
		 * history of what was thrown away without being named, and mixing
		 * named deletes into it makes "1p…"9p unpredictable. */
		if (!reg) {
			free(e->regs[reg_index('9')].text);
			for (int i = 9; i > 1; i--)
				e->regs[reg_index('0' + i)] = e->regs[reg_index('0' + i - 1)];
			e->regs[reg_index('1')].text = NULL;
		}
		reg_set(e, reg ? reg : '1', text, linewise);
	} else {
		reg_set(e, reg ? reg : '-', text, linewise);
	}
	if (!reg)
		reg_set(e, '"', text, linewise);
	free(text);

	if (op == 'c' && linewise) {
		/* cc keeps the indentation, which is the entire reason to use it
		 * rather than S — and with autoindent off it does not.
		 *
		 * ⚠ The LITERAL leading whitespace is copied, not a width fed back
		 * through indent_string(). Re-deriving it converts the indent to
		 * whatever this buffer's tabstop/expandtab say, so `cc` on a
		 * space-indented line in a file that uses tabs silently retabs it —
		 * one line of unrelated diff every time somebody changes a line. */
		size_t ind_len = 0;
		if (e->o.autoindent) {
			const char *s0 = buf_line(b, y0);
			size_t l0 = buf_linelen(b, y0);
			while (ind_len < l0 && (s0[ind_len] == ' ' || s0[ind_len] == '\t'))
				ind_len++;
		}
		char *ind = xstrndup(buf_line(b, y0), ind_len);
		char *one[1] = { ind };
		buf_splice(b, y0, y1 - y0 + 1, one, 1, e->cy, e->cx);
		free(ind);
		e->cy = y0;
		e->cx = buf_linelen(b, y0);
		e->mode = M_INSERT;
		e->ins_y = e->cy;
		e->ins_x = e->cx;
		return;             /* group stays open across the insert */
	}

	range_delete(e, y0, x0, y1, x1, linewise);

	e->cy = y0;
	if (e->cy >= b->n)
		e->cy = b->n - 1;
	e->cx = linewise ? line_first_nonblank(b, e->cy) : x0;

	if (op == 'c') {
		e->mode = M_INSERT;
		e->ins_y = e->cy;
		e->ins_x = e->cx;
		return;
	}

	ed_clamp(e);
	buf_group_end(b);
}

/* ── put ────────────────────────────────────────────────────────────────── */

static void do_put(ed_t *e, bool after, long count, int reg)
{
	const reg_t *r = reg_get(e, reg ? reg : '"');
	if (!r || !r->text || !*r->text) {
		ed_message(e, true, "nothing to put");
		return;
	}
	buf_t *b = B(e);
	long n = count > 0 ? count : 1;

	buf_group_begin(b);

	if (r->linewise) {
		/* Split the register into lines. A trailing newline is the marker
		 * that made it linewise and does not become an extra blank line. */
		size_t tlen = strlen(r->text);
		char *copy = xstrndup(r->text, tlen && r->text[tlen - 1] == '\n'
		                               ? tlen - 1 : tlen);
		size_t nl = 1;
		for (char *p = copy; *p; p++)
			if (*p == '\n')
				nl++;

		size_t total = nl * (size_t)n;
		char **lines = xmalloc(total * sizeof *lines);
		size_t k = 0;
		for (long i = 0; i < n; i++) {
			char *seg = copy;
			for (size_t j = 0; j < nl; j++) {
				char *end = strchr(seg, '\n');
				lines[k++] = xstrndup(seg, end ? (size_t)(end - seg) : strlen(seg));
				seg = end ? end + 1 : seg + strlen(seg);
			}
		}
		size_t at = after ? e->cy + 1 : e->cy;
		buf_splice(b, at, 0, lines, total, e->cy, e->cx);
		for (size_t i = 0; i < total; i++)
			free(lines[i]);
		free(lines);
		free(copy);
		e->cy = at;
		e->cx = line_first_nonblank(b, e->cy);
	} else {
		size_t len = buf_linelen(b, e->cy);
		size_t at = after ? (len ? e->cx + 1 : 0) : e->cx;
		if (at > len)
			at = len;
		size_t ey = e->cy, ex = at;
		for (long i = 0; i < n; i++)
			insert_text_at(e, ey, ex, r->text, &ey, &ex);
		e->cy = ey;
		e->cx = ex ? ex - 1 : 0;
	}

	buf_group_end(b);
	ed_clamp(e);
}

/* ── join ───────────────────────────────────────────────────────────────── */

static void do_join(ed_t *e, long count, bool spaced)
{
	buf_t *b = B(e);
	long n = count > 1 ? count - 1 : 1;
	buf_group_begin(b);
	for (long i = 0; i < n; i++) {
		if (e->cy + 1 >= b->n)
			break;
		const char *cur = buf_line(b, e->cy);
		size_t clen = buf_linelen(b, e->cy);
		const char *nxt = buf_line(b, e->cy + 1);
		size_t j = 0;
		if (spaced)
			while (nxt[j] == ' ' || nxt[j] == '\t')
				j++;

		/* One space between them, unless the first line already ends in
		 * whitespace or the next begins with a closing bracket. */
		bool sep = spaced && clen > 0 && cur[clen - 1] != ' '
		           && cur[clen - 1] != '\t' && nxt[j] != ')' && nxt[j] != '\0';
		char *joined = xasprintf("%.*s%s%s", (int)clen, cur, sep ? " " : "", nxt + j);
		char *one[1] = { joined };
		buf_splice(b, e->cy, 2, one, 1, e->cy, e->cx);
		e->cx = clen;
		free(joined);
	}
	buf_group_end(b);
	ed_clamp(e);
}

/* ── insert mode ────────────────────────────────────────────────────────── */

static void insert_newline(ed_t *e)
{
	buf_t *b = B(e);
	const char *s = buf_line(b, e->cy);
	size_t len = buf_linelen(b, e->cy);
	size_t x = e->cx > len ? len : e->cx;

	char *ind = xstrdup("");
	if (e->o.autoindent) {
		size_t i = 0;
		while (i < len && i < x && (s[i] == ' ' || s[i] == '\t'))
			i++;
		free(ind);
		ind = xstrndup(s, i);
	}

	char *head = xstrndup(s, x);
	char *tail = xasprintf("%s%s", ind, s + x);
	char *two[2] = { head, tail };
	buf_splice(b, e->cy, 1, two, 2, e->cy, e->cx);
	e->cy++;
	e->cx = strlen(ind);
	free(head);
	free(tail);
	free(ind);
}

static void insert_byte(ed_t *e, int k)
{
	const buf_t *b = B(e);
	const char *s = buf_line(b, e->cy);
	size_t len = buf_linelen(b, e->cy);
	size_t x = e->cx > len ? len : e->cx;

	/* Replace mode overwrites, except at the end of the line where there is
	 * nothing to overwrite. */
	bool overwrite = (e->mode == M_REPLACE) && x < len;

	char *line = xmalloc(len + 2);
	memcpy(line, s, x);
	line[x] = (char)k;
	memcpy(line + x + 1, s + x + (overwrite ? 1 : 0),
	       len - x - (overwrite ? 1 : 0));
	line[len + (overwrite ? 0 : 1)] = '\0';

	set_line(e, e->cy, line);
	free(line);
	e->cx = x + 1;
}

static void insert_tab(ed_t *e)
{
	if (!e->o.expandtab) {
		insert_byte(e, '\t');
		return;
	}
	int w = (int)disp_col(buf_line(e->buf[e->cur], e->cy), e->cx, e->o.tabstop);
	int n = e->o.shiftwidth - (w % e->o.shiftwidth);
	if (n <= 0)
		n = e->o.shiftwidth;
	for (int i = 0; i < n; i++)
		insert_byte(e, ' ');
}

static void insert_backspace(ed_t *e)
{
	buf_t *b = B(e);
	if (e->cx > 0) {
		const char *s = buf_line(b, e->cy);
		size_t len = buf_linelen(b, e->cy);
		/* One CHARACTER, not one byte: backspacing through a multi-byte
		 * character one byte at a time leaves the line invalid and draws as
		 * a replacement glyph that then takes three more presses. */
		size_t start = e->cx - 1;
		while (start > 0 && ((unsigned char)s[start] & 0xc0) == 0x80)
			start--;
		char *line = xmalloc(len + 1);
		memcpy(line, s, start);
		memcpy(line + start, s + e->cx, len - e->cx);
		line[len - (e->cx - start)] = '\0';
		set_line(e, e->cy, line);
		free(line);
		e->cx = start;
	} else if (e->cy > 0) {
		size_t prev = buf_linelen(b, e->cy - 1);
		char *joined = xasprintf("%s%s", buf_line(b, e->cy - 1), buf_line(b, e->cy));
		char *one[1] = { joined };
		buf_splice(b, e->cy - 1, 2, one, 1, e->cy, e->cx);
		free(joined);
		e->cy--;
		e->cx = prev;
	}
}

static void leave_insert(ed_t *e)
{
	buf_t *b = B(e);

	/* 3ifoo<Esc> inserts foo three times. The first copy went in as it was
	 * typed; the rest are replayed here, which is also what makes the count
	 * survive being replayed by `.`. */
	if (e->ins_count > 1) {
		char *typed = NULL;
		if (e->ins_y == e->cy && e->cx >= e->ins_x) {
			typed = xstrndup(buf_line(b, e->cy) + e->ins_x, e->cx - e->ins_x);
		}
		if (typed && *typed) {
			size_t ey = e->cy, ex = e->cx;
			for (long i = 1; i < e->ins_count; i++)
				insert_text_at(e, ey, ex, typed, &ey, &ex);
			e->cy = ey;
			e->cx = ex;
		}
		free(typed);
	}
	e->ins_count = 0;

	e->mode = M_NORMAL;
	if (e->cx > 0)
		e->cx--;
	ed_clamp(e);
	buf_group_end(b);
}

/* ── the dot register ───────────────────────────────────────────────────── */

static void dot_append(ed_t *e, int k)
{
	if (e->replaying)
		return;
	const char *nm = key_name(k);
	size_t n = strlen(nm);
	if (e->ndotpend + n < sizeof e->dotpend) {
		memcpy(e->dotpend + e->ndotpend, nm, n);
		e->ndotpend += n;
	}
}

static void dot_commit(ed_t *e)
{
	if (e->replaying || e->ndotpend == 0)
		return;
	size_t n = e->ndotpend < sizeof e->dot - 1 ? e->ndotpend : sizeof e->dot - 1;
	memcpy(e->dot, e->dotpend, n);
	e->dot[n] = '\0';
	e->ndot = n;
}

/* ── visual mode ────────────────────────────────────────────────────────── */

bool ed_selection(const ed_t *e, size_t *y0, size_t *x0, size_t *y1, size_t *x1)
{
	if (e->mode != M_VISUAL && e->mode != M_VISUAL_LINE
	                        && e->mode != M_VISUAL_BLOCK)
		return false;
	size_t ay = e->vy, ax = e->vx, by = e->cy, bx = e->cx;
	if (ay > by || (ay == by && ax > bx)) {
		size_t t;
		t = ay; ay = by; by = t;
		t = ax; ax = bx; bx = t;
	}
	*y0 = ay; *x0 = ax; *y1 = by; *x1 = bx;
	return true;
}

/* ── the normal-mode dispatcher ─────────────────────────────────────────── */

static void reset_pending(ed_t *e)
{
	e->count = e->opcount = 0;
	e->op = e->opg = e->prefix = 0;
	e->reg = 0;
	e->npend = 0;
}

static long total_count(const ed_t *e)
{
	long a = e->opcount > 0 ? e->opcount : 1;
	long b = e->count > 0 ? e->count : 1;
	long n = a * b;
	return (e->opcount > 0 || e->count > 0) ? n : 0;
}

static void start_insert(ed_t *e, long count)
{
	e->mode = M_INSERT;
	e->ins_count = count > 0 ? count : 1;
	e->ins_y = e->cy;
	e->ins_x = e->cx;
	buf_group_begin(B(e));
}

/* Applies a completed operator+motion pair. */
static void op_with_motion(ed_t *e, const mot_t *m)
{
	buf_t *b = B(e);
	size_t y0 = e->cy, x0 = e->cx, y1 = m->y, x1 = m->x;
	bool linewise = (m->kind == MK_LINE);

	if (y0 > y1 || (y0 == y1 && x0 > x1)) {
		size_t t;
		t = y0; y0 = y1; y1 = t;
		t = x0; x0 = x1; x1 = t;
	}

	if (!linewise) {
		if (m->kind == MK_INCL)
			x1++;
		size_t l1 = buf_linelen(b, y1);
		if (x1 > l1)
			x1 = l1;
	}

	apply_op(e, e->op, e->opg, y0, x0, y1, x1, linewise, e->reg);
}

static void normal_key(ed_t *e, int k);

/* `f`, `t`, `r`, `m`, `"`, `'`, `` ` ``, `q`, `@` and the two-key `g`/`z`
 * commands all need the NEXT key before they can do anything. */
static void prefix_key(ed_t *e, int k)
{
	buf_t *b = B(e);
	int pfx = e->prefix;
	e->prefix = 0;

	if (k == K_ESC) {
		reset_pending(e);
		return;
	}

	switch (pfx) {
	case '"':
		e->reg = k;
		return;

	case 'f': case 'F': case 't': case 'T': {
		e->last_ft = k;
		e->last_ft_cmd = pfx;
		mot_t m;
		long n = total_count(e);
		if (!do_motion(e, pfx, n, k, &m) || !m.ok) {
			reset_pending(e);
			return;
		}
		if (e->op) {
			op_with_motion(e, &m);
			dot_commit(e);
		} else if (e->mode == M_VISUAL || e->mode == M_VISUAL_LINE
		                                || e->mode == M_VISUAL_BLOCK) {
			e->cy = m.y;
			e->cx = m.x;
			ed_clamp(e);
		} else {
			e->cy = m.y;
			e->cx = m.x;
			ed_clamp(e);
		}
		reset_pending(e);
		return;
	}

	case 'r': {
		if (k > 0xff)
			break;
		/* In visual mode r replaces every selected character, which is the
		 * only way to blank out a block without retyping it. */
		if (e->mode == M_VISUAL || e->mode == M_VISUAL_LINE) {
			size_t y0, x0, y1, x1;
			ed_selection(e, &y0, &x0, &y1, &x1);
			bool lw = (e->mode != M_VISUAL);
			e->mode = M_NORMAL;
			buf_group_begin(b);
			for (size_t y = y0; y <= y1 && y < b->n; y++) {
				size_t len = buf_linelen(b, y);
				size_t s = (!lw && y == y0) ? x0 : 0;
				size_t t = (!lw && y == y1) ? (x1 + 1 > len ? len : x1 + 1) : len;
				if (s >= t)
					continue;
				char *line = xstrndup(buf_line(b, y), len);
				for (size_t i = s; i < t; i++)
					line[i] = (char)k;
				set_line(e, y, line);
				free(line);
			}
			buf_group_end(b);
			e->cy = y0;
			e->cx = lw ? 0 : x0;
			ed_clamp(e);
			dot_commit(e);
			reset_pending(e);
			return;
		}
		long n = total_count(e);
		if (n <= 0)
			n = 1;
		size_t len = buf_linelen(b, e->cy);
		if (e->cx + (size_t)n > len) {
			/* r refuses rather than replacing what it can — replacing three
			 * characters when two were asked for silently is worse than
			 * doing nothing. */
			reset_pending(e);
			return;
		}
		buf_group_begin(b);
		char *line = xstrndup(buf_line(b, e->cy), len);
		for (long i = 0; i < n; i++)
			line[e->cx + (size_t)i] = (char)k;
		set_line(e, e->cy, line);
		free(line);
		buf_group_end(b);
		e->cx += (size_t)(n - 1);
		ed_clamp(e);
		dot_commit(e);
		reset_pending(e);
		return;
	}

	case 'm': {
		int idx = (k >= 'a' && k <= 'z') ? k - 'a' : -1;
		if (idx >= 0) {
			b->mark[idx] = e->cy + 1;
			b->markx[idx] = e->cx;
		}
		reset_pending(e);
		return;
	}

	case '\'': case '`': {
		int idx = (k >= 'a' && k <= 'z') ? k - 'a' : (k == '\'' || k == '`') ? 26 : -1;
		if (idx < 0 || b->mark[idx] == 0) {
			ed_message(e, true, "mark not set");
			reset_pending(e);
			return;
		}
		mot_t m;
		m.y = b->mark[idx] - 1;
		if (m.y >= b->n)
			m.y = b->n - 1;
		m.x = (pfx == '`') ? b->markx[idx] : line_first_nonblank(b, m.y);
		m.kind = (pfx == '`') ? MK_EXCL : MK_LINE;
		m.ok = true;
		m.keep_col = false;
		m.to_first_nb = (pfx == '\'');
		if (e->op) {
			op_with_motion(e, &m);
			dot_commit(e);
		} else {
			/* Jumping sets the '' mark to where you came from, which is what
			 * makes '' hop back and forth. */
			b->mark[26] = e->cy + 1;
			b->markx[26] = e->cx;
			e->cy = m.y;
			e->cx = m.x;
			ed_clamp(e);
		}
		reset_pending(e);
		return;
	}

	case 'g': {
		long n = total_count(e);
		switch (k) {
		case 'g': {
			mot_t m;
			m.y = e->count > 0 ? (size_t)(e->count - 1) : 0;
			if (m.y >= b->n)
				m.y = b->n - 1;
			m.x = line_first_nonblank(b, m.y);
			m.kind = MK_LINE;
			m.ok = true;
			m.keep_col = false;
			m.to_first_nb = true;
			if (e->op) {
				op_with_motion(e, &m);
				dot_commit(e);
			} else {
				e->cy = m.y;
				e->cx = m.x;
				ed_clamp(e);
			}
			reset_pending(e);
			return;
		}
		case 'u': case 'U': case '~': case 'c':
			/* An operator in its own right: gu waits for a motion. In visual
			 * mode it acts on the selection immediately. */
			if (e->mode == M_VISUAL || e->mode == M_VISUAL_LINE
			                        || e->mode == M_VISUAL_BLOCK) {
				size_t y0, x0, y1, x1;
				ed_selection(e, &y0, &x0, &y1, &x1);
				bool lw = (e->mode != M_VISUAL);
				if (!lw) {
					size_t l1 = buf_linelen(b, y1);
					x1 = x1 + 1 > l1 ? l1 : x1 + 1;
				}
				e->mode = M_NORMAL;
				apply_op(e, 'g', k, y0, x0, y1, x1, lw, e->reg);
				dot_commit(e);
				reset_pending(e);
				return;
			}
			e->op = 'g';
			e->opg = k;
			e->opcount = n;
			e->count = 0;
			return;
		case 'J':
			do_join(e, n, false);
			dot_commit(e);
			reset_pending(e);
			return;
		case 'v':
			ed_message(e, false, "gv: no previous selection");
			reset_pending(e);
			return;
		default:
			reset_pending(e);
			return;
		}
	}

	case 'z':
		/* Scrolling is the front-end's business; the engine records the
		 * request so the TUI and the GUI centre the same way. */
		if (k == 'z' || k == 't' || k == 'b')
			ed_message(e, false, "z%c", (char)k);
		reset_pending(e);
		return;

	case 'Z':
		if (k == 'Z') {
			char *err = NULL;
			if (b->modified && !buf_save(b, NULL, &err)) {
				ed_message(e, true, "%s", err ? err : "write failed");
				free(err);
			} else {
				e->quit = true;
			}
		} else if (k == 'Q') {
			e->quit = true;
		}
		reset_pending(e);
		return;

	case 'q': {
		/* q<reg> starts recording; the stopping q is handled before we get
		 * here, in ed_key. */
		int idx = reg_index(k);
		if (idx >= 0 && k >= 'a' && k <= 'z') {
			e->rec_reg = k;
			e->nrec = 0;
			ed_message(e, false, "recording @%c", (char)k);
		}
		reset_pending(e);
		return;
	}

	case '@': {
		int use = (k == '@') ? e->last_macro : k;
		if (use == 0) {
			ed_message(e, true, "no previously played register");
			reset_pending(e);
			return;
		}
		e->last_macro = use;
		const reg_t *r = reg_get(e, use);
		if (!r || !r->text) {
			ed_message(e, true, "register %c is empty", (char)use);
			reset_pending(e);
			return;
		}
		/* ⚠ The count has to be read BEFORE the pending state is cleared —
		 * reset_pending zeroes it, so 2@a played the macro once. */
		long n = total_count(e);
		char *copy = xstrdup(r->text);
		reset_pending(e);
		bool was = e->replaying;
		e->replaying = true;
		for (long i = 0; i < (n > 0 ? n : 1); i++)
			ed_keys(e, copy);
		e->replaying = was;
		free(copy);
		return;
	}

	case 'i': case 'a': {
		/* A text object, only meaningful with an operator or in visual. */
		size_t y0, x0, y1, x1;
		bool lw;
		if (!textobj(e, pfx == 'a', k, &y0, &x0, &y1, &x1, &lw)) {
			reset_pending(e);
			return;
		}
		if (e->mode == M_VISUAL || e->mode == M_VISUAL_LINE) {
			e->vy = y0;
			e->vx = x0;
			e->cy = y1;
			e->cx = x1 ? x1 - 1 : 0;
			if (lw)
				e->mode = M_VISUAL_LINE;
			ed_clamp(e);
			reset_pending(e);
			return;
		}
		if (e->op) {
			apply_op(e, e->op, e->opg, y0, x0, y1, x1, lw, e->reg);
			dot_commit(e);
		}
		reset_pending(e);
		return;
	}
	}

	reset_pending(e);
}

static void visual_or_move(ed_t *e, const mot_t *m)
{
	if (m->kind == MK_LINE && !m->keep_col) {
		e->cy = m->y;
		e->cx = m->to_first_nb ? line_first_nonblank(B(e), m->y) : e->cx;
	} else if (m->kind == MK_LINE && m->keep_col) {
		/* j and k keep the column you were aiming for, so moving through a
		 * short line and out the other side returns to where you were. */
		e->cy = m->y;
		size_t len = buf_linelen(B(e), e->cy);
		size_t want = e->want_col;
		e->cx = want < len ? want : (len ? len - 1 : 0);
		ed_clamp(e);
		return;
	} else {
		e->cy = m->y;
		e->cx = m->x;
	}
	ed_clamp(e);
	e->want_col = e->cx;
}

static void normal_key(ed_t *e, int k)
{
	buf_t *b = B(e);
	bool visual = (e->mode == M_VISUAL || e->mode == M_VISUAL_LINE
	                                   || e->mode == M_VISUAL_BLOCK);

	if (e->prefix) {
		prefix_key(e, k);
		return;
	}

	/* counts. A leading 0 is the motion, not a digit. */
	if (k >= '0' && k <= '9' && !(k == '0' && e->count == 0)) {
		e->count = e->count * 10 + (k - '0');
		if (e->count > 100000000L)
			e->count = 100000000L;
		return;
	}

	long n = total_count(e);

	/* ⚠ The keys that need ANOTHER key before they mean anything are claimed
	 * BEFORE the motion table is consulted.
	 *
	 * f, F, t and T are in that table — they have to be, so that `df,` works
	 * — but they cannot be dispatched from it, because the character to find
	 * has not been typed yet. Asking do_motion first made `fa` search for
	 * byte 0, fail, and discard the pending state, so the `a` that followed
	 * was read as "append" and the rest of the command was typed into the
	 * buffer. `di(` after an `fa` inserted the literal text "di(". */
	if (k == '"' || k == 'f' || k == 'F' || k == 't' || k == 'T'
	 || k == 'r' || k == 'm' || k == '\'' || k == '`' || k == 'g'
	 || k == 'z' || k == 'Z' || k == 'q' || k == '@') {
		if (k == 'q' && e->rec_reg) {
			/* The q that stops recording. The key itself must not be part of
			 * the recording, which is why ed_key catches it before the
			 * append; here it only has to clear the state. */
			ed_message(e, false, "recorded @%c", (char)e->rec_reg);
			e->rec_reg = 0;
			reset_pending(e);
			return;
		}
		e->prefix = k;
		return;
	}

	/* motions next, so an operator waiting for one gets it */
	mot_t m;
	if (do_motion(e, k, n, 0, &m)) {
		if (!m.ok) {
			reset_pending(e);
			return;
		}
		if (e->op) {
			/* cw acts like ce when the cursor is on a word character — the
			 * one special case in vim's motion table, and leaving it out
			 * makes every `cw` eat the following space. */
			if (e->op == 'c' && (k == 'w' || k == 'W')
			    && class_at(b, e->cy, e->cx, k == 'W') != 0) {
				mot_t em;
				do_motion(e, k == 'w' ? 'e' : 'E', n, 0, &em);
				if (em.ok)
					m = em;
			}
			op_with_motion(e, &m);
			dot_commit(e);
			reset_pending(e);
			return;
		}
		visual_or_move(e, &m);
		reset_pending(e);
		return;
	}

	switch (k) {
	case K_ESC:
		if (visual)
			e->mode = M_NORMAL;
		reset_pending(e);
		ed_clamp(e);
		return;

	case 'i': case 'a':
		if (visual || e->op) {
			e->prefix = k;       /* a text object follows */
			return;
		}
		if (k == 'a' && buf_linelen(b, e->cy) > 0)
			e->cx++;
		start_insert(e, n);
		e->ins_cmd = k;
		reset_pending(e);
		return;

	case 'I':
		if (visual) { reset_pending(e); return; }
		e->cx = line_first_nonblank(b, e->cy);
		start_insert(e, n);
		reset_pending(e);
		return;

	case 'A':
		if (visual) { reset_pending(e); return; }
		e->cx = buf_linelen(b, e->cy);
		start_insert(e, n);
		reset_pending(e);
		return;

	case 'o': case 'O': {
		if (visual) { reset_pending(e); return; }
		buf_group_begin(b);
		char *ind = xstrdup("");
		if (e->o.autoindent) {
			free(ind);
			size_t i = line_first_nonblank(b, e->cy);
			if (blank_line(b, e->cy))
				i = 0;
			ind = xstrndup(buf_line(b, e->cy), i);
		}
		char *one[1] = { ind };
		size_t at = (k == 'o') ? e->cy + 1 : e->cy;
		buf_splice(b, at, 0, one, 1, e->cy, e->cx);
		e->cy = at;
		e->cx = strlen(ind);
		free(ind);
		e->mode = M_INSERT;
		e->ins_count = n > 0 ? n : 1;
		e->ins_y = e->cy;
		e->ins_x = e->cx;
		reset_pending(e);
		return;
	}

	case 'v':
		if (e->mode == M_VISUAL) {
			e->mode = M_NORMAL;
		} else if (visual) {
			e->mode = M_VISUAL;
		} else {
			e->mode = M_VISUAL;
			e->vy = e->cy;
			e->vx = e->cx;
		}
		reset_pending(e);
		return;

	case 'V':
		if (e->mode == M_VISUAL_LINE) {
			e->mode = M_NORMAL;
		} else if (visual) {
			e->mode = M_VISUAL_LINE;
		} else {
			e->mode = M_VISUAL_LINE;
			e->vy = e->cy;
			e->vx = e->cx;
		}
		reset_pending(e);
		return;

	case 'd': case 'c': case 'y': case '<': case '>':
		if (visual) {
			size_t y0, x0, y1, x1;
			ed_selection(e, &y0, &x0, &y1, &x1);
			bool lw = (e->mode != M_VISUAL);
			if (!lw) {
				size_t l1 = buf_linelen(b, y1);
				x1 = x1 + 1 > l1 ? l1 : x1 + 1;
			}
			int op = k;
			e->mode = M_NORMAL;
			apply_op(e, op, 0, y0, x0, y1, x1, lw, e->reg);
			dot_commit(e);
			reset_pending(e);
			return;
		}
		if (e->op == k) {
			/* dd, cc, yy, >>, << — the operator doubled means this line. */
			size_t y0 = e->cy;
			size_t y1 = e->cy + (size_t)(n > 0 ? n : 1) - 1;
			if (y1 >= b->n)
				y1 = b->n - 1;
			apply_op(e, e->op, e->opg, y0, 0, y1, 0, true, e->reg);
			dot_commit(e);
			reset_pending(e);
			return;
		}
		e->op = k;
		e->opcount = n;
		e->count = 0;
		return;

	case 'x': case K_DEL: {
		if (visual) {
			normal_key(e, 'd');
			return;
		}
		size_t len = buf_linelen(b, e->cy);
		if (len == 0) { reset_pending(e); return; }
		size_t cnt = (size_t)(n > 0 ? n : 1);
		size_t end = e->cx + cnt;
		if (end > len)
			end = len;
		buf_group_begin(b);
		char *text = range_text(e, e->cy, e->cx, e->cy, end, false);
		reg_set(e, e->reg ? e->reg : '-', text, false);
		if (!e->reg)
			reg_set(e, '"', text, false);
		free(text);
		range_delete(e, e->cy, e->cx, e->cy, end, false);
		buf_group_end(b);
		ed_clamp(e);
		dot_commit(e);
		reset_pending(e);
		return;
	}

	case 'X': {
		if (visual) { normal_key(e, 'd'); return; }
		if (e->cx == 0) { reset_pending(e); return; }
		size_t cnt = (size_t)(n > 0 ? n : 1);
		size_t s = cnt > e->cx ? 0 : e->cx - cnt;
		buf_group_begin(b);
		char *text = range_text(e, e->cy, s, e->cy, e->cx, false);
		reg_set(e, e->reg ? e->reg : '-', text, false);
		if (!e->reg)
			reg_set(e, '"', text, false);
		free(text);
		range_delete(e, e->cy, s, e->cy, e->cx, false);
		buf_group_end(b);
		e->cx = s;
		ed_clamp(e);
		dot_commit(e);
		reset_pending(e);
		return;
	}

	case 'D': case 'C': {
		size_t len = buf_linelen(b, e->cy);
		apply_op(e, k == 'D' ? 'd' : 'c', 0, e->cy, e->cx, e->cy, len, false,
		         e->reg);
		dot_commit(e);
		reset_pending(e);
		return;
	}

	case 's': {
		if (visual) {
			normal_key(e, 'c');
			return;
		}
		size_t len = buf_linelen(b, e->cy);
		size_t end = e->cx + (size_t)(n > 0 ? n : 1);
		if (end > len)
			end = len;
		apply_op(e, 'c', 0, e->cy, e->cx, e->cy, end, false, e->reg);
		reset_pending(e);
		return;
	}

	case 'S':
		if (visual) { normal_key(e, 'c'); return; }
		{
			size_t y1 = e->cy + (size_t)(n > 0 ? n : 1) - 1;
			if (y1 >= b->n)
				y1 = b->n - 1;
			apply_op(e, 'c', 0, e->cy, 0, y1, 0, true, e->reg);
		}
		reset_pending(e);
		return;

	case 'Y':
		apply_op(e, 'y', 0, e->cy, 0,
		         e->cy + (size_t)(n > 0 ? n : 1) - 1, 0, true, e->reg);
		reset_pending(e);
		return;

	case 'p': case 'P':
		if (visual) {
			/* Replacing a selection with the register: the deleted text takes
			 * the unnamed register with it in vim, which surprises everyone.
			 * Here the register being pasted is read FIRST. */
			const reg_t *r = reg_get(e, e->reg ? e->reg : '"');
			char *keep = r && r->text ? xstrdup(r->text) : NULL;
			bool lw = r ? r->linewise : false;
			size_t y0, x0, y1, x1;
			ed_selection(e, &y0, &x0, &y1, &x1);
			bool sel_lw = (e->mode != M_VISUAL);
			if (!sel_lw) {
				size_t l1 = buf_linelen(b, y1);
				x1 = x1 + 1 > l1 ? l1 : x1 + 1;
			}
			e->mode = M_NORMAL;
			buf_group_begin(b);
			apply_op(e, 'd', 0, y0, x0, y1, x1, sel_lw, 0);
			if (keep) {
				reg_set(e, '"', keep, lw);
				/* Before the cursor in both cases: after the delete the
				 * cursor sits exactly where the selection was, so "after"
				 * would put the text one position past the hole it is
				 * meant to fill. */
				do_put(e, false, 1, 0);
				free(keep);
			}
			buf_group_end(b);
			dot_commit(e);
			reset_pending(e);
			return;
		}
		do_put(e, k == 'p', n, e->reg);
		dot_commit(e);
		reset_pending(e);
		return;

	case 'J':
		if (visual) {
			size_t y0, x0, y1, x1;
			ed_selection(e, &y0, &x0, &y1, &x1);
			e->mode = M_NORMAL;
			e->cy = y0;
			do_join(e, (long)(y1 - y0 + 1), true);
			dot_commit(e);
			reset_pending(e);
			return;
		}
		do_join(e, n, true);
		dot_commit(e);
		reset_pending(e);
		return;

	case 'u':
		if (visual) { reset_pending(e); return; }
		for (long i = 0; i < (n > 0 ? n : 1); i++) {
			size_t cy = e->cy, cx = e->cx;
			if (!buf_undo(b, &cy, &cx)) {
				ed_message(e, false, "already at oldest change");
				break;
			}
			e->cy = cy;
			e->cx = cx;
		}
		ed_clamp(e);
		reset_pending(e);
		return;

	case 18:    /* Ctrl-R */
		for (long i = 0; i < (n > 0 ? n : 1); i++) {
			size_t cy = e->cy, cx = e->cx;
			if (!buf_redo(b, &cy, &cx)) {
				ed_message(e, false, "already at newest change");
				break;
			}
			e->cy = cy;
			e->cx = cx;
		}
		ed_clamp(e);
		reset_pending(e);
		return;

	case '~': {
		if (visual) {
			size_t y0, x0, y1, x1;
			ed_selection(e, &y0, &x0, &y1, &x1);
			bool lw = (e->mode != M_VISUAL);
			if (!lw) {
				size_t l1 = buf_linelen(b, y1);
				x1 = x1 + 1 > l1 ? l1 : x1 + 1;
			}
			e->mode = M_NORMAL;
			apply_op(e, 'g', '~', y0, x0, y1, x1, lw, 0);
			dot_commit(e);
			reset_pending(e);
			return;
		}
		size_t len = buf_linelen(b, e->cy);
		if (len == 0) { reset_pending(e); return; }
		size_t end = e->cx + (size_t)(n > 0 ? n : 1);
		if (end > len)
			end = len;
		buf_group_begin(b);
		case_range(e, '~', e->cy, e->cx, e->cy, end, false);
		buf_group_end(b);
		e->cx = end < len ? end : (len ? len - 1 : 0);
		dot_commit(e);
		reset_pending(e);
		return;
	}

	case 'R':
		if (visual) { reset_pending(e); return; }
		start_insert(e, n);
		e->mode = M_REPLACE;
		reset_pending(e);
		return;

	case ':': case '/': case '?':
		e->mode = M_CMDLINE;
		e->cmdchar = k;
		e->ncmd = 0;
		e->cmd[0] = '\0';
		if (visual && k == ':') {
			/* :'<,'> — the selection travels into the ex line, which is how
			 * every "do this to what I selected" command is spelled. The
			 * marks are set here and the visual mode ends, because the ex
			 * command runs against MARKS, not against a selection that would
			 * otherwise have to survive the whole command being typed. */
			size_t y0, x0, y1, x1;
			ed_selection(e, &y0, &x0, &y1, &x1);
			b->mark[27] = y0 + 1;
			b->markx[27] = x0;
			b->mark[28] = y1 + 1;
			b->markx[28] = x1;
			e->mode = M_CMDLINE;
			strcpy(e->cmd, "'<,'>");
			e->ncmd = 5;
		}
		return;

	case 'n': case 'N': {
		if (!e->search || !*e->search) {
			ed_message(e, true, "no previous search");
			reset_pending(e);
			return;
		}
		char *err = NULL;
		int dir = (k == 'n') ? e->search_dir : -e->search_dir;
		for (long i = 0; i < (n > 0 ? n : 1); i++) {
			if (!ed_search(e, e->search, dir, &err)) {
				ed_message(e, true, "%s", err ? err : "pattern not found");
				free(err);
				err = NULL;
				break;
			}
		}
		reset_pending(e);
		return;
	}

	case '*': case '#': {
		size_t len = buf_linelen(b, e->cy);
		if (len == 0) { reset_pending(e); return; }
		const char *s = buf_line(b, e->cy);
		size_t st = e->cx, en = e->cx;
		if (cclass((unsigned char)s[st]) != 1) {
			while (st < len && cclass((unsigned char)s[st]) != 1)
				st++;
			if (st >= len) { reset_pending(e); return; }
			en = st;
		}
		while (st > 0 && cclass((unsigned char)s[st - 1]) == 1)
			st--;
		while (en + 1 < len && cclass((unsigned char)s[en + 1]) == 1)
			en++;
		char *word = xstrndup(s + st, en - st + 1);
		free(e->search);
		/* Word boundaries, so * on `count` does not stop on `counter`. */
		e->search = xasprintf("\\<%s\\>", word);
		free(word);
		e->search_dir = (k == '*') ? 1 : -1;
		reg_set(e, '/', e->search, false);
		char *err = NULL;
		if (!ed_search(e, e->search, e->search_dir, &err)) {
			ed_message(e, true, "%s", err ? err : "pattern not found");
			free(err);
		}
		reset_pending(e);
		return;
	}

	case '.':
		if (e->ndot == 0) {
			reset_pending(e);
			return;
		}
		{
			char *copy = xstrndup(e->dot, e->ndot);
			reset_pending(e);
			bool was = e->replaying;
			e->replaying = true;
			ed_keys(e, copy);
			e->replaying = was;
			free(copy);
		}
		return;

	case 4:     /* Ctrl-D */
	case 21: {  /* Ctrl-U */
		size_t half = (e->view_rows ? e->view_rows : 20) / 2;
		if (half == 0)
			half = 1;
		if (k == 4) {
			e->cy = e->cy + half >= b->n ? b->n - 1 : e->cy + half;
			e->view_top = e->view_top + half;
		} else {
			e->cy = half > e->cy ? 0 : e->cy - half;
			e->view_top = half > e->view_top ? 0 : e->view_top - half;
		}
		ed_clamp(e);
		reset_pending(e);
		return;
	}

	case 6:     /* Ctrl-F */
	case 2: {   /* Ctrl-B */
		size_t page = e->view_rows ? e->view_rows : 20;
		if (k == 6)
			e->cy = e->cy + page >= b->n ? b->n - 1 : e->cy + page;
		else
			e->cy = page > e->cy ? 0 : e->cy - page;
		ed_clamp(e);
		reset_pending(e);
		return;
	}

	case K_PGDN: normal_key(e, 6); return;
	case K_PGUP: normal_key(e, 2); return;

	case 7:     /* Ctrl-G — where am I */
		ed_message(e, false, "\"%s\" %zu lines --%d%%--", buf_name(b), b->n,
		           b->n ? (int)((e->cy + 1) * 100 / b->n) : 0);
		reset_pending(e);
		return;

	default:
		reset_pending(e);
		return;
	}
}

/* ── command line ───────────────────────────────────────────────────────── */

static void cmdline_key(ed_t *e, int k)
{
	if (k == K_ESC) {
		e->mode = M_NORMAL;
		e->ncmd = 0;
		e->cmd[0] = '\0';
		return;
	}
	if (k == '\r' || k == '\n') {
		char line[sizeof e->cmd];
		memcpy(line, e->cmd, e->ncmd);
		line[e->ncmd] = '\0';
		int ch = e->cmdchar;
		e->mode = M_NORMAL;
		e->ncmd = 0;
		e->cmd[0] = '\0';

		if (ch == ':') {
			ed_ex(e, line);
		} else {
			if (*line) {
				free(e->search);
				e->search = xstrdup(line);
				reg_set(e, '/', line, false);
			}
			e->search_dir = (ch == '/') ? 1 : -1;
			if (e->search && *e->search) {
				char *err = NULL;
				if (!ed_search(e, e->search, e->search_dir, &err)) {
					ed_message(e, true, "%s", err ? err : "pattern not found");
					free(err);
				}
			}
		}
		ed_clamp(e);
		return;
	}
	if (k == K_BS || k == 8) {
		if (e->ncmd > 0) {
			e->ncmd--;
			e->cmd[e->ncmd] = '\0';
		} else {
			/* Backspacing off the end of an empty command line leaves it,
			 * which is what makes a mistyped ":" recoverable with one key. */
			e->mode = M_NORMAL;
		}
		return;
	}
	if (k == 21) {          /* Ctrl-U clears the line */
		e->ncmd = 0;
		e->cmd[0] = '\0';
		return;
	}
	if (k > 0xff || k < 0x20)
		return;
	if (e->ncmd + 1 < sizeof e->cmd) {
		e->cmd[e->ncmd++] = (char)k;
		e->cmd[e->ncmd] = '\0';
	}
}

/* ── the entry point ────────────────────────────────────────────────────── */

void ed_key(ed_t *e, int key)
{
	if (key == K_NONE)
		return;

	/* Macro recording taps the stream here, before anything interprets the
	 * key, so a macro replays exactly what was typed. The `q` that STOPS the
	 * recording is excluded — otherwise every macro ends by stopping a
	 * recording that is not running, and @q in normal mode starts one. */
	if (e->rec_reg && !(e->mode == M_NORMAL && key == 'q' && !e->prefix)) {
		const char *nm = key_name(key);
		size_t n = strlen(nm);
		if (e->nrec + n < sizeof e->rec) {
			memcpy(e->rec + e->nrec, nm, n);
			e->nrec += n;
		}
	}
	if (e->rec_reg && e->mode == M_NORMAL && key == 'q' && !e->prefix) {
		char *text = xstrndup(e->rec, e->nrec);
		int r = e->rec_reg;
		e->rec_reg = 0;
		e->nrec = 0;
		int i = reg_index(r);
		if (i >= 0) {
			free(e->regs[i].text);
			e->regs[i].text = text;
			e->regs[i].linewise = false;
		} else {
			free(text);
		}
		ed_message(e, false, "recorded @%c", (char)r);
		return;
	}

	e->msg[0] = '\0';

	switch (e->mode) {
	case M_INSERT:
	case M_REPLACE:
		dot_append(e, key);
		if (key == K_ESC) {
			leave_insert(e);
			dot_commit(e);
			e->dot_ins = false;
			return;
		}
		if (key == '\r' || key == '\n') {
			insert_newline(e);
		} else if (key == K_BS || key == 8) {
			insert_backspace(e);
		} else if (key == '\t') {
			insert_tab(e);
		} else if (key == K_DEL) {
			buf_t *b = B(e);
			size_t len = buf_linelen(b, e->cy);
			if (e->cx < len) {
				range_delete(e, e->cy, e->cx, e->cy, e->cx + 1, false);
			} else if (e->cy + 1 < b->n) {
				char *j = xasprintf("%s%s", buf_line(b, e->cy),
				                    buf_line(b, e->cy + 1));
				char *one[1] = { j };
				buf_splice(b, e->cy, 2, one, 1, e->cy, e->cx);
				free(j);
			}
		} else if (key >= K_UP && key <= K_INS) {
			/* Arrow keys in insert mode END the inserted run for the purposes
			 * of `.` and of a repeat count, exactly as vim does: 3ifoo<Left>x
			 * repeating "foox" would be a guess about intent. */
			mot_t m;
			e->mode = M_NORMAL;
			bool moved = do_motion(e, key, 1, 0, &m);
			e->mode = M_INSERT;
			if (moved && m.ok) {
				e->cy = m.y;
				e->cx = m.x;
			}
			e->ins_count = 1;
			e->ins_y = e->cy;
			e->ins_x = e->cx;
			ed_clamp(e);
		} else if (key >= 0x20 && key <= 0xff) {
			insert_byte(e, key);
		}
		return;

	case M_CMDLINE:
		cmdline_key(e, key);
		return;

	default:
		/* A new command starts a new dot recording. Anything still pending
		 * (a count, an operator) means the command is mid-flight. */
		if (!e->op && !e->prefix && e->count == 0 && e->opcount == 0)
			e->ndotpend = 0;
		dot_append(e, key);
		normal_key(e, key);
		return;
	}
}

/* ── key notation ───────────────────────────────────────────────────────── */

/* "<Esc>", "<C-r>", "<CR>", "<Space>" — one spelling shared by the tests, the
 * dot register, macros and anything a config file might hold. */
int key_parse(const char **p)
{
	const char *s = *p;
	if (!*s)
		return K_NONE;

	if (*s != '<') {
		*p = s + 1;
		return (unsigned char)*s;
	}

	static const struct { const char *name; int key; } N[] = {
		{ "Esc", K_ESC }, { "CR", '\r' }, { "Enter", '\r' }, { "NL", '\n' },
		{ "Tab", '\t' }, { "BS", K_BS }, { "Space", ' ' }, { "Del", K_DEL },
		{ "Up", K_UP }, { "Down", K_DOWN }, { "Left", K_LEFT },
		{ "Right", K_RIGHT }, { "Home", K_HOME }, { "End", K_END },
		{ "PageUp", K_PGUP }, { "PageDown", K_PGDN }, { "Insert", K_INS },
		{ "lt", '<' }, { "gt", '>' }, { "Bar", '|' }, { "Bslash", '\\' },
	};

	const char *close = strchr(s, '>');
	if (!close) {
		*p = s + 1;
		return '<';
	}
	size_t n = (size_t)(close - s - 1);

	for (size_t i = 0; i < sizeof N / sizeof *N; i++) {
		if (strlen(N[i].name) == n && strncasecmp(s + 1, N[i].name, n) == 0) {
			*p = close + 1;
			return N[i].key;
		}
	}
	/* <C-x> is the control code, which for a letter is the letter's low five
	 * bits — the same arithmetic a terminal does. */
	if (n == 3 && (s[1] == 'C' || s[1] == 'c') && s[2] == '-') {
		*p = close + 1;
		return toupper((unsigned char)s[3]) - 'A' + 1;
	}
	*p = s + 1;
	return '<';
}

const char *key_name(int key)
{
	static char buf[16];
	switch (key) {
	case K_ESC:   return "<Esc>";
	case '\r':    return "<CR>";
	case '\n':    return "<NL>";
	case '\t':    return "<Tab>";
	case K_BS:    return "<BS>";
	case K_DEL:   return "<Del>";
	case K_UP:    return "<Up>";
	case K_DOWN:  return "<Down>";
	case K_LEFT:  return "<Left>";
	case K_RIGHT: return "<Right>";
	case K_HOME:  return "<Home>";
	case K_END:   return "<End>";
	case K_PGUP:  return "<PageUp>";
	case K_PGDN:  return "<PageDown>";
	case K_INS:   return "<Insert>";
	case '<':     return "<lt>";
	}
	if (key > 0 && key < 0x20) {
		snprintf(buf, sizeof buf, "<C-%c>", 'a' + key - 1);
		return buf;
	}
	if (key < 0 || key > 0xff)
		return "";
	buf[0] = (char)key;
	buf[1] = '\0';
	return buf;
}

void ed_keys(ed_t *e, const char *s)
{
	if (!s)
		return;
	/* A guard against a macro that invokes itself. Without it, qaq@a is an
	 * infinite loop that takes the whole editor with it — and a user who
	 * writes one deserves an error, not a hang. */
	static int depth = 0;
	if (depth > 64) {
		ed_message(e, true, "recursive macro stopped");
		return;
	}
	depth++;
	for (const char *p = s; *p; ) {
		int k = key_parse(&p);
		if (k == K_NONE)
			break;
		ed_key(e, k);
		if (e->quit)
			break;
	}
	depth--;
}
