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
"VIDEO  (a project file is a text document; nothing is rendered until export)\n"
"  timeline new PROJ [--size WxH] [--fps F]\n"
"  timeline show PROJ            the document, as written\n"
"  timeline keys                 every clip property, with its range\n"
"\n"
" tracks and clips\n"
"  timeline track PROJ video|audio [NAME]        add a track\n"
"  timeline track PROJ N [--mute 0|1] [--hide 0|1] [--name NAME]\n"
"  timeline clip PROJ TRACK FILE [--at T] [--in A] [--out-at B] [--dur S]\n"
"       [--gain dB] [--opacity F] [--fade-in S] [--fade-out S] [--speed F]\n"
"  timeline title PROJ TRACK TEXT [--at T] [--dur S] [--colour R,G,B]\n"
"  timeline solid PROJ TRACK [--at T] [--dur S] [--colour R,G,B]\n"
"\n"
" editing (rearranges intent; never touches a frame)\n"
"  timeline move  PROJ T C --to SECONDS\n"
"  timeline trim  PROJ T C [--head S] [--tail S]   + shortens the head,\n"
"                                                    + lengthens the tail\n"
"  timeline split PROJ T [C] --at SECONDS          the razor\n"
"  timeline delete PROJ T C [--ripple]             lift, or close the gap\n"
"  timeline at    PROJ T --at SECONDS              which clip is under there\n"
"\n"
" what a clip looks like\n"
"  timeline get   PROJ T C [KEY]          everything known about one clip\n"
"  timeline set   PROJ T C KEY=VALUE...   opacity, speed, fades, motion,\n"
"                                         transition, title (`timeline keys`)\n"
"  timeline grade PROJ T C KEY=VALUE...   the develop stack, as a LUT\n"
"\n"
" out\n"
"  timeline frame PROJ --at T --out F.png [--size N]   one composited frame\n"
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
    double dur, to, head, tail;
    int    has_dur, has_to, has_head, has_tail, ripple;
    const char *colour;
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
        else if (!strcmp(a, "--dur"))     { const char *v = NEXT(); if (!v) return -1; o->dur = atof(v); o->has_dur = 1; }
        else if (!strcmp(a, "--to"))      { const char *v = NEXT(); if (!v) return -1; o->to = atof(v); o->has_to = 1; }
        else if (!strcmp(a, "--head"))    { const char *v = NEXT(); if (!v) return -1; o->head = atof(v); o->has_head = 1; }
        else if (!strcmp(a, "--tail"))    { const char *v = NEXT(); if (!v) return -1; o->tail = atof(v); o->has_tail = 1; }
        else if (!strcmp(a, "--colour") ||
                 !strcmp(a, "--color"))   { const char *v = NEXT(); if (!v) return -1; o->colour = v; }
        else if (!strcmp(a, "--ripple"))  { o->ripple = 1; }
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

/* Every clip property in one listing, the same shape `keys` uses for the
 * develop stack: KEY, current, ui-lo, ui-hi, type, group, label, lo, hi and —
 * for an enum — the choices. The inspector in the window is built from this,
 * so a property added to the table in timeline.c appears there without the
 * QML being touched. */
static int cmd_timeline_keys(void)
{
    ss_clip c;
    ss_clip_info f;
    int i;

    ss_clip_reset(&c);
    for (i = 0; ss_clip_describe(i, &f); i++) {
        char buf[512];
        ss_clip_get(&c, f.key, buf, sizeof buf);
        printf("%s\t%s\t%.6g\t%.6g\t%s\t%s\t%s\t%s\n",
               f.key, buf, f.lo, f.hi,
               f.type == SS_CT_ENUM ? "enum" : f.type == SS_CT_TEXT ? "text"
               : f.type == SS_CT_INT ? "int" : "float",
               f.group, f.label, f.choices ? f.choices : "");
    }
    return 0;
}

/* TRACK and CLIP off the command line, checked once so seven verbs do not
 * each write the same two range tests differently. */
