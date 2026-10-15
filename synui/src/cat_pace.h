/*
 * cat_pace.h — the cat's step rate, kept apart from the compositor so it can be
 * tested alone (tests/cat_pace_test.c). See cat_pace.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNUI_CAT_PACE_H
#define SYNUI_CAT_PACE_H

#include <stdbool.h>

#define CAT_PACE_FPS    60.0
#define CAT_PACE_PERIOD (1.0 / CAT_PACE_FPS)
#define CAT_PACE_EARLY  0.004   /* s; under half a 144 Hz vblank */

/* Take a step at `now`? If so, *due moves on to the next one. *due <= 0 means
 * "never stepped". */
bool cat_pace_due(double *due, double now);

/* How long to sleep before asking for the frame of the next step. */
int cat_pace_wait_ms(double due, double now);

#endif
