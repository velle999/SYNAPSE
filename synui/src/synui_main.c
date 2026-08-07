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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_color_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_xdg_toplevel_icon_v1.h>

#include <scenefx/render/fx_renderer/fx_renderer.h>

#include "synui.h"
#include "effects.h"
#include "kde_blur.h"

/* ── Security context: which globals a sandboxed client may see ──── */
/*
 * wlroots hands every Wayland client the full set of globals, so on a stock
 * build ANY application the user runs — a game, a Flatpak, a browser helper —
 * can bind zwlr_screencopy_manager_v1 and read the screen continuously, bind
 * zwp_input_method_manager_v2 and see everything typed, or bind
 * zwlr_data_control_manager_v1 and watch the clipboard. That is the default
 * for every wlroots compositor (sway and Hyprland included), not something
 * synui invented, but it deserves saying plainly on a system that advertises
 * itself as security-focused: synguard watches /dev/input specifically to
 * catch keyloggers, and none of the above touches /dev/input at all.
 *
 * BE CLEAR ABOUT WHAT THIS FIXES. A client is restricted only if it HAS a
 * security context, and a security context is something a sandbox (Flatpak,
 * snap, a bubblewrap wrapper) sets on the client's behalf before handing over
 * the socket. A native binary the user launches from a shell simply never
 * creates one and keeps full access. So this makes sandboxing MEAN something
 * here; it is not a defence against arbitrary code the user chose to run —
 * that code already runs as the user. Do not let this land in a changelog as
 * "synui now blocks keylogging".
 *
 * The list is deny-by-name rather than allow-by-name on purpose: a wlroots
 * upgrade that adds a global should not silently start restricting it and
 * break an app. New privileged protocols must be added here deliberately.
 */
static const char *const privileged_globals[] = {
    /* Screen capture — the whole framebuffer, no prompt, no indicator. */
    "zwlr_screencopy_manager_v1",
    "zwlr_export_dmabuf_manager_v1",
    /* Clipboard snooping: these watch every copy, not just paste-on-demand. */
    "zwlr_data_control_manager_v1",
    "ext_data_control_manager_v1",
    /* Input injection and input interception. */
    "zwp_virtual_keyboard_manager_v1",
    "zwp_input_method_manager_v2",
    /* Other windows: enumerate, focus, close. */
    "zwlr_foreign_toplevel_manager_v1",
    "ext_foreign_toplevel_list_v1",
    /* Display manipulation — gamma, DPMS, and output layout. */
    "zwlr_gamma_control_manager_v1",
    "zwlr_output_power_manager_v1",
    "zwlr_output_manager_v1",
    /* A sandboxed client must not be able to mint contexts for others. */
    "wp_security_context_manager_v1",
};

static bool security_context_filter(const struct wl_client *client,
                                    const struct wl_global *global,
                                    void *data)
{
    syn_server_t *s = data;

    /* No context => not sandboxed => unchanged behaviour. This is the path
     * grim, wl-clipboard, the bar and every ordinary app take. */
    if (!s->security_context_mgr ||
        !wlr_security_context_manager_v1_lookup_client(s->security_context_mgr,
                                                       client))
        return true;

    const struct wl_interface *iface = wl_global_get_interface(global);
    if (!iface || !iface->name)
        return true;

    for (size_t i = 0; i < sizeof(privileged_globals) /
                           sizeof(privileged_globals[0]); i++) {
        if (strcmp(iface->name, privileged_globals[i]) == 0)
            return false;
    }
    return true;
}

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

    /* Re-tile the visible desktop (layout_apply covers every output) with the
     * new gap/border. Hidden desktops re-flow on switch. */
    layout_apply(s, server_active_workspace(s));
    /* Then re-apply every view's geometry: border_width/titlebar_height may
     * have changed, which moves the content box inside an unchanged frame. The
     * layout pass doesn't cover that for floating windows, and repainting the
     * chrome alone would leave the client sized for the old metrics. */
    deco_refresh_all(s);

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
    int idx = s->active_workspace;
    if (idx < 0 || idx >= WORKSPACE_MAX) idx = 0;
    return &s->workspaces[idx];
}

/* The monitor X11 should call primary (see xwayland_apply_primary). An
 * explicit choice — the display panel's p key, persisted as primary=1 in
 * outputs.conf — always wins. With nothing marked we fall back to the
 * largest enabled output rather than leaving X with no primary at all,
 * because "no primary" is what makes SDL games open on an arbitrary
 * monitor. Biggest screen is a better guess than connector order. */
syn_output_t *server_primary_output(syn_server_t *s)
{
    syn_output_t *o, *best = NULL;
    int64_t best_area = -1;

    wl_list_for_each(o, &s->outputs, link) {
        if (o->primary) return o;          /* explicit choice */
        if (!o->wlr_output->enabled) continue;

        int w, h;
        wlr_output_effective_resolution(o->wlr_output, &w, &h);
        int64_t area = (int64_t)w * h;
        if (area > best_area) { best_area = area; best = o; }
    }
    return best;
}

int workspace_visible(syn_workspace_t *ws)
{
    return ws && ws->visible;
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
/*
 * The scene's commit, with night light on the FINAL output state.
 *
 * This is wlr_scene_output_commit() opened up, because the one thing it will
 * not do is the thing night light needs. Handing the transform to the scene as
 * wlr_scene_output_state_options.color_transform — the call every wlroots
 * example makes — routes it to wlr_buffer_pass_options.color_transform, and
 * scenefx's fx_renderer sets features.output_color_transform = false and never
 * reads the field. The warmth is dropped with no log line, no failed commit and
 * an empty CRTC GAMMA_LUT: the toggle simply does nothing. Set on the output
 * state, the DRM backend programs the CRTC LUT after blending, which is where
 * the pre-0.20 wlr_output_state_set_gamma_lut() call put it all along.
 *
 * Tested before it is kept, on a copy, the way scenefx tests a gamma-control
 * client's LUT: a backend that cannot take the transform fails the WHOLE commit
 * otherwise, trading a colour tint for a dead output.
 */
static void scene_commit_nightlight(syn_output_t *output)
{
    struct wlr_scene_output *scene_output = output->scene_output;
    struct wlr_output *wo = output->wlr_output;
    int temp = nightlight_effective_temp(output->server);

    /* wlr_scene_output_commit()'s own guard — with the addition that a night
     * light change is worth a commit even when nothing on screen moved. */
    if (temp == output->nightlight_temp &&
        !wlr_scene_output_needs_frame(scene_output))
        return;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!wlr_scene_output_build_state(scene_output, &state, NULL)) {
        wlr_output_state_finish(&state);
        return;
    }

    /* NULL is night light off, and committing it is how the screen gets its
     * colour back — an identity transform every backend can honour. */
    struct wlr_color_transform *nl = nightlight_color_transform(output->server);
    bool warm_ok = true;
    struct wlr_output_state warm = {0};
    if (wlr_output_state_copy(&warm, &state)) {
        wlr_output_state_set_color_transform(&warm, nl);
        warm_ok = wlr_output_test_state(wo, &warm);
        if (warm_ok)
            wlr_output_state_copy(&state, &warm);
        wlr_output_state_finish(&warm);
    } else {
        warm_ok = false;
    }

    if (wlr_output_commit_state(wo, &state)) {
        if (!warm_ok)
            wlr_log(WLR_ERROR, "synui: nightlight: %s will not take the %dK "
                    "transform — frame committed without it", wo->name, temp);
        /* Stamped even when the transform was refused: a backend that says no
         * once says no to the same transform every frame, and retrying it per
         * frame would mean testing, and logging, at the refresh rate. The next
         * temperature change asks again. */
        output->nightlight_temp = temp;
    }

    wlr_output_state_finish(&state);
}

