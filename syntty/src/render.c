/* render.c — cells to pixels, on the CPU, into a buffer somebody else owns.
 *
 * ── Why there is no GPU in this file ───────────────────────────────────────
 *
 * Because of the single biggest number in this project's design. Of a fresh
 * kitty's 264 MB resident, 188 MB is the graphics stack that exists purely
 * because a GL context was created: 83.5 MB of libLLVM (Mesa's shader
 * compiler), 88 MB of nvidia EGL, 15.8 MB of gallium. kitty's own working set
 * is 32 MB. Creating no GL context is worth more than every other memory
 * decision in this program combined, and it is exactly the decision that lets
 * foot sit at 21 MB.
 *
 * A terminal is also the wrong shape for a GPU. What changes between two
 * frames is usually one cell — a cursor blink, a character typed. Uploading a
 * texture and running a pipeline to move eight by twenty pixels is more work
 * than moving them, and the buffer is already in memory the compositor can
 * read.
 *
 * ── What this file does NOT do, deliberately ───────────────────────────────
 *
 * NO SHAPING. There is no harfbuzz here and no ligatures. The design trades
 * that tail away for speed and a small binary, and it says so out loud rather
 * than leaving it to be discovered. One codepoint maps to one glyph from one
 * face, which is what a monospace grid means anyway; when shaping does arrive
 * it will be for non-ASCII runs only, because a pure-ASCII run is a cost most
 * terminals pay without noticing.
 *
 * ── The output format ──────────────────────────────────────────────────────
 *
 * 32-bit little-endian XRGB — 0x00RRGGBB — which is wl_shm's
 * WL_SHM_FORMAT_XRGB8888 and needs no conversion on the way to the
 * compositor. The buffer belongs to the caller: this file never allocates one,
 * so the same code paints a Wayland buffer, a PPM for the test suite, and
 * whatever a benchmark wants to throw away.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdlib.h>
#include <string.h>

struct st_render {
	st_font_t *font;
	const struct st_gfx *gfx;   /* images to draw over the cells, or NULL */
	uint32_t   palette[256];
	uint32_t   def_fg, def_bg;
	uint32_t   cursor_fg, cursor_bg;
	bool       cursor_set;      /* the config named them; otherwise: invert */
	bool       show_cursor;
	/* ── where row 0 starts ─────────────────────────────────────────────────
	 *
	 * Pixels above the grid that belong to somebody else — the tab bar. It is
	 * an OFFSET rather than a smaller buffer because every coordinate in this
	 * file is derived from the grid, and a renderer that thought the window
	 * began at the top would paint the bar's row of pixels with cell content
	 * once per frame and the bar would flicker under the text. */
	int        origin_y;
};

/* ── the 256 colours ───────────────────────────────────────────────────────
 *
 * Built, not tabulated. 232 of the 256 follow two exact formulas that every
 * terminal agrees on, and a hand-typed table of them is 232 chances to make a
 * typo that shows up as one wrong shade in one program, months later. Only the
 * first sixteen are a choice, and those are the ones worth stating. */
static void palette_build(uint32_t *p)
{
	/* The sixteen. A dark-background scheme with enough contrast that the
	 * "bright" half is genuinely brighter rather than merely different —
	 * see project_synui_pale_theme_legibility for how that goes wrong. */
	static const uint32_t base16[16] = {
		0x1B1F26, 0xCC5555, 0x5FA85F, 0xC7A03C,
		0x4C86C4, 0x9A76DB, 0x4FA8A8, 0xC8CDD6,
		0x4A5260, 0xE87B7B, 0x86C986, 0xE3C25F,
		0x74A6E0, 0xA391E8, 0x74C9C9, 0xF0F4FA,
	};
	for (int i = 0; i < 16; i++)
		p[i] = base16[i];

	/* 16..231: a 6x6x6 cube. The levels are NOT evenly spaced — 0 then
	 * 95,135,175,215,255 — which is the xterm convention every program that
	 * draws a colour cube assumes. */
	static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
	int n = 16;
	for (int r = 0; r < 6; r++)
		for (int g = 0; g < 6; g++)
			for (int b = 0; b < 6; b++)
				p[n++] = (uint32_t)(lv[r] << 16 | lv[g] << 8 | lv[b]);

	/* 232..255: twenty-four greys, 8 to 238 in steps of ten. */
	for (int i = 0; i < 24; i++) {
		int v = 8 + i * 10;
		p[n++] = (uint32_t)(v << 16 | v << 8 | v);
	}
}