static int tl_pick(const ss_timeline *t, const char *ts, const char *cs,
                   int *tr, int *cl)
{
    if (!ts) return die("which track?");
    *tr = atoi(ts);
    if (*tr < 0 || *tr >= t->ntracks) return die("no track %d", *tr);
    if (!cl) return 0;
    if (!cs) return die("which clip?");
    *cl = atoi(cs);
    if (*cl < 0 || *cl >= t->track[*tr].nclips)
        return die("no clip %d on track %d", *cl, *tr);
    return 0;
}

/* --colour R,G,B or R,G,B,A, each 0..1. */
static int parse_colour(const char *s, float *r, float *g, float *b, float *a)
{
    double v[4] = { 0, 0, 0, 1 };
    int n = sscanf(s, "%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3]);
    if (n < 3) return -1;
    *r = (float)v[0]; *g = (float)v[1]; *b = (float)v[2];
    if (n > 3) *a = (float)v[3];
    return 0;
}

/* Run a graph, or print it. One argument per line so a filter graph
 * containing spaces and semicolons stays readable and copy-pasteable. */
static int tl_run(char **av, int ac, int print)
{
    int i, rc;

    if (print) {
        for (i = 0; i < ac; i++) printf("%s\n", av[i]);
        rc = 0;
    } else {
        pid_t pid = fork();
        if (pid == 0) { execvp(av[0], av); _exit(127); }
        if (pid < 0) rc = die("cannot start ffmpeg");
        else {
            int st;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
            rc = (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : die("ffmpeg failed");
        }
    }
    for (i = 0; i < ac; i++) free(av[i]);
    free(av);
    return rc;
}

/* The verbs. Takes the document by pointer so ONE caller owns it and can free
 * it — there are ninety-odd returns in here and no cleanup label was ever
 * going to survive the next verb added. */
static int timeline_verb(int argc, char **argv, ss_timeline *t)
{
    const char *verb = argc > 2 ? argv[2] : NULL;
    const char *proj = argc > 3 ? argv[3] : NULL;
    opts o;
    char **rest;
    int nrest, tr, cl;

    if (verb && !strcmp(verb, "keys")) return cmd_timeline_keys();
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
        ss_timeline_reset(t, w, h, fps);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (tl_load(proj, t) != 0) return die("cannot read %s", proj);

    if (!strcmp(verb, "show")) {
        ss_timeline_write(t, stdout);
        printf("# duration\t%.4f\n", ss_timeline_duration(t));
        return 0;
    }

    /* `track PROJ video|audio [NAME]` adds one; `track PROJ N ...` edits the
     * one that is there. The kind word and an index cannot be confused for
     * each other, and keeping the add form exactly as it was means no script
     * that already drives this has to change. */
    if (!strcmp(verb, "track")) {
        const char *what = argc > 4 ? argv[4] : "video";

        if (!strcmp(what, "video") || !strcmp(what, "audio")) {
            const char *name = argc > 5 ? argv[5] : NULL;
            int n = ss_timeline_add_track(t, strcmp(what, "audio") ? SS_TRACK_VIDEO
                                                                    : SS_TRACK_AUDIO, name);
            if (n < 0) return die("no room for another track");
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("%d\n", n);
            return 0;
        }
        if (tl_pick(t, what, NULL, &tr, NULL) != 0) return 1;
        {
            int i;
            for (i = 5; i < argc; i++) {
                if (!strcmp(argv[i], "--mute") && i + 1 < argc)
                    t->track[tr].muted = atoi(argv[++i]);
                else if (!strcmp(argv[i], "--hide") && i + 1 < argc)
                    t->track[tr].hidden = atoi(argv[++i]);
                else if (!strcmp(argv[i], "--name") && i + 1 < argc)
                    snprintf(t->track[tr].name, sizeof t->track[tr].name,
                             "%s", argv[++i]);
                else return die("track: unknown option %s", argv[i]);
            }
        }
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (!strcmp(verb, "clip")) {
        ss_clip c;
        ss_probe p;

        if (argc < 6) return die("clip wants PROJ TRACK FILE");
        if (tl_pick(t, argv[4], NULL, &tr, NULL) != 0) return 1;
        ss_clip_reset(&c);
        snprintf(c.path, sizeof c.path, "%s", argv[5]);
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
            return die("bad option");

        /* An out point nobody gave defaults to the whole source, which is
         * what "add this clip" means before anyone has trimmed it. */
        c.src_in  = o.in;
        c.src_out = o.outp;
        if (ss_probe_file(c.path, &p) == 0) {
            /* Whether this is a photograph is decided ONCE, here, and stored.
             * The export has to know — a still needs -loop or it contributes a
             * single frame to a graph expecting seconds of them, and finishes
             * early with no error at all — and the graph builder has no
             * business probing files. */
            c.still = !p.is_video || p.duration <= 0;
            if (c.src_out < 0)
                c.src_out = (!c.still && p.duration > 0)
                            ? p.duration : c.src_in + (o.has_dur ? o.dur : 5.0);
        } else if (c.src_out < 0) {
            c.src_out = c.src_in + (o.has_dur ? o.dur : 5.0);
        }
        if (o.has_dur) c.src_out = c.src_in + o.dur;
        if (c.src_out <= c.src_in) return die("out point is not after the in point");

        c.tl_in    = o.at;
        c.speed    = o.speed > 0 ? o.speed : 1.0;
        c.gain_db  = o.gain;
        c.opacity  = o.opacity;
        c.fade_in  = o.fade_in;
        c.fade_out = o.fade_out;

        cl = ss_timeline_add_clip(t, tr, &c);
        if (cl < 0) return die("no track %d, or no room on it", tr);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("%d\n", cl);
        return 0;
    }

    /* A caption and a background are clips, not a separate kind of object.
     * They sit on a video track, take the same fades, transform, transition
     * and grade, and the only thing that knows they are generated is the one
     * place that decides what ffmpeg reads. */
    if (!strcmp(verb, "title") || !strcmp(verb, "solid")) {
        ss_clip c;
        int is_title = !strcmp(verb, "title");

        if (argc < 5) return die("%s wants PROJ TRACK%s", verb,
                                 is_title ? " TEXT" : "");
        if (tl_pick(t, argv[4], NULL, &tr, NULL) != 0) return 1;
        if (t->track[tr].type != SS_TRACK_VIDEO)
            return die("track %d is an audio track", tr);

        ss_clip_reset(&c);
        c.kind = is_title ? SS_CLIP_TITLE : SS_CLIP_SOLID;
        if (is_title) {
            if (argc < 6) return die("title wants the words to put on screen");
            snprintf(c.text, sizeof c.text, "%s", argv[5]);
            if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
                return die("bad option");
        } else {
            if (parse_opts(argc, argv, 5, &o, &rest, &nrest) != 0)
                return die("bad option");
        }
        if (o.colour) {
            float a = is_title ? 1.0f : 1.0f;
            if (is_title) {
                if (parse_colour(o.colour, &c.text_r, &c.text_g, &c.text_b, &a) != 0)
                    return die("--colour wants R,G,B each 0..1");
            } else {
                if (parse_colour(o.colour, &c.col_r, &c.col_g, &c.col_b, &a) != 0)
                    return die("--colour wants R,G,B each 0..1");
                c.col_a = 1.0f;
            }
        } else if (!is_title) {
            c.col_a = 1.0f;
        }
        c.tl_in   = o.at;
        c.src_in  = 0;
        c.src_out = o.has_dur ? o.dur : 4.0;
        if (c.src_out <= 0) return die("--dur has to be more than nothing");
        c.fade_in  = o.fade_in;
        c.fade_out = o.fade_out;
        c.opacity  = o.opacity;

        cl = ss_timeline_add_clip(t, tr, &c);
        if (cl < 0) return die("cannot add to track %d", tr);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("%d\n", cl);
        return 0;
    }

    if (!strcmp(verb, "set")) {
        int i;
        if (argc < 7) return die("set wants PROJ TRACK CLIP KEY=VALUE...");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        for (i = 6; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
            *eq = '\0';
            switch (ss_clip_set(&t->track[tr].clip[cl], argv[i], eq + 1)) {
            case 0:  break;
            case -2: return die("%s: out of range: %s  (try `synstudio timeline keys`)",
                                argv[i], eq + 1);
            default: return die("no such clip property: %s  "
                                "(try `synstudio timeline keys`)", argv[i]);
            }
        }
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (!strcmp(verb, "get")) {
        char buf[512];
        ss_clip_info f;
        int i;
        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                        argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        if (argc > 6) {
            if (ss_clip_get(&t->track[tr].clip[cl], argv[6], buf, sizeof buf) != 0)
                return die("no such clip property: %s", argv[6]);
            printf("%s\n", buf);
            return 0;
        }
        /* The whole clip, including what the property table does not carry:
         * a window drawing a timeline needs the position and the length far
         * more than it needs the opacity. */
        {
            const ss_clip *c = &t->track[tr].clip[cl];
            printf("kind\t%s\n", c->kind == SS_CLIP_TITLE ? "title"
                                : c->kind == SS_CLIP_SOLID ? "solid" : "media");
            printf("path\t%s\n", c->path);
            printf("still\t%d\n", c->still);
            printf("tl_in\t%.6f\n", c->tl_in);
            printf("src_in\t%.6f\n", c->src_in);
            printf("src_out\t%.6f\n", c->src_out);
            printf("length\t%.6f\n", ss_clip_length(c));
            printf("graded\t%d\n", c->has_grade);
            for (i = 0; ss_clip_describe(i, &f); i++) {
                ss_clip_get(c, f.key, buf, sizeof buf);
                printf("%s\t%s\n", f.key, buf);
            }
        }
        return 0;
    }

    if (!strcmp(verb, "move")) {
        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                        argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0) return die("bad option");
        if (!o.has_to) return die("move wants --to SECONDS");
        if (ss_timeline_move(t, tr, cl, o.to) != 0) return die("cannot move that");
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (!strcmp(verb, "trim")) {
        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                        argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0) return die("bad option");
        if (!o.has_head && !o.has_tail)
            return die("trim wants --head SECONDS or --tail SECONDS "
                       "(positive shortens the head, lengthens the tail)");
        if (o.has_head && ss_timeline_trim(t, tr, cl, -1, o.head) != 0)
            return die("that head trim would leave nothing");
        if (o.has_tail && ss_timeline_trim(t, tr, cl, 1, o.tail) != 0)
            return die("that tail trim would leave nothing");
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    if (!strcmp(verb, "split")) {
        int n;
        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0) return die("bad option");
        if (nrest < 1) return die("split wants PROJ TRACK --at SECONDS");
        if (tl_pick(t, rest[0], NULL, &tr, NULL) != 0) return 1;
        /* The clip is found from the razor position rather than named. That
         * is what a razor IS, and it means the window can pass the playhead
         * straight through without first working out what it is over. */
        cl = nrest > 1 ? atoi(rest[1]) : ss_timeline_at(t, tr, o.at);
        if (cl < 0) return die("nothing on track %d at %.3f", tr, o.at);
        n = ss_timeline_split(t, tr, cl, o.at);
        if (n < 0) return die("%.3f is not inside clip %d", o.at, cl);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("%d\n", n);
        return 0;
    }

    if (!strcmp(verb, "delete")) {
        double from, len;
        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                        argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0) return die("bad option");
        from = t->track[tr].clip[cl].tl_in;
        len  = ss_clip_length(&t->track[tr].clip[cl]);
        if (ss_timeline_remove(t, tr, cl) != 0) return die("cannot remove that");
        /* Without --ripple this is a lift: the gap stays. With it, the track
         * closes up. They are different edits and neither is the safe
         * default for the other, so the flag says which one was meant. */
        if (o.ripple) ss_timeline_ripple(t, tr, from, len);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    /* Which clip is under a time — the window's hit test, answered by the
     * engine so a click and a render can never disagree about it. */
    if (!strcmp(verb, "at")) {
        if (tl_pick(t, argc > 4 ? argv[4] : NULL, NULL, &tr, NULL) != 0) return 1;
        if (parse_opts(argc, argv, 5, &o, &rest, &nrest) != 0) return die("bad option");
        printf("%d\n", ss_timeline_at(t, tr, o.at));
        return 0;
    }

    if (!strcmp(verb, "grade")) {
        int i;
        if (argc < 7) return die("grade wants PROJ TRACK CLIP KEY=VALUE...");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        if (!t->track[tr].clip[cl].has_grade) {
            ss_develop_reset(&t->track[tr].clip[cl].grade);
            t->track[tr].clip[cl].has_grade = 1;
        }
        for (i = 6; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
            *eq = '\0';
            if (apply_set(&t->track[tr].clip[cl].grade, argv[i], eq + 1) != 0) return 1;
        }
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    /* One composited frame — the program monitor's whole data source. */
    if (!strcmp(verb, "frame")) {
        char dir[] = "/tmp/synstudio-tl-XXXXXX";
        char **av;
        int ac, rc;

        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0) return die("bad option");
        if (!o.out) return die("frame needs --out");
        if (!mkdtemp(dir)) return die("cannot make a scratch directory");
        if (ss_timeline_bake(t, dir) < 0) {
            rmdir(dir);
            return die("cannot write the grade LUTs");
        }
        ac = ss_timeline_frame(t, o.at, o.out, dir, o.size, &av);
        if (ac < 0) { ss_timeline_unbake(t, dir); rmdir(dir);
                      return die("cannot build the preview graph"); }
        rc = tl_run(av, ac, o.print);
        ss_timeline_unbake(t, dir);
        rmdir(dir);
        return rc;
    }

    if (!strcmp(verb, "export")) {
        char dir[] = "/tmp/synstudio-lut-XXXXXX";
        char **av;
        int ac, rc;

        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (!o.out) return die("export needs --out");
        if (!mkdtemp(dir)) return die("cannot make a scratch directory");

        /* The .cube per graded clip and the text file per title, written
         * before the graph is built so every path it names exists by the
         * time ffmpeg opens it. */
        if (ss_timeline_bake(t, dir) < 0) {
            rmdir(dir);
            return die("cannot write the grade LUTs");
        }

        ac = ss_timeline_ffmpeg(t, o.out, dir, &av);
        if (ac < 0) { ss_timeline_unbake(t, dir); rmdir(dir);
                      return die("cannot build the export graph"); }
        rc = tl_run(av, ac, o.print);
        ss_timeline_unbake(t, dir);
        rmdir(dir);
        return rc;
    }

    return die("unknown timeline verb: %s", verb);
}

/* A timeline's clips are a grown array, so the document owns heap. Every verb
 * above returns straight out, which left that array leaked at exit — harmless
 * in a process about to end, and fatal to any attempt to run this suite under
 * a leak checker, which is the reason it is worth fixing rather than
 * annotating away. */
static int cmd_timeline(int argc, char **argv)
{
    ss_timeline t;
    int rc;

    ss_timeline_reset(&t, 1920, 1080, 25.0);
    rc = timeline_verb(argc, argv, &t);
    ss_timeline_free(&t);
    return rc;
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
     * command line: quickshell owns argv and passes nothing through.
     *
     * A timeline and a photograph both arrive as one path, and the window
     * opens on a different page for each. Which it is comes from the file's
     * first line, not from its extension: a project someone saved as .txt is
     * still a project, and a .syntl that is actually a photograph would open
     * an editor onto nothing. */
    if (argc > 2) {
        FILE *fp = fopen(argv[2], "r");
        char head[32] = "";
        int is_proj = 0;
        if (fp) {
            if (fgets(head, sizeof head, fp))
                is_proj = !strncmp(head, "# synstudio timeline", 20);
            fclose(fp);
        }
        setenv(is_proj ? "SYNSTUDIO_PROJECT" : "SYNSTUDIO_OPEN", argv[2], 1);
    }

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
