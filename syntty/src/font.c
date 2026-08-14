/* font.c — glyphs, and the startup cost of finding them.
 *
 * This file exists because of one row in the table this project is built on:
 * kitty spends 230 ms before it can run `true` and foot spends 25 ms. Three
 * things account for that gap — interpreter import, GL context creation, and
 * FONT DISCOVERY — and this is the third one.
 *
 * ── Why fontconfig is not simply called ────────────────────────────────────
 *
 * Asking fontconfig "which font is Monospace" is the correct question and it is
 * not a cheap one: FcInit builds or validates a cache over every font directory
 * on the machine. On a warm cache that is a few milliseconds; on a cold one it
 * is seconds. Either way it is the same answer every time, for months, because
 * the answer only changes when fonts are installed or removed.
 *
 * So it is asked ONCE and the answer is written to
 * $XDG_CACHE_HOME/syntty/fonts.v1 — the resolved file path and face index, with
 * the font file's size and mtime beside them. A later start reads that one
 * small file, checks the font it names is still the same file, and hands the
 * path straight to FreeType. fontconfig is never initialised on that path.
 *
 * ⚠ The validation is what makes this honest rather than merely fast. A cache
 * that is trusted blindly survives the font being upgraded, renamed or removed,
 * and the failure is a terminal that draws nothing or dies at startup — for a
 * reason nobody would connect to a package upgrade weeks earlier. Cheap to
 * check: one stat(), and any mismatch falls back to asking properly and
 * rewriting the cache.
 *
 * ── The atlas ──────────────────────────────────────────────────────────────
 *
 * A terminal draws the same hundred glyphs over and over, so they are
 * rasterised once into a fixed cell box and kept as 8-bit coverage. ASCII 32..126
 * is rasterised up front because every one of them is certain to be needed;
 * everything else arrives on demand and is kept in a small hash. Coverage, not
 * colour: the blend against fg and bg happens in render.c, so one rasterised
 * glyph serves every colour it is ever drawn in.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

/* ── the four faces ─────────────────────────────────────────────────────────
 *
 * Regular, bold, italic and bold-italic. They are loaded LAZILY and separately:
 * a session that never prints bold never opens the bold face, and most never
 * print italic at all. Loading four faces at startup to satisfy a style that
 * may not appear would be paying the startup budget for the uncommon case. */
enum { FACE_REGULAR, FACE_BOLD, FACE_ITALIC, FACE_BOLDITALIC, FACE_COUNT };

/* How many distinct fallback fonts one session may open. A terminal showing
 * mixed scripts touches two or three; anything near this bound is drawing the
 * whole of Unicode, and an unbounded chain would hold a file open per script. */
#define FALLBACK_MAX 8

#define ASCII_LO 32
#define ASCII_HI 126
#define ASCII_N  (ASCII_HI - ASCII_LO + 1)

typedef struct {
	uint32_t     cp;
	st_glyph_t   g;
	bool         used;
} glyph_slot_t;

typedef struct {
	FT_Face      face;
	bool         tried;      /* opened, or attempted and failed */
	char        *path;
	int          index;

	/* ASCII 32..126, contiguous. One allocation, indexed by cp - 32, so the
	 * common case is a bounds check and a multiply rather than a hash. */
	st_glyph_t  *ascii;
	uint8_t     *ascii_bits;

	/* Everything else, open-addressed. Sized as a power of two and grown by
	 * rehashing; a terminal session touches a few hundred distinct non-ASCII
	 * codepoints at most. */
	glyph_slot_t *tab;
	uint32_t      tab_mask, tab_used;
} face_t;

struct st_font {
	FT_Library  ft;
	double      size_px;
	char       *family;

	face_t      faces[FACE_COUNT];

	/* Cell metrics, taken from the regular face and used by all four: a
	 * terminal grid is defined by one box, and a bold face whose advance is a
	 * pixel wider does not get to widen the grid under it. */
	int cell_w, cell_h, baseline;

	/* The blank glyph. Returned for anything that cannot be rasterised, so a
	 * caller never has to hold a NULL check in the blit loop. */
	st_glyph_t blank;

	/* Fonts opened only to cover characters the chosen face cannot draw.
	 * Deduplicated by path — one fallback usually covers a whole script. */
	struct { char *path; FT_Face face; } fallback[FALLBACK_MAX];
	int nfallback;

	st_font_stats_t stats;
};

