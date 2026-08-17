/*
 * palette_test.c — the palette SYNAPSE Prism takes off the wallpaper.
 *
 * Images are BUILT here rather than shipped, for the reason every other
 * data-driven test in this tree gives: a checked-in PNG is a fixture nobody
 * can read the intent of, and "the accent came out teal" is only a useful
 * assertion when the file next to it says the image was 60% teal.
 *
 * What is worth testing is not "it returns a colour" — anything does that. It
 * is the four ways this can be confidently wrong:
 *
 *   1. Electing a colour off a wallpaper that has none (greyscale).
 *   2. Electing the majority DARKNESS rather than the minority hue, which is
 *      what a straight average does on every photograph.
 *   3. Handing back a colour that cannot be read on the surface it is for.
 *   4. Answering differently on the same image twice — this runs at login, and
 *      a desktop whose accent drifts is worse than one that never had the
 *      feature.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "palette.h"
#include "contrast.h"

#define W 200
#define H 200

/* A canvas in the format the extractor reads: native-endian ARGB32, so
 * B,G,R,A on little-endian. Same assumption wallpaper.c makes. */
static unsigned char *canvas(void)
{
    unsigned char *px = calloc((size_t)W * H, 4);
    assert(px);
    return px;
}

static void fill_rows(unsigned char *px, int y0, int y1,
                      int r, int g, int b)
{
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
            unsigned char *p = px + ((size_t)y * W + x) * 4;
            p[0] = (unsigned char)b;
            p[1] = (unsigned char)g;
            p[2] = (unsigned char)r;
            p[3] = 255;
        }
}

/* Which of r/g/b is largest — the coarsest possible statement of "what colour
 * is this", and the right granularity for an assertion: pinning exact channel
 * values would make every future tweak to the UI band a test failure without
 * telling anyone whether the answer got better or worse. */
static char dominant(const float c[3])
{
    if (c[0] >= c[1] && c[0] >= c[2]) return 'r';
    if (c[1] >= c[0] && c[1] >= c[2]) return 'g';
    return 'b';
}

static double lum(const float c[3])
{
    return syn_rel_luminance(c[0], c[1], c[2]);
}

/* ── 1. A greyscale wallpaper has no colour to give ────────── */

static void test_greyscale_refuses(void)
{
    unsigned char *px = canvas();
    for (int y = 0; y < H; y++)
        fill_rows(px, y, y + 1, y, y, y);      /* a black-to-grey ramp */

    syn_palette_t p;
    bool got = syn_palette_from_pixels(px, W, H, W * 4, 0.05, &p);

    if (got || p.ok) {
        printf("    a greyscale wallpaper produced an accent "
               "(%.2f %.2f %.2f)\n", p.accent[0], p.accent[1], p.accent[2]);
        printf("    — inventing one is worse than the theme's own.\n");
        assert(0);
    }
    free(px);
    printf("  greyscale refuses, honestly ........ ok\n");
}

/* ── 2. A minority hue beats a majority of darkness ────────── */
/*
 * The failure a straight average always has. This image is 85% near-black and
 * 15% teal; the mean pixel is a very dark slightly-teal grey, which is a
 * colour nothing can be drawn in. The answer has to be the teal.
 */

static void test_minority_hue_wins(void)
{
    unsigned char *px = canvas();
    fill_rows(px, 0, 170, 8, 8, 10);            /* night sky */
    fill_rows(px, 170, H, 0, 190, 180);         /* a teal band */

    syn_palette_t p;
    assert(syn_palette_from_pixels(px, W, H, W * 4, 0.05, &p));
    assert(p.ok);

    /* Teal is green-dominant with blue close behind; what must NOT come back is
     * red, and what must not come back is a colour this dark. */
    if (dominant(p.accent) == 'r') {
        printf("    accent came back red-dominant (%.2f %.2f %.2f) from a "
               "black-and-teal image\n", p.accent[0], p.accent[1], p.accent[2]);
        assert(0);
    }
    if (lum(p.accent) < 0.08) {
        printf("    accent luminance %.3f — that is the night sky, not the "
               "teal band\n", lum(p.accent));
        assert(0);
    }
    free(px);
    printf("  a minority hue beats the darkness .. ok\n");
}

/* ── 3. The secondary is a different colour ────────────────── */

