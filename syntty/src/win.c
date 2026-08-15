/* win.c — the window, and the loop that keeps it fed.
 *
 * Stage 2's last piece, and the one that turns every claim in this project
 * from an argument into a number. Startup and memory cannot be measured
 * without a window, because what kitty spends 230 ms and 264 MB on is
 * precisely the work of having one.
 *
 * ── What is linked, and what is not ────────────────────────────────────────
 *
 * wayland-client and xkbcommon. No EGL, no GL, no toolkit, no cairo, no
 * pango. The pixels come from render.c in the exact format wl_shm wants and go
 * to the compositor with nothing in between.
 *
 * ── The loop ───────────────────────────────────────────────────────────────
 *
 * One poll() over two file descriptors: the Wayland connection and the pty.
 * That is the whole design, and the shape of it is what makes a flood cheap.
 *
 *   pty readable   -> read as much as is there, feed the parser, mark dirty
 *   wayland ready  -> dispatch (input, configure, buffer release)
 *   frame callback -> if dirty and a buffer is free, paint and commit
 *
 * ⚠ PARSING AND PAINTING ARE NOT THE SAME RATE, and that is deliberate. A
 * program printing a megabyte produces thousands of screens and a display can
 * show sixty a second; a terminal that paints every intermediate state spends
 * all its time drawing frames nobody will ever see, which is why `cat` on a big
 * file used to take longer than the read. Everything is parsed — the grid must
 * be correct — and only the LAST state before each frame is drawn. Both
 * incumbents already do this; it is the structural win, and it is table stakes
 * rather than an advantage.
 *
 * ── Two buffers ────────────────────────────────────────────────────────────
 *
 * The compositor may still be reading the buffer that was committed, so
 * painting into it would tear. Two are allocated from one pool and the client
 * uses whichever the compositor has released. With none free the frame is
 * skipped rather than waited for, because the next one is 16 ms away and the
 * content will only be fresher.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#include "primary-selection-unstable-v1-client-protocol.h"

#define NBUFFERS 2

/* The per-frame damage set lives on the stack, so it needs a bound. A terminal
 * taller than this on a real display would need a font under two pixels high;
 * beyond it the paint simply covers the rows it can, rather than overflowing. */
#define SYN_MAX_ROWS 1024

/* ── measuring latency, which nobody publishes ──────────────────────────────
 *
 * The design page lists input latency as the third win and marks it "not
 * measured" for both incumbents, which is the honest state of the art: almost
 * nobody measures input-to-photon properly, because it needs a timestamp at
 * each end and almost nobody owns both.
 *
 * wp_presentation supplies the far end. It reports, per committed frame, the
 * time that frame was actually SCANNED OUT — not queued, not composited,
 * scanned out. Two different numbers come out of it and they answer different
 * questions:
 *
 *   COMMIT -> PHOTON   how long our pixels waited on the compositor and the
 *                      display. Needs no input, so it can be measured in the
 *                      test suite on every run.
 *
 *   INPUT -> PHOTON    what a person actually feels: from the keystroke to the
 *                      frame that shows its consequence. Needs somebody to
 *                      type, so it is recorded when they do and never
 *                      synthesised.
 *
 * ⚠ INPUT -> PHOTON HONESTLY INCLUDES THE CHILD. A keystroke goes to the pty,
 * the shell decides what to echo, and only then is there anything to draw. That
 * round trip is part of what the person waits for, so it belongs in the number
 * — but it means this figure is not purely the terminal's, and comparing it
 * against a terminal measured without the child would be comparing two
 * different things. It is labelled accordingly wherever it is printed.
 *
 * The clock has to be checked, not assumed: the compositor announces which one
 * its timestamps are on, and a client that assumes CLOCK_MONOTONIC and gets
 * something else computes a difference between two unrelated clocks and prints
 * it with a straight face. */
typedef struct win win_t;

/* ⚠ A CAP, AND A DELIBERATE ONE. Tabs are navigated by a person: past a couple
 * of dozen the bar is unreadable and nobody is finding anything in it anyway.
 * A bound also means the poll set, the bar layout and the tab array are all
 * fixed-size, and a window cannot be made to allocate without limit by a key
 * held down. Reaching it is reported rather than silently ignored. */
#define SYN_MAX_TABS 32

/* One tab: a whole terminal. Everything a session needs except the window, the
 * font and the renderer, which are the only things tabs share. */
typedef struct {
	st_grid_t g;
	st_vt_t   vt;
	st_pty_t  pty;
	bool      first;         /* the one named on the command line */
	char      label[64];     /* the fallback name — the command */
	char      shown[128];    /* the title last given to the compositor */
} tab_t;

/* One of these per committed frame, handed to the feedback object and freed by
 * whichever of `presented` or `discarded` arrives — the protocol guarantees
 * exactly one of the two, and destroys the object itself afterwards.
 *
 * ⚠ EXACTLY ONE OF THE TWO ARRIVES *EVENTUALLY*, WHICH IS NOT THE SAME AS
 * BEFORE WE EXIT. A frame committed just before the child quits is still in
 * flight when the loop ends, and neither event will ever come — so the object
 * and this context leaked, one pair per unfinished frame. LeakSanitizer caught
 * it and, because it makes the process exit 1, it also broke the assertion
 * that the child's exit status is the window's: the terminal reported 1 for a
 * child that had exited 3. Hence the list — teardown reaps whatever is still
 * outstanding. */
typedef struct feedback_ctx {
	win_t   *win;
	uint64_t commit_ns;   /* our clock, when the frame was committed */
	uint64_t input_ns;    /* our clock, the keystroke behind it (0 if none) */

	struct wp_presentation_feedback *fb;
	struct feedback_ctx *prev, *next;
} feedback_ctx_t;

typedef struct {
	uint64_t n;
	double   min_ms, max_ms, sum_ms;
} latency_stat_t;

static void stat_add(latency_stat_t *s, double ms)
{
	if (s->n == 0 || ms < s->min_ms) s->min_ms = ms;
	if (s->n == 0 || ms > s->max_ms) s->max_ms = ms;
	s->sum_ms += ms;
	s->n++;
}

typedef struct {
	struct wl_buffer *wl;
	uint32_t         *px;
	size_t            size;
	int               w, h;
	bool              busy;      /* the compositor still has it */

	/* ── why damage is PER BUFFER ────────────────────────────────────────────
	 *
	 * Two buffers alternate, so the one about to be painted holds what was
	 * drawn TWO frames ago, not one. A row that changed while the other buffer
	 * was in front is still stale in this one — repainting only what changed
	 * since the last frame would leave it showing week-old pixels.
	 *
	 * So each buffer carries the rows it still owes, and a row marked dirty by
	 * the grid is added to the buffer that is NOT being painted. `fresh` is
	 * false until it has been painted in full at least once, because a buffer
	 * straight out of mmap contains zeroes rather than a screen. */
	uint8_t          *pending;   /* one byte per grid row */
	int               pending_rows;
	bool              fresh;
	/* The tab bar is not a grid row, so `pending` says nothing about it — and
	 * it is stale in this buffer for exactly the same reason a row is: the
	 * other buffer was in front when it last changed. */
	bool              bar_stale;
} buffer_t;

struct win {
	/* Wayland globals. */
	struct wl_display    *dpy;
	struct wl_registry   *registry;
	struct wl_compositor *compositor;
	struct wl_shm        *shm;
	struct xdg_wm_base   *wm_base;
	struct wl_seat       *seat;
	struct wl_keyboard   *kbd;
	struct wl_pointer    *ptr;

	/* ── the cursor image ───────────────────────────────────────────────────
	 *
	 * wp_cursor_shape_v1 names a shape and lets the COMPOSITOR draw it. The
	 * alternative is libwayland-cursor: load a theme from disk, find the
	 * "text" image, allocate a shm buffer, attach it to a surface — a file
	 * search and a second buffer at startup, for an I-beam. This protocol is
	 * one request.
	 *
	 * ⚠ A CLIENT THAT NEVER SETS A CURSOR DOES NOT GET A DEFAULT ONE. The
	 * pointer keeps whatever image the last surface asked for, so the terminal
	 * inherits an I-beam from an editor or, worse, a resize arrow — and it
	 * looks like the compositor is stuck rather than like this program never
	 * spoke. With no manager announced there is nothing to be done about it,
	 * and that is stated here rather than left as a mystery. */
	struct wp_cursor_shape_manager_v1 *shape_mgr;
	struct wp_cursor_shape_device_v1  *shape_dev;
	uint32_t enter_serial;
	bool     cursor_is_text;

	struct wl_surface    *surface;
	struct xdg_surface   *xdg_surface;
	struct xdg_toplevel  *toplevel;
	struct wl_callback   *frame;

	/* Pixels. One pool, NBUFFERS slices of it. */
	struct wl_shm_pool *pool;
	int                 pool_fd;
	uint8_t            *pool_mem;
	size_t              pool_size;
	buffer_t            buf[NBUFFERS];

	/* Keyboard. */
	struct xkb_context *xkb;
	struct xkb_keymap  *keymap;
	struct xkb_state   *xkb_state;

	/* ── the tabs ───────────────────────────────────────────────────────────
	 *
	 * One window, several terminals. Each tab is a whole session — its own
	 * grid, its own parser, its own pty and its own child — and the ONLY thing
	 * they share is this window, the font and the renderer.
	 *
	 * ⚠ EVERY TAB IS PARSED, NOT ONLY THE VISIBLE ONE. A program in a
	 * background tab keeps producing output, and a terminal that stopped
	 * reading it would fill the pty's buffer and BLOCK THAT PROGRAM — a build
	 * running in another tab would stop making progress whenever it produced a
	 * few kilobytes, which looks like the build hanging. Only the ACTIVE tab is
	 * ever drawn; the rest are read, parsed into their own grids, and shown the
	 * moment somebody switches.
	 *
	 * `g`, `vt` and `pty` point INTO the active tab, so every line of code
	 * written before tabs existed still means what it meant. */
	tab_t      *tabs[SYN_MAX_TABS];
	int         ntabs;
	int         active;
	st_grid_t  *g;
	st_vt_t    *vt;
	st_pty_t   *pty;
	st_font_t  *font;
	st_render_t *ren;
	/* ⚠ THE CALLER'S HANDLE ON THE FONT. A reload can open a different face and
	 * close the one that was in use; the caller closes whatever is left at the
	 * end, so its pointer has to be moved too or it closes freed memory. */
	st_font_t **fontp;

	/* ── re-reading the config while running ────────────────────────────────
	 *
	 * `conf` is what main.c decided (see st_win_conf_t): the file, whether to
	 * watch it, and the flags that must keep beating it. `cur_font`/`cur_size`
	 * are what is actually loaded, so a reload can tell whether the font
	 * changed — reopening the same face on every write to the file would
	 * rebuild the glyph atlas for nothing.
	 *
	 * The watch is on DIRECTORIES, not files, and that is not a shortcut. Both
	 * writers of these files — the desktop's theme helper and any competent
	 * editor — write a temp file and rename it over the target, which is the
	 * right way to avoid a reader seeing half a palette. A watch on the file
	 * itself follows the INODE, so it stays attached to the old contents and
	 * never fires again after the first rename. */
	const st_win_conf_t *conf;
	char        cur_font[256];
	double      cur_size;
	int         inotify_fd;
	int         nwatch;

	/* What a new tab runs, and how it starts out. */
	const st_tab_spec_t *spec;

	/* ── the bar ────────────────────────────────────────────────────────────
	 *
	 * Height in pixels, and ZERO WHENEVER THERE IS ONE TAB. A row of chrome
	 * that never changes is a row of the screen the person paid for and cannot
	 * use; every terminal with tabs hides it at one, and so does this.
	 *
	 * `bar_dirty` is set by anything that changes what it SAYS — a new tab, a
	 * closed one, a switch, a title arriving. It is tracked per buffer as well
	 * (see buffer_t.bar_stale) for the same reason the rows are. */
	int      bar_h;
	bool     bar_dirty;
	int      tab_x0[SYN_MAX_TABS], tab_x1[SYN_MAX_TABS];

	/* ⚠ THE STATUS THE WINDOW RETURNS IS THE FIRST TAB'S, and it is recorded
	 * when that tab exits rather than read at the end — by then the tab is
	 * gone. A script running `syntty win -- make; echo $?` asked about the
	 * command it named, and opening a second tab must not change the answer. */
	int      first_status;
	bool     first_done;
	uint64_t tabs_opened;
	/* Bumped whenever a tab is opened or closed. The poll set is built from
	 * the tab array and then handed to poll(); if a KEYSTROKE opens or closes
	 * one while those results are being processed, every index after it means
	 * a different session than it did. Rather than repair the mapping, the
	 * reads are abandoned for that one iteration — poll is level-triggered, so
	 * whatever was waiting is still waiting a moment later. */
	unsigned tab_gen;
	/* ⚠ TAKEN BEFORE THE GRID IS FREED. The window's memory figure used to be
	 * read from the caller's grid at the end; there is no such grid now, and
	 * asking after the last tab closed printed a confident `0 bytes`. */
	size_t   last_grid_bytes;
	uint16_t last_cols, last_rows;

	int  win_w, win_h;       /* pixels the compositor asked for */
	bool dirty;              /* the grid changed since the last paint */
	bool configured;
	bool closed;
	bool needs_frame;        /* a frame callback is outstanding */

	uint64_t t_start;
	uint64_t t_first_frame;
	uint64_t frames, skipped;

	/* Latency. `presentation` is NULL on a compositor that does not offer the
	 * protocol, and everything here then stays zero rather than guessing. */
	struct wp_presentation *presentation;
	bool     clock_ok;        /* the compositor's clock is our clock */
	uint32_t clock_id;
	latency_stat_t commit_to_photon;
	latency_stat_t input_to_photon;
	uint64_t discarded;       /* frames never shown — composited over, or idle */

	/* The keystroke whose consequence has not yet been drawn. Earliest wins:
	 * if three keys arrive before the next frame, the number that matters is
	 * how long the FIRST one waited. */
	uint64_t pending_input_ns;

	/* Frames committed whose fate is not yet known. */
	feedback_ctx_t *outstanding;

	/* ── the pointer ────────────────────────────────────────────────────────
	 *
	 * Where it is, in cells, and what is held down. `held` is the button that
	 * motion is reported WITH — ?1002 sends motion only while something is
	 * pressed, and the button number goes in the same field.
	 *
	 * `clicks` is how many times the same cell has been hit in quick
	 * succession: one selects characters, two selects words, three selects
	 * lines. It resets on a different cell as well as on time, because two
	 * deliberate clicks in different places are not a double-click however
	 * fast somebody is. */
	int      ptr_col, ptr_row;
	int      ptr_px, ptr_py;   /* surface pixels, for the bar */
	unsigned buttons;          /* bit per protocol button, 0..2 */
	int      held;             /* the lowest button held, or ST_BTN_NONE */
	bool     selecting;        /* a drag with the left button is under way */
	uint32_t click_ms;
	int      clicks, click_col, click_row;

	/* Wheel notches waiting for the frame that completes them. A wheel sends
	 * axis_discrete on version 5 and up; a touchpad sends only the continuous
	 * value, which is accumulated into notches so that both end up here. */
	double   axis_acc[2];
	int      axis_notch[2];

	int      scroll_lines;     /* lines a wheel notch moves — configurable */

	uint64_t mouse_sent;       /* events handed to the child */
	uint64_t mouse_dropped;    /* events the old encoding could not carry */

