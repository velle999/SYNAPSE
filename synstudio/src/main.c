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
#include <time.h>
#include <unistd.h>
#include <dirent.h>
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
"  set FILE KEY=VALUE... [--no-history]   change the sidecar\n"
"  reset FILE                    back to the picture as it arrived\n"
"  undo FILE / redo FILE         step the sidecar back and forward\n"
"  history FILE                  how far each way\n"
"  mask FILE add linear|radial   add a local adjustment\n"
"  mask FILE list                list them\n"
"  mask FILE N KEY=VALUE...      change one (geom=x0,y0,x1,y1,feather)\n"
"  render FILE --out OUT         apply the sidecar and write a new file\n"
"  thumb FILE keys|sizes|get|set K=V...|reset\n"
"  thumb FILE render --out F.jpg [--size N] [--quality Q]\n"
"                                a THUMBNAIL of it: a canvas, the picture\n"
"                                framed into it, and words big enough to\n"
"                                read at the size one is actually seen\n"
"  source FILE --at S --out F.png [--size N]   one frame of any file, for\n"
"                                a source monitor — through its sidecar\n"
"  histogram FILE                256 bins per channel, tab separated\n"
"  logcurve [none|slog3|vlog] [--value CODE]   what a camera's own curve\n"
"                                means: a code value in, scene light out\n"
"  match FILE --ref REFERENCE    make this shot look like that one:\n"
"                                brightness, contrast and white balance,\n"
"                                FITTED through the engine rather than solved\n"
"  scope FILE --out F.png [--kind waveform|parade|vector] [--size N]\n"
"                                the picture, measured — computed here and\n"
"                                not by a filter, so it cannot disagree\n"
"  peaks FILE [--in A] [--out-at B] [--count N]\n"
"                                the audio envelope: peak and RMS per bucket\n"
"                                (exit 100 = the file has no audio)\n"
"  pixel R G B [--set K=V]...    one colour through the stack (0..1 encoded)\n"
"\n"
"LOOKS\n"
"  lut --out F.cube              bake the colour half as a 3D LUT\n"
"       [--from FILE] [--size 33] [--set K=V]...\n"
"  rnns                          the arnndn noise models this machine has\n"
"       (none ship: a trained model is somebody else's licensed work)\n"
"\n"
"VIDEO  (a project file is a text document; nothing is rendered until export)\n"
"  timeline new PROJ [--size WxH] [--fps F] [--unique|--no-clobber]\n"
"  timeline show PROJ            the document, as written\n"
"  timeline undo PROJ            step back; redo steps forward again\n"
"  timeline redo PROJ\n"
"  timeline history PROJ         how many steps there are either way\n"
"  timeline version PROJ save NAME|list|restore NAME\n"
"       a document you decided to KEEP. Undo is the auto-save half and it\n"
"       is a ring; a version is a name and nothing expires it\n"
"  timeline mark PROJ --at T [--text S] [--colour 0-5]   a note at an instant\n"
"  timeline unmark PROJ N\n"
"  timeline keys                 every clip property, with its range\n"
"\n"
" tracks and clips\n"
"  timeline track PROJ video|audio [NAME]        add a track\n"
"  timeline track PROJ N [--mute 0|1] [--hide 0|1] [--name NAME]\n"
"       [--gain dB] [--pan -1..1] [--solo 0|1]      the track's fader\n"
"       [--duck N|off] [--duck-amount 0-100]   this track gets out of the\n"
"       way of track N — a music bed under dialogue\n"
"  timeline master PROJ [--gain dB]              one fader after the mix\n"
"  timeline auto PROJ T add --at S --value dB [--ease E]   ride the fader\n"
"  timeline auto PROJ T list|at --at S|remove N|clear\n"
"       ⚠ these keys are in TIMELINE seconds, not clip seconds\n"
"  timeline loudness PROJ [--value LUFS] [--off]  what a DELIVERY is\n"
"       normalised to: -23 is broadcast, -14 is what streaming does anyway\n"
"  timeline normalise PROJ T C [--target LUFS]   measure, then set the gain\n"
"  timeline clip PROJ TRACK FILE [--at T] [--in A] [--out-at B] [--dur S]\n"
"  timeline insert PROJ TRACK FILE --at T [--in A] [--out-at B]\n"
"                  make room for it: every track moves later\n"
"  timeline overwrite PROJ TRACK FILE --at T [--in A] [--out-at B]\n"
"                  cut a hole its own length on that track\n"
"                  [--flat]                      ignore its own develop\n"
"       [--gain dB] [--opacity F] [--fade-in S] [--fade-out S] [--speed F]\n"
"  timeline title PROJ TRACK TEXT [--at T] [--dur S] [--colour R,G,B]\n"
"       [--style S]      \\n in TEXT is a line break; `timeline styles`\n"
"                        lists the styles\n"
"  timeline solid PROJ TRACK [--at T] [--dur S] [--colour R,G,B]\n"
"  timeline styles               plain, lower third, subtitle, heading, roll\n"
"  timeline style PROJ TRACK CLIP NAME    restyle a title already there\n"
"  timeline stabilise PROJ T C [--dur SMOOTH] [--size ZOOM%] [--value 1-10]\n"
"       watch the shot and write the analysis beside the project; --off\n"
"       turns it off and KEEPS the measurement\n"
"  timeline subs PROJ TRACK import FILE     a .srt in, one title per cue\n"
"  timeline subs PROJ TRACK export FILE     the titles on a track back out\n"
"\n"
" editing (rearranges intent; never touches a frame)\n"
"  timeline move  PROJ T C --to SECONDS\n"
"  timeline link  PROJ T C T C [T C ...]   a shot and its sound move,\n"
"                                          trim and delete together\n"
"  timeline unlink PROJ T C                that one leaves the group\n"
"  timeline trim  PROJ T C [--head S] [--tail S]   + shortens the head,\n"
"                                                    + lengthens the tail\n"
"  timeline split PROJ T [C] --at SECONDS          the razor\n"
"  timeline delete PROJ T C [--ripple]             lift, or close the gap\n"
"  timeline saveas PROJ --out PATH [--force]       the cut, under a name\n"
"  timeline copy   PROJ T C                        onto the clipboard\n"
"  timeline clipboard              what is on it, without pasting to find out\n"
"  timeline paste  PROJ T [--at S]                 as a new clip\n"
"  timeline paste  PROJ T C --grade                its GRADE onto that clip\n"
"  timeline paste  PROJ T --grade --all            onto every clip there\n"
"  timeline duplicate PROJ T C [--at S]            straight after itself\n"
"  timeline at    PROJ T --at SECONDS              which clip is under there\n"
"\n"
" what a clip looks like\n"
"  timeline get   PROJ T C [KEY]          everything known about one clip\n"
"  timeline set   PROJ T C KEY=VALUE...   opacity, speed, fades, motion,\n"
"                                         transition, title (`timeline keys`)\n"
"       retime: `reverse=1` plays it backwards, `freeze=S` holds the frame\n"
"       at S, `retime=blend|flow` invents the frames a slowdown needs.\n"
"       KEYS on `speed` are a RAMP, and theirs are SOURCE seconds\n"
"  timeline grade PROJ T C KEY=VALUE...   the develop stack, as a LUT\n"
"  timeline match PROJ T C REFT REFC      make this shot look like that one\n"
"  timeline key PROJ T C add --at S [KEY=VALUE...]   pin the grade here\n"
"  timeline key PROJ T C list|remove N\n"
"  timeline key PROJ T C set N KEY=VALUE...\n"
"       two or more keys and the grade MOVES between them\n"
"  timeline anim PROJ T C add PROP --at S [--value V] [--ease E]\n"
"  timeline anim PROJ T C list|clear [PROP]   remove PROP N   at PROP --at S\n"
"  timeline anim PROJ T C move PROP N [--at S] [--value V] [--ease E]\n"
"  timeline anim PROJ T C curve PROP [--count N]   the curve, sampled\n"
"       a key on ONE property — opacity, gain, a scale, a position, an\n"
"       angle. `timeline keys` marks which ones take them; ease is\n"
"       linear, in, out, inout or hold\n"
"  timeline transition PROJ T [C] [--at S] [--kind K] [--dur D]\n"
"       a transition on the cut under the playhead, overlap and all —\n"
"       out of the outgoing clip's handles if it has them, by rippling\n"
"       what follows if it does not\n"
"  timeline transitions          every transition, with its name\n"
"  timeline fx PROJ T C add NAME [KEY=VALUE...]      an effect on a clip\n"
"  timeline fx PROJ T C list|remove N|move N TO|set N KEY=VALUE...\n"
"\n"
" out\n"
"  timeline frame PROJ --at T --out F.png [--size N]   one composited frame\n"
"  timeline scope PROJ --at T --out F.png [--kind waveform|parade|vector]\n"
"       the same frame, MEASURED — composited first, so it describes the\n"
"       picture that will be delivered\n"
"  timeline range PROJ [--at A] [--to B] [--off]   what a render covers\n"
"  timeline presets              delivery sizes, by where they are going\n"
"  timeline queue PROJ add --out F ...   put a render on the queue\n"
"  timeline queue PROJ list|run|clear    run them one at a time\n"
"  timeline export PROJ --out OUT [--format F] [--print] [--preview]\n"
"       [--preset P]   deliver at that size and frame rate\n"
"       [--burn timecode|name|both]   over the picture, for a review copy\n"
"       [--watermark F.png]           a logo over the delivered frame\n"
"       --format png|exr with `--out dir/f_%04d.png` writes a SEQUENCE\n"
"       [--subs FILE]  ship a .srt as a STREAM a player can switch off,\n"
"                      instead of the burnt-in kind `subs import` makes\n"
"  timeline formats              what an export can come out as\n"
"       --preview: small, fast, rough, and playable while still encoding —\n"
"                  the same graph, so the cut you watch is the cut you ship\n"
"\n"
"COMMON\n"
"  --size N        longest edge for a render or a preview (0 = full)\n"
"  --quality 1-100 jpeg quality        --bits 8|16   output depth\n"
"  --set K=V       an override applied on top of the sidecar, not saved\n"
"\n"
"  fonts [PATTERN]              the families a title can be lettered in\n"
"  fonts have FAMILY            whether this machine has that one\n"
"  devices         what can capture, for a voiceover\n"
"  record --out F [--device D] [--limit S] [--channels 1|2]\n"
"                  a take, with a live meter; stops on a signal\n"
"  loudness FILE [--in A] [--length S]\n"
"                  integrated LUFS, true peak and range, from ebur128\n"
"  fx list         every effect installed, with its group\n"
"  fx params      every parameter of every effect, with its range\n"
"  fx show NAME    an effect\'s parameters, with their ranges\n"
"  fx check NAME|FILE.synfx\n"
"                  render one frame through it — what an effect has to\n"
"                  survive before it is worth putting on a clip\n"
"  formats         what a developed photograph can be written as\n"
"  browse [DIR]    what is in a folder that this engine can open\n"
"  kind FILE       image|video|audio|project|none — asked of ffmpeg, not of\n"
"                  the extension (exit 1 = nothing here can be opened)\n"
"  gui [FILE]      the window\n");
}

/* ------------------------------------------------------------ arg soup -- */

typedef struct {
    const char *out;
    const char *from;
    const char *format;
    int    size, quality, bits, lutsize, print;
    double at, in, outp, speed;
    double fade_in, fade_out;
    double dur, to, head, tail;
    int    has_dur, has_to, has_head, has_tail, ripple, preview, count;
    /* ⚠ has_at, because 0 is a REAL instant. Every other verb reading --at
     * wants a position and the start of the timeline is a position; the two
     * that asked `o.at > 0` to mean "was it given" therefore could not be
     * told to paste at the head of the cut, and silently pasted the clip back
     * where it was copied from instead. */
    int    has_at;
    const char *colour;
    const char *ease;
    int    force;               /* --force: write over a file already there */
    int    flat;                /* --flat: ignore the photograph's own develop */
    int    off;                 /* --off: turn a thing off, keep its work */
    int    grade;               /* --grade: the develop stack and nothing else */
    int    all;                 /* --all: every clip on the track */
    const char *burn;           /* what to write over the delivered picture */
    const char *preset;         /* a delivery size and frame rate, by name */
    const char *mark;           /* a watermark PNG, over the delivered frame */
    /* ⚠ NOT --to: that is already a timeline instant, a double, and giving
     * one flag two types is how a typo becomes a silent zero. */
    const char *ref;            /* the shot to match, a file or T,C */
    const char *duck;           /* the track whose sound pushes this one down */
    double duck_amt;
    int    has_duck_amt;
    const char *style;          /* a title style, applied on creation */
    const char *subs;           /* a .srt shipped as a stream, not burnt in */
    double value;
    int    has_value;
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

/* Does a path exist?
 *
 * Asked before anything is written under a name somebody TYPED. Every verb in
 * this file writes its document the instant it runs, so nothing is ever
 * unsaved — but that same habit is what makes `new` on a name already in use,
 * or a Save as onto an existing project, a thing you find out about
 * afterwards. The one answer an editor must never give to "save this as
 * Holiday" is to throw away the Holiday that was already there. */
static int path_exists(const char *p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0;
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
        else if (!strcmp(a, "--at"))      { const char *v = NEXT(); if (!v) return -1; o->at = atof(v); o->has_at = 1; }
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
        else if (!strcmp(a, "--value"))   { const char *v = NEXT(); if (!v) return -1; o->value = atof(v); o->has_value = 1; }
        else if (!strcmp(a, "--ease"))    { const char *v = NEXT(); if (!v) return -1; o->ease = v; }
        else if (!strcmp(a, "--style"))   { const char *v = NEXT(); if (!v) return -1; o->style = v; }
        else if (!strcmp(a, "--subs"))    { const char *v = NEXT(); if (!v) return -1; o->subs = v; }
        else if (!strcmp(a, "--ripple"))  { o->ripple = 1; }
        else if (!strcmp(a, "--force"))   { o->force = 1; }
        /* --flat: take the file as it is, ignoring the develop beside it. */
        else if (!strcmp(a, "--flat"))    { o->flat = 1; }
        else if (!strcmp(a, "--off"))     { o->off = 1; }
        else if (!strcmp(a, "--grade"))   { o->grade = 1; }
        else if (!strcmp(a, "--all"))     { o->all = 1; }
        else if (!strcmp(a, "--burn"))    { const char *v = NEXT(); if (!v) return -1; o->burn = v; }
        else if (!strcmp(a, "--preset"))  { const char *v = NEXT(); if (!v) return -1; o->preset = v; }
        else if (!strcmp(a, "--watermark")) { const char *v = NEXT(); if (!v) return -1; o->mark = v; }
        else if (!strcmp(a, "--ref"))     { const char *v = NEXT(); if (!v) return -1; o->ref = v; }
        else if (!strcmp(a, "--duck"))    { const char *v = NEXT(); if (!v) return -1; o->duck = v; }
        else if (!strcmp(a, "--duck-amount")) { const char *v = NEXT(); if (!v) return -1; o->duck_amt = atof(v); o->has_duck_amt = 1; }
        else if (!strcmp(a, "--preview")) { o->preview = 1; }
        else if (!strcmp(a, "--format") && i + 1 < argc) { o->format = argv[++i]; }
        /* The same slot: an export names a container and a transition names a
         * kind, and no verb takes both. A separate field would be a second
         * thing to forget to parse. */
        else if (!strcmp(a, "--kind") && i + 1 < argc)   { o->format = argv[++i]; }
        else if (!strcmp(a, "--count"))   { const char *v = NEXT(); if (!v) return -1; o->count = atoi(v); }
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
        /* And naming a LUT without saying how much of it means all of it.
         * Zero is the null value for every field in the struct, which is what
         * makes a zeroed develop stack the null grade — but it also means a
         * LUT set and nothing else would render as nothing, and look like the
         * import had failed. Same reasoning, same place: the reader must not
         * do this, so the command layer does. */
        if (!strcmp(key, "lut") && *val && d->lut_amount == 0.0f)
            d->lut_amount = 100.0f;
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
               f.type == SS_T_CURVE ? "curve" : f.type == SS_T_INT ? "int"
                                                : f.type == SS_T_STR ? "str" : "float",
               f.group, f.label, f.lo, f.hi);
    }
    return 0;
}


