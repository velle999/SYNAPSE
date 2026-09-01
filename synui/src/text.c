/*
 * text.c — per-glyph font fallback for every compositor-drawn panel
 *
 * WHY THIS EXISTS
 *
 * Every panel synui draws — the control panel, the wallpaper picker, the
 * mixer, window titles, notifications — goes through cairo's *toy* font API,
 * which resolves a family name to exactly ONE face and has no fallback of any
 * kind. A character that face has no glyph for simply does not draw.
 *
 * render.c used to paper over that by substituting '?' for the missing runs,
 * so a CJK window title became a row of question marks rather than an invisible
 * one. That was the right call while the box had no CJK font to fall back TO;
 * it is the wrong call now, and it was never anything but damage control.
 *
 * This file does the real thing: when the active face cannot draw a character,
 * fontconfig is asked which installed font can, that font is loaded through
 * cairo-ft, and the character is drawn in it. Measured on the dev box, that
 * takes the emoji planes from 932 of 1824 codepoints drawable to 1791, and CJK
 * from nothing at all to complete.
 *
 * WHAT CALLERS SEE
 *
 * syn_show_text() and syn_text_extents() are drop-in replacements for
 * cairo_show_text() and cairo_text_extents(). They agree with each other, which
 * is the part that matters: a fallback glyph is routinely a different width
 * from the '?' it replaces, so anything that measures a string to centre, elide
 * or right-align it has to measure through the same fallback that draws it, or
 * the layout is wrong in exactly the cases this file was written for.
 *
 * COLOUR
 *
 * fontconfig, asked only "who covers U+1F600", answers DejaVu Sans — a
 * monochrome outline — because DejaVu happens to cover a slice of the Emoticons
 * block and scores higher on everything else. Noto Color Emoji only wins when
 * the pattern also carries FC_COLOR. So the fallback asks for colour when the
 * character is one that is *meant* to be an emoji, and does not otherwise.
 *
 * The rule is deliberately cheap: astral pictographs (>= U+1F000), plus any
 * character followed by VARIATION SELECTOR-16. It is not the Unicode
 * Emoji_Presentation property, which would want a ~90-range table pinned to a
 * Unicode version and re-checked on every update. The difference shows up only
 * on the BMP symbols that default to emoji presentation (⌚ ⏰ ⛄ ⛔ ✅ ❌ ➕ and
 * about thirty more): those draw as monochrome outlines here. On a desktop
 * styled after a terminal that is a defensible look rather than a bug, and the
 * emoji picker — which knows for a fact that everything it lists is an emoji —
 * asks for colour explicitly instead of relying on this heuristic.
 *
 * WHAT THIS IS STILL NOT
 *
 * Fallback is glyph SELECTION, not text layout. cairo's toy API does no complex
 * shaping and no bidi, and neither does this:
 *
 *   - Arabic draws unjoined — every letter in its isolated form, because
 *     nothing here runs the shaper that would pick initial/medial/final.
 *   - RTL scripts draw in logical order, left to right, so Hebrew and Arabic
 *     read backwards.
 *   - Indic reordering and conjuncts are not applied.
 *
 * All three were equally broken before, and worse: those strings drew as rows
 * of '?'. This makes them legible-ish rather than correct. Fixing them properly
 * means HarfBuzz for shaping plus a bidi pass, which is a different and much
 * larger change than choosing a face.
 *
 * ⛔ AND IT IS NEEDED NOW. The sentence that stood here said no panel in this
 * tree needs it, "since the strings that reach it are window titles and
 * filenames". That stopped being true at 575: po/ carries hi and ar catalogs,
 * so every panel title, footer hint and menu row in this compositor reaches
 * this function in Devanagari and in Arabic. The catalogs are correct; what
 * they draw is not. An Arabic reader gets isolated letter forms in reverse
 * order, which is worse to read than the English it replaced — Devanagari gets
 * unreordered matras.
 *
 * The other eleven languages are unaffected: Latin and Cyrillic need no
 * shaping, and CJK needs only the face fallback this file already does.
 *
 * Until a shaper lands, hi and ar are translated but not usable, and po/LINGUAS
 * says so beside their lines.
 *
 * Do not read "supports Arabic" into the test's contact sheet. It supports
 * DRAWING Arabic characters.
 *
 * WHAT IT COSTS
 *
 * This sits on every draw path in the compositor, so it was measured rather
 * than assumed. On the common path — an ASCII panel label the UI font covers
 * completely, where the fallback never fires and all the work is the coverage
 * check — measure+draw goes from 3.98us to 5.14us per string, 1.29x. A full
 * control-panel repaint draws on the order of 40 strings, so that is ~46us
 * added to a 16.7ms frame. A mixed-script string that actually exercises the
 * fallback costs ~30us.
 *
 * The number that would matter is a cache miss, and that is what the face list
 * and the negative ring below are for: without them every frame would re-run
 * FcFontMatch for every character no font covers.
 *
 * THREADING
 *
 * The face cache has no lock. Rendering only ever runs on the main loop (see
 * render.c), and this file is only reachable from a draw path.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <fontconfig/fontconfig.h>

#include "synui.h"

/* VARIATION SELECTOR-16 — "draw the preceding character as an emoji". */
#define SYN_VS16  0xFE0FU