st_render_t *st_render_new(st_font_t *f)
{
	st_render_t *r = xcalloc(1, sizeof *r);
	r->font = f;
	palette_build(r->palette);
	r->def_bg      = ST_DEF_BG;
	r->def_fg      = ST_DEF_FG;
	r->cursor_bg   = ST_DEF_FG;
	r->cursor_fg   = ST_DEF_BG;
	r->show_cursor = true;
	return r;
}

void st_render_free(st_render_t *r) { free(r); }

/* Swap the face every glyph comes from. Only the binding — the CELL SIZE goes
 * with the font, so whoever calls this owns re-fitting the grid to the window
 * and telling every child about it. The renderer holds no cached metrics of
 * its own, which is what makes this one assignment rather than an invalidation
 * pass. */
void st_render_set_font(st_render_t *r, st_font_t *f)
{
	if (f)
		r->font = f;
}

/* ⚠ BACK TO THE BUILT-IN SCHEME, and a reload cannot work without it. Applying
 * a config sets the colours it NAMES; a key the file used to have and no
 * longer does would otherwise keep the value it had, so deleting a line — or a
 * theme switching from a palette that set `color4` to one that does not —
 * would do nothing at all. That is the same silent failure the config chapter
 * of this program exists to avoid, on the one path where the config is read
 * twice. */
void st_render_colors_reset(st_render_t *r)
{
	palette_build(r->palette);
	r->def_bg     = ST_DEF_BG;
	r->def_fg     = ST_DEF_FG;
	r->cursor_bg  = ST_DEF_FG;
	r->cursor_fg  = ST_DEF_BG;
	r->cursor_set = false;
}

/* ⚠ ST_CFG_UNSET LEAVES ONE ALONE. A config that names a background and not a
 * foreground must not silently reset the foreground to whatever the caller
 * happened to have in a variable — and the alternative, a getter for each, is
 * two more entry points for the same fact. */
void st_render_colors(st_render_t *r, uint32_t fg, uint32_t bg)
{
	if (fg != ST_CFG_UNSET) r->def_fg = fg;
	if (bg != ST_CFG_UNSET) r->def_bg = bg;
}

void st_render_colors_get(const st_render_t *r, uint32_t *fg, uint32_t *bg)
{
	if (fg) *fg = r->def_fg;
	if (bg) *bg = r->def_bg;
}

void st_render_cursor(st_render_t *r, bool on) { r->show_cursor = on; }

void st_render_origin(st_render_t *r, int y) { r->origin_y = y > 0 ? y : 0; }
int  st_render_origin_get(const st_render_t *r) { return r->origin_y; }

void st_render_palette(st_render_t *r, int idx, uint32_t rgb)
{
	if (idx >= 0 && idx < 16)
		r->palette[idx] = rgb & 0xFFFFFFu;
}

uint32_t st_render_palette_get(const st_render_t *r, int idx)
{
	return (idx >= 0 && idx < 256) ? r->palette[idx] : 0;
}

/* ⚠ BOTH OR NEITHER. A cursor given a background and not a foreground would
 * draw the cell's own text colour on it, which is invisible whenever the two
 * happen to match — and they match exactly when somebody has themed both. */
void st_render_cursor_color(st_render_t *r, uint32_t bg, uint32_t fg)
{
	if (bg == ST_CFG_UNSET && fg == ST_CFG_UNSET) {
		r->cursor_set = false;
		return;
	}
	r->cursor_set = true;
	r->cursor_bg = bg == ST_CFG_UNSET ? r->def_fg : bg;
	r->cursor_fg = fg == ST_CFG_UNSET ? r->def_bg : fg;
}

