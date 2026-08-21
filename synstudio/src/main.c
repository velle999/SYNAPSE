/* main.c — the command line, which IS the application.
 *
 * The window in data/synstudio.qml is a renderer over this binary and holds no
 * decision of its own: it shells out to `synstudio` for every read and every
 * write, exactly as synfiles and syn-edit do. That is what makes the whole
 * editor testable from a shell script with no display, no compositor and no
 * GPU — tests/run.sh drives the same code path the window does.
 *
 * Output is tab-separated, one record per line, because that is what the rest
 * of the suite parses and because it survives a photograph whose name
 * contains a space, a quote or a newline better than anything prettier.
 */
#include "synstudio.h"
#include "config.h"

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int die(const char *fmt, ...)
{
    va_list ap;
    fputs("synstudio: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return 1;
}

static void usage(void)
{
    puts(
"synstudio " SYNSTUDIO_VERSION " — the SynapseOS photo and video editor\n"
"\n"
"PHOTOGRAPHS  (edits live in <file>.synstudio; the original is never written)\n"
"  probe FILE                    what the file is: size, codec, duration\n"
"  keys                          every develop key, with its range\n"
"  get FILE [KEY]                read the sidecar\n"
"  set FILE KEY=VALUE...         change the sidecar\n"
"  reset FILE                    throw the sidecar away\n"
"  mask FILE add linear|radial   add a local adjustment\n"
"  mask FILE list                list them\n"
"  mask FILE N KEY=VALUE...      change one (geom=x0,y0,x1,y1,feather)\n"
"  render FILE --out OUT         apply the sidecar and write a new file\n"
"  histogram FILE                256 bins per channel, tab separated\n"
"  pixel R G B [--set K=V]...    one colour through the stack (0..1 encoded)\n"
"\n"
"LOOKS\n"
"  lut --out F.cube              bake the colour half as a 3D LUT\n"
"       [--from FILE] [--size 33] [--set K=V]...\n"
"\n"
"VIDEO\n"
"  timeline new PROJ [--size WxH] [--fps F]\n"
"  timeline track PROJ video|audio [NAME]\n"
"  timeline clip PROJ TRACK FILE --at T --in A --out B\n"
"       [--gain dB] [--opacity F] [--fade-in S] [--fade-out S] [--speed F]\n"
"  timeline show PROJ            the document, as written\n"
"  timeline grade PROJ T C KEY=VALUE...   grade one clip\n"
"  timeline export PROJ --out OUT [--print]\n"
"\n"
"COMMON\n"
"  --size N        longest edge for a render or a preview (0 = full)\n"
"  --quality 1-100 jpeg quality        --bits 8|16   output depth\n"
"  --set K=V       an override applied on top of the sidecar, not saved\n"
"\n"
"  browse [DIR]    what is in a folder that this engine can open\n"
"  gui [FILE]      the window\n");
}

/* ------------------------------------------------------------ arg soup -- */

typedef struct {
    const char *out;
    const char *from;
    int    size, quality, bits, lutsize, print;
    double at, in, outp, speed;
    double fade_in, fade_out;
    float  gain, opacity;
    char   set_key[64][64];
    char   set_val[64][256];
    int    nsets;
} opts;

static void opts_default(opts *o)
{
    memset(o, 0, sizeof(*o));
    o->quality = 95;
    o->bits = 8;
    o->lutsize = 33;
    o->speed = 1.0;
    o->opacity = 1.0f;
    o->outp = -1.0;
}

/* Returns the index of the first non-option argument left over, or -1. */
static int parse_opts(int argc, char **argv, int start, opts *o, char ***rest,
                      int *nrest)
{
    static char *leftover[64];
    int i, n = 0;

    for (i = start; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : NULL)
        if (!strcmp(a, "--out"))          { const char *v = NEXT(); if (!v) return -1; o->out = v; }
        else if (!strcmp(a, "--from"))    { const char *v = NEXT(); if (!v) return -1; o->from = v; }
        else if (!strcmp(a, "--size"))    { const char *v = NEXT(); if (!v) return -1; o->size = atoi(v); }
        else if (!strcmp(a, "--quality")) { const char *v = NEXT(); if (!v) return -1; o->quality = atoi(v); }
        else if (!strcmp(a, "--bits"))    { const char *v = NEXT(); if (!v) return -1; o->bits = atoi(v); }
        else if (!strcmp(a, "--lut-size")){ const char *v = NEXT(); if (!v) return -1; o->lutsize = atoi(v); }
        else if (!strcmp(a, "--at"))      { const char *v = NEXT(); if (!v) return -1; o->at = atof(v); }
        else if (!strcmp(a, "--in"))      { const char *v = NEXT(); if (!v) return -1; o->in = atof(v); }
        else if (!strcmp(a, "--out-at"))  { const char *v = NEXT(); if (!v) return -1; o->outp = atof(v); }
        else if (!strcmp(a, "--speed"))   { const char *v = NEXT(); if (!v) return -1; o->speed = atof(v); }
        else if (!strcmp(a, "--gain"))    { const char *v = NEXT(); if (!v) return -1; o->gain = (float)atof(v); }
        else if (!strcmp(a, "--opacity")) { const char *v = NEXT(); if (!v) return -1; o->opacity = (float)atof(v); }
        else if (!strcmp(a, "--fade-in")) { const char *v = NEXT(); if (!v) return -1; o->fade_in = atof(v); }
        else if (!strcmp(a, "--fade-out")){ const char *v = NEXT(); if (!v) return -1; o->fade_out = atof(v); }
        else if (!strcmp(a, "--print"))   { o->print = 1; }
        else if (!strcmp(a, "--set")) {
            const char *v = NEXT(), *eq;
            if (!v) return -1;
            eq = strchr(v, '=');
            if (!eq || o->nsets >= 64) return -1;
            snprintf(o->set_key[o->nsets], 64, "%.*s", (int)(eq - v), v);
            snprintf(o->set_val[o->nsets], 256, "%s", eq + 1);
            o->nsets++;
        }
        else if (n < 64) leftover[n++] = argv[i];
#undef NEXT
    }
    *rest = leftover;
    *nrest = n;
    return 0;
}

