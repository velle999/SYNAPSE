/*
 * saver.c — the screensaver: the thing that actually draws.
 *
 * Not to be confused with screensaver.c, which owns the org.freedesktop.ScreenSaver
 * D-Bus name so that Firefox, mpv and Steam can *inhibit* idle. That file is the
 * reason this one can be simple: by the time a stage fires here, everything that
 * wanted to stop it has already had its say, through either that bus name or the
 * Wayland idle-inhibit protocol. The two meet at idle_inhibited() and nowhere else.
 *
 * WHY A SAVER AT ALL, given that DPMS already blanks the panel. Two reasons, and
 * neither is nostalgia:
 *
 *   - Blanking is all-or-nothing and takes seconds to come back on some monitors.
 *     A saver is the state between "working" and "asleep" — the screen still says
 *     what time it is, and a mouse twitch is instant.
 *   - The blank stage is where the *lock* wants to be, and a lock with no visual
 *     is indistinguishable from a dead machine. That was the swaylock complaint
 *     that started the native lock (see lock.c).
 *
 * The saver is a stage in power.c's idle machine like dim/blank/lock/suspend, and
 * is armed by the same code. It differs in that it draws: a full-screen scene
 * buffer per output, repainted on a timer while it is up and torn down completely
 * when it is not. Nothing here costs anything while the saver is off — no scene
 * nodes, no timer, no decoded image.
 *
 * MODES. Three of the four that draw are pure cairo (clock, starfield, slideshow)
 * and work on llvmpipe, in a VM, and on the greeter's software path. The fourth
 * (matrix) is the existing GLES2 rain from matrix.c and fails closed to the clock
 * on anything without GLES2, exactly as the wallpaper does — a screensaver that
 * shows a black screen because the GPU said no is a screensaver that reads as
 * broken.
 *
 * DISMISSAL is the part worth being careful about. Any real input takes it down,
 * and the input that dismisses it is SWALLOWED — you do not want the keystroke
 * that woke the screen to also land in whatever was focused underneath, which is
 * how a saver dismissal ends up typing into a terminal. power.c calls
 * saver_dismiss(s, true) from the activity path and input.c checks
 * saver_active() before forwarding.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Repaint interval. 33 ms is the same tick the lock's fade runs at; the
 * starfield and the slideshow crossfade both look smooth at it, and it is cheap
 * enough that a saver left up all night is not a space heater. */
#define SAVER_TICK_MS 33

/* Slideshow bounds. Below the floor the crossfade never finishes before the
 * next image starts; the ceiling is just a sane end for the panel's ladder. */
#define SAVER_INTERVAL_MIN 5
#define SAVER_INTERVAL_MAX 600
#define SAVER_FADE_MS      1200

/* How many images a slideshow will collect. A wallpapers directory with more
 * than this is not a slideshow, it is a filesystem. */
#define SAVER_SLIDES_MAX 512

/* The clock mode's buffer. Fixed, and MOVED rather than repainted — big enough
 * for the 160px digits plus the date under them, with room for the glow. The
 * drift margins in saver_step_drift are a fraction of the OUTPUT, so this never
 * has to grow with the screen. */
#define SAVER_CLOCK_W 900
#define SAVER_CLOCK_H 340

static uint32_t saver_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* The mode that will actually be drawn. MATRIX degrades to CLOCK wherever the
 * rain cannot render, for the reason in the file header: a black screen is not
 * a recognisable failure.
 *
 * This is re-evaluated every frame, not decided once at saver_show(). It has to
 * be: matrix builds its shader and atlas on the FIRST FRAME, so a saver that
 * comes up in matrix mode may only discover a frame later that the rain is
 * impossible. Asking again each tick is what lets it recover to the clock
 * instead of staying black. */
static syn_saver_mode_t saver_effective_mode(syn_server_t *s)
{
    syn_saver_mode_t m = s->config.saver_mode;
    if (m == SYN_SAVER_MATRIX && !matrix_usable(s)) {
        /* matrix_usable(), not `s->matrix != NULL`: the shader and the kanji
         * atlas are built on the first frame, so a missing atlas or a shader
         * that will not compile is not known at init. Testing the weaker
         * condition is how this shipped a saver that logged "showing (matrix)"
         * and put up a black screen — the precise failure the file header says
         * must not happen. */
        return SYN_SAVER_CLOCK;
    }
    return m;
}

/* Is the rain being drawn as the SCREENSAVER right now, rather than as the
 * wallpaper? matrix.c asks, because that is the one thing that differs between
 * the two jobs: as a wallpaper the rain sits at the bottom of wallpaper_tree,
 * under every window; as a saver it sits at the top of the saver's own tree,
 * over all of them. Same shader, same buffer, same swapchain. */
bool saver_wants_matrix(syn_server_t *s)
{
    return s->saver.active && s->saver.tree &&
           saver_effective_mode(s) == SYN_SAVER_MATRIX;
}

/* ── Slideshow image list ────────────────────────────────── */

static bool saver_is_image(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot, ".png")  || !strcasecmp(dot, ".jpg") ||
           !strcasecmp(dot, ".jpeg");
}

/* Collect every image in one directory into the slide list. Appends, so the
 * caller can sweep several directories into one show. */
static void saver_scan_dir(syn_saver_t *sv, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) && sv->nslides < SAVER_SLIDES_MAX) {
        if (de->d_name[0] == '.') continue;
        if (!saver_is_image(de->d_name)) continue;

        /* A truncated path is a path that will not open, and it would sit in
         * the list forever failing to decode once per lap. Drop it here, where
         * we can still say why. */
        int n = snprintf(sv->slides[sv->nslides], sizeof(sv->slides[0]),
                         "%s/%s", dir, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(sv->slides[0])) {
            wlr_log(WLR_ERROR, "synui: saver: path too long, skipping '%s/%s'",
                    dir, de->d_name);
            continue;
        }
        sv->nslides++;
    }
    closedir(d);
}

