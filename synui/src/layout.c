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
 * https://synapseos.dev
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
        .id   = (uint64_t)(uintptr_t)ws,
    };
    strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
    ai_thread_send(s, &req);

    /* Apply tiling immediately as placeholder; AI response will
     * arrive asynchronously and trigger layout_apply_ai_response() */
    layout_tile(s, ws);
}

/* ── Apply AI layout response ────────────────────────────── */
void layout_apply_ai_response(syn_server_t *s, syn_workspace_t *ws,
                               const char *json_response)
{
    struct wlr_box area;
    get_output_geom(s, &area);

    /* Parse each JSON line */
    char copy[2048];
    strncpy(copy, json_response, sizeof(copy) - 1);

    char *line = strtok(copy, "\n");
    while (line) {
        char app_id[64] = {0};
        float fx, fy, fw, fh;

        if (sscanf(line, "{\"app\":\"%63[^\"]\",\"x\":%f,\"y\":%f,\"w\":%f,\"h\":%f}",
                   app_id, &fx, &fy, &fw, &fh) == 5) {

            /* Find matching window */
            syn_view_t *v;
            wl_list_for_each(v, &ws->windows, link) {
                if (!v->mapped || v->floating) continue;
                const char *aid = v->xdg_surface->toplevel->app_id;
                if (aid && strcmp(aid, app_id) == 0) {
                    int nx = area.x + (int)(fx * area.width);
                    int ny = area.y + (int)(fy * area.height);
                    int nw = (int)(fw * area.width);
                    int nh = (int)(fh * area.height);
                    /* Clamp and apply gap */
                    nw = nw > GAP * 2 ? nw - GAP : nw;
                    nh = nh > GAP * 2 ? nh - GAP : nh;
                    place_view(v, nx + GAP/2, ny + GAP/2, nw, nh);
                    break;
                }
            }
        }
        line = strtok(NULL, "\n");
    }
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