/* ⚠ WITH NO CONFIGURED CURSOR THERE IS NO CURSOR COLOUR — the block INVERTS
 * whatever cell it sits on, which is the default precisely because it contrasts
 * with anything a program paints. The default foreground is the honest answer
 * for a program that asks: it is what the cursor comes out as over ordinary
 * background, and it is a colour, which "an inversion" is not. */
uint32_t st_render_cursor_color_get(const st_render_t *r)
{
	return r->cursor_set ? r->cursor_bg : r->def_fg;
}

void st_render_set_gfx(st_render_t *r, const struct st_gfx *g) { r->gfx = g; }

int  st_render_width (const st_render_t *r, const st_grid_t *g)
{
	return st_font_cell_w(r->font) * g->cols;
}
int  st_render_height(const st_render_t *r, const st_grid_t *g)
{
	return st_font_cell_h(r->font) * g->rows;
}

/* A colour word from the style's 32-bit encoding. */
static inline uint32_t resolve(const st_render_t *r, uint32_t c, uint32_t dflt)
{
	switch (c & 0xFF000000u) {
	case ST_COL_INDEXED: return r->palette[c & 0xFF];
	case ST_COL_RGB:     return c & 0x00FFFFFFu;
	default:             return dflt;
	}
}

/* `t` sixteenths of the way from `a` to `b`, per channel. t=8 is the halfway
 * mix ST_DIM used to be, and is byte-identical to it. */
static inline uint32_t mix_16th(uint32_t a, uint32_t b, unsigned t)
{
	unsigned s = 16 - t;
	return ((((a >> 16 & 0xFF) * s + (b >> 16 & 0xFF) * t) / 16) << 16)
	     | ((((a >>  8 & 0xFF) * s + (b >>  8 & 0xFF) * t) / 16) <<  8)
	     |  (((a       & 0xFF) * s + (b       & 0xFF) * t) / 16);
}

/* sRGB → linear, 0..65535, so relative luminance is integer arithmetic and
 * this file keeps needing no libm. Generated, not hand-written. */
static const uint16_t srgb_lin[256] = {
	    0,    20,    40,    60,    80,    99,   119,   139,   159,   179,   199,   219,
	  241,   264,   288,   313,   340,   367,   396,   427,   458,   491,   526,   562,
	  599,   637,   677,   718,   761,   805,   851,   898,   947,   997,  1048,  1101,
	 1156,  1212,  1270,  1330,  1391,  1453,  1517,  1583,  1651,  1720,  1790,  1863,
	 1937,  2013,  2090,  2170,  2250,  2333,  2418,  2504,  2592,  2681,  2773,  2866,
	 2961,  3058,  3157,  3258,  3360,  3464,  3570,  3678,  3788,  3900,  4014,  4129,
	 4247,  4366,  4488,  4611,  4736,  4864,  4993,  5124,  5257,  5392,  5530,  5669,
	 5810,  5953,  6099,  6246,  6395,  6547,  6700,  6856,  7014,  7174,  7335,  7500,
	 7666,  7834,  8004,  8177,  8352,  8528,  8708,  8889,  9072,  9258,  9445,  9635,
	 9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090,
	12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874,
	15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
	18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481,
	21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325,
	25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542,
	29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
	34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138,
	39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534,
	45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341,
	50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
	57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221,
	63795, 64372, 64952, 65535,
};

static inline uint32_t luma(uint32_t c)
{
	return (2126u * srgb_lin[c >> 16 & 0xFF]
	      + 7152u * srgb_lin[c >>  8 & 0xFF]
	      +  722u * srgb_lin[c       & 0xFF]) / 10000u;
}

/* Light or dark, by the same measure the dim floor uses. The midpoint is the
 * WCAG one: a background is "light" when black text on it beats white text on
 * it, which is where its relative luminance passes 0.1791 — 11737 on this
 * file's 0..65535 scale.
 *
 * ⚠ NOT (r+g+b)/3 > 128. That calls #0000ff light and #ffff00 dark, and both
 * are backwards. The whole point of telling a child which it has is that the
 * child then picks a palette for it. */
