/*
 * synui_main.c — SynapseOS Wayland Compositor
 *
 * Entry point and wlroots initialization.
 *
 * wlroots gives us:
 *   - Backend abstraction (DRM/KMS, Wayland, X11 nested, headless)
 *   - Scene graph for compositing
 *   - XDG shell surface management
 *   - Input (libinput via wlroots)
 *   - Output management
 *
 * We add on top:
 *   - AI layout engine (synapd IPC)
 *   - Neural overlay (rendered each frame)
 *   - Command bar (Super+Space)
 *   - Security borders (synguard event feed)
 *   - Workspace intents
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <time.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>

#include "synui.h"
#include "effects.h"

/* ── Signal handling ─────────────────────────────────────── */
static int handle_terminate_signal(int sig, void *data)
{
    struct wl_display *display = data;
    wlr_log(WLR_INFO, "synui: caught signal %d — terminating", sig);
    wl_display_terminate(display);
    return 0;
}

static int handle_reload_signal(int sig, void *data)
{
    (void)sig;
    synui_config_reload(data);
    return 0;
}

/* Reparse synuirc and apply everything that makes sense at runtime:
 * keybindings (the bind table is read per keypress), keymap + repeat,
 * libinput options, border_width/gap (re-tile + redraw borders), wallpaper
 * (re-decode + repaint every output), and the ai/terminal knobs (read at
 * use). Autostart entries are start-only, and per-workspace master factors
 * keep any interactively adjusted value. */
void synui_config_reload(syn_server_t *s)
{
    syn_config_t fresh = {0};
    synui_config_load(&fresh);
    s->config = fresh;

    wallpaper_reload(s);
    input_reload_config(s);

    /* Re-tile every output's visible workspace with the new gap/border, and
     * refresh every mapped view's border rects (floating windows aren't
     * touched by the layout pass). Hidden workspaces re-flow on switch. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        layout_apply(s, &s->workspaces[o->active_workspace]);
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            view_update_borders(v);
    }

    wlr_log(WLR_INFO, "synui: config reloaded (%d binds, gap %d, border %d)",
            s->config.bind_count, s->config.gap, s->config.border_width);
}

/* ── Active output resolution ────────────────────────────── */
syn_output_t *server_focused_output(syn_server_t *s)
{
    /* 1. The output under the cursor. */
    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, s->cursor->x, s->cursor->y);
    if (wo && wo->data) return wo->data;

    /* 2. The output holding the focused window (by its centre). */
    if (s->focused_view && s->focused_view->mapped) {
        double cx = s->focused_view->x + s->focused_view->w / 2.0;
        double cy = s->focused_view->y + s->focused_view->h / 2.0;
        wo = wlr_output_layout_output_at(s->output_layout, cx, cy);
        if (wo && wo->data) return wo->data;
    }

    /* 3. The first connected output. */
    if (!wl_list_empty(&s->outputs)) {
        syn_output_t *o = wl_container_of(s->outputs.next, o, link);
        return o;
    }
    return NULL;
}

syn_workspace_t *server_active_workspace(syn_server_t *s)
{
    syn_output_t *o = server_focused_output(s);
    int idx = o ? o->active_workspace : 0;
    if (idx < 0 || idx >= WORKSPACE_MAX) idx = 0;
    return &s->workspaces[idx];
}

int workspace_visible(syn_workspace_t *ws)
{
    return ws && ws->output && ws->output->active_workspace == ws->index;
}

void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    if (o) {
        wlr_output_layout_get_box(s->output_layout, o->wlr_output, box);
        if (box->width > 0 && box->height > 0) return;
    }
    *box = (struct wlr_box){ 0, 0, 1920, 1080 };
}

void output_usable_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    if (o && o->usable_area.width > 0 && o->usable_area.height > 0) {
        *box = o->usable_area;
        return;
    }
    output_box_of(s, o, box);
}

void server_output_box(syn_server_t *s, struct wlr_box *box)
{
    output_box_of(s, server_focused_output(s), box);
}

void server_usable_box(syn_server_t *s, struct wlr_box *box)
{
    output_usable_box_of(s, server_focused_output(s), box);
}

