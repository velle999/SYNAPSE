/*
 * layout.c — SynapseOS window layout engine
 *
 * Implements three layout modes:
 *
 *   TILING   — Master-stack tiling (dwm-style)
 *              First window is master (left, 60% width).
 *              Remaining windows stack right.
 *
 *   MONOCLE  — One window per output, filling that output's usable
 *              box; the rest of its windows are hidden. Which one is
 *              whichever has focus, so Alt+Tab and Super+J/K change it
 *              (focus_view reflows the desktop for exactly this).
 *              Floating windows are exempt and stay on top.
 *
 *   AI       — Ask synapd to suggest positions based on
 *              workspace intent + running apps. If AI is
 *              unavailable, falls back to TILING.
 *
 * Border width: 2px (configurable).
 * Gap between windows: 8px.
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

#include "synui.h"

/* border width and gap come from s->config (synuirc `border_width`/`gap`,
 * live-reloadable via SIGHUP) */
#define MASTER_FACTOR  0.60f   /* default master column width fraction */
#define MASTER_MIN     0.10f
#define MASTER_MAX     0.90f
/* MIN_WIN lives in synui.h — geom_persist.c needs the same floor so it never
 * records a box this code would only have to clamp back up again. */

/* ── Get output geometry for a view ──────────────────────── */
/* A window is laid out on the monitor it lives on (falling back to the focused
 * output if it has none yet), minus any layer-shell exclusive zones so tiling
 * doesn't cover panels/bars. */
static void get_view_geom(syn_server_t *s, syn_view_t *view,
                          struct wlr_box *out)
{
    syn_output_t *o = (view && view->output) ? view->output
                                            : server_focused_output(s);
    output_usable_box_of(s, o, out);
}

/* ── Count mapped windows of ws on one output ────────────── */
static int count_windows(syn_workspace_t *ws, syn_output_t *o)
{
    int n = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped && !v->floating && !v->fullscreen && !v->minimized &&
            v->output == o)
            n++;
    return n;
}

/* ── Place / size a view ─────────────────────────────────── */
/* Public so input.c (interactive move/resize) reuses the same path. */
void view_resize(syn_view_t *view, int x, int y, int w, int h)
{
    view->x = x;
    view->y = y;
    view->w = w;
    view->h = h;

    /* x/y/w/h is the *frame*; the client gets what's left inside the border and
     * below the titlebar. */
    struct wlr_box c;
    view_content_box(view, &c);

    /* Record what the client is being asked for, and reset the heal budget
     * whenever that target changes — view_heal_size() re-sends this configure a
     * bounded number of times if the client settles on a different size. */
    if (c.width != view->cfg_w || c.height != view->cfg_h) {
        view->cfg_w = c.width;
        view->cfg_h = c.height;
        view->heal_tries = 0;
    }

    /* Commit the size to the client. X11 clients also need their absolute
     * layout position, which the xdg path derives from the scene node. */
    if (view->is_xwayland)
        wlr_xwayland_surface_configure(view->xsurface, c.x, c.y,
                                       c.width, c.height);
    else
        wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel,
                                  c.width, c.height);

    /* Move the frame; the client surface sits at its content offset *inside*
     * the frame, so the chrome travels with the window for free. */
    wlr_scene_node_set_position(view_node(view), x, y);
    if (view->frame)
        wlr_scene_node_set_position(&view->scene_tree->node, c.x - x, c.y - y);

    view_update_decorations(view);
}
#define place_view(v, x, y, w, h) view_resize((v), (x), (y), (w), (h))

/* What a layout is called when a HUMAN reads it — the Super+Tab toast, the
 * control panel's Layout row, the AI overlay's context line. All three used to
 * spell the list out for themselves, and the control panel was about to be the
 * fourth.
 *
 * NOT the same list as ipc.c's layout_name(), and they must not be merged: that
 * one is the wire value `synctl` and the test rigs parse, so it is lowercase
 * "ai" and changing it is an API break. This one is for reading, so it is "AI".
 * Two audiences, two functions, on purpose. */
const char *layout_label(syn_layout_t l)
{
    switch (l) {
    case LAYOUT_TILING:   return "tiling";
    case LAYOUT_FLOATING: return "floating";
    case LAYOUT_MONOCLE:  return "monocle";
    case LAYOUT_AI:       return "AI";
    }
    return "unknown";
}

/* ── Persisted layout choice ─────────────────────────────── */
/*
 * The layout is a per-desktop *setting*, not session scratch. Every desktop
 * used to start at LAYOUT_TILING no matter what was chosen last, so a desk
 * left on floating came back tiling on the next login and had to be cycled
 * round again — every restart, by hand.
 *
 * Written on every change (there are only two places that assign ws->layout:
 * layout_cycle, and the retile that switches a floating desktop), so a crash
 * or a kill -9 keeps the choice; read once at startup, over the default.
 *
 * master_factor is deliberately NOT in this file: it is per-desktop too, but
 * it already has a synuirc default (`master_factor`) that a state file would
 * start shadowing for every desktop the moment anyone touched Super+H once.
 * If it is ever added here, it needs a "never set" marker, not a value.
 */