	/* ── the clipboard, and the OTHER clipboard ─────────────────────────────
	 *
	 * Two selections, and they are not the same thing. THE CLIPBOARD is what
	 * Ctrl+Shift+C fills and Ctrl+Shift+V pastes. THE PRIMARY is filled merely
	 * by highlighting and pasted by the middle button — a habit thirty years
	 * old, and a terminal without it feels broken in a way people struggle to
	 * name.
	 *
	 * `*_text` is what WE offer, when we own a selection; `*_offer` is what
	 * somebody else is offering. Owning means holding the bytes until another
	 * program asks for them, which is why the text is kept rather than the
	 * grid coordinates: the selection it came from may be long gone. */
	struct wl_data_device_manager *dd_mgr;
	struct wl_data_device         *data_dev;
	struct wl_data_source         *clip_source;
	struct wl_data_offer          *clip_offer;   /* the selection itself */
	struct wl_data_offer          *clip_pending; /* announced, not yet claimed */
	int                            clip_offer_mime;   /* index, -1 for none */
	int                            clip_pending_mime;

	struct zwp_primary_selection_device_manager_v1 *ps_mgr;
	struct zwp_primary_selection_device_v1         *ps_dev;
	struct zwp_primary_selection_source_v1         *prim_source;
	struct zwp_primary_selection_offer_v1          *prim_offer;
	struct zwp_primary_selection_offer_v1          *prim_pending;
	int                                             prim_offer_mime;
	int                                             prim_pending_mime;

	char  *clip_text;   size_t clip_text_len;
	char  *prim_text;   size_t prim_text_len;
	/* Text held that we have not been able to CLAIM yet — see
	 * selections_flush(): taking a selection needs a serial from a real input
	 * event, and a child can copy before anybody has touched the window. */
	bool   clip_want, prim_want;

	/* ⚠ A SELECTION CANNOT BE SET WITHOUT A SERIAL from a real input event.
	 * The compositor uses it to check that a client taking the clipboard was
	 * actually being used by the person at the time, which is what stops a
	 * background window stealing it. */
	uint32_t last_serial;

	/* A paste in flight: the read end of a pipe the other program is writing
	 * to, plus what has arrived. In the poll loop rather than read inline —
	 * see the note on paste_start(). */
	int      paste_fd;
	char    *paste_buf;
	size_t   paste_len, paste_cap;
	uint64_t paste_too_big;

	/* Where the cursor was when the last frame was painted. Moving it changes
	 * no CELL, so the grid never reports it — but it changes two rows on
	 * screen, the one it left and the one it arrived at. Left out, the cursor
	 * smears a trail of itself across the window. */
	uint16_t last_cx, last_cy;

	/* Rows painted, and rows there were to paint, since the window opened.
	 * The ratio is the whole claim damage tracking makes. */
	uint64_t rows_painted, rows_possible;

	/* ── deadline rendering ─────────────────────────────────────────────────
	 *
	 * `last_present_ns` and `refresh_ns` come from the presented event and are
	 * the compositor's own account of when the last frame turned into light
	 * and how long until the next refresh may occur. Together they predict the
	 * next vblank, which is what makes rendering LATE possible.
	 *
	 * `paint_due_ns` is when the pending paint should happen — 0 for "nothing
	 * scheduled". `paint_cost_ns` is a decaying average of what painting has
	 * actually cost, so the margin is measured rather than guessed. */
	uint64_t last_present_ns;
	uint64_t refresh_ns;
	uint64_t paint_due_ns;
	uint64_t paint_cost_ns;
	bool     deadline;        /* the mode is on (it can be turned off to A/B) */
	uint64_t late;            /* paints that missed their own deadline */
	uint64_t on_time;
};

/* Copy and paste, defined with the rest of the selection plumbing further
 * down. Declared here because the KEYBOARD is the first thing that needs them
 * and it comes first in this file. */
static void own_clipboard(win_t *w, char *text, size_t len);
static void paste_start(win_t *w, bool primary);
static void selections_flush(win_t *w);

/* The next vblank at or after `now`, or 0 when there is nothing to predict
 * from — no presented event yet, or a compositor that reported refresh 0
 * because the output has no constant rate.
 *
 * ⚠ Zero is a real answer and means "do not guess". A client that invents a
 * 16.7 ms cadence because it assumes 60 Hz will be wrong on every 144 Hz
 * monitor and on every variable-refresh one, and being wrong here does not
 * degrade gracefully: it renders after the deadline and lands a whole frame
 * late, which is the exact problem this is meant to fix. */
static uint64_t next_vblank(const win_t *w, uint64_t now)
{
	if (!w->last_present_ns || !w->refresh_ns)
		return 0;
	if (now <= w->last_present_ns)
		return w->last_present_ns + w->refresh_ns;
	uint64_t elapsed = now - w->last_present_ns;
	uint64_t periods = elapsed / w->refresh_ns + 1;
	return w->last_present_ns + periods * w->refresh_ns;
}

static void feedback_unlink(feedback_ctx_t *ctx)
{
	win_t *w = ctx->win;
	if (ctx->prev) ctx->prev->next = ctx->next;
	else           w->outstanding  = ctx->next;
	if (ctx->next) ctx->next->prev = ctx->prev;
	ctx->prev = ctx->next = NULL;
}

/* ── shm ────────────────────────────────────────────────────────────────────
 *
 * memfd, not a file in /tmp. A terminal that creates a temporary file to hold
 * its pixels leaves one behind when it is killed, and the contents of a
 * terminal window are not something to write to disk even briefly. */