/* Astral pictographs start here. Everything at or above this that has an emoji
 * font behind it is emoji-presentation in practice; see the header. */
#define SYN_EMOJI_PLANE_START  0x1F000U

/* ── The UI font ─────────────────────────────────────────────
 *
 * The family every panel draws in, and the thing the font picker sets. Kept
 * here rather than in the config struct because cairo_begin() runs on the draw
 * path with no server handle, exactly like the panel accent above it in
 * render.c.
 *
 * "monospace" is not a font — it is a fontconfig alias that resolves to
 * whatever the box calls its monospace face. That is what synui drew in for its
 * whole life before the picker existed, so it stays the default: a user who
 * never opens the picker sees no change at all.
 */
static char g_ui_font[128] = "monospace";

/* ⚠ There is exactly ONE font family on this desktop and it lives in
 * ~/.config/synui/font.state, written by synui-apply-font(1) and read by the
 * bar, synfiles, syn-settings, syn-disks, syn-update, syn-arsenal and synpkg.
 * synui's own panels used to keep a SECOND copy in the config (`ui_font =`,
 * mirrored into settings.state by the picker), which meant a font changed from
 * any other window in the suite moved every application and left the
 * compositor's own panels behind — the exact bug that moving the text SCALE
 * into font.state was done to remove, one setting over.
 *
 * The file is READ in config.c, not here: this file is a leaf on the draw path
 * (see the header) and stays free of the config helpers, so the pure render
 * tests can link it on its own. What is below is only the cache.
 */
void syn_text_set_ui_font(const char *family)
{
    if (!family || !*family) {
        snprintf(g_ui_font, sizeof(g_ui_font), "monospace");
        return;
    }
    snprintf(g_ui_font, sizeof(g_ui_font), "%s", family);
}

const char *syn_text_ui_font(void)
{
    return g_ui_font;
}

/* ── The fallback face cache ─────────────────────────────────
 *
 * Keyed by coverage, not by codepoint. A per-codepoint cache would be correct
 * but would call FcFontMatch once per new character; a face list checked
 * against each face's own charset answers almost every lookup with a bitset
 * test and only reaches fontconfig for a script nothing cached covers yet.
 *
 * In practice this settles at a handful of entries — one emoji font, one CJK,
 * one for whatever else is on screen — so the linear scan is cheaper than
 * hashing would be.
 *
 * FB_MAX_FACES is a ceiling, not a target. Past it, lookups still WORK (they
 * just miss the cache and re-resolve), so a pathological string full of rare
 * scripts gets slow rather than wrong.
 */
#define FB_MAX_FACES 32

struct fb_face {
    FcPattern         *pat;    /* kept alive: the cairo face is bound to it */
    FcCharSet         *cs;     /* borrowed from pat — do NOT destroy separately */
    cairo_font_face_t *face;
    bool               color;  /* was this resolved with FC_COLOR set? */
};

