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

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
    /* The outline and the line spacing that titles were drawn with before
     * either was a setting. They are the DEFAULTS rather than a sentinel, so
     * a project saved before this existed reads back as itself: no style line
     * means these numbers, which is what it was rendered with. */
    c->text_border = 0.045f;
    c->text_line   = 0.25f;
    /* Off, and off is not zero: zero is a legitimate instant to freeze on. */
    c->freeze      = -1.0;
    c->stab_smooth = 10.0f;
    c->stab_zoom   = 0.0f;
    /* Where a compressor starts working if somebody turns one on without
     * saying where. Speech sits around -18 dBFS, so this catches the peaks
     * and leaves the level alone. */
    c->comp_thresh = -18.0f;
    ss_xform_reset(&c->xf);
}

/* ------------------------------------------------------------ clipboard -- */

int ss_clipboard_path(char *out, size_t n)
{
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *env = getenv("SYNSTUDIO_CLIPBOARD");

    if (!out || n == 0) return -1;
    if (env && *env) { snprintf(out, n, "%s", env); return 0; }
    if (xdg && *xdg) { snprintf(out, n, "%s/synstudio/clipboard", xdg); return 0; }
    if (home && *home) { snprintf(out, n, "%s/.config/synstudio/clipboard", home); return 0; }
    return -1;
}

static int clip_mkdir_p(const char *dir)
{
    char buf[1024], *p;
    snprintf(buf, sizeof buf, "%s", dir);
    for (p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    return (mkdir(buf, 0755) != 0 && errno != EEXIST) ? -1 : 0;
}

int ss_clip_copy_out(const ss_clip *c, int w, int h, double fps)
{
    char path[1024], dir[1024], *slash;
    ss_timeline tmp;
    FILE *fp;
    int rc;

    if (!c || ss_clipboard_path(path, sizeof path) != 0) return -1;
    snprintf(dir, sizeof dir, "%s", path);
    if ((slash = strrchr(dir, '/')) != NULL) {
        *slash = '\0';
        if (clip_mkdir_p(dir) != 0) return -1;
    }

    /* A one-track, one-clip document. The project's size and rate go with it
     * because a clip's own numbers are fractions of them — a title sized 0.08
     * of the frame height means nothing without knowing the frame. */
    ss_timeline_reset(&tmp, w, h, fps);
    if (ss_timeline_add_track(&tmp, SS_TRACK_VIDEO, "clipboard") < 0) {
        ss_timeline_free(&tmp);
        return -1;
    }
    if (ss_timeline_add_clip(&tmp, 0, c) < 0) {
        ss_timeline_free(&tmp);
        return -1;
    }

    fp = fopen(path, "w");
    if (!fp) { ss_timeline_free(&tmp); return -1; }
    rc = ss_timeline_write(&tmp, fp);
    if (fclose(fp) != 0) rc = -1;
    ss_timeline_free(&tmp);
    return rc;
}

int ss_clip_copy_in(ss_clip *out)
{
    char path[1024];
    ss_timeline tmp;
    FILE *fp;

    if (!out || ss_clipboard_path(path, sizeof path) != 0) return -1;
    fp = fopen(path, "r");
    if (!fp) return -1;
    ss_timeline_reset(&tmp, 1920, 1080, 25.0);
    ss_timeline_read(&tmp, fp);
    fclose(fp);

    if (tmp.ntracks < 1 || tmp.track[0].nclips < 1) {
        ss_timeline_free(&tmp);
        return -1;
    }
    *out = tmp.track[0].clip[0];
    ss_timeline_free(&tmp);
    return 0;
}

void ss_timeline_stabdir(const char *proj, char *out, size_t n)
{
    if (!out || n == 0) return;
    snprintf(out, n, "%s.stab", proj ? proj : "");
}

int ss_clip_has_ramp(const ss_clip *c)
{
    return c && ss_clip_prop_nkeys(c, "speed") >= 2;
}

/* The timebase, sampled once.
 *
 * A ramp is a staircase here, the way a moving grade is 48 cubes and a moving
 * opacity is a run of sendcmd steps. That is not a shortcut: the EXPORT can
 * only be handed a piecewise setpts expression and a run of atempo commands,
 * so a smooth curve would be an answer the renderer cannot say. Sampling it
 * in one place is what keeps the length, the seek, the expression and the
 * tempo talking about the same staircase.
 *
 * The speed of a segment is read at its MIDPOINT rather than its start, which
 * halves the error against the curve for the same number of steps.
 */
int ss_clip_retime(const ss_clip *c, ss_retime_seg *seg, int max)
{
    double srclen, out = 0;
    int i, n;

    if (!c || !seg || max < 1) return 0;
    srclen = c->src_out - c->src_in;
    if (srclen < 0) srclen = 0;

    if (!ss_clip_has_ramp(c)) {
        double sp = c->speed > 0 ? c->speed : 1.0;
        seg[0].src0 = 0; seg[0].src1 = srclen;
        seg[0].out0 = 0; seg[0].out1 = srclen / sp;
        seg[0].speed = sp;
        return 1;
    }

    n = SS_MAX_RETIME_SEG < max ? SS_MAX_RETIME_SEG : max;
    for (i = 0; i < n; i++) {
        double s0 = srclen * i / n, s1 = srclen * (i + 1) / n;
        double sp = ss_clip_prop_at(c, "speed", (s0 + s1) / 2.0);
        if (sp <= 0.0) sp = 0.1;      /* the table's own floor; never a stop */
        seg[i].src0 = s0; seg[i].src1 = s1;
        seg[i].out0 = out;
        out += (s1 - s0) / sp;
        seg[i].out1 = out;
        seg[i].speed = sp;
    }
    return n;
}

double ss_clip_length(const ss_clip *c)
{
    ss_retime_seg seg[SS_MAX_RETIME_SEG];
    int n = ss_clip_retime(c, seg, SS_MAX_RETIME_SEG);
    double l = n > 0 ? seg[n - 1].out1 : 0;
    return l > 0 ? l : 0;
}

double ss_clip_source_at(const ss_clip *c, double tt, double fps)
{
    ss_retime_seg seg[SS_MAX_RETIME_SEG];
    int n = ss_clip_retime(c, seg, SS_MAX_RETIME_SEG), i;
    double src = 0, srclen;

    if (n <= 0) return c ? c->src_in : 0;
    if (tt < 0) tt = 0;
    for (i = 0; i < n; i++) {
        if (tt < seg[i].out1 || i == n - 1) {
            src = seg[i].src0 + (tt - seg[i].out0) * seg[i].speed;
            break;
        }
    }
    srclen = c->src_out - c->src_in;
    if (src < 0) src = 0;
    if (src > srclen) src = srclen;
    /* Backwards is a mirror of the SOURCE span, applied after the ramp: a
     * clip that is both ramped and reversed slows down at the same point in
     * the shot either way round, which is what a person means by both.
     *
     * Minus one frame, and that is not a rounding fudge: the `reverse` filter
     * hands out frame N-1 first, whose own instant is one frame before the
     * end of the span. Mirroring the TIME alone lands on the frame after it. */
    if (c->reverse) {
        double fr = fps > 0 ? 1.0 / fps : 0.0;
        src = srclen - src - fr;
        if (src < 0) src = 0;
    }
    return c->src_in + src;
}

/* One of the four numbers a transform is made of, `tt` seconds into the clip.
 *
 * Keys WIN over the two-point ramp. `animate` came first and is a ramp from
 * one framing to another with nothing in between; parameter keys are the
 * general case, and a property that has any is being driven by them. Both
 * cannot apply at once, and a clip that has neither is simply its own
 * numbers. */
static float xf_one(const ss_clip *c, const char *key, double tt,
                    float a, float b, double p)
{
    if (ss_clip_prop_nkeys(c, key) > 0)
        return (float)ss_clip_prop_at(c, key, tt);
    return a + (b - a) * (float)(c->xf.animate ? p : 0.0);
}

/* The transform `tt` seconds into a clip `len` long. ONE definition, read by
 * the frame compositor (which evaluates it in C, because a still frame has
 * nothing to animate) and by the export (which hands the endpoints to
 * zoompan, or the whole key list to it as an expression). If a scrub and an
 * export ever disagree about framing, this is the function that is wrong, and
 * it is the only one. */
static void xform_at(const ss_clip *c, double tt, double len,
                     float *scale, float *px, float *py, float *rot)
{
    const ss_xform *x = &c->xf;
    double p = len > 0 ? tt / len : 0.0;

    if (p < 0) p = 0;
    if (p > 1) p = 1;
    *scale = xf_one(c, "xform.scale",  tt, x->scale,  x->scale2,  p);
    *px    = xf_one(c, "xform.x",      tt, x->pos_x,  x->pos_x2,  p);
    *py    = xf_one(c, "xform.y",      tt, x->pos_y,  x->pos_y2,  p);
    *rot   = xf_one(c, "xform.rotate", tt, x->rotate, x->rotate2, p);
    if (*scale < 0.05f) *scale = 0.05f;
    if (*scale > 10.0f) *scale = 10.0f;
}

/* Whether the framing MOVES: either the old two-point ramp, or a key list on
 * any of the three properties zoompan is responsible for. The export picks
 * its whole video chain on this, so it has one name. */
static int xform_moves(const ss_clip *c)
{
    return c->xf.animate ||
           ss_clip_prop_moves(c, "xform.scale") ||
           ss_clip_prop_moves(c, "xform.x") ||
           ss_clip_prop_moves(c, "xform.y");
}

static int xform_is_identity(const ss_xform *x)
{
    return !x->animate && x->scale == 1.0f && x->pos_x == 0.0f &&
           x->pos_y == 0.0f && x->rotate == 0.0f;
}

/* How much of a clip is showing at time `tt` seconds into it: the fades
 * multiplied with the clip opacity. The export expresses these as filters; the
 * frame compositor needs the number.
 *
 * The TRANSITION is not in here any more. It used to be a term — a clip whose
 * alpha rose while the one under it still played IS a cross dissolve — and
 * that bought every transition for free, but it can only ever be a dissolve.
 * A wipe was a geq on the export side and this same uniform ramp on the
 * monitor's, so the two showed different pictures. Transitions are xfade now,
 * on both sides, and they are a layer rather than an alpha. */
static double alpha_at(const ss_clip *c, double tt, double len)
{
    double a = ss_clip_prop_at(c, "opacity", tt);
    if (a <= 0 && ss_clip_prop_nkeys(c, "opacity") == 0) a = 1.0;
    if (c->fade_in > 0 && tt < c->fade_in)  a *= tt / c->fade_in;
    if (c->fade_out > 0 && tt > len - c->fade_out)
        a *= (len - tt) / c->fade_out;
    return a < 0 ? 0 : (a > 1 ? 1 : a);
}

/* ------------------------------------------------------- the transitions -- */

/* One filter, sixty looks. `xfade` takes two streams and a name and does the
 * whole catalogue — wipes, slides, circles, slices, blurs — so a transition
 * here is a row in a table rather than a shader.
 *
 * ⚠ DIRECTION. Ours names where the incoming picture comes FROM; xfade's
 * names which way the boundary TRAVELS. They are exact opposites, and
 * uniformly so: measured at half progress across every directional family
 * (wipe, slide, cover, reveal, slice, wind, diagonal, corner), xfade's
 * `*left` always puts the incoming picture on the RIGHT. So every mapping in
 * here is MIRRORED — left↔right, up↔down, tl↔br, tr↔bl — and a row that is
 * not mirrored is a bug you will only see by rendering it.
 *
 * The first six rows keep the numbers they had before xfade existed, because
 * `trans` is an enum a document can carry as an integer. The four original
 * wipes were a soft-edged geq and map to xfade's `smooth*`, which is the
 * soft-edged one — a project made before this still looks like itself.
 *
 * `dip` is the one that is not an xfade at all: a cut at the halfway point
 * under a colour whose alpha rises and falls, which is what a dip through
 * black has always been. */
#define TRANS_LIST(X) \
    X("dissolve",    "fade",        "Dissolve") \
    X("wipeleft",    "smoothright", "Wipe from the left") \
    X("wiperight",   "smoothleft",  "Wipe from the right") \
    X("wipeup",      "smoothdown",  "Wipe from the top") \
    X("wipedown",    "smoothup",    "Wipe from the bottom") \
    X("dip",         NULL,          "Dip to a colour") \
    X("hardleft",    "wiperight",   "Hard wipe from the left") \
    X("hardright",   "wipeleft",    "Hard wipe from the right") \
    X("hardup",      "wipedown",    "Hard wipe from the top") \
    X("harddown",    "wipeup",      "Hard wipe from the bottom") \
    X("slideleft",   "slideright",  "Slide in from the left") \
    X("slideright",  "slideleft",   "Slide in from the right") \
    X("slideup",     "slidedown",   "Slide in from the top") \
    X("slidedown",   "slideup",     "Slide in from the bottom") \
    X("coverleft",   "coverright",  "Cover from the left") \
    X("coverright",  "coverleft",   "Cover from the right") \
    X("coverup",     "coverdown",   "Cover from the top") \
    X("coverdown",   "coverup",     "Cover from the bottom") \
    X("revealleft",  "revealright", "Reveal from the left") \
    X("revealright", "revealleft",  "Reveal from the right") \
    X("revealup",    "revealdown",  "Reveal from the top") \
    X("revealdown",  "revealup",    "Reveal from the bottom") \
    X("sliceleft",   "hrslice",     "Slices in from the left") \
    X("sliceright",  "hlslice",     "Slices in from the right") \
    X("sliceup",     "vdslice",     "Slices in from the top") \
    X("slicedown",   "vuslice",     "Slices in from the bottom") \
    X("windleft",    "hrwind",      "Blown in from the left") \
    X("windright",   "hlwind",      "Blown in from the right") \
    X("windup",      "vdwind",      "Blown in from the top") \
    X("winddown",    "vuwind",      "Blown in from the bottom") \
    X("diagtl",      "diagbr",      "Diagonal from the top left") \
    X("diagtr",      "diagbl",      "Diagonal from the top right") \
    X("diagbl",      "diagtr",      "Diagonal from the bottom left") \
    X("diagbr",      "diagtl",      "Diagonal from the bottom right") \
    X("cornertl",    "wipebr",      "Corner wipe from the top left") \
    X("cornertr",    "wipebl",      "Corner wipe from the top right") \
    X("cornerbl",    "wipetr",      "Corner wipe from the bottom left") \
    X("cornerbr",    "wipetl",      "Corner wipe from the bottom right") \
    X("circleopen",  "circleopen",  "Circle opens") \
    X("circleclose", "circleclose", "Circle closes") \
    X("circlecrop",  "circlecrop",  "Circle crop") \
    X("rectcrop",    "rectcrop",    "Rectangle crop") \
    X("radial",      "radial",      "Radial sweep") \
    X("vertopen",    "vertopen",    "Vertical curtain opens") \
    X("vertclose",   "vertclose",   "Vertical curtain closes") \
    X("horzopen",    "horzopen",    "Horizontal curtain opens") \
    X("horzclose",   "horzclose",   "Horizontal curtain closes") \
    X("noise",       "dissolve",    "Noise dissolve") \
    X("pixelize",    "pixelize",    "Pixelate through") \
    X("distance",    "distance",    "Distance") \
    X("blur",        "hblur",       "Blur through") \
    X("fadeblack",   "fadeblack",   "Fade through black") \
    X("fadewhite",   "fadewhite",   "Fade through white") \
    X("fadegrays",   "fadegrays",   "Fade through grey") \
    X("fadefast",    "fadefast",    "Fade, fast out") \
    X("fadeslow",    "fadeslow",    "Fade, slow out") \
    X("squeezeh",    "squeezeh",    "Squeeze horizontally") \
    X("squeezev",    "squeezev",    "Squeeze vertically") \
    X("zoomin",      "zoomin",      "Zoom in")

#define TRANS_ROW(n, x, l) { n, x, l },
#define TRANS_BAR(n, x, l) "|" n
#define TRANS_ONE(n, x, l) + 1

/* Built from the ONE list, so the string a picker is drawn from and the table
 * the renderer reads cannot say different things. */
#define TRANS_CHOICES "none" TRANS_LIST(TRANS_BAR)
#define TRANS_COUNT   (1 TRANS_LIST(TRANS_ONE))

static const struct {
    const char *name, *xfade, *label;
} transes[] = {
    { "none", NULL, "None" },
    TRANS_LIST(TRANS_ROW)
};

int ss_trans_count(void) { return TRANS_COUNT; }

const char *ss_trans_name(int v)
{
    return (v > 0 && v < TRANS_COUNT) ? transes[v].name : "none";
}

const char *ss_trans_label(int v)
{
    return (v >= 0 && v < TRANS_COUNT) ? transes[v].label : "None";
}

const char *ss_trans_xfade(int v)
{
    return (v > 0 && v < TRANS_COUNT) ? transes[v].xfade : NULL;
}

int ss_trans_value(const char *s)
{
    int i;
    if (!s) return -1;
    for (i = 0; i < TRANS_COUNT; i++)
        if (!strcmp(s, transes[i].name)) return i;
    return -1;
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
/* A caption crossing a tab-separated line.
 *
 * Only two bytes need to travel differently: a newline, which would end the
 * record, and the backslash that says so. Everything else — colons, percent
 * signs, apostrophes, quotes — is stored literally, because the file format
 * is columns and not a shell, and the escaping drawtext needs happens far
 * later and to a copy in a file of its own. */
static void esc_text(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 3 < n; in++) {
        if (*in == '\\')      { out[o++] = '\\'; out[o++] = '\\'; }
        else if (*in == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (*in == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else                  out[o++] = *in;
    }
    out[o] = '\0';
}

static void unesc_text(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 1 < n; in++) {
        if (*in == '\\' && in[1]) {
            in++;
            out[o++] = *in == 'n' ? '\n' : *in == 't' ? '\t' : *in;
        } else {
            out[o++] = *in;
        }
    }
    out[o] = '\0';
}

enum { CO_FLOAT, CO_DOUBLE, CO_INT, CO_ENUM, CO_TEXT };

typedef struct {
    const char *key;
    int         type;
    size_t      off;
    float       lo, hi;
    const char *group, *label, *choices;
    /* Whether this property can carry parameter keys. It lives in the table
     * with everything else about the property, so the inspector's diamond
     * appears on exactly the rows the export knows how to animate — a button
     * offering to key something the renderer would then ignore is worse than
     * no button. A row is animatable when ffmpeg has a way to vary it per
     * frame; the ones that are missing are missing because it does not. */
    int         anim;
    /* The member's OWN size. CO_TEXT used to snprintf with `sizeof c->text`
     * whatever field it was writing, which was correct while `text` was the
     * only one of them and a 64-byte overflow the moment a second arrived. */
    size_t      len;
} cfield;

#define C(k, t, m, lo, hi, grp, lbl, ch, an) \
    { k, t, offsetof(ss_clip, m), lo, hi, grp, lbl, ch, an, \
      sizeof(((ss_clip *)0)->m) }

#define POS_CHOICES   "topleft|topcentre|topright|left|centre|right|" \
                      "bottomleft|bottomcentre|bottomright"
#define WEIGHT_CHOICES "regular|bold|light|italic|bolditalic"
#define RETIME_CHOICES "nearest|blend|flow"
#define AFADE_CHOICES  "linear|qsin|hsin|esin|log|exp"

static const cfield cfields[] = {
    C("opacity",      CO_FLOAT, opacity,      0.0f,    1.0f, "Levels", "Opacity", NULL, 1),
    C("gain",         CO_FLOAT, gain_db,    -60.0f,   24.0f, "Levels", "Gain (dB)", NULL, 1),
    /* Keyed speed is a RAMP, and its keys are in SOURCE seconds — the one
     * property whose axis is not output time, because a ramp says "at this
     * point in the shot" and because the output length is then an integral
     * rather than an equation. ss_clip_retime is where that is decided. */
    C("speed",        CO_DOUBLE,speed,        0.1f,   10.0f, "Levels", "Speed", NULL, 1),
    C("reverse",      CO_INT,   reverse,      0.0f,    1.0f, "Levels", "Backwards", NULL, 0),
    C("freeze",       CO_DOUBLE,freeze,      -1.0f, 86400.0f, "Levels", "Freeze at (s)", NULL, 0),
    C("retime",       CO_ENUM,  retime,       0.0f,    2.0f, "Levels", "Retime", RETIME_CHOICES, 0),
    C("stab",         CO_INT,   stab,         0.0f,    1.0f, "Stabiliser", "On", NULL, 0),
    C("stab.smooth",  CO_FLOAT, stab_smooth,  1.0f,  100.0f, "Stabiliser", "Smoothing", NULL, 0),
    C("stab.zoom",    CO_FLOAT, stab_zoom,    0.0f,   20.0f, "Stabiliser", "Zoom %", NULL, 0),
    C("fade.in",      CO_DOUBLE,fade_in,      0.0f,   30.0f, "Levels", "Fade in (s)", NULL, 0),
    C("fade.out",     CO_DOUBLE,fade_out,     0.0f,   30.0f, "Levels", "Fade out (s)", NULL, 0),
    C("fade.shape",   CO_ENUM,  fade_shape,   0.0f,    5.0f, "Levels", "Fade shape", AFADE_CHOICES, 0),

    /* The dialogue chain, in the order it is built: clean it, shape it,
     * control it. Each is one filter with one knob, and zero means the filter
     * is not in the graph AT ALL rather than in it doing nothing. */
    C("nr",           CO_FLOAT, nr_audio,     0.0f,  100.0f, "Sound", "Noise reduction", NULL, 0),
    /* Which denoiser that amount drives: empty is afftdn, a name or a path is
     * an arnndn model. A model this machine has not got is KEPT and does not
     * denoise, the way a missing LUT renders as nothing. */
    C("nr.model",     CO_TEXT,  nr_model,     0.0f,    0.0f, "Sound", "Noise model", NULL, 0),
    C("gate",         CO_FLOAT, gate,         0.0f,  100.0f, "Sound", "Gate", NULL, 0),
    C("eq.60",        CO_FLOAT, eq_db[0],   -18.0f,   18.0f, "Sound", "60 Hz", NULL, 0),
    C("eq.200",       CO_FLOAT, eq_db[1],   -18.0f,   18.0f, "Sound", "200 Hz", NULL, 0),
    C("eq.600",       CO_FLOAT, eq_db[2],   -18.0f,   18.0f, "Sound", "600 Hz", NULL, 0),
    C("eq.2k",        CO_FLOAT, eq_db[3],   -18.0f,   18.0f, "Sound", "2 kHz", NULL, 0),
    C("eq.6k",        CO_FLOAT, eq_db[4],   -18.0f,   18.0f, "Sound", "6 kHz", NULL, 0),
    C("eq.12k",       CO_FLOAT, eq_db[5],   -18.0f,   18.0f, "Sound", "12 kHz", NULL, 0),
    C("comp",         CO_FLOAT, comp,         0.0f,  100.0f, "Sound", "Compression", NULL, 0),
    C("comp.thresh",  CO_FLOAT, comp_thresh,-60.0f,    0.0f, "Sound", "Threshold (dB)", NULL, 0),
    C("deess",        CO_FLOAT, deess,        0.0f,  100.0f, "Sound", "De-ess", NULL, 0),

    C("trans",        CO_ENUM,  trans,        0.0f, TRANS_COUNT - 1.0f, "Transition", "Kind", TRANS_CHOICES, 0),
    C("trans.dur",    CO_DOUBLE,trans_dur,    0.0f,   10.0f, "Transition", "Length (s)", NULL, 0),
    C("trans.r",      CO_FLOAT, trans_r,      0.0f,    1.0f, "Transition", "Dip red", NULL, 0),
    C("trans.g",      CO_FLOAT, trans_g,      0.0f,    1.0f, "Transition", "Dip green", NULL, 0),
    C("trans.b",      CO_FLOAT, trans_b,      0.0f,    1.0f, "Transition", "Dip blue", NULL, 0),

    C("xform.scale",  CO_FLOAT, xf.scale,     0.05f,  10.0f, "Motion", "Scale", NULL, 1),
    C("xform.x",      CO_FLOAT, xf.pos_x,    -1.0f,    1.0f, "Motion", "Position X", NULL, 1),
    C("xform.y",      CO_FLOAT, xf.pos_y,    -1.0f,    1.0f, "Motion", "Position Y", NULL, 1),
    C("xform.rotate", CO_FLOAT, xf.rotate, -180.0f,  180.0f, "Motion", "Rotation", NULL, 1),
    C("xform.animate",CO_INT,   xf.animate,   0.0f,    1.0f, "Motion", "Animate to", NULL, 0),
    C("xform.scale2", CO_FLOAT, xf.scale2,    0.05f,  10.0f, "Motion", "End scale", NULL, 0),
    C("xform.x2",     CO_FLOAT, xf.pos_x2,   -1.0f,    1.0f, "Motion", "End X", NULL, 0),
    C("xform.y2",     CO_FLOAT, xf.pos_y2,   -1.0f,    1.0f, "Motion", "End Y", NULL, 0),
    C("xform.rotate2",CO_FLOAT, xf.rotate2,-180.0f,  180.0f, "Motion", "End rotation", NULL, 0),

    C("text",         CO_TEXT,  text,         0.0f,    0.0f, "Title", "Caption", NULL, 0),
    C("text.size",    CO_FLOAT, text_size,    0.01f,   0.5f, "Title", "Size", NULL, 0),
    C("text.r",       CO_FLOAT, text_r,       0.0f,    1.0f, "Title", "Red", NULL, 0),
    C("text.g",       CO_FLOAT, text_g,       0.0f,    1.0f, "Title", "Green", NULL, 0),
    C("text.b",       CO_FLOAT, text_b,       0.0f,    1.0f, "Title", "Blue", NULL, 0),
    C("text.pos",     CO_ENUM,  text_pos,     0.0f,    8.0f, "Title", "Placement", POS_CHOICES, 0),
    C("text.font",    CO_TEXT,  text_font,    0.0f,    0.0f, "Title", "Font", NULL, 0),
    C("text.weight",  CO_ENUM,  text_weight,  0.0f,    4.0f, "Title", "Weight", WEIGHT_CHOICES, 0),
    C("text.border",  CO_FLOAT, text_border,  0.0f,    0.4f, "Title", "Outline", NULL, 0),
    C("text.shadow",  CO_FLOAT, text_shadow,  0.0f,    0.4f, "Title", "Shadow", NULL, 0),
    C("text.box",     CO_FLOAT, text_box,     0.0f,    1.0f, "Title", "Plate", NULL, 0),
    C("text.line",    CO_FLOAT, text_line,    0.0f,    3.0f, "Title", "Line spacing", NULL, 0),
    C("text.roll",    CO_FLOAT, text_roll,    0.0f,    2.0f, "Title", "Credit roll", NULL, 0),

    C("colour.r",     CO_FLOAT, col_r,        0.0f,    1.0f, "Background", "Red", NULL, 0),
    C("colour.g",     CO_FLOAT, col_g,        0.0f,    1.0f, "Background", "Green", NULL, 0),
    C("colour.b",     CO_FLOAT, col_b,        0.0f,    1.0f, "Background", "Blue", NULL, 0),
    C("colour.a",     CO_FLOAT, col_a,        0.0f,    1.0f, "Background", "Opacity", NULL, 0),
};
#undef C

/* ---- title styles ----
 *
 * Four sets of the fields above, applied together. A lower third is not a
 * feature: it is a size, a corner, a plate and a weight, and once a style has
 * set them every one is still a slider — the same bargain a look strikes with
 * a grade, and the reason there is no separate lower-third object to keep in
 * step with the title it is made of.
 *
 * Placement and plate travel with the style; the CAPTION and the colour never
 * do. A style says how a title is drawn, not what it says.
 */
static const ss_title_style tstyles[] = {
    { "plain",       "white with an outline, wherever it is put" },
    { "lower-third", "bottom left, bold, on a plate — a name over a shot" },
    { "subtitle",    "small, bottom centre, on a plate — a line of dialogue" },
    { "heading",     "large and centred, for a card between scenes" },
    { "credit-roll", "centred and climbing, for the end of it" },
    { NULL, NULL }
};

const ss_title_style *ss_title_styles(void) { return tstyles; }

int ss_title_style_apply(ss_clip *c, const char *name)
{
    if (!c || !name) return -1;

    if (!strcmp(name, "plain")) {
        c->text_size = 0.08f; c->text_pos = SS_TEXT_BC;
        c->text_weight = SS_FW_REGULAR;
        c->text_border = 0.045f; c->text_shadow = 0.0f; c->text_box = 0.0f;
        c->text_line = 0.25f; c->text_roll = 0.0f;
        return 0;
    }
    if (!strcmp(name, "lower-third")) {
        c->text_size = 0.055f; c->text_pos = SS_TEXT_BL;
        c->text_weight = SS_FW_BOLD;
        /* A plate instead of a heavy outline: a name sits still on screen
         * long enough to be read, and an outline that wide over a moving
         * shot shimmers. */
        c->text_border = 0.0f; c->text_shadow = 0.03f; c->text_box = 0.55f;
        c->text_line = 0.2f; c->text_roll = 0.0f;
        return 0;
    }
    if (!strcmp(name, "subtitle")) {
        c->text_size = 0.05f; c->text_pos = SS_TEXT_BC;
        c->text_weight = SS_FW_REGULAR;
        /* Both, and deliberately: a subtitle lands on a picture nobody has
         * graded yet, so the plate carries the dark shots and the outline
         * carries the frames where the plate is not enough. */
        c->text_border = 0.03f; c->text_shadow = 0.0f; c->text_box = 0.5f;
        c->text_line = 0.2f; c->text_roll = 0.0f;
        return 0;
    }
    if (!strcmp(name, "heading")) {
        c->text_size = 0.12f; c->text_pos = SS_TEXT_MC;
        c->text_weight = SS_FW_BOLD;
        c->text_border = 0.04f; c->text_shadow = 0.0f; c->text_box = 0.0f;
        c->text_line = 0.3f; c->text_roll = 0.0f;
        return 0;
    }
    if (!strcmp(name, "credit-roll")) {
        c->text_size = 0.05f; c->text_pos = SS_TEXT_MC;
        c->text_weight = SS_FW_REGULAR;
        c->text_border = 0.0f; c->text_shadow = 0.0f; c->text_box = 0.0f;
        c->text_line = 0.6f;
        /* Slow enough to read: a screen height every ten seconds, which for
         * a 1080 project is about a hundred pixels a second. */
        c->text_roll = 0.1f;
        return 0;
    }
    return -1;
}

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
    out->animatable = cfields[i].anim;
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
        /* \n is a line break here too, and not only in the file. A caption is
         * typed at a shell or into a one-line field, neither of which can
         * carry a real newline, so the two bytes that mean one in the project
         * file mean one on the way in as well — otherwise a title could be
         * SAVED multi-line and never SET that way. */
        unesc_text(val, (char *)p, f->len);
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
    /* ESCAPED on the way out, for the same reason it is escaped in the file:
     * this printing is one record per line and a caption with a line break in
     * it would end the record halfway through — and the window reads these
     * lines. What comes back out is exactly what `set` takes in. */
    case CO_TEXT: esc_text((const char *)p, out, n); return 0;
    case CO_ENUM: { char b[32]; enum_name(f, *(const int *)p, b, sizeof b);
                    snprintf(out, n, "%s", b); return 0; }
    case CO_INT:    snprintf(out, n, "%d", *(const int *)p); return 0;
    case CO_DOUBLE: snprintf(out, n, "%.6g", *(const double *)p); return 0;
    default:        snprintf(out, n, "%.6g", (double)*(const float *)p); return 0;
    }
}

