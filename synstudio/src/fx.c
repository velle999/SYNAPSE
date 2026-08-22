/* fx.c — other people's effects.
 *
 * Resolve takes third-party work three ways: OpenFX (compiled C++), DCTL (GPU
 * shader source) and LUTs (data). Only the third fits a program that never
 * links anything — and this is the fourth way, which fits it just as well: a
 * text manifest naming an ffmpeg filter chain and the knobs on it.
 *
 *     name    halation
 *     label   Halation
 *     param   strength  0.4  0  1   Strength
 *     param   radius    12   1  64  Radius
 *     filter  [$in]split[a][b];[b]lumakey=0.6,gblur=sigma=$radius[g];...[$out]
 *
 * An effect is then a FILE. Somebody can write one in a text editor, mail it,
 * and it works — no compiler, no ABI, nothing to rebuild when ffmpeg bumps a
 * SONAME. The engine substitutes the declared parameters, renames the internal
 * labels so two copies of the same effect can sit on two clips, and hands the
 * result to the same graph builders everything else goes through.
 *
 * ⚠ A FILTER STRING CAN DO ANYTHING FFMPEG CAN, INCLUDING READ FILES. That is
 * the whole risk of this format, and it is why every recipe is checked against
 * a WHITELIST of filter names before it ever enters the catalogue, why nothing
 * but a declared parameter is ever interpolated, why a parameter is always a
 * NUMBER, and why any argument that names a file is refused outright. A recipe
 * that does not pass is not loaded, so a broken or hostile one fails when it
 * lands rather than in the middle of somebody's export.
 *
 * The whitelist is also what keeps the MONITOR honest. Everything on it takes
 * one frame in and hands one frame out, the same size — so a filter that needs
 * a window of frames (tblend, tmix, deflicker), or that changes the frame rate
 * or the geometry (fps, scale, crop, rotate), or that is random (noise), is
 * not on it. The first would render differently on a one-frame monitor than in
 * an export, the second would move the picture out from under the transform
 * that owns it, and the third would disagree with itself.
 */
#include "synstudio.h"
#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- what a recipe may name ----
 *
 * One frame in, one frame out, same size, same rate, same answer every time.
 * Adding to this list is a deliberate act: read the filter's documentation for
 * a `file`, `model` or `script` option before you do, because that is the one
 * that turns a recipe into a way to read somebody's disk. */
static const char *allowed[] = {
    /* plumbing */
    "split", "format", "null", "copy", "setsar",
    /* blur and sharpen */
    "gblur", "boxblur", "avgblur", "unsharp", "smartblur", "sab",
    /* combine */
    "blend", "alphamerge", "alphaextract", "maskedmerge", "premultiply",
    "unpremultiply",
    /* colour */
    "colorchannelmixer", "colorbalance", "colorlevels", "colortemperature",
    "colorize", "exposure", "eq", "hue", "huesaturation", "vibrance",
    "negate", "monochrome", "pseudocolor", "selectivecolor", "swapuv",
    "lutrgb", "lutyuv", "lut", "colorcontrast", "colorcorrect",
    /* keys and alpha */
    "chromakey", "colorkey", "lumakey", "chromahold", "colorhold", "despill",
    /* shape and texture */
    "edgedetect", "sobel", "prewitt", "roberts", "convolution", "dilation",
    "erosion", "deband", "gradfun", "pixelize", "vignette", "geq",
    "lenscorrection", "rgbashift", "chromashift", "hflip", "vflip",
    "fillborders", "shuffleplanes", "elbg", "photosensitivity",
    NULL
};

/* Belt and braces on top of the whitelist: an option that names anything on
 * disk. None of the filters above have one, which is the point — this catches
 * the day one of them GAINS one, or the day this list and that one drift. */
static const char *forbidden[] = {
    "file=", "filename=", "textfile=", "fontfile=", "psfile=", "path=",
    "model=", "script=", "movie=", "amovie=", NULL
};

/* --------------------------------------------------------- the scanner -- */