/* Build the list the slideshow will walk. Rebuilt on every show rather than
 * cached, so a wallpaper dropped in since the last idle is picked up without a
 * restart — this runs once per saver, not once per frame. */
static void saver_build_slides(syn_server_t *s)
{
    syn_saver_t *sv = &s->saver;

    sv->nslides = 0;
    if (!sv->slides) {
        sv->slides = calloc(SAVER_SLIDES_MAX, sizeof(sv->slides[0]));
        if (!sv->slides) return;
    }

    if (s->config.saver_dir[0]) {
        saver_scan_dir(sv, s->config.saver_dir);
    } else {
        /* No directory configured: use the same wallpapers the Super+W picker
         * offers, which is what makes the mode work out of the box. */
        char path[512];
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(path, sizeof(path), "%s/Pictures/Wallpapers", home);
            saver_scan_dir(sv, path);
            snprintf(path, sizeof(path), "%s/.config/synui/wallpapers", home);
            saver_scan_dir(sv, path);
        }
        saver_scan_dir(sv, SYNUI_DATADIR "/wallpapers");
        saver_scan_dir(sv, SYNUI_DATADIR);
    }

    wlr_log(WLR_INFO, "synui: saver: slideshow has %d image(s)", sv->nslides);
    sv->slide = 0;
    sv->slide_started_ms = saver_now_ms();
}

static void saver_drop_slides(syn_saver_t *sv)
{
    if (sv->slide_surf) { cairo_surface_destroy(sv->slide_surf); sv->slide_surf = NULL; }
    if (sv->slide_prev) { cairo_surface_destroy(sv->slide_prev); sv->slide_prev = NULL; }
}

/* ── Lock background image list ──────────────────────────── */

/* The pictures the panel's "Lock image" row walks, built on first use. The
 * same scan the Super+W picker browses with, so the two lists cannot drift —
 * into our own array rather than the picker's, which is live for as long as
 * its panel is open. False when there is nothing to offer. */
static bool saver_lock_imgs(syn_server_t *s)
{
    syn_saver_t *sv = &s->saver;

    if (!sv->lock_imgs) {
        sv->lock_imgs = calloc(WPPICK_FOUND_MAX, sizeof(sv->lock_imgs[0]));
        if (!sv->lock_imgs) return false;
        sv->nlock_imgs = wppick_scan_into(sv->lock_imgs, WPPICK_FOUND_MAX);
    }
    return sv->nlock_imgs > 0;
}

static void saver_drop_lock_imgs(syn_saver_t *sv)
{
    free(sv->lock_imgs);
    sv->lock_imgs  = NULL;
    sv->nlock_imgs = 0;
}

/* Move to the next image, keeping the outgoing one for the crossfade. Only one
 * decoded image plus the one fading out is ever held: a full-screen decode is
 * megabytes, and a slideshow holding ten would cost more resident memory than
 * the rest of the compositor. */
static void saver_next_slide(syn_server_t *s)
{
    syn_saver_t *sv = &s->saver;
    if (sv->nslides <= 0) return;

    if (sv->slide_prev) cairo_surface_destroy(sv->slide_prev);
    sv->slide_prev = sv->slide_surf;
    sv->slide_surf = NULL;

    /* Walk forward until something decodes. A directory of broken files would
     * otherwise spin here forever, so give up after one full lap. */
    for (int tries = 0; tries < sv->nslides; tries++) {
        sv->slide = (sv->slide + 1) % sv->nslides;
        sv->slide_surf = wallpaper_decode(sv->slides[sv->slide]);
        if (sv->slide_surf) break;
    }

    sv->slide_started_ms = saver_now_ms();
    sv->repaint = 1;            /* a new picture is new pixels */
}

/* ── Drawing ─────────────────────────────────────────────── */

/* The accent every mode draws with. Follows the desktop theme unless the lock
 * has been given a colour of its own — the saver and the lock screen share one
 * look deliberately, since one fades into the other. */
static void saver_accent(syn_server_t *s, double out[3])
{
    const float *a = s->config.lock_theme_follow ? s->config.panel_accent
                                                 : s->config.lock_accent;
    for (int i = 0; i < 3; i++) out[i] = a[i];
}

/* Centred in its OWN buffer — the drift is applied to the scene node in
 * saver_render, not here, so that moving the clock costs a position update
 * rather than a repaint. */
static void saver_draw_clock(syn_server_t *s, cairo_t *cr, int w, int h)
{
    double acc[3];
    saver_accent(s, acc);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    char hhmm[16], date[64];
    strftime(hhmm, sizeof(hhmm), s->clock.fmt24 ? "%H:%M" : "%-I:%M", &tm);
    strftime(date, sizeof(date), "%A, %B %-d", &tm);

    double cx = w / 2.0;
    double cy = h / 2.0;

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 160);
    cairo_text_extents_t te;
    syn_text_extents(cr, hhmm, &te);

    /* Glow, then glyphs — the same two-pass the lock panel draws its clock
     * with, so the two read as one screen when the saver hands over to it. */
    cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.20);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing + 3, cy + 3);
    syn_show_text(cr, hhmm);
    cairo_set_source_rgba(cr, 0.88, 0.96, 1.0, 0.92);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, cy);
    syn_show_text(cr, hhmm);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 28);
    syn_text_extents(cr, date, &te);
    cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.75);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, cy + 52);
    syn_show_text(cr, date);
}

