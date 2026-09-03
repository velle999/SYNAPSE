/*
 * expand.c — word splitting and expansion. See expand.h for the four bugs
 * this replaced and for what is deliberately out of scope.
 *
 * The shape of it:
 *
 *   scan the line once, character by character, tracking quote state;
 *   build each word into a buffer WITH A PARALLEL MASK saying which bytes came
 *   from an unquoted expansion; at the end of the word, split it on whitespace
 *   that the mask says is splittable, and glob each field that came out.
 *
 * ⚠ THE MASK IS THE WHOLE TRICK, and it is why this is not two passes. Field
 * splitting has to happen to the RESULT of an expansion but not to the literal
 * text around it: `pacman -Rns $(pacman -Qtdq)` must become one argument per
 * orphan, while `echo "a b"` and `ls My Documents/x` must not be re-split. By
 * the time a word is a finished string those two cases are indistinguishable —
 * so the distinction is recorded as the word is built, at the only moment it
 * is still known.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <sys/wait.h>

#include "synsh.h"
#include "expand.h"
#include "exec.h"
#include "i18n.h"

/* ── A word under construction ────────────────────────────── */
typedef struct {
    char   *b;        /* the bytes */
    char   *mask;     /* 1 = this byte may be split on; in step with b */
    size_t  n, cap;
    bool    quoted;   /* any part of this word was quoted or escaped */
    bool    started;  /* distinguishes "" (a real empty word) from nothing */
} wordbuf;

static bool wb_grow(wordbuf *w, size_t need)
{
    if (w->n + need + 1 <= w->cap) return true;
    size_t cap = w->cap ? w->cap * 2 : 128;
    while (cap < w->n + need + 1) cap *= 2;
    char *b = realloc(w->b, cap);
    char *m = realloc(w->mask, cap);
    if (b) w->b = b;
    if (m) w->mask = m;
    if (!b || !m) return false;
    w->cap = cap;
    return true;
}

static void wb_put(wordbuf *w, const char *s, size_t n, bool splittable)
{
    if (!n) return;
    if (!wb_grow(w, n)) return;
    memcpy(w->b + w->n, s, n);
    memset(w->mask + w->n, splittable ? 1 : 0, n);
    w->n += n;
    w->b[w->n] = '\0';
    w->started = true;
}

static void wb_putc(wordbuf *w, char c, bool splittable)
{
    wb_put(w, &c, 1, splittable);
}

static void wb_reset(wordbuf *w)
{
    w->n = 0;
    w->quoted = false;
    w->started = false;
    if (w->b) w->b[0] = '\0';
}

static void wb_free(wordbuf *w)
{
    free(w->b);
    free(w->mask);
    memset(w, 0, sizeof(*w));
}

/* ── The output list ──────────────────────────────────────── */
static bool words_push(synsh_words_t *o, char *s, int is_op)
{
    if (o->n + 1 >= o->cap) {
        int cap = o->cap ? o->cap * 2 : 16;
        char **v = realloc(o->v, (size_t)cap * sizeof(*v));
        int   *p = realloc(o->op, (size_t)cap * sizeof(*p));
        if (v) o->v = v;
        if (p) o->op = p;
        if (!v || !p) { free(s); return false; }
        o->cap = cap;
    }
    o->v[o->n]  = s;
    o->op[o->n] = is_op;
    o->n++;
    o->v[o->n]  = NULL;
    return true;
}

void synsh_words_free(synsh_words_t *w)
{
    if (!w) return;
    for (int i = 0; i < w->n; i++) free(w->v[i]);
    free(w->v);
    free(w->op);
    memset(w, 0, sizeof(*w));
}

/* ── Command substitution ─────────────────────────────────── */
/*
 * `$(cmd)` and `` `cmd` `` run through synsh's OWN executor, in a child, with
 * stdout on a pipe. Not popen(): popen would hand the command to /bin/sh,
 * which is a different shell with different built-ins — `$(syn status)` would
 * fail there, and an alias defined in this session would not exist. Running it
 * here means a substitution sees exactly the shell it was typed into.
 *
 * stdout is flushed before the fork or anything still sitting in this
 * process's buffer would be written twice, once by each side.
 */
