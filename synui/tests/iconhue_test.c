/*
 * iconhue_test.c — the recolour that makes our own app icons follow the theme.
 *
 * Images are BUILT here rather than shipped, for the same reason palette_test.c
 * gives: a checked-in PNG is a fixture nobody can read the intent of, and "the
 * knobs stayed visible" is only a useful assertion when the file next to it
 * says what colour the knobs were drawn.
 *
 * What is worth testing is not "it changes the colours" — a memset does that.
 * It is the six ways this can be confidently wrong:
 *
 *   1. Repainting somebody else's icon. The content half of the gate looks
 *      sufficient right up until you measure a stock install: nordvpn-tray-blue
 *      is 100% brand-hue and mpv is 71%.
 *   2. Repainting nothing, because the gate is too strict to match our own.
 *   3. Moving the neutrals — the whites and the near-black bodies — which reads
 *      as a colour cast over the icon rather than a themed icon.
 *   4. Losing the teal detail on the themes whose accent is ITSELF teal. That
 *      is SYNAPSE, the default, so getting this wrong ships broken by default.
 *   5. Breaking the premultiply. Cairo's ARGB32 is premultiplied and the hue
 *      maths is not; a channel that ends up above its own alpha is a corrupt
 *      surface, and it shows as fringing on every antialiased edge.
 *   6. Drifting on repeat application, which is the whole reason icons.c keeps
 *      a pristine base instead of re-tinting what it drew last time.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iconhue.h"

#define W 32
#define H 32

/* The house palette, as the SVGs spell it. */
#define BRAND   0xa78bfau     /* the violet everything of ours is drawn from */
#define SHADE   0x7c5cd6u     /* the same hue, darker                        */
#define TEAL    0x4ec9b0u     /* knobs, pad bars, disk LED                   */
#define PAPER   0xf1ecffu     /* the near-white highlight                    */
#define BODY    0x1b1030u     /* the near-black icon body                    */
#define RESOLVE 0x38bdf8u     /* DaVinci's sky blue — not ours to touch      */

/* Accents, straight out of theme.c's presets. */
static const float ACC_SYNAPSE[3] = { 0.00f, 0.85f, 0.75f };   /* teal!      */
static const float ACC_GRUVBOX[3] = { 0.996f, 0.502f, 0.098f };
static const float ACC_DRACULA[3] = { 1.000f, 0.475f, 0.776f };
static const float ACC_PRISM[3]   = { 0.000f, 0.839f, 0.898f }; /* teal too  */
static const float ACC_GREY[3]    = { 0.55f, 0.55f, 0.55f };

/* A canvas in the format iconhue reads: native-endian ARGB32, premultiplied,
 * so B,G,R,A on little-endian. Same assumption icons.c makes. */
static unsigned char *canvas(void)
{
    unsigned char *px = calloc((size_t)W * H, 4);
    assert(px);
    return px;
}

static void put(unsigned char *px, int x, int y, unsigned hex, unsigned a)
{
    unsigned r = (hex >> 16) & 0xff, g = (hex >> 8) & 0xff, b = hex & 0xff;
    /* premultiply, the way cairo stores it */
    r = r * a / 255; g = g * a / 255; b = b * a / 255;
    uint32_t *p = (uint32_t *)(px + ((size_t)y * W + x) * 4);
    *p = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
}

static unsigned get(const unsigned char *px, int x, int y)
{
    const uint32_t *p = (const uint32_t *)(px + ((size_t)y * W + x) * 4);
    return *p;
}

/* Un-premultiplied colour at a pixel, for comparing against a hex. */
static unsigned straight(const unsigned char *px, int x, int y)
{
    uint32_t v = get(px, x, y);
    unsigned a = (v >> 24) & 0xff;
    if (a == 0) return 0;
    unsigned r = ((v >> 16) & 0xff) * 255 / a;
    unsigned g = ((v >>  8) & 0xff) * 255 / a;
    unsigned b = ( v        & 0xff) * 255 / a;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}

/* Fill the whole canvas with one colour, fully opaque. */
static unsigned char *flat(unsigned hex)
{
    unsigned char *px = canvas();
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            put(px, x, y, hex, 255);
    return px;
}

/* One of our icons, in miniature: a violet body, a lighter violet glyph, a
 * white highlight and one teal detail. */