/* The filter names in a graph fragment.
 *
 * Not a split on commas: a filter argument can be a quoted expression with
 * commas in it — `geq=lum='if(lt(X,10),0,255)'` is one filter, not three — and
 * a scanner that did not know that would let an unknown name through by
 * cutting it in half. Labels in brackets are skipped; what follows a `;`, a
 * `,` or a `]` is a filter name up to its `=`. */
static int scan_names(const char *s, int (*visit)(const char *, void *),
                      void *ctx)
{
    int expect = 1;

    while (*s) {
        if (*s == '[') {                       /* a label */
            while (*s && *s != ']') s++;
            if (*s) s++;
            expect = 1;
            continue;
        }
        if (*s == ';' || *s == ',') { s++; expect = 1; continue; }
        if (isspace((unsigned char)*s)) { s++; continue; }
        if (expect) {
            char name[64];
            size_t n = 0;
            while (*s && *s != '=' && *s != ',' && *s != ';' && *s != '[' &&
                   !isspace((unsigned char)*s)) {
                if (n + 1 < sizeof name) name[n++] = *s;
                s++;
            }
            name[n] = '\0';
            if (n && visit(name, ctx) != 0) return -1;
            expect = 0;
            continue;
        }
        /* inside a filter's arguments: step over quoted runs whole */
        if (*s == '\'') {
            s++;
            while (*s && *s != '\'') { if (*s == '\\' && s[1]) s++; s++; }
            if (*s) s++;
            continue;
        }
        s++;
    }
    return 0;
}

static int check_name(const char *name, void *ctx)
{
    char *bad = ctx;
    int i;
    for (i = 0; allowed[i]; i++)
        if (!strcmp(allowed[i], name)) return 0;
    snprintf(bad, 64, "%s", name);
    return -1;
}

/* --------------------------------------------------------- the parser -- */

static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

/* The word at the front of a line, and what follows it. */
static char *split_word(char *s, char **rest)
{
    char *p = s;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) { *p++ = '\0'; }
    *rest = trim(p);
    return s;
}