/* A KEY=VALUE on the command line, applied and reported by name. Silently
 * ignoring a typo'd key is the single most annoying thing a tool like this can
 * do: the user changes a slider, nothing happens, and there is no message. */
static int apply_set(ss_develop *d, const char *key, const char *val)
{
    switch (ss_develop_set(d, key, val)) {
    case 0:
        /* Naming a crop rectangle without also saying crop=1 is unambiguous
         * about intent, so honour it. This lives HERE and not in
         * ss_develop_set because the sidecar reader shares that function —
         * see the note at the top of develop.c. */
        if (!strncmp(key, "crop.", 5) && strcmp(key, "crop.angle"))
            d->crop.on = 1;
        return 0;
    case -1: return die("no such setting: %s  (try `synstudio keys`)", key);
    case -2: return die("%s: not a number: %s", key, val);
    case -3: return die("%s: out of range: %s  (try `synstudio keys`)", key, val);
    default: return die("%s: rejected", key);
    }
}

static int apply_sets(ss_develop *d, const opts *o)
{
    int i;
    for (i = 0; i < o->nsets; i++)
        if (apply_set(d, o->set_key[i], o->set_val[i]) != 0) return 1;
    return 0;
}

/* --------------------------------------------------------------- photos -- */

static int cmd_probe(const char *path)
{
    ss_probe p;
    if (ss_probe_file(path, &p) != 0)
        return die("cannot read %s", path);
    printf("path\t%s\n", path);
    printf("width\t%d\n", p.w);
    printf("height\t%d\n", p.h);
    printf("kind\t%s\n", p.is_video ? "video" : "image");
    printf("codec\t%s\n", p.codec);
    printf("format\t%s\n", p.fmt);
    printf("duration\t%.4f\n", p.duration);
    printf("fps\t%.6g\n", p.fps);
    return 0;
}

/* key, default, min, max, type, group, label — everything the window needs to
 * draw a control it has never heard of. */