/* ── Output events ───────────────────────────────────────── */
static void output_frame(struct wl_listener *listener, void *data)
{
    syn_output_t *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;

    /* Apply any pending synguard security verdicts to window borders. */
    secfeed_dispatch(output->server);

    /* Poll for AI responses (non-blocking) and route by request type. */
    syn_ai_response_t resp;
    if (ai_thread_poll(output->server, &resp) == 0) {
        syn_server_t *server = output->server;
        switch (resp.type) {
        case AI_MSG_QUERY_CMD:
            if (server->cmdbar.visible && server->cmdbar.waiting) {
                server->cmdbar.waiting = 0;
                execute_ai_action(server, resp.response);
                synui_render_cmdbar(server);
            }
            break;
        case AI_MSG_QUERY_LAYOUT:
            /* request_id carries the target workspace index */
            if (resp.request_id < WORKSPACE_MAX) {
                syn_workspace_t *ws = &server->workspaces[resp.request_id];
                if (ws->layout == LAYOUT_AI)
                    layout_apply_ai_response(server, ws, resp.response);
            }
            break;
        case AI_MSG_STATUS_UPDATE:
            /* Surface the AI's status text in the neural overlay. */
            snprintf(server->overlay.ai_context,
                     sizeof(server->overlay.ai_context), "%s", resp.response);
            if (server->overlay.visible)
                synui_render_overlay(server);
            break;
        }
    }

    /* GLES post-process pass when available; plain scene commit otherwise
     * (and whenever any step of the effects pass fails). */
    if (!effects_output_commit(output))
        wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
    syn_output_t *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
    syn_output_t *output = wl_container_of(listener, output, destroy);
    syn_server_t *server = output->server;
    /* Close any layer surfaces (panels/bars) anchored to this output. */
    layer_output_destroy(output);
    effects_output_destroy(output);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    /* Clear the back-pointer before freeing: the dying wlr_output may still be
     * momentarily reachable via the output layout, and server_focused_output()
     * dereferences ->data — leave it NULL so that lookup skips this output. */
    output->wlr_output->data = NULL;

    /* Orphan every workspace that lived on this output: hide its windows and
     * mark it unassigned. The windows aren't lost — switching to the
     * workspace (Super+N) re-homes it onto the focused output (i3/sway
     * semantics). Skipped during shutdown, when the scene graph is gone. */
    if (!server->shutting_down) {
        wallpaper_output_destroy(output);
        dock_output_destroy(output);
        for (int i = 0; i < WORKSPACE_MAX; i++) {
            syn_workspace_t *ws = &server->workspaces[i];
            if (ws->output != output) continue;
            ws->output  = NULL;
            ws->visible = 0;
            syn_view_t *v;
            wl_list_for_each(v, &ws->windows, link)
                if (v->mapped)
                    wlr_scene_node_set_enabled(&v->scene_tree->node, false);
            wlr_log(WLR_INFO, "synui: workspace %d orphaned by output removal "
                    "— switch to it to re-home its windows", i + 1);
        }
    }
    free(output);

    /* Re-home the compositor UI onto a surviving output. */
    if (!server->shutting_down && !wl_list_empty(&server->outputs)) {
        if (server->welcome_ui.shown)
            synui_render_welcome(server);
        if (server->overlay.visible)
            synui_render_overlay(server);
        if (server->cmdbar.visible)
            synui_render_cmdbar(server);
        /* Drop the freed output from the display panel's arrangement. */
        dispcfg_outputs_changed(server);
    }
    if (!server->shutting_down)
        output_mgmt_update(server);
}

static void server_new_output(struct wl_listener *listener, void *data)
{
    syn_server_t *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* Configure output state */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    syn_output_t *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;
    wlr_output->data = output;
    wl_list_init(&output->layer_surfaces);

    /* Seed the dispcfg grid cell from connection order — one row, in the
     * order outputs were plugged in — matching wlr_output_layout_add_auto's
     * left-to-right placement below. The display panel (Super+D) is where
     * this gets rearranged into an L-shape or whatever the desk needs. */
    output->grid_x = wl_list_length(&server->outputs);
    output->grid_y = 0;

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    /* Restore a saved mode/transform/scale/position for this connector, if
     * one was persisted by a previous session; otherwise fall back to
     * auto-placement beside whatever's already laid out. */
    struct wlr_output_layout_output *l_output =
        output_persist_apply(server, output);
    if (!l_output)
        l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);

    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                       output->scene_output);

    wl_list_insert(&server->outputs, &output->link);

    /* Give the new output its own workspace. Prefer an orphaned workspace
     * that still has windows (its output was unplugged — replugging brings
     * them back), then the lowest-numbered unassigned one. */
    int assigned = -1;
    for (int i = 0; i < WORKSPACE_MAX && assigned < 0; i++) {
        syn_workspace_t *ws = &server->workspaces[i];
        if (!ws->output && !wl_list_empty(&ws->windows))
            assigned = i;
    }
    for (int i = 0; i < WORKSPACE_MAX && assigned < 0; i++) {
        if (!server->workspaces[i].output)
            assigned = i;
    }
    if (assigned < 0)   /* all 9 somehow assigned: fall back to ws 0 */
        assigned = 0;

    syn_workspace_t *ws = &server->workspaces[assigned];
    output->active_workspace = assigned;
    ws->output  = output;
    ws->visible = 1;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped)
            wlr_scene_node_set_enabled(&v->scene_tree->node, true);

    wlr_log(WLR_INFO, "synui: new output %s %dx%d — workspace %d",
            wlr_output->name, wlr_output->width, wlr_output->height,
            assigned + 1);

    /* Seed the usable area (full box; no layer surfaces yet), then lay the
     * workspace out on it and re-home all UI. */
    layer_arrange_output(output);
    wallpaper_output_created(output);
    dock_output_created(output);
    layout_apply(server, ws);
    if (server->welcome_ui.shown)
        synui_render_welcome(server);
    if (server->overlay.visible)
        synui_render_overlay(server);
    if (server->cmdbar.visible)
        synui_render_cmdbar(server);
    dispcfg_outputs_changed(server);

    output_mgmt_update(server);
}

/* ── XDG surface events ──────────────────────────────────── */
static void xdg_surface_map(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, map);
    view->mapped = 1;
    focus_view(view->server, view, view->xdg_surface->surface);
    layout_apply(view->server, view->workspace);
    foreign_toplevel_map(view);

    /* Hide welcome screen when first window opens */
    synui_welcome_hide(view->server);
}