/* Advance the clock's slow drift and bounce it off the margins. The point is
 * burn-in: a clock parked on the same pixels for eight hours is exactly the
 * pattern OLED panels retain, and this box has one. */
static void saver_step_drift(syn_saver_t *sv, int w, int h, double dt)
{
    const double margin_x = w * 0.18, margin_y = h * 0.22;

    sv->drift_x += sv->drift_dx * dt;
    sv->drift_y += sv->drift_dy * dt;

    if (sv->drift_x < -margin_x) { sv->drift_x = -margin_x; sv->drift_dx = -sv->drift_dx; }
    if (sv->drift_x >  margin_x) { sv->drift_x =  margin_x; sv->drift_dx = -sv->drift_dx; }
    if (sv->drift_y < -margin_y) { sv->drift_y = -margin_y; sv->drift_dy = -sv->drift_dy; }
    if (sv->drift_y >  margin_y) { sv->drift_y =  margin_y; sv->drift_dy = -sv->drift_dy; }
}

static void saver_draw_starfield(syn_server_t *s, cairo_t *cr, int w, int h)
{
    syn_saver_t *sv = &s->saver;
    double acc[3];
    saver_accent(s, acc);

    double cx = w / 2.0, cy = h / 2.0;

    /* Half the viewport per axis, NOT a single scale off the long edge. With
     * one shared scale a star at x=1 lands beyond the right edge even at its
     * spawn depth, so most of the field is off-screen from birth and the
     * visible density is a fraction of the star count — which is exactly what
     * the first capture showed. Scaling each axis by its own half-extent makes
     * z=1 fill the screen precisely, and everything nearer flies out of it. */
    double sx = w / 2.0, sy = h / 2.0;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    for (int i = 0; i < SYN_SAVER_STARS; i++) {
        syn_star_t *st = &sv->stars[i];
        if (st->z <= 0.02f) continue;

        /* Perspective divide: nearer stars (small z) throw further out and
         * move faster, which is the whole illusion. */
        double x  = cx + (st->x / st->z) * sx;
        double y  = cy + (st->y / st->z) * sy;
        double px = cx + (st->x / st->pz) * sx;
        double py = cy + (st->y / st->pz) * sy;

        if (x < -50 || x > w + 50 || y < -50 || y > h + 50) continue;

        /* Brightness and thickness both track closeness. */
        double near = 1.0 - st->z;
        double a = 0.25 + near * 0.75;
        cairo_set_line_width(cr, 0.6 + near * 2.2);

        /* The far half stays white; the near half picks up the accent, so the
         * field reads as themed without turning into a wall of one colour. */
        double mix = near * near;
        cairo_set_source_rgba(cr,
                              1.0 - mix * (1.0 - acc[0]),
                              1.0 - mix * (1.0 - acc[1]),
                              1.0 - mix * (1.0 - acc[2]), a);
        cairo_move_to(cr, px, py);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);
    }
}

/* rand() is fine here — these are decorative positions, not anything that has
 * to be unpredictable, and seeding is done once in saver_init. */
static float saver_frand(void)
{
    return (float)rand() / (float)RAND_MAX;
}

static void saver_star_respawn(syn_star_t *st, bool fresh)
{
    st->x = saver_frand() * 2.0f - 1.0f;
    st->y = saver_frand() * 2.0f - 1.0f;
    /* `fresh` spreads the initial field through the whole depth range so the
     * first frame is a starfield rather than a single distant plane. */
    st->z = fresh ? 0.05f + saver_frand() * 0.95f : 1.0f;
    st->pz = st->z;
}

static void saver_step_stars(syn_saver_t *sv, double dt)
{
    for (int i = 0; i < SYN_SAVER_STARS; i++) {
        syn_star_t *st = &sv->stars[i];
        st->pz = st->z;
        st->z -= (float)(dt * 0.25);
        if (st->z <= 0.02f) saver_star_respawn(st, false);
    }
}