/* The darkroom's tl_save.
 *
 * A photograph's document is its SIDECAR, and history is a property of a file
 * — so the machinery that gives the cutting room undo gives the darkroom undo
 * with no second history, no second shape of it on disk and nothing new to
 * get out of step. Every verb that writes a sidecar ends here, exactly as
 * every timeline verb ends in tl_save.
 *
 * ⚠ The UNTOUCHED photograph is a state undo has to be able to reach, and
 * ss_history_seed can only snapshot a file that is there. A first edit
 * therefore writes the defaults FIRST and seeds from those — otherwise the
 * oldest state undo knows is the one the first edit produced, and the picture
 * as it arrived is gone from the moment the first slider moves. */
static int dev_save_opt(const ss_edit *e, const char *side, int nohist)
{
    if (!path_exists(side)) {
        ss_edit base;
        if (ss_edit_load(&base, side) == 0) ss_edit_save(&base, side);
    }
    /* ⚠ Seeded even when this write is not being recorded: the state to come
     * back to is the picture BEFORE the gesture started, and a drag that
     * happened to be the first edit of the session would otherwise have
     * nothing behind it. */
    ss_history_seed(side);
    if (ss_edit_save(e, side) != 0) return -1;
    /* A failed snapshot is not a failed edit — the same bargain tl_save
     * strikes. What is lost is one step of undo. */
    if (!nohist) ss_history_push(side);
    return 0;
}

static int dev_save(const ss_edit *e, const char *side)
{
    return dev_save_opt(e, side, 0);
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
    int i, changed = 0, nohist = 0;

    ss_sidecar_path(path, side, sizeof side);
    if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);

    for (i = start; i < argc; i++) {
        char *eq;
        /* ⚠ --no-history is what makes undo step by GESTURE rather than by
         * mouse event. A slider drag is one `set` per tick, so recording each
         * would fill a hundred-deep history with a hundred stops of one
         * exposure slider — and Ctrl+Z would walk back through them a
         * hundredth of a stop at a time, which is not undo. The window passes
         * it while the hand is down and commits once on release. */
        if (!strcmp(argv[i], "--no-history")) { nohist = 1; continue; }
        eq = strchr(argv[i], '=');
        if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
        *eq = '\0';
        if (apply_set(&e.dev, argv[i], eq + 1) != 0) return 1;
        changed++;
    }
    if (!changed) return die("nothing to set");
    if (dev_save_opt(&e, side, nohist) != 0) return die("cannot write %s", side);
    return 0;
}

/* ⚠ Reset WRITES the defaults rather than unlinking the sidecar, so that it
 * is an edit like any other and `undo` takes it back. Pressing Reset on an
 * afternoon's work and having no way back is the single worst thing this
 * window could do, and unlinking the file put that state outside the history
 * entirely — there is nothing on disk left to snapshot. A sidecar of defaults
 * renders identically to no sidecar at all; that is why `get` works on a
 * photograph that has never been touched. */
static int cmd_reset(const char *path)
{
    char side[4200];
    ss_edit e;

    ss_sidecar_path(path, side, sizeof side);
    if (!path_exists(side)) return 0;          /* nothing to take back */
    if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);
    ss_edit_reset(&e);
    if (dev_save(&e, side) != 0) return die("cannot write %s", side);
    return 0;
}

/* Undo, redo and how deep they go — for a PHOTOGRAPH.
 *
 * The same three answers the cutting room gives, in the same shape, off the
 * same machinery: a sidecar is a document on disk and history is a property
 * of a file. ⚠ Like the timeline's, these move the FILE and so do NOT go
 * through dev_save — a history move that recorded itself would bury the thing
 * it moved to.
 *
 * `undo` on a photograph nobody has edited is not an error: it prints
 * `nothing` and exits 1, which is what the window reads to grey the button. */
static int cmd_devhist(const char *path, const char *verb)
{
    char side[4200];
    int u = 0, r = 0;

    ss_sidecar_path(path, side, sizeof side);
    if (!strcmp(verb, "history")) {
        ss_history_depth(side, &u, &r);
        printf("undo\t%d\nredo\t%d\n", u, r);
        return 0;
    }
    {
        int rc = !strcmp(verb, "undo") ? ss_history_undo(side)
                                       : ss_history_redo(side);
        if (rc < 0) return die("cannot %s: the history is unreadable", verb);
        ss_history_depth(side, &u, &r);
        printf("%s\tundo\t%d\tredo\t%d\n",
               rc == 0 ? "moved" : "nothing", u, r);
        return rc == 0 ? 0 : 1;
    }
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
        if (dev_save(&e, side) != 0) return die("cannot write %s", side);
        printf("%d\n", e.nmasks - 1);
        return 0;
    }

    if (!strcmp(verb, "remove")) {
        int n = start + 1 < argc ? atoi(argv[start + 1]) : -1;
        if (n < 0 || n >= e.nmasks) return die("no mask %d", n);
        memmove(&e.mask[n], &e.mask[n+1], sizeof(ss_mask) * (e.nmasks - n - 1));
        e.nmasks--;
        if (dev_save(&e, side) != 0) return die("cannot write %s", side);
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
        if (dev_save(&e, side) != 0) return die("cannot write %s", side);
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

/* One frame of ANY file, for a source monitor.
 *
 * `timeline frame` composites a project; this decodes a file that is not in
 * one yet — the footage somebody is deciding an in and an out point on.
 *
 * ⚠ Through the sidecar, when there is one. The source monitor has to show
 * what an insert would actually put in the cut, and an insert brings a
 * photograph's develop with it (see `timeline clip`). A flat source viewer
 * beside a developed timeline is a disagreement the eye catches immediately
 * and cannot explain.
 *
 * ⚠ A still is decoded at 0 whatever is asked for: -ss past the end of a
 * one-frame input yields NOTHING, and scrubbing a photograph is not a thing
 * anybody is trying to do. */
static int cmd_source(const char *path, const opts *o)
{
    ss_image im;
    ss_edit e;
    char side[4200];
    double dur = ss_media_duration(path);
    double at = dur > 0 ? o->at : 0;
    int max = o->size > 0 ? o->size : 960;

    if (!o->out) return die("source needs --out");
    if (at < 0) at = 0;
    if (dur > 0 && at > dur) at = dur;
    if (ss_load_frame(path, at, &im, max) != 0)
        return die("cannot decode %s at %.3f  (is ffmpeg installed?)", path, at);

    ss_sidecar_path(path, side, sizeof side);
    if (path_exists(side) && ss_edit_load(&e, side) == 0)
        ss_edit_apply(&im, &e);

    if (ss_save(o->out, &im, o->quality, 8) != 0) {
        ss_image_free(&im);
        return die("cannot write %s", o->out);
    }
    printf("out\t%s\nwidth\t%d\nheight\t%d\nduration\t%.6f\nat\t%.6f\n",
           o->out, im.w, im.h, dur > 0 ? dur : 0.0, at);
    ss_image_free(&im);
    return 0;
}

/* ------------------------------------------------------- the thumbnail -- */

/* `thumb render` is two steps and one temporary: this program develops the
 * photograph and writes it once, and ONE ffmpeg pass frames it and letters
 * it. Colour never leaves src/colour.c, and no font rasteriser is linked.
 *
 * ⚠ The temporary and the caption files live BESIDE THE OUTPUT, not in /tmp:
 * the output is somewhere the user can write by definition, /tmp may be a
 * different filesystem, and a rename across one is a copy. They are removed
 * on the way out, including when the render fails. */
static void thumb_side(const char *out, const char *leaf, char *buf, size_t n)
{
    const char *slash = strrchr(out, '/');
    if (slash) snprintf(buf, n, "%.*s/.synstudio-%s",
                        (int)(slash - out), out, leaf);
    else       snprintf(buf, n, ".synstudio-%s", leaf);
}

static int cmd_thumb(const char *path, int argc, char **argv, int start)
{
    char side[4200];
    ss_edit e;
    const char *sub = start < argc ? argv[start] : "get";
    opts o;
    char **rest;
    int nrest, i;

    ss_sidecar_path(path, side, sizeof side);
    if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);

    if (!strcmp(sub, "keys")) {
        ss_thumb_info f;
        for (i = 0; ss_thumb_describe(&e.thumb, i, &f); i++)
            printf("%s\t%s\t%g\t%g\t%s\t%s\t%s\t%s\n",
                   f.key, f.value, (double)f.lo, (double)f.hi,
                   f.type, f.group, f.label, f.choices);
        return 0;
    }

    if (!strcmp(sub, "sizes")) {
        const ss_thumb_size *z = ss_thumb_sizes();
        for (i = 0; z[i].name; i++)
            printf("%s\t%d\t%d\t%s\n", z[i].name, z[i].w, z[i].h, z[i].label);
        return 0;
    }

    if (!strcmp(sub, "get")) {
        char buf[512];
        if (start + 1 < argc) {
            if (ss_thumb_get(&e.thumb, argv[start + 1], buf, sizeof buf) != 0)
                return die("no such thumbnail setting: %s", argv[start + 1]);
            puts(buf);
            return 0;
        }
        {
            int w, h;
            ss_thumb_info f;
            ss_thumb_canvas(&e.thumb, &w, &h);
            printf("width\t%d\nheight\t%d\n", w, h);
            for (i = 0; ss_thumb_describe(&e.thumb, i, &f); i++)
                printf("%s\t%s\n", f.key, f.value);
        }
        return 0;
    }

    if (!strcmp(sub, "reset")) {
        ss_thumb_reset(&e.thumb);
        if (dev_save(&e, side) != 0) return die("cannot write %s", side);
        return 0;
    }

    if (!strcmp(sub, "set")) {
        int changed = 0;
        for (i = start + 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
            *eq = '\0';
            if (ss_thumb_set(&e.thumb, argv[i], eq + 1) != 0)
                return die("no such thumbnail setting: %s", argv[i]);
            /* Typing anything about a thumbnail means wanting one. Making
             * somebody set `on=1` as well is the kind of second step that
             * reads as the feature not working. */
            e.thumb.on = 1;
            changed++;
        }
        if (!changed) return die("nothing to set");
        if (dev_save(&e, side) != 0) return die("cannot write %s", side);
        return 0;
    }

    if (strcmp(sub, "render")) return die("thumb: unknown subcommand %s — try "
                                         "keys, sizes, get, set, reset, render", sub);

    /* ---- render ---------------------------------------------------------- */
    opts_default(&o);
    if (parse_opts(argc, argv, start + 1, &o, &rest, &nrest) != 0)
        return die("bad option");
    if (!o.out) return die("thumb render needs --out");

    {
        ss_image im;
        char built[4200], graph[8192], qbuf[16];
        char *av[24];
        int ac = 0, w, h, ntext, rc = -1, q;

        thumb_side(o.out, "thumb-build.png", built, sizeof built);

        /* Developed HERE, by this program, exactly as `render` does it — so a
         * thumbnail and an export of the same photograph are the same
         * colours, and the words are the only difference. */
        if (load_edited(path, &o, &im, &e) != 0) return 1;
        if (ss_save(built, &im, 100, 8) != 0) {
            ss_image_free(&im);
            return die("cannot write %s", built);
        }
        ss_image_free(&im);

        ss_thumb_canvas(&e.thumb, &w, &h);
        {
            /* The caption files, one per layer, beside the output. */
            char dir[4200];
            const char *slash = strrchr(o.out, '/');
            if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - o.out), o.out);
            else       snprintf(dir, sizeof dir, ".");
            for (i = 0; i < SS_THUMB_TEXTS; i++) {
                char tp[4300];
                FILE *fp;
                snprintf(tp, sizeof tp, "%s/thumbtext%d.txt", dir, i);
                fp = fopen(tp, "w");
                if (!fp) { unlink(built); return die("cannot write %s", tp); }
                fputs(e.thumb.text[i].words, fp);
                fclose(fp);
            }
            ntext = ss_thumb_graph(&e.thumb, dir, graph, sizeof graph);
        }

        /* ⚠ quality is the JPEG one and reaches ffmpeg as -q:v 2..31, where
         * LOWER is better — the opposite direction from every other quality
         * number in this program, which is why it is converted in one place.
         * PNG ignores it, which is correct: a PNG has no quality knob. */
        q = o.quality > 0 ? o.quality : 95;
        {
            int qv = 2 + (int)((100 - q) * 29 / 100);
            if (qv < 2) qv = 2;
            if (qv > 31) qv = 31;
            snprintf(qbuf, sizeof qbuf, "%d", qv);
        }
        av[ac++] = (char *)"ffmpeg";
        av[ac++] = (char *)"-v";        av[ac++] = (char *)"error";
        av[ac++] = (char *)"-nostdin";
        av[ac++] = (char *)"-y";
        av[ac++] = (char *)"-i";        av[ac++] = built;
        av[ac++] = (char *)"-vf";       av[ac++] = graph;
        av[ac++] = (char *)"-frames:v"; av[ac++] = (char *)"1";
        av[ac++] = (char *)"-q:v";      av[ac++] = qbuf;
        av[ac++] = (char *)o.out;
        av[ac] = NULL;

        if (o.print) {
            puts(graph);
            rc = 0;
        } else {
            pid_t pid = fork();
            int status = 0;
            if (pid == 0) { execvp(av[0], av); _exit(127); }
            if (pid > 0) {
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;
                rc = (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
            }
        }

        unlink(built);
        {
            char dir[4200], tp[4300];
            const char *slash = strrchr(o.out, '/');
            if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - o.out), o.out);
            else       snprintf(dir, sizeof dir, ".");
            for (i = 0; i < SS_THUMB_TEXTS; i++) {
                snprintf(tp, sizeof tp, "%s/thumbtext%d.txt", dir, i);
                unlink(tp);
            }
        }
        if (rc != 0) return die("cannot render the thumbnail");
        if (!o.print)
            printf("out\t%s\nwidth\t%d\nheight\t%d\ntexts\t%d\n",
                   o.out, w, h, ntext);
        return 0;
    }
}

/* A scope of a photograph, through the same develop stack the picture goes
 * through. `load_edited` is shared with render and histogram for exactly this
 * reason: three ways of looking at one frame, one way of making it. */
static int cmd_scope(const char *path, const opts *o)
{
    ss_image im, sc;
    ss_edit e;
    int rc, kind = SS_SCOPE_WAVEFORM, sw, sh;

    if (!o->out) return die("scope needs --out");
    if (o->format) {
        kind = ss_scope_value(o->format);
        if (kind < 0) return die("scope --kind takes waveform, parade or vector");
    }
    rc = load_edited(path, o, &im, &e);
    if (rc) return rc;

    sw = o->size > 0 ? o->size : 512;
    if (sw < 64) sw = 64;
    if (sw > 4096) sw = 4096;
    /* A vectorscope is square because it is a POLAR plot — a wide one would
     * stretch the hue wheel into an ellipse and every angle read off it would
     * be wrong. The other two are wide because their x axis is the frame. */
    sh = kind == SS_SCOPE_VECTOR ? sw : sw / 2;

    rc = ss_scope_render(&im, kind, sw, sh, &sc);
    ss_image_free(&im);
    if (rc != 0) return die("cannot build the scope");

    rc = ss_save(o->out, &sc, o->quality, 8);
    printf("out\t%s\nkind\t%s\nwidth\t%d\nheight\t%d\n",
           o->out, ss_scope_name(kind), sc.w, sc.h);
    ss_image_free(&sc);
    return rc == 0 ? 0 : die("cannot write %s", o->out);
}

/* Match one photograph to another. The reference is developed through ITS
 * sidecar first — what is being matched is the picture somebody has already
 * graded, not the raw file underneath it. */
