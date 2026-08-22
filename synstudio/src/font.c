/* Fonts — a family NAME on one side, a font FILE on the other.
 *
 * drawtext takes either `font=Sans`, which needs an ffmpeg built against
 * fontconfig, or `fontfile=/a/path.ttf`, which needs nothing. This program
 * has always used the second, because the first fails at EXPORT time — after
 * the edit, with a message about a filter graph — on a machine whose ffmpeg
 * happens to be built without it, and there is no way to find that out from
 * inside a filter string.
 *
 * So the family a person picks is resolved to a file HERE, once, through
 * fc-match. Where fontconfig is not installed the shipped faces are still
 * found by looking, which is what the program did before it could be asked
 * for a family at all.
 */
#include "synstudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *weight_names[] = { "regular", "bold", "light",
                                      "italic", "bolditalic" };
static const int nweights = (int)(sizeof weight_names / sizeof weight_names[0]);

int ss_textweight_value(const char *s)
{
    int i;
    if (!s) return -1;
    for (i = 0; i < nweights; i++)
        if (!strcmp(s, weight_names[i])) return i;
    return -1;
}

const char *ss_textweight_name(int v)
{
    return (v >= 0 && v < nweights) ? weight_names[v] : weight_names[0];
}

/* The faces that ship with a SynapseOS install, in the order they are
 * preferred. Kept as the fallback rather than as the answer: this is what a
 * caption gets when fontconfig cannot be asked, or when it answers with a
 * path that is not there. */
static const char *shipped[] = {
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
    NULL
};