static bool layout_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "layouts.state");
}

/* The spelling on disk is the lowercase `synctl` wire vocabulary, not
 * layout_label()'s "AI". A state file is a format: it must not change spelling
 * because a toast was restyled, which is exactly the trap layout_label's
 * comment above warns about. Third audience, third switch, on purpose. */
static const char *layout_key(syn_layout_t l)
{
    switch (l) {
    case LAYOUT_TILING:   return "tiling";
    case LAYOUT_FLOATING: return "floating";
    case LAYOUT_MONOCLE:  return "monocle";
    case LAYOUT_AI:       return "ai";
    }
    return "tiling";
}

void layout_state_save(syn_server_t *s)
{
    char path[256];
    if (!layout_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: layout: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    /* Every desktop, every time — the file is nine lines and one desktop's
     * layout is not independent of the others as far as the user is concerned
     * ("my desks are how I left them" or they are not). */
    for (int i = 0; i < WORKSPACE_MAX; i++)
        fprintf(f, "desktop%d=%s\n", i + 1, layout_key(s->workspaces[i].layout));
    fclose(f);
}

/* Applied over the LAYOUT_TILING seeded in server init. An absent file is not
 * an error — it means "never changed one", and every desktop keeps the
 * default. So does a line with a value this build doesn't know. */
void layout_state_load(syn_server_t *s)
{
    char path[256];
    if (!layout_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if (strncmp(key, "desktop", 7) != 0) continue;

        /* 1-based on disk, the way the toast, the control panel and Super+1-9
         * all count desktops. */
        int idx = atoi(key + 7) - 1;
        if (idx < 0 || idx >= WORKSPACE_MAX) continue;

        syn_layout_t l;
        if      (strcmp(val, "tiling")   == 0) l = LAYOUT_TILING;
        else if (strcmp(val, "floating") == 0) l = LAYOUT_FLOATING;
        else if (strcmp(val, "monocle")  == 0) l = LAYOUT_MONOCLE;
        else if (strcmp(val, "ai")       == 0) l = LAYOUT_AI;
        else continue;

        s->workspaces[idx].layout = l;
    }
    fclose(f);
}

/* ── TILING layout (master-stack) ────────────────────────── */
/* Tiles only the windows of ws that live on o, into o's usable box. */
void layout_tile(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Apply outer gap. Clamp the working area: a large configured gap on a
     * small output must not go negative — negative sizes would flow into
     * scene rects and client configures. */
    int gap = s->config.gap;
    int x = area.x + gap;
    int y = area.y + gap;
    int W = area.width  - 2 * gap;
    int H = area.height - 2 * gap;
    if (W < MIN_WIN) { x = area.x; W = area.width  > MIN_WIN ? area.width  : MIN_WIN; }
    if (H < MIN_WIN) { y = area.y; H = area.height > MIN_WIN ? area.height : MIN_WIN; }

    int n = count_windows(ws, o);
    if (n == 0) return;

    float mf = ws->master_factor;
    if (mf < MASTER_MIN || mf > MASTER_MAX) mf = MASTER_FACTOR;
    int master_w = (n == 1) ? W : (int)(W * mf) - gap / 2;
    if (master_w < MIN_WIN) master_w = MIN_WIN;
    int stack_w  = W - master_w - gap;
    if (stack_w < MIN_WIN) stack_w = MIN_WIN;
    int stack_x  = x + master_w + gap;

    int i = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->fullscreen || v->minimized) continue;
        if (v->output != o) continue;

        if (i == 0) {
            /* Master */
            place_view(v, x, y, master_w, H);
        } else {
            /* Stack */
            int nstack = n - 1;
            int slot_h = (H - (nstack - 1) * gap) / nstack;
            if (slot_h < MIN_WIN) slot_h = MIN_WIN;
            int vy = y + (i - 1) * (slot_h + gap);
            place_view(v, stack_x, vy, stack_w, slot_h);
        }
        i++;
    }
}

/* ── MONOCLE layout ──────────────────────────────────────── */
/* Each monitor showing ws gets its own top window — monocle is per-output, so
 * a 3-monitor desktop still shows three windows, one filling each screen. */
void layout_monocle(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Show exactly one window *of this output*: the focused view if it lives
     * here, else the first mapped one. Keying off the global focused view
     * would blank the monitor whenever focus sits on another output (or on
     * nothing, right after a close). */
    syn_view_t *top = NULL;
    syn_view_t *v;
    if (s->focused_view && s->focused_view->workspace == ws &&
        s->focused_view->output == o &&
        s->focused_view->mapped && !s->focused_view->floating &&
        !s->focused_view->minimized)
        top = s->focused_view;
    else
        wl_list_for_each(v, &ws->windows, link)
            if (v->mapped && !v->floating && !v->minimized && v->output == o) {
                top = v;
                break;
            }

    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->minimized) continue;
        if (v->output != o) continue;
        /* Only configure a window whose box actually moved. Every window here
         * is already at the same full-usable-box, and focus_view() now reflows
         * a monocle desktop on every focus change — without the compare that
         * would send each client a redundant configure per Alt+Tab press, per
         * click, per Super+J. Same guard, same reason, as the maximize re-fit
         * in layout_apply. */
        if (v->x != area.x || v->y != area.y ||
            v->w != area.width || v->h != area.height)
            place_view(v,
                       area.x, area.y,
                       area.width, area.height);
        wlr_scene_node_set_enabled(view_node(v), v == top);
    }
}