static char *capture(synsh_state_t *s, const char *cmd)
{
    int fds[2];
    if (pipe(fds) < 0) return NULL;

    fflush(NULL);

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return NULL; }

    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        if (fds[1] > STDERR_FILENO) close(fds[1]);
        _exit(execute_command_line(s, cmd) & 0xff);
    }

    close(fds[1]);

    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fds[0]); waitpid(pid, NULL, 0); return NULL; }

    for (;;) {
        if (n + 1024 > cap) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) break;
            buf = nb; cap *= 2;
        }
        ssize_t r = read(fds[0], buf + n, cap - n - 1);
        if (r <= 0) break;
        n += (size_t)r;
    }
    buf[n] = '\0';
    close(fds[0]);

    int st = 0;
    waitpid(pid, &st, 0);
    /* The substitution's status is the shell's, exactly as in POSIX — this is
     * what makes `x=$(false)` observable through $?. */
    s->last_exit = WIFEXITED(st) ? WEXITSTATUS(st) : 1;

    /* Trailing newlines are stripped; interior ones are not, because they are
     * what field splitting turns into separate arguments. */
    while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return buf;
}

/* ── Parameter expansion ──────────────────────────────────── */
/*
 * Returns a malloc'd value for the parameter starting at *p (which points just
 * past the '$'), advancing *p past it. Returns NULL and does not advance if
 * what follows is not a parameter at all, so `echo 100$` keeps its dollar.
 */
static char *param(synsh_state_t *s, const char **p)
{
    const char *q = *p;
    char name[128];
    size_t i = 0;

    if (*q == '{') {
        q++;
        while (*q && *q != '}' && i < sizeof(name) - 1) name[i++] = *q++;
        if (*q != '}') return NULL;          /* unterminated — leave it alone */
        q++;
        name[i] = '\0';
    } else if (*q == '?') {
        q++;
        char *r; if (asprintf(&r, "%d", s->last_exit) < 0) return NULL;
        *p = q; return r;
    } else if (*q == '$') {
        q++;
        char *r; if (asprintf(&r, "%d", (int)getpid()) < 0) return NULL;
        *p = q; return r;
    } else if (*q == '#') {
        q++;
        *p = q; return strdup("0");          /* no positional parameters */
    } else if (isdigit((unsigned char)*q)) {
        /* $0 is the shell; there are no others, and POSIX says an unset
         * parameter is the empty string. */
        bool zero = (*q == '0');
        q++;
        *p = q;
        return strdup(zero ? "synsh" : "");
    } else if (isalpha((unsigned char)*q) || *q == '_') {
        while ((isalnum((unsigned char)*q) || *q == '_') && i < sizeof(name) - 1)
            name[i++] = *q++;
        name[i] = '\0';
    } else {
        return NULL;
    }

    *p = q;
    const char *v = getenv(name);
    return strdup(v ? v : "");
}

/* ── Finishing a word: field splitting, then globbing ─────── */

static bool has_glob_meta(const char *s)
{
    return strpbrk(s, "*?[") != NULL;
}

/* Push one finished field, globbing it if it can be globbed. */
static bool push_field(synsh_words_t *out, const char *field, bool quoted)
{
    if (!quoted && has_glob_meta(field)) {
        glob_t g;
        /* GLOB_NOCHECK: a pattern that matches nothing stays itself, which is
         * what every shell does by default and what `ls *.c` in an empty
         * directory has to keep doing — the error belongs to ls, which can say
         * which file it wanted. */
        if (glob(field, GLOB_NOCHECK, NULL, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; i++) {
                char *d = strdup(g.gl_pathv[i]);
                if (!d || !words_push(out, d, 0)) { globfree(&g); return false; }
            }
            globfree(&g);
            return true;
        }
        globfree(&g);
    }
    char *d = strdup(field);
    return d && words_push(out, d, 0);
}

/*
 * A REDIRECTION TARGET IS NOT FIELD-SPLIT AND NOT GLOBBED, and it has to be
 * decided here rather than afterwards.
 *
 * `> $f` names ONE file even when $f holds two words. bash calls that an
 * ambiguous redirect and refuses; synsh takes it as a filename with a space in
 * it, which is the more useful of the two answers here and never the more
 * surprising one — the alternative a split would give is writing to the first
 * word and handing the second to the command as an argument, which is a file
 * nobody named and a command nobody typed.
 *
 * ⚠ AND EXPANDING IT LATER IS NOT THE SAME THING. The first version of this
 * left the target to be expanded a second time by the caller, which meant a
 * file legitimately called `a$b` — quoted at the prompt, so expanded to itself
 * here — went through expansion again on the way out and became `a`. One
 * expansion, at the point where the quoting is still known.
 */