static int cmd_match(const char *path, const opts *o)
{
    ss_image tgt, ref;
    ss_edit te, re;
    ss_match_report rep;
    char side[4200];
    int rc;

    if (!o->ref) return die("match needs --ref REFERENCE");

    /* Small on purpose. A fit is dozens of renders, and the numbers it is
     * fitting are averages over the whole frame — which do not change for
     * having been measured at 400 pixels instead of 4000. */
    { opts so = *o; so.size = 400;
      rc = load_edited(o->ref, &so, &ref, &re); }
    if (rc) return rc;

    ss_sidecar_path(path, side, sizeof side);
    if (ss_edit_load(&te, side) != 0) { ss_image_free(&ref);
                                        return die("cannot read %s", side); }
    if (ss_load(path, &tgt, 400) != 0) { ss_image_free(&ref);
                                         return die("cannot decode %s", path); }

    if (ss_shot_match(&ref, &tgt, &te.dev, &rep) != 0) {
        ss_image_free(&ref); ss_image_free(&tgt);
        return die("cannot match");
    }
    ss_image_free(&ref);
    ss_image_free(&tgt);

    if (dev_save(&te, side) != 0) return die("cannot write %s", side);

    {
        char v[64];
        printf("matched\t%s\nto\t%s\n", path, o->ref);
        ss_develop_get(&te.dev, "exposure", v, sizeof v); printf("exposure\t%s\n", v);
        ss_develop_get(&te.dev, "contrast", v, sizeof v); printf("contrast\t%s\n", v);
        ss_develop_get(&te.dev, "temp", v, sizeof v);     printf("temp\t%s\n", v);
        ss_develop_get(&te.dev, "tint", v, sizeof v);     printf("tint\t%s\n", v);
        printf("luma\t%.4f\t%.4f\n", rep.want_luma, rep.got_luma);
        printf("spread\t%.4f\t%.4f\n", rep.want_spread, rep.got_spread);
    }
    return 0;
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

/* ---- LUTs and looks, coming IN ----
 *
 * The `lut` verb bakes one out; these read one in. A LUT is a SETTING, so
 * putting one on a photograph or a clip is `set lut=NAME` and `timeline grade
 * … lut=NAME` and needs no verb of its own — what needs a verb is finding out
 * what is installed, which is the question a picker asks.
 */
static int cmd_luts(void)
{
    int i;
    for (i = 0; i < ss_lut_count(); i++) {
        const ss_lut_entry *e = ss_lut_at(i);
        printf("%s\t%dD\t%d\t%s\n", e->name, e->dims, e->size, e->path);
    }
    return 0;
}

static int cmd_lut_show(const char *ref)
{
    char path[1024];
    ss_lut3d l;
    char err[256] = "";
    const char *slash;

    /* Resolved WITHOUT being read, so that a file which is present and broken
     * says why it is broken. Asking the catalogue instead answers "no LUT of
     * that name" for a truncated download, and sends the wrong person looking
     * in the wrong place. */
    if (ss_lut_resolve(ref, path, sizeof path) != 0)
        return die("no LUT named %s  (try `synstudio luts`)", ref);
    if (ss_lut_read(path, &l, err, sizeof err) != 0) return die("%s", err);
    {
        char name[1024];        /* what `path` can hold: the basename of one */
        size_t len;
        slash = strrchr(path, '/');
        snprintf(name, sizeof name, "%s", slash ? slash + 1 : path);
        len = strlen(name);
        if (len > 5 && !strcasecmp(name + len - 5, ".cube")) name[len - 5] = '\0';
        printf("name\t%s\n", name);
    }
    printf("from\t%s\n", path);
    printf("title\t%s\n", l.title);
    printf("dims\t%d\n", l.dims);
    printf("size\t%d\n", l.size);
    printf("domain\t%g %g %g\t%g %g %g\n",
           (double)l.dmin[0], (double)l.dmin[1], (double)l.dmin[2],
           (double)l.dmax[0], (double)l.dmax[1], (double)l.dmax[2]);
    printf("nodes\t%ld\n",
           l.dims == 3 ? (long)l.size * l.size * l.size : (long)l.size);
    ss_lut_free(&l);
    return 0;
}

static int cmd_look(int argc, char **argv)
{
    const char *sub = argc > 2 ? argv[2] : "list";
    int i;

    if (!strcmp(sub, "list")) {
        for (i = 0; i < ss_look_count(); i++) {
            const ss_look *k = ss_look_at(i);
            printf("%s\t%s\t%s\t%s\n", k->name, k->label, k->about, k->path);
        }
        return 0;
    }

    if (!strcmp(sub, "show")) {
        const ss_look *k = argc > 3 ? ss_look_find(argv[3]) : NULL;
        ss_develop d;
        ss_develop_info f;
        ss_develop def;

        if (!k) return die("no such look: %s  (try `synstudio look list`)",
                          argc > 3 ? argv[3] : "");
        ss_develop_reset(&d);
        ss_develop_reset(&def);
        if (ss_look_apply(k, &d) < 0) return die("cannot read %s", k->path);
        printf("name\t%s\n", k->name);
        printf("label\t%s\n", k->label);
        printf("about\t%s\n", k->about);
        printf("from\t%s\n", k->path);
        /* What it SETS, which is the only part of a develop stack a look
         * carries — printed by asking the table, so it is the same list the
         * apply walks and cannot drift from it. */
        for (i = 0; ss_develop_describe(i, &f) == 0; i++) {
            char have[512], was[512];
            if (ss_develop_get(&d, f.key, have, sizeof have) != 0) continue;
            if (ss_develop_get(&def, f.key, was, sizeof was) != 0) continue;
            if (!strcmp(have, was)) continue;
            printf("set\t%s\t%s\n", f.key, have);
        }
        return 0;
    }

    /* `look save NAME --from IMG` takes the develop stack off a photograph
     * that already looks right, which is how a look actually gets made. */
    if (!strcmp(sub, "save")) {
        const char *name = argc > 3 ? argv[3] : NULL;
        const char *label = NULL, *from = NULL;
        ss_develop d;
        char path[1200];
        int n;

        if (!name) return die("look save needs a name");
        ss_develop_reset(&d);
        for (i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--from") && i + 1 < argc) from = argv[++i];
            else if (!strcmp(argv[i], "--label") && i + 1 < argc) label = argv[++i];
            else if (strchr(argv[i], '=')) {
                char *eq = strchr(argv[i], '=');
                *eq = '\0';
                if (apply_set(&d, argv[i], eq + 1) != 0) return 1;
            } else return die("look save: unknown option %s", argv[i]);
        }
        if (from) {
            char side[4200];
            ss_edit e;
            ss_develop had = d;
            ss_develop_info f;
            ss_sidecar_path(from, side, sizeof side);
            if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);
            d = e.dev;
            /* Anything named on the command line as well WINS over the
             * photograph, so `--from shot.jpg contrast=0` saves the look
             * without the contrast that shot happened to need. */
            for (i = 0; ss_develop_describe(i, &f) == 0; i++) {
                char v[512], def_[512];
                ss_develop dflt;
                ss_develop_reset(&dflt);
                if (ss_develop_get(&had, f.key, v, sizeof v) != 0) continue;
                if (ss_develop_get(&dflt, f.key, def_, sizeof def_) != 0) continue;
                if (strcmp(v, def_)) ss_develop_set(&d, f.key, v);
            }
        }
        n = ss_look_save(name, label, &d, path, sizeof path);
        if (n < 0) return die("cannot save a look called %s", name);
        printf("out\t%s\n", path);
        printf("fields\t%d\n", n);
        return 0;
    }

    if (!strcmp(sub, "apply")) {
        const ss_look *k = argc > 3 ? ss_look_find(argv[3]) : NULL;
        const char *to = NULL;
        char side[4200];
        ss_edit e;
        int bad;

        if (!k) return die("no such look: %s  (try `synstudio look list`)",
                          argc > 3 ? argv[3] : "");
        for (i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--to") && i + 1 < argc) to = argv[++i];
            else return die("look apply: unknown option %s", argv[i]);
        }
        if (!to) return die("look apply needs --to FILE");

        ss_sidecar_path(to, side, sizeof side);
        if (ss_edit_load(&e, side) != 0) return die("cannot read %s", side);
        bad = ss_look_apply(k, &e.dev);
        if (bad < 0) return die("cannot read %s", k->path);
        if (dev_save(&e, side) != 0) return die("cannot write %s", side);
        /* A look written by a newer synstudio still lands, minus whatever
         * this build has never heard of — but it says so, because a look that
         * arrived 80%% applied and silent is a look somebody will spend an
         * afternoon trying to match. */
        if (bad) fprintf(stderr, "synstudio: %d setting%s in %s this build does "
                                 "not know\n", bad, bad == 1 ? "" : "s", k->name);
        printf("out\t%s\n", side);
        return 0;
    }

    if (!strcmp(sub, "remove")) {
        const ss_look *k = argc > 3 ? ss_look_find(argv[3]) : NULL;
        char dir[1024], own[1200];
        if (!k) return die("no such look: %s", argc > 3 ? argv[3] : "");
        /* Only a look of your OWN can be removed. Deleting an installed one
         * would come back on the next update and take the user's copy of the
         * name with it. */
        if (ss_look_dir(dir, sizeof dir) != 0) return die("no place for looks");
        snprintf(own, sizeof own, "%s/%s.synlook", dir, k->name);
        if (strcmp(own, k->path))
            return die("%s is installed, not yours: %s", k->name, k->path);
        if (unlink(own) != 0) return die("cannot remove %s: %s", own, strerror(errno));
        printf("removed\t%s\n", own);
        return 0;
    }

    return die("look: unknown subcommand %s — try list, show, save, apply, remove",
               sub);
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
    /* The document knows where its own sidecars are, so nothing downstream
     * has to be handed a second path beside the project. */
    ss_timeline_stabdir(proj, t->stabdir, sizeof t->stabdir);
    return 0;
}

/* Every mutating verb in this file ends here, which is what makes undo one
 * change rather than twenty: the document as it was is recorded before the
 * write, and the document as it becomes is recorded after it. Nothing else has
 * to know history exists. */
static int tl_save(const char *proj, const ss_timeline *t)
{
    char tmp[4200];
    FILE *fp;
    int rc;

    /* Before the write, and only ever once per project: the state undo has to
     * come back to is the one nobody has changed yet. */
    ss_history_seed(proj);

    snprintf(tmp, sizeof tmp, "%s.tmp", proj);
    fp = fopen(tmp, "w");
    if (!fp) return -1;
    rc = ss_timeline_write(t, fp);
    if (fclose(fp) != 0) rc = -1;
    if (rc != 0) { unlink(tmp); return -1; }
    if (rename(tmp, proj) != 0) return -1;

    /* A failed snapshot is not a failed edit. The document is written and
     * correct; what is lost is one step of undo, and refusing the edit over
     * that would be the worse trade. */
    ss_history_push(proj);
    return 0;
}


static int copy_bytes(const char *from, const char *to)
{
    FILE *a, *b;
    char buf[65536];
    size_t n;
    int rc = 0;

    a = fopen(from, "rb");
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

/* ⚠ The stabiliser's measurements live in `<project>.stab`, keyed to the
 * project's NAME — so a project written under a new one would look for them
 * in a directory nothing has written, find nothing, and render every
 * stabilised clip UNSTEADY. Not an error and not a message: the shake would
 * simply be back, in a copy of a cut that was steady a moment ago. So a save
 * under a new name carries them across. */
static void copy_stabdir(const char *from_proj, const char *to_proj)
{
    char from[1100], to[1100], a[1400], b[1400];
    DIR *d;
    struct dirent *e;

    ss_timeline_stabdir(from_proj, from, sizeof from);
    ss_timeline_stabdir(to_proj, to, sizeof to);
    d = opendir(from);
    if (!d) return;                 /* nothing stabilised: nothing to carry */
    if (mkdir(to, 0755) != 0 && errno != EEXIST) { closedir(d); return; }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(a, sizeof a, "%.900s/%.120s", from, e->d_name);
        snprintf(b, sizeof b, "%.900s/%.120s", to, e->d_name);
        copy_bytes(a, b);
    }
    closedir(d);
}

/* `Untitled.syntl` taken → `Untitled-2.syntl`, and the EXTENSION is kept: a
 * project called Untitled-2 is still a .syntl, and a window that opens what
 * this prints would not find one named otherwise. */
static void free_name(const char *want, char *out, size_t n)
{
    const char *dot, *slash;
    char stem[1024], ext[64];
    int i;

    snprintf(out, n, "%s", want ? want : "");
    if (!path_exists(out)) return;

    slash = strrchr(out, '/');
    dot   = strrchr(slash ? slash : out, '.');
    if (dot) {
        snprintf(stem, sizeof stem, "%.*s", (int)(dot - out), out);
        snprintf(ext, sizeof ext, "%s", dot);
    } else {
        snprintf(stem, sizeof stem, "%s", out);
        ext[0] = '\0';
    }
    for (i = 2; i < 1000; i++) {
        snprintf(out, n, "%.900s-%d%.40s", stem, i, ext);
        if (!path_exists(out)) return;
    }
}

/* Every clip property in one listing, the same shape `keys` uses for the
 * develop stack: KEY, current, ui-lo, ui-hi, type, group, label, lo, hi and —
 * for an enum — the choices. The inspector in the window is built from this,
 * so a property added to the table in timeline.c appears there without the
 * QML being touched. */
/* An effect a project names but this machine has not got.
 *
 * The graph builder SKIPS it rather than failing, because a render that stops
 * dead is worse than one missing a glow. But skipping quietly would mean a
 * project made on somebody else's machine comes out different with nothing
 * said, so it is said here — once per name, before anything starts. */
static void warn_missing_fx(const ss_timeline *t)
{
    char seen[SS_MAX_FX * 16][32];
    int nseen = 0, i, j, k, q;

    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++) {
            const ss_clip *c = &t->track[i].clip[j];
            for (k = 0; k < c->nfx; k++) {
                if (ss_fx_find(c->fx[k].name)) continue;
                for (q = 0; q < nseen; q++)
                    if (!strcmp(seen[q], c->fx[k].name)) break;
                if (q < nseen || nseen >= (int)(sizeof seen / sizeof seen[0]))
                    continue;
                snprintf(seen[nseen++], sizeof seen[0], "%s", c->fx[k].name);
                fprintf(stderr, "synstudio: no effect called %s is installed — "
                                "clips using it render without it\n",
                        c->fx[k].name);
            }
        }
}

/* Ask each media clip whether it has sound, once, before a graph is built.
 *
 * This lives in the command layer and not in the graph builder for the same
 * reason `still` does: timeline.c describes intent and must not go opening
 * files. It is not stored either — a clip's source can be replaced under it,
 * and a stale "no audio" is a silent silence. */
static void tl_fill_audio(ss_timeline *t)
{
    int i, j;
    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++) {
            ss_clip *c = &t->track[i].clip[j];
            c->achannels = (c->kind == SS_CLIP_MEDIA && c->path[0])
                           ? ss_media_channels(c->path) : 0;
            c->has_audio = c->achannels > 0;
        }
}

static int cmd_timeline_keys(void)
{
    ss_clip c;
    ss_clip_info f;
    int i;

    ss_clip_reset(&c);
    for (i = 0; ss_clip_describe(i, &f); i++) {
        char buf[512];
        ss_clip_get(&c, f.key, buf, sizeof buf);
        /* The last column says whether the property can be keyed. It is
         * appended rather than inserted so a reader that already knows the
         * first eight columns keeps working. */
        printf("%s\t%s\t%.6g\t%.6g\t%s\t%s\t%s\t%s\t%d\n",
               f.key, buf, f.lo, f.hi,
               f.type == SS_CT_ENUM ? "enum" : f.type == SS_CT_TEXT ? "text"
               : f.type == SS_CT_INT ? "int" : "float",
               f.group, f.label, f.choices ? f.choices : "", f.animatable);
    }
    return 0;
}

/* A clip property, at an instant. The same call as ss_clip_get for anything
 * that is not keyed — which is nearly everything — and the evaluator for
 * anything that is, so no caller has to ask which kind it is holding. */
