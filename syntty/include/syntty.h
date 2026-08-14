/* syntty — the SynapseOS terminal.
 *
 * ── What this is, and what it deliberately is not, yet ─────────────────────
 *
 * Stage 2: a PTY, a VT parser, a cell grid, a glyph atlas and a wl_shm window.
 *
 * Stage 1 had NO WINDOW AT ALL, which was not an accident of ordering — it is
 * the only way the claims this program is built on could be checked before
 * there was anything to look at. That discipline survives the window: every
 * layer under it still answers on a machine with no seat and no display
 * (`dump`, `bench`, `font`, `render`), and the window is one more front end
 * over the same code rather than a place where logic went to hide.
 *
 * What stage 2 measured, on the machine this was written on, with hyperfine
 * inside a headless cage and cage's own 122 ms subtracted:
 *
 *              startup        fresh RSS      GL stack mapped
 *   kitty      230.3 ms       264 MB         188 MB
 *   foot        24.9 ms        21 MB         none
 *   syntty       8.6 ms       9.4 MB         none
 *
 * The baseline it exists to beat was measured before a line of this was
 * written, with hyperfine inside a headless cage, on the machine it is being
 * written on:
 *
 *              startup        fresh RSS      2.6 MB through the parser
 *   kitty      230.3 ms       264 MB         118.6 ms
 *   foot        24.9 ms        21 MB         117.9 ms
 *
 * Three things follow from that table and they shape everything here.
 *
 *   1. THROUGHPUT HAS NO CHAMPION. The two incumbents are within 0.7 ms of
 *      each other, so there is no trick to copy and no lead to erode — the
 *      parser either beats 22 MB/s on its own merits or it does not. Hence
 *      `syntty bench`, which exists from the first commit rather than being
 *      added once the number stops being flattering.
 *
 *   2. THE MEMORY IS THE GRAPHICS STACK, not the terminal. Of kitty's 264 MB,
 *      83.5 MB is libLLVM, 88 MB is the nvidia EGL driver and 15.8 MB is
 *      gallium: 188 MB that exists because a GL context was created. kitty's
 *      own working set is 32 MB and its Python interpreter is 5.4 MB — two per
 *      cent, and not the problem anybody assumes it is. So this program will
 *      not link GL, and the cell layout below matters far less than that one
 *      decision. It is still worth doing: it is simply worth 30 MB, not 190.
 *
 *   3. STARTUP IS THE WIN A PERSON FEELS, and it is spent before anything is
 *      drawn. Nothing in this file may allocate per-window state that scales
 *      with the scrollback limit, and nothing may scan a font directory. Rows
 *      arrive when they are used.
 *
 * ── The cell ───────────────────────────────────────────────────────────────
 *
 * Eight bytes, and the reason is the scrollback: at 10,000 lines of 200
 * columns, a 32-byte cell is 64 MB and an 8-byte cell is 16 MB. The attributes
 * do not live in the cell — they are interned in a table and the cell holds a
 * 16-bit index, because a screen full of text uses a handful of distinct
 * styles and stores them thousands of times.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNTTY_H
#define SYNTTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/* ── util.c ─────────────────────────────────────────────────────────────── */

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void  die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

/* Monotonic nanoseconds. The benchmark's only clock — CLOCK_MONOTONIC, never
 * wall time, because a bench that a clock adjustment can improve is a bench
 * that will eventually report a lie. */
uint64_t now_ns(void);

/* ── style.c (inside grid.c) ────────────────────────────────────────────── */

/* Attribute bits. Kept in a uint16 so a style is 12 bytes and the table stays
 * in cache — a terminal touches its style table once per cell written. */
enum {
	ST_BOLD       = 1u << 0,
	ST_DIM        = 1u << 1,
	ST_ITALIC     = 1u << 2,
	ST_UNDERLINE  = 1u << 3,
	ST_BLINK      = 1u << 4,
	ST_REVERSE    = 1u << 5,
	ST_HIDDEN     = 1u << 6,
	ST_STRIKE     = 1u << 7,
};

/* A colour is either DEFAULT, one of the 256 indexed colours, or 24-bit RGB.
 * Encoded in 32 bits with a tag in the top byte so the whole style compares
 * with one memcmp and hashes with one pass. */
#define ST_COL_DEFAULT   0x00000000u
#define ST_COL_INDEXED   0x01000000u   /* | index  */
#define ST_COL_RGB       0x02000000u   /* | 0xRRGGBB */

typedef struct {
	uint32_t fg;
	uint32_t bg;
	uint16_t attrs;
	uint16_t _pad;
} st_style_t;