bool st_render_bg_is_light(const st_render_t *r)
{
	return luma(r->def_bg) > 11737u;
}

/* Does `fg` clear 4.5:1 against `bg`? 3277 is the 0.05 of the WCAG formula in
 * the same 0..65535 scale, and the ratio is cross-multiplied so there is no
 * division and no float. */
static inline bool clears_4_5(uint32_t fg, uint32_t bg)
{
	uint32_t a = luma(fg) + 3277, b = luma(bg) + 3277;
	uint32_t hi = a > b ? a : b, lo = a > b ? b : a;
	return (uint64_t)hi * 10u >= (uint64_t)lo * 45u;
}

/* ST_DIM, which is a real attribute programs use for de-emphasis — rendering it
 * as plain fg makes `ls` output and diff headers indistinguishable from
 * ordinary text.
 *
 * ⚠ DIMMING IS A BLEND TOWARD THE BACKGROUND, AND THAT IS ONLY LEGIBLE ON A
 * DARK ONE. The old code always took the halfway mix, which darkens toward
 * black on a dark theme (fine) and LIGHTENS toward silver on a pale one, where
 * it lands on grey-on-grey: measured on the win95 theme's #c0c0c0, dimmed
 * default text was 3.46:1 and dimmed blue and grey were both 2.10:1. Claude
 * Code, `ls`, git and diff all mark their secondary lines DIM, so on a light
 * theme most of the screen was the unreadable half.
 *
 * So the blend is the LARGEST one that still clears 4.5:1 rather than a fixed
 * half.
 *
 * ⚠ THIS DOES MOVE THE DARK THEMES A LITTLE, and that is deliberate — the
 * opposite of the invariant contrast.c holds for the panels. Measured on
 * gruvbox, the halfway mix is #89816d on #282828 = 3.81:1, already under the
 * floor, so t comes back 7 rather than 8. The panel invariant exists because an
 * accent is the THEME AUTHOR'S colour and correcting it overrides a deliberate
 * choice; a dimmed foreground is a colour SYNTTY DERIVES from two of theirs, so
 * there is no authored value here to preserve. A dark theme shifts by one
 * sixteenth at most, which is why the floor is allowed to be unconditional.
 *
 * The cost, stated because it is real: where the undimmed colour is itself near
 * the floor there is no blend left to make, and DIM becomes a no-op. The light
 * palette's blue is 4.59:1 on silver, so dim blue renders as plain blue. Losing
 * the de-emphasis is the better half of that trade — the alternative measured
 * 2.10:1.
 *
 * A fg that ALREADY fails against its own bg is left exactly where it is. It is
 * the program's colour, not ours to correct, and dimming it further is the only
 * outcome that is certainly wrong. */
static inline uint32_t dim_fg(uint32_t fg, uint32_t bg)
{
	for (unsigned t = 8; t > 0; t--) {
		uint32_t d = mix_16th(fg, bg, t);
		if (clears_4_5(d, bg))
			return d;
	}
	return fg;
}

/* Composite one coverage value. `cov` is 0..255 from the glyph atlas.
 *
 * Kept branch-free apart from the two ends, which are the overwhelming
 * majority of pixels in a terminal: a glyph box is mostly empty, and the parts
 * that are not are mostly solid. */
static inline uint32_t blend(uint32_t fg, uint32_t bg, uint8_t cov)
{
	if (cov == 0)   return bg;
	if (cov == 255) return fg;

	uint32_t a = cov, ia = 255 - cov;
	uint32_t rr = ((fg >> 16 & 0xFF) * a + (bg >> 16 & 0xFF) * ia) / 255;
	uint32_t gg = ((fg >>  8 & 0xFF) * a + (bg >>  8 & 0xFF) * ia) / 255;
	uint32_t bb = ((fg       & 0xFF) * a + (bg       & 0xFF) * ia) / 255;
	return rr << 16 | gg << 8 | bb;
}

