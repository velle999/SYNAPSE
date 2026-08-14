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
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-client-protocol.h"
#include "presentation-time-client-protocol.h"

#define NBUFFERS 2

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

	/* The terminal. */
	st_grid_t   *g;
	st_vt_t     *vt;
	st_pty_t    *pty;
	st_font_t   *font;
	st_render_t *ren;

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

/* ── painting ───────────────────────────────────────────────────────────── */

static void frame_done(void *data, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_listener = { frame_done };

static void paint(win_t *w)
{
	if (!w->configured || w->win_w <= 0 || w->win_h <= 0)
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
	st_render_grid(w->ren, w->g, b->px, b->w, b->w, b->h);
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
	/* Whole-surface damage. Cell-level damage is the next thing worth doing
	 * and it is NOT free to get right — see the note at the bottom of this
	 * file. At 0.38 ms for a full 80x24 repaint there is no pressure yet, and
	 * a damage rectangle that is subtly wrong leaves stale pixels on screen,
	 * which is far worse than repainting. */
	wl_surface_damage_buffer(w->surface, 0, 0, b->w, b->h);
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
	int cw = st_font_cell_w(w->font), ch = st_font_cell_h(w->font);
	int cols = w->win_w / cw, rows = w->win_h / ch;
	if (cols < 1) cols = 1;
	if (rows < 1) rows = 1;

	if (cols == w->g->cols && rows == w->g->rows)
		return;

	st_grid_resize(w->g, (uint16_t)cols, (uint16_t)rows);
	/* The CHILD has to be told too, or every full-screen program on the
	 * other end keeps drawing to the old size. This is the half people
	 * forget, because the terminal itself looks correct. */
	if (w->pty)
		st_pty_resize(w->pty, (uint16_t)cols, (uint16_t)rows);
	w->dirty = true;
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
		w->win_h = st_render_height(w->ren, w->g);
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

static void kbd_enter(void *d, struct wl_keyboard *k, uint32_t s,
                      struct wl_surface *surf, struct wl_array *keys)
{ (void)d; (void)k; (void)s; (void)surf; (void)keys; }

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

static void kbd_key(void *data, struct wl_keyboard *k, uint32_t serial,
                    uint32_t time, uint32_t key, uint32_t state)
{
	(void)k; (void)serial; (void)time;
	win_t *w = data;
	if (!w->xkb_state)
		return;

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

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
	win_t *w = data;
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !w->kbd) {
		w->kbd = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(w->kbd, &kbd_listener, w);
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

int st_win_run(st_grid_t *g, st_vt_t *vt, st_pty_t *pty, st_font_t *font,
               st_render_t *ren, const char *title, bool deadline,
               st_win_stats_t *stats)
{
	win_t W = {0};
	win_t *w = &W;
	w->g = g; w->vt = vt; w->pty = pty; w->font = font; w->ren = ren;
	w->pool_fd = -1;
	w->deadline = deadline;
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

		struct pollfd fds[2] = {
			{ .fd = wlfd,      .events = POLLIN },
			{ .fd = pty->fd,   .events = POLLIN },
		};

		/* Sleep until the deadline, if one is pending — and keep taking input
		 * the whole way there, which is the entire trick. -1 means "no paint
		 * scheduled, wake only for work". */
		int timeout = -1;
		if (w->paint_due_ns) {
			uint64_t now = now_ns();
			timeout = w->paint_due_ns > now
				? (int)((w->paint_due_ns - now + 999999) / 1000000) : 0;
		}

		if (poll(fds, 2, timeout) < 0) {
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

		if (fds[1].revents & (POLLIN | POLLHUP)) {
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
				ssize_t n = read(pty->fd, rbuf, sizeof rbuf);
				if (n > 0) {
					st_vt_feed(vt, rbuf, (size_t)n);
					w->dirty = true;
					continue;
				}
				if (n == 0) { eof = true; break; }
				if (errno == EINTR)  continue;
				if (errno == EAGAIN || errno == EWOULDBLOCK) break;
				eof = true;
				break;
			}
			if (eof)
				break;

			/* Answers the child asked for while we were parsing. Written
			 * straight back, as though typed — see vt_reply: the parser holds
			 * no descriptor, so draining is the caller's job and this is the
			 * only caller that has somewhere to drain to. */
			char rep[128];
			size_t rn = st_vt_take_reply(vt, rep, sizeof rep);
			if (rn)
				(void)!write(pty->fd, rep, rn);
		}

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
		stats->margin_ms     = (w->paint_cost_ns + w->paint_cost_ns / 2
		                        + 1000000ull) / 1e6;
	}

	/* The child's status is what this returns. The loop above ends either
	 * because the pty hung up (the child is gone) or because the window was
	 * closed (it is not, and closing the master is what tells it). Both go
	 * through the same reap. */
	int rc = st_pty_reap(pty);

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
	if (w->kbd)         wl_keyboard_destroy(w->kbd);
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

/* ── the next thing, recorded rather than done ──────────────────────────────
 *
 * CELL-LEVEL DAMAGE. Every commit above damages the whole surface, so the
 * compositor re-uploads the entire window for a one-character change. The fix
 * is a per-row dirty flag in the grid — every mutation there already goes
 * through one of about fifteen functions, so there is exactly one place to set
 * it — turned into damage rectangles here.
 *
 * It is NOT done yet, and the reason is a measurement rather than a shrug: a
 * full 80x24 repaint costs 0.38 ms, which is 2% of a frame. Damage tracking
 * would take that to nearly nothing and save the compositor an upload, which
 * matters most on a large window; but a damage rectangle that is subtly too
 * small leaves stale pixels on screen and looks like memory corruption. It is
 * worth doing with a test that renders the same screen twice — once fully,
 * once by damage — and compares the buffers byte for byte. That test is the
 * hard part, and it is the part worth having.
 */
