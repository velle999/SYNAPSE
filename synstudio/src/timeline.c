/* timeline.c — the video document, and the ffmpeg graph it turns into.
 *
 * The timeline is a plain description of intent: tracks, clips, in and out
 * points, a transform, a transition, a gain, a grade. It renders NOTHING.
 * Export walks it once and emits an argv for a single ffmpeg invocation,
 * which means the export is one process, one pass, and can be printed and
 * read before it is run — a property worth more than it sounds, because an
 * export that goes wrong at minute forty is diagnosed by looking at the
 * graph, not by bisecting a pipeline of intermediate files.
 *
 * The grade on a clip is the SAME ss_develop a photograph uses. Its pointwise
 * half arrives here as a .cube (see lut.c) applied with lut3d; its spatial
 * half maps onto ffmpeg's own filters. Anything that cannot be expressed
 * either way is not silently dropped — ss_timeline_ffmpeg says so.
 *
 * ── Everything composites onto a black base ────────────────────────────────
 *
 * There is one structural decision here and every feature leans on it: each
 * visible clip is overlaid onto a black canvas the length of the timeline,
 * gated by `enable`. Gaps, overlaps, track order, transforms that push a clip
 * off the edge of the frame, and transitions are then all the same mechanism.
 * In particular a transition needs no second code path — a clip whose alpha
 * rises from zero while the previous clip is still playing underneath IS a
 * cross dissolve.
 *
 * ── Two graph builders, one geometry ───────────────────────────────────────
 *
 * `ss_timeline_ffmpeg` renders the whole thing; `ss_timeline_frame` renders
 * ONE composited frame for the program monitor, from only the clips actually
 * under the playhead. They are separate because their costs are opposite —
 * the export wants a single long pass, the monitor wants to touch as little
 * as possible — but they must AGREE about where a pixel lands, so the two
 * share `xform_at`, which is the only place a transform becomes numbers.
 */
#include "synstudio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------- the clip -- */

void ss_xform_reset(ss_xform *x)
{
    memset(x, 0, sizeof(*x));
    x->scale = x->scale2 = 1.0f;
}

void ss_clip_reset(ss_clip *c)
{
    memset(c, 0, sizeof(*c));
    c->kind    = SS_CLIP_MEDIA;
    c->speed   = 1.0;
    c->opacity = 1.0f;
    c->text_size = 0.08f;
    c->text_r = c->text_g = c->text_b = 1.0f;
    c->text_pos = SS_TEXT_BC;
    ss_xform_reset(&c->xf);
}

double ss_clip_length(const ss_clip *c)
{
    double l = (c->src_out - c->src_in) / (c->speed > 0 ? c->speed : 1.0);
    return l > 0 ? l : 0;
}

/* The transform at a point through the clip, p in 0..1. ONE definition, read
 * by the frame compositor (which evaluates it in C, because a still frame has
 * nothing to animate) and by the export (which hands the two endpoints to
 * zoompan). If a scrub and an export ever disagree about framing, this is the
 * function that is wrong, and it is the only one. */
static void xform_at(const ss_xform *x, double p,
                     float *scale, float *px, float *py, float *rot)
{
    float a;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    a = x->animate ? (float)p : 0.0f;
    *scale = x->scale  + (x->scale2  - x->scale ) * a;
    *px    = x->pos_x  + (x->pos_x2  - x->pos_x ) * a;
    *py    = x->pos_y  + (x->pos_y2  - x->pos_y ) * a;
    *rot   = x->rotate + (x->rotate2 - x->rotate) * a;
    if (*scale < 0.05f) *scale = 0.05f;
    if (*scale > 10.0f) *scale = 10.0f;
}

static int xform_is_identity(const ss_xform *x)
{
    return !x->animate && x->scale == 1.0f && x->pos_x == 0.0f &&
           x->pos_y == 0.0f && x->rotate == 0.0f;
}

/* How much of a clip is showing at time `tt` seconds into it: the fades and
 * the incoming transition, multiplied together with the clip opacity. The
 * export expresses these as filters; the frame compositor needs the number. */
static double alpha_at(const ss_clip *c, double tt, double len)
{
    double a = c->opacity > 0 ? c->opacity : 1.0;
    if (c->fade_in > 0 && tt < c->fade_in)  a *= tt / c->fade_in;
    if (c->fade_out > 0 && tt > len - c->fade_out)
        a *= (len - tt) / c->fade_out;
    if (c->trans != SS_TRANS_NONE && c->trans_dur > 0 && tt < c->trans_dur)
        a *= tt / c->trans_dur;
    return a < 0 ? 0 : (a > 1 ? 1 : a);
}

/* --------------------------------------------------- the clip properties -- */

/* Everything the inspector can change about a clip, in ONE table.
 *
 * The same argument as develop.c's: the CLI's `timeline set`, the GUI panel
 * and the range clamping all read this, so adding a property is adding a row
 * and there is no second list that can quietly disagree about a limit. The
 * enums carry their choices here too, because a drop-down whose options live
 * in the QML is a drop-down that can offer a transition the engine will
 * refuse.
 *
 * What is NOT here: tl_in, src_in, src_out and path. Those are moved by the
 * edit operations, which have to keep two numbers consistent with each other
 * — a head trim changes the in point AND the position — and a setter that
 * writes one field cannot do that. Editing is not a property. */

/* CO_DOUBLE is not fussiness. Four of these fields are `double` on the
 * struct and the rest are `float`, and a table that stored a float through a
 * double's offset writes four bytes of a mantissa and reads back a zero — no
 * warning, no error, the value simply does not stick. That is exactly what
 * `trans.dur` and both fades did until this type existed. */
enum { CO_FLOAT, CO_DOUBLE, CO_INT, CO_ENUM, CO_TEXT };

typedef struct {
    const char *key;
    int         type;
    size_t      off;
    float       lo, hi;
    const char *group, *label, *choices;
} cfield;

#define C(k, t, m, lo, hi, grp, lbl, ch) \
    { k, t, offsetof(ss_clip, m), lo, hi, grp, lbl, ch }

#define TRANS_CHOICES "none|dissolve|wipeleft|wiperight|wipeup|wipedown"
#define POS_CHOICES   "topleft|topcentre|topright|left|centre|right|" \
                      "bottomleft|bottomcentre|bottomright"