/* ── the grid ───────────────────────────────────────────────────────────── */

typedef struct {
	uint32_t cp;      /* codepoint, 0 for a blank cell */
	uint16_t style;   /* index into st_grid_t.styles */
	uint8_t  width;   /* 1 normally, 2 for a wide glyph, 0 for its tail */
	uint8_t  flags;
} st_cell_t;

/* One row. `len` is how many cells are actually stored: a scrollback row that
 * ended after 12 columns keeps 12 cells, not the full width. The live screen
 * always stores its full width; only rows that have been scrolled off are
 * trimmed, because those are the ones there are ten thousand of. */
typedef struct {
	st_cell_t *cells;
	uint16_t   len;
	/* An UPPER BOUND on the columns holding anything — never lower than the
	 * truth. Trimming a row for the scrollback used to scan the whole width
	 * backwards looking for the last non-blank cell: eighty comparisons per
	 * line of output, thirty million over a build log. The scan still runs and
	 * still decides, it just starts here instead of at the end, so a watermark
	 * that is merely close costs nothing and one that is wrong high costs a
	 * few comparisons. Under-reporting would truncate real text, which is why
	 * every operation that could widen a row raises it and none lowers it
	 * except where the cells are provably blank. */
	uint16_t   hi;
	bool       wrapped;   /* the line continues on the next row */
} st_row_t;

typedef struct {
	uint16_t cols, rows;

	st_row_t *screen;        /* rows entries, each cols wide */

	/* Scrollback as a ring. Allocated lazily and never larger than `limit`;
	 * a terminal that reserves ten thousand rows at startup has spent the
	 * startup budget before it has drawn anything. */
	st_row_t *scroll;
	uint32_t  limit, count, head;

	/* Interned styles. Index 0 is always the default style, so a cleared
	 * cell is all-zero and a fresh row is one calloc. */
	st_style_t *styles;
	uint32_t    nstyles, styles_cap;
	uint16_t   *hash;        /* open addressing, indexes into styles */
	uint32_t    hash_mask;

	/* Recycled full-width row buffers. See row_alloc() in grid.c: the malloc
	 * triple this removes was 73% of the parse time on ordinary output. */
	st_cell_t **pool;
	uint32_t    npool, pool_cap;
	uint16_t    pool_cols;


	/* Cursor and the state the parser owns. */
	uint16_t cx, cy;
	uint16_t top, bot;       /* scroll region, inclusive */
	uint16_t cur_style;
	bool     wrap_next;      /* the cursor is parked past the last column */
	bool     autowrap;
	bool     origin;         /* DECOM: cursor addressing is region-relative */
} st_grid_t;

/* Columns a codepoint occupies: 0, 1 or 2. NOT wcwidth() — see grid.c. */
int st_char_width(uint32_t cp);

void st_grid_init(st_grid_t *g, uint16_t cols, uint16_t rows, uint32_t limit);
void st_grid_free(st_grid_t *g);
void st_grid_resize(st_grid_t *g, uint16_t cols, uint16_t rows);

/* Intern a style, returning its index. Identical styles always return the
 * same index, which is what makes a 16-bit field in the cell enough. */
uint16_t st_style_intern(st_grid_t *g, const st_style_t *s);
const st_style_t *st_style_get(const st_grid_t *g, uint16_t idx);

/* The operations the parser drives. They are the whole grid API: everything
 * the VT layer does to the screen goes through one of these, so the invariants
 * (cursor in range, scroll region honoured, scrollback fed) live in one file
 * rather than at forty call sites. */
void st_put(st_grid_t *g, uint32_t cp, int width);
void st_newline(st_grid_t *g);          /* LF, honouring the scroll region */
void st_carriage_return(st_grid_t *g);
void st_backspace(st_grid_t *g);
void st_tab(st_grid_t *g, int n);
void st_move_to(st_grid_t *g, int col, int row);
void st_move_by(st_grid_t *g, int dcol, int drow);
void st_scroll_up(st_grid_t *g, int n);   /* feeds the scrollback */
void st_scroll_down(st_grid_t *g, int n);
void st_erase_display(st_grid_t *g, int mode);
void st_erase_line(st_grid_t *g, int mode);
void st_insert_lines(st_grid_t *g, int n);
void st_delete_lines(st_grid_t *g, int n);
void st_insert_chars(st_grid_t *g, int n);
void st_delete_chars(st_grid_t *g, int n);
void st_erase_chars(st_grid_t *g, int n);
void st_set_region(st_grid_t *g, int top, int bot);

