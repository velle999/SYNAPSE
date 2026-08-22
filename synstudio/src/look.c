/* look.c — somebody else's colour, coming in.
 *
 * lut.c writes a .cube. This reads one, and reads a `.synlook` — the two ways
 * a look arrives from outside this program.
 *
 * The .cube is the important one, because it is the currency: Resolve, Premiere,
 * every camera manufacturer's "film" pack and half the internet ship 3D LUTs,
 * and a program that never links anything can still read a text file full of
 * numbers. It goes on at the END of the pointwise chain, in the DISPLAY
 * encoding — the domain a .cube is defined in — and that placement is the whole
 * design, because ss_lut_write bakes by walking ss_pixel_pointwise. An imported
 * look therefore comes out INSIDE the baked cube: the export gets it with no
 * second lut3d, no new graph, and no chance of the still and the frame
 * disagreeing about it.
 *
 * ⚠ Two things a .cube cannot say, and neither is this code's fault:
 *
 *  - Its domain is bounded (usually [0,1]). A value above white has no entry,
 *    so it is clamped to the top of the domain, which is what every other
 *    application that applies one does.
 *  - It is a lattice with linear interpolation between the nodes, so a look
 *    with a hard knee comes back slightly softened. That is the format.
 *
 * A `.synlook` is not a table at all — it is the develop stack, as text. A LUT
 * says what a colour becomes; a look says which SLIDERS moved, which means it
 * can still be adjusted after it lands. Both are worth having and they are not
 * the same thing.
 */
#include "synstudio.h"
#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------- reading a cube -- */

static int is_blank(const char *s)
{
    while (*s) { if (!isspace((unsigned char)*s)) return 0; s++; }
    return 1;
}

/* A .cube is line-oriented: keyword lines, then one RGB triple per line with
 * RED VARYING FASTEST. Getting that order backwards produces a LUT that loads
 * without complaint and swaps the red and blue of the picture, which is the
 * classic way to get this wrong in both directions. */