static void fill_rect(uint32_t *px, int stride, int x, int y, int w, int h,
                      uint32_t c)
{
	for (int j = 0; j < h; j++) {
		uint32_t *row = px + (size_t)(y + j) * stride + x;
		for (int i = 0; i < w; i++)
			row[i] = c;
	}
}

/* One cell. `cw` may be twice the cell width for a double-width glyph, in
 * which case the caller has already skipped the tail cell. */
static void draw_cell(const st_render_t *r, uint32_t *px, int stride,
                      int x0, int y0, int cw, int ch,
                      const st_cell_t *cell, const st_style_t *st,
                      bool cursor, bool selected)
{
	uint32_t fg = resolve(r, st->fg, r->def_fg);
	uint32_t bg = resolve(r, st->bg, r->def_bg);
	uint16_t at = st->attrs;

	/* REVERSE FIRST, then the selection, then the cursor. Each is an
	 * inversion and they cancel in pairs, which is the behaviour to want: a
	 * cursor sitting on reversed text that stayed reversed would be invisible.
	 *
	 * ⚠ THE SELECTION IS AN INVERSION AND NOT A COLOUR, on purpose. A fixed
	 * highlight colour has to contrast with whatever is under it, and what is
	 * under it here is arbitrary — a program can paint any of 16 million
	 * backgrounds. Inverting whatever the cell already is contrasts with it by
	 * construction, in every theme, without a palette entry anybody has to
	 * check. See project_synui_pale_theme_legibility for how the other way
	 * goes. */
	if (at & ST_REVERSE) { uint32_t t = fg; fg = bg; bg = t; }
	if (selected)        { uint32_t t = fg; fg = bg; bg = t; }
	if (cursor) {
		if (r->cursor_set) { fg = r->cursor_fg; bg = r->cursor_bg; }
		else               { uint32_t t = fg; fg = bg; bg = t; }
	}

	if (at & ST_DIM)     fg = dim_fg(fg, bg);
	if (at & ST_HIDDEN)  fg = bg;

	fill_rect(px, stride, x0, y0, cw, ch, bg);

	/* A blank cell is most of a terminal screen. Nothing below would draw
	 * anything for it, and the glyph lookup alone is worth skipping. */
	if (cell->cp != 0 && cell->cp != ' ' && !(at & ST_HIDDEN)) {
		const st_glyph_t *gl = st_font_glyph(r->font, cell->cp, at);
		int gw = gl->w < cw ? gl->w : cw;
		int gh = gl->h < ch ? gl->h : ch;
		for (int j = 0; j < gh; j++) {
			const uint8_t *src = gl->bits + (size_t)j * gl->w;
			uint32_t *dst = px + (size_t)(y0 + j) * stride + x0;
			for (int i = 0; i < gw; i++)
				dst[i] = blend(fg, bg, src[i]);
		}
	}

	/* Lines last, so they cross the glyph rather than being covered by it —
	 * a strikethrough drawn before the text is a strikethrough nobody sees. */
	if (at & ST_UNDERLINE) {
		int uy = st_font_baseline(r->font) + 1;
		if (uy >= ch) uy = ch - 1;
		if (uy >= 0)  fill_rect(px, stride, x0, y0 + uy, cw, 1, fg);
	}
	if (at & ST_STRIKE) {
		int sy = st_font_baseline(r->font) / 2 + ch / 4;
		if (sy >= ch) sy = ch - 1;
		if (sy >= 0)  fill_rect(px, stride, x0, y0 + sy, cw, 1, fg);
	}
}

/* Paint one row of cells. Shared by the full and the damaged paths so there is
 * exactly one place that knows how a row becomes pixels — two implementations
 * of that would drift, and the whole value of damage tracking rests on the two
 * producing byte-identical output. */
