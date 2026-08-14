/*
 * lock.c — the native lock screen.
 *
 * synui is the ext-session-lock *compositor* (session.c); this is the lock
 * *itself*, drawn by the compositor instead of by a spawned swaylock. The point
 * is the look: a big clock that brightens the instant you touch a key or the
 * mouse and fades back to black after a few idle seconds — a locked screen you
 * can read at a glance, not swaylock's featureless black rectangle (which, with
 * pam_faillock behind it, is the "black screen that won't take my password"
 * that stranded the session more than once).
 *
 * It reuses `s->locked` — the same flag the ext-session-lock path sets — so
 * every input gate already written for the session lock (keyboard forwarding,
 * pointer focus, the VT-switch escape hatch that runs *before* the lock check)
 * holds here unchanged. What differs is where a key goes while locked: to
 * lock_handle_key() below, not to a client surface.
 *
 * AUTH runs in a child (synui-lock-auth), never in the compositor: pam_unix's
 * fail delay is ~2s and pam_faillock can block for minutes, and the wl_event
 * loop must not stall for either. The password goes to the child down a pipe
 * (never argv — that is world-readable in ps), the verdict comes back up
 * another pipe read off the event loop, and the child is reaped by the global
 * SIGCHLD handler like every other — so SIGCHLD is blocked across the fork, as
 * everywhere else in this tree (see ai_interface.c and
 * reference-inherited-signal-dispositions).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"
#include "contrast.h"   /* syn_ink_floor / CONTRAST_TARGET — see the palette below */

/* Panel drawn on each output, centred. Fixed-size (not the whole output) so a
 * redraw allocates a ~kB buffer, not a full framebuffer, 30 times a second
 * while it fades. */
#define LOCK_PANEL_W    720
#define LOCK_PANEL_H    360

/* Hold bright this long after the last input, then ease to black. "A few
 * seconds", as asked. */
#define LOCK_HOLD_MS    4000
#define LOCK_FADE_STEP  0.05     /* per 33 ms tick ⇒ ~0.7 s to fade out */

static uint32_t lock_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── Palette ─────────────────────────────────────────────────
 *
 * Every colour on this screen used to be a cairo literal — about twenty of
 * them, all some shade of the SYNAPSE cyan. That made the lock screen and the
 * greeter the two surfaces in the desktop that a theme switch did not reach,
 * which is backwards: they are the screens people look at longest, and the
 * first thing anyone sees at boot.
 *
 * They are also drawn over a background that is now a PHOTOGRAPH rather than
 * flat black, so a fixed ink colour is not merely off-theme, it is a legibility
 * bug — light text on a pale wallpaper. The dim/blur settings exist to keep the
 * background dark enough for the ink below, and lock_ink() takes the same
 * "position between surface and ink" approach the panels use, clamped by
 * contrast.c so the lower rungs cannot collapse on a pale background.
 *
 * See project-synui-pale-theme-legibility: a literal is not a rung.
 */

/* The accent: the theme's, or the lock's own if it has been given one. */
static void lock_accent_rgb(syn_server_t *s, double out[3])
{
    const float *a = s->config.lock_theme_follow ? s->config.panel_accent
                                                 : s->config.lock_accent;
    for (int i = 0; i < 3; i++) out[i] = a[i];
}

static void lock_set_accent(syn_server_t *s, cairo_t *cr, double alpha)
{
    double a[3];
    lock_accent_rgb(s, a);
    cairo_set_source_rgba(cr, a[0], a[1], a[2], alpha);
}

/* Ink at `level`, 0 = the background, 1 = full-strength text.
 *
 * The lock panel is drawn over the background rather than over a panel surface,
 * so the "surface" this ladder runs from is what the background actually looks
 * like after dimming — near-black by construction, since lock_bg_dim defaults
 * to darkening whatever wallpaper is behind it. Ink is therefore near-white and
 * the ladder is a straight fade, with syn_ink_floor() keeping the low rungs
 * legible if someone dials the dim down to nothing over a bright picture. */
static void lock_set_ink(syn_server_t *s, cairo_t *cr, double level, double alpha)
{
    /* The luminance actually measured off the built background, under the
     * panel. See nlock.bg_lum — the earlier version guessed this from
     * lock_bg_dim and guessed low on a pale wallpaper. */
    double bg_lum = s->nlock.bg_lum;

    const float bg[3]  = { (float)bg_lum, (float)bg_lum, (float)bg_lum };
    const float ink[3] = { 0.93f, 0.98f, 1.0f };

    double floor_lvl = syn_ink_floor(bg, ink, CONTRAST_TARGET);
    if (level > 0.0 && level < floor_lvl) level = floor_lvl;

    cairo_set_source_rgba(cr,
                          bg[0] + (ink[0] - bg[0]) * level,
                          bg[1] + (ink[1] - bg[1]) * level,
                          bg[2] + (ink[2] - bg[2]) * level, alpha);
}

/* ── Background ──────────────────────────────────────────────
 *
 * The picture behind the clock panel. Built once when the lock engages: the
 * decode is megabytes and the blur is a multi-pass filter over the whole
 * output, neither of which belongs on the 30 Hz fade tick.
 */

/* Which image this output should show behind the lock, or NULL for plain black.
 * Returns a path into the config, valid until the config changes. */
static const char *lock_bg_path(syn_server_t *s, struct wlr_output *wo)
{
    switch (s->config.lock_bg) {
    case SYN_LOCK_BG_BLACK:
        return NULL;
    case SYN_LOCK_BG_IMAGE:
        return s->config.lock_bg_image[0] ? s->config.lock_bg_image : NULL;
    case SYN_LOCK_BG_DESKTOP:
    default: {
        /* Whatever this monitor's wallpaper is — including a per-output
         * override, so a two-screen setup locks to the two pictures it was
         * already showing. */
        syn_wallpaper_src_t src;
        const char *path = NULL;
        wallpaper_effective(&s->config, wo->name, &src, &path, NULL);

        /* MATRIX has no still frame to grab, and a Workshop wallpaper is
         * another process's layer surface we cannot read at all. Both fall
         * back to black rather than to a stale or wrong picture. */
        if (src != SYN_WP_SRC_IMAGE) return NULL;
        return (path && *path) ? path : NULL;
    }
    }
}