int ss_lut_read(const char *path, ss_lut3d *out, char *err, size_t errn)
{
    FILE *fp;
    char line[512];
    long want = 0, got = 0;
    int i;

#define FAIL(...) do { if (err) snprintf(err, errn, __VA_ARGS__); \
                       ss_lut_free(out); if (fp) fclose(fp); return -1; } while (0)

    if (!out) return -1;
    memset(out, 0, sizeof *out);
    for (i = 0; i < 3; i++) { out->dmin[i] = 0.0f; out->dmax[i] = 1.0f; }

    fp = fopen(path, "r");
    if (!fp) { if (err) snprintf(err, errn, "cannot read %s", path); return -1; }

    while (fgets(line, sizeof line, fp)) {
        char *s = line;
        char *hash;

        /* A comment is a whole line in this format, but a trailing one is
         * common enough in files people have hand-edited that refusing it
         * would reject working LUTs. */
        hash = strchr(s, '#');
        if (hash) *hash = '\0';
        while (*s == ' ' || *s == '\t') s++;
        if (is_blank(s)) continue;

        if (!strncmp(s, "TITLE", 5) && isspace((unsigned char)s[5])) {
            char *q = strchr(s, '"'), *e;
            if (q && (e = strchr(q + 1, '"')) != NULL) {
                size_t n = (size_t)(e - q - 1);
                if (n >= sizeof out->title) n = sizeof out->title - 1;
                memcpy(out->title, q + 1, n);
                out->title[n] = '\0';
            }
            continue;
        }
        if (!strncmp(s, "LUT_3D_SIZE", 11) || !strncmp(s, "LUT_1D_SIZE", 11)) {
            int three = s[4] == '3';
            long sz = strtol(s + 11, NULL, 10);
            long max = three ? SS_LUT_MAX_3D : SS_LUT_MAX_1D;
            if (out->tab) FAIL("%s: two LUT sizes in one file", path);
            if (sz < 2 || sz > max)
                FAIL("%s: LUT size %ld is not between 2 and %ld", path, sz, max);
            out->dims = three ? 3 : 1;
            out->size = (int)sz;
            want = three ? sz * sz * sz : sz;
            out->tab = calloc((size_t)want * 3, sizeof(float));
            if (!out->tab) FAIL("%s: out of memory for %ld nodes", path, want);
            continue;
        }
        if (!strncmp(s, "DOMAIN_MIN", 10) || !strncmp(s, "DOMAIN_MAX", 10)) {
            /* ⚠ INDEX 8, not 7. "DOMAIN_MIN" and "DOMAIN_MAX" share every
             * character up to and including the M — so picking them apart on
             * s[7] reads 'M' for both, sends DOMAIN_MIN into dmax, and leaves
             * the floor at its default. The table then applies over the wrong
             * range with nothing to show for it but slightly wrong colour. */
            float *d = s[8] == 'I' ? out->dmin : out->dmax;
            char *p = s + 10, *end;
            for (i = 0; i < 3; i++) {
                float v = strtof(p, &end);
                if (end == p) break;
                d[i] = v; p = end;
            }
            /* One value is legal shorthand for all three. */
            if (i == 1) d[1] = d[2] = d[0];
            continue;
        }
        if (!strncmp(s, "LUT_3D_INPUT_RANGE", 18) ||
            !strncmp(s, "LUT_1D_INPUT_RANGE", 18)) {
            char *p = s + 18, *end;
            float lo = strtof(p, &end);
            if (end != p) {
                float hi = strtof(end, &p);
                if (p != end) for (i = 0; i < 3; i++) { out->dmin[i] = lo; out->dmax[i] = hi; }
            }
            continue;
        }

        /* Anything else has to be a data row. Refuse an unknown KEYWORD
         * rather than silently reading zeroes: a file this code does not
         * understand is a look that would come out wrong, and wrong colour
         * that loaded is worse than a file that did not. */
        if (isalpha((unsigned char)*s)) FAIL("%s: unknown keyword: %.20s", path, s);
        if (!out->tab) FAIL("%s: data before LUT_3D_SIZE", path);
        if (got >= want) FAIL("%s: more than %ld rows", path, want);
        {
            char *p = s, *end;
            float v[3];
            for (i = 0; i < 3; i++) {
                v[i] = strtof(p, &end);
                if (end == p) FAIL("%s: row %ld is not three numbers", path, got + 1);
                p = end;
            }
            out->tab[got * 3 + 0] = v[0];
            out->tab[got * 3 + 1] = v[1];
            out->tab[got * 3 + 2] = v[2];
            got++;
        }
    }
    fclose(fp);
    fp = NULL;

    if (!out->tab) FAIL("%s: no LUT_3D_SIZE or LUT_1D_SIZE", path);
    /* A truncated LUT is the failure this catches: a download that stopped
     * loads perfectly and renders the top of the picture as black. */
    if (got != want) FAIL("%s: %ld rows, expected %ld", path, got, want);
    for (i = 0; i < 3; i++)
        if (!(out->dmax[i] > out->dmin[i]))
            FAIL("%s: empty domain on channel %d", path, i);
    return 0;
#undef FAIL
}

void ss_lut_free(ss_lut3d *l)
{
    if (!l) return;
    free(l->tab);
    l->tab = NULL;
    l->size = l->dims = 0;
}

