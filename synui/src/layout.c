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

/* BORDER_WIDTH defined in synui.h */
#define GAP            8
#define MASTER_FACTOR  0.60f   /* master window takes 60% of width */

/* ── Get output geometry for a workspace ─────────────────── */
static void get_output_geom(syn_server_t *s, struct wlr_box *out)
{
    /* Use the first connected output */
    syn_output_t *output;
    wl_list_for_each(output, &s->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout,
                                   output->wlr_output, &box);
        *out = box;
        return;
    }
    /* Fallback */
    out->x = 0; out->y = 0; out->width = 1920; out->height = 1080;
}

/* ── Count mapped windows in workspace ───────────────────── */
static int count_windows(syn_workspace_t *ws)
{
    int n = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped && !v->floating && !v->fullscreen)
            n++;
    return n;
}

/* ── Tile a view ─────────────────────────────────────────── */
static void place_view(syn_view_t *view, int x, int y, int w, int h)
{
    view->x = x;
    view->y = y;
    view->w = w;
    view->h = h;

    /* Commit the size to the xdg toplevel */
    wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel,
                               w - 2 * BORDER_WIDTH,
                               h - 2 * BORDER_WIDTH);

    /* Move the scene tree node */
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);

    view_update_borders(view);
}

/* ── TILING layout (master-stack) ────────────────────────── */
void layout_tile(syn_server_t *s, syn_workspace_t *ws)
{
    struct wlr_box area;
    get_output_geom(s, &area);

    /* Apply outer gap */
    int x = area.x + GAP;
    int y = area.y + GAP;
    int W = area.width  - 2 * GAP;
    int H = area.height - 2 * GAP;

    int n = count_windows(ws);
    if (n == 0) return;

    int master_w = (n == 1) ? W : (int)(W * MASTER_FACTOR) - GAP / 2;
    int stack_w  = W - master_w - GAP;
    int stack_x  = x + master_w + GAP;

    int i = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->fullscreen) continue;

        if (i == 0) {
            /* Master */
            place_view(v, x, y, master_w, H);
        } else {
            /* Stack */
            int nstack = n - 1;
            int slot_h = (H - (nstack - 1) * GAP) / nstack;
            int vy = y + (i - 1) * (slot_h + GAP);
            place_view(v, stack_x, vy, stack_w, slot_h);
        }
        i++;
    }
}

/* ── MONOCLE layout ──────────────────────────────────────── */
void layout_monocle(syn_server_t *s, syn_workspace_t *ws)
{
    struct wlr_box area;
    get_output_geom(s, &area);

    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating) continue;
        place_view(v,
                   area.x, area.y,
                   area.width, area.height);
        /* Only the focused view should be visible */
        wlr_scene_node_set_enabled(&v->scene_tree->node,
                                    v == s->focused_view);
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
        const char *title = v->xdg_surface->toplevel->title
                            ? v->xdg_surface->toplevel->title : "unknown";
        const char *app   = v->xdg_surface->toplevel->app_id
                            ? v->xdg_surface->toplevel->app_id : "unknown";
        pos += snprintf(win_list + pos, sizeof(win_list) - pos,
                        "  - app=%s title=\"%.30s\"\n", app, title);
        if (pos >= (int)sizeof(win_list) - 64) break;
    }

    if (!win_list[0]) {
        layout_tile(s, ws);
        return;
    }

    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "[LAYOUT_REQUEST]\n"
        "workspace: %s\n"
        "intent: %s\n"
        "windows:\n%s\n"
        "output: 1920x1080\n"
        "\n"
        "Suggest a tiling layout. For each window reply with one JSON object per line:\n"
        "{\"app\":\"APP_ID\",\"x\":FRAC,\"y\":FRAC,\"w\":FRAC,\"h\":FRAC}\n"
        "x,y,w,h are fractions 0.0-1.0 of output dimensions. No explanation.",
        ws->name,
        ws->intent[0] ? ws->intent : "general use",
        win_list
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
    get_output_geom(s, &area);

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
                const char *aid = v->xdg_surface->toplevel->app_id;
                if (aid && strcmp(aid, app_id) == 0) {
                    int nx = area.x + (int)(fx * area.width);
                    int ny = area.y + (int)(fy * area.height);
                    int nw = (int)(fw * area.width);
                    int nh = (int)(fh * area.height);
                    nw = nw > GAP * 2 ? nw - GAP : nw;
                    nh = nh > GAP * 2 ? nh - GAP : nh;
                    /* Mark AI-managed before placing so the border picks the
                     * AI colour, and record the app as the window's intent. */
                    v->ai_ctx.has_ctx = 1;
                    snprintf(v->ai_ctx.intent, sizeof(v->ai_ctx.intent),
                             "%s", app_id);
                    place_view(v, nx + GAP/2, ny + GAP/2, nw, nh);
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

    /* Re-enable all nodes first */
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped)
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

/* ── Workspace switching ─────────────────────────────────── */
void workspace_switch(syn_server_t *s, int index)
{
    if (index < 0 || index >= WORKSPACE_MAX) return;
    if (index == s->active_workspace) return;

    /* Hide current workspace windows */
    syn_view_t *v;
    wl_list_for_each(v, &s->workspaces[s->active_workspace].windows, link)
        if (v->mapped)
            wlr_scene_node_set_enabled(&v->scene_tree->node, false);

    s->workspaces[s->active_workspace].visible = 0;
    s->active_workspace = index;
    s->workspaces[index].visible = 1;

    /* Show new workspace windows */
    wl_list_for_each(v, &s->workspaces[index].windows, link)
        if (v->mapped)
            wlr_scene_node_set_enabled(&v->scene_tree->node, true);

    layout_apply(s, &s->workspaces[index]);

    /* Focus first window on new workspace */
    if (!wl_list_empty(&s->workspaces[index].windows)) {
        syn_view_t *first = wl_container_of(
            s->workspaces[index].windows.next, first, link);
        if (first->mapped)
            focus_view(s, first, first->xdg_surface->surface);
    }

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

    wl_list_remove(&view->link);
    wlr_scene_node_set_enabled(&view->scene_tree->node,
                                ws_index == s->active_workspace);

    view->workspace = &s->workspaces[ws_index];
    wl_list_insert(&view->workspace->windows, &view->link);

    layout_apply(s, &s->workspaces[old_ws]);
    layout_apply(s, &s->workspaces[ws_index]);
}
