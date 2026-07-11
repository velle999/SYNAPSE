/*
 * layout.c — SynapseOS window layout engine
 *
 * Implements three layout modes:
 *
 *   TILING   — Master-stack tiling (dwm-style)
 *              First window is master (left, 60% width).
 *              Remaining windows stack right.
 *
 *   MONOCLE  — All windows fullscreen, cycle with Alt+Tab.
 *
 *   AI       — Ask synapd to suggest positions based on
 *              workspace intent + running apps. If AI is
 *              unavailable, falls back to TILING.
 *
 * Border width: 2px (configurable).
 * Gap between windows: 8px.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synui.h"

/* border width and gap come from s->config (synuirc `border_width`/`gap`,
 * live-reloadable via SIGHUP) */
#define MASTER_FACTOR  0.60f   /* default master column width fraction */
#define MASTER_MIN     0.10f
#define MASTER_MAX     0.90f
#define MIN_WIN        40      /* smallest interactive window size, px */

/* ── Get output geometry for a workspace ─────────────────── */
/* Lay out on the output the workspace is assigned to (falling back to the
 * focused output for an unassigned one), minus any layer-shell exclusive
 * zones so tiling doesn't cover panels/bars. */
static void get_output_geom(syn_server_t *s, syn_workspace_t *ws,
                            struct wlr_box *out)
{
    syn_output_t *o = (ws && ws->output) ? ws->output
                                         : server_focused_output(s);
    output_usable_box_of(s, o, out);
}

/* ── Count mapped windows in workspace ───────────────────── */
static int count_windows(syn_workspace_t *ws)
{
    int n = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped && !v->floating && !v->fullscreen && !v->minimized)
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

    int bw = view->server->config.border_width;
    int iw = w - 2 * bw;
    int ih = h - 2 * bw;
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;

    /* Commit the size to the client. X11 clients also need their absolute
     * layout position, which the xdg path derives from the scene node. */
    if (view->is_xwayland)
        wlr_xwayland_surface_configure(view->xsurface, x, y, iw, ih);
    else
        wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, iw, ih);

    /* Move the scene tree node */
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);

    view_update_borders(view);
}
#define place_view(v, x, y, w, h) view_resize((v), (x), (y), (w), (h))

/* ── TILING layout (master-stack) ────────────────────────── */
void layout_tile(syn_server_t *s, syn_workspace_t *ws)
{
    struct wlr_box area;
    get_output_geom(s, ws, &area);

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

    int n = count_windows(ws);
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
void layout_monocle(syn_server_t *s, syn_workspace_t *ws)
{
    struct wlr_box area;
    get_output_geom(s, ws, &area);

    /* Show exactly one window: the focused view if it lives on this
     * workspace, else the first mapped one. Keying off the *global* focused
     * view would blank the whole workspace whenever focus sits on another
     * output (or on nothing, right after a close). */
    syn_view_t *top = NULL;
    syn_view_t *v;
    if (s->focused_view && s->focused_view->workspace == ws &&
        s->focused_view->mapped && !s->focused_view->floating &&
        !s->focused_view->minimized)
        top = s->focused_view;
    else
        wl_list_for_each(v, &ws->windows, link)
            if (v->mapped && !v->floating && !v->minimized) { top = v; break; }

    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->minimized) continue;
        place_view(v,
                   area.x, area.y,
                   area.width, area.height);
        wlr_scene_node_set_enabled(&v->scene_tree->node, v == top);
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
void layout_request_ai(syn_server_t *s, syn_workspace_t *ws)
{
    if (!atomic_load(&s->ai_connected)) {
        layout_tile(s, ws);
        return;
    }

    /* Build window list string */
    char win_list[1024] = {0};
    int pos = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating) continue;
        const char *title = view_title(v) ? view_title(v) : "unknown";
        const char *app   = view_app_id(v) ? view_app_id(v) : "unknown";
        pos += snprintf(win_list + pos, sizeof(win_list) - pos,
                        "  - app=%s title=\"%.30s\"\n", app, title);
        if (pos >= (int)sizeof(win_list) - 64) break;
    }

    if (!win_list[0]) {
        layout_tile(s, ws);
        return;
    }

    struct wlr_box area;
    get_output_geom(s, ws, &area);

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
    layout_tile(s, ws);
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
    struct wlr_box area;
    get_output_geom(s, ws, &area);

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
                if (!v->mapped || v->floating) continue;
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
            wlr_scene_node_set_enabled(&v->scene_tree->node, true);

    /* AI-managed marking (and its cyan border) only persists under the AI
     * layout; clear it for the other layouts so the border reflects reality. */
    if (ws->layout != LAYOUT_AI)
        wl_list_for_each(v, &ws->windows, link)
            v->ai_ctx.has_ctx = 0;

    switch (ws->layout) {
    case LAYOUT_TILING:   layout_tile(s, ws);        break;
    case LAYOUT_MONOCLE:  layout_monocle(s, ws);     break;
    case LAYOUT_AI:       layout_request_ai(s, ws);  break;
    case LAYOUT_FLOATING: /* no-op: user positions windows */ break;
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
    return (view->workspace && view->workspace->output)
               ? view->workspace->output
               : server_focused_output(s);
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
        syn_output_t *o = fullscreen_target_output(s, view);
        /* Fullscreening onto another monitor hands the window to that
         * monitor's workspace, so it stays put when the workspace it came
         * from is switched away, and untiles back where it is shown. */
        if (o && view->workspace && view->workspace->output != o)
            workspace_move_view(s, view, o->active_workspace);
        struct wlr_box area;
        output_box_of(s, o, &area);
        view->x = area.x;    view->y = area.y;
        view->w = area.width; view->h = area.height;
        if (view->is_xwayland)
            wlr_xwayland_surface_configure(view->xsurface, area.x, area.y,
                                           area.width, area.height);
        else
            wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel,
                                      area.width, area.height);
        wlr_scene_node_set_position(&view->scene_tree->node, area.x, area.y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
        view_update_borders(view);
        /* A sub-native X11 client (old game locked to 1080p) fills the box by
         * scaling its buffer; re-applied per-commit from xw_surface_commit. */
        view_fullscreen_rescale(view);
    } else {
        layout_apply(s, view->workspace);
        if (view->floating)
            layout_float_place(s, view);
        view_update_borders(view);
        /* Undo any fullscreen buffer scale now the view is back in the layout. */
        view_fullscreen_rescale(view);
    }

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
    wlr_scene_node_set_enabled(&view->scene_tree->node, show);

    if (view->minimized) {
        if (s->focused_view == view)
            workspace_focus_first(s, view->workspace);
    } else if (show) {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
        focus_view(s, view, view_surface(view));
    }

    layout_apply(s, view->workspace);
}