void ss_lut_eval(const ss_lut3d *l, const float in[3], float out[3])
{
    float x[3];
    int i;

    if (!l || !l->tab) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; return; }

    for (i = 0; i < 3; i++)
        x[i] = ss_clampf((in[i] - l->dmin[i]) / (l->dmax[i] - l->dmin[i]),
                         0.0f, 1.0f) * (float)(l->size - 1);

    if (l->dims == 1) {
        /* Three independent curves. A 1D LUT cannot move a hue, only a
         * channel's response — which is exactly what makes it the right shape
         * for a transfer function and the wrong one for a look. */
        for (i = 0; i < 3; i++) {
            int i0 = (int)x[i], i1;
            float f = x[i] - (float)i0;
            if (i0 >= l->size - 1) { i0 = l->size - 1; i1 = i0; f = 0.0f; }
            else i1 = i0 + 1;
            out[i] = l->tab[i0 * 3 + i] + (l->tab[i1 * 3 + i] - l->tab[i0 * 3 + i]) * f;
        }
        return;
    }

    {
        int i0[3], i1[3];
        float f[3];
        int s = l->size;

        for (i = 0; i < 3; i++) {
            i0[i] = (int)x[i];
            if (i0[i] >= s - 1) { i0[i] = s - 1; i1[i] = i0[i]; f[i] = 0.0f; }
            else { i1[i] = i0[i] + 1; f[i] = x[i] - (float)i0[i]; }
        }

#define NODE(R, G, B) (((size_t)(B) * s + (G)) * s + (R)) * 3
        for (i = 0; i < 3; i++) {
            float c00 = l->tab[NODE(i0[0], i0[1], i0[2]) + i] * (1 - f[0])
                      + l->tab[NODE(i1[0], i0[1], i0[2]) + i] * f[0];
            float c01 = l->tab[NODE(i0[0], i0[1], i1[2]) + i] * (1 - f[0])
                      + l->tab[NODE(i1[0], i0[1], i1[2]) + i] * f[0];
            float c10 = l->tab[NODE(i0[0], i1[1], i0[2]) + i] * (1 - f[0])
                      + l->tab[NODE(i1[0], i1[1], i0[2]) + i] * f[0];
            float c11 = l->tab[NODE(i0[0], i1[1], i1[2]) + i] * (1 - f[0])
                      + l->tab[NODE(i1[0], i1[1], i1[2]) + i] * f[0];
            float c0 = c00 * (1 - f[1]) + c10 * f[1];
            float c1 = c01 * (1 - f[1]) + c11 * f[1];
            out[i] = c0 * (1 - f[2]) + c1 * f[2];
        }
#undef NODE
    }
}

/* ------------------------------------------------------- where they live -- */

/* Installed, then the user's, then anything named on the way in — the same
 * order and the same reasoning as the effects catalogue: your own copy of a
 * name beats the shipped one, so a look can be fixed without waiting for
 * anybody. */
static void each_dir(const char *leaf, void (*visit)(const char *, void *),
                     void *ctx)
{
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *env = getenv(!strcmp(leaf, "luts") ? "SYNSTUDIO_LUTS"
                                                   : "SYNSTUDIO_LOOKS");
    char buf[1024];

    snprintf(buf, sizeof buf, "%s/%s", SYNSTUDIO_DATADIR, leaf);
    visit(buf, ctx);
    if (xdg && *xdg)        snprintf(buf, sizeof buf, "%s/synstudio/%s", xdg, leaf);
    else if (home && *home) snprintf(buf, sizeof buf, "%s/.config/synstudio/%s", home, leaf);
    else                    buf[0] = '\0';
    if (buf[0]) visit(buf, ctx);

    if (env && *env) {
        char *copy = strdup(env), *p, *save;
        if (copy) {
            for (p = strtok_r(copy, ":", &save); p; p = strtok_r(NULL, ":", &save))
                if (*p) visit(p, ctx);
            free(copy);
        }
    }
}

static void basename_noext(const char *file, const char *ext, char *out, size_t n)
{
    const char *slash = strrchr(file, '/');
    size_t len;
    const char *b = slash ? slash + 1 : file;
    len = strlen(b);
    if (len > strlen(ext) && !strcmp(b + len - strlen(ext), ext)) len -= strlen(ext);
    if (len >= n) len = n - 1;
    memcpy(out, b, len);
    out[len] = '\0';
}

/* `~/x` and a relative path both become something fopen can use. */
static void expand_path(const char *in, char *out, size_t n)
{
    const char *home = getenv("HOME");
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0') && home && *home)
        snprintf(out, n, "%s%s", home, in + 1);
    else
        snprintf(out, n, "%s", in);
}