static int cmd_keys(void)
{
    ss_develop d;
    ss_develop_info f;
    int i;

    ss_develop_reset(&d);
    for (i = 0; ss_develop_describe(i, &f) == 0; i++) {
        char buf[512];
        ss_develop_get(&d, f.key, buf, sizeof buf);
        printf("%s\t%s\t%.6g\t%.6g\t%s\t%s\t%s\t%.6g\t%.6g\n",
               f.key, buf, f.ui_lo, f.ui_hi,
               f.type == SS_T_CURVE ? "curve" : f.type == SS_T_INT ? "int" : "float",
               f.group, f.label, f.lo, f.hi);
    }
    return 0;
}

static int cmd_get(const char *path, const char *key)
{
    char side[4200], buf[512];
    ss_edit e;
    const char *k;
    int i;

    ss_sidecar_path(path, side, sizeof side);
    if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);

    if (key) {
        if (ss_develop_get(&e.dev, key, buf, sizeof buf) != 0)
            return die("no such setting: %s", key);
        puts(buf);
        return 0;
    }
    for (i = 0; (k = ss_develop_key(i)); i++) {
        ss_develop_get(&e.dev, k, buf, sizeof buf);
        printf("%s\t%s\n", k, buf);
    }
    printf("masks\t%d\n", e.nmasks);
    return 0;
}

static int cmd_set(const char *path, int argc, char **argv, int start)
{
    char side[4200];
    ss_edit e;
    int i, changed = 0;

    ss_sidecar_path(path, side, sizeof side);
    if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);

    for (i = start; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
        *eq = '\0';
        if (apply_set(&e.dev, argv[i], eq + 1) != 0) return 1;
        changed++;
    }
    if (!changed) return die("nothing to set");
    if (ss_edit_save(&e, side) != 0) return die("cannot write %s", side);
    return 0;
}

static int cmd_reset(const char *path)
{
    char side[4200];
    ss_sidecar_path(path, side, sizeof side);
    if (unlink(side) != 0 && errno != ENOENT)
        return die("cannot remove %s: %s", side, strerror(errno));
    return 0;
}

static int cmd_mask(const char *path, int argc, char **argv, int start)
{
    char side[4200];
    ss_edit e;
    const char *verb = start < argc ? argv[start] : "list";
    int i;

    ss_sidecar_path(path, side, sizeof side);
    if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);

    if (!strcmp(verb, "list")) {
        for (i = 0; i < e.nmasks; i++) {
            const ss_mask *m = &e.mask[i];
            printf("%d\t%s\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%d\n", i,
                   m->type == SS_MASK_LINEAR ? "linear" : "radial",
                   m->x0, m->y0, m->x1, m->y1, m->feather, m->invert);
        }
        return 0;
    }

    if (!strcmp(verb, "add")) {
        const char *kind = start + 1 < argc ? argv[start + 1] : "radial";
        if (strcmp(kind, "linear") && strcmp(kind, "radial"))
            return die("mask kind must be linear or radial, not %s", kind);
        if (e.nmasks >= SS_MAX_MASKS) return die("at the %d mask limit", SS_MAX_MASKS);
        ss_mask_reset(&e.mask[e.nmasks],
                      strcmp(kind, "radial") ? SS_MASK_LINEAR : SS_MASK_RADIAL);
        e.nmasks++;
        if (ss_edit_save(&e, side) != 0) return die("cannot write %s", side);
        printf("%d\n", e.nmasks - 1);
        return 0;
    }

    if (!strcmp(verb, "remove")) {
        int n = start + 1 < argc ? atoi(argv[start + 1]) : -1;
        if (n < 0 || n >= e.nmasks) return die("no mask %d", n);
        memmove(&e.mask[n], &e.mask[n+1], sizeof(ss_mask) * (e.nmasks - n - 1));
        e.nmasks--;
        if (ss_edit_save(&e, side) != 0) return die("cannot write %s", side);
        return 0;
    }

    /* mask FILE N KEY=VALUE... */
    {
        int n = atoi(verb);
        if (n < 0 || n >= e.nmasks) return die("no mask %d", n);
        for (i = start + 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
            *eq = '\0';
            if (!strcmp(argv[i], "geom")) {
                ss_mask *m = &e.mask[n];
                if (sscanf(eq + 1, "%f,%f,%f,%f,%f",
                           &m->x0, &m->y0, &m->x1, &m->y1, &m->feather) < 4)
                    return die("geom wants x0,y0,x1,y1[,feather]");
            } else if (!strcmp(argv[i], "invert")) {
                e.mask[n].invert = atoi(eq + 1) ? 1 : 0;
            } else if (apply_set(&e.mask[n].dev, argv[i], eq + 1) != 0) {
                return 1;
            }
        }
        if (ss_edit_save(&e, side) != 0) return die("cannot write %s", side);
        return 0;
    }
}

