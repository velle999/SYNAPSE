/*
 * iconhue_test.c — the recolour that makes our own app icons follow the theme.
 *
 * Images are BUILT here rather than shipped, for the same reason palette_test.c
 * gives: a checked-in PNG is a fixture nobody can read the intent of, and "the
 * knobs stayed visible" is only a useful assertion when the file next to it
 * says what colour the knobs were drawn.
 *
 * What is worth testing is not "it changes the colours" — a memset does that.
 * It is the nine ways this can be confidently wrong:
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
 *   7. Landing on the accent's hue at the icon's own HSL lightness, which is
 *      not the icon's own BRIGHTNESS: green carries five times the luminance of
 *      blue at the same L, so the violet came out 30 points of CIE L* brighter
 *      on a yellow-green accent than it was drawn. That is what 415 shipped,
 *      every assertion below it passed, and the icons were highlighters on one
 *      theme and correct on nine.
 *   8. Reading a COLOURLESS accent as no accent. It is the answer for a
 *      wallpaper with no colour in it, the whole rest of the desktop goes white
 *      and grey with it, and the two ways to get this wrong are opposites: leave
 *      the icons as drawn and the dock is nine violet tiles on a grey desktop,
 *      or "follow the accent" and a grey reads as h = 0 and the family turns RED.
 *   9. Losing the teal detail the OTHER way. It cannot step off a colliding
 *      accent in monochrome — there is no hue to step around — and desaturated
 *      where it was drawn it is within ten CIE L* of the plate it sits on. It
 *      steps in lightness instead, and that step has to land on the detail and
 *      not on every dark green the hue window happens to admit.
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
/* One pixel of synstudio's green aperture blade where its antialias runs into
 * the dark opening. It sits INSIDE the teal's hue window (173.6 deg against
 * the teal's 166.4) with none of the teal's chroma, and it is the reason the
 * monochrome lift is weighted rather than flat. */
#define GREENRAMP 0x324e4bu

/* Accents, straight out of theme.c's presets. */
static const float ACC_SYNAPSE[3] = { 0.00f, 0.85f, 0.75f };   /* teal!      */
static const float ACC_GRUVBOX[3] = { 0.996f, 0.502f, 0.098f };
static const float ACC_DRACULA[3] = { 1.000f, 0.475f, 0.776f };
static const float ACC_PRISM[3]   = { 0.000f, 0.839f, 0.898f }; /* teal too  */
static const float ACC_NORD[3]    = { 0.533f, 0.753f, 0.816f }; /* frost      */
static const float ACC_OLIVE[3]   = { 0.671f, 0.722f, 0.396f }; /* #ABB865    */
static const float ACC_GREY[3]    = { 0.55f, 0.55f, 0.55f };

/* The two the monochrome palette actually publishes: white on a dark panel,
 * #333333 on a pale one — syn_palette_monochrome(), palette.c. */
static const float ACC_MONO[3]      = { 1.00f, 1.00f, 1.00f };
static const float ACC_MONO_PALE[3] = { 0.20f, 0.20f, 0.20f };

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

/* CIELAB for a packed sRGB colour. Deliberately NOT the OKLab the transform
 * itself works in: a test that measures a thing in the same space the code
 * computed it in can only ever confirm the arithmetic. CIELAB is the older,
 * independent answer to "how far apart do these two look", and L* is the
 * independent answer to "how bright is this" — which is the whole question the
 * recolour turns on. */
static void lab_of(unsigned c, double out[3])
{
    double v[3];
    for (int k = 0; k < 3; k++) {
        double s = ((c >> (16 - 8 * k)) & 0xff) / 255.0;
        v[k] = (s <= 0.04045) ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
    }
    double X = (0.4124 * v[0] + 0.3576 * v[1] + 0.1805 * v[2]) / 0.95047;
    double Y = (0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]);
    double Z = (0.0193 * v[0] + 0.1192 * v[1] + 0.9505 * v[2]) / 1.08883;
    double f[3], t[3] = { X, Y, Z };
    for (int k = 0; k < 3; k++)
        f[k] = (t[k] > 216.0 / 24389.0) ? cbrt(t[k])
                                        : (841.0 / 108.0) * t[k] + 4.0 / 29.0;
    out[0] = 116.0 * f[1] - 16.0;
    out[1] = 500.0 * (f[0] - f[1]);
    out[2] = 200.0 * (f[1] - f[2]);
}