/* ── AI layout ───────────────────────────────────────────── */
/*
 * Build a prompt describing all open windows and the workspace intent,
 * then ask synapd for a JSON layout suggestion.
 *
 * Expected response (JSON, one window per line):
 *   {"comm":"vim","x":0,"y":0,"w":0.6,"h":1.0}
 *   {"comm":"terminal","x":0.6,"y":0.5,"w":0.4,"h":0.5}
 *   {"comm":"firefox","x":0.6,"y":0.0,"w":0.4,"h":0.5}
 *
 * w and h are fractions of the output dimensions (0.0-1.0).
 * If the response is malformed we fall back to tiling.
 */
void layout_request_ai(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    if (!atomic_load(&s->ai_connected)) {
        layout_tile(s, ws, o);
        return;
    }

    /* Build window list string */
    char win_list[1024] = {0};
    int pos = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->output != o) continue;
        const char *title = view_title(v) ? view_title(v) : "unknown";
        const char *app   = view_app_id(v) ? view_app_id(v) : "unknown";
        pos += snprintf(win_list + pos, sizeof(win_list) - pos,
                        "  - app=%s title=\"%.30s\"\n", app, title);
        if (pos >= (int)sizeof(win_list) - 64) break;
    }

    if (!win_list[0]) {
        layout_tile(s, ws, o);
        return;
    }

    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Remember which monitor asked, so the async response places the right
     * windows into the right box. */
    s->ai_layout_output = o;

    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "[LAYOUT_REQUEST]\n"
        "workspace: %s\n"
        "intent: %s\n"
        "windows:\n%s\n"
        "output: %dx%d\n"
        "\n"
        "Suggest a tiling layout. For each window reply with one JSON object per line:\n"
        "{\"app\":\"APP_ID\",\"x\":FRAC,\"y\":FRAC,\"w\":FRAC,\"h\":FRAC}\n"
        "x,y,w,h are fractions 0.0-1.0 of output dimensions. No explanation.",
        ws->name,
        ws->intent[0] ? ws->intent : "general use",
        win_list,
        area.width, area.height
    );

    syn_ai_request_t req = {
        .type = AI_MSG_QUERY_LAYOUT,
        .id   = (uint64_t)ws->index,   /* response routes back by workspace index */
    };
    strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
    ai_thread_send(s, &req);

    /* Apply tiling immediately as placeholder; the AI response arrives
     * asynchronously and the frame loop calls layout_apply_ai_response(). */
    layout_tile(s, ws, o);
}

/* ── Parse one AI layout line ────────────────────────────── */
/*
 * Parses a single line of the AI layout response:
 *   {"app":"APP_ID","x":FRAC,"y":FRAC,"w":FRAC,"h":FRAC}
 * Returns 1 and fills the outputs on success, 0 on a malformed line.
 * Fractions are validated to the sane 0.0–1.0 range (w/h must be > 0) so a
 * bad model reply can't place a window off-screen or at zero size. Pure
 * function (no wlroots deps) so it can be unit-tested directly.
 */
int parse_ai_layout_line(const char *line, char *app_id, size_t app_len,
                         float *x, float *y, float *w, float *h)
{
    char app[128] = {0};
    float fx, fy, fw, fh;
    if (sscanf(line, " {\"app\":\"%127[^\"]\",\"x\":%f,\"y\":%f,\"w\":%f,\"h\":%f}",
               app, &fx, &fy, &fw, &fh) != 5)
        return 0;

    if (fx < 0.0f || fx > 1.0f || fy < 0.0f || fy > 1.0f) return 0;
    if (fw <= 0.0f || fw > 1.0f || fh <= 0.0f || fh > 1.0f) return 0;
    if (fx + fw > 1.001f || fy + fh > 1.001f) return 0;   /* must fit on screen */

    snprintf(app_id, app_len, "%s", app);
    *x = fx; *y = fy; *w = fw; *h = fh;
    return 1;
}