/* Load the file, apply the sidecar plus any --set overrides. Shared by
 * render and histogram so a histogram can never disagree with the picture. */
static int load_edited(const char *path, const opts *o, ss_image *im, ss_edit *e)
{
    char side[4200];

    ss_sidecar_path(path, side, sizeof side);
    if (ss_edit_load(e, side) != 0) return die("cannot read %s", side);
    if (apply_sets(&e->dev, o) != 0) return 1;

    if (ss_load(path, im, o->size) != 0)
        return die("cannot decode %s  (is ffmpeg installed?)", path);
    if (ss_edit_apply(im, e) != 0) { ss_image_free(im); return die("render failed"); }
    return 0;
}

static int cmd_render(const char *path, const opts *o)
{
    ss_image im;
    ss_edit e;
    int rc;

    if (!o->out) return die("render needs --out");
    rc = load_edited(path, o, &im, &e);
    if (rc) return rc;

    rc = ss_save(o->out, &im, o->quality, o->bits);
    printf("out\t%s\nwidth\t%d\nheight\t%d\n", o->out, im.w, im.h);
    ss_image_free(&im);
    return rc == 0 ? 0 : die("cannot write %s", o->out);
}

static int cmd_histogram(const char *path, const opts *o)
{
    ss_image im;
    ss_edit e;
    ss_histogram h;
    int i, rc;

    rc = load_edited(path, o, &im, &e);
    if (rc) return rc;

    ss_histogram_of(&im, &h);
    printf("pixels\t%ld\n", (long)im.w * im.h);
    printf("clipped_black\t%u\n", h.clip_lo);
    printf("clipped_white\t%u\n", h.clip_hi);
    for (i = 0; i < 256; i++)
        printf("bin\t%d\t%u\t%u\t%u\t%u\n", i, h.r[i], h.g[i], h.b[i], h.l[i]);
    ss_image_free(&im);
    return 0;
}

/* The pixel probe. Every test of the colour maths goes through this: it needs
 * no image file, no ffmpeg and no display, so the whole of colour.c is
 * exercised by a shell script in milliseconds. */
static int cmd_pixel(int argc, char **argv, int start, const opts *o)
{
    ss_develop d;
    float in[3], out[3];
    char **rest;
    int nrest;

    ss_develop_reset(&d);
    if (o->from) {
        char side[4200];
        ss_edit e;
        ss_sidecar_path(o->from, side, sizeof side);
        if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);
        d = e.dev;
    }
    if (apply_sets(&d, o) != 0) return 1;

    parse_opts(argc, argv, start, &(opts){0}, &rest, &nrest);
    if (nrest < 3) return die("pixel wants R G B, each 0..1 display-encoded");

    in[0] = ss_srgb_to_linear((float)atof(rest[0]));
    in[1] = ss_srgb_to_linear((float)atof(rest[1]));
    in[2] = ss_srgb_to_linear((float)atof(rest[2]));

    ss_pixel_pointwise(&d, in, out);

    printf("%.6f\t%.6f\t%.6f\n",
           ss_linear_to_srgb(out[0]),
           ss_linear_to_srgb(out[1]),
           ss_linear_to_srgb(out[2]));
    return 0;
}

