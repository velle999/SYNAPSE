/*
 * titles.c — title templates: a layout of several things drawn together.
 *
 * A style sets a handful of a title's fields and gets out of the way; there
 * is nothing it can say that four `timeline set` commands cannot. A template
 * is what those cannot say — a name over a role in two sizes, an accent bar
 * beside them, a rule under a heading, a move in — so it is a FILE, and the
 * file is the effect manifest with a chain of drawtext and drawbox behind it.
 *
 * ⚠ DRAWTEXT READS FILES. `fontfile=` and `textfile=` are its whole reason
 * for existing, and an effect recipe refuses both outright. A template needs
 * a font and a caption, so it names them as TOKENS and the engine writes what
 * they stand for: the clip's own family resolved to a file, the caption baked
 * to a file of the engine's choosing, read with expansion=none. A template
 * that could say `textfile=` could print any file on the machine into a
 * frame; one that could say `text=` would put its own words in a caption the
 * user typed, through drawtext's %{...} expansion.
 *
 * So the rules are a WHITELIST at every level: two filters, a short list of
 * options for each — none of which reads anything — written NAME=VALUE, and
 * every `$` one of the tokens below or a declared number. Nothing else
 * reaches ffmpeg.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "synstudio.h"
#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- what a template may say ----
 *
 * Every option here is one ffmpeg 9 takes as a runtime command too, which is
 * what lets a keyed size or colour move a templated title the way it moves a
 * plain one. `text_align` and `y_align` are newer than some ffmpeg builds; a
 * template that uses them fails `titles check` there rather than an export.
 *
 * ⚠ AND WHAT KIND OF VALUE EACH TAKES, because they are not alike. An
 * EXPRESSION is evaluated by the filter, with w, h, t and the rest; a NUMBER
 * is an int option, which libavutil evaluates as constant arithmetic; a
 * COLOUR is a colour. But boxborderw is a STRING drawtext reads with its own
 * parser, which takes `14*0.3` as 14 — a plate four times too deep, drawn
 * without complaint. So it takes WHOLE pixels only: digits, `|` between
 * sides, and the tokens that are already whole. A literal takes no token. */
enum { OK_EXPR, OK_NUM, OK_COLOUR, OK_WHOLE, OK_LITERAL };
typedef struct { const char *name; int kind; } topt;
static const topt dt_opts[] = {
    { "fontcolor", OK_COLOUR }, { "fontsize", OK_EXPR }, { "x", OK_EXPR },
    { "y", OK_EXPR }, { "alpha", OK_EXPR }, { "line_spacing", OK_NUM },
    { "text_align", OK_LITERAL }, { "y_align", OK_LITERAL },
    { "tabsize", OK_NUM }, { "box", OK_LITERAL }, { "boxcolor", OK_COLOUR },
    { "boxborderw", OK_WHOLE }, { "boxw", OK_NUM }, { "boxh", OK_NUM },
    { "borderw", OK_NUM }, { "bordercolor", OK_COLOUR },
    { "shadowx", OK_NUM }, { "shadowy", OK_NUM }, { "shadowcolor", OK_COLOUR },
    { NULL, 0 }
};
static const topt db_opts[] = {
    { "x", OK_EXPR }, { "y", OK_EXPR }, { "w", OK_EXPR }, { "h", OK_EXPR },
    { "width", OK_EXPR }, { "height", OK_EXPR }, { "color", OK_COLOUR },
    { "c", OK_COLOUR }, { "thickness", OK_EXPR }, { "t", OK_EXPR },
    { "replace", OK_LITERAL },
    { NULL, 0 }
};
static const struct {
    const char *name;
    const topt *opts;
    int text;                   /* takes a font token and a text token */
} tfilters[] = {
    { "drawtext", dt_opts, 1 },
    { "drawbox",  db_opts, 0 },
    { NULL, NULL, 0 }
};

