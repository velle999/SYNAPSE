/* thumb.c — the thumbnail maker.
 *
 * A thumbnail is a SECOND picture made from a photograph: a fixed canvas, the
 * developed frame filling it, and a few words over the top big enough to read
 * at the size a thumbnail is actually seen — which is about a thumbnail, and
 * not about the photograph.
 *
 * So it is its own layer rather than more develop settings. The develop stack
 * is what the picture IS; this is how it is presented, and the two travel in
 * one sidecar because they are decisions about the same file.
 *
 * ⚠ THE WORDS ARE DRAWN BY FFMPEG. Nothing here is linked to a font
 * rasteriser and nothing here is going to be — the same bargain the titles in
 * a timeline strike, and it means a thumbnail's caption and a video's caption
 * come out of the same code. The picture is developed by this program, saved
 * once, and handed to one ffmpeg pass that frames it and letters it.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "synstudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ canvases -- */

/* Where a thumbnail is going decides its shape, so the list is by
 * DESTINATION and not by aspect ratio: nobody has ever wanted "16:9", they
 * have wanted the one YouTube takes. */
static const ss_thumb_size sizes[] = {
    { "youtube", 1280,  720, "YouTube (1280×720)" },
    { "hd",      1920, 1080, "Full HD (1920×1080)" },
    { "short",   1080, 1920, "Short / Reel (1080×1920)" },
    { "square",  1080, 1080, "Square (1080×1080)" },
    { "social",  1200,  630, "Link preview (1200×630)" },
    { "custom",  1280,  720, "Custom" },
    { NULL, 0, 0, NULL }
};

const ss_thumb_size *ss_thumb_sizes(void) { return sizes; }

static int nsizes(void)
{
    int n = 0;
    while (sizes[n].name) n++;
    return n;
}

void ss_thumb_canvas(const ss_thumb *t, int *w, int *h)
{
    int n = nsizes(), i = (t && t->canvas >= 0 && t->canvas < n) ? t->canvas : 0;

    if (!strcmp(sizes[i].name, "custom")) {
        *w = t->w > 0 ? t->w : sizes[i].w;
        *h = t->h > 0 ? t->h : sizes[i].h;
    } else {
        *w = sizes[i].w;
        *h = sizes[i].h;
    }
    /* Even dimensions, because every encoder that will ever be pointed at one
     * of these wants them and an odd number is a failure at the last step. */
    if (*w & 1) (*w)++;
    if (*h & 1) (*h)++;
}

void ss_thumb_reset(ss_thumb *t)
{
    int i;

    memset(t, 0, sizeof(*t));
    t->canvas = 0;
    t->w = 1280;
    t->h = 720;
    t->fit = 0;
    for (i = 0; i < SS_THUMB_TEXTS; i++) {
        ss_thumb_text *x = &t->text[i];
        x->size   = 0.16f;
        x->r = x->g = x->b = 1.0f;
        x->border = 0.10f;
        x->br = x->bg_ = x->bb = 0.0f;
        x->shadow = 0.04f;
        /* Top, middle, bottom — so three layers used in order come out where
         * somebody would put them without touching a single control. */
        x->pos    = i == 0 ? SS_TEXT_TC : i == 1 ? SS_TEXT_MC : SS_TEXT_BC;
        x->weight = SS_FW_BOLD;
        x->pad    = 0.25f;
        x->pr = x->pg = x->pb = 0.0f;
    }
}

/* ------------------------------------------------------------- the table -- */

enum { TF_FLOAT, TF_INT, TF_ENUM, TF_TEXT };

#define FIT_CHOICES  "fill|fit"
#define POS_CHOICES  "topleft|topcentre|topright|left|centre|right|" \
                     "bottomleft|bottomcentre|bottomright"

typedef struct {
    const char *key;
    int         type;
    size_t      off;
    float       lo, hi;
    const char *group, *label, *choices;
    size_t      len;
} tfield;

