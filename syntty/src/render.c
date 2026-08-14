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
	uint32_t   palette[256];
	uint32_t   def_fg, def_bg;
	uint32_t   cursor_fg, cursor_bg;
	bool       show_cursor;
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

void st_render_colors(st_render_t *r, uint32_t fg, uint32_t bg)
{
	r->def_fg = fg;
	r->def_bg = bg;
}

void st_render_cursor(st_render_t *r, bool on) { r->show_cursor = on; }

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
                      bool cursor)
{
	uint32_t fg = resolve(r, st->fg, r->def_fg);
	uint32_t bg = resolve(r, st->bg, r->def_bg);
	uint16_t at = st->attrs;

	/* REVERSE FIRST, then the cursor. A cell that is already reversed and
	 * also under the cursor must come back to normal rather than staying
	 * swapped — two inversions cancel, and a cursor sitting on reversed text
	 * that stays reversed is invisible. */
	if (at & ST_REVERSE) { uint32_t t = fg; fg = bg; bg = t; }
	if (cursor)          { uint32_t t = fg; fg = bg; bg = t; }

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

size_t st_render_grid(st_render_t *r, const st_grid_t *g,
                      uint32_t *px, int stride_px, int w, int h)
{
	int cw = st_font_cell_w(r->font);
	int ch = st_font_cell_h(r->font);
	size_t drawn = 0;

	/* The window is rarely an exact multiple of the cell box, so there is a
	 * strip on the right and one along the bottom that no cell covers. Filled
	 * with the default background: leaving it is how a resized terminal shows
	 * a band of whatever the compositor last had in that memory. */
	int used_w = cw * g->cols, used_h = ch * g->rows;
	if (used_w > w) used_w = w;
	if (used_h > h) used_h = h;
	if (used_w < w) fill_rect(px, stride_px, used_w, 0, w - used_w, h, r->def_bg);
	if (used_h < h) fill_rect(px, stride_px, 0, used_h, used_w, h - used_h, r->def_bg);

	for (int row = 0; row < g->rows; row++) {
		int y0 = row * ch;
		if (y0 + ch > h)
			break;
		const st_row_t *rw = &g->screen[row];

		for (int col = 0; col < g->cols; ) {
			int x0 = col * cw;
			if (x0 + cw > w)
				break;

			/* A row stores only the cells it has: past `len` it is blank, and
			 * reading there would be reading past the allocation. */
			static const st_cell_t empty = { 0, 0, 1, 0 };
			const st_cell_t *cell = (col < rw->len) ? &rw->cells[col] : &empty;

			/* ⚠ `width == 0` MEANS TWO THINGS, and telling them apart is the
			 * whole of this test.
			 *
			 * It is the tail of a double-width glyph — whose head has already
			 * painted both columns, so drawing it would blank the right half
			 * of the glyph. It is ALSO every erased cell, because a fresh row
			 * is one calloc and erase_line writes zeroes.
			 *
			 * Skipping on width alone therefore skipped every blank cell on
			 * the screen, which left them holding whatever was in the buffer:
			 * a 10x2 grid reported 8 cells drawn out of 20, and the untouched
			 * 12 came out black rather than the background colour. The tail is
			 * the case where the PREVIOUS cell was a wide head — which is the
			 * same test st_dump_text uses in grid.c, deliberately, rather than
			 * a second opinion about the same invariant. */
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

			bool cursor = r->show_cursor && row == g->cy && col == g->cx;
			draw_cell(r, px, stride_px, x0, y0, cw * span, ch,
			          cell, st_style_get(g, cell->style), cursor);
			drawn++;
			col += span;
		}
	}
	return drawn;
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
