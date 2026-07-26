/*
 * deco.c — server-side window decorations
 *
 * synui tells clients SERVER_SIDE via xdg-decoration, so it owes them chrome:
 * a border and a titlebar with the three buttons everyone's fingers already
 * know.  [ _ ] minimize  [ □ ] maximize  [ × ] close
 *
 * Frame model
 * -----------
 * Every managed toplevel gets a `frame` scene tree; the client's surface tree
 * and all chrome are children of it. That means one node to enable, hide, raise
 * and move — decorations can't be left behind by a workspace switch or end up
 * z-ordered over a window that was raised above them. (They used to be siblings
 * in window_tree, and both of those bugs were real: a hidden window's borders
 * stayed painted on screen.)
 *
 * The frame node sits at (view->x, view->y), so everything below is laid out in
 * frame-local coordinates and only the frame moves when the window does:
 *
 *   frame  (view->x, view->y, view->w, view->h)        the window's outer rect
 *   ├── borders    bw wide, around the edge
 *   ├── titlebar   (bw, bw) → (w - 2bw) × th
 *   └── client     (bw, bw + th) → (w - 2bw) × (h - 2bw - th)
 *
 * view->x/y/w/h is always the *frame*; view_content_box() is what the client
 * actually gets. Fullscreen drops both border and titlebar, so frame == content.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <scenefx/types/wlr_scene.h>
#include <wlr/util/edges.h>

#include "synui.h"

#define BTN_COUNT     3
#define CORNER_GRAB  24   /* px from a corner that resizes both edges at once */

/* Width of the invisible resize-grab ring outside the window (see the grab_*
 * rects in syn_view). The visible border is border_width px — 2 by default —
 * which is not a click target a hand can hit, so the real grab band is
 * border_width + GRAB_RING, and the corners get an L of that thickness
 * extending CORNER_GRAB along both edges. */
#define GRAB_RING     8

/* ── Frame ───────────────────────────────────────────────── */
struct wlr_scene_node *view_node(syn_view_t *view)
{
    return view->frame ? &view->frame->node : &view->scene_tree->node;
}

struct wlr_scene_tree *view_frame_create(syn_view_t *view,
                                         struct wlr_scene_tree *parent)
{
    view->frame = wlr_scene_tree_create(parent);
    view->alpha = 1.0f;      /* calloc'd to 0 = fully transparent otherwise */
    view->base_opacity = 1.0f;   /* opaque until the transparency setting bites */
    /* surface_at() walks up from the node it hit until it finds one carrying a
     * view — the titlebar buffer is a direct child of the frame, so the frame
     * has to carry it too. */
    view->frame->node.data = view;
    return view->frame;
}

/* ── Decoration metrics ──────────────────────────────────── */
int view_deco_border(const syn_view_t *view)
{
    if (view->fullscreen) return 0;   /* would poke out past the output */
    return view->server->config.border_width;
}

int view_deco_titlebar(const syn_view_t *view)
{
    if (view->fullscreen || view->override_redirect) return 0;
    if (!view->frame) return 0;
    if (view->server->titlebars_hidden) return 0;  /* decorations_toggle */
    return view->server->config.titlebar_height;   /* 0 = disabled in synuirc */
}

void view_content_box(const syn_view_t *view, struct wlr_box *out)
{
    int bw = view_deco_border(view);
    int th = view_deco_titlebar(view);

    out->x      = view->x + bw;
    out->y      = view->y + bw + th;
    out->width  = view->w - 2 * bw;
    out->height = view->h - 2 * bw - th;

    /* Never hand a client (or a scene rect) a non-positive size. */
    if (out->width  < 1) out->width  = 1;
    if (out->height < 1) out->height = 1;
}

/* ── Titlebar painting ───────────────────────────────────── */
/* Buttons are square and titlebar-height, right-aligned in that order:
 * minimize, maximize, close. Returns the x of button `i`'s left edge. */
static int btn_x(int tb_w, int th, int i)
{
    return tb_w - (BTN_COUNT - i) * th;
}

/* ── Retro chrome helpers ─────────────────────────────────
 * The retro themes are not just a different set of colours: XP's caption is a
 * vertical gradient with rounded top corners and pill buttons, and 95's is a
 * flat bar inside a raised 3D bevel with square bevelled buttons. Colours alone
 * never read as either. All of this is confined to the titlebar surface, which
 * is cairo (the borders around it are flat scene rects and stay flat). */

/* Mix two colours; t=0 is a, t=1 is b. */
static void mix(const float a[4], const float b[4], double t, double out[3])
{
    for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t;
}

static void rounded_top(cairo_t *cr, double w, double h, double r)
{
    cairo_new_path(cr);
    cairo_arc(cr, r,     r,     r, M_PI,        1.5 * M_PI);
    cairo_arc(cr, w - r, r,     r, 1.5 * M_PI,  2.0 * M_PI);
    cairo_line_to(cr, w, h);
    cairo_line_to(cr, 0, h);
    cairo_close_path(cr);
}

/* Luna's caption is not a two-stop ramp: it is bright near the top, dips to the
 * saturated base through the middle and lifts again at the bottom edge. Four
 * stops off the theme's two registry colours (ActiveTitle + GradientActiveTitle)
 * get within a hair of the real thing. */
static void luna_caption(cairo_t *cr, double w, double h,
                         const float base[4], const float grad[4])
{
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, 0, h);
    double c[3];
    mix(base, grad, 0.45, c);
    cairo_pattern_add_color_stop_rgba(p, 0.00, c[0], c[1], c[2], base[3]);
    cairo_pattern_add_color_stop_rgba(p, 0.14, grad[0], grad[1], grad[2], base[3]);
    cairo_pattern_add_color_stop_rgba(p, 0.55, base[0], base[1], base[2], base[3]);
    mix(base, grad, 0.28, c);
    cairo_pattern_add_color_stop_rgba(p, 1.00, c[0], c[1], c[2], base[3]);

    rounded_top(cr, w, h, 6);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);

    /* The pale inner highlight that runs just inside the rounded top edge. */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 7,     1.5);
    cairo_line_to(cr, w - 7, 1.5);
    cairo_stroke(cr);
}

