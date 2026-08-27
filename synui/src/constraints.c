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
 *
 * ⚠ THE REGION IS IN SURFACE COORDINATES AND THE DELTA IS NOT.
 *
 * A constraint region, and the seat's sx/sy, are surface-local: they count in
 * the units the CLIENT painted in. A cursor delta is layout-space: it counts
 * in the units the SCREEN is laid out in. On an ordinary window those are the
 * same number and mixing them is invisible.
 *
 * They stop being the same number the moment synui scales a client's buffer,
 * which it does to exactly the clients this file exists for:
 * view_fullscreen_rescale() (xwayland.c) upscales a sub-native fullscreen X11
 * game — a 1920x1080 title on a 2560x1440 monitor — to fill the screen. Its
 * surface is then 1920 wide inside a 2560-wide box, so a confine that adds
 * layout deltas to surface coordinates runs out of region three quarters of
 * the way across: the pointer stops dead before the right-hand edge and the
 * bottom of the game are reachable. Every coordinate that crosses between the
 * two spaces here goes through constraint_surface_geom().
 *
 *
 * ⚠ wlr_region_confine() ANSWERS FALSE WHEN THE POINTER IS ALREADY OUTSIDE.
 *
 * It confines a movement, not a point: the old position has to be inside the
 * region or there is no ray to clip and it returns false. Treating that as
 * "nothing to clamp" leaves the delta untouched, so the one case a confinement
 * exists to handle — the pointer is out, put it back — is the case where the
 * confinement does nothing at all and the pointer walks out of the game.
 *
 * So there are two answers to that here, and both are needed. The pointer is
 * warped INTO the region when a confinement engages and whenever the region
 * moves under it (constraint_confine_cursor), which is what keeps the ordinary
 * case inside; and a motion that starts outside anyway is pulled to the nearest
 * point in the region rather than passed through (region_closest).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>

#include <wlr/util/region.h>

#include "synui.h"

typedef struct {
    syn_server_t                     *server;
    struct wlr_pointer_constraint_v1 *constraint;
    struct wl_listener                destroy;
    struct wl_listener                set_region;
} syn_constraint_t;

/* ── Surface space and layout space ──────────────────────────
 *
 * Where the constraint surface's origin sits on the screen, and how many
 * surface units one layout unit buys. Both come off the scene buffer the
 * surface is painted by rather than off the view: the view's x/y is the FRAME
 * origin (titlebar and border included) and knows nothing about a scaled
 * buffer, while the buffer node's own coordinates already carry the offset
 * view_fullscreen_rescale() centres a letterboxed game with.
 *
 * Answers 0 for a surface that is not a mapped toplevel of ours — a layer
 * surface, a popup, a subsurface. The caller keeps the 1:1 defaults, which is
 * what those surfaces have anyway: nothing else in synui scales a buffer.
 */
struct constraint_geom_probe {
    struct wlr_surface      *want;
    struct wlr_scene_buffer *buf;
};

static void constraint_geom_probe(struct wlr_scene_buffer *buffer,
                                  int sx, int sy, void *data)
{
    (void)sx; (void)sy;
    struct constraint_geom_probe *p = data;
    if (p->buf) return;
    struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
    if (ss && ss->surface == p->want) p->buf = buffer;
}

static int constraint_surface_geom(syn_server_t *s, struct wlr_surface *surface,
                                   double *ox, double *oy,
                                   double *kx, double *ky)
{
    *ox = *oy = 0.0;
    *kx = *ky = 1.0;
    if (!surface) return 0;

    struct wlr_scene_tree *root = NULL;
    for (int i = 0; i < WORKSPACE_MAX && !root; i++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[i].windows, link) {
            if (v->mapped && v->scene_tree && view_surface(v) == surface) {
                root = v->scene_tree;
                break;
            }
        }
    }
    if (!root) return 0;

    struct constraint_geom_probe p = { .want = surface, .buf = NULL };
    wlr_scene_node_for_each_buffer(&root->node, constraint_geom_probe, &p);
    if (!p.buf) return 0;

    int lx, ly;
    if (!wlr_scene_node_coords(&p.buf->node, &lx, &ly)) return 0;
    *ox = lx;
    *oy = ly;

    int sw = surface->current.width, sh = surface->current.height;
    if (sw > 0 && p.buf->dst_width  > 0) *kx = (double)sw / p.buf->dst_width;
    if (sh > 0 && p.buf->dst_height > 0) *ky = (double)sh / p.buf->dst_height;
    return 1;
}