static double lightness(unsigned c)
{
    double lab[3];
    lab_of(c, lab);
    return lab[0];
}

/* CIE76 between two packed sRGB colours — the only honest way to say "these
 * two are still telling apart", which is the whole claim about the teal. */
static double delta_e(unsigned x, unsigned y)
{
    double a[3], b[3];
    lab_of(x, a);
    lab_of(y, b);
    double d = 0;
    for (int k = 0; k < 3; k++) d += pow(a[k] - b[k], 2);
    return sqrt(d);
}

/* OKLab hue in degrees, for the one constant that is measured there. */
static double ok_hue(unsigned c)
{
    double v[3];
    for (int k = 0; k < 3; k++) {
        double s = ((c >> (16 - 8 * k)) & 0xff) / 255.0;
        v[k] = (s <= 0.04045) ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
    }
    double l = cbrt(0.4122214708 * v[0] + 0.5363325363 * v[1] + 0.0514459929 * v[2]);
    double m = cbrt(0.2119034982 * v[0] + 0.6806995451 * v[1] + 0.1073969566 * v[2]);
    double s = cbrt(0.0883024619 * v[0] + 0.2817188376 * v[1] + 0.6299787005 * v[2]);
    double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    double b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    double h = atan2(b, a) * 180.0 / M_PI;
    return (h < 0) ? h + 360.0 : h;
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

static void test_perceived_lightness_survives(void)
{
    /* The one that would have caught what 415 shipped. HSL's L is a channel
     * average, not a brightness: hold #a78bfa's L = 0.76 and rotate the hue to
     * the wallpaper's yellow-green and you get #E9FA8B, which is 30 points of
     * CIE L* above the violet it replaced — a highlighter where an icon was.
     * Cyan came out 27 points up and Nord's frost 21, while every purple, pink
     * and blue landed within 8, which is exactly why this looked fine on most
     * themes and shipped wrong on a green one.
     *
     * So the assertion is not "the lightness channel was copied" — the old code
     * copied it faithfully. It is that the result LOOKS as bright as what it
     * replaced, on hues nowhere near each other. */
    const struct { const char *name; const float *acc; } themes[] = {
        { "olive",   ACC_OLIVE   },   /* the wallpaper accent that caught it */
        { "cyan",    ACC_SYNAPSE },
        { "frost",   ACC_NORD    },
        { "orange",  ACC_GRUVBOX },
        { "pink",    ACC_DRACULA },
    };

    for (unsigned i = 0; i < sizeof(themes) / sizeof(themes[0]); i++) {
        unsigned char *px = house_icon();
        syn_iconhue_apply(px, W, H, W * 4, themes[i].acc);

        /* The plate and the darker shade it is drawn over. */
        double dl_glyph = fabs(lightness(straight(px, 5, 16)) - lightness(BRAND));
        double dl_shade = fabs(lightness(straight(px, 12, 12)) - lightness(SHADE));
        assert(dl_glyph <= 5.0);
        assert(dl_shade <= 5.0);
        free(px);
    }
}

static void test_the_icons_are_as_quiet_as_their_theme(void)
{
    /* The other half of following a theme. The violet is a vivid colour and
     * Nord's frost blue is a deliberately quiet one; an icon that keeps the
     * violet's chroma on a quiet theme is a poster pinned to a muted desktop,
     * which is the same complaint as the wrong lightness wearing another hat.
     * Gruvbox's orange is the control: it is at least as vivid as the violet,
     * so nothing there should be pulled down. */
    unsigned char *quiet = house_icon(), *vivid = house_icon();
    syn_iconhue_apply(quiet, W, H, W * 4, ACC_NORD);
    syn_iconhue_apply(vivid, W, H, W * 4, ACC_GRUVBOX);

    double drawn[3], q[3], v[3];
    lab_of(BRAND, drawn);
    lab_of(straight(quiet, 5, 16), q);
    lab_of(straight(vivid, 5, 16), v);

    double c_drawn = hypot(drawn[1], drawn[2]);
    assert(hypot(q[1], q[2]) < c_drawn * 0.75);
    assert(hypot(v[1], v[2]) > c_drawn * 0.90);
    free(quiet); free(vivid);
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
    const float *accents[3] = { ACC_SYNAPSE, ACC_PRISM, ACC_NORD };
    for (int i = 0; i < 3; i++) {
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
     * wheel would stop reading as the same mark from theme to theme. Measured
     * in OKLab, because that is where the step is decided and how big it is —
     * COLLIDE, 45 deg — and an HSL reading of the same move is not a bound on
     * anything: the two circles do not run at the same rate, and 45 deg of
     * OKLab through the cyans comes out as 69 deg of HSL without the detail
     * having gone one step further than it was told to. */
    const float *accents[3] = { ACC_SYNAPSE, ACC_PRISM, ACC_NORD };
    for (int i = 0; i < 3; i++) {
        unsigned char *px = house_icon();
        syn_iconhue_apply(px, W, H, W * 4, accents[i]);
        assert(hue_gap(ok_hue(straight(px, 20, 20)), ok_hue(TEAL)) <= 46.0);
        free(px);
    }
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

/* ── 7. a colourless accent: the monochrome desktop ─────── */

static void test_a_colourless_accent_gives_colourless_icons(void)
{
    /* An accent with no hue is an ANSWER, not an absence: it is what
     * syn_palette_monochrome() hands back for a wallpaper with no colour in
     * it, and the panels, the bar and every app window go white and grey with
     * it. So do the icons — every pixel of them, including the sky blue below,
     * which the hue path leaves alone and which monochrome cannot: a grey
     * desktop with one blue speck in one dock icon is not a monochrome
     * desktop. What protects somebody else's icon is the GATE, and it is still
     * there — syn-resolve-gui never reaches this function at all.
     *
     * Byte equality of the three channels, not "close to grey", for the same
     * reason palette_test.c asserts it: a near-grey is what you get when a
     * greyscale accent is fed through a saturation floor, and the failure it
     * hides is the family being rotated to h = 0, which is RED. */
    unsigned char *px = house_icon();
    put(px, 30, 30, RESOLVE, 255);
    syn_iconhue_apply(px, W, H, W * 4, ACC_MONO);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned c = straight(px, x, y);
            assert(((c >> 16) & 0xff) == ((c >> 8) & 0xff));
            assert(((c >>  8) & 0xff) == ( c       & 0xff));
        }
    free(px);
}

static void test_every_colourless_accent_gives_the_SAME_icon(void)
{
    /* The monochrome palette's accent is #FFFFFF on a dark panel and #333333
     * on a pale one — same answer, different surface. The icon is a plate with
     * its own light and dark in it and it is not drawn ON either of those, so
     * it must not change between them: a dock icon that shifted value with the
     * theme's surface would be the one thing in the dock that flickers when
     * the wallpaper changes brightness. There is no hue here to follow, so
     * there is nothing to follow but the absence of one. */
    unsigned char *a = house_icon(), *b = house_icon(), *c = house_icon();
    syn_iconhue_apply(a, W, H, W * 4, ACC_MONO);
    syn_iconhue_apply(b, W, H, W * 4, ACC_MONO_PALE);
    syn_iconhue_apply(c, W, H, W * 4, ACC_GREY);
    assert(memcmp(a, b, (size_t)W * H * 4) == 0);
    assert(memcmp(a, c, (size_t)W * H * 4) == 0);
    free(a); free(b); free(c);
}

static void test_monochrome_keeps_the_drawing(void)
{
    /* Taking the colour out is all it may do. Once the hue is gone the
     * light/dark ordering IS the drawing — it is the only thing left telling a
     * body from a glyph — so if it ever came out of order there would be
     * nothing else to read the icon by. */
    unsigned char *px = house_icon();
    syn_iconhue_apply(px, W, H, W * 4, ACC_MONO);

    double body  = lightness(straight(px,  0,  0));   /* #1b1030 */
    double shade = lightness(straight(px, 12, 12));   /* #7c5cd6 */
    double plate = lightness(straight(px,  5, 16));   /* #a78bfa */
    double paper = lightness(straight(px,  7,  7));   /* #f1ecff */
    assert(body < shade && shade < plate && plate < paper);

    /* And each one lands where it was drawn, give or take the rounding of a
     * hex to a grey — the icon in grey, not the icon lightened. */
    assert(fabs(plate - lightness(BRAND)) < 3.0);
    assert(fabs(shade - lightness(SHADE)) < 3.0);
    assert(fabs(body  - lightness(BODY))  < 3.0);
    free(px);
}

static void test_the_detail_still_reads_in_monochrome(void)
{
    /* The teal knob/LED/pad bar cannot step off a colliding accent here the
     * way it does on SYNAPSE and Prism — greyscale has no hue circle to step
     * around. Desaturated where it was drawn it lands within TEN CIE L* of the
     * violet plate, which is not a detail, it is a smudge; the assertion below
     * on the DRAWN palette is what says so, and says it in a way that cannot
     * quietly stop being true. So in monochrome it gives way in LIGHTNESS, by
     * the step this desktop already uses between its monochrome accent and its
     * monochrome secondary. */
    assert(fabs(lightness(TEAL) - lightness(BRAND)) < 10.0);

    unsigned char *px = house_icon();
    syn_iconhue_apply(px, W, H, W * 4, ACC_MONO);

    unsigned detail = straight(px, 20, 20);
    unsigned plate  = straight(px,  5, 16);
    assert(lightness(detail) - lightness(plate) > 20.0);
    assert(delta_e(detail, plate) > 20.0);
    free(px);
}

static void test_the_lift_lands_on_the_detail_and_not_on_a_lookalike(void)
{
    /* SECOND_WINDOW asks about HUE and nothing else, and that was enough while
     * the detail's move was a hue nudge. It is not enough for a lightness move,
     * which is far louder on a picture with no other colour left in it: the
     * antialias ramp of synstudio's GREEN aperture blade crosses the teal's hue
     * window on its way into the dark opening — 27 pixels, in an icon with no
     * teal in it anywhere — and a flat lift turns them into a bright speckle
     * inside the iris. The lift is scaled by how much of the detail a pixel
     * actually is, chroma included, so a dark desaturated green in the window
     * stays where it was drawn. */
    unsigned char *px = house_icon();
    put(px, 28, 28, GREENRAMP, 255);
    syn_iconhue_apply(px, W, H, W * 4, ACC_MONO);

    assert(fabs(lightness(straight(px, 28, 28)) - lightness(GREENRAMP)) < 5.0);
    assert(lightness(straight(px, 20, 20)) - lightness(TEAL) > 15.0);
    free(px);
}

static void test_monochrome_preserves_the_premultiply(void)
{
    /* The same round trip as the hue path, down the branch that does not use
     * it — a corrupt surface fringes every antialiased edge, and the edges are
     * the whole of the drawing once the colour is gone. */
    unsigned char *px = canvas();
    for (unsigned a = 1; a <= 255; a++)
        put(px, (int)(a % W), (int)(a / W), TEAL, a);

    syn_iconhue_apply(px, W, H, W * 4, ACC_MONO);

    for (unsigned a = 1; a <= 255; a++) {
        uint32_t v = get(px, (int)(a % W), (int)(a / W));
        unsigned al = (v >> 24) & 0xff;
        assert(al == a);
        assert(((v >> 16) & 0xff) <= al);
        assert(((v >>  8) & 0xff) <= al);
        assert(( v        & 0xff) <= al);
    }
    free(px);
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
    test_perceived_lightness_survives();
    test_the_icons_are_as_quiet_as_their_theme();
    test_neutrals_are_not_tinted();
    test_a_foreign_hue_is_left_alone();
    test_teal_is_untouched_when_it_can_be();
    test_teal_gives_way_to_a_teal_accent();
    test_the_detail_moves_no_further_than_it_must();
    test_premultiply_is_preserved();
    test_fully_transparent_stays_transparent();
    test_reapplying_drifts();
    test_same_accent_is_stable();
    test_a_colourless_accent_gives_colourless_icons();
    test_every_colourless_accent_gives_the_SAME_icon();
    test_monochrome_keeps_the_drawing();
    test_the_detail_still_reads_in_monochrome();
    test_the_lift_lands_on_the_detail_and_not_on_a_lookalike();
    test_monochrome_preserves_the_premultiply();
    test_apply_survives_bad_input();
    printf("iconhue_test: all ok\n");
    return 0;
}