static const struct { const char *tok; int weight; } ftoks[] = {
    { "font",            -1 },       /* the clip's own weight */
    { "font_regular",    SS_FW_REGULAR },
    { "font_bold",       SS_FW_BOLD },
    { "font_light",      SS_FW_LIGHT },
    { "font_italic",     SS_FW_ITALIC },
    { "font_bolditalic", SS_FW_BOLDITALIC },
    { NULL, 0 }
};

/* ---- a bounded writer ---- */

typedef struct {
    char  *buf;
    size_t n, len;
    int    over;
} obuf;

static void oput(obuf *o, const char *s, size_t len)
{
    if (!o || !o->buf || o->over) return;
    if (o->len + len + 1 > o->n) { o->over = 1; return; }
    memcpy(o->buf + o->len, s, len);
    o->len += len;
    o->buf[o->len] = '\0';
}

static void oputs(obuf *o, const char *s) { oput(o, s, strlen(s)); }

static void oprintf(obuf *o, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    oputs(o, tmp);
}

/* ---- the tokens ---- */

static size_t tok_len(const char *s)
{
    size_t n = 0;
    while (s[n] && (isalnum((unsigned char)s[n]) || s[n] == '_')) n++;
    return n;
}

static int tok_is(const char *s, size_t len, const char *word)
{
    return strlen(word) == len && !strncmp(s, word, len);
}

/* A font token's weight: -1 for the clip's own, an SS_FW_* otherwise, and
 * -2 for "not a font token". */
static int font_tok(const char *s, size_t len)
{
    int i;
    for (i = 0; ftoks[i].tok; i++)
        if (tok_is(s, len, ftoks[i].tok)) return ftoks[i].weight;
    return -2;
}

/* 0 for $text, 1..9 for $lineN, -1 for neither. */
static int text_tok(const char *s, size_t len)
{
    if (tok_is(s, len, "text")) return 0;
    if (len == 5 && !strncmp(s, "line", 4) && s[4] >= '1' &&
        s[4] <= '0' + SS_TITLE_LINES)
        return s[4] - '0';
    return -1;
}

/* The value tokens: what each reads of the clip, whether it is a whole
 * number of pixels, and whether it is a colour. */
static const struct { const char *tok; int uses, whole, colour; } vtoks[] = {
    { "size",    SS_TT_SIZE,    1, 0 },
    { "pad",     SS_TT_SIZE,    1, 0 },
    { "outline", SS_TT_OUTLINE, 1, 0 },
    { "shadow",  SS_TT_SHADOW,  1, 0 },
    { "colour",  SS_TT_COLOUR,  0, 1 },
    { "plate",   SS_TT_PLATE,   0, 1 },
    { NULL, 0, 0, 0 }
};

static int reserved(const char *s, size_t len)
{
    int i;
    for (i = 0; vtoks[i].tok; i++)
        if (tok_is(s, len, vtoks[i].tok)) return 1;
    return font_tok(s, len) != -2 || text_tok(s, len) >= 0;
}

/* An option's OK_* kind, by filter and name. */
static int opt_kind(const char *fname, const char *opt)
{
    int f, i;
    for (f = 0; tfilters[f].name; f++)
        if (!strcmp(tfilters[f].name, fname))
            for (i = 0; tfilters[f].opts[i].name; i++)
                if (!strcmp(tfilters[f].opts[i].name, opt))
                    return tfilters[f].opts[i].kind;
    return OK_EXPR;
}

/* Options whose value moves when the size or colour is keyed. */
#define TT_MOVING (SS_TT_SIZE | SS_TT_COLOUR | SS_TT_OUTLINE | SS_TT_SHADOW)

/* A value with its tokens replaced. Without a context it is only checked —
 * which is what reading the recipe does — and `uses` collects what it reads.
 * `kind` is the option's OK_*, which decides what may appear in it at all. */
