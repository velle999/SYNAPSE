/*
 * pointer_confine_test.c — a confined pointer stays in the surface, and can
 * reach its edges.
 *
 * zwp_pointer_constraints_v1's confined mode is what a game asks for when it
 * wants the pointer kept inside its window without hiding it — strategy games
 * pushing the map at the screen edge, anything with an in-window cursor on a
 * multi-monitor desktop. synui's implementation of it did neither of the two
 * things it is named for, and both failures came out of one wlroots contract:
 *
 *   wlr_region_confine() confines a MOVEMENT. The old position has to be
 *   inside the region or there is no ray to clip, and it answers false.
 *
 * Reading that false as "nothing to clamp" and passing the delta through
 * means the one case the confinement exists for — the pointer is outside,
 * bring it back — is the case where the confinement does nothing whatsoever.
 * The pointer then walks out of the game and never comes back, because every
 * subsequent motion starts outside too.
 *
 * So this drives constraints_apply_motion() directly, which needs no
 * compositor: the constraint is a struct, the region is pixman's, and the
 * seat's surface-local position is the one field it reads. The scene lookup
 * finds nothing behind an empty workspace list and falls back to 1:1 units,
 * which is what an unscaled surface has anyway.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "synui.h"

/* The compositor half. constraints.c only asks this while walking the
 * workspaces for the view that owns a surface, and every list here is empty. */
struct wlr_surface *view_surface(syn_view_t *v) { (void)v; return NULL; }

static int fails;

static void check(const char *what, double want, double got)
{
    if (fabs(want - got) < 0.01) {
        printf("  ok    %s (%.2f)\n", what, got);
    } else {
        printf("  FAIL  %s — expected %.2f, got %.2f\n", what, want, got);
        fails++;
    }
}

int main(void)
{
    syn_server_t s;
    struct wlr_seat seat;
    struct wlr_pointer_constraint_v1 c;
    /* Only its address is ever compared. */
    struct wlr_surface *surface = (struct wlr_surface *)(void *)&c;

    memset(&s, 0, sizeof(s));
    memset(&seat, 0, sizeof(seat));
    memset(&c, 0, sizeof(c));
    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&s.workspaces[i].windows);

    s.seat = &seat;
    c.type    = WLR_POINTER_CONSTRAINT_V1_CONFINED;
    c.surface = surface;
    pixman_region32_init_rect(&c.region, 0, 0, 800, 600);
    s.active_constraint = &c;
    seat.pointer_state.focused_surface = surface;

    double dx, dy;

    printf("a confinement that is not engaged clamps nothing\n");
    s.active_constraint = NULL;
    dx = 5000; dy = 0;
    assert(constraints_apply_motion(&s, &dx, &dy) == 0);
    check("delta untouched with no constraint", 5000, dx);
    s.active_constraint = &c;

    printf("a locked pointer absorbs the motion entirely\n");
    c.type = WLR_POINTER_CONSTRAINT_V1_LOCKED;
    dx = 5000; dy = 0;
    check("locked answers 'the cursor stays put'", 1,
          constraints_apply_motion(&s, &dx, &dy));
    c.type = WLR_POINTER_CONSTRAINT_V1_CONFINED;

    printf("inside the region, an ordinary move is untouched\n");
    seat.pointer_state.sx = 400; seat.pointer_state.sy = 300;
    dx = 10; dy = -20;
    assert(constraints_apply_motion(&s, &dx, &dy) == 0);
    check("dx", 10, dx);
    check("dy", -20, dy);

    /* THE EDGE. A push past the right-hand side lands ON it rather than short
     * of it: the pointer has to be able to reach the last column of the
     * surface, which is what "can't reach the edges" was about. */
    printf("a push past the edge lands on the edge\n");
    seat.pointer_state.sx = 400; seat.pointer_state.sy = 300;
    dx = 5000; dy = 0;
    assert(constraints_apply_motion(&s, &dx, &dy) == 0);
    if (400 + dx < 799.0 || 400 + dx > 800.0) {
        printf("  FAIL  right edge — landed at %.2f, wanted [799, 800]\n",
               400 + dx);
        fails++;
    } else {
        printf("  ok    right edge (%.2f)\n", 400 + dx);
    }

    /* THE ESCAPE. The pointer is outside the region — a resize, a region the
     * client only just sent, a warp — so there is no ray to clip. The old code
     * let the delta through untouched and the pointer left for good. */
    printf("a move that STARTS outside is pulled back in, not passed through\n");
    seat.pointer_state.sx = 1200; seat.pointer_state.sy = 300;
    dx = 40; dy = 0;
    assert(constraints_apply_motion(&s, &dx, &dy) == 0);
    if (1200 + dx > 800.0) {
        printf("  FAIL  escape — pointer left at %.2f, still outside 0..800\n",
               1200 + dx);
        fails++;
    } else {
        printf("  ok    pulled back to %.2f\n", 1200 + dx);
    }

    /* …and once pulled back it must be strictly INSIDE, or the next motion
     * starts outside again and the confinement dies one event later. pixman
     * floors the coordinate, so 800.0 itself is already out. */
    printf("and lands somewhere the NEXT motion can confine from\n");
    seat.pointer_state.sx = 1200 + dx;
    seat.pointer_state.sy = 300;
    double px = seat.pointer_state.sx;
    dx = -5; dy = 0;
    assert(constraints_apply_motion(&s, &dx, &dy) == 0);
    check("a step back inside is an ordinary clipped move", -5, dx);
    (void)px;

    /* An empty region is not a lock. A client that confines to nothing gets
     * the pointer left alone rather than welded to the origin. */
    printf("an empty region confines nothing\n");
    pixman_region32_clear(&c.region);
    seat.pointer_state.sx = 400; seat.pointer_state.sy = 300;
    dx = 5000; dy = 0;
    assert(constraints_apply_motion(&s, &dx, &dy) == 0);
    check("empty region leaves the delta alone", 5000, dx);

    pixman_region32_fini(&c.region);

    if (fails) { printf("FAIL: %d check(s)\n", fails); return 1; }
    printf("PASS\n");
    return 0;
}
