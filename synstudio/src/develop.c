/* develop.c — the develop settings, and the ONE table that describes them.
 *
 * Every control in the app is a row in `fields[]`. The CLI's `set`, the
 * sidecar reader and writer, the `get` used by the GUI to populate its
 * sliders, and the range clamping all read that table. Adding a control is
 * adding one row — there is no second list to update, and therefore no way to
 * ship a control the sidecar silently drops on save, which is the classic
 * failure of a hand-written serialiser.
 *
 * ss_develop_set has NO side effects beyond the field named. It is tempting
 * to have it switch cropping on when a crop.x arrives, since that is clearly
 * what a person typing `crop.x=0.1` means — but the sidecar reader goes
 * through the same function, and reading back the default `crop.w 1` then
 * turned cropping ON for every image on every load. A setter that a reader
 * shares has to be inert. Intent belongs in the command layer, which knows a
 * human typed it.
 *
 * Ranges are enforced on the way IN, not at use. An out-of-range value that
 * reaches the pipeline is a value some later maths has to defend against
 * forever; rejecting it at the one door is cheaper and the error names the
 * key and the limits.
 */
#include "synstudio.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { F_FLOAT, F_INT, F_CURVE };

typedef struct {
    const char *key;
    int         type;
    size_t      off;
    float       lo, hi;
    const char *group;      /* the panel it belongs in */
    const char *label;      /* what a person calls it */
} field;

#define D(k, t, m, lo, hi, grp, lbl) \
    { k, t, offsetof(ss_develop, m), lo, hi, grp, lbl }