/* ------------------------------------------------------- the LUT catalogue */

static ss_lut_entry *lcat;
static int nlcat, lcatcap, lloaded;

static void lut_add(const char *path)
{
    ss_lut_entry e;
    ss_lut3d l;
    char err[256];
    int i;

    memset(&e, 0, sizeof e);
    basename_noext(path, ".cube", e.name, sizeof e.name);
    snprintf(e.path, sizeof e.path, "%s", path);

    /* Read it NOW rather than at first use. A catalogue that lists a file it
     * cannot parse hands the window a row that fails when clicked, which is
     * the same mistake the browser avoids by not listing what it cannot
     * open. It also fills in the size, which is what a person picking one
     * actually wants to see. */
    if (ss_lut_read(path, &l, err, sizeof err) != 0) return;
    e.dims = l.dims;
    e.size = l.size;
    ss_lut_free(&l);

    for (i = 0; i < nlcat; i++)
        if (!strcmp(lcat[i].name, e.name)) { lcat[i] = e; return; }
    if (nlcat >= lcatcap) {
        int want = lcatcap ? lcatcap * 2 : 16;
        ss_lut_entry *n = realloc(lcat, sizeof *n * (size_t)want);
        if (!n) return;
        lcat = n; lcatcap = want;
    }
    lcat[nlcat++] = e;
}

static void lut_dir(const char *dir, void *ctx)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    (void)ctx;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".cube")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        lut_add(path);
    }
    closedir(d);
}

static void lut_load(void)
{
    if (lloaded) return;
    lloaded = 1;
    each_dir("luts", lut_dir, NULL);
}

int ss_lut_count(void)             { lut_load(); return nlcat; }
const ss_lut_entry *ss_lut_at(int i)
{
    lut_load();
    return (i >= 0 && i < nlcat) ? &lcat[i] : NULL;
}

int ss_lut_resolve(const char *ref, char *out, size_t n)
{
    int i;

    if (!ref || !*ref) return -1;
    if (strchr(ref, '/')) {
        char full[1024];
        expand_path(ref, full, sizeof full);
        snprintf(out, n, "%s", full);
        return 0;
    }
    lut_load();
    for (i = 0; i < nlcat; i++)
        if (!strcmp(lcat[i].name, ref)) { snprintf(out, n, "%s", lcat[i].path); return 0; }
    return -1;
}

const ss_lut_entry *ss_lut_lookup(const char *ref)
{
    static ss_lut_entry direct;
    int i;

    if (!ref || !*ref) return NULL;

    /* A '/' means a path — somebody dropped a file in from anywhere. Without
     * one it is a catalogue name, which is what travels: a project that names
     * `kodak2383` opens on a machine where that LUT lives somewhere else. */
    if (strchr(ref, '/')) {
        ss_lut3d l;
        char err[256];
        memset(&direct, 0, sizeof direct);
        expand_path(ref, direct.path, sizeof direct.path);
        basename_noext(direct.path, ".cube", direct.name, sizeof direct.name);
        if (ss_lut_read(direct.path, &l, err, sizeof err) != 0) return NULL;
        direct.dims = l.dims;
        direct.size = l.size;
        ss_lut_free(&l);
        return &direct;
    }

    lut_load();
    for (i = 0; i < nlcat; i++)
        if (!strcmp(lcat[i].name, ref)) return &lcat[i];
    return NULL;
}

/* ---- the cache ----
 *
 * ss_pixel_pointwise asks for this per PIXEL. Reading and parsing thirty-five
 * thousand rows of text per pixel is not a thing that can be allowed to
 * happen by accident, so the answer is held; and a reference that resolves to
 * nothing is held too, as a NEGATIVE entry, or a missing LUT would re-open
 * (and re-fail, and re-warn) once for every pixel of the frame. */
#define LUT_CACHE 4