static void saver_draw_slideshow(syn_server_t *s, cairo_t *cr, int w, int h)
{
    syn_saver_t *sv = &s->saver;

    if (sv->nslides <= 0) {
        /* Nothing to show. Say so rather than presenting a black screen that
         * looks like the saver is broken. */
        double acc[3];
        saver_accent(s, acc);
        const char *msg = "No wallpapers found for the slideshow";
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 22);
        cairo_text_extents_t te;
        syn_text_extents(cr, msg, &te);
        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.8);
        cairo_move_to(cr, w / 2.0 - te.width / 2 - te.x_bearing, h / 2.0);
        syn_show_text(cr, msg);
        return;
    }

    uint32_t elapsed = saver_now_ms() - sv->slide_started_ms;
    double fade = elapsed < SAVER_FADE_MS ? (double)elapsed / SAVER_FADE_MS : 1.0;

    /* The outgoing image at full strength underneath, the incoming one faded
     * over it. Painting both every frame for the first second is the whole
     * cost of the crossfade. */
    if (sv->slide_prev && fade < 1.0) {
        cairo_save(cr);
        wallpaper_paint_box(cr, sv->slide_prev, w, h, SYN_WALLPAPER_FILL);
        cairo_restore(cr);
    }
    if (sv->slide_surf) {
        cairo_save(cr);
        cairo_push_group(cr);
        wallpaper_paint_box(cr, sv->slide_surf, w, h, SYN_WALLPAPER_FILL);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, fade);
        cairo_restore(cr);
    }

    /* Drop the outgoing image as soon as the fade is over — holding it until
     * the next slide would double the resident cost for no benefit. */
    if (fade >= 1.0 && sv->slide_prev) {
        cairo_surface_destroy(sv->slide_prev);
        sv->slide_prev = NULL;
    }

    /* A small clock in the corner, so the slideshow is still useful as a
     * screensaver rather than only as a picture frame.
     *
     * It sits on a DARK PILL rather than on the photograph. A drop shadow was
     * the first attempt and it is not enough: half the wallpapers in this tree
     * are pale engravings on cream paper, and white-on-cream stays unreadable
     * however much shadow is under it — the same pale-surface failure the
     * panels hit. The pill makes the surface the text is drawn on a known
     * colour instead of an unknown one, which is the only fix that holds for
     * every possible image. */
    {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        char hhmm[16];
        strftime(hhmm, sizeof(hhmm), s->clock.fmt24 ? "%H:%M" : "%-I:%M %p", &tm);

        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 40);
        cairo_text_extents_t te;
        syn_text_extents(cr, hhmm, &te);

        /* Size the pill by the PEN ADVANCE, not by the ink width.
         *
         * te.width is the bounding box of the marks, which for "4:38 AM" stops
         * at the last glyph's ink and ignores the trailing space's advance —
         * and syn_show_text lays out by advance across its fallback runs. Sized
         * by te.width the pill came up short and the "AM" hung off the right
         * edge, back on the photograph, which is the exact thing the pill is
         * here to prevent. Take whichever is larger: x_advance is right for a
         * string ending in whitespace, width for one whose last glyph overhangs
         * its advance (an italic f, say). */
        double tw = te.x_advance > te.width ? te.x_advance : te.width;

        const double padx = 22, pady = 14, margin = 40;
        double bw = tw + padx * 2, bh = te.height + pady * 2;
        double bx = w - bw - margin, by = h - bh - margin;

        cairo_rounded_rect(cr, bx, by, bw, bh, 14);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.05, 0.62);
        cairo_fill(cr);

        /* Now that the surface is known-dark, plain light ink is correct. */
        cairo_set_source_rgba(cr, 0.93, 0.98, 1.0, 0.96);
        cairo_move_to(cr, bx + padx - te.x_bearing, by + pady - te.y_bearing);
        syn_show_text(cr, hhmm);
    }
}

/* Repaint every pane. One full-screen buffer per output — unavoidable, since
 * the saver covers the screen — allocated lazily here so saver_show() need only
 * record the outputs, exactly as the lock does. */
/* Does this tick need new PIXELS, as opposed to just a new position?
 *
 * Only the starfield genuinely animates every frame. The clock changes once a
 * minute and otherwise merely moves, and the slideshow is a still picture
 * except during its crossfade. Answering honestly here is what keeps an idle
 * machine idle — see the note on saver.drawn_min. */
static bool saver_needs_paint(syn_server_t *s, syn_saver_mode_t mode, int min_now)
{
    syn_saver_t *sv = &s->saver;

    if (sv->repaint || sv->drawn_min != min_now) return true;

    switch (mode) {
    case SYN_SAVER_STARFIELD:
        return true;                       /* every frame, by nature */
    case SYN_SAVER_SLIDESHOW:
        /* Only while a crossfade is actually in flight. */
        return (saver_now_ms() - sv->slide_started_ms) < SAVER_FADE_MS;
    default:
        return false;
    }
}

static void saver_render(syn_server_t *s)
{
    syn_saver_t *sv = &s->saver;
    if (!sv->active || !sv->tree) return;

    syn_saver_mode_t mode = saver_effective_mode(s);

    /* MATRIX draws through matrix.c's own per-output GL path, not through a
     * cairo buffer. Nothing to paint here; the black backstop shows until the
     * output frame handler runs. */
    if (mode == SYN_SAVER_MATRIX || mode == SYN_SAVER_BLANK) return;

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    int min_now = tm.tm_hour * 60 + tm.tm_min;

    bool paint = saver_needs_paint(s, mode, min_now);

    for (int i = 0; i < sv->npane; i++) {
        struct wlr_output *o = sv->pane[i].output;
        if (!o) continue;

        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, o, &box);
        if (box.width <= 0 || box.height <= 0) continue;   /* output went away */

        /* CLOCK gets a FIXED-SIZE buffer that is moved, not a full-screen one
         * that is repainted. The drift is the whole reason a full-screen buffer
         * looked necessary, and it is precisely the part that does not need
         * one: moving a scene node is a position update, not a reallocation.
         * The other two modes cover the screen and genuinely need its size. */
        bool panel_sized = (mode == SYN_SAVER_CLOCK);
        int  bw = panel_sized ? SAVER_CLOCK_W : box.width;
        int  bh = panel_sized ? SAVER_CLOCK_H : box.height;

        if (paint || !sv->pane[i].buf) {
            cairo_t *cr;
            struct wlr_buffer *buf = create_cairo_buf(bw, bh, &cr);
            if (!buf) continue;
            cairo_begin(cr);

            switch (mode) {
            case SYN_SAVER_CLOCK:     saver_draw_clock(s, cr, bw, bh); break;
            case SYN_SAVER_STARFIELD: saver_draw_starfield(s, cr, bw, bh); break;
            case SYN_SAVER_SLIDESHOW: saver_draw_slideshow(s, cr, bw, bh); break;
            default: break;
            }
            cairo_destroy(cr);

            set_scene_buffer(&sv->pane[i].buf, sv->tree, buf);
        }
        if (!sv->pane[i].buf) continue;

        /* Position: the clock rides the drift here, at the node, rather than
         * inside its own buffer. */
        int px = box.x, py = box.y;
        if (panel_sized) {
            px += (box.width  - SAVER_CLOCK_W) / 2 + (int)sv->drift_x;
            py += (box.height - SAVER_CLOCK_H) / 2 + (int)sv->drift_y;
        }
        wlr_scene_node_set_position(&sv->pane[i].buf->node, px, py);
    }

    if (paint) {
        sv->drawn_min = min_now;
        sv->repaint   = 0;
    }
}

