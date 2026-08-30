/*
 * lock_fp_row_test — is the fingerprint prompt on the LOGIN screen's pixels?
 *
 * ⛔ THE BUG THIS EXISTS FOR TOOK SIX RELEASES AND LOOKED LIKE DEAD HARDWARE.
 * The reader was armed, pam_fprintd prompted, greetd forwarded the words, the
 * greeter parsed them and copied them into nlock.fp_msg, and it logged
 *
 *     synui greeter: pam says [info] "Place your finger on the fingerprint reader"
 *     synui greeter: drawing it under the clock
 *
 * while the login screen showed nothing at all. lock_draw_core() returns early
 * on the greeter's two-field block — `if (s->greeter) { …; return; }` — and the
 * fingerprint row was written AFTER that return, so it only ever existed on the
 * session lock. The lock is the screen where you already know the reader works.
 * The login screen is the one where you cannot.
 *
 * Every test that watched this path was a grep, and every one of them passed:
 * greeter.c does write nlock.fp_msg, lock.c does draw nlock.fp_msg, and no
 * amount of reading either file says whether the second ever runs for the
 * first. So this rig renders the real panel through lock_render() into an image
 * surface and READS THE PIXELS in the row's band.
 *
 * ⚠ DIFFERENTIAL, NOT ABSOLUTE. Each case renders twice — once with fp_msg
 * empty, once with it set — and asserts on the pixels that APPEARED between the
 * two. That way the assertion cannot be satisfied by the password dots, the
 * status line or the layout chip drifting into the band, and it does not have
 * to be retuned every time something near it moves.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include <cairo.h>

#include "synui.h"

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* The panel-local band the row is drawn in: baseline 312 in the CORE band's
 * coordinates, which lock_draw_panel translates down by the header. 15px
 * monospace, so the glyphs live a little above the baseline and a little below.
 * Deliberately NOT the whole panel — a row drawn in the wrong place is as
 * broken as one not drawn at all.
 *
 * ⚠ THE TOP EDGE IS WHERE IT IS FOR A REASON. The greeter's empty `pass:` field
 * draws a caret underscore whose ink lands two rows above this band, so a
 * band opened any wider reports "something is drawn here" on a screen with no
 * prompt at all — which is the assertion at the bottom of this file. */
#define FP_BASELINE  (LOCK_HEAD_H_T + 320)   /* lock.c's LOCK_FP_Y */
#define FP_TOP       (FP_BASELINE - 11)   /* clears the greeter caret at 358 */
#define FP_BOT       (FP_BASELINE + 5)

/* lock.c's, private to it, spelt out so a change to either has to be made in
 * both places deliberately. */
#define LOCK_HEAD_H_T   60
#define LOCK_CORE_H_T   400
#define LOCK_FOOT_H_T   120
#define LOCK_PANEL_W_T  720
#define LOCK_PANEL_H_T  (LOCK_HEAD_H_T + LOCK_CORE_H_T + LOCK_FOOT_H_T)

/* ── The compositor, stubbed ─────────────────────────────── */

/* A REAL cairo context: the whole question here is what reached a pixel, so
 * create_cairo_buf() returning NULL (which is what the cheaper rigs do) would
 * make lock_render() skip the drawing entirely and every case below pass. */
static cairo_surface_t *surface;
static struct wlr_buffer fake_buffer;

struct wlr_buffer *create_cairo_buf(int w, int h, cairo_t **cr_out)
{
    if (surface) cairo_surface_destroy(surface);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    *cr_out = cairo_create(surface);
    return &fake_buffer;
}

void cairo_begin(cairo_t *cr) { (void)cr; }

static struct wlr_scene_buffer fake_scene_buffer;
void set_scene_buffer(struct wlr_scene_buffer **node,
                      struct wlr_scene_tree *parent, struct wlr_buffer *buf)
{ (void)parent; (void)buf; *node = &fake_scene_buffer; }

static struct wlr_output fake_output;
void wlr_output_layout_get_box(struct wlr_output_layout *layout,
                               struct wlr_output *reference,
                               struct wlr_box *dest_box)
{
    (void)layout; (void)reference;
    dest_box->x = 0; dest_box->y = 0;
    dest_box->width = 1920; dest_box->height = 1080;
}

/* Real text, real metrics — the row is CENTRED on what it measures, so a stub
 * that measured zero would put every string in the same place and could not
 * tell a drawn row from an undrawn one. */
void syn_show_text(cairo_t *cr, const char *text) { cairo_show_text(cr, text); }
void syn_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext)
{ cairo_text_extents(cr, text, ext); }

/* ── Everything the panel can ask about and this rig has nothing to say to ── */

bool weather_current(syn_weather_now_t *out) { (void)out; return false; }
void weather_refresh(syn_server_t *s, bool force) { (void)s; (void)force; }
void weather_draw_icon(cairo_t *cr, syn_weather_icon_t icon,
                       double x, double y, double size)
{ (void)cr; (void)icon; (void)x; (void)y; (void)size; }