/* ------------------------------------------------ a property that moves -- */

/* Colour has to be baked to a cube, which is why a grade key carries a whole
 * develop stack and why an animated grade costs forty-eight files. Everything
 * else about a clip is ONE NUMBER, and ffmpeg will take an expression for
 * nearly all of them — so a parameter key is a name, a time and a value, and
 * an animated zoom is a string.
 *
 * ss_clip_prop_at is to a keyed property what xform_at is to a transform: the
 * only place it becomes a number. The monitor calls it; the export builds its
 * expressions from the same list. */

static const char *ease_names[] = { "linear", "in", "out", "inout", "hold" };
static const int   nease = (int)(sizeof ease_names / sizeof ease_names[0]);

int ss_ease_value(const char *name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < nease; i++) if (!strcmp(name, ease_names[i])) return i;
    return -1;
}

const char *ss_ease_name(int e)
{
    return (e >= 0 && e < nease) ? ease_names[e] : "linear";
}

int ss_clip_prop_animatable(const char *key)
{
    const cfield *f = cfind(key);
    return f && f->anim;
}

/* The value a clip with no keys has. */
static double prop_static(const ss_clip *c, const cfield *f)
{
    const void *p = (const char *)c + f->off;
    switch (f->type) {
    case CO_INT: case CO_ENUM: return (double)*(const int *)p;
    case CO_DOUBLE:            return *(const double *)p;
    default:                   return (double)*(const float *)p;
    }
}

/* How finely the EXPORT can express this property, and therefore how finely
 * the monitor is allowed to show it.
 *
 * Almost everything here reaches ffmpeg as an expression — zoompan takes one
 * for the zoom and the crop window, rotate takes one for the angle, volume
 * takes one per frame — and an expression is exact. Opacity is the exception:
 * no filter multiplies alpha by an expression, so it is driven by sendcmd,
 * which sets a value at an instant and holds it until the next. Quantising to
 * one code value makes that a staircase nobody can see, and rounding the SAME
 * way here is what makes the monitor equal to the export rather than close to
 * it. Zero means no quantisation: the export can say it exactly. */
static double prop_quant(const char *key)
{
    return !strcmp(key, "opacity") ? 1.0 / 255.0 : 0.0;
}

/* u is 0..1 through a segment. Every one of these has to be expressible in
 * ffmpeg's expression language as well, which is why they are polynomials. */
static double ease_apply(int ease, double u)
{
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    switch (ease) {
    case SS_EASE_HOLD:  return 0.0;
    case SS_EASE_IN:    return u * u;
    case SS_EASE_OUT:   return u * (2.0 - u);
    case SS_EASE_INOUT: return u < 0.5 ? 2.0 * u * u
                                       : 1.0 - 2.0 * (1.0 - u) * (1.0 - u);
    default:            return u;
    }
}

int ss_clip_prop_nkeys(const ss_clip *c, const char *key)
{
    int i, n = 0;
    for (i = 0; i < c->npkeys; i++)
        if (!strcmp(c->pkey[i].key, key)) n++;
    return n;
}

int ss_clip_prop_key(const ss_clip *c, const char *key, int i, ss_propkey *out)
{
    int j, n = 0;
    for (j = 0; j < c->npkeys; j++) {
        if (strcmp(c->pkey[j].key, key)) continue;
        if (n++ == i) { *out = c->pkey[j]; return 1; }
    }
    return 0;
}

int ss_clip_prop_moves(const ss_clip *c, const char *key)
{
    return ss_clip_prop_nkeys(c, key) >= 2;
}

int ss_clip_animated(const ss_clip *c)
{
    return c->npkeys > 0;
}

double ss_clip_prop_at(const ss_clip *c, const char *key, double tt)
{
    const cfield *f = cfind(key);
    const ss_propkey *a = NULL, *b = NULL;
    double v, q;
    int i;

    if (!f) return 0.0;
    /* The list is sorted, so the LAST key at or before tt is the one the
     * value is coming from and the first one after it is where it is going. */
    for (i = 0; i < c->npkeys; i++) {
        const ss_propkey *k = &c->pkey[i];
        if (strcmp(k->key, key)) continue;
        if (k->t <= tt)   a = k;
        else if (!b)      b = k;
    }
    if (!a && !b) return prop_static(c, f);
    if (!a)                             v = b->v;   /* before the first key */
    else if (!b || b->t <= a->t)        v = a->v;   /* after the last */
    else v = a->v + (b->v - a->v) *
             ease_apply(a->ease, (tt - a->t) / (b->t - a->t));

    if (v < f->lo) v = f->lo;
    if (v > f->hi) v = f->hi;
    q = prop_quant(key);
    if (q > 0.0) v = floor(v / q) * q;
    return v;
}

void ss_clip_prop_range(const ss_clip *c, const char *key, double *lo, double *hi)
{
    const cfield *f = cfind(key);
    int i, seen = 0;
    double a = 0, b = 0;

    if (!f) { *lo = *hi = 0; return; }
    for (i = 0; i < c->npkeys; i++) {
        const ss_propkey *k = &c->pkey[i];
        if (strcmp(k->key, key)) continue;
        if (!seen) { a = b = k->v; seen = 1; }
        if (k->v < a) a = k->v;
        if (k->v > b) b = k->v;
    }
    if (!seen) a = b = prop_static(c, f);
    if (a < f->lo) a = f->lo;
    if (b > f->hi) b = f->hi;
    *lo = a; *hi = b;
}

int ss_clip_prop_add(ss_clip *c, const char *key, double t, double v, int ease)
{
    const cfield *f = cfind(key);
    int i, at, n = 0;

    if (!f || !f->anim) return -1;
    if (ease < 0 || ease >= nease) ease = SS_EASE_LINEAR;
    if (t < 0) t = 0;
    if (v < f->lo) v = f->lo;
    if (v > f->hi) v = f->hi;

    /* A key at an instant that already has one REPLACES it. Otherwise moving
     * a slider with the playhead parked would grow the list a key at a time
     * and the second of two keys at the same moment would never be read. */
    for (i = 0; i < c->npkeys; i++) {
        if (strcmp(c->pkey[i].key, key)) continue;
        if (fabs(c->pkey[i].t - t) < 1e-6) {
            c->pkey[i].v = v;
            c->pkey[i].ease = ease;
            return n;
        }
        n++;
    }
    if (c->npkeys >= SS_MAX_PKEYS) return -1;

    /* Sorted by property, then by time, so every reader can stop looking. */
    for (at = 0; at < c->npkeys; at++) {
        int cmp = strcmp(c->pkey[at].key, key);
        if (cmp > 0) break;
        if (cmp == 0 && c->pkey[at].t > t) break;
    }
    memmove(&c->pkey[at + 1], &c->pkey[at],
            sizeof c->pkey[0] * (size_t)(c->npkeys - at));
    memset(&c->pkey[at], 0, sizeof c->pkey[at]);
    snprintf(c->pkey[at].key, sizeof c->pkey[at].key, "%s", key);
    c->pkey[at].t = t;
    c->pkey[at].v = v;
    c->pkey[at].ease = ease;
    c->npkeys++;

    for (i = 0, n = 0; i < c->npkeys; i++) {
        if (strcmp(c->pkey[i].key, key)) continue;
        if (i == at) return n;
        n++;
    }
    return -1;
}

int ss_clip_prop_remove(ss_clip *c, const char *key, int idx)
{
    int i, n = 0, hit = 0;

    for (i = 0; i < c->npkeys; ) {
        if (strcmp(c->pkey[i].key, key)) { i++; continue; }
        if (idx < 0 || n == idx) {
            memmove(&c->pkey[i], &c->pkey[i + 1],
                    sizeof c->pkey[0] * (size_t)(c->npkeys - i - 1));
            c->npkeys--;
            hit = 1;
            if (idx >= 0) return 0;
            continue;
        }
        n++;
        i++;
    }
    return hit ? 0 : -1;
}

/* ------------------------------------------------------- the fx stack -- */

/* Effects on a clip, applied in order after the grade.
 *
 * Order is the whole reason this is a list and not a set: a blur under a glow
 * and a glow under a blur are different pictures. `move` is how you say which.
 */
int ss_clip_fx_add(ss_clip *c, const char *name, int at)
{
    const ss_fx *r = ss_fx_find(name);
    ss_clip_fx n;
    int i;

    if (!r) return -1;
    if (c->nfx >= SS_MAX_FX) return -2;
    memset(&n, 0, sizeof n);
    snprintf(n.name, sizeof n.name, "%s", r->name);
    n.on = 1;
    /* Seeded from the recipe's own defaults, so an effect that has just been
     * added does what its author meant it to do rather than nothing. */
    for (i = 0; i < r->nparam; i++) n.val[i] = r->param[i].def;

    if (at < 0 || at > c->nfx) at = c->nfx;
    memmove(&c->fx[at + 1], &c->fx[at],
            sizeof c->fx[0] * (size_t)(c->nfx - at));
    c->fx[at] = n;
    c->nfx++;
    return at;
}

int ss_clip_fx_remove(ss_clip *c, int i)
{
    if (i < 0 || i >= c->nfx) return -1;
    memmove(&c->fx[i], &c->fx[i + 1],
            sizeof c->fx[0] * (size_t)(c->nfx - i - 1));
    c->nfx--;
    return 0;
}

int ss_clip_fx_move(ss_clip *c, int i, int to)
{
    ss_clip_fx t;
    if (i < 0 || i >= c->nfx || to < 0 || to >= c->nfx) return -1;
    t = c->fx[i];
    if (to > i) memmove(&c->fx[i], &c->fx[i + 1],
                        sizeof c->fx[0] * (size_t)(to - i));
    else        memmove(&c->fx[to + 1], &c->fx[to],
                        sizeof c->fx[0] * (size_t)(i - to));
    c->fx[to] = t;
    return 0;
}

int ss_clip_fx_set(ss_clip *c, int i, const char *key, double v)
{
    const ss_fx *r;
    int k;

    if (i < 0 || i >= c->nfx) return -1;
    if (!strcmp(key, "on")) { c->fx[i].on = v != 0; return 0; }
    r = ss_fx_find(c->fx[i].name);
    if (!r) return -1;
    for (k = 0; k < r->nparam; k++)
        if (!strcmp(r->param[k].key, key)) {
            if (v < r->param[k].lo) v = r->param[k].lo;
            if (v > r->param[k].hi) v = r->param[k].hi;
            c->fx[i].val[k] = v;
            return 0;
        }
    return -1;
}

int ss_clip_fx_get(const ss_clip *c, int i, const char *key, double *v)
{
    const ss_fx *r;
    int k;

    if (i < 0 || i >= c->nfx) return -1;
    if (!strcmp(key, "on")) { *v = c->fx[i].on; return 0; }
    r = ss_fx_find(c->fx[i].name);
    if (!r) return -1;
    for (k = 0; k < r->nparam; k++)
        if (!strcmp(r->param[k].key, key)) { *v = c->fx[i].val[k]; return 0; }
    return -1;
}

/* An effect whose recipe is not installed, kept whole so that saving the
 * project does not throw it away. It renders as nothing and lists as missing;
 * put the .synfx back and it comes straight to life. */
static int fx_add_unknown(ss_clip *c, const char *name, int on, const char *raw)
{
    if (c->nfx >= SS_MAX_FX) return -1;
    memset(&c->fx[c->nfx], 0, sizeof c->fx[0]);
    /* The precision, not a bare %s: the name comes off a line up to four
     * kilobytes long and at -O3 gcc can see that going into thirty-two bytes.
     * Saying the bound out loud both silences it and records that losing the
     * tail of a pathological name is the right answer to one. */
    snprintf(c->fx[c->nfx].name, sizeof c->fx[0].name, "%.*s",
             (int)(sizeof c->fx[0].name - 1), name);
    snprintf(c->fx[c->nfx].raw, sizeof c->fx[0].raw, "%.*s",
             (int)(sizeof c->fx[0].raw - 1), raw ? raw : "");
    c->fx[c->nfx].on = on;
    return c->nfx++;
}

/* Whether any effect on this clip can make a pixel transparent — a key, a
 * despill, anything the recipe declared `alpha 1` for. The chain has to be
 * rgba before one of those runs or the alpha it produces has nowhere to go. */
static int fx_needs_alpha(const ss_clip *c)
{
    int i;
    for (i = 0; i < c->nfx; i++) {
        const ss_fx *r;
        if (!c->fx[i].on) continue;
        r = ss_fx_find(c->fx[i].name);
        if (r && r->alpha) return 1;
    }
    return 0;
}

/* ------------------------------------------------------- a moving grade -- */

/* How finely a moving grade is sampled.
 *
 * A 3D LUT is a static table and ffmpeg cannot fade between two of them, so an
 * animated grade is rendered as a run of cubes, each gated to its own span
 * with the lut3d filter's `enable`. The count is what trades smoothness
 * against how many cubes get written and loaded: the visible step is the
 * change ACROSS THE WHOLE CLIP divided by this, so a two-stop exposure ramp
 * steps by 1/24th of a stop — under a code value, and nothing anyone can see.
 *
 * It is deliberately not per-second. A longer clip holding the same grade
 * move changes more slowly, so a fixed count keeps the step the same size
 * however long the shot is. */
#define SS_GRADE_STEPS 48

int ss_clip_grade_steps(const ss_clip *c)
{
    if (!c->has_grade) return 0;
    return c->nkeys >= 2 ? SS_GRADE_STEPS : 1;
}

