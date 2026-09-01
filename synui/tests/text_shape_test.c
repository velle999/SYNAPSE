/*
 * text_shape_test — shaping and bidi (src/text.c)
 *
 * text_fallback_test asks whether a character resolves to a font that can draw
 * it. This asks the question after that one: whether what gets drawn is the
 * right shape, in the right order.
 *
 * ⛔ THE HARD PART OF THIS TEST IS NOT THE SHAPING, IT IS ASSERTING ON IT. A
 * check like "Arabic renders correctly" is not something a program can make,
 * and one written against specific glyph indices would be a check on whichever
 * font happened to be installed rather than on this compositor. So every
 * assertion here is a RELATION between two draws that only holds if the shaper
 * ran, and holds for any font that implements the script at all:
 *
 *   JOINING     two Arabic letters side by side are NARROWER than the same two
 *               drawn separately. Joined, they take initial and final forms — a
 *               tooth and a tail; isolated, each carries a full bowl. Every
 *               Arabic font on earth is narrower joined. Unjoined output makes
 *               this an equality, to the pixel, because the unshaped path draws
 *               the isolated forms and nothing else.
 *
 *   CONJUNCTS   the same relation in Devanagari: KA + VIRAMA + SSA shaped into
 *               a conjunct is narrower than the three drawn as three.
 *
 *   ORDER       an RTL paragraph puts its Latin run on the LEFT. Asserted by
 *               ink mass, not by glyph identity: three H's carry several times
 *               the ink of one alef, so which end of the string is dense says
 *               which end the Latin landed on. Logical-order output puts the
 *               alef first and inverts the comparison.
 *
 * ⚠ AND EVERY ONE OF THEM SKIPS WHERE THE SCRIPT HAS NO FONT. A CI container
 * with no Arabic face must say so rather than fail — the assertion needs real
 * glyphs to compare. Absence is detected by asking the fallback to draw the
 * script and finding no ink, which is also why the skip is per-script.
 *
 *   ./text_shape_test [out.png]
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cairo/cairo.h>

#include "synui.h"

static int failures;
static int skipped;

#define SURF_W 900
#define SURF_H 80
#define FONT_PX 32.0
#define BASE_X 20.0
#define BASE_Y 56.0

static void ok(bool cond, const char *what)
{
    printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) failures++;
}

static void skip(const char *what, const char *why)
{
    printf("  skip %s (%s)\n", what, why);
    skipped++;
}

/* Same font selection render.c's cairo_begin() does, for the same reason
 * text_fallback_test reimplements it: linking render.c would drag the whole
 * compositor in to test a file that needs cairo and fontconfig alone. */
static void begin(cairo_t *cr)
{
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_select_font_face(cr, syn_text_ui_font(),
                           CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, FONT_PX);
}

/* Draw one string onto a fresh surface. The surface is the caller's. */
static cairo_surface_t *draw(const char *text)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SURF_W, SURF_H);
    cairo_t *cr = cairo_create(s);
    begin(cr);
    cairo_move_to(cr, BASE_X, BASE_Y);
    syn_show_text(cr, text);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

/* What syn_text_extents() says the string is worth. */
static double advance(const char *text)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
    cairo_t *cr = cairo_create(s);
    begin(cr);
    cairo_text_extents_t te;
    syn_text_extents(cr, text, &te);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return te.x_advance;
}

/* Ink pixels in [x0, x1). The surface is ARGB32 premultiplied. */
static long ink_in(cairo_surface_t *s, int x0, int x1)
{
    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);
    int stride = cairo_image_surface_get_stride(s);
    unsigned char *d = cairo_image_surface_get_data(s);
    if (x0 < 0) x0 = 0;
    if (x1 > w) x1 = w;

    long n = 0;
    for (int y = 0; y < h; y++)
        for (int x = x0; x < x1; x++)
            /* The blue channel: the text is painted white on black, so any
             * non-zero component is ink. Alpha would work too — this reads the
             * same on a surface that was composited rather than cleared. */
            if (d[y * stride + x * 4] > 24) n++;
    return n;
}