static void xdg_surface_unmap(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, unmap);
    syn_server_t *server = view->server;
    int was_focused = (server->focused_view == view);
    /* L2 (interim): a closing window fires a brief screen glitch. */
    effects_notify_close(server);
    view->mapped = 0;
    foreign_toplevel_unmap(view);
    /* Drop focus/grab references to this window */
    if (server->focused_view == view)
        server->focused_view = NULL;
    if (server->grabbed_view == view) {
        server->grabbed_view = NULL;
        server->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
    }
    /* Remove borders from scene */
    if (view->border_top)    { wlr_scene_node_destroy(&view->border_top->node);    view->border_top    = NULL; }
    if (view->border_bottom) { wlr_scene_node_destroy(&view->border_bottom->node); view->border_bottom = NULL; }
    if (view->border_left)   { wlr_scene_node_destroy(&view->border_left->node);   view->border_left   = NULL; }
    if (view->border_right)  { wlr_scene_node_destroy(&view->border_right->node);  view->border_right  = NULL; }

    /* Reflow the remaining tiled windows and hand focus to one of them
     * (the XWayland unmap path already re-tiled; this one never did). */
    if (!server->shutting_down) {
        layout_apply(server, view->workspace);
        if (was_focused)
            workspace_focus_first(server, view->workspace);
    }
}

static void xdg_surface_destroy(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, destroy);
    foreign_toplevel_unmap(view);   /* no-op if unmap already retracted it */
    if (view->server->focused_view == view)
        view->server->focused_view = NULL;
    if (view->server->grabbed_view == view) {
        view->server->grabbed_view = NULL;
        view->server->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
    }
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->link);
    free(view);
}

static void xdg_surface_commit(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, commit);
    if (view->xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
        return;

    /* wlroots 0.19 requires the compositor to answer the initial commit with
     * a configure, or the client waits forever and never maps. (Clients using
     * xdg-decoration used to get one as a side effect of our set_mode; plain
     * xdg clients hung.) 0x0 lets the client pick its own size; the layout
     * resizes it on map. */
    if (view->xdg_surface->initial_commit) {
        wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, 0, 0);
        /* Apply a maximize request the client made before this first
         * commit (see xdg_toplevel_request_maximize) now that the surface
         * is actually initialized. */
        if (view->maximized)
            wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, view->maximized);
        return;
    }

    /* Update borders when surface geometry changes */
    if (view->mapped)
        view_update_borders(view);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_maximize);
    /* Honour the requested state (not a blind toggle). Tiling still governs
     * the geometry; set_maximized acks with the required configure. */
    view->maximized = view->xdg_surface->toplevel->requested.maximized;
    /* Clients may send set_maximized before their first commit, while
     * xdg_surface->initialized is still false. wlroots asserts on that in
     * wlr_xdg_surface_schedule_configure(), aborting the whole compositor.
     * The state is already recorded above; xdg_surface_commit's
     * initial_commit path will pick it up once the surface is ready. */
    if (!view->xdg_surface->initialized)
        return;
    wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, view->maximized);
    foreign_toplevel_update_state(view);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_fullscreen);
    /* Honour the state the client asked for (not a blind toggle), and give
     * the window real fullscreen geometry / hand it back to the layout. */
    view_apply_fullscreen(view->server, view,
                          view->xdg_surface->toplevel->requested.fullscreen);
}

/*
 * wlroots 0.19 splits surface creation into role-specific signals that fire
 * only once the role is known — new_surface no longer guarantees a role, so we
 * subscribe to new_toplevel / new_popup instead of asserting the role here.
 */
struct syn_popup_watch {
    struct wlr_xdg_popup *popup;
    syn_server_t *server;
    syn_view_t   *parent_view;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void popup_watch_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_popup_watch *w = wl_container_of(listener, w, destroy);
    wl_list_remove(&w->commit.link);
    wl_list_remove(&w->destroy.link);
    free(w);
}

/* wlroots 0.19 requires the compositor to answer a popup's initial commit
 * with a configure the same way toplevels do (see xdg_surface_commit) — a
 * client that waits for a real ack, like GTK4's, otherwise hangs unmapped
 * forever and never shows or accepts input. */