/* The grade at a point through the clip, from the keyframes either side. */
static void grade_at(const ss_clip *c, double tt, ss_develop *out)
{
    int i;

    if (c->nkeys == 0) { *out = c->grade; return; }
    if (c->nkeys == 1) { *out = c->key[0].dev; return; }

    /* Held before the first and after the last. A grade that ramped away to
     * nothing outside its keyframes would be a surprise nobody asked for. */
    if (tt <= c->key[0].t)              { *out = c->key[0].dev; return; }
    if (tt >= c->key[c->nkeys - 1].t)   { *out = c->key[c->nkeys - 1].dev; return; }

    for (i = 0; i + 1 < c->nkeys; i++) {
        double a = c->key[i].t, b = c->key[i + 1].t;
        if (tt >= a && tt <= b) {
            double span = b - a;
            float m = span > 1e-9 ? (float)((tt - a) / span) : 0.0f;
            ss_develop_lerp(&c->key[i].dev, &c->key[i + 1].dev, m, out);
            return;
        }
    }
    *out = c->key[c->nkeys - 1].dev;
}

int ss_clip_grade_step(const ss_clip *c, int s, ss_develop *out)
{
    int n = ss_clip_grade_steps(c);
    double len;

    if (n <= 0) return 0;
    if (n == 1) { *out = c->nkeys ? c->key[0].dev : c->grade; return 1; }

    if (s < 0) s = 0;
    if (s >= n) s = n - 1;
    len = ss_clip_length(c);
    /* The CENTRE of the step, not its edge: a step that took its value from
     * the start of its span would lag the grade by half a step all the way
     * through, and the whole ramp would arrive late. */
    grade_at(c, len * ((double)s + 0.5) / (double)n, out);
    return 1;
}

int ss_clip_grade_step_at(const ss_clip *c, double tt)
{
    int n = ss_clip_grade_steps(c), s;
    double len = ss_clip_length(c);

    if (n <= 1) return 0;
    if (len <= 0) return 0;
    s = (int)(tt / len * (double)n);
    if (s < 0) s = 0;
    if (s >= n) s = n - 1;
    return s;
}

int ss_clip_key_add(ss_clip *c, double t, const ss_develop *d)
{
    int i, at;

    if (t < 0) t = 0;

    /* Same instant means REPLACE. Two keyframes at one time is a grade with
     * two opinions about the same frame, and the interpolation between them
     * divides by nothing. */
    for (i = 0; i < c->nkeys; i++)
        if (c->key[i].t > t - 1e-6 && c->key[i].t < t + 1e-6) {
            c->key[i].dev = *d;
            c->has_grade = 1;
            return i;
        }

    if (c->nkeys >= SS_MAX_KEYS) return -1;

    for (at = 0; at < c->nkeys && c->key[at].t < t; at++) ;
    for (i = c->nkeys; i > at; i--) c->key[i] = c->key[i - 1];
    c->key[at].t = t;
    c->key[at].dev = *d;
    c->nkeys++;
    c->has_grade = 1;
    return at;
}

int ss_clip_key_remove(ss_clip *c, int i)
{
    int k;
    if (i < 0 || i >= c->nkeys) return -1;
    for (k = i; k + 1 < c->nkeys; k++) c->key[k] = c->key[k + 1];
    c->nkeys--;
    /* The last keyframe leaving does not throw the grade away — it becomes
     * the static grade again, which is what it was before anyone keyed it. */
    if (c->nkeys == 0) c->has_grade = ss_develop_is_identity(&c->grade) ? 0 : 1;
    return 0;
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
    /* Off is -1 here, and a zeroed struct means "duck from track 0". */
    tr->duck_from = -1;
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

/* ---------------------------------------------------------- automation -- */

/* A track's fader, keyed. The same shape as a clip's parameter keys — sorted,
 * replace-at-the-same-instant, interpolated through eases the export can also
 * evaluate — and kept separate for one reason: these keys are in TIMELINE
 * seconds. A clip's are relative to the clip, because a clip can be moved and
 * its keys have to move with it; a track cannot be moved, and its fader is
 * ridden against the picture. */
int ss_track_key_add(ss_timeline *t, int track, double at, double db, int ease)
{
    ss_track *tr;
    int i, j;

    if (!t || track < 0 || track >= t->ntracks) return -1;
    tr = &t->track[track];
    if (at < 0) at = 0;
    if (db < -60.0) db = -60.0;
    if (db > 24.0) db = 24.0;
    if (ease < 0 || ease > SS_EASE_HOLD) ease = SS_EASE_LINEAR;

    for (i = 0; i < tr->nauto; i++)
        if (fabs(tr->akey[i].t - at) < 1e-9) {   /* replace, do not stack */
            tr->akey[i].v = db;
            tr->akey[i].ease = ease;
            return i;
        }
    if (tr->nauto >= SS_MAX_PKEYS) return -1;
    for (i = 0; i < tr->nauto && tr->akey[i].t < at; i++) ;
    for (j = tr->nauto; j > i; j--) tr->akey[j] = tr->akey[j - 1];
    snprintf(tr->akey[i].key, sizeof tr->akey[i].key, "gain");
    tr->akey[i].t = at;
    tr->akey[i].v = db;
    tr->akey[i].ease = ease;
    tr->nauto++;
    return i;
}

int ss_track_key_remove(ss_timeline *t, int track, int i)
{
    ss_track *tr;
    if (!t || track < 0 || track >= t->ntracks) return -1;
    tr = &t->track[track];
    if (i < 0 || i >= tr->nauto) return -1;
    memmove(&tr->akey[i], &tr->akey[i + 1],
            sizeof(ss_propkey) * (size_t)(tr->nauto - i - 1));
    tr->nauto--;
    return 0;
}

int ss_track_key_count(const ss_timeline *t, int track)
{
    if (!t || track < 0 || track >= t->ntracks) return 0;
    return t->track[track].nauto;
}

int ss_track_key_at(const ss_timeline *t, int track, int i, ss_propkey *out)
{
    if (!t || track < 0 || track >= t->ntracks) return -1;
    if (i < 0 || i >= t->track[track].nauto) return -1;
    *out = t->track[track].akey[i];
    return 0;
}

double ss_track_gain_at(const ss_timeline *t, int track, double time)
{
    const ss_track *tr;
    const ss_propkey *a = NULL, *b = NULL;
    int i;

    if (!t || track < 0 || track >= t->ntracks) return 0.0;
    tr = &t->track[track];
    /* No automation at all means the static fader, which is what every track
     * is until somebody rides one. */
    if (tr->nauto == 0) return tr->gain_db;

    for (i = 0; i < tr->nauto; i++) {
        if (tr->akey[i].t <= time) a = &tr->akey[i];
        else if (!b) b = &tr->akey[i];
    }
    /* Held before the first key and after the last, exactly as a clip's
     * properties are: a fader that extrapolated would arrive at the programme
     * already moving. */
    if (!a) return b ? b->v : tr->gain_db;
    if (!b || b->t <= a->t) return a->v;
    return a->v + (b->v - a->v) *
           ease_apply(a->ease, (time - a->t) / (b->t - a->t));
}

/* ------------------------------------------------------------- linking -- */

static int link_max(const ss_timeline *t)
{
    int i, j, m = 0;
    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++)
            if (t->track[i].clip[j].link > m) m = t->track[i].clip[j].link;
    return m;
}

int ss_timeline_link(ss_timeline *t, const int *track, const int *clip, int n)
{
    int i, g = 0;

    if (!t || !track || !clip || n < 2) return -1;
    /* Join an existing group rather than starting a new one, so linking a
     * third clip to a linked pair does not quietly split the pair in two. */
    for (i = 0; i < n; i++) {
        const ss_clip *c = clip_ptr(t, track[i], clip[i]);
        if (!c) return -1;
        if (c->link && !g) g = c->link;
    }
    if (!g) g = link_max(t) + 1;
    for (i = 0; i < n; i++) {
        ss_clip *c = clip_ptr(t, track[i], clip[i]);
        if (c) c->link = g;
    }
    return g;
}

int ss_timeline_unlink(ss_timeline *t, int track, int clip)
{
    ss_clip *c = clip_ptr(t, track, clip);
    if (!c) return -1;
    /* Only this one leaves. The others stay linked to each other, which is
     * what unlinking one shot from a group of three has to mean. */
    c->link = 0;
    return 0;
}

int ss_timeline_linked(const ss_timeline *t, int track, int clip)
{
    const ss_clip *c = clip_ptr((ss_timeline *)t, track, clip);
    int i, j, n = 0;
    if (!c) return 0;
    if (!c->link) return 1;
    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++)
            if (t->track[i].clip[j].link == c->link) n++;
    return n;
}

/* Run `fn` over every clip in this one's group, itself included. The group is
 * collected BEFORE anything is applied: an operation that changes a clip's
 * position while the walk is in progress would otherwise decide membership
 * from a half-moved timeline. */
static void link_each(ss_timeline *t, int track, int clip,
                      void (*fn)(ss_clip *, void *), void *user)
{
    ss_clip *c = clip_ptr(t, track, clip);
    int g, i, j;

    if (!c) return;
    g = c->link;
    if (!g) { fn(c, user); return; }
    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++)
            if (t->track[i].clip[j].link == g)
                fn(&t->track[i].clip[j], user);
}

static void link_shift(ss_clip *c, void *user)
{
    double d = *(double *)user;
    c->tl_in += d;
    if (c->tl_in < 0) c->tl_in = 0;
}

int ss_timeline_move(ss_timeline *t, int track, int clip, double tl_in)
{
    ss_clip *c = clip_ptr(t, track, clip);
    double want, delta;

    if (!c) return -1;
    want = tl_in < 0 ? 0 : tl_in;
    /* ⚠ The group moves by the DELTA, not to the destination. Moving every
     * linked clip TO the same instant would stack a shot's dialogue on top of
     * it instead of keeping the offset they were cut with. */
    delta = want - c->tl_in;
    if (c->link) link_each(t, track, clip, link_shift, &delta);
    else         c->tl_in = want;
    return 0;
}

/* One clip's trim, without the link walk. `ss_timeline_trim` is the entry
 * point and applies this to every clip in the group. */
/* What `delta` becomes for THIS clip once its own limits are applied — the
 * same clamping trim_one does, extracted so a link group can agree on one
 * number before any of it is committed.
 *
 * ⚠ Clamping per clip is what makes a group dangerous. Two linked clips with
 * different in points asked for a head trim of -99 clamp to DIFFERENT amounts
 * and come out of sync by the difference, silently. The group takes the most
 * restrictive answer and every member moves by that. */
static double trim_clamp(const ss_clip *c, int which, double delta)
{
    double sp = c->speed > 0 ? c->speed : 1.0, d = delta;

    if (which < 0) {
        if (c->src_in + d * sp < 0) d = -c->src_in / sp;
        if (c->tl_in + d < 0) d = -c->tl_in;
    }
    return d;
}