static int readable(const char *p)
{
    FILE *fp;
    if (!p || !*p) return 0;
    fp = fopen(p, "rb");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

static const char *default_face(void)
{
    static const char *found;
    int i;
    if (found) return found;
    for (i = 0; shipped[i]; i++)
        if (readable(shipped[i])) { found = shipped[i]; return found; }
    /* Name the one that is missing rather than nothing at all: ffmpeg then
     * says which file it could not open, which is a fixable message. */
    found = shipped[0];
    return found;
}

/* fc-match's pattern language uses `:` and `,` as separators, so a family
 * containing either would silently become a different query. A family name
 * has no business containing them; anything that does is refused here rather
 * than half-honoured. */
static int family_sane(const char *f)
{
    size_t i;
    if (!f || !*f) return 0;
    if (strlen(f) > 63) return 0;
    for (i = 0; f[i]; i++)
        if (f[i] == ':' || f[i] == ',' || f[i] == '\\' || (unsigned char)f[i] < 0x20)
            return 0;
    return 1;
}

/* A tiny cache. A timeline of a hundred subtitle cues in one family asks for
 * the same file a hundred times per graph, and each miss is a fork. */
#define FCACHE 16
static struct { char fam[64]; int w; char file[1024]; } fcache[FCACHE];
static int nfcache;

static const char *cached(const char *fam, int w)
{
    int i;
    for (i = 0; i < nfcache; i++)
        if (fcache[i].w == w && !strcmp(fcache[i].fam, fam))
            return fcache[i].file;
    return NULL;
}

static void cache_put(const char *fam, int w, const char *file)
{
    int i = nfcache < FCACHE ? nfcache++ : 0;
    snprintf(fcache[i].fam, sizeof fcache[i].fam, "%s", fam);
    fcache[i].w = w;
    snprintf(fcache[i].file, sizeof fcache[i].file, "%s", file);
}

const char *ss_font_file(const char *family, int weight)
{
    char pat[160], outbuf[1024], *nl;
    const char *hit;
    char *av[6];

    if (weight < 0 || weight >= nweights) weight = SS_FW_REGULAR;

    /* Nothing asked for at all: the face that ships, with no fork. This is
     * the overwhelmingly common case — every title that was ever made before
     * a family could be named — and it must cost nothing and behave exactly
     * as it always did. */
    if ((!family || !*family) && weight == SS_FW_REGULAR)
        return default_face();

    /* A weight with NO family still has to mean something. `sans-serif` is
     * fontconfig's own generic, so asking it for a bold sans is the same
     * question a person means by ticking Bold without picking a face —
     * whereas returning the default file here would have quietly ignored the
     * tick, which is the failure mode this whole path exists to avoid. */
    if (!family || !*family) family = "sans-serif";
    if (!family_sane(family)) return default_face();

    if ((hit = cached(family, weight)) != NULL)
        return *hit ? hit : default_face();

    switch (weight) {
    case SS_FW_BOLD:       snprintf(pat, sizeof pat, "%s:weight=bold", family); break;
    case SS_FW_LIGHT:      snprintf(pat, sizeof pat, "%s:weight=light", family); break;
    case SS_FW_ITALIC:     snprintf(pat, sizeof pat, "%s:slant=italic", family); break;
    case SS_FW_BOLDITALIC: snprintf(pat, sizeof pat, "%s:weight=bold:slant=italic",
                                    family); break;
    default:               snprintf(pat, sizeof pat, "%s", family); break;
    }

    av[0] = (char *)"fc-match";
    av[1] = (char *)"-f";
    av[2] = (char *)"%{file}";
    av[3] = pat;
    av[4] = NULL;

    if (ss_capture(av, outbuf, sizeof outbuf) < 0) {
        cache_put(family, weight, "");
        return default_face();
    }
    if ((nl = strchr(outbuf, '\n')) != NULL) *nl = '\0';

    /* ⚠ fc-match ALWAYS answers. Asked for a family this machine has not got
     * it returns its best substitute — usually DejaVu Sans — and says so with
     * exit status 0, so "did it run" is not "was the font found". That is the
     * right behaviour for rendering (a caption draws in something) and the
     * wrong one for a checkbox, which is why ss_font_have asks a different
     * question instead of this one. */
    if (!readable(outbuf)) {
        cache_put(family, weight, "");
        return default_face();
    }
    cache_put(family, weight, outbuf);
    return cached(family, weight);
}

int ss_font_have(const char *family)
{
    char pat[160], outbuf[1024], *nl;
    char *av[6];

    if (!family_sane(family)) return 0;
    snprintf(pat, sizeof pat, "%s", family);
    av[0] = (char *)"fc-match";
    av[1] = (char *)"-f";
    av[2] = (char *)"%{family}";
    av[3] = pat;
    av[4] = NULL;
    if (ss_capture(av, outbuf, sizeof outbuf) < 0) return 0;
    if ((nl = strchr(outbuf, '\n')) != NULL) *nl = '\0';

    /* fc-match answers with the family it SETTLED on. Comparing that to what
     * was asked for is the only way to tell a hit from a substitution, and it
     * is compared case-insensitively because fontconfig normalises case and a
     * person typing "noto sans" means the same face as the catalogue's. */
    {
        const char *a = outbuf, *b = family;
        /* A family can answer as a comma-separated list of its aliases. */
        for (;;) {
            const char *comma = strchr(a, ',');
            size_t len = comma ? (size_t)(comma - a) : strlen(a);
            if (len == strlen(b)) {
                size_t i;
                for (i = 0; i < len; i++) {
                    int x = a[i], y = b[i];
                    if (x >= 'A' && x <= 'Z') x += 32;
                    if (y >= 'A' && y <= 'Z') y += 32;
                    if (x != y) break;
                }
                if (i == len) return 1;
            }
            if (!comma) break;
            a = comma + 1;
        }
    }
    return 0;
}

static int strcmp_qsort(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int ss_font_families(char *out, size_t n)
{
    static char buf[262144];
    char *lines[4096];
    int nlines = 0, i, count = 0;
    size_t o = 0;
    char *av[5];

    if (!out || n == 0) return 0;
    out[0] = '\0';

    av[0] = (char *)"fc-list";
    av[1] = (char *)":";
    av[2] = (char *)"family";
    av[3] = NULL;
    if (ss_capture(av, buf, sizeof buf) < 0) return 0;

    /* One family per line, and each line may carry the family's aliases
     * comma-separated. The first is the canonical name; the rest are the same
     * face under another spelling, and listing those would offer a person
     * four ways to pick one font. */
    {
        char *p = buf;
        while (*p && nlines < (int)(sizeof lines / sizeof lines[0])) {
            char *e = strchr(p, '\n'), *comma;
            if (e) *e = '\0';
            if ((comma = strchr(p, ',')) != NULL) *comma = '\0';
            if (*p) lines[nlines++] = p;
            if (!e) break;
            p = e + 1;
        }
    }
    qsort(lines, (size_t)nlines, sizeof lines[0], strcmp_qsort);

    for (i = 0; i < nlines; i++) {
        size_t len;
        if (i > 0 && !strcmp(lines[i], lines[i - 1])) continue;   /* fc-list repeats */
        len = strlen(lines[i]);
        if (o + len + 2 >= n) break;
        memcpy(out + o, lines[i], len);
        o += len;
        out[o++] = '\n';
        count++;
    }
    out[o] = '\0';
    return count;
}

/* How a retimed clip makes the frames that were never shot. Lives beside the
 * weight names for the same reason: a name a person types, an integer the
 * file stores, and one table that turns each into the other. */
static const char *retime_names[] = { "nearest", "blend", "flow" };

int ss_retime_value(const char *s)
{
    int i;
    if (!s) return -1;
    for (i = 0; i < 3; i++) if (!strcmp(s, retime_names[i])) return i;
    return -1;
}

const char *ss_retime_name(int v)
{
    return (v >= 0 && v < 3) ? retime_names[v] : retime_names[0];
}

/* Fade shapes. ffmpeg's own curve names live in the third column so nothing
 * here invents a vocabulary the renderer would then have to translate. */
static const char *afade_names[6]  = { "linear", "qsin", "hsin",
                                       "esin", "log", "exp" };
static const char *afade_curves[6] = { "tri", "qsin", "hsin",
                                       "esin", "log", "exp" };

int ss_afade_value(const char *s)
{
    int i;
    if (!s) return -1;
    for (i = 0; i < 6; i++) if (!strcmp(s, afade_names[i])) return i;
    return -1;
}

const char *ss_afade_name(int v)
{
    return (v >= 0 && v < 6) ? afade_names[v] : afade_names[0];
}

const char *ss_afade_curve(int v)
{
    return (v >= 0 && v < 6) ? afade_curves[v] : afade_curves[0];
}