/* ── Apply AI layout response ────────────────────────────── */
void layout_apply_ai_response(syn_server_t *s, syn_workspace_t *ws,
                               const char *json_response)
{
    /* The monitor whose windows this suggestion is about (recorded when the
     * request went out — the response is async and the focus may have moved). */
    syn_output_t *o = s->ai_layout_output ? s->ai_layout_output
                                          : server_focused_output(s);
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    char copy[2048];
    strncpy(copy, json_response, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    int applied = 0;
    char *save = NULL;
    char *line = strtok_r(copy, "\n", &save);
    while (line) {
        char app_id[128];
        float fx, fy, fw, fh;
        if (parse_ai_layout_line(line, app_id, sizeof(app_id),
                                 &fx, &fy, &fw, &fh)) {
            syn_view_t *v;
            wl_list_for_each(v, &ws->windows, link) {
                if (!v->mapped || v->floating || v->output != o) continue;
                const char *aid = view_app_id(v);
                if (aid && strcmp(aid, app_id) == 0) {
                    int gap = s->config.gap;
                    int nx = area.x + (int)(fx * area.width);
                    int ny = area.y + (int)(fy * area.height);
                    int nw = (int)(fw * area.width);
                    int nh = (int)(fh * area.height);
                    nw = nw > gap * 2 ? nw - gap : nw;
                    nh = nh > gap * 2 ? nh - gap : nh;
                    /* Mark AI-managed before placing so the border picks the
                     * AI colour, and record the app as the window's intent. */
                    v->ai_ctx.has_ctx = 1;
                    snprintf(v->ai_ctx.intent, sizeof(v->ai_ctx.intent),
                             "%s", app_id);
                    place_view(v, nx + gap/2, ny + gap/2, nw, nh);
                    applied++;
                    break;
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }

    /* If the model returned nothing usable, keep the tiling placeholder. */
    if (!applied)
        wlr_log(WLR_DEBUG, "synui: AI layout response had no usable windows");
}

/* ── Main dispatch ───────────────────────────────────────── */
/* A workspace spans the whole desk, so laying it out means laying out each
 * monitor's share of it: every output runs the workspace's layout over just
 * the windows that live on that output. */
void layout_apply(syn_server_t *s, syn_workspace_t *ws)
{
    if (!s || !ws) return;

    /* A hidden workspace re-flows when it next becomes visible; laying it
     * out now would re-enable its scene nodes on top of the visible one. */
    if (!workspace_visible(ws)) return;

    /* Re-enable all nodes first (minimized ones stay hidden — their own
     * apply path owns disabling/enabling the node). */
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped && !v->minimized)
            wlr_scene_node_set_enabled(view_node(v), true);

    /* AI-managed marking (and its cyan border) only persists under the AI
     * layout; clear it for the other layouts so the border reflects reality. */
    if (ws->layout != LAYOUT_AI)
        wl_list_for_each(v, &ws->windows, link)
            v->ai_ctx.has_ctx = 0;

    /* Re-fit maximized windows to the CURRENT usable box.
     *
     * view_apply_maximized sizes the window once and marks it floating (it has
     * to — otherwise the tiling pass below would drag it back into a slot), so
     * from then on every loop here skips it. But the usable box is not fixed:
     * it moves whenever a panel maps, unmaps or changes its exclusive zone, and
     * the bar's auto-hide changes it on every reveal (28 → 0 → 28). Without
     * this the window keeps whatever box it was maximized into, so a hidden bar
     * leaves a strip of bare desktop above a "maximized" window. Verified: with
     * the bar's zone dropped to 0, a maximized window stayed at 1280x692 while
     * a freshly tiled one correctly took the full 704.
     *
     * Runs for every layout, FLOATING included — maximize is not a tiling
     * feature. The compare is what makes it cheap: layout_apply is called from
     * a lot of paths and view_resize sends the client a configure, so it must
     * not fire when the box has not actually moved. */
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || !v->maximized || v->fullscreen || v->minimized)
            continue;
        struct wlr_box area;
        output_usable_box_of(s, v->output ? v->output : server_focused_output(s),
                             &area);
        if (v->x == area.x && v->y == area.y &&
            v->w == area.width && v->h == area.height)
            continue;
        view_resize(v, area.x, area.y, area.width, area.height);
    }

    /* AI layout is a single-monitor feature: only the focused output gets a
     * suggestion (one in-flight request at a time), the rest tile. */
    syn_output_t *focused = server_focused_output(s);
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        switch (ws->layout) {
        case LAYOUT_TILING:   layout_tile(s, ws, o);     break;
        case LAYOUT_MONOCLE:  layout_monocle(s, ws, o);  break;
        case LAYOUT_AI:
            if (o == focused) layout_request_ai(s, ws, o);
            else              layout_tile(s, ws, o);
            break;
        case LAYOUT_FLOATING: /* no-op: user positions windows */ break;
        }
    }
}

/* ── Fullscreen ──────────────────────────────────────────── */
/* Which monitor should a fullscreen window cover?
 *
 * A client names the monitor it wants in whatever way its protocol allows.
 * xdg-shell clients pass a wl_output straight to set_fullscreen. X11 clients
 * have no such argument, so SDL — and therefore Chibi, which picks the
 * portrait monitor itself — moves the window onto the target monitor first
 * and only then sets _NET_WM_STATE_FULLSCREEN; the window's own rectangle is
 * the request. Honouring that also matches what X11 window managers do.
 *
 * Only called with a mapped view, so xsurface geometry is the placed one.
 * A client that named no monitor falls back to its workspace's output, which
 * for a window already on that output is the same answer as before. */