/* A Win95 bevel, drawn with the real system colours: the outer ring is
 * ButtonHilight #FFFFFF against ButtonDkShadow #000000, the inner ring
 * ButtonLight #DFDFDF against ButtonShadow #808080. `raised` lights the top and
 * left (a button, a frame); sunken swaps the pairs (a pressed button, a well). */
static void bevel(cairo_t *cr, double x, double y, double w, double h, int raised)
{
    static const double ring[2][2] = {   /* { light, dark } per ring */
        { 1.000, 0.000 },                /* outer: #FFFFFF / #000000 */
        { 0.875, 0.502 },                /* inner: #DFDFDF / #808080 */
    };
    cairo_set_line_width(cr, 1);
    for (int i = 0; i < 2; i++) {
        double o  = (double)i;
        double tl = raised ? ring[i][0] : ring[i][1];
        double br = raised ? ring[i][1] : ring[i][0];

        cairo_set_source_rgba(cr, tl, tl, tl, 1);
        cairo_move_to(cr, x + o + 0.5, y + h - o);
        cairo_line_to(cr, x + o + 0.5, y + o + 0.5);
        cairo_line_to(cr, x + w - o,   y + o + 0.5);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, br, br, br, 1);
        cairo_move_to(cr, x + w - o - 0.5, y + o);
        cairo_line_to(cr, x + w - o - 0.5, y + h - o - 0.5);
        cairo_line_to(cr, x + o,           y + h - o - 0.5);
        cairo_stroke(cr);
    }
}

static void draw_btn_glyph(cairo_t *cr, syn_deco_region_t which,
                           double cx, double cy, double s, const float col[4])
{
    cairo_set_source_rgba(cr, col[0], col[1], col[2], col[3]);
    cairo_set_line_width(cr, 1.4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    switch (which) {
    case DECO_BTN_MIN:      /* an underscore */
        cairo_move_to(cr, cx - s, cy + s);
        cairo_line_to(cr, cx + s, cy + s);
        cairo_stroke(cr);
        break;
    case DECO_BTN_MAX:      /* a hollow square */
        cairo_rectangle(cr, cx - s, cy - s, 2 * s, 2 * s);
        cairo_stroke(cr);
        break;
    case DECO_BTN_CLOSE:    /* an X */
        cairo_move_to(cr, cx - s, cy - s);
        cairo_line_to(cr, cx + s, cy + s);
        cairo_move_to(cr, cx + s, cy - s);
        cairo_line_to(cr, cx - s, cy + s);
        cairo_stroke(cr);
        break;
    default:
        break;
    }
}

/* Drop the "nothing changed, keep the surface" cache below, so the next
 * view_update_decorations really repaints. Needed whenever something the
 * titlebar draws with — rather than something it draws — has changed: a theme
 * switch moves the caption colours AND the chrome style, and neither is in the
 * cache key. Without this a theme change left every open window wearing its old
 * caption until it was resized, refocused or retitled. */
void view_invalidate_titlebar(syn_view_t *view)
{
    if (view) view->tb_title[0] = '\0';
}

static void titlebar_render(syn_view_t *view)
{
    syn_server_t *s   = view->server;
    syn_config_t *cfg = &s->config;

    int th = view_deco_titlebar(view);
    int bw = view_deco_border(view);
    int w  = view->w - 2 * bw;

    if (th <= 0 || w <= 0) {              /* disabled, or fullscreen */
        if (view->titlebar)
            wlr_scene_node_set_enabled(&view->titlebar->node, false);
        return;
    }

    const char *title = view_title(view);
    if (!title) title = "";
    int focused = (view == s->focused_view);

    /* Repainting a cairo surface every motion event would be absurd — only
     * redraw when something the titlebar actually shows has changed. */
    if (view->titlebar &&
        view->tb_w == w && view->tb_h == th &&
        view->tb_focused == focused &&
        strncmp(view->tb_title, title, sizeof(view->tb_title) - 1) == 0) {
        wlr_scene_node_set_enabled(&view->titlebar->node, true);
        wlr_scene_node_set_position(&view->titlebar->node, bw, bw);
        return;
    }

    cairo_t *cr = NULL;
    struct wlr_buffer *buf = create_cairo_buf(w, th, &cr);
    if (!buf) return;
    cairo_begin(cr);

    const float *bg   = focused ? cfg->titlebar_color_focus : cfg->titlebar_color;
    const float *grad = focused ? cfg->titlebar_grad_focus  : cfg->titlebar_grad;
    const float *fg   = focused ? cfg->titlebar_text_focus  : cfg->titlebar_text;
    const float *face = cfg->chrome_face;

    /* text_x/text_right bound the caption. Both retro styles inset it: 95 by the
     * frame bevel, XP by its rounded corner. */
    double text_x     = 8;
    int    text_right = btn_x(w, th, 0) - 6;

    switch (cfg->chrome) {
    case SYN_CHROME_LUNA:
        luna_caption(cr, w, th, bg, grad);
        text_x = 10;
        break;
    case SYN_CHROME_BEVEL:
        /* The raised silver frame, then the flat caption bar inset inside it —
         * in 95 the caption never touches the window edge. */
        cairo_set_source_rgba(cr, face[0], face[1], face[2], face[3]);
        cairo_paint(cr);
        bevel(cr, 0, 0, w, th + 2, 1);   /* +2: the bevel continues past the
                                          * caption into the client area */
        cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
        cairo_rectangle(cr, 3, 3, w - 6, th - 6);
        cairo_fill(cr);
        text_x     = 7;
        text_right = btn_x(w, th, 0) - 4;
        break;
    case SYN_CHROME_FLAT:
    default:
        cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
        cairo_paint(cr);
        break;
    }

    /* Hover feedback. Flat chrome lights the whole cell behind the glyph (close
     * goes alarm red, so the destructive one is never hit by accident); the retro
     * styles instead brighten their own button shape below, because a full-cell
     * wash would paint straight over an XP pill or a 95 bevel. */
    if (cfg->chrome == SYN_CHROME_FLAT &&
        (view->tb_hover == DECO_BTN_MIN || view->tb_hover == DECO_BTN_MAX ||
         view->tb_hover == DECO_BTN_CLOSE)) {
        int i = (view->tb_hover == DECO_BTN_MIN) ? 0
              : (view->tb_hover == DECO_BTN_MAX) ? 1 : 2;
        const float *hl = (view->tb_hover == DECO_BTN_CLOSE)
                              ? cfg->border_color_warn
                              : cfg->border_color_norm;
        cairo_set_source_rgba(cr, hl[0], hl[1], hl[2], 1.0);
        cairo_rectangle(cr, btn_x(w, th, i), 0, th, th);
        cairo_fill(cr);
    }

    /* Title, vertically centred, clipped to where the buttons start. Both retro
     * captions were bold — that weight is a surprising amount of the likeness. */
    if (text_right > text_x && title[0]) {
        cairo_save(cr);
        cairo_rectangle(cr, text_x, 0, text_right - text_x, th);
        cairo_clip(cr);

        if (cfg->chrome != SYN_CHROME_FLAT)
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, th * 0.46);
        cairo_font_extents_t fe;
        cairo_font_extents(cr, &fe);
        double ty = (th + fe.ascent - fe.descent) / 2.0;

        /* XP's caption text carries a soft drop shadow; without it white-on-blue
         * looks pasted on rather than part of the bar. */
        if (cfg->chrome == SYN_CHROME_LUNA) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
            cairo_move_to(cr, text_x + 1, ty + 1);
            cairo_show_text(cr, title);
        }
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
        cairo_move_to(cr, text_x, ty);
        cairo_show_text(cr, title);
        cairo_restore(cr);
    }

    /* The three buttons. Flat chrome draws bare glyphs (the cell behind them is
     * the hover wash above); the retro styles draw a real button under each. */
    double cy = th / 2.0;
    double gs = th * 0.18;
    static const syn_deco_region_t order[BTN_COUNT] = {
        DECO_BTN_MIN, DECO_BTN_MAX, DECO_BTN_CLOSE
    };
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    for (int i = 0; i < BTN_COUNT; i++) {
        double bx      = btn_x(w, th, i);
        int    hovered = (view->tb_hover == order[i]);
        const float *gc = fg;

        if (cfg->chrome == SYN_CHROME_LUNA) {
            /* XP's pills: close is the red one, always visible; the other two are
             * a lighter wash of the caption. All three sit inset from the edges,
             * squarer than a circle — a rounded rect with a big radius. */
            double bs = th * 0.72, by = (th - bs) / 2.0;
            double px = bx + (th - bs) / 2.0;
            double r  = bs * 0.28;
            static const float xp_close[4]  = { 0.906f, 0.220f, 0.153f, 1.0f };
            static const float xp_close_h[4] = { 0.965f, 0.400f, 0.310f, 1.0f };
            double col[3];
            if (order[i] == DECO_BTN_CLOSE) {
                const float *c = hovered ? xp_close_h : xp_close;
                col[0] = c[0]; col[1] = c[1]; col[2] = c[2];
            } else {
                mix(bg, white, hovered ? 0.45 : 0.28, col);
            }
            cairo_new_path(cr);
            cairo_arc(cr, px + r,      by + r,      r, M_PI, 1.5 * M_PI);
            cairo_arc(cr, px + bs - r, by + r,      r, 1.5 * M_PI, 2.0 * M_PI);
            cairo_arc(cr, px + bs - r, by + bs - r, r, 0.0, 0.5 * M_PI);
            cairo_arc(cr, px + r,      by + bs - r, r, 0.5 * M_PI, M_PI);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, col[0], col[1], col[2], 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.55);   /* the pill's rim */
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            gc = white;
        } else if (cfg->chrome == SYN_CHROME_BEVEL) {
            /* 95's buttons: square silver, raised — pressed-looking (sunken) while
             * hovered, which is as close as we get to the real press state. The
             * glyphs go black, as they were. */
            double bs = th - 8, by = 4;
            double px = bx + (th - bs) / 2.0;
            cairo_set_source_rgba(cr, face[0], face[1], face[2], face[3]);
            cairo_rectangle(cr, px, by, bs, bs);
            cairo_fill(cr);
            bevel(cr, px, by, bs, bs, !hovered);
            gc = black;
        } else if (order[i] == DECO_BTN_CLOSE && hovered) {
            gc = white;   /* reads on the alarm-red wash */
        }

        draw_btn_glyph(cr, order[i], bx + th / 2.0, cy, gs, gc);
    }

    cairo_destroy(cr);
    set_scene_buffer(&view->titlebar, view->frame, buf);
    wlr_scene_node_set_enabled(&view->titlebar->node, true);
    wlr_scene_node_set_position(&view->titlebar->node, bw, bw);

    view->tb_w = w;
    view->tb_h = th;
    view->tb_focused = focused;
    snprintf(view->tb_title, sizeof(view->tb_title), "%s", title);
}