/* Build one output's background buffer. NULL-safe throughout: every failure
 * path leaves pane[i].bg NULL, and the black backstop under it is what shows. */
static void lock_bg_build_pane(syn_server_t *s, int i)
{
    struct wlr_output *wo = s->nlock.pane[i].output;
    if (!wo) return;

    struct wlr_box box;
    wlr_output_layout_get_box(s->output_layout, wo, &box);
    if (box.width <= 0 || box.height <= 0) return;

    const char *path = lock_bg_path(s, wo);
    if (!path) return;                       /* black, by choice or by fallback */

    cairo_surface_t *img = wallpaper_decode(path);
    if (!img) return;                        /* wallpaper_decode logged it */

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(box.width, box.height, &cr);
    if (!buf) { cairo_surface_destroy(img); return; }
    cairo_begin(cr);

    /* Scale it the way the desktop scales it, so the lock shows the same
     * framing as the screen it just covered. */
    wallpaper_paint_box(cr, img, box.width, box.height, SYN_WALLPAPER_FILL);
    cairo_surface_destroy(img);

    /* Blur BEFORE dimming: blurring the dimmed image gives the same result
     * (both are linear), but dimming first means the blur runs over darker
     * pixels and any clipping happens twice. Flush so the blur sees the paint. */
    if (s->config.lock_bg_blur > 0) {
        cairo_surface_t *target = cairo_get_target(cr);
        syn_surface_blur(target, s->config.lock_bg_blur);
    }

    if (s->config.lock_bg_dim > 0) {
        cairo_set_source_rgba(cr, 0, 0, 0, s->config.lock_bg_dim / 100.0);
        cairo_paint(cr);
    }

    /* Measure what the ink will actually sit on, before the cairo_t goes.
     *
     * Only the panel's own rect matters — a wallpaper can be black at the edges
     * and white in the middle, and it is the middle the clock is drawn over.
     * Whichever pane measures LIGHTEST wins, because one ladder is shared by
     * every output and the safe choice is the one that stays legible on the
     * brightest of them. */
    {
        cairo_surface_t *tgt = cairo_get_target(cr);
        cairo_surface_flush(tgt);
        const unsigned char *px = cairo_image_surface_get_data(tgt);
        int stride = cairo_image_surface_get_stride(tgt);

        if (px) {
            int x0 = (box.width  - LOCK_PANEL_W) / 2;
            int y0 = (box.height - LOCK_PANEL_H) / 2;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            int x1 = x0 + LOCK_PANEL_W; if (x1 > box.width)  x1 = box.width;
            int y1 = y0 + LOCK_PANEL_H; if (y1 > box.height) y1 = box.height;

            /* Every 4th pixel each way: 16x fewer samples for a mean that is
             * indistinguishable at this precision. */
            double sum = 0; long n = 0;
            for (int y = y0; y < y1; y += 4) {
                const unsigned char *row = px + (size_t)y * stride;
                for (int x = x0; x < x1; x += 4) {
                    /* Premultiplied ARGB32, little-endian: B,G,R,A. The buffer
                     * is opaque here (the wallpaper was painted over the whole
                     * rect), so premultiplied equals straight. */
                    sum += syn_rel_luminance(row[x * 4 + 2] / 255.0,
                                             row[x * 4 + 1] / 255.0,
                                             row[x * 4 + 0] / 255.0);
                    n++;
                }
            }
            if (n > 0) {
                double lum = sum / n;
                if (lum > s->nlock.bg_lum) s->nlock.bg_lum = lum;
            }
        }
    }

    cairo_destroy(cr);

    set_scene_buffer(&s->nlock.pane[i].bg, s->nlock.tree, buf);
    if (s->nlock.pane[i].bg) {
        wlr_scene_node_set_position(&s->nlock.pane[i].bg->node, box.x, box.y);

        /* Above the black backstop, below every clock panel. On the first
         * build the panels do not exist yet and lock_render puts them on top
         * naturally; on a REBUILD (the Super+Z panel changing a row while the
         * screen is locked) this node is new and therefore topmost, so the
         * panels have to be lifted back over it. */
        for (int j = 0; j < s->nlock.npane; j++)
            if (s->nlock.pane[j].buf)
                wlr_scene_node_raise_to_top(&s->nlock.pane[j].buf->node);
    }
}

void lock_bg_invalidate(syn_server_t *s)
{
    if (!s->nlock.active || !s->nlock.tree) return;

    /* Recomputed from scratch below — it is a running maximum over the panes,
     * so it has to start at black or a previous, brighter wallpaper would go
     * on setting the ink ladder after it was replaced. */
    s->nlock.bg_lum = 0.0;

    for (int i = 0; i < s->nlock.npane; i++) {
        if (s->nlock.pane[i].bg) {
            wlr_scene_node_destroy(&s->nlock.pane[i].bg->node);
            s->nlock.pane[i].bg = NULL;
        }
        lock_bg_build_pane(s, i);
    }
}

/* ── Drawing ─────────────────────────────────────────────── */

/* Draw one "label: " prefix, set its colour by focus, and hand back the pen
 * advance so the caller can place the value right after it. */
static double lock_field_label(syn_server_t *s, cairo_t *cr, double x, double y,
                               const char *lab, int focused, double a)
{
    if (focused) lock_set_accent(s, cr, a);
    else         lock_set_ink(s, cr, 0.45, a);
    cairo_move_to(cr, x, y);
    syn_show_text(cr, lab);
    cairo_text_extents_t te;
    syn_text_extents(cr, lab, &te);
    return te.x_advance;
}

/* The greeter's two-field login: an editable "user:" row over the "pass:" dots,
 * with Tab moving focus between them (greetd.editing_user). Only ever drawn in
 * --greeter mode; the in-session lock keeps its single anonymous password row. */
