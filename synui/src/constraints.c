/*
 * constraints.c — pointer-constraints + relative-pointer
 *
 * Games and remote-desktop clients lock or confine the pointer to their
 * surface (zwp_pointer_constraints_v1) and read raw deltas through
 * zwp_relative_pointer_v1. synui activates whichever constraint belongs to
 * the surface holding pointer focus; input.c feeds every relative motion
 * through constraints_apply_motion() and always broadcasts the raw delta to
 * relative-pointer clients, so a locked pointer still delivers look input.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>

#include <wlr/util/region.h>

#include "synui.h"

typedef struct {
    syn_server_t                     *server;
    struct wlr_pointer_constraint_v1 *constraint;
    struct wl_listener                destroy;
} syn_constraint_t;

/* Layout-space origin of the constraint's surface: the owning view's
 * position. Returns 0 if the surface isn't a mapped toplevel we track. */
static int constraint_surface_origin(syn_server_t *s,
                                     struct wlr_surface *surface,
                                     double *lx, double *ly)
{
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[i].windows, link) {
            if (v->mapped && view_surface(v) == surface) {
                *lx = v->x;
                *ly = v->y;
                return 1;
            }
        }
    }
    return 0;
}

/* When a lock ends, honour the client's cursor-position hint (e.g. a game
 * parking the cursor on its crosshair) so control returns without a jump. */
static void constraint_warp_to_hint(syn_server_t *s,
                                    struct wlr_pointer_constraint_v1 *constraint)
{
    if (!constraint->current.cursor_hint.enabled)
        return;

    double sx = constraint->current.cursor_hint.x;
    double sy = constraint->current.cursor_hint.y;
    double lx, ly;
    if (constraint_surface_origin(s, constraint->surface, &lx, &ly)) {
        wlr_cursor_warp(s->cursor, NULL, lx + sx, ly + sy);
        s->cursor_x = s->cursor->x;
        s->cursor_y = s->cursor->y;
    }
    wlr_seat_pointer_warp(s->seat, sx, sy);
}

static void constraint_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_constraint_t *sc = wl_container_of(listener, sc, destroy);
    syn_server_t *s = sc->server;

    if (s->active_constraint == sc->constraint) {
        constraint_warp_to_hint(s, sc->constraint);
        s->active_constraint = NULL;
    }
    wl_list_remove(&sc->destroy.link);
    wlr_log(WLR_DEBUG, "synui: pointer constraint destroyed");
    free(sc);
}

static void server_new_constraint(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;

    syn_constraint_t *sc = calloc(1, sizeof(*sc));
    if (!sc) return;
    sc->server = s;
    sc->constraint = constraint;
    sc->destroy.notify = constraint_destroy;
    wl_signal_add(&constraint->events.destroy, &sc->destroy);

    wlr_log(WLR_DEBUG, "synui: new %s pointer constraint",
            constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED
                ? "locked" : "confined");

    /* If its surface already holds pointer focus, engage immediately —
     * clients typically lock in response to a click they just received. */
    if (s->seat->pointer_state.focused_surface == constraint->surface)
        constraints_focus_surface(s, constraint->surface);
}

void constraints_focus_surface(syn_server_t *s, struct wlr_surface *surface)
{
    if (!s->pointer_constraints) return;

    struct wlr_pointer_constraint_v1 *constraint = surface
        ? wlr_pointer_constraints_v1_constraint_for_surface(
              s->pointer_constraints, surface, s->seat)
        : NULL;
    if (s->active_constraint == constraint) return;

    if (s->active_constraint) {
        /* Clear first: deactivating a oneshot constraint destroys it, and the
         * destroy handler must not treat it as still active (double warp). */
        struct wlr_pointer_constraint_v1 *old = s->active_constraint;
        s->active_constraint = NULL;
        wlr_pointer_constraint_v1_send_deactivated(old);
    }
    if (constraint) {
        s->active_constraint = constraint;
        wlr_pointer_constraint_v1_send_activated(constraint);
        wlr_log(WLR_DEBUG, "synui: pointer constraint activated");
    }
}

int constraints_apply_motion(syn_server_t *s, double *dx, double *dy)
{
    struct wlr_pointer_constraint_v1 *c = s->active_constraint;
    if (!c) return 0;
    /* A constraint only binds while its surface holds pointer focus. */
    if (s->seat->pointer_state.focused_surface != c->surface) return 0;

    if (c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
        return 1;

    /* Confined: clamp the delta so the surface-local position stays inside
     * the constraint region. The seat tracks the current surface-local
     * coordinates from the last pointer motion we sent. */
    double sx = s->seat->pointer_state.sx;
    double sy = s->seat->pointer_state.sy;
    double cx, cy;
    if (wlr_region_confine(&c->region, sx, sy, sx + *dx, sy + *dy, &cx, &cy)) {
        *dx = cx - sx;
        *dy = cy - sy;
    }
    return 0;
}

void constraints_setup(syn_server_t *s)
{
    s->relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(s->display);
    s->pointer_constraints  = wlr_pointer_constraints_v1_create(s->display);
    s->new_constraint.notify = server_new_constraint;
    wl_signal_add(&s->pointer_constraints->events.new_constraint,
                  &s->new_constraint);
}
