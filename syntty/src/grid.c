/* grid.c — the cell grid, the interned style table, and the scrollback.
 *
 * Every change the VT layer makes to the screen goes through this file. That
 * is a deliberate choke point: the cursor must stay inside the grid, the
 * scroll region must be honoured, and rows leaving the top must reach the
 * scrollback. Those three invariants held at one place are three invariants;
 * re-derived at every call site in vt.c they are forty chances to be wrong,
 * and the failure is a corrupted screen an hour later with nothing to point at.
 *
 * ── Rows are pointers, and scrolling moves the pointers ────────────────────
 *
 * Scrolling a 200x50 screen by one line moves 50 pointers, not 10,000 cells.
 * This is the difference between a terminal that keeps up with a build log and
 * one that does not, and it costs nothing but remembering that a row's cells
 * are owned by the row and not by the screen array.
 *
 * ── The scrollback is trimmed, the screen is not ───────────────────────────
 *
 * A live row is always `cols` wide because the cursor can be put anywhere on
 * it. A row that has scrolled off cannot be written to again, so it is
 * reallocated down to the last non-blank cell before being stored. Terminal
 * output is mostly short lines: on a real build log this is a 3–5x saving on
 * the structure there are ten thousand of, and the live screen is unaffected.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdlib.h>
#include <string.h>

/* ── character width ────────────────────────────────────────────────────────
 *
 * NOT wcwidth(). wcwidth's answer depends on the locale the process happens to
 * be started in, which would make the golden-output tests pass or fail
 * according to an environment variable — the exact class of test that is worse
 * than no test. A compact table is deterministic everywhere, which is what a
 * parser's test suite needs.
 */
typedef struct { uint32_t lo, hi; } range_t;

static const range_t zero_width[] = {
	{ 0x0300, 0x036F }, { 0x0483, 0x0489 }, { 0x0591, 0x05BD },
	{ 0x0610, 0x061A }, { 0x064B, 0x065F }, { 0x0670, 0x0670 },
	{ 0x06D6, 0x06DC }, { 0x0700, 0x070F }, { 0x0711, 0x0711 },
	{ 0x0730, 0x074A }, { 0x07A6, 0x07B0 }, { 0x0900, 0x0902 },
	{ 0x093C, 0x093C }, { 0x0941, 0x0948 }, { 0x1AB0, 0x1AFF },
	{ 0x1DC0, 0x1DFF }, { 0x200B, 0x200F }, { 0x20D0, 0x20F0 },
	{ 0xFE00, 0xFE0F }, { 0xFE20, 0xFE2F }, { 0xE0100, 0xE01EF },
};

static const range_t wide[] = {
	{ 0x1100, 0x115F }, { 0x2E80, 0x303E }, { 0x3041, 0x33FF },
	{ 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xA000, 0xA4CF },
	{ 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF }, { 0xFE10, 0xFE19 },
	{ 0xFE30, 0xFE6F }, { 0xFF00, 0xFF60 }, { 0xFFE0, 0xFFE6 },
	{ 0x1F300, 0x1F64F }, { 0x1F900, 0x1F9FF },
	{ 0x20000, 0x2FFFD }, { 0x30000, 0x3FFFD },
};

static bool in_ranges(uint32_t cp, const range_t *r, size_t n)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (cp < r[mid].lo)      hi = mid;
		else if (cp > r[mid].hi) lo = mid + 1;
		else                     return true;
	}
	return false;
}

int st_char_width(uint32_t cp)
{
	if (cp == 0)
		return 0;
	if (in_ranges(cp, zero_width, sizeof zero_width / sizeof *zero_width))
		return 0;
	if (in_ranges(cp, wide, sizeof wide / sizeof *wide))
		return 2;
	return 1;
}

/* ── style interning ────────────────────────────────────────────────────── */

