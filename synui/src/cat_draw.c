/*
 * cat_draw.c — the kitty itself: pure cairo, no compositor.
 *
 * Split out from cat.c deliberately. The one bug this drawing can have is "that
 * does not read as a cat", which no assertion catches — so the drawing is kept
 * free of wlroots and syn_server_t, and tests/cat_render_test.c links just this
 * file to render the poses to a PNG a human can look at.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <math.h>
#include <cairo.h>

#include "synui.h"

/* ── Coats ───────────────────────────────────────────────────
 * The kitty is procedural, so a breed is not a sprite — it is four colours and
 * a marking style laid over one drawing. Anatomy, gait and poses are shared:
 * a breed can only change how it is painted, never how it moves, which is what
 * keeps nine of them from being nine things to maintain.
 *
 * NEON is first and its numbers are the literals this file used before breeds
 * existed, so the default cat is unchanged to the pixel.
 *
 * `coat` is the body/head fill, `rim` the outline that also carries the tail,
 * whiskers, z's and the eye halo, `eye` the bright core, and `mark` whatever
 * the marking style paints. Alphas stay at the call sites: the body fills at
 * 0.92 and the head at 0.95, and that difference is deliberate. */
typedef enum {
    CAT_MARK_NONE,
    CAT_MARK_TABBY,      /* body bars + the forehead M */
    CAT_MARK_POINTS,     /* dark ears, muzzle, legs, tail — colourpoint */
    CAT_MARK_TUXEDO,     /* white bib and muzzle */
    CAT_MARK_PATCHES,    /* calico/tortie blotches */
} cat_mark_t;

typedef struct {
    double     coat[3];
    double     rim[3];
    double     eye[3];
    double     mark[3];
    /* Second patch colour. A calico is not white-and-ginger, it is white AND
     * ginger AND black, and with one colour it renders as a ginger cat with a
     * pale face. Only CAT_MARK_PATCHES reads it; everything else leaves it
     * zeroed and never looks. */
    double     mark2[3];
    cat_mark_t marking;
} cat_coat_t;

const char *const cat_breed_names[CAT_BREED_COUNT] = {
    [CAT_BREED_NEON]         = "neon",
    [CAT_BREED_TABBY]        = "tabby",
    [CAT_BREED_GINGER]       = "ginger",
    [CAT_BREED_TUXEDO]       = "tuxedo",
    [CAT_BREED_SIAMESE]      = "siamese",
    [CAT_BREED_CALICO]       = "calico",
    [CAT_BREED_TORTIE]       = "tortie",
    [CAT_BREED_RUSSIAN_BLUE] = "russian-blue",
    [CAT_BREED_BLACK]        = "black",
};