/* ── Cropping a client's CSD shadow margin ───────────────────
 *
 * An xdg client may paint outside the window it reports through
 * set_window_geometry; the classic use is an invisible margin holding a
 * client-drawn drop shadow. Under xdg-decoration that never happens, because a
 * client taking SERVER_SIDE stops drawing its own frame — but only a client
 * that *binds* the protocol is bound by it, and Firefox does not bind it at
 * all. So its GTK shadow lands outside synui's border, on top of synui's own
 * shadow: a second ring, wider (its margin is bigger than our blur reaches)
 * and square-cornered (GTK clips it to the rectangular margin), around exactly
 * one app. Measured on velle's desktop at pkgrel 182: 37 px of client shadow to
 * the left of the frame against Dolphin's 32 px of synui shadow, with a hard
 * step back to the wallpaper at its outer edge instead of a gaussian tail.
 *
 * Cropping to the geometry box drops those pixels, so every window's ring is
 * the one this file draws. It also takes the margin's input region off the grab
 * ring — the reason Firefox could not be resized by its edges (see
 * xdg_toplevel_request_resize in synui_main.c).
 *
 * The clip is in root-surface coordinates, which is exactly what
 * wlr_xdg_surface.geometry is. Both setters early-return when nothing changed,
 * so this is cheap enough to run from the per-commit decoration update — and it
 * has to, because a client can move its geometry on any commit.
 */