static const cfield cfields[] = {
    C("opacity",      CO_FLOAT, opacity,      0.0f,    1.0f, "Levels", "Opacity", NULL),
    C("gain",         CO_FLOAT, gain_db,    -60.0f,   24.0f, "Levels", "Gain (dB)", NULL),
    C("speed",        CO_DOUBLE,speed,        0.1f,   10.0f, "Levels", "Speed", NULL),
    C("fade.in",      CO_DOUBLE,fade_in,      0.0f,   30.0f, "Levels", "Fade in (s)", NULL),
    C("fade.out",     CO_DOUBLE,fade_out,     0.0f,   30.0f, "Levels", "Fade out (s)", NULL),

    C("trans",        CO_ENUM,  trans,        0.0f,    5.0f, "Transition", "Kind", TRANS_CHOICES),
    C("trans.dur",    CO_DOUBLE,trans_dur,    0.0f,   10.0f, "Transition", "Length (s)", NULL),

    C("xform.scale",  CO_FLOAT, xf.scale,     0.05f,  10.0f, "Motion", "Scale", NULL),
    C("xform.x",      CO_FLOAT, xf.pos_x,    -1.0f,    1.0f, "Motion", "Position X", NULL),
    C("xform.y",      CO_FLOAT, xf.pos_y,    -1.0f,    1.0f, "Motion", "Position Y", NULL),
    C("xform.rotate", CO_FLOAT, xf.rotate, -180.0f,  180.0f, "Motion", "Rotation", NULL),
    C("xform.animate",CO_INT,   xf.animate,   0.0f,    1.0f, "Motion", "Animate to", NULL),
    C("xform.scale2", CO_FLOAT, xf.scale2,    0.05f,  10.0f, "Motion", "End scale", NULL),
    C("xform.x2",     CO_FLOAT, xf.pos_x2,   -1.0f,    1.0f, "Motion", "End X", NULL),
    C("xform.y2",     CO_FLOAT, xf.pos_y2,   -1.0f,    1.0f, "Motion", "End Y", NULL),
    C("xform.rotate2",CO_FLOAT, xf.rotate2,-180.0f,  180.0f, "Motion", "End rotation", NULL),

    C("text",         CO_TEXT,  text,         0.0f,    0.0f, "Title", "Caption", NULL),
    C("text.size",    CO_FLOAT, text_size,    0.01f,   0.5f, "Title", "Size", NULL),
    C("text.r",       CO_FLOAT, text_r,       0.0f,    1.0f, "Title", "Red", NULL),
    C("text.g",       CO_FLOAT, text_g,       0.0f,    1.0f, "Title", "Green", NULL),
    C("text.b",       CO_FLOAT, text_b,       0.0f,    1.0f, "Title", "Blue", NULL),
    C("text.pos",     CO_ENUM,  text_pos,     0.0f,    8.0f, "Title", "Placement", POS_CHOICES),

    C("colour.r",     CO_FLOAT, col_r,        0.0f,    1.0f, "Background", "Red", NULL),
    C("colour.g",     CO_FLOAT, col_g,        0.0f,    1.0f, "Background", "Green", NULL),
    C("colour.b",     CO_FLOAT, col_b,        0.0f,    1.0f, "Background", "Blue", NULL),
    C("colour.a",     CO_FLOAT, col_a,        0.0f,    1.0f, "Background", "Opacity", NULL),
};
#undef C

static const int ncfields = (int)(sizeof cfields / sizeof cfields[0]);

int ss_clip_describe(int i, ss_clip_info *out)
{
    if (i < 0 || i >= ncfields) return 0;
    out->key     = cfields[i].key;
    out->group   = cfields[i].group;
    out->label   = cfields[i].label;
    out->lo      = cfields[i].lo;
    out->hi      = cfields[i].hi;
    out->choices = cfields[i].choices;
    /* CO_DOUBLE and CO_FLOAT are the same thing to a caller — a number with
     * a range. The distinction is about where the bytes land, and nothing
     * outside this file has any business knowing it. */
    out->type    = cfields[i].type == CO_INT   ? SS_CT_INT
                 : cfields[i].type == CO_ENUM  ? SS_CT_ENUM
                 : cfields[i].type == CO_TEXT  ? SS_CT_TEXT
                                               : SS_CT_FLOAT;
    return 1;
}

static const cfield *cfind(const char *key)
{
    int i;
    for (i = 0; i < ncfields; i++)
        if (!strcmp(cfields[i].key, key)) return &cfields[i];
    return NULL;
}

/* The enums accept a NAME, and also the number it stands for, because a GUI
 * that has just read `4` out of `ss_clip_get` should be able to hand it
 * straight back without a lookup table of its own. */
static int enum_value(const cfield *f, const char *val)
{
    const char *p = f->choices;
    int idx = 0;
    char *end;
    long n;

    while (p) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (!strncmp(val, p, len) && val[len] == '\0') return idx;
        if (!bar) break;
        p = bar + 1;
        idx++;
    }
    n = strtol(val, &end, 10);
    if (end != val && !*end && n >= (long)f->lo && n <= (long)f->hi) return (int)n;
    return -1;
}

static const char *enum_name(const cfield *f, int v, char *buf, size_t n)
{
    const char *p = f->choices;
    int idx = 0;
    while (p) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (idx == v) {
            if (len >= n) len = n - 1;
            memcpy(buf, p, len);
            buf[len] = '\0';
            return buf;
        }
        if (!bar) break;
        p = bar + 1;
        idx++;
    }
    snprintf(buf, n, "%d", v);
    return buf;
}

int ss_clip_set(ss_clip *c, const char *key, const char *val)
{
    const cfield *f = cfind(key);
    void *p;

    if (!f || !val) return -1;
    p = (char *)c + f->off;

    switch (f->type) {
    case CO_TEXT:
        snprintf((char *)p, sizeof c->text, "%s", val);
        return 0;
    case CO_ENUM: {
        int v = enum_value(f, val);
        if (v < 0) return -1;
        *(int *)p = v;
        return 0;
    }
    case CO_INT: {
        double d = atof(val);
        if (d < f->lo || d > f->hi) return -2;
        *(int *)p = (int)d;
        return 0;
    }
    case CO_DOUBLE: {
        double d = atof(val);
        if (d < f->lo || d > f->hi) return -2;
        *(double *)p = d;
        return 0;
    }
    default: {
        double d = atof(val);
        if (d < f->lo || d > f->hi) return -2;
        *(float *)p = (float)d;
        return 0;
    }
    }
}

int ss_clip_get(const ss_clip *c, const char *key, char *out, size_t n)
{
    const cfield *f = cfind(key);
    const void *p;

    if (!f) return -1;
    p = (const char *)c + f->off;

    switch (f->type) {
    case CO_TEXT: snprintf(out, n, "%s", (const char *)p); return 0;
    case CO_ENUM: { char b[32]; enum_name(f, *(const int *)p, b, sizeof b);
                    snprintf(out, n, "%s", b); return 0; }
    case CO_INT:    snprintf(out, n, "%d", *(const int *)p); return 0;
    case CO_DOUBLE: snprintf(out, n, "%.6g", *(const double *)p); return 0;
    default:        snprintf(out, n, "%.6g", (double)*(const float *)p); return 0;
    }
}

/* ---------------------------------------------------------- the document -- */

void ss_timeline_free(ss_timeline *t)
{
    int i;
    for (i = 0; i < t->ntracks; i++) {
        free(t->track[i].clip);
        t->track[i].clip = NULL;
        t->track[i].nclips = t->track[i].cap = 0;
    }
    t->ntracks = 0;
}

void ss_timeline_reset(ss_timeline *t, int w, int h, double fps)
{
    memset(t, 0, sizeof(*t));
    snprintf(t->name, sizeof t->name, "Untitled");
    t->w = w > 0 ? w : 1920;
    t->h = h > 0 ? h : 1080;
    t->fps = fps > 0 ? fps : 25.0;
}

int ss_timeline_add_track(ss_timeline *t, int type, const char *name)
{
    ss_track *tr;
    if (t->ntracks >= SS_MAX_TRACKS) return -1;
    tr = &t->track[t->ntracks];
    memset(tr, 0, sizeof(*tr));
    tr->type = type;
    snprintf(tr->name, sizeof tr->name, "%s",
             name && *name ? name : (type == SS_TRACK_VIDEO ? "V" : "A"));
    return t->ntracks++;
}

int ss_timeline_add_clip(ss_timeline *t, int track, const ss_clip *c)
{
    ss_track *tr;

    if (track < 0 || track >= t->ntracks) return -1;
    tr = &t->track[track];

    if (tr->nclips >= tr->cap) {
        int ncap = tr->cap ? tr->cap * 2 : 8;
        ss_clip *nc = realloc(tr->clip, sizeof(ss_clip) * (size_t)ncap);
        if (!nc) return -1;
        tr->clip = nc;
        tr->cap = ncap;
    }
    tr->clip[tr->nclips] = *c;
    if (tr->clip[tr->nclips].speed <= 0.0) tr->clip[tr->nclips].speed = 1.0;
    if (tr->clip[tr->nclips].opacity <= 0.0f) tr->clip[tr->nclips].opacity = 1.0f;
    if (tr->clip[tr->nclips].xf.scale <= 0.0f) ss_xform_reset(&tr->clip[tr->nclips].xf);
    return tr->nclips++;
}