/* Render the grid as plain text — the golden-output format the test suite
 * compares against. Trailing blanks are trimmed so a test's expected output is
 * something a person can type. */
void st_dump_text(const st_grid_t *g, FILE *out);
/* The same, plus a style map: one line of text, one line of style indices, so
 * a test can assert that `ESC[31m` actually coloured something without
 * inventing a colour-comparison syntax. */
void st_dump_styled(const st_grid_t *g, FILE *out);
void st_dump_scrollback(const st_grid_t *g, FILE *out);

/* Bytes of grid + scrollback + styles actually held. The memory claim on the
 * design page is only a claim until something prints this. */
size_t st_grid_bytes(const st_grid_t *g);

/* ── vt.c ───────────────────────────────────────────────────────────────── */

/* The parser is a state machine over BYTES with a UTF-8 decoder in front of
 * the printable path. It holds no buffer of its own: st_vt_feed can be handed
 * any split of the stream, including one that lands mid-escape or mid-codepoint,
 * and the state carries across. A parser that assumed a read() ended on a
 * sequence boundary would work for years and then corrupt one screen in ten
 * thousand, on the reads that happen to be big. */
typedef enum {
	VT_GROUND,
	VT_ESC,
	VT_CSI_ENTRY,
	VT_CSI_PARAM,
	VT_CSI_INTERMEDIATE,
	VT_CSI_IGNORE,
	VT_OSC,
	VT_OSC_ESC,
	VT_DCS_IGNORE,
	VT_DCS_ESC,
} vt_state_t;

#define VT_MAX_PARAMS 16
#define VT_OSC_MAX    2048

typedef struct {
	st_grid_t *g;

	vt_state_t state;

	int      params[VT_MAX_PARAMS];
	int      nparams;
	bool     param_seen;      /* tells `ESC[m` from `ESC[0m` — they agree, but
	                           * `ESC[;5m` needs the empty first one kept */
	char     intermediate;
	char     priv;            /* '?' and friends */

	char     osc[VT_OSC_MAX];
	int      osc_len;

	/* UTF-8 decode state, carried across feeds. */
	uint32_t utf_cp;
	int      utf_need;
	int      utf_seen;

	/* The current SGR state, kept unpacked and interned on change rather
	 * than interned per cell. */
	st_style_t style;

	/* DECSC/DECRC and CSI s/u. The saved cursor carries the STYLE too —
	 * restoring the position and not the colour is how a full-screen program
	 * leaves the rest of the session painted in its last attribute. */
	uint16_t   saved_cx, saved_cy;
	st_style_t saved_style;

	/* What the program asked for that this stage does not draw. Counted, not
	 * ignored silently — "it rendered nothing" and "it was never sent" are
	 * different bugs and a stage with no window cannot tell them apart later. */
	uint64_t unhandled_csi;
	uint64_t unhandled_esc;
	uint64_t osc_seen;

	char title[512];
} st_vt_t;

void st_vt_init(st_vt_t *vt, st_grid_t *g);
void st_vt_feed(st_vt_t *vt, const uint8_t *buf, size_t len);

/* ── font.c ─────────────────────────────────────────────────────────────── */

/* A rasterised glyph: 8-bit COVERAGE, not colour.
 *
 * Coverage is what makes the atlas worth having. The same 'e' is drawn in
 * white, in red, on a selected background and dimmed, and if the atlas stored
 * pixels it would need one entry per combination. Storing how much of each
 * pixel the glyph covers means render.c blends it against whatever fg and bg
 * the cell carries, and one rasterisation serves them all.
 *
 * `w` is cell_w for a normal glyph and twice that for a wide one, so the blit
 * loop reads the extent from the glyph rather than re-deriving it. */
typedef struct {
	uint8_t *bits;    /* w * h coverage, row-major, owned by the font */
	uint16_t w, h;
	uint8_t  cols;    /* 1, or 2 for a double-width glyph */
} st_glyph_t;

typedef struct st_font st_font_t;

/* Open a font by fontconfig family name ("monospace", "JetBrains Mono", …).
 * Returns NULL and sets *err to a malloc'd sentence on failure. */
st_font_t *st_font_open(const char *family, double size_px, char **err);
void       st_font_close(st_font_t *f);

/* The cell box every row and column is laid out on. Taken from the regular
 * face and shared by all four, because the grid is one box. */
int st_font_cell_w(const st_font_t *f);
int st_font_cell_h(const st_font_t *f);
int st_font_baseline(const st_font_t *f);