void view_clip_csd_margin(syn_view_t *view)
{
    /* X11 has no window geometry to clip to, and no CSD margin either. */
    if (!view->client_tree || !view->xdg_surface) return;

    struct wlr_box geo = view->xdg_surface->geometry;
    struct wlr_box ext = {0};
    wlr_surface_get_extents(view->xdg_surface->surface, &ext);

    /* Nothing to crop unless the client paints outside the window it declared.
     * Almost no one does — measured in the nested rig, foot reports geometry
     * 1260x637 against extents of exactly 1260x637, while Firefox reports
     * 896x490 at (26,23) inside a 948x542 surface: 26 px of margin on three
     * sides and 29 on the bottom. Skipping the no-op case matters because a
     * clip is not free even when it covers everything: it puts a source box and
     * a destination size on the scene buffer, and that alone shifted the
     * blurred backdrop around foot by ~7/255. Every SSD client therefore keeps
     * the exact pixels it had before this existed. */
    bool inside = ext.x >= geo.x && ext.y >= geo.y &&
                  ext.x + ext.width  <= geo.x + geo.width &&
                  ext.y + ext.height <= geo.y + geo.height;

    if (!view->server->config.clip_csd_margin || inside ||
        geo.width <= 0 || geo.height <= 0) {
        /* NULL clears it. Free when there was none: wlr_box_equal() treats an
         * empty box and NULL as the same thing and returns early. */
        wlr_scene_subsurface_tree_set_clip(&view->client_tree->node, NULL);
        return;
    }

    wlr_scene_subsurface_tree_set_clip(&view->client_tree->node, &geo);
}

/* ── Borders + titlebar ──────────────────────────────────── */
void view_update_decorations(syn_view_t *view)
{
    /* Before the early returns below: a fullscreen window has no chrome but
     * still has a margin to crop, and an unsized one still has to be able to
     * drop the clip when the setting goes off. */
    view_clip_csd_margin(view);

    if (!view->mapped || !view->frame) return;
    /* No sane geometry yet (e.g. focused at map before the first layout) — the
     * chrome is (re)created once the window is sized. */
    if (view->w <= 0 || view->h <= 0) return;

    int bw = view_deco_border(view);

    /* Fullscreen covers the whole output: no border, no titlebar. */
    if (bw == 0) {
        if (view->border)   wlr_scene_node_set_enabled(&view->border->node,   false);
        if (view->titlebar) wlr_scene_node_set_enabled(&view->titlebar->node, false);
        view_grab_ring_update(view);
        view_shadow_update(view);   /* disables it — fullscreen has no shadow */
        view_halo_update(view);     /* likewise: no desktop beside it to blur */
        return;
    }

    /* Pick border color (config-driven; defaults COLOR_BORDER_*) */
    float color[4];
    syn_config_t *cfg = &view->server->config;

    if (view->security == WIN_SECURE_ALERT ||
        view->security == WIN_SECURE_DENIED)
        memcpy(color, cfg->border_color_warn, sizeof(color));
    else if (view->ai_ctx.has_ctx)
        memcpy(color, cfg->border_color_ai, sizeof(color));
    else if (view == view->server->focused_view)
        memcpy(color, cfg->border_color_focus, sizeof(color));
    else
        memcpy(color, cfg->border_color_norm, sizeof(color));

    /* A fading window has to fade *whole*: scene rects carry their alpha in the
     * colour (unlike buffers, which have set_opacity), so fold it in here. The
     * same effective alpha the buffers get — fade × focus-driven translucency —
     * or a translucent window would show solid borders around a see-through body. */
    float eff = view->alpha * anim_view_opacity(view);
    color[3] *= (eff > 0.0f ? eff : 0.0f);

    /* Frame-local: the frame node already sits at (view->x, view->y). */
    int w = view->w, h = view->h;

    /*
     * The border is one rect covering the frame with the content area clipped
     * out, leaving a ring exactly border_width thick. Concentric radii: the
     * outer edge is inset from the content by bw on every side, so an inner
     * radius of corner_radius makes the outer one corner_radius + bw and the
     * ring keeps a constant thickness around the curve. corner_radius 0 gives
     * a square ring — the old four-strip look, unchanged.
     */
    int radius = chrome_corner_radius(&view->server->config);
    int iw = w - 2 * bw, ih = h - 2 * bw;   /* the clipped-out content box */
    if (iw < 0) iw = 0;                     /* a window thinner than its own */
    if (ih < 0) ih = 0;                     /* border: degenerate, not negative */
    if (radius > iw / 2) radius = iw / 2;   /* scenefx clamps, but keep the two */
    if (radius > ih / 2) radius = ih / 2;   /* radii consistent with each other */
    if (radius < 0) radius = 0;

    if (!view->border)
        view->border = wlr_scene_rect_create(view->frame, w, h, color);
    else {
        wlr_scene_rect_set_color(view->border, color);
        wlr_scene_rect_set_size(view->border, w, h);
    }
    if (view->border) {
        /* scenefx 0.5 fused "how round" and "which corners" into one
         * fx_corner_radii, so the radius is no longer a separate argument. */
        wlr_scene_rect_set_corner_radius(view->border, radius + bw);
        struct clipped_region ring = {
            .area    = { .x = bw, .y = bw, .width = iw, .height = ih },
            .corners = corner_radii_all(radius),
        };
        wlr_scene_rect_set_clipped_region(view->border, ring);
        wlr_scene_node_set_enabled(&view->border->node, true);
        wlr_scene_node_set_position(&view->border->node, 0, 0);
        /*
         * Below the client, not above it. The clip is a *render*-time cutout —
         * wlr_scene_node_at() still reports a hit anywhere in the rect's box, so
         * a full-frame border left on top of the client would swallow every
         * click on the window and answer it as DECO_BORDER (resize). Lowering
         * costs nothing visually: the ring lies outside the content anyway.
         * view_shadow_update() lowers the shadow afterwards, which is what keeps
         * the shadow underneath this.
         */
        wlr_scene_node_lower_to_bottom(&view->border->node);
    }

    view_grab_ring_update(view);
    view_shadow_update(view);
    titlebar_render(view);
    /* Last, and in this order: the border and then the shadow each lower
     * themselves to the bottom of the frame, so the halo has to be lowered after
     * both of them to end up underneath. See view_halo_update(). */
    view_halo_update(view);
}