double ss_timeline_duration(const ss_timeline *t)
{
    double end = 0;
    int i, j;
    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++) {
            const ss_clip *c = &t->track[i].clip[j];
            double e = c->tl_in + ss_clip_length(c);
            if (e > end) end = e;
        }
    return end;
}

/* ----------------------------------------------------------- the editing -- */

static ss_clip *clip_ptr(ss_timeline *t, int track, int clip)
{
    if (track < 0 || track >= t->ntracks) return NULL;
    if (clip < 0 || clip >= t->track[track].nclips) return NULL;
    return &t->track[track].clip[clip];
}

int ss_timeline_at(const ss_timeline *t, int track, double time)
{
    int j, hit = -1;
    if (track < 0 || track >= t->ntracks) return -1;
    for (j = 0; j < t->track[track].nclips; j++) {
        const ss_clip *c = &t->track[track].clip[j];
        double len = ss_clip_length(c);
        if (len <= 0) continue;
        if (time >= c->tl_in && time < c->tl_in + len) hit = j;
    }
    return hit;
}

int ss_timeline_move(ss_timeline *t, int track, int clip, double tl_in)
{
    ss_clip *c = clip_ptr(t, track, clip);
    if (!c) return -1;
    c->tl_in = tl_in < 0 ? 0 : tl_in;
    return 0;
}

int ss_timeline_trim(ss_timeline *t, int track, int clip, int which, double delta)
{
    ss_clip *c = clip_ptr(t, track, clip);
    double sp;
    if (!c) return -1;
    sp = c->speed > 0 ? c->speed : 1.0;

    if (which < 0) {
        /* A head trim moves the source in point and the timeline position by
         * the same amount of TIMELINE time. Moving only the in point would
         * slide the shot's content out from under the cut; moving only the
         * position would leave a gap and re-time nothing. Both, together, is
         * what makes the frame under the cursor stay where it is. */
        double d = delta;
        if (c->src_in + d * sp < 0) d = -c->src_in / sp;
        if (c->src_out - (c->src_in + d * sp) <= 0) return -1;
        if (c->tl_in + d < 0) d = -c->tl_in;
        c->src_in += d * sp;
        c->tl_in  += d;
        /* An incoming transition longer than what is left of the clip would
         * never finish ramping in, so it follows the edge down. */
        if (c->trans_dur > ss_clip_length(c)) c->trans_dur = ss_clip_length(c);
    } else {
        double no = c->src_out + delta * sp;
        if (no <= c->src_in) return -1;
        c->src_out = no;
    }
    return 0;
}

int ss_timeline_split(ss_timeline *t, int track, int clip, double at)
{
    ss_clip *c = clip_ptr(t, track, clip), a, b, orig;
    double len, off, sp;
    int n;

    if (!c) return -1;
    orig = *c;
    len = ss_clip_length(c);
    if (len <= 0) return -1;
    off = at - c->tl_in;
    /* Strictly inside. A razor landing exactly on an edge is a no-op, not a
     * zero-length clip nobody can see and nobody can select. */
    if (off <= 0 || off >= len) return -1;

    sp = c->speed > 0 ? c->speed : 1.0;
    a = b = *c;

    a.src_out = c->src_in + off * sp;
    a.fade_out = 0;

    b.src_in  = a.src_out;
    b.tl_in   = c->tl_in + off;
    b.fade_in = 0;
    /* A transition into the second half would be a dissolve starting in the
     * middle of a continuous shot, which is never what a razor meant. */
    b.trans     = SS_TRANS_NONE;
    b.trans_dur = 0;
    /* An animated transform is cut in two along with the picture, so the move
     * continues across the cut instead of restarting at each half. */
    if (a.xf.animate) {
        float s, px, py, r;
        xform_at(&c->xf, off / len, &s, &px, &py, &r);
        a.xf.scale2 = s;  a.xf.pos_x2 = px; a.xf.pos_y2 = py; a.xf.rotate2 = r;
        b.xf.scale  = s;  b.xf.pos_x  = px; b.xf.pos_y  = py; b.xf.rotate  = r;
    }

    *c = a;
    n = ss_timeline_add_clip(t, track, &b);
    if (n < 0) {
        /* `c` cannot be reused here. Adding a clip GROWS the track's array,
         * and a realloc that moved it leaves this pointer dangling — writing
         * the rollback through it would be a use-after-free on the one path
         * that is already having a bad day. Ask for the clip again. */
        ss_clip *back = clip_ptr(t, track, clip);
        if (back) *back = orig;
        return -1;
    }
    return n;
}

int ss_timeline_remove(ss_timeline *t, int track, int clip)
{
    ss_track *tr;
    if (!clip_ptr(t, track, clip)) return -1;
    tr = &t->track[track];
    memmove(&tr->clip[clip], &tr->clip[clip + 1],
            sizeof(ss_clip) * (size_t)(tr->nclips - clip - 1));
    tr->nclips--;
    return 0;
}

void ss_timeline_ripple(ss_timeline *t, int track, double from, double len)
{
    int j;
    if (track < 0 || track >= t->ntracks || len <= 0) return;
    for (j = 0; j < t->track[track].nclips; j++) {
        ss_clip *c = &t->track[track].clip[j];
        if (c->tl_in >= from) {
            c->tl_in -= len;
            if (c->tl_in < from) c->tl_in = from;
        }
    }
}

/* -------------------------------------------------------- serialisation -- */

static const char *trans_name(int v)
{
    switch (v) {
    case SS_TRANS_DISSOLVE: return "dissolve";
    case SS_TRANS_WIPE_L:   return "wipeleft";
    case SS_TRANS_WIPE_R:   return "wiperight";
    case SS_TRANS_WIPE_U:   return "wipeup";
    case SS_TRANS_WIPE_D:   return "wipedown";
    default:                return "none";
    }
}

int ss_trans_value(const char *s)
{
    if (!s) return -1;
    if (!strcmp(s, "none"))      return SS_TRANS_NONE;
    if (!strcmp(s, "dissolve"))  return SS_TRANS_DISSOLVE;
    if (!strcmp(s, "wipeleft"))  return SS_TRANS_WIPE_L;
    if (!strcmp(s, "wiperight")) return SS_TRANS_WIPE_R;
    if (!strcmp(s, "wipeup"))    return SS_TRANS_WIPE_U;
    if (!strcmp(s, "wipedown"))  return SS_TRANS_WIPE_D;
    return -1;
}

static const char *kind_name(int v)
{
    return v == SS_CLIP_TITLE ? "title" : v == SS_CLIP_SOLID ? "solid" : "media";
}

int ss_clip_kind_value(const char *s)
{
    if (!s) return -1;
    if (!strcmp(s, "media")) return SS_CLIP_MEDIA;
    if (!strcmp(s, "title")) return SS_CLIP_TITLE;
    if (!strcmp(s, "solid")) return SS_CLIP_SOLID;
    return -1;
}

static const char *textpos_name(int v)
{
    static const char *n[] = { "topleft", "topcentre", "topright",
                               "left", "centre", "right",
                               "bottomleft", "bottomcentre", "bottomright" };
    return (v >= 0 && v < 9) ? n[v] : "bottomcentre";
}

int ss_textpos_value(const char *s)
{
    int i;
    static const char *n[] = { "topleft", "topcentre", "topright",
                               "left", "centre", "right",
                               "bottomleft", "bottomcentre", "bottomright" };
    if (!s) return -1;
    for (i = 0; i < 9; i++) if (!strcmp(s, n[i])) return i;
    return -1;
}

/* Only what differs from the default is written. A timeline of plain cuts
 * stays as readable as it was before transforms and titles existed, and a
 * file written by an older build still loads — every added line is optional
 * and its absence means the default. */