static int trim_one(ss_clip *c, int which, double delta)
{
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

int ss_timeline_trim(ss_timeline *t, int track, int clip, int which, double delta)
{
    ss_clip *c = clip_ptr(t, track, clip);
    int i, j, g;

    if (!c) return -1;
    g = c->link;
    if (!g) return trim_one(c, which, delta);

    /* ⚠ EVERY clip in the group or NONE of them. A head trim is refused when
     * it would run past a clip's source, and a group where one member refuses
     * and the others do not is a shot whose sound has just slipped out of
     * sync with it — silently, and by exactly the amount that was refused.
     * So the group is tried on copies first and only committed if all of them
     * can take it. */
    {
        ss_clip *members[SS_MAX_TRACKS * 8];
        ss_clip probe;
        double d = delta;
        int n = 0;
        for (i = 0; i < t->ntracks; i++)
            for (j = 0; j < t->track[i].nclips; j++)
                if (t->track[i].clip[j].link == g &&
                    n < (int)(sizeof members / sizeof members[0]))
                    members[n++] = &t->track[i].clip[j];

        /* One delta for the whole group: the most restrictive of what each
         * member would have clamped to. Smallest magnitude wins, and the sign
         * is the caller's. */
        for (i = 0; i < n; i++) {
            double dm = trim_clamp(members[i], which, d);
            if (fabs(dm) < fabs(d)) d = dm;
        }
        /* And then all of them or none: a member can still REFUSE outright —
         * a trim that would leave it with no frames at all — and a group where
         * one refuses and the rest move is the desync this exists to stop. */
        for (i = 0; i < n; i++) {
            probe = *members[i];
            if (trim_one(&probe, which, d) != 0) return -1;
        }
        for (i = 0; i < n; i++) trim_one(members[i], which, d);
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
    /* Keyframes are cut in two along with the picture, so each half keeps the
     * part of the move that happened inside it and a new key is planted at
     * the cut holding the value the grade had reached there. Without this the
     * second half would inherit every keyframe, including ones timed to
     * moments now in the first half. */
    if (c->nkeys >= 2) {
        ss_develop mid;
        int k;
        grade_at(c, off, &mid);
        a.nkeys = 0; b.nkeys = 0;
        for (k = 0; k < c->nkeys; k++) {
            if (c->key[k].t < off) ss_clip_key_add(&a, c->key[k].t, &c->key[k].dev);
            else ss_clip_key_add(&b, c->key[k].t - off, &c->key[k].dev);
        }
        ss_clip_key_add(&a, off, &mid);
        ss_clip_key_add(&b, 0.0, &mid);
    }

    /* Parameter keys are cut the same way, and for the same reason: each half
     * keeps the keys that fall inside it, and both get one planted at the cut
     * holding the value the property had reached there — so razoring a move
     * in two leaves two halves of the same move rather than two copies of it
     * starting over. */
    if (c->npkeys > 0) {
        ss_propkey src[SS_MAX_PKEYS];
        int k, n = c->npkeys;
        memcpy(src, c->pkey, sizeof src[0] * (size_t)n);
        a.npkeys = 0; b.npkeys = 0;
        for (k = 0; k < n; k++) {
            /* The value AT the cut, read before either half exists. */
            if (k == 0 || strcmp(src[k].key, src[k - 1].key)) {
                double v = ss_clip_prop_at(c, src[k].key, off);
                ss_clip_prop_add(&a, src[k].key, off, v, SS_EASE_LINEAR);
                ss_clip_prop_add(&b, src[k].key, 0.0, v, src[k].ease);
            }
            if (src[k].t < off)
                ss_clip_prop_add(&a, src[k].key, src[k].t, src[k].v, src[k].ease);
            else
                ss_clip_prop_add(&b, src[k].key, src[k].t - off, src[k].v,
                                 src[k].ease);
        }
    }

    /* An animated transform is cut in two along with the picture, so the move
     * continues across the cut instead of restarting at each half. */
    if (a.xf.animate) {
        float s, px, py, r;
        xform_at(c, off, len, &s, &px, &py, &r);
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
    ss_clip *c = clip_ptr(t, track, clip);
    int g;

    if (!c) return -1;
    g = c->link;
    if (g) {
        /* The whole group goes. ⚠ Highest index first, on each track, for the
         * same reason a multi-select delete does: removing a clip shifts every
         * index above it down, so deleting upward deletes the wrong clips
         * from the second one on. */
        int i, j;
        for (i = 0; i < t->ntracks; i++)
            for (j = t->track[i].nclips - 1; j >= 0; j--)
                if (t->track[i].clip[j].link == g) {
                    ss_track *k = &t->track[i];
                    memmove(&k->clip[j], &k->clip[j + 1],
                            sizeof(ss_clip) * (size_t)(k->nclips - j - 1));
                    k->nclips--;
                }
        return 0;
    }
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

/* The other direction: make ROOM. Every clip at or after `from` moves later
 * by `len`, which is what an insert edit does to everything downstream.
 *
 * ⚠ A separate function rather than a negative `len` to ss_timeline_ripple:
 * that one clamps at `from` so a gap can never be over-closed, and the clamp
 * is exactly wrong here — it would pile every pushed clip onto the insert
 * point. Two names, two rules, no flag to get the wrong way round. */
void ss_timeline_push(ss_timeline *t, int track, double from, double len)
{
    int j;
    if (track < 0 || track >= t->ntracks || len <= 0) return;
    for (j = 0; j < t->track[track].nclips; j++) {
        ss_clip *c = &t->track[track].clip[j];
        if (c->tl_in >= from - 1e-9) c->tl_in += len;
    }
}

/* -------------------------------------------------------- serialisation -- */

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
    if (t->master_db != 0.0f) fprintf(fp, "master\t%.3f\n", t->master_db);
    if (t->lufs < 0.0f) fprintf(fp, "loudness\t%.2f\n", t->lufs);
    /* The render range, when there is one. Absent means the whole timeline,
     * which is what a project without one has always meant. */
    if (t->range_out > t->range_in)
        fprintf(fp, "range\t%.6f\t%.6f\n", t->range_in, t->range_out);
    /* Before the tracks, because a marker belongs to the timeline and not to
     * anything on it — and because a reader that meets one after a `track`
     * line would have to know it is not a clip property. */
    for (i = 0; i < t->nmarkers; i++)
        fprintf(fp, "marker\t%.6f\t%d\t%s\n",
                t->marker[i].t, t->marker[i].colour, t->marker[i].text);

    for (i = 0; i < t->ntracks; i++) {
        const ss_track *tr = &t->track[i];
        fprintf(fp, "track\t%s\t%s\t%d\t%d\n",
                tr->type == SS_TRACK_VIDEO ? "video" : "audio",
                tr->name, tr->muted, tr->hidden);
        /* The fader, on its own optional line, so a timeline that nobody has
         * mixed reads exactly as it did before there was a mixer. */
        /* Automation, one line per key, under the track it rides. */
        {
            int q;
            for (q = 0; q < tr->nauto; q++)
                fprintf(fp, "auto\t%.6f\t%.4f\t%s\n",
                        tr->akey[q].t, tr->akey[q].v,
                        ss_ease_name(tr->akey[q].ease));
        }
        /* Ducking, on a line of its own for the same reason. */
        if (tr->duck_from >= 0)
            fprintf(fp, "duck\t%d\t%.3f\n", tr->duck_from, tr->duck);
        if (tr->gain_db != 0.0f || tr->pan != 0.0f || tr->solo)
            fprintf(fp, "mix\t%.3f\t%.4f\t%d\n",
                    tr->gain_db, tr->pan, tr->solo);
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
            /* Written for a LENGTH with no kind as well, because setting the
             * two in either order has to work: a length saved under `none`
             * used to vanish with the line it would have ridden on, so
             * choosing the length first and the kind second silently gave a
             * transition of zero. */
            if (c->trans != SS_TRANS_NONE || c->trans_dur > 0)
                /* The dip colour rides on the same line rather than a line of
                 * its own, and a reader that finds only two fields is reading
                 * a file written before there was a colour to dip through. */
                fprintf(fp, "trans\t%s\t%.6f\t%.4f\t%.4f\t%.4f\n",
                        ss_trans_name(c->trans), c->trans_dur,
                        c->trans_r, c->trans_g, c->trans_b);
            /* A link group, when there is one. A number on its own line
             * rather than a field on the clip record, because the record is
             * positional and a project written before links existed has one
             * column fewer. */
            if (c->link) fprintf(fp, "link\t%d\n", c->link);
            /* The sound chain, named fields, written only when something is
             * on. A clip with no processing keeps the record it always had. */
            if (c->nr_audio > 0 || c->gate > 0 || c->comp > 0 ||
                c->deess > 0 || c->fade_shape || *c->nr_model ||
                c->eq_db[0] || c->eq_db[1] || c->eq_db[2] ||
                c->eq_db[3] || c->eq_db[4] || c->eq_db[5]) {
                int q;
                fprintf(fp, "sound\tnr=%.3f\tgate=%.3f\tcomp=%.3f"
                            "\tthresh=%.3f\tdeess=%.3f\tshape=%s",
                        c->nr_audio, c->gate, c->comp, c->comp_thresh,
                        c->deess, ss_afade_name(c->fade_shape));
                for (q = 0; q < 6; q++)
                    fprintf(fp, "\teq%d=%.3f", q, c->eq_db[q]);
                /* Last, and only when there is one, so every project written
                 * before models existed reads back byte for byte. ⚠ Escaped
                 * like a caption: this is a PATH, and a tab in one would end
                 * the record halfway through. */
                if (*c->nr_model) {
                    char esc[600];
                    esc_text(c->nr_model, esc, sizeof esc);
                    fprintf(fp, "\tmodel=%s", esc);
                }
                fputc('\n', fp);
            }
            /* (the clip's own lines follow)
             * Retime and the stabiliser, named fields on one line, written
             * only when something is not the default — the same shape the
             * title's style line uses and for the same reason: a clip is
             * going to gain more of these. */
            if (c->reverse || c->freeze >= 0 || c->retime ||
                c->stab || c->stab_zoom > 0)
                fprintf(fp, "retime\treverse=%d\tfreeze=%.6f\tmode=%s"
                            "\tstab=%d\tsmooth=%.3f\tzoom=%.3f\n",
                        c->reverse, c->freeze, ss_retime_name(c->retime),
                        c->stab, c->stab_smooth, c->stab_zoom);
            if (c->kind == SS_CLIP_SOLID || c->col_a > 0.0f)
                fprintf(fp, "solid\t%.4f\t%.4f\t%.4f\t%.4f\n",
                        c->col_r, c->col_g, c->col_b, c->col_a);
            if (c->kind == SS_CLIP_TITLE) {
                char cap[1100];
                /* The caption is LAST on the line so it may contain anything
                 * — except the two bytes this format is made of. A line break
                 * inside a caption would end the record halfway through, so
                 * it travels as \n and comes back as a newline. */
                esc_text(c->text, cap, sizeof cap);
                fprintf(fp, "text\t%.4f\t%.4f\t%.4f\t%.4f\t%s\t%s\n",
                        c->text_size, c->text_r, c->text_g, c->text_b,
                        textpos_name(c->text_pos), cap);
                /* How it is DRAWN, on a line of its own with named fields.
                 * Named because a title gained seven properties in one
                 * release and will gain more: a reader that finds a key it
                 * does not know ignores it, and one that finds none of them
                 * is reading a project made before any of this existed and
                 * gets the defaults it was rendered with. Written only when
                 * something is not the default, so a plain caption's record
                 * is the line it always was. */
                if (*c->text_font || c->text_weight ||
                    c->text_border != 0.045f || c->text_shadow > 0 ||
                    c->text_box > 0 || c->text_line != 0.25f || c->text_roll > 0)
                    fprintf(fp, "style\tfont=%s\tweight=%s\tborder=%.4f"
                                "\tshadow=%.4f\tbox=%.4f\tline=%.4f\troll=%.4f\n",
                            c->text_font, ss_textweight_name(c->text_weight),
                            c->text_border, c->text_shadow, c->text_box,
                            c->text_line, c->text_roll);
            }
            /* The effect stack. One line each, in the order they apply,
             * with the knobs written by NAME — a recipe that gains a
             * parameter must not shift the meaning of a project saved before
             * it had one. */
            {
                int k, q;
                for (k = 0; k < c->nfx; k++) {
                    const ss_fx *r = ss_fx_find(c->fx[k].name);
                    fprintf(fp, "fx\t%s\t%d", c->fx[k].name, c->fx[k].on);
                    if (!r) {
                        /* Not installed here: written back exactly as it was
                         * read, so the machine that HAS it still gets it. */
                        if (*c->fx[k].raw) fprintf(fp, "\t%s", c->fx[k].raw);
                    } else {
                        for (q = 0; q < r->nparam; q++)
                            fprintf(fp, "\t%s=%g", r->param[q].key,
                                    c->fx[k].val[q]);
                    }
                    fprintf(fp, "\n");
                }
            }
            /* Parameter keys, one line each: a name, a time, a value and
             * how it leaves. Flat rather than nested because unlike a grade
             * key there is nothing to nest — the whole key IS the line. */
            {
                int k;
                for (k = 0; k < c->npkeys; k++)
                    fprintf(fp, "anim\t%s\t%.6f\t%.6f\t%s\n",
                            c->pkey[k].key, c->pkey[k].t, c->pkey[k].v,
                            ss_ease_name(c->pkey[k].ease));
            }
            if (c->has_grade && c->nkeys == 0) {
                /* Indented so the reader can tell a grade line belongs to the
                 * clip above it without needing a nesting syntax. */
                fprintf(fp, "grade\t%d\n", j);
                ss_develop_write(&c->grade, fp);
                fprintf(fp, "endgrade\n");
            }
            /* Keyframes are written as their own blocks rather than folded
             * into the grade block, so a file with none is byte for byte what
             * it was before keyframes existed. */
            {
                int k;
                for (k = 0; k < c->nkeys; k++) {
                    fprintf(fp, "key\t%d\t%.6f\n", k, c->key[k].t);
                    ss_develop_write(&c->key[k].dev, fp);
                    fprintf(fp, "endkey\n");
                }
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
        } else if (!strncmp(line, "master\t", 7)) {
            t->master_db = (float)atof(line + 7);
        } else if (!strncmp(line, "loudness\t", 9)) {
            t->lufs = (float)atof(line + 9);
        } else if (!strncmp(line, "range\t", 6)) {
            char *f[2];
            if (tabsplit(line + 6, f, 2) == 2) {
                t->range_in  = atof(f[0]);
                t->range_out = atof(f[1]);
            }
        } else if (!strncmp(line, "marker\t", 7)) {
            char *f[3];
            /* ONCE. tabsplit writes NULs over the tabs it finds, so a second
             * call on the same buffer sees one field and the note silently
             * becomes an empty string — which reads as a marker somebody
             * placed without typing anything. */
            int nf = tabsplit(line + 7, f, 3);
            if (nf >= 2)
                ss_timeline_mark(t, atof(f[0]), atoi(f[1]), nf >= 3 ? f[2] : "");
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
        } else if (!strncmp(line, "auto\t", 5) && cur_track >= 0) {
            char *f[3];
            if (tabsplit(line + 5, f, 3) == 3) {
                int e = ss_ease_value(f[2]);
                ss_track_key_add(t, cur_track, atof(f[0]), atof(f[1]),
                                 e < 0 ? SS_EASE_LINEAR : e);
            }
        } else if (!strncmp(line, "duck\t", 5) && cur_track >= 0) {
            char *f[2];
            if (tabsplit(line + 5, f, 2) == 2) {
                t->track[cur_track].duck_from = atoi(f[0]);
                t->track[cur_track].duck      = (float)atof(f[1]);
            }
        } else if (!strncmp(line, "mix\t", 4) && cur_track >= 0) {
            float g = 0.0f, pan = 0.0f;
            int solo = 0;
            sscanf(line + 4, "%f %f %d", &g, &pan, &solo);
            t->track[cur_track].gain_db = g;
            t->track[cur_track].pan = pan;
            t->track[cur_track].solo = solo;
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
            char *f[5];
            int v, n = tabsplit(line + 6, f, 5);
            if (n >= 2 && (v = ss_trans_value(f[0])) >= 0) {
                cc->trans = v;
                cc->trans_dur = atof(f[1]);
                if (n >= 5) {
                    cc->trans_r = (float)atof(f[2]);
                    cc->trans_g = (float)atof(f[3]);
                    cc->trans_b = (float)atof(f[4]);
                }
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
                unesc_text(f[5], cc->text, sizeof cc->text);
            }
        } else if (!strncmp(line, "link\t", 5) && cc) {
            cc->link = atoi(line + 5);
        } else if (!strncmp(line, "sound\t", 6) && cc) {
            char *f[20];
            int nf = tabsplit(line + 6, f, 20), q;
            for (q = 0; q < nf; q++) {
                char *eq = strchr(f[q], '=');
                const char *v;
                if (!eq) continue;
                *eq = '\0';
                v = eq + 1;
                if      (!strcmp(f[q], "nr"))     cc->nr_audio    = (float)atof(v);
                else if (!strcmp(f[q], "gate"))   cc->gate        = (float)atof(v);
                else if (!strcmp(f[q], "comp"))   cc->comp        = (float)atof(v);
                else if (!strcmp(f[q], "thresh")) cc->comp_thresh = (float)atof(v);
                else if (!strcmp(f[q], "deess"))  cc->deess       = (float)atof(v);
                else if (!strcmp(f[q], "model"))
                    unesc_text(v, cc->nr_model, sizeof cc->nr_model);
                else if (!strcmp(f[q], "shape"))  { int w = ss_afade_value(v);
                                                    if (w >= 0) cc->fade_shape = w; }
                else if (!strncmp(f[q], "eq", 2) && f[q][2] >= '0' && f[q][2] <= '5')
                    cc->eq_db[f[q][2] - '0'] = (float)atof(v);
            }
        } else if (!strncmp(line, "retime\t", 7) && cc) {
            char *f[16];
            int nf = tabsplit(line + 7, f, 16), q;
            for (q = 0; q < nf; q++) {
                char *eq = strchr(f[q], '=');
                const char *v;
                if (!eq) continue;
                *eq = '\0';
                v = eq + 1;
                if      (!strcmp(f[q], "reverse")) cc->reverse = atoi(v);
                else if (!strcmp(f[q], "freeze"))  cc->freeze  = atof(v);
                else if (!strcmp(f[q], "mode"))  { int m = ss_retime_value(v);
                                                   if (m >= 0) cc->retime = m; }
                else if (!strcmp(f[q], "stab"))    cc->stab    = atoi(v);
                else if (!strcmp(f[q], "smooth"))  cc->stab_smooth = (float)atof(v);
                else if (!strcmp(f[q], "zoom"))    cc->stab_zoom   = (float)atof(v);
            }
        } else if (!strncmp(line, "style\t", 6) && cc) {
            /* Named fields, each optional. An unknown key is skipped rather
             * than failing the line: this file is going to gain title
             * properties, and a project written by a newer build has to stay
             * readable by an older one. */
            char *f[16];
            int nf = tabsplit(line + 6, f, 16), q;
            for (q = 0; q < nf; q++) {
                char *eq = strchr(f[q], '=');
                const char *v;
                if (!eq) continue;
                *eq = '\0';
                v = eq + 1;
                if      (!strcmp(f[q], "font"))   snprintf(cc->text_font,
                                                           sizeof cc->text_font, "%s", v);
                else if (!strcmp(f[q], "weight")) { int w = ss_textweight_value(v);
                                                    if (w >= 0) cc->text_weight = w; }
                else if (!strcmp(f[q], "border")) cc->text_border = (float)atof(v);
                else if (!strcmp(f[q], "shadow")) cc->text_shadow = (float)atof(v);
                else if (!strcmp(f[q], "box"))    cc->text_box    = (float)atof(v);
                else if (!strcmp(f[q], "line"))   cc->text_line   = (float)atof(v);
                else if (!strcmp(f[q], "roll"))   cc->text_roll   = (float)atof(v);
            }
        } else if (!strncmp(line, "fx\t", 3) && cc) {
            char raw[256] = "", *f[2 + SS_MAX_FX_PARAMS];
            int nf, q, at;
            /* The parameter text is kept before tabsplit gets at it, because
             * tabsplit writes NULs over the tabs it finds and an effect this
             * machine cannot render has to be written back exactly. */
            {
                char *p = strchr(line + 3, '\t');
                if (p) p = strchr(p + 1, '\t');
                if (p) snprintf(raw, sizeof raw, "%s", p + 1);
            }
            nf = tabsplit(line + 3, f, 2 + SS_MAX_FX_PARAMS);
            if (nf >= 2 && ss_fx_find(f[0])) {
                at = ss_clip_fx_add(cc, f[0], -1);
                if (at >= 0) {
                    cc->fx[at].on = atoi(f[1]) != 0;
                    for (q = 2; q < nf; q++) {
                        char *eq = strchr(f[q], '=');
                        if (!eq) continue;
                        *eq = '\0';
                        /* A knob the recipe no longer has is DROPPED rather
                         * than refused: an effect rewritten since the project
                         * was saved still loads, with whatever it kept. */
                        ss_clip_fx_set(cc, at, f[q], atof(eq + 1));
                    }
                }
            } else if (nf >= 2) {
                fx_add_unknown(cc, f[0], atoi(f[1]) != 0, raw);
            }
        } else if (!strncmp(line, "anim\t", 5) && cc) {
            char *f[4];
            if (tabsplit(line + 5, f, 4) == 4)
                ss_clip_prop_add(cc, f[0], atof(f[1]), atof(f[2]),
                                 ss_ease_value(f[3]));
        } else if (!strncmp(line, "key\t", 4)) {
            char buf[16384];
            size_t used = 0;
            double kt = 0;
            FILE *ms;
            { char *f[2]; if (tabsplit(line + 4, f, 2) == 2) kt = atof(f[1]); }
            while (fgets(line, sizeof line, fp)) {
                if (!strncmp(line, "endkey", 6)) break;
                if (used + strlen(line) + 1 >= sizeof buf) break;
                strcpy(buf + used, line);
                used += strlen(line);
            }
            buf[used] = '\0';
            if (cc) {
                ss_develop d;
                ss_develop_reset(&d);
                ms = fmemopen(buf, used, "r");
                if (ms) {
                    ss_develop_read(&d, ms);
                    fclose(ms);
                    ss_clip_key_add(cc, kt, &d);
                }
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

static void grade_path(char *out, size_t n, const char *dir,
                       int track, int idx, int step, int nsteps);

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

/* ---- the same curve, as something ffmpeg will evaluate ----
 *
 * `tv` is an expression giving seconds into the CLIP in whatever filter this
 * is going into: `on/25` inside zoompan, `t-3.5` in an overlay reading
 * timeline time, plain `t` in a chain that has been zeroed. The shape is a
 * nest of ifs, one per segment, and it MUST be the same arithmetic
 * ss_clip_prop_at does or the render and the monitor part company.
 *
 * Half-open comparisons, never `between`: a frame landing exactly on a
 * boundary that satisfies two segments is the bug that made an animated grade
 * apply twice for one frame a second. */
static void seg_expr(strbuf *b, const char *tv, const ss_propkey *a,
                     const ss_propkey *n)
{
    double d = n->t - a->t, dv = n->v - a->v;

    if (a->ease == SS_EASE_HOLD || d <= 0 || dv == 0.0) {
        sb_add(b, "%.6f", a->v);
        return;
    }
    switch (a->ease) {
    case SS_EASE_IN:
        sb_add(b, "(%.6f+(%.6f)*pow(((%s)-%.6f)/%.6f,2))",
               a->v, dv, tv, a->t, d);
        break;
    case SS_EASE_OUT:
        sb_add(b, "(%.6f+(%.6f)*(((%s)-%.6f)/%.6f)*(2-((%s)-%.6f)/%.6f))",
               a->v, dv, tv, a->t, d, tv, a->t, d);
        break;
    case SS_EASE_INOUT:
        sb_add(b, "(%.6f+(%.6f)*if(lt(((%s)-%.6f)/%.6f,0.5),"
                  "2*(((%s)-%.6f)/%.6f)*(((%s)-%.6f)/%.6f),"
                  "1-2*(1-((%s)-%.6f)/%.6f)*(1-((%s)-%.6f)/%.6f)))",
               a->v, dv, tv, a->t, d, tv, a->t, d, tv, a->t, d,
               tv, a->t, d, tv, a->t, d);
        break;
    default:
        sb_add(b, "(%.6f+(%.6f)*((%s)-%.6f)/%.6f)", a->v, dv, tv, a->t, d);
        break;
    }
}

/* Returns 0 and writes nothing if the property is not keyed. */
/* The key list itself, as an expression. Split out of prop_expr so a TRACK's
 * automation — which is not a clip property and never will be, because its
 * axis is timeline seconds — generates the identical shape rather than a
 * second implementation of the same interpolation. */
static int keys_expr(strbuf *b, const ss_propkey *k, int n, const char *tv)
{
    int i;

    if (n <= 0) return 0;
    if (n == 1) { sb_add(b, "%.6f", k[0].v); return 1; }

    /* if(lt(t,t0), v0,                       — before the first key it HOLDS
     *   if(lt(t,t1), <segment 0>,            — which is what prop_at does;
     *     if(lt(t,t2), <segment 1>, ... vn)))  without this line the first
     * segment's formula would extrapolate backwards and the export would
     * arrive at the clip already moving. */
    sb_add(b, "if(lt((%s),%.6f),%.6f,", tv, k[0].t, k[0].v);
    for (i = 0; i + 1 < n; i++) {
        sb_add(b, "if(lt((%s),%.6f),", tv, k[i + 1].t);
        seg_expr(b, tv, &k[i], &k[i + 1]);
        sb_add(b, ",");
    }
    sb_add(b, "%.6f", k[n - 1].v);
    for (i = 0; i + 1 < n; i++) sb_add(b, ")");
    sb_add(b, ")");
    return 1;
}

static int prop_expr(strbuf *b, const ss_clip *c, const char *key, const char *tv)
{
    ss_propkey k[SS_MAX_PKEYS];
    int n = ss_clip_prop_nkeys(c, key), i;

    if (n <= 0) return 0;
    for (i = 0; i < n; i++) ss_clip_prop_key(c, key, i, &k[i]);
    return keys_expr(b, k, n, tv);
}

/* A track's fader as an expression, evaluated at TIMELINE time.
 *
 * ⚠ The clip's chain runs in CLIP seconds — the trim and the speed setpts are
 * behind us by the time this is spliced in — so the variable handed to the
 * track's keys has to be shifted by where the clip starts. A track fader
 * generated against `t` alone would ride the automation from the top of the
 * programme for every clip on it, which for a clip nine minutes in is the
 * wrong nine minutes of the curve. */
static int track_gain_expr(strbuf *b, const ss_track *tr, double tl_in)
{
    char tv[64];
    if (tr->nauto <= 0) return 0;
    snprintf(tv, sizeof tv, "(t)%+.6f", tl_in);
    return keys_expr(b, tr->akey, tr->nauto, tv);
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
int ss_timeline_bake(const ss_timeline *t, const char *dir, double at)
{
    int i, j, n = 0;

    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++) {
            const ss_clip *c = &t->track[i].clip[j];
            char p[4300];
            FILE *fp;
            double off = at - c->tl_in, len = ss_clip_length(c);
            int steps = ss_clip_grade_steps(c), s, s0, s1;

            /* A single frame needs the clips on screen and nothing else. The
             * monitor used to bake every cube of every clip on every scrub
             * frame; with a moving grade that is dozens of cubes per frame,
             * for a picture that uses one. */
            if (at >= 0 && (off < 0 || off >= len || len <= 0)) continue;

            if (steps > 0) {
                if (at >= 0) { s0 = ss_clip_grade_step_at(c, off); s1 = s0 + 1; }
                else         { s0 = 0; s1 = steps; }
                for (s = s0; s < s1; s++) {
                    ss_develop d;
                    ss_clip_grade_step(c, s, &d);
                    grade_path(p, sizeof p, dir, i, j, s, steps);
                    fp = fopen(p, "w");
                    if (!fp) return -1;
                    ss_lut_write(&d, 33, fp, "synstudio clip grade");
                    fclose(fp);
                    n++;
                }
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
            int steps = ss_clip_grade_steps(&t->track[i].clip[j]), s;
            for (s = 0; s < steps; s++) {
                grade_path(p, sizeof p, dir, i, j, s, steps);
                remove(p);
            }
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
 * only a transform that can leave the frame, a partial opacity, and a title.
 * A transition does not either, any more: it composites onto a transparent
 * layer of its own. */
static int needs_alpha(const ss_clip *c)
{
    return c->opacity < 1.0f ||
           c->xf.rotate != 0.0f || c->xf.rotate2 != 0.0f ||
           c->kind == SS_CLIP_TITLE || fx_needs_alpha(c);
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

/* ---- a keyed opacity, which is the one that cannot be an expression ----
 *
 * Nothing in ffmpeg multiplies alpha by an expression. What every filter DOES
 * take is a command at an instant, so a keyed opacity is sendcmd driving one
 * named colorchannelmixer: a value is set and held until the next one. The
 * steps are placed where the value crosses a CODE VALUE, so the staircase is
 * below what eight bits can show, and ss_clip_prop_at rounds down the same way
 * so the monitor is equal to this and not merely close to it.
 *
 * The budget is what keeps a long fade from writing ten thousand commands into
 * the graph string; a fade that hits it gets a coarser staircase rather than a
 * truncated one. */
#define ALPHA_CMD_BUDGET 600

static void chain_alpha(strbuf *b, const ss_clip *c, int id)
{
    int n = ss_clip_prop_nkeys(c, "opacity"), i, k, total = 0;
    ss_propkey pk[SS_MAX_PKEYS];
    double last = -1.0, scale = 1.0;
    int first = 1;

    if (n <= 0) return;
    for (i = 0; i < n; i++) ss_clip_prop_key(c, "opacity", i, &pk[i]);
    if (n == 1) {
        sb_add(b, ",format=rgba,colorchannelmixer=aa=%.6f",
               ss_clip_prop_at(c, "opacity", pk[0].t));
        return;
    }
    for (i = 0; i + 1 < n; i++) {
        double d = fabs(pk[i + 1].v - pk[i].v) * 255.0;
        total += (int)(d < 1 ? 1 : d) + 1;
    }
    if (total > ALPHA_CMD_BUDGET) scale = (double)ALPHA_CMD_BUDGET / total;

    sb_add(b, ",format=rgba,sendcmd=c='");
    for (i = 0; i + 1 < n; i++) {
        double t0 = pk[i].t, t1 = pk[i + 1].t;
        double d = fabs(pk[i + 1].v - pk[i].v) * 255.0 * scale;
        int steps = (int)(d < 1 ? 1 : d);
        if (t1 <= t0) continue;
        for (k = 0; k < steps; k++) {
            double tt = t0 + (t1 - t0) * k / steps;
            double v = ss_clip_prop_at(c, "opacity", tt);
            if (!first && fabs(v - last) < 1e-9) continue;
            /* The separator is escaped because a bare ; ends the filter chain
             * it is sitting in, and the graph then refuses to parse with a
             * message about the label that follows. */
            if (!first) sb_add(b, "\\;");
            sb_add(b, "%.6f colorchannelmixer@op%d aa %.6f", tt, id, v);
            first = 0;
            last = v;
        }
    }
    if (first) sb_add(b, "%.6f colorchannelmixer@op%d aa %.6f",
                      pk[0].t, id, ss_clip_prop_at(c, "opacity", pk[0].t));
    /* The starting value is the filter's own, not a command: a command at the
     * very first frame can land after that frame has already gone through. */
    sb_add(b, "',colorchannelmixer@op%d=aa=%.6f", id,
           ss_clip_prop_at(c, "opacity", pk[0].t));
}

/* One of the two position offsets, as an overlay x/y expression in TIMELINE
 * time — which is what the overlay sees, the clip starting at tl_in.
 *
 * Position is the overlay's job in every case now. It used to be zoompan's
 * whenever the transform was animated, and zoompan positions by moving its
 * CROP WINDOW: sliding the window right shows what is to the right, so the
 * picture goes LEFT. The monitor, which has no zoompan, moved it right. An
 * animated pan therefore came out of the export mirrored, and no test caught
 * it because the one that measures the picture only measured the zoom. */
static void chain_pos(strbuf *b, const ss_clip *c, const char *key,
                      const char *tv, double len, float a, float bb)
{
    if (prop_expr(b, c, key, tv)) return;
    if (c->xf.animate && len > 0)
        sb_add(b, "%.5f+(%.5f)*clip((%s)/%.6f,0,1)",
               (double)a, (double)(bb - a), tv, len);
    else
        sb_add(b, "%.5f", (double)a);
}

/* The seconds-into-the-clip expression for a stream whose own clock starts
 * `shift` seconds into it. The main overlay reads timeline time, so its shift
 * is minus the clip's position; a transition segment starts partway through
 * the outgoing clip, so its shift is positive. */
static void clip_tv(char *out, size_t n, double shift)
{
    snprintf(out, n, "(t)%+.6f", shift);
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

/* A credit roll is the vertical placement replaced by a ramp: the block
 * starts one frame height below the bottom edge and climbs at `roll` screen
 * heights a second, so a roll long enough to read is a small number and the
 * length of the clip decides how much of it is seen.
 *
 * `at` is why this is not simply an expression. In the export the caption is
 * drawn into a stream whose `t` is clip seconds, so `t` is the ramp; in the
 * monitor it is drawn into a graph holding ONE frame at t=0, and an
 * expression there would put every roll back at its starting position while
 * the export scrolled it. So the monitor is handed the same ramp evaluated as
 * a NUMBER at the instant it is showing — the same bargain chain_grade_at
 * strikes, and the reason a scrub and a render agree about a moving title. */
static void text_roll_y(const ss_clip *c, double at, char *y, size_t yn)
{
    if (at >= 0)
        snprintf(y, yn, "(h-(h*%.6f))", at * (double)c->text_roll);
    else
        snprintf(y, yn, "(h-(t*h*%.6f))", (double)c->text_roll);
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

/* ONE place that decides what a cube is called. The baker writes them and
 * both graph builders reference them, and a name invented twice is a name
 * that eventually differs by an underscore and fails at export time with a
 * message about a missing file. */
static void grade_path(char *out, size_t n, const char *dir,
                       int track, int idx, int step, int nsteps)
{
    if (nsteps <= 1) snprintf(out, n, "%s/grade_%d_%d.cube", dir, track, idx);
    else snprintf(out, n, "%s/grade_%d_%d_s%d.cube", dir, track, idx, step);
}

/* `at` is seconds into the clip for a single frame, or negative for the whole
 * thing. A frame wants ONE cube — the step under it, ungated — because the
 * baker only wrote that one. Emitting the full run of gated stages here made
 * the monitor reference forty-seven cubes that were never written, and ffmpeg
 * refuses a graph it cannot open a file for, so the frame simply failed and
 * the window went on showing the last one that worked. */
static void chain_grade_at(strbuf *fc, const ss_clip *c, const char *lutdir,
                           int track, int idx, double at)
{
    char lp[2048], esc[4200];
    int n = ss_clip_grade_steps(c), s;
    ss_develop mid;

    if (n <= 0) return;

    if (at >= 0 && n > 1) {
        s = ss_clip_grade_step_at(c, at);
        grade_path(lp, sizeof lp, lutdir, track, idx, s, n);
        esc_filter(lp, esc, sizeof esc);
        sb_add(fc, ",lut3d=file='%s':interp=tetrahedral", esc);
        ss_clip_grade_step(c, n / 2, &mid);
        chain_spatial(fc, &mid);
        return;
    }

    if (n == 1) {
        grade_path(lp, sizeof lp, lutdir, track, idx, 0, 1);
        esc_filter(lp, esc, sizeof esc);
        sb_add(fc, ",lut3d=file='%s':interp=tetrahedral", esc);
    } else {
        /* One cube per step, each gated to its own span. lut3d passes a frame
         * through untouched when its `enable` is false, so a frame meets every
         * filter in the run and is coloured by exactly one of them. */
        /* HALF-OPEN spans, and this is not tidiness.
         *
         * `between(t,a,b)` is inclusive at BOTH ends, so a frame landing
         * exactly on a boundary satisfies two of them — and because these are
         * chained, both cubes apply and the grade is composed with itself. A
         * two-stop ramp showed it as single frames of roughly double the
         * grade, once a second, exactly where a step boundary fell on a frame
         * time. Every twelfth frame at 48 steps over four seconds, which is
         * to say: whenever the arithmetic happens to be exact.
         *
         * The first span reaches back before zero and the last runs past the
         * end, so a frame slightly outside the clip's nominal length from
         * rounding still gets graded rather than none of them matching. */
        double len = ss_clip_length(c);
        for (s = 0; s < n; s++) {
            double t0 = len * s / n, t1 = len * (s + 1) / n;
            grade_path(lp, sizeof lp, lutdir, track, idx, s, n);
            esc_filter(lp, esc, sizeof esc);
            sb_add(fc, ",lut3d=file='%s':interp=tetrahedral:enable='", esc);
            if (s == 0)          sb_add(fc, "lt(t,%.6f)", t1);
            else if (s == n - 1) sb_add(fc, "gte(t,%.6f)", t0);
            else                 sb_add(fc, "gte(t,%.6f)*lt(t,%.6f)", t0, t1);
            sb_add(fc, "'");
        }
    }

    /* The spatial half is taken from the MIDDLE of the clip and held.
     * Sharpening, noise reduction and a vignette are ffmpeg filters whose
     * arguments are numbers, not expressions — there is no honest way to
     * animate them here, so keyframing them is not offered rather than
     * silently ignored. Colour is what a grade keyframe is for. */
    ss_clip_grade_step(c, n / 2, &mid);
    chain_spatial(fc, &mid);
}

static void chain_grade(strbuf *fc, const ss_clip *c, const char *lutdir,
                        int track, int idx)
{
    chain_grade_at(fc, c, lutdir, track, idx, -1.0);
}

/* A title's caption, drawn over whatever the clip already is. The border is
 * not decoration: white text lands on a white sky often enough that a caption
 * without one is unreadable on the take you most wanted to label. */
static void chain_title_at(strbuf *fc, const ss_timeline *t, const ss_clip *c,
                           const char *dir, int track, int idx, double at)
{
    char tp[2048], esc[4200], fesc[1024], x[64], y[96], col[32], plate[32];
    int size, bw;
    if (c->kind != SS_CLIP_TITLE) return;
    snprintf(tp, sizeof tp, "%s/text_%d_%d.txt", dir, track, idx);
    esc_filter(tp, esc, sizeof esc);
    /* A family NAME resolved to a FILE, once, before the graph is built. The
     * family may be one this machine has not got — a project travels — and
     * what comes back then is the default face rather than a path that does
     * not open, the same way a missing LUT renders as no LUT. */
    esc_filter(ss_font_file(c->text_font, c->text_weight), fesc, sizeof fesc);

    size = (int)(c->text_size * t->h + 0.5f);
    if (size < 1) size = 1;

    text_xy(c->text_pos, x, sizeof x, y, sizeof y);
    if (c->text_roll > 0.0f) text_roll_y(c, at, y, sizeof y);
    hexcol(c->text_r, c->text_g, c->text_b, 1.0f, col, sizeof col);

    /* expansion=none. `textfile=` gets the caption past the filtergraph's
     * quoting, but drawtext STILL runs its own %%{...} expansion over whatever
     * it read, so a caption containing a percent sign fails the graph with
     * "Stray %%" — at export time, long after the title was typed. Nothing
     * here wants a strftime, and a caption is literal text by definition. */
    sb_add(fc, ",drawtext=fontfile='%s':textfile='%s':expansion=none"
               ":fontcolor=%s:fontsize=%d:x=%s:y=%s:line_spacing=%d",
           fesc, esc, col, size, x, y,
           (int)(size * (c->text_line > 0 ? c->text_line : 0.0f)));

    /* Which edge the LINES line up on inside the block, which only matters
     * once a caption has more than one of them. drawtext lays a multi-line
     * caption out left-aligned and then places the BLOCK, so a centred
     * two-line title is a centred block of left-aligned text — right for a
     * lower third, wrong for a credit roll.
     *
     * ⚠ Asked for, not assumed: text_align reached drawtext in 2024, and an
     * option this machine's ffmpeg does not know fails the whole graph rather
     * than being ignored. Where it is missing the layout is what it always
     * was, which is the correct thing to lose. */
    if (strchr(c->text, '\n') && ss_ffmpeg_filter_has("drawtext", "text_align")) {
        int colmn = (c->text_pos >= 0 && c->text_pos <= 8) ? c->text_pos % 3 : 1;
        sb_add(fc, ":text_align=%s", colmn == 0 ? "L" : colmn == 1 ? "C" : "R");
    }

    /* The plate goes on FIRST in the option list but drawtext paints it
     * under the words either way. Its colour is the clip's background colour,
     * which is the one a title already has — a caption on a plate is the
     * same object as a caption over a solid, just smaller. */
    if (c->text_box > 0.0f) {
        hexcol(c->col_r, c->col_g, c->col_b, c->text_box, plate, sizeof plate);
        sb_add(fc, ":box=1:boxcolor=%s:boxborderw=%d",
               plate, (int)(size * 0.35f + 0.5f));
    }
    /* Shadow before border, because a border drawn over a shadow is what
     * every other titler does and the other order reads as a smear. */
    if (c->text_shadow > 0.0f) {
        int off = (int)(size * c->text_shadow + 0.5f);
        if (off < 1) off = 1;
        sb_add(fc, ":shadowx=%d:shadowy=%d:shadowcolor=0x000000@0.75", off, off);
    }
    /* The outline is not decoration: white text lands on a white sky often
     * enough that a caption without one is unreadable on the take you most
     * wanted to label. It is a setting now, and zero means somebody chose
     * that — so nothing is emitted rather than a one-pixel minimum. */
    bw = c->text_border > 0.0f ? (int)(size * c->text_border + 1.5f) : 0;
    if (bw > 0)
        sb_add(fc, ":borderw=%d:bordercolor=0x000000@0.65", bw);
}

/* The export, where a title's own `t` is clip seconds and a roll can be an
 * expression. */
static void chain_title(strbuf *fc, const ss_timeline *t, const ss_clip *c,
                        const char *dir, int track, int idx)
{
    chain_title_at(fc, t, c, dir, track, idx, -1.0);
}

/* What gets written OVER the delivered picture: a timecode, the file's own
 * name, or both.
 *
 * Deliberately not part of the cut. A burn-in is for a review copy — it says
 * which version somebody is looking at and where in it they are, and it must
 * never survive into the master, which is why it lives on the delivery
 * arguments and not in the document.
 *
 * ⚠ The timecode starts at the RANGE, not at zero. The trim above resets
 * timestamps so a ranged render begins at 0, and a burn-in reading 00:00:00
 * for a clip that starts nine minutes into the timeline is worse than none at
 * all — it is a wrong answer to the exact question it was added to answer.
 */
static void chain_burnin(strbuf *fc, const ss_timeline *t, int burn,
                         const char *out, double range_in)
{
    char fesc[1024];
    int size = (int)(t->h * 0.035f + 0.5f);

    if (!burn) return;
    if (size < 10) size = 10;
    esc_filter(ss_font_file("", SS_FW_REGULAR), fesc, sizeof fesc);

    if (burn & SS_BURN_TIMECODE) {
        long f = (long)(range_in * t->fps + 0.5);
        long fr = (long)(t->fps + 0.5);
        int hh, mm, ss2, ff;
        if (fr < 1) fr = 25;
        ff = (int)(f % fr); f /= fr;
        ss2 = (int)(f % 60);  f /= 60;
        mm = (int)(f % 60);   f /= 60;
        hh = (int)f;
        /* The colons are escaped twice over: once for the filtergraph's own
         * option splitting, and drawtext then reads the value it is handed. */
        sb_add(fc, ",drawtext=fontfile='%s':timecode='%02d\\:%02d\\:%02d\\:%02d'"
                   /* Bottom RIGHT, not bottom centre: a subtitle is bottom
                    * centre by convention and the two landed on top of each
                    * other on the first review copy this made. */
                   ":rate=%.6g:fontcolor=white:fontsize=%d:box=1:boxcolor=black@0.5"
                   ":boxborderw=%d:x=w-text_w-%d:y=h-text_h-%d",
               fesc, hh, mm, ss2, ff, t->fps, size, size / 3, size, size);
    }
    if (burn & SS_BURN_NAME) {
        const char *base = out ? strrchr(out, '/') : NULL;
        char esc[2100];
        esc_filter(base ? base + 1 : (out ? out : ""), esc, sizeof esc);
        sb_add(fc, ",drawtext=fontfile='%s':text='%s':expansion=none"
                   ":fontcolor=white:fontsize=%d:box=1:boxcolor=black@0.5"
                   ":boxborderw=%d:x=%d:y=%d",
               fesc, esc, size, size / 3, size, size);
    }
}

/* The sound of one clip, in the order a dialogue chain is actually built.
 *
 * Clean it, shape it, control it — noise reduction, gate, EQ, compressor,
 * de-esser. Anything else is an argument about taste; that order is not,
 * because each stage is deciding what the next one gets to work on. A gate
 * after a compressor gates a signal whose quiet parts have already been
 * lifted, and a de-esser before an EQ chases sibilance the EQ is about to
 * move.
 *
 * Every one of these is skipped entirely at zero. A filter in the graph set
 * to do nothing still costs a pass over every sample, and afftdn in
 * particular is not cheap.
 */
static void chain_clip_audio(strbuf *fc, const ss_clip *c)
{
    static const int eq_hz[6] = { 60, 200, 600, 2000, 6000, 12000 };
    int i;

    /* First, because everything downstream is deciding what to do about a
     * signal and the noise is not part of it. `nf` is where afftdn thinks the
     * noise floor is; -40 dB is a room, not a hiss. */
    if (c->nr_audio > 0.0f) {
        char model[1024];
        /* ⚠ arnndn is a TRAINED denoiser and nothing without its model, so a
         * clip naming one this machine has not got falls through to no
         * filter at all rather than to afftdn: silently substituting a
         * different denoiser is how a delivery comes back sounding unlike
         * every take that was approved. `timeline get` says whether the name
         * resolved, so the window can show it the way it shows a font this
         * machine has not got. */
        if (*c->nr_model) {
            if (ss_rnn_resolve(c->nr_model, model, sizeof model) == 0)
                sb_add(fc, ",arnndn=m=%s:mix=%.3f", model,
                       (double)c->nr_audio / 100.0);
        } else {
            sb_add(fc, ",afftdn=nr=%.2f:nf=-40", (double)c->nr_audio * 0.97);
        }
    }

    /* The gate is on the ORIGINAL dynamics, before anything lifts the quiet
     * parts. 0..100 maps onto a threshold from silence to -20 dBFS, which is
     * as far up as a gate can go before it starts eating speech. */
    if (c->gate > 0.0f) {
        double th = pow(10.0, (-60.0 + (double)c->gate * 0.4) / 20.0);
        sb_add(fc, ",agate=threshold=%.6f:ratio=4:attack=10:release=200", th);
    }

    /* Six bands, six biquads. `anequalizer` does this in one filter and does
     * it PER CHANNEL — a stereo clip needs every band written twice, and a
     * mono one written once, which is a shape that gets out of step with the
     * source the first time somebody swaps a take. A chain of `equalizer`
     * applies to whatever channels arrive. */
    for (i = 0; i < 6; i++)
        if (c->eq_db[i] != 0.0f)
            sb_add(fc, ",equalizer=f=%d:t=q:w=1.2:g=%.3f",
                   eq_hz[i], (double)c->eq_db[i]);

    /* One knob: the amount drives the ratio and the make-up together, because
     * a compressor that squashes 4:1 and does not give the level back is a
     * compressor that just made everything quieter. */
    if (c->comp > 0.0f) {
        double amt = ss_clampf(c->comp, 0.0f, 100.0f) / 100.0;
        double ratio = 1.0 + amt * 7.0;                  /* 1:1 .. 8:1 */
        double th = pow(10.0, (double)ss_clampf(c->comp_thresh, -60.0f, 0.0f) / 20.0);
        double makeup = 1.0 + amt * 1.5;
        sb_add(fc, ",acompressor=threshold=%.6f:ratio=%.3f:attack=20"
                   ":release=250:makeup=%.3f", th, ratio, makeup);
    }

    /* Last, on the signal that is actually going out: a compressor lifts
     * sibilance along with everything else, so de-essing before it would be
     * de-essing the wrong level. */
    if (c->deess > 0.0f)
        sb_add(fc, ",deesser=i=%.4f", (double)c->deess / 100.0);
}

/* A tempo that atempo will actually accept.
 *
 * ⚠ atempo's range is 0.5 to 100 and the SPEED property's range is 0.1 to 10,
 * so every clip slower than half speed with a sound track on it failed the
 * WHOLE export — "Value 0.200000 for parameter 'tempo' out of range", at the
 * end of the render, on a graph that had been building for minutes. Two
 * halvings multiply, so a chain of them reaches any slowdown: 0.2 is
 * 0.5 x 0.5 x 0.8.
 *
 * The other end needs nothing: 10x is inside atempo's range on its own. */
static void chain_atempo(strbuf *fc, double tempo)
{
    int guard = 0;
    if (tempo <= 0) return;
    while (tempo < 0.5 && guard++ < 8) {
        sb_add(fc, ",atempo=0.5");
        tempo /= 0.5;
    }
    if (tempo < 0.5) tempo = 0.5;       /* 0.5^8 is far below the range floor */
    if (tempo > 100.0) tempo = 100.0;
    if (tempo != 1.0) sb_add(fc, ",atempo=%.6f", tempo);
}

/* A speed RAMP as a setpts expression.
 *
 * `T` here is seconds into the source segment, because the input was seeked
 * with -ss and its timestamps start at zero — which is the same axis
 * ss_clip_retime samples on. Each segment contributes one branch: inside it
 * the output time is where the segment started plus however far into it we
 * are, divided by the speed that segment runs at.
 *
 * Nested rather than summed because ffmpeg's expression language has no
 * piecewise construct other than if(), and generated from the SAME table the
 * length and the seek come from, so the three cannot disagree. */
static void chain_ramp_setpts(strbuf *fc, const ss_retime_seg *seg, int n)
{
    int i;
    sb_add(fc, ",setpts='");
    for (i = 0; i < n - 1; i++)
        sb_add(fc, "if(lt(T,%.6f),%.6f+(T-%.6f)/%.6f,",
               seg[i].src1, seg[i].out0, seg[i].src0, seg[i].speed);
    sb_add(fc, "%.6f+(T-%.6f)/%.6f", seg[n - 1].out0, seg[n - 1].src0,
           seg[n - 1].speed);
    for (i = 0; i < n - 1; i++) sb_add(fc, ")");
    sb_add(fc, "/TB'");
}

/* Whether a ramp's speeds all sit inside atempo's own range.
 *
 * A ramp drives ONE atempo with sendcmd, and a chain of them cannot be
 * commanded as a unit — so a ramp that leaves 0.5..2 is one this program
 * cannot pitch the sound for. It says so and drops that clip's audio rather
 * than exporting a graph that fails, or one that desyncs. */
static int ramp_audio_possible(const ss_retime_seg *seg, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (seg[i].speed < 0.5 || seg[i].speed > 2.0) return 0;
    return 1;
}

/* Stabilise, reverse, freeze — the three things that happen to the SOURCE
 * frames, before anything is scaled or framed.
 *
 * ⚠ Order and position both matter. vidstabtransform's numbers are pixels of
 * the video that was ANALYSED, so it has to run at the source's own size —
 * after a scale it would move the picture by the wrong amount. And a reverse
 * has to be behind the transform ramp and the zoompan, or the move would run
 * backwards too, which is not what anybody means by playing a shot
 * backwards.
 *
 * ⚠ `reverse` buffers the whole segment in memory. That is affordable for the
 * seconds-long clip a reverse is usually wanted on and is not affordable for
 * a four-minute one; there is no streaming way to do it.
 */
static void chain_source_ops(strbuf *fc, const ss_timeline *t, const ss_clip *c,
                             int track, int idx)
{
    if (c->kind != SS_CLIP_MEDIA) return;

    if (c->stab && *t->stabdir) {
        char trf[2048], esc[4200];
        snprintf(trf, sizeof trf, "%s/stab_%d_%d.trf", t->stabdir, track, idx);
        esc_filter(trf, esc, sizeof esc);
        sb_add(fc, "vidstabtransform=input='%s':smoothing=%d:zoom=%.3f"
                   ":optzoom=0:crop=black,", esc,
               (int)(c->stab_smooth + 0.5f), (double)c->stab_zoom);
    }

    if (c->freeze >= 0.0) {
        /* ONE frame, held. The input was seeked to the instant, so the frame
         * wanted is the first one out of it; `loop` then repeats it and
         * setpts gives the copies timestamps of their own, because a looped
         * frame arrives carrying the timestamp of the original and a run of
         * identical ones is a stream every muxer drops back to one frame. */
        double len = ss_clip_length(c);
        int frames = (int)(len * t->fps + 1.5);
        if (frames < 1) frames = 1;
        sb_add(fc, "select='eq(n\\,0)',fps=%.6g,loop=loop=%d:size=1:start=0"
                   ",setpts=N/%.6g/TB,", t->fps, frames, t->fps);
    } else if (c->reverse) {
        sb_add(fc, "reverse,");
    }
}

/* The effect stack, spliced into a clip's chain.
 *
 * Each effect breaks OUT of the comma-chain into a fragment of its own,
 * because a recipe can branch — a glow splits the picture, blurs one half and
 * screens it back over the other — and a branch cannot live inside a chain.
 * The chain so far ends in a label, the fragment runs between that label and
 * the next, and the chain picks up again from there.
 *
 * `id` makes every label in every instance unique. Two clips wearing the same
 * effect are the same recipe twice in one graph, and two `[a]`s is a graph
 * ffmpeg refuses to parse.
 *
 * An effect naming a recipe that is not installed is SKIPPED here rather than
 * failing the render — the command layer says so before it starts, which is
 * where a person can do something about it. */
static void chain_fx(strbuf *fc, const ss_clip *c, int id)
{
    char in[40], out[40], buf[4096];
    int i, n = 0, any = 0;

    for (i = 0; i < c->nfx; i++) {
        const ss_fx *r = c->fx[i].on ? ss_fx_find(c->fx[i].name) : NULL;
        if (r) any = 1;
    }
    if (!any) return;

    /* The chain so far is closed into a label; each effect runs from the
     * previous label to the next, so nothing has to be bridged; and the last
     * one comes back into a `null`, which is a filter and can therefore have
     * `,whatever` appended to it. A bare label cannot: `[x],setpts=...` is a
     * graph ffmpeg refuses, and `[a][b]` between two effects reads as one
     * filter taking two inputs. */
    sb_add(fc, "[fx%d_0]", id);
    for (i = 0; i < c->nfx; i++) {
        const ss_fx *r;
        if (!c->fx[i].on) continue;
        r = ss_fx_find(c->fx[i].name);
        if (!r) continue;
        snprintf(in,  sizeof in,  "fx%d_%d", id, n);
        snprintf(out, sizeof out, "fx%d_%d", id, n + 1);
        if (ss_fx_expand(r, c->fx[i].val, SS_MAX_FX_PARAMS, id * 64 + n,
                         in, out, buf, sizeof buf) != 0)
            continue;
        sb_add(fc, ";%s", buf);
        n++;
    }
    sb_add(fc, ";[fx%d_%d]null", id, n);
}

/* ---- one side of a transition, as a project-sized layer ----
 *
 * xfade takes two streams and they must be the same size. A clip chain is not
 * that size: it is FITTED — 1280 wide for a scale of 0.67 — and the overlay
 * that composites it is what applies the position. So a transition has to
 * build the frame the overlay would have built, on transparent, and hand that
 * to xfade. Transparent and not black, because a transition on an upper track
 * has to let the tracks under it through.
 *
 * `shift` is how far into the clip this side begins: zero for the incoming
 * clip, and the distance from the outgoing clip's start to the cut for the
 * other. Everything timed in clip seconds — a position, a keyed move — is
 * shifted with it, so a clip transitioning in the middle of a pan keeps
 * panning while it does. */
static void chain_trans_side(strbuf *fc, const ss_timeline *t, const ss_clip *c,
                             int src, double shift, double dur, int id,
                             const char *side)
{
    double len = ss_clip_length(c);
    float s0, px0, py0, r0, s1, px1, py1, r1;
    char tv[64];

    xform_at(c, 0.0, len, &s0, &px0, &py0, &r0);
    xform_at(c, len, len, &s1, &px1, &py1, &r1);
    clip_tv(tv, sizeof tv, shift);

    sb_add(fc, ";color=c=black@0:s=%dx%d:r=%.6g:d=%.6f,format=rgba[%sb%d]",
           t->w, t->h, t->fps, dur, side, id);
    sb_add(fc, ";[s%d]trim=start=%.6f:end=%.6f,setpts=PTS-STARTPTS[%sc%d]",
           src, shift, shift + dur, side, id);
    sb_add(fc, ";[%sb%d][%sc%d]overlay=eof_action=pass:x='(W-w)/2+(",
           side, id, side, id);
    chain_pos(fc, c, "xform.x", tv, len, px0, px1);
    sb_add(fc, ")*W':y='(H-h)/2+(");
    chain_pos(fc, c, "xform.y", tv, len, py0, py1);
    sb_add(fc, ")*H'[%s%d]", side, id);
}

/* An empty side. A transition at the head of a track has nothing to come
 * from, which is not an error — it is a shot arriving over the tracks below. */
static void chain_trans_empty(strbuf *fc, const ss_timeline *t, double dur,
                              int id, const char *side)
{
    sb_add(fc, ";color=c=black@0:s=%dx%d:r=%.6g:d=%.6f,format=rgba[%s%d]",
           t->w, t->h, t->fps, dur, side, id);
}

/* The two sides joined. Every kind but one is xfade doing the whole job; `dip`
 * is a cut at the halfway point under a colour that rises and falls, which is
 * what dipping through black has always been and what no xfade transition
 * does for an arbitrary colour. */
static void chain_trans_join(strbuf *fc, const ss_timeline *t, const ss_clip *c,
                             double dur, int id)
{
    const char *xf = ss_trans_xfade(c->trans);
    char col[32];

    if (c->trans == SS_TRANS_DIP || !xf) {
        /* Two dissolves through a colour, rather than a colour laid over a cut
         * with a fade on it. `fade` steps by FRAME INDEX while the monitor
         * would work its alpha out from the time — close, and measurably not
         * the same, which is the kind of drift between the two builders this
         * program does not allow. Two xfades are the same filter on both
         * sides at the same progress.
         *
         * The second half's incoming picture is TRIMMED before it goes in:
         * xfade plays its second input from that input's own beginning, so
         * without the trim the shot would come out of the dip half a
         * transition behind where it should be. */
        hexcol(c->trans_r, c->trans_g, c->trans_b, 1.0f, col, sizeof col);
        sb_add(fc, ";color=c=%s:s=%dx%d:r=%.6g:d=%.6f,format=rgba[xc%d]",
               col, t->w, t->h, t->fps, dur, id);
        sb_add(fc, ";[xa%d][xc%d]xfade=transition=fade:duration=%.6f:offset=0"
                   "[xh%d]", id, id, dur / 2.0, id);
        sb_add(fc, ";[xb%d]trim=start=%.6f,setpts=PTS-STARTPTS[xbt%d]",
               id, dur / 2.0, id);
        sb_add(fc, ";[xh%d][xbt%d]xfade=transition=fade:duration=%.6f"
                   ":offset=%.6f[xj%d]", id, id, dur / 2.0, dur / 2.0, id);
        return;
    }
    sb_add(fc, ";[xa%d][xb%d]xfade=transition=%s:duration=%.6f:offset=0[xj%d]",
           id, id, xf, dur, id);
}

/* The stage of the background chain an overlay reads. Its own name, because
 * the chain now grows by more than one layer per clip — a transition is a
 * layer too — and `bg` where `bg<n>` was meant leaves every intermediate
 * stage unconnected and ffmpeg refuses the whole graph. */
static const char *prevlab(char *buf, size_t n, int layer)
{
    snprintf(buf, n, "bg%d", layer);
    return buf;
}

/* ------------------------------------------------- where a transition is -- */

/* A transition belongs to the INCOMING clip and needs a clip to come FROM.
 *
 * The partner is the clip on the same track that is still playing where this
 * one starts — the latest such, if several overlap. There may be none, and
 * that is not an error: a transition at the head of a track plays against
 * nothing, which for a slide is the shot sliding in over whatever the lower
 * tracks are showing.
 *
 * The length is clamped to what is actually there: never longer than the
 * incoming clip, and never longer than the outgoing one has left. A transition
 * that outlives its partner would be blending against a stream that has ended,
 * which ffmpeg answers by holding the last frame — a freeze in the middle of a
 * dissolve, and nothing in the graph says so.
 *
 * Both graph builders call this. If a scrub and an export ever disagree about
 * WHEN a transition happens, it is because one of them did not. */
static int trans_span(const ss_timeline *t, int tr, int j,
                      double *t0, double *dur, int *partner)
{
    const ss_track *k = &t->track[tr];
    const ss_clip *c = &k->clip[j];
    double len = ss_clip_length(c), d = c->trans_dur;
    int i, best = -1;

    if (c->trans == SS_TRANS_NONE || d <= 0 || len <= 0) return 0;
    if (d > len) d = len;

    for (i = 0; i < k->nclips; i++) {
        const ss_clip *o = &k->clip[i];
        double ol = ss_clip_length(o);
        if (i == j || ol <= 0) continue;
        if (o->tl_in <= c->tl_in + 1e-9 && o->tl_in + ol > c->tl_in + 1e-9) {
            if (best < 0 || o->tl_in > k->clip[best].tl_in) best = i;
        }
    }
    if (best >= 0) {
        const ss_clip *a = &k->clip[best];
        double avail = a->tl_in + ss_clip_length(a) - c->tl_in;
        if (d > avail) d = avail;
    }
    if (d <= 0) return 0;
    *t0 = c->tl_in;
    *dur = d;
    *partner = best;
    return 1;
}

/* The other end of the same relationship: clip `j` is what some clip on its
 * track transitions FROM, starting `st` seconds into it and lasting `dur`.
 *
 * The export needs the first fact before it builds j's chain, because a
 * partner's picture is wanted TWICE — once where it plays and once inside the
 * transition — and a stream can only be read once without a split. It needs
 * the second for the SOUND: a transition that dissolves the picture and cuts
 * the sound is half a transition, and two clips overlapping with no fades
 * between them do not cross, they ADD. */
static int trans_out(const ss_timeline *t, int tr, int j,
                     double *st, double *dur)
{
    double t0, d;
    int i, p;
    for (i = 0; i < t->track[tr].nclips; i++)
        if (trans_span(t, tr, i, &t0, &d, &p) && p == j) {
            *st = t0 - t->track[tr].clip[j].tl_in;
            *dur = d;
            return 1;
        }
    return 0;
}

static int is_trans_partner(const ss_timeline *t, int tr, int j)
{
    double st, d;
    return trans_out(t, tr, j, &st, &d);
}

/* The clip whose transition covers `time`, if this clip is the one coming in.
 * The monitor's question, and the answer has to be the export's. */
static int trans_at(const ss_timeline *t, int tr, int j, double time,
                    double *p, double *dur, int *partner)
{
    double t0, d;
    if (!trans_span(t, tr, j, &t0, &d, partner)) return 0;
    if (time < t0 || time >= t0 + d) return 0;
    *p = d > 0 ? (time - t0) / d : 1.0;
    *dur = d;
    return 1;
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

/* ------------------------------------------------------------- markers -- */

/* Kept in time order, so the window draws them in the order it walks them and
 * "the next marker" is the next one in the array rather than a search. */
int ss_timeline_mark(ss_timeline *t, double at, int colour, const char *text)
{
    int i, j;

    if (!t || t->nmarkers >= SS_MAX_MARKERS) return -1;
    if (at < 0) at = 0;
    if (colour < 0) colour = 0;
    if (colour > 5) colour = 5;

    for (i = 0; i < t->nmarkers; i++) if (t->marker[i].t > at) break;
    for (j = t->nmarkers; j > i; j--) t->marker[j] = t->marker[j - 1];

    memset(&t->marker[i], 0, sizeof t->marker[i]);
    t->marker[i].t = at;
    t->marker[i].colour = colour;
    if (text) snprintf(t->marker[i].text, sizeof t->marker[i].text, "%s", text);
    t->nmarkers++;
    return i;
}

int ss_timeline_unmark(ss_timeline *t, int i)
{
    int j;
    if (!t || i < 0 || i >= t->nmarkers) return -1;
    for (j = i; j < t->nmarkers - 1; j++) t->marker[j] = t->marker[j + 1];
    t->nmarkers--;
    return 0;
}

/* ------------------------------------------------------------- history -- */

/* ------------------------------------------------------------- versions -- */

/* ⚠ A NAME, not a path. A version is named by a person and the name becomes a
 * file, so a `/` or a `..` in it would write outside the project's own
 * directory. Refused rather than sanitised: quietly turning "a/b" into "a_b"
 * means a later `restore a/b` cannot find what it just saved. */
static int version_name_ok(const char *name)
{
    size_t i;
    if (!name || !*name || strlen(name) > 63) return 0;
    if (name[0] == '.') return 0;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return 0;
    }
    return 1;
}

int ss_version_path(const char *proj, const char *name, char *out, size_t n)
{
    if (!out || !version_name_ok(name)) return -1;
    snprintf(out, n, "%s.versions/%s", proj, name);
    return 0;
}

int ss_version_save(const char *proj, const char *name, const char *when)
{
    char dir[4300], path[4400], meta[4500];
    FILE *in, *fp;
    char buf[8192];
    size_t got;

    if (!version_name_ok(name)) return -2;
    snprintf(dir, sizeof dir, "%s.versions", proj);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    if (ss_version_path(proj, name, path, sizeof path) != 0) return -1;

    /* A byte copy of the document as it is on disk, not a re-serialisation of
     * what happens to be in memory. A version has to be the thing that was
     * there, including anything a newer build would have rewritten. */
    in = fopen(proj, "rb");
    if (!in) return -1;
    fp = fopen(path, "wb");
    if (!fp) { fclose(in); return -1; }
    while ((got = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, got, fp) != got) { fclose(in); fclose(fp); return -1; }
    fclose(in);
    if (fclose(fp) != 0) return -1;

    /* When, as the caller reports it. Not re-derived from the file's mtime:
     * a copy takes the time of the copy, and what a person wants to see is
     * when they decided to keep it. */
    snprintf(meta, sizeof meta, "%s.when", path);
    fp = fopen(meta, "w");
    if (fp) { fprintf(fp, "%s\n", when ? when : ""); fclose(fp); }
    return 0;
}

int ss_version_list(const char *proj, ss_version *out, int max)
{
    char dir[4300];
    DIR *d;
    struct dirent *e;
    int n = 0;

    snprintf(dir, sizeof dir, "%s.versions", proj);
    d = opendir(dir);
    if (!d) return 0;                   /* no directory is no versions */
    while ((e = readdir(d)) != NULL && n < max) {
        char meta[4500];
        FILE *fp;
        size_t len = strlen(e->d_name);
        if (e->d_name[0] == '.') continue;
        if (len > 5 && !strcmp(e->d_name + len - 5, ".when")) continue;
        /* ⚠ Skipped rather than truncated. ss_version_save refuses a name
         * longer than 63 bytes, so anything longer in here was not written by
         * this program — and listing it under a shortened name would offer a
         * version that `restore` then cannot find. */
        if (len >= sizeof out[n].name) continue;
        memcpy(out[n].name, e->d_name, len + 1);
        out[n].when[0] = '\0';
        /* The VALIDATED copy, not the raw dirent: it is already known to fit
         * in 64 bytes, which is both the correct thing to use and what lets
         * the compiler see that this path cannot be truncated. */
        snprintf(meta, sizeof meta, "%s/%s.when", dir, out[n].name);
        if ((fp = fopen(meta, "r")) != NULL) {
            if (fgets(out[n].when, sizeof out[n].when, fp)) {
                size_t w = strlen(out[n].when);
                while (w && (out[n].when[w-1] == '\n' || out[n].when[w-1] == '\r'))
                    out[n].when[--w] = '\0';
            }
            fclose(fp);
        }
        n++;
    }
    closedir(d);
    return n;
}

/* `<project>.undo/NNNN`, one whole document per state, and a `head` file
 * naming which of them is the one on disk.
 *
 * Whole documents rather than inverse operations: a .syntl is a few kilobytes
 * of text, every verb is a separate process that loads-changes-saves, and
 * there is no session to hold a stack in. An inverse per verb would be twenty
 * more things that can be wrong in one direction only.
 */
#define HIST_MAX 100

static void hist_dir(const char *proj, char *out, size_t n)
{
    snprintf(out, n, "%s.undo", proj);
}

static void hist_file(const char *proj, int i, char *out, size_t n)
{
    snprintf(out, n, "%s.undo/%04d", proj, i);
}

/* pos, first, last. Absent or unreadable means no history at all, which is the
 * ordinary state of a project nobody has edited yet. */
static int hist_read(const char *proj, int *pos, int *first, int *last)
{
    char path[4400];
    FILE *fp;
    int a = 0, b = 0, c = 0;

    *pos = *first = *last = 0;
    snprintf(path, sizeof path, "%s.undo/head", proj);
    fp = fopen(path, "r");
    if (!fp) return -1;
    if (fscanf(fp, "%d %d %d", &a, &b, &c) != 3) { fclose(fp); return -1; }
    fclose(fp);
    *pos = a; *first = b; *last = c;
    return 0;
}

static int hist_write_head(const char *proj, int pos, int first, int last)
{
    char path[4400], tmp[4500];
    FILE *fp;

    snprintf(path, sizeof path, "%s.undo/head", proj);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    fp = fopen(tmp, "w");
    if (!fp) return -1;
    fprintf(fp, "%d %d %d\n", pos, first, last);
    if (fclose(fp) != 0) { unlink(tmp); return -1; }
    return rename(tmp, path);
}

static int file_copy(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b;
    char buf[8192];
    size_t n;
    int rc = 0;

    if (!a) return -1;
    b = fopen(to, "wb");
    if (!b) { fclose(a); return -1; }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n) { rc = -1; break; }
    if (ferror(a)) rc = -1;
    fclose(a);
    if (fclose(b) != 0) rc = -1;
    if (rc != 0) unlink(to);
    return rc;
}

static int hist_snapshot(const char *proj, int index)
{
    char dst[4400];
    hist_file(proj, index, dst, sizeof dst);
    return file_copy(proj, dst);
}

int ss_history_seed(const char *proj)
{
    char dir[4300], path[4400];
    int pos, first, last;
    FILE *probe;

    if (!proj || !*proj) return -1;
    if (!hist_read(proj, &pos, &first, &last)) return 0;   /* already going */

    probe = fopen(proj, "r");
    if (!probe) return 0;      /* nothing on disk yet: `new` will seed it */
    fclose(probe);

    hist_dir(proj, dir, sizeof dir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    (void)path;
    if (hist_snapshot(proj, 1) != 0) return -1;
    return hist_write_head(proj, 1, 1, 1);
}

int ss_history_push(const char *proj)
{
    char dir[4300], dead[4400];
    int pos, first, last, i;

    if (!proj || !*proj) return -1;
    hist_dir(proj, dir, sizeof dir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;

    if (hist_read(proj, &pos, &first, &last) != 0) {
        /* A project with no history yet — `new` lands here. */
        if (hist_snapshot(proj, 1) != 0) return -1;
        return hist_write_head(proj, 1, 1, 1);
    }

    /* Everything after the current position is a future that just stopped
     * being reachable. Redo past an edit is not a thing any editor offers. */
    for (i = pos + 1; i <= last; i++) {
        hist_file(proj, i, dead, sizeof dead);
        unlink(dead);
    }
    last = pos;

    if (hist_snapshot(proj, pos + 1) != 0) return -1;
    pos++; last = pos;

    /* A hundred states of a few kilobytes each. The floor moves rather than
     * everything being renamed down, so a long session costs one unlink. */
    while (last - first + 1 > HIST_MAX) {
        hist_file(proj, first, dead, sizeof dead);
        unlink(dead);
        first++;
    }
    return hist_write_head(proj, pos, first, last);
}

static int hist_go(const char *proj, int delta)
{
    char src[4400];
    int pos, first, last;

    if (hist_read(proj, &pos, &first, &last) != 0) return 1;
    if (delta < 0 && pos <= first) return 1;
    if (delta > 0 && pos >= last) return 1;

    pos += delta;
    hist_file(proj, pos, src, sizeof src);
    /* Onto the project itself: the state IS the document, so there is nothing
     * to replay and nothing that can half-apply. */
    if (file_copy(src, proj) != 0) return -1;
    return hist_write_head(proj, pos, first, last) == 0 ? 0 : -1;
}

int ss_history_undo(const char *proj) { return hist_go(proj, -1); }
int ss_history_redo(const char *proj) { return hist_go(proj, +1); }

int ss_history_depth(const char *proj, int *undo, int *redo)
{
    int pos, first, last;
    if (undo) *undo = 0;
    if (redo) *redo = 0;
    if (hist_read(proj, &pos, &first, &last) != 0) return -1;
    if (undo) *undo = pos - first;
    if (redo) *redo = last - pos;
    return 0;
}

/* ---------------------------------------------------------------- mix -- */

int ss_track_shows(const ss_timeline *t, int i)
{
    if (!t || i < 0 || i >= t->ntracks) return 0;
    return !t->track[i].hidden;
}

int ss_track_sounds(const ss_timeline *t, int i)
{
    int k;

    if (!t || i < 0 || i >= t->ntracks) return 0;
    if (t->track[i].muted) return 0;
    /* Solo is a property of the whole timeline, not of the track holding the
     * flag: one soloed track silences every other one. */
    for (k = 0; k < t->ntracks; k++)
        if (t->track[k].solo) return t->track[i].solo;
    return 1;
}

/* Constant power, so a sound panned to the middle is not louder than the same
 * sound hard left. A linear law is 3dB up in the centre and every mix made
 * with one drifts quieter as things are panned out. */
static void pan_law(float pan, double *l, double *r)
{
    double a = ((double)pan + 1.0) * 3.14159265358979 / 4.0;
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    *l = cos(a);
    *r = sin(a);
}

/* ------------------------------------------------------------ formats -- */

/* What an export can come out as.
 *
 * ONE table, the same discipline as the develop settings and the clip
 * properties: the CLI takes a name from it, `timeline formats` prints it, and
 * the window builds its picker from that — so adding a format here is the
 * whole change, and the window cannot offer one the engine does not have.
 *
 * The extension is what a container needs to BE that container; ffmpeg picks
 * its muxer from the name it is given, so writing ProRes to a file called
 * .mp4 produces an mp4 that no NLE will read the way it was meant.
 */
static const ss_tl_format tl_formats[] = {
    { "mp4",    "mp4",  "libx264",    "aac",       "-preset", "medium", "-crf", "18",
      "yuv420p",     "mov_text", "H.264 and AAC — plays on everything" },
    { "mkv",    "mkv",  "libx264",    "aac",       "-preset", "medium", "-crf", "18",
      "yuv420p",     "srt",      "the same encode in Matroska" },
    { "mov",    "mov",  "libx264",    "aac",       "-preset", "medium", "-crf", "18",
      "yuv420p",     "mov_text", "the same encode in QuickTime" },
    { "h265",   "mp4",  "libx265",    "aac",       "-preset", "medium", "-crf", "22",
      "yuv420p",     "mov_text", "HEVC — half the size, fewer players" },
    { "webm",   "webm", "libvpx-vp9", "libopus",   "-b:v",    "0",      "-crf", "32",
      "yuv420p",     "webvtt",   "VP9 and Opus, for the web" },
    { "prores", "mov",  "prores_ks",  "pcm_s16le", "-profile:v", "3",   NULL,   NULL,
      "yuv422p10le", "mov_text", "ProRes 422 HQ — 10-bit, for grading on" },
    /* Image sequences. A folder of frames has nowhere to put a sound track,
     * so the audio codec is NULL and the graph leaves the mix unmapped —
     * which is also what makes `--out frames/f_%05d.png` a complete
     * instruction rather than half of one. */
    { "png",    "png",  "png",        NULL,        "-compression_level", "3", NULL, NULL,
      "rgb24",       NULL,       "a PNG sequence — lossless, huge, no sound" },
    { "exr",    "exr",  "exr",        NULL,        NULL,      NULL,     NULL,   NULL,
      "gbrpf32le",   NULL,       "an OpenEXR sequence — 32-bit float, linear" },
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

/* Sizes people deliver to, named for where they are going rather than for
 * their pixel count — which is how anybody actually picks one. */
static const ss_tl_preset tl_presets[] = {
    { "youtube-1080p", 1920, 1080, 0,  "1920x1080, the project's frame rate" },
    { "youtube-4k",    3840, 2160, 0,  "3840x2160" },
    { "youtube-720p",  1280,  720, 0,  "1280x720" },
    { "instagram",     1080, 1350, 30, "1080x1350 at 30 — the 4:5 feed post" },
    { "reel",          1080, 1920, 30, "1080x1920 at 30 — vertical, full screen" },
    { "square",        1080, 1080, 30, "1080x1080" },
    { "proxy",          960,  540, 0,  "960x540, for cutting on a slow machine" },
    { NULL, 0, 0, 0, NULL }
};

const ss_tl_preset *ss_timeline_presets(void) { return tl_presets; }

const ss_tl_preset *ss_timeline_preset(const char *name)
{
    int i;
    if (!name) return NULL;
    for (i = 0; tl_presets[i].name; i++)
        if (!strcmp(tl_presets[i].name, name)) return &tl_presets[i];
    return NULL;
}

void ss_timeline_range(const ss_timeline *t, double *in, double *out)
{
    double dur = ss_timeline_duration(t);
    double a = t->range_in, b = t->range_out;
    if (!(b > a)) { a = 0; b = dur; }        /* no range = the whole thing */
    if (a < 0) a = 0;
    if (b > dur) b = dur;
    if (!(b > a)) { a = 0; b = dur > 0 ? dur : 1.0; }
    if (in)  *in  = a;
    if (out) *out = b;
}

int ss_burn_value(const char *s)
{
    if (!s || !strcmp(s, "off") || !strcmp(s, "none")) return 0;
    if (!strcmp(s, "timecode")) return SS_BURN_TIMECODE;
    if (!strcmp(s, "name"))     return SS_BURN_NAME;
    if (!strcmp(s, "both"))     return SS_BURN_TIMECODE | SS_BURN_NAME;
    return -1;
}

const ss_tl_format *ss_timeline_formats(void)
{
    return tl_formats;
}

/* A name if one was given, otherwise the output's own extension, otherwise
 * mp4. Inferring from the extension is what makes `--out cut.webm` do the
 * obvious thing without anybody having to say it twice. */
const ss_tl_format *ss_timeline_format(const char *name, const char *out)
{
    const char *dot;
    int i;

    if (name && *name) {
        for (i = 0; tl_formats[i].name; i++)
            if (!strcasecmp(name, tl_formats[i].name)) return &tl_formats[i];
        return NULL;
    }
    dot = out ? strrchr(out, '.') : NULL;
    if (dot) {
        /* By extension, first match wins — .mov is H.264 unless ProRes was
         * asked for by name, which is the quieter of the two surprises. */
        for (i = 0; tl_formats[i].name; i++)
            if (!strcasecmp(dot + 1, tl_formats[i].ext)) return &tl_formats[i];
    }
    return &tl_formats[0];
}

int ss_timeline_ffmpeg(const ss_timeline *t, const char *out,
                       const char *lutdir, int preview,
                       const ss_tl_format *fmt, const char *subs,
                       int burn, const char *mark, char ***argv_out)
{
    strbuf fc = {0};
    char **av = NULL;
    int ac = 0, cap = 64;
    int i, j, input = 0, nvid = 0, naud = 0, nlayer = 0, nclip = 0;
    int voffs[SS_MAX_TRACKS], *vlab = NULL, *atrack = NULL;
    double dur = ss_timeline_duration(t);
    double rng_in, rng_out;
    /* What actually leaves the graph. A range or a burn-in adds one more
     * filter after [vout], and naming the result in ONE variable is what
     * keeps the map, the preview scale and the muxer from each deciding for
     * themselves which label was the last one written. */
    const char *vlabel = "[vout]", *alabel = "[aout]";

    ss_timeline_range(t, &rng_in, &rng_out);

    av = malloc(sizeof(char *) * cap);
    if (!av) return -1;

    /* Which label each clip's picture will carry, worked out in a pass of its
     * own. A transition names its PARTNER's stream, and the partner is not
     * necessarily earlier in the array than the clip transitioning from it —
     * `move` changes when a clip plays without changing where it is stored.
     * Filter graph labels resolve globally rather than in order, so naming one
     * before it is defined is fine; not knowing its number is not. */
    for (i = 0; i < t->ntracks; i++) {
        voffs[i] = nclip;
        nclip += t->track[i].nclips;
    }
    vlab = malloc(sizeof(int) * (size_t)(nclip > 0 ? nclip : 1));
    if (!vlab) { free(av); return -1; }
    /* Which TRACK each audio label came from. Ducking is a relationship
     * between two tracks, and by the time the mix is built all that is left
     * of a clip is a label — so the answer has to be written down while the
     * chains are being emitted. */
    atrack = malloc(sizeof(int) * (size_t)(nclip > 0 ? nclip : 1));
    if (!atrack) { free(vlab); free(atrack); free(av); return -1; }
    for (i = 0; i < nclip; i++) vlab[i] = -1;
    for (i = 0; i < t->ntracks; i++) {
        if (!ss_track_shows(t, i) && !ss_track_sounds(t, i)) continue;
        for (j = 0; j < t->track[i].nclips; j++) {
            if (ss_clip_length(&t->track[i].clip[j]) <= 0) continue;
            if (t->track[i].type == SS_TRACK_VIDEO && ss_track_shows(t, i))
                vlab[voffs[i] + j] = nvid++;
        }
    }
    nvid = 0;

    PUSH(xdup("ffmpeg"));
    PUSH(xdup("-v")); PUSH(xdup("error"));
    /* No progress ticker on a preview. It goes to stderr, and the window
     * shows a process's stderr in the status line — so a background render
     * filled it with `frame= 0 fps=0.0 q=0.0 size=`. A deliverable export is
     * run from a terminal and wants it. */
    if (!preview) PUSH(xdup("-stats"));
    PUSH(xdup("-y"));

    /* One -i per clip. Seeking with -ss BEFORE -i is a keyframe seek and is
     * the only form that does not decode the whole file up to the in point;
     * the accurate-seek cost is paid by the trim in the graph instead. */
    for (i = 0; i < t->ntracks; i++) {
        const ss_track *tr = &t->track[i];
        /* Only a track that contributes NEITHER is dropped outright. The two
         * flags used to be one condition, so muting a video track took its
         * picture with it and hiding one took the dialogue.
         *
         * ⚠ This predicate must stay identical to the one on the graph loop
         * below: the two walk in lockstep and `input` is the -i index. */
        if (!ss_track_shows(t, i) && !ss_track_sounds(t, i)) continue;
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
            } else if (c->freeze >= 0.0) {
                /* A freeze opens the source AT THE INSTANT, not at the clip's
                 * in point. The graph holds the first frame it is given, so
                 * seeking anywhere else freezes the wrong picture — and it
                 * freezes it convincingly, which is why this was worth a
                 * measurement rather than a look. Half a second is read
                 * because a keyframe seek can land just before the instant
                 * asked for, and one frame of slack is cheaper than an
                 * accurate seek through the whole file. */
                PUSH(xdup("-ss")); PUSH(xfmt("%.6f", c->src_in + c->freeze));
                PUSH(xdup("-t")); PUSH(xdup("0.5"));
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
        if (!ss_track_shows(t, i) && !ss_track_sounds(t, i)) continue;
        for (j = 0; j < tr->nclips; j++) {
            const ss_clip *c = &tr->clip[j];
            double len = ss_clip_length(c);
            if (len <= 0) continue;

            if (tr->type == SS_TRACK_VIDEO && ss_track_shows(t, i)) {
                float s0, px0, py0, r0, s1, px1, py1, r1;
                int fw, fh;
                double t0 = 0, tdur = 0;
                int tpart = -1, splitme;
                char tvm[64], prev[32];
                int moves  = xform_moves(c);
                int rotkey = ss_clip_prop_nkeys(c, "xform.rotate") > 0;
                int opkey  = ss_clip_prop_nkeys(c, "opacity") > 0;
                int rotmoves;

                xform_at(c, 0.0, len, &s0, &px0, &py0, &r0);
                xform_at(c, len, len, &s1, &px1, &py1, &r1);
                /* An angle that changes needs the expression either way. The
                 * old two-point ramp used to be dropped ENTIRELY here — the
                 * animated branch had no rotate at all — so a clip that was
                 * both moving and turning exported without the turn while the
                 * monitor showed it. */
                rotmoves = rotkey || (c->xf.animate && r0 != r1);

                /* This clip's own number, decided in the pass above so that a
                 * transition can name a stream it has not reached yet. */
                nvid = vlab[voffs[i] + j];
                if (!trans_span(t, i, j, &t0, &tdur, &tpart)) tdur = 0;
                splitme = tdur > 0 || is_trans_partner(t, i, j);
                clip_tv(tvm, sizeof tvm, -c->tl_in);

                sb_add(&fc, ";[%d:v]", input);
                if (needs_alpha(c) || opkey) sb_add(&fc, "format=rgba,");
                /* Stabilise, reverse or freeze FIRST: all three are about the
                 * source frames, and two of them would be wrong once the
                 * picture has been scaled or re-framed. */
                chain_source_ops(&fc, t, c, i, j);

                if (moves) {
                    /* A framing that MOVES cannot be a scale, whose output
                     * size is fixed for the life of the filter. zoompan is the
                     * one filter that re-frames per output frame; it only ever
                     * zooms IN, so the source is fitted generously first and
                     * the move happens inside that. `on` counts output frames,
                     * which is what makes this work over a video clip and not
                     * just a still.
                     *
                     * zoompan does the ZOOM and nothing else. The position is
                     * the overlay's, below — see chain_pos for what happened
                     * when it was not.
                     *
                     * The canvas: the source is fitted at the LARGEST scale
                     * the move reaches, for resolution, and then padded out to
                     * K frames wide. What ends up on screen is the source at
                     * magnification z, so z must never go below 1 — which is
                     * the whole reason for the padding, because a scale that
                     * dips BELOW 1 (the picture smaller than the frame) is
                     * then a zoom of 1 into a canvas that is bigger than the
                     * frame rather than an impossible z of 0.6. With no dip,
                     * K is the fit and this is arithmetically what it was
                     * before parameter keys existed. */
                    double zlo, zhi, K, F;
                    int cw, ch;
                    char tv[64];

                    ss_clip_prop_range(c, "xform.scale", &zlo, &zhi);
                    if (!ss_clip_prop_nkeys(c, "xform.scale")) {
                        zlo = s0 < s1 ? s0 : s1;
                        zhi = s0 < s1 ? s1 : s0;
                    }
                    if (zlo < 0.05) zlo = 0.05;
                    if (zhi < zlo)  zhi = zlo;
                    F = zhi;
                    K = zhi / (zlo < 1.0 ? zlo : 1.0);
                    if (K > 8.0) K = 8.0;
                    if (K < 1.0) K = 1.0;
                    fitted_size(t, (float)F, &fw, &fh);
                    fitted_size(t, (float)K, &cw, &ch);
                    sb_add(&fc, "scale=%d:%d:force_original_aspect_ratio=decrease"
                                ",pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black",
                           fw, fh, cw, ch);
                    /* `on` is a frame index and zoompan runs BEFORE the speed
                     * setpts, so on/(fps*speed) is the frame's time in seconds
                     * into the clip — the very number the monitor hands
                     * xform_at. Dividing by one frame less makes the move
                     * arrive a frame early, and the monitor and the export
                     * then disagree about framing for the whole length of the
                     * move. Measured: 23 dB. */
                    snprintf(tv, sizeof tv, "on/%.6g",
                             t->fps * (c->speed > 0 ? c->speed : 1.0));
                    sb_add(&fc, ",zoompan=z='max((%.6f)*(", K / F);
                    if (!prop_expr(&fc, c, "xform.scale", tv))
                        sb_add(&fc, "%.5f+(%.5f)*clip((%s)/%.6f,0,1)",
                               (double)s0, (double)(s1 - s0), tv, len);
                    sb_add(&fc, "),1)'"
                                ":x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)'"
                                ":d=1:s=%dx%d:fps=%.6g",
                           t->w, t->h, t->fps);
                    /* A still framing rotates up in the branch below; a moving
                     * one rotates AFTER the zoom, on the project frame, so the
                     * angle is the last thing applied either way. */
                    if (!rotmoves && r0 != 0.0f)
                        sb_add(&fc, ",rotate=%.6f:ow='hypot(iw,ih)':oh='hypot(iw,ih)'"
                                    ":c=black@0", (double)r0 * M_PI / 180.0);
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
                    if (!rotmoves && r0 != 0.0f)
                        sb_add(&fc, ",rotate=%.6f:ow='hypot(iw,ih)':oh='hypot(iw,ih)'"
                                    ":c=black@0", (double)r0 * M_PI / 180.0);
                }
                sb_add(&fc, ",fps=%.6g", t->fps);
                {
                    ss_retime_seg rseg[SS_MAX_RETIME_SEG];
                    int rn = ss_clip_retime(c, rseg, SS_MAX_RETIME_SEG);
                    if (c->freeze >= 0.0) {
                        /* A held frame has no timebase to stretch. */
                    } else if (rn > 1) {
                        chain_ramp_setpts(&fc, rseg, rn);
                    } else if (c->speed != 1.0) {
                        sb_add(&fc, ",setpts=%.6f*PTS", 1.0 / c->speed);
                    }
                    /* The frames that were never shot. Only worth asking for
                     * where the timebase actually moved: at 1x every output
                     * frame is an input frame, and minterpolate would spend
                     * minutes rebuilding what it was already given. */
                    if (c->retime != SS_RETIME_NEAREST && c->freeze < 0.0 &&
                        (rn > 1 || c->speed != 1.0)) {
                        if (c->retime == SS_RETIME_BLEND)
                            sb_add(&fc, ",minterpolate=fps=%.6g:mi_mode=blend",
                                   t->fps);
                        else
                            sb_add(&fc, ",minterpolate=fps=%.6g:mi_mode=mci"
                                        ":mc_mode=aobmc:me_mode=bidir:vsbmc=1",
                                   t->fps);
                    }
                }
                /* Everything from here on is timed in CLIP seconds: the speed
                 * setpts is behind us, so `t` is what the grade's gates, the
                 * fades and the keys all mean by it. */
                if (rotmoves) {
                    sb_add(&fc, ",rotate=a='(");
                    if (!prop_expr(&fc, c, "xform.rotate", "t"))
                        sb_add(&fc, "%.6f+(%.6f)*clip(t/%.6f,0,1)",
                               (double)r0, (double)(r1 - r0), len > 0 ? len : 1.0);
                    sb_add(&fc, ")*PI/180':ow='hypot(iw,ih)':oh='hypot(iw,ih)'"
                                ":c=black@0");
                }

                chain_grade(&fc, c, lutdir, i, j);
                /* Colour first, then effects, then the caption. A title is
                 * something written ON the shot and blurring it with the shot
                 * is nobody's intention; the grade comes first because every
                 * effect here is looking at a picture that has been graded,
                 * which is the order a darkroom works in too. */
                chain_fx(&fc, c, nvid);
                chain_title(&fc, t, c, lutdir, i, j);

                if (c->fade_in > 0.0)
                    sb_add(&fc, ",fade=t=in:st=0:d=%.4f", c->fade_in);
                if (c->fade_out > 0.0)
                    sb_add(&fc, ",fade=t=out:st=%.4f:d=%.4f",
                           len - c->fade_out, c->fade_out);
                if (opkey)
                    chain_alpha(&fc, c, nvid);
                else if (c->opacity < 1.0f)
                    sb_add(&fc, ",format=rgba,colorchannelmixer=aa=%.4f",
                           c->opacity);

                /* A picture wanted TWICE needs a split: once where the clip
                 * plays, once inside a transition. A stream can only be read
                 * once, and naming it twice fails the whole graph. */
                if (splitme) {
                    sb_add(&fc, ",split[m%d][s%d]", nvid, nvid);
                    sb_add(&fc, ";[m%d]setpts=PTS-STARTPTS+%.6f/TB[v%d]",
                           nvid, c->tl_in, nvid);
                } else {
                    sb_add(&fc, ",setpts=PTS-STARTPTS+%.6f/TB[v%d]",
                           c->tl_in, nvid);
                }

                /* Composite. `enable` is what keeps a clip off the output
                 * outside its own span — without it the last frame of every
                 * clip would persist to the end of the timeline.
                 *
                 * The chain is base -> bg1 -> bg2 -> ..., each overlay taking
                 * the PREVIOUS stage by name. Naming the input "bg" instead
                 * of "bg<n>" leaves every intermediate stage unconnected and
                 * ffmpeg refuses the whole graph.
                 *
                 * A transition is its own layer, laid down BEFORE the clip it
                 * belongs to and gated to the overlap. At progress zero it is
                 * the outgoing clip exactly, so it covers what is under it
                 * with what was already there and the join is seamless; the
                 * incoming clip's own layer then starts where it ends. */
                if (tdur > 0) {
                    if (tpart >= 0 && vlab[voffs[i] + tpart] >= 0) {
                        const ss_clip *a = &tr->clip[tpart];
                        chain_trans_side(&fc, t, a, vlab[voffs[i] + tpart],
                                         t0 - a->tl_in, tdur, nvid, "xa");
                    } else {
                        chain_trans_empty(&fc, t, tdur, nvid, "xa");
                    }
                    chain_trans_side(&fc, t, c, nvid, 0.0, tdur, nvid, "xb");
                    chain_trans_join(&fc, t, c, tdur, nvid);
                    sb_add(&fc, ";[xj%d]setpts=PTS-STARTPTS+%.6f/TB[xt%d]",
                           nvid, t0, nvid);
                    /* Half open, like every other gate here: a frame that
                     * satisfies both this and the clip's own layer is drawn
                     * twice, and the second one is not what the transition
                     * had arrived at. */
                    sb_add(&fc, ";[%s][xt%d]overlay=eof_action=pass"
                                ":enable='gte(t,%.6f)*lt(t,%.6f)'[bg%d]",
                           nlayer == 0 ? "base" : prevlab(prev, sizeof prev, nlayer),
                           nvid, t0, t0 + tdur, nlayer + 1);
                    nlayer++;
                }
                {
                    sb_add(&fc, ";[%s][v%d]overlay=eof_action=pass:x='(W-w)/2+(",
                           nlayer == 0 ? "base" : prevlab(prev, sizeof prev, nlayer),
                           nvid);
                    chain_pos(&fc, c, "xform.x", tvm, len, px0, px1);
                    sb_add(&fc, ")*W':y='(H-h)/2+(");
                    chain_pos(&fc, c, "xform.y", tvm, len, py0, py1);
                    /* The clip's own layer starts where the transition ends,
                     * so the two never both draw it. */
                    sb_add(&fc, ")*H':enable='between(t,%.6f,%.6f)'[bg%d]",
                           c->tl_in + tdur, c->tl_in + len, nlayer + 1);
                    nlayer++;
                }

            }

            /* A clip's OWN sound, whichever kind of track it sits on.
             *
             * This used to hang off the track type, so audio existed only on
             * an audio track — and a video clip with dialogue on it, which is
             * most footage anybody shoots, exported and played back SILENT.
             * The track decides what is composited, not whether the shot has
             * a sound track.
             *
             * `has_audio` is asked before the graph is built rather than
             * guessed here: naming [N:a] for an input with no audio stream
             * fails the whole graph, so this cannot be optimistic. */
            if (c->kind == SS_CLIP_MEDIA && c->has_audio && ss_track_sounds(t, i)) {
                /* Decibels ADD, so the clip's gain and the track's fader are
                 * one volume filter and not two. */
                float g = c->gain_db + tr->gain_db;
                sb_add(&fc, ";[%d:a]", input);
                sb_add(&fc, "aresample=48000");
                if (c->reverse) sb_add(&fc, ",areverse");
                {
                    ss_retime_seg rseg[SS_MAX_RETIME_SEG];
                    int rn = ss_clip_retime(c, rseg, SS_MAX_RETIME_SEG), q;
                    if (c->freeze >= 0.0) {
                        /* A frozen frame is a picture, not a moment: there is
                         * no stretch of sound that corresponds to it. */
                    } else if (rn > 1 && ramp_audio_possible(rseg, rn)) {
                        /* ONE atempo, stepped by sendcmd at the segment
                         * boundaries.
                         *
                         * ⚠ In SOURCE seconds, not output seconds. asendcmd
                         * sits BEFORE atempo, so the frames it is timing have
                         * not been stretched yet — scheduling on the output
                         * axis makes every command land early and the sound
                         * comes out short: 2.579s against the picture's
                         * 2.760s on a 1x-to-2x ramp, which is a drift no
                         * amount of listening localises. */
                        sb_add(&fc, ",asetpts=PTS-STARTPTS,asendcmd=c='");
                        for (q = 0; q < rn; q++)
                            sb_add(&fc, "%s%.6f atempo@r%d tempo %.6f",
                                   q ? "\\;" : "", rseg[q].src0, nvid,
                                   rseg[q].speed);
                        sb_add(&fc, "',atempo@r%d=%.6f", nvid, rseg[0].speed);
                    } else if (rn > 1) {
                        /* A ramp that leaves atempo's range cannot be pitched
                         * as one filter, and a chain of them cannot be
                         * commanded as a unit. Said once, by name, rather
                         * than shipping a graph that fails or a sound that
                         * drifts out of sync with its own picture. */
                        fprintf(stderr, "synstudio: track %d clip %d ramps "
                                        "outside 0.5-2x; its sound is dropped\n",
                                i, j);
                        sb_add(&fc, ",volume=0");
                    } else if (c->speed != 1.0) {
                        chain_atempo(&fc, c->speed);
                    }
                }
                /* Cleaned, shaped and controlled BEFORE the fader: the
                 * gain is where a person sets the level, and a compressor
                 * after it would undo whatever they set. */
                chain_clip_audio(&fc, c);
                if (ss_clip_prop_nkeys(c, "gain") > 0 || tr->nauto > 0) {
                    /* A keyed fader needs no steps and no cubes: volume takes
                     * an expression, once told to evaluate it per frame rather
                     * than once. The clip's gain and the track's ADD in
                     * decibels, so both are terms inside one expression and
                     * not two filters. asetpts first, because the expression
                     * is timed from zero and a seeked input is not. */
                    sb_add(&fc, ",asetpts=PTS-STARTPTS,volume=eval=frame:volume='"
                                "pow(10,((");
                    if (!prop_expr(&fc, c, "gain", "t"))
                        sb_add(&fc, "%.4f", (double)c->gain_db);
                    sb_add(&fc, ")+(");
                    /* ⚠ The track's curve is in TIMELINE seconds and this
                     * chain is in CLIP seconds, so its variable is shifted by
                     * where the clip starts. Without the shift every clip on
                     * the track would ride the automation from the top of the
                     * programme. */
                    if (!track_gain_expr(&fc, tr, c->tl_in))
                        sb_add(&fc, "%.4f", (double)tr->gain_db);
                    sb_add(&fc, "))/20)'");
                } else if (g != 0.0f)
                    sb_add(&fc, ",volume=%.3fdB", g);
                if (tr->pan != 0.0f) {
                    double l, r;
                    pan_law(tr->pan, &l, &r);
                    /* Built from the SOURCE's channel count, not through an
                     * upmix. `aformat=channel_layouts=stereo` on a mono clip
                     * spreads it at -3dB a side, so the same fader position
                     * came out quieter for having been panned at all — and
                     * hard left measured 3dB under centre, which is the one
                     * thing a pan must never do. A mono source names c0
                     * twice; anything else keeps its own two channels. */
                    if (c->achannels < 2)
                        sb_add(&fc, ",pan=stereo|c0=%.4f*c0|c1=%.4f*c0", l, r);
                    else
                        sb_add(&fc, ",pan=stereo|c0=%.4f*c0|c1=%.4f*c1", l, r);
                }
                if (c->fade_in > 0.0)
                    sb_add(&fc, ",afade=t=in:st=0:d=%.4f:curve=%s",
                           c->fade_in, ss_afade_curve(c->fade_shape));
                if (c->fade_out > 0.0)
                    sb_add(&fc, ",afade=t=out:st=%.4f:d=%.4f:curve=%s",
                           len - c->fade_out, c->fade_out,
                           ss_afade_curve(c->fade_shape));
                /* And the sound follows the picture across a transition.
                 *
                 * `qsin` on both sides, not a straight line: two linear fades
                 * sum to a dip of about three decibels in the middle, which is
                 * audible on anything continuous — room tone, music — as a
                 * hole exactly where the cut is. A quarter sine and its mirror
                 * hold the POWER constant, which is what a crossfade is for.
                 *
                 * There is no acrossfade here and there could not be: that
                 * filter joins two streams end to end, and these two are
                 * already laid out in time and mixed with everything else. Two
                 * fades over the overlap ARE the crossfade. */
                {
                    double xt0, xdur, xst;
                    int xp;
                    if (trans_span(t, i, j, &xt0, &xdur, &xp))
                        sb_add(&fc, ",afade=t=in:st=0:d=%.4f:curve=qsin", xdur);
                    if (trans_out(t, i, j, &xst, &xdur))
                        sb_add(&fc, ",afade=t=out:st=%.4f:d=%.4f:curve=qsin",
                               xst, xdur);
                }
                sb_add(&fc, ",adelay=%d:all=1[a%d]",
                       (int)(c->tl_in * 1000.0 + 0.5), naud);
                if (atrack) atrack[naud] = i;
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
    if (nlayer > 0) sb_add(&fc, ";[bg%d]null[vout]", nlayer);
    else            sb_add(&fc, ";[base]null[vout]");

    /* The RENDER RANGE, and the burn-in that goes over whatever it leaves.
     *
     * Trimmed at the END rather than by seeking the inputs: every clip is
     * placed at its own tl_in and composited onto a base the length of the
     * whole timeline, so a range is a WINDOW onto the finished picture and
     * not a different edit. Seeking would have to move every clip, every
     * transition and every keyed expression with it.
     *
     * ⚠ setpts=PTS-STARTPTS after the trim. Without it the frames keep the
     * timestamps they had on the timeline, and a render of minutes nine to
     * ten arrives as a file with nine minutes of nothing at the front. */
    if (rng_in > 0.0 || rng_out < dur) {
        sb_add(&fc, ";[vout]trim=start=%.6f:end=%.6f,setpts=PTS-STARTPTS",
               rng_in, rng_out);
        chain_burnin(&fc, t, burn, out, rng_in);
        sb_add(&fc, "[rout]");
        vlabel = "[rout]";
    } else if (burn) {
        /* `null` because a chain has to start with a filter: `[vout],drawtext`
         * is a graph ffmpeg refuses to parse. */
        sb_add(&fc, ";[vout]null");
        chain_burnin(&fc, t, burn, out, 0.0);
        sb_add(&fc, "[rout]");
        vlabel = "[rout]";
    }

    /* Over the delivered picture, after the range and the burn-in, because a
     * watermark is the last thing that happens to a frame before it is
     * encoded — and because a range that trimmed it away afterwards would be
     * a watermark on nothing.
     *
     * Sized as a FRACTION of the frame (12% of the width) so the same file
     * marks a 1080 delivery and a 4K one identically, and inset by the same
     * fraction of the height so it sits where it looks placed rather than
     * where the pixels happened to land. */
    if (mark && *mark && !preview) {
        int mi = input + ((subs && *subs) ? 1 : 0);
        sb_add(&fc, ";[%d:v]format=rgba,scale=iw*%.4f/iw*%d:-1[wm]",
               mi, 0.12, t->w);
        sb_add(&fc, ";%s[wm]overlay=W-w-H*0.04:H-h-H*0.04[wout]", vlabel);
        vlabel = "[wout]";
    }

    if (preview && t->w > 960)
        sb_add(&fc, ";%sscale=960:-2:flags=fast_bilinear[pout]", vlabel);

    if (naud > 0) {
        int k;
        /* ---- ducking ----
         *
         * A music bed gets out of the way of the dialogue. The dialogue is
         * another TRACK, so its sound has to exist as one stream before it
         * can key anything — and every clip on it has already been spent on
         * the main mix, which is why each one is split rather than read
         * twice: naming a stream twice fails the whole graph.
         *
         * Built here, between the clip chains and the mix, because that is
         * the only point where every clip's label exists and nothing has
         * been summed yet. */
        static char lab[SS_MAX_TRACKS][24];
        char alab[64][24];
        int ducked = 0, kt;

        for (k = 0; k < naud && k < 64; k++)
            snprintf(alab[k], sizeof alab[k], "[a%d]", k);

        for (kt = 0; kt < t->ntracks; kt++) {
            int users = 0, keyn = 0, ui;
            if (t->track[kt].duck_from < 0) continue;
            /* kt is ducked FROM track `key`. */
            {
                int key = t->track[kt].duck_from;
                if (key < 0 || key >= t->ntracks || key == kt) continue;

                /* Every audio label belonging to the key track, split off. */
                for (k = 0; k < naud && k < 64; k++)
                    if (atrack[k] == key) keyn++;
                for (k = 0; k < naud && k < 64; k++)
                    if (atrack[k] == kt) users++;
                if (keyn == 0 || users == 0) continue;

                sb_add(&fc, ";");
                for (k = 0; k < naud && k < 64; k++) {
                    if (atrack[k] != key) continue;
                    sb_add(&fc, "%s", alab[k]);
                }
                /* One stream for the key, however many clips it came from. */
                if (keyn > 1)
                    sb_add(&fc, "amix=inputs=%d:normalize=0[kmix%d]", keyn, key);
                else
                    sb_add(&fc, "anull[kmix%d]", key);
                /* ⚠ The key clips are CONSUMED by that mix, so they have to
                 * come back for the main mix as well — one copy for it and
                 * one for every clip they are keying. */
                sb_add(&fc, ";[kmix%d]asplit=%d", key, users + 1);
                for (ui = 0; ui <= users; ui++) sb_add(&fc, "[ks%d_%d]", key, ui);

                /* The key track's own sound goes back into the mix as the
                 * single stream it now is. */
                ui = 0;
                for (k = 0; k < naud && k < 64; k++)
                    if (atrack[k] == key) {
                        if (ui == 0) snprintf(alab[k], sizeof alab[k],
                                              "[ks%d_0]", key);
                        else         alab[k][0] = '\0';   /* folded into it */
                        ui++;
                    }

                ui = 1;
                for (k = 0; k < naud && k < 64; k++) {
                    double amt;
                    if (atrack[k] != kt) continue;
                    amt = ss_clampf(t->track[kt].duck, 0.0f, 100.0f) / 100.0;
                    sb_add(&fc, ";%s[ks%d_%d]sidechaincompress=threshold=0.03"
                                ":ratio=%.3f:attack=20:release=400:makeup=1[dk%d]",
                           alab[k], key, ui, 1.0 + amt * 19.0, k);
                    snprintf(alab[k], sizeof alab[k], "[dk%d]", k);
                    ui++;
                }
                ducked = 1;
            }
        }
        (void)lab; (void)ducked;

        sb_add(&fc, ";");
        for (k = 0; k < naud; k++)
            sb_add(&fc, "%s", k < 64 ? alab[k] : "");
        /* normalize=0: amix otherwise divides every input by the number of
         * inputs, so adding a quiet music bed would duck the dialogue. */
        {
            int nin = 0;
            for (k = 0; k < naud; k++)
                if (k >= 64 || alab[k][0]) nin++;
            if (nin > 1)
                sb_add(&fc, "amix=inputs=%d:normalize=0:dropout_transition=0", nin);
            else
                sb_add(&fc, "anull");
        }
        if (t->master_db != 0.0f)
            sb_add(&fc, ",volume=%.3fdB", t->master_db);
        /* alimiter, not a clip: summing tracks with normalize=0 CAN go past
         * full scale, and what that sounds like is a crackle nobody can trace
         * back to the fader that caused it. 0.99 leaves a hair of headroom. */
        sb_add(&fc, ",alimiter=limit=0.99:level=disabled");
        /* Delivery loudness, LAST — after the mix, after the master fader
         * and after the limiter, because a broadcast target is a statement
         * about the FILE and nothing downstream of it may change the level
         * again.
         *
         * ⚠ One pass, deliberately. Two-pass loudnorm measures the whole
         * programme and then encodes it, which means rendering the timeline
         * twice; single-pass is a live gain that reaches the target within a
         * decibel or so and costs nothing. `loudness FILE` measures what
         * actually came out, so the answer is checkable rather than
         * promised. */
        if (t->lufs < 0.0f)
            sb_add(&fc, ",loudnorm=I=%.2f:TP=-1.5:LRA=11", (double)t->lufs);
        sb_add(&fc, "[aout]");
        if (rng_in > 0.0 || rng_out < dur) {
            sb_add(&fc, ";[aout]atrim=start=%.6f:end=%.6f,asetpts=PTS-STARTPTS"
                        "[raout]", rng_in, rng_out);
            alabel = "[raout]";
        }
    }

    /* A soft subtitle stream is an INPUT, and it goes in last on purpose.
     * Every label in the graph above names an input by NUMBER, so a file
     * inserted anywhere else would renumber the clips and hand the whole
     * timeline the wrong pictures. Appended here it takes the next index and
     * nothing already written has to change.
     *
     * Not offered on a preview: a preview is the CUT being judged, it is
     * always x264 in an mp4 whatever the deliverable is, and a stream nobody
     * switched on would only be there to go stale. */
    if (subs && *subs && !preview) {
        PUSH(xdup("-i")); PUSH(xdup(subs));
    }
    /* A watermark is a PICTURE, so unlike the burn-in it cannot be a filter
     * on the end of the chain — it is another input, and it goes in last for
     * the same reason the subtitles do: every label in the graph above names
     * an input by NUMBER, and a file inserted anywhere else renumbers the
     * clips and hands the timeline the wrong pictures.
     *
     * ⚠ AFTER the subtitle input, not before. Both are appended, so their
     * order here is the order of their indices, and the subtitle map below
     * names `input` — which has to still be the subtitle's. */
    if (mark && *mark && !preview) {
        PUSH(xdup("-i")); PUSH(xdup(mark));
    }

    PUSH(xdup("-filter_complex"));
    PUSH(xdup(fc.s ? fc.s : ""));
    PUSH(xdup("-map"));
    PUSH(xdup(preview && t->w > 960 ? "[pout]" : vlabel));
    {
        const ss_tl_format *af = fmt ? fmt : ss_timeline_format(NULL, out);
        /* ⚠ A format with no audio codec is a format with nowhere to put the
         * sound. Mapping the mix into a PNG sequence fails the whole render
         * with "Could not find tag for codec", after the graph is built and
         * the encode has started. */
        if (naud > 0 && (preview || (af && af->acodec))) {
            PUSH(xdup("-map")); PUSH(xdup(alabel));
        }
    }
    if (subs && *subs && !preview) {
        const ss_tl_format *sf = fmt ? fmt : ss_timeline_format(NULL, out);
        PUSH(xdup("-map")); PUSH(xfmt("%d:s:0", input));
        PUSH(xdup("-c:s")); PUSH(xdup(sf->scodec ? sf->scodec : "mov_text"));
    }
    PUSH(xdup("-t"));
    PUSH(xfmt("%.6f", rng_out > rng_in ? rng_out - rng_in
                                       : (dur > 0 ? dur : 1.0)));
    /* A preview is watched once and thrown away, so every setting there is
     * traded for the time it takes to produce: ultrafast/crf 30 is roughly an
     * order of magnitude quicker than the deliverable settings and looks it,
     * which is correct, because what is being judged at that point is the
     * CUT and not the encode. A preview is therefore always x264 in an mp4,
     * whatever the deliverable is going to be — the picture is the same graph
     * either way, which is the part that has to agree. */
    if (preview) {
        PUSH(xdup("-c:v")); PUSH(xdup("libx264"));
        PUSH(xdup("-preset")); PUSH(xdup("ultrafast"));
        PUSH(xdup("-crf"));    PUSH(xdup("30"));
        PUSH(xdup("-pix_fmt")); PUSH(xdup("yuv420p"));
        if (naud > 0) { PUSH(xdup("-c:a")); PUSH(xdup("aac"));
                        PUSH(xdup("-b:a")); PUSH(xdup("96k")); }
    } else {
        const ss_tl_format *f = fmt ? fmt : ss_timeline_format(NULL, out);
        PUSH(xdup("-c:v")); PUSH(xdup(f->vcodec));
        if (f->v1 && f->v2) { PUSH(xdup(f->v1)); PUSH(xdup(f->v2)); }
        if (f->v3 && f->v4) { PUSH(xdup(f->v3)); PUSH(xdup(f->v4)); }
        PUSH(xdup("-pix_fmt")); PUSH(xdup(f->pix));
        if (naud > 0 && f->acodec) {
            PUSH(xdup("-c:a")); PUSH(xdup(f->acodec));
            /* A bitrate means nothing to a lossless codec and ffmpeg refuses
             * some of them outright, so it is only said where it applies. */
            if (strncmp(f->acodec, "pcm_", 4) && strcmp(f->acodec, "flac")) {
                PUSH(xdup("-b:a")); PUSH(xdup("192k"));
            }
        }
    }
    /* NOT fragmented, deliberately.
     *
     * A preview was written as fragmented mp4 so a player could open it while
     * ffmpeg was still writing. Nothing ever does: playback waits for the
     * render to finish, and the window renders one in the background after an
     * edit so it is ready before anybody asks. What the flags bought was
     * therefore nothing, and what they cost is a container that players
     * handle inconsistently — an empty_moov file can report the wrong
     * duration and stop early, which is exactly what a preview must never do.
     * A plain mp4 gets a real moov written at the end, with the real
     * duration in it. */
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
    free(vlab); free(atrack);
    *argv_out = av;
    return ac;

fail:
    free(fc.s);
    free(vlab); free(atrack);
    for (i = 0; i < ac; i++) free(av[i]);
    free(av);
    return -1;
}

/* ---- a transition, for the monitor ----
 *
 * The export hands xfade two moving streams; the monitor has ONE frame of each
 * and a progress number, and it has to land on exactly the picture the export
 * will write at that instant. So it hands xfade the same filter under the same
 * name, and buys the progress with a second frame.
 *
 * xfade takes its progress from a frame's timestamp against the FIRST frame it
 * saw, so a single frame is progress zero whatever PTS it carries — which is
 * why this loops each side once and stamps the copy at p×duration. Two frames
 * is the entire cost, for any transition and any length.
 *
 * `src` is the label the side's picture arrives on, because a partner's stream
 * has been split and the incoming clip's has not. */
static void chain_frame_side(strbuf *fc, const ss_timeline *t, const char *src,
                             double px, double py, int id, const char *side)
{
    sb_add(fc, ";color=c=black@0:s=%dx%d:d=0.04,format=rgba[%sb%d]",
           t->w, t->h, side, id);
    sb_add(fc, ";[%sb%d][%s]overlay=eof_action=pass:x='(W-w)/2+(%.5f)*W'"
               ":y='(H-h)/2+(%.5f)*H'[%s%d]",
           side, id, src, px, py, side, id);
}

/* The same picture twice, the second stamped `at` seconds in.
 *
 * xfade takes its progress from a frame's timestamp against the FIRST frame it
 * saw, so one frame is always progress zero whatever PTS it carries. Two are
 * enough to ask for any moment of any transition, and two is the whole cost —
 * the monitor never renders the frames in between. */
static void loop_at(strbuf *fc, const char *lab, int id, double at,
                    const char *out)
{
    sb_add(fc, ";[%s%d]loop=loop=1:size=1:start=0,setpts=N*%.6f/TB[%s%d]",
           lab, id, at, out, id);
}

static void chain_frame_join(strbuf *fc, const ss_timeline *t, const ss_clip *c,
                             double p, double dur, int id)
{
    const char *xf = ss_trans_xfade(c->trans);
    char col[40];

    if (c->trans == SS_TRANS_DIP || !xf) {
        /* The export's two dissolves through a colour, run at one instant.
         * BOTH of them, even though only one is ever moving: the second half's
         * outgoing side is what the first xfade handed on, and going around it
         * would leave the monitor one rounding away from the export — the sort
         * of difference that is invisible until it is the thing being argued
         * about. */
        double half = dur / 2.0, at = p * dur;
        hexcol(c->trans_r, c->trans_g, c->trans_b, 1.0f, col, sizeof col);
        sb_add(fc, ";color=c=%s:s=%dx%d:d=0.04,format=rgba[xc%d]",
               col, t->w, t->h, id);
        loop_at(fc, "xa", id, at, "xal");
        loop_at(fc, "xc", id, at, "xcl");
        loop_at(fc, "xb", id, at, "xbl");
        sb_add(fc, ";[xal%d][xcl%d]xfade=transition=fade:duration=%.6f"
                   ":offset=0[xh%d]", id, id, half, id);
        sb_add(fc, ";[xh%d][xbl%d]xfade=transition=fade:duration=%.6f"
                   ":offset=%.6f,select=eq(n\\,1),setpts=PTS-STARTPTS[xj%d]",
               id, id, half, half, id);
        return;
    }
    /* At the very start there is nothing to blend, and two frames one
     * timestamp apart is not a duration xfade can divide by. `enable` picks a
     * side and consumes both, which it has to: a filter_complex label nothing
     * reads is a graph ffmpeg refuses. */
    if (p < 1e-6 || dur <= 0) {
        sb_add(fc, ";[xa%d][xb%d]overlay=eof_action=pass:enable='0'[xj%d]",
               id, id, id);
        return;
    }
    loop_at(fc, "xa", id, p * dur, "xal");
    loop_at(fc, "xb", id, p * dur, "xbl");
    sb_add(fc, ";[xal%d][xbl%d]xfade=transition=%s:duration=%.6f:offset=0"
               ",select=eq(n\\,1),setpts=PTS-STARTPTS[xj%d]",
           id, id, xf, dur, id);
}

/* Whether some clip on this track is transitioning FROM clip j at `time` —
 * which is what decides that j's picture is needed twice. */
static int trans_partner_at(const ss_timeline *t, int tr, int j, double time)
{
    double p, d;
    int i, pt;
    for (i = 0; i < t->track[tr].nclips; i++)
        if (trans_at(t, tr, i, time, &p, &d, &pt) && pt == j) return 1;
    return 0;
}

/* ------------------------------------------------------ the one frame -- */

int ss_timeline_frame(const ss_timeline *t, double time, const char *out,
                      const char *lutdir, int max_edge, char ***argv_out)
{
    strbuf fc = {0};
    char **av = NULL;
    int ac = 0, cap = 64;
    int i, j, input = 0, nvid = 0, nlayer = 0, nclip = 0;
    int voffs[SS_MAX_TRACKS], *mlab = NULL;

    av = malloc(sizeof(char *) * cap);
    if (!av) return -1;

    /* The same label pass the export does, for the same reason: a transition
     * names its partner's picture, and the partner may sit anywhere in the
     * array. Here the number is also the INPUT index, because the monitor
     * opens one input per clip on screen. */
    for (i = 0; i < t->ntracks; i++) {
        voffs[i] = nclip;
        nclip += t->track[i].nclips;
    }
    mlab = malloc(sizeof(int) * (size_t)(nclip > 0 ? nclip : 1));
    if (!mlab) { free(av); return -1; }
    for (i = 0; i < nclip; i++) mlab[i] = -1;
    for (i = 0; i < t->ntracks; i++) {
        if (t->track[i].type != SS_TRACK_VIDEO || t->track[i].hidden) continue;
        for (j = 0; j < t->track[i].nclips; j++) {
            const ss_clip *c = &t->track[i].clip[j];
            double l = ss_clip_length(c), o = time - c->tl_in;
            if (l <= 0 || o < 0 || o >= l) continue;
            mlab[voffs[i] + j] = nvid++;
        }
    }
    nvid = 0;

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
                /* Where the ramp, the reverse and the freeze are ALL
                 * resolved for the monitor: one function, the same one the
                 * length and the export expression come from. */
                double srct = c->freeze >= 0.0 ? c->src_in + c->freeze
                                               : ss_clip_source_at(c, off, t->fps);
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
            double a, tp = 0, tdur = 0;
            int fw, fh, tpart = -1, intrans, ispart;
            char prev[32], src[32];

            if (len <= 0 || off < 0 || off >= len) continue;

            nvid = mlab[voffs[i] + j];
            intrans = trans_at(t, i, j, time, &tp, &tdur, &tpart);
            ispart  = trans_partner_at(t, i, j, time);

            /* A single frame has nothing to animate, so the transform and the
             * fades are just numbers here — evaluated by the same xform_at and
             * alpha_at the export's filters are generated from. */
            xform_at(c, off, len, &s, &px, &py, &rot);
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
            /* The monitor seeks straight to the frame, so a reverse and a
             * ramp are already accounted for by ss_clip_source_at — but a
             * stabilised clip still has to be transformed, or the picture
             * being judged is not the picture that will be delivered. */
            if (c->stab && *t->stabdir) {
                char trf[2048], esc2[4200];
                snprintf(trf, sizeof trf, "%s/stab_%d_%d.trf", t->stabdir, i, j);
                esc_filter(trf, esc2, sizeof esc2);
                sb_add(&fc, ",vidstabtransform=input='%s':smoothing=%d"
                            ":zoom=%.3f:optzoom=0:crop=black", esc2,
                       (int)(c->stab_smooth + 0.5f), (double)c->stab_zoom);
            }
            chain_grade_at(&fc, c, lutdir, i, j, off);
            chain_fx(&fc, c, nvid);
            /* `off` and not -1: the monitor holds one frame, so a title that
             * MOVES has to be drawn where it is at this instant rather than
             * where its expression starts. */
            chain_title_at(&fc, t, c, lutdir, i, j, off);
            if (a < 1.0)
                sb_add(&fc, ",colorchannelmixer=aa=%.4f", a);
            /* Split when this clip's picture is wanted twice: once where it
             * plays, once as the outgoing side of the transition above it. */
            if (ispart) sb_add(&fc, ",split[v%d][s%d]", nvid, nvid);
            else        sb_add(&fc, "[v%d]", nvid);

            if (intrans) {
                /* The incoming clip of a transition does not draw itself — the
                 * transition layer is what it looks like right now, and at
                 * progress zero that is the OUTGOING clip exactly. */
                if (tpart >= 0 && mlab[voffs[i] + tpart] >= 0) {
                    const ss_clip *pc = &tr->clip[tpart];
                    double plen = ss_clip_length(pc), poff = time - pc->tl_in;
                    float ps, ppx, ppy, prot;
                    xform_at(pc, poff, plen, &ps, &ppx, &ppy, &prot);
                    snprintf(src, sizeof src, "s%d", mlab[voffs[i] + tpart]);
                    chain_frame_side(&fc, t, src, ppx, ppy, nvid, "xa");
                } else {
                    chain_trans_empty(&fc, t, 0.04, nvid, "xa");
                }
                snprintf(src, sizeof src, "v%d", nvid);
                chain_frame_side(&fc, t, src, px, py, nvid, "xb");
                chain_frame_join(&fc, t, c, tp, tdur, nvid);
                sb_add(&fc, ";[%s][xj%d]overlay[bg%d]",
                       nlayer == 0 ? "base" : prevlab(prev, sizeof prev, nlayer),
                       nvid, nlayer + 1);
                nlayer++;
            } else {
                sb_add(&fc, ";[%s][v%d]overlay=x='(W-w)/2+(%.5f)*W'"
                            ":y='(H-h)/2+(%.5f)*H'[bg%d]",
                       nlayer == 0 ? "base" : prevlab(prev, sizeof prev, nlayer),
                       nvid, (double)px, (double)py, nlayer + 1);
                nlayer++;
            }
            input++;
        }
    }

    if (nlayer > 0) sb_add(&fc, ";[bg%d]null[vout]", nlayer);
    else            sb_add(&fc, ";[base]null[vout]");
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
    free(mlab);
    *argv_out = av;
    return ac;

fail:
    free(fc.s);
    free(mlab);
    for (i = 0; i < ac; i++) free(av[i]);
    free(av);
    return -1;
}
#undef PUSH
