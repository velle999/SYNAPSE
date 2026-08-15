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

/* One 8-bit sRGB channel value → linear, table-backed. Callers measuring whole
 * strips of pixels go through this rather than syn_rel_luminance's doubles: a
 * pow() per channel per pixel over a 3840-wide strip is ~400k calls on every
 * repaint, and the table makes each one a load. Clamps out of range. */
double syn_srgb_lut(int v);
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

/* ── A bar with no background of its own ─────────────────── */
/*
 * macOS 26's menu bar draws NOTHING behind itself: its ink sits straight on the
 * wallpaper. That makes the wallpaper the surface, and the surface is no longer
 * something a theme can know — #1D1D1F is 12.6:1 on Tahoe's own pale desktop and
 * 1.2:1 on the near-black one this box actually runs. So the ink is CHOSEN from
 * the backdrop rather than shipped with the palette, which is what macOS does.
 *
 * Two colours only, because that is the honest range: a clear bar cannot tint
 * itself out of trouble, so the ink is either black or white and the answer is
 * which one clears CONTRAST_TARGET. NONE is a real answer and the important one
 * — a mid-tone backdrop where NEITHER passes, and the caller must not go clear.
 */
typedef enum {
    SYN_INK_NONE = 0,   /* no legible ink — do not draw a clear bar here */
    SYN_INK_DARK,       /* dark ink; the backdrop is pale */
    SYN_INK_LIGHT,      /* light ink; the backdrop is dark */
} syn_ink_t;

/* The two ink colours the enum names, 0..1 sRGB. Not the theme's — see above. */
#define SYN_INK_DARK_LUM  0.0122771  /* #1D1D1F, Apple's `label` */
#define SYN_INK_LIGHT_LUM 1.0        /* #FFFFFF */

/* Which of the two clears `target` over a backdrop of relative luminance `lum`.
 * When both do (a mid-grey never happens, but a backdrop can be pale enough for
 * dark ink and still let white pass at a lower target) the HIGHER contrast wins,
 * so the answer does not flip on a rounding error near the boundary. */
syn_ink_t syn_ink_for_backdrop(double lum, double target);

/*
 * The better of the two, WHETHER OR NOT it clears the target. NONE only when the
 * backdrop was not measured.
 *
 * This exists because "neither ink is legible on this wallpaper" was being
 * answered by putting the bar's background back, and a near-opaque strip is a
 * loud fix for a problem the user experiences as "my bar stops being see-through
 * on some wallpapers, and comes back if I change it". The band that triggers it
 * is narrow — roughly 0.184 to 0.238 relative luminance — so an evenly-lit
 * photograph falls in and out of it for no reason the user can see.
 *
 * A SCRIM is the better answer: a thin wash in the opposite direction to the ink
 * pushes the backdrop out of the band and buys ~8:1 at a third of the coverage
 * an opaque strip needs, so the bar still reads as glass. But a scrim can only
 * be laid in ONE direction, and picking it needs the losing question answered:
 * not "which ink passes" (neither) but "which ink is closer". Hence a second
 * value alongside the first rather than folding the two — the bar has to be able
 * to tell "clear is safe" from "clear is safe once I dim it", because those are
 * different pixels.
 */
syn_ink_t syn_ink_best(double lum);

/* Fold two monitors' answers into one. The bar's ink is a singleton in QML — one
 * value for every screen — so two screens that disagree have no shared answer,
 * and the honest result is NONE rather than picking a side and leaving the other
 * monitor's clock invisible. NONE absorbs, which also makes a monitor whose
 * backdrop could not be measured veto the clear bar instead of being silently
 * skipped.
 *
 * That last set is smaller than it looks, and assuming otherwise was a bug: an
 * external client painting the background (wallpaper-engine) is unknowable, but
 * synui's own solid colour and its own matrix rain are not — see wp_top_lum in
 * synui.h. Both used to answer NONE here, which turned the clear bar opaque on
 * exactly those two wallpaper choices. */
syn_ink_t syn_ink_combine(syn_ink_t a, syn_ink_t b);

/* The token written to backdrop.state and read by the bar: "dark", "light", or
 * "none". Never NULL. */
const char *syn_ink_name(syn_ink_t ink);

#endif /* SYNUI_CONTRAST_H */