static void lock_draw_greeter_fields(syn_server_t *s, cairo_t *cr, double cx, double a)
{
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 22);

    const double lx = cx - 170;     /* left edge of the field block */
    const double y_user = 258, y_pass = 296;
    int editing_user = s->greetd.editing_user;
    cairo_text_extents_t te;

    /* ── user row ── */
    double adv = lock_field_label(s, cr, lx, y_user, "user: ", editing_user, a);
    lock_set_ink(s, cr, editing_user ? 0.97 : 0.62, a);
    cairo_move_to(cr, lx + adv, y_user);
    syn_show_text(cr, s->greetd.user);
    if (editing_user) {              /* a caret marks the focused, editable field */
        syn_text_extents(cr, s->greetd.user, &te);
        cairo_move_to(cr, lx + adv + te.x_advance + 1, y_user);
        syn_show_text(cr, "_");
    }

    /* ── pass row ── */
    adv = lock_field_label(s, cr, lx, y_pass, "pass: ", !editing_user, a);
    int dots = s->nlock.pw_len;
    if (dots > 24) dots = 24;
    if (dots > 0) {
        double r = 4, gap = 15, dx = lx + adv + 6;
        lock_set_ink(s, cr, 0.82, a);
        for (int i = 0; i < dots; i++) {
            cairo_arc(cr, dx + i * gap, y_pass - 7, r, 0, 2 * 3.14159265);
            cairo_fill(cr);
        }
    } else if (!editing_user) {       /* focused and empty: show the caret here */
        lock_set_ink(s, cr, 0.82, a);
        cairo_move_to(cr, lx + adv, y_pass);
        syn_show_text(cr, "_");
    }

    /* ── status line ──
     *
     * A rejection stays RED rather than becoming the accent. It is the one
     * colour on this screen that is not decoration: "wrong password" has to
     * read as wrong under every theme, and a theme whose accent happens to be
     * green would otherwise print a failure in the colour of success. */
    const char *msg = NULL;
    int failed = 0;
    if (s->nlock.failed)     { msg = "Wrong password"; failed = 1; }
    else if (s->nlock.busy)  { msg = "Checking\xe2\x80\xa6"; }
    if (msg) {
        cairo_set_font_size(cr, 16);
        syn_text_extents(cr, msg, &te);
        if (failed) cairo_set_source_rgba(cr, 1.0, 0.36, 0.42, a);
        else        lock_set_accent(s, cr, a);
        cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 332);
        syn_show_text(cr, msg);
    }
}

/* One panel: the clock, the date, and the password dots — everything scaled by
 * `bright`, so at bright 0 the buffer is empty (transparent over the black
 * backstop) and the screen is dark. */
static void lock_draw_panel(syn_server_t *s, cairo_t *cr)
{
    double a = s->nlock.bright;
    if (a <= 0.004) return;      /* faded out: leave it transparent */

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    /* The bar's own 12/24-hour setting, not a guess. This was a hardcoded
     * "%-I:%M" carrying a comment claiming it matched the bar, and it had not
     * matched it since the Date and Time panel gained the toggle: choosing
     * 24-hour changed the bar and the desktop widget and nothing on the lock
     * screen — a stranded toggle of exactly the kind synui-clock exists to
     * prevent. The date stays long-form here; this is a full-screen panel,
     * not a bar, and it has room to spell the day out. */
    char hhmm[16], ampm[8], date[64];
    strftime(hhmm, sizeof(hhmm), s->clock.fmt24 ? "%H:%M" : "%-I:%M", &tm);
    /* Set explicitly rather than by strftime("") — an empty format returns 0,
     * which is also strftime's error return, and the standard calls the buffer
     * contents indeterminate on error. */
    if (s->clock.fmt24) ampm[0] = '\0';
    else strftime(ampm, sizeof(ampm), "%p", &tm);
    strftime(date, sizeof(date), "%A, %B %-d", &tm);

    double cx = LOCK_PANEL_W / 2.0;

    /* Clock — big, centred. */
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 120);
    cairo_text_extents_t te;
    syn_text_extents(cr, hhmm, &te);
    /* A soft glow in the accent, then the glyphs. Was the SYNAPSE cyan as a
     * literal; it is the theme's accent now, so a theme switch reaches the
     * lock screen like it reaches every panel. */
    lock_set_accent(s, cr, 0.18 * a);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing + 2, 150 + 2);
    syn_show_text(cr, hhmm);
    lock_set_ink(s, cr, 0.97, a);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 150);
    syn_show_text(cr, hhmm);

    /* AM/PM, small, trailing the clock — nothing at all on a 24-hour clock. */
    if (ampm[0]) {
        cairo_set_font_size(cr, 30);
        lock_set_accent(s, cr, a);
        cairo_move_to(cr, cx + te.width / 2 + 12, 150);
        syn_show_text(cr, ampm);
    }

    /* Date. */
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    syn_text_extents(cr, date, &te);
    lock_set_ink(s, cr, 0.66, a);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 205);
    syn_show_text(cr, date);

    /* The greeter draws a two-field (user + pass) block here instead of the
     * lock's single anonymous password row. */
    if (s->greeter) {
        lock_draw_greeter_fields(s, cr, cx, a);
        return;
    }

    /* Password row: dots while typing, or the error after a rejection. The row
     * is only worth drawing once there is something to say. */
    if (s->nlock.failed) {
        const char *msg = "Wrong password";
        cairo_set_font_size(cr, 18);
        syn_text_extents(cr, msg, &te);
        cairo_set_source_rgba(cr, 1.0, 0.36, 0.42, a);
        cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 275);
        syn_show_text(cr, msg);
    } else if (s->nlock.busy) {
        const char *msg = "Checking\xe2\x80\xa6";
        cairo_set_font_size(cr, 18);
        syn_text_extents(cr, msg, &te);
        lock_set_accent(s, cr, a);
        cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 275);
        syn_show_text(cr, msg);
    } else if (s->nlock.pw_len > 0) {
        /* A dot per character, capped so a held key cannot draw off-panel. */
        int dots = s->nlock.pw_len;
        if (dots > 24) dots = 24;
        double r = 5, gap = 18;
        double total = (dots - 1) * gap;
        double x = cx - total / 2;
        lock_set_ink(s, cr, 0.82, a);
        for (int i = 0; i < dots; i++) {
            cairo_arc(cr, x + i * gap, 268, r, 0, 2 * 3.14159265);
            cairo_fill(cr);
        }
    }

    /* The fingerprint reader gets its OWN row, below the password one, because
     * both are live at once — "Place your finger on the reader" has to be able
     * to sit under the dots being typed, not replace them. Empty (so nothing is
     * drawn) whenever the reader has nothing to say, which on a machine without
     * one is always. Dimmer than the password row: it is the second way in. */
    if (s->nlock.fp_msg[0]) {
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15);
        syn_text_extents(cr, s->nlock.fp_msg, &te);
        lock_set_ink(s, cr, 0.58, a);
        cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, 312);
        syn_show_text(cr, s->nlock.fp_msg);
    }
}

