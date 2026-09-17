/*
 * gesture.c — touchpad gestures as binds: `gesture = swipe:4:left ws next`.
 *
 * libinput does the hard half. Telling a four-finger swipe from a pinch, from
 * two fingers scrolling, from a palm resting on the pad — all of that is
 * decided before wlroots hands synui a swipe_begin with `fingers = 4`. What is
 * left is two questions, and this file answers them with no compositor in it
 * so that tests/gesture_test.c can ask them without a touchpad:
 *
 *   1. AT BEGIN: is this gesture synui's, or the app's? The answer has to be
 *      given before the direction is known, because the begin event is the one
 *      a client would be sent first. So synui takes the whole kind-and-count
 *      when anything in the table wants it — a bound `swipe:4:left` means every
 *      four-finger swipe stops reaching apps, in every direction. A client must
 *      never see a begin without its end, or an end without its begin.
 *
 *   2. AT END: which way did it go? The rule is sway's (common/gesture.c):
 *      whichever axis moved further wins, with no distance threshold of its
 *      own. libinput will not report a swipe that has not moved, and a rule
 *      that has run on sway's users' touchpads since 1.8 is a better starting
 *      point than a number tuned here, where there is no touchpad to tune it
 *      on. A pinch is the one place a threshold exists: 10% either way, also
 *      sway's, because a pinch always drifts a little from 1.0.
 *
 * The action runs on RELEASE, never mid-swipe. The desktop does not follow the
 * fingers — the switch animation plays once the gesture is over, the same as
 * it does for Super+2.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <math.h>

#include "synui.h"

/* sway's min_scale_delta. A pinch between 0.9 and 1.1 has no direction. */
#define PINCH_MIN_SCALE_DELTA 0.1

int gesture_direction(int kind, double dx, double dy, double scale)
{
    switch (kind) {
    case SYN_GESTURE_SWIPE:
        /* No motion is no direction. sway would call it "up" (the else arm of
         * its comparison); a swipe that summed to nothing did not go anywhere. */
        if (dx == 0.0 && dy == 0.0) return SYN_GESTURE_DIR_NONE;
        if (fabs(dx) > fabs(dy))
            return dx > 0.0 ? SYN_GESTURE_RIGHT : SYN_GESTURE_LEFT;
        return dy > 0.0 ? SYN_GESTURE_DOWN : SYN_GESTURE_UP;
    case SYN_GESTURE_PINCH:
        if (scale > 1.0 + PINCH_MIN_SCALE_DELTA) return SYN_GESTURE_OUT;
        if (scale < 1.0 - PINCH_MIN_SCALE_DELTA) return SYN_GESTURE_IN;
        return SYN_GESTURE_DIR_NONE;
    default:
        return SYN_GESTURE_DIR_NONE;
    }
}

bool gesture_begin(syn_gesture_tracker_t *t, const syn_config_t *cfg,
                   int kind, uint32_t fingers, bool may_claim)
{
    /* A begin while one is still claimed means the last end never came — a
     * touchpad unplugged mid-swipe. That gesture is over either way; nothing
     * about it may leak into this one. */
    *t = (syn_gesture_tracker_t){ 0 };

    if (!may_claim || !cfg->gestures) return false;

    for (int i = 0; i < cfg->gesture_count; i++) {
        const syn_gesture_bind_t *g = &cfg->gesture_binds[i];
        if (g->kind == kind && (uint32_t)g->fingers == fingers) {
            t->active  = true;
            t->kind    = kind;
            t->fingers = (int)fingers;
            t->scale   = 1.0;
            return true;
        }
    }
    return false;
}

bool gesture_update(syn_gesture_tracker_t *t, int kind,
                    double dx, double dy, double scale)
{
    if (!t->active || t->kind != kind) return false;
    t->dx += dx;
    t->dy += dy;
    /* libinput's pinch scale is absolute since the begin, not a step. */
    if (kind == SYN_GESTURE_PINCH) t->scale = scale;
    return true;
}

bool gesture_end(syn_gesture_tracker_t *t, const syn_config_t *cfg,
                 int kind, bool cancelled, const syn_gesture_bind_t **out)
{
    *out = NULL;
    if (!t->active || t->kind != kind) return false;

    syn_gesture_tracker_t done = *t;
    *t = (syn_gesture_tracker_t){ 0 };

    /* libinput cancels a gesture when a finger is added or lifted part-way, or
     * when it turns out to be something else. That is not a swipe the user
     * finished, and nothing runs for it. */
    if (cancelled || !cfg->gestures) return true;

    int dir = gesture_direction(done.kind, done.dx, done.dy, done.scale);
    if (dir == SYN_GESTURE_DIR_NONE) return true;

    for (int i = 0; i < cfg->gesture_count; i++) {
        const syn_gesture_bind_t *g = &cfg->gesture_binds[i];
        if (g->kind == done.kind && g->fingers == done.fingers && g->dir == dir) {
            *out = g;
            break;
        }
    }
    return true;
}