/* ── the resolved-font cache ───────────────────────────────────────────────
 *
 * One line per style, written as text rather than a struct: it is read once at
 * startup, it is tiny, and a cache a person can `cat` when a terminal comes up
 * in the wrong font is worth more than the microseconds a binary format saves.
 *
 *   <style>\t<size>\t<mtime>\t<path>
 */
#define CACHE_MAGIC "syntty-fonts-1\n"

static char *cache_path(void)
{
	const char *base = getenv("XDG_CACHE_HOME");
	char *dir;
	if (base && *base) {
		dir = xasprintf("%s/syntty", base);
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home)
			return NULL;
		dir = xasprintf("%s/.cache/syntty", home);
	}
	/* Best effort. A cache that cannot be created costs a fontconfig lookup
	 * per start and nothing else, so it is never worth failing over. */
	mkdir(dir, 0700);
	char *p = xasprintf("%s/fonts.v1", dir);
	free(dir);
	return p;
}

/* Is the cached answer still true? One stat, comparing the size and mtime the
 * lookup was recorded against. A font upgraded in place keeps its path and
 * changes both. */
static bool cache_entry_valid(const char *path, long long want_size,
                              long long want_mtime)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	return (long long)st.st_size == want_size
	    && (long long)st.st_mtime == want_mtime;
}

/* Look for `spec` in the cache. Returns a malloc'd path, or NULL. */
static char *cache_lookup(const char *spec, int *index_out)
{
	char *cp = cache_path();
	if (!cp)
		return NULL;

	FILE *f = fopen(cp, "r");
	free(cp);
	if (!f)
		return NULL;

	char line[1024];
	if (!fgets(line, sizeof line, f) || strcmp(line, CACHE_MAGIC) != 0) {
		fclose(f);
		return NULL;
	}

	char *found = NULL;
	while (fgets(line, sizeof line, f)) {
		/* spec \t index \t size \t mtime \t path \n */
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';

		char *save = NULL;
		char *f_spec  = strtok_r(line, "\t", &save);
		char *f_index = strtok_r(NULL, "\t", &save);
		char *f_size  = strtok_r(NULL, "\t", &save);
		char *f_mtime = strtok_r(NULL, "\t", &save);
		char *f_path  = strtok_r(NULL, "\t", &save);
		if (!f_spec || !f_index || !f_size || !f_mtime || !f_path)
			continue;
		if (strcmp(f_spec, spec) != 0)
			continue;
		if (!cache_entry_valid(f_path, atoll(f_size), atoll(f_mtime)))
			break;          /* stale: fall through to a real lookup */
		*index_out = atoi(f_index);
		found = xstrdup(f_path);
		break;
	}
	fclose(f);
	return found;
}

/* Rewrite the cache with `spec` resolved to `path`. Every other entry is kept,
 * so resolving the bold face does not throw away the regular one. */
static void cache_store(const char *spec, const char *path, int index)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return;

	char *cp = cache_path();
	if (!cp)
		return;

	/* Read what is there, minus any entry for this spec. */
	char  *keep[64];
	int    nkeep = 0;
	FILE  *in = fopen(cp, "r");
	if (in) {
		char line[1024];
		if (fgets(line, sizeof line, in) && strcmp(line, CACHE_MAGIC) == 0) {
			while (nkeep < 64 && fgets(line, sizeof line, in)) {
				if (strncmp(line, spec, strlen(spec)) == 0
				    && line[strlen(spec)] == '\t')
					continue;
				keep[nkeep++] = xstrdup(line);
			}
		}
		fclose(in);
	}

	/* ⚠ Written to a temporary and renamed, never opened in place. Two
	 * terminals starting at once is the ordinary case, not a rare one, and a
	 * half-written cache read by the other is a startup failure that would
	 * reproduce roughly never. rename(2) is atomic within a directory. */
	char *tmp = xasprintf("%s.tmp.%ld", cp, (long)getpid());
	FILE *out = fopen(tmp, "w");
	if (out) {
		fputs(CACHE_MAGIC, out);
		for (int i = 0; i < nkeep; i++)
			fputs(keep[i], out);
		fprintf(out, "%s\t%d\t%lld\t%lld\t%s\n", spec, index,
		        (long long)st.st_size, (long long)st.st_mtime, path);
		fclose(out);
		if (rename(tmp, cp) != 0)
			unlink(tmp);
	}
	free(tmp);
	for (int i = 0; i < nkeep; i++)
		free(keep[i]);
	free(cp);
}

/* ── asking fontconfig, which is the slow path and runs once ───────────────
 *
 * Deliberately the only place in this program that touches fontconfig, and
 * deliberately reached only on a cache miss. */
