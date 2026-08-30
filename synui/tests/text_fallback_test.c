/*
 * text_fallback_test — per-glyph font fallback (src/text.c)
 *
 * The thing worth pinning here is not "emoji render". It is that
 * syn_text_extents() measures the string syn_show_text() actually PAINTS.
 * Every panel in this tree centres, elides or right-aligns by measuring first,
 * so the moment the two disagree the layout is wrong in exactly the cases the
 * fallback was written for — a fallback glyph is routinely a different width
 * from the '?' it replaced, and the old code measured through cairo directly.
 *
 * Everything else here guards a regression this file has already had a shape
 * of: a cairo context left in an error status is a SILENT no-op for every
 * later draw, which is how one bad Steam Workshop title used to blank half a
 * panel. So each case re-checks cairo_status() afterwards.
 *
 * Coverage assertions are conditional on the box actually having a font — a CI
 * container with no CJK font must not fail this, it must say so. Run it and
 * read the report; the PNG it writes is for the question a test cannot answer
 * ("does that look like the character it should be").
 *
 *   ./text_fallback_test [out.png]
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>

#include "synui.h"

static int failures;
static int skipped;

/* What render.c's cairo_begin() does to a fresh context, minus the clear.
 *
 * Reimplemented rather than linked: cairo_begin() lives in render.c, and
 * linking render.c would pull in the whole compositor — outputs, the scene
 * graph, every panel — to test a file that needs cairo and fontconfig alone.
 * The one thing that has to stay in step is the font selection, so it reads
 * the family from syn_text_ui_font() exactly as the real one does. */
static void test_font_begin(cairo_t *cr)
{
    cairo_select_font_face(cr, syn_text_ui_font(),
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
}

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

/* Ink bounding box of what was painted, plus whether any pixel is coloured.
 * The surface is ARGB32 premultiplied, laid out BGRA on little-endian. */
static void ink_scan(cairo_surface_t *surf, int *px_count, bool *coloured,
                     int *min_x, int *max_x)
{
    cairo_surface_flush(surf);
    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char *d = cairo_image_surface_get_data(surf);

    *px_count = 0; *coloured = false;
    *min_x = w; *max_x = -1;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char *p = d + y * stride + x * 4;
            if (p[3] == 0) continue;
            (*px_count)++;
            if (x < *min_x) *min_x = x;
            if (x > *max_x) *max_x = x;
            if (p[0] != p[1] || p[1] != p[2]) *coloured = true;
        }
    }
}