static void output_frame(struct wl_listener *listener, void *data)
{
    syn_output_t *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;

    /* No scene output yet — nothing can be composited, and most of this
     * function dereferences it. Unreachable given the subscription ordering in
     * new_output; here because the alternative to an early return is a NULL
     * deref in the render path. Logged once so it cannot hide if it ever fires. */
    if (!scene_output) {
        static int warned;
        if (!warned) {
            warned = 1;
            wlr_log(WLR_ERROR, "synui: frame before scene_output — skipping");
        }
        return;
    }

    /* Apply any pending synguard security verdicts to window borders. */
    secfeed_dispatch(output->server);

    /* Drive the auto-hide dock's slide/hover; keep frames coming mid-slide. */
    struct timespec dnow;
    clock_gettime(CLOCK_MONOTONIC, &dnow);
    double now_s = (double)dnow.tv_sec + (double)dnow.tv_nsec / 1e9;
    if (dock_tick(output, now_s))
        wlr_output_schedule_frame(output->wlr_output);

    /* Walk the kitty (cat mode). After the dock, so it stays on top of it. */
    if (cat_tick(output, now_s))
        wlr_output_schedule_frame(output->wlr_output);

    /* Advance window fades (open, desktop cross-fade); keep frames coming
     * while any is still running. */
    if (anim_tick(output->server, now_s))
        wlr_output_schedule_frame(output->wlr_output);

    /* Slide the niri strip toward its scroll target; keep frames coming while
     * it is still moving. Same timebase as the fades above, so a desktop switch
     * — which starts both — settles as one animation. */
    if (layout_scroll_tick(output->server, now_s))
        wlr_output_schedule_frame(output->wlr_output);

    /* Control panel: waiting on the AI-backend helper to land (see
     * ctlpanel_tick). Idle unless a switch is actually in flight. */
    if (ctlpanel_tick(output->server))
        wlr_output_schedule_frame(output->wlr_output);

    /* Poll for AI responses (non-blocking) and route by request type. */
    syn_ai_response_t resp;
    if (ai_thread_poll(output->server, &resp) == 0) {
        syn_server_t *server = output->server;
        switch (resp.type) {
        case AI_MSG_QUERY_CMD:
            if (server->cmdbar.visible && server->cmdbar.waiting) {
                server->cmdbar.waiting = 0;
                /* resp.ok == 0 is the AI thread reporting a failed round trip;
                 * its text is a diagnostic, not something to parse for CMD:. */
                if (resp.ok)
                    execute_ai_action(server, resp.response);
                else
                    snprintf(server->cmdbar.response,
                             sizeof(server->cmdbar.response), "%s", resp.response);
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

    /* Animated matrix wallpaper: render this frame into the output's
     * matrix_buf (a wallpaper_tree sibling) before compositing, then keep
     * frames coming so it animates at the output's refresh rate. Damage the
     * scene so the effects/plain commit below actually re-renders. */
    if (matrix_output_frame(output)) {
        wlr_damage_ring_add_whole(&scene_output->damage_ring);
        wlr_output_schedule_frame(output->wlr_output);
    }

    /*
     * Backdrop blur forces a whole-output repaint, for the same reason the CRT
     * pass does (effects.c) — and it has to be done HERE because the CRT pass is
     * skipped entirely when no filter is enabled, so its add_whole never ran and
     * the flash came straight back the moment glass shipped without CRT.
     *
     * scenefx compensates for blur edge artifacts by saving the pixels around
     * each blurred region and painting them back afterwards, and it reads them
     * out of the buffer it is *currently rendering into*
     * (fx_renderer_read_to_buffer(..., render_pass->buffer) in fx_pass.c). Under
     * partial damage that buffer is one of 2–3 rotating swapchain buffers whose
     * undamaged area still holds a frame from several vsyncs ago — so the
     * "restore" step paints genuinely stale content back over the window. That
     * is the wallpaper flash on a click in Dolphin, and the glimpse of the window
     * behind a game: not a transient, but old pixels being copied forward.
     *
     * Repainting whole means the buffer never carries a stale region for that
     * step to find. It costs a full repaint per frame, but only while blur is on
     * — which is already the expensive path — and only on outputs with damage.
     */
    if (output->server->config.blur)
        wlr_damage_ring_add_whole(&scene_output->damage_ring);

    /* A night light change has no damage of its own — the pixels are identical,
     * only the LUT they are scanned out through moves — so on a still screen
     * the commit below would be skipped and the toggle would do nothing until
     * something else happened to repaint. Damage the output for it. */
    if (output->nightlight_temp != nightlight_effective_temp(output->server))
        wlr_damage_ring_add_whole(&scene_output->damage_ring);

    /* GLES post-process pass when available; plain scene commit otherwise
     * (and whenever any step of the effects pass fails). Both paths carry night
     * light on the output state they commit — see scene_commit_nightlight. */
    if (!effects_output_commit(output))
        scene_commit_nightlight(output);

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

    /* Workspaces span the whole desk, so unplugging a monitor orphans windows,
     * not workspaces: every window that lived on this output — on any desktop —
     * moves to a surviving one, so nothing is stranded off-screen. Skipped
     * during shutdown, when the scene graph is gone. */
    if (!server->shutting_down) {
        wallpaper_output_destroy(output);
        matrix_output_destroy(output);
        dock_output_destroy(output);
        lock_output_destroy(output);   /* drop the pane before its wlr_output frees */
        /* The Workshop wallpaper engine gets no say in this: synui is about to
         * close its layer surface for this output, and it has no code that can
         * ever build another one. Remember that it happened, so the connector
         * coming back re-launches it (wppick.c). */
        wpengine_output_lost(server);

        syn_output_t *home = wl_list_empty(&server->outputs)
                                 ? NULL
                                 : wl_container_of(server->outputs.next, home, link);
        int moved = 0;
        for (int i = 0; i < WORKSPACE_MAX; i++) {
            syn_view_t *v;
            wl_list_for_each(v, &server->workspaces[i].windows, link) {
                if (v->output != output) continue;
                v->output = home;      /* NULL only when the last monitor goes */
                moved++;
            }
        }
        if (server->ai_layout_output == output)
            server->ai_layout_output = NULL;
        if (moved)
            wlr_log(WLR_INFO, "synui: %d window(s) re-homed from %s onto %s",
                    moved, output->wlr_output->name,
                    home ? home->wlr_output->name : "(no output left)");
        if (home)
            layout_apply(server, server_active_workspace(server));
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

    /* Subscribe to `frame` only now that output->scene_output exists.
     *
     * This used to be registered ~16 lines earlier, above the output-layout
     * calls — and those commit output state, which a backend may answer with a
     * frame event synchronously, running output_frame with scene_output still
     * NULL. Most of output_frame dereferences it. No crash has been pinned on
     * this window (the one we chased turned out to be later, with scene_output
     * already valid), so this is hygiene rather than a fix: ordering the
     * subscription after the field it needs costs nothing and closes it. */
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    wl_list_insert(&server->outputs, &output->link);

    /* Ask once whether this connector can carry a 10-bit framebuffer, and once
     * what the monitor's EDID claims about HDR, so the display panel can show
     * both columns truthfully before anything is toggled. These are separate
     * questions: every 10-bit plane passes the first, only an HDR panel passes
     * the second. output_persist_apply() may already have re-enabled it. */
    dispcfg_probe_deep_color(server, output);
    dispcfg_probe_edid(server, output);

    /* A new output comes up at identity gamma. With night light on, leaving it
     * that way means the second monitor stays blue while the first is warm —
     * which reads as a broken monitor, not a setting. */
    nightlight_output_added(server, output);

    /* If the native lock (or the greeter, which reuses it) is up, this output
     * needs a clock/password pane — without one, a connector recreated by a
     * suspend/resume cycle wakes to the lock's black backstop: still locked,
     * still takes the password, but paints nothing. */
    lock_output_create(output);

    /* Same class of problem one layer up: a connector recreated by a
     * suspend/resume leaves linux-wallpaperengine with a dead layer surface it
     * cannot replace, so the Workshop wallpaper never comes back. No-op unless
     * an output was lost earlier — see wpengine_output_added(). */
    wpengine_output_added(server);

    /* A new monitor doesn't claim a workspace — every desktop already spans it.
     * It simply comes up showing the current desktop's share, which is empty
     * until windows are moved onto it (Super+O). If this is the *first* output,
     * adopt any windows created before it existed (they have no home yet). */
    if (wl_list_length(&server->outputs) == 1) {
        for (int i = 0; i < WORKSPACE_MAX; i++) {
            syn_view_t *v;
            wl_list_for_each(v, &server->workspaces[i].windows, link)
                if (!v->output) v->output = output;
        }
    }

    wlr_log(WLR_INFO, "synui: new output %s %dx%d — showing workspace %d",
            wlr_output->name, wlr_output->width, wlr_output->height,
            server->active_workspace + 1);

    /* Seed the usable area (full box; no layer surfaces yet), then lay the
     * visible desktop out across every output (this one included) and re-home
     * all UI. */
    layer_arrange_output(output);
    wallpaper_output_created(output);
    dock_output_created(output);
    /* The icon grid needs an output to be sized against — at startup there is
     * none yet when deskicons_reload() first runs, so this is where the very
     * first layout actually happens. */
    deskicons_layout(server);
    synui_render_deskicons(server);
    layout_apply(server, server_active_workspace(server));
    if (server->welcome_ui.shown)
        synui_render_welcome(server);
    if (server->overlay.visible)
        synui_render_overlay(server);
    if (server->cmdbar.visible)
        synui_render_cmdbar(server);
    dispcfg_outputs_changed(server);

    /* Plugging a monitor in can change which one is primary — either it is
     * the saved primary coming back, or it is now the largest and so wins
     * the fallback in server_primary_output(). */
    xwayland_apply_primary(server);

    output_mgmt_update(server);
}

/* ── XDG surface events ──────────────────────────────────── */
static void xdg_surface_map(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, map);
    view->mapped = 1;
    /* Before focus_view, which is what makes this window the focused one: on a
     * niri desktop the new window is placed relative to the column the user was
     * in, and after the focus moves there is no such column to name. */
    layout_strip_insert(view->server, view);
    focus_view(view->server, view, view->xdg_surface->surface);
    layout_apply(view->server, view->workspace);
    /* Reopen where this app was last closed (geom_persist.c) — on a floating
     * or monocle desktop; on tiling and AI this returns without reading the
     * table and the layout_apply above stands. After layout_apply, which would
     * otherwise tile straight over the restored box, and after mapped = 1,
     * which view_apply_maximized requires.
     *
     * On a FLOATING desktop go through layout_float_place, which consults the
     * same table first and hands the window to layout_float_arrange when the
     * app has nothing saved. This used to be the ONLY thing that placed such a
     * window — layout_apply was a bare no-op for LAYOUT_FLOATING ("the user
     * positions windows"), and a freshly-mapped xdg view is never
     * view->floating (that flag is only ever set by Super+F, a snap, or
     * maximize). So the FIRST window of an app never seen before came up at the
     * calloc'd 0,0 with size 0x0: no chrome, and on a multi-monitor layout
     * whose origin is dead space (velle's is), drawn where no output covers —
     * invisible, with `qs -n --no-duplicate` then making every later click on
     * the menu entry a silent success. That is how SynapseOS Updates failed to
     * appear (velle, 2026-08-02). Every app in windows.conf was fine, which is
     * why only new ones showed it.
     *
     * The floating desktop now has a tiler of its own, so layout_apply above
     * has already given this window a cell and the call below is what keeps a
     * REMEMBERED box winning over that cell (layout_restore_geometry marks it
     * hand_placed). Belt and braces on the 0x0 bug rather than a replacement
     * for the fix: both paths now place the window. */
    if (view->workspace && view->workspace->layout == LAYOUT_FLOATING)
        layout_float_place(view->server, view);
    else
        layout_restore_geometry(view->server, view);

    /* A client that asked to be maximized before it ever mapped (Firefox does,
     * restoring its session) only had the request held by wlroots — see
     * xdg_toplevel_request_maximize. Make it real now, through the one path that
     * also floats the window and records saved_geo. After
     * layout_restore_geometry, which may have maximized it already from
     * windows.conf: view_apply_maximized early-returns when the flag matches, so
     * whichever ran first owns the restore box. */
    if (view->xdg_surface->toplevel->requested.maximized && !view->maximized)
        view_apply_maximized(view->server, view, 1);

    foreign_toplevel_map(view);
    anim_fade_in(view);          /* windows arrive, they don't just appear */

    /* A client that asked for fullscreen before it ever mapped only got the
     * state recorded (see xdg_toplevel_request_fullscreen) — layout_apply just
     * tiled it. Hand it the output now that it is mapped and has a taskbar
     * handle for view_set_fullscreen() to update. */
    if (view->fullscreen)
        view_apply_fullscreen(view->server, view, 1);

    /* Hide welcome screen when first window opens */
    synui_welcome_hide(view->server);

    /* The window may have appeared under a cursor that never moved; without
     * this it gets no wl_pointer.enter until the user nudges the mouse, and a
     * client that wants the pointer at startup (SDL pointer lock) silently
     * never gets it. Last, so the geometry below it has settled. */
    pointer_rebase(view->server);
}

static void xdg_surface_unmap(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, unmap);
    syn_server_t *server = view->server;
    int was_focused = (server->focused_view == view);
    /* L2 (interim): a closing window fires a brief screen glitch. */
    effects_notify_close(server);
    /* Before mapped clears — the geometry is only meaningful while the window
     * is still the thing the user just sized. */
    geom_persist_save(view);
    view->mapped = 0;
    foreign_toplevel_unmap(view);
    /* A fullscreen client that exits never un-fullscreens itself: bring back
     * any bar it was covering. */
    layer_update_occlusion_all(server);
    game_reevaluate(server);
    /* Drop focus/grab references to this window */
    if (server->focused_view == view)
        server->focused_view = NULL;
    if (server->grabbed_view == view) {
        server->grabbed_view = NULL;
        server->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
        snap_preview_hide(server);   /* the window it was previewing is gone */
    }
    /* Drop the chrome (the frame itself goes with the scene tree). */
    view_deco_destroy(view);

    /* Reflow the remaining tiled windows and hand focus to one of them
     * (the XWayland unmap path already re-tiled; this one never did). */
    if (!server->shutting_down) {
        layout_apply(server, view->workspace);
        if (was_focused)
            workspace_focus_first(server, view->workspace);
    }
    /* Whatever this window was covering is now under the cursor. */
    pointer_rebase(server);
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
        snap_preview_hide(view->server);
    }
    /* server_new_xdg_toplevel builds the frame once, for the view's whole life
     * (so unmap keeps it for the next map) — which leaves this the only place
     * it can go. Nothing destroyed it before, so every Wayland window leaked
     * its frame, still tagged node.data = view, into a scene graph that
     * outlives the free() below. */
    view_frame_destroy(view);

    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->link);
    free(view);
}

