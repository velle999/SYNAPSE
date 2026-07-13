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
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <wlr/types/wlr_scene.h>
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
    const float *fg   = focused ? cfg->titlebar_text_focus  : cfg->titlebar_text;

    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    /* Hovered button gets a lit cell behind its glyph — close goes alarm red,
     * so the destructive one never gets clicked by accident. */
    if (view->tb_hover == DECO_BTN_MIN || view->tb_hover == DECO_BTN_MAX ||
        view->tb_hover == DECO_BTN_CLOSE) {
        int i = (view->tb_hover == DECO_BTN_MIN) ? 0
              : (view->tb_hover == DECO_BTN_MAX) ? 1 : 2;
        const float *hl = (view->tb_hover == DECO_BTN_CLOSE)
                              ? cfg->border_color_warn
                              : cfg->border_color_norm;
        cairo_set_source_rgba(cr, hl[0], hl[1], hl[2], 1.0);
        cairo_rectangle(cr, btn_x(w, th, i), 0, th, th);
        cairo_fill(cr);
    }

    /* Title, vertically centred, clipped to where the buttons start. */
    int text_right = btn_x(w, th, 0) - 6;
    if (text_right > 8 && title[0]) {
        cairo_save(cr);
        cairo_rectangle(cr, 8, 0, text_right - 8, th);
        cairo_clip(cr);

        cairo_set_font_size(cr, th * 0.46);
        cairo_font_extents_t fe;
        cairo_font_extents(cr, &fe);
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
        cairo_move_to(cr, 8, (th + fe.ascent - fe.descent) / 2.0);
        cairo_show_text(cr, title);
        cairo_restore(cr);
    }

    /* The three glyphs. A hovered close gets white so it reads on the red. */
    double cy = th / 2.0;
    double gs = th * 0.18;
    static const syn_deco_region_t order[BTN_COUNT] = {
        DECO_BTN_MIN, DECO_BTN_MAX, DECO_BTN_CLOSE
    };
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (int i = 0; i < BTN_COUNT; i++) {
        const float *gc = fg;
        if (order[i] == DECO_BTN_CLOSE && view->tb_hover == DECO_BTN_CLOSE)
            gc = white;
        draw_btn_glyph(cr, order[i], btn_x(w, th, i) + th / 2.0, cy, gs, gc);
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

/* ── Borders + titlebar ──────────────────────────────────── */
void view_update_decorations(syn_view_t *view)
{
    if (!view->mapped || !view->frame) return;
    /* No sane geometry yet (e.g. focused at map before the first layout) — the
     * chrome is (re)created once the window is sized. */
    if (view->w <= 0 || view->h <= 0) return;

    int bw = view_deco_border(view);

    /* Fullscreen covers the whole output: no border, no titlebar. */
    if (bw == 0) {
        if (view->border_top)    wlr_scene_node_set_enabled(&view->border_top->node,    false);
        if (view->border_bottom) wlr_scene_node_set_enabled(&view->border_bottom->node, false);
        if (view->border_left)   wlr_scene_node_set_enabled(&view->border_left->node,   false);
        if (view->border_right)  wlr_scene_node_set_enabled(&view->border_right->node,  false);
        if (view->titlebar)      wlr_scene_node_set_enabled(&view->titlebar->node,      false);
        view_grab_ring_update(view);
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
     * colour (unlike buffers, which have set_opacity), so fold it in here. */
    color[3] *= (view->alpha > 0.0f ? view->alpha : 0.0f);

    /* Frame-local: the frame node already sits at (view->x, view->y). */
    int w = view->w, h = view->h;
    int side_h = h - 2 * bw;      /* side borders sit between top/bottom */
    if (side_h < 0) side_h = 0;   /* scene rects must be non-negative */

    /* Create borders as scene rects if they don't exist yet. Existing rects
     * are re-sized too: the view may have been resized (retile), and a config
     * reload can change border_width. */
    #define MAKE_BORDER(field, bx, by, bw2, bh) do { \
        if (!view->field) \
            view->field = wlr_scene_rect_create(view->frame, bw2, bh, color); \
        else { \
            wlr_scene_rect_set_color(view->field, color); \
            wlr_scene_rect_set_size(view->field, bw2, bh); \
        } \
        wlr_scene_node_set_enabled(&view->field->node, true); \
        wlr_scene_node_set_position(&view->field->node, bx, by); \
    } while(0)

    MAKE_BORDER(border_top,    0,        0,        w,  bw);
    MAKE_BORDER(border_bottom, 0,        h-bw,     w,  bw);
    MAKE_BORDER(border_left,   0,        bw,       bw, side_h);
    MAKE_BORDER(border_right,  w-bw,     bw,       bw, side_h);

    #undef MAKE_BORDER

    view_grab_ring_update(view);
    titlebar_render(view);
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

void view_deco_destroy(syn_view_t *view)
{
    if (view->border_top)    { wlr_scene_node_destroy(&view->border_top->node);    view->border_top    = NULL; }
    if (view->border_bottom) { wlr_scene_node_destroy(&view->border_bottom->node); view->border_bottom = NULL; }
    if (view->border_left)   { wlr_scene_node_destroy(&view->border_left->node);   view->border_left   = NULL; }
    if (view->border_right)  { wlr_scene_node_destroy(&view->border_right->node);  view->border_right  = NULL; }
    if (view->titlebar)      { wlr_scene_node_destroy(&view->titlebar->node);      view->titlebar      = NULL; }
    if (view->grab_top)      { wlr_scene_node_destroy(&view->grab_top->node);      view->grab_top      = NULL; }
    if (view->grab_bottom)   { wlr_scene_node_destroy(&view->grab_bottom->node);   view->grab_bottom   = NULL; }
    if (view->grab_left)     { wlr_scene_node_destroy(&view->grab_left->node);     view->grab_left     = NULL; }
    if (view->grab_right)    { wlr_scene_node_destroy(&view->grab_right->node);    view->grab_right    = NULL; }

    if (view->server->deco_hover_view    == view) view->server->deco_hover_view    = NULL;
    if (view->server->tb_last_click_view == view) view->server->tb_last_click_view = NULL;
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
        if (rect == v->border_top || rect == v->border_bottom ||
            rect == v->border_left || rect == v->border_right ||
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
        if (view->floating && view->saved_geo.width > 0)
            view_resize(view, view->saved_geo.x, view->saved_geo.y,
                        view->saved_geo.width, view->saved_geo.height);
    }
    view_update_decorations(view);
}