int ss_fx_read(const char *path, ss_fx *out, char *err, size_t errn)
{
    FILE *fp = fopen(path, "r");
    char line[4096], bad[64] = "";
    int ok = 0;

    if (err && errn) *err = '\0';
    if (!fp) {
        if (err) snprintf(err, errn, "cannot open %s", path);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->path, sizeof out->path, "%s", path);

    while (fgets(line, sizeof line, fp)) {
        char *nl = strchr(line, '\n'), *word, *rest;
        if (nl) *nl = '\0';
        word = trim(line);
        if (!*word || *word == '#') continue;
        word = split_word(word, &rest);

        if (!strcmp(word, "name"))       snprintf(out->name, sizeof out->name, "%s", rest);
        else if (!strcmp(word, "label")) snprintf(out->label, sizeof out->label, "%s", rest);
        else if (!strcmp(word, "group")) snprintf(out->group, sizeof out->group, "%s", rest);
        else if (!strcmp(word, "about")) snprintf(out->about, sizeof out->about, "%s", rest);
        else if (!strcmp(word, "alpha")) out->alpha = atoi(rest) != 0;
        else if (!strcmp(word, "filter")) {
            /* Appended, so a long chain can be written over several lines
             * rather than as one unreadable ribbon. */
            size_t have = strlen(out->filter);
            snprintf(out->filter + have, sizeof out->filter - have, "%s", rest);
            ok = 1;
        } else if (!strcmp(word, "param")) {
            ss_fx_param *p;
            char *k, *d, *lo, *hi;
            if (out->nparam >= SS_MAX_FX_PARAMS) {
                if (err) snprintf(err, errn, "more than %d parameters",
                                  SS_MAX_FX_PARAMS);
                fclose(fp);
                return -1;
            }
            p = &out->param[out->nparam];
            k  = split_word(rest, &rest);
            d  = split_word(rest, &rest);
            lo = split_word(rest, &rest);
            hi = split_word(rest, &rest);
            if (!*k || !*d || !*lo || !*hi) {
                if (err) snprintf(err, errn, "param wants KEY DEFAULT LO HI LABEL");
                fclose(fp);
                return -1;
            }
            snprintf(p->key, sizeof p->key, "%s", k);
            p->def = atof(d);
            p->lo  = atof(lo);
            p->hi  = atof(hi);
            snprintf(p->label, sizeof p->label, "%s", *rest ? rest : k);
            out->nparam++;
        } else {
            if (err) snprintf(err, errn, "unknown directive: %s", word);
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);

    if (!*out->name) { if (err) snprintf(err, errn, "no name"); return -1; }
    if (!ok || !*out->filter) {
        if (err) snprintf(err, errn, "no filter chain");
        return -1;
    }
    if (!*out->label) snprintf(out->label, sizeof out->label, "%s", out->name);
    if (!*out->group) snprintf(out->group, sizeof out->group, "Effects");

    if (!strstr(out->filter, "[$in]") || !strstr(out->filter, "[$out]")) {
        if (err) snprintf(err, errn,
                          "the chain must take [$in] and produce [$out]");
        return -1;
    }
    {
        int i;
        for (i = 0; forbidden[i]; i++)
            if (strstr(out->filter, forbidden[i])) {
                if (err) snprintf(err, errn,
                                  "an effect may not name a file (%s)",
                                  forbidden[i]);
                return -1;
            }
    }
    if (scan_names(out->filter, check_name, bad) != 0) {
        if (err) snprintf(err, errn, "%s is not an allowed filter", bad);
        return -1;
    }
    /* Every $name has to be one that was declared. An effect referring to a
     * parameter nobody can set would reach ffmpeg as a literal dollar sign and
     * fail the whole graph at export time. */
    {
        const char *p = out->filter;
        while ((p = strchr(p, '$')) != NULL) {
            char nm[32];
            size_t n = 0;
            int i, found = 0;
            p++;
            while (p[n] && (isalnum((unsigned char)p[n]) || p[n] == '_')) {
                if (n + 1 < sizeof nm) nm[n] = p[n];
                n++;
            }
            nm[n < sizeof nm ? n : sizeof nm - 1] = '\0';
            if (!strcmp(nm, "in") || !strcmp(nm, "out")) { p += n; continue; }
            for (i = 0; i < out->nparam; i++)
                if (!strcmp(out->param[i].key, nm)) { found = 1; break; }
            if (!found) {
                if (err) snprintf(err, errn, "$%s is not a parameter", nm);
                return -1;
            }
            p += n;
        }
    }
    return 0;
}

/* ------------------------------------------------------- the catalogue -- */

static ss_fx  *cat;
static int     ncat, catcap, loaded;

static int cat_add(const ss_fx *fx)
{
    int i;
    for (i = 0; i < ncat; i++)
        if (!strcmp(cat[i].name, fx->name)) { cat[i] = *fx; return i; }
    if (ncat >= catcap) {
        int want = catcap ? catcap * 2 : 32;
        ss_fx *n = realloc(cat, sizeof(ss_fx) * (size_t)want);
        if (!n) return -1;
        cat = n; catcap = want;
    }
    cat[ncat] = *fx;
    return ncat++;
}

static void load_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char path[1024], err[128];
        const char *dot = strrchr(e->d_name, '.');
        ss_fx fx;
        if (!dot || strcmp(dot, ".synfx")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (ss_fx_read(path, &fx, err, sizeof err) == 0) cat_add(&fx);
    }
    closedir(d);
}