int ss_timeline_write(const ss_timeline *t, FILE *fp)
{
    int i, j;

    fprintf(fp, "# synstudio timeline\n");
    fprintf(fp, "name\t%s\n", t->name);
    fprintf(fp, "size\t%d\t%d\n", t->w, t->h);
    fprintf(fp, "fps\t%.6g\n", t->fps);

    for (i = 0; i < t->ntracks; i++) {
        const ss_track *tr = &t->track[i];
        fprintf(fp, "track\t%s\t%s\t%d\t%d\n",
                tr->type == SS_TRACK_VIDEO ? "video" : "audio",
                tr->name, tr->muted, tr->hidden);
        for (j = 0; j < tr->nclips; j++) {
            const ss_clip *c = &tr->clip[j];
            fprintf(fp, "clip\t%.6f\t%.6f\t%.6f\t%.6f\t%.4f\t%.4f\t%.6f\t%.6f\t%s\n",
                    c->tl_in, c->src_in, c->src_out, c->speed,
                    c->gain_db, c->opacity, c->fade_in, c->fade_out, c->path);
            if (c->kind != SS_CLIP_MEDIA || c->still)
                fprintf(fp, "kind\t%s\t%d\n", kind_name(c->kind), c->still);
            if (!xform_is_identity(&c->xf))
                fprintf(fp, "xform\t%.5f\t%.5f\t%.5f\t%.4f\t%d\t%.5f\t%.5f\t%.5f\t%.4f\n",
                        c->xf.scale, c->xf.pos_x, c->xf.pos_y, c->xf.rotate,
                        c->xf.animate,
                        c->xf.scale2, c->xf.pos_x2, c->xf.pos_y2, c->xf.rotate2);
            if (c->trans != SS_TRANS_NONE)
                fprintf(fp, "trans\t%s\t%.6f\n", trans_name(c->trans), c->trans_dur);
            if (c->kind == SS_CLIP_SOLID || c->col_a > 0.0f)
                fprintf(fp, "solid\t%.4f\t%.4f\t%.4f\t%.4f\n",
                        c->col_r, c->col_g, c->col_b, c->col_a);
            if (c->kind == SS_CLIP_TITLE)
                fprintf(fp, "text\t%.4f\t%.4f\t%.4f\t%.4f\t%s\t%s\n",
                        c->text_size, c->text_r, c->text_g, c->text_b,
                        textpos_name(c->text_pos), c->text);
            if (c->has_grade) {
                /* Indented so the reader can tell a grade line belongs to the
                 * clip above it without needing a nesting syntax. */
                fprintf(fp, "grade\t%d\n", j);
                ss_develop_write(&c->grade, fp);
                fprintf(fp, "endgrade\n");
            }
        }
    }
    return ferror(fp) ? -1 : 0;
}

/* Split a tab-separated line into at most `max` fields IN PLACE. Returns the
 * count. The last field keeps any tabs that follow it, which is how a path or
 * a caption containing whitespace survives the round trip. */
static int tabsplit(char *s, char **f, int max)
{
    int n = 0;
    char *tab;
    while (n < max - 1 && (tab = strchr(s, '\t'))) {
        *tab = '\0';
        f[n++] = s;
        s = tab + 1;
    }
    f[n++] = s;
    return n;
}