static char *fc_resolve(const char *spec, int *index_out);
/* A font that covers ONE character. The fallback path, asked once per distinct
 * codepoint the chosen face cannot draw. */
static char *fc_resolve_cp(uint32_t cp);

/* ── faces ─────────────────────────────────────────────────────────────────*/

static const char *face_spec(int which, const char *family)
{
	static char buf[256];
	static const char *suffix[FACE_COUNT] = {
		"", ":bold", ":italic", ":bold:italic"
	};
	snprintf(buf, sizeof buf, "%s%s", family, suffix[which]);
	return buf;
}

static bool face_load(st_font_t *f, int which, const char *family)
{
	face_t *fa = &f->faces[which];
	if (fa->tried)
		return fa->face != NULL;
	fa->tried = true;

	const char *spec = face_spec(which, family);
	int index = 0;

	uint64_t t0 = now_ns();
	char *path = cache_lookup(spec, &index);
	if (!path) {
		/* THE SLOW PATH, and the one the cache exists to skip. Recorded rather
		 * than merely taken: `syntty font` prints which of the two answered,
		 * so the claim that the cache is doing anything is checkable instead
		 * of assumed. */
		f->stats.used_fontconfig = true;
		path = fc_resolve(spec, &index);
		if (path)
			cache_store(spec, path, index);
	}
	if (which == FACE_REGULAR)
		f->stats.lookup_ms = (double)(now_ns() - t0) / 1e6;

	if (!path)
		return false;

	if (FT_New_Face(f->ft, path, index, &fa->face) != 0) {
		/* The cache said this file was a font and FreeType disagrees. Ask
		 * again rather than giving up: the usual cause is a cache written
		 * against a font that has since been replaced by something else at
		 * the same path with the same size, which stat cannot see. */
		free(path);
		path = fc_resolve(spec, &index);
		if (!path)
			return false;
		if (FT_New_Face(f->ft, path, index, &fa->face) != 0) {
			free(path);
			return false;
		}
		cache_store(spec, path, index);
	}

	FT_Set_Pixel_Sizes(fa->face, 0, (FT_UInt)(f->size_px + 0.5));
	fa->path  = path;
	fa->index = index;
	return true;
}

/* ── rasterising into the cell box ─────────────────────────────────────────
 *
 * The bitmap FreeType produces is glyph-sized and positioned by a bearing; what
 * the renderer wants is a fixed cell-sized box it can blit without arithmetic.
 * So each glyph is composited into its own box once, here, CLIPPED to it.
 *
 * Clipping is the right answer rather than a compromise: a monospace grid has
 * exactly this much room per cell, and a glyph that overflows would otherwise
 * paint into a neighbour that has already been drawn — which shows up as
 * fragments left behind when the neighbour scrolls away. */