int ss_fx_load(void)
{
    const char *env = getenv("SYNSTUDIO_EFFECTS");
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char buf[1024];

    if (loaded) return ncat;
    loaded = 1;

    /* The shipped ones first, then the user's, then whatever was named on the
     * way in — so a recipe of your own with the same name as a built-in wins,
     * which is how you fix one without waiting for anybody. */
    snprintf(buf, sizeof buf, "%s/effects", SYNSTUDIO_DATADIR);
    load_dir(buf);
    if (xdg && *xdg) {
        snprintf(buf, sizeof buf, "%s/synstudio/effects", xdg);
        load_dir(buf);
    } else if (home && *home) {
        snprintf(buf, sizeof buf, "%s/.config/synstudio/effects", home);
        load_dir(buf);
    }
    if (env && *env) {
        /* A colon-separated list, like a PATH. What a bundle of effects
         * shipped with a project is loaded from, and what the tests use. */
        char *copy = strdup(env), *p, *save;
        if (copy) {
            for (p = strtok_r(copy, ":", &save); p; p = strtok_r(NULL, ":", &save))
                if (*p) load_dir(p);
            free(copy);
        }
    }
    return ncat;
}

int ss_fx_count(void)            { ss_fx_load(); return ncat; }
const ss_fx *ss_fx_at(int i)     { ss_fx_load();
                                   return (i >= 0 && i < ncat) ? &cat[i] : NULL; }

const ss_fx *ss_fx_find(const char *name)
{
    int i;
    if (!name) return NULL;
    ss_fx_load();
    for (i = 0; i < ncat; i++)
        if (!strcmp(cat[i].name, name)) return &cat[i];
    return NULL;
}

/* ------------------------------------------------------ the expansion -- */

/* `[$in]` and `[$out]` become the labels the caller is splicing between, and
 * every OTHER label gets the instance number stuck on the end of it — because
 * the same effect on two clips is the same recipe twice in one graph, and two
 * `[a]`s in a filter graph is a graph ffmpeg refuses.
 *
 * A parameter becomes a NUMBER, always: clamped to the range the recipe
 * declared, printed by this code and never taken from the document as text.
 * That is what makes a value in a project file unable to smuggle a filter
 * argument into the chain. */
int ss_fx_expand(const ss_fx *fx, const double *vals, int nvals, int uid,
                 const char *inlab, const char *outlab, char *out, size_t n)
{
    const char *s = fx->filter;
    size_t o = 0;

#define PUT(str) do { const char *_p = (str); \
        while (*_p) { if (o + 1 >= n) return -1; out[o++] = *_p++; } } while (0)

    while (*s) {
        if (*s == '[') {
            const char *e = strchr(s, ']');
            char lab[64];
            size_t len;
            if (!e) return -1;
            len = (size_t)(e - s - 1);
            if (len >= sizeof lab) return -1;
            memcpy(lab, s + 1, len);
            lab[len] = '\0';
            PUT("[");
            if (!strcmp(lab, "$in"))       PUT(inlab);
            else if (!strcmp(lab, "$out")) PUT(outlab);
            else {
                char uniq[80];
                snprintf(uniq, sizeof uniq, "%s_%d", lab, uid);
                PUT(uniq);
            }
            PUT("]");
            s = e + 1;
            continue;
        }
        if (*s == '$') {
            char nm[32], num[48];
            size_t len = 0;
            int i;
            s++;
            while (s[len] && (isalnum((unsigned char)s[len]) || s[len] == '_')) {
                if (len + 1 < sizeof nm) nm[len] = s[len];
                len++;
            }
            nm[len < sizeof nm ? len : sizeof nm - 1] = '\0';
            s += len;
            for (i = 0; i < fx->nparam; i++)
                if (!strcmp(fx->param[i].key, nm)) break;
            if (i >= fx->nparam) return -1;
            {
                double v = (i < nvals && vals) ? vals[i] : fx->param[i].def;
                if (v < fx->param[i].lo) v = fx->param[i].lo;
                if (v > fx->param[i].hi) v = fx->param[i].hi;
                snprintf(num, sizeof num, "%g", v);
            }
            PUT(num);
            continue;
        }
        if (o + 1 >= n) return -1;
        out[o++] = *s++;
    }
    if (o >= n) return -1;
    out[o] = '\0';
    return 0;
#undef PUT
}