static const field fields[] = {
    D("temp",             F_FLOAT, temp_k,           0.0f,   50000.0f, "Basic", "Temperature"),
    D("tint",             F_FLOAT, tint,          -150.0f,     150.0f, "Basic", "Tint"),

    D("exposure",         F_FLOAT, exposure,        -8.0f,       8.0f, "Basic", "Exposure"),
    D("contrast",         F_FLOAT, contrast,      -100.0f,     100.0f, "Basic", "Contrast"),
    D("highlights",       F_FLOAT, highlights,    -100.0f,     100.0f, "Basic", "Highlights"),
    D("shadows",          F_FLOAT, shadows,       -100.0f,     100.0f, "Basic", "Shadows"),
    D("whites",           F_FLOAT, whites,        -100.0f,     100.0f, "Basic", "Whites"),
    D("blacks",           F_FLOAT, blacks,        -100.0f,     100.0f, "Basic", "Blacks"),

    D("texture",          F_FLOAT, texture,       -100.0f,     100.0f, "Presence", "Texture"),
    D("clarity",          F_FLOAT, clarity,       -100.0f,     100.0f, "Presence", "Clarity"),
    D("dehaze",           F_FLOAT, dehaze,        -100.0f,     100.0f, "Presence", "Dehaze"),
    D("vibrance",         F_FLOAT, vibrance,      -100.0f,     100.0f, "Presence", "Vibrance"),
    D("saturation",       F_FLOAT, saturation,    -100.0f,     100.0f, "Presence", "Saturation"),

    D("hsl.red.hue",      F_FLOAT, hsl_hue[0],    -100.0f,     100.0f, "Colour mixer", "Red hue"),
    D("hsl.orange.hue",   F_FLOAT, hsl_hue[1],    -100.0f,     100.0f, "Colour mixer", "Orange hue"),
    D("hsl.yellow.hue",   F_FLOAT, hsl_hue[2],    -100.0f,     100.0f, "Colour mixer", "Yellow hue"),
    D("hsl.green.hue",    F_FLOAT, hsl_hue[3],    -100.0f,     100.0f, "Colour mixer", "Green hue"),
    D("hsl.aqua.hue",     F_FLOAT, hsl_hue[4],    -100.0f,     100.0f, "Colour mixer", "Aqua hue"),
    D("hsl.blue.hue",     F_FLOAT, hsl_hue[5],    -100.0f,     100.0f, "Colour mixer", "Blue hue"),
    D("hsl.purple.hue",   F_FLOAT, hsl_hue[6],    -100.0f,     100.0f, "Colour mixer", "Purple hue"),
    D("hsl.magenta.hue",  F_FLOAT, hsl_hue[7],    -100.0f,     100.0f, "Colour mixer", "Magenta hue"),

    D("hsl.red.sat",      F_FLOAT, hsl_sat[0],    -100.0f,     100.0f, "Colour mixer", "Red saturation"),
    D("hsl.orange.sat",   F_FLOAT, hsl_sat[1],    -100.0f,     100.0f, "Colour mixer", "Orange saturation"),
    D("hsl.yellow.sat",   F_FLOAT, hsl_sat[2],    -100.0f,     100.0f, "Colour mixer", "Yellow saturation"),
    D("hsl.green.sat",    F_FLOAT, hsl_sat[3],    -100.0f,     100.0f, "Colour mixer", "Green saturation"),
    D("hsl.aqua.sat",     F_FLOAT, hsl_sat[4],    -100.0f,     100.0f, "Colour mixer", "Aqua saturation"),
    D("hsl.blue.sat",     F_FLOAT, hsl_sat[5],    -100.0f,     100.0f, "Colour mixer", "Blue saturation"),
    D("hsl.purple.sat",   F_FLOAT, hsl_sat[6],    -100.0f,     100.0f, "Colour mixer", "Purple saturation"),
    D("hsl.magenta.sat",  F_FLOAT, hsl_sat[7],    -100.0f,     100.0f, "Colour mixer", "Magenta saturation"),

    D("hsl.red.lum",      F_FLOAT, hsl_lum[0],    -100.0f,     100.0f, "Colour mixer", "Red luminance"),
    D("hsl.orange.lum",   F_FLOAT, hsl_lum[1],    -100.0f,     100.0f, "Colour mixer", "Orange luminance"),
    D("hsl.yellow.lum",   F_FLOAT, hsl_lum[2],    -100.0f,     100.0f, "Colour mixer", "Yellow luminance"),
    D("hsl.green.lum",    F_FLOAT, hsl_lum[3],    -100.0f,     100.0f, "Colour mixer", "Green luminance"),
    D("hsl.aqua.lum",     F_FLOAT, hsl_lum[4],    -100.0f,     100.0f, "Colour mixer", "Aqua luminance"),
    D("hsl.blue.lum",     F_FLOAT, hsl_lum[5],    -100.0f,     100.0f, "Colour mixer", "Blue luminance"),
    D("hsl.purple.lum",   F_FLOAT, hsl_lum[6],    -100.0f,     100.0f, "Colour mixer", "Purple luminance"),
    D("hsl.magenta.lum",  F_FLOAT, hsl_lum[7],    -100.0f,     100.0f, "Colour mixer", "Magenta luminance"),

    D("grade.shadow.hue", F_FLOAT, shadow_hue,       0.0f,     360.0f, "Grading", "Shadow hue"),
    D("grade.shadow.sat", F_FLOAT, shadow_sat,    -100.0f,     100.0f, "Grading", "Shadow strength"),
    D("grade.hilite.hue", F_FLOAT, hilite_hue,       0.0f,     360.0f, "Grading", "Highlight hue"),
    D("grade.hilite.sat", F_FLOAT, hilite_sat,    -100.0f,     100.0f, "Grading", "Highlight strength"),
    D("grade.balance",    F_FLOAT, grade_balance, -100.0f,     100.0f, "Grading", "Balance"),

    D("sharpen",          F_FLOAT, sharpen,          0.0f,     150.0f, "Detail", "Sharpening"),
    D("sharpen.radius",   F_FLOAT, sharpen_radius,   0.3f,       5.0f, "Detail", "Radius"),
    D("nr.luma",          F_FLOAT, nr_luma,          0.0f,     100.0f, "Detail", "Luminance noise"),
    D("nr.chroma",        F_FLOAT, nr_chroma,        0.0f,     100.0f, "Detail", "Colour noise"),

    D("vignette",         F_FLOAT, vignette,      -100.0f,     100.0f, "Effects", "Vignette"),
    D("vignette.mid",     F_FLOAT, vignette_mid,     0.0f,     100.0f, "Effects", "Vignette midpoint"),
    D("vignette.feather", F_FLOAT, vignette_feather, 0.0f,     100.0f, "Effects", "Vignette feather"),
    D("grain",            F_FLOAT, grain,            0.0f,     100.0f, "Effects", "Grain"),
    D("grain.size",       F_FLOAT, grain_size,       0.0f,     100.0f, "Effects", "Grain size"),

    D("crop",             F_INT,   crop.on,          0.0f,       1.0f, "Geometry", "Crop"),
    D("crop.x",           F_FLOAT, crop.x,           0.0f,       1.0f, "Geometry", "Crop left"),
    D("crop.y",           F_FLOAT, crop.y,           0.0f,       1.0f, "Geometry", "Crop top"),
    D("crop.w",           F_FLOAT, crop.w,           0.0f,       1.0f, "Geometry", "Crop width"),
    D("crop.h",           F_FLOAT, crop.h,           0.0f,       1.0f, "Geometry", "Crop height"),
    D("crop.angle",       F_FLOAT, crop.angle,     -45.0f,      45.0f, "Geometry", "Straighten"),
    D("flip.h",           F_INT,   flip_h,           0.0f,       1.0f, "Geometry", "Flip horizontally"),
    D("flip.v",           F_INT,   flip_v,           0.0f,       1.0f, "Geometry", "Flip vertically"),
    D("rotate90",         F_INT,   rotate90,         0.0f,       3.0f, "Geometry", "Quarter turns"),

    D("curve.rgb",        F_CURVE, curve_rgb,        0.0f,       0.0f, "Curve", "RGB curve"),
    D("curve.red",        F_CURVE, curve_r,          0.0f,       0.0f, "Curve", "Red curve"),
    D("curve.green",      F_CURVE, curve_g,          0.0f,       0.0f, "Curve", "Green curve"),
    D("curve.blue",       F_CURVE, curve_b,          0.0f,       0.0f, "Curve", "Blue curve"),
};

