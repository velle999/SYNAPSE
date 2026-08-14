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
	r->def_bg      = 0x1B1F26;
	r->def_fg      = 0xC8CDD6;
	r->cursor_bg   = 0xC8CDD6;
	r->cursor_fg   = 0x1B1F26;
	r->show_cursor = true;
	return r;
}

void st_render_free(st_render_t *r) { free(r); }

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

/* Halfway between two colours, per channel. Used for ST_DIM, which is a real
 * attribute programs use for de-emphasis — rendering it as plain fg makes
 * `ls` output and diff headers indistinguishable from ordinary text. */
static inline uint32_t mix_half(uint32_t a, uint32_t b)
{
	return (((a >> 16 & 0xFF) + (b >> 16 & 0xFF)) / 2) << 16
	     | (((a >>  8 & 0xFF) + (b >>  8 & 0xFF)) / 2) <<  8
	     | (((a       & 0xFF) + (b       & 0xFF)) / 2);
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

	if (at & ST_DIM)     fg = mix_half(fg, bg);
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
		 * position it cannot be in, and it moves as the history scrolls. */
		bool cursor = r->show_cursor && g->view == 0
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
