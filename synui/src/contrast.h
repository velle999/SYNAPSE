/*
 * contrast.h — WCAG relative luminance, and the one correction the panels need.
 *
 * Split out of render.c so it can be tested without linking the compositor:
 * it is pure arithmetic over colours and depends on nothing else in the tree.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#ifndef SYNUI_CONTRAST_H
#define SYNUI_CONTRAST_H

/* WCAG AA for body text. */
#define CONTRAST_TARGET 4.5

/* Above this relative luminance a panel surface counts as PALE. XP's beige
 * measures 0.81 and 95's silver 0.53; the palest surface any dark theme uses is
 * 0.02, so nothing sits near the line. */
#define SURFACE_PALE 0.35

/* Channels are 0..1 sRGB. */
double syn_rel_luminance(double r, double g, double b);
double syn_contrast(double r, double g, double b, double surface_lum);

/* Darken `in` into `out` until it clears CONTRAST_TARGET against a surface of
 * luminance `surface_lum`. A no-op on a dark surface, and a no-op on a pale one
 * when the colour already passes — see the comment on the definition, which is
 * the part that matters. */
void syn_contrast_fix(const float in[3], float out[3], double surface_lum);

/* synui's panels draw text as a POSITION between the surface and the ink, which
 * is what makes the ladder flip with the theme — but a position is not a
 * contrast, and the same rung buys far less separation on a pale surface than a
 * dark one. Returns the smallest position that clears `target` against `bg`, so
 * the lower half of the ladder can be clamped to it. Zero on a dark surface,
 * which makes the clamp a no-op and leaves every dark theme untouched. */
double syn_ink_floor(const float bg[3], const float ink[3], double target);

#endif /* SYNUI_CONTRAST_H */