/* Render every pane. Panels are created lazily here so synui_lock() need only
 * record the outputs. Non-static so the greeter (greeter.c) can repaint the
 * shared panel after a rejected login. */
void lock_render(syn_server_t *s)
{
    if (!s->nlock.active || !s->nlock.tree) return;

    for (int i = 0; i < s->nlock.npane; i++) {
        struct wlr_output *o = s->nlock.pane[i].output;
        if (!o) continue;

        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, o, &box);
        if (box.width <= 0 || box.height <= 0) continue;   /* output went away */

        cairo_t *cr;
        struct wlr_buffer *buf = create_cairo_buf(LOCK_PANEL_W, LOCK_PANEL_H, &cr);
        if (!buf) continue;
        cairo_begin(cr);
        lock_draw_panel(s, cr);
        cairo_destroy(cr);

        set_scene_buffer(&s->nlock.pane[i].buf, s->nlock.tree, buf);

        int px = box.x + (box.width  - LOCK_PANEL_W) / 2;
        int py = box.y + (box.height - LOCK_PANEL_H) / 2;
        wlr_scene_node_set_position(&s->nlock.pane[i].buf->node, px, py);
    }
}

/* A monitor unplugged — or destroyed and recreated across a suspend/DPMS
 * cycle — while the session is locked leaves its pane holding a freed
 * wlr_output. lock_render() and the 1 Hz clock tick then feed that dangling
 * pointer to wlr_output_layout_get_box(), whose wlr_addon_find() dereferences
 * it and segfaults. Called from output_destroy() (synui_main.c) before the
 * wlr_output is freed, this drops the pane so nothing outlives the output.
 * The output layout has not removed the output yet, so we match by pointer. */
void lock_output_destroy(syn_output_t *o)
{
    if (!o) return;
    syn_server_t *s = o->server;
    struct wlr_output *dead = o->wlr_output;

    for (int i = 0; i < s->nlock.npane; i++) {
        if (s->nlock.pane[i].output != dead) continue;

        /* Tear down this pane's scene buffer, then null the output pointer so
         * lock_render()'s `if (!o) continue;` skips the slot from now on. The
         * slot is left in place (npane unchanged); the render loop tolerates
         * holes, and synui_unlock() resets the array wholesale anyway. */
        if (s->nlock.pane[i].buf) {
            wlr_scene_node_destroy(&s->nlock.pane[i].buf->node);
            s->nlock.pane[i].buf = NULL;
        }
        if (s->nlock.pane[i].bg) {
            wlr_scene_node_destroy(&s->nlock.pane[i].bg->node);
            s->nlock.pane[i].bg = NULL;
        }
        s->nlock.pane[i].output = NULL;
    }
}

/* Inverse of the above: an output that appears while the native lock is active
 * (suspend/resume on the NVIDIA box destroys and recreates the connector) must
 * get a pane back, or the wake shows only the backstop — a locked screen that
 * still takes the password but stays black. Slots nulled by
 * lock_output_destroy are reused before the array grows. Called from
 * server_new_output() after the output joins the layout, so lock_render()'s
 * get_box sees real geometry. */
void lock_output_create(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!s->nlock.active) return;

    int slot = -1;
    for (int i = 0; i < s->nlock.npane; i++) {
        if (s->nlock.pane[i].output == o->wlr_output) return;   /* already paned */
        if (slot < 0 && !s->nlock.pane[i].output) slot = i;
    }
    if (slot < 0) {
        if (s->nlock.npane >= (int)(sizeof(s->nlock.pane) / sizeof(s->nlock.pane[0])))
            return;                     /* out of slots: the backstop keeps it black */
        slot = s->nlock.npane++;
    }
    s->nlock.pane[slot].output = o->wlr_output;
    s->nlock.pane[slot].buf    = NULL;
    s->nlock.pane[slot].bg     = NULL;
    /* A monitor that appears mid-lock needs its background built too, or the
     * screen that comes back after a resume is the only black one. */
    lock_bg_build_pane(s, slot);
    lock_render(s);
}

/* ── Fade / clock timers ─────────────────────────────────── */

static int lock_fade_cb(void *data)
{
    syn_server_t *s = data;
    if (!s->nlock.active) return 0;

    uint32_t idle = lock_now_ms() - s->nlock.last_input_ms;
    if (idle < LOCK_HOLD_MS) {
        /* Still within the hold: nothing to do but check back when it ends. */
        if (s->nlock.t_fade)
            wl_event_source_timer_update(s->nlock.t_fade, LOCK_HOLD_MS - idle);
        return 0;
    }

    s->nlock.bright -= LOCK_FADE_STEP;
    if (s->nlock.bright <= 0.0) {
        s->nlock.bright = 0.0;
        lock_render(s);          /* one last frame: fully dark */
        return 0;                /* stop — re-armed by the next input */
    }
    lock_render(s);
    if (s->nlock.t_fade)
        wl_event_source_timer_update(s->nlock.t_fade, 33);
    return 0;
}

static int lock_clock_cb(void *data)
{
    syn_server_t *s = data;
    if (!s->nlock.active) return 0;
    if (s->nlock.bright > 0.02)      /* nothing visible ⇒ nothing to update */
        lock_render(s);
    if (s->nlock.t_clock)
        wl_event_source_timer_update(s->nlock.t_clock, 1000);
    return 0;
}

/* Defined in the fingerprint section below; called from here because a failed
 * swipe is re-armed by user activity. */