/* ── Animation tick ──────────────────────────────────────── */

static int saver_frame_cb(void *data)
{
    syn_server_t *s = data;
    syn_saver_t *sv = &s->saver;
    if (!sv->active) return 0;

    uint32_t now = saver_now_ms();
    double dt = (now - sv->last_frame_ms) / 1000.0;
    sv->last_frame_ms = now;
    /* A resume, or a machine that was stopped, can hand us a huge dt; clamp so
     * the starfield does not jump a light year in one frame. */
    if (dt > 0.5) dt = 0.5;
    if (dt < 0.0) dt = 0.0;

    syn_saver_mode_t mode = saver_effective_mode(s);

    switch (mode) {
    case SYN_SAVER_CLOCK: {
        struct wlr_box box = { 0 };
        if (sv->npane > 0 && sv->pane[0].output)
            wlr_output_layout_get_box(s->output_layout, sv->pane[0].output, &box);
        if (box.width > 0) saver_step_drift(sv, box.width, box.height, dt);
        break;
    }
    case SYN_SAVER_STARFIELD:
        saver_step_stars(sv, dt);
        break;
    case SYN_SAVER_SLIDESHOW: {
        int iv = s->config.saver_interval;
        if (iv < SAVER_INTERVAL_MIN) iv = SAVER_INTERVAL_MIN;
        if (now - sv->slide_started_ms >= (uint32_t)iv * 1000)
            saver_next_slide(s);
        break;
    }
    default:
        break;
    }

    saver_render(s);
    wl_event_source_timer_update(sv->t_frame, SAVER_TICK_MS);
    return 0;
}

/* ── Show / dismiss ──────────────────────────────────────── */

bool saver_active(syn_server_t *s)
{
    return s->saver.active != 0;
}

void saver_show(syn_server_t *s)
{
    syn_saver_t *sv = &s->saver;

    if (sv->active) return;
    /* BLANK has nothing to draw that the blank stage does not already do
     * better (it powers the panel down). Treat it as "no saver". */
    if (s->config.saver_mode == SYN_SAVER_BLANK) return;

    sv->npane = 0;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (sv->npane >= SYN_SAVER_PANE_MAX) break;
        sv->pane[sv->npane].output = o->wlr_output;
        sv->pane[sv->npane].buf    = NULL;
        sv->npane++;
    }
    if (sv->npane == 0) return;      /* nowhere to draw */

    sv->active    = 1;
    sv->start_ms  = sv->last_frame_ms = saver_now_ms();
    sv->drawn_min = -1;         /* nothing painted yet: force the first frame */
    sv->repaint   = 1;

    /* Whether the session was ALREADY locked when the saver came up. A saver
     * drawn over the lock screen must not re-lock on dismissal, and must hand
     * the screen back to the lock rather than to the desktop. */
    sv->over_lock = s->locked ? 1 : 0;

    /* Above everything, including the lock — the lock's own tree was raised to
     * the top when it engaged, so this has to be raised after it. */
    sv->tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_raise_to_top(&sv->tree->node);

    /* A black backstop across the whole layout, for the same reason the lock
     * has one: an output with no pane, or one hotplugged mid-saver, must never
     * show the desktop underneath. */
    float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    struct wlr_scene_rect *bg =
        wlr_scene_rect_create(sv->tree, 32768, 32768, black);
    wlr_scene_node_set_position(&bg->node, -16384, -16384);

    syn_saver_mode_t mode = saver_effective_mode(s);

    if (mode == SYN_SAVER_STARFIELD)
        for (int i = 0; i < SYN_SAVER_STARS; i++)
            saver_star_respawn(&sv->stars[i], true);

    if (mode == SYN_SAVER_CLOCK) {
        sv->drift_x = sv->drift_y = 0;
        /* Slow: a couple of dozen pixels a second, enough to have moved
         * meaningfully across an hour and not enough to be distracting. */
        sv->drift_dx = 18 + saver_frand() * 10;
        sv->drift_dy = 12 + saver_frand() * 8;
        if (rand() & 1) sv->drift_dx = -sv->drift_dx;
        if (rand() & 1) sv->drift_dy = -sv->drift_dy;
    }

    if (mode == SYN_SAVER_SLIDESHOW) {
        saver_build_slides(s);
        if (sv->nslides > 0) {
            /* Decode the first image directly rather than through
             * saver_next_slide, which would start the show on image 1. */
            sv->slide = 0;
            sv->slide_surf = wallpaper_decode(sv->slides[0]);
            if (!sv->slide_surf) saver_next_slide(s);
            sv->slide_started_ms = saver_now_ms();
        }
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    sv->t_frame = wl_event_loop_add_timer(loop, saver_frame_cb, s);
    wl_event_source_timer_update(sv->t_frame, SAVER_TICK_MS);

    saver_render(s);
    wlr_log(WLR_INFO, "synui: saver: showing (%s%s)",
            syn_saver_mode_names[mode],
            sv->over_lock ? ", over the lock" : "");
}

