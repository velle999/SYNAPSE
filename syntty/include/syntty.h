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

	/* ── damage ─────────────────────────────────────────────────────────────
	 *
	 * Set by anything that changes what this row LOOKS like; cleared only by
	 * the renderer, once it has drawn it. It costs nothing — the struct was
	 * already padded to sixteen bytes and this fits inside that padding.
	 *
	 * ⚠ IT IS DELIBERATELY GENEROUS. A row marked dirty that did not change
	 * costs one repaint of one row. A row that changed and was NOT marked
	 * leaves stale pixels on the screen, which looks like memory corruption
	 * and is invisible to every test that only checks the final screen. When
	 * in doubt, mark it — and the damage-check test exists precisely because
	 * "when in doubt" is not a guarantee. */
	bool       dirty;

	/* ── semantic marks (OSC 133) ───────────────────────────────────────────
	 *
	 * Where a prompt began, and where a command's output began. Set by the
	 * shell telling us rather than by us guessing, and carried on the ROW so
	 * it travels into the scrollback with the text it describes — which is
	 * the only place jump-to-prompt can look. */
	uint8_t    mark;
} st_row_t;

enum { ST_MARK_NONE = 0, ST_MARK_PROMPT = 1, ST_MARK_OUTPUT = 2 };

/* ── what is highlighted ────────────────────────────────────────────────────
 *
 * Two ends, and both are ABSOLUTE LINE NUMBERS rather than screen rows — see
 * `scrolled` below. A selection kept in screen coordinates slides off the text
 * it was made on the moment anything prints, and the person watches their
 * highlight crawl up the window over words they never chose.
 *
 * `mode` is what a click COUNT meant, and the expansion is derived rather than
 * stored: a double-click that becomes a drag keeps snapping to whole words,
 * which is what it does everywhere else and is impossible if the word
 * boundaries were resolved once at the click. */
enum { ST_SEL_CHAR = 0, ST_SEL_WORD = 1, ST_SEL_LINE = 2 };

typedef struct {
	bool     active;
	int64_t  a_line, b_line;   /* anchor, and the end being dragged */
	int      a_col,  b_col;
	uint8_t  mode;
} st_sel_t;