static int shm_fd(size_t size)
{
	int fd = memfd_create("syntty", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (fd < 0)
		return -1;
	if (ftruncate(fd, (off_t)size) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl)
{
	(void)wl;
	buffer_t *b = data;
	b->busy = false;
}
static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void pool_destroy(win_t *w)
{
	for (int i = 0; i < NBUFFERS; i++) {
		if (w->buf[i].wl)
			wl_buffer_destroy(w->buf[i].wl);
		w->buf[i].wl = NULL;
		w->buf[i].px = NULL;
		free(w->buf[i].pending);
		w->buf[i].pending = NULL;
		w->buf[i].pending_rows = 0;
	}
	if (w->pool)     { wl_shm_pool_destroy(w->pool); w->pool = NULL; }
	if (w->pool_mem) { munmap(w->pool_mem, w->pool_size); w->pool_mem = NULL; }
	if (w->pool_fd >= 0) { close(w->pool_fd); w->pool_fd = -1; }
}

/* Allocate both buffers at the current window size. Called on every resize,
 * which is rare enough that reallocating is simpler than growing in place and
 * has no ongoing cost. */
static bool pool_create(win_t *w, int width, int height)
{
	pool_destroy(w);

	size_t one   = (size_t)width * height * 4;
	size_t total = one * NBUFFERS;

	w->pool_fd = shm_fd(total);
	if (w->pool_fd < 0)
		return false;

	w->pool_mem = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED,
	                   w->pool_fd, 0);
	if (w->pool_mem == MAP_FAILED) {
		w->pool_mem = NULL;
		close(w->pool_fd);
		w->pool_fd = -1;
		return false;
	}
	w->pool_size = total;
	w->pool = wl_shm_create_pool(w->shm, w->pool_fd, (int32_t)total);

	for (int i = 0; i < NBUFFERS; i++) {
		w->buf[i].wl = wl_shm_pool_create_buffer(
			w->pool, (int32_t)(one * i), width, height,
			(int32_t)(width * 4), WL_SHM_FORMAT_XRGB8888);
		w->buf[i].px   = (uint32_t *)(w->pool_mem + one * i);
		w->buf[i].size = one;
		w->buf[i].w    = width;
		w->buf[i].h    = height;
		w->buf[i].busy = false;
		w->buf[i].fresh = false;
		w->buf[i].bar_stale = true;
		free(w->buf[i].pending);
		w->buf[i].pending_rows = w->g ? w->g->rows : 1;
		w->buf[i].pending = xcalloc((size_t)w->buf[i].pending_rows, 1);
		wl_buffer_add_listener(w->buf[i].wl, &buffer_listener, &w->buf[i]);
	}
	return true;
}

static buffer_t *buffer_free_one(win_t *w)
{
	for (int i = 0; i < NBUFFERS; i++)
		if (!w->buf[i].busy && w->buf[i].px)
			return &w->buf[i];
	return NULL;
}

/* ── presentation feedback ──────────────────────────────────────────────── */

/* The frame reached the screen. `tv_sec_hi/lo` and `tv_nsec` are on the clock
 * the compositor named in its clock_id event, which is why that is checked
 * before any of this is believed. */
static void fb_presented(void *data, struct wp_presentation_feedback *fb,
                         uint32_t tv_sec_hi, uint32_t tv_sec_lo,
                         uint32_t tv_nsec, uint32_t refresh,
                         uint32_t seq_hi, uint32_t seq_lo, uint32_t flags)
{
	(void)seq_hi; (void)seq_lo; (void)flags;
	feedback_ctx_t *ctx = data;
	win_t *w = ctx->win;

	if (w->clock_ok) {
		uint64_t shown = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * 1000000000ull
		               + tv_nsec;
		/* Guarded rather than trusted. A compositor that reports a time before
		 * our commit is reporting on a clock we do not share, whatever it said
		 * its clock_id was, and a negative latency printed as a huge unsigned
		 * number is the classic way that goes unnoticed. */
		if (shown > ctx->commit_ns)
			stat_add(&w->commit_to_photon,
			         (double)(shown - ctx->commit_ns) / 1e6);
		if (ctx->input_ns && shown > ctx->input_ns)
			stat_add(&w->input_to_photon,
			         (double)(shown - ctx->input_ns) / 1e6);

		/* The cadence, straight from the compositor. `refresh` is its own
		 * prediction of how long after this presentation the next refresh may
		 * occur — the protocol says in as many words that it is there to help
		 * clients target the next few vblanks, which is exactly what the
		 * deadline mode does with it. */
		if (shown > w->last_present_ns) {
			w->last_present_ns = shown;
			w->refresh_ns      = refresh;   /* 0 = no constant rate; see above */
		}
	}
	feedback_unlink(ctx);
	wp_presentation_feedback_destroy(fb);
	free(ctx);
}

/* The content was never shown — superseded by a later commit, or the output
 * went idle. Counted, not ignored: a run whose frames are mostly discarded is
 * measuring something other than what it thinks. */
static void fb_discarded(void *data, struct wp_presentation_feedback *fb)
{
	feedback_ctx_t *ctx = data;
	ctx->win->discarded++;
	feedback_unlink(ctx);
	wp_presentation_feedback_destroy(fb);
	free(ctx);
}

static void fb_sync_output(void *data, struct wp_presentation_feedback *fb,
                           struct wl_output *o)
{ (void)data; (void)fb; (void)o; }

static const struct wp_presentation_feedback_listener feedback_listener = {
	fb_sync_output, fb_presented, fb_discarded
};

static void presentation_clock(void *data, struct wp_presentation *p,
                               uint32_t clk_id)
{
	(void)p;
	win_t *w = data;
	w->clock_id = clk_id;
	/* ⚠ CHECKED, NOT ASSUMED. now_ns() is CLOCK_MONOTONIC; a compositor
	 * timestamping on anything else would have us subtracting two unrelated
	 * clocks and printing the result as a latency. */
	w->clock_ok = (clk_id == CLOCK_MONOTONIC);
}
static const struct wp_presentation_listener presentation_listener = {
	presentation_clock
};

/* ── tabs ───────────────────────────────────────────────────────────────────
 *
 * One window, several terminals, and the tabs share nothing except the window,
 * the font and the renderer. That is what makes this cheap: a second tab costs
 * a grid, a parser and a pty — tens of kilobytes — rather than a second process
 * with a second copy of everything in it.
 *
 * ⚠ AND EVERY TAB IS READ, whether or not it is the one on screen. A terminal
 * that stopped draining a background pty would fill the kernel's buffer and
 * BLOCK the program writing into it: a build running in another tab would stop
 * dead every few kilobytes, and it would look like the build had hung rather
 * than like the terminal had stopped listening. Reading is not the expensive
 * part — drawing is, and only the active tab is ever drawn. */

static void fit_grid(win_t *w);
static void tabs_invalidate(win_t *w);
static void bar_update(win_t *w);

/* The name a tab wears until the program inside it sets a title. */
static void tab_label(tab_t *t, char *const argv[])
{
	const char *cmd = (argv && argv[0]) ? argv[0] : "sh";
	const char *slash = strrchr(cmd, '/');
	snprintf(t->label, sizeof t->label, "%s", slash ? slash + 1 : cmd);
}

/* What the bar and the window title call a tab: what the program says it is,
 * and the command it was started as until it says anything. */
static const char *tab_name(const tab_t *t)
{
	return t->vt.title[0] ? t->vt.title : t->label;
}

static tab_t *tab_new(win_t *w)
{
	if (w->ntabs >= SYN_MAX_TABS)
		return NULL;

	/* ⚠ AT THE CURRENT SIZE, not the size the window started at. A tab born
	 * 80x24 inside a 200x50 window would have its child told 80x24 first and
	 * resized a moment later; every full-screen program redraws twice and some
	 * of them wrap their first screen at the wrong width, which is visible and
	 * looks like a bug in the program. */
	uint16_t cols = w->g ? w->g->cols : w->spec->cols;
	uint16_t rows = w->g ? w->g->rows : w->spec->rows;

	tab_t *t = xcalloc(1, sizeof *t);
	st_grid_init(&t->g, cols, rows, w->spec->scrollback);
	st_vt_init(&t->vt, &t->g);

	/* ⚠ A SERIAL, NOT A POSITION. `SYNTTY_TAB=2` has to keep meaning the same
	 * session for as long as it runs; numbering by where a tab sits in the bar
	 * would renumber every shell behind it the moment somebody closed one. */
	char serial[24];
	snprintf(serial, sizeof serial, "%llu",
	         (unsigned long long)(w->tabs_opened + 1));
	if (!st_pty_spawn_env(&t->pty, w->spec->argv, cols, rows,
	                      "SYNTTY_TAB", serial)) {
		st_vt_free(&t->vt);
		st_grid_free(&t->g);
		free(t);
		return NULL;
	}
	/* The window's loop polls; a blocking read here would mean not answering
	 * the compositor while a child is quiet. */
	st_pty_set_nonblocking(&t->pty);

	tab_label(t, w->spec->argv);
	t->first = (w->ntabs == 0);
	w->tabs[w->ntabs++] = t;
	w->tabs_opened++;
	w->tab_gen++;
	return t;
}

static void tab_free(tab_t *t)
{
	st_vt_free(&t->vt);
	st_grid_free(&t->g);
	free(t);
}

/* Both buffers hold the OTHER tab's pixels, so neither can be repaired by a
 * partial repaint — there is no row-level relationship between what is there
 * and what should be. A full paint of both, once, is the only correct answer.
 *
 * ⚠ This is not the ping-pong bug that damage tracking had. That one marked the
 * other buffer stale on EVERY full paint, so the two invalidated each other for
 * ever; this happens once, when the content genuinely changes wholesale. */
static void tabs_invalidate(win_t *w)
{
	for (int i = 0; i < NBUFFERS; i++)
		w->buf[i].fresh = false;
	if (w->g) {
		st_grid_dirty_all(w->g);
		w->last_cx = w->g->cx;
		w->last_cy = w->g->cy;
	}
	w->bar_dirty = true;
	w->dirty = true;
}

static void tab_activate(win_t *w, int i)
{
	if (i < 0 || i >= w->ntabs)
		return;
	w->active = i;
	tab_t *t = w->tabs[i];
	w->g   = &t->g;
	w->vt  = &t->vt;
	w->pty = &t->pty;
	/* The images belong to the tab's parser, not to the window. Left pointing
	 * at the old one, a picture from another tab would be composited over this
	 * one's text. */
	st_render_set_gfx(w->ren, t->vt.gfx);
	tabs_invalidate(w);
}

/* The window's title follows the active tab, and only changes when it has to:
 * `set_title` on every frame is a message per frame for a string that is the
 * same, and some compositors animate on it. */
static void title_update(win_t *w)
{
	if (!w->toplevel || w->ntabs == 0)
		return;
	tab_t *t = w->tabs[w->active];
	const char *name = tab_name(t);
	if (!strcmp(name, t->shown))
		return;
	snprintf(t->shown, sizeof t->shown, "%s", name);
	xdg_toplevel_set_title(w->toplevel, name);
	w->bar_dirty = true;
}

static void tab_close(win_t *w, int i)
{
	if (i < 0 || i >= w->ntabs)
		return;
	tab_t *t = w->tabs[i];

	/* ⚠ RECORDED HERE, NOT READ AT THE END. By the time the window closes this
	 * tab no longer exists, and the status a script asked about is the status
	 * of the command it named on the command line. */
	if (i == w->active) {
		w->last_grid_bytes = st_grid_bytes(&t->g);
		w->last_cols = t->g.cols;
		w->last_rows = t->g.rows;
	}

	int status = st_pty_reap(&t->pty);
	if (t->first && !w->first_done) {
		w->first_status = status;
		w->first_done = true;
	}
	tab_free(t);

	for (int k = i; k + 1 < w->ntabs; k++)
		w->tabs[k] = w->tabs[k + 1];
	w->ntabs--;
	w->tab_gen++;

	if (w->ntabs == 0) {
		w->g = NULL; w->vt = NULL; w->pty = NULL;
		w->closed = true;
		return;
	}

	int next = w->active;
	if (i < next)        next--;             /* everything after it shifted */
	else if (i == next)  next = (i < w->ntabs) ? i : w->ntabs - 1;
	tab_activate(w, next);
	bar_update(w);
	title_update(w);
}

static void tab_open(win_t *w)
{
	if (!tab_new(w))
		return;                              /* at the cap, or the fork failed */
	tab_activate(w, w->ntabs - 1);
	bar_update(w);
	title_update(w);
}

static void tab_cycle(win_t *w, int delta)
{
	if (w->ntabs < 2)
		return;
	int n = (w->active + delta) % w->ntabs;
	if (n < 0)
		n += w->ntabs;
	tab_activate(w, n);
	title_update(w);
}

/* ⚠ THE BAR EXISTS ONLY WHEN THERE IS SOMETHING TO CHOOSE. One tab is the
 * overwhelmingly common case, and a permanent row of chrome saying "1 bash" is
 * a row of somebody's screen spent on nothing. Appearing and disappearing costs
 * a resize, which the grid and the child are told about exactly as they are for
 * any other resize. */
static void bar_update(win_t *w)
{
	int want = w->ntabs > 1 ? st_font_cell_h(w->font) : 0;
	if (want == w->bar_h)
		return;
	w->bar_h = want;
	st_render_origin(w->ren, want);
	fit_grid(w);
	tabs_invalidate(w);
}

/* Draw it. The active tab is inverted, which is the same trick the selection
 * uses and for the same reason: it contrasts with whatever the colours are. */
static void draw_bar(win_t *w, buffer_t *b)
{
	if (w->bar_h <= 0 || !b->px)
		return;

	uint32_t fg, bg;
	st_render_colors_get(w->ren, &fg, &bg);
	int cw = st_font_cell_w(w->font);
	int x = 0;

	for (int i = 0; i < w->ntabs && x < b->w; i++) {
		char seg[96];
		snprintf(seg, sizeof seg, " %d %s ", i + 1, tab_name(w->tabs[i]));

		/* At most 24 cells each, so one program with a very long title cannot
		 * push every other tab off the bar. */
		int max = 24 * cw;
		if (x + max > b->w)
			max = b->w - x;

		bool act = (i == w->active);
		int nx = st_render_text(w->ren, b->px, b->w, b->w, b->h, x, 0, max,
		                        seg, act ? bg : fg, act ? fg : bg);
		w->tab_x0[i] = x;
		w->tab_x1[i] = nx;
		x = nx;
	}
	/* The rest of the strip, so a closed tab leaves no ghost behind it. */
	if (x < b->w)
		st_render_text(w->ren, b->px, b->w, b->w, b->h, x, 0, b->w - x, "",
		               fg, bg);
}

/* ── painting ───────────────────────────────────────────────────────────── */

static void frame_done(void *data, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_listener = { frame_done };

static void paint(win_t *w)
{
	if (!w->configured || w->win_w <= 0 || w->win_h <= 0 || !w->g)
		return;

	buffer_t *b = buffer_free_one(w);
	if (!b) {
		/* Both buffers are with the compositor. Skipping is right: the next
		 * frame is milliseconds away and will carry newer content than
		 * anything that could be drawn by blocking here. */
		w->skipped++;
		return;
	}

	uint64_t t0 = now_ns();

	/* ── what actually needs repainting ─────────────────────────────────────
	 *
	 * `changed` is what moved on screen SINCE THE LAST FRAME: the rows the
	 * grid marked, plus the two the cursor touched — moving the cursor changes
	 * no cell, so the grid never reports it, but it changes the row it left
	 * and the row it arrived at. Left out, the cursor smears a trail of itself
	 * across the window.
	 *
	 * It is computed once and used twice: to paint this buffer, and to tell
	 * the OTHER buffer what it has missed. */
	int nrows = w->g->rows;
	uint8_t changed[SYN_MAX_ROWS];
	if (nrows > SYN_MAX_ROWS)
		nrows = SYN_MAX_ROWS;
	memset(changed, 0, (size_t)nrows);
	for (int y = 0; y < nrows; y++)
		if (st_grid_row_dirty(w->g, y))
			changed[y] = 1;
	if (w->last_cy < nrows) changed[w->last_cy] = 1;
	if (w->g->cy   < nrows) changed[w->g->cy]   = 1;

	/* A buffer straight out of mmap holds zeroes, not a screen — and
	 * st_render_rows deliberately leaves the margins alone. So a buffer that
	 * has never been painted in full gets a full paint, once.
	 *
	 * ⚠ THE ARRAY IS SIZED HERE, NOT WHERE THE BUFFER IS ALLOCATED. The pool
	 * is created during configure, and the grid is resized to fit the window
	 * immediately AFTERWARDS — so a `pending` sized at pool time is sized from
	 * the OLD row count. The mismatch made `full` true on every single frame
	 * and nothing ever corrected it: damage tracking reported 100% painted,
	 * saved nothing, and looked like it was working. */
	bool full = !b->fresh;
	if (b->pending_rows != nrows) {
		free(b->pending);
		b->pending      = xcalloc((size_t)nrows, 1);
		b->pending_rows = nrows;
		full = true;
	}

	size_t painted = 0;
	if (full) {
		st_render_grid(w->ren, w->g, b->px, b->w, b->w, b->h);
		painted = (size_t)nrows;
		b->fresh = true;
		b->bar_stale = true;      /* the strip above row 0 was not covered */
		memset(b->pending, 0, (size_t)b->pending_rows);
	} else {
		for (int y = 0; y < nrows; y++)
			if (changed[y])
				b->pending[y] = 1;
		st_render_rows(w->ren, w->g, b->pending, b->px, b->w, b->w, b->h);
		for (int y = 0; y < nrows; y++)
			painted += b->pending[y] ? 1 : 0;
	}
	w->rows_painted  += painted;
	w->rows_possible += (size_t)nrows;

	/* The bar, which is not a row and is therefore not in `pending`. Drawn
	 * when this buffer has never had it, or when what it says has changed
	 * since this buffer was last in front. */
	bool bar_drawn = false;
	if (w->bar_h > 0 && (b->bar_stale || w->bar_dirty)) {
		draw_bar(w, b);
		b->bar_stale = false;
		bar_drawn = true;
	}

	b->busy = true;
	w->dirty = false;
	w->frames++;
	uint64_t commit_ns = now_ns();

	/* What painting actually costs, as a decaying average. The margin below is
	 * built from this rather than from a constant, because the honest answer
	 * depends on the window size and the machine — a number picked here would
	 * be too small on somebody's 4K screen, which is the direction that
	 * fails. */
	uint64_t cost = commit_ns - t0;
	w->paint_cost_ns = w->paint_cost_ns
		? (w->paint_cost_ns * 3 + cost) / 4 : cost;
	if (!w->t_first_frame)
		w->t_first_frame = commit_ns;

	/* Ask to be told when THIS frame reaches the screen, and carry with the
	 * request the keystroke that caused it. Requested before the attach so the
	 * feedback belongs to the commit below and not to the one after it. */
	if (w->presentation) {
		feedback_ctx_t *ctx = xcalloc(1, sizeof *ctx);
		ctx->win       = w;
		ctx->commit_ns = commit_ns;
		ctx->input_ns  = w->pending_input_ns;
		w->pending_input_ns = 0;

		struct wp_presentation_feedback *fb =
			wp_presentation_feedback(w->presentation, w->surface);
		wp_presentation_feedback_add_listener(fb, &feedback_listener, ctx);

		ctx->fb   = fb;
		ctx->next = w->outstanding;
		if (ctx->next)
			ctx->next->prev = ctx;
		w->outstanding = ctx;
	}

	wl_surface_attach(w->surface, b->wl, 0, 0);

	/* Tell the compositor what changed, as runs of rows rather than one
	 * rectangle per row: a screen edit is usually contiguous, and a hundred
	 * one-row rectangles cost more to send and to process than the four they
	 * collapse into. */
	int ch = st_font_cell_h(w->font);
	if (full) {
		wl_surface_damage_buffer(w->surface, 0, 0, b->w, b->h);
	} else {
		if (bar_drawn)
			wl_surface_damage_buffer(w->surface, 0, 0, b->w, w->bar_h);
		/* ⚠ THE ROWS ARE OFFSET BY THE BAR. A damage rectangle computed from
		 * the row index alone points a whole cell too high once there is a tab
		 * bar, so every partial repaint would tell the compositor to refresh
		 * the wrong strip — the change would appear a row late, or not at
		 * all. */
		for (int y = 0; y < nrows; ) {
			if (!b->pending[y]) { y++; continue; }
			int start = y;
			while (y < nrows && b->pending[y])
				y++;
			wl_surface_damage_buffer(w->surface, 0, w->bar_h + start * ch,
			                         b->w, (y - start) * ch);
		}
	}

	/* ⚠ THE OTHER BUFFER HAS NOT SEEN THIS FRAME, and it accumulates
	 * `changed` whether this paint was full or partial. The first version set
	 * the other buffer's `fresh` to false after a full paint instead, which
	 * made the two ping-pong full repaints forever: every frame invalidated
	 * its neighbour, so damage tracking measured 100% painted and saved
	 * exactly nothing while appearing to work. */
	for (int i = 0; i < NBUFFERS; i++) {
		buffer_t *o = &w->buf[i];
		if (o == b)
			continue;
		if (bar_drawn)
			o->bar_stale = true;
		if (!o->pending || o->pending_rows != nrows)
			continue;
		for (int y = 0; y < nrows; y++)
			if (changed[y])
				o->pending[y] = 1;
	}
	w->bar_dirty = false;
	if (!full)
		memset(b->pending, 0, (size_t)nrows);

	st_grid_clear_dirty(w->g);
	w->last_cx = w->g->cx;
	w->last_cy = w->g->cy;

	wl_surface_commit(w->surface);
}

/* Ask to be told when the compositor is ready for another frame. This is the
 * throttle: nothing paints outside it, so a program flooding the pty cannot
 * make this process draw faster than the display can show. */
static void request_frame(win_t *w)
{
	if (w->needs_frame || !w->configured)
		return;
	w->frame = wl_surface_frame(w->surface);
	wl_callback_add_listener(w->frame, &frame_listener, w);
	w->needs_frame = true;
	wl_surface_commit(w->surface);
}

/* ── the whole point of stage 3 ─────────────────────────────────────────────
 *
 * A client that paints when the frame callback fires paints at the START of
 * the interval, committing to a state that any keystroke arriving a
 * millisecond later is not part of — so that keystroke waits for the NEXT
 * frame, and the person waits up to a whole refresh period for a character
 * they have already typed.
 *
 * Painting as late as the deadline allows catches it in THIS frame. The
 * deadline is the next vblank, less what painting costs and less a slack that
 * covers getting the buffer to the compositor in time.
 *
 * ⚠ IT DEGRADES TO THE OLD BEHAVIOUR RATHER THAN TO A BROKEN ONE. With no
 * presented event yet, or a compositor that reports no constant refresh rate,
 * next_vblank() returns 0 and this paints immediately — which is exactly what
 * every client does today. Being unable to predict the cadence must never be
 * worse than not trying to. */
static void schedule_paint(win_t *w)
{
	uint64_t now = now_ns();

	if (!w->deadline) {
		paint(w);
		request_frame(w);
		return;
	}

	uint64_t vb = next_vblank(w, now);
	if (!vb) {
		paint(w);
		request_frame(w);
		return;
	}

	/* The margin: what a paint costs, plus half again as slack, plus a floor.
	 * Measured rather than assumed — see paint(). */
	uint64_t margin = w->paint_cost_ns + w->paint_cost_ns / 2 + 1000000ull;
	if (margin > w->refresh_ns / 2)
		margin = w->refresh_ns / 2;   /* never give up more than half a frame */

	uint64_t due = vb > margin ? vb - margin : now;
	if (due <= now) {
		/* Already past it — this frame is spoken for, so paint now and aim at
		 * the next one rather than deliberately missing two. */
		w->late++;
		paint(w);
		request_frame(w);
		return;
	}
	w->paint_due_ns = due;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t t)
{
	(void)t;
	win_t *w = data;
	wl_callback_destroy(cb);
	w->frame = NULL;
	w->needs_frame = false;

	if (w->dirty)
		schedule_paint(w);
}

/* ── xdg-shell ──────────────────────────────────────────────────────────── */

static void wm_base_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
	(void)d;
	/* Answered immediately and unconditionally. A client that does not is
	 * declared unresponsive and the compositor may offer to kill it. */
	xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { wm_base_ping };

/* Resize the grid to whatever the window now is, in whole cells.
 *
 * The GRID follows the WINDOW, not the other way round: a terminal that
 * insisted on its own size would fight every tiling compositor on the machine,
 * and SynapseOS ships one. */
static void fit_grid(win_t *w)
{
	if (!w->g)
		return;
	/* ⚠ NOT BEFORE THE WINDOW HAS A SIZE. Opening the startup tabs raises the
	 * bar, which calls this — and at that point the compositor has not
	 * configured anything, so win_w and win_h are zero and the arithmetic
	 * below clamps to a ONE BY ONE grid. Every child was then told it had one
	 * column, wrote its first line into it, and the real configure a moment
	 * later resized around the wreckage: the terminal came up with the bar
	 * drawn correctly and the program's first line of output gone. It looked
	 * like the bar had eaten it. */
	if (w->win_w <= 0 || w->win_h <= 0)
		return;

	int cw = st_font_cell_w(w->font), ch = st_font_cell_h(w->font);
	int cols = w->win_w / cw;
	int rows = (w->win_h - w->bar_h) / ch;
	if (cols < 1) cols = 1;
	if (rows < 1) rows = 1;

	if (cols == w->g->cols && rows == w->g->rows)
		return;

	/* ⚠ EVERY TAB, NOT ONLY THE ONE ON SCREEN. A program in a background tab
	 * that was never told the window changed keeps drawing to the old size,
	 * and the mess it makes only appears when somebody switches to it — long
	 * after the resize that caused it, which makes it look like the program's
	 * fault. */
	for (int i = 0; i < w->ntabs; i++) {
		st_grid_resize(&w->tabs[i]->g, (uint16_t)cols, (uint16_t)rows);
		/* The CHILD has to be told too, or every full-screen program on the
		 * other end keeps drawing to the old size. This is the half people
		 * forget, because the terminal itself looks correct. */
		st_pty_resize(&w->tabs[i]->pty, (uint16_t)cols, (uint16_t)rows);
	}
	w->dirty = true;
}

/* ── the config, re-read ────────────────────────────────────────────────────
 *
 * What a theme switch looks like from in here. See st_win_conf_t for why this
 * is driven by a file watch rather than by a signal.
 *
 * ⚠ NOTHING HERE MAY KILL THE WINDOW. Every failure is survivable and is
 * survived: an unreadable file leaves the colours alone, a font name that will
 * not open keeps the face already loaded, a broken line is reported by the
 * parser and the rest of the file is still applied. The alternative is a
 * terminal that vanishes because something else wrote a bad config while it
 * happened to be open, which is the worst possible way to lose a shell. */
static void config_reload(win_t *w)
{
	if (!w->conf || !w->conf->watch)
		return;

	st_config_t nc;
	st_config_defaults(&nc);
	st_config_load(&nc, w->conf->cfg && w->conf->cfg->path[0]
	                        ? w->conf->cfg->path : NULL);

	/* Colours first, because they cannot fail and cannot change the layout.
	 * Applied even when the file has gone: the config then names nothing, and
	 * "nothing" correctly means the built-in scheme rather than whatever the
	 * last theme left behind. */
	st_config_apply_colors(&nc, w->ren);

	if (nc.scroll_lines > 0)
		w->scroll_lines = nc.scroll_lines;

	/* ── the font ───────────────────────────────────────────────────────────
	 *
	 * A flag beats the file here exactly as it does at startup, or `syntty
	 * --font=X` would lose its font the first time the desktop wrote the
	 * config. */
	const char *want_font = w->conf->flag_font ? w->conf->flag_font : nc.font;
	double want_size = w->conf->flag_size > 0 ? w->conf->flag_size
	                 : nc.font_size    > 0 ? nc.font_size
	                 : 10.5;   /* points; 14px as it was */

	bool same_family = (!want_font && !w->cur_font[0])
	                || (want_font && !strcmp(want_font, w->cur_font));
	if (!same_family || want_size != w->cur_size) {
		char *err = NULL;
		st_font_t *nf = st_font_open(want_font, want_size, &err);
		if (!nf) {
			/* ⚠ SAID OUT LOUD AND SURVIVED. A font that cannot be opened is a
			 * typo in a file somebody just edited, and the terminal keeping
			 * the face it has while saying why is the only outcome that lets
			 * them fix it — in this window. */
			fprintf(stderr, "syntty: %s: %s (keeping the current font)\n",
			        want_font ? want_font : "the default font",
			        err ? err : "cannot open");
			free(err);
		} else {
			st_font_close(w->font);
			w->font = nf;
			*w->fontp = nf;
			st_render_set_font(w->ren, nf);
			snprintf(w->cur_font, sizeof w->cur_font, "%s",
			         want_font ? want_font : "");
			w->cur_size = want_size;

			/* ⚠ THE CELL SIZE CHANGED, SO EVERYTHING DERIVED FROM IT HAS TO BE
			 * REDONE — and bar_update alone is not enough: it returns early
			 * when the bar's height happens to come out the same, which is
			 * always true with one tab (both are zero). fit_grid is what tells
			 * every tab AND every child the new size, and without it the
			 * programs on the other end keep drawing to the old one. */
			bar_update(w);
			fit_grid(w);
		}
	}

	/* Both buffers hold pixels drawn in the old colours at the old size, and
	 * no per-row comparison can repair that — a full repaint of both is the
	 * only correct answer, which is what tabs_invalidate means. */
	tabs_invalidate(w);

	if (nc.errors)
		fprintf(stderr, "syntty: %s: %d problem%s, first at %s\n",
		        nc.path, nc.errors, nc.errors == 1 ? "" : "s", nc.first_error);
	st_config_free(&nc);
}

/* Watch the DIRECTORY of every file the config load actually opened — see the
 * note on win_t.inotify_fd for why the directory and not the file.
 *
 * ⚠ THE INCLUDED FILES MATTER MORE THAN THE MAIN ONE. The main file is the
 * user's and rarely changes; the generated palette an `include` points at is
 * the one a theme switch rewrites, and watching only syntty.conf would miss
 * every one of them. Duplicates are skipped rather than deduplicated properly:
 * they land in the same directory in every arrangement anybody actually has,
 * and inotify_add_watch returns the SAME descriptor for a directory already
 * watched, so a duplicate costs nothing anyway. */
static void watch_config(win_t *w)
{
	if (!w->conf || !w->conf->watch)
		return;

	w->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (w->inotify_fd < 0)
		return;                       /* no live reload; new windows still theme */

	const st_config_t *c = w->conf->cfg;
	/* The main file's own directory even when there is no file yet: that is
	 * where one appears the first time the desktop writes a theme, and a
	 * terminal open across that moment should follow it like any other. */
	const char *paths[1 + ST_CFG_MAX_FILES];
	int n = 0;
	if (c && c->path[0])
		paths[n++] = c->path;
	for (int i = 0; c && i < c->nfiles && n < (int)(sizeof paths / sizeof *paths); i++)
		paths[n++] = c->files[i];

	for (int i = 0; i < n; i++) {
		char dir[512];
		snprintf(dir, sizeof dir, "%s", paths[i]);
		char *slash = strrchr(dir, '/');
		if (!slash)
			continue;
		*slash = '\0';
		/* CLOSE_WRITE catches an editor writing in place; MOVED_TO catches the
		 * rename that every careful writer uses instead, including the theme
		 * helper; CREATE catches the file appearing where there was none. */
		if (inotify_add_watch(w->inotify_fd, dir,
		                      IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) >= 0)
			w->nwatch++;
	}

	if (w->nwatch == 0) {
		close(w->inotify_fd);
		w->inotify_fd = -1;
	}
}

/* Drain the watch. ⚠ EVERY EVENT IS DRAINED BEFORE ANYTHING IS RELOADED: one
 * rename produces several events across several watches, and reloading per
 * event would re-read the file and rebuild the glyph atlas four times for one
 * theme switch. It also does not matter WHICH file changed — the reload reads
 * the whole config from the top either way — so the events are counted, not
 * inspected. */
static void config_pump(win_t *w)
{
	if (w->inotify_fd < 0)
		return;

	/* Sized for a name of any length inotify can produce, so a single read
	 * never returns EINVAL for a buffer too small to hold one event. */
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	bool changed = false;
	for (;;) {
		ssize_t n = read(w->inotify_fd, buf, sizeof buf);
		if (n > 0) { changed = true; continue; }
		if (n < 0 && errno == EINTR)
			continue;
		break;
	}
	if (changed)
		config_reload(w);
}

static void xdg_surface_configure(void *data, struct xdg_surface *s,
                                  uint32_t serial)
{
	win_t *w = data;
	xdg_surface_ack_configure(s, serial);

	if (w->win_w <= 0 || w->win_h <= 0) {
		/* The compositor left the size to us, which it does for a
		 * floating window. Ask for what the grid we started with wants. */
		w->win_w = st_render_width(w->ren, w->g);
		w->win_h = st_render_height(w->ren, w->g) + w->bar_h;
	}

	bool resized = !w->buf[0].px
	            || w->buf[0].w != w->win_w || w->buf[0].h != w->win_h;
	if (resized && !pool_create(w, w->win_w, w->win_h))
		die("cannot allocate a %dx%d buffer", w->win_w, w->win_h);

	w->configured = true;
	fit_grid(w);
	w->dirty = true;
	paint(w);
	request_frame(w);
}
static const struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_configure
};

static void toplevel_configure(void *data, struct xdg_toplevel *t,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
	(void)t; (void)states;
	win_t *w = data;
	/* Zero means "you choose". Anything else is not a suggestion. */
	if (width > 0 && height > 0) {
		w->win_w = width;
		w->win_h = height;
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *t)
{
	(void)t;
	((win_t *)data)->closed = true;
}
static const struct xdg_toplevel_listener toplevel_listener = {
	toplevel_configure, toplevel_close, NULL, NULL
};

/* ── keyboard ───────────────────────────────────────────────────────────── */

static void kbd_keymap(void *data, struct wl_keyboard *k, uint32_t format,
                       int fd, uint32_t size)
{
	(void)k;
	win_t *w = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return;
	}
	struct xkb_keymap *km = xkb_keymap_new_from_string(
		w->xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	close(fd);
	if (!km)
		return;

	if (w->xkb_state) xkb_state_unref(w->xkb_state);
	if (w->keymap)    xkb_keymap_unref(w->keymap);
	w->keymap    = km;
	w->xkb_state = xkb_state_new(km);
}

/* ⚠ THIS SERIAL COUNTS TOO, and it is the only one a window gets before
 * anybody has typed into it. A program that copies something as it starts —
 * a script, a shell printing a token — would otherwise have to wait for a
 * keystroke before the clipboard could be claimed. Gaining keyboard focus is
 * a legitimate input event and the compositor accepts its serial. */
static void kbd_enter(void *d, struct wl_keyboard *k, uint32_t s,
                      struct wl_surface *surf, struct wl_array *keys)
{
	(void)k; (void)surf; (void)keys;
	win_t *w = d;
	w->last_serial = s;
	selections_flush(w);
}

static void kbd_leave(void *d, struct wl_keyboard *k, uint32_t s,
                      struct wl_surface *surf)
{ (void)d; (void)k; (void)s; (void)surf; }

/* The bytes a key that produces no text sends instead.
 *
 * These are the sequences every curses program reads. They are NOT the kitty
 * keyboard protocol — that is stage 4, it is table stakes rather than a win,
 * and it is additive to this. Returns NULL for a key that has none, which is
 * the signal to send the key's own text. */
static const char *key_sequence(xkb_keysym_t sym)
{
	switch (sym) {
	case XKB_KEY_Up:        return "\033[A";
	case XKB_KEY_Down:      return "\033[B";
	case XKB_KEY_Right:     return "\033[C";
	case XKB_KEY_Left:      return "\033[D";
	case XKB_KEY_Home:      return "\033[H";
	case XKB_KEY_End:       return "\033[F";
	case XKB_KEY_Page_Up:   return "\033[5~";
	case XKB_KEY_Page_Down: return "\033[6~";
	case XKB_KEY_Insert:    return "\033[2~";
	case XKB_KEY_Delete:    return "\033[3~";
	/* DEL and not BS: that is what `stty` reports and what readline expects,
	 * and getting it the other way round is why backspace sometimes prints
	 * ^H instead of erasing. */
	case XKB_KEY_BackSpace: return "\177";
	case XKB_KEY_Escape:    return "\033";
	default:                return NULL;
	}
}

/* ── encoding a key the way the program asked for it ────────────────────────
 *
 * Two encodings live here and the program chooses between them by pushing
 * flags (see the kitty keyboard protocol block in syntty.h).
 *
 * LEGACY is what every terminal has sent since the seventies, and it is
 * genuinely ambiguous: Ctrl+I and Tab are both 0x09, Ctrl+M and Enter are both
 * 0x0D, Ctrl+[ and Escape are both 0x1B. Nothing can report a key RELEASE, or
 * Ctrl+Shift+1, or which of two physical keys made a character.
 *
 * THE PROTOCOL encoding says exactly what happened:
 *
 *   CSI unicode-key ; modifiers : event ; text u
 *
 * Trailing empty fields are omitted, because a program reading `CSI 97 u`
 * and one reading `CSI 97 ; 1 : 1 u` must agree, and the short form is what
 * everything in the wild sends. */

/* 1 + a bitmask, which is the protocol's convention: 1 means "no modifiers",
 * so a parameter of 0 or an absent one both mean the same thing. */
static unsigned kkp_mods(struct xkb_state *st)
{
	unsigned m = 0;
	if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_SHIFT,
	                                 XKB_STATE_MODS_EFFECTIVE) > 0) m |= 1;
	if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_ALT,
	                                 XKB_STATE_MODS_EFFECTIVE) > 0) m |= 2;
	if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_CTRL,
	                                 XKB_STATE_MODS_EFFECTIVE) > 0) m |= 4;
	if (xkb_state_mod_name_is_active(st, XKB_MOD_NAME_LOGO,
	                                 XKB_STATE_MODS_EFFECTIVE) > 0) m |= 8;
	return m + 1;
}