static void lock_fprint_start(syn_server_t *s);

/* Snap to full brightness and reschedule the fade. Called on every key and on
 * pointer motion while locked. */
void lock_notify_activity(syn_server_t *s)
{
    if (!s->nlock.active) return;
    s->nlock.last_input_ms = lock_now_ms();
    if (s->nlock.bright < 1.0) {
        s->nlock.bright = 1.0;
        lock_render(s);
    }
    if (s->nlock.t_fade)
        wl_event_source_timer_update(s->nlock.t_fade, LOCK_HOLD_MS);

    /* A finger that did not match leaves the reader idle. Re-arm it on
     * activity — someone walking up and moving the mouse is exactly when the
     * next swipe is coming — but never before fp_retry_ms, so a held-down key
     * cannot fork a helper per repeat. SYN_FP_UNAVAIL is not IDLE, so a machine
     * with no reader never re-forks at all. */
    if (s->nlock.fp_state == SYN_FP_IDLE &&
        (int32_t)(lock_now_ms() - s->nlock.fp_retry_ms) >= 0)
        lock_fprint_start(s);
}

/* ── Authentication ──────────────────────────────────────── */

static void lock_auth_cleanup(syn_server_t *s)
{
    if (s->nlock.auth_src) {
        wl_event_source_remove(s->nlock.auth_src);
        s->nlock.auth_src = NULL;
    }
    if (s->nlock.auth_fd >= 0) {
        close(s->nlock.auth_fd);
        s->nlock.auth_fd = -1;
    }
    s->nlock.busy = 0;
}

static int lock_auth_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;

    char c = 0;
    ssize_t n = read(fd, &c, 1);
    if (n < 0 && errno == EINTR) return 0;   /* interrupted; try again when ready */

    int ok = (n == 1 && c == '1');           /* anything else — a '0', or EOF — fails */
    lock_auth_cleanup(s);

    if (ok) {
        wlr_log(WLR_INFO, "synui: lock: authenticated");
        synui_unlock(s);
    } else {
        wlr_log(WLR_INFO, "synui: lock: authentication failed");
        s->nlock.failed = 1;
        lock_notify_activity(s);             /* wake the screen to show the error */
        lock_render(s);
    }
    return 0;
}

static void lock_auth_start(syn_server_t *s)
{
    if (s->nlock.busy || s->nlock.pw_len == 0) return;

    int rp[2], pp[2];                        /* result pipe, password pipe */
    if (pipe2(rp, O_CLOEXEC) < 0) return;
    if (pipe2(pp, O_CLOEXEC) < 0) { close(rp[0]); close(rp[1]); return; }

    /* Block SIGCHLD across the fork: the global reap_children() must not reap
     * this child before we have wired its result fd up. */
    sigset_t chld, prev;
    sigemptyset(&chld);
    sigaddset(&chld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld, &prev);

    pid_t pid = fork();
    if (pid < 0) {
        sigprocmask(SIG_SETMASK, &prev, NULL);
        close(rp[0]); close(rp[1]); close(pp[0]); close(pp[1]);
        return;
    }
    if (pid == 0) {
        /* Child: password on stdin, verdict on stdout. The O_CLOEXEC ends we
         * keep are cleared by dup2 (which drops the flag on the new fd). */
        dup2(pp[0], STDIN_FILENO);
        dup2(rp[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); if (devnull > 2) close(devnull); }
        synui_child_reset_signals();
        execlp("synui-lock-auth", "synui-lock-auth", (char *)NULL);
        _exit(127);
    }

    /* Parent. */
    close(pp[0]);
    close(rp[1]);

    /* Hand the password over, then wipe our copy — it has no reason to sit in
     * the compositor's heap a moment longer than the write. */
    ssize_t off = 0;
    while (off < s->nlock.pw_len) {
        ssize_t w = write(pp[1], s->nlock.pw + off, s->nlock.pw_len - off);
        if (w <= 0) { if (errno == EINTR) continue; break; }
        off += w;
    }
    (void)!write(pp[1], "\n", 1);
    close(pp[1]);
    explicit_bzero(s->nlock.pw, sizeof(s->nlock.pw));
    s->nlock.pw_len = 0;

    sigprocmask(SIG_SETMASK, &prev, NULL);

    s->nlock.auth_pid = pid;
    s->nlock.auth_fd  = rp[0];
    s->nlock.busy     = 1;
    s->nlock.failed   = 0;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->nlock.auth_src = wl_event_loop_add_fd(loop, rp[0], WL_EVENT_READABLE,
                                             lock_auth_readable, s);
    lock_render(s);                          /* show "Checking…" */
}

/* ── Fingerprint ─────────────────────────────────────────── */

/* The fingerprint path is the password path's opposite in shape: one helper
 * that lives for the whole lock (it sits blocked inside pam_fprintd waiting for
 * a finger) rather than one fork per attempt, talking a line protocol the whole
 * time instead of answering once. See src/synui-lock-fprint.c for the protocol
 * and for why "no reader on this machine" is the case it is built around.
 *
 * Both paths run at once and neither knows about the other: a finger and a
 * password are two ways to answer the same screen, and whichever lands first
 * calls synui_unlock().
 */

/* Wait this long after a rejected finger before re-forking. Long enough that a
 * burst of keystrokes cannot turn into a burst of forks, short enough that a
 * second swipe feels immediate. */
#define LOCK_FP_RETRY_MS   3000
/* Consecutive rejections after which the lock stops offering the reader for the
 * rest of this lock. A reader that cannot match the enrolled finger — a wet
 * fingertip, a bad enrollment — must not become an endless fork loop behind a
 * password prompt that works fine. */
#define LOCK_FP_MAX_FAILS  5

/* Drop a partial UTF-8 sequence off the end of a truncated string.
 *
 * fp_msg is filled with snprintf, which truncates by BYTES, and pam_fprintd's
 * messages are TRANSLATED — a German or Japanese "place your finger on the
 * reader" runs well past the buffer and gets cut wherever byte 127 lands, quite
 * possibly mid-codepoint. syn_show_text() on invalid UTF-8 does not skip the
 * bad glyph: it puts the cairo_t into a permanent error state, and every draw
 * call after it silently does nothing. That would take the CLOCK down, on a
 * locked screen, because a status line was one byte too long. */