static const int nfields = (int)(sizeof(fields) / sizeof(fields[0]));

const char *ss_develop_key(int i)
{
    if (i < 0 || i >= nfields) return NULL;
    return fields[i].key;
}

/* The window builds its whole develop panel from this. That is the point of
 * the table: a new control is a row here and appears in the GUI with the
 * right group, label and limits without a line of QML being touched. A GUI
 * that carried its own copy of the ranges would drift from the engine's, and
 * the visible symptom is a slider that refuses at 90 because the engine's
 * limit is 80. */
/* A few controls ACCEPT a much wider range than a slider should OFFER.
 * Temperature is the clear one: the engine takes anything from "as shot" (0)
 * up to 50000K, but a track covering all of that puts the entire useful band
 * of daylight photography inside four percent of its length. The hard limit
 * stays the engine's; the slider gets the part people actually use, and a
 * typed value outside it is still accepted. */
static const struct { const char *key; float lo, hi; } ui_range[] = {
    { "temp",           2000.0f, 12000.0f },
    { "sharpen.radius",    0.5f,     3.0f },
    { "crop.angle",      -15.0f,    15.0f },
};

/* Read straight off the SAME table the setters use, so a develop setting
 * added to fields[] interpolates without anything else being touched. A
 * hand-written lerp is a second list of every field, and the failure it
 * produces is a new control that silently refuses to animate. */