/*
 * Re-send a configure the client never took.
 *
 * view_resize() is the ONLY place synui sizes a window, and it always moves the
 * frame and configures the client in the same breath — so the two can never
 * disagree at the moment it runs. What it does not do is check afterwards, and
 * nothing else ever re-configures a window that is not being moved or re-laid
 * out. A client that ends up at a size synui never asked for therefore *stays*
 * there, under chrome drawn at the size synui thinks it has, until the user
 * happens to drag the window and the next view_resize() puts it right.
 *
 * That is exactly what velle reported (2026-07-31): a maximized Firefox frame
 * at 2544x1396 with the page rendered into 552x304 in its top-left corner and
 * the desktop showing through the rest, "resets if you move it". The one place
 * synui hands a client its own choice of size is the 0x0 answer to the initial
 * commit above — pick your own, the layout resizes you on map — and a client
 * that misses or loses the configure that follows keeps whatever it picked.
 *
 * So: whenever the client is settled (nothing in flight) and the size it
 * declares is not the size it was told, say it again.
 *
 * Every guard here is about not fighting a client that is already doing the
 * right thing:
 *  - a non-empty configure_list means the client simply has not caught up yet,
 *    which is every ordinary resize;
 *  - min/max size are the client's own declared limits, so a box outside them
 *    is one it is *entitled* to refuse, and re-asking would never end;
 *  - a client only a little inside the box is rounding, not refusing. There is
 *    no size-increment protocol in xdg-shell (that was X11's WM_NORMAL_HINTS),
 *    so a terminal snapping to a character cell simply commits a few px short
 *    and wlroots has nothing to consult about it. Measured in the nested rig,
 *    foot lands 2 px under on height and 14 px under at the worst — under 5% —
 *    while the bug this exists for was at 22% of its box. Anything the client
 *    could have reached by rounding is therefore left alone: without this the
 *    heal cost every terminal on the desktop two redundant configures on every
 *    single resize (22 of them across three windows and eight layout ops), all
 *    of which foot answered by committing the same size again;
 *  - and the heal budget is per *target* size (view_resize resets it), which is
 *    the backstop that makes a storm impossible even for a client whose idea of
 *    "close enough" is outside the band above.
 */
#define SYN_HEAL_MAX 2
/* How near the client has to land to count as complying, as a fraction of the
 * box: 7/8 leaves an eighth of slack in either dimension. */
#define SYN_HEAL_NUM 7
#define SYN_HEAL_DEN 8

static void view_heal_size(syn_view_t *view)
{
    syn_server_t *s = view->server;

    if (!view->mapped || view->is_xwayland || !view->xdg_surface) return;
    if (view->w <= 0 || view->h <= 0) return;

    /* A configure is still on its way to the client. */
    if (!wl_list_empty(&view->xdg_surface->configure_list)) return;
    if (view->xdg_surface->configure_idle) return;

    /* Mid-drag the resize itself is generating configures faster than this
     * could usefully add to them. */
    if (s->grabbed_view == view && s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH)
        return;

    /* Nothing declared yet — there is no size to disagree with. */
    struct wlr_box geo = view->xdg_surface->geometry;
    if (geo.width <= 0 || geo.height <= 0) return;

    struct wlr_box c;
    view_content_box(view, &c);
    if (geo.width * SYN_HEAL_DEN >= c.width  * SYN_HEAL_NUM &&
        geo.height * SYN_HEAL_DEN >= c.height * SYN_HEAL_NUM) {
        view->heal_tries = 0;         /* settled at, or near enough to, the box */
        return;
    }

    /* A box the client told us it cannot take is not a client ignoring us. */
    struct wlr_xdg_toplevel_state *st = &view->xdg_surface->toplevel->current;
    if ((st->min_width  > 0 && c.width  < st->min_width)  ||
        (st->min_height > 0 && c.height < st->min_height) ||
        (st->max_width  > 0 && c.width  > st->max_width)  ||
        (st->max_height > 0 && c.height > st->max_height))
        return;

    if (view->heal_tries >= SYN_HEAL_MAX) return;
    view->heal_tries++;

    wlr_log(WLR_DEBUG, "synui: re-configuring %s: committed %dx%d, asked %dx%d "
                       "(try %d/%d)",
            view_app_id(view) ? view_app_id(view) : "?",
            geo.width, geo.height, c.width, c.height,
            view->heal_tries, SYN_HEAL_MAX);
    wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, c.width, c.height);
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
         * is actually initialized. Read from `requested`, not view->maximized:
         * the view is not maximized yet, and pretending it is here is what left
         * it maximized-but-tiled (see xdg_surface_map). */
        if (view->xdg_surface->toplevel->requested.maximized)
            wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, true);
        /* Same for a fullscreen request made before this first commit — the
         * surface is initialized by now, so the configure is safe to send. */
        if (view->fullscreen)
            wlr_xdg_toplevel_set_fullscreen(view->xdg_surface->toplevel, true);
        return;
    }

    /* Update borders when surface geometry changes */
    if (view->mapped)
        view_update_decorations(view);

    /* …and, on the same signal, notice when the geometry that just changed is
     * not the one the client was configured with. */
    view_heal_size(view);

    /* The client just painted, and scenefx's own commit handler (which ran
     * before this one — wlr_scene_xdg_surface_create is called before we add
     * this listener) has reset the surface buffer's opacity to 1.0. Put the
     * window's translucency back, or transparency only ever survives on the
     * chrome synui draws itself. */
    anim_reapply_opacity(view);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_maximize);
    /* Honour the requested state (not a blind toggle). Once mapped this really
     * does resize the window (view_apply_maximized).
     *
     * Before the map there is nothing to resize, and the request is deliberately
     * NOT copied into view->maximized: that flag means "this window has been
     * through view_apply_maximized", which is what sets floating = 1, saved_geo
     * and saved_floating. Setting it here instead produced a window that claimed
     * to be maximized while still in the tiling flow, so layout_tile placed it
     * in a master slot while the re-fit pass in layout_apply pulled it to the
     * full usable box — and, because layout_restore_geometry only maximizes
     * `if (saved_max && !view->maximized)`, the real path was skipped for good.
     * Super+M was a dead first press for the same reason (view_apply_maximized
     * early-returns when the flag already matches).
     *
     * wlroots keeps the request in toplevel->requested.maximized, so nothing has
     * to be recorded here at all: the initial_commit path sends the state, and
     * xdg_surface_map applies it once the window is real. */
    int want = view->xdg_surface->toplevel->requested.maximized;
    if (view->mapped)
        view_apply_maximized(view->server, view, want);
    /* Clients may send set_maximized before their first commit, while
     * xdg_surface->initialized is still false. wlroots asserts on that in
     * wlr_xdg_surface_schedule_configure(), aborting the whole compositor.
     * wlroots is holding the request for us; xdg_surface_commit's initial_commit
     * path sends it once the surface is ready. */
    if (!view->xdg_surface->initialized)
        return;
    /* Mapped, this is the state view_apply_maximized just settled on. Unmapped
     * but initialized, view->maximized is still 0 and would answer a request to
     * maximize with a refusal we do not mean — the window will be maximized at
     * map, so acknowledge what was asked for. */
    wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel,
                                   view->mapped ? view->maximized : want);
    foreign_toplevel_update_state(view);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_fullscreen);
    int fs = view->xdg_surface->toplevel->requested.fullscreen ? 1 : 0;

    /* Same pre-initial-commit hazard as xdg_toplevel_request_maximize, and it
     * aborts the compositor the same way: a client may set_fullscreen before
     * its first commit (Firefox --kiosk does, which is how tepris starts), and
     * view_apply_fullscreen() reaches wlr_xdg_toplevel_set_fullscreen() ->
     * wlr_xdg_surface_schedule_configure(), which asserts on an uninitialized
     * surface. Record the state; xdg_surface_commit's initial_commit path
     * sends it and xdg_surface_map gives it the geometry. */
    if (!view->xdg_surface->initialized) {
        view->fullscreen = fs;
        return;
    }

    /* Honour the state the client asked for (not a blind toggle), and give
     * the window real fullscreen geometry / hand it back to the layout. */
    view_apply_fullscreen(view->server, view, fs);
}

