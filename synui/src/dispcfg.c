/*
 * dispcfg.c — built-in display settings panel
 *
 * A compositor-drawn menu (Super+D, or "Display Settings" on the welcome
 * menu) for configuring monitors without external tools:
 *
 *   Up/Down (j/k)      select a monitor
 *   Left/Right (h/l)   rotate it (normal → 90° → 180° → 270°)
 *   [ / ]              move it earlier/later in the arrangement
 *   a                  toggle arrangement axis (row ↔ column)
 *   Enter / Esc / q    close
 *
 * Every change applies immediately: the transform is committed to the
 * output, then all outputs are re-chained edge-to-edge along the chosen
 * axis and the usual post-apply reflow runs (output_layout_changed —
 * the same path the wlr-randr protocol handler uses).
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "synui.h"

static syn_output_t *selected_output(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    if (d->selected < 0 || d->selected >= d->count) return NULL;
    return d->order[d->selected];
}

/* Seed order[] from the current layout, sorted by position so the panel
 * lists monitors in the order they sit on the desk. */
static void dispcfg_seed(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    d->count = 0;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (d->count >= DISPCFG_MAX_OUTPUTS) break;
        d->order[d->count++] = o;
    }

    /* Insertion sort by layout (x, y). */
    for (int i = 1; i < d->count; i++) {
        syn_output_t *cur = d->order[i];
        struct wlr_box bc;
        wlr_output_layout_get_box(s->output_layout, cur->wlr_output, &bc);
        int j = i - 1;
        while (j >= 0) {
            struct wlr_box bj;
            wlr_output_layout_get_box(s->output_layout,
                                      d->order[j]->wlr_output, &bj);
            if (bj.x < bc.x || (bj.x == bc.x && bj.y <= bc.y)) break;
            d->order[j + 1] = d->order[j];
            j--;
        }
        d->order[j + 1] = cur;
    }

    if (d->selected >= d->count)
        d->selected = d->count ? d->count - 1 : 0;
    if (d->selected < 0)
        d->selected = 0;
}

/* Re-position every output edge-to-edge along the arrangement axis (using
 * transform-aware effective sizes), then run the shared post-apply reflow. */
static void dispcfg_rechain(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    int pos = 0;
    for (int i = 0; i < d->count; i++) {
        struct wlr_output *wo = d->order[i]->wlr_output;
        int w, h;
        wlr_output_effective_resolution(wo, &w, &h);
        if (d->column) {
            wlr_output_layout_add(s->output_layout, wo, 0, pos);
            pos += h;
        } else {
            wlr_output_layout_add(s->output_layout, wo, pos, 0);
            pos += w;
        }
    }
    output_layout_changed(s);   /* re-renders the panel too */
}

static void dispcfg_rotate(syn_server_t *s, int dir)
{
    syn_dispcfg_t *d = &s->dispcfg;
    syn_output_t *o = selected_output(s);
    if (!o) return;

    /* Cycle the pure rotations; a flipped transform folds back onto its
     * rotation first (transform & 3 is the rotation component). */
    int rot = (((int)o->wlr_output->transform & 3) + dir) & 3;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_transform(&state, (enum wl_output_transform)rot);
    if (!wlr_output_test_state(o->wlr_output, &state) ||
        !wlr_output_commit_state(o->wlr_output, &state)) {
        wlr_output_state_finish(&state);
        snprintf(d->status, sizeof(d->status),
                 "%s: rotation rejected by backend", o->wlr_output->name);
        synui_render_dispcfg(s);
        return;
    }
    wlr_output_state_finish(&state);

    static const char *rot_names[] = { "normal", "90°", "180°", "270°" };
    snprintf(d->status, sizeof(d->status), "%s → %s",
             o->wlr_output->name, rot_names[rot]);

    /* The effective size changed — re-chain so neighbours don't overlap. */
    dispcfg_rechain(s);
}

static void dispcfg_reorder(syn_server_t *s, int dir)
{
    syn_dispcfg_t *d = &s->dispcfg;
    int i = d->selected, j = i + dir;
    if (i < 0 || i >= d->count || j < 0 || j >= d->count) return;

    syn_output_t *tmp = d->order[i];
    d->order[i] = d->order[j];
    d->order[j] = tmp;
    d->selected = j;

    snprintf(d->status, sizeof(d->status), "%s moved %s",
             d->order[j]->wlr_output->name,
             d->column ? (dir < 0 ? "up" : "down")
                       : (dir < 0 ? "left" : "right"));
    dispcfg_rechain(s);
}

void dispcfg_show(syn_server_t *s)
{
    s->dispcfg.visible = 1;
    s->dispcfg.status[0] = '\0';
    dispcfg_seed(s);
    synui_render_dispcfg(s);
}

void dispcfg_hide(syn_server_t *s)
{
    s->dispcfg.visible = 0;
    synui_render_dispcfg(s);
}

void dispcfg_toggle(syn_server_t *s)
{
    if (s->dispcfg.visible) dispcfg_hide(s);
    else                    dispcfg_show(s);
}

void dispcfg_outputs_changed(syn_server_t *s)
{
    if (!s->dispcfg.visible) return;
    dispcfg_seed(s);
    synui_render_dispcfg(s);
}

int dispcfg_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_dispcfg_t *d = &s->dispcfg;
    if (!d->visible) return 0;

    /* Modified combos (Super+…) still reach the bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        dispcfg_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (d->selected > 0) d->selected--;
        synui_render_dispcfg(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (d->selected < d->count - 1) d->selected++;
        synui_render_dispcfg(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        dispcfg_rotate(s, -1);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        dispcfg_rotate(s, +1);
        return 1;
    case XKB_KEY_bracketleft:
        dispcfg_reorder(s, -1);
        return 1;
    case XKB_KEY_bracketright:
        dispcfg_reorder(s, +1);
        return 1;
    case XKB_KEY_a:
        d->column = !d->column;
        snprintf(d->status, sizeof(d->status), "arrangement: %s",
                 d->column ? "column (top→bottom)" : "row (left→right)");
        dispcfg_rechain(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