static void popup_watch_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_popup_watch *w = wl_container_of(listener, w, commit);
    wlr_log(WLR_INFO, "synui: DEBUG popup_watch_commit surf=%p initial_commit=%d mapped=%d",
            (void *)w->popup->base->surface, w->popup->base->initial_commit,
            w->popup->base->surface->mapped);
    if (!w->popup->base->initial_commit)
        return;

    /* layer.c's (working) popup path always unconstrains before the first
     * configure; this toplevel-parented path never did, so a popup near a
     * screen edge (e.g. Firefox's hamburger menu, which runs ~715px tall)
     * could render extending past the output bounds instead of being
     * flipped/slid into view. Matches layer_popup_commit's pattern. */
    if (w->parent_view) {
        struct wlr_output *output = w->parent_view->workspace && w->parent_view->workspace->output
            ? w->parent_view->workspace->output->wlr_output : NULL;
        if (output) {
            struct wlr_box out_box;
            wlr_output_layout_get_box(w->server->output_layout, output, &out_box);
            struct wlr_box constraint = {
                .x = out_box.x - w->parent_view->x,
                .y = out_box.y - w->parent_view->y,
                .width = out_box.width,
                .height = out_box.height,
            };
            wlr_xdg_popup_unconstrain_from_box(w->popup, &constraint);
        }
    }

    wlr_xdg_surface_schedule_configure(w->popup->base);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;

    /* Layer-shell popups (waybar menus, wofi) are created parentless: the
     * client makes the xdg_popup first and attaches it afterwards via
     * zwlr_layer_surface_v1.get_popup, which fires the layer surface's own
     * new_popup — layer.c handles those there. */
    if (!popup->parent)
        return;

    struct wlr_scene_tree *parent_tree = NULL;
    struct wlr_xdg_surface *xdg_parent =
        wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (xdg_parent)
        parent_tree = xdg_parent->data;
    if (!parent_tree) {
        wlr_log(WLR_ERROR, "synui: xdg popup with no resolvable parent tree");
        return;
    }
    popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, popup->base);

    struct syn_popup_watch *w = calloc(1, sizeof(*w));
    w->popup = popup;
    w->server = s;
    w->parent_view = parent_tree->node.data;
    w->commit.notify = popup_watch_commit;
    wl_signal_add(&popup->base->surface->events.commit, &w->commit);
    w->destroy.notify = popup_watch_destroy;
    wl_signal_add(&popup->base->surface->events.destroy, &w->destroy);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    syn_server_t *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;
    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    syn_view_t *view = calloc(1, sizeof(*view));
    view->xdg_surface = xdg_surface;
    view->scene_tree = wlr_scene_xdg_surface_create(server->window_tree, xdg_surface);
    view->scene_tree->node.data = view;
    xdg_surface->data = view->scene_tree;

    /* Assign to the focused output's workspace */
    view->workspace = server_active_workspace(server);
    wl_list_insert(&view->workspace->windows, &view->link);

    view->map.notify = xdg_surface_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);

    view->unmap.notify = xdg_surface_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);

    /* Listen on the toplevel's destroy, not the surface's: under wlroots 0.19
     * the toplevel is torn down first and asserts its own signal listener
     * lists are empty, so our request_maximize/fullscreen listeners must be
     * removed before then. */
    view->destroy.notify = xdg_surface_destroy;
    wl_signal_add(&xdg_surface->toplevel->events.destroy, &view->destroy);

    view->commit.notify = xdg_surface_commit;
    wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);

    view->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_surface->toplevel->events.request_maximize,
                  &view->request_maximize);

    view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen,
                  &view->request_fullscreen);

    /* Assign server pointer so view callbacks can reach it */
    view->server = server;

    /* Check if process has AI_CTX set */
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(xdg_surface->resource),
                              &pid, NULL, NULL);
    if (pid > 0) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        FILE *f = fopen(path, "r");
        if (f) {
            char comm[32] = {0};
            fread(comm, 1, sizeof(comm)-1, f);
            fclose(f);
            /* Comm has newline — strip it */
            char *nl = strchr(comm, '\n');
            if (nl) *nl = '\0';

            /* Announce new window to AI for layout suggestion */
            char prompt[256];
            snprintf(prompt, sizeof(prompt),
                     "[WINDOW_OPENED] app=%s pid=%d workspace=%s — "
                     "suggest layout adjustment? Reply YES or NO only.",
                     comm, pid, view->workspace->name);

            /* Advisory YES/NO query — id is a sentinel outside the workspace
             * range so the frame loop's QUERY_LAYOUT router (which treats
             * request_id as a workspace index) can never feed this reply
             * into layout_apply_ai_response(). Previously the pid was used,
             * which only worked because client pids are never < 9. */
            syn_ai_request_t req = {
                .type = AI_MSG_QUERY_LAYOUT,
                .id   = UINT64_MAX,
            };
            strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
            ai_thread_send(server, &req);
        }
    }

    wlr_log(WLR_DEBUG, "synui: new toplevel");
}

/* ── xdg-decoration ──────────────────────────────────────── */
/* synui draws its own borders, so we ask clients to skip client-side
 * titlebars. set_mode() schedules a configure, which asserts the surface is
 * initialized — but a client may create the decoration before its first
 * commit, so we defer the mode until an initialized commit arrives. */
struct syn_decoration {
    struct wlr_xdg_toplevel_decoration_v1 *deco;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void decoration_apply(struct syn_decoration *d)
{
    wlr_xdg_toplevel_decoration_v1_set_mode(
        d->deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void decoration_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_decoration *d = wl_container_of(listener, d, commit);
    if (d->deco->toplevel->base->initialized) {
        decoration_apply(d);
        /* Only needed once; stop listening to further commits. */
        wl_list_remove(&d->commit.link);
        wl_list_init(&d->commit.link);
    }
}

static void decoration_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_decoration *d = wl_container_of(listener, d, destroy);
    wl_list_remove(&d->commit.link);
    wl_list_remove(&d->destroy.link);
    free(d);
}

static void server_new_decoration(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_xdg_toplevel_decoration_v1 *deco = data;

    struct syn_decoration *d = calloc(1, sizeof(*d));
    d->deco = deco;
    d->destroy.notify = decoration_destroy;
    wl_signal_add(&deco->events.destroy, &d->destroy);
    d->commit.notify = decoration_commit;
    wl_signal_add(&deco->toplevel->base->surface->events.commit, &d->commit);

    /* If the surface is already initialized, set the mode straight away. */
    if (deco->toplevel->base->initialized) {
        decoration_apply(d);
        wl_list_remove(&d->commit.link);
        wl_list_init(&d->commit.link);
    }
}

/* ── Idle inhibit ────────────────────────────────────────── */
/* Each inhibitor (e.g. a video player) suppresses idle-notify so swayidle-style
 * clients don't blank/lock the screen while it's active. */
struct syn_idle_inhibitor {
    syn_server_t      *server;
    struct wl_listener destroy;
};

static void idle_inhibitor_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_idle_inhibitor *inh = wl_container_of(listener, inh, destroy);
    syn_server_t *s = inh->server;
    if (--s->idle_inhibitors < 0) s->idle_inhibitors = 0;
    wlr_idle_notifier_v1_set_inhibited(s->idle_notifier, s->idle_inhibitors > 0);
    wl_list_remove(&inh->destroy.link);
    free(inh);
}