static unsigned char *house_icon(void)
{
    unsigned char *px = flat(BODY);
    for (int y = 4; y < 26; y++)
        for (int x = 4; x < 26; x++)
            put(px, x, y, BRAND, 255);
    for (int y = 10; y < 16; y++)
        for (int x = 10; x < 22; x++)
            put(px, x, y, SHADE, 255);
    for (int y = 6; y < 9; y++)
        for (int x = 6; x < 12; x++)
            put(px, x, y, PAPER, 255);
    for (int y = 19; y < 23; y++)          /* the knob / the LED */
        for (int x = 19; x < 23; x++)
            put(px, x, y, TEAL, 255);
    return px;
}

static double hue_of(unsigned hex)
{
    double r = ((hex >> 16) & 0xff) / 255.0;
    double g = ((hex >>  8) & 0xff) / 255.0;
    double b = ( hex        & 0xff) / 255.0;
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
    if (d <= 0.0) return -1.0;
    double h;
    if (mx == r)      h = fmod((g - b) / d, 6.0);
    else if (mx == g) h = (b - r) / d + 2.0;
    else              h = (r - g) / d + 4.0;
    h *= 60.0;
    if (h < 0) h += 360.0;
    return h;
}

static double hue_gap(double a, double b)
{
    double d = fabs(a - b);
    if (d > 180.0) d = 360.0 - d;
    return d;
}

/* CIE76 between two packed sRGB colours — the only honest way to say "these
 * two are still telling apart", which is the whole claim about the teal. */
static double delta_e(unsigned x, unsigned y)
{
    double lab[2][3];
    unsigned c[2] = { x, y };
    for (int i = 0; i < 2; i++) {
        double v[3];
        for (int k = 0; k < 3; k++) {
            double s = ((c[i] >> (16 - 8 * k)) & 0xff) / 255.0;
            v[k] = (s <= 0.04045) ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
        }
        double X = (0.4124 * v[0] + 0.3576 * v[1] + 0.1805 * v[2]) / 0.95047;
        double Y = (0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]);
        double Z = (0.0193 * v[0] + 0.1192 * v[1] + 0.9505 * v[2]) / 1.08883;
        double f[3], t[3] = { X, Y, Z };
        for (int k = 0; k < 3; k++)
            f[k] = (t[k] > 216.0 / 24389.0) ? cbrt(t[k])
                                            : (841.0 / 108.0) * t[k] + 4.0 / 29.0;
        lab[i][0] = 116.0 * f[1] - 16.0;
        lab[i][1] = 500.0 * (f[0] - f[1]);
        lab[i][2] = 200.0 * (f[1] - f[2]);
    }
    double d = 0;
    for (int k = 0; k < 3; k++) d += pow(lab[0][k] - lab[1][k], 2);
    return sqrt(d);
}

/* ── 1. the gate turns away everything that is not ours ──── */

static void test_gate_rejects_other_apps(void)
{
    /* The exact trap the double gate exists for: an icon drawn ENTIRELY in our
     * brand violet, belonging to somebody else. On a stock install this is not
     * hypothetical — nordvpn-tray-blue measures 100%. */
    unsigned char *px = flat(BRAND);
    assert(!syn_iconhue_wants("nordvpn-tray-blue", px, W, H, W * 4));
    assert(!syn_iconhue_wants("mpv", px, W, H, W * 4));
    assert(!syn_iconhue_wants("falkon", px, W, H, W * 4));
    free(px);
}

static void test_gate_rejects_our_name_but_not_our_palette(void)
{
    /* syn-resolve-gui is ours by name and DaVinci's by design: the clapperboard
     * is Resolve's branding and retinting it would be us vandalising a logo.
     * The same guard is what keeps syncthing and synfig out. */
    unsigned char *px = flat(RESOLVE);
    assert(!syn_iconhue_wants("syn-resolve-gui", px, W, H, W * 4));
    assert(!syn_iconhue_wants("syncthing", px, W, H, W * 4));
    free(px);
}

static void test_gate_accepts_ours(void)
{
    unsigned char *px = house_icon();
    assert(syn_iconhue_wants("syn-settings", px, W, H, W * 4));
    assert(syn_iconhue_wants("synfiles", px, W, H, W * 4));
    assert(syn_iconhue_wants("syntty", px, W, H, W * 4));
    free(px);
}

static void test_gate_survives_bad_input(void)
{
    unsigned char *px = house_icon();
    assert(!syn_iconhue_wants(NULL, px, W, H, W * 4));
    assert(!syn_iconhue_wants("syn-settings", px, 0, 0, W * 4));
    /* No pixels is the "could this ever qualify" question, and our name can. */
    assert(syn_iconhue_wants("syn-settings", NULL, W, H, W * 4));
    assert(!syn_iconhue_wants("firefox", NULL, W, H, W * 4));
    free(px);
}

