/* Subtitles — a cue is a title clip.
 *
 * There is no fourth clip kind here and no subtitle track type. A cue read
 * out of a .srt becomes an ordinary title on an ordinary video track, which
 * means the moment it is imported it takes the font, the plate, the
 * placement, the fades, the transform and the grade that a typed caption
 * takes, and it is edited with the commands that already exist. Burning in
 * is then not a feature at all: it is what a title has always done.
 *
 * Shipping the cues as a SOFT stream instead is a delivery option — the
 * `--subs` argument to an export — because a stream a player switches on and
 * off never touches the picture and so never touches the filter graph.
 */
#include "synstudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* HH:MM:SS,mmm — and also HH:MM:SS.mmm, which half the tools in the world
 * write instead. Returns seconds, or -1 for anything that is not a timestamp.
 * Deliberately strict about the SHAPE and forgiving about the separator: a
 * line that is nearly a timestamp is far more likely to be dialogue that
 * happens to contain digits than a cue with a typo in it. */
static double srt_time(const char *s)
{
    int h, m, sec, ms;
    char sep;
    if (sscanf(s, "%d:%d:%d%c%d", &h, &m, &sec, &sep, &ms) != 5) return -1;
    if (sep != ',' && sep != '.') return -1;
    if (h < 0 || m < 0 || m > 59 || sec < 0 || sec > 59 || ms < 0 || ms > 999)
        return -1;
    return h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
}

static void srt_stamp(char *out, size_t n, double t)
{
    long ms;
    int h, m, s;
    if (t < 0) t = 0;
    ms = (long)(t * 1000.0 + 0.5);
    h = (int)(ms / 3600000); ms -= (long)h * 3600000;
    m = (int)(ms / 60000);   ms -= (long)m * 60000;
    s = (int)(ms / 1000);    ms -= (long)s * 1000;
    snprintf(out, n, "%02d:%02d:%02d,%03ld", h, m, s, ms);
}

/* Strip a trailing CR and any trailing whitespace. A .srt written on Windows
 * is CRLF, and a CR left on the end of a timestamp line makes the arrow
 * comparison fail on a file that is not actually malformed. */
static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t'))
        s[--n] = '\0';
}

static const char *arrow(const char *s)
{
    return strstr(s, "-->");
}

/* SubRip's own markup. A cue may carry <i>, <b>, <u> and <font …> tags;
 * drawtext has no idea what to do with them and would draw the angle brackets
 * on screen. They are dropped rather than honoured — italics for one cue is
 * not worth a second text renderer — and the words inside them are kept. */
static void strip_tags(char *s)
{
    char *r = s, *w = s;
    int in = 0;
    while (*r) {
        if (*r == '<') in = 1;
        else if (*r == '>') in = 0;
        else if (!in) *w++ = *r;
        r++;
    }
    *w = '\0';
}

int ss_subs_import(ss_timeline *t, const char *file, int track,
                   char *err, size_t errn)
{
    FILE *fp;
    char line[1024];
    int added = 0, lineno = 0;

    if (err && errn) err[0] = '\0';
    if (!t || track < 0 || track >= t->ntracks) {
        if (err) snprintf(err, errn, "no such track");
        return -1;
    }
    if (t->track[track].type != SS_TRACK_VIDEO) {
        if (err) snprintf(err, errn, "track %d is an audio track", track);
        return -1;
    }
    if ((fp = fopen(file, "r")) == NULL) {
        if (err) snprintf(err, errn, "cannot read %s", file);
        return -1;
    }

    while (fgets(line, sizeof line, fp)) {
        double in, out;
        char text[512] = "";
        const char *a;
        size_t used = 0;

        lineno++;
        chomp(line);
        /* The cue NUMBER is not trusted and not required. Files in the wild
         * skip numbers, repeat them and start at nought; what actually
         * separates one cue from the next is the timestamp line, so that is
         * what is looked for and everything before it is skipped. */
        if ((a = arrow(line)) == NULL) continue;

        in = srt_time(line);
        out = srt_time(a + 3 + strspn(a + 3, " \t"));
        if (in < 0 || out < 0) {
            if (err) snprintf(err, errn, "line %d: unreadable timestamp", lineno);
            fclose(fp);
            return -1;
        }

        /* The caption runs to the first blank line. Newlines are kept — a
         * two-line cue is two lines on screen, which is the whole reason a
         * caption can hold one. */
        while (fgets(line, sizeof line, fp)) {
            lineno++;
            chomp(line);
            if (!*line) break;
            strip_tags(line);
            if (used) {
                if (used + 1 < sizeof text) text[used++] = '\n';
                else break;
            }
            {
                size_t len = strlen(line);
                if (used + len >= sizeof text) len = sizeof text - 1 - used;
                memcpy(text + used, line, len);
                used += len;
            }
            text[used] = '\0';
        }

        if (!*text) continue;      /* a cue with no words is not a caption */

        {
            ss_clip c;
            ss_clip_reset(&c);
            c.kind    = SS_CLIP_TITLE;
            c.tl_in   = in;
            c.src_in  = 0;
            /* A zero-length or reversed cue would be a clip that renders
             * nothing and cannot be grabbed on the timeline to fix. One frame
             * is the shortest thing that exists here. */
            c.src_out = out > in ? out - in : 1.0 / (t->fps > 0 ? t->fps : 25.0);
            snprintf(c.text, sizeof c.text, "%s", text);
            /* Subtitles are not titles-with-a-look: they are small, at the
             * bottom, and on a plate, because that is the only styling that
             * stays readable over a picture nobody has seen yet. */
            ss_title_style_apply(&c, "subtitle");
            if (ss_timeline_add_clip(t, track, &c) < 0) {
                if (err) snprintf(err, errn, "the track is full at cue %d", added + 1);
                fclose(fp);
                return -1;
            }
            added++;
        }
    }

    fclose(fp);
    if (added == 0 && err) snprintf(err, errn, "no cues in %s", file);
    return added;
}

int ss_subs_export(const ss_timeline *t, int track, const char *file)
{
    FILE *fp;
    int i, n = 0;

    if (!t || track < 0 || track >= t->ntracks) return -1;
    if ((fp = fopen(file, "w")) == NULL) return -1;

    for (i = 0; i < t->track[track].nclips; i++) {
        const ss_clip *c = &t->track[track].clip[i];
        char a[32], b[32];
        const char *p;
        if (c->kind != SS_CLIP_TITLE || !*c->text) continue;
        srt_stamp(a, sizeof a, c->tl_in);
        srt_stamp(b, sizeof b, c->tl_in + ss_clip_length(c));
        fprintf(fp, "%d\n%s --> %s\n", ++n, a, b);
        /* The caption goes out as it is stored, with its line breaks — a
         * SubRip cue is allowed several lines and this is the one format
         * where that is not an escape. */
        for (p = c->text; *p; p++) fputc(*p, fp);
        fputs("\n\n", fp);
    }

    fclose(fp);
    return n;
}
