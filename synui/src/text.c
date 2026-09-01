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
 * SHAPING AND BIDI
 *
 * Fallback is glyph SELECTION. Choosing the right face is not enough for the
 * scripts where the shape of a letter depends on its neighbours or where the
 * reading order is not the storage order, and this file used to do neither:
 *
 *   - Arabic drew unjoined, every letter in its isolated form.
 *   - RTL text drew in logical order, so it read backwards.
 *   - Devanagari drew with its matras unreordered.
 *
 * That was tolerable while the strings reaching here were window titles and
 * filenames. It stopped being tolerable at 575, when po/ grew hi and ar
 * catalogs: every panel title, footer hint and menu row in this compositor now
 * arrives in Devanagari and in Arabic, and what they drew was worse to read
 * than the English they replaced. A correct catalog that renders wrong is not
 * a translation.
 *
 * So there is a second path now. FriBidi resolves the embedding levels and the
 * visual order of the runs; HarfBuzz shapes each run against the face that
 * covers it; the glyphs go out through cairo_show_glyphs() at the positions
 * HarfBuzz computed. Joining, mark positioning, Indic reordering and RTL all
 * come from that, and they come from the same libraries pango uses, so what
 * this draws matches what every GTK window on the desktop draws.
 *
 * ⚠ IT IS A SECOND PATH AND NOT A REPLACEMENT, WHICH IS DELIBERATE. Eleven of
 * the thirteen catalogs are Latin, Cyrillic or CJK and need no shaping at all
 * — they were already correct, drawn by the run-batching walk below, whose
 * cost is measured in the section under this one. Routing them through
 * HarfBuzz would re-lay-out every panel on the desktop to fix two languages
 * that do not use them: every metric would shift by a fraction, on 663 call
 * sites, in exchange for nothing visible. cp_needs_shaping() is the switch,
 * and it is a whole-string decision: one Arabic character anywhere shapes the
 * entire string, because a mixed string's runs have to be ordered against each
 * other and that is a decision about the string, not about a character.
 *
 * ⛔ AND THE SHAPED PATH DEGRADES TO THE SIMPLE ONE RATHER THAN TO NOTHING.
 * cairo_ft_scaled_font_lock_face() answers NULL when cairo was not built on
 * FreeType — rare, but a build option, not an impossibility — and there is no
 * FT_Face to hand HarfBuzz. That returns false and the character walk runs
 * instead, which is exactly the pre-shaping behaviour: unjoined and backwards,
 * but legible and measured consistently. A blank panel would be worse.
 *
 * What is still not done: no line breaking and no vertical writing. Every
 * caller here draws one line and elides it itself, and nothing in this tree
 * draws Mongolian.
 *
 * WHAT IT COSTS
 *
 * This sits on every draw path in the compositor, so it was measured rather
 * than assumed. Per string, measure plus draw, at 16px on the dev box:
 *
 *   ASCII, the UI face covers it          4.6us   simple path
 *   Cyrillic                              6.0us   simple path
 *   CJK, resolved through the fallback    6.5us   simple path
 *   Arabic                               11.3us   shaped
 *   mixed Arabic and Latin               13.7us   shaped
 *   Devanagari                           18.1us   shaped
 *
 * A full control-panel repaint draws on the order of 40 strings, so the two
 * shaped languages add roughly 0.5ms to a 16.7ms frame and the other eleven
 * add nothing, because they do not enter this code at all.
 *
 * ⛔ THE SHAPED NUMBERS ARE ENTIRELY A CACHING RESULT AND WOULD BE 78us AND
 * 162us WITHOUT IT. hb_ft_font_create() builds an hb_face_t and the first
 * hb_shape() against one parses that font's whole OpenType layout — GSUB, GPOS,
 * GDEF — which for a text font is most of a megabyte. Done per run per string
 * that is the entire cost of the feature, spent re-reading tables that did not
 * change. See the HarfBuzz font cache below; it is load-bearing, not a tidy-up.
 *
 * The other number that would matter is a fallback cache miss, and that is what
 * the face list and the negative ring below are for: without them every frame
 * would re-run FcFontMatch for every character no font covers.
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

#include <math.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <fontconfig/fontconfig.h>