static bool finish_word_as(synsh_words_t *out, wordbuf *w, bool target)
{
    if (!w->started) return true;      /* nothing was accumulated */

    if (target) {
        char *d = strndup(w->b, w->n);
        return d && words_push(out, d, 0);
    }

    /* Split on whitespace the mask says came from an unquoted expansion. */
    size_t i = 0;
    bool any = false;
    while (i < w->n) {
        while (i < w->n && w->mask[i] && isspace((unsigned char)w->b[i])) i++;
        if (i >= w->n) break;
        size_t start = i;
        while (i < w->n && !(w->mask[i] && isspace((unsigned char)w->b[i]))) i++;

        char save = w->b[i];
        w->b[i] = '\0';
        bool ok = push_field(out, w->b + start, w->quoted);
        w->b[i] = save;
        if (!ok) return false;
        any = true;
    }

    /*
     * An unquoted expansion that came back empty produces NO word — `rm $x`
     * with x unset is `rm`, not `rm ""`. But `rm ""` itself must survive, and
     * that is the one thing `quoted` is asked to remember here.
     */
    if (!any && w->quoted) {
        char *d = strdup("");
        if (!d || !words_push(out, d, 0)) return false;
    }
    return true;
}

/*
 * Does this operator take a file after it? `&`, the fd duplications (`2>&1`)
 * and the closes (`2>&-`) do not — the thing after them is the next command's
 * argument, and treating it as a filename would swallow it.
 */
static bool op_takes_target(const char *op)
{
    if (strcmp(op, "&") == 0) return false;
    return strchr(op, '&') == NULL || strncmp(op, "&>", 2) == 0;
}

/* ── The scanner ──────────────────────────────────────────── */

/* Read the body of a $( ) or ` `, honouring nesting and quotes inside it.
 * Returns a malloc'd body and advances *p past the terminator, or NULL. */
static char *sub_body(const char **p, char open, char close)
{
    const char *q = *p;
    int depth = 1;
    const char *start = q;
    char quote = 0;

    while (*q) {
        if (quote) {
            if (*q == quote) quote = 0;
            else if (*q == '\\' && quote == '"' && q[1]) q++;
        } else if (*q == '\'' || *q == '"') {
            quote = *q;
        } else if (*q == '\\' && q[1]) {
            q++;
        } else if (open && *q == open) {
            depth++;
        } else if (*q == close) {
            if (--depth == 0) {
                char *body = strndup(start, (size_t)(q - start));
                *p = q + 1;
                return body;
            }
        }
        q++;
    }
    return NULL;   /* unterminated */
}

/* Everything a '$' can introduce, appended to the word. */
static void expand_dollar(synsh_state_t *s, wordbuf *w, const char **p,
                          bool splittable)
{
    const char *q = *p;   /* points at the '$' */
    q++;

    if (*q == '(') {
        q++;
        char *body = sub_body(&q, '(', ')');
        if (!body) { wb_putc(w, '$', false); *p += 1; return; }
        char *val = capture(s, body);
        free(body);
        if (val) { wb_put(w, val, strlen(val), splittable); free(val); }
        *p = q;
        return;
    }

    char *val = param(s, &q);
    if (!val) { wb_putc(w, '$', false); *p += 1; return; }
    wb_put(w, val, strlen(val), splittable);
    free(val);
    *p = q;
}

/*
 * A redirection operator, or NULL if what is here is not one.
 *
 * `fd_word` is the word built so far: when it is nothing but digits it is the
 * file descriptor the operator applies to ("2>"), and it is consumed rather
 * than emitted as an argument. This is the only place in the scanner where a
 * character changes the meaning of what came before it.
 */
static char *redir_op(const char **p, wordbuf *w, bool *consumed_word)
{
    const char *q = *p;
    char fd[8] = "";
    *consumed_word = false;

    if (*q == '<' || *q == '>') {
        /* A pure-digit word in front of the operator is its fd. */
        if (w->started && !w->quoted && w->n && w->n < sizeof(fd)) {
            bool digits = true;
            for (size_t i = 0; i < w->n; i++)
                if (!isdigit((unsigned char)w->b[i])) { digits = false; break; }
            if (digits) {
                memcpy(fd, w->b, w->n);
                fd[w->n] = '\0';
                *consumed_word = true;
            }
        }
    } else if (*q == '&' && q[1] == '>') {
        /* &> — both streams to one file. */
        q += 2;
        char *op;
        if (*q == '>') { q++; op = strdup("&>>"); }
        else             op = strdup("&>");
        *p = q;
        return op;
    } else {
        return NULL;
    }

    char buf[16];
    size_t n = 0;
    buf[n++] = *q++;                      /* '<' or '>' */
    if (buf[0] == '>' && *q == '>') buf[n++] = *q++;      /* >> */
    if (*q == '&') {                                      /* >&2, <&0, >&- */
        buf[n++] = *q++;
        while ((isdigit((unsigned char)*q) || *q == '-') && n < sizeof(buf) - 1)
            buf[n++] = *q++;
    }
    buf[n] = '\0';
    *p = q;

    char *op;
    if (asprintf(&op, "%s%s", fd, buf) < 0) return NULL;
    return op;
}