static void test_secondary_differs(void)
{
    unsigned char *px = canvas();
    fill_rows(px, 0, 120, 20, 90, 220);         /* blue, the majority */
    fill_rows(px, 120, H, 230, 130, 20);        /* orange, the minority */

    syn_palette_t p;
    assert(syn_palette_from_pixels(px, W, H, W * 4, 0.05, &p));
    assert(p.ok);
    assert(p.measured_secondary);

    if (dominant(p.accent) == dominant(p.secondary)) {
        printf("    accent (%.2f %.2f %.2f) and secondary (%.2f %.2f %.2f) "
               "are the same colour\n",
               p.accent[0], p.accent[1], p.accent[2],
               p.secondary[0], p.secondary[1], p.secondary[2]);
        printf("    — the second one exists to tell two states apart.\n");
        assert(0);
    }
    free(px);
    printf("  the secondary is a real second ..... ok\n");
}

/* ── 4. One hue still yields two colours, and says so ──────── */

static void test_single_hue_rotates(void)
{
    unsigned char *px = canvas();
    for (int y = 0; y < H; y++)                  /* a blue gradient, one hue */
        fill_rows(px, y, y + 1, 10, 40 + y / 3, 150 + y / 4);

    syn_palette_t p;
    assert(syn_palette_from_pixels(px, W, H, W * 4, 0.05, &p));
    assert(p.ok);

    /* A stand-in is fine. Claiming it came off the picture is not. */
    assert(!p.measured_secondary);
    printf("  one hue rotates, and admits it ..... ok\n");
}

/* ── 5. Legible on the surface it is for ───────────────────── */
/*
 * The whole reason the corrector is the last step. A yellow wallpaper on a
 * near-white panel is the case that produced 1.4:1 text before contrast.c
 * existed — see the pale-theme work. The accent has to come back usable on
 * BOTH kinds of surface, from the same image.
 */

static void test_corrected_for_the_surface(void)
{
    unsigned char *px = canvas();
    fill_rows(px, 0, H, 245, 225, 40);           /* bright yellow */

    syn_palette_t dark, light;
    assert(syn_palette_from_pixels(px, W, H, W * 4, 0.04, &dark));
    assert(syn_palette_from_pixels(px, W, H, W * 4, 0.92, &light));

    double c_light = syn_contrast(light.accent[0], light.accent[1],
                                  light.accent[2], 0.92);
    if (c_light < 3.0) {
        printf("    yellow on a pale panel came back at %.2f:1\n", c_light);
        printf("    — that is the accent nobody can see.\n");
        assert(0);
    }
    /* And the dark case is left alone: correcting a colour that already passes
     * would drag every dark theme's accent toward the surface for nothing. */
    assert(lum(dark.accent) > lum(light.accent));
    printf("  corrected for the surface .......... ok\n");
}

/* ── 6. The same image, the same answer ────────────────────── */

static void test_deterministic(void)
{
    unsigned char *px = canvas();
    fill_rows(px, 0, 80,  200, 40, 60);
    fill_rows(px, 80, 140, 40, 60, 200);
    fill_rows(px, 140, H, 30, 30, 30);

    syn_palette_t a, b;
    assert(syn_palette_from_pixels(px, W, H, W * 4, 0.05, &a));
    assert(syn_palette_from_pixels(px, W, H, W * 4, 0.05, &b));

    /* Bit-identical, not close. A desktop whose accent drifts between logins
     * is worse than one that never had the feature, and "close enough" is how
     * a seeded clusterer passes this and still drifts in the field. */
    assert(memcmp(&a, &b, sizeof(a)) == 0);
    free(px);
    printf("  the same image, the same answer .... ok\n");
}

/* ── 7. Nonsense in is not a crash ─────────────────────────── */

static void test_bad_input(void)
{
    syn_palette_t p;
    assert(!syn_palette_from_pixels(NULL, W, H, W * 4, 0.5, &p));
    unsigned char *px = canvas();
    assert(!syn_palette_from_pixels(px, 0, H, W * 4, 0.5, &p));
    assert(!syn_palette_from_pixels(px, W, H, 4, 0.5, &p));   /* stride < row */
    assert(!syn_palette_from_pixels(px, W, H, W * 4, 0.5, NULL));
    free(px);
    printf("  nonsense in, no crash out .......... ok\n");
}