/* The keys that keep a legacy CSI shape even under the protocol: arrows, the
 * navigation block and the function keys. Returns the numeric parameter and
 * the final byte, or 0 for "not one of these". */
static uint32_t kkp_functional(xkb_keysym_t sym, char *final)
{
	switch (sym) {
	case XKB_KEY_Up:        *final = 'A'; return 1;
	case XKB_KEY_Down:      *final = 'B'; return 1;
	case XKB_KEY_Right:     *final = 'C'; return 1;
	case XKB_KEY_Left:      *final = 'D'; return 1;
	case XKB_KEY_Home:      *final = 'H'; return 1;
	case XKB_KEY_End:       *final = 'F'; return 1;
	case XKB_KEY_Insert:    *final = '~'; return 2;
	case XKB_KEY_Delete:    *final = '~'; return 3;
	case XKB_KEY_Page_Up:   *final = '~'; return 5;
	case XKB_KEY_Page_Down: *final = '~'; return 6;
	case XKB_KEY_F1:        *final = 'P'; return 1;
	case XKB_KEY_F2:        *final = 'Q'; return 1;
	case XKB_KEY_F3:        *final = 'R'; return 1;
	case XKB_KEY_F4:        *final = 'S'; return 1;
	case XKB_KEY_F5:        *final = '~'; return 15;
	case XKB_KEY_F6:        *final = '~'; return 17;
	case XKB_KEY_F7:        *final = '~'; return 18;
	case XKB_KEY_F8:        *final = '~'; return 19;
	case XKB_KEY_F9:        *final = '~'; return 20;
	case XKB_KEY_F10:       *final = '~'; return 21;
	case XKB_KEY_F11:       *final = '~'; return 23;
	case XKB_KEY_F12:       *final = '~'; return 24;
	default:                return 0;
	}
}

/* The first codepoint of a UTF-8 run.
 *
 * The associated-text field is a CODEPOINT, not a byte — sending the first
 * byte of a multi-byte sequence would report 0xC3 for 'a' with an umlaut, and
 * the program would render whatever that is instead. xkb hands us UTF-8, so it
 * has to be decoded back. */
static uint32_t decode_utf8_first(const char *s, int n)
{
	if (n <= 0)
		return 0;
	unsigned char c = (unsigned char)s[0];
	int need;
	uint32_t cp;
	if      (c < 0x80) return c;
	else if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
	else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
	else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
	else return 0;
	if (n < need + 1)
		return 0;
	for (int i = 1; i <= need; i++) {
		unsigned char b = (unsigned char)s[i];
		if ((b & 0xC0) != 0x80)
			return 0;
		cp = cp << 6 | (b & 0x3F);
	}
	return cp;
}

/* The protocol's number for a key that is not one of the above. Escape, Enter,
 * Tab and Backspace have fixed codes; everything else is its own codepoint. */
