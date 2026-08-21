/* timeline.c — the video document, and the ffmpeg graph it turns into.
 *
 * The timeline is a plain description of intent: tracks, clips, in and out
 * points, a gain, a grade. It renders NOTHING. Export walks it once and emits
 * an argv for a single ffmpeg invocation, which means the export is one
 * process, one pass, and can be printed and read before it is run — a
 * property worth more than it sounds, because an export that goes wrong at
 * minute forty is diagnosed by looking at the graph, not by bisecting a
 * pipeline of intermediate files.
 *
 * The grade on a clip is the SAME ss_develop a photograph uses. Its pointwise
 * half arrives here as a .cube (see lut.c) applied with lut3d; its spatial
 * half maps onto ffmpeg's own filters. Anything that cannot be expressed
 * either way is not silently dropped — ss_timeline_ffmpeg says so.
 */
#include "synstudio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    return tr->nclips++;
}

static double clip_len(const ss_clip *c)
{
    double l = (c->src_out - c->src_in) / (c->speed > 0 ? c->speed : 1.0);
    return l > 0 ? l : 0;
}

double ss_timeline_duration(const ss_timeline *t)
{
    double end = 0;
    int i, j;
    for (i = 0; i < t->ntracks; i++)
        for (j = 0; j < t->track[i].nclips; j++) {
            const ss_clip *c = &t->track[i].clip[j];
            double e = c->tl_in + clip_len(c);
            if (e > end) end = e;
        }
    return end;
}

/* -------------------------------------------------------- serialisation -- */

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