/*
 * Client-initiated move/resize (xdg_toplevel.move / .resize).
 *
 * A client that draws its own decorations does its own hit-testing and then asks
 * us to run the drag. synui says SERVER_SIDE over xdg-decoration and gives every
 * toplevel a frame, so most clients never send these — but a client only has to
 * *honour* xdg-decoration if it binds it, and Firefox does not bind it at all: it
 * keeps its GTK frame and its invisible shadow margin, whose input region covers
 * the pixels just outside our frame — i.e. exactly where the grab ring lives. The
 * shadow wins the hit test (it is a client surface, above the ring in the frame),
 * so Firefox's edges and corners reached the client and stopped dead here, with
 * nothing listening. Ignoring the request meant Firefox could not be resized at
 * all.
 *
 * Guard: only the window the pointer is actually over may start a grab, and only
 * when no grab is already running. Without that, any background client could take
 * the pointer whenever it liked.
 */
static bool grab_request_ok(syn_view_t *view)
{
    syn_server_t *s = view->server;
    if (s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH) return false;

    struct wlr_surface *focus = s->seat->pointer_state.focused_surface;
    return focus && wlr_surface_get_root_surface(focus) == view_surface(view);
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_move);
    if (grab_request_ok(view))
        view_begin_interactive(view, SYNUI_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, request_resize);
    struct wlr_xdg_toplevel_resize_event *ev = data;
    /* The client names the edges it grabbed — a corner arrives as two of them,
     * which is precisely what our resize path already expects. */
    if (grab_request_ok(view))
        view_begin_interactive(view, SYNUI_CURSOR_RESIZE, ev->edges);
}

/*
 * wlroots 0.19 splits surface creation into role-specific signals that fire
 * only once the role is known — new_surface no longer guarantees a role, so we
 * subscribe to new_toplevel / new_popup instead of asserting the role here.
 */
struct syn_popup_watch {
    struct wlr_xdg_popup *popup;
    syn_server_t *server;
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

/* Layout-space origin of the surface a popup chain ultimately hangs off.
 *
 * wlr_xdg_popup_unconstrain_from_box() wants its box in the coordinate system
 * of the popup's ROOT surface — the toplevel or layer surface at the top of the
 * chain, not the popup's immediate parent. Walk the xdg_popup parent links up to
 * that surface and report where it sits in the output layout.
 *
 * Both root kinds can be resolved from the surface: views stash their scene tree
 * on xdg_surface->data, and layer.c stashes its syn_layer_surface_t on
 * wlr_layer_surface_v1->data for exactly this purpose.
 *
 * NB: do NOT try to carry this on the popup's scene-tree node.data instead —
 * input.c resolves a view by walking up the scene graph to the first node with
 * data set, so anything but a syn_view_t* there silently corrupts input routing.
 */
static bool popup_root_origin(struct wlr_xdg_popup *popup, int *root_lx, int *root_ly)
{
    struct wlr_surface *surf = popup->parent;

    for (;;) {
        struct wlr_xdg_surface *xs = wlr_xdg_surface_try_from_wlr_surface(surf);
        if (xs && xs->role == WLR_XDG_SURFACE_ROLE_POPUP &&
            xs->popup && xs->popup->parent) {
            surf = xs->popup->parent;   /* a submenu — keep climbing */
            continue;
        }
        break;
    }

    struct wlr_scene_tree *root = NULL;

    struct wlr_xdg_surface *xs = wlr_xdg_surface_try_from_wlr_surface(surf);
    if (xs && xs->data) {
        root = xs->data;                /* toplevel: view->scene_tree */
    } else {
        struct wlr_layer_surface_v1 *lsurf =
            wlr_layer_surface_v1_try_from_wlr_surface(surf);
        syn_layer_surface_t *ls = lsurf ? lsurf->data : NULL;
        if (ls && ls->scene)
            root = ls->scene->tree;     /* layer surface: waybar, wofi */
    }

    if (!root)
        return false;

    return wlr_scene_node_coords(&root->node, root_lx, root_ly);
}

/* wlroots 0.19 requires the compositor to answer a popup's initial commit
 * with a configure the same way toplevels do (see xdg_surface_commit) — a
 * client that waits for a real ack, like GTK4's, otherwise hangs unmapped
 * forever and never shows or accepts input. */
static void popup_watch_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_popup_watch *w = wl_container_of(listener, w, commit);
    if (!w->popup->base->initial_commit)
        return;

    /* Tell the popup how much room it has, or it renders at whatever size it
     * asked for and runs straight off the screen.
     *
     * This used to key off w->parent_view, which is only ever set for a popup
     * whose parent is a toplevel VIEW. A popup parented to another popup — i.e.
     * every submenu — resolved parent_view to NULL and so was never unconstrained
     * at all. waybar's "Applications" submenu (69 entries, ~1900px) is the
     * obvious casualty: GTK only grows scroll arrows when it is told it doesn't
     * fit, so it just overflowed the output. Same for any nested menu, in any
     * client. Resolving the chain's root surface handles toplevel- and
     * layer-shell-rooted popups alike, at any nesting depth.
     */
    int root_lx = 0, root_ly = 0;
    if (popup_root_origin(w->popup, &root_lx, &root_ly)) {
        struct wlr_output *output = wlr_output_layout_output_at(
            w->server->output_layout, root_lx, root_ly);
        if (output) {
            struct wlr_box out_box;
            wlr_output_layout_get_box(w->server->output_layout, output, &out_box);
            struct wlr_box constraint = {
                .x = out_box.x - root_lx,
                .y = out_box.y - root_ly,
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

    /*
     * A popup's scene_buffers are born HERE — after the owning view's effects
     * were last applied — and a fresh wlr_scene_buffer defaults to opacity 1.0,
     * corner_radius 0, backdrop_blur off. anim_apply_alpha() is a one-shot walk
     * over the buffers that exist at the instant it runs, and every one of its
     * callers is a discrete event (map, focus, maximize, theme, config reload).
     * None of them fire when a client opens a menu. So the menu rendered opaque
     * and square inside an otherwise-glass window until some *unrelated* focus
     * change finally swept it up — the "UI flashes on click" in Dolphin.
     *
     * Re-walking the view now covers the new subtree, and since the popup tree
     * hangs under the view's frame it covers submenus at any nesting depth too.
     * Popups are rare events, so the extra walk costs nothing in steady state.
     *
     * Finding the view: climb until a node carries one, the same way surface_at()
     * does — a popup parented to another popup has no data on its immediate
     * parent. Only xdg surfaces reach here (layer-shell popups are parentless and
     * handled in layer.c), so the data, when found, is always a syn_view_t.
     */
    struct wlr_scene_tree *owner = parent_tree;
    while (owner && !owner->node.data)
        owner = owner->node.parent;
    if (owner && owner->node.data)
        anim_apply_alpha(owner->node.data);

    struct syn_popup_watch *w = calloc(1, sizeof(*w));
    w->popup = popup;
    w->server = s;
    w->commit.notify = popup_watch_commit;
    wl_signal_add(&popup->base->surface->events.commit, &w->commit);
    /* The popup's own destroy signal, not the surface's: a client may destroy
     * the xdg_popup role object and keep committing on the wl_surface, which
     * would leave popup_watch_commit dereferencing a freed w->popup. Matches
     * layer.c's layer_surface_new_popup. */
    w->destroy.notify = popup_watch_destroy;
    wl_signal_add(&popup->events.destroy, &w->destroy);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    syn_server_t *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;
    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    syn_view_t *view = calloc(1, sizeof(*view));
    view->xdg_surface = xdg_surface;
    /* The client's surface tree hangs inside a per-view frame together with the
     * borders and titlebar, so the whole window shows/hides/raises as one. */
    struct wlr_scene_tree *frame = view_frame_create(view, server->window_tree);
    view->scene_tree = wlr_scene_xdg_surface_create(frame, xdg_surface);
    view->scene_tree->node.data = view;
    /* Right now — before any popup can be parented here — the xdg surface tree
     * holds exactly one child, the client's subsurface tree. That is the node
     * view_clip_csd_margin() crops; see syn_view_t::client_tree. */
    if (!wl_list_empty(&view->scene_tree->children)) {
        struct wlr_scene_node *n =
            wl_container_of(view->scene_tree->children.next, n, link);
        if (n->type == WLR_SCENE_NODE_TREE)
            view->client_tree = wlr_scene_tree_from_node(n);
    }
    xdg_surface->data = view->scene_tree;

    /* Land on the current desktop, on the monitor the user is looking at. */
    view->workspace = server_active_workspace(server);
    view->output    = server_focused_output(server);
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

    view->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_surface->toplevel->events.request_move,
                  &view->request_move);

    view->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_surface->toplevel->events.request_resize,
                  &view->request_resize);

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

/* ── xdg-activation ──────────────────────────────────────── */
/*
 * A client that already has a window asks to be brought to the front: clicking
 * a link hands the URL to the running Firefox, which then requests activation
 * so its window actually surfaces. synui never implemented this, so the request
 * went nowhere and the window stayed buried on whatever desktop it was on.
 *
 * Honour it fully — raise, un-minimize, and switch to the window's desktop —
 * because the activation token is only handed out to a client the user just
 * interacted with; this is not a channel a background app can steal focus over.
 */
static void handle_request_activate(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, request_activate);
    const struct wlr_xdg_activation_v1_request_activate_event *event = data;

    struct wlr_xdg_surface *xdg =
        wlr_xdg_surface_try_from_wlr_surface(event->surface);
    if (!xdg || !xdg->data) return;

    struct wlr_scene_tree *tree = xdg->data;
    syn_view_t *view = tree->node.data;
    if (!view || !view->mapped) return;

    if (view->minimized)
        view_apply_minimized(s, view, 0);
    if (view->workspace && !workspace_visible(view->workspace))
        workspace_switch(s, view->workspace->index);

    wlr_scene_node_raise_to_top(view_node(view));
    focus_view(s, view, view_surface(view));
}

/* ── cursor-shape-v1 ─────────────────────────────────────── */
/* The client names the cursor it wants ("text", "grab", "ns-resize") and we
 * load it from the same xcursor theme the compositor uses. Without this the
 * client draws its own, so the cursor jumped theme and size between apps. */
static void handle_request_set_shape(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, request_set_shape);
    const struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