static int put_value(obuf *o, const ss_fx *tt, const char *v, size_t vl,
                     int kind, const double *vals, int nvals,
                     const ss_title_ctx *cx, int *uses, char *err, size_t errn)
{
    const char *e = v + vl, *q;

    if (kind == OK_LITERAL && memchr(v, '$', vl)) {
        if (err) snprintf(err, errn, "takes a plain value, not a token");
        return -1;
    }
    if (kind == OK_WHOLE)
        for (q = v; q < e; q++) {
            if (*q == '$') { q += tok_len(q + 1); continue; }
            if (!isdigit((unsigned char)*q) && *q != '|') {
                if (err) snprintf(err, errn, "takes whole pixels — digits, "
                                             "$size, $pad, $outline or $shadow — "
                                             "and no arithmetic");
                return -1;
            }
        }

    while (v < e) {
        const char *d = memchr(v, '$', (size_t)(e - v));
        size_t len;
        int i;

        if (!d) { oput(o, v, (size_t)(e - v)); break; }
        oput(o, v, (size_t)(d - v));
        len = tok_len(d + 1);
        if (len == 0) {
            if (err) snprintf(err, errn, "a $ with no name after it");
            return -1;
        }
        for (i = 0; vtoks[i].tok; i++)
            if (tok_is(d + 1, len, vtoks[i].tok)) break;
        if (vtoks[i].tok) {
            if (vtoks[i].colour != (kind == OK_COLOUR) ||
                (kind == OK_WHOLE && !vtoks[i].whole)) {
                if (err) snprintf(err, errn, vtoks[i].colour
                                  ? "$%s is a colour, and this is not a colour"
                                  : "$%s is a number, and this is a colour",
                                  vtoks[i].tok);
                return -1;
            }
            *uses |= vtoks[i].uses;
            if (cx) {
                if (!strcmp(vtoks[i].tok, "size"))         oprintf(o, "%d", cx->size);
                else if (!strcmp(vtoks[i].tok, "pad"))     oprintf(o, "%d", cx->pad);
                else if (!strcmp(vtoks[i].tok, "outline")) oprintf(o, "%d", cx->outline);
                else if (!strcmp(vtoks[i].tok, "shadow"))  oprintf(o, "%d", cx->shadow);
                else if (!strcmp(vtoks[i].tok, "colour"))  oputs(o, cx->colour);
                else                                        oputs(o, cx->plate);
            }
        } else if (kind == OK_WHOLE) {
            if (err) snprintf(err, errn, "$%.*s: only whole-pixel tokens here",
                              (int)len, d + 1);
            return -1;
        } else {
            for (i = 0; i < tt->nparam; i++)
                if (tok_is(d + 1, len, tt->param[i].key)) break;
            if (i == tt->nparam) {
                if (err) snprintf(err, errn,
                                  font_tok(d + 1, len) != -2 || text_tok(d + 1, len) >= 0
                                  ? "$%.*s is a whole option, not part of a value"
                                  : "$%.*s is not a parameter or a token",
                                  (int)len, d + 1);
                return -1;
            }
            if (cx) {
                double x = (i < nvals && vals) ? vals[i] : tt->param[i].def;
                if (x < tt->param[i].lo) x = tt->param[i].lo;
                if (x > tt->param[i].hi) x = tt->param[i].hi;
                oprintf(o, "%g", x);
            }
        }
        v = d + 1 + len;
    }
    return 0;
}

/* ---- the walk ----
 *
 * ONE function reads the chain for both purposes — judging it when the file
 * is loaded (`meta` set, no context) and writing it out for a render (a
 * context and a writer) — so the shape that was approved is exactly the
 * shape that is expanded. Two walkers would be two opinions about where a
 * filter ends.
 *
 * A chain is filters joined by commas. No labels, no `;`: a template draws
 * on the title's own picture in place, and a second stream would be a graph
 * the monitor and the export would have to wire the same way twice. */