static size_t draw_row(st_render_t *r, const st_grid_t *g, int row,
                       uint32_t *px, int stride_px, int w, int h)
{
	int cw = st_font_cell_w(r->font);
	int ch = st_font_cell_h(r->font);
	int y0 = r->origin_y + row * ch;
	if (y0 + ch > h)
		return 0;

	/* Through the viewport, not straight at the live screen — this is the one
	 * place in the program that knows the reader may be looking at history. */
	const st_row_t *rw = st_grid_view_row(g, row);
	if (!rw) {
		/* Above the oldest line kept: background, not stale pixels. */
		fill_rect(px, stride_px, 0, y0,
		          st_font_cell_w(r->font) * g->cols, ch, r->def_bg);
		return 0;
	}
	size_t drawn = 0;

	/* The highlighted span on this row, asked once rather than per cell: most
	 * rows have none, and the answer cannot change halfway across a row. */
	int sel_from = 0, sel_to = -1;
	st_sel_row_span(g, row, &sel_from, &sel_to);

	for (int col = 0; col < g->cols; ) {
		int x0 = col * cw;
		if (x0 + cw > w)
			break;

		static const st_cell_t empty = { 0, 0, 1, 0 };
		const st_cell_t *cell = (col < rw->len) ? &rw->cells[col] : &empty;

		/* ⚠ `width == 0` MEANS TWO THINGS, and telling them apart is the
		 * whole of this test.
		 *
		 * It is the tail of a double-width glyph — whose head has already
		 * painted both columns, so drawing it would blank the right half of
		 * the glyph. It is ALSO every erased cell, because a fresh row is one
		 * calloc and erase_line writes zeroes.
		 *
		 * Skipping on width alone therefore skipped every blank cell on the
		 * screen, which left them holding whatever was in the buffer: a 10x2
		 * grid reported 8 cells drawn out of 20, and the untouched 12 came out
		 * black rather than the background colour. The tail is the case where
		 * the PREVIOUS cell was a wide head — the same test st_dump_text uses
		 * in grid.c, deliberately, rather than a second opinion about the same
		 * invariant. */
		bool tail = cell->width == 0 && cell->cp == 0
		         && col > 0 && col - 1 < rw->len
		         && rw->cells[col - 1].width == 2;
		if (tail) {
			col++;
			continue;
		}

		int span = cell->width == 2 ? 2 : 1;
		if (col + span > g->cols)
			span = 1;

		/* ⚠ ONLY WHEN LIVE. A cursor painted over scrollback is a cursor in a
		 * position it cannot be in, and it moves as the history scrolls.
		 *
		 * ⚠ AND ONLY WHEN THE PROGRAM WANTS ONE. `g->cursor_visible` is
		 * DECTCEM — `ESC[?25l` — and vt.c has recorded it since stage 1 while
		 * NOTHING read it, so the block was drawn through every full-screen
		 * program that hides it. cmatrix moves the cursor along the bottom row
		 * as it writes, so the symptom was a block flickering across the
		 * bottom of the window in the theme's cursor colour, on top of an
		 * animation that had asked for no cursor at all. */
		bool cursor = r->show_cursor && g->cursor_visible && g->view == 0
		           && row == g->cy && col == g->cx;
		draw_cell(r, px, stride_px, x0, y0, cw * span, ch,
		          cell, st_style_get(g, cell->style), cursor,
		          col >= sel_from && col <= sel_to);
		drawn++;
		col += span;
	}
	return drawn;
}

/* The strip on the right and along the bottom that no cell covers — the window
 * is rarely an exact multiple of the cell box. Filled with the default
 * background: leaving it is how a resized terminal shows a band of whatever the
 * compositor last had in that memory. */
static void fill_margins(st_render_t *r, const st_grid_t *g,
                         uint32_t *px, int stride_px, int w, int h)
{
	int cw = st_font_cell_w(r->font), ch = st_font_cell_h(r->font);
	int used_w = cw * g->cols;
	int used_h = r->origin_y + ch * g->rows;
	if (used_w > w) used_w = w;
	if (used_h > h) used_h = h;
	/* From the origin down: everything above it belongs to whoever draws
	 * there, and filling it here would erase the tab bar every frame. */
	if (used_w < w)
		fill_rect(px, stride_px, used_w, r->origin_y, w - used_w,
		          h - r->origin_y, r->def_bg);
	if (used_h < h)
		fill_rect(px, stride_px, 0, used_h, used_w, h - used_h, r->def_bg);
}