/* ── Runtime titlebar toggle ─────────────────────────────── */
/*
 * Re-apply each mapped view's geometry to the frame box it already has. The
 * tiling is untouched on purpose: layout_tile lays out *frames*, and the
 * titlebar lives inside the frame, so no frame moves when the titlebar comes
 * and goes. What does change is view_content_box(), and only view_resize()
 * pushes that to the client (configure + the client subtree's offset inside the
 * frame). Floating windows are the ones that prove it: layout_apply skips them,
 * so a bare view_update_decorations() loop would repaint their chrome and leave
 * the client itself sized for the titlebar that is no longer there.
 */
void deco_refresh_all(syn_server_t *s)
{
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            /* Not sized yet: it gets the current metrics at its first layout. */
            if (!v->mapped || v->w <= 0 || v->h <= 0) continue;
            view_resize(v, v->x, v->y, v->w, v->h);
        }
    }
}

void deco_toggle_titlebars(syn_server_t *s)
{
    s->titlebars_hidden = !s->titlebars_hidden;
    deco_refresh_all(s);
    deco_state_save(s);
    wlr_log(WLR_INFO, "synui: titlebars %s",
            s->titlebars_hidden ? "hidden" : "shown");
}

/* ── Persisted state (~/.config/synui/deco.state) ─────────── */
/*
 * The toggle stays *server* state rather than moving into syn_config_t, so a
 * config reload still can't undo it (see synui.h) — it just now survives a
 * logout as well. Its own state file, alongside wallpaper/dock/power/filters,
 * keeps it out of synuirc, where `titlebar_height = 0` remains the permanent
 * "I never want titlebars" answer.
 */
static bool deco_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "deco.state");
}

void deco_state_save(syn_server_t *s)
{
    char path[256];
    if (!deco_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: deco: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    fprintf(f, "titlebars_hidden=%d\n", s->titlebars_hidden ? 1 : 0);
    fclose(f);
}

void deco_state_load(syn_server_t *s)
{
    char path[256];
    if (!deco_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted choice — titlebars start shown */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, "titlebars_hidden") == 0)
            s->titlebars_hidden = atoi(eq + 1) != 0;
    }
    fclose(f);
}

/* ── Invisible resize-grab ring ──────────────────────────── */
/*
 * Four transparent rects hugging the outside of the frame, overhanging at the
 * corners so each corner is a GRAB_RING-thick L rather than a 2px dot. They are
 * pure hit-testing: wlr_scene_node_at() accepts a WLR_SCENE_NODE_RECT on its
 * bounds alone (only scene *buffers* get a point_accepts_input veto), so a rect
 * with alpha 0 is a click target that paints nothing.
 *
 * They sit strictly outside the window, never over the client, so they can't eat
 * a click meant for a scrollbar sitting flush against the window edge. They do
 * overlay ~8px of whatever is behind the window — the same trade every CSD
 * toolkit makes with its shadow region.
 */
void view_grab_ring_update(syn_view_t *view)
{
    if (!view->frame) return;

    /* Nothing to resize: a fullscreen or maximized window is not draggable by
     * its edges, and the ring would just be an 8px dead zone over its
     * neighbours (or over the dock, at the screen edge). */
    bool want = view->mapped && !view->fullscreen && !view->maximized &&
                view->w > 0 && view->h > 0;

    static const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int m = GRAB_RING, w = view->w, h = view->h;

    #define MAKE_GRAB(field, gx, gy, gw, gh) do { \
        if (!want) { \
            if (view->field) \
                wlr_scene_node_set_enabled(&view->field->node, false); \
            break; \
        } \
        if (!view->field) \
            view->field = wlr_scene_rect_create(view->frame, gw, gh, clear); \
        else \
            wlr_scene_rect_set_size(view->field, gw, gh); \
        wlr_scene_node_set_enabled(&view->field->node, true); \
        wlr_scene_node_set_position(&view->field->node, gx, gy); \
        wlr_scene_node_lower_to_bottom(&view->field->node); \
    } while (0)

    MAKE_GRAB(grab_top,    -m, -m, w + 2 * m, m);
    MAKE_GRAB(grab_bottom, -m,  h, w + 2 * m, m);
    MAKE_GRAB(grab_left,   -m,  0, m, h);
    MAKE_GRAB(grab_right,   w,  0, m, h);

    #undef MAKE_GRAB
}

/* ── Drop shadow ─────────────────────────────────────────────
 *
 * One wlr_scene_shadow node per frame, lowered to the bottom so it renders
 * behind the client and chrome. scenefx's box-shadow shader (box_shadow.frag)
 * insets the *solid* shadow rect by blur_sigma on every side and clips the blur
 * to the node's box, so to make the shadow's solid rect coincide with the window
 * the node is grown 2·sigma and origined at -sigma; shadow_offset_{x,y} then bias
 * the drop direction. The window's own rounded rect is subtracted via the node's
 * clipped_region, leaving a soft outer ring — without that the solid centre would
 * bleed through a translucent glass body and darken it.
 */
