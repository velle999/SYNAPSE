/*
 * cat_pace.c — how often the cat moves: 60 times a second, whatever the screen.
 *
 * The cat used to chain frames: every frame it drew asked for the next one, so
 * on a 144 Hz monitor it was redrawn — and everything under it recomposited —
 * 144 times a second, sitting or asleep included (the tail never stops). With
 * glass on, every one of those frames was a full-output blur: 57-80% of a core
 * on an idle desktop (2026-10-15). 60 steps a second is as smooth as a 70 px/s
 * walk needs to be.
 *
 * Pure arithmetic, no scene and no server, so tests/cat_pace_test.c can drive
 * it with the vblank times of a 144, 165, 60 or 30 Hz screen.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>

#include "cat_pace.h"

/*
 * Frames land on vblanks, never exactly on a due time. A step is taken on the
 * first frame no more than CAT_PACE_EARLY before it is due, and the due time
 * advances by exactly one period — so on 144 Hz the steps alternate two and
 * three vblanks apart and still average 60, and on a 60 Hz screen, whose
 * vblanks wobble around the due times, no frame is skipped.
 */
bool cat_pace_due(double *due, double now)
{
    if (*due <= 0.0 || now - *due > CAT_PACE_PERIOD) {
        /* First step, or back from a stall (a lock, a blank, a long frame):
         * start a fresh cadence rather than firing a burst of catch-up steps. */
        *due = now + CAT_PACE_PERIOD;
        return true;
    }
    if (now < *due - CAT_PACE_EARLY)
        return false;
    *due += CAT_PACE_PERIOD;
    return true;
}

/* Milliseconds until the wake-up for the next step: aimed CAT_PACE_EARLY ahead
 * of the due time, so the frame it asks for is never too early to step. */
int cat_pace_wait_ms(double due, double now)
{
    double s = due - CAT_PACE_EARLY - now;
    int ms = (int)(s * 1000.0 + 0.999);
    return ms < 1 ? 1 : ms;
}
