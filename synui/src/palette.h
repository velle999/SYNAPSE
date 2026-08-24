/*
 * palette.h — the small palette SYNAPSE Prism takes off the wallpaper.
 *
 * Deliberately its own header, with no synui.h behind it: the extractor is
 * pure — pixels in, colours out — and that is what makes it testable against
 * images built in the test rather than against a compositor. wallpaper.c is
 * the only caller in the compositor.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_PALETTE_H
#define SYN_PALETTE_H

#include <stdbool.h>

typedef struct {
    /* Whether there is a palette here to draw with at all. false = nothing was
     * measured; syn_palette_from_pixels() sets it false when the wallpaper
     * offered no usable hue (greyscale, near-black, or chromatic in so small a
     * patch that taking it would repaint the desktop to match a logo), and that
     * is NOT an error — it is the honest answer for a black-and-white
     * photograph, and the caller answers it with syn_palette_monochrome()
     * rather than with a colour the picture does not contain. */
    bool  ok;

    /* Every colour is sRGB 0..1, already pushed into a range an interface can
     * use and already corrected against the surface it will be drawn on. A
     * caller that corrects them again is correcting twice. */
    float accent[3];      /* the dominant hue — selections, focus, the caret  */
    float accent_dim[3];  /* the same hue, quieter — rules, unfocused rows    */
    float secondary[3];   /* a plainly different hue — the second state       */
    /*
     * ⚠ `accent` and `secondary` clear 4.5:1 against `surface_lum`.
     * `accent_dim` DELIBERATELY DOES NOT — it is a rule, a fill, an edge, and
     * its whole job is to be quieter than the accent. Drawing TEXT in it is a
     * bug, and it will measure around 1.9:1 on a dark panel if you try.
     */

    /* Whether `secondary` was measured or rotated off the accent. A
     * single-hue wallpaper has no second colour to find, and the rotation is a
     * reasonable stand-in — but a row that says where its colour came from
     * should not claim this one came from the picture. */
    bool  measured_secondary;

    /* true = these colours came from the ABSENCE of a hue rather than from one:
     * white and greys, from syn_palette_monochrome(). Never set by the
     * extractor. The desktop draws with them exactly as it draws with a
     * measured palette — the flag is here so a log line, a picker or a test can
     * tell "the picture is grey" from "the picture is teal", which `ok` alone
     * no longer says. */
    bool  monochrome;
} syn_palette_t;

/*
 * Extract from a 32-bit image buffer (native-endian ARGB32, i.e. B,G,R,A on
 * little-endian — the format cairo hands back, and what wallpaper.c already
 * assumes about the same buffers).
 *
 * `surface_lum` is the relative luminance of the surface these colours will be
 * drawn ON, and it is not optional: it is what the contrast corrector needs,
 * and without it a wallpaper's own yellow lands unreadable on a pale panel.
 *
 * Returns false and leaves `out` zeroed (ok = false) when there is nothing
 * worth taking. Deterministic: the same image always yields the same palette,
 * which matters because this runs at every login.
 */
bool syn_palette_from_pixels(const unsigned char *data, int w, int h,
                             int stride, double surface_lum,
                             syn_palette_t *out);

/*
 * The palette for a picture that has no colour in it: white and greys on a dark
 * surface, deep greys on a pale one, in the same three roles.
 *
 * ⚠ THIS IS WHAT "NO USABLE HUE" MEANS TO A DESKTOP, and it used to mean the
 * theme's own accent — so a black-and-white photograph, a near-black wallpaper
 * and a Prism desktop that had never been given a picture all came up the house
 * cyan, a colour from nowhere near the screen. Monochrome is the answer that
 * still follows the wallpaper: a grey picture gets a grey desktop.
 *
 * `surface_lum` is the surface these will be drawn on, exactly as above, and it
 * is what decides the direction — white is the accent on a dark panel and is
 * invisible on a pale one.
 *
 * Fills `out` with ok = true and monochrome = true. It measures nothing and
 * cannot fail: the answer is a function of the surface alone.
 */
void syn_palette_monochrome(double surface_lum, syn_palette_t *out);

#endif /* SYN_PALETTE_H */