static int cmd_lut(const opts *o)
{
    ss_develop d;
    FILE *fp;
    int rc;

    if (!o->out) return die("lut needs --out");
    ss_develop_reset(&d);
    if (o->from) {
        char side[4200];
        ss_edit e;
        ss_sidecar_path(o->from, side, sizeof side);
        if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);
        d = e.dev;
    }
    if (apply_sets(&d, o) != 0) return 1;

    fp = fopen(o->out, "w");
    if (!fp) return die("cannot write %s: %s", o->out, strerror(errno));
    rc = ss_lut_write(&d, o->lutsize, fp, "synstudio");
    if (fclose(fp) != 0) rc = -1;
    if (rc != 0) return die("cannot write %s", o->out);

    /* Say what could NOT be baked. Silence here would let somebody grade a
     * clip with clarity and sharpening, export a LUT, and wonder why the
     * result is flat. */
    if (d.clarity || d.texture || d.dehaze || d.sharpen ||
        d.nr_luma || d.nr_chroma || d.vignette || d.grain)
        fprintf(stderr, "synstudio: note: clarity/texture/dehaze/sharpen/noise/"
                        "vignette/grain are not colour and are NOT in the LUT\n");
    return 0;
}

/* ---------------------------------------------------------------- video -- */

static int tl_load(const char *proj, ss_timeline *t)
{
    FILE *fp;

    /* ss_timeline_read frees the tracks it is handed before reading, so the
     * caller's struct has to be a valid empty document and not whatever was
     * on the stack. */
    ss_timeline_reset(t, 1920, 1080, 25.0);
    fp = fopen(proj, "r");
    if (!fp) return -1;
    ss_timeline_read(t, fp);
    fclose(fp);
    return 0;
}

static int tl_save(const char *proj, const ss_timeline *t)
{
    char tmp[4200];
    FILE *fp;
    int rc;

    snprintf(tmp, sizeof tmp, "%s.tmp", proj);
    fp = fopen(tmp, "w");
    if (!fp) return -1;
    rc = ss_timeline_write(t, fp);
    if (fclose(fp) != 0) rc = -1;
    if (rc != 0) { unlink(tmp); return -1; }
    return rename(tmp, proj);
}