static void lock_utf8_trim_partial(char *str)
{
    size_t len = strlen(str);
    size_t i = len;

    /* Step back over the trailing continuation bytes to the lead byte. */
    while (i > 0 && ((unsigned char)str[i - 1] & 0xC0) == 0x80) i--;
    if (i == 0) { str[0] = 0; return; }   /* continuations with no lead: garbage */
    i--;                                  /* i is now that lead byte */

    unsigned char c = (unsigned char)str[i];
    size_t need;
    if      ((c & 0x80) == 0x00) need = 1;
    else if ((c & 0xE0) == 0xC0) need = 2;
    else if ((c & 0xF0) == 0xE0) need = 3;
    else if ((c & 0xF8) == 0xF0) need = 4;
    else { str[i] = 0; return; }          /* not a lead byte at all */

    if (len - i < need) str[i] = 0;       /* sequence was cut short — drop it */
}

static void lock_fprint_stop(syn_server_t *s)
{
    if (s->nlock.fp_src) {
        wl_event_source_remove(s->nlock.fp_src);
        s->nlock.fp_src = NULL;
    }
    if (s->nlock.fp_fd >= 0) {
        close(s->nlock.fp_fd);
        s->nlock.fp_fd = -1;
    }
    /* Closing the pipe does NOT reach this child: it is blocked in PAM waiting
     * for a finger, not reading anything, so it never sees the EOF. It has to
     * be signalled, or an unlock leaves it holding fprintd's claim on the
     * device and the next lock finds the reader busy. Reaped by the global
     * SIGCHLD handler, like every other child in this tree. */
    if (s->nlock.fp_pid > 0) {
        kill(s->nlock.fp_pid, SIGTERM);
        s->nlock.fp_pid = 0;
    }
    s->nlock.fp_rxlen = 0;
    if (s->nlock.fp_state == SYN_FP_RUNNING)
        s->nlock.fp_state = SYN_FP_IDLE;
}

/* Give up on the reader for the rest of this lock. */
static void lock_fprint_disable(syn_server_t *s, const char *why)
{
    lock_fprint_stop(s);
    s->nlock.fp_state = SYN_FP_UNAVAIL;
    wlr_log(WLR_INFO, "synui: lock: fingerprint off for this lock (%s)", why);
}

/* Handle one protocol line. Returns 1 if it was a verdict — the caller must
 * then stop touching the fd, which this has closed (and, for 'A', stop touching
 * the lock at all: it is gone). */
static int lock_fprint_line(syn_server_t *s, const char *line)
{
    switch (line[0]) {
    case 'M':
        snprintf(s->nlock.fp_msg, sizeof(s->nlock.fp_msg), "%s", line + 1);
        lock_utf8_trim_partial(s->nlock.fp_msg);
        /* The reader asking for a finger is worth waking the screen for — a
         * prompt nobody can read because the panel faded out is no prompt. */
        lock_notify_activity(s);
        lock_render(s);
        return 0;

    case 'A':
        wlr_log(WLR_INFO, "synui: lock: authenticated (fingerprint)");
        lock_fprint_stop(s);
        synui_unlock(s);
        return 1;

    case 'U':
        /* The common case, and the quiet one: no reader, no fprintd, or no
         * enrolled prints. Clear the message — there is nothing to tell the
         * user about a feature this machine does not have. */
        s->nlock.fp_msg[0] = 0;
        lock_fprint_disable(s, "unavailable");
        lock_render(s);
        return 1;

    case 'F':
    default:
        s->nlock.fp_fails++;
        lock_fprint_stop(s);
        if (s->nlock.fp_fails >= LOCK_FP_MAX_FAILS) {
            snprintf(s->nlock.fp_msg, sizeof(s->nlock.fp_msg),
                     "Fingerprint disabled \xe2\x80\x94 use your password");
            lock_fprint_disable(s, "too many failed swipes");
        } else {
            /* Keep whatever the helper last said — "Swipe was too short" beats
             * anything written here — but never leave the row blank, or a
             * rejected finger looks like a reader that did nothing. */
            if (s->nlock.fp_msg[0] == 0)
                snprintf(s->nlock.fp_msg, sizeof(s->nlock.fp_msg),
                         "Fingerprint not recognised");
            s->nlock.fp_retry_ms = lock_now_ms() + LOCK_FP_RETRY_MS;
        }
        lock_render(s);
        return 1;
    }
}

static int lock_fprint_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        n = 0;                       /* a broken pipe reads as EOF here */
    }
    if (n == 0) {
        /* EOF with no verdict: the helper was killed, crashed, or could not be
         * exec'd at all. Counted as a failure rather than ignored, so a helper
         * that dies the moment it starts hits LOCK_FP_MAX_FAILS and stops
         * instead of being re-forked on every keystroke forever. */
        s->nlock.fp_fails++;
        lock_fprint_stop(s);
        if (s->nlock.fp_fails >= LOCK_FP_MAX_FAILS)
            lock_fprint_disable(s, "helper kept exiting without a verdict");
        else
            s->nlock.fp_retry_ms = lock_now_ms() + LOCK_FP_RETRY_MS;
        return 0;
    }

    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] != '\n') {
            /* An over-long line keeps its head and drops the rest; the newline
             * still terminates it, so the stream stays in sync. */
            if (s->nlock.fp_rxlen < (int)sizeof(s->nlock.fp_rx) - 1)
                s->nlock.fp_rx[s->nlock.fp_rxlen++] = buf[i];
            continue;
        }
        s->nlock.fp_rx[s->nlock.fp_rxlen] = 0;
        s->nlock.fp_rxlen = 0;
        if (s->nlock.fp_rx[0] && lock_fprint_line(s, s->nlock.fp_rx))
            return 0;                /* verdict: fd is closed, `s` may be unlocked */
    }
    return 0;
}