int ss_timeline_read(ss_timeline *t, FILE *fp)
{
    char line[4096];
    int cur_track = -1, cur_clip = -1;

    ss_timeline_free(t);
    ss_timeline_reset(t, 1920, 1080, 25.0);

    while (fgets(line, sizeof line, fp)) {
        char *nl = strchr(line, '\n');
        ss_clip *cc;
        if (nl) *nl = '\0';
        if (line[0] == '#' || !line[0]) continue;

        cc = (cur_track >= 0 && cur_clip >= 0)
             ? &t->track[cur_track].clip[cur_clip] : NULL;

        if (!strncmp(line, "name\t", 5)) {
            /* The precision, not a bare %s. snprintf truncates safely either
             * way, but at -O3 gcc can see a 4091-byte source going into 256
             * bytes and warns; saying the bound out loud both silences it and
             * records that the truncation is intended. A project name longer
             * than 255 bytes is pathological, and losing its tail is the right
             * answer to it. */
            snprintf(t->name, sizeof t->name, "%.*s",
                     (int)(sizeof t->name - 1), line + 5);
        } else if (!strncmp(line, "size\t", 5)) {
            sscanf(line + 5, "%d %d", &t->w, &t->h);
        } else if (!strncmp(line, "fps\t", 4)) {
            t->fps = atof(line + 4);
        } else if (!strncmp(line, "track\t", 6)) {
            char kind[16] = "video", nm[64] = "";
            int muted = 0, hidden = 0;
            sscanf(line + 6, "%15s %63s %d %d", kind, nm, &muted, &hidden);
            cur_track = ss_timeline_add_track(
                t, strcmp(kind, "audio") ? SS_TRACK_VIDEO : SS_TRACK_AUDIO, nm);
            if (cur_track >= 0) {
                t->track[cur_track].muted = muted;
                t->track[cur_track].hidden = hidden;
            }
            cur_clip = -1;
        } else if (!strncmp(line, "clip\t", 5)) {
            ss_clip c;
            char *f[9];
            /* Fields are tab separated and the LAST one is the path, which
             * may contain spaces. Split on tabs, never on whitespace. */
            if (tabsplit(line + 5, f, 9) < 9) continue;
            ss_clip_reset(&c);
            c.tl_in = atof(f[0]); c.src_in = atof(f[1]); c.src_out = atof(f[2]);
            c.speed = atof(f[3]); c.gain_db = (float)atof(f[4]);
            c.opacity = (float)atof(f[5]);
            c.fade_in = atof(f[6]); c.fade_out = atof(f[7]);
            snprintf(c.path, sizeof c.path, "%s", f[8]);
            cur_clip = ss_timeline_add_clip(t, cur_track, &c);
        } else if (!strncmp(line, "kind\t", 5) && cc) {
            char *f[2];
            int n = tabsplit(line + 5, f, 2), k;
            k = ss_clip_kind_value(f[0]);
            if (k >= 0) cc->kind = k;
            if (n > 1) cc->still = atoi(f[1]);
        } else if (!strncmp(line, "xform\t", 6) && cc) {
            char *f[9];
            if (tabsplit(line + 6, f, 9) == 9) {
                cc->xf.scale  = (float)atof(f[0]);
                cc->xf.pos_x  = (float)atof(f[1]);
                cc->xf.pos_y  = (float)atof(f[2]);
                cc->xf.rotate = (float)atof(f[3]);
                cc->xf.animate = atoi(f[4]);
                cc->xf.scale2  = (float)atof(f[5]);
                cc->xf.pos_x2  = (float)atof(f[6]);
                cc->xf.pos_y2  = (float)atof(f[7]);
                cc->xf.rotate2 = (float)atof(f[8]);
            }
        } else if (!strncmp(line, "trans\t", 6) && cc) {
            char *f[2];
            int v;
            if (tabsplit(line + 6, f, 2) == 2 &&
                (v = ss_trans_value(f[0])) >= 0) {
                cc->trans = v;
                cc->trans_dur = atof(f[1]);
            }
        } else if (!strncmp(line, "solid\t", 6) && cc) {
            char *f[4];
            if (tabsplit(line + 6, f, 4) == 4) {
                cc->col_r = (float)atof(f[0]); cc->col_g = (float)atof(f[1]);
                cc->col_b = (float)atof(f[2]); cc->col_a = (float)atof(f[3]);
            }
        } else if (!strncmp(line, "text\t", 5) && cc) {
            char *f[6];
            int v;
            if (tabsplit(line + 5, f, 6) == 6) {
                cc->text_size = (float)atof(f[0]);
                cc->text_r = (float)atof(f[1]);
                cc->text_g = (float)atof(f[2]);
                cc->text_b = (float)atof(f[3]);
                if ((v = ss_textpos_value(f[4])) >= 0) cc->text_pos = v;
                snprintf(cc->text, sizeof cc->text, "%s", f[5]);
            }
        } else if (!strncmp(line, "grade\t", 6)) {
            /* Read the develop block up to endgrade into a memory stream so
             * ss_develop_read, which takes a FILE*, needs no second parser. */
            char buf[16384];
            size_t used = 0;
            FILE *ms;
            while (fgets(line, sizeof line, fp)) {
                if (!strncmp(line, "endgrade", 8)) break;
                if (used + strlen(line) + 1 >= sizeof buf) break;
                strcpy(buf + used, line);
                used += strlen(line);
            }
            buf[used] = '\0';
            if (cc) {
                ms = fmemopen(buf, used, "r");
                if (ms) {
                    ss_develop_read(&cc->grade, ms);
                    fclose(ms);
                    cc->has_grade = 1;
                }
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------- the ffmpeg export -- */

typedef struct { char *s; size_t len, cap; } strbuf;

static int sb_add(strbuf *b, const char *fmt, ...)
{
    va_list ap;
    int n;

    for (;;) {
        size_t space = b->cap - b->len;
        va_start(ap, fmt);
        n = vsnprintf(b->s + b->len, space, fmt, ap);
        va_end(ap);
        if (n < 0) return -1;
        if ((size_t)n < space) { b->len += (size_t)n; return 0; }
        {
            size_t want = b->cap ? b->cap * 2 : 4096;
            char *ns;
            while (want < b->len + (size_t)n + 1) want *= 2;
            ns = realloc(b->s, want);
            if (!ns) return -1;
            b->s = ns; b->cap = want;
        }
    }
}

/* ffmpeg's filter argument syntax gives \ : ' and , their own meanings, so a
 * LUT path containing any of them has to be escaped or the graph fails to
 * parse with a message about an unknown option. */
static void esc_filter(const char *in, char *out, size_t n)
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

static char *xdup(const char *s)
{
    char *p = malloc(strlen(s) + 1);
    if (p) strcpy(p, s);
    return p;
}

static char *xfmt(const char *fmt, ...)
{
    va_list ap;
    char buf[4096];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return xdup(buf);
}

static void hexcol(float r, float g, float b, float a, char *out, size_t n)
{
    int ri = (int)(ss_clampf(r, 0, 1) * 255.0f + 0.5f);
    int gi = (int)(ss_clampf(g, 0, 1) * 255.0f + 0.5f);
    int bi = (int)(ss_clampf(b, 0, 1) * 255.0f + 0.5f);
    snprintf(out, n, "0x%02X%02X%02X@%.3f", ri, gi, bi, (double)ss_clampf(a, 0, 1));
}

/* The side files a graph refers to: one .cube per graded clip, and one text
 * file per title.
 *
 * The caption goes in a FILE rather than into `text=`. drawtext's argument is
 * parsed twice — once by the filtergraph splitter and once by drawtext's own
 * expansion — so a colon, a percent sign, a backslash or an apostrophe in a
 * caption needs escaping that differs between the two passes, and getting it
 * wrong fails at parse time with a message about the graph, not the caption.
 * `textfile=` has one level of quoting and holds whatever bytes are in it. */
int ss_timeline_bake(const ss_timeline *t, const char *dir)
{
    int i, j, n = 0;

    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++) {
            const ss_clip *c = &t->track[i].clip[j];
            char p[4300];
            FILE *fp;
            if (c->has_grade) {
                snprintf(p, sizeof p, "%s/grade_%d_%d.cube", dir, i, j);
                fp = fopen(p, "w");
                if (!fp) return -1;
                ss_lut_write(&c->grade, 33, fp, "synstudio clip grade");
                fclose(fp);
                n++;
            }
            if (c->kind == SS_CLIP_TITLE) {
                snprintf(p, sizeof p, "%s/text_%d_%d.txt", dir, i, j);
                fp = fopen(p, "w");
                if (!fp) return -1;
                fputs(c->text, fp);
                fclose(fp);
                n++;
            }
        }
    return n;
}

void ss_timeline_unbake(const ss_timeline *t, const char *dir)
{
    int i, j;
    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++) {
            char p[4300];
            snprintf(p, sizeof p, "%s/grade_%d_%d.cube", dir, i, j);
            remove(p);
            snprintf(p, sizeof p, "%s/text_%d_%d.txt", dir, i, j);
            remove(p);
        }
}

/* ------------------------------------------------------------ the chain -- */

/* Does this clip's chain need an alpha channel? Asked rather than assumed
 * because an RGBA pipeline is a third more memory per frame all the way down
 * the graph, and most cuts are opaque. A fade to BLACK does not need one —
 * only a transform that can leave the frame, a partial opacity, and a
 * transition, which is a fade to whatever is underneath. */
static int needs_alpha(const ss_clip *c)
{
    return c->opacity < 1.0f || c->trans != SS_TRANS_NONE ||
           c->xf.rotate != 0.0f || c->xf.rotate2 != 0.0f ||
           c->kind == SS_CLIP_TITLE;
}

/* Round to an even size: a yuv420 intermediate with an odd dimension makes
 * ffmpeg either refuse the graph or silently pick its own number. */
static void fitted_size(const ss_timeline *t, float scale, int *w, int *h)
{
    int a = (int)(t->w * scale + 0.5f), b = (int)(t->h * scale + 0.5f);
    if (a < 2) a = 2;
    if (b < 2) b = 2;
    *w = a & ~1;
    *h = b & ~1;
}

/* The nine title positions as drawtext x/y expressions. `w`/`h` are the frame
 * and `text_w`/`text_h` the caption box, both drawtext's own variables. */
static void text_xy(int pos, char *x, size_t xn, char *y, size_t yn)
{
    static const char *xs[3] = { "(w*0.06)", "((w-text_w)/2)", "(w-text_w-w*0.06)" };
    static const char *ys[3] = { "(h*0.06)", "((h-text_h)/2)", "(h-text_h-h*0.06)" };
    if (pos < 0 || pos > 8) pos = SS_TEXT_BC;
    snprintf(x, xn, "%s", xs[pos % 3]);
    snprintf(y, yn, "%s", ys[pos / 3]);
}

/* The grade's spatial half, which a 3D LUT cannot carry because every one of
 * these needs a neighbouring pixel. Shared by both graph builders so a scrub
 * and an export sharpen by the same amount. */
static void chain_spatial(strbuf *fc, const ss_develop *d)
{
    if (d->sharpen > 0.0f) {
        int lsz = (int)(d->sharpen_radius * 2) * 2 + 3;
        if (lsz < 3) lsz = 3;
        if (lsz > 23) lsz = 23;
        sb_add(fc, ",unsharp=%d:%d:%.3f", lsz, lsz, d->sharpen / 100.0f);
    }
    if (d->vignette < 0.0f)
        sb_add(fc, ",vignette=angle=%.4f", (double)(-d->vignette / 100.0f) * 1.2);
    if (d->nr_luma > 0.0f || d->nr_chroma > 0.0f)
        sb_add(fc, ",hqdn3d=%.2f:%.2f:%.2f:%.2f",
               d->nr_luma / 25.0f, d->nr_chroma / 25.0f,
               d->nr_luma / 16.0f, d->nr_chroma / 16.0f);
    if (d->crop.on)
        sb_add(fc, ",crop=iw*%.5f:ih*%.5f:iw*%.5f:ih*%.5f",
               d->crop.w, d->crop.h, d->crop.x, d->crop.y);
}

static void chain_grade(strbuf *fc, const ss_clip *c, const char *lutdir,
                        int track, int idx)
{
    char lp[2048], esc[4200];
    if (!c->has_grade) return;
    snprintf(lp, sizeof lp, "%s/grade_%d_%d.cube", lutdir, track, idx);
    esc_filter(lp, esc, sizeof esc);
    sb_add(fc, ",lut3d=file='%s':interp=tetrahedral", esc);
    chain_spatial(fc, &c->grade);
}

/* A font FILE, not drawtext's `font=Sans`. That option only works in an
 * ffmpeg built against fontconfig, and when it is missing the failure is a
 * graph that will not parse — at export time, after the edit. A path either
 * exists or it does not, and this checks. */
static const char *title_font(void)
{
    static const char *cand[] = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
        NULL
    };
    static const char *found;
    int i;
    if (found) return found;
    for (i = 0; cand[i]; i++) {
        FILE *fp = fopen(cand[i], "rb");
        if (fp) { fclose(fp); found = cand[i]; return found; }
    }
    found = cand[0];            /* say which one is missing, rather than nothing */
    return found;
}

/* A title's caption, drawn over whatever the clip already is. The border is
 * not decoration: white text lands on a white sky often enough that a caption
 * without one is unreadable on the take you most wanted to label. */
static void chain_title(strbuf *fc, const ss_timeline *t, const ss_clip *c,
                        const char *dir, int track, int idx)
{
    char tp[2048], esc[4200], fesc[1024], x[64], y[64], col[32];
    if (c->kind != SS_CLIP_TITLE) return;
    snprintf(tp, sizeof tp, "%s/text_%d_%d.txt", dir, track, idx);
    esc_filter(tp, esc, sizeof esc);
    esc_filter(title_font(), fesc, sizeof fesc);
    text_xy(c->text_pos, x, sizeof x, y, sizeof y);
    hexcol(c->text_r, c->text_g, c->text_b, 1.0f, col, sizeof col);
    /* expansion=none. `textfile=` gets the caption past the filtergraph's
     * quoting, but drawtext STILL runs its own %%{...} expansion over whatever
     * it read, so a caption containing a percent sign fails the graph with
     * "Stray %%" — at export time, long after the title was typed. Nothing
     * here wants a strftime, and a caption is literal text by definition. */
    sb_add(fc, ",drawtext=fontfile='%s':textfile='%s':expansion=none"
               ":fontcolor=%s:fontsize=%d"
               ":x=%s:y=%s:borderw=%d:bordercolor=0x000000@0.65:line_spacing=%d",
           fesc, esc, col,
           (int)(c->text_size * t->h + 0.5f), x, y,
           (int)(c->text_size * t->h * 0.045f + 1.5f),
           (int)(c->text_size * t->h * 0.25f));
}

/* The transition, as an alpha ramp over the head of the clip. `dur` is its
 * length; `T` is drawtext-style stream time, which at this point in the chain
 * is still zero-based within the clip because the setpts that offsets it onto
 * the timeline comes afterwards. Order matters and is not incidental. */
static void chain_transition(strbuf *fc, const ss_clip *c)
{
    const double f = 0.12;      /* soft edge, as a fraction of the frame */
    const char *num = NULL;
    if (c->trans == SS_TRANS_NONE || c->trans_dur <= 0) return;

    if (c->trans == SS_TRANS_DISSOLVE) {
        sb_add(fc, ",format=rgba,fade=t=in:st=0:d=%.4f:alpha=1", c->trans_dur);
        return;
    }
    switch (c->trans) {
    case SS_TRANS_WIPE_L: num = "(X/W)";       break;
    case SS_TRANS_WIPE_R: num = "(1-X/W)";     break;
    case SS_TRANS_WIPE_U: num = "(Y/H)";       break;
    case SS_TRANS_WIPE_D: num = "(1-Y/H)";     break;
    default: return;
    }
    /* geq wants a planar RGB layout with alpha before it will hand out r/g/b/a
     * at all; naming the format is cheaper than discovering that the graph
     * negotiated its way to a plane the expression cannot see. */
    sb_add(fc, ",format=gbrap,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)'"
               ":a='255*clip((min(T/%.4f,1)*%.4f-%s)/%.4f,0,1)'",
           c->trans_dur, 1.0 + f, num, f);
}

/* ---------------------------------------------------------- the inputs -- */

/* A clip's ffmpeg input. Generated clips are real lavfi inputs rather than
 * sources spliced into the filter graph, so the input index of every clip is
 * simply its position in this walk and nothing downstream has to know which
 * kinds own a file. */
#define PUSH(x) do { \
        if (ac + 2 > cap) { \
            char **na = realloc(av, sizeof(char *) * (cap *= 2)); \
            if (!na) goto fail; \
            av = na; \
        } \
        av[ac] = (x); \
        if (!av[ac]) goto fail; \
        ac++; \
    } while (0)