#define T(k, ty, m, lo, hi, grp, lbl, ch) \
    { k, ty, offsetof(ss_thumb, m), lo, hi, grp, lbl, ch, \
      sizeof(((ss_thumb *)0)->m) }

/* ⚠ ONE table. The CLI reads it, the sidecar reads it, and the window builds
 * its panel from it — so a control added here appears in all three and can
 * never appear in only two. Adding a setting is adding a ROW. */
static const tfield tfields[] = {
    T("on",        TF_INT,   on,       0.0f, 1.0f,   "Canvas", "Thumbnail on", NULL),
    T("canvas",    TF_ENUM,  canvas,   0.0f, 5.0f,   "Canvas", "Size", NULL),
    T("width",     TF_INT,   w,       16.0f, 8192.0f,"Canvas", "Custom width", NULL),
    T("height",    TF_INT,   h,       16.0f, 8192.0f,"Canvas", "Custom height", NULL),
    T("fit",       TF_ENUM,  fit,      0.0f, 1.0f,   "Canvas", "Framing", FIT_CHOICES),
    T("bg.r",      TF_FLOAT, bg_r,     0.0f, 1.0f,   "Canvas", "Background red", NULL),
    T("bg.g",      TF_FLOAT, bg_g,     0.0f, 1.0f,   "Canvas", "Background green", NULL),
    T("bg.b",      TF_FLOAT, bg_b,     0.0f, 1.0f,   "Canvas", "Background blue", NULL),

#define TEXTROWS(n, N, grp) \
    T(#n ".words",  TF_TEXT,  text[N].words,  0.0f, 0.0f,  grp, "Words", NULL), \
    T(#n ".size",   TF_FLOAT, text[N].size,   0.02f, 0.6f, grp, "Size", NULL), \
    T(#n ".r",      TF_FLOAT, text[N].r,      0.0f, 1.0f,  grp, "Red", NULL), \
    T(#n ".g",      TF_FLOAT, text[N].g,      0.0f, 1.0f,  grp, "Green", NULL), \
    T(#n ".b",      TF_FLOAT, text[N].b,      0.0f, 1.0f,  grp, "Blue", NULL), \
    T(#n ".pos",    TF_ENUM,  text[N].pos,    0.0f, 8.0f,  grp, "Placement", POS_CHOICES), \
    T(#n ".x",      TF_FLOAT, text[N].dx,    -0.5f, 0.5f,  grp, "Nudge across", NULL), \
    T(#n ".y",      TF_FLOAT, text[N].dy,    -0.5f, 0.5f,  grp, "Nudge down", NULL), \
    T(#n ".border", TF_FLOAT, text[N].border, 0.0f, 0.4f,  grp, "Outline", NULL), \
    T(#n ".border.r", TF_FLOAT, text[N].br,   0.0f, 1.0f,  grp, "Outline red", NULL), \
    T(#n ".border.g", TF_FLOAT, text[N].bg_,  0.0f, 1.0f,  grp, "Outline green", NULL), \
    T(#n ".border.b", TF_FLOAT, text[N].bb,   0.0f, 1.0f,  grp, "Outline blue", NULL), \
    T(#n ".shadow", TF_FLOAT, text[N].shadow, 0.0f, 0.3f,  grp, "Shadow", NULL), \
    T(#n ".plate",  TF_FLOAT, text[N].plate,  0.0f, 1.0f,  grp, "Plate", NULL), \
    T(#n ".plate.r",TF_FLOAT, text[N].pr,     0.0f, 1.0f,  grp, "Plate red", NULL), \
    T(#n ".plate.g",TF_FLOAT, text[N].pg,     0.0f, 1.0f,  grp, "Plate green", NULL), \
    T(#n ".plate.b",TF_FLOAT, text[N].pb,     0.0f, 1.0f,  grp, "Plate blue", NULL), \
    T(#n ".pad",    TF_FLOAT, text[N].pad,    0.0f, 1.0f,  grp, "Plate padding", NULL), \
    T(#n ".font",   TF_TEXT,  text[N].font,   0.0f, 0.0f,  grp, "Font", NULL), \
    T(#n ".weight", TF_ENUM,  text[N].weight, 0.0f, 4.0f,  grp, "Weight", \
      "regular|bold|light|italic|bolditalic")

    TEXTROWS(text1, 0, "Text 1"),
    TEXTROWS(text2, 1, "Text 2"),
    TEXTROWS(text3, 2, "Text 3")
};

static const int ntfields = (int)(sizeof tfields / sizeof tfields[0]);

static const char *canvas_choices(void)
{
    /* Built from the canvas table rather than written out, for the reason the
     * transition choices are: two lists that must agree are one list. */
    static char buf[256];
    int i;
    if (buf[0]) return buf;
    for (i = 0; sizes[i].name; i++)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf),
                 "%s%s", i ? "|" : "", sizes[i].name);
    return buf;
}

static const tfield *find(const char *key)
{
    int i;
    for (i = 0; i < ntfields; i++)
        if (!strcmp(tfields[i].key, key)) return &tfields[i];
    return NULL;
}

static int enum_value(const char *choices, const char *s)
{
    const char *p = choices;
    int i = 0;
    size_t n = strlen(s);
    while (p) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (len == n && !strncmp(p, s, n)) return i;
        if (!bar) break;
        p = bar + 1;
        i++;
    }
    return -1;
}

static void enum_name(const char *choices, int v, char *out, size_t n)
{
    const char *p = choices;
    int i = 0;
    while (p) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (i == v) { if (len >= n) len = n - 1; memcpy(out, p, len); out[len] = '\0'; return; }
        if (!bar) break;
        p = bar + 1;
        i++;
    }
    snprintf(out, n, "%d", v);
}

static const char *choices_of(const tfield *f)
{
    return !strcmp(f->key, "canvas") ? canvas_choices() : f->choices;
}

int ss_thumb_get(const ss_thumb *t, const char *key, char *out, size_t n)
{
    const tfield *f = find(key);
    const char *base = (const char *)t;

    if (!f) return -1;
    switch (f->type) {
    case TF_FLOAT: snprintf(out, n, "%g", (double)*(const float *)(base + f->off)); break;
    case TF_INT:   snprintf(out, n, "%d", *(const int *)(base + f->off)); break;
    case TF_ENUM:  enum_name(choices_of(f), *(const int *)(base + f->off), out, n); break;
    case TF_TEXT:  snprintf(out, n, "%s", base + f->off); break;
    default: return -1;
    }
    return 0;
}

int ss_thumb_set(ss_thumb *t, const char *key, const char *val)
{
    const tfield *f = find(key);
    char *base = (char *)t;

    if (!f) return -1;
    switch (f->type) {
    case TF_FLOAT: {
        float v = (float)atof(val);
        *(float *)(base + f->off) = ss_clampf(v, f->lo, f->hi);
        break;
    }
    case TF_INT: {
        int v = atoi(val);
        if (v < (int)f->lo) v = (int)f->lo;
        if (v > (int)f->hi) v = (int)f->hi;
        *(int *)(base + f->off) = v;
        break;
    }
    case TF_ENUM: {
        int v = enum_value(choices_of(f), val);
        /* A number is accepted too, because a document written by a later
         * version may carry a row this build has no name for — and the value
         * it means is still the value. */
        if (v < 0) v = atoi(val);
        if (v < (int)f->lo) v = (int)f->lo;
        if (v > (int)f->hi) v = (int)f->hi;
        *(int *)(base + f->off) = v;
        break;
    }
    case TF_TEXT:
        /* ⚠ The member's OWN size. A shared sizeof here is a 200-byte
         * overflow the first time a shorter field is added. */
        snprintf(base + f->off, f->len, "%s", val);
        break;
    default: return -1;
    }
    return 0;
}

int ss_thumb_describe(const ss_thumb *t, int i, ss_thumb_info *out)
{
    static char val[512];
    const tfield *f;

    if (i < 0 || i >= ntfields) return 0;
    f = &tfields[i];
    ss_thumb_get(t, f->key, val, sizeof val);
    out->key    = f->key;
    out->value  = val;
    out->lo     = f->lo;
    out->hi     = f->hi;
    out->type   = f->type == TF_FLOAT ? "float" : f->type == TF_INT ? "int"
                : f->type == TF_ENUM  ? "enum"  : "text";
    out->group  = f->group;
    out->label  = f->label;
    out->choices = choices_of(f) ? choices_of(f) : "";
    return 1;
}

int ss_thumb_used(const ss_thumb *t)
{
    ss_thumb d;
    ss_thumb_reset(&d);
    return memcmp(t, &d, sizeof d) != 0;
}

/* Written only when it is not the default, so every sidecar made before
 * thumbnails existed reads back byte for byte. */
int ss_thumb_write(const ss_thumb *t, FILE *fp)
{
    char buf[512];
    int i;

    if (!ss_thumb_used(t)) return 0;
    for (i = 0; i < ntfields; i++) {
        const tfield *f = &tfields[i];
        if (ss_thumb_get(t, f->key, buf, sizeof buf) != 0) continue;
        /* ⚠ A caption can contain anything a keyboard makes, and the sidecar
         * is one record per LINE. A newline in the words would end the record
         * halfway through and the rest would read as unknown keys. */
        if (f->type == TF_TEXT) {
            char esc[1024];
            size_t o = 0, k;
            for (k = 0; buf[k] && o + 3 < sizeof esc; k++) {
                if (buf[k] == '\n')      { esc[o++] = '\\'; esc[o++] = 'n'; }
                else if (buf[k] == '\t') { esc[o++] = '\\'; esc[o++] = 't'; }
                else if (buf[k] == '\\') { esc[o++] = '\\'; esc[o++] = '\\'; }
                else                       esc[o++] = buf[k];
            }
            esc[o] = '\0';
            if (!esc[0]) continue;
            fprintf(fp, "thumb.%s\t%s\n", f->key, esc);
        } else {
            fprintf(fp, "thumb.%s\t%s\n", f->key, buf);
        }
    }
    return ferror(fp) ? -1 : 0;
}

/* ------------------------------------------------------------ rendering -- */

/* Everything below builds ONE ffmpeg command: the developed picture in, the
 * canvas and the words applied, the thumbnail out. The picture itself was
 * already developed by this program and written to `src` — this pass never
 * touches colour, which is what keeps `src/colour.c` the only place a pixel's
 * colour is decided. */

static void esc_f(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 3 < n; in++) {
        if (*in == '\\' || *in == ':' || *in == '\'' || *in == ',' ||
            *in == '[' || *in == ']' || *in == ';')
            out[o++] = '\\';
        out[o++] = *in;
    }
    out[o] = '\0';
}

static void hexc(float r, float g, float b, float a, char *out, size_t n)
{
    int ri = (int)(ss_clampf(r, 0, 1) * 255.0f + 0.5f);
    int gi = (int)(ss_clampf(g, 0, 1) * 255.0f + 0.5f);
    int bi = (int)(ss_clampf(b, 0, 1) * 255.0f + 0.5f);
    snprintf(out, n, "0x%02X%02X%02X@%.3f", ri, gi, bi, (double)ss_clampf(a, 0, 1));
}

/* The nine anchors, plus a nudge. The same expressions a title uses, because
 * a caption in the corner of a thumbnail and a caption in the corner of a
 * frame are the same request. */
static void xy_of(const ss_thumb_text *x, char *xs, size_t xn, char *ys, size_t yn)
{
    static const char *cx[3] = { "(w*0.05)", "((w-text_w)/2)", "(w-text_w-w*0.05)" };
    static const char *cy[3] = { "(h*0.05)", "((h-text_h)/2)", "(h-text_h-h*0.05)" };
    int p = (x->pos >= 0 && x->pos <= 8) ? x->pos : SS_TEXT_BC;

    snprintf(xs, xn, "%s+(w*%.5f)", cx[p % 3], (double)x->dx);
    snprintf(ys, yn, "%s+(h*%.5f)", cy[p / 3], (double)x->dy);
}

int ss_thumb_graph(const ss_thumb *t, const char *textdir, char *out, size_t n)
{
    int w, h, i, used = 0;
    char bg[40];
    size_t o = 0;

    ss_thumb_canvas(t, &w, &h);
    hexc(t->bg_r, t->bg_g, t->bg_b, 1.0f, bg, sizeof bg);

    /* FILL crops to the canvas, FIT pads to it. Fill is the default because a
     * thumbnail is a fixed rectangle somebody else's page will show whatever
     * happens — letterboxing inside it is a decision, not a default. */
    if (t->fit == 0)
        o += (size_t)snprintf(out + o, n - o,
                              "scale=%d:%d:force_original_aspect_ratio=increase,"
                              "crop=%d:%d", w, h, w, h);
    else
        o += (size_t)snprintf(out + o, n - o,
                              "scale=%d:%d:force_original_aspect_ratio=decrease,"
                              "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:%s", w, h, w, h, bg);

    for (i = 0; i < SS_THUMB_TEXTS && o < n; i++) {
        const ss_thumb_text *x = &t->text[i];
        char tp[1200], esc[2400], fesc[1200], xs[96], ys[96];
        char col[40], bcol[40], pcol[40];
        int size, bw, sh;

        if (!x->words[0]) continue;
        used++;

        /* The words go in a FILE, never into `text=`. drawtext's argument is
         * parsed twice — once by the filtergraph splitter and once by
         * drawtext itself — so a caption with a colon, a comma or a quote in
         * it fails the whole render rather than being lettered. */
        snprintf(tp, sizeof tp, "%s/thumbtext%d.txt", textdir, i);
        esc_f(tp, esc, sizeof esc);
        esc_f(ss_font_file(x->font, x->weight), fesc, sizeof fesc);

        size = (int)(x->size * h + 0.5f);
        if (size < 4) size = 4;
        /* An outline is not decoration. White words land on a white sky often
         * enough that a caption without one is unreadable on exactly the
         * picture somebody chose for being bright. */
        bw = (int)(x->border * size + 0.5f);
        sh = (int)(x->shadow * size + 0.5f);
        xy_of(x, xs, sizeof xs, ys, sizeof ys);
        hexc(x->r, x->g, x->b, 1.0f, col, sizeof col);
        hexc(x->br, x->bg_, x->bb, 1.0f, bcol, sizeof bcol);
        hexc(x->pr, x->pg, x->pb, x->plate, pcol, sizeof pcol);

        /* expansion=none: drawtext runs its own %{...} expansion over
         * whatever it read from the file, so a caption with a percent sign in
         * it fails with "Stray %" long after it was typed. */
        o += (size_t)snprintf(out + o, n - o,
                 ",drawtext=fontfile='%s':textfile='%s':expansion=none"
                 ":fontcolor=%s:fontsize=%d:x=%s:y=%s",
                 fesc, esc, col, size, xs, ys);
        if (bw > 0 && o < n)
            o += (size_t)snprintf(out + o, n - o, ":borderw=%d:bordercolor=%s",
                                  bw, bcol);
        if (sh > 0 && o < n)
            o += (size_t)snprintf(out + o, n - o,
                                  ":shadowx=%d:shadowy=%d:shadowcolor=0x000000@0.8",
                                  sh, sh);
        if (x->plate > 0.0f && o < n)
            o += (size_t)snprintf(out + o, n - o, ":box=1:boxcolor=%s:boxborderw=%d",
                                  pcol, (int)(x->pad * size + 0.5f));
    }

    return used;
}