void saver_dismiss(syn_server_t *s, bool by_input)
{
    syn_saver_t *sv = &s->saver;
    if (!sv->active) return;

    /* Clear `active` FIRST: matrix_output_destroy() below runs while the tree
     * still exists, and saver_wants_matrix() has to already be answering false
     * so nothing re-parents a node back into a tree that is about to go. */
    sv->active = 0;

    if (sv->t_frame) {
        wl_event_source_remove(sv->t_frame);
        sv->t_frame = NULL;
    }

    /* The matrix rain's scene node is a CHILD of the saver tree while the saver
     * is driving it, so destroying the tree would take that node with it and
     * leave o->matrix_buf dangling — the next frame would then write through a
     * freed pointer. Drop those nodes explicitly first; matrix.c rebuilds them
     * under wallpaper_tree on the next frame if the rain is also the wallpaper. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (o->matrix_buf) matrix_output_destroy(o);

    if (sv->tree) {
        wlr_scene_node_destroy(&sv->tree->node);   /* takes the panes with it */
        sv->tree = NULL;
    }
    for (int i = 0; i < sv->npane; i++) sv->pane[i].buf = NULL;
    sv->npane = 0;

    saver_drop_slides(sv);

    wlr_log(WLR_INFO, "synui: saver: dismissed (%s)",
            by_input ? "input" : "teardown");

    /* Lock on the way out, if asked — but never when the saver was drawn OVER
     * an already-locked session (there is nothing to lock, and synui_lock is
     * idempotent but the sound and the re-arm are not wanted), and never on a
     * teardown, which is a mode change or a shutdown rather than a user
     * arriving back at the machine. */
    if (by_input && !sv->over_lock && s->config.saver_lock)
        synui_lock(s);
}

void saver_output_destroy(syn_output_t *o)
{
    if (!o) return;
    syn_server_t *s = o->server;
    syn_saver_t *sv = &s->saver;
    struct wlr_output *dead = o->wlr_output;

    /* Same dangling-pointer hazard the lock has: a monitor destroyed across a
     * suspend/DPMS cycle leaves a pane holding a freed wlr_output, which
     * saver_render() would then hand to wlr_output_layout_get_box(). */
    for (int i = 0; i < sv->npane; i++) {
        if (sv->pane[i].output != dead) continue;
        if (sv->pane[i].buf) {
            wlr_scene_node_destroy(&sv->pane[i].buf->node);
            sv->pane[i].buf = NULL;
        }
        sv->pane[i].output = NULL;
    }
}

void saver_output_create(syn_output_t *o)
{
    syn_server_t *s = o->server;
    syn_saver_t *sv = &s->saver;
    if (!sv->active) return;

    int slot = -1;
    for (int i = 0; i < sv->npane; i++) {
        if (sv->pane[i].output == o->wlr_output) return;   /* already paned */
        if (slot < 0 && !sv->pane[i].output) slot = i;
    }
    if (slot < 0) {
        if (sv->npane >= SYN_SAVER_PANE_MAX) return;   /* backstop keeps it black */
        slot = sv->npane++;
    }
    sv->pane[slot].output = o->wlr_output;
    sv->pane[slot].buf    = NULL;
    sv->repaint = 1;            /* the new pane has no buffer to reuse */
    saver_render(s);
}

/* ── Lifecycle ───────────────────────────────────────────── */

void saver_init(syn_server_t *s)
{
    /* Decorative randomness only — see saver_frand. Seeded from the clock so
     * the starfield is not identical on every boot. */
    srand((unsigned)time(NULL));
    memset(&s->saver, 0, sizeof(s->saver));
}

void saver_finish(syn_server_t *s)
{
    saver_dismiss(s, false);
    free(s->saver.slides);
    s->saver.slides = NULL;
    saver_drop_lock_imgs(&s->saver);
}

/* ── Settings panel (Super+Z) ────────────────────────────── */

static const char *saver_row_label(int row)
{
    switch (row) {
    case SAVER_ROW_MODE:       return "Screensaver";
    case SAVER_ROW_TIMEOUT:    return "Show after";
    case SAVER_ROW_LOCK:       return "Lock on wake";
    case SAVER_ROW_INTERVAL:   return "Slide interval";
    case SAVER_ROW_LOCK_BG:    return "Lock background";
    case SAVER_ROW_LOCK_IMAGE: return "Lock image";
    case SAVER_ROW_LOCK_DIM:   return "Lock dim";
    case SAVER_ROW_LOCK_BLUR:  return "Lock blur";
    case SAVER_ROW_LOCK_THEME: return "Lock colours";
    default:                   return "?";
    }
}

/* ── The lock background's own picture ───────────────────────
 *
 * `lock_background = image` used to be a source with no way to name the image
 * short of editing synuirc — the panel offered the option and then said
 * nothing, and an empty lock_bg_image locks to plain black, so choosing it
 * looked like it had done nothing at all. The row below walks the pictures
 * saver_lock_imgs() found.
 */

/* Step to the next (dir > 0) or previous picture, wrapping. A path set from
 * synuirc that is not in the list simply is not found — start at one end
 * rather than refusing to move, which would strand the row. */
static bool saver_lock_image_step(syn_server_t *s, int dir)
{
    syn_saver_t *sv = &s->saver;
    if (!saver_lock_imgs(s)) return false;

    int cur = -1;
    for (int i = 0; i < sv->nlock_imgs; i++)
        if (strcmp(sv->lock_imgs[i], s->config.lock_bg_image) == 0) { cur = i; break; }

    int next = cur < 0 ? (dir > 0 ? 0 : sv->nlock_imgs - 1)
                       : (cur + dir + sv->nlock_imgs) % sv->nlock_imgs;

    snprintf(s->config.lock_bg_image, sizeof(s->config.lock_bg_image),
             "%s", sv->lock_imgs[next]);
    lock_bg_invalidate(s);
    return true;
}

/* The file name, cut to what the value column can hold — these are paths and
 * the column is about 200px of a 560px panel. Cut on a UTF-8 lead byte: half a
 * sequence draws as a replacement glyph, which looks like a broken file name
 * rather than a truncated one. */