static struct fb_face g_fb[FB_MAX_FACES];
static int  g_fb_count;
static bool g_fc_ready;

/* Codepoints fontconfig had nothing for. Without this a string of unknowns
 * re-runs a full match per character per frame, which is the one way this file
 * could cost more than the '?' it replaced. Small and dumb on purpose: a ring,
 * not a set, because the miss list is short and never needs to be exact. */
#define FB_MISS_RING 64
static uint32_t g_miss[FB_MISS_RING];
static int      g_miss_next;

static bool fb_missed_before(uint32_t cp, bool color)
{
    uint32_t key = cp | (color ? 0x80000000U : 0U);
    for (int i = 0; i < FB_MISS_RING; i++)
        if (g_miss[i] == key) return true;
    return false;
}

static void fb_remember_miss(uint32_t cp, bool color)
{
    g_miss[g_miss_next] = cp | (color ? 0x80000000U : 0U);
    g_miss_next = (g_miss_next + 1) % FB_MISS_RING;
}

/* A font that covers cp, or NULL. Ownership stays with the cache. */
static cairo_font_face_t *fb_face_for(uint32_t cp, bool want_color)
{
    if (!g_fc_ready) {
        if (!FcInit()) return NULL;
        g_fc_ready = true;
    }

    for (int i = 0; i < g_fb_count; i++) {
        if (g_fb[i].color != want_color) continue;
        if (g_fb[i].cs && FcCharSetHasChar(g_fb[i].cs, cp))
            return g_fb[i].face;
    }

    if (fb_missed_before(cp, want_color))
        return NULL;

    FcCharSet *cs = FcCharSetCreate();
    if (!cs) return NULL;
    FcCharSetAddChar(cs, cp);

    FcPattern *pat = FcPatternCreate();
    if (!pat) { FcCharSetDestroy(cs); return NULL; }
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    if (want_color)
        FcPatternAddBool(pat, FC_COLOR, FcTrue);

    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult   res;
    FcPattern *m = FcFontMatch(NULL, pat, &res);
    FcPatternDestroy(pat);
    FcCharSetDestroy(cs);

    if (!m) { fb_remember_miss(cp, want_color); return NULL; }

    /* FcFontMatch always returns SOMETHING — its scoring never fails, it just
     * ranks. So the charset of the winner has to be checked against the
     * codepoint that was asked for, or a missing script silently resolves to
     * whatever sorted first and draws .notdef boxes. This is the check that
     * makes "no font for this" a real answer. */
    FcCharSet *got = NULL;
    if (FcPatternGetCharSet(m, FC_CHARSET, 0, &got) != FcResultMatch ||
        !got || !FcCharSetHasChar(got, cp)) {
        FcPatternDestroy(m);
        fb_remember_miss(cp, want_color);
        return NULL;
    }

    cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern(m);
    if (!face || cairo_font_face_status(face) != CAIRO_STATUS_SUCCESS) {
        if (face) cairo_font_face_destroy(face);
        FcPatternDestroy(m);
        fb_remember_miss(cp, want_color);
        return NULL;
    }

    if (g_fb_count >= FB_MAX_FACES) {
        /* Cache full. The face is still usable for this call, but it cannot be
         * kept — dropping it here rather than growing the table keeps the
         * failure "slow", never "wrong". */
        cairo_font_face_destroy(face);
        FcPatternDestroy(m);
        return NULL;
    }

    g_fb[g_fb_count] = (struct fb_face){
        .pat = m, .cs = got, .face = face, .color = want_color,
    };
    return g_fb[g_fb_count++].face;
}

void syn_text_shutdown(void)
{
    for (int i = 0; i < g_fb_count; i++) {
        if (g_fb[i].face) cairo_font_face_destroy(g_fb[i].face);
        if (g_fb[i].pat)  FcPatternDestroy(g_fb[i].pat);
    }
    memset(g_fb, 0, sizeof(g_fb));
    g_fb_count = 0;
    memset(g_miss, 0, sizeof(g_miss));
    g_miss_next = 0;
}