/* ── 2. the brand family lands on the accent ─────────────── */

static void test_brand_takes_the_accent_hue(void)
{
    unsigned char *px = house_icon();
    syn_iconhue_apply(px, W, H, W * 4, ACC_GRUVBOX);

    double want = hue_of(0xfe8019);           /* Gruvbox orange */
    assert(hue_gap(hue_of(straight(px, 5, 16)), want) < 3.0);   /* the body glyph */
    assert(hue_gap(hue_of(straight(px, 12, 12)), want) < 3.0);  /* the darker shade */
    free(px);
}

static void test_lightness_structure_survives(void)
{
    /* The reason only the hue moves: an icon whose internal light/dark ordering
     * is preserved still reads as the same icon. If the shade ever came out
     * lighter than the glyph it sits on, the drawing would inverted. */
    unsigned char *px = house_icon();
    syn_iconhue_apply(px, W, H, W * 4, ACC_DRACULA);

    unsigned glyph = straight(px, 5, 16), shade = straight(px, 12, 12);
    double lg = 0.2126 * ((glyph >> 16) & 0xff) + 0.7152 * ((glyph >> 8) & 0xff)
              + 0.0722 * (glyph & 0xff);
    double ls = 0.2126 * ((shade >> 16) & 0xff) + 0.7152 * ((shade >> 8) & 0xff)
              + 0.0722 * (shade & 0xff);
    assert(lg > ls);
    free(px);
}

/* ── 3. neutrals are left where they are ─────────────────── */

static void test_neutrals_are_not_tinted(void)
{
    /* A pure white and a pure grey carry no hue worth moving, and moving them
     * puts a colour wash over the icon instead of theming it. */
    unsigned char *px = canvas();
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            put(px, x, y, BRAND, 255);
    put(px, 0, 0, 0xffffff, 255);
    put(px, 1, 0, 0x808080, 255);
    put(px, 2, 0, 0x000000, 255);

    syn_iconhue_apply(px, W, H, W * 4, ACC_GRUVBOX);
    assert(straight(px, 0, 0) == 0xffffff);
    assert(straight(px, 1, 0) == 0x808080);
    assert(straight(px, 2, 0) == 0x000000);
    free(px);
}

static void test_a_foreign_hue_is_left_alone(void)
{
    /* Resolve's sky blue sits 57 deg off the brand hue. That margin is the
     * whole reason the window can be as wide as 40 deg. */
    unsigned char *px = house_icon();
    put(px, 30, 30, RESOLVE, 255);
    syn_iconhue_apply(px, W, H, W * 4, ACC_GRUVBOX);
    assert(hue_gap(hue_of(straight(px, 30, 30)), hue_of(RESOLVE)) < 2.0);
    free(px);
}

/* ── 4. the teal detail keeps carrying its meaning ───────── */

static void test_teal_is_untouched_when_it_can_be(void)
{
    /* On 11 of the 14 themes the accent is nowhere near the teal, and the
     * detail should come out byte-identical to the way it was drawn. */
    unsigned char *px = house_icon();
    unsigned before = straight(px, 20, 20);
    syn_iconhue_apply(px, W, H, W * 4, ACC_GRUVBOX);
    assert(straight(px, 20, 20) == before);

    unsigned char *px2 = house_icon();
    syn_iconhue_apply(px2, W, H, W * 4, ACC_DRACULA);
    assert(straight(px2, 20, 20) == before);
    free(px); free(px2);
}

static void test_teal_gives_way_to_a_teal_accent(void)
{
    /* SYNAPSE is the DEFAULT theme and its accent is teal. Left as drawn, the
     * knob/LED lands 19.5 dE from the glyph it sits on and disappears. */
    const float *accents[2] = { ACC_SYNAPSE, ACC_PRISM };
    for (int i = 0; i < 2; i++) {
        unsigned char *px = house_icon();
        syn_iconhue_apply(px, W, H, W * 4, accents[i]);

        unsigned detail = straight(px, 20, 20);
        unsigned glyph  = straight(px, 5, 16);
        assert(detail != TEAL);                    /* it had to move */
        assert(delta_e(detail, glyph) > 25.0);     /* and far enough */
        free(px);
    }
}

static void test_the_detail_moves_no_further_than_it_must(void)
{
    /* It gives way, it does not run: a detail that swung to the far side of the
     * wheel would stop reading as the same mark from theme to theme. */
    unsigned char *px = house_icon();
    syn_iconhue_apply(px, W, H, W * 4, ACC_SYNAPSE);
    assert(hue_gap(hue_of(straight(px, 20, 20)), hue_of(TEAL)) < 45.0);
    free(px);
}