/* ⚠ AFTER cairo-ft.h, WHICH IS WHAT PULLS FreeType IN. hb-ft.h names FT_Face
 * in its prototypes and does not include FreeType itself, so on its own it is
 * a wall of "unknown type name" from a header that looks self-contained. */
#include <fribidi.h>
#include <hb.h>
#include <hb-ft.h>

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

/* Defined with the rest of the shaping machinery further down; declared here
 * because shutdown is the one thing that has to reach every cache in the file
 * and it belongs beside the cache it was already clearing. */
static void hb_cache_clear(void);

void syn_text_shutdown(void)
{
    hb_cache_clear();
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

/* ── Shaping and bidi ────────────────────────────────────────
 *
 * The path Arabic, Hebrew and the Indic scripts take. See SHAPING AND BIDI at
 * the top of this file for why it is a second path and not a replacement.
 */

/* One string's worth. `safe` is 512 bytes, so it can hold at most 512
 * codepoints and this can never be the thing that truncates. */
#define SHAPE_MAX 512

/* Glyphs can outnumber characters — Devanagari decomposes, Arabic ligates the
 * other way — so this is not SHAPE_MAX. Past it a string is drawn short rather
 * than overrunning; nothing in this tree draws a label anywhere near it. */
#define SHAPE_MAX_GLYPHS 1024

/*
 * Does this character need a shaper, or does drawing it in storage order in a
 * face that covers it give the right answer?
 *
 * ⚠ RANGES, NOT THE Script PROPERTY. The honest version of this question is
 * "is this character in a script with a complex shaping engine, or does it
 * carry a bidi class other than L", which means a Unicode property table
 * pinned to a version and re-checked on every update — the same trade the
 * emoji-presentation heuristic above declined, for the same reason. What is
 * here is the block ranges of every script that needs joining, reordering or
 * RTL, and they do not move between Unicode versions: a block's range is fixed
 * when it is allocated.
 *
 * ⛔ ERRING TOWARDS true IS FREE; ERRING TOWARDS false IS THE BUG. A string
 * needlessly shaped comes out identical and costs microseconds. A string
 * wrongly declared simple comes out backwards, and does so only for the people
 * who cannot read the English it replaced. So the combining-mark ranges and
 * the bidi control characters are in here even though they are not scripts:
 * a combining mark needs positioning, and an explicit RLM in an otherwise
 * Latin string is a request for reordering that this must not ignore.
 */
static bool cp_needs_shaping(uint32_t cp)
{
    if (cp < 0x0300U)  return false;  /* ASCII, Latin-1, Latin Extended-A/B */

    if (cp <= 0x036FU)                    return true;   /* combining diacriticals */
    if (cp <= 0x058FU)                    return false;  /* Greek, Cyrillic, Armenian */
    if (cp <= 0x08FFU)                    return true;   /* Hebrew … Arabic Extended-A */
    if (cp <= 0x0DFFU)                    return true;   /* Devanagari … Sinhala */
    if (cp <= 0x0FFFU)                    return true;   /* Thai, Lao, Tibetan */
    if (cp <= 0x109FU)                    return true;   /* Myanmar */
    if (cp >= 0x1700U && cp <= 0x17FFU)   return true;   /* Tagalog … Khmer */
    if (cp >= 0x1AB0U && cp <= 0x1AFFU)   return true;   /* combining extended */
    if (cp >= 0x1B00U && cp <= 0x1B7FU)   return true;   /* Balinese */
    if (cp >= 0x1C80U && cp <= 0x1CFFU)   return true;   /* Cyrillic Ext-C, Vedic */
    if (cp >= 0x1DC0U && cp <= 0x1DFFU)   return true;   /* combining supplement */
    if (cp >= 0x200EU && cp <= 0x200FU)   return true;   /* LRM, RLM */
    if (cp >= 0x202AU && cp <= 0x202EU)   return true;   /* the embedding controls */
    if (cp >= 0x2066U && cp <= 0x2069U)   return true;   /* the isolate controls */
    if (cp >= 0x20D0U && cp <= 0x20FFU)   return true;   /* marks for symbols */
    if (cp >= 0xA980U && cp <= 0xA9DFU)   return true;   /* Javanese */
    if (cp >= 0xAA60U && cp <= 0xAA7FU)   return true;   /* Myanmar Extended-A */
    if (cp >= 0xFB1DU && cp <= 0xFDFFU)   return true;   /* Hebrew/Arabic presentation A */
    if (cp >= 0xFE00U && cp <= 0xFE0FU)   return true;   /* variation selectors */
    if (cp >= 0xFE20U && cp <= 0xFE2FU)   return true;   /* combining half marks */
    if (cp >= 0xFE70U && cp <= 0xFEFFU)   return true;   /* Arabic presentation B */
    if (cp >= 0x10800U && cp <= 0x10FFFU) return true;   /* the RTL historic scripts */
    if (cp >= 0x11000U && cp <= 0x11FFFU) return true;   /* Brahmic supplement */
    if (cp >= 0x1E800U && cp <= 0x1EFFFU) return true;   /* Adlam, Arabic mathematical */
    return false;
}

/* ── The HarfBuzz font cache ─────────────────────────────────
 *
 * ⛔ THIS IS NOT AN OPTIMISATION, IT IS THE DIFFERENCE BETWEEN SHAPING BEING
 * USABLE AND NOT. hb_ft_font_create() builds an hb_face_t, and the first
 * hb_shape() against a new face parses that font's GSUB, GPOS and GDEF tables
 * — the whole OpenType layout machinery, for a font that is routinely a
 * megabyte of it. Creating one per run per string, which is what the first
 * version of this did, measured 78us for an Arabic label and 162us for a Hindi
 * one against 5us for the ASCII equivalent. Forty strings in a control-panel
 * repaint is 6.5ms of a 16.7ms frame, spent re-reading the same tables — a
 * Hindi desktop would have dropped frames scrolling a menu, and only a Hindi
 * one, which is the kind of defect that ships.
 *
 * Keyed on the cairo_scaled_font_t, which is what pins BOTH the face and the
 * size the FT_Face is set to, and holding a reference to it so the pointer
 * cannot be recycled under the cache while an entry still names it.
 *
 * Small and linear for the same reason the fallback face list is: a panel draws
 * in one face at two or three sizes, so this settles at a handful of entries
 * and a scan is cheaper than a hash. Past the ceiling, lookups still work and
 * simply pay the old price.
 */
#define HB_CACHE_MAX 16

struct hb_entry {
    cairo_scaled_font_t *sf;     /* referenced, so the key stays unique */
    hb_font_t           *font;
};

static struct hb_entry g_hb[HB_CACHE_MAX];
static int             g_hb_count;

/*
 * The hb_font for this scaled font. ⚠ ONLY VALID WHILE THE CALLER HOLDS THE
 * FT_Face LOCK: hb_ft reads advances out of the FT_Face at shaping time, and
 * cairo sets that face's size on lock. Called with the lock held, used with the
 * lock held, and never touched between.
 */
static hb_font_t *hb_font_for(cairo_scaled_font_t *sf, FT_Face ft)
{
    for (int i = 0; i < g_hb_count; i++)
        if (g_hb[i].sf == sf) return g_hb[i].font;

    /* _referenced so the FT_Face survives independently of cairo's lock; the
     * scaled font reference below keeps the cairo side alive to match. */
    hb_font_t *f = hb_ft_font_create_referenced(ft);
    if (!f) return NULL;

    if (g_hb_count >= HB_CACHE_MAX)
        return f;   /* usable once; the caller destroys it — see shape_run_emit */

    g_hb[g_hb_count].sf   = cairo_scaled_font_reference(sf);
    g_hb[g_hb_count].font = f;
    g_hb_count++;
    return f;
}

static bool hb_font_is_cached(hb_font_t *f)
{
    for (int i = 0; i < g_hb_count; i++)
        if (g_hb[i].font == f) return true;
    return false;
}

/* One buffer, reset per run. hb_buffer_create() is a malloc and a table of
 * defaults; there is exactly one thread here (see THREADING) and exactly one
 * shaping call in flight at a time. */
static hb_buffer_t *g_hb_buf;

static void hb_cache_clear(void)
{
    for (int i = 0; i < g_hb_count; i++) {
        if (g_hb[i].font) hb_font_destroy(g_hb[i].font);
        if (g_hb[i].sf)   cairo_scaled_font_destroy(g_hb[i].sf);
    }
    memset(g_hb, 0, sizeof(g_hb));
    g_hb_count = 0;
    if (g_hb_buf) { hb_buffer_destroy(g_hb_buf); g_hb_buf = NULL; }
}

/* A shaping run: one face, one direction, one script, one stretch of the
 * string. The unit HarfBuzz is handed and the unit the bidi reorder moves. */
struct shape_run {
    int                start;   /* index into the codepoint array */
    int                len;
    FriBidiLevel       level;   /* even = LTR, odd = RTL */
    hb_script_t        script;
    cairo_font_face_t *face;    /* NULL = the face already set on the context */
};

/*
 * Which face draws this character.
 *
 * ⛔ THERE ARE FOUR ANSWERS AND ONLY THREE OF THEM ARE OBVIOUS. The first
 * version of this returned a face-or-NULL and let the caller read NULL as
 * "carry on in whatever face the previous character used". That collapsed two
 * completely different answers into one value, and the bug it produced is the
 * kind this whole file exists to prevent: the first LATIN letter after an
 * Arabic run inherited the ARABIC fallback face, drew in it, and — because a
 * face change is a run boundary — split "HHHH" into "H" + "HHH", two runs, at
 * two different widths. The string measured 4.9px wider than the identical
 * string with the words the other way round. Nothing errored. It was found
 * only because a bidi test compared the two orderings and they disagreed.
 *
 * So the answers are named:
 *
 *   FACE_BASE      the context's own face draws it. Ends any inheritance.
 *   FACE_FALLBACK  a face fontconfig found draws it. Becomes the inherited one.
 *   FACE_INHERIT   a combining mark or a format character, which MUST be drawn
 *                  in the same face as the letter it belongs to — positioned
 *                  against a different font, a mark lands in the wrong place.
 *   FACE_MISSING   nothing on the box draws it; the caller substitutes '?',
 *                  the same answer the character walk gives, so an undrawable
 *                  string does not depend on which path drew it.
 */
enum face_kind { FACE_BASE, FACE_FALLBACK, FACE_INHERIT, FACE_MISSING };

static enum face_kind shape_face_for(cairo_t *cr, uint32_t cp,
                                     cairo_font_face_t **out)
{
    *out = NULL;

    hb_unicode_funcs_t           *uf = hb_unicode_funcs_get_default();
    hb_unicode_general_category_t gc = hb_unicode_general_category(uf, cp);
    if (gc == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
        gc == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK ||
        gc == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
        gc == HB_UNICODE_GENERAL_CATEGORY_FORMAT)
        return FACE_INHERIT;

    char utf8[8];
    int  n = 0;
    if (cp < 0x80U) {
        utf8[n++] = (char)cp;
    } else if (cp < 0x800U) {
        utf8[n++] = (char)(0xC0U | (cp >> 6));
        utf8[n++] = (char)(0x80U | (cp & 0x3FU));
    } else if (cp < 0x10000U) {
        utf8[n++] = (char)(0xE0U | (cp >> 12));
        utf8[n++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        utf8[n++] = (char)(0x80U | (cp & 0x3FU));
    } else {
        utf8[n++] = (char)(0xF0U | (cp >> 18));
        utf8[n++] = (char)(0x80U | ((cp >> 12) & 0x3FU));
        utf8[n++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        utf8[n++] = (char)(0x80U | (cp & 0x3FU));
    }

    if (base_covers(cr, utf8, n))
        return FACE_BASE;

    bool want_color = cp >= SYN_EMOJI_PLANE_START;
    cairo_font_face_t *fb = fb_face_for(cp, want_color);
    if (!fb && want_color)
        fb = fb_face_for(cp, false);
    if (!fb)
        return FACE_MISSING;

    *out = fb;
    return FACE_FALLBACK;
}

/*
 * Rule L2 of the bidi algorithm, over RUNS rather than characters.
 *
 * ⚠ AND THAT DISTINCTION IS THE WHOLE POINT. fribidi_reorder_line() exists and
 * would reorder the CHARACTERS into visual order, which is the classic way to
 * get this wrong: the shaper has to see Arabic in LOGICAL order to know which
 * letter joins to which, and handing it a reversed string gives every letter
 * the wrong neighbours. So the characters stay put, the runs are shaped where
 * they are, and only the runs move.
 *
 * L2 itself: from the highest level down to the lowest odd level, reverse every
 * maximal run of items at or above that level.
 */
static void shape_reorder(struct shape_run *runs, int *order, int n)
{
    for (int i = 0; i < n; i++)
        order[i] = i;
    if (n < 2) return;

    FriBidiLevel highest = 0, lowest_odd = FRIBIDI_BIDI_MAX_RESOLVED_LEVELS;
    for (int i = 0; i < n; i++) {
        if (runs[i].level > highest) highest = runs[i].level;
        if ((runs[i].level & 1) && runs[i].level < lowest_odd)
            lowest_odd = runs[i].level;
    }
    if (lowest_odd == FRIBIDI_BIDI_MAX_RESOLVED_LEVELS)
        return;   /* nothing RTL: already in visual order */

    for (FriBidiLevel lvl = highest; lvl >= lowest_odd; lvl--) {
        for (int i = 0; i < n; i++) {
            if (runs[order[i]].level < lvl) continue;
            int j = i;
            while (j + 1 < n && runs[order[j + 1]].level >= lvl) j++;
            for (int a = i, b = j; a < b; a++, b--) {
                int t = order[a]; order[a] = order[b]; order[b] = t;
            }
            i = j;
        }
    }
}

/*
 * Shape one run and either paint it or measure it. `pen` is the position of the
 * run's origin in user space relative to the string's origin, and is advanced.
 * Returns false only if HarfBuzz could not be reached at all, which the caller
 * turns into abandoning the whole shaped attempt rather than drawing half a
 * string.
 */
static bool shape_run_emit(cairo_t *cr, const uint32_t *cps, int total,
                           const struct shape_run *run,
                           cairo_font_face_t *base,
                           double ox, double oy, bool measure, double *pen)
{
    cairo_set_font_face(cr, run->face ? run->face : base);

    cairo_scaled_font_t *sf = cairo_get_scaled_font(cr);
    if (!sf || cairo_scaled_font_status(sf) != CAIRO_STATUS_SUCCESS) return false;

    FT_Face ft = cairo_ft_scaled_font_lock_face(sf);
    if (!ft) return false;

    /*
     * ⛔ HarfBuzz ANSWERS IN DEVICE PIXELS AND cairo_show_glyphs() WANTS USER
     * SPACE. hb_ft sizes itself from the FT_Face, and cairo sized that face for
     * the scaled font — i.e. through the CTM. On a scaled output (and this
     * compositor scales outputs) the two spaces differ by exactly the display
     * scale, so using HarfBuzz's numbers directly draws every glyph at the
     * right shape in the wrong place, and only on a HiDPI screen. The ratio of
     * the font matrix (font space -> USER space) to the scale matrix (font
     * space -> DEVICE space) is that factor; hypot() rather than .xx alone so a
     * sheared or rotated matrix scales by its actual magnitude.
     */
    cairo_matrix_t fm, sm;
    cairo_scaled_font_get_font_matrix(sf, &fm);
    cairo_scaled_font_get_scale_matrix(sf, &sm);
    double dev = hypot(sm.xx, sm.yx);
    double usr = hypot(fm.xx, fm.yx);
    /* 1/64 because hb_ft reports 26.6 fixed point. */
    double k = ((dev > 0.0) ? (usr / dev) : 1.0) / 64.0;

    hb_font_t *hf = hb_font_for(sf, ft);
    if (!hf) { cairo_ft_scaled_font_unlock_face(sf); return false; }

    if (!g_hb_buf) g_hb_buf = hb_buffer_create();
    hb_buffer_t *buf = g_hb_buf;
    if (!buf) {
        if (!hb_font_is_cached(hf)) hb_font_destroy(hf);
        cairo_ft_scaled_font_unlock_face(sf);
        return false;
    }
    hb_buffer_reset(buf);

    /*
     * ⚠ THE WHOLE STRING IS HANDED OVER, WITH THE RUN AS A WINDOW INTO IT.
     * That is what the offset/length arguments are for: a shaper decides an
     * Arabic letter's form from the characters on either side of it, and those
     * can be in the neighbouring run. Adding only the run's own characters
     * gives the first and last letter of every run an isolated form — the exact
     * defect this file was opened to fix, reintroduced one level down.
     */
    hb_buffer_add_utf32(buf, cps, total, (unsigned)run->start, run->len);
    hb_buffer_set_direction(buf, (run->level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, run->script);
    hb_buffer_set_language(buf, hb_language_get_default());
    hb_shape(hf, buf, NULL, 0);

    unsigned int             count = hb_buffer_get_length(buf);
    hb_glyph_info_t         *info  = hb_buffer_get_glyph_infos(buf, NULL);
    hb_glyph_position_t     *pos   = hb_buffer_get_glyph_positions(buf, NULL);

    cairo_glyph_t glyphs[SHAPE_MAX_GLYPHS];
    unsigned int  ng = 0;
    double        x  = *pen;

    for (unsigned int i = 0; i < count && ng < SHAPE_MAX_GLYPHS; i++) {
        if (!measure) {
            glyphs[ng].index = info[i].codepoint;
            glyphs[ng].x     = ox + x + pos[i].x_offset * k;
            glyphs[ng].y     = oy - pos[i].y_offset * k;
            ng++;
        }
        x += pos[i].x_advance * k;
    }

    /* ⛔ EVERYTHING THAT TOUCHES THE FT_Face IS ABOVE THIS LINE. cairo hands
     * the face out under a lock and takes it back here; hb_font_destroy after
     * the unlock would be FreeType calls on a face cairo has resumed using. */
    /* The buffer is reused, not freed. The font is freed only when the cache
     * was full and handed out a one-shot. */
    if (!hb_font_is_cached(hf)) hb_font_destroy(hf);
    cairo_ft_scaled_font_unlock_face(sf);

    if (!measure && ng)
        cairo_show_glyphs(cr, glyphs, (int)ng);

    *pen = x;
    return true;
}

/*
 * The shaped walk. Returns false if it could not run at all, in which case the
 * caller falls back to the character walk — see the header.
 */
static bool text_shape(cairo_t *cr, const uint32_t *in, int n,
                       bool measure, double *advance)
{
    if (n <= 0 || n > SHAPE_MAX) return false;

    /* ── 1. faces, and the '?' substitution ──────────────────────────────── */
    uint32_t           cps[SHAPE_MAX];
    cairo_font_face_t *faces[SHAPE_MAX];

    cairo_font_face_t *base = cairo_get_font_face(cr);
    if (!base) return false;
    cairo_font_face_reference(base);

    cairo_font_face_t *carry = NULL;   /* the face a mark would inherit */
    for (int i = 0; i < n; i++) {
        cairo_font_face_t *f = NULL;
        cps[i] = in[i];
        switch (shape_face_for(cr, in[i], &f)) {
        case FACE_MISSING:
            /* Substituted here rather than skipped, so the bidi pass and the
             * shaper both see a character in its place and the run boundaries
             * do not move. */
            cps[i]   = (uint32_t)'?';
            faces[i] = NULL;
            carry    = NULL;
            break;
        case FACE_BASE:
            faces[i] = NULL;
            carry    = NULL;
            break;
        case FACE_FALLBACK:
            faces[i] = f;
            carry    = f;
            break;
        case FACE_INHERIT:
            faces[i] = carry;
            break;
        }
    }

    /* ── 2. bidi ─────────────────────────────────────────────────────────── */
    FriBidiCharType    types[SHAPE_MAX];
    FriBidiBracketType brackets[SHAPE_MAX];
    FriBidiLevel       levels[SHAPE_MAX];
    /*
     * FRIBIDI_PAR_ON = work the base direction out from the text itself (rule
     * P2/P3: the first strong character decides). ⚠ NOT FRIBIDI_PAR_LTR. These
     * strings are a panel's own labels in the user's own language; on an Arabic
     * desktop the paragraph is RTL, and forcing LTR would put the trailing
     * punctuation of every label on the wrong side — the tell that a bidi
     * implementation was bolted on rather than asked.
     */
    FriBidiParType par = FRIBIDI_PAR_ON;

    fribidi_get_bidi_types((const FriBidiChar *)cps, n, types);
    fribidi_get_bracket_types((const FriBidiChar *)cps, n, types, brackets);
    if (fribidi_get_par_embedding_levels_ex(types, brackets, n, &par, levels) == 0) {
        cairo_font_face_destroy(base);
        return false;
    }

    /* ── 3. itemise ──────────────────────────────────────────────────────── */
    struct shape_run runs[SHAPE_MAX];
    int              nruns = 0;
    hb_unicode_funcs_t *uf = hb_unicode_funcs_get_default();

    hb_script_t cur_script = HB_SCRIPT_COMMON;
    for (int i = 0; i < n; i++) {
        hb_script_t sc = hb_unicode_script(uf, cps[i]);
        /* Common and Inherited take the script of what they sit among —
         * a space, a digit or a combining mark must not split an Arabic run
         * into three, because each split is a shaping boundary. */
        if (sc == HB_SCRIPT_COMMON || sc == HB_SCRIPT_INHERITED || sc == HB_SCRIPT_UNKNOWN)
            sc = (nruns > 0) ? cur_script : HB_SCRIPT_COMMON;

        bool split = (nruns == 0)
                  || runs[nruns - 1].level  != levels[i]
                  || runs[nruns - 1].script != sc
                  || runs[nruns - 1].face   != faces[i];

        if (split) {
            runs[nruns++] = (struct shape_run){
                .start = i, .len = 1, .level = levels[i],
                .script = sc, .face = faces[i],
            };
        } else {
            runs[nruns - 1].len++;
        }
        cur_script = sc;
    }

    /* ── 4. visual order, then shape each run where it stands ────────────── */
    int order[SHAPE_MAX];
    shape_reorder(runs, order, nruns);

    double ox = 0.0, oy = 0.0;
    if (!measure) {
        if (!cairo_has_current_point(cr)) {
            cairo_font_face_destroy(base);
            return false;
        }
        cairo_get_current_point(cr, &ox, &oy);
    }

    double pen = 0.0;
    bool   ok  = true;
    for (int i = 0; i < nruns && ok; i++)
        ok = shape_run_emit(cr, cps, n, &runs[order[i]], base, ox, oy, measure, &pen);

    cairo_set_font_face(cr, base);
    cairo_font_face_destroy(base);

    if (!ok) return false;

    /*
     * ⛔ cairo_show_glyphs() LEAVES THE CURRENT POINT UNDEFINED, AND
     * cairo_show_text() ADVANCES IT. Thirty places in this tree draw a
     * breadcrumb or a two-tone footer as consecutive syn_show_text() calls with
     * no move_to between them — render.c's control-panel header is four in a
     * row — so a shaped string that did not put the point back would leave the
     * rest of the line stacked on top of itself. Restored explicitly, to
     * exactly where cairo_show_text() would have left it.
     */
    if (!measure)
        cairo_move_to(cr, ox + pen, oy);

    if (advance) *advance = pen;
    return true;
}

/*
 * The entry both public functions go through: decode once, decide which walk
 * the string needs, and fall back to the simple one if the shaped one cannot
 * run.
 */
static void text_render(cairo_t *cr, const char *text, bool measure, double *advance)
{
    char safe[512];
    syn_utf8_copy(safe, sizeof(safe), text);
    if (!safe[0]) { if (advance) *advance = 0.0; return; }

    uint32_t cps[SHAPE_MAX];
    int      n       = 0;
    bool     complex = false;
    for (size_t i = 0; safe[i] && n < SHAPE_MAX; ) {
        uint32_t cp;
        int len = utf8_next(safe + i, &cp);
        cps[n++] = cp;
        i += (size_t)len;
        if (cp_needs_shaping(cp)) complex = true;
    }

    if (complex && text_shape(cr, cps, n, measure, advance))
        return;

    text_walk(cr, safe, measure, advance);
}

/* Draw `text` at the current point, shaped where the script needs it and
 * falling back per character where it does not. */
void syn_show_text(cairo_t *cr, const char *text)
{
    text_render(cr, text, false, NULL);
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
    text_render(cr, text, true, &adv);
    ext->x_advance = adv;
    if (ext->width < adv)
        ext->width = adv;
}