    /* Tablet tools carry their own cursor; only the pointer is ours to set. */
    if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER)
        return;
    /* Only the client the pointer is actually over may change the cursor. */
    if (event->seat_client != s->seat->pointer_state.focused_client)
        return;
    /* An interactive move/resize owns the cursor until the button comes up, as
     * does a hover over our own resize edges — a client must not paint over the
     * arrow that says what the compositor's chrome will do. */
    if (s->cursor_mode != SYNUI_CURSOR_PASSTHROUGH || s->deco_cursor)
        return;

    const char *name = wlr_cursor_shape_v1_name(event->shape);
    if (name)
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, name);
}

/* ── xdg-toplevel-icon-v1 ────────────────────────────────── */
/* A window naming its own icon is the only icon source for an app with no
 * .desktop file. Feed it to the icon cache the dock already reads. */
static void handle_set_icon(struct wl_listener *listener, void *data)
{
    (void)listener;
    const struct wlr_xdg_toplevel_icon_manager_v1_set_icon_event *event = data;
    if (!event->icon || !event->icon->name || !event->toplevel) return;

    const char *app_id = event->toplevel->app_id;
    if (!app_id || !app_id[0]) return;

    icon_provide_name(app_id, event->icon->name);
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
    wlr_idle_notifier_v1_set_inhibited(s->idle_notifier, idle_inhibited(s));
    /* The last inhibitor going away starts the idle clock again. */
    power_notify_activity(s);
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
    /* Disarms every stage (power_arm bails while an inhibitor is held) and
     * undoes a dim/blank we may already be in. */
    power_notify_activity(s);
}