static int tt_walk(const ss_fx *tt, ss_fx *meta, const double *vals,
                   int nvals, const ss_title_ctx *cx, const char *tag,
                   obuf *o, char *err, size_t errn)
{
    const char *s = tt->filter;
    int k = -1, uses = 0;

    if (meta) meta->ntarget = 0;

    for (;;) {
        char fname[32];
        size_t n = 0;
        int f, nfont = 0, ntext = 0;

        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) break;
        if (k >= 0) {
            if (*s != ',') {
                if (err) snprintf(err, errn, "filters are joined by commas — "
                                             "no labels and no ';'");
                return -1;
            }
            s++;
            oputs(o, ",");
            while (*s && isspace((unsigned char)*s)) s++;
        }
        while (*s && *s != '=' && *s != ',' && !isspace((unsigned char)*s)) {
            if (n + 1 < sizeof fname) fname[n++] = *s;
            s++;
        }
        fname[n] = '\0';
        for (f = 0; tfilters[f].name; f++)
            if (!strcmp(tfilters[f].name, fname)) break;
        if (!tfilters[f].name) {
            if (err) snprintf(err, errn, "%s is not drawtext or drawbox",
                              *fname ? fname : "(nothing)");
            return -1;
        }
        k++;
        oputs(o, fname);
        if (tag) oprintf(o, "@%s_%d", tag, k);

        if (*s == '=') {
            s++;
            oputs(o, "=");
            for (;;) {
                const char *a = s, *b, *eq = NULL;
                int quotes = 0;

                while (*s && *s != ':' && *s != ',') {
                    if (*s == '\'') {
                        quotes++;
                        s++;
                        while (*s && *s != '\'') {
                            if (*s == '\\') {
                                if (err) snprintf(err, errn, "no backslashes");
                                return -1;
                            }
                            s++;
                        }
                        if (!*s) {
                            if (err) snprintf(err, errn, "a quote is not closed");
                            return -1;
                        }
                        s++;
                        continue;
                    }
                    if (*s == '\\' || *s == '[' || *s == ']' || *s == ';') {
                        if (err) snprintf(err, errn, "'%c' cannot appear outside "
                                                     "quotes in a template", *s);
                        return -1;
                    }
                    if (*s == '=' && !eq && !quotes) eq = s;
                    s++;
                }
                b = s;
                while (b > a && isspace((unsigned char)b[-1])) b--;
                while (a < b && isspace((unsigned char)*a)) a++;

                if (a < b && *a == '$' && !eq &&
                    (size_t)(b - a - 1) == tok_len(a + 1)) {
                    /* A whole-option token: a font or a caption. */
                    size_t len = (size_t)(b - a - 1);
                    int w = font_tok(a + 1, len), ln = text_tok(a + 1, len);
                    if (!tfilters[f].text || (w == -2 && ln < 0)) {
                        if (err) snprintf(err, errn,
                                          tfilters[f].text
                                          ? "$%.*s cannot stand alone — write NAME=$%.*s"
                                          : "a drawbox takes no font or text ($%.*s)",
                                          (int)len, a + 1, (int)len, a + 1);
                        return -1;
                    }
                    if (w != -2) {
                        nfont++;
                        uses |= SS_TT_FONT |
                                (w == -1 ? SS_TT_WEIGHT : SS_TT_FONTW(w));
                        if (cx) {
                            int ww = w == -1 ? cx->weight : w;
                            if (ww < 0 || ww > 4) ww = 0;
                            oprintf(o, "fontfile='%s'", cx->font[ww] ? cx->font[ww] : "");
                        }
                    } else {
                        ntext++;
                        uses |= SS_TT_TEXT | SS_TT_LINE(ln);
                        if (cx) {
                            oprintf(o, "textfile='%s':expansion=none",
                                    cx->textfile[ln] ? cx->textfile[ln] : "");
                        }
                    }
                } else {
                    const char *v;
                    size_t ol, vl;
                    int i, before = uses;
                    if (!eq) {
                        if (err) snprintf(err, errn, "%.*s is not NAME=VALUE",
                                          (int)(b - a), a);
                        return -1;
                    }
                    ol = (size_t)(eq - a);
                    for (i = 0; tfilters[f].opts[i].name; i++)
                        if (tok_is(a, ol, tfilters[f].opts[i].name)) break;
                    if (!tfilters[f].opts[i].name) {
                        if (err) snprintf(err, errn, "%.*s cannot be set on a %s "
                                                     "in a template",
                                          (int)ol, a, fname);
                        return -1;
                    }
                    v = eq + 1;
                    vl = (size_t)(b - v);
                    if (quotes) {
                        /* One quoted run that IS the value. */
                        if (quotes != 1 || vl < 2 || *v != '\'' || b[-1] != '\'') {
                            if (err) snprintf(err, errn, "%.*s: quote the whole "
                                                         "value or none of it",
                                              (int)ol, a);
                            return -1;
                        }
                        v++;
                        vl -= 2;
                    }
                    oput(o, a, ol);
                    oputs(o, quotes ? "='" : "=");
                    uses &= ~TT_MOVING;
                    if (put_value(o, tt, v, vl, tfilters[f].opts[i].kind, vals,
                                  nvals, cx, &uses, err, errn) != 0) {
                        if (err) {
                            char why[160];
                            snprintf(why, sizeof why, "%s", err);
                            snprintf(err, errn, "%s %.*s: %s", fname,
                                     (int)ol, a, why);
                        }
                        return -1;
                    }
                    if (quotes) oputs(o, "'");
                    /* An option that reads a keyable row is one a command may
                     * have to re-send, whole, every frame it changes. */
                    if (meta && (uses & TT_MOVING)) {
                        ss_fx_target *t;
                        if (meta->ntarget >= SS_MAX_FX_TARGETS ||
                            vl >= sizeof t->tmpl) {
                            if (err) snprintf(err, errn, "more than %d options "
                                              "read the size or the colour",
                                              SS_MAX_FX_TARGETS);
                            return -1;
                        }
                        t = &meta->target[meta->ntarget++];
                        t->filt = k;
                        snprintf(t->fname, sizeof t->fname, "%s", fname);
                        snprintf(t->opt, sizeof t->opt, "%.*s", (int)ol, a);
                        memcpy(t->tmpl, v, vl);
                        t->tmpl[vl] = '\0';
                    }
                    uses |= before;
                }
                if (*s != ':') break;
                s++;
                oputs(o, ":");
            }
        }
        if (tfilters[f].text && (nfont != 1 || ntext != 1)) {
            if (err) snprintf(err, errn, "drawtext %d wants one font token and "
                                         "one text token ($font, $line1…)", k);
            return -1;
        }
    }
    if (k < 0) {
        if (err) snprintf(err, errn, "no filter chain");
        return -1;
    }
    if (meta) meta->uses = uses;
    return (o && o->over) ? -1 : 0;
}