/* Rasterise-or-recall. Never returns NULL: anything with no glyph comes back
 * as the shared blank box, so the blit loop holds no NULL check. `attrs` is
 * the ST_* bits — only ST_BOLD and ST_ITALIC pick a face. */
const st_glyph_t *st_font_glyph(st_font_t *f, uint32_t cp, uint16_t attrs);

/* What the font layer had to do to answer, for `syntty font`. The startup
 * claim is only a claim until something prints where the milliseconds went. */
typedef struct {
	bool     used_fontconfig;   /* false means the cache answered */
	double   open_ms;           /* everything: lookup, face, ASCII atlas */
	double   lookup_ms;         /* the path question alone */
	uint32_t ascii_glyphs;
	size_t   atlas_bytes;
	uint32_t fallbacks;         /* fonts opened to cover missing glyphs */
	char     path[512];         /* the file actually opened */
} st_font_stats_t;

const st_font_stats_t *st_font_get_stats(const st_font_t *f);

/* ── render.c ───────────────────────────────────────────────────────────── */

/* Cells to pixels, on the CPU. The output word is 0x00RRGGBB — wl_shm's
 * XRGB8888 — so a painted buffer goes to the compositor with no conversion.
 *
 * THE BUFFER IS ALWAYS THE CALLER'S. Nothing here allocates one, which is what
 * lets the same code paint a Wayland buffer, a PPM for the test suite, and a
 * throwaway for a benchmark. */
typedef struct st_render st_render_t;

st_render_t *st_render_new(st_font_t *f);
void         st_render_free(st_render_t *r);

/* Default foreground and background, as 0xRRGGBB. Cells that name a colour
 * still win; these are what ST_COL_DEFAULT resolves to. */
void st_render_colors(st_render_t *r, uint32_t fg, uint32_t bg);
void st_render_cursor(st_render_t *r, bool on);

/* The pixel size a grid wants. The window may be any size; anything the cells
 * do not cover is painted with the default background. */
int  st_render_width (const st_render_t *r, const st_grid_t *g);
int  st_render_height(const st_render_t *r, const st_grid_t *g);

/* Paint. `stride_px` is in PIXELS, not bytes — every caller so far has a
 * pixel-aligned stride, and a byte stride invites one caller to forget the
 * divide. Returns the number of cells drawn. */
size_t st_render_grid(st_render_t *r, const st_grid_t *g,
                      uint32_t *px, int stride_px, int w, int h);

/* The renderer's golden output. Stage 1 could assert on text because its
 * output was text; a renderer that is only checked for "it returned" passes on
 * an all-black screen. */
void st_render_write_ppm(const uint32_t *px, int stride_px, int w, int h,
                         FILE *out);

/* ── pty.c ──────────────────────────────────────────────────────────────── */

typedef struct {
	int   fd;
	pid_t pid;
} st_pty_t;

/* forkpty, argv exec'd with no shell in the middle. Returns false and leaves
 * errno set on failure. */
bool st_pty_spawn(st_pty_t *p, char *const argv[], uint16_t cols, uint16_t rows);
/* Read until the child exits and the pty drains, feeding everything through
 * the parser. Returns the child's exit status. */
int  st_pty_pump(st_pty_t *p, st_vt_t *vt);
void st_pty_resize(st_pty_t *p, uint16_t cols, uint16_t rows);
/* ⚠ Explicit, because the two readers of a pty want opposite things — see the
 * comment on the definition. st_pty_pump must block; the window's loop must
 * never. */
void st_pty_set_nonblocking(st_pty_t *p);
/* Hang the child up (closing the master sends SIGHUP) and return its exit
 * status. The window's status IS the child's — see the definition. */
int  st_pty_reap(st_pty_t *p);

/* ── win.c ──────────────────────────────────────────────────────────────── */

/* What the window did, for the claim that it starts fast.
 *
 * `first_frame_ms` is measured from entering st_win_run to the first buffer
 * committed — it is the number that answers kitty's 230 ms, and it excludes
 * only the argument parsing above it. `skipped` counts frames dropped because
 * the compositor still held both buffers, which under a flood is the throttle
 * working rather than a fault. */
typedef struct {
	uint64_t frames;
	uint64_t skipped;
	double   first_frame_ms;
} st_win_stats_t;

/* Open a window and run until the child exits or it is closed. Everything it
 * needs is built by the caller, so this function owns no terminal state and
 * the headless paths above share every line of it. */
int st_win_run(st_grid_t *g, st_vt_t *vt, st_pty_t *pty, st_font_t *font,
               st_render_t *ren, const char *title, st_win_stats_t *stats);

#endif /* SYNTTY_H */