static uint32_t kkp_code(xkb_keysym_t sym, const char *utf8, int n)
{
	switch (sym) {
	case XKB_KEY_Escape:    return 27;
	case XKB_KEY_Return:    return 13;
	case XKB_KEY_Tab:       return 9;
	case XKB_KEY_BackSpace: return 127;
	default: break;
	}
	if (sym >= 0x20 && sym < 0x7f)
		return (uint32_t)sym;
	/* A key whose keysym is not a character but which produced one — a dead
	 * key resolving, or a compose sequence finishing. */
	if (n > 0)
		return (unsigned char)utf8[0];
	return 0;
}

/* ⚠ CTRL+SHIFT+1 IS NOT THE `1` KEYSYM. With shift held, xkb resolves the key
 * to what the layout puts on that level — `exclam` on a US layout, and
 * something else again on every other. A binding written against XKB_KEY_1
 * therefore never fires, and the terminal appears to ignore the key.
 *
 * So the SHIFTED symbol is not what a Ctrl+Shift binding should look at: the
 * unshifted one is, which is level 0 of the key in the layout that is active.
 * It also makes Ctrl+Shift+C and Ctrl+Shift+c one case instead of two. */
static xkb_keysym_t base_sym(win_t *w, xkb_keycode_t code)
{
	if (!w->keymap || !w->xkb_state)
		return XKB_KEY_NoSymbol;
	xkb_layout_index_t layout = xkb_state_key_get_layout(w->xkb_state, code);
	const xkb_keysym_t *syms = NULL;
	int n = xkb_keymap_key_get_syms_by_level(w->keymap, code, layout, 0, &syms);
	return n > 0 ? syms[0] : XKB_KEY_NoSymbol;
}