int ss_title_read(const char *path, ss_fx *out, char *err, size_t errn)
{
    const char *bad;
    int i;

    if (ss_recipe_parse(path, out, err, errn) != 0) return -1;
    if (!*out->group) snprintf(out->group, sizeof out->group, "Titles");
    if ((bad = ss_recipe_names_file(out->filter)) != NULL) {
        if (err) snprintf(err, errn, "a template may not name a file (%s)", bad);
        return -1;
    }
    for (i = 0; i < out->nparam; i++)
        if (reserved(out->param[i].key, strlen(out->param[i].key))) {
            if (err) snprintf(err, errn, "%s is a token's name, not a parameter's",
                              out->param[i].key);
            return -1;
        }
    return tt_walk(out, out, NULL, 0, NULL, NULL, NULL, err, errn);
}

int ss_title_uses(const ss_fx *tt) { return tt ? tt->uses : 0; }

void ss_title_text_path(char *out, size_t n, const char *dir, int track,
                        int idx, int k)
{
    snprintf(out, n, "%s/title_%d_%d_%d.txt", dir, track, idx, k);
}

int ss_title_expand(const ss_fx *tt, const double *vals, int nvals,
                    const ss_title_ctx *cx, const char *tag, char *out, size_t n)
{
    obuf o = { out, n, 0, 0 };
    if (!tt || !cx || !out || n == 0) return -1;
    *out = '\0';
    return tt_walk(tt, NULL, vals, nvals, cx, tag, &o, NULL, 0);
}