static uint32_t style_hash(const st_style_t *s)
{
	/* FNV-1a over the twelve bytes. The struct is padded explicitly and
	 * memset before use, so hashing it whole is safe — hashing a struct with
	 * uninitialised padding is the classic way to get two identical styles
	 * with different hashes. */
	const uint8_t *p = (const uint8_t *)s;
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < sizeof *s; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

static void style_table_init(st_grid_t *g)
{
	g->styles_cap = 64;
	g->styles = xcalloc(g->styles_cap, sizeof *g->styles);
	g->nstyles = 0;
	g->hash_mask = 1023;
	g->hash = xmalloc((g->hash_mask + 1) * sizeof *g->hash);
	memset(g->hash, 0xFF, (g->hash_mask + 1) * sizeof *g->hash);

	/* Index 0 is the default style, always. That is what lets a cleared cell
	 * be all-zero and a fresh row be a single calloc. */
	st_style_t def;
	memset(&def, 0, sizeof def);
	st_style_intern(g, &def);
}

uint16_t st_style_intern(st_grid_t *g, const st_style_t *s)
{
	uint32_t h = style_hash(s) & g->hash_mask;
	for (uint32_t probe = 0; probe <= g->hash_mask; probe++) {
		uint32_t slot = (h + probe) & g->hash_mask;
		uint16_t idx = g->hash[slot];
		if (idx == 0xFFFF) {
			if (g->nstyles == g->styles_cap) {
				g->styles_cap *= 2;
				g->styles = xrealloc(g->styles,
				                     g->styles_cap * sizeof *g->styles);
			}
			/* 65535 distinct styles on one screen does not happen; if it
			 * ever did, reusing the default is a wrong colour rather than a
			 * crash or a corrupted index. */
			if (g->nstyles >= 0xFFFF)
				return 0;
			uint16_t new_idx = (uint16_t)g->nstyles++;
			g->styles[new_idx] = *s;
			g->hash[slot] = new_idx;
			return new_idx;
		}
		if (!memcmp(&g->styles[idx], s, sizeof *s))
			return idx;
	}
	return 0;
}

const st_style_t *st_style_get(const st_grid_t *g, uint16_t idx)
{
	return idx < g->nstyles ? &g->styles[idx] : &g->styles[0];
}

/* ── rows ───────────────────────────────────────────────────────────────── */

/* ── recycling full-width rows ───────────────────────────────────────────────
 *
 * Measured, not guessed. Parsing 2.56 MB of ordinary lines, and the order the
 * two fixes landed in:
 *
 *   no newlines at all        8.5 ms   257 MB/s   the ASCII path alone
 *   newlines, no scrollback  26.3 ms    97 MB/s   + row shuffling
 *   newlines, scrollback     96.1 ms    27 MB/s   where this started
 *                            81.4 ms    31 MB/s   after this pool
 *                            44.5 ms    58 MB/s   after the watermark
 *
 * Every scrolled line was a realloc to trim the row, a free of the row the
 * ring evicted, and a calloc for the row replacing it at the bottom — three
 * allocator round trips per line of output, four hundred thousand times. The
 * screen's rows are all exactly `cols` wide and there are only ever `rows` of
 * them in flight, so they can simply be handed back and reused: a memset
 * instead of a calloc, and no free at all.
 *
 * ⚠ It was worth 15%, and the watermark on st_row_t was worth 45%. The
 * allocator was the obvious suspect and the eighty-cell backward scan hidden
 * inside row_used() was the bigger one — thirty million comparisons over a
 * build log, invisible in every profile that only counts function calls.
 */
static st_cell_t *row_alloc(st_grid_t *g, uint16_t cols)
{
	if (g->npool > 0 && g->pool_cols == cols) {
		st_cell_t *cells = g->pool[--g->npool];
		memset(cells, 0, (size_t)cols * sizeof *cells);
		return cells;
	}
	return xcalloc(cols, sizeof(st_cell_t));
}

static void row_release(st_grid_t *g, st_cell_t *cells, uint16_t len)
{
	/* Only full-width buffers go back: a trimmed scrollback row is some other
	 * size and reusing it would hand out a buffer too short for a screen row,
	 * which is the kind of bug that shows up as one corrupted line an hour
	 * later. */
	if (!cells || len != g->cols || g->cols != g->pool_cols) {
		free(cells);
		return;
	}
	if (g->npool == g->pool_cap) {
		/* Bounded: the pool only ever needs to hold what is in flight, and a
		 * pool that grows without limit is a leak wearing a hat. */
		if (g->pool_cap >= (uint32_t)g->rows + 8u) {
			free(cells);
			return;
		}
		g->pool_cap = g->pool_cap ? g->pool_cap * 2 : 8;
		g->pool = xrealloc(g->pool, g->pool_cap * sizeof *g->pool);
	}
	g->pool[g->npool++] = cells;
}

/* ── what was tried for the TRIMMED rows, and rejected ───────────────────────
 *
 * After the watermark below, one malloc/free pair per scrolled line was all
 * that was left of the scrollback's cost — about 15 ms of the 42 it takes to
 * parse 2.6 MB. Two ways to remove it were built and measured:
 *
 *   size classes of 8 cells   41.9 ms   but 820 KB per 10k lines, up from 353
 *   size classes of 2 cells   44.0 ms   353 KB, and 1.5% — inside the noise
 *
 * Neither is worth it. glibc's tcache already serves same-sized short-lived
 * allocations about as well as a hand-rolled list can, and the coarse version
 * bought 6% by spending 2.3x the scrollback memory — a bad trade for a design
 * whose headline is the memory. A ring arena would be tighter still, since
 * these rows are allocated and evicted in strict FIFO order, but on this
 * evidence the ceiling is around 15 ms and the cost is every row's cells
 * becoming a pointer into one hand-managed buffer.
 *
 * Recorded rather than deleted quietly: the next person to look at this
 * profile will see the same 15 ms and have the same idea.
 */

/* A fresh row has never been drawn, so it starts dirty. */
static st_row_t make_row(st_grid_t *g, uint16_t cols)
{
	st_row_t r;
	r.cells = row_alloc(g, cols);
	r.len = cols;
	r.hi = 0;
	r.wrapped = false;
	r.dirty = true;
	return r;
}

static void clear_row(st_row_t *r, uint16_t cols, uint16_t style)
{
	memset(r->cells, 0, (size_t)cols * sizeof *r->cells);
	/* A cleared row holds nothing — unless the clear painted a background,
	 * in which case every cell of it is meaningful. */
	r->hi = style ? cols : 0;
	if (style) {
		/* A cleared region inherits the current background, which is how
		 * `clear` on a themed prompt paints the whole screen rather than
		 * leaving the old colour in the untouched cells. */
		for (uint16_t i = 0; i < cols; i++)
			r->cells[i].style = style;
	}
	r->wrapped = false;
	r->dirty   = true;
}

static uint16_t row_used(const st_row_t *r)
{
	uint16_t n = r->hi < r->len ? r->hi : r->len;
	while (n > 0 && r->cells[n - 1].cp == 0 && r->cells[n - 1].style == 0)
		n--;
	return n;
}

/* ── the grid ───────────────────────────────────────────────────────────── */

void st_grid_init(st_grid_t *g, uint16_t cols, uint16_t rows, uint32_t limit)
{
	memset(g, 0, sizeof *g);
	g->cols = cols ? cols : 80;
	g->rows = rows ? rows : 24;
	g->limit = limit;

	g->screen = xmalloc((size_t)g->rows * sizeof *g->screen);
	for (uint16_t y = 0; y < g->rows; y++)
		g->screen[y] = make_row(g, g->cols);

	/* The ring's SLOTS are allocated; its rows are not. Ten thousand slots is
	 * 160 KB of pointers, and the rows behind them arrive only as output
	 * scrolls off. Reserving the rows here is how a terminal spends 40 MB
	 * and 30 ms before it has shown anything. */
	g->scroll = limit ? xcalloc(limit, sizeof *g->scroll) : NULL;
	g->pool_cols = g->cols;

	style_table_init(g);
	g->top = 0;
	g->bot = (uint16_t)(g->rows - 1);
	g->autowrap = true;
	g->cursor_visible = true;
}

void st_grid_free(st_grid_t *g)
{
	for (uint16_t y = 0; y < g->rows; y++)
		free(g->screen[y].cells);
	free(g->screen);
	for (uint32_t i = 0; i < g->count; i++) {
		uint32_t idx = (g->head + g->limit - g->count + i) % g->limit;
		free(g->scroll[idx].cells);
	}
	free(g->scroll);
	for (uint32_t i = 0; i < g->npool; i++)
		free(g->pool[i]);
	free(g->pool);
	free(g->styles);
	free(g->hash);

	/* ⚠ BEFORE THE memset, which is where this was not. The alternate screen
	 * is the one not currently shown, and `g->rows` is what says how many rows
	 * it has — zero the struct first and both the pointer and the count are
	 * gone, so the loop never runs and the whole screen leaks. It read as
	 * correct code sitting in the wrong place, and only the sanitiser noticed. */
	if (g->alt) {
		for (uint16_t y = 0; y < g->rows; y++)
			free(g->alt[y].cells);
		free(g->alt);
	}

	memset(g, 0, sizeof *g);
}

size_t st_grid_bytes(const st_grid_t *g)
{
	size_t n = sizeof *g;
	n += (size_t)g->rows * sizeof *g->screen;
	for (uint16_t y = 0; y < g->rows; y++)
		n += (size_t)g->screen[y].len * sizeof(st_cell_t);
	n += (size_t)g->limit * sizeof *g->scroll;
	for (uint32_t i = 0; i < g->count; i++) {
		uint32_t idx = (g->head + g->limit - g->count + i) % g->limit;
		n += (size_t)g->scroll[idx].len * sizeof(st_cell_t);
	}
	n += (size_t)g->styles_cap * sizeof *g->styles;
	n += ((size_t)g->hash_mask + 1) * sizeof *g->hash;
	return n;
}

/* A row leaving the top of the screen. Trimmed to its used width on the way
 * in — see the file header. */
static void push_scrollback(st_grid_t *g, st_row_t *r)
{
	if (g->limit == 0) {
		free(r->cells);
		r->cells = NULL;
		return;
	}
	uint16_t used = row_used(r);
	if (used == 0) {
		row_release(g, r->cells, r->len);
		r->cells = NULL;
	} else if (used < r->len) {
		/* Copy out and hand the full-width buffer BACK, rather than
		 * realloc'ing it down. A shrink keeps the big block out of
		 * circulation and forces the row replacing it at the bottom of the
		 * screen to be allocated fresh; this way the screen's rows cycle
		 * between the screen and the pool and never touch the allocator at
		 * all, and only the small trimmed copy is new. */
		st_cell_t *small = xmalloc((size_t)used * sizeof(st_cell_t));
		memcpy(small, r->cells, (size_t)used * sizeof(st_cell_t));
		row_release(g, r->cells, r->len);
		r->cells = small;
	}
	r->len = used;

	if (g->count == g->limit) {
		uint32_t oldest = (g->head + g->limit - g->count) % g->limit;
		free(g->scroll[oldest].cells);
		g->count--;
	}
	g->scroll[g->head] = *r;
	g->head = (g->head + 1) % g->limit;
	g->count++;
	r->cells = NULL;
}

/* ── damage ─────────────────────────────────────────────────────────────────
 *
 * Rows are moved by shuffling st_row_t structs, so a row's dirty flag travels
 * with its CONTENT — which is not what matters. What matters is its POSITION
 * on screen: a scroll leaves every row of the region showing something
 * different from what it showed before, whether or not its cells changed. So
 * anything that moves rows marks the whole region it touched.
 *
 * Over-marking costs one repaint. Under-marking leaves stale pixels that look
 * like memory corruption, so the bias here is deliberate and one-directional. */
static void dirty_range(st_grid_t *g, int from, int to)
{
	if (from < 0) from = 0;
	if (to >= g->rows) to = g->rows - 1;
	for (int y = from; y <= to; y++)
		g->screen[y].dirty = true;
}

/* ── selected text ──────────────────────────────────────────────────────────
 *
 * The text between two cells, in reading order, as UTF-8.
 *
 * ⚠ A SOFT WRAP IS NOT A NEWLINE. When a long line ran past the right edge the
 * terminal broke it across two rows, but it is ONE line and the person who
 * copies it wants one line — paste it into a shell and a newline in the middle
 * runs half a command. The row's `wrapped` flag is what distinguishes the
 * break the terminal invented from the newline the program actually sent, and
 * it is the whole reason that flag is kept.
 *
 * Trailing blanks are trimmed per line, because the cells to the right of the
 * text are padding rather than spaces anybody typed.
 *
 * Coordinates are VIEW coordinates — what the person can see and clicked on —
 * so this resolves them through the scrollback the same way the renderer does. */
char *st_grid_selection_text(const st_grid_t *g, int c0, int r0, int c1, int r1)
{
	/* Normalise: the person may have dragged upwards or right-to-left. */
	if (r1 < r0 || (r1 == r0 && c1 < c0)) {
		int t;
		t = r0; r0 = r1; r1 = t;
		t = c0; c0 = c1; c1 = t;
	}
	if (r0 < 0) r0 = 0;
	if (r1 >= g->rows) r1 = g->rows - 1;
	if (r1 < r0)
		return xstrdup("");

	size_t cap = 256, len = 0;
	char *out = xmalloc(cap);

	for (int y = r0; y <= r1; y++) {
		const st_row_t *row = st_grid_view_row(g, y);
		if (!row)
			continue;

		int from = (y == r0) ? c0 : 0;
		int to   = (y == r1) ? c1 : g->cols - 1;
		if (from < 0) from = 0;
		if (to >= row->len) to = row->len - 1;

		/* Trim the padding on the right of this line's selected span. */
		int last = from - 1;
		for (int x = from; x <= to; x++)
			if (row->cells[x].cp != 0 && row->cells[x].cp != ' ')
				last = x;

		for (int x = from; x <= last; x++) {
			uint32_t cp = row->cells[x].cp;
			if (row->cells[x].width == 0 && cp == 0)
				continue;             /* the tail of a wide glyph */
			if (cp == 0)
				cp = ' ';

			/* UTF-8, four bytes at most, plus room for a newline. */
			if (len + 8 > cap) {
				cap *= 2;
				out = xrealloc(out, cap);
			}
			if (cp < 0x80) {
				out[len++] = (char)cp;
			} else if (cp < 0x800) {
				out[len++] = (char)(0xC0 | cp >> 6);
				out[len++] = (char)(0x80 | (cp & 0x3F));
			} else if (cp < 0x10000) {
				out[len++] = (char)(0xE0 | cp >> 12);
				out[len++] = (char)(0x80 | (cp >> 6 & 0x3F));
				out[len++] = (char)(0x80 | (cp & 0x3F));
			} else {
				out[len++] = (char)(0xF0 | cp >> 18);
				out[len++] = (char)(0x80 | (cp >> 12 & 0x3F));
				out[len++] = (char)(0x80 | (cp >> 6 & 0x3F));
				out[len++] = (char)(0x80 | (cp & 0x3F));
			}
		}

		/* ⚠ Only where the terminal did NOT invent the break. */
		if (y < r1 && !row->wrapped) {
			if (len + 2 > cap) {
				cap *= 2;
				out = xrealloc(out, cap);
			}
			out[len++] = '\n';
		}
	}

	if (len + 1 > cap)
		out = xrealloc(out, len + 1);
	out[len] = '\0';
	return out;
}

/* ── the alternate screen ───────────────────────────────────────────────────
 *
 * A second screen, swapped in whole. Every full-screen program uses it — vim,
 * less, htop, man — so that quitting leaves the shell session exactly as it
 * was.
 *
 * ⚠ THE SCROLLBACK IS NOT FED WHILE IT IS SHOWING, which is the whole reason
 * it exists. st_scroll_up checks `on_alt`: without that, ten minutes in an
 * editor fills the history with fragments of its redraws and the commands the
 * person actually ran are gone. The symptom reads as "the scrollback is
 * broken", not as "the alternate screen is missing", which is why this is
 * worth a paragraph.
 *
 * Allocated LAZILY. A session that never opens a full-screen program never
 * pays for a second screen, and most of the startup budget this project is
 * built around is spent on exactly this kind of "just in case". */
void st_grid_alt_screen(st_grid_t *g, bool enable, bool save_cursor)
{
	if (enable == g->on_alt)
		return;

	if (!g->alt) {
		g->alt = xcalloc(g->rows, sizeof *g->alt);
		for (uint16_t y = 0; y < g->rows; y++)
			g->alt[y] = make_row(g, g->cols);
	}

	if (enable && save_cursor) {
		g->alt_cx = g->cx;
		g->alt_cy = g->cy;
		g->alt_style = g->cur_style;
	}

	/* A pointer swap, not a copy: `alt` always holds whichever screen is not
	 * being shown. */
	st_row_t *tmp = g->screen;
	g->screen = g->alt;
	g->alt = tmp;
	g->on_alt = enable;

	if (enable) {
		/* Entering: the program expects a blank screen to draw on, and
		 * expects it blank EVERY time — a second `vim` that opened onto the
		 * first one's leftovers would be a memorable bug. */
		for (uint16_t y = 0; y < g->rows; y++)
			clear_row(&g->screen[y], g->cols, 0);
		st_move_to(g, 0, 0);
	} else if (save_cursor) {
		g->cx = g->alt_cx < g->cols ? g->alt_cx : 0;
		g->cy = g->alt_cy < g->rows ? g->alt_cy : 0;
		g->cur_style = g->alt_style;
	}

	/* The whole surface is different now. */
	g->view = 0;
	st_grid_dirty_all(g);
}

/* ── the scrollback viewport ────────────────────────────────────────────────
 *
 * The scrollback is a ring of `count` rows, oldest first once unwound. With a
 * view offset of N, screen line 0 shows the row N lines above the live screen's
 * line 0 — so lines below N come from the live screen, shifted down, and lines
 * above it come from history.
 *
 * ⚠ The offset is bounded by what is actually KEPT, not by what has ever been
 * written: scrolling back further than the scrollback goes must stop, not wrap
 * around to something arbitrary. */
const st_row_t *st_grid_view_row(const st_grid_t *g, int y)
{
	if (y < 0 || y >= g->rows)
		return NULL;
	if (g->view == 0)
		return &g->screen[y];

	long want = (long)y - (long)g->view;
	if (want >= 0)
		return &g->screen[want];      /* still the live screen, pushed down */

	/* -1 is the newest scrollback row, -2 the one before it. */
	long back = -want;                /* 1 .. count */
	if (back > (long)g->count)
		return NULL;                  /* older than anything we kept */

	uint32_t idx = (g->head + g->limit - (uint32_t)back) % g->limit;
	return &g->scroll[idx];
}

bool st_grid_view_scroll(st_grid_t *g, int delta)
{
	long v = (long)g->view + delta;
	if (v < 0)
		v = 0;
	if (v > (long)g->count)
		v = (long)g->count;
	if ((uint32_t)v == g->view)
		return false;
	g->view = (uint32_t)v;
	st_grid_dirty_all(g);
	return true;
}

bool st_grid_view_reset(st_grid_t *g)
{
	if (g->view == 0)
		return false;
	g->view = 0;
	st_grid_dirty_all(g);
	return true;
}

/* Walk outward from where we are looking until a row carrying a prompt mark
 * turns up, and return the offset that puts it at the TOP of the window —
 * which is what "jump to the previous prompt" has to mean, or the thing jumped
 * to is off the bottom of the screen. */
long st_grid_find_prompt(const st_grid_t *g, int dir)
{
	long limit = (long)g->count + g->rows;
	for (long off = (long)g->view + dir; off >= 0 && off <= limit; off += dir) {
		/* The row that would be at the top of the window at this offset. */
		long want = -off;
		const st_row_t *r;
		if (want >= 0)
			r = &g->screen[0];
		else {
			long back = off;
			if (back > (long)g->count)
				break;
			if (back == 0)
				r = &g->screen[0];
			else {
				uint32_t idx = (g->head + g->limit - (uint32_t)back) % g->limit;
				r = &g->scroll[idx];
			}
		}
		if (r && r->mark == ST_MARK_PROMPT)
			return off;
		if (dir < 0 && off == 0)
			break;
	}
	return -1;
}

bool st_grid_row_dirty(const st_grid_t *g, int row)
{
	return row >= 0 && row < g->rows && g->screen[row].dirty;
}

void st_grid_clear_dirty(st_grid_t *g)
{
	for (int y = 0; y < g->rows; y++)
		g->screen[y].dirty = false;
}

void st_grid_dirty_all(st_grid_t *g)
{
	dirty_range(g, 0, g->rows - 1);
}

void st_scroll_up(st_grid_t *g, int n)
{
	dirty_range(g, g->top, g->bot);
	if (n <= 0)
		return;
	int span = g->bot - g->top + 1;
	if (n > span)
		n = span;

	for (int i = 0; i < n; i++) {
		st_row_t going = g->screen[g->top];
		/* Only the top of the SCREEN feeds the scrollback. A program that
		 * set a scroll region is managing a pane — its discarded lines are
		 * not history, and putting them in the scrollback is how a full
		 * screen application fills it with garbage.
		 *
		 * ⚠ And NOTHING from the alternate screen does, which is the entire
		 * reason that screen exists: an editor's redraws are not history
		 * either. Miss this and ten minutes in vim erases the commands the
		 * person actually ran — read as "the scrollback is broken". */
		if (g->top == 0 && !g->on_alt)
			push_scrollback(g, &going);
		else
			row_release(g, going.cells, going.len);

		memmove(&g->screen[g->top], &g->screen[g->top + 1],
		        (size_t)(g->bot - g->top) * sizeof *g->screen);
		g->screen[g->bot] = make_row(g, g->cols);
		if (g->cur_style)
			clear_row(&g->screen[g->bot], g->cols, g->cur_style);
	}
}

void st_scroll_down(st_grid_t *g, int n)
{
	dirty_range(g, g->top, g->bot);
	if (n <= 0)
		return;
	int span = g->bot - g->top + 1;
	if (n > span)
		n = span;

	for (int i = 0; i < n; i++) {
		row_release(g, g->screen[g->bot].cells, g->screen[g->bot].len);
		memmove(&g->screen[g->top + 1], &g->screen[g->top],
		        (size_t)(g->bot - g->top) * sizeof *g->screen);
		g->screen[g->top] = make_row(g, g->cols);
		if (g->cur_style)
			clear_row(&g->screen[g->top], g->cols, g->cur_style);
	}
}

void st_set_region(st_grid_t *g, int top, int bot)
{
	if (top < 0) top = 0;
	if (bot > g->rows - 1) bot = g->rows - 1;
	if (top >= bot)
		{ top = 0; bot = g->rows - 1; }
	g->top = (uint16_t)top;
	g->bot = (uint16_t)bot;
	st_move_to(g, 0, g->origin ? 0 : top);
}

void st_move_to(st_grid_t *g, int col, int row)
{
	if (g->origin) {
		row += g->top;
		if (row > g->bot)
			row = g->bot;
	}
	if (col < 0) col = 0;
	if (row < 0) row = 0;
	if (col > g->cols - 1) col = g->cols - 1;
	if (row > g->rows - 1) row = g->rows - 1;
	g->cx = (uint16_t)col;
	g->cy = (uint16_t)row;
	g->wrap_next = false;
}

void st_move_by(st_grid_t *g, int dcol, int drow)
{
	int col = g->cx + dcol;
	int row = g->cy + drow;
	/* Vertical movement is CLAMPED BY THE REGION when the cursor is already
	 * inside it — CUU at the top of a scroll region must not walk out of the
	 * pane it belongs to. */
	if (drow < 0 && g->cy >= g->top && row < g->top)
		row = g->top;
	if (drow > 0 && g->cy <= g->bot && row > g->bot)
		row = g->bot;
	if (col < 0) col = 0;
	if (col > g->cols - 1) col = g->cols - 1;
	if (row < 0) row = 0;
	if (row > g->rows - 1) row = g->rows - 1;
	g->cx = (uint16_t)col;
	g->cy = (uint16_t)row;
	g->wrap_next = false;
}

void st_carriage_return(st_grid_t *g)
{
	g->cx = 0;
	g->wrap_next = false;
}

void st_newline(st_grid_t *g)
{
	g->wrap_next = false;
	if (g->cy == g->bot)
		st_scroll_up(g, 1);
	else if (g->cy < g->rows - 1)
		g->cy++;
}

void st_backspace(st_grid_t *g)
{
	if (g->wrap_next)
		g->wrap_next = false;
	else if (g->cx > 0)
		g->cx--;
}

void st_tab(st_grid_t *g, int n)
{
	/* Fixed eight-column stops. Programmable stops (HTS/TBC) are a later
	 * stage; nothing in the test corpus moves them, and pretending to
	 * support them would be worse than not. */
	for (int i = 0; i < n; i++) {
		int next = (g->cx / 8 + 1) * 8;
		if (next > g->cols - 1)
			next = g->cols - 1;
		g->cx = (uint16_t)next;
	}
	g->wrap_next = false;
}

void st_put(st_grid_t *g, uint32_t cp, int width)
{
	if (width == 0) {
		/* A combining mark belongs to the cell before the cursor. Stage 1
		 * keeps only the base character — recording it is a grid change, and
		 * changing the cell layout for it belongs with the renderer that has
		 * to draw it. */
		return;
	}

	if (g->wrap_next && g->autowrap) {
		g->screen[g->cy].wrapped = true;
		st_carriage_return(g);
		st_newline(g);
	}

	/* A double-width glyph will not straddle the right edge: it wraps whole.
	 * Splitting it puts half a character in each of two rows, and every
	 * consumer of the grid downstream then has to know about halves. */
	if (width == 2 && g->cx == g->cols - 1) {
		if (g->autowrap) {
			g->screen[g->cy].wrapped = true;
			st_carriage_return(g);
			st_newline(g);
		} else {
			return;
		}
	}

	st_row_t *row = &g->screen[g->cy];
	row->dirty = true;
	if (g->cx + width > row->hi)
		row->hi = (uint16_t)(g->cx + width);
	row->cells[g->cx].cp = cp;
	row->cells[g->cx].style = g->cur_style;
	row->cells[g->cx].width = (uint8_t)width;

	if (width == 2 && g->cx + 1 < g->cols) {
		row->cells[g->cx + 1].cp = 0;
		row->cells[g->cx + 1].style = g->cur_style;
		row->cells[g->cx + 1].width = 0;   /* the tail of the glyph before it */
	}

	int advance = width;
	if (g->cx + advance > g->cols - 1) {
		g->cx = (uint16_t)(g->cols - 1);
		g->wrap_next = true;
	} else {
		g->cx = (uint16_t)(g->cx + advance);
	}
}

void st_erase_display(st_grid_t *g, int mode)
{
	switch (mode) {
	case 0:   /* cursor to end */
		st_erase_line(g, 0);
		for (uint16_t y = (uint16_t)(g->cy + 1); y < g->rows; y++)
			clear_row(&g->screen[y], g->cols, g->cur_style);
		break;
	case 1:   /* start to cursor */
		st_erase_line(g, 1);
		for (uint16_t y = 0; y < g->cy; y++)
			clear_row(&g->screen[y], g->cols, g->cur_style);
		break;
	case 2:   /* all */
	case 3:   /* all + scrollback; the scrollback half is below */
		for (uint16_t y = 0; y < g->rows; y++)
			clear_row(&g->screen[y], g->cols, g->cur_style);
		if (mode == 3) {
			for (uint32_t i = 0; i < g->count; i++) {
				uint32_t idx = (g->head + g->limit - g->count + i) % g->limit;
				free(g->scroll[idx].cells);
				g->scroll[idx].cells = NULL;
				g->scroll[idx].len = 0;
			}
			g->count = 0;
			g->head = 0;
		}
		break;
	default:
		break;
	}
	g->wrap_next = false;
}

void st_erase_line(st_grid_t *g, int mode)
{
	st_row_t *row = &g->screen[g->cy];
	row->dirty = true;
	uint16_t from = 0, to = g->cols;
	if (mode == 0)      from = g->cx;
	else if (mode == 1) to = (uint16_t)(g->cx + 1);
	else if (mode != 2) return;

	for (uint16_t x = from; x < to && x < g->cols; x++) {
		row->cells[x].cp = 0;
		row->cells[x].width = 0;
		row->cells[x].style = g->cur_style;
	}
	if (g->cur_style)
		row->hi = g->cols;          /* a painted background is content */
	else if (mode == 0 && g->cx < row->hi)
		row->hi = g->cx;            /* everything from here is provably blank */
	g->wrap_next = false;
}

void st_erase_chars(st_grid_t *g, int n)
{
	if (n < 1) n = 1;
	st_row_t *row = &g->screen[g->cy];
	row->dirty = true;
	for (int i = 0; i < n && g->cx + i < g->cols; i++) {
		row->cells[g->cx + i].cp = 0;
		row->cells[g->cx + i].width = 0;
		row->cells[g->cx + i].style = g->cur_style;
	}
	if (g->cur_style)
		row->hi = g->cols;
	g->wrap_next = false;
}

void st_insert_lines(st_grid_t *g, int n)
{
	dirty_range(g, g->cy, g->bot);
	if (g->cy < g->top || g->cy > g->bot)
		return;
	uint16_t saved_top = g->top;
	g->top = g->cy;
	st_scroll_down(g, n);
	g->top = saved_top;
	g->cx = 0;
	g->wrap_next = false;
}

void st_delete_lines(st_grid_t *g, int n)
{
	dirty_range(g, g->cy, g->bot);
	if (g->cy < g->top || g->cy > g->bot)
		return;
	uint16_t saved_top = g->top;
	g->top = g->cy;
	st_scroll_up(g, n);
	g->top = saved_top;
	g->cx = 0;
	g->wrap_next = false;
}

void st_insert_chars(st_grid_t *g, int n)
{
	if (n < 1) n = 1;
	if (n > g->cols - g->cx) n = g->cols - g->cx;
	st_row_t *row = &g->screen[g->cy];
	row->dirty = true;
	memmove(&row->cells[g->cx + n], &row->cells[g->cx],
	        (size_t)(g->cols - g->cx - n) * sizeof *row->cells);
	row->hi = g->cols;
	for (int i = 0; i < n; i++) {
		row->cells[g->cx + i].cp = 0;
		row->cells[g->cx + i].width = 0;
		row->cells[g->cx + i].style = g->cur_style;
	}
	g->wrap_next = false;
}

void st_delete_chars(st_grid_t *g, int n)
{
	if (n < 1) n = 1;
	if (n > g->cols - g->cx) n = g->cols - g->cx;
	st_row_t *row = &g->screen[g->cy];
	row->dirty = true;
	memmove(&row->cells[g->cx], &row->cells[g->cx + n],
	        (size_t)(g->cols - g->cx - n) * sizeof *row->cells);
	row->hi = g->cols;
	for (int i = 0; i < n; i++) {
		uint16_t x = (uint16_t)(g->cols - n + i);
		row->cells[x].cp = 0;
		row->cells[x].width = 0;
		row->cells[x].style = g->cur_style;
	}
	g->wrap_next = false;
}

void st_grid_resize(st_grid_t *g, uint16_t cols, uint16_t rows)
{
	/* Every row is at a new size or a new place. */
	st_grid_dirty_all(g);
	if (cols == 0 || rows == 0 || (cols == g->cols && rows == g->rows))
		return;

	st_row_t *ns = xmalloc((size_t)rows * sizeof *ns);
	/* Anchor on the BOTTOM of the old screen: a shell prompt is at the
	 * bottom, and a resize that keeps the top keeps the part nobody is
	 * looking at and throws away the line being typed on. */
	int keep = g->rows < rows ? g->rows : rows;
	int first = g->rows - keep;

	for (int y = 0; y < rows; y++) {
		int src = y - (rows - keep) + first;
		if (src >= first && src < g->rows) {
			ns[y] = g->screen[src];
			if (cols != g->cols) {
				ns[y].cells = xrealloc(ns[y].cells,
				                       (size_t)cols * sizeof(st_cell_t));
				if (cols > ns[y].len)
					memset(&ns[y].cells[ns[y].len], 0,
					       (size_t)(cols - ns[y].len) * sizeof(st_cell_t));
				ns[y].len = cols;
				if (ns[y].hi > cols)
					ns[y].hi = cols;
			}
		} else {
			ns[y] = make_row(g, cols);
		}
	}
	for (int y = 0; y < first; y++)
		free(g->screen[y].cells);
	for (int y = first + keep; y < g->rows; y++)
		free(g->screen[y].cells);

	free(g->screen);
	g->screen = ns;
	g->cols = cols;
	g->rows = rows;
	for (uint32_t i = 0; i < g->npool; i++)
		free(g->pool[i]);
	g->npool = 0;
	g->pool_cols = cols;
	g->top = 0;
	g->bot = (uint16_t)(rows - 1);
	if (g->cx > cols - 1) g->cx = (uint16_t)(cols - 1);
	if (g->cy > rows - 1) g->cy = (uint16_t)(rows - 1);
	g->wrap_next = false;
}

/* ── dumping ────────────────────────────────────────────────────────────── */

static void put_utf8(FILE *out, uint32_t cp)
{
	if (cp < 0x80) {
		fputc((int)cp, out);
	} else if (cp < 0x800) {
		fputc((int)(0xC0 | (cp >> 6)), out);
		fputc((int)(0x80 | (cp & 0x3F)), out);
	} else if (cp < 0x10000) {
		fputc((int)(0xE0 | (cp >> 12)), out);
		fputc((int)(0x80 | ((cp >> 6) & 0x3F)), out);
		fputc((int)(0x80 | (cp & 0x3F)), out);
	} else {
		fputc((int)(0xF0 | (cp >> 18)), out);
		fputc((int)(0x80 | ((cp >> 12) & 0x3F)), out);
		fputc((int)(0x80 | ((cp >> 6) & 0x3F)), out);
		fputc((int)(0x80 | (cp & 0x3F)), out);
	}
}

static void dump_row(const st_row_t *r, FILE *out)
{
	uint16_t used = row_used(r);
	for (uint16_t x = 0; x < used; x++) {
		if (r->cells[x].width == 0 && r->cells[x].cp == 0 && x > 0 &&
		    r->cells[x - 1].width == 2)
			continue;   /* the tail of a wide glyph prints nothing */
		put_utf8(out, r->cells[x].cp ? r->cells[x].cp : ' ');
	}
	fputc('\n', out);
}

void st_dump_text(const st_grid_t *g, FILE *out)
{
	for (uint16_t y = 0; y < g->rows; y++)
		dump_row(st_grid_view_row(g, y), out);
}

void st_dump_scrollback(const st_grid_t *g, FILE *out)
{
	for (uint32_t i = 0; i < g->count; i++) {
		uint32_t idx = (g->head + g->limit - g->count + i) % g->limit;
		dump_row(&g->scroll[idx], out);
	}
}

void st_dump_styled(const st_grid_t *g, FILE *out)
{
	/* Text line, then the style index per column in hex. A test can assert a
	 * colour changed without this file inventing a colour syntax nobody else
	 * uses — the indices are meaningless on their own, and that is the point:
	 * what a test checks is that two runs of text have DIFFERENT indices, or
	 * that a style survived a scroll. */
	for (uint16_t y = 0; y < g->rows; y++) {
		uint16_t used = row_used(&g->screen[y]);
		if (used == 0)
			continue;
		fprintf(out, "%2u| ", y);
		dump_row(&g->screen[y], out);
		fprintf(out, "  | ");
		for (uint16_t x = 0; x < used; x++)
			fprintf(out, "%x", g->screen[y].cells[x].style & 0xF);
		fputc('\n', out);
	}
	fprintf(out, "styles: %u\n", g->nstyles);
}
