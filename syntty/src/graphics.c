/* graphics.c — the kitty graphics protocol: images in a terminal.
 *
 * ⚠ THIS HAS NOTHING TO DO WITH THE GPU, despite the name, and the name is why
 * this comment is here. It is an ESCAPE SEQUENCE protocol for getting an image
 * onto the screen — `icat`, file-manager previews, plots from a REPL. The
 * pixels are composited on the CPU like everything else in this program.
 *
 * Table stakes, exactly as the design page files it: zero performance, cannot
 * be skipped, because a de-facto standard is what programs assume.
 *
 * ── The shape of it ────────────────────────────────────────────────────────
 *
 *   ESC _ G <key>=<value>,<key>=<value>,... ; <base64 payload> ESC \
 *
 * The keys that matter here:
 *
 *   a=  action     q query, t transmit, T transmit and display, p place,
 *                  d delete
 *   f=  format     24 = RGB, 32 = RGBA, 100 = PNG
 *   t=  medium     d = direct (the payload is the data)
 *   s=,v=          width and height in pixels
 *   i=  id         the program's handle for this image
 *   m=  more       1 = another chunk follows, 0 = that was the last
 *   c=,r=          how many CELLS to draw it across
 *
 * ── What the child is trusted with ─────────────────────────────────────────
 *
 * Nothing. Every number here arrives from a program on the other end of a pty,
 * and an image protocol is a memory-exhaustion vector wearing a friendly face:
 * `s=65535,v=65535,f=32` is a 17 GB allocation request expressed in eleven
 * bytes. So there are hard caps on the pixel count, on the number of images
 * held, and on how much of a chunked transmission will be accumulated before
 * it is abandoned. A refused image is a visible missing picture; an unbounded
 * one is a machine that stops.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdlib.h>
#include <string.h>

/* ── the caps, all of them here so they can be argued with in one place ──── */

/* 64 megapixels. A 4K screen is 8.3, so this is eight screens' worth — beyond
 * any plausible terminal image and far below anything that threatens a
 * machine. At 4 bytes a pixel it bounds one image at 256 MB, which is why the
 * byte cap below exists as well. */
#define GFX_MAX_PIXELS   (64u * 1024u * 1024u)
/* 64 MB of actual pixel data per image. */
#define GFX_MAX_BYTES    (64u * 1024u * 1024u)
/* How many images may be held at once. Each is freed when deleted or evicted;
 * this bounds a program that transmits without ever displaying. */
#define GFX_MAX_IMAGES   64
/* And how much may be accumulated across chunks before a transmission that
 * never says "m=0" is abandoned. */
#define GFX_MAX_PENDING  GFX_MAX_BYTES

typedef struct {
	uint32_t id;
	uint32_t w, h;         /* pixels */
	uint8_t *rgba;         /* w * h * 4, always RGBA once stored */
	bool     used;
} image_t;

typedef struct {
	uint32_t img_id;
	uint32_t placement_id;
	int      col, row;     /* top-left cell */
	int      cols, rows;   /* how many cells it covers */
	bool     used;
} placement_t;

struct st_gfx {
	image_t     img[GFX_MAX_IMAGES];
	placement_t place[GFX_MAX_IMAGES];

	/* A transmission in progress, assembled across `m=1` chunks. */
	uint8_t  *pending;
	size_t    pending_len, pending_cap;
	uint32_t  p_id, p_w, p_h, p_fmt;
	bool      p_display;
	int       p_cols, p_rows;

	uint64_t  refused;     /* images turned away by the caps above */
};

/* ── base64, because the payload is always base64 ───────────────────────────
 *
 * Decoded in place into a caller buffer. Invalid characters are SKIPPED rather
 * than treated as an error: the protocol allows the payload to be split across
 * chunks at any point, and some senders wrap lines. */