/* ── Server init ─────────────────────────────────────────── */
int synui_init(syn_server_t *s)
{
    /* Read BEFORE the setenv below overwrites it: if SYNUI_RUNNING is already
     * in the environment we were launched from inside another synui, and the
     * session-wide things below must not reach out and clobber that session. */
    s->nested = (getenv("SYNUI_RUNNING") != NULL);

    /* Tell synui it's a SynapseOS compositor */
    setenv("SYNUI_RUNNING", "1", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_CURRENT_DESKTOP", "SynapseOS", 1);
    /* Qt chooses its platform theme plugin by matching XDG_CURRENT_DESKTOP against
     * the desktops it knows, and neither "SynapseOS" nor "synui" is one of them —
     * so Qt apps loaded NO platform theme and ran on Qt's built-in palette, Base
     * #FFFFFF and Text #000000, whatever the desktop theme was.
     *
     * Most of a KDE app hides that, because Dolphin applies our generated colour
     * scheme itself and repaints its chrome, sidebar and breadcrumb. The icon view
     * does not follow: KStandardItemListWidget::textColor() reads
     * styleOption().palette, which is still the untouched stock palette, so file
     * NAMES were drawn in black on the theme's dark view on every dark theme — and
     * no colour key could move them (setting every foreground in the scheme to
     * pure red left them black). The palette has to be right before the app builds
     * its views, and only a platform theme can do that.
     *
     * Two plugins can do the job and they are not equivalent:
     *
     *   - `kde` (plasma-integration) reads kdeglobals directly, so a Qt app gets
     *     the theme's EXACT colours, and gets them before it builds its views —
     *     which is the only thing that fixes Dolphin's icon-view filenames,
     *     because KStandardItemListWidget::textColor() reads the palette the app
     *     started with and no late-applied colour scheme reaches it.
     *   - `xdgdesktopportal` follows org.freedesktop.appearance color-scheme,
     *     which is one bit. It tracks dark↔light for free but cannot carry a
     *     theme change between two dark themes, and pays out generic near-white
     *     rather than the theme's ink.
     *
     * So: prefer kde when its plugin is actually on disk, and fall back to the
     * portal when it is not. Named by FILE rather than assumed, because naming a
     * platform theme whose plugin is absent is not a graceful degradation — Qt
     * loads no platform theme at all and every app comes up on the stock LIGHT
     * palette, which is precisely the bug 261 fixed. An unconditional "kde" here
     * would put every box without plasma-integration back into it.
     *
     * plasma-integration hard-depends on xdg-desktop-portal-kde, i.e. a second
     * portal backend, and a second backend taking ScreenCast is what breaks
     * screen sharing on wlroots. That is already handled a layer up:
     * synui-portals.conf sets default=gtk and pins
     * org.freedesktop.impl.portal.ScreenCast (and Screenshot) to wlr, so the kde
     * backend can be installed without anything routing to it. If that file ever
     * stops being installed, this preference is the thing that turns into a
     * screen-sharing bug.
     *
     * Overwrite 0, unlike the lines around it: this is a default, not a policy, so
     * a user who exports qt6ct or kde in their own environment keeps it. */
    setenv("QT_QPA_PLATFORMTHEME",
           access("/usr/lib/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so",
                  F_OK) == 0 ? "kde" : "xdgdesktopportal",
           0);
    /* Force GTK/Firefox onto their Wayland backends rather than XWayland. Firefox
     * only honours the glass prefs (transparency, blur-behind) on its Wayland
     * surface — under XWayland the window is opaque no matter the CSS/prefs. Its
     * auto-detection is unreliable across restarts (some come up XWayland), so pin
     * it here for every child of the session. See [[project_synui_firefox_glass]]. */
    setenv("MOZ_ENABLE_WAYLAND", "1", 1);
    setenv("MOZ_DBUS_REMOTE", "1", 1);
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
    /* Keep the session (2nd arg) instead of discarding it: it is what
     * wlr_session_change_vt() needs, and without a VT switch there is no way
     * off a session whose lock client will not let you back in. */
    s->backend = wlr_backend_autocreate(wl_display_get_event_loop(s->display),
                                        &s->session);
    if (!s->backend) {
        fprintf(stderr, "synui: wlr_backend_autocreate() failed (WLR_BACKENDS=%s WLR_RENDERER=%s)\n",
                getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "(auto)",
                getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)");
        wlr_log(WLR_ERROR, "synui: failed to create backend");
        return -1;
    }

    /* scenefx's fx_renderer, not wlr_renderer_autocreate: it is a GLES2 renderer
     * that also knows how to paint corner radius / backdrop blur / shadows during
     * the scene pass — the whole reason we can do real glass. Everything else on
     * the wlr_renderer interface (allocator autocreate, wl_display init, the CRT
     * post-pass) treats it as an ordinary renderer. */
    s->renderer = fx_renderer_create(s->backend);
    if (!s->renderer && !getenv("WLR_RENDERER_FORCE_SOFTWARE")) {
        /* The GPU we decided to trust cannot drive scenefx — nouveau is the
         * standing example, and a hypervisor advertising a render node it
         * cannot back is another. Falling back beats exiting: a soft, slow
         * desktop is recoverable, a black screen at login is not. Retrying
         * here is safe because the backend is already up and fx_renderer reads
         * these on the call, not at backend creation. */
        fprintf(stderr, "synui: hardware GLES2 unavailable — falling back to "
                        "software rendering\n");
        setenv("WLR_RENDERER_FORCE_SOFTWARE", "1", 1);
        setenv("WLR_RENDERER_ALLOW_SOFTWARE", "1", 1);
        s->renderer = fx_renderer_create(s->backend);
    }
    if (!s->renderer) {
        fprintf(stderr, "synui: fx_renderer_create() failed (WLR_RENDERER=%s)\n",
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

    /* Optional animated "matrix rain" wallpaper (matrix.c); like effects it
     * needs GLES2 and stays disabled (falling back to the static wallpaper)
     * otherwise. Must run after the renderer exists. */
    matrix_init(s);

    /* Compositor protocols */
    s->compositor = wlr_compositor_create(s->display, 5, s->renderer);
    wlr_subcompositor_create(s->display);
    wlr_data_device_manager_create(s->display);
    wlr_viewporter_create(s->display);
    wlr_presentation_create(s->display, s->backend, 1);

    /*
     * Colour management (wp_color_management_v1).
     *
     * What synui advertises here is the truth about what it composites: sRGB
     * primaries, the sRGB transfer function, perceptual intent. It is NOT an
     * HDR claim — scenefx renders 8-bit sRGB through GLES and the connector is
     * never put into PQ/BT.2020 (see dispcfg.c and the display panel).
     *
     * Exporting it anyway is worth doing precisely because it is honest. Until
     * now synui offered no colour management at all, so a client with HDR or
     * wide-gamut content had nothing to ask and had to guess what the
     * compositor would do with it — and the usual guess is "assume sRGB and
     * hope", which is how HDR video ends up washed out and grey. With the
     * global present, mpv/Firefox/games can read the preferred description off
     * the surface and tone-map into sRGB themselves, correctly, in the one
     * place that still knows the original colour volume. Clients that need
     * nothing are unaffected: they never bind it.
     *
     * When the renderer eventually gains a linear-light HDR path, this is also
     * the global that grows BT.2020 + ST2084_PQ — the protocol wiring is then
     * already in place and only the capability list changes.
     */
    static const enum wp_color_manager_v1_render_intent cm_intents[] = {
        WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL,  /* required by the protocol */
    };
    static const enum wp_color_manager_v1_primaries cm_primaries[] = {
        WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
    };
    struct wlr_color_manager_v1_options cm_opts = {
        /* Parametric only: an ICC profile would have to be applied by the
         * renderer, which cannot yet. Claiming icc_v2_v4 would make clients
         * hand over profiles synui would silently ignore. */
        .features = { .parametric = true },
        .render_intents     = cm_intents,
        .render_intents_len = sizeof(cm_intents) / sizeof(cm_intents[0]),
        /* Empty, and that is the sRGB answer: the protocol treats sRGB as
         * always supported and this list as the *additional* transfer
         * functions on top of it — wlroots asserts if sRGB appears in it. So
         * an SDR-only compositor advertises none, and adding
         * ST2084_PQ here is precisely the one-line change that goes with a
         * renderer that can actually composite in PQ. */
        .transfer_functions = NULL,
        .transfer_functions_len = 0,
        .primaries          = cm_primaries,
        .primaries_len      = sizeof(cm_primaries) / sizeof(cm_primaries[0]),
    };
    if (!wlr_color_manager_v1_create(s->display, 2, &cm_opts))
        wlr_log(WLR_ERROR, "synui: colour management global failed — clients "
                           "will fall back to assuming sRGB");

    /* Output layout */
    s->output_layout = wlr_output_layout_create(s->display);

    /* Scene graph */
    /*
     * Software rendering: re-render the whole scene every frame.
     *
     * Measured, not guessed. In a VM (fx_renderer on llvmpipe, software
     * cursor) the bar's layer surface comes out wrong: the SYNAPSE badge draws
     * over itself and the active-workspace pip goes missing, and moving the
     * pointer across the bar loses more of it. A/B against a full-re-render
     * reference frame, comparing only the static left section so the clock
     * could not skew it:
     *
     *     normal damage tracking      2950 pixels wrong
     *     whole-output damage         2042 pixels wrong   (blur's add_whole path)
     *     WLR_SCENE_DEBUG_DAMAGE=rerender   0
     *
     * Note the middle row: damaging the whole output is NOT enough, so this is
     * not a damage-region problem — the scene has to actually re-render its
     * nodes. That also rules out the effects: the clean run above had blur,
     * shadow and corner_radius at their defaults, so there is nothing to gain
     * by clamping them here.
     *
     * Yes, this is a debug knob. It is a documented, stable wlroots/scenefx
     * one, it is the only setting that produced a correct frame, and the cost
     * — a full repaint per frame — is paid only where the renderer is already
     * software. An explicit setting from the environment always wins.
     */
    if (getenv("WLR_RENDERER_FORCE_SOFTWARE") && !getenv("WLR_SCENE_DEBUG_DAMAGE")) {
        setenv("WLR_SCENE_DEBUG_DAMAGE", "rerender", 1);
        fprintf(stderr, "synui: software rendering — full re-render per frame "
                        "(set WLR_SCENE_DEBUG_DAMAGE to override)\n");
    }

    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);

    /* scenefx global blur parameters (Stage 5). One-time; per-buffer backdrop
     * blur is toggled in anim.c. Cheap when no buffer opts into blur. */
    wlr_scene_set_blur_data(s->scene, s->config.blur_passes, s->config.blur_radius,
                            s->config.blur_noise, s->config.blur_brightness,
                            s->config.blur_contrast, s->config.blur_saturation);

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
    /* Desktop icons are part of the desktop, not a panel: they belong above
     * the wallpaper and background layer but UNDER every window, so they are
     * created here rather than with the UI trees in synui_ui_init(). */
    s->deskicons_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_set_enabled(&s->deskicons_ui.tree->node, false);
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

    /* security-context: the gate that makes the privileged globals below mean
     * something for a sandboxed app. Must be created BEFORE the filter it
     * feeds; see security_context_filter() for what is restricted and, just as
     * importantly, what this does NOT protect against. */
    s->security_context_mgr = wlr_security_context_manager_v1_create(s->display);
    if (s->security_context_mgr)
        wl_display_set_global_filter(s->display, security_context_filter, s);
    else
        wlr_log(WLR_ERROR, "security-context: manager creation failed — "
                           "sandboxed clients will NOT be restricted");

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

    /* xdg-activation: "raise the window I already have open" (link handoff). */
    s->xdg_activation = wlr_xdg_activation_v1_create(s->display);
    s->request_activate.notify = handle_request_activate;
    wl_signal_add(&s->xdg_activation->events.request_activate,
                  &s->request_activate);

    /* cursor-shape: clients name a cursor instead of drawing their own. */
    s->cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(s->display, 1);
    s->request_set_shape.notify = handle_request_set_shape;
    wl_signal_add(&s->cursor_shape_mgr->events.request_set_shape,
                  &s->request_set_shape);

    /* xdg-toplevel-icon: per-window icons for apps with no .desktop file. */
    s->toplevel_icon_mgr = wlr_xdg_toplevel_icon_manager_v1_create(s->display, 1);
    s->set_icon.notify = handle_set_icon;
    wl_signal_add(&s->toplevel_icon_mgr->events.set_icon, &s->set_icon);

    /* org_kde_kwin_blur: mostly a signal rather than a feature. Qt/Breeze
     * clients check for this global and only paint a translucent background
     * when it exists, so advertising it is what gives the KDE apps their glass
     * look; syn_kde_blur_wants() then tells anim.c to actually frost them. */
    syn_kde_blur_init(s);

    /* text-input-v3 + input-method-v2: IME (CJK, compose, emoji picker). */
    ime_setup(s);

    /* Seat */
    s->seat = wlr_seat_create(s->display, "seat0");

    /* Cursor */
    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);
    /* Theme and size come from synuirc / cursor.state (cursor.c). An untouched
     * system still gets NULL + 24 — exactly what this was hardcoded to before
     * the cursor picker existed — because that is what config.c defaults to. */
    s->cursor_mgr = wlr_xcursor_manager_create(
        s->config.cursor_theme[0] ? s->config.cursor_theme : NULL,
        s->config.cursor_size > 0 ? s->config.cursor_size : 24);
    wlr_xcursor_manager_load(s->cursor_mgr, 1);

    /* Children inherit these, so an app launched from the menu agrees with the
     * compositor rather than falling back to whatever the session wrapper
     * exported at login. */
    if (s->config.cursor_theme[0])
        setenv("XCURSOR_THEME", s->config.cursor_theme, 1);
    {
        char cbuf[16];
        snprintf(cbuf, sizeof(cbuf), "%d",
                 s->config.cursor_size > 0 ? s->config.cursor_size : 24);
        setenv("XCURSOR_SIZE", cbuf, 1);
    }

    /* XWayland — X11 app support (lazy: Xwayland starts on first X client).
     * xw_views must be live first: server_new_xwayland_surface() inserts into
     * it, and a zeroed wl_list is not an empty one. */
    wl_list_init(&s->xw_views);
    xwayland_setup(s);

    /* Initialize workspaces */
    const char *ws_names[WORKSPACE_MAX] = {
        "main", "web", "code", "terminal", "media",
        "docs", "chat", "sys", "scratch"
    };
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        s->workspaces[i].index   = i;
        s->workspaces[i].layout  = LAYOUT_TILING;
        s->workspaces[i].visible = (i == 0);   /* desktop 1 is shown at start */
        s->workspaces[i].master_factor = s->config.master_factor;
        strncpy(s->workspaces[i].name, ws_names[i], WORKSPACE_NAME_LEN - 1);
        wl_list_init(&s->workspaces[i].windows);
    }
    /* …and then whatever was last chosen per desktop (layouts.state), over the
     * tiling default seeded above. Here rather than with the other
     * *_state_loads further down because the seed loop is what it overrides,
     * and nothing is mapped yet either way — no layout_apply is owed. */
    layout_state_load(s);

    /* Wire up listeners */
    s->new_output.notify = server_new_output;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);

    s->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&s->xdg_shell->events.new_toplevel, &s->new_xdg_toplevel);
    s->new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&s->xdg_shell->events.new_popup, &s->new_xdg_popup);

    wl_list_init(&s->outputs);
    wl_list_init(&s->keyboards);
    wl_list_init(&s->cmdcaps);

    /* dock.c: needs s->workspaces[].windows and s->outputs already
     * wl_list_init'd (dock_rebuild walks both) — seeds pinned-only entries;
     * no output exists yet for dock_relayout to actually paint into.
     * The greeter has no dock — it is a bare login panel. */
    if (!s->greeter)
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

    /* Control socket (synctl). After WAYLAND_DISPLAY is set: the socket is keyed
     * on it, so a nested/headless synui can't collide with the session's. */
    ipc_setup(s);

    /* Publish the socket name for synui-foot.service and synui-media-inhibit.
     *
     * This goes in XDG_RUNTIME_DIR (0700, owned by the session user), not in
     * /tmp as it used to. synui-foot runs the session's only terminal as root,
     * and it sets WAYLAND_DISPLAY from this file — but WAYLAND_DISPLAY may be
     * an *absolute path*, so whoever wins the race to create a world-writable
     * /tmp/synui-display could point a root `foot synsh` at a Wayland socket
     * they control and type into a root shell. The runtime dir is not writable
     * by other users, which closes that off.
     *
     * /tmp remains the fallback only for the case where XDG_RUNTIME_DIR is
     * unset (synui started outside a session — e.g. the headless test rig),
     * where there is no root consumer to attack. */
    const char *rtdir = getenv("XDG_RUNTIME_DIR");
    char dpath[256];
    if (rtdir && *rtdir)
        snprintf(dpath, sizeof(dpath), "%s/synui-display", rtdir);
    else
        snprintf(dpath, sizeof(dpath), "/tmp/synui-display");

    FILE *sf = fopen(dpath, "w");
    if (sf) { fprintf(sf, "%s\n", socket); fclose(sf); }
    else wlr_log(WLR_ERROR, "synui: cannot write '%s': %s",
                 dpath, strerror(errno));

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

    /* Monitor synapd's live activity for the neural overlay (no-op without
     * a synapd socket; polls only while the overlay is open). */
    if (!s->ai_disabled)
        synmon_start(s);

    /* Initialize UI scene nodes (welcome screen, cmdbar, overlay) */
    synui_ui_init(s);

    /* Clock & Time: loads clock.state and arms the panel's 1 Hz repaint timer.
     * After synui_ui_init so the scene trees it will draw into already exist. */
    clock_init(s);

    /* Desktop icons: a no-op unless synuirc turned them on. After the outputs
     * exist, since the grid is laid out against the primary one's usable box. */
    deskicons_reload(s);

    /* Drag-and-drop icon layer: created last so it stacks above everything,
     * including the compositor UI. input.c moves it with the cursor. */
    s->drag_icon_tree = wlr_scene_tree_create(&s->scene->tree);

    /* Idle stages: needs the scene (dim overlay) and the loaded config. */
    power_init(s);

    /* Task manager: creates its poll timer (disarmed) and probes for a GPU. */
    taskmgr_init(s);

    /* News: loads the cached river off disk and parks a fetch thread on its
     * condvar. Nothing goes near the network until the panel is opened. */
    news_init(s);
    aimodel_init(s);

    /* org.freedesktop.ScreenSaver — lets apps inhibit idle the standard way.
     * Best-effort; no session bus just means the feature stays off. */
    screensaver_init(s);
    bt_init(s);
    notif_init(s);
    logind_init(s);
    nightlight_apply(s);   /* honour night_light = on from synuirc at startup */
    clipboard_init(s);

    /* Stamp game mode "off" for waybar's indicator. */
    game_init(s);

    /* Filter strengths tuned in the Super+E panel and saved there, applied over
     * the config defaults. */
    filters_state_load(s);

    /* Page two of the same panel: corners, shadow, blur, translucency. The
     * apply() is owed here and not by the CRT page — the scene took its blur
     * data from the config back at wlr_scene_create, so a loaded blur_radius
     * would otherwise sit in the config unpushed until someone opened the panel
     * and moved something. No windows are mapped yet, so it costs one call. */
    uifx_state_load(s);
    uifx_apply(s);

    /* Titlebars-hidden, as last left by Super+Shift+D. Nothing is mapped yet,
     * so the flag is simply in place for the first layout — no deco_refresh_all
     * is owed here. */
    deco_state_load(s);

    /* The theme last picked in the manager (theme.state), applied over whatever
     * synuirc left. No windows are mapped yet, so this is mostly about firing
     * synui-apply-theme once so Dolphin/GTK/Firefox match on login. */
    theme_state_load(s);

    /* Event sounds: the udev watch that makes "device plugged in" an event, and
     * the login chime. Both no-op on a desktop that has never turned a sound on,
     * which is every desktop until someone does. */
    sound_udev_init(s);
    sound_play(s, SOUND_EVT_LOGIN);

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


    /* The greeter takes over here: draw the login panel (same as the lock
     * screen) and skip everything a desktop session would start — cat mode,
     * autostarted apps (waybar, etc.). greetd will kill us when the password
     * it is handed starts the real session. */
    if (s->greeter) {
        greeter_start(s);
        wl_display_run(s->display);
        return 0;
    }

    /* Cat mode, if synuirc asked for it. After backend start, so the outputs it
     * needs to pick a starting spot and a destination already exist. */
    if (s->config.cat_start)
        cat_toggle(s);

    /* Publish the session's display variables to the D-Bus activation
     * environment and, through --systemd, to the systemd user manager.
     *
     * Without this the user manager's environment holds only what greetd/PAM
     * put there — no WAYLAND_DISPLAY, no XDG_CURRENT_DESKTOP. That is fine for
     * everything we fork below, which inherits our environment directly, but
     * dbus-broker turns a traditional /usr/share/dbus-1/services `Exec=` into a
     * TRANSIENT SYSTEMD UNIT, and that unit inherits the user manager's
     * environment instead of the caller's. So a D-Bus-activated GUI service
     * comes up with no display, falls back to the Qt xcb platform plugin, finds
     * nothing, and abort()s — while the process that asked for it sees only
     * "Could not activate remote peer '<name>': unit failed", which names
     * neither the display nor Qt. kdeconnectd is the case that found this
     * (the tray icon appeared, the daemon behind it died on every activation,
     * so no phone could pair); it is not specific to kdeconnect.
     *
     * Ordered BEFORE the autostart loop deliberately: an autostarted client
     * that activates a service on startup (kdeconnect-indicator does, at once)
     * would otherwise race us and lose. Hence also the wait below rather than
     * fire-and-forget — this is one short D-Bus round trip, and the race it
     * closes is the whole point of running it here.
     *
     * QT_QPA_PLATFORMTHEME rides along for a related but distinct reason: the
     * activated unit inherits the user manager's environment, so without it a
     * D-Bus-activated Qt app loads NO platform theme and comes up on Qt's stock
     * LIGHT palette — no kdeglobals colours, and no live theme switching either.
     * Dolphin is the case that matters, because it ships
     * org.kde.dolphin.FileManager1.service: opened from the dock it is our child
     * and themed, opened as a file manager by another app it was activated and
     * white. Two routes to the same binary that did not look the same.
     *
     * All four variables are set unconditionally by synui_init(), so naming
     * them bare (read from our own environment) can't hit the unset case.
     *
     * NOT done when nested: a nested synui shares the session bus with the
     * desktop hosting it, so this would repoint that desktop's activation
     * environment at our socket. */
    if (!s->nested) {
        sigset_t chld, prev;
        sigemptyset(&chld);
        sigaddset(&chld, SIGCHLD);
        sigprocmask(SIG_BLOCK, &chld, &prev);

        pid_t pid = fork();
        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > 2) close(devnull);
            }
            synui_child_reset_signals();
            execlp("dbus-update-activation-environment",
                   "dbus-update-activation-environment", "--systemd",
                   "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE",
                   "QT_QPA_PLATFORMTHEME",
                   (char *)NULL);
            _exit(127);
        }
        if (pid > 0) {
            /* SIGCHLD stays blocked across the wait so reap_children() cannot
             * take the status out from under us. */
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
                ;
            if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
                wlr_log(WLR_ERROR, "synui: dbus-update-activation-environment "
                        "failed — D-Bus-activated services will start without a "
                        "display (is dbus installed?)");
        } else {
            wlr_log(WLR_ERROR, "synui: fork for dbus-update-activation-environment "
                    "failed: %s", strerror(errno));
        }
        sigprocmask(SIG_SETMASK, &prev, NULL);
    }

    /* Autostart configured applications */
    for (int i = 0; i < s->config.autostart_count; i++) {
        wlr_log(WLR_INFO, "synui: autostart: %s", s->config.autostart[i]);
        if (fork() == 0) {
            setsid();
            synui_child_reset_signals();
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

    /* The logout chime, here rather than on the quit action so that every way
     * out gets it — greetd's SIGTERM and ^C reach this and never touch that
     * branch. It is fire-and-forget by nature: the sample plays on after synui
     * is gone, which is what a logout sound is. */
    sound_play(s, SOUND_EVT_LOGOUT);
    sound_udev_finish(s);

    /* Stop the background threads first: after this point nothing else
     * touches the server struct while we tear it down, and their pipes and
     * sockets are closed (they'd otherwise leak and trip LeakSanitizer). */
    ai_thread_stop(s);
    cmdcap_stop_all(s);
    secfeed_stop(s);
    synmon_stop(s);
    effects_finish(s);
    matrix_finish(s);

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
    wl_list_remove(&s->request_activate.link);
    wl_list_remove(&s->request_set_shape.link);
    wl_list_remove(&s->set_icon.link);
    ipc_destroy(s);
    /* NOT ime_destroy() — it must outlive the clients; see below. */

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

    power_finish(s);
    taskmgr_finish(s);
    news_finish(s);
    aimodel_finish(s);
    clipboard_finish(s);
    logind_finish(s);
    notif_finish(s);
    bt_finish(s);
    clock_finish(s);
    screensaver_finish(s);
    /* Before anything else tears down: if game mode stopped synapd, start it
     * again. A synui that exits mid-game must not leave the box with no AI. */
    game_finish(s);

    wl_display_destroy_clients(s->display);

    /* Only now is the IME relay safe to free. Every text-input object belongs to
     * a *client*, and its destroy listener (text_input_destroy → ime_deactivate)
     * dereferences relay through ti->relay — so the relay has to outlive the
     * clients that point at it. Freeing it earlier was a heap-use-after-free on
     * every shutdown with a client still running, i.e. on every real logout;
     * it went unseen because the ASan smoke test's client exits before SIGTERM.
     * Destroying the clients first also lets each text_input unregister itself,
     * so the relay's list is empty by the time it goes. */
    ime_destroy(s);

    wlr_scene_node_destroy(&s->scene->tree.node);

    /* The fallback font faces, after the scene is gone so nothing can still be
     * mid-draw with one of them selected. Every entry holds a cairo font face
     * and the FcPattern it was built from, both of which LeakSanitizer counts
     * against the ASan smoke run. */
    syn_text_shutdown();

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
/*
 * Is there a GPU here we could actually render on?
 *
 * A render node is the thing fx_renderer needs: it is what EGL opens to get a
 * GLES2 context. Its presence is not a promise that GL works (nouveau ships one
 * and cannot drive scenefx), which is why the renderer path below falls back on
 * failure rather than trusting this.
 */
static int have_render_node(void)
{
    for (int i = 128; i < 136; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
        if (access(path, R_OK | W_OK) == 0)
            return 1;
    }
    return 0;
}

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
        "  --greeter      Run as the greetd login greeter (mirrors the lock\n"
        "                 screen; hands the password to greetd via GREETD_SOCK)\n"
        "  --no-ai        Disable AI features (layout hints, command bar AI)\n"
        "  --overlay      Start with neural overlay visible\n"
        "  -d, --debug    Enable verbose wlroots logging\n"
        "  -v, --version  Print version\n"
        "  -h, --help     This help\n"
        "\n"
        /* Keep this list in step with seed_default_binds() in config.c — it is
         * the only copy that is hand-written (the control panel's shortcuts
         * column is generated from the live bind table, so it cannot drift).
         * Super tap alone opens the start menu; it is not a bind-table entry. */
        "Default keybindings (override with 'bind =' lines in synuirc):\n"
        "  Super (tapped)     Start menu\n"
        "  Super+Enter        Open terminal\n"
        "  Super+Space        Application launcher (rofi)\n"
        "  Super+=            Open AI command bar\n"
        "                     (swap the two: Control panel \xe2\x96\xb8 Desktop \xe2\x96\xb8\n"
        "                      Super+Space opens, or `super_space = cmdbar`)\n"
        "  Super+Backspace    Ask the AI about the focused window\n"
        "  Super+C            Control panel (every shortcut + the settings)\n"
        "  Super+/  Super+?   Keyboard shortcuts, searchable (Enter runs one)\n"
        "  Super+A            Toggle neural overlay\n"
        "  Super+D            Display settings (rotate/arrange monitors)\n"
        "  Super+Shift+D      Show/hide titlebars\n"
        "  Super+W            Wallpaper picker\n"
        "  Super+Shift+W      Reload the wallpaper\n"
        "  Super+T            Theme manager (SYNAPSE/Dark/XP/95)\n"
        "  Super+Shift+T      Tile the current workspace (from any layout)\n"
        "  Super+Shift+Y      Cascade the current workspace\n"
        "  Super+P            Power-saving panel\n"
        "  Super+R            News (Hacker News, Arch, LWN, Phoronix, …)\n"
        "  Super+Shift+R      Start/stop screen recording (sound: Control panel\n"
        "                     \xe2\x96\xb8 Sound \xe2\x96\xb8 Record audio)\n"
        "  Super+B            Bluetooth\n"
        "  Super+Shift+B      Night light (blue-light filter)\n"
        "  Super+I            Network / Wi-Fi\n"
        "  Super+V            Clipboard history\n"
        "  Super+G            Game mode\n"
        "  Super+Shift+A      Desktop widgets\n"
        "  Super+Shift+C      Cat mode\n"
        "  Ctrl+Alt+Delete    Task manager\n"
        "  Super+L            Lock the screen\n"
        "  Super+E            Visual effects panel (per-filter sliders)\n"
        "  Super+Escape       Toggle the welcome menu\n"
        "  Super+1..9         Switch workspace\n"
        "  Super+Shift+1..9   Move window to workspace\n"
        "  Super+Tab          Next layout mode\n"
        "  Alt+Tab            Switch window — mission control by default\n"
        "                     (alt_tab_style = switcher for the MRU strip)\n"
        "  Super+J/K          Focus next/previous window\n"
        "  Super+Shift+J/K    Move window down/up the stack\n"
        "  Super+H            Shrink the master column\n"
        "  Super+Shift+L      Grow the master column\n"
        "  Super+F            Toggle floating\n"
        "  Super+M            Toggle maximize\n"
        "  Super+Shift+F      Force fullscreen (games that only do borderless)\n"
        "  Super+N            Minimize\n"
        "  Super+Shift+N      Restore a minimized window\n"
        "  Super+O            Move window to next monitor\n"
        "  Super+Shift+O      Move window to previous monitor\n"
        "  Super+Q            Close focused window\n"
        "  Super+Shift+Q      Quit compositor\n"
        "  Print              Screenshot this monitor\n"
        "  Shift+Print        Screenshot an area (slurp)\n"
        "  Ctrl+Print         Screenshot every monitor\n"
        "  Super+Shift+S      Screenshot an area\n"
        "  Volume/brightness  Handled (incl. the USB volume knob)\n"
        "\n"
        "Nothing is bound to `calendar`; the bar clock opens it. Bind it back with\n"
        "a `bind =` line if you want a key. Note `=` is spelled `equal` in a bind:\n"
        "the combo is split on '+', so `bind = super+equal cmdbar`.\n"
        "\n"
        "Config: ~/.config/synui/synuirc (or /etc/synui/synuirc; $SYNUI_CONFIG\n"
        "overrides both) — keybinds, xkb_layout/variant/options,\n"
        "repeat_rate/delay, tap, natural_scroll, left_handed, accel_speed,\n"
        "terminal, autostart, gap, border_width,\n"
        "border_color_norm/focus/ai/warn (#rrggbb),\n"
        "effects on/off + effect_scanline/curvature/aberration/glitch (0..1, GLES2 only),\n"
        "bar_shell = synapse|antiquity (which QML bar synui-bar starts; read by\n"
        "  synui-bar at LOGIN, not by the compositor) + bar_icon_theme.\n"
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
    int greeter = 0;

    static struct option long_opts[] = {
        {"no-ai",   no_argument, 0, 'N'},
        {"overlay", no_argument, 0, 'O'},
        {"greeter", no_argument, 0, 'G'},
        {"debug",   no_argument, 0, 'd'},
        {"version", no_argument, 0, 'v'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "NOGdvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'N': no_ai = 1; break;
        case 'O': start_overlay = 1; break;
        case 'G': greeter = 1; no_ai = 1; break;  /* greeter: no session AI */
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
        /* Software rendering, but NOT via WLR_RENDERER=pixman: since the scenefx
         * migration the renderer is built by fx_renderer_create(), which is
         * GLES2-only and never consults WLR_RENDERER — so that setting quietly
         * became a no-op here, and a VM with no DRM render node died at
         * "no DRM FD available" instead of falling back. These are the knobs
         * fx_renderer/EGL actually reads. WLR_RENDERER is still set for the
         * benefit of anything else in the process that honours it. */
        /* But only when there is nothing to render WITH. This used to force
         * llvmpipe in EVERY VM, which threw away working acceleration on any
         * hypervisor with 3D enabled — virtio-gpu+virgl, VirtualBox 3D,
         * VMware 3D all expose a render node — and llvmpipe is where the
         * layer-surface corruption lives. A guest with a GPU should use it.
         * If the GPU turns out not to work, fx_renderer_create() below falls
         * back to software rather than leaving a black screen. */
        if (have_render_node()) {
            fprintf(stderr, "synui: VM/hypervisor detected, but a render node "
                            "exists — trying hardware GLES2 first\n");
        } else {
            fprintf(stderr, "synui: VM/hypervisor detected — using software GLES2 (llvmpipe)\n");
            setenv("WLR_RENDERER_FORCE_SOFTWARE", "1", 1);
            setenv("WLR_RENDERER_ALLOW_SOFTWARE", "1", 1);
            setenv("WLR_RENDERER", "pixman", 1);
        }
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
    if (greeter) {
        server.greeter = 1;
        /* The greeter is a login screen, not a desktop: no welcome menu, no
         * dock, no autostarted apps — gated below on server.greeter. */
        server.config.welcome_at_startup = 0;
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