/* ── UTF-8 ───────────────────────────────────────────────────
 *
 * Copy `src` into `dst`, dropping anything malformed and truncating only on a
 * character boundary.
 *
 * This is the guard every draw path runs first, and it is not cosmetic: cairo
 * puts its ENTIRE context into CAIRO_STATUS_INVALID_STRING on bad bytes, after
 * which every later operation on that context is a silent no-op. One Steam
 * Workshop title cut mid-character by a fixed-size buffer was enough to blank
 * the whole bottom half of the wallpaper picker.
 */
void syn_utf8_copy(char *dst, size_t n, const char *src)
{
    if (!n) return;

    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; ) {
        int len;
        if      (*p < 0x80)                 len = 1;
        else if (*p >= 0xC2 && *p <= 0xDF)  len = 2;
        else if (*p >= 0xE0 && *p <= 0xEF)  len = 3;
        else if (*p >= 0xF0 && *p <= 0xF4)  len = 4;
        else { p++; continue; }   /* stray continuation byte, or an invalid lead */

        /* A NUL is not a continuation byte, so this stops at the end of the
         * string rather than reading past it. */
        bool ok = true;
        for (int i = 1; i < len; i++)
            if ((p[i] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { p++; continue; }

        if (o + (size_t)len + 1 > n) break;   /* truncate on a char boundary */
        memcpy(dst + o, p, (size_t)len);
        o += (size_t)len;
        p += len;
    }
    dst[o] = '\0';
}

/* Decode one codepoint. Assumes the string has already been through
 * syn_utf8_copy() above, so every sequence here is well-formed. */
static int utf8_next(const char *s, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80)                    { *cp = p[0]; return 1; }
    if (p[0] >= 0xC2 && p[0] <= 0xDF)   { *cp = ((uint32_t)(p[0] & 0x1F) << 6)  |  (p[1] & 0x3F); return 2; }
    if (p[0] >= 0xE0 && p[0] <= 0xEF)   { *cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6)  | (p[2] & 0x3F); return 3; }
    if (p[0] >= 0xF0 && p[0] <= 0xF4)   { *cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                                                ((uint32_t)(p[2] & 0x3F) << 6)  |  (p[3] & 0x3F); return 4; }
    *cp = 0xFFFD;
    return 1;
}

/* Does the face currently set on `cr` have a glyph for this one character? */
static bool base_covers(cairo_t *cr, const char *chr, int len)
{
    char one[8];
    if (len < 1 || len > 4) return false;
    memcpy(one, chr, (size_t)len);
    one[len] = '\0';

    cairo_glyph_t *g = NULL;
    int n = 0;
    if (cairo_scaled_font_text_to_glyphs(cairo_get_scaled_font(cr), 0, 0,
                                         one, -1, &g, &n, NULL, NULL, NULL)
        != CAIRO_STATUS_SUCCESS) {
        if (g) cairo_glyph_free(g);
        return false;
    }

    bool ok = (n > 0);
    for (int i = 0; i < n; i++)
        if (g[i].index == 0) { ok = false; break; }

    if (g) cairo_glyph_free(g);
    return ok;
}

/* ── The walk ────────────────────────────────────────────────
 *
 * One pass over the string, splitting it into runs: characters the active face
 * can draw accumulate into a buffer and go out in a single cairo call, and a
 * character it cannot gets resolved, drawn (or measured) on its own, and the
 * run starts over.
 *
 * Batching matters. Drawing character-by-character would defeat kerning and
 * turn every label into one cairo call per glyph; the common case here is that
 * the whole string is one run and this costs a coverage check per character.
 *
 * `measure` runs the identical walk without painting, which is the only way the
 * two stay in agreement about how wide a string is.
 */