static syn_output_t *fullscreen_target_output(syn_server_t *s, syn_view_t *view)
{
    struct wlr_output *wo = NULL;

    if (view->is_xwayland) {
        struct wlr_xwayland_surface *xs = view->xsurface;
        if (xs->width > 0 && xs->height > 0)
            wo = wlr_output_layout_output_at(s->output_layout,
                                             xs->x + xs->width  / 2.0,
                                             xs->y + xs->height / 2.0);
    } else {
        wo = view->xdg_surface->toplevel->requested.fullscreen_output;
    }

    if (wo && wo->data) return wo->data;
    return view->output ? view->output : server_focused_output(s);
}

/* Enter/leave fullscreen with real geometry: cover the target output's
 * full box (raised, borders hidden — view_update_borders checks the flag),
 * or hand the window back to the layout. Shared by the xdg and XWayland
 * request handlers and the foreign-toplevel (taskbar) request. */
void view_apply_fullscreen(syn_server_t *s, syn_view_t *view, int fs)
{
    view->fullscreen = fs ? 1 : 0;
    view_set_fullscreen(view, view->fullscreen);   /* client + taskbar state */
    if (!view->mapped) return;

    if (view->fullscreen) {
        /* Leaving fullscreen re-places a floating window (layout_float_place),
         * which would move it out of the zone it claims to be snapped to. */
        view->snapped = SYN_SNAP_NONE;
        syn_output_t *o = fullscreen_target_output(s, view);
        /* Fullscreening onto another monitor hands the window to that monitor
         * (same desktop), so it untiles back where it is actually shown. */
        if (o && view->output != o)
            view->output = o;
        struct wlr_box area;
        output_box_of(s, o, &area);
        /* view_resize re-seats the client at the frame origin: fullscreen has
         * no border or titlebar, so content == frame. */
        view_resize(view, area.x, area.y, area.width, area.height);
        wlr_scene_node_raise_to_top(view_node(view));
        /* A sub-native X11 client (old game locked to 1080p) fills the box by
         * scaling its buffer; re-applied per-commit from xw_surface_commit. */
        view_fullscreen_rescale(view);
    } else {
        layout_apply(s, view->workspace);
        if (view->floating)
            layout_float_place(s, view);
        view_update_decorations(view);
        /* Undo any fullscreen buffer scale now the view is back in the layout. */
        view_fullscreen_rescale(view);
    }

    /* Re-push the glass: corner_radius is squared off while fullscreen and blur
     * is gated on !fullscreen, and both persist on the scene_buffer node until
     * something recomputes them — same staleness that left maximized windows
     * rounded (see view_apply_maximized). Only for a mapped view; the unmapped
     * early-return above never reaches here. */
    anim_apply_alpha(view);

    /* A fullscreen view has to cover the bar, and it may have just been handed
     * to another output — refresh every output, not just the target. */
    layer_update_occlusion_all(s);

    /* Fullscreen is the game-mode signal, and this is the one choke point every
     * path (xdg, XWayland, foreign-toplevel) funnels through. */
    game_reevaluate(s);
}

/* ── Minimize (iconify) ──────────────────────────────────── */
/* Same split as fullscreen: view_set_minimized tells the client (X11 only —
 * xdg-shell has no minimize protocol) and taskbars; this makes it real by
 * hiding the scene node and excluding the window from tiling/monocle, then
 * reflowing. Restoring re-enables the node, raises and focuses it if its
 * workspace is currently shown (a hidden workspace's window stays disabled
 * until workspace_switch re-enables mapped, non-minimized nodes). */
void view_apply_minimized(syn_server_t *s, syn_view_t *view, int minimized)
{
    view->minimized = minimized ? 1 : 0;
    view_set_minimized(view, view->minimized);
    if (!view->mapped) return;

    /* Only actually show it if its workspace is visible somewhere — a hidden
     * workspace keeps all its nodes disabled regardless of minimized state
     * (workspace_switch re-enables mapped, non-minimized nodes when it next
     * becomes visible). */
    int show = !view->minimized && workspace_visible(view->workspace);
    wlr_scene_node_set_enabled(view_node(view), show);

    if (view->minimized) {
        if (s->focused_view == view)
            workspace_focus_first(s, view->workspace);
    } else if (show) {
        wlr_scene_node_raise_to_top(view_node(view));
        focus_view(s, view, view_surface(view));
    }

    layout_apply(s, view->workspace);
}

/* Focus the first mapped, non-minimized window on ws — or clear focus
 * entirely if there is none, so keyboard input can't keep flowing to a
 * hidden window. */