int ss_timeline_ffmpeg(const ss_timeline *t, const char *out,
                       const char *lutdir, int preview, char ***argv_out)
{
    strbuf fc = {0};
    char **av = NULL;
    int ac = 0, cap = 64;
    int i, j, input = 0, nvid = 0, naud = 0;
    double dur = ss_timeline_duration(t);

    av = malloc(sizeof(char *) * cap);
    if (!av) return -1;

    PUSH(xdup("ffmpeg"));
    PUSH(xdup("-v")); PUSH(xdup("error"));
    PUSH(xdup("-stats"));
    PUSH(xdup("-y"));

    /* One -i per clip. Seeking with -ss BEFORE -i is a keyframe seek and is
     * the only form that does not decode the whole file up to the in point;
     * the accurate-seek cost is paid by the trim in the graph instead. */
    for (i = 0; i < t->ntracks; i++) {
        const ss_track *tr = &t->track[i];
        if (tr->hidden || tr->muted) continue;
        for (j = 0; j < tr->nclips; j++) {
            const ss_clip *c = &tr->clip[j];
            double srclen = c->src_out - c->src_in;
            if (ss_clip_length(c) <= 0) continue;

            if (c->kind != SS_CLIP_MEDIA) {
                char col[32];
                hexcol(c->col_r, c->col_g, c->col_b,
                       c->kind == SS_CLIP_SOLID ? 1.0f : c->col_a, col, sizeof col);
                PUSH(xdup("-f")); PUSH(xdup("lavfi"));
                PUSH(xdup("-i"));
                /* The `,format=rgba` is doing real work. Read as an INPUT,
                 * lavfi has no downstream filter to negotiate a format with
                 * and settles on an opaque one, so a title's transparent
                 * backdrop arrives as solid black and the caption blanks out
                 * the shot it was labelling. Inside the input's own graph
                 * there is something to negotiate with. */
                PUSH(xfmt("color=c=%s:s=%dx%d:r=%.6g:d=%.6f,format=rgba",
                          col, t->w, t->h, t->fps, srclen));
            } else if (c->still) {
                /* A photograph decodes to ONE frame no matter what -t says.
                 * Without -loop the clip contributes a single frame to a graph
                 * that was told to expect seconds of them, and the export
                 * finishes early with no error — which is exactly what a
                 * still on this timeline used to do. */
                PUSH(xdup("-loop")); PUSH(xdup("1"));
                PUSH(xdup("-t")); PUSH(xfmt("%.6f", srclen));
                PUSH(xdup("-i")); PUSH(xdup(c->path));
            } else {
                if (c->src_in > 0.0) { PUSH(xdup("-ss")); PUSH(xfmt("%.6f", c->src_in)); }
                PUSH(xdup("-t")); PUSH(xfmt("%.6f", srclen));
                PUSH(xdup("-i")); PUSH(xdup(c->path));
            }
        }
    }

    /* A black base the length of the timeline. Every video clip is composited
     * onto it, which is what makes gaps, overlaps and track order all work
     * without a special case for any of them. */
    if (sb_add(&fc, "color=c=black:s=%dx%d:r=%.6g:d=%.6f[base]",
               t->w, t->h, t->fps, dur > 0 ? dur : 1.0) != 0) goto fail;

    input = 0;
    for (i = 0; i < t->ntracks; i++) {
        const ss_track *tr = &t->track[i];
        if (tr->hidden || tr->muted) continue;
        for (j = 0; j < tr->nclips; j++) {
            const ss_clip *c = &tr->clip[j];
            double len = ss_clip_length(c);
            if (len <= 0) continue;

            if (tr->type == SS_TRACK_VIDEO) {
                float s0, px0, py0, r0, s1, px1, py1, r1;
                int fw, fh;

                xform_at(&c->xf, 0.0, &s0, &px0, &py0, &r0);
                xform_at(&c->xf, 1.0, &s1, &px1, &py1, &r1);

                sb_add(&fc, ";[%d:v]", input);
                if (needs_alpha(c)) sb_add(&fc, "format=rgba,");

                if (c->xf.animate) {
                    /* An animated framing cannot be a scale, whose output size
                     * is fixed for the life of the filter. zoompan is the one
                     * filter that re-frames per output frame; it only ever
                     * zooms IN, so the source is fitted generously first and
                     * the move happens inside that. `on` counts output frames,
                     * which is what makes this work over a video clip and not
                     * just a still. */
                    double zs = s0 < 1.0f ? 1.0 : s0, ze = s1 < 1.0f ? 1.0 : s1;
                    double nfr = len * t->fps;
                    if (nfr < 2) nfr = 2;
                    fitted_size(t, (float)(zs > ze ? zs : ze), &fw, &fh);
                    sb_add(&fc, "scale=%d:%d:force_original_aspect_ratio=decrease"
                                ",pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black",
                           fw, fh, fw, fh);
                    /* Divided by nfr, NOT nfr-1. `on` is a frame index, so
                     * on/nfr is the frame's TIME as a fraction of the clip —
                     * which is exactly the `p` the monitor hands xform_at.
                     * Dividing by nfr-1 instead makes the move arrive one
                     * frame early, and the monitor and the export then
                     * disagree about framing by a frame's worth of zoom for
                     * the whole length of the move. Measured: 23 dB. */
                    sb_add(&fc, ",zoompan=z='%.5f+(%.5f)*on/%.4f'"
                                ":x='iw/2-(iw/zoom/2)+iw*(%.5f+(%.5f)*on/%.4f)'"
                                ":y='ih/2-(ih/zoom/2)+ih*(%.5f+(%.5f)*on/%.4f)'"
                                ":d=1:s=%dx%d:fps=%.6g",
                           zs, ze - zs, nfr,
                           (double)px0, (double)(px1 - px0), nfr,
                           (double)py0, (double)(py1 - py0), nfr,
                           t->w, t->h, t->fps);
                } else {
                    /* Scale into the project frame, letterboxing rather than
                     * distorting: a clip shot in a different aspect is a
                     * framing decision, and stretching it is never the one
                     * intended. The position is applied by the overlay below
                     * rather than by a pad, so a clip pushed past the edge is
                     * clipped instead of failing the graph. */
                    fitted_size(t, s0, &fw, &fh);
                    sb_add(&fc, "scale=%d:%d:force_original_aspect_ratio=decrease",
                           fw, fh);
                    if (r0 != 0.0f)
                        sb_add(&fc, ",rotate=%.6f:ow='hypot(iw,ih)':oh='hypot(iw,ih)'"
                                    ":c=black@0", (double)r0 * M_PI / 180.0);
                }
                sb_add(&fc, ",fps=%.6g", t->fps);
                if (c->speed != 1.0)
                    sb_add(&fc, ",setpts=%.6f*PTS", 1.0 / c->speed);

                chain_grade(&fc, c, lutdir, i, j);
                chain_title(&fc, t, c, lutdir, i, j);

                if (c->fade_in > 0.0)
                    sb_add(&fc, ",fade=t=in:st=0:d=%.4f", c->fade_in);
                if (c->fade_out > 0.0)
                    sb_add(&fc, ",fade=t=out:st=%.4f:d=%.4f",
                           len - c->fade_out, c->fade_out);
                chain_transition(&fc, c);
                if (c->opacity < 1.0f)
                    sb_add(&fc, ",format=rgba,colorchannelmixer=aa=%.4f",
                           c->opacity);

                sb_add(&fc, ",setpts=PTS-STARTPTS+%.6f/TB[v%d]", c->tl_in, nvid);

                /* Composite. `enable` is what keeps a clip off the output
                 * outside its own span — without it the last frame of every
                 * clip would persist to the end of the timeline.
                 *
                 * The chain is base -> bg1 -> bg2 -> ..., each overlay taking
                 * the PREVIOUS stage by name. Naming the input "bg" instead
                 * of "bg<n>" leaves every intermediate stage unconnected and
                 * ffmpeg refuses the whole graph. */
                {
                    char prev[32];
                    if (nvid == 0) snprintf(prev, sizeof prev, "base");
                    else           snprintf(prev, sizeof prev, "bg%d", nvid);
                    sb_add(&fc, ";[%s][v%d]overlay=eof_action=pass"
                                ":x='(W-w)/2+(%.5f)*W':y='(H-h)/2+(%.5f)*H'"
                                ":enable='between(t,%.6f,%.6f)'[bg%d]",
                            prev, nvid,
                            c->xf.animate ? 0.0 : (double)px0,
                            c->xf.animate ? 0.0 : (double)py0,
                            c->tl_in, c->tl_in + len, nvid + 1);
                }
                nvid++;
            } else {
                if (c->kind != SS_CLIP_MEDIA) { input++; continue; }
                sb_add(&fc, ";[%d:a]", input);
                sb_add(&fc, "aresample=48000");
                if (c->speed != 1.0)
                    sb_add(&fc, ",atempo=%.6f", c->speed);
                if (c->gain_db != 0.0f)
                    sb_add(&fc, ",volume=%.3fdB", c->gain_db);
                if (c->fade_in > 0.0)
                    sb_add(&fc, ",afade=t=in:st=0:d=%.4f", c->fade_in);
                if (c->fade_out > 0.0)
                    sb_add(&fc, ",afade=t=out:st=%.4f:d=%.4f",
                           len - c->fade_out, c->fade_out);
                sb_add(&fc, ",adelay=%d:all=1[a%d]",
                       (int)(c->tl_in * 1000.0 + 0.5), naud);
                naud++;
            }
            input++;
        }
    }

    /* The overlay chain names its output bgN; rename the last one so the map
     * below does not have to know how many clips there were.
     *
     * A preview is scaled here rather than by building the whole graph at a
     * smaller size: every clip has already been composited at project scale,
     * so the framing, the transforms and the transitions are the ones the real
     * export produces and only the last step is cheaper. Scaling earlier would
     * make the preview a different cut, subtly, in exactly the places worth
     * checking. `-2` keeps the aspect and lands on an even height, which
     * yuv420p requires. */
    if (nvid > 0) sb_add(&fc, ";[bg%d]null[vout]", nvid);
    else          sb_add(&fc, ";[base]null[vout]");
    if (preview && t->w > 960)
        sb_add(&fc, ";[vout]scale=960:-2:flags=fast_bilinear[pout]");

    if (naud > 0) {
        int k;
        sb_add(&fc, ";");
        for (k = 0; k < naud; k++) sb_add(&fc, "[a%d]", k);
        /* normalize=0: amix otherwise divides every input by the number of
         * inputs, so adding a quiet music bed would duck the dialogue. */
        sb_add(&fc, "amix=inputs=%d:normalize=0:dropout_transition=0[aout]", naud);
    }

    PUSH(xdup("-filter_complex"));
    PUSH(xdup(fc.s ? fc.s : ""));
    PUSH(xdup("-map"));
    PUSH(xdup(preview && t->w > 960 ? "[pout]" : "[vout]"));
    if (naud > 0) { PUSH(xdup("-map")); PUSH(xdup("[aout]")); }
    PUSH(xdup("-t")); PUSH(xfmt("%.6f", dur > 0 ? dur : 1.0));
    PUSH(xdup("-c:v")); PUSH(xdup("libx264"));
    /* A preview is watched once and thrown away, so every setting here is
     * traded for the time it takes to produce. ultrafast/crf 30 is roughly an
     * order of magnitude quicker than the deliverable settings and looks it —
     * which is correct, because the thing being judged at this point is the
     * CUT, not the encode. */
    PUSH(xdup("-preset")); PUSH(xdup(preview ? "ultrafast" : "medium"));
    PUSH(xdup("-crf"));    PUSH(xdup(preview ? "30" : "18"));
    PUSH(xdup("-pix_fmt")); PUSH(xdup("yuv420p"));
    if (naud > 0) { PUSH(xdup("-c:a")); PUSH(xdup("aac"));
                    PUSH(xdup("-b:a")); PUSH(xdup(preview ? "96k" : "192k")); }
    /* Fragmented, so the file is playable while it is still being written and
     * a player opening it early does not need a moov atom that will not exist
     * until the encode finishes. */
    if (preview) {
        PUSH(xdup("-movflags")); PUSH(xdup("+frag_keyframe+empty_moov+default_base_moof"));
    }
    PUSH(xdup(out));

    /* execvp needs the NULL terminator but it is not an argument, so it is
     * appended past the returned count rather than through PUSH, whose whole
     * job is to treat a NULL as an allocation failure. */
    if (ac + 1 > cap) {
        char **na = realloc(av, sizeof(char *) * (cap + 1));
        if (!na) goto fail;
        av = na; cap = cap + 1;
    }
    av[ac] = NULL;

    free(fc.s);
    *argv_out = av;
    return ac;

fail:
    free(fc.s);
    for (i = 0; i < ac; i++) free(av[i]);
    free(av);
    return -1;
}

