/*
 * output_mgmt.c — output-management (wlr-randr / kanshi), DPMS, gamma
 *
 * wlr_output_manager_v1 lets clients query and reconfigure outputs (mode,
 * scale, transform, position, enable/disable). wlr_output_power_manager_v1
 * exposes per-output power (DPMS on/off). wlr_gamma_control_manager_v1 lets
 * night-light tools (wlsunset, gammastep) set per-output gamma ramps.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>

#include "synui.h"

/* Broadcast the current output configuration to management clients. Called on
 * hotplug and after any apply so wlr-randr reflects reality. */
void output_mgmt_update(syn_server_t *s)
{
    if (!s->output_mgr) return;

    struct wlr_output_configuration_v1 *config =
        wlr_output_configuration_v1_create();

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        struct wlr_output_configuration_head_v1 *head =
            wlr_output_configuration_head_v1_create(config, o->wlr_output);
        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, o->wlr_output, &box);
        head->state.x = box.x;
        head->state.y = box.y;
    }

    wlr_output_manager_v1_set_configuration(s->output_mgr, config);
}

/* Apply (or just test) a client-requested configuration. */
static void apply_configuration(syn_server_t *s,
                                struct wlr_output_configuration_v1 *config,
                                bool test_only)
{
    bool ok = true;

    /* First pass: test every head so we commit all-or-nothing. */
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        struct wlr_output *o = head->state.output;
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&head->state, &state);
        if (!wlr_output_test_state(o, &state))
            ok = false;
        wlr_output_state_finish(&state);
        if (!ok) break;
    }

    if (ok && !test_only) {
        wl_list_for_each(head, &config->heads, link) {
            struct wlr_output *o = head->state.output;
            struct wlr_output_state state;
            wlr_output_state_init(&state);
            wlr_output_head_v1_state_apply(&head->state, &state);
            wlr_output_commit_state(o, &state);
            wlr_output_state_finish(&state);

            /* Position isn't part of output state — apply it to the layout. */
            if (head->state.enabled)
                wlr_output_layout_add(s->output_layout, o,
                                      head->state.x, head->state.y);
            else
                wlr_output_layout_remove(s->output_layout, o);
        }
    }

    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);

    if (ok && !test_only)
        output_layout_changed(s);
}

/* Geometry changed (protocol apply or the built-in display panel): re-place
 * layer surfaces (which recomputes each output's usable area and re-tiles
 * its workspace), lock surfaces, and the compositor UI — otherwise
 * everything keeps the stale pre-apply geometry until an unrelated
 * re-layout. Finishes by broadcasting the new config to clients. */
void output_layout_changed(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        layer_arrange_output(o);
    wallpaper_relayout(s);
    dock_relayout(s);
    /* The icon grid is sized against the primary output's usable box, so it
     * has to be rebuilt whenever that changes — including the very first time
     * an output appears, which is after the startup deskicons_reload(). */
    deskicons_layout(s);
    synui_render_deskicons(s);
    session_lock_arrange(s);
    if (s->overlay.visible)
        synui_render_overlay(s);
    if (s->cmdbar.visible)
        synui_render_cmdbar(s);
    if (s->dispcfg.visible)
        synui_render_dispcfg(s);

    output_mgmt_update(s);
    output_persist_save(s);

    /* The first output appearing is also this session's first chance to publish
     * what the LOGIN screen should show — the answer is resolved against the
     * primary screen, so before one exists there is nothing to resolve. It runs
     * on every layout change after that too, which is right: the primary can
     * move, and with it which wallpaper the greeter ought to be showing. Cheap
     * when nothing changed — greeterbg_publish compares the source's path, size
     * and mtime before copying anything. */
    greeterbg_publish(s);
}

static void output_mgr_apply(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, output_mgr_apply);
    apply_configuration(s, data, false);
}

static void output_mgr_test(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, output_mgr_test);
    apply_configuration(s, data, true);
}

/* ── DPMS ────────────────────────────────────────────────── */
static void output_power_set_mode(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_output_power_v1_set_mode_event *ev = data;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state,
        ev->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    if (!wlr_output_commit_state(ev->output, &state))
        wlr_log(WLR_ERROR, "synui: DPMS commit failed for %s",
                ev->output->name);
    wlr_output_state_finish(&state);
}

/* ── Gamma (night light) ─────────────────────────────────── */
static void gamma_set(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_gamma_control_manager_v1_set_gamma_event *ev = data;

    /* control == NULL means "reset to identity"; apply() handles both. */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!wlr_gamma_control_v1_apply(ev->control, &state)) {
        wlr_output_state_finish(&state);
        return;
    }
    if (!wlr_output_commit_state(ev->output, &state)) {
        /* Backend can't do gamma (e.g. headless/pixman) — tell the client
         * rather than leaving it waiting for a ramp that never applies. */
        wlr_log(WLR_INFO, "synui: gamma commit failed for %s",
                ev->output->name);
        if (ev->control)
            wlr_gamma_control_v1_send_failed_and_destroy(ev->control);
    }
    wlr_output_state_finish(&state);
}

/* ── Setup ───────────────────────────────────────────────── */
void output_mgmt_setup(syn_server_t *s)
{
    s->output_mgr = wlr_output_manager_v1_create(s->display);
    s->output_mgr_apply.notify = output_mgr_apply;
    wl_signal_add(&s->output_mgr->events.apply, &s->output_mgr_apply);
    s->output_mgr_test.notify = output_mgr_test;
    wl_signal_add(&s->output_mgr->events.test, &s->output_mgr_test);

    s->power_mgr = wlr_output_power_manager_v1_create(s->display);
    s->output_power_set_mode.notify = output_power_set_mode;
    wl_signal_add(&s->power_mgr->events.set_mode, &s->output_power_set_mode);

    s->gamma_mgr = wlr_gamma_control_manager_v1_create(s->display);
    s->gamma_set.notify = gamma_set;
    wl_signal_add(&s->gamma_mgr->events.set_gamma, &s->gamma_set);
}