void workspace_focus_first(syn_server_t *s, syn_workspace_t *ws)
{
    /* The desktop spans every monitor, so prefer a window on the one the user
     * is actually looking at — otherwise switching desktops would throw focus
     * onto whichever screen happens to hold the list's first window. */
    syn_output_t *focused = server_focused_output(s);
    syn_view_t *v, *fallback = NULL;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->minimized) continue;
        if (v->output == focused) {
            focus_view(s, v, view_surface(v));
            return;
        }
        if (!fallback) fallback = v;
    }
    focus_view(s, fallback, fallback ? view_surface(fallback) : NULL);
}

/* ── Workspace switching ─────────────────────────────────── */
/* Switch the whole desk to virtual desktop `index`: every monitor swaps to its
 * share of that workspace at once. Nothing is bound to a particular output, so
 * this always does something no matter how many monitors are plugged in. */
void workspace_switch(syn_server_t *s, int index)
{
    if (index < 0 || index >= WORKSPACE_MAX) return;
    if (index == s->active_workspace) return;

    syn_workspace_t *cur    = &s->workspaces[s->active_workspace];
    syn_workspace_t *target = &s->workspaces[index];

    /* Cross-fade: the outgoing desktop's windows fade out and are only disabled
     * once they're actually invisible (anim.c), the incoming ones fade in. */
    syn_view_t *v;
    wl_list_for_each(v, &cur->windows, link)
        if (v->mapped)
            anim_fade_out_and_hide(v);
    cur->visible = 0;

    /* Show the incoming one. */
    s->active_workspace = index;
    target->visible = 1;
    wlr_log(WLR_INFO, "synui: workspace %d", index + 1);
    wl_list_for_each(v, &target->windows, link)
        if (v->mapped && !v->minimized) {
            wlr_scene_node_set_enabled(view_node(v), true);
            anim_fade_in(v);
        }

    layout_apply(s, target);

    /* Focus the target workspace's first window (or clear focus if empty —
     * the previous workspace's windows are hidden now). */
    workspace_focus_first(s, target);

    /* Switching away from a fullscreen window must bring the bar back — and
     * switching onto one must hide it again. */
    layer_update_occlusion_all(s);

    /* Refresh overlay if visible */
    if (s->overlay.visible)
        synui_render_overlay(s);

    /* Notify AI about workspace switch */
    if (atomic_load(&s->ai_connected)) {
        char prompt[256];
        snprintf(prompt, sizeof(prompt),
            "[WORKSPACE_SWITCH] switched to workspace '%s' (intent: %s). "
            "Update neural overlay context.",
            s->workspaces[index].name,
            s->workspaces[index].intent[0] ? s->workspaces[index].intent : "general");
        syn_ai_request_t req = { .type = AI_MSG_STATUS_UPDATE };
        strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
        ai_thread_send(s, &req);
    }
}

/* Send a window to another virtual desktop (Super+Shift+1…9). It keeps the
 * monitor it was on, so switching to that desktop finds it on the same screen
 * you sent it from. */
void workspace_move_view(syn_server_t *s, syn_view_t *view, int ws_index)
{
    if (ws_index < 0 || ws_index >= WORKSPACE_MAX) return;
    int old_ws = view->workspace->index;
    if (old_ws == ws_index) return;

    view->workspace = &s->workspaces[ws_index];
    wl_list_remove(&view->link);
    wl_list_insert(&view->workspace->windows, &view->link);

    /* Visible only if the target desktop is the one being shown. */
    wlr_scene_node_set_enabled(view_node(view),
                                workspace_visible(view->workspace) &&
                                !view->minimized);

    layout_apply(s, &s->workspaces[old_ws]);
    layout_apply(s, &s->workspaces[ws_index]);

    /* If the moved window was focused and is now hidden, hand focus back to
     * the desktop still on screen so keys don't keep going to an invisible
     * window. */
    if (s->focused_view == view && !workspace_visible(view->workspace))
        workspace_focus_first(s, &s->workspaces[old_ws]);
}

/* Re-home a window onto another monitor, keeping its desktop (Super+O). */
void view_set_output(syn_server_t *s, syn_view_t *view, syn_output_t *o)
{
    if (!view || !o || view->output == o) return;
    view->output = o;
    layout_apply(s, view->workspace);
}

/* ── Remembered geometry ─────────────────────────────────── */
/*
 * Put a window back where its app last left it (the table lives in
 * geom_persist.c). Called from the xdg and XWayland map handlers, so an app
 * reopens the size it was closed at, and from layout_float_place, where a
 * remembered box beats the centred default.
 *
 * WHICH DESKTOP DECIDES. A layout that places windows itself owns their
 * geometry, and a remembered box is the user's answer to a question those
 * layouts do not ask. So a window *opening* on a tiling or AI desktop skips
 * this entirely: it goes wherever the layout puts it, which on tiling is the
 * whole usable box for the first window and a master/stack slot after that.
 *
 * That is not a refinement of the old rule, it replaces one. The entry carries
 * a floating flag (see geom_persist_save) and this function used to honour it
 * on any desktop, re-floating the window so its box would survive. On velle's
 * tiling desktop that made the tiler look broken: every app reopened floating,
 * every layout skips floating windows, and closing one wrote floating=1 back
 * out — self-sustaining, and Super+Tab moved nothing because there was nothing
 * left in the flow to move. Edge-snapping seeded it (snap_view sets floating
 * for the duration and unmap records the live flag), but any window ever
 * floated would have done.
 *
 * The floating and monocle desktops keep the whole feature: on those, where a
 * window sits is the user's business, so a saved box is worth honouring.
 *
 * Two ways in that ARE still honoured on a tiling desktop, because neither is
 * a window merely opening:
 *
 *  - Super+F (and a drag that auto-floats): the caller sets view->floating
 *    before calling through layout_float_place, so the window has already left
 *    the flow and is asking where it used to live. Freshly-mapped views are
 *    never floating yet, which is exactly what separates the two cases.
 *  - A client that asks to be maximized before it ever commits (Firefox does,
 *    restoring its session) — that is the client's own request, handled by the
 *    map path, not something read out of windows.conf.
 *
 * Returns false when the app has nothing saved — or when the layout owns the
 * placement — in which case the caller's own placement stands.
 */