void ss_develop_lerp(const ss_develop *a, const ss_develop *b, float m,
                     ss_develop *out)
{
    int i;

    if (m < 0.0f) m = 0.0f;
    if (m > 1.0f) m = 1.0f;

    /* Start from the nearer end, so anything the loop below does not touch —
     * and any field a later version adds before it is taught to interpolate —
     * is at least a real value from a real keyframe rather than a zero. */
    *out = (m < 0.5f) ? *a : *b;

    for (i = 0; i < nfields; i++) {
        const field *f = &fields[i];
        const char *pa = (const char *)a + f->off;
        const char *pb = (const char *)b + f->off;
        char *po = (char *)out + f->off;

        if (f->type == F_FLOAT) {
            float va = *(const float *)pa, vb = *(const float *)pb;
            *(float *)po = va + (vb - va) * m;
        } else if (f->type == F_CURVE) {
            const ss_curve *ca = (const ss_curve *)pa;
            const ss_curve *cb = (const ss_curve *)pb;
            ss_curve *co = (ss_curve *)po;
            int k;

            if (ca->identity && cb->identity) { *co = *ca; continue; }

            /* Both tables have to exist before they can be mixed. Evaluating
             * one point is what builds it, and costs nothing if it is built. */
            ss_curve_eval(ca, 0.5f);
            ss_curve_eval(cb, 0.5f);

            /* The points are left as the nearer keyframe's. They are not read
             * again — `built` is what ss_curve_eval looks at — and there is no
             * honest set of control points for a mixture of two curves with
             * different numbers of them. */
            for (k = 0; k < SS_CURVE_LUT; k++) {
                float la = ca->identity ? (float)k / (SS_CURVE_LUT - 1) : ca->lut[k];
                float lb = cb->identity ? (float)k / (SS_CURVE_LUT - 1) : cb->lut[k];
                co->lut[k] = la + (lb - la) * m;
            }
            co->built = 1;
            co->identity = 0;
        }
        /* F_INT is deliberately not interpolated: it took the nearer end
         * above. Half a horizontal flip is not a thing. */
    }
}

int ss_develop_describe(int i, ss_develop_info *out)
{
    size_t u;

    if (i < 0 || i >= nfields) return -1;
    out->key   = fields[i].key;
    out->group = fields[i].group;
    out->label = fields[i].label;
    out->lo    = fields[i].lo;
    out->hi    = fields[i].hi;
    out->type  = fields[i].type == F_CURVE ? SS_T_CURVE
               : fields[i].type == F_INT   ? SS_T_INT : SS_T_FLOAT;

    out->ui_lo = fields[i].lo;
    out->ui_hi = fields[i].hi;
    for (u = 0; u < sizeof ui_range / sizeof ui_range[0]; u++)
        if (!strcmp(ui_range[u].key, fields[i].key)) {
            out->ui_lo = ui_range[u].lo;
            out->ui_hi = ui_range[u].hi;
            break;
        }
    return 0;
}

void ss_develop_reset(ss_develop *d)
{
    memset(d, 0, sizeof(*d));
    ss_curve_reset(&d->curve_rgb);
    ss_curve_reset(&d->curve_r);
    ss_curve_reset(&d->curve_g);
    ss_curve_reset(&d->curve_b);
    /* A zeroed crop is the whole frame, not an empty one. */
    d->crop.w = 1.0f;
    d->crop.h = 1.0f;
    d->sharpen_radius = 1.0f;
    d->vignette_mid = 50.0f;
    d->vignette_feather = 50.0f;
    d->grain_size = 25.0f;
}

int ss_develop_is_identity(const ss_develop *d)
{
    int i;
    for (i = 0; i < nfields; i++) {
        const field *f = &fields[i];
        const char *base = (const char *)d;
        switch (f->type) {
        case F_FLOAT: {
            float v = *(const float *)(base + f->off);
            /* Defaults that are not zero are still "no effect". */
            if (!strcmp(f->key, "sharpen.radius")   && v == 1.0f)  continue;
            if (!strcmp(f->key, "vignette.mid")     && v == 50.0f) continue;
            if (!strcmp(f->key, "vignette.feather") && v == 50.0f) continue;
            if (!strcmp(f->key, "grain.size")       && v == 25.0f) continue;
            if (!strcmp(f->key, "crop.w")           && v == 1.0f)  continue;
            if (!strcmp(f->key, "crop.h")           && v == 1.0f)  continue;
            if (v != 0.0f) return 0;
            break;
        }
        case F_INT:
            if (*(const int *)(base + f->off) != 0) return 0;
            break;
        case F_CURVE:
            if (!((const ss_curve *)(base + f->off))->identity) return 0;
            break;
        }
    }
    return 1;
}