static void server_new_idle_inhibitor(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_idle_inhibitor);
    struct wlr_idle_inhibitor_v1 *wlr_inh = data;

    struct syn_idle_inhibitor *inh = calloc(1, sizeof(*inh));
    inh->server = s;
    inh->destroy.notify = idle_inhibitor_destroy;
    wl_signal_add(&wlr_inh->events.destroy, &inh->destroy);

    s->idle_inhibitors++;
    wlr_idle_notifier_v1_set_inhibited(s->idle_notifier, true);
}

/* ── Server init ─────────────────────────────────────────── */
int synui_init(syn_server_t *s)
{
    /* Tell synui it's a SynapseOS compositor */
    setenv("SYNUI_RUNNING", "1", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_CURRENT_DESKTOP", "SynapseOS", 1);
    /* WAYLAND_DISPLAY will be set after socket creation below */

    /* Declare AI intent to the kernel */
    struct {
        uint32_t flags;
        char intent[256];
        uint32_t priority_hint;
        uint32_t reserved[4];
    } ctx_args = {
        .flags = (1 << 5) | (1 << 2),  /* INTERACTIVE | LATENCY */
        .priority_hint = 90,
    };
    strncpy(ctx_args.intent,
            "Wayland compositor — I manage all window rendering and user input",
            sizeof(ctx_args.intent) - 1);
    syscall(NR_AI_CTX_SET, &ctx_args);

    /* Create Wayland display */
    s->display = wl_display_create();
    if (!s->display) {
        fprintf(stderr, "synui: wl_display_create() failed\n");
        return -1;
    }

    /* Terminate cleanly on SIGINT/SIGTERM. Registered before the AI/security
     * threads are spawned (in synui_run) so they inherit the blocked signal
     * mask and only the event loop's signalfd handles them. */
    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->sigint_src  = wl_event_loop_add_signal(loop, SIGINT,
                                              handle_terminate_signal, s->display);
    s->sigterm_src = wl_event_loop_add_signal(loop, SIGTERM,
                                              handle_terminate_signal, s->display);
    s->sighup_src  = wl_event_loop_add_signal(loop, SIGHUP,
                                              handle_reload_signal, s);

    /* Create wlroots backend */
    s->backend = wlr_backend_autocreate(wl_display_get_event_loop(s->display), NULL);
    if (!s->backend) {
        fprintf(stderr, "synui: wlr_backend_autocreate() failed (WLR_BACKENDS=%s WLR_RENDERER=%s)\n",
                getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "(auto)",
                getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)");
        wlr_log(WLR_ERROR, "synui: failed to create backend");
        return -1;
    }

    s->renderer = wlr_renderer_autocreate(s->backend);
    if (!s->renderer) {
        fprintf(stderr, "synui: wlr_renderer_autocreate() failed (WLR_RENDERER=%s)\n",
                getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)");
        return -1;
    }
    wlr_renderer_init_wl_display(s->renderer, s->display);

    s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
    if (!s->allocator) {
        fprintf(stderr, "synui: wlr_allocator_autocreate() failed\n");
        return -1;
    }

    /* Optional GLES post-process (CRT/scanlines/aberration); logs and
     * stays off on pixman. */
    effects_init(s);

    /* Compositor protocols */
    s->compositor = wlr_compositor_create(s->display, 5, s->renderer);
    wlr_subcompositor_create(s->display);
    wlr_data_device_manager_create(s->display);
    wlr_viewporter_create(s->display);
    wlr_presentation_create(s->display, s->backend, 1);

    /* Output layout */
    s->output_layout = wlr_output_layout_create(s->display);

    /* Scene graph */
    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);

    /* Background: dark rect so the compositor isn't pure black */
    float bg_color[4] = { 0.07f, 0.07f, 0.12f, 1.0f };
    s->bg_rect = wlr_scene_rect_create(&s->scene->tree, 8192, 8192, bg_color);
    wlr_scene_node_set_position(&s->bg_rect->node, -4096, -4096);
    wlr_scene_node_lower_to_bottom(&s->bg_rect->node);

    /* wallpaper.c: creates wallpaper_tree (just above bg_rect, since it's
     * created next) and decodes the configured image, if any. */
    wallpaper_init(s);

    /* Scene z-order layers, created bottom→top so insertion order is the stack:
     * layer[BACKGROUND] < layer[BOTTOM] < window_tree < layer[TOP] <
     * layer[OVERLAY]. The compositor UI trees (render.c) are created later and
     * therefore sit above all of these. */
    s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] =
        wlr_scene_tree_create(&s->scene->tree);
    s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] =
        wlr_scene_tree_create(&s->scene->tree);
    s->window_tree = wlr_scene_tree_create(&s->scene->tree);
    s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_TOP] =
        wlr_scene_tree_create(&s->scene->tree);
    s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] =
        wlr_scene_tree_create(&s->scene->tree);

    /* XDG shell */
    s->xdg_shell = wlr_xdg_shell_create(s->display, 3);

    /* Layer shell — panels, bars, wallpaper, launchers (waybar/swaybg/wofi). */
    layer_shell_init(s);

    /* xdg-output — bars/panels (waybar) need it to enumerate output geometry. */
    wlr_xdg_output_manager_v1_create(s->display, s->output_layout);

    /* xdg-decoration — negotiate server-side decorations (we draw borders). */
    struct wlr_xdg_decoration_manager_v1 *deco_mgr =
        wlr_xdg_decoration_manager_v1_create(s->display);
    s->new_decoration.notify = server_new_decoration;
    wl_signal_add(&deco_mgr->events.new_toplevel_decoration, &s->new_decoration);

    /* fractional-scale — HiDPI clients negotiate sub-integer scale factors. */
    wlr_fractional_scale_manager_v1_create(s->display, 1);

    /* idle-notify (swayidle) + idle-inhibit (players suppress it). */
    s->idle_notifier = wlr_idle_notifier_v1_create(s->display);
    s->idle_inhibit  = wlr_idle_inhibit_v1_create(s->display);
    s->new_idle_inhibitor.notify = server_new_idle_inhibitor;
    wl_signal_add(&s->idle_inhibit->events.new_inhibitor, &s->new_idle_inhibitor);

    /* output-management (wlr-randr/kanshi) + output-power (DPMS). */
    output_mgmt_setup(s);

    /* ext-session-lock (swaylock). */
    session_lock_setup(s);

    /* screencopy (grim, slurp-based tools) + export-dmabuf (wf-recorder). */
    wlr_screencopy_manager_v1_create(s->display);
    wlr_export_dmabuf_manager_v1_create(s->display);

    /* Clipboard managers: zwlr data-control (wl-clipboard watch mode,
     * cliphist) and its ext successor; plus middle-click primary selection. */
    wlr_data_control_manager_v1_create(s->display);
    wlr_ext_data_control_manager_v1_create(s->display, 1);
    wlr_primary_selection_v1_device_manager_create(s->display);

    /* foreign-toplevel — window lists for taskbars/docks. */
    foreign_toplevel_setup(s);

    /* Seat */
    s->seat = wlr_seat_create(s->display, "seat0");

    /* Cursor */
    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);
    s->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    wlr_xcursor_manager_load(s->cursor_mgr, 1);

    /* XWayland — X11 app support (lazy: Xwayland starts on first X client). */
    xwayland_setup(s);

    /* Initialize workspaces */
    const char *ws_names[WORKSPACE_MAX] = {
        "main", "web", "code", "terminal", "media",
        "docs", "chat", "sys", "scratch"
    };
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        s->workspaces[i].index   = i;
        s->workspaces[i].layout  = LAYOUT_TILING;
        s->workspaces[i].visible = 0;      /* new_output assigns and shows */
        s->workspaces[i].output  = NULL;
        s->workspaces[i].master_factor = s->config.master_factor;
        strncpy(s->workspaces[i].name, ws_names[i], WORKSPACE_NAME_LEN - 1);
        wl_list_init(&s->workspaces[i].windows);
    }

    /* Wire up listeners */
    s->new_output.notify = server_new_output;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);

    s->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&s->xdg_shell->events.new_toplevel, &s->new_xdg_toplevel);
    s->new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&s->xdg_shell->events.new_popup, &s->new_xdg_popup);

    wl_list_init(&s->outputs);
    wl_list_init(&s->keyboards);

    /* dock.c: needs s->workspaces[].windows and s->outputs already
     * wl_list_init'd (dock_rebuild walks both) — seeds pinned-only entries;
     * no output exists yet for dock_relayout to actually paint into. */
    dock_init(s);

    /* Input */
    input_setup(s);

    /* Add the socket */
    const char *socket = wl_display_add_socket_auto(s->display);
    if (!socket) {
        wlr_log(WLR_ERROR, "synui: failed to create Wayland socket");
        return -1;
    }
    setenv("WAYLAND_DISPLAY", socket, 1);
    wlr_log(WLR_INFO, "synui: running on WAYLAND_DISPLAY=%s", socket);

    /* Write socket name for synui-foot.service */
    FILE *sf = fopen("/tmp/synui-display", "w");
    if (sf) { fprintf(sf, "%s\n", socket); fclose(sf); }

    /* Start AI thread (it owns the synapd connection). Skipped under --no-ai;
     * mark the pipes invalid so send/poll become no-ops. */
    if (s->ai_disabled) {
        s->ai_pipe_req[0]  = s->ai_pipe_req[1]  = -1;
        s->ai_pipe_resp[0] = s->ai_pipe_resp[1] = -1;
        wlr_log(WLR_INFO, "synui: AI disabled (--no-ai)");
    } else if (ai_thread_start(s) < 0) {
        /* Pipes are already closed and marked -1; run as a plain compositor
         * (send/poll are no-ops on invalid fds). */
        wlr_log(WLR_ERROR, "synui: AI thread failed to start — AI disabled");
        s->ai_disabled = 1;
    }

    /* Subscribe to synguard's security-verdict feed (shares --no-ai gate:
     * with AI disabled we run a plain compositor with no daemon coupling). */
    s->sec_disabled = s->ai_disabled;
    secfeed_start(s);

    /* Initialize UI scene nodes (welcome screen, cmdbar, overlay) */
    synui_ui_init(s);

    /* Drag-and-drop icon layer: created last so it stacks above everything,
     * including the compositor UI. input.c moves it with the cursor. */
    s->drag_icon_tree = wlr_scene_tree_create(&s->scene->tree);

    return 0;
}