static long ink_total(cairo_surface_t *s)
{
    return ink_in(s, 0, cairo_image_surface_get_width(s));
}

/* Does this box have a font for the script? Asked by drawing and looking, and
 * not by asking fontconfig: text.c substitutes '?' for a character nothing
 * covers, and a row of question marks IS ink. So the probe is a string of one
 * character, and the comparison is against the '?' the same context draws. */
static bool have_script(const char *probe)
{
    cairo_surface_t *a = draw(probe);
    cairo_surface_t *b = draw("?");
    long ia = ink_total(a), ib = ink_total(b);
    /* Identical ink means the fallback gave up and drew '?' — as close to
     * "no font" as this can get without reaching into text.c's internals. */
    bool have = (ia > 0) && (ia != ib);
    cairo_surface_destroy(a);
    cairo_surface_destroy(b);
    return have;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "text_shape.png";

    printf("text shaping and bidi\n\n");

    /* ── 1. Arabic joining ──────────────────────────────────────────────────
     *
     * BEH BEH as one string against BEH and BEH as two. Joined they are a
     * tooth and a tail; isolated they are two bowls. ⚠ The two-draw comparison
     * is what makes this font-independent: it does not care how wide a beh is,
     * only that the shaper made it narrower than it is alone.
     */
    printf("arabic joining\n");
    if (!have_script("ب")) {
        skip("two joined letters are narrower than two isolated", "no Arabic font");
    } else {
        double joined   = advance("بب");
        double isolated = 2.0 * advance("ب");
        printf("       joined %.2f vs isolated %.2f\n", joined, isolated);
        ok(joined < isolated - 0.5,
           "two joined letters are narrower than two isolated");
    }

    /* ── 2. Devanagari conjuncts ────────────────────────────────────────── */
    printf("\ndevanagari conjuncts\n");
    if (!have_script("क")) {
        skip("a conjunct is narrower than its parts", "no Devanagari font");
    } else {
        double conjunct = advance("क्ष");            /* क्ष */
        double apart    = advance("क") + advance("्") + advance("ष");
        printf("       conjunct %.2f vs apart %.2f\n", conjunct, apart);
        ok(conjunct < apart - 0.5, "a conjunct is narrower than its parts");
    }

    /* ── 3. bidi: reversing the logical order changes nothing on screen ────
     *
     * ⛔ TWO EARLIER VERSIONS OF THIS CHECK PASSED CORRECT OUTPUT AND FAILED
     * IT, so what they measured is worth recording. Both tried to identify the
     * Latin run by how much ink it carries — first the end quarters of the
     * advance, then the ink centroid against the geometric middle — and both
     * rest on an assumption that is simply not true of a monospace face: that
     * four alefs are much lighter than four H's. They are not. An alef is one
     * solid full-height stroke and an H is two stems and a hairline crossbar,
     * so four of each come out within a few per cent of the same ink, and the
     * centroid lands on the middle whichever end the alefs are at.
     *
     * What is measured instead needs no assumption about the font at all.
     *
     *   "ااااHHHH"  first strong character is an alef -> RTL paragraph
     *   "HHHHاااا"  first strong character is an H    -> LTR paragraph
     *
     * The two strings are each other's reverse, and the two paragraph
     * directions are each other's reverse, so a correct implementation draws
     * them IDENTICALLY — the reordering cancels out. An implementation that
     * ignores direction draws them mirrored, alefs on opposite ends. Comparing
     * the two column profiles therefore asks about ORDER and nothing else: it
     * is blind to which glyphs the font chose, how wide they are and how much
     * ink they carry, because both sides of the comparison get the same ones.
     */
    printf("\nbidi ordering\n");
    if (!have_script("ا")) {
        skip("reversing the logical order draws the same line", "no Arabic font");
    } else {
        cairo_surface_t *rtl = draw("ااااHHHH");
        cairo_surface_t *ltr = draw("HHHHاااا");

        int w = cairo_image_surface_get_width(rtl);
        int h = cairo_image_surface_get_height(rtl);
        int sr = cairo_image_surface_get_stride(rtl);
        int sl = cairo_image_surface_get_stride(ltr);
        unsigned char *dr = cairo_image_surface_get_data(rtl);
        unsigned char *dl = cairo_image_surface_get_data(ltr);

        long total = 0, diff = 0;
        for (int x = 0; x < w; x++) {
            long a = 0, b = 0;
            for (int y = 0; y < h; y++) {
                if (dr[y * sr + x * 4] > 24) a++;
                if (dl[y * sl + x * 4] > 24) b++;
            }
            total += a + b;
            diff  += (a > b) ? (a - b) : (b - a);
        }
        double drift = total > 0 ? (double)diff / (double)total : 1.0;
        printf("       column profiles differ by %.1f%% of their ink\n", drift * 100.0);
        ok(total > 0 && drift < 0.05,
           "reversing the logical order draws the same line");

        cairo_surface_destroy(rtl);
        cairo_surface_destroy(ltr);
    }

    /* ── 4. what must not have changed ─────────────────────────────────────
     *
     * ⛔ THE INVARIANT THE WHOLE FILE RESTS ON. Every panel centres, elides and
     * right-aligns by measuring first, so syn_text_extents() has to measure the
     * string syn_show_text() paints — through the shaped path as much as the
     * simple one. Checked here on a shaped string specifically, because the two
     * paths compute the advance in completely different ways and only the
     * simple one was ever covered.
     */
    printf("\nmeasure agrees with draw\n");
    static const char *const measured[] = {
        "مرحبا",     /* مرحبا */
        "नमस्ते", /* नमस्ते */
        "שלום",           /* שלום */
        "mixed العربية text",
    };
    for (unsigned i = 0; i < sizeof(measured) / sizeof(measured[0]); i++) {
        const char *t = measured[i];
        double adv = advance(t);
        cairo_surface_t *surf = draw(t);

        /* The ink must sit inside the advance it was measured at. 3px of slack
         * for the side bearings a bowl or a tail can hang past its origin. */
        int w = cairo_image_surface_get_width(surf);
        long past = ink_in(surf, (int)(BASE_X + adv) + 3, w);
        char what[128];
        snprintf(what, sizeof(what), "[%u] ink stays within the measured advance (%.1f)", i, adv);
        ok(adv > 0.0 && past == 0, what);
        cairo_surface_destroy(surf);
    }

    /* ── 5. the current point still advances ───────────────────────────────
     *
     * ⛔ cairo_show_glyphs() LEAVES THE CURRENT POINT UNDEFINED and
     * cairo_show_text() ADVANCES IT. Thirty places in this tree draw a
     * breadcrumb or a two-tone footer as consecutive syn_show_text() calls with
     * no move_to between them, so a shaped string that did not put the point
     * back would stack the rest of the line on top of itself — a defect that
     * would appear only in Arabic and Hindi, i.e. only where nobody is looking.
     */
    printf("\nthe current point\n");
    {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SURF_W, SURF_H);
        cairo_t *cr = cairo_create(s);
        begin(cr);
        cairo_move_to(cr, BASE_X, BASE_Y);
        const char *rtl = "العربية";
        double adv = 0.0;
        {
            cairo_text_extents_t te;
            syn_text_extents(cr, rtl, &te);
            adv = te.x_advance;
        }
        cairo_move_to(cr, BASE_X, BASE_Y);
        syn_show_text(cr, rtl);
        double x = 0, y = 0;
        bool has = cairo_has_current_point(cr);
        if (has) cairo_get_current_point(cr, &x, &y);
        ok(has, "a shaped draw leaves a current point");
        ok(has && fabs(x - (BASE_X + adv)) < 0.5,
           "the current point advanced by exactly the measured width");
        ok(has && fabs(y - BASE_Y) < 0.001, "the baseline did not move");
        ok(cairo_status(cr) == CAIRO_STATUS_SUCCESS,
           "context is not in an error state after a shaped draw");
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }

    /* ── 6. the languages that need no shaper still do not get one ─────────
     *
     * ⚠ THE POINT OF cp_needs_shaping() BEING A SWITCH AND NOT A REPLACEMENT.
     * Latin and Cyrillic do not go near HarfBuzz, so their metrics must be
     * exactly what they were — a fraction of a pixel of drift here is 663 call
     * sites re-laying-out to fix two languages that do not use them. There is
     * no "before" to compare against in a unit test, so what is checked is the
     * property that produces it: a string the UI face covers outright measures
     * what plain cairo says it measures, to the pixel.
     *
     * ⛔ CJK IS DELIBERATELY NOT IN THIS LIST, and the reason is worth writing
     * down because it looks like an omission. 控制面板 does not measure as
     * plain cairo measures it and never did: the monospace UI face has no CJK
     * glyphs, so cairo answers with the width of four .notdef boxes while
     * text.c answers with the width of the four characters the FALLBACK found a
     * font for. That gap is this file's whole original purpose, it predates
     * shaping by two years, and asserting it away here would be asserting the
     * fallback does not work. What matters for CJK is that it stays on the
     * fallback path, which the joining and conjunct cases above cover from the
     * other side: those relations hold only for scripts that ARE shaped.
     */
    printf("\nthe simple path is untouched\n");
    static const char *const simple[] = {
        "Control panel", "Панель управления", "naïve café", "0123456789 %",
    };
    for (unsigned i = 0; i < sizeof(simple) / sizeof(simple[0]); i++) {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
        cairo_t *cr = cairo_create(s);
        begin(cr);
        cairo_text_extents_t plain;
        cairo_text_extents(cr, simple[i], &plain);
        cairo_text_extents_t ours;
        syn_text_extents(cr, simple[i], &ours);
        char what[160];
        snprintf(what, sizeof(what), "[%u] \"%s\" measures as plain cairo does", i, simple[i]);
        ok(fabs(ours.x_advance - plain.x_advance) < 0.01, what);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }

    /* ── the contact sheet ─────────────────────────────────────────────────
     * For the question a test cannot ask: does that look like the word. */
    {
        /*
         * ⚠ REAL msgstrs OUT OF po/ar.po AND po/hi.po, not strings invented
         * for a test. What broke was the compositor drawing its OWN catalogs,
         * so the sheet has to show the sentences a person actually reads on an
         * Arabic or a Hindi desktop — a control panel row label, its help line
         * — rather than a dictionary word that happens to shape cleanly. The
         * English above each is the msgid it was looked up by.
         */
        static const char *const sheet_rows[] = {
            "Wallpaper accent  ·  لون التمييز من الخلفية",
            "مجموعة الألوان لإطارات النوافذ ولوحات synui نفسها",
            "Cursor theme  ·  कर्सर थीम",
            "खिड़कियों के किनारों और synui के अपने पैनलों के लिए रंग-संयोजन",
            "Hebrew — שלום",
            "Control panel · Панель · 控制面板",
        };
        const int n = (int)(sizeof(sheet_rows) / sizeof(sheet_rows[0]));
        const int rowh = 48;
        cairo_surface_t *sheet = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                            1180, rowh * n + 12);
        cairo_t *cr = cairo_create(sheet);
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.10);
        cairo_paint(cr);
        cairo_select_font_face(cr, syn_text_ui_font(),
                               CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 26.0);
        cairo_set_source_rgb(cr, 0.92, 0.92, 0.95);
        for (int i = 0; i < n; i++) {
            cairo_move_to(cr, 20, 38 + i * rowh);
            syn_show_text(cr, sheet_rows[i]);
        }
        cairo_destroy(cr);
        cairo_surface_write_to_png(sheet, out);
        cairo_surface_destroy(sheet);
        printf("\nwrote %s\n", out);
    }

    syn_text_shutdown();

    printf("\n%s (%d failures, %d skipped)\n",
           failures ? "FAILED" : "PASSED", failures, skipped);
    return failures ? 1 : 0;
}