/* The point inside `region` closest to (x, y), in surface coordinates.
 * Answers 0 for an empty region — there is nowhere to put the pointer, and
 * clamping to a rectangle that does not exist would park it at the origin. */
static int region_closest(const pixman_region32_t *region,
                          double x, double y, double *out_x, double *out_y)
{
    int nboxes = 0;
    const pixman_box32_t *boxes =
        pixman_region32_rectangles((pixman_region32_t *)region, &nboxes);
    if (nboxes <= 0) return 0;

    double best = -1.0;
    for (int i = 0; i < nboxes; i++) {
        /* Half a pixel inside the far edges. pixman's contains-point test
         * FLOORS the coordinate, so x2 is the first column OUTSIDE the box:
         * a point clamped exactly onto it fails the very test this answers,
         * and the next motion would find itself outside again. */
        double cx = x, cy = y;
        if (cx < boxes[i].x1)       cx = boxes[i].x1;
        if (cx > boxes[i].x2 - 0.5) cx = boxes[i].x2 - 0.5;
        if (cy < boxes[i].y1)       cy = boxes[i].y1;
        if (cy > boxes[i].y2 - 0.5) cy = boxes[i].y2 - 0.5;

        double d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        if (best < 0.0 || d < best) { best = d; *out_x = cx; *out_y = cy; }
    }
    return 1;
}

/* Put the pointer inside a confinement that has just taken effect, or whose
 * region has just moved. Without this the first wlr_region_confine() of the
 * session starts from a point outside the region, answers false, and the
 * confinement never engages at all — see the header. */
static void constraint_confine_cursor(syn_server_t *s,
                                      struct wlr_pointer_constraint_v1 *c)
{
    if (!c || c->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) return;
    if (!pixman_region32_not_empty(&c->region)) return;

    double ox, oy, kx, ky;
    if (!constraint_surface_geom(s, c->surface, &ox, &oy, &kx, &ky)) return;

    double sx = (s->cursor->x - ox) * kx;
    double sy = (s->cursor->y - oy) * ky;
    if (pixman_region32_contains_point(&c->region,
                                       (int)floor(sx), (int)floor(sy), NULL))
        return;

    double nx, ny;
    if (!region_closest(&c->region, sx, sy, &nx, &ny)) return;

    wlr_cursor_warp_closest(s->cursor, NULL, ox + nx / kx, oy + ny / ky);
    s->cursor_x = s->cursor->x;
    s->cursor_y = s->cursor->y;

