/* syntty — the SynapseOS terminal.
 *
 * ── What this is, and what it deliberately is not, yet ─────────────────────
 *
 * Stage 1: a PTY, a VT parser and a cell grid, with NO WINDOW AT ALL. That is
 * not an accident of ordering — it is the only way the claims this program is
 * being built on can be checked. A terminal that draws is a terminal whose
 * numbers need a compositor, a seat and a human; a terminal that only parses
 * can be measured in CI, on a headless machine, in one command.
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

#endif /* SYNTTY_H */