int synui_run(syn_server_t *s)
{
    if (!wlr_backend_start(s->backend)) {
        fprintf(stderr, "synui: wlr_backend_start() failed — check DRM/KMS access and kernel logs\n");
        wlr_log(WLR_ERROR, "synui: failed to start backend");
        return -1;
    }

    /* Set initial cursor image so it's visible immediately */
    wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");

    /* Autostart configured applications */
    for (int i = 0; i < s->config.autostart_count; i++) {
        wlr_log(WLR_INFO, "synui: autostart: %s", s->config.autostart[i]);
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", s->config.autostart[i], NULL);
            _exit(1);
        }
    }

    wl_display_run(s->display);
    return 0;
}

void synui_destroy(syn_server_t *s)
{
    s->shutting_down = 1;

    /* Stop the background threads first: after this point nothing else
     * touches the server struct while we tear it down, and their pipes and
     * sockets are closed (they'd otherwise leak and trip LeakSanitizer). */
    ai_thread_stop(s);
    secfeed_stop(s);
    effects_finish(s);

    /* Tear down Xwayland first so its surfaces/listeners are gone before we
     * destroy the display and scene. */
    if (s->xwayland) {
        wl_list_remove(&s->new_xwayland_surface.link);
        wl_list_remove(&s->xwayland_ready.link);
        wlr_xwayland_destroy(s->xwayland);
        s->xwayland = NULL;
    }
    wl_list_remove(&s->new_decoration.link);
    wl_list_remove(&s->new_idle_inhibitor.link);
    wl_list_remove(&s->output_mgr_apply.link);
    wl_list_remove(&s->output_mgr_test.link);
    wl_list_remove(&s->output_power_set_mode.link);
    wl_list_remove(&s->new_session_lock.link);

    /* Detach the compositor's singleton listeners before destroying the
     * objects they hang off — wlroots asserts empty listener lists on destroy
     * (wlr_cursor_destroy, etc.). Per-output/-keyboard/-view listeners are
     * removed by their own destroy handlers during the teardown below. */
    wl_list_remove(&s->new_output.link);
    wl_list_remove(&s->new_input.link);
    wl_list_remove(&s->new_xdg_toplevel.link);
    wl_list_remove(&s->new_xdg_popup.link);
    wl_list_remove(&s->new_layer_surface.link);
    wl_list_remove(&s->cursor_motion.link);
    wl_list_remove(&s->cursor_motion_absolute.link);
    wl_list_remove(&s->cursor_button.link);
    wl_list_remove(&s->cursor_axis.link);
    wl_list_remove(&s->cursor_frame.link);
    wl_list_remove(&s->request_cursor.link);
    wl_list_remove(&s->request_set_selection.link);
    wl_list_remove(&s->new_constraint.link);
    wl_list_remove(&s->touch_down.link);
    wl_list_remove(&s->touch_up.link);
    wl_list_remove(&s->touch_motion.link);
    wl_list_remove(&s->touch_frame.link);
    wl_list_remove(&s->touch_cancel.link);
    wl_list_remove(&s->tablet_axis.link);
    wl_list_remove(&s->tablet_proximity.link);
    wl_list_remove(&s->tablet_tip.link);
    wl_list_remove(&s->tablet_button.link);
    wl_list_remove(&s->swipe_begin.link);
    wl_list_remove(&s->swipe_update.link);
    wl_list_remove(&s->swipe_end.link);
    wl_list_remove(&s->pinch_begin.link);
    wl_list_remove(&s->pinch_update.link);
    wl_list_remove(&s->pinch_end.link);
    wl_list_remove(&s->hold_begin.link);
    wl_list_remove(&s->hold_end.link);
    wl_list_remove(&s->request_set_primary_selection.link);
    wl_list_remove(&s->request_start_drag.link);
    wl_list_remove(&s->start_drag.link);
    wl_list_remove(&s->drag_destroy.link);   /* wl_list_init'd when idle */
    wl_list_remove(&s->gamma_set.link);

    wl_display_destroy_clients(s->display);
    wlr_scene_node_destroy(&s->scene->tree.node);
    wlr_xcursor_manager_destroy(s->cursor_mgr);
    wlr_cursor_destroy(s->cursor);
    wlr_output_layout_destroy(s->output_layout);
    wlr_allocator_destroy(s->allocator);
    wlr_renderer_destroy(s->renderer);
    wlr_backend_destroy(s->backend);
    if (s->sigint_src)  wl_event_source_remove(s->sigint_src);
    if (s->sigterm_src) wl_event_source_remove(s->sigterm_src);
    if (s->sighup_src)  wl_event_source_remove(s->sighup_src);
    wl_display_destroy(s->display);
}