/* ── 5. the premultiply survives the round trip ──────────── */

static void test_premultiply_is_preserved(void)
{
    unsigned char *px = canvas();
    /* Every alpha, including the near-transparent edges where an un-premultiply
     * has almost no precision left to work with. */
    for (unsigned a = 1; a <= 255; a++)
        put(px, (int)(a % W), (int)(a / W), BRAND, a);

    syn_iconhue_apply(px, W, H, W * 4, ACC_GRUVBOX);

    for (unsigned a = 1; a <= 255; a++) {
        uint32_t v = get(px, (int)(a % W), (int)(a / W));
        unsigned al = (v >> 24) & 0xff;
        assert(al == a);                       /* alpha is never touched */
        assert(((v >> 16) & 0xff) <= al);      /* and no channel exceeds it */
        assert(((v >>  8) & 0xff) <= al);
        assert(( v        & 0xff) <= al);
    }
    free(px);
}

static void test_fully_transparent_stays_transparent(void)
{
    unsigned char *px = house_icon();
    put(px, 31, 31, BRAND, 0);
    syn_iconhue_apply(px, W, H, W * 4, ACC_GRUVBOX);
    assert(get(px, 31, 31) == 0);
    free(px);
}

/* ── 6. the reasons icons.c keeps a pristine base ────────── */

static void test_reapplying_drifts(void)
{
    /* Not a bug being tolerated — a contract being pinned. Hue assignment is
     * not idempotent ACROSS accents, so tinting what you drew last time walks
     * the icon further from itself on every theme switch. This is exactly why
     * syn_icon_entry_t carries icon_base, and if this assertion ever starts
     * failing, that field has become dead weight and should go. */
    unsigned char *once = house_icon();
    syn_iconhue_apply(once, W, H, W * 4, ACC_GRUVBOX);

    unsigned char *twice = house_icon();
    syn_iconhue_apply(twice, W, H, W * 4, ACC_DRACULA);
    syn_iconhue_apply(twice, W, H, W * 4, ACC_GRUVBOX);

    assert(memcmp(once, twice, (size_t)W * H * 4) != 0);
    free(once); free(twice);
}

static void test_same_accent_is_stable(void)
{
    /* The other half: from the SAME starting pixels, the same accent must give
     * the same answer every time, or the dock flickers between repaints. */
    unsigned char *a = house_icon(), *b = house_icon();
    syn_iconhue_apply(a, W, H, W * 4, ACC_DRACULA);
    syn_iconhue_apply(b, W, H, W * 4, ACC_DRACULA);
    assert(memcmp(a, b, (size_t)W * H * 4) == 0);
    free(a); free(b);
}

static void test_greyscale_accent_is_a_no_op(void)
{
    /* A theme with no hue to give wants the icons as drawn, not the whole
     * family rotated to red because 0 is where a greyscale hue reads. */
    unsigned char *px = house_icon(), *ref = house_icon();
    syn_iconhue_apply(px, W, H, W * 4, ACC_GREY);
    assert(memcmp(px, ref, (size_t)W * H * 4) == 0);
    free(px); free(ref);
}

static void test_apply_survives_bad_input(void)
{
    unsigned char *px = house_icon();
    syn_iconhue_apply(NULL, W, H, W * 4, ACC_GRUVBOX);
    syn_iconhue_apply(px, W, H, W * 4, NULL);
    syn_iconhue_apply(px, 0, 0, W * 4, ACC_GRUVBOX);
    free(px);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("iconhue_test\n");
    test_gate_rejects_other_apps();
    test_gate_rejects_our_name_but_not_our_palette();
    test_gate_accepts_ours();
    test_gate_survives_bad_input();
    test_brand_takes_the_accent_hue();
    test_lightness_structure_survives();
    test_neutrals_are_not_tinted();
    test_a_foreign_hue_is_left_alone();
    test_teal_is_untouched_when_it_can_be();
    test_teal_gives_way_to_a_teal_accent();
    test_the_detail_moves_no_further_than_it_must();
    test_premultiply_is_preserved();
    test_fully_transparent_stays_transparent();
    test_reapplying_drifts();
    test_same_accent_is_stable();
    test_greyscale_accent_is_a_no_op();
    test_apply_survives_bad_input();
    printf("iconhue_test: all ok\n");
    return 0;
}