typedef struct {
	uint16_t cols, rows;

	st_row_t *screen;        /* rows entries, each cols wide */

	/* Scrollback as a ring. Allocated lazily and never larger than `limit`;
	 * a terminal that reserves ten thousand rows at startup has spent the
	 * startup budget before it has drawn anything. */
	st_row_t *scroll;
	uint32_t  limit, count, head;

	/* ── the one number that does not wrap ──────────────────────────────────
	 *
	 * How many rows have EVER scrolled off, which `count` cannot say once the
	 * ring is full and it stops growing. It is what gives every line an
	 * address that stays the same as output arrives, and the selection is
	 * anchored to it: a person who highlights a filename in the middle of a
	 * running build must keep that filename highlighted while the build pushes
	 * it up the screen, not keep the same two screen rows lit up over whatever
	 * text has since moved into them. */
	uint64_t  scrolled;

	/* Interned styles. Index 0 is always the default style, so a cleared
	 * cell is all-zero and a fresh row is one calloc. */
	st_style_t *styles;
	uint32_t    nstyles, styles_cap;
	uint16_t   *hash;        /* open addressing, indexes into styles */
	uint32_t    hash_mask;

	/* ── the alternate screen ───────────────────────────────────────────────
	 *
	 * Every full-screen program uses it — vim, less, htop, top, man. It is a
	 * second screen the program draws on, so that when it exits the shell
	 * session is exactly as it left it.
	 *
	 * ⚠ NOTHING SCROLLED OFF THE ALTERNATE SCREEN GOES TO THE SCROLLBACK.
	 * That is the entire point: without it, ten minutes in vim fills the
	 * history with fragments of the editor's redraws and the commands the
	 * person actually ran are gone. A terminal missing this destroys the
	 * scrollback every time an editor opens, and it looks like the scrollback
	 * is broken rather than like the alternate screen is missing.
	 *
	 * `alt` holds whichever screen is NOT currently shown, so switching is a
	 * pointer swap rather than a copy. */
	st_row_t *alt;
	bool      on_alt;
	uint16_t  alt_cx, alt_cy, alt_style;   /* the cursor saved across 1049 */

	/* Modes a program turns on that only the renderer and the input layer
	 * care about. Kept here because the parser is what is told about them. */
	bool     cursor_visible;   /* DECTCEM, ?25 */
	bool     bracketed_paste;  /* ?2004 */
	/* ⚠ uint16_t, AND THIS WAS A uint8_t FOR A WHOLE RELEASE. The values
	 * stored here are 1000, 1002 and 1003 — none of which fit in a byte. They
	 * came back as 232, 234 and 235, still distinct from each other and still
	 * non-zero, so the test that asserted the modes were "understood rather
	 * than counted as unknown" passed on every one of them. Nothing compared
	 * the field against 1000 until the input layer existed, which is exactly
	 * when it would have started sending the wrong events. */
	uint16_t mouse_mode;       /* 0 off, or 1000 / 1002 / 1003 */
	bool     mouse_sgr;        /* ?1006 — extended coordinates */

	/* Recycled full-width row buffers. See row_alloc() in grid.c: the malloc
	 * triple this removes was 73% of the parse time on ordinary output. */
	st_cell_t **pool;
	uint32_t    npool, pool_cap;
	uint16_t    pool_cols;


	/* Cursor and the state the parser owns. */
	uint16_t cx, cy;
	uint16_t top, bot;       /* scroll region, inclusive */
	uint16_t cur_style;
	/* ── the scrollback viewport ────────────────────────────────────────────
	 *
	 * How many rows back from the live screen the window is looking. 0 is
	 * live and is the only state in which the cursor is drawn — a cursor
	 * painted over history is a cursor in a place it cannot be.
	 *
	 * The parser does not care about this: output still lands on the live
	 * screen while somebody is reading history, exactly as it does in every
	 * other terminal, and the view snaps back when they type. */
	uint32_t view;

	bool     wrap_next;      /* the cursor is parked past the last column */
	bool     autowrap;
	bool     origin;         /* DECOM: cursor addressing is region-relative */

	/* What the pointer has highlighted. On the grid rather than in the window
	 * because the renderer has to draw it and the copy has to read it, and
	 * both already hold this. */
	st_sel_t sel;
} st_grid_t;

/* Columns a codepoint occupies: 0, 1 or 2. NOT wcwidth() — see grid.c. */
int st_char_width(uint32_t cp);

void st_grid_init(st_grid_t *g, uint16_t cols, uint16_t rows, uint32_t limit);
void st_grid_free(st_grid_t *g);
void st_grid_resize(st_grid_t *g, uint16_t cols, uint16_t rows);

/* Switch to or from the alternate screen (DEC modes 1049, 1047, 47).
 * `save_cursor` is what tells 1049 apart from the older two.
 *
 * ⚠ Nothing scrolled off the alternate screen reaches the scrollback — see the
 * definition. That is what the feature is FOR. */
void st_grid_alt_screen(st_grid_t *g, bool enable, bool save_cursor);

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

/* ── damage ─────────────────────────────────────────────────────────────────
 *
 * Which rows have changed since the renderer last cleared them. The whole
 * point is that typing one character repaints one row rather than the screen:
 * at 4K a full repaint is 9.4 ms, which is 56% of a frame, and almost none of
 * it is work anybody asked for.
 *
 * ⚠ THE CALLER CLEARS, NOT THE RENDERER — because with double buffering there
 * are two buffers of differing age, and a row drawn into one is still stale in
 * the other. Whoever owns the buffers owns the bookkeeping; the grid only ever
 * reports what changed since it was last asked. */
/* ── the scrollback viewport ────────────────────────────────────────────────
 *
 * `st_grid_view_row` is what the renderer draws: screen line `y` resolved
 * through the current scroll offset into either the live screen or the
 * scrollback. NULL means "above the oldest line we kept", which draws blank.
 *
 * Everything else in this program addresses rows by their position on the LIVE
 * screen. This is the one place that translates, deliberately — a viewport that
 * leaks into the parser would mean output landing where the reader happens to
 * be looking. */