static const cat_coat_t cat_coats[CAT_BREED_COUNT] = {
    /* The original: slate body, cyan rim, near-white glowing eyes. */
    [CAT_BREED_NEON] = {
        .coat = { 0.07, 0.09, 0.14 }, .rim  = { 0.60, 0.95, 0.90 },
        .eye  = { 0.85, 1.00, 0.98 }, .mark = { 0.60, 0.95, 0.90 },
        .marking = CAT_MARK_NONE },
    /* Brown mackerel tabby, green eyes — the default housecat. */
    [CAT_BREED_TABBY] = {
        .coat = { 0.45, 0.34, 0.22 }, .rim  = { 0.72, 0.58, 0.38 },
        .eye  = { 0.60, 0.90, 0.40 }, .mark = { 0.22, 0.16, 0.10 },
        .marking = CAT_MARK_TABBY },
    /* Marmalade. Same stripes, warmer coat, amber eyes. */
    [CAT_BREED_GINGER] = {
        .coat = { 0.78, 0.42, 0.13 }, .rim  = { 0.95, 0.65, 0.30 },
        .eye  = { 1.00, 0.82, 0.35 }, .mark = { 0.52, 0.24, 0.06 },
        .marking = CAT_MARK_TABBY },
    /* Black with a white bib and muzzle; gold eyes. */
    [CAT_BREED_TUXEDO] = {
        .coat = { 0.09, 0.09, 0.11 }, .rim  = { 0.55, 0.55, 0.60 },
        .eye  = { 1.00, 0.80, 0.25 }, .mark = { 0.96, 0.96, 0.94 },
        .marking = CAT_MARK_TUXEDO },
    /* Cream body, seal points, and the blue eyes that make it read Siamese. */
    [CAT_BREED_SIAMESE] = {
        .coat = { 0.91, 0.85, 0.72 }, .rim  = { 0.50, 0.39, 0.32 },
        .eye  = { 0.30, 0.65, 1.00 }, .mark = { 0.26, 0.18, 0.15 },
        .marking = CAT_MARK_POINTS },
    /* Pale coat carrying ginger patches; amber eyes. */
    [CAT_BREED_CALICO] = {
        .coat = { 0.95, 0.93, 0.89 }, .rim  = { 0.45, 0.40, 0.36 },
        .eye  = { 1.00, 0.78, 0.30 }, .mark = { 0.87, 0.47, 0.13 },
        .mark2 = { 0.17, 0.14, 0.13 }, .marking = CAT_MARK_PATCHES },
    /* The same blotches the other way up: dark coat, ginger over it. */
    [CAT_BREED_TORTIE] = {
        .coat = { 0.20, 0.14, 0.11 }, .rim  = { 0.62, 0.46, 0.32 },
        .eye  = { 0.95, 0.70, 0.25 }, .mark = { 0.82, 0.42, 0.10 },
        .mark2 = { 0.46, 0.30, 0.12 }, .marking = CAT_MARK_PATCHES },
    /* Blue-grey, unmarked, green eyes. */
    [CAT_BREED_RUSSIAN_BLUE] = {
        .coat = { 0.38, 0.44, 0.50 }, .rim  = { 0.68, 0.75, 0.82 },
        .eye  = { 0.55, 0.90, 0.55 }, .mark = { 0.38, 0.44, 0.50 },
        .marking = CAT_MARK_NONE },
    /* Near-black. The rim does all the work — without it the cat is a hole. */
    [CAT_BREED_BLACK] = {
        .coat = { 0.06, 0.06, 0.07 }, .rim  = { 0.42, 0.42, 0.48 },
        .eye  = { 1.00, 0.85, 0.20 }, .mark = { 0.06, 0.06, 0.07 },
        .marking = CAT_MARK_NONE },
};

/* ── Clipping to the silhouette ──────────────────────────────
 * Markings are drawn clipped to the body or the head so a stripe cannot spill
 * past the outline — at 64x48 that reads as a drawing mistake, not as fur.
 * The caller still needs the shape afterwards to stroke its outline, so the
 * path has to survive the clip.
 *
 * `cairo_clip_preserve` is the trap here, and it cost a rewrite: it keeps the
 * body ON THE CURRENT PATH, so the next `cairo_arc` APPENDS to it and the
 * following `cairo_fill` fills the body as well as the marking. The Siamese
 * and the calico both came out painted solid in their marking colour — a
 * uniformly brown cat and an orange one — which looks like badly chosen
 * colours rather than like a path bug, and that is what made it worth a
 * comment. cairo_save/restore does NOT save the path, so it is copied out and
 * appended back by hand. */
static cairo_path_t *cat_clip_begin(cairo_t *cr)
{
    cairo_path_t *keep = cairo_copy_path(cr);
    cairo_save(cr);
    cairo_clip(cr);          /* consumes the path — deliberately */
    cairo_new_path(cr);      /* …so markings start from nothing */
    return keep;
}

static void cat_clip_end(cairo_t *cr, cairo_path_t *keep)
{
    cairo_restore(cr);
    cairo_new_path(cr);
    cairo_append_path(cr, keep);
    cairo_path_destroy(keep);
}