static const int8_t b64tab[256] = {
	['A']= 0,['B']= 1,['C']= 2,['D']= 3,['E']= 4,['F']= 5,['G']= 6,['H']= 7,
	['I']= 8,['J']= 9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
	['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
	['Y']=24,['Z']=25,
	['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
	['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
	['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,
	['y']=50,['z']=51,
	['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,
	['8']=60,['9']=61,['+']=62,['/']=63,
};

size_t st_b64_decode(const char *in, size_t len, uint8_t *out, size_t cap)
{
	uint32_t acc = 0;
	int      bits = 0;
	size_t   n = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c == '=')
			break;
		if (c != 'A' && b64tab[c] == 0 && c != '0')
			continue;              /* not a base64 character: skip it */
		acc = acc << 6 | (uint32_t)b64tab[c];
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (n < cap)
				out[n++] = (uint8_t)(acc >> bits);
		}
	}
	return n;
}

/* ── the control data ───────────────────────────────────────────────────────
 *
 * `key=value` pairs separated by commas. Parsed into a small struct rather than
 * looked up ad hoc, so an unknown key is ignored once instead of at each use. */
typedef struct {
	char     action;       /* q, t, T, p, d — 0 if absent */
	uint32_t format;       /* 24, 32, 100 */
	char     medium;       /* d, f, t, s */
	uint32_t s, v;         /* width, height */
	uint32_t id, num, placement;
	uint32_t more;
	uint32_t cols, rows;
	uint32_t quiet;
	bool     have_action;
} gfx_cmd_t;

static void parse_control(const char *p, size_t len, gfx_cmd_t *c)
{
	memset(c, 0, sizeof *c);
	c->action = 't';       /* the protocol's default */
	c->format = 32;
	c->medium = 'd';

	size_t i = 0;
	while (i < len) {
		char key = p[i];
		if (i + 1 >= len || p[i + 1] != '=')
			break;
		i += 2;

		/* The value: everything to the next comma. Numbers are read with
		 * strtoul so a value the sender got wrong yields 0 rather than
		 * whatever happened to be in the struct. */
		char valbuf[32];
		size_t vl = 0;
		while (i < len && p[i] != ',' && vl + 1 < sizeof valbuf)
			valbuf[vl++] = p[i++];
		valbuf[vl] = '\0';
		while (i < len && p[i] != ',')
			i++;
		if (i < len)
			i++;                    /* the comma */

		unsigned long n = strtoul(valbuf, NULL, 10);
		switch (key) {
		case 'a': c->action = valbuf[0]; c->have_action = true; break;
		case 'f': c->format = (uint32_t)n; break;
		case 't': c->medium = valbuf[0]; break;
		case 's': c->s = (uint32_t)n; break;
		case 'v': c->v = (uint32_t)n; break;
		case 'i': c->id = (uint32_t)n; break;
		case 'I': c->num = (uint32_t)n; break;
		case 'p': c->placement = (uint32_t)n; break;
		case 'm': c->more = (uint32_t)n; break;
		case 'c': c->cols = (uint32_t)n; break;
		case 'r': c->rows = (uint32_t)n; break;
		case 'q': c->quiet = (uint32_t)n; break;
		default: break;             /* an enhancement we do not have */
		}
	}
}

/* ── the store ──────────────────────────────────────────────────────────── */

static struct st_gfx *gfx_get(st_vt_t *vt)
{
	if (!vt->gfx)
		vt->gfx = xcalloc(1, sizeof *vt->gfx);
	return vt->gfx;
}

static image_t *image_find(struct st_gfx *g, uint32_t id)
{
	for (int i = 0; i < GFX_MAX_IMAGES; i++)
		if (g->img[i].used && g->img[i].id == id)
			return &g->img[i];
	return NULL;
}

static image_t *image_slot(struct st_gfx *g, uint32_t id)
{
	image_t *e = image_find(g, id);
	if (e) {
		free(e->rgba);
		e->rgba = NULL;
		return e;
	}
	for (int i = 0; i < GFX_MAX_IMAGES; i++)
		if (!g->img[i].used) {
			g->img[i].used = true;
			g->img[i].id = id;
			return &g->img[i];
		}
	return NULL;                    /* full — the caller refuses */
}

static void pending_reset(struct st_gfx *g)
{
	free(g->pending);
	g->pending = NULL;
	g->pending_len = g->pending_cap = 0;
}

/* Turn an accumulated payload into a stored image. */
static bool image_store(struct st_gfx *g, uint32_t id, uint32_t w, uint32_t h,
                        uint32_t fmt, const uint8_t *data, size_t len)
{
	if (w == 0 || h == 0)
		return false;
	if ((uint64_t)w * h > GFX_MAX_PIXELS)
		return false;

	size_t need = (size_t)w * h * 4;
	if (need > GFX_MAX_BYTES)
		return false;

	int bpp = fmt == 24 ? 3 : 4;
	if (len < (size_t)w * h * bpp)
		return false;               /* short payload: refuse rather than
		                             * render whatever follows it in memory */

	image_t *e = image_slot(g, id);
	if (!e)
		return false;

	/* Stored as RGBA whatever arrived, so the blit has one shape. RGB gains an
	 * opaque alpha rather than an undefined one. */
	e->w = w; e->h = h;
	e->rgba = xmalloc(need);
	for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
		e->rgba[i * 4 + 0] = data[i * bpp + 0];
		e->rgba[i * 4 + 1] = data[i * bpp + 1];
		e->rgba[i * 4 + 2] = data[i * bpp + 2];
		e->rgba[i * 4 + 3] = bpp == 4 ? data[i * bpp + 3] : 255;
	}
	return true;
}

static void place(struct st_gfx *g, const st_grid_t *grid, uint32_t img_id,
                  uint32_t pid, int cols, int rows)
{
	image_t *e = image_find(g, img_id);
	if (!e)
		return;

	/* Where the cursor is NOW is where the image goes — the protocol places
	 * relative to the cursor, which is why programs move it first. */
	for (int i = 0; i < GFX_MAX_IMAGES; i++) {
		placement_t *p = &g->place[i];
		if (p->used && !(p->img_id == img_id && p->placement_id == pid))
			continue;
		if (!p->used || (p->img_id == img_id && p->placement_id == pid)) {
			p->used = true;
			p->img_id = img_id;
			p->placement_id = pid;
			p->col = grid->cx;
			p->row = grid->cy;
			p->cols = cols > 0 ? cols : 1;
			p->rows = rows > 0 ? rows : 1;
			return;
		}
	}
}

static void delete_all(struct st_gfx *g, bool free_data)
{
	for (int i = 0; i < GFX_MAX_IMAGES; i++) {
		g->place[i].used = false;
		if (free_data && g->img[i].used) {
			free(g->img[i].rgba);
			g->img[i].rgba = NULL;
			g->img[i].used = false;
		}
	}
}

/* ── the entry point the parser calls ───────────────────────────────────── */

void st_gfx_apc(st_vt_t *vt, const char *payload, size_t len)
{
	/* Only `G` is ours. Any other APC belongs to something else and is left
	 * alone rather than guessed at. */
	if (len < 1 || payload[0] != 'G')
		return;

	const char *body = payload + 1;
	size_t blen = len - 1;

	/* Control data up to the first ';', payload after it. */
	const char *semi = memchr(body, ';', blen);
	size_t ctl_len = semi ? (size_t)(semi - body) : blen;
	const char *data = semi ? semi + 1 : NULL;
	size_t data_len = semi ? blen - ctl_len - 1 : 0;

	gfx_cmd_t c;
	parse_control(body, ctl_len, &c);

	struct st_gfx *g = gfx_get(vt);

	switch (c.action) {
	case 'q':
		/* THE SUPPORT PROBE. Programs send this and decide from the answer
		 * whether to send images at all, so answering it is most of what
		 * "supporting the protocol" means in practice. */
		vt_gfx_reply(vt, c.id, "OK");
		return;

	case 'd':
		delete_all(g, true);
		return;

	case 't':
	case 'T': {
		if (c.medium != 'd') {
			/* File and shared-memory transports are not implemented. Refused
			 * out loud: a program told "OK" for a transport we ignore draws
			 * nothing and has no idea why. */
			vt_gfx_reply(vt, c.id, "ENOTSUPP:only t=d is implemented");
			return;
		}
		if (c.format == 100) {
			vt_gfx_reply(vt, c.id, "ENOTSUPP:PNG is not implemented yet");
			return;
		}
		if (c.format != 24 && c.format != 32) {
			vt_gfx_reply(vt, c.id, "EINVAL:format must be 24 or 32");
			return;
		}

		/* Accumulate. A chunked transmission carries its geometry on the FIRST
		 * chunk only, so it is remembered here rather than re-read. */
		if (g->pending_len == 0) {
			g->p_id = c.id; g->p_w = c.s; g->p_h = c.v; g->p_fmt = c.format;
			g->p_display = (c.action == 'T');
			g->p_cols = (int)c.cols; g->p_rows = (int)c.rows;
		}

		if (data && data_len) {
			size_t want = g->pending_len + (data_len * 3) / 4 + 4;
			if (want > GFX_MAX_PENDING) {
				pending_reset(g);
				g->refused++;
				vt_gfx_reply(vt, c.id, "EINVAL:too much data");
				return;
			}
			if (want > g->pending_cap) {
				g->pending_cap = want * 2;
				g->pending = xrealloc(g->pending, g->pending_cap);
			}
			g->pending_len += st_b64_decode(data, data_len,
			                             g->pending + g->pending_len,
			                             g->pending_cap - g->pending_len);
		}

		if (c.more)
			return;                 /* more chunks to come */

		bool ok = image_store(g, g->p_id, g->p_w, g->p_h, g->p_fmt,
		                      g->pending, g->pending_len);
		bool display = g->p_display;
		int  pc = g->p_cols, pr = g->p_rows;
		uint32_t sid = g->p_id;
		pending_reset(g);

		if (!ok) {
			g->refused++;
			vt_gfx_reply(vt, sid, "EINVAL:image refused");
			return;
		}
		if (display)
			place(g, vt->g, sid, c.placement, pc, pr);

		/* q=1 asks for success to be silent, which matters: a program that
		 * transmits a hundred frames does not want a hundred replies coming
		 * back as though typed. */
		if (c.quiet == 0)
			vt_gfx_reply(vt, sid, "OK");
		return;
	}

	case 'p':
		place(g, vt->g, c.id, c.placement, (int)c.cols, (int)c.rows);
		if (c.quiet == 0)
			vt_gfx_reply(vt, c.id, "OK");
		return;

	default:
		return;
	}
}

void st_gfx_free(struct st_gfx *g)
{
	if (!g)
		return;
	delete_all(g, true);
	free(g->pending);
	free(g);
}

/* ── what the renderer needs ────────────────────────────────────────────── */

int st_gfx_placements(const struct st_gfx *g, st_gfx_place_t *out, int max)
{
	if (!g)
		return 0;
	int n = 0;
	for (int i = 0; i < GFX_MAX_IMAGES && n < max; i++) {
		const placement_t *p = &g->place[i];
		if (!p->used)
			continue;
		const image_t *e = NULL;
		for (int j = 0; j < GFX_MAX_IMAGES; j++)
			if (g->img[j].used && g->img[j].id == p->img_id) {
				e = &g->img[j];
				break;
			}
		if (!e || !e->rgba)
			continue;
		out[n].col  = p->col;
		out[n].row  = p->row;
		out[n].cols = p->cols;
		out[n].rows = p->rows;
		out[n].w    = e->w;
		out[n].h    = e->h;
		out[n].rgba = e->rgba;
		n++;
	}
	return n;
}

uint64_t st_gfx_refused(const struct st_gfx *g)
{
	return g ? g->refused : 0;
}