void view_shadow_update(syn_view_t *view)
{
    if (!view->frame) return;   /* override-redirect: no frame, no shadow */

    syn_config_t *cfg = &view->server->config;

    /* Off, unmapped, or edge-to-edge: a maximized/fullscreen window's shadow is
     * clipped to nothing and would only burn fill, so disable rather than draw. */
    bool want = chrome_shadow(cfg) && view->mapped &&
                !view->fullscreen && !view->maximized &&
                view->w > 0 && view->h > 0;
    if (!want) {
        if (view->shadow)
            wlr_scene_node_set_enabled(&view->shadow->node, false);
        return;
    }

    int   sigma  = (int)lroundf(cfg->shadow_blur_sigma);
    if (sigma < 1) sigma = 1;
    int   spread = (int)lroundf(cfg->shadow_spread);
    if (spread < 0) spread = 0;
    int   radius = chrome_corner_radius(cfg);   /* match the glass corners */
    int   w = view->w, h = view->h;

    /* Growing the node by MORE than sigma is what produces a spread: the shader
     * insets its solid rect by sigma from the node box either way, so at
     * sigma+S the solid rect lands S px outside the window on every side and
     * the gaussian starts from there. At S = 0 this is the old geometry
     * exactly. The node's own corners grow with it so the solid band stays
     * concentric with the window; a square window (radius 0) stays square. */
    int   grow   = sigma + spread;
    int   sradius = radius > 0 ? radius + spread : 0;

    int bx  = -grow + cfg->shadow_offset_x;
    int by  = -grow + cfg->shadow_offset_y;
    int bw2 = w + 2 * grow;
    int bh2 = h + 2 * grow;

    /* Fold fade × focus-translucency into the shadow alpha, exactly like the
     * border rects, so a fading or hidden window's shadow fades away with it. */
    float eff = view->alpha * anim_view_opacity(view);
    if (eff < 0.0f) eff = 0.0f;
    float color[4] = {
        cfg->shadow_color[0], cfg->shadow_color[1],
        cfg->shadow_color[2], cfg->shadow_color[3] * eff,
    };

    if (!view->shadow) {
        view->shadow = wlr_scene_shadow_create(view->frame, bw2, bh2,
                                               sradius, (float)sigma, color);
    } else {
        wlr_scene_shadow_set_size(view->shadow, bw2, bh2);
        wlr_scene_shadow_set_corner_radius(view->shadow, sradius);
        wlr_scene_shadow_set_blur_sigma(view->shadow, (float)sigma);
        wlr_scene_shadow_set_color(view->shadow, color);
    }
    if (!view->shadow) return;   /* alloc failed — nothing more to do */

    wlr_scene_node_set_position(&view->shadow->node, bx, by);

    /* Cut the window's rounded rect out of the shadow. clipped_region.area is
     * node-relative: the frame box (0,0,w,h) sits at (-bx,-by) from the shadow
     * origin. Rounding the cutout to the window's corner_radius keeps the ring
     * from poking through the rounded corners. */
    struct clipped_region clip = {
        .area    = { .x = -bx, .y = -by, .width = w, .height = h },
        .corners = corner_radii_all(radius),
    };
    wlr_scene_shadow_set_clipped_region(view->shadow, clip);

    wlr_scene_node_set_enabled(&view->shadow->node, true);
    wlr_scene_node_lower_to_bottom(&view->shadow->node);
}

/* ── Glass halo ──────────────────────────────────────────────
 *
 * The backdrop blur that reaches PAST the window: a ring of blurred desktop
 * `glass_halo` px wide around the frame, the effect Firefox has always had by
 * accident (it never binds xdg-decoration, so its GTK shadow margin lives inside
 * its own surface and the per-buffer blur covers that too).
 *
 * ONE node owned by the frame, exactly like the shadow — NOT a grown version of
 * each buffer's blur companion, which is what it was until now and is why the
 * ring squared off at the top. A decorated window is two stacked buffers, and
 * their corner radii carry the seam rule (titlebar rounds the top, content the
 * bottom, so the edges that meet in the middle stay straight). Growing those
 * boxes outwards inherits the seam: the content buffer's ring covered the whole
 * left edge but kept SQUARE top corners, and it started `glass_halo` px below
 * the titlebar's bottom rather than at the window's top. Measured on velle's
 * desktop at pkgrel 183 (Firefox, frame top y=46, titlebar 26 + border 2): the
 * blurred band began at y=60 = content_top − halo with a hard right angle, while
 * the bottom corners curved correctly. A window is one rounded rect, so its ring
 * is one rounded rect too — the seam rule has no business out here.
 *
 * Lowered to the bottom of the frame (under the shadow, which is under the
 * border), so the stack reads backdrop → halo → shadow → border → client. The
 * window's own rounded rect is cut out via clipped_region, leaving a pure ring:
 * inside the frame the blur that belongs there is the per-buffer masked one, and
 * a second pass under a translucent client would only double-blur it.
 */
void view_halo_update(syn_view_t *view)
{
    if (!view->frame) return;   /* override-redirect: no frame, no halo */

    syn_config_t *cfg = &view->server->config;

    /* Same edge-to-edge rule the shadow and the corner radii follow: a maximized
     * or fullscreen window has no desktop beside it, so the ring would only
     * reach onto the neighbouring tile or off the output. */
    int  spread = cfg->glass_halo;
    bool want = cfg->blur && spread > 0 && view->mapped &&
                !view->fullscreen && !view->maximized &&
                view->w > 0 && view->h > 0;
    if (!want) {
        if (view->halo)
            wlr_scene_node_set_enabled(&view->halo->node, false);
        return;
    }

    int radius = chrome_corner_radius(cfg);   /* match the glass corners */
    int w = view->w, h = view->h;

    if (!view->halo) {
        view->halo = wlr_scene_blur_create(view->frame,
                                           w + 2 * spread, h + 2 * spread);
        if (!view->halo) return;
        /* No transparency mask, deliberately — a mask blurs only where its
         * source RENDERS, and every pixel of this node is outside the window.
         * (It could not be dropped later anyway: scenefx's setter dereferences
         * the source before its NULL check.) */
    } else {
        wlr_scene_blur_set_size(view->halo, w + 2 * spread, h + 2 * spread);
    }

    /* The ring is concentric with the window, so its arcs grow with it. A square
     * window (radius 0 — retro chrome, or corner_radius off) stays square. */
    wlr_scene_blur_set_corner_radii(view->halo,
        corner_radii_all(radius > 0 ? radius + spread : 0));

    /* Fold fade × focus-translucency in, exactly like the shadow and the border
     * rects, so a fading or hidden window's ring goes with it. */
    float eff = view->alpha * anim_view_opacity(view);
    wlr_scene_blur_set_alpha(view->halo, eff > 0.0f ? eff : 0.0f);

    wlr_scene_node_set_position(&view->halo->node, -spread, -spread);

    /* Cut the window out. clipped_region.area is node-relative, and the frame
     * box (0,0,w,h) sits at (spread,spread) from the halo's origin. */
    struct clipped_region clip = {
        .area    = { .x = spread, .y = spread, .width = w, .height = h },
        .corners = corner_radii_all(radius),
    };
    wlr_scene_blur_set_clipped_region(view->halo, clip);

    wlr_scene_node_set_enabled(&view->halo->node, true);
    wlr_scene_node_lower_to_bottom(&view->halo->node);
}

