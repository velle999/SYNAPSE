/*
 * output_exile.c — a window goes back to the monitor it was rescued from
 *
 * ⛔ THE BUG. A monitor that goes to standby does not stay connected. The panel
 * drops its link, the kernel raises a drm change event, wlroots destroys the
 * wlr_output, and output_destroy() (synui_main.c) sweeps every window that was
 * on it onto a screen that still exists. That sweep is not optional: a
 * view->output pointing at a freed output is a crash, and a box in a region the
 * layout no longer covers is a window nobody can reach.
 *
 * The other half was missing. The monitor COMES BACK — two seconds later, every
 * single time the screens wake — and the windows stayed where the rescue put
 * them. From velle's journal, a wake on 2026-09-19:
 *
 *     15:34:27  power: DP-3 is being destroyed with no sink — releasing its CRTC
 *     15:34:27  2 window(s) re-homed from DP-3 onto DP-2
 *     15:34:29  new output DP-3 2560x1440 — showing workspace 1
 *
 * Two seconds of DP-3 being away, and both windows spent the rest of the day on
 * DP-2. That is the whole report: let the screens stand by with windows open,
 * and the windows are on DP-2 when they wake.
 *
 * ── WHAT IS REMEMBERED ───────────────────────────────────────────────────────
 *
 * The CONNECTOR NAME, not the output. The syn_output_t is freed moments after
 * the sweep and the wlr_output with it, so a pointer is the one thing that
 * cannot be kept; the name is also exactly what comes back — DP-3 re-enumerates
 * as DP-3 — and it is what outputs.conf, input_maps_reapply() and the display
 * panel all key on already.
 *
 * And the BOX, RELATIVE TO THAT OUTPUT'S ORIGIN. A window's x/y are absolute
 * layout coordinates, so they name a monitor as much as a position, and a
 * monitor that comes back at a different place in the desk (a mode change while
 * it was away, a grid the display panel re-packed) would otherwise hand the
 * window back coordinates that are now over its neighbour. Relative survives
 * that; the placement clamps to the screen that exists NOW, exactly as
 * view_place_saved_box() does for every other "put it back where it was".
 *
 * ⚠ THE FIRST HOME WINS. Screens can go one after another — the 07:03 blackout
 * in the same journal took HDMI-A-1 onto DP-3 and then DP-3 onto nothing at all
 * — and a record overwritten by the second hop would send the window back to
 * the screen it was only passing through. So a view that already carries a
 * record keeps it, and the cascade unwinds correctly however it happened.
 *
 * ⚠ AN EXPLICIT MOVE FORGETS. Super+O, a drag onto another monitor, anything
 * that goes through view_set_output(): the user has just said where the window
 * lives, and a monitor re-appearing an hour later must not overrule them. That
 * is what keeps "it comes home" from becoming a second kind of window-moving.
 *
 * There is no timer. The record costs 80 bytes on a view that is already
 * kilobytes, and any expiry would be a guess about how long a screen is allowed
 * to be off — a suspend is minutes, a standby overnight is hours, and neither
 * changes where the window belongs.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"

void view_exile_forget(syn_view_t *view)
{
    if (!view) return;
    view->exile_from[0] = '\0';
    view->exile_geo = (struct wlr_box){ 0, 0, 0, 0 };
}

/*
 * The screen is going away: move its windows onto `home` (NULL when it was the
 * last one) and remember where they came from. Returns how many moved.
 *
 * Called from output_destroy() with the output still in the layout — synui's
 * destroy listener is registered before wlr_output_layout_add_auto() adds its
 * own, so it runs first and the origin below is still readable — and from
 * dispcfg_detach(), which removes the output from the layout after this.
 */
int output_exile_take(syn_server_t *s, syn_output_t *going, syn_output_t *home)
{
    if (!s || !going || going == home) return 0;

    /* The origin the saved boxes are measured from. output_box_of() answers
     * 0,0 1920x1080 for an output the layout has already dropped, which makes
     * the record absolute rather than relative — the honest fallback, since an
     * absolute box is what we would have had to store anyway. */
    struct wlr_box ob;
    output_box_of(s, going, &ob);

    int moved = 0;
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[i].windows, link) {
            if (v->output != going) continue;
            /* The FIRST home wins — see the header. */
            if (!v->exile_from[0]) {
                snprintf(v->exile_from, sizeof(v->exile_from), "%s",
                         going->wlr_output->name);
                v->exile_geo = (struct wlr_box){ v->x - ob.x, v->y - ob.y,
                                                 v->w, v->h };
            }
            v->output = home;   /* NULL only when the last monitor goes */
            moved++;
        }
    }

    if (moved)
        wlr_log(WLR_INFO, "synui: %d window(s) re-homed from %s onto %s — "
                "they go back when %s does",
                moved, going->wlr_output->name,
                home ? home->wlr_output->name : "(no output left)",
                going->wlr_output->name);
    return moved;
}

/* Does this window carry its own box, or does a layout own it? A tiled,
 * maximized or edge-expanded window is re-fitted by layout_apply() against
 * whatever monitor it is on, so handing it back its old box would only be a
 * resize the tiler immediately overwrites. Same question layout.c asks when it
 * leaves fullscreen, asked the same way. */
static bool exile_owns_its_box(syn_view_t *v)
{
    if (v->maximized || v->expanded) return false;
    return v->floating ||
           (v->workspace && v->workspace->layout == LAYOUT_FLOATING);
}

/*
 * The screen is back: every window that was rescued off it comes home, to the
 * box it had there. Returns how many came back.
 *
 * Called from server_new_output() once the output is in the layout and has its
 * saved mode and position, so the origin below is the one the window's box will
 * actually be measured against.
 */
int output_exile_return(syn_server_t *s, syn_output_t *back)
{
    if (!s || !back || !back->wlr_output) return 0;

    const char *name = back->wlr_output->name;
    struct wlr_box ob;
    output_box_of(s, back, &ob);

    int came = 0;
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[i].windows, link) {
            if (strcmp(v->exile_from, name) != 0) continue;

            struct wlr_box saved = v->exile_geo;
            view_exile_forget(v);
            v->output = back;
            came++;

            if (v->fullscreen) {
                /* The box belongs to the screen, not to the window: it was
                 * covering the old monitor and is now covering this one. */
                view_resize(v, ob.x, ob.y, ob.width, ob.height);
                view_fullscreen_rescale(v);
            } else if (saved.width > 0 && saved.height > 0 &&
                       exile_owns_its_box(v)) {
                saved.x += ob.x;
                saved.y += ob.y;
                view_place_saved_box(s, v, saved);
            }
        }
    }

    if (!came) return 0;

    /* The windows just moved between monitors, and under per-monitor desktops
     * that changes whether they are on screen at all — this screen may be
     * showing another desktop than the one they were parked on. Both halves,
     * in the order output_destroy() does them. */
    view_refresh_visibility(s);
    layout_apply_visible(s);

    wlr_log(WLR_INFO, "synui: %d window(s) went back to %s", came, name);
    return came;
}