/* ── VM detection ────────────────────────────────────────── */
/*
 * Reads /sys/class/dmi/id/sys_vendor to detect hypervisors.
 * Returns 1 if running in VirtualBox, VMware, or QEMU; 0 otherwise.
 */
static int detect_vm(void)
{
    static const char *const vendors[] = {
        "VirtualBox", "VMware", "QEMU", "innotek", "KVM", "Xen", NULL
    };
    FILE *f = fopen("/sys/class/dmi/id/sys_vendor", "r");
    if (!f) return 0;
    char buf[128] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    for (int i = 0; vendors[i]; i++) {
        if (strstr(buf, vendors[i]))
            return 1;
    }
    return 0;
}

/* ── Entry point ─────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "SynapseOS Wayland Compositor\n"
        "\n"
        "Options:\n"
        "  --no-ai        Disable AI features (layout hints, command bar AI)\n"
        "  --overlay      Start with neural overlay visible\n"
        "  -d, --debug    Enable verbose wlroots logging\n"
        "  -v, --version  Print version\n"
        "  -h, --help     This help\n"
        "\n"
        "Default keybindings (override with 'bind =' lines in synuirc):\n"
        "  Super+Enter        Open terminal\n"
        "  Super+Space        Open AI command bar\n"
        "  Super+A            Toggle neural overlay\n"
        "  Super+D            Display settings (rotate/arrange monitors)\n"
        "  Super+Escape       Toggle the welcome menu\n"
        "  Super+1..9         Switch workspace\n"
        "  Super+Shift+1..9   Move window to workspace\n"
        "  Super+Tab          Next layout mode\n"
        "  Super+Q            Close focused window\n"
        "  Super+Shift+Q      Quit compositor\n"
        "\n"
        "Config: ~/.config/synui/synuirc (or /etc/synui/synuirc; $SYNUI_CONFIG\n"
        "overrides both) — keybinds, xkb_layout/variant/options,\n"
        "repeat_rate/delay, tap, natural_scroll, left_handed, accel_speed,\n"
        "terminal, autostart, gap, border_width,\n"
        "border_color_norm/focus/ai/warn (#rrggbb),\n"
        "effects on/off + effect_scanline/curvature/aberration/glitch (0..1, GLES2 only).\n"
        "Send SIGHUP to reload the config at runtime (binds, keymap, libinput,\n"
        "gap/border; autostart entries only run at startup).\n",
        prog
    );
}

static void reap_children(int sig)
{
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

int main(int argc, char *argv[])
{
    int debug = 0;
    int no_ai = 0;
    int start_overlay = 0;

    static struct option long_opts[] = {
        {"no-ai",   no_argument, 0, 'N'},
        {"overlay", no_argument, 0, 'O'},
        {"debug",   no_argument, 0, 'd'},
        {"version", no_argument, 0, 'v'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "NOdvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'N': no_ai = 1; break;
        case 'O': start_overlay = 1; break;
        case 'd': debug = 1; break;
        case 'v':
            printf("synui %s (SynapseOS Wayland Compositor)\n", SYNUI_VERSION);
            return 0;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, NULL);

    /*
     * Ignore SIGPIPE: the AI thread writes to the synapd socket, and if
     * synapd disconnects mid-write an unhandled SIGPIPE would take down the
     * whole compositor. Auto-reap children (autostart + AI "CMD:" launches)
     * via a real SIGCHLD handler rather than SIG_IGN: SIG_IGN's "auto-reap"
     * disposition survives exec() into any child we fork (e.g. wlroots'
     * Xwayland helper), which broke Xwayland's own wait4() on its xkbcomp
     * keymap-compile helper (got ECHILD instead of the real exit status,
     * so it always assumed "compile failed" and aborted, even though
     * xkbcomp succeeded). A caught handler resets to SIG_DFL across exec,
     * so it can't leak into children the same way.
     */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, reap_children);

    /* Detect VM and force software rendering before any wlroots init */
    if (detect_vm()) {
        fprintf(stderr, "synui: VM/hypervisor detected — forcing pixman renderer\n");
        setenv("WLR_RENDERER", "pixman", 1);
        /* Only set WLR_BACKENDS if caller hasn't already chosen one */
        if (!getenv("WLR_BACKENDS"))
            setenv("WLR_BACKENDS", "drm,libinput", 1);
        /* Disable hardware cursor; vmwgfx can't do it */
        setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);
    }

    syn_server_t server = {0};
    synui_config_load(&server.config);

    if (no_ai) {
        server.ai_disabled = 1;
        atomic_store(&server.ai_connected, 0);
    }
    if (start_overlay || server.config.start_overlay) {
        server.overlay.visible = 1;
    }

    fprintf(stderr, "synui: starting (WLR_RENDERER=%s WLR_BACKENDS=%s)\n",
            getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)",
            getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "(auto)");

    if (synui_init(&server) < 0) {
        fprintf(stderr, "synui: initialization failed\n");
        return 1;
    }

    fprintf(stderr, "synui %s — SynapseOS compositor running\n", SYNUI_VERSION);

    int ret = synui_run(&server);
    synui_destroy(&server);
    return ret;
}