static int cmd_timeline(int argc, char **argv)
{
    const char *verb = argc > 2 ? argv[2] : NULL;
    const char *proj = argc > 3 ? argv[3] : NULL;
    ss_timeline t;
    opts o;
    char **rest;
    int nrest;

    if (!verb || !proj) { usage(); return 1; }
    opts_default(&o);

    if (!strcmp(verb, "new")) {
        int w = 1920, h = 1080;
        double fps = 25.0;
        int i;
        for (i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--size") && i + 1 < argc) {
                if (sscanf(argv[++i], "%dx%d", &w, &h) != 2)
                    return die("--size wants WxH, e.g. 1920x1080");
            } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
                fps = atof(argv[++i]);
            }
        }
        ss_timeline_reset(&t, w, h, fps);
        if (tl_save(proj, &t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (tl_load(proj, &t) != 0) return die("cannot read %s", proj);

    if (!strcmp(verb, "show")) {
        ss_timeline_write(&t, stdout);
        printf("# duration\t%.4f\n", ss_timeline_duration(&t));
        return 0;
    }

    if (!strcmp(verb, "track")) {
        const char *kind = argc > 4 ? argv[4] : "video";
        const char *name = argc > 5 ? argv[5] : NULL;
        int n = ss_timeline_add_track(&t, strcmp(kind, "audio") ? SS_TRACK_VIDEO
                                                                : SS_TRACK_AUDIO, name);
        if (n < 0) return die("no room for another track");
        if (tl_save(proj, &t) != 0) return die("cannot write %s", proj);
        printf("%d\n", n);
        return 0;
    }

    if (!strcmp(verb, "clip")) {
        ss_clip c;
        int track;
        ss_probe p;

        if (argc < 6) return die("clip wants PROJ TRACK FILE");
        track = atoi(argv[4]);
        memset(&c, 0, sizeof c);
        snprintf(c.path, sizeof c.path, "%s", argv[5]);
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
            return die("bad option");

        /* An out point nobody gave defaults to the whole source, which is
         * what "add this clip" means before anyone has trimmed it. */
        c.src_in  = o.in;
        c.src_out = o.outp;
        if (c.src_out < 0) {
            if (ss_probe_file(c.path, &p) == 0 && p.duration > 0)
                c.src_out = p.duration;
            else
                c.src_out = c.src_in + 5.0;     /* a still: five seconds */
        }
        if (c.src_out <= c.src_in) return die("out point is not after the in point");

        c.tl_in    = o.at;
        c.speed    = o.speed > 0 ? o.speed : 1.0;
        c.gain_db  = o.gain;
        c.opacity  = o.opacity;
        c.fade_in  = o.fade_in;
        c.fade_out = o.fade_out;

        if (ss_timeline_add_clip(&t, track, &c) < 0)
            return die("no track %d, or no room on it", track);
        if (tl_save(proj, &t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (!strcmp(verb, "grade")) {
        int tr, cl, i;
        if (argc < 7) return die("grade wants PROJ TRACK CLIP KEY=VALUE...");
        tr = atoi(argv[4]); cl = atoi(argv[5]);
        if (tr < 0 || tr >= t.ntracks) return die("no track %d", tr);
        if (cl < 0 || cl >= t.track[tr].nclips) return die("no clip %d", cl);
        if (!t.track[tr].clip[cl].has_grade) {
            ss_develop_reset(&t.track[tr].clip[cl].grade);
            t.track[tr].clip[cl].has_grade = 1;
        }
        for (i = 6; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
            *eq = '\0';
            if (apply_set(&t.track[tr].clip[cl].grade, argv[i], eq + 1) != 0) return 1;
        }
        if (tl_save(proj, &t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (!strcmp(verb, "export")) {
        char lutdir[] = "/tmp/synstudio-lut-XXXXXX";
        char **av;
        int ac, i, j, rc;

        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (!o.out) return die("export needs --out");
        if (!mkdtemp(lutdir)) return die("cannot make a scratch directory");

        /* One .cube per graded clip, written before the graph is built so the
         * paths it references all exist by the time ffmpeg opens them. */
        for (i = 0; i < t.ntracks; i++)
            for (j = 0; j < t.track[i].nclips; j++) {
                char lp[4200];
                FILE *fp;
                if (!t.track[i].clip[j].has_grade) continue;
                snprintf(lp, sizeof lp, "%s/grade_%d_%d.cube", lutdir, i, j);
                fp = fopen(lp, "w");
                if (!fp) { rc = 1; goto cleanup; }
                ss_lut_write(&t.track[i].clip[j].grade, 33, fp, "synstudio clip grade");
                fclose(fp);
            }

        ac = ss_timeline_ffmpeg(&t, o.out, lutdir, &av);
        if (ac < 0) { rc = die("cannot build the export graph"); goto cleanup; }

        if (o.print) {
            /* The graph, exactly as it would run. One argument per line so a
             * filter graph containing spaces and semicolons stays readable
             * and stays copy-pasteable. */
            for (i = 0; i < ac; i++) printf("%s\n", av[i]);
            rc = 0;
        } else {
            pid_t pid = fork();
            if (pid == 0) { execvp(av[0], av); _exit(127); }
            if (pid < 0) rc = die("cannot start ffmpeg");
            else {
                int st;
                while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
                rc = (WIFEXITED(st) && WEXITSTATUS(st) == 0)
                     ? 0 : die("ffmpeg failed");
            }
        }
        for (i = 0; i < ac; i++) free(av[i]);
        free(av);

cleanup:
        for (i = 0; i < t.ntracks; i++)
            for (j = 0; j < t.track[i].nclips; j++) {
                char lp[4200];
                snprintf(lp, sizeof lp, "%s/grade_%d_%d.cube", lutdir, i, j);
                unlink(lp);
            }
        rmdir(lutdir);
        return rc;
    }

    return die("unknown timeline verb: %s", verb);
}

/* --------------------------------------------------------------- browse -- */

/* The picker's data. One row per line, tab separated:
 *
 *     up|dir|image|video <TAB> NAME <TAB> ABSOLUTE-PATH
 *
 * The first line is the directory that was actually listed, as `.` — a window
 * that asked for `~/Pictures/../Pictures` needs to be told where it landed, or
 * its breadcrumb and its `..` disagree. */
static int cmd_browse(int argc, char **argv)
{
    static const char *names[] = { "up", "dir", "image", "video" };
    char abs[1024];
    ss_row *rows = NULL;
    int n, i;

    n = ss_browse(argc > 2 ? argv[2] : NULL, &rows, abs);
    if (n < 0) return die("cannot list %s: %s",
                          argc > 2 ? argv[2] : ".", strerror(errno));

    printf(".\t%s\t%s\n", strrchr(abs, '/') && strrchr(abs, '/')[1]
                            ? strrchr(abs, '/') + 1 : "/", abs);
    for (i = 0; i < n; i++)
        printf("%s\t%s\t%s\n", names[rows[i].type], rows[i].name, rows[i].path);

    free(rows);
    return 0;
}

/* ------------------------------------------------------------------ gui -- */

static int cmd_gui(int argc, char **argv)
{
    char qml[512];
    char *av[8];
    int n = 0;

    snprintf(qml, sizeof qml, "%s/synstudio.qml", SYNSTUDIO_DATADIR);

    /* The window's app_id. Without it the surface belongs to "org.quickshell",
     * wears quickshell's icon and does not match this program's .desktop, so a
     * taskbar cannot tell it apart from any other quickshell window.
     *
     * Overwrite, not the 0 flag: this variable is INHERITED. Launched from
     * synfiles, this process would otherwise start life claiming to be
     * synfiles, and the editor's window would land on the file browser's
     * launcher. */
    setenv("QS_APP_ID", "synstudio", 1);

    av[n++] = "quickshell";
    av[n++] = "-p";
    av[n++] = qml;
    av[n] = NULL;

    /* The file to open is handed over in the environment rather than on the
     * command line: quickshell owns argv and passes nothing through. */
    if (argc > 2) setenv("SYNSTUDIO_OPEN", argv[2], 1);

    execvp(av[0], av);
    return die("cannot start quickshell (is it installed?): %s", strerror(errno));
}

/* ----------------------------------------------------------------- main -- */

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : NULL;
    opts o;
    char **rest;
    int nrest;

    if (!cmd || !strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
        usage();
        return cmd ? 0 : 1;
    }
    if (!strcmp(cmd, "version") || !strcmp(cmd, "--version")) {
        puts(SYNSTUDIO_VERSION);
        return 0;
    }
    if (!strcmp(cmd, "keys"))     return cmd_keys();
    if (!strcmp(cmd, "browse"))   return cmd_browse(argc, argv);
    if (!strcmp(cmd, "gui"))      return cmd_gui(argc, argv);
    if (!strcmp(cmd, "timeline")) return cmd_timeline(argc, argv);

    opts_default(&o);

    if (!strcmp(cmd, "pixel")) {
        if (parse_opts(argc, argv, 2, &o, &rest, &nrest) != 0) return die("bad option");
        return cmd_pixel(argc, argv, 2, &o);
    }
    if (!strcmp(cmd, "lut")) {
        if (parse_opts(argc, argv, 2, &o, &rest, &nrest) != 0) return die("bad option");
        return cmd_lut(&o);
    }

    if (argc < 3) return die("%s needs a file", cmd);

    if (!strcmp(cmd, "probe"))  return cmd_probe(argv[2]);
    if (!strcmp(cmd, "get"))    return cmd_get(argv[2], argc > 3 ? argv[3] : NULL);
    if (!strcmp(cmd, "set"))    return cmd_set(argv[2], argc, argv, 3);
    if (!strcmp(cmd, "reset"))  return cmd_reset(argv[2]);
    if (!strcmp(cmd, "mask"))   return cmd_mask(argv[2], argc, argv, 3);

    if (parse_opts(argc, argv, 3, &o, &rest, &nrest) != 0) return die("bad option");
    if (!strcmp(cmd, "render"))    return cmd_render(argv[2], &o);
    if (!strcmp(cmd, "histogram")) return cmd_histogram(argv[2], &o);

    return die("unknown command: %s  (try `synstudio help`)", cmd);
}