void view_deco_destroy(syn_view_t *view)
{
    if (view->border)        { wlr_scene_node_destroy(&view->border->node);        view->border        = NULL; }
    if (view->titlebar)      { wlr_scene_node_destroy(&view->titlebar->node);      view->titlebar      = NULL; }
    if (view->grab_top)      { wlr_scene_node_destroy(&view->grab_top->node);      view->grab_top      = NULL; }
    if (view->grab_bottom)   { wlr_scene_node_destroy(&view->grab_bottom->node);   view->grab_bottom   = NULL; }
    if (view->grab_left)     { wlr_scene_node_destroy(&view->grab_left->node);     view->grab_left     = NULL; }
    if (view->grab_right)    { wlr_scene_node_destroy(&view->grab_right->node);    view->grab_right    = NULL; }
    if (view->shadow)        { wlr_scene_node_destroy(&view->shadow->node);        view->shadow        = NULL; }
    if (view->halo)          { wlr_scene_node_destroy(&view->halo->node);          view->halo          = NULL; }

    if (view->server->deco_hover_view    == view) view->server->deco_hover_view    = NULL;
    if (view->server->tb_last_click_view == view) view->server->tb_last_click_view = NULL;
}

/* Tear down the whole frame tree: the chrome, the client's surface tree, and
 * the frame itself.
 *
 * The frame is the *parent* of view->scene_tree (view_frame_create), so
 * destroying the surface tree leaves the frame behind — it does not "go with"
 * it. Nothing else destroys view->frame, so without this every teardown leaked
 * the frame into window_tree, and view_frame_create on a re-map overwrote
 * view->frame and orphaned the old one for good. Worse, the frame carries
 * node.data = view (so surface_at() can walk up to it), so a frame outliving
 * its freed view leaves the scene graph holding a dangling view pointer.
 *
 * Destroying the frame takes its children with it, so the chrome and the
 * surface tree must not be destroyed again afterwards — view_deco_destroy runs
 * first only to clear the pointers (and the server's hover/click refs) that the
 * view keeps to them. */
void view_frame_destroy(syn_view_t *view)
{
    view_deco_destroy(view);

    if (view->frame) {
        wlr_scene_node_destroy(&view->frame->node);
        view->frame       = NULL;
        view->scene_tree  = NULL;   /* was a child of the frame */
        view->client_tree = NULL;   /* …and that was a child of scene_tree */
        return;
    }
    /* Override-redirect surfaces have no frame: their tree hangs straight off
     * the overlay layer (xw_map), so it has to be destroyed on its own. */
    if (view->scene_tree) {
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree  = NULL;
        view->client_tree = NULL;
    }
}

/* ── Hit testing ─────────────────────────────────────────── */
/* Which resize edges a press at (lx, ly) on the border should drag: the edge it
 * landed on, plus the perpendicular one if it's within CORNER_GRAB of a corner
 * (so the corners resize both axes, as everywhere else). */
static uint32_t border_edges(const syn_view_t *v, double lx, double ly)
{
    double cx = lx - v->x, cy = ly - v->y;
    uint32_t edges = 0;

    if (cx <= CORNER_GRAB)            edges |= WLR_EDGE_LEFT;
    else if (cx >= v->w - CORNER_GRAB) edges |= WLR_EDGE_RIGHT;
    if (cy <= CORNER_GRAB)            edges |= WLR_EDGE_TOP;
    else if (cy >= v->h - CORNER_GRAB) edges |= WLR_EDGE_BOTTOM;

    if (edges) return edges;

    /* Mid-edge: pick the side the point is actually on. */
    if (cx < v->w / 2.0) {
        if (cx < cy && cx < v->h - cy) return WLR_EDGE_LEFT;
    } else {
        if (v->w - cx < cy && v->w - cx < v->h - cy) return WLR_EDGE_RIGHT;
    }
    return (cy < v->h / 2.0) ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
}

syn_view_t *deco_at(syn_server_t *s, double lx, double ly,
                    syn_deco_region_t *region, uint32_t *edges)
{
    if (region) *region = DECO_NONE;
    if (edges)  *edges  = 0;

    double nx, ny;
    struct wlr_scene_node *node =
        wlr_scene_node_at(&s->scene->tree.node, lx, ly, &nx, &ny);
    if (!node) return NULL;

    /* Resolve the node to its view (chrome hangs directly off the frame). */
    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data)
        tree = tree->node.parent;
    if (!tree) return NULL;
    syn_view_t *v = tree->node.data;
    if (!v || !v->mapped || !v->frame) return NULL;

    /* A border rect, or the invisible grab ring just outside it? Both resize;
     * border_edges() works off the cursor's position relative to the window, so
     * it reads a point in the ring (negative, or past w/h) as that edge without
     * caring which rect was actually hit. */
    if (node->type == WLR_SCENE_NODE_RECT) {
        struct wlr_scene_rect *rect = wl_container_of(node, rect, node);
        if (rect == v->border ||
            rect == v->grab_top    || rect == v->grab_bottom  ||
            rect == v->grab_left   || rect == v->grab_right) {
            if (v->fullscreen) return NULL;
            if (region) *region = DECO_BORDER;
            if (edges)  *edges  = border_edges(v, lx, ly);
            return v;
        }
        return NULL;
    }

    /* The titlebar buffer? nx is then buffer-local, i.e. titlebar-local. */
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
        if (buf != v->titlebar) return NULL;   /* the client's own surface */

        int th = view_deco_titlebar(v);
        int w  = v->tb_w;
        if (th <= 0 || w <= 0) return NULL;

        syn_deco_region_t r = DECO_TITLEBAR;
        if      (nx >= btn_x(w, th, 2)) r = DECO_BTN_CLOSE;
        else if (nx >= btn_x(w, th, 1)) r = DECO_BTN_MAX;
        else if (nx >= btn_x(w, th, 0)) r = DECO_BTN_MIN;

        if (region) *region = r;
        return v;
    }

    return NULL;
}

/* ── Resize cursors ──────────────────────────────────────── */
/*
 * The cursor for a set of resize edges. Driven by border_edges() — the very same
 * bits the press reads — so the arrow can never promise a resize the click won't
 * perform. Corners are the point: the ring overhangs into an L at each corner, so
 * they must show the diagonal, and a side arrow there means the 24px corner zone
 * is invisible to the user.
 *
 * Themes name these two ways. The CSS names ("se-resize") are what cursor-shape-v1
 * clients ask for and what modern themes ship; the X11 legacy names are still all
 * some older themes have. Ask for the first, fall back to the second, or a theme
 * without the CSS names silently leaves the previous cursor on screen.
 */