/* ------------------------------------------------------ the one frame -- */

int ss_timeline_frame(const ss_timeline *t, double time, const char *out,
                      const char *lutdir, int max_edge, char ***argv_out)
{
    strbuf fc = {0};
    char **av = NULL;
    int ac = 0, cap = 64;
    int i, j, input = 0, nvid = 0;

    av = malloc(sizeof(char *) * cap);
    if (!av) return -1;

    PUSH(xdup("ffmpeg"));
    PUSH(xdup("-v")); PUSH(xdup("error"));
    PUSH(xdup("-nostdin"));
    PUSH(xdup("-y"));

    /* Only what is on screen. Each media clip is seeked straight to its own
     * source position, so scrubbing to minute nine costs one seek and one
     * frame rather than nine minutes of decode. */
    for (i = 0; i < t->ntracks; i++) {
        const ss_track *tr = &t->track[i];
        if (tr->type != SS_TRACK_VIDEO || tr->hidden) continue;
        for (j = 0; j < tr->nclips; j++) {
            const ss_clip *c = &tr->clip[j];
            double len = ss_clip_length(c), off = time - c->tl_in;
            if (len <= 0 || off < 0 || off >= len) continue;

            if (c->kind != SS_CLIP_MEDIA) {
                char col[32];
                hexcol(c->col_r, c->col_g, c->col_b,
                       c->kind == SS_CLIP_SOLID ? 1.0f : c->col_a, col, sizeof col);
                PUSH(xdup("-f")); PUSH(xdup("lavfi"));
                PUSH(xdup("-i"));
                PUSH(xfmt("color=c=%s:s=%dx%d:d=0.04,format=rgba",
                          col, t->w, t->h));
            } else {
                double srct = c->src_in + off * (c->speed > 0 ? c->speed : 1.0);
                if (!c->still && srct > 0.0) {
                    PUSH(xdup("-ss")); PUSH(xfmt("%.6f", srct));
                }
                PUSH(xdup("-i")); PUSH(xdup(c->path));
            }
        }
    }

    if (sb_add(&fc, "color=c=black:s=%dx%d:d=0.04[base]", t->w, t->h) != 0)
        goto fail;

    for (i = 0; i < t->ntracks; i++) {
        const ss_track *tr = &t->track[i];
        if (tr->type != SS_TRACK_VIDEO || tr->hidden) continue;
        for (j = 0; j < tr->nclips; j++) {
            const ss_clip *c = &tr->clip[j];
            double len = ss_clip_length(c), off = time - c->tl_in;
            float s, px, py, rot;
            double a;
            int fw, fh;
            char prev[32];

            if (len <= 0 || off < 0 || off >= len) continue;

            /* A single frame has nothing to animate, so the transform, the
             * fades and the transition are all just numbers here — evaluated
             * by the same xform_at and alpha_at the export's filters are
             * generated from. */
            xform_at(&c->xf, off / len, &s, &px, &py, &rot);
            a = alpha_at(c, off, len);
            fitted_size(t, s, &fw, &fh);

            /* setpts FIRST, and it is not optional. A clip seeked with -ss
             * hands the graph frames still carrying their SOURCE timestamps,
             * while the one-frame base sits at zero — so overlay waits for a
             * secondary frame at t<=0, never gets one, and emits the black
             * base on its own. The monitor goes black at exactly the moments
             * a seek was needed, which is to say everywhere except the very
             * start of the timeline. */
            sb_add(&fc, ";[%d:v]setpts=PTS-STARTPTS,format=rgba,scale=%d:%d"
                        ":force_original_aspect_ratio=decrease", input, fw, fh);
            if (rot != 0.0f)
                sb_add(&fc, ",rotate=%.6f:ow='hypot(iw,ih)':oh='hypot(iw,ih)'"
                            ":c=black@0", (double)rot * M_PI / 180.0);
            chain_grade(&fc, c, lutdir, i, j);
            chain_title(&fc, t, c, lutdir, i, j);
            if (a < 1.0)
                sb_add(&fc, ",colorchannelmixer=aa=%.4f", a);
            sb_add(&fc, "[v%d]", nvid);

            if (nvid == 0) snprintf(prev, sizeof prev, "base");
            else           snprintf(prev, sizeof prev, "bg%d", nvid);
            sb_add(&fc, ";[%s][v%d]overlay=x='(W-w)/2+(%.5f)*W'"
                        ":y='(H-h)/2+(%.5f)*H'[bg%d]",
                   prev, nvid, (double)px, (double)py, nvid + 1);
            nvid++;
            input++;
        }
    }

    if (nvid > 0) sb_add(&fc, ";[bg%d]null[vout]", nvid);
    else          sb_add(&fc, ";[base]null[vout]");
    if (max_edge > 0)
        sb_add(&fc, ";[vout]scale=w=%d:h=%d:force_original_aspect_ratio=decrease"
                    ":flags=area[sout]", max_edge, max_edge);

    PUSH(xdup("-filter_complex"));
    PUSH(xdup(fc.s ? fc.s : ""));
    PUSH(xdup("-map")); PUSH(xdup(max_edge > 0 ? "[sout]" : "[vout]"));
    PUSH(xdup("-frames:v")); PUSH(xdup("1"));
    PUSH(xdup("-update")); PUSH(xdup("1"));
    PUSH(xdup(out));

    if (ac + 1 > cap) {
        char **na = realloc(av, sizeof(char *) * (cap + 1));
        if (!na) goto fail;
        av = na; cap = cap + 1;
    }
    av[ac] = NULL;

    free(fc.s);
    *argv_out = av;
    return ac;

fail:
    free(fc.s);
    for (i = 0; i < ac; i++) free(av[i]);
    free(av);
    return -1;
}
#undef PUSH