static void saver_image_label(const char *path, char *buf, size_t n)
{
    if (!path || !*path) { snprintf(buf, n, "none"); return; }

    const char *slash = strrchr(path, '/');
    const char *base  = slash ? slash + 1 : path;

    size_t keep = 22;
    if (strlen(base) <= keep) { snprintf(buf, n, "%s", base); return; }
    while (keep > 0 && ((unsigned char)base[keep] & 0xC0) == 0x80) keep--;
    snprintf(buf, n, "%.*s\xe2\x80\xa6", (int)keep, base);
}

/* The same ladder power.c steps its timeouts along, minus the hours: a
 * screensaver three hours out is one that never shows. */
static const int saver_ladder[] = {
    0, 30, 60, 120, 180, 240, 300, 600, 900, 1200, 1800,
};
static const int saver_ladder_len =
    (int)(sizeof(saver_ladder) / sizeof(saver_ladder[0]));

static void saver_format_timeout(int secs, char *buf, size_t n)
{
    if (secs <= 0)          snprintf(buf, n, "never");
    else if (secs < 60)     snprintf(buf, n, "%ds", secs);
    else if (secs % 60 == 0) snprintf(buf, n, "%dm", secs / 60);
    else                    snprintf(buf, n, "%dm %ds", secs / 60, secs % 60);
}

int saver_panel_rows(syn_server_t *s, int row, char *name, size_t nn,
                     char *value, size_t vn)
{
    syn_config_t *c = &s->config;
    snprintf(name, nn, "%s", saver_row_label(row));

    switch (row) {
    case SAVER_ROW_MODE:
        snprintf(value, vn, "%s", syn_saver_mode_names[c->saver_mode]);
        return c->saver_mode == SYN_SAVER_BLANK;
    case SAVER_ROW_TIMEOUT:
        saver_format_timeout(c->saver_timeout, value, vn);
        return c->saver_timeout <= 0;
    case SAVER_ROW_LOCK:
        snprintf(value, vn, "%s", c->saver_lock ? "yes" : "no");
        return !c->saver_lock;
    case SAVER_ROW_INTERVAL:
        snprintf(value, vn, "%ds", c->saver_interval);
        /* Inert unless the slideshow is the mode — the row stays visible so
         * the setting is discoverable, but greyed so it does not read as
         * something that is doing anything right now. */
        return c->saver_mode != SYN_SAVER_SLIDESHOW;
    case SAVER_ROW_LOCK_BG:
        snprintf(value, vn, "%s", syn_lock_bg_names[c->lock_bg]);
        return c->lock_bg == SYN_LOCK_BG_BLACK;
    case SAVER_ROW_LOCK_IMAGE:
        saver_image_label(c->lock_bg_image, value, vn);
        /* Dim unless it is the picture the lock will actually use — but always
         * listed, because a source called "image" with no way to name one is
         * how this ended up looking broken. */
        return c->lock_bg != SYN_LOCK_BG_IMAGE;
    case SAVER_ROW_LOCK_DIM:
        snprintf(value, vn, "%d%%", c->lock_bg_dim);
        return c->lock_bg == SYN_LOCK_BG_BLACK;
    case SAVER_ROW_LOCK_BLUR:
        if (c->lock_bg_blur <= 0) snprintf(value, vn, "off");
        else                      snprintf(value, vn, "%dpx", c->lock_bg_blur);
        return c->lock_bg == SYN_LOCK_BG_BLACK || c->lock_bg_blur <= 0;
    case SAVER_ROW_LOCK_THEME:
        snprintf(value, vn, "%s", c->lock_theme_follow ? "follow theme" : "custom");
        return 0;
    default:
        snprintf(value, vn, "?");
        return 1;
    }
}

/* Step the selected row. Every row is a value Left/Right walks, as in the
 * power panel — see the panel pointer contract in synui.h. */