/* Focus the first mapped, non-minimized window on ws — or clear focus
 * entirely if there is none, so keyboard input can't keep flowing to a
 * hidden window. */
void workspace_focus_first(syn_server_t *s, syn_workspace_t *ws)
{
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (v->mapped && !v->minimized) {
            focus_view(s, v, view_surface(v));
            return;
        }
    }
    focus_view(s, NULL, NULL);
}

/* ── Workspace switching ─────────────────────────────────── */
void workspace_switch(syn_server_t *s, int index)
{
    if (index < 0 || index >= WORKSPACE_MAX) return;
    syn_output_t *o = server_focused_output(s);
    if (!o) return;
    if (o->active_workspace == index) return;

    syn_workspace_t *target = &s->workspaces[index];

    /* Already visible on another output? Jump focus there instead of
     * stealing the workspace (i3/sway semantics): warp the cursor onto that
     * output so focused-output resolution follows the user. */
    if (workspace_visible(target) && target->output != o) {
        struct wlr_box b;
        output_box_of(s, target->output, &b);
        wlr_cursor_warp(s->cursor, NULL,
                        b.x + b.width / 2.0, b.y + b.height / 2.0);
        /* Warping doesn't emit a motion event; re-derive pointer focus now. */
        pointer_update_focus(s, 0);
    } else {
        /* Hide this output's current workspace */
        syn_workspace_t *cur = &s->workspaces[o->active_workspace];
        syn_view_t *v;
        wl_list_for_each(v, &cur->windows, link)
            if (v->mapped)
                wlr_scene_node_set_enabled(&v->scene_tree->node, false);
        cur->visible = 0;

        /* Show the target here (re-homing it if it lived elsewhere) */
        o->active_workspace = index;
        target->output  = o;
        target->visible = 1;
        wl_list_for_each(v, &target->windows, link)
            if (v->mapped)
                wlr_scene_node_set_enabled(&v->scene_tree->node, true);

        layout_apply(s, target);
    }

    /* Focus the target workspace's first window (or clear focus if empty —
     * the previous workspace's window is hidden now). */
    workspace_focus_first(s, target);

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

void workspace_move_view(syn_server_t *s, syn_view_t *view, int ws_index)
{
    if (ws_index < 0 || ws_index >= WORKSPACE_MAX) return;
    int old_ws = view->workspace->index;
    if (old_ws == ws_index) return;

    view->workspace = &s->workspaces[ws_index];
    wl_list_remove(&view->link);
    wl_list_insert(&view->workspace->windows, &view->link);

    /* Visible only if the target workspace is shown on some output. */
    wlr_scene_node_set_enabled(&view->scene_tree->node,
                                workspace_visible(view->workspace));

    layout_apply(s, &s->workspaces[old_ws]);
    layout_apply(s, &s->workspaces[ws_index]);

    /* If the moved window was focused and is now hidden, hand focus to the
     * old workspace so keys don't keep going to an invisible window. */
    if (s->focused_view == view && !workspace_visible(view->workspace))
        workspace_focus_first(s, &s->workspaces[old_ws]);
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
    get_output_geom(s, view->workspace, &area);

    int w = view->w, h = view->h;
    int bw = s->config.border_width;

    /* Prefer the surface's natural size. */
    if (view->is_xwayland) {
        if (view->xsurface->width > 0 && view->xsurface->height > 0) {
            w = view->xsurface->width  + 2 * bw;
            h = view->xsurface->height + 2 * bw;
        }
    } else {
        struct wlr_box geo = view->xdg_surface->geometry;
        if (geo.width > 0 && geo.height > 0) {
            w = geo.width  + 2 * bw;
            h = geo.height + 2 * bw;
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
