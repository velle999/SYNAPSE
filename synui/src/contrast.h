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

#include <stdbool.h>   /* syn_mark_ink_t's `rescued` — the only type not a double */

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

/* ── The wallpaper under a SURFACE, not just under the bar ──
 *
 * Everything above this line answers one question about one strip: the bar sits
 * at a known edge, so a single mean over its rows is the whole of what it needs.
 * Nothing else on the desktop has a fixed home. A menu opens where the pointer
 * is, the calculator opens in the middle, and a widget sits wherever it was
 * dragged — so "which ink is legible on the wallpaper" stopped being one answer
 * per monitor the moment anything but the bar had to ask it.
 *
 * A GRID rather than a second strip, or a mean over the whole picture. The mean
 * is what a per-output answer collapses to, and it is wrong in exactly the case
 * that made this necessary: a photograph with a bright sky over dark ground
 * averages to the mid-tone band where NEITHER ink passes, so every panel on the
 * desktop would take the scrim while most of them sat over ground that black
 * text reads on perfectly well.
 *
 * ⚠ THE CELLS ARE SIZED AGAINST THE BLUR, not against the panels. What a glass
 * panel actually sits on is the FROSTED wallpaper, and a blur is a local mean —
 * so a cell wants to be about the width the blur kernel already smears, and
 * measuring finer than that would be measuring detail the user cannot see
 * through the panel anyway. 16x9 puts a cell at ~160 logical px on this box's
 * 2560x1440, which is that scale.
 */
#define SYN_LUM_COLS  16
#define SYN_LUM_ROWS   9
#define SYN_LUM_CELLS (SYN_LUM_COLS * SYN_LUM_ROWS)

/*
 * What a surface of luminance `surface_lum` drawn at `alpha` over a backdrop of
 * luminance `backdrop_lum` actually reads as. alpha 1 is the surface untouched,
 * alpha 0 is the backdrop untouched.
 *
 * ⚠ MIXED IN THE ENCODING THE GPU MIXES IN, which is not the one these numbers
 * are in. Relative luminance is linear-light; scenefx blends 8-bit sRGB values
 * without linearising them first. Mixing the two luminances directly would
 * describe a compositor that works in linear light and would put the answer
 * several hundredths out in the midtones — which is the whole width of the band
 * where the ink flips, so it is the difference between a scrim and no scrim.
 *
 * A negative backdrop is "not measured" and passes the surface straight
 * through: an unmeasurable wallpaper must not be allowed to invent a luminance.
 */
double syn_lum_over(double surface_lum, double alpha, double backdrop_lum);

/* Contrast between two things already expressed as luminances. syn_contrast()
 * takes an r,g,b for the ink because its callers have one; the callers here have
 * a MEAN over wallpaper pixels on one side and often a mean on the other, and
 * neither ever existed as a triple. */
double syn_contrast_lum(double a, double b);

/* The 0..1 sRGB channel value of a neutral grey with this relative luminance —
 * srgb_to_linear's inverse, wrapped for the one caller outside this file.
 *
 * render.c needs it to build a stand-in for the surface a glass panel actually
 * presents: the ink floor (syn_ink_floor) wants a COLOUR, and what shows through
 * a see-through panel is the panel's own colour mixed with a wallpaper whose
 * hue was never measured — only its luminance. A grey of that luminance is the
 * honest stand-in, and it is exact for every question the floor asks, because
 * those are all contrast and contrast is a function of luminance alone. */
double syn_lum_to_srgb(double lum);