bool mpris_now_playing(syn_mpris_now_t *out) { (void)out; return false; }
void mpris_playpause(void) {}
void mpris_next(void) {}
void mpris_previous(void) {}

/* ONE layout, which is what AUTO shows no chip for — the chip is not what is
 * under test and a second layout would put one in the band's neighbourhood. */
int  kbd_layout_count(syn_server_t *s) { (void)s; return 1; }
int  kbd_layout_active(syn_server_t *s) { (void)s; return 0; }
void kbd_layout_label(syn_server_t *s, int idx, char *buf, size_t n)
{ (void)s; (void)idx; snprintf(buf, n, "us"); }
void kbd_layout_cycle(syn_server_t *s, int dir) { (void)s; (void)dir; }

void clock_state_load(syn_server_t *s) { (void)s; }
void sound_play(syn_server_t *s, syn_sound_event_t evt) { (void)s; (void)evt; }
void power_notify_activity(syn_server_t *s) { (void)s; }
void greeter_submit(syn_server_t *s) { (void)s; }
void greeter_notify_activity(syn_server_t *s) { (void)s; }
void focus_view(syn_server_t *s, syn_view_t *view, struct wlr_surface *surface_)
{ (void)s; (void)view; (void)surface_; }
struct wlr_surface *view_surface(syn_view_t *v) { (void)v; return NULL; }
syn_output_t *server_focused_output(syn_server_t *s) { (void)s; return NULL; }
syn_output_t *server_primary_output(syn_server_t *s) { (void)s; return NULL; }
void synui_child_reset_signals(void) {}

cairo_surface_t *wallpaper_decode(const char *path) { (void)path; return NULL; }
void wallpaper_effective(syn_config_t *cfg, const char *name,
                         syn_wallpaper_src_t *src, const char **path,
                         syn_wallpaper_mode_t *mode)
{ (void)cfg; (void)name; (void)src; (void)path; (void)mode; }
void wallpaper_paint_box(cairo_t *cr, cairo_surface_t *src,
                         int dst_w, int dst_h, syn_wallpaper_mode_t mode)
{ (void)cr; (void)src; (void)dst_w; (void)dst_h; (void)mode; }
void syn_surface_blur(cairo_surface_t *surf, int radius)
{ (void)surf; (void)radius; }

/* ── The scene and the event loop, stubbed ───────────────── */

static struct wlr_scene_tree fake_tree;
struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_tree *parent)
{ (void)parent; return &fake_tree; }
static struct wlr_scene_rect fake_rect;
struct wlr_scene_rect *wlr_scene_rect_create(struct wlr_scene_tree *parent,
                                             int width, int height,
                                             const float color[static 4])
{ (void)parent; (void)width; (void)height; (void)color; return &fake_rect; }
void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y)
{ node->x = x; node->y = y; }
void wlr_scene_node_raise_to_top(struct wlr_scene_node *node) { (void)node; }
void wlr_scene_node_destroy(struct wlr_scene_node *node) { (void)node; }
void wlr_seat_keyboard_notify_clear_focus(struct wlr_seat *seat) { (void)seat; }

void _wlr_log(enum wlr_log_importance v, const char *fmt, ...)
{ (void)v; (void)fmt; }

struct wl_event_loop *wl_display_get_event_loop(struct wl_display *display)
{ (void)display; return NULL; }
struct wl_event_source *wl_event_loop_add_fd(struct wl_event_loop *loop, int fd,
                                             uint32_t mask,
                                             wl_event_loop_fd_func_t func,
                                             void *data)
{ (void)loop; (void)fd; (void)mask; (void)func; (void)data; return NULL; }
struct wl_event_source *wl_event_loop_add_timer(struct wl_event_loop *loop,
                                                wl_event_loop_timer_func_t func,
                                                void *data)
{ (void)loop; (void)func; (void)data; return NULL; }
int wl_event_source_remove(struct wl_event_source *source)
{ (void)source; return 0; }
int wl_event_source_timer_update(struct wl_event_source *source, int ms_delay)
{ (void)source; (void)ms_delay; return 0; }

/* ── The rig ─────────────────────────────────────────────── */

static syn_server_t server;

/* Enough of a locked compositor for lock_render() to paint one pane. */
static void rig_reset(int greeter)
{
    memset(&server, 0, sizeof(server));
    server.greeter        = greeter;
    server.nlock.active   = 1;
    server.nlock.tree     = &fake_tree;
    server.nlock.npane    = 1;
    server.nlock.pane[0].output = &fake_output;
    server.nlock.bright   = 1.0;
    server.nlock.bg_lum   = 0.10;      /* a dark background, as the lock builds */
    snprintf(server.greetd.user, sizeof(server.greetd.user), "velle");
}