/* ── 8. A mark on black is a colour ────────────────────────── */
/*
 * ⚠ THE CASE THAT WAS WRONG. The first cut required 6% of surviving pixels to
 * carry chroma, on the reasoning that "a logo in the corner should not repaint
 * the desktop" — and SynapseOS's own default wallpaper is a dendrite mark on
 * black at 2.8%, so the house theme fell back to its fallback on the house
 * wallpaper. Asked what colour that picture is, a person answers instantly.
 *
 * What separates it from noise is CONCENTRATION, which the next test covers.
 */

static void test_a_mark_on_black(void)
{
    unsigned char *px = canvas();
    fill_rows(px, 0, H, 6, 6, 8);               /* black, near enough */
    /* ~3% of the image, one strong hue — a logo. */
    fill_rows(px, 97, 103, 40, 110, 250);

    syn_palette_t p;
    if (!syn_palette_from_pixels(px, W, H, W * 4, 0.011, &p) || !p.ok) {
        printf("    a bright mark on black came back as 'no usable hue'\n");
        printf("    — that is the shipped wallpaper, and it has a colour.\n");
        assert(0);
    }
    assert(dominant(p.accent) == 'b');
    free(px);
    printf("  a mark on black is a colour ........ ok\n");
}

/* ── 9. …but smeared chroma is still noise ─────────────────── */
/*
 * The other half of the same question, and the reason the floor alone could not
 * be lowered. This image carries as much chromatic weight as the logo above,
 * spread evenly round the wheel — which is what compression tint and sensor
 * noise look like. There is no colour to name here.
 */

static void test_smeared_chroma_refused(void)
{
    unsigned char *px = canvas();
    for (int y = 0; y < H; y++) {
        /* A different weak hue every row: the same total chroma, no hue. */
        int phase = (y * 360) / H;
        int r = 120 + (int)(60 * cos(phase * 3.14159 / 180.0));
        int g = 120 + (int)(60 * cos((phase - 120) * 3.14159 / 180.0));
        int b = 120 + (int)(60 * cos((phase - 240) * 3.14159 / 180.0));
        fill_rows(px, y, y + 1, r, g, b);
    }

    syn_palette_t p;
    bool got = syn_palette_from_pixels(px, W, H, W * 4, 0.011, &p);
    if (got && p.ok) {
        printf("    chroma smeared evenly round the wheel elected "
               "#%02X%02X%02X\n",
               (int)(p.accent[0] * 255), (int)(p.accent[1] * 255),
               (int)(p.accent[2] * 255));
        printf("    — that is what noise looks like; there is no colour here.\n");
        assert(0);
    }
    free(px);
    printf("  smeared chroma is still noise ...... ok\n");
}

/* ── 10. Legible on a DARK panel, which is the default one ── */
/*
 * ⚠ THE SECOND THING THAT WAS WRONG, and it is not symmetrical with test 5.
 * syn_contrast_fix() only darkens — a deliberate no-op on a dark surface,
 * because every theme's own accent was chosen to work there. A MEASURED accent
 * has no such guarantee: a deep-blue wallpaper handed back #3A24B7, which is
 * 1.7:1 on Prism's near-black panel. Blue is the case that exposes it, because
 * luminance weights blue at 0.07 and a "bright" blue is still dark.
 */

static void test_dark_panel_is_legible(void)
{
    unsigned char *px = canvas();
    fill_rows(px, 0, H, 42, 26, 150);            /* a deep blue */

    const double panel = 0.011;                  /* Prism's #191C23 */
    syn_palette_t p;
    assert(syn_palette_from_pixels(px, W, H, W * 4, panel, &p));
    assert(p.ok);

    double c = syn_contrast(p.accent[0], p.accent[1], p.accent[2], panel);
    if (c < 4.4) {
        printf("    a deep-blue wallpaper gave an accent at %.2f:1 on the "
               "dark panel\n", c);
        printf("    — nothing downstream brightens it; this is the last "
               "chance.\n");
        assert(0);
    }
    free(px);
    printf("  legible on a dark panel too ........ ok\n");
}