static void kbd_key(void *data, struct wl_keyboard *k, uint32_t serial,
                    uint32_t time, uint32_t key, uint32_t state)
{
	(void)k; (void)time;
	win_t *w = data;
	if (!w->xkb_state)
		return;
	w->last_serial = serial;
	/* A selection the child asked for before there was a serial to claim it
	 * with. This keystroke is the first legitimate chance. */
	selections_flush(w);

	unsigned flags = st_vt_kbd_flags(w->vt);
	bool pressed  = state == WL_KEYBOARD_KEY_STATE_PRESSED;

	/* A RELEASE is only ever reported when the program asked for events. Sent
	 * unasked, it arrives at a shell as a burst of unrecognised escapes, which
	 * is how a terminal "types garbage by itself". */
	if (!pressed && !(flags & KKP_REPORT_EVENTS))
		return;

	if (pressed && !w->pending_input_ns)
		w->pending_input_ns = now_ns();

	/* +8: evdev codes and X11 keycodes differ by exactly that, forever, and
	 * every Wayland client carries this line. */
	xkb_keycode_t code = key + 8;
	char utf8[16];
	int  n = xkb_state_key_get_utf8(w->xkb_state, code, utf8, sizeof utf8);
	xkb_keysym_t sym = xkb_state_key_get_one_sym(w->xkb_state, code);

	/* ── the keys the TERMINAL keeps ────────────────────────────────────────
	 *
	 * Scrollback and prompt navigation are handled here and NOT sent to the
	 * child, because they are about the window rather than about the program.
	 * Shift is what distinguishes them: PageUp belongs to whatever is running,
	 * Shift+PageUp belongs to the terminal, which is the convention every
	 * terminal already follows.
	 *
	 * Ctrl+Shift+Up/Down jumps between PROMPTS, which is the thing owning both
	 * ends buys — the shell says where its prompts are, so this is exact
	 * rather than a heuristic over blank lines. */
	if (pressed) {
		bool shift = xkb_state_mod_name_is_active(w->xkb_state,
			XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
		bool ctl = xkb_state_mod_name_is_active(w->xkb_state,
			XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;

		/* ── tabs ───────────────────────────────────────────────────────────
		 *
		 * Ctrl+Shift again, for the same reason copy and paste use it: every
		 * plain Ctrl combination belongs to the program. `t` opens, `w` closes,
		 * left and right move, and the digits go straight to a tab.
		 *
		 * ⚠ ALL OF THEM ON THE UNSHIFTED SYMBOL — see base_sym above. */
		xkb_keysym_t bsym = base_sym(w, code);
		if (ctl && shift) {
			if (bsym == XKB_KEY_t) { tab_open(w); return; }
			if (bsym == XKB_KEY_w) { tab_close(w, w->active); return; }
			if (sym == XKB_KEY_Right || sym == XKB_KEY_Left) {
				tab_cycle(w, sym == XKB_KEY_Right ? +1 : -1);
				return;
			}
			if (bsym >= XKB_KEY_1 && bsym <= XKB_KEY_9) {
				int want = (int)(bsym - XKB_KEY_1);
				if (want < w->ntabs) {
					tab_activate(w, want);
					title_update(w);
				}
				return;
			}
		}

		/* ── copy and paste ─────────────────────────────────────────────────
		 *
		 * ⚠ CTRL+SHIFT, BECAUSE CTRL+C IS TAKEN and has been since before any
		 * of this. Ctrl+C interrupts the running program; a terminal that
		 * bound it to copy would take away the one key everybody uses to stop
		 * something. Every terminal that has this uses Ctrl+Shift for the
		 * same reason.
		 *
		 * Shift+Insert pastes the PRIMARY selection — the X11 binding for the
		 * middle button, kept for people whose hands know it. */
		if (ctl && shift && bsym == XKB_KEY_c) {
			char *text = st_sel_text(w->g);
			if (text && *text)
				own_clipboard(w, text, strlen(text));
			else
				free(text);
			return;
		}
		if (ctl && shift && bsym == XKB_KEY_v) {
			paste_start(w, false);
			return;
		}
		if (shift && sym == XKB_KEY_Insert) {
			paste_start(w, true);
			return;
		}

		if (shift && (sym == XKB_KEY_Page_Up || sym == XKB_KEY_Page_Down)) {
			int page = w->g->rows / 2;
			if (page < 1) page = 1;
			if (st_grid_view_scroll(w->g,
			        sym == XKB_KEY_Page_Up ? page : -page))
				w->dirty = true;
			return;
		}
		if (shift && ctl && (sym == XKB_KEY_Up || sym == XKB_KEY_Down)) {
			long off = st_grid_find_prompt(w->g,
			              sym == XKB_KEY_Up ? +1 : -1);
			if (off >= 0
			    && st_grid_view_scroll(w->g, (int)(off - (long)w->g->view)))
				w->dirty = true;
			return;
		}
		if (shift && sym == XKB_KEY_Home) {
			if (st_grid_view_scroll(w->g, INT_MAX / 2))
				w->dirty = true;
			return;
		}
		if (shift && sym == XKB_KEY_End) {
			if (st_grid_view_reset(w->g))
				w->dirty = true;
			return;
		}

		/* ⚠ TYPING SNAPS BACK TO LIVE. Somebody reading history who starts a
		 * command must see what they are typing — a terminal that leaves the
		 * view where it was looks like the keyboard has stopped working. */
		if (st_grid_view_reset(w->g))
			w->dirty = true;
	}

	char out[64];
	int  len = 0;

	if (flags) {
		unsigned mods = kkp_mods(w->xkb_state);
		unsigned evt  = pressed ? 1 : 3;

		char final = 0;
		uint32_t num = kkp_functional(sym, &final);
		if (!num) {
			num   = kkp_code(sym, utf8, n);
			final = 'u';
			if (!num)
				return;         /* a modifier by itself: nothing to report */
		}

		/* The short forms matter. `CSI A` is what every program expects for an
		 * unmodified Up, and one that suddenly reads `CSI 1;1A` will not
		 * recognise it — so the parameters are only spelled out once there is
		 * something to say. */
		if (mods == 1 && evt == 1) {
			if (final == 'u')
				len = snprintf(out, sizeof out, "\033[%uu", num);
			else if (final == '~')
				len = snprintf(out, sizeof out, "\033[%u~", num);
			else
				len = snprintf(out, sizeof out, "\033[%c", final);
		} else if (evt == 1) {
			len = snprintf(out, sizeof out, "\033[%u;%u%c",
			               num, mods, final == '~' ? '~' : final);
		} else {
			len = snprintf(out, sizeof out, "\033[%u;%u:%u%c",
			               num, mods, evt, final == '~' ? '~' : final);
		}

		/* The text the key produced, when it was asked for and there is any.
		 * It is a THIRD parameter, so the first two have to be spelled out
		 * even when they are defaults — `CSI 97;;97u` would be ambiguous
		 * about which field was omitted, and the protocol counts positions.
		 *
		 * Only for the `u` form: a functional key produces no text, and there
		 * is no position for it in the `~` and letter-terminated shapes. */
		if ((flags & KKP_ASSOCIATED_TEXT) && pressed && n > 0 && final == 'u') {
			uint32_t cp = decode_utf8_first(utf8, n);
			if (cp)
				len = snprintf(out, sizeof out, "\033[%u;%u:%u;%uu",
				               num, mods, evt, cp);
		}
	} else {
		/* Legacy. Ctrl+letter is a control code and xkb does not fold it. */
		bool ctrl = xkb_state_mod_name_is_active(w->xkb_state, XKB_MOD_NAME_CTRL,
		                                         XKB_STATE_MODS_EFFECTIVE) > 0;
		if (ctrl && ((sym >= 'a' && sym <= 'z') || (sym >= 'A' && sym <= 'Z'))) {
			out[0] = (char)((sym | 0x20) - 'a' + 1);
			len = 1;
		} else {
			/* A named sequence WINS over the key's own text: several of these
			 * keys do produce text and it is the wrong text — Escape yields
			 * \033 and Backspace yields \010, which is BS where every line
			 * editor wants DEL. */
			const char *seq = key_sequence(sym);
			if (seq) {
				len = (int)strlen(seq);
				memcpy(out, seq, (size_t)len);
			} else if (n > 0) {
				len = n;
				memcpy(out, utf8, (size_t)n);
			}
		}
	}

	if (len > 0)
		(void)!write(w->pty->fd, out, (size_t)len);
}

static void kbd_modifiers(void *data, struct wl_keyboard *k, uint32_t serial,
                          uint32_t dep, uint32_t lat, uint32_t lock,
                          uint32_t group)
{
	(void)k; (void)serial;
	win_t *w = data;
	if (w->xkb_state)
		xkb_state_update_mask(w->xkb_state, dep, lat, lock, 0, 0, group);
}

static void kbd_repeat(void *d, struct wl_keyboard *k, int32_t rate,
                       int32_t delay)
{ (void)d; (void)k; (void)rate; (void)delay; }

static const struct wl_keyboard_listener kbd_listener = {
	kbd_keymap, kbd_enter, kbd_leave, kbd_key, kbd_modifiers, kbd_repeat
};

/* ── the clipboard, and the other clipboard ─────────────────────────────────
 *
 * On Wayland a clipboard is not storage — it is an OFFER. The program that
 * copied keeps the bytes and hands them over, through a pipe, each time
 * somebody pastes. That has two consequences worth stating: the text has to be
 * held here for as long as we own the selection, and pasting is asynchronous —
 * a request goes out and the answer arrives on a file descriptor later.
 *
 * ⚠ WHICH IS WHY THE READ IS IN THE POLL LOOP. The obvious implementation asks
 * for the data and read()s until EOF right there, and it deadlocks the first
 * time somebody pastes text this same process owns: we would be waiting for a
 * writer that is us, on a thread that is blocked waiting. (That self-paste is
 * short-circuited below as well, because the round trip is pointless — but the
 * general case still must not block, or a slow or malicious source hangs the
 * terminal.) */

/* Offered in order of preference. The first three are what modern programs
 * publish; the last two are X11 names that survive in the wild through XWayland
 * and older toolkits, and a terminal that only understands the modern ones
 * cannot paste out of half the applications on the machine. */
static const char *const TEXT_MIMES[] = {
	"text/plain;charset=utf-8",
	"text/plain",
	"UTF8_STRING",
	"TEXT",
	"STRING",
};
#define N_TEXT_MIMES ((int)(sizeof TEXT_MIMES / sizeof TEXT_MIMES[0]))

static int mime_rank(const char *mime)
{
	for (int i = 0; i < N_TEXT_MIMES; i++)
		if (!strcmp(mime, TEXT_MIMES[i]))
			return i;
	return -1;
}

/* Hand our text to whoever asked for it.
 *
 * ⚠ WITH A DEADLINE, AND WITHOUT BLOCKING FOREVER. The receiver may take the
 * data slowly, or never; a pipe holds 64 KB and then stops accepting. A plain
 * write() of a large selection to a receiver that has gone away would hang the
 * terminal with no way out, so this waits for writability in bounded steps and
 * gives up rather than becoming unresponsive. */
static void send_text(const char *text, size_t len, int fd)
{
	size_t off = 0;
	while (off < len) {
		struct pollfd p = { .fd = fd, .events = POLLOUT };
		if (poll(&p, 1, 200) <= 0)
			break;                       /* gone, or not taking it: give up */
		ssize_t n = write(fd, text + off, len - off);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		break;
	}
	close(fd);
}

static void clip_send(void *data, struct wl_data_source *src, const char *mime,
                      int fd)
{
	(void)src; (void)mime;
	win_t *w = data;
	send_text(w->clip_text ? w->clip_text : "", w->clip_text_len, fd);
}

/* Somebody else took the clipboard. Our bytes are no longer anybody's, and
 * holding them would mean pasting stale text on the next Ctrl+Shift+V. */
static void clip_cancelled(void *data, struct wl_data_source *src)
{
	win_t *w = data;
	wl_data_source_destroy(src);
	if (w->clip_source == src)
		w->clip_source = NULL;
	free(w->clip_text);
	w->clip_text = NULL;
	w->clip_text_len = 0;
	w->clip_want = false;
}

static void clip_target(void *d, struct wl_data_source *s, const char *m)
{ (void)d; (void)s; (void)m; }
static void clip_dnd_drop(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static void clip_dnd_finished(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static void clip_action(void *d, struct wl_data_source *s, uint32_t a)
{ (void)d; (void)s; (void)a; }

static const struct wl_data_source_listener clip_source_listener = {
	.target = clip_target,
	.send = clip_send,
	.cancelled = clip_cancelled,
	.dnd_drop_performed = clip_dnd_drop,
	.dnd_finished = clip_dnd_finished,
	.action = clip_action,
};

static void prim_send(void *data, struct zwp_primary_selection_source_v1 *src,
                      const char *mime, int fd)
{
	(void)src; (void)mime;
	win_t *w = data;
	send_text(w->prim_text ? w->prim_text : "", w->prim_text_len, fd);
}

static void prim_cancelled(void *data,
                           struct zwp_primary_selection_source_v1 *src)
{
	win_t *w = data;
	zwp_primary_selection_source_v1_destroy(src);
	if (w->prim_source == src)
		w->prim_source = NULL;
	free(w->prim_text);
	w->prim_text = NULL;
	w->prim_text_len = 0;
	w->prim_want = false;
}

static const struct zwp_primary_selection_source_v1_listener prim_source_listener = {
	.send = prim_send,
	.cancelled = prim_cancelled,
};

/* ── taking a selection ─────────────────────────────────────────────────────
 *
 * ⚠ IT CANNOT BE DONE WITHOUT A SERIAL FROM A REAL INPUT EVENT. The compositor
 * uses it to check that the client claiming the clipboard was the one the
 * person was using at the time — otherwise any background window could take
 * the clipboard whenever it liked.
 *
 * Which leaves a case that is easy to drop on the floor: a child sends OSC 52
 * before anybody has touched this window — a script, or output arriving as it
 * starts. There is no serial to use, and throwing the text away means a copy
 * that silently did nothing. So the text is KEPT and the selection is claimed
 * at the next keystroke or click, which is the first moment it legitimately
 * can be. */
static void selections_flush(win_t *w)
{
	if (!w->last_serial)
		return;

	if (w->clip_want && w->dd_mgr && w->data_dev) {
		if (w->clip_source)
			wl_data_source_destroy(w->clip_source);
		w->clip_source = wl_data_device_manager_create_data_source(w->dd_mgr);
		wl_data_source_add_listener(w->clip_source, &clip_source_listener, w);
		for (int i = 0; i < N_TEXT_MIMES; i++)
			wl_data_source_offer(w->clip_source, TEXT_MIMES[i]);
		wl_data_device_set_selection(w->data_dev, w->clip_source,
		                             w->last_serial);
		w->clip_want = false;
	}

	if (w->prim_want && w->ps_mgr && w->ps_dev) {
		if (w->prim_source)
			zwp_primary_selection_source_v1_destroy(w->prim_source);
		w->prim_source =
			zwp_primary_selection_device_manager_v1_create_source(w->ps_mgr);
		zwp_primary_selection_source_v1_add_listener(w->prim_source,
		                                             &prim_source_listener, w);
		for (int i = 0; i < N_TEXT_MIMES; i++)
			zwp_primary_selection_source_v1_offer(w->prim_source,
			                                      TEXT_MIMES[i]);
		zwp_primary_selection_device_v1_set_selection(w->ps_dev,
		                                              w->prim_source,
		                                              w->last_serial);
		w->prim_want = false;
	}
}

/* Take ownership of a selection. `text` is consumed either way. */
static void own_clipboard(win_t *w, char *text, size_t len)
{
	free(w->clip_text);
	w->clip_text = text;
	w->clip_text_len = len;
	w->clip_want = true;
	selections_flush(w);
}

static void own_primary(win_t *w, char *text, size_t len)
{
	free(w->prim_text);
	w->prim_text = text;
	w->prim_text_len = len;
	w->prim_want = true;
	selections_flush(w);
}

/* ── receiving ──────────────────────────────────────────────────────────── */

/* ⚠ AN OFFER IS ANNOUNCED BEFORE ANYONE SAYS WHAT IT IS FOR, and it may never
 * become the selection at all — a drag-and-drop offer arrives the same way. So
 * there are two slots: the one just announced, and the one that is actually the
 * clipboard. Overwriting a single slot leaks the old proxy, which is exactly
 * what LeakSanitizer caught here: 96 bytes, once per selection change, on a
 * terminal that is otherwise clean. */
static void offer_mime(void *data, struct wl_data_offer *offer,
                       const char *mime)
{
	win_t *w = data;
	if (offer != w->clip_pending)
		return;
	int rank = mime_rank(mime);
	if (rank >= 0 && (w->clip_pending_mime < 0 || rank < w->clip_pending_mime))
		w->clip_pending_mime = rank;
}
static void offer_source_actions(void *d, struct wl_data_offer *o, uint32_t a)
{ (void)d; (void)o; (void)a; }
static void offer_action(void *d, struct wl_data_offer *o, uint32_t a)
{ (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {
	.offer = offer_mime,
	.source_actions = offer_source_actions,
	.action = offer_action,
};

static void prim_offer_mime(void *data,
                            struct zwp_primary_selection_offer_v1 *offer,
                            const char *mime)
{
	win_t *w = data;
	if (offer != w->prim_pending)
		return;
	int rank = mime_rank(mime);
	if (rank >= 0 && (w->prim_pending_mime < 0 || rank < w->prim_pending_mime))
		w->prim_pending_mime = rank;
}
static const struct zwp_primary_selection_offer_v1_listener prim_offer_listener = {
	.offer = prim_offer_mime,
};

/* A new offer exists. ⚠ The mime types arrive as SEPARATE EVENTS afterwards,
 * so nothing can be decided here — the listener is attached and the answer to
 * "is there any text in it?" is only complete when `selection` arrives. */
static void dd_data_offer(void *data, struct wl_data_device *dd,
                          struct wl_data_offer *offer)
{
	(void)dd;
	win_t *w = data;
	/* One that was announced and never claimed by a selection: a drag we do
	 * not take part in, or an offer the compositor superseded. */
	if (w->clip_pending)
		wl_data_offer_destroy(w->clip_pending);
	w->clip_pending = offer;
	w->clip_pending_mime = -1;
	wl_data_offer_add_listener(offer, &offer_listener, w);
}

static void dd_selection(void *data, struct wl_data_device *dd,
                         struct wl_data_offer *offer)
{
	(void)dd;
	win_t *w = data;

	/* Whatever the clipboard WAS is now over, whether or not there is a
	 * replacement — this is the only event that says so. */
	if (w->clip_offer && w->clip_offer != offer)
		wl_data_offer_destroy(w->clip_offer);
	w->clip_offer = NULL;
	w->clip_offer_mime = -1;

	if (offer && offer == w->clip_pending) {
		w->clip_offer = offer;
		w->clip_offer_mime = w->clip_pending_mime;
		w->clip_pending = NULL;
	} else if (w->clip_pending) {
		wl_data_offer_destroy(w->clip_pending);
		w->clip_pending = NULL;
	}
}

static void dd_enter(void *d, struct wl_data_device *dd, uint32_t s,
                     struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y,
                     struct wl_data_offer *o)
{ (void)d; (void)dd; (void)s; (void)surf; (void)x; (void)y; (void)o; }
static void dd_leave(void *d, struct wl_data_device *dd) { (void)d; (void)dd; }
static void dd_motion(void *d, struct wl_data_device *dd, uint32_t t,
                      wl_fixed_t x, wl_fixed_t y)
{ (void)d; (void)dd; (void)t; (void)x; (void)y; }
static void dd_drop(void *d, struct wl_data_device *dd) { (void)d; (void)dd; }

static const struct wl_data_device_listener data_device_listener = {
	.data_offer = dd_data_offer,
	.enter = dd_enter,
	.leave = dd_leave,
	.motion = dd_motion,
	.drop = dd_drop,
	.selection = dd_selection,
};

static void psd_data_offer(void *data,
                           struct zwp_primary_selection_device_v1 *dev,
                           struct zwp_primary_selection_offer_v1 *offer)
{
	(void)dev;
	win_t *w = data;
	if (w->prim_pending)
		zwp_primary_selection_offer_v1_destroy(w->prim_pending);
	w->prim_pending = offer;
	w->prim_pending_mime = -1;
	zwp_primary_selection_offer_v1_add_listener(offer, &prim_offer_listener, w);
}

static void psd_selection(void *data,
                          struct zwp_primary_selection_device_v1 *dev,
                          struct zwp_primary_selection_offer_v1 *offer)
{
	(void)dev;
	win_t *w = data;

	if (w->prim_offer && w->prim_offer != offer)
		zwp_primary_selection_offer_v1_destroy(w->prim_offer);
	w->prim_offer = NULL;
	w->prim_offer_mime = -1;

	if (offer && offer == w->prim_pending) {
		w->prim_offer = offer;
		w->prim_offer_mime = w->prim_pending_mime;
		w->prim_pending = NULL;
	} else if (w->prim_pending) {
		zwp_primary_selection_offer_v1_destroy(w->prim_pending);
		w->prim_pending = NULL;
	}
}

static const struct zwp_primary_selection_device_v1_listener ps_device_listener = {
	.data_offer = psd_data_offer,
	.selection = psd_selection,
};

/* Everything a paste ends in: the transform, then the pty. */
static void paste_finish(win_t *w, const char *text, size_t len)
{
	size_t n = 0;
	char *out = st_paste_encode(text, len, w->g->bracketed_paste, &n);
	if (n)
		(void)!write(w->pty->fd, out, n);
	free(out);
	/* Somebody pasting is somebody working at the bottom of the buffer. */
	if (st_grid_view_reset(w->g))
		w->dirty = true;
}

/* Ask for a selection, or answer it immediately when it is ours.
 *
 * ⚠ THE SHORT CIRCUIT IS NOT AN OPTIMISATION. Asking the compositor for text
 * this process is offering means we must write to a pipe and read from it in
 * the same loop, and the write side is a callback that cannot run while we are
 * blocked in the read. Answering from our own copy avoids the whole question. */
static void paste_start(win_t *w, bool primary)
{
	if (primary && w->prim_text) {
		paste_finish(w, w->prim_text, w->prim_text_len);
		return;
	}
	if (!primary && w->clip_text) {
		paste_finish(w, w->clip_text, w->clip_text_len);
		return;
	}
	if (w->paste_fd >= 0)
		return;      /* one at a time; the second press is simply ignored */

	int mime = primary ? w->prim_offer_mime : w->clip_offer_mime;
	if (mime < 0)
		return;      /* nothing offered that is text */

	int fds[2];
	if (pipe2(fds, O_CLOEXEC) != 0)
		return;

	if (primary)
		zwp_primary_selection_offer_v1_receive(w->prim_offer,
		                                       TEXT_MIMES[mime], fds[1]);
	else
		wl_data_offer_receive(w->clip_offer, TEXT_MIMES[mime], fds[1]);
	/* ⚠ OUR END OF THE WRITE SIDE MUST GO. The compositor duplicated the
	 * descriptor for the other program; if this copy stays open there is still
	 * a writer, EOF never arrives, and the paste hangs waiting for the end of
	 * something that finished. */
	close(fds[1]);
	wl_display_flush(w->dpy);

	w->paste_fd  = fds[0];
	w->paste_len = 0;
}

/* The paste's read end became readable. Called from the loop. */
static void paste_pump(win_t *w)
{
	for (;;) {
		if (w->paste_len + 4096 > w->paste_cap) {
			/* ⚠ REFUSED WHOLE RATHER THAN TRUNCATED. Half a pasted command is
			 * a command that runs and does something else; four megabytes is
			 * already far past anything anybody meant to paste into a
			 * terminal. */
			if (w->paste_cap >= 4u * 1024 * 1024) {
				w->paste_too_big++;
				goto done;
			}
			w->paste_cap = w->paste_cap ? w->paste_cap * 2 : 8192;
			w->paste_buf = xrealloc(w->paste_buf, w->paste_cap);
		}
		ssize_t n = read(w->paste_fd, w->paste_buf + w->paste_len,
		                 w->paste_cap - w->paste_len);
		if (n > 0) {
			w->paste_len += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;        /* more to come; the loop will call again */
		break;             /* 0 = the other program has finished */
	}
	paste_finish(w, w->paste_buf, w->paste_len);
done:
	close(w->paste_fd);
	w->paste_fd = -1;
	w->paste_len = 0;
}

/* What a child asked to copy, via OSC 52. Called after every feed, for the tab
 * that produced it — which is not necessarily the visible one. */
static void take_child_clipboard_of(win_t *w, st_vt_t *vt)
{
	size_t len = 0;
	uint8_t target = ST_CLIP_NONE;
	char *text = st_vt_take_clipboard(vt, &len, &target);
	if (!text)
		return;
	if (target == ST_CLIP_PRIMARY)
		own_primary(w, text, len);
	else
		own_clipboard(w, text, len);
}

/* ── the pointer ────────────────────────────────────────────────────────────
 *
 * Two jobs, and which one runs is not this program's choice:
 *
 *   THE CHILD ASKED  — a program that set ?1000, ?1002 or ?1003 wants the
 *                      pointer as escape sequences on its input. vim's visual
 *                      mode, htop's click-to-sort, less's scrolling, and every
 *                      mouse-driven TUI there is. The events go straight to the
 *                      pty and the terminal does nothing else with them.
 *
 *   NOBODY ASKED     — the pointer belongs to the terminal, and dragging with
 *                      it highlights text.
 *
 * ⚠ SHIFT TAKES IT BACK. Holding shift selects by hand even while a program is
 * reporting, and every terminal does this because otherwise there is no way to
 * copy anything out of vim without turning its mouse support off first. The
 * shift is NOT then reported to the program — sending it a modified click it
 * cannot see the consequence of is worse than sending nothing.
 *
 * ⚠ AND NONE OF THIS IS TESTED BY THE SUITE, because input is never
 * synthesised on a live session and a headless cage has no pointer. What IS
 * tested is every rule about what the bytes should be — see mouse.c and
 * `syntty mouse`. This file is the plumbing that carries them, kept as thin as
 * it can be for exactly that reason. */

static unsigned mouse_mods(const win_t *w)
{
	if (!w->xkb_state)
		return 0;
	unsigned m = 0;
	if (xkb_state_mod_name_is_active(w->xkb_state, XKB_MOD_NAME_SHIFT,
	                                 XKB_STATE_MODS_EFFECTIVE) > 0)
		m |= ST_MOUSE_SHIFT;
	if (xkb_state_mod_name_is_active(w->xkb_state, XKB_MOD_NAME_ALT,
	                                 XKB_STATE_MODS_EFFECTIVE) > 0)
		m |= ST_MOUSE_ALT;
	if (xkb_state_mod_name_is_active(w->xkb_state, XKB_MOD_NAME_CTRL,
	                                 XKB_STATE_MODS_EFFECTIVE) > 0)
		m |= ST_MOUSE_CTRL;
	return m;
}

static bool shift_held(const win_t *w)
{
	return w->xkb_state
	    && xkb_state_mod_name_is_active(w->xkb_state, XKB_MOD_NAME_SHIFT,
	                                    XKB_STATE_MODS_EFFECTIVE) > 0;
}

/* Is the child driving the pointer right now? */
static bool mouse_reporting(const win_t *w)
{
	return w->g->mouse_mode != 0 && !shift_held(w);
}

/* Surface pixels to a cell.
 *
 * ⚠ CLAMPED, not rejected. The pointer leaves the window constantly while a
 * button is held — that is what dragging past the edge IS — and the compositor
 * keeps sending motion because the press took an implicit grab. Both a
 * selection and a program's drag want the nearest cell; the alternative is a
 * highlight that stops updating the moment the pointer crosses the edge. */
static void pointer_cell(win_t *w, wl_fixed_t sx, wl_fixed_t sy)
{
	if (!w->g)
		return;
	int cw = st_font_cell_w(w->font), ch = st_font_cell_h(w->font);
	int x = wl_fixed_to_int(sx), y = wl_fixed_to_int(sy);

	w->ptr_px = x;
	w->ptr_py = y;
	/* ⚠ THE BAR IS NOT ROW 0. Every grid coordinate is measured from under it,
	 * and forgetting the offset puts every click one row above where the
	 * person pointed — which reads as the terminal selecting the wrong line. */
	int gy = y - w->bar_h;

	int col = x  < 0 ? 0 : x / cw;
	int row = gy < 0 ? 0 : gy / ch;
	if (col > w->g->cols - 1) col = w->g->cols - 1;
	if (row > w->g->rows - 1) row = w->g->rows - 1;
	w->ptr_col = col;
	w->ptr_row = row;
}

/* Is the pointer on the bar rather than on the terminal? */
static bool ptr_in_bar(const win_t *w)
{
	return w->bar_h > 0 && w->ptr_py >= 0 && w->ptr_py < w->bar_h;
}

/* Which tab was clicked, or -1. */
static int bar_tab_at(const win_t *w, int x)
{
	for (int i = 0; i < w->ntabs; i++)
		if (x >= w->tab_x0[i] && x < w->tab_x1[i])
			return i;
	return -1;
}

static void mouse_report(win_t *w, int event, int button)
{
	char out[32];
	const char *why = NULL;
	size_t n = st_mouse_encode(w->g->mouse_mode, w->g->mouse_sgr, event,
	                           button, mouse_mods(w), w->ptr_col, w->ptr_row,
	                           out, sizeof out, &why);
	if (n) {
		(void)!write(w->pty->fd, out, n);
		w->mouse_sent++;
		return;
	}
	/* ⚠ ONLY A REAL DROP IS COUNTED. "?1000 does not report motion" is the
	 * mode working; "column 240 cannot be named" is an event the program
	 * should have had and did not, and the second is the one worth a number
	 * somebody can see. */
	if (event != ST_MOUSE_MOTION && why)
		w->mouse_dropped++;
}

/* The I-beam, or the arrow while a program is driving. Re-sent only when it
 * changes: the compositor is being asked to draw, and asking it to redraw the
 * same shape on every motion event is a message per pointer step. */
static void cursor_update(win_t *w)
{
	/* Created here rather than where the pointer is bound, because the two
	 * globals arrive in whatever order the compositor lists them and the
	 * manager may not have been announced yet when the seat's capabilities
	 * came in. */
	if (!w->shape_dev && w->shape_mgr && w->ptr)
		w->shape_dev = wp_cursor_shape_manager_v1_get_pointer(w->shape_mgr,
		                                                      w->ptr);
	if (!w->shape_dev || !w->enter_serial)
		return;
	bool text = !mouse_reporting(w);
	if (text == w->cursor_is_text)
		return;
	w->cursor_is_text = text;
	wp_cursor_shape_device_v1_set_shape(
		w->shape_dev, w->enter_serial,
		text ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT
		     : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *surf, wl_fixed_t sx, wl_fixed_t sy)
{
	(void)p; (void)surf;
	win_t *w = data;
	w->enter_serial = serial;
	/* Force the request: the compositor forgot our shape when the pointer
	 * left, and whatever it is showing now belongs to another window. */
	w->cursor_is_text = !w->cursor_is_text;
	cursor_update(w);
	pointer_cell(w, sx, sy);
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *surf)
{
	(void)p; (void)serial; (void)surf;
	win_t *w = d;
	/* The highlight STAYS. Somebody selects a path and moves the pointer to
	 * another window to paste it; a selection that vanished on the way out
	 * would make copying between windows impossible. */
	w->selecting = false;
	w->buttons   = 0;
	w->held      = ST_BTN_NONE;
}

static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time,
                       wl_fixed_t sx, wl_fixed_t sy)
{
	(void)p; (void)time;
	win_t *w = data;
	int was_col = w->ptr_col, was_row = w->ptr_row;
	pointer_cell(w, sx, sy);
	cursor_update(w);

	/* ⚠ PER CELL, NOT PER PIXEL. A program is told about the grid, so two
	 * motion events inside one cell say the same thing twice — at 1000 Hz
	 * that is a thousand identical packets a second for a pointer that has not
	 * moved anywhere the program can see. */
	if (w->ptr_col == was_col && w->ptr_row == was_row)
		return;

	if (ptr_in_bar(w))
		return;
	if (mouse_reporting(w)) {
		mouse_report(w, ST_MOUSE_MOTION, w->held);
		return;
	}
	if (w->selecting) {
		st_sel_extend(w->g, w->ptr_col, w->ptr_row);
		w->dirty = true;
	}
}

static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial,
                       uint32_t time, uint32_t button, uint32_t state)
{
	(void)p;
	win_t *w = data;
	bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;

	/* ⚠ NOT `button - BTN_LEFT`. BTN_LEFT is 0x110, BTN_RIGHT is 0x111 and
	 * BTN_MIDDLE is 0x112 — evdev's order, which is not the order the mouse
	 * protocol numbers them in. Subtracting swaps middle and right in every
	 * event a program receives, so a paste lands on the context menu and the
	 * context menu on paste, and it looks like the program's bug. */
	int b;
	switch (button) {
	case BTN_LEFT:   b = ST_BTN_LEFT;   break;
	case BTN_MIDDLE: b = ST_BTN_MIDDLE; break;
	case BTN_RIGHT:  b = ST_BTN_RIGHT;  break;
	default:         return;            /* side buttons: nothing to say yet */
	}

	if (down)
		w->buttons |= 1u << b;
	else
		w->buttons &= ~(1u << b);
	w->held = w->buttons ? (w->buttons & 1 ? 0 : (w->buttons & 2 ? 1 : 2))
	                     : ST_BTN_NONE;

	if (mouse_reporting(w)) {
		mouse_report(w, down ? ST_MOUSE_PRESS : ST_MOUSE_RELEASE, b);
		return;
	}

	w->last_serial = serial;
	selections_flush(w);

	/* ⚠ A CLICK ON THE BAR IS NOT A CLICK IN THE TERMINAL. Without this it
	 * would start a selection on row 0 of whatever tab is showing, and a
	 * middle-click meant for a tab would paste into the shell. */
	if (ptr_in_bar(w)) {
		if (down && b == ST_BTN_LEFT) {
			int i = bar_tab_at(w, w->ptr_px);
			if (i >= 0 && i != w->active) {
				tab_activate(w, i);
				title_update(w);
			}
		}
		return;
	}

	if (b == ST_BTN_MIDDLE) {
		/* The habit: highlight somewhere, middle-click here. It pastes the
		 * PRIMARY selection, which is a different one from the clipboard and
		 * is filled by highlighting alone. On the press, not the release —
		 * that is where every other program does it. */
		if (down)
			paste_start(w, true);
		return;
	}
	if (b != ST_BTN_LEFT)
		return;

	if (down) {
		/* One click selects characters, two words, three lines, and a fourth
		 * starts over. 400 ms is the interval every toolkit uses. */
		bool same = w->ptr_col == w->click_col && w->ptr_row == w->click_row
		         && time - w->click_ms < 400;
		w->clicks    = same ? w->clicks + 1 : 1;
		w->click_ms  = time;
		w->click_col = w->ptr_col;
		w->click_row = w->ptr_row;

		static const int modes[3] = { ST_SEL_CHAR, ST_SEL_WORD, ST_SEL_LINE };
		st_sel_start(w->g, w->ptr_col, w->ptr_row,
		             modes[(w->clicks - 1) % 3]);
		w->selecting = true;
		w->dirty = true;
	} else if (w->selecting) {
		w->selecting = false;
		/* ⚠ THE PRIMARY, NOT THE CLIPBOARD. Highlighting fills the middle-
		 * click selection and nothing else: a terminal that put every drag on
		 * the clipboard would destroy whatever the person had copied five
		 * minutes ago, every time they highlighted a word to read it.
		 * Ctrl+Shift+C is what fills the clipboard, deliberately. */
		char *text = st_sel_text(w->g);
		if (text && *text)
			own_primary(w, text, strlen(text));
		else
			free(text);
	}
}

/* One wheel notch, once the frame that describes it has arrived. */
static void wheel(win_t *w, int axis, int notches)
{
	if (notches == 0)
		return;

	if (mouse_reporting(w)) {
		int b = axis == WL_POINTER_AXIS_VERTICAL_SCROLL
		      ? (notches > 0 ? ST_BTN_WHEEL_DOWN : ST_BTN_WHEEL_UP)
		      : (notches > 0 ? ST_BTN_WHEEL_RIGHT : ST_BTN_WHEEL_LEFT);
		int n = notches < 0 ? -notches : notches;
		/* One event per notch. A program counting notches to scroll by lines
		 * would otherwise see a single click for a fast flick of the wheel. */
		for (int i = 0; i < n; i++)
			mouse_report(w, ST_MOUSE_PRESS, b);
		return;
	}
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
		return;

	/* ⚠ ON THE ALTERNATE SCREEN THERE IS NOTHING TO SCROLL. `less`, `man` and
	 * an editor own the whole screen and keep their own text; the scrollback
	 * is not theirs and is deliberately not fed while they are running. A
	 * terminal that scrolls its own view here does nothing visible at all,
	 * which reads as a broken wheel. Arrow keys are what those programs
	 * understand, and sending them is what every terminal does. */
	if (w->g->on_alt) {
		const char *seq = notches > 0 ? "\033[B" : "\033[A";
		int n = notches < 0 ? -notches : notches;
		for (int i = 0; i < n * w->scroll_lines; i++)
			(void)!write(w->pty->fd, seq, 3);
		return;
	}

	if (st_grid_view_scroll(w->g, -notches * w->scroll_lines))
		w->dirty = true;
}

/* ⚠ THE VALUE AND THE NOTCH DESCRIBE THE SAME SCROLL. A wheel sends both — a
 * continuous value AND a discrete count — and adding them scrolls twice as far
 * as the person asked. The discrete count wins where it exists, and the value
 * is only accumulated for devices that have no notches at all (a touchpad,
 * where the scroll is a finger moving). */
static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time,
                     uint32_t axis, wl_fixed_t value)
{
	(void)p; (void)time;
	win_t *w = data;
	if (axis > 1)
		return;
	w->axis_acc[axis] += wl_fixed_to_double(value);
}

static void ptr_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis,
                              int32_t discrete)
{
	(void)p;
	win_t *w = data;
	if (axis > 1)
		return;
	w->axis_notch[axis] += discrete;
}

static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t src)
{ (void)d; (void)p; (void)src; }

static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t,
                          uint32_t axis)
{ (void)d; (void)p; (void)t; (void)axis; }

/* The end of one logical pointer event. Everything above only records; this is
 * where a scroll actually happens, because a notch and its value arrive as two
 * separate events and acting on the first one would act twice. */
static void ptr_frame(void *data, struct wl_pointer *p)
{
	(void)p;
	win_t *w = data;
	for (int a = 0; a < 2; a++) {
		int notches = w->axis_notch[a];
		if (!notches) {
			/* No discrete count: a continuous device. 15.0 is one wheel
			 * detent's worth of value, which is the unit libinput reports and
			 * therefore the honest size of "a notch" here. */
			while (w->axis_acc[a] >= 15.0)  { notches++; w->axis_acc[a] -= 15.0; }
			while (w->axis_acc[a] <= -15.0) { notches--; w->axis_acc[a] += 15.0; }
		} else {
			w->axis_acc[a] = 0;
		}
		w->axis_notch[a] = 0;
		wheel(w, a, notches);
	}
}

static const struct wl_pointer_listener ptr_listener = {
	/* Designated, unlike the listeners above it: this struct has grown twice
	 * since version 5 (axis_value120, axis_relative_direction) and positional
	 * initialisers would silently attach a handler to the wrong member the
	 * next time wayland-client adds one. */
	.enter         = ptr_enter,
	.leave         = ptr_leave,
	.motion        = ptr_motion,
	.button        = ptr_button,
	.axis          = ptr_axis,
	.frame         = ptr_frame,
	.axis_source   = ptr_axis_source,
	.axis_stop     = ptr_axis_stop,
	.axis_discrete = ptr_axis_discrete,
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
	win_t *w = data;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !w->kbd) {
		w->kbd = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(w->kbd, &kbd_listener, w);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !w->ptr) {
		w->ptr  = wl_seat_get_pointer(seat);
		w->held = ST_BTN_NONE;
		wl_pointer_add_listener(w->ptr, &ptr_listener, w);
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n)
{ (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

/* ── registry ───────────────────────────────────────────────────────────── */

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t version)
{
	win_t *w = data;
	if (!strcmp(iface, wl_compositor_interface.name)) {
		w->compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
		                                 version < 4 ? version : 4);
	} else if (!strcmp(iface, wl_shm_interface.name)) {
		w->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		w->wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface,
		                              version < 2 ? version : 2);
		xdg_wm_base_add_listener(w->wm_base, &wm_base_listener, w);
	} else if (!strcmp(iface, wp_presentation_interface.name)) {
		w->presentation = wl_registry_bind(reg, name, &wp_presentation_interface,
		                                   version < 1 ? version : 1);
		wp_presentation_add_listener(w->presentation, &presentation_listener, w);
	} else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
		w->dd_mgr = wl_registry_bind(reg, name, &wl_data_device_manager_interface,
		                             version < 3 ? version : 3);
	} else if (!strcmp(iface,
	                   zwp_primary_selection_device_manager_v1_interface.name)) {
		w->ps_mgr = wl_registry_bind(
			reg, name, &zwp_primary_selection_device_manager_v1_interface, 1);
	} else if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name)) {
		w->shape_mgr = wl_registry_bind(reg, name,
		                                &wp_cursor_shape_manager_v1_interface,
		                                version < 1 ? version : 1);
	} else if (!strcmp(iface, wl_seat_interface.name)) {
		w->seat = wl_registry_bind(reg, name, &wl_seat_interface,
		                           version < 5 ? version : 5);
		wl_seat_add_listener(w->seat, &seat_listener, w);
	}
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t name)
{ (void)d; (void)r; (void)name; }
static const struct wl_registry_listener registry_listener = {
	registry_global, registry_remove
};