const st_row_t *st_grid_view_row(const st_grid_t *g, int y);
/* Move the view. Positive is back into history. Returns true if it moved,
 * which is what tells the caller whether anything needs repainting. */
bool st_grid_view_scroll(st_grid_t *g, int delta);
bool st_grid_view_reset(st_grid_t *g);
/* The row of the nearest prompt mark in `dir` (-1 back, +1 forward), as a view
 * offset, or -1 if there is none. */
long st_grid_find_prompt(const st_grid_t *g, int dir);

/* The text between two cells, in reading order, as a malloc'd UTF-8 string.
 * Coordinates are VIEW coordinates — what the person can see.
 *
 * ⚠ A soft wrap does NOT become a newline: the terminal invented that break,
 * the line is one line, and pasting it back with a newline in the middle runs
 * half a command. See the definition. */
char *st_grid_selection_text(const st_grid_t *g, int c0, int r0, int c1, int r1);

/* ── selecting with the pointer ─────────────────────────────────────────────
 *
 * The window drives these with VIEW coordinates — the cell under the pointer —
 * and they store absolute lines, so the highlight stays on its text while the
 * screen scrolls under it.
 *
 * Each of them marks what it changed dirty, including what it stopped
 * highlighting: a selection that is cleared but not repainted leaves the old
 * highlight on screen, which is indistinguishable from a selection that is
 * still there and cannot be copied. */
void  st_sel_start(st_grid_t *g, int col, int row, int mode);
void  st_sel_extend(st_grid_t *g, int col, int row);
void  st_sel_clear(st_grid_t *g);
bool  st_sel_active(const st_grid_t *g);
/* The selected span on one visible row, in columns, inclusive. False when this
 * row has nothing selected — which is most rows, most of the time. */
bool  st_sel_row_span(const st_grid_t *g, int row, int *from, int *to);
/* What the selection would copy. NULL when there is none — distinct from an
 * empty string, which is what selecting a blank line gives. */
char *st_sel_text(const st_grid_t *g);

/* The absolute line under a visible row, and the visible row of an absolute
 * line (which may be off-screen in either direction). */
int64_t st_grid_abs_line(const st_grid_t *g, int row);
int     st_grid_row_of(const st_grid_t *g, int64_t line);

bool st_grid_row_dirty(const st_grid_t *g, int row);
void st_grid_clear_dirty(st_grid_t *g);
/* Mark everything — after a resize, or anything else that invalidates the
 * whole surface. */
void st_grid_dirty_all(st_grid_t *g);

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
	VT_APC,          /* ESC _ ... ESC \  — the graphics protocol lives here */
	VT_APC_ESC,
} vt_state_t;

#define VT_MAX_PARAMS 16
#define VT_OSC_MAX    2048

/* One APC payload. The graphics protocol chunks its transmissions at 4096
 * bytes of base64, so this holds a whole chunk with the control data in front
 * of it and room to spare.
 *
 * ⚠ A FIXED BUFFER IS THE POINT. The bytes arriving here come from the CHILD,
 * which is not trusted: a program that sends an unterminated APC sequence
 * forever would grow an unbounded buffer, and "the terminal ate all the
 * memory" is a denial of service, not a parsing bug. Overlong sequences are
 * dropped and counted. */
#define VT_APC_MAX    8192

/* How many finished commands are remembered. The questions this answers —
 * "how long did that take", "did it work" — are about the recent past. */
#define ST_MAX_CMDS   64

/* One finished command, as the shell reported it.
 *
 * ⚠ `status` is -1 for UNKNOWN, which is not the same as 0. A shell that emits
 * a bare `D` with no status has told us it finished and nothing else, and
 * reporting that as success is how a failing command comes back green. */
typedef struct {
	uint64_t duration_ns;
	int      status;
	uint16_t row;
} st_cmd_t;

