/*
 * output_mgmt.c — output-management (wlr-randr / kanshi) and DPMS
 *
 * wlr_output_manager_v1 lets clients query and reconfigure outputs (mode,
 * scale, transform, position, enable/disable). wlr_output_power_manager_v1
 * exposes per-output power (DPMS on/off).
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>

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
        output_mgmt_update(s);
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
}
