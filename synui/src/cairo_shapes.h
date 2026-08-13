/*
 * cairo_shapes.h — see cairo_shapes.c.
 *
 * Standalone of synui.h on purpose, so the test can link the implementation
 * without a compositor behind it. synui.h includes this rather than declaring
 * the function itself, which is what keeps the one declaration honest.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#ifndef SYNUI_CAIRO_SHAPES_H
#define SYNUI_CAIRO_SHAPES_H

#include <cairo/cairo.h>

/*
 * Append a rounded rectangle to the current path — the cairo half of the corner
 * radius, for the panels that stroke or fill their own frame in the overlay
 * buffer rather than leaving it to a scene rect.
 *
 * `r <= 0` (and NaN) degenerate to a plain cairo_rectangle() rather than being
 * an error, so a caller can pass chrome_corner_radius() straight through and a
 * square desktop keeps drawing the square frame it always did. r is clamped to
 * half the shorter side: a radius wider than the box turns the arcs inside out.
 */
void cairo_rounded_rect(cairo_t *cr, double x, double y,
                        double w, double h, double r);

#endif /* SYNUI_CAIRO_SHAPES_H */