/* ── images, over the top of the cells ──────────────────────────────────────
 *
 * The graphics protocol places an image across a rectangle of CELLS, so the
 * image is scaled to that rectangle. Nearest-neighbour: a terminal image is
 * usually already the size it wants to be, the sender chose the cell count to
 * match, and a smoothing filter would cost more than the whole blit for a
 * difference nobody asked for.
 *
 * Drawn AFTER the cells and blended by alpha, because that is what the protocol
 * means — an image sits on top of the text cell it covers, and a transparent
 * PNG is expected to show the background through it. */
static void draw_images(st_render_t *r, const st_grid_t *g,
                        uint32_t *px, int stride_px, int w, int h)
{
	if (!r->gfx)
		return;

	st_gfx_place_t pl[64];
	int n = st_gfx_placements(r->gfx, pl, 64);
	int cw = st_font_cell_w(r->font), ch = st_font_cell_h(r->font);

	for (int i = 0; i < n; i++) {
		int x0 = pl[i].col * cw, y0 = r->origin_y + pl[i].row * ch;
		int dw = pl[i].cols * cw, dh = pl[i].rows * ch;
		if (pl[i].w == 0 || pl[i].h == 0)
			continue;

		for (int y = 0; y < dh; y++) {
			int dy = y0 + y;
			if (dy < 0 || dy >= h)
				continue;
			uint32_t sy = (uint32_t)((uint64_t)y * pl[i].h / (uint32_t)dh);
			if (sy >= pl[i].h)
				sy = pl[i].h - 1;
			uint32_t *dst = px + (size_t)dy * stride_px;

			for (int x = 0; x < dw; x++) {
				int dx = x0 + x;
				if (dx < 0 || dx >= w)
					continue;
				uint32_t sx = (uint32_t)((uint64_t)x * pl[i].w / (uint32_t)dw);
				if (sx >= pl[i].w)
					sx = pl[i].w - 1;

				const uint8_t *s4 = pl[i].rgba + ((size_t)sy * pl[i].w + sx) * 4;
				uint8_t a = s4[3];
				if (a == 0)
					continue;
				uint32_t src = (uint32_t)s4[0] << 16 | (uint32_t)s4[1] << 8 | s4[2];
				dst[dx] = a == 255 ? src : blend(src, dst[dx], a);
			}
		}
	}
	(void)g;
}

size_t st_render_grid(st_render_t *r, const st_grid_t *g,
                      uint32_t *px, int stride_px, int w, int h)
{
	fill_margins(r, g, px, stride_px, w, h);

	size_t drawn = 0;
	for (int row = 0; row < g->rows; row++)
		drawn += draw_row(r, g, row, px, stride_px, w, h);
	draw_images(r, g, px, stride_px, w, h);
	return drawn;
}

/* ── the damaged path ───────────────────────────────────────────────────────
 *
 * Paint only the rows the caller names. Everything else in the buffer is left
 * exactly as it was, which is the entire saving and also the entire risk: a row
 * that changed and was not named keeps its old pixels, and stale pixels look
 * like memory corruption rather than like a missed update.
 *
 * The margins are NOT repainted here — they never change without a resize, and
 * a resize marks every row anyway.
 *
 * `rows` is one byte per grid row, non-zero meaning "repaint". A byte rather
 * than a bitmap because the caller has to OR two of these together per frame
 * (see the double-buffer note in win.c) and bytes make that a memcpy-shaped
 * loop instead of a bit-twiddling one, for a few dozen bytes. */
size_t st_render_rows(st_render_t *r, const st_grid_t *g, const uint8_t *rows,
                      uint32_t *px, int stride_px, int w, int h)
{
	size_t drawn = 0;
	for (int row = 0; row < g->rows; row++)
		if (rows[row])
			drawn += draw_row(r, g, row, px, stride_px, w, h);
	/* ⚠ Redrawn WHOLE whenever anything was. An image overlaps cells, so a row
	 * repainted underneath one wipes the part of it that sat on that row — and
	 * the image itself is not in the grid, so nothing would ever mark it dirty
	 * again. Cheap: placements are rare and usually one. */
	if (drawn)
		draw_images(r, g, px, stride_px, w, h);
	return drawn;
}