static const char *edge_cursor_name(syn_server_t *s, uint32_t edges)
{
    const char *css, *x11;

    switch (edges) {
    case WLR_EDGE_TOP:                     css = "n-resize";  x11 = "top_side";           break;
    case WLR_EDGE_BOTTOM:                  css = "s-resize";  x11 = "bottom_side";        break;
    case WLR_EDGE_LEFT:                    css = "w-resize";  x11 = "left_side";          break;
    case WLR_EDGE_RIGHT:                   css = "e-resize";  x11 = "right_side";         break;
    case WLR_EDGE_TOP | WLR_EDGE_LEFT:     css = "nw-resize"; x11 = "top_left_corner";    break;
    case WLR_EDGE_TOP | WLR_EDGE_RIGHT:    css = "ne-resize"; x11 = "top_right_corner";   break;
    case WLR_EDGE_BOTTOM | WLR_EDGE_LEFT:  css = "sw-resize"; x11 = "bottom_left_corner"; break;
    case WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT: css = "se-resize"; x11 = "bottom_right_corner"; break;
    default:                               return NULL;
    }

    return wlr_xcursor_manager_get_xcursor(s->cursor_mgr, css, 1) ? css : x11;
}

/* The cursor a grab should hold for its whole duration: a resize keeps the arrow
 * for the edges being dragged (the pointer runs ahead of a client that is slow to
 * commit its new size, so tracking the cursor's position instead would flicker),
 * and a move shows the closed hand. */
const char *deco_grab_cursor(syn_server_t *s, syn_cursor_mode_t mode,
                             uint32_t edges)
{
    if (mode == SYNUI_CURSOR_RESIZE) return edge_cursor_name(s, edges);
    if (mode == SYNUI_CURSOR_MOVE)   return "grabbing";
    return NULL;
}

/* ── Hover highlight ─────────────────────────────────────── */
void deco_hover_update(syn_server_t *s, double lx, double ly, uint32_t time_msec)
{
    syn_deco_region_t region = DECO_NONE;
    uint32_t          edges  = 0;
    syn_view_t *v = deco_at(s, lx, ly, &region, &edges);

    /* Name the resize the border under the pointer would do — and let go of the
     * cursor the moment the pointer is anywhere else, so the client under it gets
     * its own cursor straight back. */
    cursor_set_deco(s, region == DECO_BORDER ? edge_cursor_name(s, edges) : NULL,
                    time_msec);

    /* Only the three buttons light up; the drag strip and borders don't. */
    if (region != DECO_BTN_MIN && region != DECO_BTN_MAX &&
        region != DECO_BTN_CLOSE)
        v = NULL;

    syn_view_t *prev = s->deco_hover_view;
    syn_deco_region_t want = v ? region : DECO_NONE;

    if (prev == v && (!v || v->tb_hover == want)) return;   /* nothing changed */

    if (prev && prev != v) {
        prev->tb_hover = DECO_NONE;
        prev->tb_title[0] = '\0';        /* force a repaint */
        view_update_decorations(prev);
    }
    if (v) {
        v->tb_hover = want;
        v->tb_title[0] = '\0';
        view_update_decorations(v);
    }
    s->deco_hover_view = v;
}

/* ── Maximize ────────────────────────────────────────────── */
/*
 * Maximizing used to be a lie: `maximize_toggle` set view->maximized and told
 * the client, but nothing ever read the flag — layout.c doesn't know it exists,
 * so the window never actually changed size. Now it leaves the tiling flow (a
 * maximized window is floating by definition, else the layout would re-tile it
 * back into its slot) and covers its output's usable box, remembering what to
 * come back to.
 */
void view_apply_maximized(syn_server_t *s, syn_view_t *view, int maximized)
{
    if (!view->mapped || view->fullscreen) return;
    maximized = maximized ? 1 : 0;
    if (view->maximized == maximized) return;

    view->maximized = maximized;
    view_set_maximized(view, maximized);   /* client + taskbar state */

    if (maximized) {
        /* Maximize and snap share saved_geo, so they can't both be live: a
         * maximized window is no longer "the left half", and un-maximizing puts
         * it back where it was when it was maximized. */
        view->snapped        = SYN_SNAP_NONE;
        view->saved_geo      = (struct wlr_box){ view->x, view->y,
                                                 view->w, view->h };
        view->saved_floating = view->floating;
        view->floating       = 1;

        struct wlr_box area;
        output_usable_box_of(s, view->output ? view->output
                                             : server_focused_output(s), &area);
        wlr_scene_node_raise_to_top(view_node(view));
        view_resize(view, area.x, area.y, area.width, area.height);
        layout_apply(s, view->workspace);   /* reflow what it left behind */
    } else {
        view->floating = view->saved_floating;
        layout_apply(s, view->workspace);   /* re-tiles it if it was tiled */
        /* Restore the box for any window the layout won't place itself. The
         * floating *flag* is not the whole test: on a floating desktop an
         * ordinary window has the flag clear, yet layout_apply is a no-op
         * there, so nothing would ever size it back — un-maximizing left it
         * stuck at the full output box. Only a genuinely tiled window is
         * someone else's to place. */
        if ((view->floating ||
             (view->workspace && view->workspace->layout == LAYOUT_FLOATING)) &&
            view->saved_geo.width > 0)
            view_resize(view, view->saved_geo.x, view->saved_geo.y,
                        view->saved_geo.width, view->saved_geo.height);
    }
    /* anim_apply_alpha, not view_update_decorations: the scenefx glass
     * (corner_radius, backdrop blur) is keyed on `boxy` = maximized||fullscreen,
     * and it PERSISTS on the scene_buffer node across the client's buffer swaps
     * — which is what makes it survive a repaint, but also means nothing clears
     * it on its own. Only re-tinting the borders here left a maximized window
     * wearing the rounded corners it had while floating, until some unrelated
     * event (a focus change, which does call this) recomputed them and they
     * visibly snapped square. It calls view_update_decorations itself. */
    anim_apply_alpha(view);
}