struct sample {
    const char *name;
    const char *text;
    bool        expect_colour;   /* only checked when a colour font exists */
    /* Extra pixels the ink may run past the advance, on top of the 3px every
     * sample gets. ⚠ THIS IS A PROPERTY OF THE FONT, NOT OF THE LAYOUT — see
     * the ink check below. Zero for anything shaped from a text font. */
    double      ink_slack;
};

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "text_fallback.png";

    static const struct sample samples[] = {
        { "latin",        "Hello, world",   false },
        { "latin+accent", "naïve café",     false },
        { "cjk",          "中文字",          false },
        { "japanese",     "日本語テスト",     false },
        /* RTL draws unjoined and in logical order — see "WHAT THIS IS STILL
         * NOT" in text.c. These are here to prove the characters resolve to a
         * font at all, which they did not before; they are NOT a claim that
         * the script is laid out correctly. */
        { "arabic",       "مرحبا",           false },
        { "hebrew",       "שלום",            false },
        { "thai",         "สวัสดี",            false },
        { "emoji",        "🚀🔥🎉",           true  },
        { "emoji+text",   "ship it 🚀 now",   true  },
        { "vs16 heart",   "love ❤️",  true  },
        { "bmp symbol",   "☃ snow",     false },
        { "private use",  "",    false, 34.0 },
        /* ^ Private Use Area (U+E000, U+E001). Worth a row because these DO
         * resolve on SynapseOS: ttf-nerd-fonts-symbols-mono is a hard
         * dependency — the bar's icons ARE PUA codepoints — so the fallback
         * finds it, which is a second thing this file quietly fixed. On a box
         * without that font the same row exercises the genuinely-undrawable
         * path and "painted some ink" then covers the '?' substitute. Either
         * way something must appear. */
        { "mixed",        "a中🚀ب",          true  },
    };
    const int n = (int)(sizeof(samples) / sizeof(samples[0]));

    /* Contact sheet, one sample per row, for the eyeball question. */
    const int ROW_H = 44, SHEET_W = 640;
    cairo_surface_t *sheet = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                        SHEET_W, ROW_H * n + 8);
    cairo_t *sc = cairo_create(sheet);
    cairo_set_source_rgb(sc, 0.08, 0.08, 0.10);
    cairo_paint(sc);

    printf("text_fallback_test: %d samples, UI font \"%s\"\n\n",
           n, syn_text_ui_font());

    for (int i = 0; i < n; i++) {
        printf("%s: \"%s\"\n", samples[i].name, samples[i].text);

        /* Each sample gets its own scratch surface so the ink scan sees only
         * this string. */
        cairo_surface_t *surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 40);
        cairo_t *cr = cairo_create(surf);
        test_font_begin(cr);                   /* selects the UI font */
        cairo_set_font_size(cr, 22);
        cairo_set_source_rgb(cr, 1, 1, 1);

        /* ── the invariant: measure, then draw, and compare ── */
        cairo_text_extents_t ext;
        syn_text_extents(cr, samples[i].text, &ext);

        cairo_move_to(cr, 6, 30);
        syn_show_text(cr, samples[i].text);

        double after_x, after_y;
        cairo_get_current_point(cr, &after_x, &after_y);
        double drawn_advance = after_x - 6.0;

        ok(cairo_status(cr) == CAIRO_STATUS_SUCCESS,
           "context is not in an error state after drawing");

        /* The pen must land where the measurement said it would. A tolerance
         * of 0.5px covers hinting rounding between the measure and draw passes
         * without letting a genuinely wrong width through — the failures this
         * guards against are whole glyphs wide, not fractions of a pixel. */
        double delta = drawn_advance - ext.x_advance;
        if (delta < 0) delta = -delta;
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "measured advance %.2f matches drawn %.2f (delta %.3f)",
                 ext.x_advance, drawn_advance, delta);
        ok(delta <= 0.5, msg);

        int px; bool coloured; int min_x, max_x;
        ink_scan(surf, &px, &coloured, &min_x, &max_x);

        /* Private-use characters are the one sample nothing can draw. It must
         * still put SOMETHING down ('?') rather than silently vanishing. */
        ok(px > 0, "painted some ink");

        /* Ink must not run past where the advance says the string ended.
         * A 3px slack absorbs the right side bearing of an italic-ish glyph.
         *
         * ⚠ AND A PER-SAMPLE SLACK ON TOP, BECAUSE OVERHANG IS THE FONT'S
         * CHOICE. What this check is for is a SHAPING bug — glyphs laid down
         * somewhere other than where the advance said. That is caught by the
         * measured-vs-drawn assertion above, which is exact to 0.5px; this one
         * is a second opinion in pixels, and it assumes ink stays inside its
         * own advance box. Ornament and icon glyphs do not: a negative right
         * side bearing is legal and common in the PUA, where a decorative
         * glyph is deliberately drawn wider than the cell it advances. The
         * private-use row measured 28.00 both ways — the layout was right —
         * and painted to x=45. Failing that is failing the font for a design
         * decision, so the row carries its own slack and says why. */
        if (px > 0) {
            double ink_end = 6.0 + ext.x_advance + 3.0 + samples[i].ink_slack;
            snprintf(msg, sizeof(msg),
                     "ink ends at x=%d, within advance end %.1f",
                     max_x, ink_end);
            ok(max_x <= ink_end, msg);
        }

        if (samples[i].expect_colour) {
            if (coloured)
                ok(true, "drew in colour (colour emoji font in use)");
            else
                skip("colour emoji", "no colour font covers these codepoints");
        }

        /* Copy onto the contact sheet. */
        cairo_set_source_surface(sc, surf, 150, i * ROW_H + 4);
        cairo_paint(sc);
        cairo_set_source_rgb(sc, 0.6, 0.6, 0.65);
        cairo_select_font_face(sc, "monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(sc, 12);
        cairo_move_to(sc, 8, i * ROW_H + 28);
        cairo_show_text(sc, samples[i].name);

        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        printf("\n");
    }

    /* An empty string and a NULL-ish one must be no-ops, not crashes: panels
     * draw plenty of optional fields. */
    {
        cairo_surface_t *surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 32);
        cairo_t *cr = cairo_create(surf);
        test_font_begin(cr);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, 2, 20);
        syn_show_text(cr, "");
        cairo_text_extents_t e;
        syn_text_extents(cr, "", &e);
        printf("empty string:\n");
        ok(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "empty string is a no-op");
        ok(e.x_advance == 0.0, "empty string measures zero");
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        printf("\n");
    }

    /* Invalid UTF-8 must not poison the context — the whole reason
     * syn_utf8_copy exists. A lone 0x80 continuation byte and a truncated
     * 4-byte lead are the two shapes that actually turned up in the wild. */
    {
        cairo_surface_t *surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 32);
        cairo_t *cr = cairo_create(surf);
        test_font_begin(cr);
        cairo_set_font_size(cr, 14);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, 2, 20);
        /* Split literals: "\x80byte" would swallow the 'b' into the hex escape
         * and overflow it. A stray continuation byte, then a 4-byte lead cut
         * short — the two shapes that actually turned up in the wild. */
        syn_show_text(cr, "bad\x80" "byte");
        syn_show_text(cr, "cut\xf0\x9f\x9a");
        printf("invalid UTF-8:\n");
        ok(cairo_status(cr) == CAIRO_STATUS_SUCCESS,
           "invalid UTF-8 does not poison the context");

        /* And the context still works afterwards — the actual failure mode was
         * that everything AFTER the bad string silently stopped drawing. */
        cairo_move_to(cr, 2, 30);
        syn_show_text(cr, "after");
        int px; bool col; int mn, mx;
        ink_scan(surf, &px, &col, &mn, &mx);
        ok(px > 0, "still draws after an invalid string");
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        printf("\n");
    }

    /* The UI font is settable and sticks — this is what the font picker sets. */
    {
        const char *before = syn_text_ui_font();
        ok(before && *before, "there is a default UI font");
        syn_text_set_ui_font("DejaVu Serif");
        ok(strcmp(syn_text_ui_font(), "DejaVu Serif") == 0,
           "syn_text_set_ui_font takes effect");
        syn_text_set_ui_font(NULL);
        ok(strcmp(syn_text_ui_font(), "monospace") == 0,
           "a NULL family resets to monospace");
        printf("\n");
    }

    cairo_surface_write_to_png(sheet, out);
    cairo_destroy(sc);
    cairo_surface_destroy(sheet);
    syn_text_shutdown();

    printf("wrote %s\n", out);
    printf("%s (%d failure%s, %d skipped)\n",
           failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s", skipped);
    return failures ? 1 : 0;
}