bool layout_restore_geometry(syn_server_t *s, syn_view_t *view)
{
    /* Ask before the lookup: on these desktops the table has no say at all,
     * and not touching it keeps that visible in a trace. */
    bool layout_places_it = view->workspace &&
                            (view->workspace->layout == LAYOUT_TILING ||
                             view->workspace->layout == LAYOUT_AI);
    if (layout_places_it && !view->floating)
        return false;

    struct wlr_box saved;
    int saved_max = 0, saved_float = 0;
    if (!geom_persist_lookup(view, &saved, &saved_max, &saved_float))
        return false;

    bool on_floating_desk = view->workspace &&
                            view->workspace->layout == LAYOUT_FLOATING;

    /* The window was free when it closed, so give it back its freedom before
     * asking whether it may keep its box — otherwise the tiler owns it and the
     * saved geometry is dropped. Not needed on a floating desktop (nothing
     * tiles there), and not for a maximized entry: view_apply_maximized below
     * floats it for the duration anyway, and saved_floating has to stay 0 so
     * un-maximizing hands it back to the tiler. Only monocle reaches this now
     * — tiling and AI returned above. */
    bool refloated = false;
    if (saved_float && !saved_max && !view->floating && !on_floating_desk) {
        view->floating = 1;
        refloated = true;
    }

    bool free_window = view->floating || on_floating_desk;

    if (free_window) {
        /* x/y are absolute layout coordinates, so they name a monitor as much
         * as a position. Re-home the view onto the monitor the saved box's
         * centre lands on before clamping — otherwise every window reopens on
         * whichever monitor happened to be focused, squashed into its box. If
         * that monitor is gone the lookup fails and the clamp below handles
         * it. */
        struct wlr_output *wo = wlr_output_layout_output_at(
            s->output_layout,
            saved.x + saved.width  / 2.0,
            saved.y + saved.height / 2.0);
        if (wo && wo->data)
            view->output = wo->data;

        /* Clamp back onto a monitor that exists *now*, so an entry saved on a
         * wider desk can't open the window off-screen where it could never be
         * reached. */
        struct wlr_box area;
        get_view_geom(s, view, &area);

        int sw = saved.width, sh = saved.height;
        if (sw > area.width)  sw = area.width;
        if (sh > area.height) sh = area.height;
        if (sw < MIN_WIN) sw = MIN_WIN;
        if (sh < MIN_WIN) sh = MIN_WIN;

        int sx = saved.x, sy = saved.y;
        if (sx + sw > area.x + area.width)  sx = area.x + area.width  - sw;
        if (sy + sh > area.y + area.height) sy = area.y + area.height - sh;
        if (sx < area.x) sx = area.x;
        if (sy < area.y) sy = area.y;

        view_resize(view, sx, sy, sw, sh);
    }

    /* The caller tiled this view before handing it here (both map paths run
     * layout_apply first), so the desktop is still laid out as though it were
     * one of the tiles. Reflow now it has left the flow, or the windows it was
     * sharing a slot with stay squeezed around a gap. */
    if (refloated)
        layout_apply(s, view->workspace);

    /* Re-maximizing needs the restore box already in place, which the
     * view_resize above just established. */
    if (saved_max && !view->maximized)
        view_apply_maximized(s, view, 1);
    return true;
}