/* ── the kitty keyboard protocol ────────────────────────────────────────────
 *
 * Not a performance feature and not ours: it is a de-facto standard that
 * programs now assume, and the design page files it under table stakes — work
 * that buys zero speed and cannot be skipped, because things break without it.
 *
 * What it fixes is genuinely broken in the legacy encoding. `Ctrl+I` and `Tab`
 * are the same byte. So are `Ctrl+M` and `Enter`, and `Ctrl+[` and `Escape`.
 * There is no way at all to report a key RELEASE, or `Ctrl+Shift+1`, or which
 * of two physical keys produced a character. Editors have worked around this
 * for forty years by guessing from timing.
 *
 * It is PROGRESSIVE: a program asks for the enhancements it wants and the
 * terminal replies with the ones it actually has. A terminal that echoes the
 * request back — claiming everything — makes programs send encodings it cannot
 * read, which is worse than admitting to none. */
enum {
	KKP_DISAMBIGUATE   = 1u << 0,  /* Ctrl+I is not Tab any more */
	KKP_REPORT_EVENTS  = 1u << 1,  /* press, repeat and RELEASE */
	KKP_ALTERNATE_KEYS = 1u << 2,  /* the shifted and base key as well */
	KKP_ALL_AS_ESCAPES = 1u << 3,  /* every key as a sequence, text included */
	KKP_ASSOCIATED_TEXT = 1u << 4, /* the text the key produced, alongside it */
};

/* What THIS terminal implements. The query answers with this mask and never
 * with what was asked for. */
#define KKP_SUPPORTED  (KKP_DISAMBIGUATE | KKP_REPORT_EVENTS | \
                        KKP_ALTERNATE_KEYS | KKP_ALL_AS_ESCAPES | \
                        KKP_ASSOCIATED_TEXT)

/* Programs PUSH their flags on entry and POP on exit, so one that dies does not
 * strand the shell in a mode it cannot use. Eight is deeper than any real
 * nesting (an editor inside a multiplexer inside a shell is three). */
#define VT_KKP_DEPTH  8

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

	/* The APC payload being collected — the graphics protocol's transport. */
	char     apc[VT_APC_MAX + 1];
	int      apc_len;
	bool     apc_over;        /* this one ran past the buffer */
	uint64_t apc_dropped;

	/* Images and where they are placed. Opaque here: nothing outside
	 * graphics.c needs to know what an image is, and the parser only needs to
	 * hand it bytes. NULL until the first graphics sequence arrives, so a
	 * session that never shows an image pays nothing for the feature. */
	struct st_gfx *gfx;

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

	/* The keyboard-protocol stack. Entry 0 is the base state and is always
	 * zero — legacy encodings — so popping past the bottom lands somewhere
	 * usable rather than somewhere undefined. */
	uint8_t  kkp_stack[VT_KKP_DEPTH];
	int      kkp_depth;
	uint64_t kkp_overflow;    /* pushes past the stack's depth */

	/* Bytes owed to the child: answers to its questions, written back as
	 * though typed. The parser holds no file descriptor — see vt_reply — so
	 * the caller drains this after each feed. */
	char     reply[128];
	int      reply_len;
	uint64_t reply_dropped;

	char title[512];

	/* ── what the shell told us about its commands ──────────────────────────
	 *
	 * Timing and exit status per command, from the OSC 133 C and D marks. A
	 * small ring: what this is for is "how long did that take" and "did it
	 * work", both of which are questions about the recent past. */
	st_cmd_t cmds[ST_MAX_CMDS];
	uint32_t ncmds;              /* total ever recorded, not the ring's size */
	uint64_t cmd_start_ns;
	uint16_t cmd_col;            /* where the typed command begins on its row */
	bool     cmd_running;
} st_vt_t;

void st_vt_init(st_vt_t *vt, st_grid_t *g);
void st_vt_feed(st_vt_t *vt, const uint8_t *buf, size_t len);
/* Release what the parser acquired. Only the image store, and only if a
 * graphics sequence ever arrived — but a terminal that shows one picture and
 * exits must not leak it. */
void st_vt_free(st_vt_t *vt);

/* The keyboard enhancements in force right now — what the key encoder must
 * obey. Zero means the legacy encodings, which is the base state and what every
 * program gets until it asks for more. */
unsigned st_vt_kbd_flags(const st_vt_t *vt);