int ss_title_target_arg(const ss_fx *tt, int tg, const double *vals,
                        int nvals, const ss_title_ctx *cx, char *out, size_t n)
{
    obuf o = { out, n, 0, 0 };
    int uses = 0;
    if (!tt || tg < 0 || tg >= tt->ntarget || !cx || !out || n == 0) return -1;
    *out = '\0';
    if (put_value(&o, tt, tt->target[tg].tmpl, strlen(tt->target[tg].tmpl),
                  opt_kind(tt->target[tg].fname, tt->target[tg].opt),
                  vals, nvals, cx, &uses, NULL, 0) != 0)
        return -1;
    return o.over ? -1 : 0;
}

/* ------------------------------------------------------- the catalogue -- */

static ss_fx *tcat;
static int    ntcat, tcatcap, tloaded;

static int tcat_add(const ss_fx *tt)
{
    int i;
    for (i = 0; i < ntcat; i++)
        if (!strcmp(tcat[i].name, tt->name)) { tcat[i] = *tt; return i; }
    if (ntcat >= tcatcap) {
        int want = tcatcap ? tcatcap * 2 : 16;
        ss_fx *nw = realloc(tcat, sizeof(ss_fx) * (size_t)want);
        if (!nw) return -1;
        tcat = nw; tcatcap = want;
    }
    tcat[ntcat] = *tt;
    return ntcat++;
}

static void tload_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char path[1024], err[160];
        const char *dot = strrchr(e->d_name, '.');
        ss_fx tt;
        if (!dot || strcmp(dot, ".syntitle")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (ss_title_read(path, &tt, err, sizeof err) == 0) tcat_add(&tt);
    }
    closedir(d);
}

/* Shipped, then the user's, then SYNSTUDIO_TITLES — a later one of the same
 * name replaces an earlier, so a template is fixed by dropping a copy in your
 * own folder, the rule effects and looks follow. */
int ss_title_load(void)
{
    const char *env = getenv("SYNSTUDIO_TITLES");
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char buf[1024];

    if (tloaded) return ntcat;
    tloaded = 1;
    snprintf(buf, sizeof buf, "%s/titles", SYNSTUDIO_DATADIR);
    tload_dir(buf);
    if (xdg && *xdg) {
        snprintf(buf, sizeof buf, "%s/synstudio/titles", xdg);
        tload_dir(buf);
    } else if (home && *home) {
        snprintf(buf, sizeof buf, "%s/.config/synstudio/titles", home);
        tload_dir(buf);
    }
    if (env && *env) {
        char *copy = strdup(env), *p, *save;
        if (copy) {
            for (p = strtok_r(copy, ":", &save); p; p = strtok_r(NULL, ":", &save))
                if (*p) tload_dir(p);
            free(copy);
        }
    }
    return ntcat;
}

int ss_title_count(void)          { ss_title_load(); return ntcat; }
const ss_fx *ss_title_at(int i)   { ss_title_load();
                                    return (i >= 0 && i < ntcat) ? &tcat[i] : NULL; }

const ss_fx *ss_title_find(const char *name)
{
    int i;
    if (!name || !*name) return NULL;
    ss_title_load();
    for (i = 0; i < ntcat; i++)
        if (!strcmp(tcat[i].name, name)) return &tcat[i];
    return NULL;
}