/* ── the loop ───────────────────────────────────────────────────────────── */

int st_win_run(st_font_t **font, st_render_t *ren, const st_tab_spec_t *spec,
               const char *title, bool deadline,
               const st_win_conf_t *conf, st_win_stats_t *stats)
{
	const st_config_t *cfg = conf ? conf->cfg : NULL;

	win_t W = {0};
	win_t *w = &W;
	w->fontp = font;
	w->font = *font; w->ren = ren; w->spec = spec;
	w->conf = conf;
	w->inotify_fd = -1;
	snprintf(w->cur_font, sizeof w->cur_font, "%s",
	         (conf && conf->font) ? conf->font : "");
	w->cur_size = (conf && conf->size > 0) ? conf->size : 10.5;
	w->scroll_lines = (cfg && cfg->scroll_lines > 0) ? cfg->scroll_lines : 3;
	w->pool_fd = -1;
	w->deadline = deadline;
	w->held = ST_BTN_NONE;   /* 0 is the LEFT button, not "nothing held" */
	w->paste_fd = -1;        /* 0 is stdin */
	w->clip_offer_mime = w->clip_pending_mime = -1;
	w->prim_offer_mime = w->prim_pending_mime = -1;
	w->t_start = now_ns();

	w->dpy = wl_display_connect(NULL);
	if (!w->dpy) {
		fprintf(stderr, "syntty: no Wayland display "
		        "(WAYLAND_DISPLAY is %s)\n",
		        getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "unset");
		return 1;
	}

	w->registry = wl_display_get_registry(w->dpy);
	wl_registry_add_listener(w->registry, &registry_listener, w);
	wl_display_roundtrip(w->dpy);

	if (!w->compositor || !w->shm || !w->wm_base) {
		fprintf(stderr, "syntty: the compositor is missing %s\n",
		        !w->compositor ? "wl_compositor"
		        : !w->shm      ? "wl_shm" : "xdg_wm_base");
		wl_display_disconnect(w->dpy);
		return 1;
	}

	w->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	/* ⚠ THE FIRST TAB IS SPAWNED AFTER THE CONNECTION, NOT BEFORE. A child
	 * forked first would be left running with nobody reading it when the
	 * compositor turns out to be missing — the terminal would exit with its
	 * message and leave a shell attached to a pty nothing owns. */
	if (!tab_new(w)) {
		fprintf(stderr, "syntty: cannot allocate a pty\n");
		wl_display_disconnect(w->dpy);
		return 1;
	}
	tab_activate(w, 0);
	st_render_origin(w->ren, 0);

	/* More than one asked for on the command line. They are opened before the
	 * first configure, so the window is sized with the bar already in it rather
	 * than resizing itself the moment it appears. */
	for (int i = 1; i < spec->tabs && i < SYN_MAX_TABS; i++)
		if (!tab_new(w))
			break;
	tab_activate(w, 0);
	bar_update(w);

	/* The data devices need BOTH a manager and a seat, and the two arrive as
	 * separate globals in whatever order the compositor lists them — so they
	 * are created here, after the registry roundtrip, rather than in whichever
	 * handler happened to run second. */
	if (w->seat && w->dd_mgr) {
		w->data_dev = wl_data_device_manager_get_data_device(w->dd_mgr, w->seat);
		wl_data_device_add_listener(w->data_dev, &data_device_listener, w);
	}
	if (w->seat && w->ps_mgr) {
		w->ps_dev = zwp_primary_selection_device_manager_v1_get_device(
			w->ps_mgr, w->seat);
		zwp_primary_selection_device_v1_add_listener(w->ps_dev,
		                                             &ps_device_listener, w);
	}

	w->surface     = wl_compositor_create_surface(w->compositor);
	w->xdg_surface = xdg_wm_base_get_xdg_surface(w->wm_base, w->surface);
	xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener, w);
	w->toplevel = xdg_surface_get_toplevel(w->xdg_surface);
	xdg_toplevel_add_listener(w->toplevel, &toplevel_listener, w);

	xdg_toplevel_set_title(w->toplevel, title ? title : "syntty");
	/* The app_id is what the dock and the compositor key everything off —
	 * see reference_dock_pin_desktop_basename_must_equal_app_id. It must
	 * match the .desktop basename this ships with, when it ships one. */
	xdg_toplevel_set_app_id(w->toplevel, "syntty");

	wl_surface_commit(w->surface);
	wl_display_roundtrip(w->dpy);

	int wlfd = wl_display_get_fd(w->dpy);

	/* Set up AFTER the window exists, so a theme written between the config
	 * being read and the window being mapped is still caught — the first
	 * event after this fires a reload like any other. */
	watch_config(w);

	/* 256 KB, which is the read size the design calls for: a flood arrives in
	 * whatever chunks the kernel has, and a small buffer turns one megabyte
	 * into hundreds of syscalls. It also guarantees reads land mid-escape
	 * constantly, which is exactly what the parser's split-feed tests exist
	 * to prove it survives. */
	static uint8_t rbuf[256 * 1024];

	while (!w->closed) {
		/* Flush first. Anything queued — the frame request, a pong, the
		 * commit — is still in the client's buffer until this runs, and a
		 * loop that polls before flushing can wait for a reply to a message
		 * it has not sent. */
		while (wl_display_prepare_read(w->dpy) != 0)
			wl_display_dispatch_pending(w->dpy);
		wl_display_flush(w->dpy);

		/* The compositor, the paste in flight, and every tab's child.
		 *
		 * ⚠ A NEGATIVE FD IS IGNORED BY poll(), which is what keeps the paste
		 * slot in the set unconditionally instead of shuffling indices around
		 * depending on whether one is running. */
		struct pollfd fds[3 + SYN_MAX_TABS] = {
			{ .fd = wlfd,          .events = POLLIN },
			{ .fd = w->paste_fd,   .events = POLLIN },
			{ .fd = w->inotify_fd, .events = POLLIN },
		};
		int ntabs = w->ntabs;
		for (int i = 0; i < ntabs; i++) {
			fds[3 + i].fd     = w->tabs[i]->pty.fd;
			fds[3 + i].events = POLLIN;
		}
		int nfds = 3 + ntabs;
		unsigned gen = w->tab_gen;

		/* Sleep until the deadline, if one is pending — and keep taking input
		 * the whole way there, which is the entire trick. -1 means "no paint
		 * scheduled, wake only for work". */
		int timeout = -1;
		if (w->paint_due_ns) {
			uint64_t now = now_ns();
			timeout = w->paint_due_ns > now
				? (int)((w->paint_due_ns - now + 999999) / 1000000) : 0;
		}

		if (poll(fds, (nfds_t)nfds, timeout) < 0) {
			wl_display_cancel_read(w->dpy);
			if (errno == EINTR)
				continue;
			break;
		}

		if (fds[0].revents & POLLIN)
			wl_display_read_events(w->dpy);
		else
			wl_display_cancel_read(w->dpy);
		wl_display_dispatch_pending(w->dpy);

		if (fds[1].revents & (POLLIN | POLLHUP))
			paste_pump(w);

		/* ⚠ BEFORE THE TABS ARE READ, not after. A reload can change the cell
		 * size, which resizes every grid and tells every child — and doing
		 * that in the middle of a round of reads would feed bytes sized for
		 * the old geometry into grids that have just been given a new one. */
		if (fds[2].revents & POLLIN)
			config_pump(w);

		/* ── every tab, back to front ───────────────────────────────────────
		 *
		 * ⚠ BACKWARDS, because closing a tab shifts everything after it down
		 * one and the poll results are indexed by the OLD positions. Walking
		 * forwards, one child exiting would make the next iteration read the
		 * wrong tab's descriptor into the wrong tab's parser. */
		for (int i = ntabs - 1; i >= 0; i--) {
			/* ⚠ A KEYSTROKE MAY HAVE OPENED OR CLOSED A TAB while these poll
			 * results sat here — the dispatch above runs the key handlers. The
			 * indices in `fds` then name different sessions than they did, and
			 * the honest answer is to read none of them: poll is
			 * level-triggered and the next round asks again. */
			if (w->tab_gen != gen)
				break;
			if (i >= w->ntabs)
				continue;                    /* already gone this iteration */
			if (!(fds[3 + i].revents & (POLLIN | POLLHUP)))
				continue;

			tab_t *t = w->tabs[i];
			bool active = (i == w->active);

			/* ⚠ Drained in a loop, not once. Under a flood there is far more
			 * than one read's worth waiting, and going back to poll() after
			 * each one turns a megabyte into a syscall storm. The loop stops
			 * on EAGAIN, so an interactive session still returns immediately.
			 *
			 * ⚠ AND THE HANGUP ARRIVES WITH THE DATA — POLLHUP is set on the
			 * same revents that carry the child's last output, so a loop that
			 * checks for hangup first exits having thrown that output away.
			 * Read until EAGAIN, THEN believe the hangup. */
			bool eof = false;
			for (;;) {
				ssize_t n = read(t->pty.fd, rbuf, sizeof rbuf);
				if (n > 0) {
					st_vt_feed(&t->vt, rbuf, (size_t)n);
					/* Only the visible tab needs drawing; the rest are parsed
					 * into their own grids and shown when switched to. */
					if (active)
						w->dirty = true;
					continue;
				}
				if (n == 0) { eof = true; break; }
				if (errno == EINTR)  continue;
				if (errno == EAGAIN || errno == EWOULDBLOCK) break;
				eof = true;
				break;
			}

			/* Answers the child asked for while we were parsing. Written
			 * straight back, as though typed — see vt_reply: the parser holds
			 * no descriptor, so draining is the caller's job and this is the
			 * only caller that has somewhere to drain to. */
			char rep[128];
			size_t rn = st_vt_take_reply(&t->vt, rep, sizeof rep);
			if (rn)
				(void)!write(t->pty.fd, rep, rn);

			/* And anything it asked to COPY, via OSC 52 — the only way a
			 * program on the far end of an ssh session can reach the
			 * clipboard of the machine somebody is sitting at. A background
			 * tab may do it too: it is the same person's terminal, and a job
			 * that copies its result when it finishes is the reason to want
			 * this at all. */
			take_child_clipboard_of(w, &t->vt);

			if (active)
				title_update(w);

			/* ⚠ THE TAB CLOSES, THE WINDOW DOES NOT — unless it was the last
			 * one. A child exiting used to end the whole loop, which is
			 * exactly right with one tab and would have thrown away three
			 * other sessions with it. */
			if (eof)
				tab_close(w, i);
		}
		if (w->closed)
			break;

		/* The scheduled paint, now that everything readable has been read —
		 * which is what makes waiting worth anything: a keystroke that arrived
		 * during the wait is in the grid before this draws it. */
		if (w->paint_due_ns && now_ns() >= w->paint_due_ns) {
			w->paint_due_ns = 0;
			w->on_time++;
			paint(w);
			request_frame(w);
		}

		/* Paint at most once per frame callback. With one outstanding, this
		 * does nothing and the callback will do it — which is the whole of
		 * the flood throttle. */
		if (w->dirty && !w->needs_frame && !w->paint_due_ns)
			schedule_paint(w);
	}

	if (stats) {
		stats->frames       = w->frames;
		stats->skipped      = w->skipped;
		stats->first_frame_ms = w->t_first_frame
			? (double)(w->t_first_frame - w->t_start) / 1e6 : 0.0;

		stats->have_presentation = w->presentation && w->clock_ok;
		stats->discarded         = w->discarded;
		stats->commit_n   = w->commit_to_photon.n;
		stats->commit_min = w->commit_to_photon.min_ms;
		stats->commit_max = w->commit_to_photon.max_ms;
		stats->commit_avg = w->commit_to_photon.n
			? w->commit_to_photon.sum_ms / (double)w->commit_to_photon.n : 0.0;
		stats->input_n    = w->input_to_photon.n;
		stats->input_min  = w->input_to_photon.min_ms;
		stats->input_max  = w->input_to_photon.max_ms;
		stats->input_avg  = w->input_to_photon.n
			? w->input_to_photon.sum_ms / (double)w->input_to_photon.n : 0.0;

		stats->deadline_on   = w->deadline;
		stats->deadline_used = w->on_time;
		stats->deadline_late = w->late;
		stats->refresh_ms    = w->refresh_ns / 1e6;
		stats->rows_painted  = w->rows_painted;
		stats->rows_possible = w->rows_possible;
		stats->mouse_sent    = w->mouse_sent;
		stats->mouse_dropped = w->mouse_dropped;
		stats->tabs_opened   = w->tabs_opened;
		stats->grid_bytes    = w->g ? st_grid_bytes(w->g) : w->last_grid_bytes;
		stats->cols          = w->g ? w->g->cols : w->last_cols;
		stats->rows          = w->g ? w->g->rows : w->last_rows;
		stats->margin_ms     = (w->paint_cost_ns + w->paint_cost_ns / 2
		                        + 1000000ull) / 1e6;
	}

	/* ── the status, and the tabs still open ────────────────────────────────
	 *
	 * The window ends either because the last tab's child exited or because
	 * somebody closed the window; in the second case there are still sessions
	 * running, and closing the pty master is what hangs them up.
	 *
	 * ⚠ WHAT IT RETURNS IS THE FIRST TAB'S STATUS. `syntty win -- make` asked
	 * about make, and opening a second tab to read something while it built
	 * must not change the answer that lands in $?. */
	for (int i = w->ntabs - 1; i >= 0; i--) {
		tab_t *t = w->tabs[i];
		int st = st_pty_reap(&t->pty);
		if (t->first && !w->first_done) {
			w->first_status = st;
			w->first_done = true;
		}
		tab_free(t);
		w->ntabs--;
	}
	w->g = NULL; w->vt = NULL; w->pty = NULL;
	int rc = w->first_status;

	/* Frames still in flight. Their events will never arrive because we are
	 * about to disconnect, so the objects are destroyed here — see the note on
	 * feedback_ctx_t for what leaving them did to the exit status. */
	for (feedback_ctx_t *ctx = w->outstanding, *nx; ctx; ctx = nx) {
		nx = ctx->next;
		wp_presentation_feedback_destroy(ctx->fb);
		free(ctx);
	}
	w->outstanding = NULL;

	pool_destroy(w);
	if (w->xkb_state)   xkb_state_unref(w->xkb_state);
	if (w->keymap)      xkb_keymap_unref(w->keymap);
	if (w->xkb)         xkb_context_unref(w->xkb);
	/* The clipboard goes with the window. Wayland has no persistent clipboard
	 * daemon: a selection is an offer from a live process, so closing this one
	 * takes back whatever it was offering — which is why pasting from a
	 * terminal you have just closed gives nothing, in every Wayland terminal. */
	if (w->paste_fd >= 0) close(w->paste_fd);
	/* The watch, and its watches with it: closing the inotify descriptor
	 * releases every one added to it, so there is no per-watch teardown. */
	if (w->inotify_fd >= 0) close(w->inotify_fd);
	free(w->paste_buf);
	if (w->clip_source) wl_data_source_destroy(w->clip_source);
	if (w->prim_source) zwp_primary_selection_source_v1_destroy(w->prim_source);
	if (w->clip_offer)   wl_data_offer_destroy(w->clip_offer);
	if (w->clip_pending) wl_data_offer_destroy(w->clip_pending);
	if (w->prim_offer)   zwp_primary_selection_offer_v1_destroy(w->prim_offer);
	if (w->prim_pending) zwp_primary_selection_offer_v1_destroy(w->prim_pending);
	free(w->clip_text);
	free(w->prim_text);
	if (w->data_dev)    wl_data_device_destroy(w->data_dev);
	if (w->ps_dev)      zwp_primary_selection_device_v1_destroy(w->ps_dev);
	if (w->dd_mgr)      wl_data_device_manager_destroy(w->dd_mgr);
	if (w->ps_mgr)      zwp_primary_selection_device_manager_v1_destroy(w->ps_mgr);

	if (w->kbd)         wl_keyboard_destroy(w->kbd);
	if (w->shape_dev)   wp_cursor_shape_device_v1_destroy(w->shape_dev);
	if (w->shape_mgr)   wp_cursor_shape_manager_v1_destroy(w->shape_mgr);
	if (w->ptr)         wl_pointer_destroy(w->ptr);
	if (w->seat)        wl_seat_destroy(w->seat);
	if (w->frame)       wl_callback_destroy(w->frame);
	if (w->toplevel)    xdg_toplevel_destroy(w->toplevel);
	if (w->xdg_surface) xdg_surface_destroy(w->xdg_surface);
	if (w->surface)     wl_surface_destroy(w->surface);
	if (w->presentation) wp_presentation_destroy(w->presentation);
	if (w->wm_base)     xdg_wm_base_destroy(w->wm_base);
	if (w->shm)         wl_shm_destroy(w->shm);
	if (w->compositor)  wl_compositor_destroy(w->compositor);
	if (w->registry)    wl_registry_destroy(w->registry);
	wl_display_disconnect(w->dpy);
	return rc;
}