/* ── graphics.c: the kitty graphics protocol ────────────────────────────────
 *
 * ⚠ NOTHING TO DO WITH THE GPU, despite the name. It is an escape-sequence
 * protocol for putting an IMAGE on the screen — icat, file previews, plots —
 * and the pixels are composited on the CPU like everything else here. */
struct st_gfx;

/* Fed one complete APC payload by the parser. Only sequences beginning `G`
 * are ours; anything else is left alone. */
void st_gfx_apc(st_vt_t *vt, const char *payload, size_t len);
/* Queue a graphics-protocol answer. In APC, not CSI: that is the transport the
 * program is listening on. */
void vt_gfx_reply(st_vt_t *vt, uint32_t id, const char *msg);
void st_gfx_free(struct st_gfx *g);

/* One image, and where on the grid it goes. Borrowed — `rgba` belongs to the
 * store and is valid until the next graphics sequence. */
typedef struct {
	int      col, row;     /* top-left cell */
	int      cols, rows;   /* cells covered */
	uint32_t w, h;         /* the image's own pixels */
	const uint8_t *rgba;
} st_gfx_place_t;

int      st_gfx_placements(const struct st_gfx *g, st_gfx_place_t *out, int max);
uint64_t st_gfx_refused(const struct st_gfx *g);

/* Take whatever the parser owes the child, and empty the queue. Returns the
 * number of bytes copied. A caller with no way to write back simply never calls
 * this, which is correct for `dump`: a file cannot be answered. */
size_t st_vt_take_reply(st_vt_t *vt, char *out, size_t cap);

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
/* Images to draw over the cells — the graphics protocol's store, which lives on
 * the parser. NULL for none, which is what a session that never shows an image
 * keeps forever. */
void st_render_set_gfx(st_render_t *r, const struct st_gfx *g);

/* The pixel size a grid wants. The window may be any size; anything the cells
 * do not cover is painted with the default background. */
int  st_render_width (const st_render_t *r, const st_grid_t *g);
int  st_render_height(const st_render_t *r, const st_grid_t *g);

/* Paint. `stride_px` is in PIXELS, not bytes — every caller so far has a
 * pixel-aligned stride, and a byte stride invites one caller to forget the
 * divide. Returns the number of cells drawn. */
size_t st_render_grid(st_render_t *r, const st_grid_t *g,
                      uint32_t *px, int stride_px, int w, int h);

/* Paint ONLY the rows named in `rows` — one byte per grid row, non-zero meaning
 * repaint. Everything else in the buffer is left untouched, which is both the
 * saving and the risk: a row that changed and was not named keeps its old
 * pixels. Margins are not repainted (they change only on a resize, which marks
 * every row).
 *
 * ⚠ It must produce a buffer byte-identical to st_render_grid's for the same
 * grid. Both go through the same draw_row for exactly that reason, and
 * `render --damage-check` proves it on a real stream. */