static void lock_fprint_start(syn_server_t *s)
{
    if (!s->nlock.active) return;
    if (!s->config.lock_fingerprint) return;
    if (s->nlock.fp_state != SYN_FP_IDLE || s->nlock.fp_pid > 0) return;
    /* Never in the greeter. There is no session to unlock there — greetd owns
     * authentication and starts one — and the greeter process runs as the
     * unprivileged `greeter` user, so pam_fprintd would be matching fingers
     * against THAT account rather than the one being logged into. */
    if (s->greeter) return;

    int rp[2];                               /* status pipe, child → parent */
    if (pipe2(rp, O_CLOEXEC) < 0) return;

    /* Block SIGCHLD across the fork, as everywhere else in this tree: the
     * global reap_children() must not reap this child before its fd is wired
     * up. See reference-inherited-signal-dispositions. */
    sigset_t chld, prev;
    sigemptyset(&chld);
    sigaddset(&chld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld, &prev);

    pid_t pid = fork();
    if (pid < 0) {
        sigprocmask(SIG_SETMASK, &prev, NULL);
        close(rp[0]); close(rp[1]);
        return;
    }
    if (pid == 0) {
        /* Child: status on stdout, nothing on stdin — this helper reads no
         * input at all, by design. dup2 clears the O_CLOEXEC on the new fd. */
        dup2(rp[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        /* Unlike the password helper, this child can sit blocked for the entire
         * length of the lock, and it is not reading its pipe — so if synui dies
         * it would never notice, and would keep the reader claimed until
         * fprintd timed it out. Have the kernel signal it instead. */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        synui_child_reset_signals();
        execlp("synui-lock-fprint", "synui-lock-fprint", (char *)NULL);
        _exit(127);
    }

    /* Parent. */
    close(rp[1]);
    sigprocmask(SIG_SETMASK, &prev, NULL);

    s->nlock.fp_pid   = pid;
    s->nlock.fp_fd    = rp[0];
    s->nlock.fp_state = SYN_FP_RUNNING;
    s->nlock.fp_rxlen = 0;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->nlock.fp_src = wl_event_loop_add_fd(loop, rp[0], WL_EVENT_READABLE,
                                           lock_fprint_readable, s);
}

/* ── Keyboard ────────────────────────────────────────────── */

/* Append one Unicode code point to the password as UTF-8. */
static void lock_pw_append(syn_server_t *s, uint32_t cp)
{
    char u[4];
    int n;
    if      (cp < 0x80)    { u[0] = cp; n = 1; }
    else if (cp < 0x800)   { u[0] = 0xC0 | (cp >> 6); u[1] = 0x80 | (cp & 0x3F); n = 2; }
    else if (cp < 0x10000) { u[0] = 0xE0 | (cp >> 12); u[1] = 0x80 | ((cp >> 6) & 0x3F);
                             u[2] = 0x80 | (cp & 0x3F); n = 3; }
    else                   { u[0] = 0xF0 | (cp >> 18); u[1] = 0x80 | ((cp >> 12) & 0x3F);
                             u[2] = 0x80 | ((cp >> 6) & 0x3F); u[3] = 0x80 | (cp & 0x3F); n = 4; }

    if (s->nlock.pw_len + n >= (int)sizeof(s->nlock.pw)) return;  /* full: drop it */
    memcpy(s->nlock.pw + s->nlock.pw_len, u, n);
    s->nlock.pw_len += n;
    s->nlock.pw[s->nlock.pw_len] = 0;
}

/* Append one Unicode code point to the greeter's username buffer as UTF-8. The
 * buffer is NUL-terminated (greetd wants a plain string), so its length is just
 * strlen — no separate counter to keep in sync. */
static void greeter_user_append(syn_server_t *s, uint32_t cp)
{
    char u[4];
    int n;
    if      (cp < 0x80)    { u[0] = cp; n = 1; }
    else if (cp < 0x800)   { u[0] = 0xC0 | (cp >> 6); u[1] = 0x80 | (cp & 0x3F); n = 2; }
    else if (cp < 0x10000) { u[0] = 0xE0 | (cp >> 12); u[1] = 0x80 | ((cp >> 6) & 0x3F);
                             u[2] = 0x80 | (cp & 0x3F); n = 3; }
    else                   { u[0] = 0xF0 | (cp >> 18); u[1] = 0x80 | ((cp >> 12) & 0x3F);
                             u[2] = 0x80 | ((cp >> 6) & 0x3F); u[3] = 0x80 | (cp & 0x3F); n = 4; }

    size_t len = strlen(s->greetd.user);
    if (len + n >= sizeof(s->greetd.user)) return;               /* full: drop it */
    memcpy(s->greetd.user + len, u, n);
    s->greetd.user[len + n] = 0;
}

/* Drop the last UTF-8 code point from the greeter's username buffer. */
static void greeter_user_backspace(syn_server_t *s)
{
    size_t len = strlen(s->greetd.user);
    if (len == 0) return;
    do { len--; } while (len > 0 && (s->greetd.user[len] & 0xC0) == 0x80);
    s->greetd.user[len] = 0;
}

int lock_handle_key(syn_server_t *s, xkb_keysym_t sym, uint32_t codepoint)
{
    if (!s->nlock.active) return 0;

    lock_notify_activity(s);        /* any key brightens the screen */
    if (s->nlock.busy) return 1;    /* a check is in flight: swallow, do nothing */

    /* Tab moves focus between the greeter's username and password fields. The
     * in-session lock has only the password, so it never toggles. */
    if (s->greeter && (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab)) {
        s->greetd.editing_user = !s->greetd.editing_user;
        s->nlock.failed = 0;
        lock_render(s);
        return 1;
    }
    int editing_user = s->greeter && s->greetd.editing_user;

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        /* In the greeter there is no session to unlock — hand the password to
         * greetd to start one instead. Same panel, different verb. */
        if (s->greeter)
            greeter_submit(s);
        else
            lock_auth_start(s);
        return 1;
    case XKB_KEY_BackSpace:
        if (editing_user) {
            greeter_user_backspace(s);
        } else if (s->nlock.pw_len > 0) {
            /* Step back over a whole UTF-8 code point, not one byte. */
            do { s->nlock.pw_len--; }
            while (s->nlock.pw_len > 0 &&
                   (s->nlock.pw[s->nlock.pw_len] & 0xC0) == 0x80);
            s->nlock.pw[s->nlock.pw_len] = 0;
        }
        s->nlock.failed = 0;
        lock_render(s);
        return 1;
    case XKB_KEY_Escape:
        /* Clear whichever field has focus. */
        if (editing_user) {
            s->greetd.user[0] = 0;
        } else {
            s->nlock.pw_len = 0;
            s->nlock.pw[0] = 0;
        }
        s->nlock.failed = 0;
        lock_render(s);
        return 1;
    default:
        /* A printable character types into the focused field; everything else
         * is swallowed, because while locked no key may reach anything else. */
        if (codepoint >= 0x20 && codepoint != 0x7f) {
            if (editing_user)
                greeter_user_append(s, codepoint);
            else
                lock_pw_append(s, codepoint);
            s->nlock.failed = 0;
            lock_render(s);
        }
        return 1;
    }
}

/* ── Lock / unlock ───────────────────────────────────────── */

void synui_lock(syn_server_t *s)
{
    if (s->locked) return;          /* idempotent: already locked (native or client) */

    /* clock.state has writers outside this process — syn-settings' Date & Time
     * pane — and the lock panel now renders the 12/24-hour choice rather than
     * assuming 12. Re-read on engage, not per frame: this runs once per lock,
     * where a frame callback runs at the refresh rate. */
    clock_state_load(s);

    s->locked        = 1;
    s->nlock.active  = 1;
    s->nlock.busy    = 0;
    s->nlock.failed  = 0;
    s->nlock.bright  = 1.0;
    s->nlock.pw_len  = 0;
    s->nlock.pw[0]   = 0;
    s->nlock.auth_fd = -1;
    s->nlock.last_input_ms = lock_now_ms();

    /* Fingerprint state is per-lock: an "unavailable" or a run of failed
     * swipes must not carry over into the next lock, since the reason may have
     * been a reader that was asleep or a finger that was wet. */
    s->nlock.fp_state    = SYN_FP_IDLE;
    s->nlock.fp_pid      = 0;
    s->nlock.fp_fd       = -1;
    s->nlock.fp_src      = NULL;
    s->nlock.fp_fails    = 0;
    s->nlock.fp_retry_ms = lock_now_ms();
    s->nlock.fp_msg[0]   = 0;
    s->nlock.fp_rxlen    = 0;

    /* Take keyboard focus away from every window first. */
    wlr_seat_keyboard_notify_clear_focus(s->seat);

    /* Overlay above everything: a black backstop over the whole layout (so an
     * output with no pane, or one hotplugged mid-lock, shows black, never the
     * desktop), then a clock panel per output on top of it. */
    s->nlock.tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_raise_to_top(&s->nlock.tree->node);
    float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    struct wlr_scene_rect *bg =
        wlr_scene_rect_create(s->nlock.tree, 32768, 32768, black);
    wlr_scene_node_set_position(&bg->node, -16384, -16384);

    s->nlock.npane = 0;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (s->nlock.npane >= (int)(sizeof(s->nlock.pane) / sizeof(s->nlock.pane[0])))
            break;
        s->nlock.pane[s->nlock.npane].output = o->wlr_output;
        s->nlock.pane[s->nlock.npane].buf    = NULL;
        s->nlock.pane[s->nlock.npane].bg     = NULL;
        s->nlock.npane++;
    }

    /* The wallpaper behind the panel. Built here, once, while the panes are
     * known and before lock_render draws over it — a decode plus a blur per
     * output, which is why it is not on any timer. */
    lock_bg_invalidate(s);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->nlock.t_clock = wl_event_loop_add_timer(loop, lock_clock_cb, s);
    s->nlock.t_fade  = wl_event_loop_add_timer(loop, lock_fade_cb, s);
    wl_event_source_timer_update(s->nlock.t_clock, 1000);
    wl_event_source_timer_update(s->nlock.t_fade, LOCK_HOLD_MS);

    lock_render(s);
    wlr_log(WLR_INFO, "synui: session locked (native)");
    sound_play(s, SOUND_EVT_LOCK);

    /* Offer the reader from the moment the screen locks, so a finger works
     * without touching a key first. On a machine with none this costs one fork
     * that answers 'U' straight away and is never repeated. */
    lock_fprint_start(s);
}