/* The backdrop under one box, in the answers the bar already asks for.
 * `lum` is the mean over the cells it covers and is what syn_lum_over() wants;
 * the two inks are for the surface itself, and follow the bar's contract
 * exactly — `ink` is "clear is safe here", `best` is "clear is safe once you
 * have dimmed it this way", and NONE from `ink` with a real `best` is a scrim.
 *
 * ⚠ AND THE TWO EXTREMES, BECAUSE THE MEAN IS THE WRONG STATISTIC FOR THE ONE
 * QUESTION THAT MATTERS MOST. The alpha walk (panel_alpha_floor in render.c, and
 * alphaWalkOn in Theme.qml) asks "does my text still read on the composite" and
 * asked it of `lum` alone, which over a photograph is an average of a dark tree
 * trunk and bright leaves — a number that describes no pixel on the screen. The
 * failure that produced it is exact: on a pale theme the mean would land just
 * past the AA threshold, the walk would decide no correction was needed, and the
 * menu would draw its ink straight onto the picture. Move a window, change the
 * wallpaper, drag the menu a cell to the left and the mean crosses back — so the
 * same menu in the same place frosts itself one minute and not the next, which
 * is exactly what it looks like from outside: a correction that works sometimes.
 *
 * The ink fold already refused to average — disagreeing cells veto, because one
 * surface draws one colour of text. The alpha had no equivalent and now does:
 * a surface has to survive its WORST cell, not its mean one. Both ends are kept
 * because both can be the worst, depending on which way the ink runs.
 */
typedef struct {
    double    lum;   /* mean luminance under the box, or -1 if unmeasured   */
    double    lum_min; /* darkest cell under it, or -1 if unmeasured        */
    double    lum_max; /* brightest cell under it, or -1 if unmeasured      */
    syn_ink_t ink;   /* the ink that clears `target` over ALL of it         */
    syn_ink_t best;  /* the closer of the two, whether or not it clears     */
} syn_backdrop_t;

/*
 * Fold the cells a box covers into one of those.
 *
 * The box is given in FRACTIONS of the output (0..1) rather than pixels, so
 * this stays pure arithmetic over the grid and needs to know nothing about
 * monitors, scale factors or layout coordinates. A box off the edge is clamped;
 * a box of no area still samples the cell it lands in, because a zero-width
 * panel is a panel that has not been laid out yet rather than one with no
 * backdrop.
 *
 * ⚠ `ink` FOLDS WITH syn_ink_combine, so a box straddling a dark cell and a
 * pale one gets NONE — the same "no shared answer" the two-monitor case gets,
 * for the same reason. One panel draws ONE colour of text, and a panel lying
 * half on sky and half on shadow has no single colour that reads on both.
 */
void syn_backdrop_for_box(const double *grid, double fx, double fy,
                          double fw, double fh, double target,
                          syn_backdrop_t *out);

/* ── A MARK drawn on a see-through surface ───────────────────
 *
 * Everything above answers for TEXT on a panel, where the surface is thick
 * enough to be a surface and the only question is which rung of the theme's own
 * ink ladder survives. This answers for the other case: a mark drawn straight
 * onto glass so thin there is nothing under it but the wallpaper — the dock's
 * clock, its apps grid, its power mark and its running dots.
 *
 * ⚠ THE THEME'S INK IS AN ASSUMPTION ABOUT THE SURFACE, AND GLASS BREAKS IT.
 * `panel_ink` is chosen to read on `panel_bg`, and for every preset that was
 * the same thing the mark landed on, because every PALE preset drew a solid
 * dock. Prism Light is the first that is pale AND see-through: at a body alpha
 * of 0.05 its near-black ink lands on a night photograph at 1.9:1 and the marks
 * are simply not there, while dark Prism's near-white ink on the same pixels is
 * 11.6:1. Neither theme ever asked what was behind it; one of them happened to
 * be right.
 *
 * Two colours, because that is the honest range for a surface that cannot tint
 * itself out of trouble — the same argument syn_ink_for_backdrop() is built on,
 * and this defers to it rather than inventing a third answer.
 *
 * ⚠ A RESCUE, NOT A POLICY, and `target` is what keeps it one. The theme's ink
 * stands wherever it still reads, so a solid dock, a dark preset and every
 * desktop that looks right today come out bit-for-bit unchanged. Callers pass
 * AA's large-text 3.0 rather than CONTRAST_TARGET deliberately: a dot grid at
 * 3.2:1 is a mark somebody chose, at 1.9:1 it is a mark nobody can find, and
 * asking for 4.5 would start repainting dark themes' marks black over a bright
 * photograph — a different change, and not one that was asked for.
 *
 * `accent` follows the ink rather than being left behind: it is the analog
 * clock's hands, and an accent picked to read on a near-white panel is exactly
 * as invisible on a night sky as the ink was. Pulled toward whichever pole the
 * ink landed on in quarter-steps until it separates, so it keeps its hue.
 *
 * An unmeasured backdrop, or one where neither ink beats the theme's, passes
 * both colours straight through — `rescued` says which happened.
 */