static void saver_adjust(syn_server_t *s, int dir)
{
    syn_config_t *c = &s->config;
    syn_saver_t *sv = &s->saver;
    char v[40];

    /* Overrides the usual "row: value" footer when the step has something to
     * say that the value alone does not. */
    const char *note = NULL;

    switch (sv->selected) {
    case SAVER_ROW_MODE: {
        int next = (c->saver_mode + dir) % SYN_SAVER_MODE_COUNT;
        if (next < 0) next += SYN_SAVER_MODE_COUNT;
        c->saver_mode = next;
        /* A mode change while the saver is up would leave the old mode's
         * buffers on screen; take it down and let the stage bring it back. */
        if (sv->active) saver_dismiss(s, false);
        break;
    }
    case SAVER_ROW_TIMEOUT: {
        int cur = c->saver_timeout, next = cur;
        if (dir > 0) {
            for (int i = 0; i < saver_ladder_len; i++)
                if (saver_ladder[i] > cur) { next = saver_ladder[i]; break; }
        } else {
            for (int i = saver_ladder_len - 1; i >= 0; i--)
                if (saver_ladder[i] < cur) { next = saver_ladder[i]; break; }
        }
        if (next == cur) return;
        c->saver_timeout = next;
        /* Retuning the timeout restarts the idle period, so the new value is
         * measured from now — the same thing the power panel does. */
        power_notify_activity(s);
        break;
    }
    case SAVER_ROW_LOCK:
        c->saver_lock = !c->saver_lock;
        break;
    case SAVER_ROW_INTERVAL: {
        int next = c->saver_interval + dir * 5;
        if (next < SAVER_INTERVAL_MIN) next = SAVER_INTERVAL_MIN;
        if (next > SAVER_INTERVAL_MAX) next = SAVER_INTERVAL_MAX;
        if (next == c->saver_interval) return;
        c->saver_interval = next;
        break;
    }
    case SAVER_ROW_LOCK_BG: {
        int next = (c->lock_bg + dir) % SYN_LOCK_BG_COUNT;
        if (next < 0) next += SYN_LOCK_BG_COUNT;
        c->lock_bg = next;
        /* "image" with no image is a black lock screen and no clue why, so
         * landing on it with nothing named picks the first picture rather than
         * leaving the choice looking like it did nothing. */
        if (next == SYN_LOCK_BG_IMAGE && !c->lock_bg_image[0] &&
            !saver_lock_image_step(s, +1))
            note = "Lock background: image \xc2\xb7 no pictures found to use";
        lock_bg_invalidate(s);
        break;
    }
    case SAVER_ROW_LOCK_IMAGE:
        if (!saver_lock_image_step(s, dir)) {
            /* Nothing changed, so nothing to mark dirty or restate. */
            snprintf(sv->status, sizeof(sv->status),
                     "No pictures in ~/Pictures or /usr/share/backgrounds");
            return;
        }
        /* Picking a picture is also how you say you want one. */
        if (c->lock_bg != SYN_LOCK_BG_IMAGE) {
            c->lock_bg = SYN_LOCK_BG_IMAGE;
            lock_bg_invalidate(s);
        }
        break;
    case SAVER_ROW_LOCK_DIM: {
        int next = c->lock_bg_dim + dir * 5;
        if (next < 0) next = 0;
        if (next > 100) next = 100;
        if (next == c->lock_bg_dim) return;
        c->lock_bg_dim = next;
        lock_bg_invalidate(s);
        break;
    }
    case SAVER_ROW_LOCK_BLUR: {
        int next = c->lock_bg_blur + dir * 4;
        if (next < 0) next = 0;
        if (next > 64) next = 64;
        if (next == c->lock_bg_blur) return;
        c->lock_bg_blur = next;
        lock_bg_invalidate(s);
        break;
    }
    case SAVER_ROW_LOCK_THEME:
        c->lock_theme_follow = !c->lock_theme_follow;
        break;
    default:
        return;
    }

    sv->dirty = 1;

    /* The login screen is this screen, so a lock background changed here has to
     * reach it — and it cannot be read across the permission boundary, so it is
     * published. One call at the commit point rather than one beside each of
     * the four lock_bg_invalidate()s above: those are the lock's own repaint,
     * and half of them fire for rows that are not the background at all. */
    greeterbg_publish(s);

    /* Name and value together must fit the status line; both are short by
     * construction (the longest value is a file name, and saver_image_label
     * cuts those to 22 characters), so the buffers are sized to make that
     * obvious rather than to be defended at runtime. */
    char nm[40];
    saver_panel_rows(s, sv->selected, nm, sizeof(nm), v, sizeof(v));
    if (note) snprintf(sv->status, sizeof(sv->status), "%s", note);
    else      snprintf(sv->status, sizeof(sv->status), "%s: %s", nm, v);
}

void saver_show_panel(syn_server_t *s)
{
    s->saver.visible   = 1;
    s->saver.selected  = SAVER_ROW_MODE;
    s->saver.status[0] = '\0';
    synui_render_saver(s);
}

void saver_hide(syn_server_t *s)
{
    s->saver.visible = 0;
    /* Dropped with the panel, so the next opening rescans and a picture added
     * since is offered without a restart. */
    saver_drop_lock_imgs(&s->saver);
    synui_render_saver(s);
    ctlpanel_child_closed(s, "saver");
}

void saver_toggle(syn_server_t *s)
{
    if (s->saver.visible) saver_hide(s);
    else                  saver_show_panel(s);
}

int saver_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->saver.visible) return 0;

    int row = hit_row_at(&s->saver.hit, lx, ly);
    if (row < 0 || row == s->saver.selected) return 1;
    s->saver.selected = row;
    synui_render_saver(s);
    return 1;
}

int saver_click(syn_server_t *s, double lx, double ly, uint32_t button,
                uint32_t time_msec)
{
    (void)time_msec;
    if (!s->saver.visible) return 0;

    if (!hit_in_panel(&s->saver.hit, lx, ly)) {
        saver_hide(s);
        return 1;
    }

    saver_motion(s, lx, ly);

    if (hit_row_at(&s->saver.hit, lx, ly) < 0) return 1;   /* chrome */
    if (button != BTN_LEFT && button != BTN_RIGHT) return 1;

    saver_adjust(s, button == BTN_LEFT ? +1 : -1);
    synui_render_saver(s);
    return 1;
}

int saver_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->saver.visible) return 0;
    if (delta == 0) return 1;

    int next = s->saver.selected + (delta > 0 ? 1 : -1);
    if (next < 0 || next >= SAVER_ROW_COUNT) return 1;
    s->saver.selected = next;
    synui_render_saver(s);
    return 1;
}

int saver_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->saver.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        saver_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->saver.selected > 0) s->saver.selected--;
        synui_render_saver(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->saver.selected < SAVER_ROW_COUNT - 1) s->saver.selected++;
        synui_render_saver(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        saver_adjust(s, -1);
        synui_render_saver(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        saver_adjust(s, +1);
        synui_render_saver(s);
        return 1;
    case XKB_KEY_p:
        /* Preview: close the panel and show the saver right now, which is the
         * only way to see what a mode looks like without waiting out the idle
         * timeout. Dismissed by input like any other showing. */
        saver_hide(s);
        saver_show(s);
        return 1;
    case XKB_KEY_s:
        saver_state_save(s);
        synui_render_saver(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