/* ── 11. A pastel is a colour, and pink is the pastel ─────── */
/*
 * ⚠ THE THIRD THING THAT WAS WRONG, and it hid behind a rule that mentions no
 * hue at all. Pixels brighter than v = 0.96 with less than 0.45 saturation were
 * dropped as blown highlights — but S_MIN already rejects anything actually
 * clipping toward white, so the only pixels that rule ever removed were bright
 * ones chromatic enough to have cleared S_MIN. That band is pastel.
 *
 * It shows up as "pink does not work" and nothing else, because pink is the one
 * colour a person names whose ordinary form IS a pastel: "blue" means the vivid
 * one, "pink" already means a pale red. Vivid pink is called magenta or fuchsia
 * and always worked, which is what made the fault look hue-specific.
 *
 * #F6BBD4 is the Sanrio pink that produced it — v = 0.96, s = 0.24, squarely in
 * the band — on a wallpaper that is nearly all of it.
 */

static void test_a_pastel_is_a_colour(void)
{
    unsigned char *px = canvas();
    fill_rows(px, 0, H, 246, 187, 212);          /* pastel pink, wall to wall */

    syn_palette_t p;
    if (!syn_palette_from_pixels(px, W, H, W * 4, 0.011, &p) || !p.ok) {
        printf("    a wallpaper that is entirely pastel pink came back as "
               "'no usable hue'\n");
        printf("    — asked what colour that picture is, a person answers "
               "instantly.\n");
        assert(0);
    }
    /* Pink is red-dominant with blue second and green last. What must not come
     * back is a colour that has lost the pink: the ordering IS the hue. */
    if (!(p.accent[0] >= p.accent[2] && p.accent[2] > p.accent[1])) {
        printf("    pastel pink elected #%02X%02X%02X — not a pink\n",
               (int)(p.accent[0] * 255), (int)(p.accent[1] * 255),
               (int)(p.accent[2] * 255));
        assert(0);
    }
    free(px);
    printf("  a pastel is a colour ............... ok\n");
}

/* ── 12. …and brightness alone never disqualifies one ─────── */
/*
 * The guard that keeps the rule from coming back. Every hue on the wheel, at
 * the brightness and saturation a pastel actually has, has to be nameable —
 * this is a property of the extractor, not a fact about pink, and stating it
 * for one hue would let the next threshold re-break the other eleven.
 *
 * The pairing with test 9 is the real assertion: this image is a pale FLAT
 * field and must be accepted, that one is pale SMEARED chroma and must be
 * refused. Concentration is what separates them, which is the same answer the
 * logo-versus-noise question got.
 */

static void test_pastels_all_round_the_wheel(void)
{
    for (int deg = 0; deg < 360; deg += 30) {
        /* HSV(deg, 0.24, 0.97) by hand — pastel, and above any old V_MAX. */
        double c = 0.97 * 0.24, m = 0.97 - c;
        double x = c * (1.0 - fabs(fmod(deg / 60.0, 2.0) - 1.0));
        double r, g, b;
        if      (deg <  60) { r = c; g = x; b = 0; }
        else if (deg < 120) { r = x; g = c; b = 0; }
        else if (deg < 180) { r = 0; g = c; b = x; }
        else if (deg < 240) { r = 0; g = x; b = c; }
        else if (deg < 300) { r = x; g = 0; b = c; }
        else                { r = c; g = 0; b = x; }

        unsigned char *px = canvas();
        fill_rows(px, 0, H, (int)((r + m) * 255 + 0.5),
                            (int)((g + m) * 255 + 0.5),
                            (int)((b + m) * 255 + 0.5));

        syn_palette_t p;
        if (!syn_palette_from_pixels(px, W, H, W * 4, 0.011, &p) || !p.ok) {
            printf("    a wallpaper that is entirely pastel hue %d came back "
                   "as 'no usable hue'\n", deg);
            printf("    — a bright colour is still a colour; only a "
                   "COLOURLESS one may be refused.\n");
            assert(0);
        }
        free(px);
    }
    printf("  every pastel hue is nameable ....... ok\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("palette_test\n");
    test_greyscale_refuses();
    test_minority_hue_wins();
    test_secondary_differs();
    test_single_hue_rotates();
    test_corrected_for_the_surface();
    test_deterministic();
    test_bad_input();
    test_a_mark_on_black();
    test_smeared_chroma_refused();
    test_dark_panel_is_legible();
    test_a_pastel_is_a_colour();
    test_pastels_all_round_the_wheel();
    printf("palette_test: all ok\n");
    return 0;
}