/* Body markings, clipped to the body the caller has just filled. */
static void cat_mark_body(cairo_t *cr, const cat_coat_t *b,
                          double cx, double cy, double now)
{
    if (b->marking == CAT_MARK_NONE || b->marking == CAT_MARK_TUXEDO) return;

    cairo_path_t *keep = cat_clip_begin(cr);
    cairo_set_source_rgba(cr, b->mark[0], b->mark[1], b->mark[2], 0.85);

    if (b->marking == CAT_MARK_TABBY) {
        /* Three bars across the back, following the body's curve. */
        cairo_set_line_width(cr, 1.8);
        for (int i = 0; i < 3; i++) {
            double x = cx - 6.0 + i * 6.0;
            cairo_move_to(cr, x, cy - 11.0);
            cairo_curve_to(cr, x + 2.0, cy - 4.0, x + 2.0, cy + 2.0, x, cy + 8.0);
            cairo_stroke(cr);
        }
    } else if (b->marking == CAT_MARK_POINTS) {
        /* Colourpoint darkens the EXTREMITIES — the rump here, ears and mask
         * on the head. Drawn small and well off-centre on purpose: a wash over
         * the whole body just repaints the cat brown and the cream that makes
         * it read Siamese disappears. */
        cairo_arc(cr, cx - 12.0, cy + 2.0, 6.5, 0, 2 * M_PI);
        cairo_fill(cr);
    } else {   /* CAT_MARK_PATCHES */
        /* Fixed offsets, not random: the cat is redrawn every frame and a
         * blotch that resamples per frame is a cat that boils. */
        static const double blob[][3] = {
            { -8.0, -3.0, 3.8 }, { -1.0, 4.0, 3.0 },
            {  6.0, -4.0, 3.2 }, {  4.0,  4.5, 2.4 },
        };
        (void)now;
        for (size_t i = 0; i < sizeof blob / sizeof *blob; i++) {
            /* Alternate the two patch colours: tricolour is the whole point of
             * a calico, and the coat has to keep showing between them. */
            const double *c = (i % 2) ? b->mark2 : b->mark;
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.88);
            cairo_arc(cr, cx + blob[i][0], cy + blob[i][1], blob[i][2],
                      0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    cat_clip_end(cr, keep);
}

/* Neon-on-slate by default, to sit with the rest of synui's palette: dark body,
 * cyan rim, glowing eyes. Other coats come from cat_coats[] above. Drawn facing
 * RIGHT and mirrored in cat_render when facing left, so there is only ever one
 * pose to reason about.
 *
 * Takes a pose rather than the server: the kitty is the one part of synui whose
 * bug is "it doesn't look like a cat", which no assertion catches. Keeping the
 * drawing free of syn_server_t lets tests/cat_render_test.c render it straight
 * to a PNG and let a human judge it. See cat_pose_t in synui.h. */
void cat_paint(cairo_t *cr, const cat_pose_t *p)
{
    int breed = (p->breed >= 0 && p->breed < CAT_BREED_COUNT)
                  ? p->breed : CAT_BREED_NEON;
    const cat_coat_t *b = &cat_coats[breed];
    const double R = b->rim[0], G = b->rim[1], B = b->rim[2];
    const double CR_ = b->coat[0], CG_ = b->coat[1], CB_ = b->coat[2];
    int    st    = p->state;
    double phase = p->phase;
    double now   = p->now;
    bool   walk  = (st == CAT_WALK);
    bool   sleep = (st == CAT_SLEEP);

    double body_y = sleep ? 34.0 : (st == CAT_SIT ? 30.0 : 27.0);
    double bob    = walk ? sin(phase * 2.0) * 0.7 : 0.0;
    body_y += bob;

    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    /* ── Tail: a bezier that sways. Curls in tight when asleep. ── */
    double sway = sin(now * (walk ? 4.0 : 1.6)) * (walk ? 6.0 : 4.0);
    cairo_move_to(cr, 14, body_y + 1);
    if (sleep)
        cairo_curve_to(cr, 6, body_y + 6, 6, body_y + 12, 18, body_y + 10);
    else
        cairo_curve_to(cr, 4, body_y - 2 + sway,
                           6, body_y - 14 + sway,
                          16, body_y - 16 + sway * 0.6);
    cairo_set_source_rgba(cr, R, G, B, 0.85);
    cairo_stroke(cr);

    /* ── Legs. Two visible pairs; they only move while walking. ── */
    if (!sleep) {
        double foot = 42.0;
        for (int i = 0; i < 4; i++) {
            double lx = 20.0 + i * 6.5;
            /* Diagonal gait: front-left with rear-right. */
            double sw = walk ? sin(phase + (i % 2 ? M_PI : 0.0)) : 0.0;
            double fx = lx + sw * 2.6;
            double fy = foot - (walk ? fabs(sw) * 2.2 : 0.0);
            if (st == CAT_SIT && i < 2) continue;   /* haunches tucked under */

            cairo_move_to(cr, lx, body_y + 5);
            cairo_line_to(cr, fx, fy);
            cairo_set_source_rgba(cr, R * 0.8, G * 0.8, B * 0.8, 0.9);
            cairo_stroke(cr);
        }
    }

    /* ── Body ── */
    cairo_save(cr);
    cairo_translate(cr, 28, body_y);
    cairo_scale(cr, 1.0, st == CAT_SIT ? 0.86 : (sleep ? 0.62 : 0.72));
    cairo_arc(cr, 0, 0, 13, 0, 2 * M_PI);
    cairo_restore(cr);
    cairo_set_source_rgba(cr, CR_, CG_, CB_, 0.92);
    cairo_fill_preserve(cr);
    /* Markings go on while the body is still the current path, so they are
     * clipped to it; cat_mark_body preserves the path for the outline below. */
    cat_mark_body(cr, b, 28.0, body_y, now);
    cairo_set_source_rgba(cr, R, G, B, 0.9);
    cairo_stroke(cr);

    /* Tuxedo's bib: a white wedge at the chest, over the coat but under the
     * outline, and only where there IS a chest — a sleeping cat is curled up
     * and the bib would float free of the body. */
    if (b->marking == CAT_MARK_TUXEDO && !sleep) {
        cairo_save(cr);
        cairo_set_source_rgba(cr, b->mark[0], b->mark[1], b->mark[2], 0.9);
        cairo_move_to(cr, 34.0, body_y - 4.0);
        cairo_curve_to(cr, 39.0, body_y - 1.0, 39.0, body_y + 4.0,
                           34.0, body_y + 7.0);
        cairo_curve_to(cr, 31.0, body_y + 3.0, 31.0, body_y, 34.0, body_y - 4.0);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    /* ── Head ── */
    double hx = sleep ? 34.0 : 43.0;
    double hy = body_y - (sleep ? 1.0 : 8.0);
    hy += walk ? sin(phase * 2.0 + 0.6) * 0.5 : 0.0;

    /* Ears: twitch every so often, independently of everything else. */
    double tw = sin(now * 1.3) > 0.97 ? 1.6 : 0.0;
    cairo_move_to(cr, hx - 6.5, hy - 4.5);
    cairo_line_to(cr, hx - 7.5, hy - 11.5 - tw);
    cairo_line_to(cr, hx - 1.5, hy - 7.5);
    cairo_close_path(cr);
    cairo_move_to(cr, hx + 2.0, hy - 7.5);
    cairo_line_to(cr, hx + 7.5, hy - 11.5 + tw);
    cairo_line_to(cr, hx + 7.0, hy - 4.0);
    cairo_close_path(cr);
    /* Colourpoint ears are the marking colour outright rather than a wash —
     * on a Siamese the ears are the darkest thing on the cat. */
    if (b->marking == CAT_MARK_POINTS)
        cairo_set_source_rgba(cr, b->mark[0], b->mark[1], b->mark[2], 0.92);
    else
        cairo_set_source_rgba(cr, CR_, CG_, CB_, 0.92);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, R, G, B, 0.9);
    cairo_stroke(cr);

    cairo_arc(cr, hx, hy, 8.0, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, CR_, CG_, CB_, 0.95);
    cairo_fill_preserve(cr);
    /* The face carries the same markings as the coat: a tabby's forehead M,
     * a colourpoint's mask. Clipped to the head for the same reason. */
    if (b->marking == CAT_MARK_TABBY) {
        cairo_path_t *hk = cat_clip_begin(cr);
        cairo_set_source_rgba(cr, b->mark[0], b->mark[1], b->mark[2], 0.85);
        cairo_set_line_width(cr, 1.1);
        for (int i = 0; i < 3; i++) {
            double mx = hx - 3.4 + i * 3.4;
            cairo_move_to(cr, mx, hy - 7.4);
            cairo_line_to(cr, mx + 0.9, hy - 4.4);
            cairo_stroke(cr);
        }
        cat_clip_end(cr, hk);
    } else if (b->marking == CAT_MARK_POINTS) {
        cairo_path_t *hk = cat_clip_begin(cr);
        cairo_set_source_rgba(cr, b->mark[0], b->mark[1], b->mark[2], 0.7);
        cairo_arc(cr, hx + 3.5, hy + 3.2, 4.4, 0, 2 * M_PI);
        cairo_fill(cr);
        cat_clip_end(cr, hk);
    } else if (b->marking == CAT_MARK_TUXEDO) {
        cairo_path_t *hk = cat_clip_begin(cr);
        cairo_set_source_rgba(cr, b->mark[0], b->mark[1], b->mark[2], 0.9);
        cairo_arc(cr, hx + 2.0, hy + 4.2, 3.2, 0, 2 * M_PI);
        cairo_fill(cr);
        cat_clip_end(cr, hk);
    }
    cairo_set_source_rgba(cr, R, G, B, 0.95);
    cairo_stroke(cr);

    /* ── Face ── */
    bool eyes_shut = sleep || p->blinking;
    if (eyes_shut) {
        cairo_set_source_rgba(cr, R, G, B, 0.8);
        cairo_move_to(cr, hx - 4.5, hy - 1.0); cairo_line_to(cr, hx - 1.5, hy - 1.0);
        cairo_move_to(cr, hx + 2.0, hy - 1.0); cairo_line_to(cr, hx + 5.0, hy - 1.0);
        cairo_stroke(cr);
    } else {
        /* Glowing eyes: a soft halo under a bright core. */
        for (int i = 0; i < 2; i++) {
            double ex = hx + (i ? 3.5 : -3.0);
            cairo_arc(cr, ex, hy - 1.0, 2.6, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, R, G, B, 0.22);
            cairo_fill(cr);
            cairo_arc(cr, ex, hy - 1.0, 1.3, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, b->eye[0], b->eye[1], b->eye[2], 0.95);
            cairo_fill(cr);
        }
    }

    /* Muzzle + whiskers */
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, R, G, B, 0.75);
    cairo_move_to(cr, hx + 0.5, hy + 3.0);
    cairo_line_to(cr, hx + 2.0, hy + 4.6);
    cairo_stroke(cr);
    for (int i = 0; i < 2; i++) {
        double wy = hy + 2.5 + i * 2.4;
        cairo_move_to(cr, hx + 4.0, wy);
        cairo_line_to(cr, hx + 12.0, wy - 1.5 + i * 2.0);
        cairo_stroke(cr);
    }

    /* Sleeping: a couple of drifting z's. */
    if (sleep) {
        cairo_select_font_face(cr, "monospace",
                               CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_BOLD);
        for (int i = 0; i < 2; i++) {
            double t  = now * 0.6 + i * 0.5;
            double zy = hy - 12.0 - fmod(t, 1.0) * 10.0;
            double a  = 0.7 * (1.0 - fmod(t, 1.0));
            cairo_set_font_size(cr, 7.0 + i * 2.0);
            cairo_set_source_rgba(cr, R, G, B, a);
            cairo_move_to(cr, hx + 8.0 + i * 4.0, zy);
            syn_show_text(cr, "z");
        }
    }
}