typedef struct {
    float ink[3];
    float accent[3];
    bool  rescued;   /* the theme's ink could not carry; these are chosen */
} syn_mark_ink_t;

void syn_mark_ink(const float surface[3], double alpha,
                  const float ink[3], const float accent[3],
                  const syn_backdrop_t *bd, double target,
                  syn_mark_ink_t *out);

/* ── A PANEL drawn on glass ──────────────────────────────────
 *
 * ⚠ syn_contrast_fix() AND syn_ink_floor() BOTH SIT OUT ON A MID-TONE SURFACE,
 * and until glass there was no such thing. Each opens with the same line —
 * `if (lum <= SURFACE_PALE) return` — because each was written when a panel's
 * surface was the THEME's own opaque colour, which is either dark (the ink is
 * near-white, everything clears, nothing to do) or pale (correct downward). A
 * glass panel's effective surface is neither: it is the theme's colour
 * composited over whatever is behind the panel, and over a bright window that
 * lands squarely in the middle.
 *
 * Measured on a stock Prism desktop with the shortcuts panel over a white web
 * page: the surface composites to L=0.135, and on it the wallpaper accent
 * measured 1.49:1, the hint line 1.24:1 and the count 1.70:1 — text that is on
 * screen and cannot be read. Both correctors were doing exactly what they say:
 * 0.135 is not pale, so both returned at the first line.
 *
 * ── The rule, and why it is this one ─────────────────────────
 *
 * EVERY COLOUR MUST REACH AT LEAST THE CONTRAST IT WOULD HAVE HAD ON THE
 * THEME'S OWN OPAQUE SURFACE, capped at its ordinary target.
 *
 * Relative, not absolute, and that is the whole of it being safe:
 *
 *   * On an opaque panel the composite IS the theme's surface, so the goal is
 *     already met by construction and NOTHING moves. Every desktop that is not
 *     glass is untouched, by arithmetic rather than by a gate.
 *   * On glass it restores precisely what the glass took away and never asks
 *     for more than the theme itself delivers. An absolute 4.5 would demand
 *     more of a glass Gruvbox than Gruvbox gives on its own panels — which is
 *     the "fixing four working dark themes to settle a complaint about two
 *     light ones" that panel_contrast_test exists to forbid.
 *   * It runs in whichever direction has headroom. syn_contrast_fix() only ever
 *     darkens, because on a pale surface that is the only way out; on a
 *     mid-tone composite the way out is usually up, and a corrector that can
 *     only go down has nothing to offer a dark theme at all.
 */

/*
 * One colour — an accent, a status hue, a badge — restored onto the composite.
 *
 * `own_lum` is the theme's own panel surface and `surface_lum` the composite the
 * panel is actually presenting. Channels are scaled together toward whichever
 * pole has room, so hue and saturation hold and only value moves: the same
 * property syn_contrast_fix() is built on, and the reason the wallpaper's orange
 * comes back as a lighter orange rather than as white.
 *
 * In-place is allowed (`in` and `out` may be the same array).
 */
void syn_glass_restore(const float in[3], float out[3],
                       double own_lum, double surface_lum, double target);

/*
 * …and the same for the ink LADDER, which is not one colour but a family of
 * them: a position between the surface and the ink, so every rung moves when the
 * surface does.
 *
 * Returns the smallest level that reaches the goal on `surface_lum`, for the
 * clamp set_ink() already applies — the same contract as syn_ink_floor(), with
 * the surface it must clear against separated from the two ends of the ladder.
 * `ref_level` is the rung the goal is taken from: the dimmest thing that counts
 * as text, so the promise is "the dimmest text is at least as readable as the
 * dimmest text has always been".
 *
 * 0 when nothing needs clamping, which is what an opaque panel always gets.
 */
double syn_ink_floor_glass(const float bg[3], const float ink[3],
                           double ref_level, double surface_lum, double target);

#endif /* SYNUI_CONTRAST_H */