static bool raster_into(st_font_t *f, FT_Face face, uint32_t cp,
                        st_glyph_t *out, uint8_t *bits)
{
	int cw = f->cell_w, ch = f->cell_h;
	memset(bits, 0, (size_t)cw * ch * (out->cols > 1 ? 2 : 1));

	out->bits = bits;
	out->w    = cw * (out->cols > 1 ? 2 : 1);
	out->h    = ch;

	/* Returns FALSE when this face has no glyph for the codepoint, which is
	 * what sends the caller to the fallback chain. A blank box and "this font
	 * cannot draw it" are different answers and the caller needs both: the
	 * first is correct for a space, the second is a font problem to solve. */
	FT_UInt gi = FT_Get_Char_Index(face, cp);
	if (gi == 0)
		return false;
	if (FT_Load_Glyph(face, gi, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
		return false;

	FT_GlyphSlot s = face->glyph;
	FT_Bitmap   *b = &s->bitmap;

	int ox = s->bitmap_left;
	int oy = f->baseline - s->bitmap_top;

	for (int y = 0; y < (int)b->rows; y++) {
		int dy = oy + y;
		if (dy < 0 || dy >= ch)
			continue;
		const uint8_t *src = b->buffer + (size_t)y * b->pitch;
		uint8_t *dst = bits + (size_t)dy * out->w;
		for (int x = 0; x < (int)b->width; x++) {
			int dx = ox + x;
			if (dx < 0 || dx >= out->w)
				continue;
			/* max, not assignment: a synthesised bold (below) draws the same
			 * glyph twice, one pixel apart, into this same box. */
			if (src[x] > dst[dx])
				dst[dx] = src[x];
		}
	}
	return true;
}

/* ── fallback: a font that can draw THIS character ─────────────────────────
 *
 * Kept per-font rather than per-face: whether a codepoint exists is a property
 * of the character, not of whether the text was bold, and asking four times for
 * the same missing glyph would open four copies of the same fallback file.
 *
 * The opened faces are deduplicated by path, because one fallback font
 * typically covers a whole script — the first CJK character opens Noto Sans CJK
 * and every other CJK character in the session reuses it. */
static FT_Face fallback_face(st_font_t *f, uint32_t cp)
{
	char *path = fc_resolve_cp(cp);
	if (!path)
		return NULL;

	for (int i = 0; i < f->nfallback; i++) {
		if (strcmp(f->fallback[i].path, path) == 0) {
			free(path);
			return f->fallback[i].face;
		}
	}
	if (f->nfallback == FALLBACK_MAX) {
		/* A session that has opened this many distinct fallback fonts is
		 * either drawing the whole of Unicode or leaking; either way, stop
		 * opening files rather than growing without a bound. */
		free(path);
		return NULL;
	}

	FT_Face face = NULL;
	if (FT_New_Face(f->ft, path, 0, &face) != 0) {
		free(path);
		return NULL;
	}
	FT_Set_Pixel_Sizes(face, 0, (FT_UInt)(f->size_px + 0.5));

	f->fallback[f->nfallback].path = path;
	f->fallback[f->nfallback].face = face;
	f->nfallback++;
	f->stats.fallbacks = (uint32_t)f->nfallback;
	return face;
}

static void face_build_ascii(st_font_t *f, face_t *fa)
{
	size_t per = (size_t)f->cell_w * f->cell_h;
	fa->ascii_bits = xcalloc(ASCII_N, per);
	fa->ascii      = xcalloc(ASCII_N, sizeof *fa->ascii);

	for (int i = 0; i < ASCII_N; i++) {
		fa->ascii[i].cols = 1;
		raster_into(f, fa->face, (uint32_t)(ASCII_LO + i), &fa->ascii[i],
		            fa->ascii_bits + (size_t)i * per);
	}
}

/* ── the on-demand table for everything that is not ASCII ──────────────────*/

static uint32_t cp_hash(uint32_t cp)
{
	/* A finalising mix, because codepoints are dense and consecutive: the low
	 * bits of a Unicode block are the bits an open-addressed table collides
	 * on, and a run of CJK would otherwise pile into one cluster. */
	cp ^= cp >> 16; cp *= 0x7feb352dU;
	cp ^= cp >> 15; cp *= 0x846ca68bU;
	cp ^= cp >> 16;
	return cp;
}

static void tab_grow(face_t *fa)
{
	uint32_t old_mask = fa->tab_mask;
	glyph_slot_t *old = fa->tab;

	fa->tab_mask = old ? (old_mask << 1 | 1) : 63;
	fa->tab      = xcalloc(fa->tab_mask + 1, sizeof *fa->tab);
	fa->tab_used = 0;

	if (!old)
		return;
	for (uint32_t i = 0; i <= old_mask; i++) {
		if (!old[i].used)
			continue;
		uint32_t j = cp_hash(old[i].cp) & fa->tab_mask;
		while (fa->tab[j].used)
			j = (j + 1) & fa->tab_mask;
		fa->tab[j] = old[i];
		fa->tab_used++;
	}
	free(old);
}

static st_glyph_t *tab_find(face_t *fa, uint32_t cp)
{
	if (!fa->tab)
		return NULL;
	uint32_t j = cp_hash(cp) & fa->tab_mask;
	while (fa->tab[j].used) {
		if (fa->tab[j].cp == cp)
			return &fa->tab[j].g;
		j = (j + 1) & fa->tab_mask;
	}
	return NULL;
}

static st_glyph_t *tab_insert(face_t *fa, uint32_t cp)
{
	if (!fa->tab || (fa->tab_used + 1) * 4 > (fa->tab_mask + 1) * 3)
		tab_grow(fa);
	uint32_t j = cp_hash(cp) & fa->tab_mask;
	while (fa->tab[j].used)
		j = (j + 1) & fa->tab_mask;
	fa->tab[j].used = true;
	fa->tab[j].cp   = cp;
	fa->tab_used++;
	return &fa->tab[j].g;
}

/* ── public ─────────────────────────────────────────────────────────────── */

st_font_t *st_font_open(const char *family, double size_px, char **err)
{
	if (!family || !*family)
		family = "monospace";

	uint64_t t_open = now_ns();

	st_font_t *f = xcalloc(1, sizeof *f);
	f->size_px = size_px > 0 ? size_px : 14.0;
	f->family  = xstrdup(family);

	if (FT_Init_FreeType(&f->ft) != 0) {
		if (err) *err = xstrdup("FreeType would not initialise");
		free(f->family);
		free(f);
		return NULL;
	}

	if (!face_load(f, FACE_REGULAR, family)) {
		if (err) *err = xasprintf("no font matched '%s'", family);
		FT_Done_FreeType(f->ft);
		free(f->family);
		free(f);
		return NULL;
	}

	FT_Face fc = f->faces[FACE_REGULAR].face;

	/* THE CELL BOX.
	 *
	 * Width is the advance of a character every monospace font has and none
	 * renders narrow — not `max_advance`, which in many fonts is set by some
	 * wide symbol nobody prints and yields a grid with a visible gap between
	 * every column.
	 *
	 * Height comes from the face's own ascender and descender rather than from
	 * the pixel size: asking for 14 px and spacing rows 14 px apart clips every
	 * descender in the font. */
	FT_UInt gi = FT_Get_Char_Index(fc, 'M');
	if (gi && FT_Load_Glyph(fc, gi, FT_LOAD_DEFAULT) == 0)
		f->cell_w = (int)(fc->glyph->advance.x >> 6);
	if (f->cell_w <= 0)
		f->cell_w = (int)(f->size_px * 0.6 + 0.5);

	int asc  = (int)(fc->size->metrics.ascender  >> 6);
	int desc = (int)(fc->size->metrics.descender >> 6);   /* negative */
	f->cell_h   = asc - desc;
	f->baseline = asc;
	if (f->cell_h <= 0) {
		f->cell_h   = (int)(f->size_px * 1.2 + 0.5);
		f->baseline = (int)(f->size_px + 0.5);
	}

	face_build_ascii(f, &f->faces[FACE_REGULAR]);

	/* One shared empty box, so the blit loop never holds a NULL check. */
	f->blank.bits = xcalloc((size_t)f->cell_w * f->cell_h, 1);
	f->blank.w    = f->cell_w;
	f->blank.h    = f->cell_h;
	f->blank.cols = 1;

	f->stats.ascii_glyphs = ASCII_N;
	f->stats.atlas_bytes  = (size_t)ASCII_N * f->cell_w * f->cell_h;
	snprintf(f->stats.path, sizeof f->stats.path, "%s",
	         f->faces[FACE_REGULAR].path ? f->faces[FACE_REGULAR].path : "");
	f->stats.open_ms = (double)(now_ns() - t_open) / 1e6;
	return f;
}

const st_font_stats_t *st_font_get_stats(const st_font_t *f)
{
	return &f->stats;
}

void st_font_close(st_font_t *f)
{
	if (!f)
		return;
	for (int i = 0; i < FACE_COUNT; i++) {
		face_t *fa = &f->faces[i];
		if (fa->face)
			FT_Done_Face(fa->face);
		free(fa->path);
		free(fa->ascii);
		free(fa->ascii_bits);
		if (fa->tab) {
			for (uint32_t j = 0; j <= fa->tab_mask; j++)
				if (fa->tab[j].used)
					free(fa->tab[j].g.bits);
			free(fa->tab);
		}
	}
	for (int i = 0; i < f->nfallback; i++) {
		FT_Done_Face(f->fallback[i].face);
		free(f->fallback[i].path);
	}
	FT_Done_FreeType(f->ft);
	free(f->blank.bits);
	free(f->family);
	free(f);
}

int st_font_cell_w(const st_font_t *f)    { return f->cell_w; }
int st_font_cell_h(const st_font_t *f)    { return f->cell_h; }
int st_font_baseline(const st_font_t *f)  { return f->baseline; }

const st_glyph_t *st_font_glyph(st_font_t *f, uint32_t cp, uint16_t attrs)
{
	if (cp == 0 || cp == ' ')
		return &f->blank;

	int which = FACE_REGULAR;
	if ((attrs & ST_BOLD) && (attrs & ST_ITALIC)) which = FACE_BOLDITALIC;
	else if (attrs & ST_BOLD)                     which = FACE_BOLD;
	else if (attrs & ST_ITALIC)                   which = FACE_ITALIC;

	/* A face that will not load falls back to regular rather than to nothing.
	 * A session in a font with no italic should look wrong, not go blank. */
	if (which != FACE_REGULAR && !face_load(f, which, f->family))
		which = FACE_REGULAR;
	face_t *fa = &f->faces[which];

	if (which != FACE_REGULAR && !fa->ascii)
		face_build_ascii(f, fa);

	if (cp >= ASCII_LO && cp <= ASCII_HI)
		return &fa->ascii[cp - ASCII_LO];

	st_glyph_t *g = tab_find(fa, cp);
	if (g)
		return g;

	int cols = st_char_width(cp);
	if (cols <= 0)
		return &f->blank;

	g = tab_insert(fa, cp);
	g->cols = (uint8_t)cols;
	uint8_t *bits = xcalloc((size_t)f->cell_w * cols * f->cell_h, 1);

	/* THE FALLBACK CHAIN, and the reason it is not optional.
	 *
	 * The chosen monospace font covers Latin and very little else. Noto Sans
	 * Mono — what "monospace" resolves to on this machine — has no CJK at all,
	 * so 日本語 rasterised as three empty boxes: correctly SPACED, because the
	 * width table is independent of the font, and completely invisible. That
	 * is the worst shape a bug can have, because the layout looks right.
	 *
	 * So a codepoint the face cannot draw goes to fontconfig, which is asked
	 * for a font that covers that exact character. Once per distinct
	 * codepoint, on the miss — never for ASCII, which the atlas answered long
	 * before this line. */
	if (!raster_into(f, fa->face, cp, g, bits)) {
		FT_Face fb = fallback_face(f, cp);
		if (fb)
			raster_into(f, fb, cp, g, bits);
	}
	return g;
}

/* ── fontconfig, isolated ───────────────────────────────────────────────────
 *
 * Kept at the bottom and behind one function so that everything above is about
 * glyphs. This is the only code that knows fontconfig exists, and it runs on a
 * cold cache and never again. */
#include <fontconfig/fontconfig.h>

static char *fc_resolve(const char *spec, int *index_out)
{
	if (!FcInit())
		return NULL;

	/* The spec is `family[:bold][:italic]`, which is fontconfig's own name
	 * syntax, so it is parsed by fontconfig rather than by a second parser
	 * here that would drift from it. */
	FcPattern *pat = FcNameParse((const FcChar8 *)spec);
	if (!pat)
		return NULL;
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcResult    res = FcResultNoMatch;
	FcPattern  *m   = FcFontMatch(NULL, pat, &res);
	FcPatternDestroy(pat);
	if (!m || res != FcResultMatch) {
		if (m) FcPatternDestroy(m);
		return NULL;
	}

	FcChar8 *file = NULL;
	char *out = NULL;
	if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch && file) {
		int idx = 0;
		FcPatternGetInteger(m, FC_INDEX, 0, &idx);
		*index_out = idx;
		out = xstrdup((const char *)file);
	}
	FcPatternDestroy(m);
	return out;
}

/* Which installed font can draw this character?
 *
 * fontconfig answers it directly, given a charset holding the one codepoint —
 * FcFontMatch then returns the best font that COVERS it rather than the best
 * font by name. Without this the answer is always the requested family, which
 * is precisely the font already known not to have the glyph. */
static char *fc_resolve_cp(uint32_t cp)
{
	if (!FcInit())
		return NULL;

	FcCharSet *cs = FcCharSetCreate();
	if (!cs)
		return NULL;
	FcCharSetAddChar(cs, (FcChar32)cp);

	FcPattern *pat = FcPatternCreate();
	if (!pat) {
		FcCharSetDestroy(cs);
		return NULL;
	}
	FcPatternAddCharSet(pat, FC_CHARSET, cs);
	/* Still ask for a monospace one. A terminal that falls back to a
	 * proportional face draws a grid whose columns no longer line up, which
	 * looks far more broken than a missing glyph. It is a preference and not a
	 * requirement — fontconfig will substitute something that covers the
	 * character over something that is merely monospace, which is the right
	 * way round. */
	FcPatternAddInteger(pat, FC_SPACING, FC_MONO);
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcResult   res = FcResultNoMatch;
	FcPattern *m   = FcFontMatch(NULL, pat, &res);
	FcPatternDestroy(pat);
	FcCharSetDestroy(cs);
	if (!m || res != FcResultMatch) {
		if (m) FcPatternDestroy(m);
		return NULL;
	}

	FcChar8 *file = NULL;
	char *out = NULL;
	if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch && file)
		out = xstrdup((const char *)file);
	FcPatternDestroy(m);
	return out;
}
