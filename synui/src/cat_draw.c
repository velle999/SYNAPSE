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

/* Neon-on-slate, to sit with the rest of synui's palette: dark body, cyan rim,
 * glowing eyes. Drawn facing RIGHT and mirrored in cat_render when facing left,
 * so there is only ever one pose to reason about.
 *
 * Takes a pose rather than the server: the kitty is the one part of synui whose
 * bug is "it doesn't look like a cat", which no assertion catches. Keeping the
 * drawing free of syn_server_t lets tests/cat_render_test.c render it straight
 * to a PNG and let a human judge it. See cat_pose_t in synui.h. */
void cat_paint(cairo_t *cr, const cat_pose_t *p)
{
    const double R = 0.60, G = 0.95, B = 0.90;   /* cyan accent */
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
    cairo_set_source_rgba(cr, 0.07, 0.09, 0.14, 0.92);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, R, G, B, 0.9);
    cairo_stroke(cr);

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
    cairo_set_source_rgba(cr, 0.07, 0.09, 0.14, 0.92);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, R, G, B, 0.9);
    cairo_stroke(cr);

    cairo_arc(cr, hx, hy, 8.0, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.07, 0.09, 0.14, 0.95);
    cairo_fill_preserve(cr);
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
            cairo_set_source_rgba(cr, 0.85, 1.0, 0.98, 0.95);
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
            cairo_show_text(cr, "z");
        }
    }
}