    /* Re-seat what the seat believes. A cursor warp tells it nothing, and
     * constraints_apply_motion() starts every clamp from the seat's sx/sy —
     * left stale, the clamp would keep working from the point outside. */
    wlr_seat_pointer_warp(s->seat, (s->cursor->x - ox) * kx,
                                   (s->cursor->y - oy) * ky);
    wlr_log(WLR_DEBUG, "synui: pointer confined into its region");
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
    double ox, oy, kx, ky;
    if (constraint_surface_geom(s, constraint->surface, &ox, &oy, &kx, &ky)) {
        wlr_cursor_warp(s->cursor, NULL, ox + sx / kx, oy + sy / ky);
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
    wl_list_remove(&sc->set_region.link);
    wlr_log(WLR_DEBUG, "synui: pointer constraint destroyed");
    free(sc);
}

/* The region moved. A game that fullscreens, changes resolution or opens a
 * menu re-sends one, and the pointer is routinely outside the new region the
 * instant it lands — which is the state wlr_region_confine() cannot recover
 * from on its own. */
static void constraint_set_region(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_constraint_t *sc = wl_container_of(listener, sc, set_region);
    if (sc->server->active_constraint != sc->constraint) return;
    constraint_confine_cursor(sc->server, sc->constraint);
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
    sc->set_region.notify = constraint_set_region;
    wl_signal_add(&constraint->events.set_region, &sc->set_region);

    wlr_log(WLR_DEBUG, "synui: new %s pointer constraint",
            constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED
                ? "locked" : "confined");

    /* If its surface already holds pointer focus, engage immediately —
     * clients typically lock in response to a click they just received. */
    if (s->seat->pointer_state.focused_surface == constraint->surface)
        constraints_focus_surface(s, constraint->surface);
}

/* Does the keyboard-focused window own this constraint's surface?
 *
 * Deliberately the keyboard focus and not the pointer's: the pointer's is what
 * has just been lost, and losing it is exactly the event that must not be read
 * as the user leaving the window. */
static int constraint_surface_holds_focus(syn_server_t *s,
                                          struct wlr_pointer_constraint_v1 *c)
{
    if (!c || !s->focused_view || !s->focused_view->mapped) return 0;
    return view_surface(s->focused_view) == c->surface;
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
        /* ⚠ AN INCIDENTAL LOSS OF POINTER FOCUS MUST NOT END A LOCK.
         *
         * Deactivating a ONESHOT constraint DESTROYS it: the client has to ask
         * again, and a game that asked once at startup never does. Pointer
         * focus, though, is cleared by anything the cursor can reach that is
         * not a surface — a border, the grab ring, a letterbox bar inside a
         * fullscreen game's own frame. None of those are the user choosing a
         * different window, and treating them as such ended mouse-look for the
         * rest of the session (measured on Cyberpunk 2077; see
         * game_confine_rect(), which stops the cursor reaching the bar in the
         * first place — this is the second lock on the same door).
         *
         * The test is the KEYBOARD focus, which moves only when the user moves
         * it. While the constrained surface still owns that, the constraint
         * stays: Alt-Tab remains the escape hatch, and a real focus change to
         * another window still falls through and deactivates. */
        if (!constraint && constraint_surface_holds_focus(s, s->active_constraint))
            return;

        /* Clear first: deactivating a oneshot constraint destroys it, and the
         * destroy handler must not treat it as still active (double warp). */
        struct wlr_pointer_constraint_v1 *old = s->active_constraint;
        s->active_constraint = NULL;
        wlr_pointer_constraint_v1_send_deactivated(old);
    }
    if (constraint) {
        s->active_constraint = constraint;
        wlr_pointer_constraint_v1_send_activated(constraint);
        /* Before the first motion, not on it: a confinement that engages with
         * the pointer outside its region never engages at all. */
        constraint_confine_cursor(s, constraint);
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

    /* Confined with nothing to confine to. An empty region is not "hold the
     * pointer still" — a locked pointer is how a client asks for that. */
    if (!pixman_region32_not_empty(&c->region)) return 0;

    /* Confined: clamp the delta so the surface-local position stays inside the
     * constraint region. The seat tracks the current surface-local coordinates
     * from the last pointer motion we sent; the delta is layout-space and has
     * to be carried across (see the header). */
    double ox, oy, kx, ky;
    constraint_surface_geom(s, c->surface, &ox, &oy, &kx, &ky);

    double sx = s->seat->pointer_state.sx;
    double sy = s->seat->pointer_state.sy;
    double tx = sx + *dx * kx;
    double ty = sy + *dy * ky;

    double cx, cy;
    if (!wlr_region_confine(&c->region, sx, sy, tx, ty, &cx, &cy)) {
        /* The pointer is outside the region already, so there is no movement
         * to clip — put it back rather than letting this one through. */
        if (!region_closest(&c->region, tx, ty, &cx, &cy)) return 0;
    }

    *dx = (cx - sx) / kx;
    *dy = (cy - sy) / ky;
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