/* "0,0 0.25,0.18 0.75,0.82 1,1" — whitespace-separated x,y pairs. */
static int parse_curve(ss_curve *c, const char *s)
{
    ss_curve_reset(c);
    if (!*s || !strcmp(s, "-") || !strcmp(s, "linear")) return 0;

    c->n = 0;
    while (*s) {
        char *end;
        float x, y;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        x = strtof(s, &end);
        if (end == s || *end != ',') return -1;
        s = end + 1;
        y = strtof(s, &end);
        if (end == s) return -1;
        s = end;
        if (c->n >= SS_CURVE_MAX_PTS) return -1;
        /* Points must arrive in x order; a curve is a function of x and an
         * out-of-order pair means the caller built it wrong. */
        if (c->n && x <= c->x[c->n-1]) return -1;
        c->x[c->n] = ss_clampf(x, 0.0f, 1.0f);
        c->y[c->n] = y;
        c->n++;
    }
    if (c->n < 2) { ss_curve_reset(c); return -1; }
    c->identity = 0;
    c->built = 0;
    ss_curve_build(c);
    return 0;
}

static void print_curve(const ss_curve *c, char *out, size_t n)
{
    size_t used = 0;
    int i;

    if (c->identity) { snprintf(out, n, "linear"); return; }
    out[0] = '\0';
    for (i = 0; i < c->n && used + 1 < n; i++) {
        int k = snprintf(out + used, n - used, "%s%.4g,%.4g",
                         i ? " " : "", c->x[i], c->y[i]);
        if (k < 0 || (size_t)k >= n - used) break;
        used += (size_t)k;
    }
}

int ss_develop_set(ss_develop *d, const char *key, const char *val)
{
    int i;

    for (i = 0; i < nfields; i++) {
        const field *f = &fields[i];
        char *base = (char *)d;
        if (strcmp(f->key, key)) continue;

        if (f->type == F_CURVE)
            return parse_curve((ss_curve *)(base + f->off), val);

        {
            char *end;
            float v = strtof(val, &end);
            if (end == val) return -2;                  /* not a number */
            while (*end == ' ' || *end == '\t') end++;
            if (*end) return -2;
            if (v < f->lo || v > f->hi) return -3;      /* out of range */
            if (f->type == F_INT) *(int *)(base + f->off) = (int)(v + 0.5f);
            else                  *(float *)(base + f->off) = v;
            return 0;
        }
    }
    return -1;                                          /* unknown key */
}

int ss_develop_get(const ss_develop *d, const char *key, char *out, size_t n)
{
    int i;
    for (i = 0; i < nfields; i++) {
        const field *f = &fields[i];
        const char *base = (const char *)d;
        if (strcmp(f->key, key)) continue;
        switch (f->type) {
        case F_CURVE: print_curve((const ss_curve *)(base + f->off), out, n); return 0;
        case F_INT:   snprintf(out, n, "%d", *(const int *)(base + f->off)); return 0;
        default:      snprintf(out, n, "%.6g", *(const float *)(base + f->off)); return 0;
        }
    }
    return -1;
}

int ss_develop_write(const ss_develop *d, FILE *fp)
{
    char buf[512];
    int i;
    for (i = 0; i < nfields; i++) {
        if (ss_develop_get(d, fields[i].key, buf, sizeof buf) != 0) continue;
        if (fprintf(fp, "%s\t%s\n", fields[i].key, buf) < 0) return -1;
    }
    return 0;
}

int ss_develop_read(ss_develop *d, FILE *fp)
{
    char line[1024];
    int bad = 0;

    ss_develop_reset(d);
    while (fgets(line, sizeof line, fp)) {
        char *tab, *nl;
        if (line[0] == '#' || line[0] == '\n') continue;
        nl = strchr(line, '\n'); if (nl) *nl = '\0';
        tab = strchr(line, '\t');
        if (!tab) { tab = strchr(line, ' '); }
        if (!tab) continue;
        *tab = '\0';
        /* An unknown or malformed key is COUNTED, never fatal: a sidecar
         * written by a newer synstudio must still open here, minus whatever
         * this build does not understand. */
        if (ss_develop_set(d, line, tab + 1) != 0) bad++;
    }
    return bad;
}