void synui_unlock(syn_server_t *s)
{
    if (!s->nlock.active) return;

    s->nlock.active = 0;
    s->locked = 0;
    sound_play(s, SOUND_EVT_UNLOCK);

    lock_auth_cleanup(s);
    /* Signals the helper as well as dropping its fd — it is blocked waiting for
     * a finger and would otherwise hold the reader claimed past the unlock. */
    lock_fprint_stop(s);
    s->nlock.fp_state  = SYN_FP_IDLE;
    s->nlock.fp_msg[0] = 0;
    if (s->nlock.t_clock) { wl_event_source_remove(s->nlock.t_clock); s->nlock.t_clock = NULL; }
    if (s->nlock.t_fade)  { wl_event_source_remove(s->nlock.t_fade);  s->nlock.t_fade  = NULL; }

    if (s->nlock.tree) {
        wlr_scene_node_destroy(&s->nlock.tree->node);   /* takes the panes with it */
        s->nlock.tree = NULL;
    }
    for (int i = 0; i < s->nlock.npane; i++) {
        s->nlock.pane[i].buf = NULL;
        s->nlock.pane[i].bg  = NULL;   /* destroyed with the tree above */
    }
    s->nlock.npane = 0;

    explicit_bzero(s->nlock.pw, sizeof(s->nlock.pw));
    s->nlock.pw_len = 0;

    /* Hand focus back to a window, as session.c's unlock does. */
    if (s->focused_view && s->focused_view->mapped)
        focus_view(s, s->focused_view, view_surface(s->focused_view));
    else
        wlr_seat_keyboard_notify_clear_focus(s->seat);

    /* Make sure the outputs are lit and the idle timers rearmed — the same path
     * a keypress takes to undo a DPMS blank. */
    power_notify_activity(s);
    wlr_log(WLR_INFO, "synui: session unlocked (native)");
}