static void text_walk(cairo_t *cr, const char *text, bool measure, double *advance)
{
    char safe[512];
    syn_utf8_copy(safe, sizeof(safe), text);
    if (!safe[0]) { if (advance) *advance = 0.0; return; }

    cairo_font_face_t *base = cairo_get_font_face(cr);
    cairo_font_face_reference(base);

    char   run[512];
    size_t run_len = 0;
    double total   = 0.0;

    /* Flush whatever has accumulated, in the face currently set. */
    #define FLUSH_RUN()                                                      \
        do {                                                                 \
            if (run_len) {                                                   \
                run[run_len] = '\0';                                         \
                if (measure) {                                               \
                    cairo_text_extents_t te;                                 \
                    cairo_text_extents(cr, run, &te);                        \
                    total += te.x_advance;                                   \
                } else {                                                     \
                    cairo_show_text(cr, run);                                \
                }                                                            \
                run_len = 0;                                                 \
            }                                                                \
        } while (0)

    size_t i = 0;
    while (safe[i]) {
        uint32_t cp;
        int len = utf8_next(safe + i, &cp);

        /* Look ahead for VS16: it does not draw, it re-presents the character
         * before it, so it has to be consumed together with that character or
         * it becomes a stray .notdef of its own. */
        uint32_t next = 0;
        int      nlen = 0;
        if (safe[i + len])
            nlen = utf8_next(safe + i + len, &next);
        bool vs16 = (nlen && next == SYN_VS16);

        if (base_covers(cr, safe + i, len) && !vs16) {
            if (run_len + (size_t)len + 1 < sizeof(run)) {
                memcpy(run + run_len, safe + i, (size_t)len);
                run_len += (size_t)len;
            }
            i += (size_t)len;
            continue;
        }

        bool want_color = vs16 || cp >= SYN_EMOJI_PLANE_START;
        cairo_font_face_t *fb = fb_face_for(cp, want_color);

        /* Nothing covers it in colour? A monochrome outline still beats a
         * blank. This is why ❤ and ☃ draw at all on a box with no emoji font. */
        if (!fb && want_color)
            fb = fb_face_for(cp, false);

        if (!fb) {
            /* Genuinely undrawable. Keep render.c's old behaviour for this one
             * character — a row that cannot be spelled is still a row that can
             * be identified — but do NOT collapse runs the way the old code
             * did, because that made two different titles look identical. */
            if (run_len + 2 < sizeof(run))
                run[run_len++] = '?';
            i += (size_t)len + (size_t)(vs16 ? nlen : 0);
            continue;
        }

        FLUSH_RUN();

        char one[8];
        memcpy(one, safe + i, (size_t)len);
        one[len] = '\0';

        cairo_set_font_face(cr, fb);
        if (measure) {
            cairo_text_extents_t te;
            cairo_text_extents(cr, one, &te);
            total += te.x_advance;
        } else {
            cairo_show_text(cr, one);
        }
        cairo_set_font_face(cr, base);

        i += (size_t)len + (size_t)(vs16 ? nlen : 0);
    }

    FLUSH_RUN();
    #undef FLUSH_RUN

    cairo_set_font_face(cr, base);
    cairo_font_face_destroy(base);

    if (advance) *advance = total;
}

/* Draw `text` at the current point, falling back per character. */
void syn_show_text(cairo_t *cr, const char *text)
{
    text_walk(cr, text, false, NULL);
}

/* Measure what syn_show_text() would draw.
 *
 * Only x_advance is filled from the fallback walk — that is the field every
 * caller in this tree actually uses (to centre, elide or right-align), and it
 * is the only one that composes across runs in different faces. The ink
 * rectangle of a mixed-face string is not the union of its runs' rectangles
 * unless you track the pen, and nothing here needs it; the other fields are
 * taken from a plain measurement so they are never garbage. */
void syn_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext)
{
    if (!ext) return;

    char safe[512];
    syn_utf8_copy(safe, sizeof(safe), text);
    cairo_text_extents(cr, safe, ext);

    double adv = 0.0;
    text_walk(cr, text, true, &adv);
    ext->x_advance = adv;
    if (ext->width < adv)
        ext->width = adv;
}