/* ── Reclaiming windows into the layout ──────────────────── */
/*
 * Hand every window on `ws` back to the layout: un-maximize it, drop its snap,
 * and clear `floating`. Returns how many it took back.
 *
 * velle, 2026-07-31: "if it's in tile mode i want the tiling to work, this
 * isn't intuitive." A tiling desktop can quietly end up with nothing to tile,
 * because four things set view->floating during a session — dragging a window
 * to move it (input.c), snapping it to an edge (snap.c), maximizing it
 * (view_apply_maximized), and Super+F — and until now the ONLY thing that ever
 * cleared it again was Super+F, one window at a time. So one drag took a window
 * out of the tiler for the rest of the session, selecting the tiling layout did
 * not bring it back, and the desktop looked like a tiler that had stopped
 * working. It had not: it was running on an empty set.
 *
 * Maximize deserves its own note, because it is the one that looks like it
 * should help and cannot. view_apply_maximized records saved_floating on the
 * way in and restores it on the way out, so for a window that is ALREADY
 * floating it reads 1 and writes 1 back — a fixed point. Maximizing and
 * un-maximizing a floating window re-confirms floating every time, which is
 * exactly what "even if i try to remaximize and try again" was describing.
 * saved_floating is therefore cleared here too, or the next maximize/restore
 * cycle would undo this one.
 *
 * Two kinds of window are left alone, because neither is in the flow by
 * mistake: a fullscreen window (it is deliberately the whole output), and a
 * dialog — an X11 modal or transient, or an xdg toplevel with a parent. Tiling
 * a file picker into a master slot is not what "make tiling work" means, and it
 * is the same exclusion geom_persist applies for the same reason.
 */
int layout_reclaim(syn_server_t *s, syn_workspace_t *ws)
{
    if (!ws) return 0;

    int taken = 0;
    syn_view_t *v, *tmp;
    /* _safe: view_apply_maximized() below calls layout_apply(), and a
     * reflow is not something to iterate a list across unguarded. */
    wl_list_for_each_safe(v, tmp, &ws->windows, link) {
        if (!v->mapped || v->fullscreen || v->override_redirect) continue;

        if (v->is_xwayland) {
            if (v->xsurface->parent || v->xsurface->modal) continue;
        } else if (v->xdg_surface->toplevel->parent) {
            continue;
        }

        if (!v->floating && !v->maximized && v->snapped == SYN_SNAP_NONE)
            continue;                       /* already the layout's */

        /* Through the real path, so the client is told and its saved_geo is
         * restored rather than left describing a box it no longer has. */
        if (v->maximized)
            view_apply_maximized(s, v, 0);

        v->snapped        = SYN_SNAP_NONE;
        v->floating       = 0;
        v->saved_floating = 0;
        taken++;
    }

    if (taken)
        layout_apply(s, ws);
    return taken;
}

/* ── Floating placement ──────────────────────────────────── */
/*
 * Give a newly-floating window a sane geometry: prefer the client's own
 * preferred size, clamp it to the output, and centre it. Called when a
 * window is toggled floating (Super+F) or auto-floated for a drag.
 */
void layout_float_place(syn_server_t *s, syn_view_t *view)
{
    struct wlr_box area;
    get_view_geom(s, view, &area);

    /* Where this app was last left wins over the centred default. */
    if (layout_restore_geometry(s, view))
        return;

    int w = view->w, h = view->h;
    /* The frame has to hold the client plus its chrome. */
    int bw = view_deco_border(view);
    int th = view_deco_titlebar(view);

    /* Prefer the surface's natural size. */
    if (view->is_xwayland) {
        if (view->xsurface->width > 0 && view->xsurface->height > 0) {
            w = view->xsurface->width  + 2 * bw;
            h = view->xsurface->height + 2 * bw + th;
        }
    } else {
        struct wlr_box geo = view->xdg_surface->geometry;
        if (geo.width > 0 && geo.height > 0) {
            w = geo.width  + 2 * bw;
            h = geo.height + 2 * bw + th;
        }
    }

    /* Fall back to two-thirds of the output if the size is unusable. */
    if (w < MIN_WIN || w > area.width)  w = area.width  * 2 / 3;
    if (h < MIN_WIN || h > area.height) h = area.height * 2 / 3;

    int x = area.x + (area.width  - w) / 2;
    int y = area.y + (area.height - h) / 2;
    view_resize(view, x, y, w, h);
}

/* ── Move focused view within the tiling stack ───────────── */
/* dir > 0 → toward tail (down the stack), dir < 0 → toward head (master). */
void layout_move_in_stack(syn_server_t *s, syn_view_t *view, int dir)
{
    if (!view) return;
    syn_workspace_t *ws = view->workspace;
    if (wl_list_length(&ws->windows) < 2) return;

    struct wl_list *head = &ws->windows;
    struct wl_list *self = &view->link;
    struct wl_list *anchor;   /* self is re-inserted immediately after this node */

    if (dir > 0)
        anchor = (self->next == head) ? head : self->next;
    else
        anchor = (self->prev == head) ? head->prev : self->prev->prev;

    wl_list_remove(self);
    wl_list_insert(anchor, self);
    layout_apply(s, ws);
}

/* ── Adjust the master column width ──────────────────────── */
void layout_adjust_master(syn_server_t *s, syn_workspace_t *ws, float delta)
{
    float mf = ws->master_factor;
    if (mf < MASTER_MIN || mf > MASTER_MAX) mf = MASTER_FACTOR;
    mf += delta;
    if (mf < MASTER_MIN) mf = MASTER_MIN;
    if (mf > MASTER_MAX) mf = MASTER_MAX;
    ws->master_factor = mf;
    layout_apply(s, ws);
}