/* ── what is left ──────────────────────────────────────────────────────────
 *
 * SPLITS. Tabs are one grid visible at a time; a split is several at once, and
 * the difference is not the layout — it is that every rule in this file about
 * "the active tab" becomes a rule about a rectangle, including which one the
 * keyboard goes to, which one the pointer is over, and how damage is tracked
 * for four grids sharing one buffer. It is a bigger change than tabs were.
 *
 * AUTO-SCROLL WHILE DRAGGING. Dragging above the top of the window does not
 * pull the view back through the scrollback. The selection can already reach
 * those lines — the ends are absolute and the copy walks the ring — so what is
 * missing is only the pointer gesture, and it is a timer and a rate curve
 * nobody has measured.
 *
 * Cell-level damage WITHIN a row. A row is the unit here, so changing one
 * character repaints its whole line — 480 cells at 4K rather than one. It is
 * already enough: an interactive frame costs 0.155 ms at 4K against 10.6 ms
 * for a full repaint, and the remaining saving is 0.15 ms of a 16.7 ms budget.
 *
 * The reason to stop at rows is not laziness, it is the damage rectangles: a
 * row is one rectangle, and per-cell damage on a line with edits scattered
 * across it is a dozen, which the compositor then has to union anyway. The
 * next real win is not smaller rectangles, it is the ring arena in grid.c.
 */