/* Ink in the fingerprint band, counted off the rendered surface. */
static int band_ink(void)
{
    cairo_surface_flush(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    int n = 0;
    for (int y = FP_TOP; y <= FP_BOT; y++) {
        const uint32_t *row = (const uint32_t *)(data + (size_t)y * stride);
        for (int x = 0; x < LOCK_PANEL_W_T; x++)
            if ((row[x] >> 24) != 0) n++;
    }
    return n;
}

/* The band's total ink WEIGHT, not its pixel count. The mark breathes by
 * changing colour at a fixed alpha, so its coverage never varies — a counting
 * probe cannot see the pulse at all, and the first version of this file could
 * have watched a frozen mark forever. */
static long band_weight(void)
{
    cairo_surface_flush(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    long sum = 0;
    for (int y = FP_TOP; y <= FP_BOT; y++) {
        const uint32_t *row = (const uint32_t *)(data + (size_t)y * stride);
        for (int x = 0; x < LOCK_PANEL_W_T; x++) {
            uint32_t p = row[x];
            sum += ((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff);
        }
    }
    return sum;
}

static void nap_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Render once with no prompt and once with one, and answer how much ink the
 * prompt ADDED to the band. */
static int ink_added_by_prompt(int greeter)
{
    rig_reset(greeter);
    server.nlock.fp_msg[0] = 0;
    lock_render(&server);
    int before = band_ink();

    snprintf(server.nlock.fp_msg, sizeof(server.nlock.fp_msg),
             "Place your finger on the fingerprint reader");
    lock_render(&server);
    int after = band_ink();

    return after - before;
}

int main(void)
{
    printf("lock fingerprint row\n");

    /* ⛔ THE CASE THAT WAS BROKEN, AND THE ONE THAT MATTERS. The login screen
     * is the only place somebody has no other way to know the reader is live. */
    int greeter_ink = ink_added_by_prompt(1);
    check(greeter_ink > 0,
          "the LOGIN screen draws the fingerprint prompt");
    if (greeter_ink <= 0)
        printf("        (nothing appeared between %d and %d — lock_draw_core's "
               "early return on s->greeter is skipping the row again)\n",
               FP_TOP, FP_BOT);

    /* The lock screen never lost it, and must not lose it to the fix. */
    check(ink_added_by_prompt(0) > 0,
          "…and so does the session lock, as it always did");

    /* An empty message is not a blank row drawn in the accent — it is no row,
     * on a machine with no reader, which is most of them. */
    rig_reset(1);
    server.nlock.fp_msg[0] = 0;
    lock_render(&server);
    int quiet_greeter = band_ink();
    check(quiet_greeter == 0 && band_ink() == 0,
          "a reader with nothing to say takes no pixels on either screen");

    /* ── The part somebody standing at the screen actually asked for ────────
     *
     * ⛔ A VERIFY TAKES SECONDS AND NOTHING REPORTS A FINGER LANDING. fprintd
     * signals VerifyStatus(result, done) when a scan RESOLVES and has no
     * touch-down event, so pam_fprintd says one thing and then goes quiet. The
     * mark is the whole of the feedback: it says the reader is listening NOW,
     * which is the only true thing available. */
    rig_reset(1);
    snprintf(server.nlock.fp_msg, sizeof(server.nlock.fp_msg),
             "Place your finger on the fingerprint reader");
    lock_render(&server);
    int text_only = band_ink();

    server.greetd.fp_live = 1;              /* PAM is waiting for a finger */
    lock_render(&server);
    check(band_ink() > text_only,
          "a live reader draws a mark beside the prompt");

    /* ⛔ AND IT HAS TO MOVE. A mark that is drawn once and never repainted is
     * the static line of text this row is replacing, with a nicer glyph. */
    long lo = -1, hi = -1;
    for (int i = 0; i < 6; i++) {           /* ~1s: over half a breath */
        lock_render(&server);
        long w = band_weight();
        if (lo < 0 || w < lo) lo = w;
        if (hi < 0 || w > hi) hi = w;
        nap_ms(170);
    }
    check(hi - lo > 200, "…and it breathes, so the screen is visibly alive");
    if (hi - lo <= 200)
        printf("        (weight held between %ld and %ld across ~1s — the mark "
               "is frozen)\n", lo, hi);

    /* ⛔ AND IT STOPS WHEN THE WINDOW DOES. Between arm cycles, and past the
     * arm cap, nothing is listening — a mark still pulsing there invites a
     * finger the reader will not answer, which is the same lie with better
     * graphics. */
    server.greetd.fp_live = 0;
    lock_render(&server);
    check(band_ink() == text_only,
          "…and it is gone the moment PAM stops waiting");

    if (surface) { cairo_surface_destroy(surface); surface = NULL; }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