int synsh_words_split(synsh_state_t *s, const char *line, synsh_words_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!line) return 0;

    wordbuf w = {0};
    const char *p = line;
    int rc = 0;
    bool target_next = false;   /* the word after a redirection operator */

    while (*p) {
        if (isspace((unsigned char)*p)) {
            if (w.started) {
                if (!finish_word_as(out, &w, target_next)) { rc = -1; goto done; }
                target_next = false;
            }
            wb_reset(&w);
            p++;
            continue;
        }

        if (*p == '\'') {
            p++;
            const char *start = p;
            while (*p && *p != '\'') p++;
            wb_put(&w, start, (size_t)(p - start), false);
            w.quoted = true;
            w.started = true;
            if (*p) p++;
            else {
                fprintf(stderr, T(M_UNTERMINATED), "'");
                rc = -1; goto done;
            }
            continue;
        }

        if (*p == '"') {
            p++;
            w.quoted = true;
            w.started = true;
            while (*p && *p != '"') {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\' ||
                                   p[1] == '$'  || p[1] == '`')) {
                    wb_putc(&w, p[1], false);
                    p += 2;
                } else if (*p == '$') {
                    /* Inside quotes an expansion is NOT field-split — that is
                     * the entire point of writing "$x" rather than $x. */
                    expand_dollar(s, &w, &p, false);
                } else if (*p == '`') {
                    p++;
                    char *body = sub_body(&p, 0, '`');
                    if (!body) { fprintf(stderr, T(M_UNTERMINATED), "`"); rc = -1; goto done; }
                    char *val = capture(s, body);
                    free(body);
                    if (val) { wb_put(&w, val, strlen(val), false); free(val); }
                } else {
                    wb_putc(&w, *p++, false);
                }
            }
            if (*p) p++;
            else {
                fprintf(stderr, T(M_UNTERMINATED), "\"");
                rc = -1; goto done;
            }
            continue;
        }

        if (*p == '\\' && p[1]) {
            wb_putc(&w, p[1], false);
            w.quoted = true;          /* an escaped * is not a glob */
            p += 2;
            continue;
        }

        if (*p == '$') { expand_dollar(s, &w, &p, true); continue; }

        if (*p == '`') {
            p++;
            char *body = sub_body(&p, 0, '`');
            if (!body) { fprintf(stderr, T(M_UNTERMINATED), "`"); rc = -1; goto done; }
            char *val = capture(s, body);
            free(body);
            if (val) { wb_put(&w, val, strlen(val), true); free(val); }
            continue;
        }

        if (*p == '<' || *p == '>' || (*p == '&' && p[1] == '>')) {
            bool consumed;
            char *op = redir_op(&p, &w, &consumed);
            if (op) {
                if (!consumed) {
                    if (!finish_word_as(out, &w, target_next)) { free(op); rc = -1; goto done; }
                }
                wb_reset(&w);
                target_next = op_takes_target(op);
                if (!words_push(out, op, 1)) { rc = -1; goto done; }
                continue;
            }
        }

        if (*p == '&') {
            /* A lone '&' reaching here is a background marker; the list layer
             * above normally takes it first, so this is the belt to that
             * braces. Either way it is an operator, never an argument. */
            if (!finish_word_as(out, &w, target_next)) { rc = -1; goto done; }
            target_next = false;
            wb_reset(&w);
            char *op = strdup("&");
            if (!op || !words_push(out, op, 1)) { rc = -1; goto done; }
            p++;
            continue;
        }

        /* Tilde, but only at the very start of a word and only bare or before
         * a slash: `~/x` is a home directory and `a~b` is a filename. */
        if (*p == '~' && !w.started &&
            (p[1] == '\0' || p[1] == '/' || isspace((unsigned char)p[1]))) {
            const char *home = s->home ? s->home : getenv("HOME");
            if (home) { wb_put(&w, home, strlen(home), false); p++; continue; }
        }

        wb_putc(&w, *p++, false);
    }

    if (!finish_word_as(out, &w, target_next)) rc = -1;

done:
    wb_free(&w);
    if (rc < 0) synsh_words_free(out);
    return rc;
}