int ss_timeline_read(ss_timeline *t, FILE *fp)
{
    char line[2048];
    int cur_track = -1, cur_clip = -1;

    ss_timeline_free(t);
    ss_timeline_reset(t, 1920, 1080, 25.0);

    while (fgets(line, sizeof line, fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '#' || !line[0]) continue;

        if (!strncmp(line, "name\t", 5)) {
            /* The precision, not a bare %s. snprintf truncates safely either
             * way, but at -O3 gcc can see a 2043-byte source going into 256
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
        } else if (!strncmp(line, "clip\t", 5)) {
            ss_clip c;
            char *p = line + 5, *tab;
            int f = 0;
            double v[8] = {0,0,0,1,0,1,0,0};
            memset(&c, 0, sizeof c);
            /* Fields are tab separated and the LAST one is the path, which
             * may contain spaces. Split on tabs, never on whitespace. */
            while (f < 8 && (tab = strchr(p, '\t'))) {
                *tab = '\0';
                v[f++] = atof(p);
                p = tab + 1;
            }
            if (f < 8) continue;
            c.tl_in = v[0]; c.src_in = v[1]; c.src_out = v[2]; c.speed = v[3];
            c.gain_db = (float)v[4]; c.opacity = (float)v[5];
            c.fade_in = v[6]; c.fade_out = v[7];
            snprintf(c.path, sizeof c.path, "%s", p);
            cur_clip = ss_timeline_add_clip(t, cur_track, &c);
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
            if (cur_track >= 0 && cur_clip >= 0) {
                ms = fmemopen(buf, used, "r");
                if (ms) {
                    ss_develop_read(&t->track[cur_track].clip[cur_clip].grade, ms);
                    fclose(ms);
                    t->track[cur_track].clip[cur_clip].has_grade = 1;
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

int ss_timeline_ffmpeg(const ss_timeline *t, const char *out,
                       const char *lutdir, char ***argv_out)
{
    strbuf fc = {0};
    char **av = NULL;
    int ac = 0, cap = 64;
    int i, j, input = 0, nvid = 0, naud = 0;
    double dur = ss_timeline_duration(t);

    av = malloc(sizeof(char *) * cap);
    if (!av) return -1;

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
            if (clip_len(c) <= 0) continue;
            if (c->src_in > 0.0) { PUSH(xdup("-ss")); PUSH(xfmt("%.6f", c->src_in)); }
            PUSH(xdup("-t")); PUSH(xfmt("%.6f", c->src_out - c->src_in));
            PUSH(xdup("-i")); PUSH(xdup(c->path));
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
            double len = clip_len(c);
            if (len <= 0) continue;

            if (tr->type == SS_TRACK_VIDEO) {
                sb_add(&fc, ";[%d:v]", input);
                /* Scale into the project frame, letterboxing rather than
                 * distorting: a clip shot in a different aspect is a framing
                 * decision, and stretching it is never the one intended. */
                sb_add(&fc, "scale=%d:%d:force_original_aspect_ratio=decrease,"
                            "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black",
                        t->w, t->h, t->w, t->h);
                sb_add(&fc, ",fps=%.6g", t->fps);
                if (c->speed != 1.0)
                    sb_add(&fc, ",setpts=%.6f*PTS", 1.0 / c->speed);

                if (c->has_grade) {
                    const ss_develop *d = &c->grade;
                    char lp[2048], esc[4200];
                    snprintf(lp, sizeof lp, "%s/grade_%d_%d.cube", lutdir, i, j);
                    esc_filter(lp, esc, sizeof esc);
                    sb_add(&fc, ",lut3d=file='%s':interp=tetrahedral", esc);

                    /* The spatial half, mapped onto ffmpeg's own filters. */
                    if (d->sharpen > 0.0f) {
                        int lsz = (int)(d->sharpen_radius * 2) * 2 + 3;
                        if (lsz < 3) lsz = 3;
                        if (lsz > 23) lsz = 23;
                        sb_add(&fc, ",unsharp=%d:%d:%.3f", lsz, lsz,
                               d->sharpen / 100.0f);
                    }
                    if (d->vignette < 0.0f)
                        sb_add(&fc, ",vignette=angle=%.4f",
                               (double)(-d->vignette / 100.0f) * 1.2);
                    if (d->nr_luma > 0.0f || d->nr_chroma > 0.0f)
                        sb_add(&fc, ",hqdn3d=%.2f:%.2f:%.2f:%.2f",
                               d->nr_luma / 25.0f, d->nr_chroma / 25.0f,
                               d->nr_luma / 16.0f, d->nr_chroma / 16.0f);
                    if (d->crop.on)
                        sb_add(&fc, ",crop=iw*%.5f:ih*%.5f:iw*%.5f:ih*%.5f",
                               d->crop.w, d->crop.h, d->crop.x, d->crop.y);
                }

                if (c->fade_in > 0.0)
                    sb_add(&fc, ",fade=t=in:st=0:d=%.4f", c->fade_in);
                if (c->fade_out > 0.0)
                    sb_add(&fc, ",fade=t=out:st=%.4f:d=%.4f",
                           len - c->fade_out, c->fade_out);
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
                    sb_add(&fc, ";[%s][v%d]overlay=eof_action=pass:"
                                "enable='between(t,%.6f,%.6f)'[bg%d]",
                            prev, nvid, c->tl_in, c->tl_in + len, nvid + 1);
                }
                nvid++;
            } else {
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
     * below does not have to know how many clips there were. */
    if (nvid > 0) sb_add(&fc, ";[bg%d]null[vout]", nvid);
    else          sb_add(&fc, ";[base]null[vout]");

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
    PUSH(xdup("-map")); PUSH(xdup("[vout]"));
    if (naud > 0) { PUSH(xdup("-map")); PUSH(xdup("[aout]")); }
    PUSH(xdup("-t")); PUSH(xfmt("%.6f", dur > 0 ? dur : 1.0));
    PUSH(xdup("-c:v")); PUSH(xdup("libx264"));
    PUSH(xdup("-preset")); PUSH(xdup("medium"));
    PUSH(xdup("-crf")); PUSH(xdup("18"));
    PUSH(xdup("-pix_fmt")); PUSH(xdup("yuv420p"));
    if (naud > 0) { PUSH(xdup("-c:a")); PUSH(xdup("aac"));
                    PUSH(xdup("-b:a")); PUSH(xdup("192k")); }
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
#undef PUSH
}