static struct {
    char     ref[SS_LUT_REF];
    ss_lut3d lut;
    int      used;              /* 0 = free, else a recency stamp */
    int      ok;
} lcache[LUT_CACHE];
static int lclock;

const ss_lut3d *ss_lut_cached(const char *ref)
{
    const ss_lut_entry *e;
    char err[256];
    int i, victim = 0;

    if (!ref || !*ref) return NULL;

    for (i = 0; i < LUT_CACHE; i++)
        if (lcache[i].used && !strcmp(lcache[i].ref, ref)) {
            lcache[i].used = ++lclock;
            return lcache[i].ok ? &lcache[i].lut : NULL;
        }

    for (i = 1; i < LUT_CACHE; i++)
        if (lcache[i].used < lcache[victim].used) victim = i;
    if (lcache[victim].used && lcache[victim].ok) ss_lut_free(&lcache[victim].lut);
    memset(&lcache[victim], 0, sizeof lcache[victim]);
    snprintf(lcache[victim].ref, sizeof lcache[victim].ref, "%s", ref);
    lcache[victim].used = ++lclock;

    e = ss_lut_lookup(ref);
    if (!e || ss_lut_read(e->path, &lcache[victim].lut, err, sizeof err) != 0) {
        /* Once per reference. The grade is KEPT either way — see the header:
         * a look this machine has not got must not be quietly deleted from
         * somebody else's project. */
        fprintf(stderr, "synstudio: no LUT named %s — the grade keeps it, "
                        "it renders as nothing\n", ref);
        return NULL;
    }
    lcache[victim].ok = 1;
    return &lcache[victim].lut;
}

/* ------------------------------------------------------ .synlook presets -- */

static ss_look *kcat;
static int nkcat, kcatcap, kloaded;

static int look_read(const char *path, ss_look *out)
{
    FILE *fp = fopen(path, "r");
    char line[1024];

    if (!fp) return -1;
    memset(out, 0, sizeof *out);
    basename_noext(path, ".synlook", out->name, sizeof out->name);
    snprintf(out->path, sizeof out->path, "%s", path);
    snprintf(out->label, sizeof out->label, "%s", out->name);

    while (fgets(line, sizeof line, fp)) {
        char *tab, *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '#' || !line[0]) continue;
        tab = strchr(line, '\t');
        if (!tab) tab = strchr(line, ' ');
        if (!tab) continue;
        *tab = '\0';
        if (!strcmp(line, "label")) snprintf(out->label, sizeof out->label, "%s", tab + 1);
        else if (!strcmp(line, "about")) snprintf(out->about, sizeof out->about, "%s", tab + 1);
    }
    fclose(fp);
    return 0;
}

static void look_dir(const char *dir, void *ctx)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    (void)ctx;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        ss_look lk;
        int i;
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcmp(dot, ".synlook")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (look_read(path, &lk) != 0) continue;
        for (i = 0; i < nkcat; i++)
            if (!strcmp(kcat[i].name, lk.name)) break;
        if (i < nkcat) { kcat[i] = lk; continue; }
        if (nkcat >= kcatcap) {
            int want = kcatcap ? kcatcap * 2 : 16;
            ss_look *n = realloc(kcat, sizeof *n * (size_t)want);
            if (!n) continue;
            kcat = n; kcatcap = want;
        }
        kcat[nkcat++] = lk;
    }
    closedir(d);
}

static void look_load(void)
{
    if (kloaded) return;
    kloaded = 1;
    each_dir("looks", look_dir, NULL);
}

int ss_look_count(void)          { look_load(); return nkcat; }
const ss_look *ss_look_at(int i)  { look_load();
                                    return (i >= 0 && i < nkcat) ? &kcat[i] : NULL; }
const ss_look *ss_look_find(const char *name)
{
    int i;
    if (!name || !*name) return NULL;
    look_load();
    for (i = 0; i < nkcat; i++)
        if (!strcmp(kcat[i].name, name)) return &kcat[i];
    return NULL;
}