/* ── text that is not the grid ──────────────────────────────────────────────
 *
 * The tab bar. It is drawn with the SAME font and the same glyph atlas as
 * everything else, which is the point of putting it here rather than in win.c:
 * a second text path would mean a second set of decisions about coverage,
 * baselines and wide characters, and they would drift.
 *
 * Clipped to `max_w` rather than wrapped, and ⚠ TRUNCATED ON A CODEPOINT
 * BOUNDARY — cutting a UTF-8 sequence in half produces a byte the decoder
 * cannot use and a replacement box on the end of every long title. Returns the
 * x it stopped at, so the caller can lay segments out left to right without
 * measuring them twice. */
int st_render_text(st_render_t *r, uint32_t *px, int stride_px, int w, int h,
                   int x, int y, int max_w, const char *utf8,
                   uint32_t fg, uint32_t bg)
{
	int cw = st_font_cell_w(r->font), ch = st_font_cell_h(r->font);
	if (y < 0 || y + ch > h || x >= w)
		return x;

	int end = x + max_w;
	if (end > w)
		end = w;
	if (end > x)
		fill_rect(px, stride_px, x, y, end - x, ch, bg);

	const unsigned char *p = (const unsigned char *)utf8;
	while (*p && x + cw <= end) {
		/* One codepoint, from as many bytes as it takes. */
		uint32_t cp = *p;
		int need = 0;
		if      (cp < 0x80)          { need = 0; }
		else if ((cp & 0xE0) == 0xC0) { cp &= 0x1F; need = 1; }
		else if ((cp & 0xF0) == 0xE0) { cp &= 0x0F; need = 2; }
		else if ((cp & 0xF8) == 0xF0) { cp &= 0x07; need = 3; }
		else                          { p++; continue; }   /* a stray tail */
		p++;
		for (int i = 0; i < need; i++) {
			if ((*p & 0xC0) != 0x80) { cp = 0; break; }
			cp = cp << 6 | (*p++ & 0x3F);
		}
		if (!cp)
			continue;

		int span = st_char_width(cp);
		if (span < 1)
			continue;
		if (x + span * cw > end)
			break;

		const st_glyph_t *gl = st_font_glyph(r->font, cp, 0);
		int gw = gl->w < span * cw ? gl->w : span * cw;
		int gh = gl->h < ch ? gl->h : ch;
		for (int j = 0; j < gh; j++) {
			const uint8_t *src = gl->bits + (size_t)j * gl->w;
			uint32_t *dst = px + (size_t)(y + j) * stride_px + x;
			for (int i = 0; i < gw; i++)
				dst[i] = blend(fg, bg, src[i]);
		}
		x += span * cw;
	}
	return x;
}

/* ── PPM, for the test suite and for a person's eyes ────────────────────────
 *
 * The renderer's golden output. Stage 1 could assert on text because its
 * output was text; a renderer's output is pixels, and a test that only checks
 * "it returned" passes on an all-black screen. PPM because it is six lines of
 * code to write, every image viewer opens it, and `cmp` compares two of them
 * meaningfully. */
void st_render_write_ppm(const uint32_t *px, int stride_px, int w, int h,
                         FILE *out)
{
	fprintf(out, "P6\n%d %d\n255\n", w, h);
	uint8_t *line = xmalloc((size_t)w * 3);
	for (int y = 0; y < h; y++) {
		const uint32_t *src = px + (size_t)y * stride_px;
		for (int x = 0; x < w; x++) {
			line[x * 3 + 0] = (uint8_t)(src[x] >> 16);
			line[x * 3 + 1] = (uint8_t)(src[x] >>  8);
			line[x * 3 + 2] = (uint8_t)(src[x]);
		}
		fwrite(line, 1, (size_t)w * 3, out);
	}
	free(line);
}