size_t st_render_rows(st_render_t *r, const st_grid_t *g, const uint8_t *rows,
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

/* ── mouse.c: telling the child where the pointer is ────────────────────────
 *
 * A program that turns on mouse reporting (?1000, ?1002, ?1003) is asking to
 * be told about the pointer in escape sequences on its own input, and this is
 * the encoder for them. It is a PURE FUNCTION, deliberately: the seat, the
 * compositor and the window are all above it, so every rule below is asserted
 * by `syntty mouse` on a machine with no display and no person — which is the
 * only way any of this gets tested, because input is never synthesised on a
 * live session.
 *
 * ⚠ THE THREE MODES ARE NOT INTERCHANGEABLE and the difference is what gets
 * sent when nothing is pressed. 1000 is buttons only. 1002 adds motion WHILE A
 * BUTTON IS HELD, which is what a program needs to implement dragging. 1003 is
 * every motion, which is a packet per pointer step — a program that asked for
 * 1000 and is handed 1003's traffic gets a flood it never asked for and cannot
 * turn off. */
enum { ST_MOUSE_PRESS = 0, ST_MOUSE_RELEASE = 1, ST_MOUSE_MOTION = 2 };

/* The protocol's modifier bits — these ARE the wire values, not our own. */
enum { ST_MOUSE_SHIFT = 4, ST_MOUSE_ALT = 8, ST_MOUSE_CTRL = 16 };

/* The button numbers as they go on the wire. ⚠ NOT the evdev order: Wayland's
 * BTN_RIGHT is 0x111 and BTN_MIDDLE is 0x112, so `code - BTN_LEFT` swaps the
 * middle and right buttons in every event a program receives — a paste landing
 * on right-click and a context menu on middle-click, from one subtraction. */
enum {
	ST_BTN_LEFT = 0, ST_BTN_MIDDLE = 1, ST_BTN_RIGHT = 2,
	ST_BTN_NONE = 3,           /* motion with nothing held; also legacy release */
	ST_BTN_WHEEL_UP = 64, ST_BTN_WHEEL_DOWN = 65,
	ST_BTN_WHEEL_LEFT = 66, ST_BTN_WHEEL_RIGHT = 67,
};

/* Encode one pointer event, or produce nothing. `col` and `row` are ZERO-BASED
 * cells; the +1 the wire wants is applied here so no caller has to remember it.
 *
 * Returns the byte count, and 0 for "this mode does not report that" — which is
 * a normal answer, not a failure. When it is 0 and `why` is non-NULL it is set
 * to a sentence saying which rule declined, because the two reasons a program
 * sees nothing (the mode does not report it, or the terminal cannot say it) are
 * different bugs. */
size_t st_mouse_encode(uint16_t mode, bool sgr, int event, int button,
                       unsigned mods, int col, int row,
                       char *out, size_t cap, const char **why);

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

	/* ── latency, from wp_presentation ──────────────────────────────────────
	 *
	 * `have_presentation` is false where the compositor does not offer the
	 * protocol, or offers it on a clock that is not ours. Everything below is
	 * then zero, and a caller must print nothing rather than print zeros —
	 * "0.00 ms" and "not measured" are very different claims.
	 *
	 * COMMIT->PHOTON needs no input, so the suite measures it every run.
	 * INPUT->PHOTON needs somebody to type and INCLUDES THE CHILD's round trip
	 * (a keystroke reaches the shell, which decides what to echo, before there
	 * is anything to draw) — which is what a person waits for, but is not
	 * purely the terminal's, so it is labelled wherever it is printed. */
	bool     have_presentation;
	uint64_t discarded;        /* frames superseded before ever being shown */

	uint64_t commit_n;
	double   commit_min, commit_max, commit_avg;
	uint64_t input_n;
	double   input_min, input_max, input_avg;

	/* ── deadline rendering ─────────────────────────────────────────────────
	 *
	 * `deadline_used` counts paints that waited for the deadline and made it;
	 * `deadline_late` counts the ones already past it when they were scheduled,
	 * which fell back to painting immediately. A run that is mostly `late` is
	 * one where the margin is too small for the machine, and it is worth
	 * knowing rather than silently absorbing. */
	bool     deadline_on;
	uint64_t deadline_used, deadline_late;
	double   refresh_ms;      /* the cadence the compositor reported, 0 if none */
	double   margin_ms;       /* how early of the vblank a paint is aimed */

	/* Damage tracking's whole claim, as a ratio. `rows_possible` is what a
	 * full repaint every frame would have cost. */
	uint64_t rows_painted, rows_possible;

	/* Pointer events handed to the child, and the ones the 1984 encoding
	 * could not carry (past column 223, from a program that never asked for
	 * ?1006). The second number is printed whenever it is not zero: a mouse
	 * that works on the left of the window and stops on the right is a
	 * memorable bug to be told about rather than to discover. */
	uint64_t mouse_sent, mouse_dropped;
} st_win_stats_t;

/* Open a window and run until the child exits or it is closed. Everything it
 * needs is built by the caller, so this function owns no terminal state and
 * the headless paths above share every line of it. */
int st_win_run(st_grid_t *g, st_vt_t *vt, st_pty_t *pty, st_font_t *font,
               st_render_t *ren, const char *title, bool deadline,
               st_win_stats_t *stats);

#endif /* SYNTTY_H */