/* Geometry is not part of a look — see the header. Asking the table which
 * group a key is in rather than listing the keys here means a geometry
 * control added later is excluded by having been put in the right group,
 * which is the only kind of list that does not go stale. */
static int is_geometry(const char *key)
{
    ss_develop_info f;
    int i;
    for (i = 0; ss_develop_describe(i, &f) == 0; i++)
        if (!strcmp(f.key, key)) return !strcmp(f.group, "Geometry");
    return 0;
}

int ss_look_apply(const ss_look *lk, ss_develop *d)
{
    FILE *fp;
    char line[1024];
    int bad = 0;

    if (!lk || !d) return -1;
    fp = fopen(lk->path, "r");
    if (!fp) return -1;

    while (fgets(line, sizeof line, fp)) {
        char *tab, *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '#' || !line[0]) continue;
        tab = strchr(line, '\t');
        if (!tab) tab = strchr(line, ' ');
        if (!tab) continue;
        *tab = '\0';
        if (!strcmp(line, "label") || !strcmp(line, "about")) continue;
        if (is_geometry(line)) continue;
        if (ss_develop_set(d, line, tab + 1) != 0) bad++;
    }
    fclose(fp);
    return bad;
}

int ss_look_dir(char *out, size_t n)
{
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *env = getenv("SYNSTUDIO_LOOKS");

    /* When a bundle is named, a look saved goes into the FIRST directory of
     * it — which is what the test suite reads back, and what a project that
     * carries its own looks would want. */
    if (env && *env) {
        const char *colon = strchr(env, ':');
        size_t len = colon ? (size_t)(colon - env) : strlen(env);
        if (len && len < n) { memcpy(out, env, len); out[len] = '\0'; return 0; }
    }
    if (xdg && *xdg) { snprintf(out, n, "%s/synstudio/looks", xdg); return 0; }
    if (home && *home) { snprintf(out, n, "%s/.config/synstudio/looks", home); return 0; }
    return -1;
}

/* The look directory may not exist yet — nothing else in this program has
 * ever had a reason to write to ~/.config/synstudio. */
static int mkdir_p(const char *dir)
{
    char buf[1024];
    char *p;
    snprintf(buf, sizeof buf, "%s", dir);
    for (p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    return (mkdir(buf, 0755) != 0 && errno != EEXIST) ? -1 : 0;
}

int ss_look_save(const char *name, const char *label, const ss_develop *d,
                 char *path, size_t n)
{
    ss_develop def;
    ss_develop_info f;
    char dir[1024], p[1200];
    FILE *fp;
    int i, wrote = 0;

    if (!name || !*name || strchr(name, '/')) return -1;
    if (ss_look_dir(dir, sizeof dir) != 0) return -1;
    if (mkdir_p(dir) != 0) return -1;
    snprintf(p, sizeof p, "%s/%s.synlook", dir, name);

    fp = fopen(p, "w");
    if (!fp) return -1;
    ss_develop_reset(&def);
    fprintf(fp, "# synstudio look\n");
    fprintf(fp, "label\t%s\n", (label && *label) ? label : name);

    /* Only what MOVED. A look that wrote all sixty-six fields would carry a
     * default temperature and a default contrast into every photograph it
     * touched, which is the difference between a look and a reset. */
    for (i = 0; ss_develop_describe(i, &f) == 0; i++) {
        char have[512], was[512];
        if (!strcmp(f.group, "Geometry")) continue;
        if (ss_develop_get(d, f.key, have, sizeof have) != 0) continue;
        if (ss_develop_get(&def, f.key, was, sizeof was) != 0) continue;
        if (!strcmp(have, was)) continue;
        fprintf(fp, "%s\t%s\n", f.key, have);
        wrote++;
    }
    if (fclose(fp) != 0) return -1;
    if (path) snprintf(path, n, "%s", p);
    kloaded = 0;                /* it is in the catalogue from now on */
    nkcat = 0;
    return wrote;
}