static int clip_get_at(const ss_clip *c, const char *key, double at,
                       char *out, size_t n)
{
    if (ss_clip_prop_animatable(key) && ss_clip_prop_nkeys(c, key) > 0) {
        snprintf(out, n, "%.6g", ss_clip_prop_at(c, key, at));
        return 0;
    }
    return ss_clip_get(c, key, out, n);
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

/* Rendering ONE frame through an effect is the check that matters.
 *
 * A recipe can name only allowed filters, interpolate only declared
 * parameters and still be nonsense — a misspelled option, a label that goes
 * nowhere, a blend mode that does not exist — and ffmpeg does not find out
 * until it builds the graph. So an effect is rendered when it ARRIVES: when it
 * is checked by hand, and again when it first lands on a clip. The alternative
 * is finding out at the end of an export. */
static int fx_render_check(const ss_fx *f)
{
    char frag[4096], graph[4400];
    char **av;
    int i, ac = 0, bad = 0;

    if (ss_fx_expand(f, NULL, 0, 1, "fxin", "fxout", frag, sizeof frag) != 0)
        return die("%s: the chain will not expand", f->name);
    snprintf(graph, sizeof graph,
             "color=c=0x808080:s=64x64:d=0.04,format=rgba[fxin];%s;"
             "[fxout]null[o]", frag);

    av = calloc(24, sizeof *av);
    if (!av) return die("out of memory");
    av[ac++] = strdup("ffmpeg");
    av[ac++] = strdup("-v"); av[ac++] = strdup("error");
    av[ac++] = strdup("-nostdin");
    av[ac++] = strdup("-filter_complex"); av[ac++] = strdup(graph);
    av[ac++] = strdup("-map"); av[ac++] = strdup("[o]");
    av[ac++] = strdup("-frames:v"); av[ac++] = strdup("1");
    av[ac++] = strdup("-f"); av[ac++] = strdup("null");
    av[ac++] = strdup("-");
    av[ac] = NULL;
    for (i = 0; i < ac; i++) if (!av[i]) bad = 1;
    if (bad) {
        for (i = 0; i < ac; i++) free(av[i]);
        free(av);
        return die("out of memory");
    }
    /* tl_run owns the array and frees it, however it goes. */
    return tl_run(av, ac, 0);
}

/* ---- the effect catalogue ----
 *
 * `list` and `show` are tables, the same discipline as `formats` and the two
 * `keys`: the window builds its picker and its sliders from them, so it cannot
 * offer an effect the engine has not got or a knob the recipe does not have.
 */
static int cmd_fx(int argc, char **argv)
{
    const char *sub = argc > 2 ? argv[2] : "list";
    int i;

    if (!strcmp(sub, "list")) {
        for (i = 0; i < ss_fx_count(); i++) {
            const ss_fx *f = ss_fx_at(i);
            printf("%s\t%s\t%s\t%d\t%d\t%s\n",
                   f->name, f->label, f->group, f->nparam, f->alpha, f->about);
        }
        return 0;
    }

    /* Every parameter of every effect, in ONE call. The window needs the
     * ranges to draw a slider with and would otherwise be asking `show` once
     * per effect — twenty-odd processes to open a panel. */
    if (!strcmp(sub, "params")) {
        int k;
        for (i = 0; i < ss_fx_count(); i++) {
            const ss_fx *f = ss_fx_at(i);
            for (k = 0; k < f->nparam; k++)
                printf("%s\t%s\t%g\t%g\t%g\t%s\n", f->name,
                       f->param[k].key, f->param[k].def, f->param[k].lo,
                       f->param[k].hi, f->param[k].label);
        }
        return 0;
    }

    if (!strcmp(sub, "show")) {
        const ss_fx *f = argc > 3 ? ss_fx_find(argv[3]) : NULL;
        if (!f) return die("no such effect: %s  (try `synstudio fx list`)",
                           argc > 3 ? argv[3] : "");
        printf("name\t%s\n", f->name);
        printf("label\t%s\n", f->label);
        printf("group\t%s\n", f->group);
        printf("about\t%s\n", f->about);
        printf("alpha\t%d\n", f->alpha);
        printf("from\t%s\n", f->path);
        for (i = 0; i < f->nparam; i++)
            printf("param\t%s\t%g\t%g\t%g\t%s\n", f->param[i].key,
                   f->param[i].def, f->param[i].lo, f->param[i].hi,
                   f->param[i].label);
        return 0;
    }

    if (!strcmp(sub, "check")) {
        ss_fx one;
        const ss_fx *f = NULL;
        char err[160] = "";

        if (argc < 4) return die("check wants an effect or a .synfx file");
        if (strstr(argv[3], ".synfx")) {
            if (ss_fx_read(argv[3], &one, err, sizeof err) != 0)
                return die("%s: %s", argv[3], err);
            f = &one;
        } else {
            f = ss_fx_find(argv[3]);
            if (!f) return die("no such effect: %s", argv[3]);
        }
        if (fx_render_check(f) != 0) return 1;
        printf("ok\t%s\t%d\n", f->name, f->nparam);
        return 0;
    }

    return die("fx: unknown subcommand %s — try list, params, show, check", sub);
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
    /* Like `keys`: a table the window builds a control from, so the picker it
     * draws and the formats this engine has cannot drift apart. */
    /* The transition catalogue, the same shape as `formats`: a table the
     * window builds its picker from, so it cannot offer one the renderer does
     * not have. */
    if (verb && !strcmp(verb, "transitions")) {
        int i;
        for (i = 0; i < ss_trans_count(); i++)
            printf("%d\t%s\t%s\n", i, ss_trans_name(i), ss_trans_label(i));
        return 0;
    }
    if (verb && !strcmp(verb, "presets")) {
        const ss_tl_preset *pr = ss_timeline_presets();
        int i;
        for (i = 0; pr[i].name; i++)
            printf("%s\t%d\t%d\t%.6g\t%s\n", pr[i].name, pr[i].w, pr[i].h,
                   pr[i].fps, pr[i].label);
        return 0;
    }
    if (verb && !strcmp(verb, "styles")) {
        const ss_title_style *st = ss_title_styles();
        int i;
        for (i = 0; st[i].name; i++)
            printf("%s\t%s\n", st[i].name, st[i].label);
        return 0;
    }
    if (verb && !strcmp(verb, "formats")) {
        const ss_tl_format *f = ss_timeline_formats();
        int i;
        for (i = 0; f[i].name; i++)
            printf("%s\t%s\t%s\n", f[i].name, f[i].ext, f[i].label);
        return 0;
    }
    /* What is ON the clipboard, without pasting it to find out.
     *
     * ⚠ Asked of the ENGINE, and no project needed. The clipboard is a file
     * under ~/.config and it outlives the window, the project it was filled
     * from and the session — which is most of what a clipboard is for. A
     * window that remembered its own last copy instead would offer Paste
     * after being restarted with nothing on it, and refuse Paste with a clip
     * sitting right there from the other project still open next door.
     *
     * `empty` and exit 0: nothing copied yet is an ANSWER, not a failure. */
    if (verb && !strcmp(verb, "clipboard")) {
        ss_clip src;
        char buf[1024];
        if (ss_clip_copy_in(&src) != 0) { puts("empty"); return 0; }
        printf("kind\t%s\n", src.kind == SS_CLIP_TITLE ? "title"
                            : src.kind == SS_CLIP_SOLID ? "solid" : "media");
        printf("path\t%s\n", src.path);
        /* The caption goes through ss_clip_get, which ESCAPES it — a title
         * with a line break in it would otherwise end this record halfway
         * through, and the window reads these lines. `path` is printed raw
         * because `timeline get` prints it raw, and one of these two readers
         * having its own rules is how a parser starts to drift. */
        if (ss_clip_get(&src, "text", buf, sizeof buf) == 0)
            printf("text\t%s\n", buf);
        printf("length\t%.6f\n", ss_clip_length(&src));
        printf("graded\t%d\n", src.has_grade);
        printf("keys\t%d\n", src.nkeys);
        printf("fx\t%d\n", src.nfx);
        return 0;
    }
    if (!verb || !proj) { usage(); return 1; }
    opts_default(&o);

    if (!strcmp(verb, "new")) {
        int w = 1920, h = 1080;
        double fps = 25.0;
        int i, uniq = 0, noclob = 0;
        char picked[1024];

        for (i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--size") && i + 1 < argc) {
                if (sscanf(argv[++i], "%dx%d", &w, &h) != 2)
                    return die("--size wants WxH, e.g. 1920x1080");
            } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
                fps = atof(argv[++i]);
            } else if (!strcmp(argv[i], "--unique")) {
                uniq = 1;
            } else if (!strcmp(argv[i], "--no-clobber")) {
                noclob = 1;
            }
        }
        /* ⚠ Plain `new` still writes over whatever is there. That is what
         * every script and every test in this tree already asks it for, and
         * changing it under them would be a silent regression of its own.
         * The two ways NOT to lose a project are asked for by name:
         *
         *   --unique      take the next free name and PRINT the one used
         *   --no-clobber  refuse an existing file, exit 3
         *
         * Exit 3 is an ANSWER, not a failure — the window turns it into a
         * Replace button rather than an error, the way `peaks` exit 100 means
         * "no audio". */
        if (uniq) {
            free_name(proj, picked, sizeof picked);
            proj = picked;
        } else if (noclob && path_exists(proj)) {
            fprintf(stderr, "synstudio: %s is there already\n", proj);
            return 3;
        }
        ss_timeline_reset(t, w, h, fps);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        /* The name is only news when the engine chose it. */
        if (uniq) puts(proj);
        return 0;
    }

    if (tl_load(proj, t) != 0) return die("cannot read %s", proj);

    if (!strcmp(verb, "show")) {
        ss_timeline_write(t, stdout);
        printf("# duration\t%.4f\n", ss_timeline_duration(t));
        return 0;
    }

    /* ── Save as ────────────────────────────────────────────────────────────
     *
     * `timeline saveas PROJ --out PATH [--force]`, and it PRINTS the path it
     * wrote, so the window can go on editing the copy without having to
     * assume it landed where it asked.
     *
     * There is no `save`, and there is nothing to sync: every mutating verb
     * ends in tl_save, so the file on disk is the cut as it stands after each
     * edit. What was missing was never persistence — it was a NAME. Without
     * this, every project the window started lived at one fixed path, so a
     * second one quietly took the first one's place.
     *
     * ⛔ Refuses an existing file with exit 3 unless --force, the same answer
     * `new --no-clobber` gives, so one branch in the window covers both. */
    if (!strcmp(verb, "saveas")) {
        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0)
            return die("saveas: bad options");
        if (!o.out || !*o.out) return die("saveas wants --out PATH");
        if (!strcmp(o.out, proj)) { puts(proj); return 0; }   /* already there */
        if (path_exists(o.out) && !o.force) {
            fprintf(stderr, "synstudio: %s is there already\n", o.out);
            return 3;
        }
        if (tl_save(o.out, t) != 0) return die("cannot write %s", o.out);
        copy_stabdir(proj, o.out);
        /* ⚠ The copy starts its OWN undo history, seeded by that write with
         * the document as it stands. Carrying the old one over would offer an
         * undo that walks this file back through edits made to a DIFFERENT
         * project, which is the one thing worse than no history at all. */
        puts(o.out);
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
                else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
                    t->track[tr].gain_db = ss_clampf((float)atof(argv[++i]),
                                                     -60.0f, 24.0f);
                else if (!strcmp(argv[i], "--pan") && i + 1 < argc)
                    t->track[tr].pan = ss_clampf((float)atof(argv[++i]),
                                                 -1.0f, 1.0f);
                else if (!strcmp(argv[i], "--solo") && i + 1 < argc)
                    t->track[tr].solo = atoi(argv[++i]) ? 1 : 0;
                /* `--duck off` and `--duck N`: a music bed gets out of the
                 * way of the track carrying the dialogue. */
                else if (!strcmp(argv[i], "--duck") && i + 1 < argc) {
                    const char *v = argv[++i];
                    if (!strcmp(v, "off") || !strcmp(v, "none"))
                        t->track[tr].duck_from = -1;
                    else {
                        int k = atoi(v);
                        if (k < 0 || k >= t->ntracks)
                            return die("no track %s to duck from", v);
                        if (k == tr)
                            return die("a track cannot duck itself");
                        t->track[tr].duck_from = k;
                        if (t->track[tr].duck <= 0.0f) t->track[tr].duck = 60.0f;
                    }
                }
                else if (!strcmp(argv[i], "--duck-amount") && i + 1 < argc)
                    t->track[tr].duck = (float)atof(argv[++i]);
                else return die("track: unknown option %s", argv[i]);
            }
        }
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    /* One fader after the mix, and the meter that says whether it is needed.
     *
     * `master` with no option PRINTS rather than refusing: the window needs to
     * read the value it is about to draw a fader for, and a verb that can only
     * be written is a verb the window has to keep its own copy of. */
    /* The render queue.
     *
     * `run` re-invokes THIS binary, once per job, with the arguments the job
     * was added with. Not a shared render function called two ways: a queued
     * job is then literally the command somebody could have typed, a failure
     * in one is a process exiting rather than a state to unwind, and there is
     * no way for the two paths to drift apart because there is one path. */
    if (!strcmp(verb, "queue")) {
        static char job[SS_QUEUE_MAX][SS_QUEUE_LINE];
        const char *act = argc > 4 ? argv[4] : "list";
        char path[1024];
        int n, k;

        if (!strcmp(act, "add")) {
            /* `timeline queue PROJ add --out F ...` — everything from the
             * project onward is the job, so it is exactly what `timeline
             * export` would have been given. */
            const char *jv[64];
            int jc = 0;
            jv[jc++] = proj;
            for (k = 5; k < argc && jc < 63; k++) jv[jc++] = argv[k];
            n = ss_queue_add(jv, jc);
            if (n == -2) return die("an argument contains a tab; the queue is "
                                    "one job per line");
            if (n != 0)  return die("cannot write the queue");
            printf("queued\t%s\n", proj);
            return 0;
        }
        if (!strcmp(act, "clear")) {
            if (ss_queue_clear() != 0) return die("cannot clear the queue");
            printf("cleared\n");
            return 0;
        }
        n = ss_queue_read(job, SS_QUEUE_MAX);
        if (n < 0) return die("cannot read the queue");
        if (!strcmp(act, "list")) {
            ss_queue_path(path, sizeof path);
            printf("queue\t%s\njobs\t%d\n", path, n);
            for (k = 0; k < n; k++) {
                char buf[SS_QUEUE_LINE];
                char *p2;
                snprintf(buf, sizeof buf, "%s", job[k]);
                for (p2 = buf; *p2; p2++) if (*p2 == '\t') *p2 = ' ';
                printf("job\t%d\t%s\n", k, buf);
            }
            return 0;
        }
        if (!strcmp(act, "run")) {
            int failed = 0, done = 0;
            for (k = 0; k < n; k++) {
                char *av[68], *p2, *tok;
                int ac2 = 0, status;
                pid_t pid;

                av[ac2++] = (char *)"synstudio";
                av[ac2++] = (char *)"timeline";
                av[ac2++] = (char *)"export";
                for (tok = strtok_r(job[k], "\t", &p2); tok && ac2 < 67;
                     tok = strtok_r(NULL, "\t", &p2))
                    av[ac2++] = tok;
                av[ac2] = NULL;

                fprintf(stderr, "== job %d of %d: %s\n", k + 1, n, av[3]);
                pid = fork();
                if (pid < 0) return die("cannot fork");
                if (pid == 0) {
                    /* This binary, not whatever `synstudio` resolves to on
                     * PATH: a queue run must not depend on which build is
                     * installed while a different one is queueing. */
                    execv("/proc/self/exe", av);
                    _exit(127);
                }
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    failed++;
                    /* Kept going on purpose: a queue is left running while
                     * nobody is watching, and stopping at the first bad job
                     * wastes the night on the ones that would have worked. */
                    fprintf(stderr, "   job %d FAILED\n", k + 1);
                } else {
                    done++;
                }
            }
            printf("done\t%d\nfailed\t%d\n", done, failed);
            /* The queue is kept. A job that failed is a job to look at, and a
             * queue that empties itself takes the evidence with it. */
            return failed ? 1 : 0;
        }
        return die("queue takes add, list, run or clear, not %s", act);
    }

    /* The render range: a DELIVERY decision that lives in the document,
     * because it is set while looking at the cut and not while typing a
     * render command. --off puts it back to the whole timeline. */
    if (!strcmp(verb, "range")) {
        double a, b;
        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (o.off) {
            t->range_in = t->range_out = 0;
        } else if (o.has_to || o.at > 0) {
            t->range_in  = o.at;
            t->range_out = o.has_to ? o.to : ss_timeline_duration(t);
            if (!(t->range_out > t->range_in))
                return die("--to has to come after --at");
        }
        if (argc > 4 && tl_save(proj, t) != 0)
            return die("cannot write %s", proj);
        ss_timeline_range(t, &a, &b);
        printf("in\t%.3f\nout\t%.3f\nlength\t%.3f\nwhole\t%s\n",
               a, b, b - a, t->range_out > t->range_in ? "no" : "yes");
        return 0;
    }

    /* The loudness a delivery is normalised to. A verb rather than an export
     * flag because it is a property of the deliverable — the same cut ships
     * to broadcast at -23 and to a streaming service at -14, and that is a
     * decision somebody makes once about the programme. */
    if (!strcmp(verb, "loudness")) {
        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (o.off) t->lufs = 0.0f;
        else if (o.has_value) {
            if (o.value > -5.0 || o.value < -40.0)
                return die("a delivery target is between -40 and -5 LUFS "
                           "(broadcast is -23, streaming -14)");
            t->lufs = (float)o.value;
        }
        if (argc > 4 && tl_save(proj, t) != 0) return die("cannot write %s", proj);
        if (t->lufs < 0.0f) printf("target\t%.2f\n", (double)t->lufs);
        else                printf("target\tnone\n");
        return 0;
    }

    if (!strcmp(verb, "master")) {
        int i;
        for (i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--gain") && i + 1 < argc)
                t->master_db = ss_clampf((float)atof(argv[++i]), -60.0f, 24.0f);
            else return die("master: unknown option %s", argv[i]);
        }
        if (argc > 4 && tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("gain\t%.3f\n", t->master_db);
        return 0;
    }

    /* Measure a clip, then write the gain that lands it on target.
     *
     * The engine measures and the engine decides: a window that read a number
     * and did the subtraction itself would be a second place where "how loud
     * should this be" is answered. -14 LUFS is where streaming lands; -23 is
     * EBU R128 for broadcast. */
    if (!strcmp(verb, "normalise") || !strcmp(verb, "normalize")) {
        double target = -14.0;
        ss_loudness l;
        ss_clip *c;
        double in, out;
        int i;

        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                    argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        for (i = 6; i < argc; i++) {
            if (!strcmp(argv[i], "--target") && i + 1 < argc)
                target = atof(argv[++i]);
            else return die("normalise: unknown option %s", argv[i]);
        }
        c = &t->track[tr].clip[cl];
        if (c->kind != SS_CLIP_MEDIA || !c->path[0])
            return die("nothing to measure on a generated clip");

        in = c->src_in;
        out = c->src_out - c->src_in;
        if (ss_media_loudness(c->path, in, out, &l) != 0)
            return die("no sound to measure in %s", c->path);
        if (l.lufs <= -70.0)
            return die("that clip is silence; there is no gain that fixes it");

        /* The clip's OWN gain is what is being set, so the measurement — which
         * was taken off the file, before any gain — is the whole answer. */
        c->gain_db = ss_clampf((float)(target - l.lufs), -60.0f, 24.0f);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("measured\t%.2f\ntarget\t%.2f\ngain\t%.3f\npeak\t%.2f\n",
               l.lufs, target, c->gain_db, l.peak_db);
        return 0;
    }

    /* Undo and redo move the FILE, so they do not go through tl_save — a
     * history move that recorded itself would bury the thing it moved to. */
    if (!strcmp(verb, "undo") || !strcmp(verb, "redo")) {
        int rc = !strcmp(verb, "undo") ? ss_history_undo(proj)
                                       : ss_history_redo(proj);
        int u = 0, r = 0;
        if (rc < 0) return die("cannot %s: the history is unreadable", verb);
        ss_history_depth(proj, &u, &r);
        printf("%s\tundo\t%d\tredo\t%d\n",
               rc == 0 ? "moved" : "nothing", u, r);
        return rc == 0 ? 0 : 1;
    }

    if (!strcmp(verb, "history")) {
        int u = 0, r = 0;
        ss_history_depth(proj, &u, &r);
        printf("undo\t%d\nredo\t%d\n", u, r);
        return 0;
    }

    /* A note pinned to an instant. Nothing renders differently for one, which
     * is exactly the point: it is the only way to put something where a
     * problem is without changing the cut to say so. */
    if (!strcmp(verb, "mark")) {
        double at = -1;
        int colour = 0, i, n;
        const char *text = "";

        for (i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--at") && i + 1 < argc) at = atof(argv[++i]);
            else if (!strcmp(argv[i], "--text") && i + 1 < argc) text = argv[++i];
            else if (!strcmp(argv[i], "--colour") && i + 1 < argc) colour = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--color") && i + 1 < argc) colour = atoi(argv[++i]);
            else return die("mark: unknown option %s", argv[i]);
        }
        if (at < 0) return die("mark needs --at SECONDS");
        n = ss_timeline_mark(t, at, colour, text);
        if (n < 0) return die("no room for another marker");
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("%d\n", n);
        return 0;
    }

    if (!strcmp(verb, "unmark")) {
        int n = argc > 4 ? atoi(argv[4]) : -1;
        if (argc < 5) return die("unmark needs a marker number");
        if (ss_timeline_unmark(t, n) != 0) return die("no marker %d", n);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    /* clip, insert and overwrite differ only in what they do to what is
     * already on the track — the clip they build is the same one, so it is
     * built in one place. Three copies of the still/duration/probe reasoning
     * is three places for an insert to disagree with an add about how long a
     * photograph is. */
    if (!strcmp(verb, "clip") || !strcmp(verb, "insert")
        || !strcmp(verb, "overwrite")) {
        ss_clip c;
        ss_probe p;
        int inserting  = !strcmp(verb, "insert");
        int overwriting = !strcmp(verb, "overwrite");

        if (argc < 6) return die("%s wants PROJ TRACK FILE", verb);
        if (tl_pick(t, argv[4], NULL, &tr, NULL) != 0) return 1;
        ss_clip_reset(&c);
        snprintf(c.path, sizeof c.path, "%s", argv[5]);
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
            return die("bad option");

        /* An out point nobody gave defaults to the whole source, which is
         * what "add this clip" means before anyone has trimmed it. */
        c.src_in  = o.in;
        c.src_out = o.outp;
        {
            /* Asked before the picture probe, because a file with no video
             * stream fails that probe entirely and used to land here as a
             * five second clip no matter how long it actually was. */
            double md = ss_media_duration(c.path);
            if (md > 0 && c.src_out < 0 && !o.has_dur) {
                c.src_out = c.src_in + md;
                c.still = 0;
            }
        }
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

        /* ⚠ A PHOTOGRAPH ARRIVES DEVELOPED.
         *
         * The darkroom writes every change to `<photo>.synstudio` as it is
         * made, so a still dragged from the photo page onto the cut has its
         * work sitting beside it on disk — and a clip whose grade started at
         * the defaults showed the picture as the camera left it. That is
         * worse than an error: the shot looks like the develop never
         * happened, and the only clue is that it is exactly the original.
         *
         * A clip's grade IS an ss_develop, the same table the sidecar holds,
         * so the whole stack travels by assignment and every setting is still
         * a row in the inspector afterwards.
         *
         * ⚠ MASKS DO NOT TRAVEL. A mask is a local adjustment on the ss_edit,
         * not part of the develop stack, and a clip has no such list — so a
         * photograph with masks lands with everything except them.
         *
         * ⚠ And an untouched photograph must not be marked as graded: an
         * identity stack written to the project is a clip that claims a grade
         * it has not got, which is the difference between "no grade" and "a
         * grade that happens to do nothing" in every reader downstream. */
        if (!o.flat) {
            char side[4200];
            ss_edit e;
            ss_sidecar_path(c.path, side, sizeof side);
            if (path_exists(side) && ss_edit_load(&e, side) == 0
                && !ss_develop_is_identity(&e.dev)) {
                c.grade = e.dev;
                c.has_grade = 1;
            }
        }

        /* ── three-point editing ─────────────────────────────────────────
         *
         * INSERT makes room: everything at or after the point moves later by
         * the clip's length, and a shot the point lands inside is split so
         * its tail travels with the rest.
         *
         * ⚠ EVERY TRACK, not just this one. That is what an insert edit IS —
         * rippling one track would slide a shot off its own dialogue, and
         * this program has linked clips precisely because those belong
         * together. Somebody who wants one track to move alone wants
         * overwrite, or a move.
         *
         * OVERWRITE cuts a hole exactly the clip's length on THIS track and
         * drops the clip into it, which is the other half of what a source
         * monitor is for: mark in and out on the footage, put the playhead
         * where it goes, and the third point is decided by the first two. */
        if (inserting || overwriting) {
            double a = c.tl_in, len = ss_clip_length(&c), b;
            int k;

            if (len <= 0) return die("that clip has no length");
            b = a + len;
            if (inserting) {
                for (k = 0; k < t->ntracks; k++) {
                    int hit = ss_timeline_at(t, k, a);
                    /* Split first: a clip STRADDLING the point has to give up
                     * its tail, or the insert lands on top of it. `split`
                     * refuses a time that is not strictly inside, which is
                     * exactly the case where nothing needs splitting. */
                    if (hit >= 0) ss_timeline_split(t, k, hit, a);
                    ss_timeline_push(t, k, a, len);
                }
            } else {
                int hit = ss_timeline_at(t, tr, a);
                if (hit >= 0) ss_timeline_split(t, tr, hit, a);
                hit = ss_timeline_at(t, tr, b);
                if (hit >= 0) ss_timeline_split(t, tr, hit, b);
                /* Highest index first: removing one renumbers everything
                 * above it, which is the same rule the window's multi-delete
                 * had to learn. */
                for (k = t->track[tr].nclips - 1; k >= 0; k--) {
                    const ss_clip *e = &t->track[tr].clip[k];
                    double s0 = e->tl_in, e0 = s0 + ss_clip_length(e);
                    if (s0 >= a - 1e-6 && e0 <= b + 1e-6)
                        ss_timeline_remove(t, tr, k);
                }
            }
        }

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
            /* A caption typed at a shell cannot contain a real newline, so
             * \n in the argument is one. The same two bytes the project file
             * uses, so what is typed and what is stored agree. */
            ss_clip_set(&c, "text", argv[5]);
            if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
                return die("bad option");
            if (o.style && ss_title_style_apply(&c, o.style) != 0)
                return die("no style called %s — `timeline styles` lists them",
                           o.style);
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

    /* Restyling a title that already exists.
     *
     * `--style` at creation was the only way to reach the styles, which left
     * every title that arrived some other way — the window's Title button, a
     * subtitle import, a project written before the styles existed — unable to
     * use them at all. The import even says "the style is something to change
     * if the picture underneath wants it", and there was nothing to change it
     * with.
     *
     * It is a verb rather than a row in the clip table on purpose. A style
     * SETS the seven fields it names and every one of them is still a slider
     * afterwards, so it is a starting point and not a mode — the same bargain
     * a look strikes with a grade. And `ss_clip_set` has to have no effect
     * beyond the field it is handed, because the project READER shares it: a
     * `style` property would restyle a title, over the numbers someone had
     * since tuned, every time the file was opened.
     */
    if (!strcmp(verb, "style")) {
        ss_clip *c;

        if (argc < 7)
            return die("style wants PROJ TRACK CLIP NAME — "
                       "`timeline styles` lists them");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        c = &t->track[tr].clip[cl];
        /* A style is only ever text fields, so on anything else it would be a
         * command that reports success and changes nothing on screen. */
        if (c->kind != SS_CLIP_TITLE)
            return die("clip %d on track %d is not a title", cl, tr);
        if (ss_title_style_apply(c, argv[6]) != 0)
            return die("no style called %s — `timeline styles` lists them",
                       argv[6]);
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("style\t%s\n", argv[6]);
        return 0;
    }

    /* Track automation — a fader ridden against the picture.
     *
     * ⚠ Its keys are in TIMELINE seconds, unlike a clip's, which are relative
     * to the clip. A clip can be moved and its keys have to move with it; a
     * track cannot, and its fader is set against what is on screen. The verb
     * says so rather than leaving somebody to find out. */
    if (!strcmp(verb, "auto")) {
        const char *act = argc > 5 ? argv[5] : "list";
        ss_propkey k;
        int n, i;

        if (argc < 5) return die("auto wants PROJ TRACK add|list|remove|clear");
        if (tl_pick(t, argv[4], NULL, &tr, NULL) != 0) return 1;

        if (!strcmp(act, "add")) {
            int e = SS_EASE_LINEAR;
            if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
                return die("bad option");
            if (!o.has_value) return die("auto add wants --value dB");
            if (o.ease) {
                e = ss_ease_value(o.ease);
                if (e < 0) return die("ease is linear, in, out, inout or hold");
            }
            if (ss_track_key_add(t, tr, o.at, o.value, e) < 0)
                return die("cannot add the key (a track holds %d)", SS_MAX_PKEYS);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("keys\t%d\n", ss_track_key_count(t, tr));
            return 0;
        }
        if (!strcmp(act, "remove")) {
            if (argc < 7) return die("auto remove wants an index");
            if (ss_track_key_remove(t, tr, atoi(argv[6])) != 0)
                return die("no key %s", argv[6]);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("keys\t%d\n", ss_track_key_count(t, tr));
            return 0;
        }
        if (!strcmp(act, "clear")) {
            while (ss_track_key_count(t, tr) > 0)
                ss_track_key_remove(t, tr, 0);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("keys\t0\n");
            return 0;
        }
        if (!strcmp(act, "at")) {
            if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
                return die("bad option");
            printf("gain\t%.4f\n", ss_track_gain_at(t, tr, o.at));
            return 0;
        }
        if (!strcmp(act, "list")) {
            n = ss_track_key_count(t, tr);
            printf("keys\t%d\n", n);
            for (i = 0; i < n; i++)
                if (ss_track_key_at(t, tr, i, &k) == 0)
                    printf("key\t%d\t%.6f\t%.4f\t%s\n",
                           i, k.t, k.v, ss_ease_name(k.ease));
            return 0;
        }
        return die("auto takes add, list, at, remove or clear, not %s", act);
    }

    /* Linked audio and video.
     *
     * Routing separated the picture from the sound — a video clip's dialogue
     * plays whatever track it sits on — which is exactly what makes a link
     * necessary: without one, moving a shot leaves its sound where it was.
     *
     * The link lives in the ENGINE's move, trim and delete rather than here,
     * so a drag in the window and a `timeline move` from a script behave the
     * same way. */
    if (!strcmp(verb, "link")) {
        int tk[16], cl[16], n = 0, i, g;
        if (argc < 8) return die("link wants PROJ T C T C [T C ...]");
        for (i = 4; i + 1 < argc && n < 16; i += 2) {
            if (tl_pick(t, argv[i], argv[i + 1], &tk[n], &cl[n]) != 0) return 1;
            n++;
        }
        if (n < 2) return die("a link needs at least two clips");
        g = ss_timeline_link(t, tk, cl, n);
        if (g < 0) return die("cannot link those");
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("linked\t%d\ngroup\t%d\n", n, g);
        return 0;
    }

    if (!strcmp(verb, "unlink")) {
        int cl;
        if (argc < 6) return die("unlink wants PROJ TRACK CLIP");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        if (ss_timeline_unlink(t, tr, cl) != 0) return die("no such clip");
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("unlinked\t%d\t%d\n", tr, cl);
        return 0;
    }

    /* Versions.
     *
     * Undo is already the auto-save half of this — every save records the
     * state it left, so nothing is lost between them — but undo is a ring of
     * a hundred states and the oldest falls off the end. A version is a
     * document somebody DECIDED to keep, with a name they chose, that nothing
     * expires and no edit disturbs.
     *
     * ⚠ Restoring goes through the ordinary save path, so a restore is itself
     * undoable. A restore that could not be undone would be the one operation
     * in this program capable of losing work. */
    if (!strcmp(verb, "version")) {
        const char *act = argc > 4 ? argv[4] : "list";
        static ss_version v[SS_MAX_VERSIONS];
        char path[4400];
        int n, k;

        if (!strcmp(act, "save")) {
            char when[32];
            time_t now = time(NULL);
            struct tm tmv;
            const char *name = argc > 5 ? argv[5] : NULL;
            if (!name) return die("version save wants a NAME");
            localtime_r(&now, &tmv);
            strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
            n = ss_version_save(proj, name, when);
            if (n == -2) return die("a version name is letters, digits, dash, "
                                    "dot and underscore — it becomes a file");
            if (n != 0) return die("cannot write the version");
            printf("saved\t%s\n%s\n", name, when);
            return 0;
        }
        if (!strcmp(act, "restore")) {
            const char *name = argc > 5 ? argv[5] : NULL;
            ss_timeline was;
            FILE *fp;
            if (!name) return die("version restore wants a NAME");
            if (ss_version_path(proj, name, path, sizeof path) != 0)
                return die("no version called %s", name);
            fp = fopen(path, "r");
            if (!fp) return die("no version called %s", name);
            ss_timeline_reset(&was, 1920, 1080, 25.0);
            ss_timeline_read(&was, fp);
            fclose(fp);
            ss_timeline_stabdir(proj, was.stabdir, sizeof was.stabdir);
            /* Through tl_save, which is what makes it undoable. */
            if (tl_save(proj, &was) != 0) {
                ss_timeline_free(&was);
                return die("cannot write %s", proj);
            }
            printf("restored\t%s\n", name);
            ss_timeline_free(&was);
            return 0;
        }
        if (!strcmp(act, "list")) {
            n = ss_version_list(proj, v, SS_MAX_VERSIONS);
            printf("versions\t%d\n", n);
            for (k = 0; k < n; k++)
                printf("version\t%s\t%s\n", v[k].name, v[k].when);
            return 0;
        }
        return die("version takes save, list or restore, not %s", act);
    }

    /* Copy, paste, duplicate.
     *
     * The clipboard is a one-clip DOCUMENT, so what travels is everything the
     * project file knows about a clip — the grade, the parameter keys, the
     * effect stack, the sound chain and the retime — rather than the handful
     * of fields a bespoke copy would have remembered to carry.
     *
     * `paste --grade` is the other half of the same idea, and the one that
     * saves an afternoon: it takes ONLY the develop stack off the clipboard
     * and leaves the target's own timing, framing and sound alone. With
     * `--all` it does that to every clip on the track, which is what grading
     * a scene actually looks like. */
    if (!strcmp(verb, "copy")) {
        int cl;
        if (argc < 6) return die("copy wants PROJ TRACK CLIP");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        if (ss_clip_copy_out(&t->track[tr].clip[cl], t->w, t->h, t->fps) != 0)
            return die("cannot write the clipboard");
        printf("copied\t%d\t%d\n", tr, cl);
        return 0;
    }

    if (!strcmp(verb, "paste")) {
        ss_clip src;
        int cl = -1;

        if (argc < 5) return die("paste wants PROJ TRACK [CLIP]");
        if (tl_pick(t, argv[4], NULL, &tr, NULL) != 0) return 1;
        if (parse_opts(argc, argv, 5, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (ss_clip_copy_in(&src) != 0)
            return die("nothing has been copied yet");

        if (o.grade) {
            /* Onto EXISTING clips: the grade only. */
            int i, n = 0;
            if (!src.has_grade)
                return die("the copied clip has no grade on it");
            if (o.all) {
                for (i = 0; i < t->track[tr].nclips; i++) {
                    t->track[tr].clip[i].grade = src.grade;
                    t->track[tr].clip[i].has_grade = 1;
                    /* ⚠ A pasted grade replaces a MOVING one. Keys hold whole
                     * develop stacks, so leaving them would leave the clip
                     * being driven by the grade it had while claiming to wear
                     * the one just pasted. */
                    t->track[tr].clip[i].nkeys = 0;
                    n++;
                }
            } else {
                if (argc < 6) return die("paste --grade wants a CLIP, or --all");
                if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
                t->track[tr].clip[cl].grade = src.grade;
                t->track[tr].clip[cl].has_grade = 1;
                t->track[tr].clip[cl].nkeys = 0;
                n = 1;
            }
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("graded\t%d\n", n);
            return 0;
        }

        /* A whole clip, as a new one. */
        if (t->track[tr].type == SS_TRACK_VIDEO && src.kind != SS_CLIP_MEDIA) {
            /* fine: a title or a solid belongs on a video track */
        }
        if (o.has_at || o.has_dur) src.tl_in = o.at;
        cl = ss_timeline_add_clip(t, tr, &src);
        if (cl < 0) return die("cannot add the clip");
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("pasted\t%d\t%d\n", tr, cl);
        printf("at\t%.6f\n", src.tl_in);
        return 0;
    }

    if (!strcmp(verb, "duplicate")) {
        ss_clip src;
        int cl, nc;
        if (argc < 6) return die("duplicate wants PROJ TRACK CLIP");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
            return die("bad option");
        src = t->track[tr].clip[cl];
        /* Straight after itself unless told otherwise, which is what
         * duplicating a clip is for. */
        src.tl_in = o.has_at ? o.at
                             : t->track[tr].clip[cl].tl_in
                               + ss_clip_length(&t->track[tr].clip[cl]);
        nc = ss_timeline_add_clip(t, tr, &src);
        if (nc < 0) return die("cannot add the clip");
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("duplicated\t%d\t%d\n", tr, nc);
        printf("at\t%.6f\n", src.tl_in);
        return 0;
    }

    /* Match one clip to another.
     *
     * The reference is measured WITH its grade on and the target WITHOUT:
     * what is being matched is the picture somebody has already made, and
     * what is being fitted is the stack that will make this shot look like
     * it. Both are read at the middle of the clip — the frame that
     * represents it, rather than a first frame that might still be a fade. */
    if (!strcmp(verb, "match")) {
        ss_clip *c;
        const ss_clip *rc2;
        ss_image tgt, ref;
        ss_match_report rep;
        int cl, rtr, rcl;
        double tt, rt;

        if (argc < 8) return die("match wants PROJ TRACK CLIP REFTRACK REFCLIP");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        if (tl_pick(t, argv[6], argv[7], &rtr, &rcl) != 0) return 1;
        c   = &t->track[tr].clip[cl];
        rc2 = &t->track[rtr].clip[rcl];
        if (tr == rtr && cl == rcl) return die("a clip already matches itself");
        if (c->kind != SS_CLIP_MEDIA || rc2->kind != SS_CLIP_MEDIA)
            return die("both have to be footage — a title has no shot in it");

        tt = ss_clip_source_at(c,   ss_clip_length(c)   / 2.0, t->fps);
        rt = ss_clip_source_at(rc2, ss_clip_length(rc2) / 2.0, t->fps);

        /* 400 pixels. A fit is dozens of renders and the numbers it fits are
         * averages over the whole frame, which do not change for having been
         * measured at 400 instead of 4000. */
        if (ss_load_frame(rc2->path, rt, &ref, 400) != 0)
            return die("cannot read %s", rc2->path);
        if (rc2->has_grade) {
            ss_edit re;
            memset(&re, 0, sizeof re);
            re.dev = rc2->grade;
            ss_edit_apply(&ref, &re);
        }
        if (ss_load_frame(c->path, tt, &tgt, 400) != 0) {
            ss_image_free(&ref);
            return die("cannot read %s", c->path);
        }

        if (ss_shot_match(&ref, &tgt, &c->grade, &rep) != 0) {
            ss_image_free(&ref); ss_image_free(&tgt);
            return die("cannot match");
        }
        ss_image_free(&ref);
        ss_image_free(&tgt);
        c->has_grade = 1;

        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        {
            char v[64];
            printf("matched\t%d\t%d\tto\t%d\t%d\n", tr, cl, rtr, rcl);
            ss_develop_get(&c->grade, "exposure", v, sizeof v); printf("exposure\t%s\n", v);
            ss_develop_get(&c->grade, "contrast", v, sizeof v); printf("contrast\t%s\n", v);
            ss_develop_get(&c->grade, "temp", v, sizeof v);     printf("temp\t%s\n", v);
            ss_develop_get(&c->grade, "tint", v, sizeof v);     printf("tint\t%s\n", v);
            printf("luma\t%.4f\t%.4f\n", rep.want_luma, rep.got_luma);
            printf("spread\t%.4f\t%.4f\n", rep.want_spread, rep.got_spread);
        }
        return 0;
    }

    /* Pass one of the stabiliser, run once and left on disk.
     *
     * A verb rather than a property because it is WORK: it watches the whole
     * clip. The property it sets — `stab` — only says that the analysis is
     * there and wanted, and `--off` puts it back without throwing the
     * measurement away, so turning it on again costs nothing. */
    if (!strcmp(verb, "stabilise") || !strcmp(verb, "stabilize")) {
        char trf[2048];
        ss_clip *c;
        int cl;

        if (argc < 6) return die("%s wants PROJ TRACK CLIP", verb);
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        c = &t->track[tr].clip[cl];
        if (parse_opts(argc, argv, 6, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (c->kind != SS_CLIP_MEDIA)
            return die("a %s has nothing to stabilise",
                       c->kind == SS_CLIP_TITLE ? "title" : "colour");

        if (o.off) {
            c->stab = 0;
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("stab\toff\n");
            return 0;
        }

        if (mkdir(t->stabdir, 0777) != 0 && errno != EEXIST)
            return die("cannot make %s", t->stabdir);
        snprintf(trf, sizeof trf, "%s/stab_%d_%d.trf", t->stabdir, tr, cl);

        /* Said before it starts, because it is the one command here that
         * takes as long as a render and gives no sign until it is done. */
        fprintf(stderr, "watching %.1f seconds of %s\n",
                c->src_out - c->src_in, c->path);
        if (ss_stabilise(c->path, c->src_in, c->src_out, trf,
                         o.has_value ? (int)o.value : 5) != 0)
            return die("cannot analyse %s  (is this ffmpeg built with "
                       "libvidstab?)", c->path);

        c->stab = 1;
        if (o.has_dur) c->stab_smooth = (float)o.dur;
        if (o.size > 0) c->stab_zoom = (float)o.size;
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("stab\ton\n");
        printf("analysis\t%s\n", trf);
        printf("smoothing\t%.0f\n", (double)c->stab_smooth);
        return 0;
    }

    /* Subtitles in and out. A cue is a title clip, so `import` is the only
     * thing here that is new — after it, every command that edits a title
     * edits a caption, and burning them in is what an export already does. */
    if (!strcmp(verb, "subs")) {
        const char *act;
        char err[256];
        int n;

        if (argc < 7)
            return die("subs wants PROJ TRACK import|export FILE");
        if (tl_pick(t, argv[4], NULL, &tr, NULL) != 0) return 1;
        act = argv[5];

        if (!strcmp(act, "import")) {
            n = ss_subs_import(t, argv[6], tr, err, sizeof err);
            if (n < 0) return die("%s", err);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("cues\t%d\n", n);
            printf("track\t%d\n", tr);
            /* Said out loud because it is the thing a person is about to
             * wonder: these are ordinary titles now, and the style is
             * something to change if the picture underneath wants it. */
            printf("style\tsubtitle\n");
            return 0;
        }
        if (!strcmp(act, "export")) {
            n = ss_subs_export(t, tr, argv[6]);
            if (n < 0) return die("cannot write %s", argv[6]);
            printf("cues\t%d\n", n);
            printf("out\t%s\n", argv[6]);
            return 0;
        }
        return die("subs takes import or export, not %s", act);
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
        const char *one = (argc > 6 && argv[6][0] != '-') ? argv[6] : NULL;
        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                        argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        /* `--at S` asks what the clip looks like S seconds INTO ITSELF, which
         * for a keyed property is not what the static field says. The window
         * reads this: an inspector parked on a moving clip has to show the
         * value at the playhead, and the engine is the only thing that knows
         * how to work that out. */
        if (parse_opts(argc, argv, one ? 7 : 6, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (one) {
            if (clip_get_at(&t->track[tr].clip[cl], one, o.at, buf, sizeof buf) != 0)
                return die("no such clip property: %s", one);
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
                clip_get_at(c, f.key, o.at, buf, sizeof buf);
                printf("%s\t%s\n", f.key, buf);
            }
            /* Whether the noise model this clip names is on THIS machine.
             * A model is somebody else's file and travels no better than a
             * LUT does — so the project keeps the name and the window shows
             * the row as unfound, the way the font field goes red for a
             * family fontconfig has never heard of. */
            if (*c->nr_model) {
                char found[1024];
                printf("nr.model.found\t%d\n",
                       ss_rnn_resolve(c->nr_model, found, sizeof found) == 0);
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
        if (argc < 7) return die("grade wants PROJ TRACK CLIP KEY=VALUE... or --look NAME");
        if (tl_pick(t, argv[4], argv[5], &tr, &cl) != 0) return 1;
        if (!t->track[tr].clip[cl].has_grade) {
            ss_develop_reset(&t->track[tr].clip[cl].grade);
            t->track[tr].clip[cl].has_grade = 1;
        }
        for (i = 6; i < argc; i++) {
            char *eq;
            /* A look on a clip is the same look as on a photograph, and it
             * lands the same way: it SETS the fields it names and leaves the
             * grade already on the shot alone. */
            if (!strcmp(argv[i], "--look") && i + 1 < argc) {
                const ss_look *k = ss_look_find(argv[++i]);
                int bad;
                if (!k) return die("no such look: %s  (try `synstudio look list`)",
                                   argv[i]);
                bad = ss_look_apply(k, &t->track[tr].clip[cl].grade);
                if (bad < 0) return die("cannot read %s", k->path);
                if (bad) fprintf(stderr, "synstudio: %d setting%s in %s this "
                                         "build does not know\n",
                                 bad, bad == 1 ? "" : "s", k->name);
                continue;
            }
            eq = strchr(argv[i], '=');
            if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
            *eq = '\0';
            if (apply_set(&t->track[tr].clip[cl].grade, argv[i], eq + 1) != 0) return 1;
        }
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        return 0;
    }

    /* Keyframes. A key holds a WHOLE develop stack at one instant in the
     * clip, so the grade between two of them is the engine interpolating its
     * own settings rather than the renderer guessing. */
    if (!strcmp(verb, "key")) {
        const char *sub = argc > 6 ? argv[6] : NULL;
        ss_clip *c;
        int i;

        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                       argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        c = &t->track[tr].clip[cl];
        if (!sub) return die("key wants add|list|set|remove");

        if (!strcmp(sub, "list")) {
            printf("steps\t%d\n", ss_clip_grade_steps(c));
            for (i = 0; i < c->nkeys; i++)
                printf("key\t%d\t%.6f\n", i, c->key[i].t);
            return 0;
        }

        if (!strcmp(sub, "add")) {
            ss_develop d;
            int n;
            if (parse_opts(argc, argv, 7, &o, &rest, &nrest) != 0)
                return die("bad option");
            /* Seeded from whatever the grade already is at that instant, so
             * dropping a key mid-ramp does not change the picture — it just
             * pins it. A key that altered the shot the moment it was added
             * would make keying anything a guessing game. */
            if (c->nkeys > 0 || c->has_grade) {
                int s = ss_clip_grade_step_at(c, o.at);
                if (!ss_clip_grade_step(c, s, &d)) ss_develop_reset(&d);
            } else {
                ss_develop_reset(&d);
            }
            for (i = 0; i < o.nsets; i++)
                if (apply_set(&d, o.set_key[i], o.set_val[i]) != 0) return 1;
            n = ss_clip_key_add(c, o.at, &d);
            if (n < 0) return die("no room for another keyframe (limit %d)",
                                  SS_MAX_KEYS);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("%d\n", n);
            return 0;
        }

        if (!strcmp(sub, "set")) {
            int n;
            if (argc < 9) return die("key set wants PROJ T C set N KEY=VALUE...");
            n = atoi(argv[7]);
            if (n < 0 || n >= c->nkeys) return die("no keyframe %d", n);
            for (i = 8; i < argc; i++) {
                char *eq = strchr(argv[i], '=');
                if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
                *eq = '\0';
                if (apply_set(&c->key[n].dev, argv[i], eq + 1) != 0) return 1;
            }
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            return 0;
        }

        if (!strcmp(sub, "remove")) {
            int n = argc > 7 ? atoi(argv[7]) : -1;
            if (ss_clip_key_remove(c, n) != 0) return die("no keyframe %d", n);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            return 0;
        }

        return die("key: unknown subcommand %s — try add, list, set, remove", sub);
    }

    /* The effect stack on one clip.
     *
     * Order matters and is the reason this is a list: a blur under a glow and
     * a glow under a blur are different pictures, and `move` is how you say
     * which one you meant. */
    if (!strcmp(verb, "fx")) {
        const char *sub = argc > 6 ? argv[6] : NULL;
        ss_clip *c;
        int i, n;

        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                       argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        c = &t->track[tr].clip[cl];
        if (!sub) return die("fx wants add|list|set|remove|move");

        if (!strcmp(sub, "list")) {
            for (i = 0; i < c->nfx; i++) {
                const ss_fx *r = ss_fx_find(c->fx[i].name);
                int q;
                /* An effect whose recipe is not installed says so rather than
                 * being quietly left out of the list — it is still IN the
                 * document, and it will come back the moment the file it
                 * needs is put back. */
                printf("%d\t%s\t%s\t%d", i, c->fx[i].name,
                       r ? r->label : "(missing)", c->fx[i].on);
                for (q = 0; r && q < r->nparam; q++)
                    printf("\t%s=%g", r->param[q].key, c->fx[i].val[q]);
                printf("\n");
            }
            return 0;
        }

        if (!strcmp(sub, "add")) {
            const char *name = argc > 7 ? argv[7] : NULL;
            if (!name) return die("fx add wants an effect  "
                                  "(try `synstudio fx list`)");
            if (!ss_fx_find(name))
                return die("no such effect: %s  (try `synstudio fx list`)",
                           name);
            /* Rendered through before it is allowed onto the clip. An effect
             * that cannot build a graph is one an export would die on, and
             * finding that out here costs one frame of a 64x64 grey. */
            if (fx_render_check(ss_fx_find(name)) != 0) return 1;
            n = ss_clip_fx_add(c, name, -1);
            if (n == -2) return die("a clip carries at most %d effects",
                                    SS_MAX_FX);
            if (n < 0) return die("cannot add %s", name);
            for (i = 8; i < argc; i++) {
                char *eq = strchr(argv[i], '=');
                if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
                *eq = '\0';
                if (ss_clip_fx_set(c, n, argv[i], atof(eq + 1)) != 0)
                    return die("%s has no %s  (try `synstudio fx show %s`)",
                               name, argv[i], name);
            }
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("%d\n", n);
            return 0;
        }

        if (!strcmp(sub, "set")) {
            if (argc < 9) return die("fx set wants PROJ T C set N KEY=VALUE...");
            n = atoi(argv[7]);
            if (n < 0 || n >= c->nfx) return die("no effect %d on this clip", n);
            for (i = 8; i < argc; i++) {
                char *eq = strchr(argv[i], '=');
                if (!eq) return die("expected KEY=VALUE, got: %s", argv[i]);
                *eq = '\0';
                if (ss_clip_fx_set(c, n, argv[i], atof(eq + 1)) != 0)
                    return die("%s has no %s", c->fx[n].name, argv[i]);
            }
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            return 0;
        }

        if (!strcmp(sub, "remove")) {
            n = argc > 7 ? atoi(argv[7]) : -1;
            if (ss_clip_fx_remove(c, n) != 0)
                return die("no effect %d on this clip", n);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            return 0;
        }

        if (!strcmp(sub, "move")) {
            if (argc < 9) return die("fx move wants an effect and where to");
            if (ss_clip_fx_move(c, atoi(argv[7]), atoi(argv[8])) != 0)
                return die("cannot move %s to %s", argv[7], argv[8]);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            return 0;
        }

        return die("fx: unknown subcommand %s — try add, list, set, remove, "
                   "move", sub);
    }

    /* A transition on the cut under the playhead, in one command.
     *
     * The kind and the length are properties like any other and `set` writes
     * them — but a transition also needs the two clips to OVERLAP, and that is
     * an edit, not a property. Doing it by hand is three commands and an
     * arithmetic mistake waiting to happen, which is why nobody would.
     *
     * Handles first: if the outgoing clip has unused source left, its tail is
     * extended and nothing else on the timeline moves. That is what an editor
     * expects, and it is only possible when the shot was trimmed. Otherwise
     * the incoming clip and everything after it RIPPLE back, which shortens
     * the programme by the length of the transition — the price of a
     * transition you have no handles for, and it says which it did. */
    if (!strcmp(verb, "transition")) {
        ss_track *k;
        ss_clip *c, *a = NULL;
        int ai = -1, i, kind = SS_TRANS_DISSOLVE;
        double dur = 1.0, need, over;
        const char *how = "overlap";
        const char *cliparg = (argc > 5 && argv[5][0] != '-') ? argv[5] : NULL;

        if (parse_opts(argc, argv, cliparg ? 6 : 5, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (o.format && (kind = ss_trans_value(o.format)) < 0)
            return die("no such transition: %s  (try `synstudio timeline "
                       "transitions`)", o.format);
        if (o.has_dur) dur = o.dur;
        if (dur <= 0) return die("a transition needs a length");

        /* With no clip named, the cut is the one under the playhead — the
         * clip whose start is nearest to it. Asking tl_pick for a clip it was
         * not given would print its own complaint before this one got a
         * chance to do the right thing. */
        if (cliparg) {
            if (tl_pick(t, argc > 4 ? argv[4] : NULL, cliparg, &tr, &cl) != 0)
                return 1;
        } else {
            if (tl_pick(t, argc > 4 ? argv[4] : NULL, NULL, &tr, NULL) != 0)
                return 1;
            cl = -1;
        }
        k = &t->track[tr];
        if (cl < 0) {
            double best = -1;
            for (i = 0; i < k->nclips; i++) {
                double d = k->clip[i].tl_in - o.at;
                if (d < 0) d = -d;
                if (best < 0 || d < best) { best = d; cl = i; }
            }
            if (cl < 0) return die("no clips on track %d", tr);
        }
        c = &k->clip[cl];

        /* The clip this one comes out of: the one ending nearest to where it
         * starts, without starting after it. A gap is allowed and is closed
         * along with everything else. */
        for (i = 0; i < k->nclips; i++) {
            double e = k->clip[i].tl_in + ss_clip_length(&k->clip[i]);
            if (i == cl || ss_clip_length(&k->clip[i]) <= 0) continue;
            if (k->clip[i].tl_in > c->tl_in - 1e-9) continue;
            if (ai < 0 || e > k->clip[ai].tl_in + ss_clip_length(&k->clip[ai]))
                ai = i;
        }
        if (ai >= 0) a = &k->clip[ai];

        over = a ? a->tl_in + ss_clip_length(a) - c->tl_in : 0.0;
        if (dur > ss_clip_length(c)) dur = ss_clip_length(c);
        need = dur - over;

        if (a && need > 1e-6) {
            /* A handle is source the clip is not using. Asked of the file
             * rather than assumed: a clip that reaches the end of its source
             * has none, and pulling its out point past that is a freeze frame
             * nobody asked for. */
            double srclen = a->still ? 0.0 : ss_media_duration(a->path);
            double sp = a->speed > 0 ? a->speed : 1.0;
            double handle = a->still ? need : (srclen - a->src_out) / sp;
            if (handle >= need - 1e-6) {
                a->src_out += need * sp;
                how = "handles";
            } else {
                for (i = 0; i < k->nclips; i++)
                    if (k->clip[i].tl_in > c->tl_in - 1e-9)
                        k->clip[i].tl_in -= need;
                how = "ripple";
            }
        } else if (!a) {
            how = "no partner";
        }

        c->trans = kind;
        c->trans_dur = dur;
        if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
        printf("%d\t%s\t%.6f\t%s\n", cl, ss_trans_name(kind), dur, how);
        return 0;
    }

    /* Parameter keys: everything about a clip that is NOT colour.
     *
     * A grade key pins a whole develop stack because colour has to be baked;
     * this pins one number, and the export hands ffmpeg an expression instead
     * of forty-eight files. Which properties can be keyed is a column in the
     * clip property table, so `timeline keys` already says. */
    if (!strcmp(verb, "anim")) {
        const char *sub = argc > 6 ? argv[6] : NULL;
        const char *key = argc > 7 ? argv[7] : NULL;
        ss_clip *c;
        int i;

        if (tl_pick(t, argc > 4 ? argv[4] : NULL,
                       argc > 5 ? argv[5] : NULL, &tr, &cl) != 0) return 1;
        c = &t->track[tr].clip[cl];
        if (!sub) return die("anim wants add|list|set|move|curve|remove|clear|at");

        if (!strcmp(sub, "list")) {
            ss_clip_info f;
            for (i = 0; ss_clip_describe(i, &f); i++) {
                int n, k;
                if (key && strcmp(key, f.key)) continue;
                n = ss_clip_prop_nkeys(c, f.key);
                for (k = 0; k < n; k++) {
                    ss_propkey pk;
                    ss_clip_prop_key(c, f.key, k, &pk);
                    printf("%s\t%d\t%.6f\t%.6f\t%s\n",
                           f.key, k, pk.t, pk.v, ss_ease_name(pk.ease));
                }
            }
            return 0;
        }

        if (!strcmp(sub, "at")) {
            char buf[512];
            if (!key) return die("anim at wants a property");
            if (ss_clip_get(c, key, buf, sizeof buf) != 0)
                return die("no such clip property: %s", key);
            if (parse_opts(argc, argv, 8, &o, &rest, &nrest) != 0)
                return die("bad option");
            printf("%.6f\n", ss_clip_prop_at(c, key, o.at));
            return 0;
        }

        if (!strcmp(sub, "add") || !strcmp(sub, "set")) {
            int ease = SS_EASE_LINEAR, n;
            double v;
            if (!key) return die("anim %s wants a property "
                                 "(try `synstudio timeline keys`)", sub);
            if (!ss_clip_prop_animatable(key))
                return die("%s cannot be keyed — `timeline keys` marks the "
                           "ones that can", key);
            if (parse_opts(argc, argv, 8, &o, &rest, &nrest) != 0)
                return die("bad option");
            if (o.ease && (ease = ss_ease_value(o.ease)) < 0)
                return die("no such ease: %s — linear, in, out, inout, hold",
                           o.ease);
            /* Seeded from whatever the property already is at that instant,
             * for the same reason a grade key is: dropping a key must pin the
             * value, never change it. */
            v = o.has_value ? o.value : ss_clip_prop_at(c, key, o.at);
            n = ss_clip_prop_add(c, key, o.at, v, ease);
            if (n < 0) return die("no room for another key on this clip "
                                  "(limit %d)", SS_MAX_PKEYS);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            printf("%d\n", n);
            return 0;
        }

        /* One key, moved — a drag in the curve editor is ONE edit.
         *
         * Remove-then-add from the window would work and would be two
         * commands: two processes, two writes, and two steps to undo for a
         * gesture that was one. It would also be two chances for the second
         * half to be dropped, and a key that vanished on a drag reads as the
         * editor eating it.
         *
         * ⚠ Fields not named KEEP what they were: a drag along the time axis
         * must not reset an ease somebody chose. */
        if (!strcmp(sub, "move")) {
            ss_propkey pk;
            int n = argc > 8 ? atoi(argv[8]) : -1, ease, made;
            double at, v;

            if (!key) return die("anim move wants a property");
            if (argc <= 8) return die("anim move wants a key index");
            /* ⚠ ss_clip_prop_key returns 1 on SUCCESS, unlike almost
             * everything else in this header — a plain `!= 0` reads every
             * key that IS there as one that is not. */
            if (!ss_clip_prop_key(c, key, n, &pk))
                return die("%s has no key %d", key, n);
            if (parse_opts(argc, argv, 9, &o, &rest, &nrest) != 0)
                return die("bad option");
            ease = pk.ease;
            if (o.ease && (ease = ss_ease_value(o.ease)) < 0)
                return die("no such ease: %s — linear, in, out, inout, hold",
                           o.ease);
            at = o.has_at    ? o.at    : pk.t;
            v  = o.has_value ? o.value : pk.v;

            if (ss_clip_prop_remove(c, key, n) != 0)
                return die("%s has no key %d", key, n);
            made = ss_clip_prop_add(c, key, at, v, ease);
            if (made < 0) {
                /* Put it back rather than leave the clip a key short: a
                 * failed move must change nothing. */
                ss_clip_prop_add(c, key, pk.t, pk.v, pk.ease);
                return die("cannot move that key");
            }
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            /* ⚠ The index it landed at, which is NOT the one it left: keys
             * are held in time order, so a drag past its neighbour renumbers
             * both. The window follows this to keep the same key selected. */
            printf("%d\n", made);
            return 0;
        }

        /* The curve itself, SAMPLED BY THE ENGINE.
         *
         * The window draws this and nothing else. Interpolating in QML would
         * be a second implementation of the five eases — and the whole reason
         * `ss_clip_prop_at` exists is that the monitor and the export must
         * agree about a keyed property to the frame. A curve drawn from a
         * second opinion is a picture of something nothing renders. */
        if (!strcmp(sub, "curve")) {
            int n, i2;
            double len = ss_clip_length(c);
            if (!key) return die("anim curve wants a property");
            if (!ss_clip_prop_animatable(key))
                return die("%s cannot be keyed — `timeline keys` marks the "
                           "ones that can", key);
            if (parse_opts(argc, argv, 8, &o, &rest, &nrest) != 0)
                return die("bad option");
            n = o.count > 1 ? o.count : 200;
            if (n > 4000) n = 4000;
            if (len <= 0) return die("that clip has no length");
            for (i2 = 0; i2 < n; i2++) {
                double tt = len * (double)i2 / (double)(n - 1);
                printf("%.6f\t%.6f\n", tt, ss_clip_prop_at(c, key, tt));
            }
            return 0;
        }

        if (!strcmp(sub, "remove")) {
            int n = argc > 8 ? atoi(argv[8]) : -1;
            if (!key) return die("anim remove wants a property");
            if (argc <= 8) return die("anim remove wants a key index "
                                      "(or use `clear`)");
            if (ss_clip_prop_remove(c, key, n) != 0)
                return die("%s has no key %d", key, n);
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            return 0;
        }

        if (!strcmp(sub, "clear")) {
            if (key) {
                if (ss_clip_prop_remove(c, key, -1) != 0)
                    return die("%s has no keys", key);
            } else {
                c->npkeys = 0;
            }
            if (tl_save(proj, t) != 0) return die("cannot write %s", proj);
            return 0;
        }

        return die("anim: unknown subcommand %s — try add, list, set, remove, "
                   "clear, at", sub);
    }

    /* One composited frame — the program monitor's whole data source. */
    if (!strcmp(verb, "frame")) {
        char dir[] = "/tmp/synstudio-tl-XXXXXX";
        char **av;
        int ac, rc;

        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0) return die("bad option");
        if (!o.out) return die("frame needs --out");
        if (!mkdtemp(dir)) return die("cannot make a scratch directory");
        if (ss_timeline_bake(t, dir, o.at) < 0) {
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

    /* A scope of the CUT, not of a file: the frame is composited first and
     * then measured, so what the waveform describes is the picture that will
     * be delivered — grade, effects, titles, transitions and all. */
    if (!strcmp(verb, "scope")) {
        char dir[] = "/tmp/synstudio-tl-XXXXXX";
        char fr[4200], **av;
        int ac, rc, kind = SS_SCOPE_WAVEFORM, sw, sh;
        ss_image im, sc;

        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0) return die("bad option");
        if (!o.out) return die("scope needs --out");
        if (o.format) {
            kind = ss_scope_value(o.format);
            if (kind < 0) return die("scope --kind takes waveform, parade or vector");
        }
        if (!mkdtemp(dir)) return die("cannot make a scratch directory");
        snprintf(fr, sizeof fr, "%s/frame.png", dir);
        if (ss_timeline_bake(t, dir, o.at) < 0) {
            rmdir(dir); return die("cannot write the grade LUTs");
        }
        /* Measured at a WORKING size rather than the project's: a scope of a
         * 4K frame counts eight million pixels to draw half a million, and
         * the shape of a waveform does not change for having been sampled. */
        ac = ss_timeline_frame(t, o.at, fr, dir, o.size > 0 ? o.size : 960, &av);
        if (ac < 0) { ss_timeline_unbake(t, dir); rmdir(dir);
                      return die("cannot build the preview graph"); }
        rc = tl_run(av, ac, 0);
        ss_timeline_unbake(t, dir);
        if (rc != 0) { remove(fr); rmdir(dir); return die("cannot render the frame"); }

        if (ss_load(fr, &im, 0) != 0) {
            remove(fr); rmdir(dir);
            return die("cannot read the frame back");
        }
        remove(fr);
        rmdir(dir);

        sw = 512;
        sh = kind == SS_SCOPE_VECTOR ? sw : sw / 2;
        rc = ss_scope_render(&im, kind, sw, sh, &sc);
        ss_image_free(&im);
        if (rc != 0) return die("cannot build the scope");
        rc = ss_save(o.out, &sc, 95, 8);
        printf("out\t%s\nkind\t%s\nat\t%.3f\n", o.out, ss_scope_name(kind), o.at);
        ss_image_free(&sc);
        return rc == 0 ? 0 : die("cannot write %s", o.out);
    }

    if (!strcmp(verb, "export")) {
        char dir[] = "/tmp/synstudio-lut-XXXXXX";
        char **av;
        int ac, rc;

        int burn = 0;

        if (parse_opts(argc, argv, 4, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (!o.out) return die("export needs --out");
        if (o.burn) {
            burn = ss_burn_value(o.burn);
            if (burn < 0)
                return die("--burn takes timecode, name, both or off");
        }
        /* Said BEFORE the encode, not after it. A subtitle file that is not
         * there, or a preview that was never going to carry one, is five
         * minutes of render either way — and finding out at the end is how a
         * person loses an evening. */
        if (o.mark) {
            FILE *mf = fopen(o.mark, "r");
            if (!mf) return die("cannot read %s", o.mark);
            fclose(mf);
            if (o.preview)
                fprintf(stderr, "a preview carries no watermark — "
                                "--watermark is ignored here\n");
        }
        if (o.subs) {
            FILE *sf = fopen(o.subs, "r");
            if (!sf) return die("cannot read %s", o.subs);
            fclose(sf);
            if (o.preview)
                fprintf(stderr, "a preview carries no subtitle stream — "
                                "--subs is ignored here\n");
        }
        warn_missing_fx(t);
        if (!mkdtemp(dir)) return die("cannot make a scratch directory");

        /* The .cube per graded clip and the text file per title, written
         * before the graph is built so every path it names exists by the
         * time ffmpeg opens it. */
        if (ss_timeline_bake(t, dir, -1.0) < 0) {
            rmdir(dir);
            return die("cannot write the grade LUTs");
        }

        tl_fill_audio(t);
        {
            const ss_tl_format *f = ss_timeline_format(o.format, o.out);
            /* A preset is a SIZE and a frame rate, applied by rendering the
             * whole composite at them. Every clip, the base, the titles and
             * the transitions are built from the project's own dimensions, so
             * changing those changes all of them together and nothing has to
             * be scaled afterwards — which is why this is a copy of the
             * document rather than a filter on the end of the graph.
             *
             * A shallow copy on purpose: the clip arrays are shared with `t`
             * and read-only from here, and the copy is never freed. */
            ss_timeline tp;
            const ss_timeline *rt = t;
            if (o.preset) {
                const ss_tl_preset *pr = ss_timeline_preset(o.preset);
                if (!pr) { ss_timeline_unbake(t, dir); rmdir(dir);
                           return die("no preset called %s  (try `timeline "
                                      "presets`)", o.preset); }
                tp = *t;
                tp.w = pr->w; tp.h = pr->h;
                if (pr->fps > 0) tp.fps = pr->fps;
                rt = &tp;
            }
            if (!f) { ss_timeline_unbake(t, dir); rmdir(dir);
                      return die("no such format: %s  (try `timeline formats`)",
                                 o.format); }
            if (o.subs && !o.preview && !f->scodec) {
                ss_timeline_unbake(t, dir); rmdir(dir);
                return die("%s cannot carry a subtitle stream — burn them in "
                           "with `timeline subs`, or deliver as mkv", f->name);
            }
            ac = ss_timeline_ffmpeg(rt, o.out, dir, o.preview, f, o.subs,
                                    burn, o.mark, &av);
        }
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
/* The waveform's data. One line per bucket: peak, then RMS, both 0..1.
 *
 * Tab separated and one record per line like everything else here, so a
 * waveform can be eyeballed in a terminal — which is also how the test checks
 * that silence reads as silence and a tone does not. */
static int cmd_peaks(const char *path, const opts *o)
{
    int n = o->count > 0 ? o->count : 400;
    float *peak, *rms;
    double in = o->in, out = o->outp;
    ss_probe p;
    int i, rc;

    if (n > 20000) return die("--count is capped at 20000");

    if (out < 0) {
        /* No duration means nothing to have audio IN. A photograph reaching
         * here is the ordinary case, not a mistake worth a message — it gets
         * the same "no audio" answer as a silent movie. */
        /* ss_media_duration, not the picture probe: a music bed has no video
         * stream and ss_probe_file fails on it outright. */
        out = ss_media_duration(path);
        if (out <= 0) {
            if (ss_probe_file(path, &p) != 0 || p.duration <= 0) return 100;
            out = p.duration;
        }
    }
    if (out <= in) return die("out point is not after the in point");

    peak = calloc((size_t)n, sizeof *peak);
    rms  = calloc((size_t)n, sizeof *rms);
    if (!peak || !rms) { free(peak); free(rms); return die("out of memory"); }

    rc = ss_peaks(path, in, out, n, peak, rms);
    if (rc != 0) {
        free(peak); free(rms);
        /* Not a failure message: a photograph has no audio and that is the
         * answer, not an error. The exit status says so for a script. */
        return 100;
    }

    for (i = 0; i < n; i++)
        printf("%.5f\t%.5f\n", (double)peak[i], (double)rms[i]);

    free(peak); free(rms);
    return 0;
}

/* One file, one answer, from ffmpeg rather than from the file's name.
 *
 * `browse` classifies a whole directory and has to do it by extension, or
 * opening a folder would cost a process per row. This is the other half: a
 * file somebody hands over on purpose — dropped on the timeline, typed on the
 * command line — gets asked properly, so a format that is on no list of ours
 * still lands on the right kind of track. A `.syntl` is answered from the
 * name, because a project is ours and not something ffmpeg has an opinion
 * about.
 */
static int cmd_kind(const char *path)
{
    const char *dot = strrchr(path, '.');
    int k;

    if (dot && !strcasecmp(dot + 1, "syntl")) { puts("project"); return 0; }

    k = ss_media_kind(path);
    switch (k) {
    case SS_KIND_IMAGE: puts("image"); return 0;
    case SS_KIND_VIDEO: puts("video"); return 0;
    case SS_KIND_AUDIO: puts("audio"); return 0;
    default:            puts("none");  return 1;
    }
}

/* Line-buffered on purpose: the window reads these while the take is running,
 * and a meter that arrives in one lump at the end is not a meter. */
static void rec_print(double t, double db, void *user)
{
    (void)user;
    printf("level\t%.2f\t%.1f\n", t, db);
    fflush(stdout);
}

static int cmd_browse(int argc, char **argv)
{
    static const char *names[] = { "up", "dir", "image", "video", "audio",
                                   "project", "look" };
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

    /* ⚠ DISABLE_MANGOHUD=1, AND IT IS NOT COSMETIC — IT IS WHY THE WINDOW
     * OPENS AT ALL ON AMD.
     *
     * The session exports MANGOHUD=1 so a game gets the overlay without a
     * per-game wrapper, and MangoHud's Vulkan manifest declares
     * "enable_environment": { "MANGOHUD": "1" } — so that one variable loads
     * VK_LAYER_MANGOHUD_overlay into EVERY Vulkan client in the session. This
     * window is one, and not by choice: constructing a QML MediaPlayer builds
     * a QMediaPlayer, whose ffmpeg backend asks libavutil for a Vulkan
     * hardware device before it has been given a file to play.
     *
     * On an AMD Renoir laptop that segfaults quickshell before a frame is
     * drawn, inside MangoHud's own vkCreateDevice hook:
     *
     *     #2  libMangoHud.so
     *     #5  vkCreateDevice
     *     #8  av_hwdevice_ctx_create      (libavutil)
     *     #9+ libffmpegmediaplugin.so
     *     #15 QMediaPlayer::QMediaPlayer
     *
     * ⚠ NVIDIA NEVER SEES IT — a different hwdevice is chosen there,
     * vkCreateDevice is never called and the hook is never entered. So the
     * development desktop opens the editor happily while every AMD laptop
     * gets a crash dialog, which is the same shape as the wallpaper engine's
     * MangoHud crash and was diagnosed from its notes.
     *
     * ⚠ And the Loader does NOT contain this. Keeping `import QtMultimedia`
     * out of the main file protects the window from a MISSING import; this is
     * a segfault inside the process, which takes the window with it however it
     * was reached.
     *
     * DISABLE_MANGOHUD is the manifest's own disable_environment and beats the
     * enable, so it is the knob to use rather than unsetting MANGOHUD and
     * hoping. MANGOHUD=0 goes with it for the OpenGL side. An FPS counter over
     * a colour grade would have been wrong anyway. */
    setenv("DISABLE_MANGOHUD", "1", 1);
    setenv("MANGOHUD", "0", 1);

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

/* Case-insensitive substring. Not strcasestr: that is a GNU extension and
 * this file compiles with -std=gnu11 today and something stricter tomorrow. */
static const char *strcasestr_(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return hay;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < n; i++) {
            int a = hay[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (!hay[i] || a != b) break;
        }
        if (i == n) return hay;
    }
    return NULL;
}

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

    /* What this machine can letter a title with.
     *
     * A LIST and not a promise: a project names a family, and a family the
     * machine playing it back has not got renders in the default face rather
     * than failing — so this is what to pick FROM here, not what a title is
     * guaranteed to look like everywhere. `have` answers the other question,
     * for one name, which is the one a checkbox needs. */
    if (!strcmp(cmd, "fonts")) {
        static char buf[262144];
        int n;
        if (argc > 3 && !strcmp(argv[2], "have")) {
            printf("%s\t%s\n", argv[3], ss_font_have(argv[3]) ? "yes" : "no");
            printf("file\t%s\n", ss_font_file(argv[3], SS_FW_REGULAR));
            return 0;
        }
        n = ss_font_families(buf, sizeof buf);
        if (n <= 0) {
            /* Not an error. fontconfig is how the list is GOT, not how a
             * caption is drawn — without it titles still render, in the face
             * that ships, which is what the default face has always been. */
            fprintf(stderr, "no font list here (fontconfig is not installed) — "
                            "titles use the default face\n");
            printf("file\t%s\n", ss_font_file("", SS_FW_REGULAR));
            return 0;
        }
        if (argc > 2) {
            /* A pattern, because a desktop has nine hundred families and
             * nobody scrolls that. Substring, case-insensitive. */
            char *p2 = buf;
            while (*p2) {
                char *e = strchr(p2, '\n');
                if (e) *e = '\0';
                if (strcasestr_(p2, argv[2])) puts(p2);
                if (!e) break;
                p2 = e + 1;
            }
            return 0;
        }
        fputs(buf, stdout);
        return 0;
    }

    if (!strcmp(cmd, "devices")) {
        ss_device *d = NULL;
        int n = ss_devices(&d), i;
        if (n < 0) return die("cannot ask what can capture");
        for (i = 0; i < n; i++)
            printf("%s\t%s\t%s\t%d\n",
                   d[i].monitor ? "monitor" : "input",
                   d[i].name[0] ? d[i].name : d[i].id, d[i].id, d[i].is_default);
        free(d);
        return 0;
    }

    /* A take, and a meter while it is taken.
     *
     * Prints `level <elapsed> <dB>` as it goes, because the question a
     * voiceover has to answer before anybody says a word is whether the
     * microphone is live — and finding out afterwards costs the take.
     *
     * Stopping is an ORDINARY end: SIGINT or SIGTERM finishes the file rather
     * than killing this, so the window's Stop button and Ctrl-C both leave a
     * WAV with its real length in the header. */
    if (!strcmp(cmd, "record")) {
        const char *fmt = "pulse", *device = "default";
        double limit = 600.0;
        int channels = 1, i;
        const char *out = NULL;

        for (i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
            else if (!strcmp(argv[i], "--device") && i + 1 < argc) device = argv[++i];
            else if (!strcmp(argv[i], "--format") && i + 1 < argc) fmt = argv[++i];
            else if (!strcmp(argv[i], "--limit") && i + 1 < argc) limit = atof(argv[++i]);
            else if (!strcmp(argv[i], "--channels") && i + 1 < argc) channels = atoi(argv[++i]);
            else return die("record: unknown option %s", argv[i]);
        }
        if (!out) return die("record needs --out");
        /* A forgotten session fills the disk, so there is always a limit and
         * it is an hour whether anybody asked for one or not. */
        if (limit <= 0 || limit > 3600) limit = 3600;

        if (ss_record(out, fmt, device, limit, channels, rec_print, NULL) != 0)
            return die("cannot record from %s", device);

        printf("out\t%s\n", out);
        printf("length\t%.3f\n", ss_media_duration(out));
        return 0;
    }

    if (!strcmp(cmd, "loudness")) {
        ss_loudness l;
        double in = 0, out = 0;
        int i;
        if (argc < 3) return die("loudness needs a file");
        for (i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--in") && i + 1 < argc) in = atof(argv[++i]);
            else if (!strcmp(argv[i], "--length") && i + 1 < argc) out = atof(argv[++i]);
            else return die("loudness: unknown option %s", argv[i]);
        }
        if (ss_media_loudness(argv[2], in, out, &l) != 0)
            return die("no sound to measure in %s", argv[2]);
        printf("lufs\t%.2f\npeak\t%.2f\nrange\t%.2f\n",
               l.lufs, l.peak_db, l.range);
        return 0;
    }
    if (!strcmp(cmd, "formats")) {
        const ss_still_format *f = ss_still_formats();
        int i;
        for (i = 0; f[i].name; i++)
            printf("%s\t%s\t%s\n", f[i].name, f[i].ext, f[i].label);
        return 0;
    }
    if (!strcmp(cmd, "browse"))   return cmd_browse(argc, argv);
    if (!strcmp(cmd, "kind")) {
        if (argc < 3) return die("kind needs a file");
        return cmd_kind(argv[2]);
    }
    if (!strcmp(cmd, "fx"))       return cmd_fx(argc, argv);
    if (!strcmp(cmd, "luts"))     return cmd_luts();
    /* The noise models this machine has, the way `luts` lists the LUTs. None
     * ship: a trained model is somebody's licensed work far more often than a
     * slider position is, so on most machines this list is empty until
     * somebody puts a .rnnn in ~/.config/synstudio/rnn. */
    if (!strcmp(cmd, "rnns")) {
        int i;
        for (i = 0; i < ss_rnn_count(); i++) {
            const ss_rnn_entry *r = ss_rnn_at(i);
            printf("%s\t%s\n", r->name, r->path);
        }
        return 0;
    }
    if (!strcmp(cmd, "look"))     return cmd_look(argc, argv);
    if (!strcmp(cmd, "gui"))      return cmd_gui(argc, argv);
    if (!strcmp(cmd, "timeline")) return cmd_timeline(argc, argv);

    opts_default(&o);

    if (!strcmp(cmd, "pixel")) {
        if (parse_opts(argc, argv, 2, &o, &rest, &nrest) != 0) return die("bad option");
        return cmd_pixel(argc, argv, 2, &o);
    }
    if (!strcmp(cmd, "lut")) {
        /* `lut show X` reads one; everything else about `lut` writes one. */
        if (argc > 3 && !strcmp(argv[2], "show")) return cmd_lut_show(argv[3]);
        if (parse_opts(argc, argv, 2, &o, &rest, &nrest) != 0) return die("bad option");
        return cmd_lut(&o);
    }

    /* What a camera's own curve does to one code value.
     *
     * A diagnostic, and the thing the suite asserts against: the anchors these
     * transforms are DEFINED by — 18% grey and 90% white — are numbers the
     * manufacturers publish, so the curve can be held against them exactly
     * rather than eyeballed through an 8-bit render whose own round trip
     * loses a code either way. */
    if (!strcmp(cmd, "logcurve")) {
        int m;
        double v;
        if (argc < 3) {
            printf("log\tnone\ta pass-through\n");
            printf("log\tslog3\tSony S-Log3 — 18%% grey 420/1023, 90%% white 598/1023\n");
            printf("log\tvlog\tPanasonic V-Log — 18%% grey 0.423, 90%% white 0.588\n");
            return 0;
        }
        m = ss_log_value(argv[2]);
        if (m < 0) return die("log is none, slog3 or vlog");
        if (parse_opts(argc, argv, 3, &o, &rest, &nrest) != 0)
            return die("bad option");
        if (!o.has_value) return die("logcurve wants --value CODE (0..1)");
        v = ss_log_to_linear(m, (float)o.value);
        printf("code\t%.6f\nlinear\t%.6f\n", o.value, v);
        return 0;
    }

    if (argc < 3) return die("%s needs a file", cmd);

    if (!strcmp(cmd, "probe"))  return cmd_probe(argv[2]);
    if (!strcmp(cmd, "get"))    return cmd_get(argv[2], argc > 3 ? argv[3] : NULL);
    if (!strcmp(cmd, "set"))    return cmd_set(argv[2], argc, argv, 3);
    if (!strcmp(cmd, "reset"))  return cmd_reset(argv[2]);
    if (!strcmp(cmd, "undo") || !strcmp(cmd, "redo") || !strcmp(cmd, "history"))
        return cmd_devhist(argv[2], cmd);
    if (!strcmp(cmd, "mask"))   return cmd_mask(argv[2], argc, argv, 3);
    if (!strcmp(cmd, "thumb"))  return cmd_thumb(argv[2], argc, argv, 3);

    if (parse_opts(argc, argv, 3, &o, &rest, &nrest) != 0) return die("bad option");
    if (!strcmp(cmd, "peaks"))     return cmd_peaks(argv[2], &o);
    if (!strcmp(cmd, "render"))    return cmd_render(argv[2], &o);
    if (!strcmp(cmd, "source"))    return cmd_source(argv[2], &o);
    if (!strcmp(cmd, "match")) {
        if (argc < 3) return die("match wants a file and --to REFERENCE");
        if (parse_opts(argc, argv, 3, &o, &rest, &nrest) != 0)
            return die("bad option");
        return cmd_match(argv[2], &o);
    }
    if (!strcmp(cmd, "histogram")) return cmd_histogram(argv[2], &o);
    if (!strcmp(cmd, "scope")) {
        if (argc < 3) return die("scope wants a file");
        if (parse_opts(argc, argv, 3, &o, &rest, &nrest) != 0)
            return die("bad option");
        return cmd_scope(argv[2], &o);
    }

    return die("unknown command: %s  (try `synstudio help`)", cmd);
}
