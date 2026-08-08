/*
 * calc.c — the calculator panel (Super+X)
 *
 * A compositor-drawn calculator: an expression line you type into, the answer
 * under it, a tape of what you worked out before, and a keypad for the mouse.
 *
 * WHY A PANEL AND NOT A LAUNCHER ENTRY
 *
 * "What is 1440 * 0.8" is a question you ask in the middle of doing something
 * else, and every second of it is spent getting a calculator on screen and out
 * of the way again. A desktop application means a window, which means a
 * placement, a titlebar, a taskbar entry and something to close afterwards —
 * the tiling layouts will even reshuffle the desktop to make room for it. This
 * is modal chrome instead: it is up on one key, it is gone on Esc, and nothing
 * that was on the desktop moved while it was there.
 *
 * THE ENTRY LINE IS THE CALCULATOR. THE KEYPAD IS FOR THE MOUSE.
 *
 * The keyboard never touches the keypad — there is no arrowing to "7". Typing
 * goes into the expression, exactly as it does in the emoji picker's search box
 * and the command bar, because a keyboard in front of an expression parser is
 * already the better calculator: `(1440-32)*0.8` is one line, and clicking the
 * same thing is eleven presses.
 *
 * The grid exists because the panel pointer contract says every panel is
 * clickable, and because a calculator with no buttons is a thing people
 * distrust on sight. Hover selects a key, a click presses it, and everything it
 * can do the keyboard can also do.
 *
 * WHAT IT UNDERSTANDS
 *
 * A real expression, not a four-function chain: + - * / % ^, parentheses, unary
 * minus, the constants pi and e, and the functions listed in calc_funcs[]. `ans`
 * is the previous answer, so a long calculation can be done in stages.
 *
 * Precedence is the ordinary one and ^ binds right, so 2^3^2 is 512 and not 64.
 * Trigonometry is in RADIANS, which is what C's libm speaks; rad() and deg()
 * convert, so `sin(rad(30))` is the degrees spelling rather than a mode switch
 * hidden behind a button. A mode would have to be shown, remembered and
 * persisted, and would still be wrong half the time you opened the panel.
 *
 * strtod() reads the numbers, which means C's hex-float literals come along for
 * free: `0x1f` is 31. That is a side effect rather than a design, but it is a
 * useful one on this desktop and there is no reason to spend code rejecting it.
 *
 * ERRORS KEEP THE EXPRESSION
 *
 * A failed evaluation leaves what you typed alone and puts the reason on the
 * status line. The alternative — clearing on error, as a pocket calculator does
 * — throws away the twenty characters that were right in order to punish the
 * one that was not.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "synui.h"

/* ── The evaluator ───────────────────────────────────────────
 *
 * Recursive descent over the string, no allocation and no tokeniser: the
 * grammar is small enough that the parser reading characters directly is
 * shorter than the lexer would be, and every function below is one line of the
 * grammar in the header.
 *
 *   expr    := term (('+' | '-') term)*
 *   term    := unary (('*' | '/' | '%') unary)*
 *   unary   := ('-' | '+') unary | power
 *   power   := primary ('^' unary)?
 *   primary := number | name | name '(' expr ')' | '(' expr ')'
 *
 * `unary` sits ABOVE `power` and `power`'s exponent goes back through `unary`.
 * That ordering is the whole reason -2^2 is -4 (the minus applies to the
 * result) while 2^-1 is 0.5 (the minus applies to the exponent), which is what
 * every other calculator and every maths textbook does. Swapping the two — the
 * obvious arrangement, with unary underneath — gets both of them wrong.
 */

typedef struct {
    const char *p;     /* the read cursor */
    double      ans;   /* what `ans` resolves to */
    const char *err;   /* first failure; parsing continues but the result is
                        * discarded, so nothing has to unwind */
} calc_parse_t;

static double calc_expr(calc_parse_t *ps);

static void calc_fail(calc_parse_t *ps, const char *msg)
{
    if (!ps->err) ps->err = msg;   /* the FIRST error is the useful one */
}

static void calc_skip_space(calc_parse_t *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t') ps->p++;
}

/* One-argument functions, by the name typed. Kept as a table rather than an
 * if-chain so the panel's own help line and this list cannot disagree about
 * what exists — calc_func_hint() below builds that line FROM this table, so a
 * function added here is a function the panel offers to teach. */
static const struct {
    const char *name;
    double (*fn)(double);
} calc_funcs[] = {
    { "sqrt",  sqrt  },
    { "cbrt",  cbrt  },
    { "abs",   fabs  },
    { "exp",   exp   },
    { "ln",    log   },
    { "log",   log10 },
    { "log2",  log2  },
    { "sin",   sin   },
    { "cos",   cos   },
    { "tan",   tan   },
    { "asin",  asin  },
    { "acos",  acos  },
    { "atan",  atan  },
    { "floor", floor },
    { "ceil",  ceil  },
    { "round", round },
    { "trunc", trunc },
    /* Degrees are a conversion, not a mode. See the header. */
    { "rad",   NULL  },
    { "deg",   NULL  },
};

/* The two entries above with no libm function behind them. Spelled here rather
 * than as file-static wrappers only because a wrapper per unit conversion is
 * more ceremony than the conversion. */
static double calc_apply_func(size_t i, double v)
{
    if (strcmp(calc_funcs[i].name, "rad") == 0) return v * M_PI / 180.0;
    if (strcmp(calc_funcs[i].name, "deg") == 0) return v * 180.0 / M_PI;
    return calc_funcs[i].fn(v);
}

static double calc_primary(calc_parse_t *ps)
{
    calc_skip_space(ps);

    if (*ps->p == '(') {
        ps->p++;
        double v = calc_expr(ps);
        calc_skip_space(ps);
        if (*ps->p == ')') ps->p++;
        else               calc_fail(ps, "missing )");
        return v;
    }

    if (isdigit((unsigned char)*ps->p) || *ps->p == '.') {
        char *end = NULL;
        double v = strtod(ps->p, &end);
        if (end == ps->p) { calc_fail(ps, "not a number"); return 0.0; }
        ps->p = end;
        return v;
    }

    if (isalpha((unsigned char)*ps->p)) {
        /* Bounded by the buffer, not by the input: an unbroken run of letters
         * longer than any name we know is a typo, and truncating it here still
         * reaches the "unknown name" arm below with something printable. */
        char name[16];
        size_t n = 0;
        while (isalpha((unsigned char)*ps->p) || isdigit((unsigned char)*ps->p)) {
            if (n + 1 < sizeof(name)) name[n++] = (char)tolower((unsigned char)*ps->p);
            ps->p++;
        }
        name[n] = '\0';

        if (strcmp(name, "pi") == 0)  return M_PI;
        if (strcmp(name, "e") == 0)   return M_E;
        if (strcmp(name, "ans") == 0) return ps->ans;

        for (size_t i = 0; i < sizeof(calc_funcs) / sizeof(calc_funcs[0]); i++) {
            if (strcmp(name, calc_funcs[i].name) != 0) continue;

            calc_skip_space(ps);
            if (*ps->p != '(') { calc_fail(ps, "expected ( after a function"); return 0.0; }
            ps->p++;
            double v = calc_expr(ps);
            calc_skip_space(ps);
            if (*ps->p == ')') ps->p++;
            else               calc_fail(ps, "missing )");
            return calc_apply_func(i, v);
        }

        calc_fail(ps, "unknown name");
        return 0.0;
    }

    calc_fail(ps, *ps->p ? "unexpected character" : "unfinished expression");
    return 0.0;
}

static double calc_unary(calc_parse_t *ps);

static double calc_power(calc_parse_t *ps)
{
    double base = calc_primary(ps);
    calc_skip_space(ps);
    if (*ps->p != '^') return base;

    ps->p++;
    /* Back through unary, and recursively: right associativity, and the reason
     * 2^-1 parses at all. */
    return pow(base, calc_unary(ps));
}

static double calc_unary(calc_parse_t *ps)
{
    calc_skip_space(ps);
    if (*ps->p == '-') { ps->p++; return -calc_unary(ps); }
    if (*ps->p == '+') { ps->p++; return  calc_unary(ps); }
    return calc_power(ps);
}

static double calc_term(calc_parse_t *ps)
{
    double v = calc_unary(ps);

    for (;;) {
        calc_skip_space(ps);
        char op = *ps->p;
        if (op != '*' && op != '/' && op != '%') return v;
        ps->p++;

        double rhs = calc_unary(ps);
        if ((op == '/' || op == '%') && rhs == 0.0) {
            /* Caught here rather than left to produce an infinity, because
             * "inf" as an answer is a thing the user has to interpret and
             * "division by zero" is a thing they can act on. */
            calc_fail(ps, "division by zero");
            return 0.0;
        }

        if      (op == '*') v *= rhs;
        else if (op == '/') v /= rhs;
        else                v = fmod(v, rhs);
    }
}

static double calc_expr(calc_parse_t *ps)
{
    double v = calc_term(ps);

    for (;;) {
        calc_skip_space(ps);
        char op = *ps->p;
        if (op != '+' && op != '-') return v;
        ps->p++;
        double rhs = calc_term(ps);
        if (op == '+') v += rhs;
        else           v -= rhs;
    }
}

bool calc_eval(const char *expr, double ans, double *out, const char **err)
{
    const char *dummy = NULL;
    if (!err) err = &dummy;
    *err = NULL;

    if (!expr) { *err = "nothing to work out"; return false; }

    calc_parse_t ps = { .p = expr, .ans = ans, .err = NULL };
    double v = calc_expr(&ps);

    calc_skip_space(&ps);
    /* Anything left over is a typo, not a second expression. `2 3` and `2pi`
     * both land here — this parser has no implicit multiplication, and quietly
     * dropping the tail would answer a question nobody asked. */
    if (!ps.err && *ps.p) calc_fail(&ps, "unexpected character");

    if (ps.err) { *err = ps.err; return false; }

    /* sqrt(-1), ln(0), 1e308*10 — every domain error and every overflow libm
     * has arrives here as a non-finite double rather than as a return code, so
     * this one check stands in for a domain test per function. */
    if (!isfinite(v)) { *err = "undefined"; return false; }

    if (out) *out = v;
    return true;
}

/*
 * The answer as text.
 *
 * %.12g rather than %f: it keeps 6.02e23 readable, prints 1/4 as 0.25 instead
 * of 0.250000, and prints an integral result with no decimal point at all — the
 * three shapes a calculator's answer actually takes. Twelve significant digits
 * is a double's honest precision for the sums people type; %.17g would make
 * 0.1+0.2 read as 0.30000000000000004, which is true, correct, and not what the
 * panel is being asked.
 */
void calc_format(double v, char *buf, size_t n)
{
    /* -0.0 formats as "-0", which is arithmetically right and reads as a bug. */
    if (v == 0.0) v = 0.0;
    snprintf(buf, n, "%.12g", v);
}

/*
 * Every function name, space-separated, for the panel's help line.
 *
 * Built from calc_funcs[] rather than written out beside it: a second list
 * would be right on the day it was typed and wrong at the next function, and a
 * calculator whose help mentions something it does not have is worse than one
 * with no help at all. Built once — the table is const, so the answer cannot
 * change between calls.
 */
const char *calc_func_hint(void)
{
    static char hint[256];
    if (hint[0]) return hint;

    size_t o = 0;
    for (size_t i = 0; i < sizeof(calc_funcs) / sizeof(calc_funcs[0]); i++) {
        int n = snprintf(hint + o, sizeof(hint) - o, "%s%s",
                         i ? " " : "", calc_funcs[i].name);
        if (n < 0 || (size_t)n >= sizeof(hint) - o) break;
        o += (size_t)n;
    }
    return hint;
}

/* ── The keypad ──────────────────────────────────────────────
 *
 * One table, drawn by render.c and pressed by calc_press(). The labels are
 * ASCII words rather than the usual calculator glyphs — no ×, ÷, √ or ⌫. The
 * UI font is whatever fontpick last set, a family with no U+232B draws a
 * missing-glyph box, and a button whose label is a box is worse than one
 * spelled "del". The same argument as the control panel's hints.
 *
 * The label is also what gets typed for every plain key, so a button and the
 * keyboard cannot drift into meaning different things.
 */

typedef enum {
    CALC_BTN_INSERT = 0,   /* append `insert` (or the label) to the expression */
    CALC_BTN_DELETE,       /* one character back */
    CALC_BTN_CLEAR,        /* the whole expression */
    CALC_BTN_COPY,         /* the answer to the clipboard */
    CALC_BTN_EQUALS,       /* evaluate — the Enter key */
} calc_btn_kind_t;

static const struct {
    const char     *label;
    const char     *insert;   /* NULL: the label is what gets typed */
    calc_btn_kind_t kind;
} calc_buttons[] = {
    { "(",    NULL,    CALC_BTN_INSERT },
    { ")",    NULL,    CALC_BTN_INSERT },
    { "^",    NULL,    CALC_BTN_INSERT },
    { "%",    NULL,    CALC_BTN_INSERT },
    { "del",  NULL,    CALC_BTN_DELETE },

    { "7",    NULL,    CALC_BTN_INSERT },
    { "8",    NULL,    CALC_BTN_INSERT },
    { "9",    NULL,    CALC_BTN_INSERT },
    { "/",    NULL,    CALC_BTN_INSERT },
    { "C",    NULL,    CALC_BTN_CLEAR  },

    { "4",    NULL,    CALC_BTN_INSERT },
    { "5",    NULL,    CALC_BTN_INSERT },
    { "6",    NULL,    CALC_BTN_INSERT },
    { "*",    NULL,    CALC_BTN_INSERT },
    { "ans",  NULL,    CALC_BTN_INSERT },

    { "1",    NULL,    CALC_BTN_INSERT },
    { "2",    NULL,    CALC_BTN_INSERT },
    { "3",    NULL,    CALC_BTN_INSERT },
    { "-",    NULL,    CALC_BTN_INSERT },
    /* The functions open their own bracket: a click that left you to type "("
     * would be a button that does most of a job. */
    { "sqrt", "sqrt(", CALC_BTN_INSERT },

    { "0",    NULL,    CALC_BTN_INSERT },
    { ".",    NULL,    CALC_BTN_INSERT },
    { "e",    NULL,    CALC_BTN_INSERT },
    { "+",    NULL,    CALC_BTN_INSERT },
    { "pi",   NULL,    CALC_BTN_INSERT },

    { "copy", NULL,    CALC_BTN_COPY   },
    { "ln",   "ln(",   CALC_BTN_INSERT },
    { "log",  "log(",  CALC_BTN_INSERT },
    { "abs",  "abs(",  CALC_BTN_INSERT },
    { "=",    NULL,    CALC_BTN_EQUALS },
};

int calc_button_count(void)
{
    return (int)(sizeof(calc_buttons) / sizeof(calc_buttons[0]));
}

const char *calc_button_label(int i)
{
    if (i < 0 || i >= calc_button_count()) return "";
    return calc_buttons[i].label;
}

/* Is this key an action rather than something to type? render.c tints those
 * differently, and asking here keeps the kinds in one file. */
int calc_button_is_action(int i)
{
    if (i < 0 || i >= calc_button_count()) return 0;
    return calc_buttons[i].kind != CALC_BTN_INSERT;
}

/* ── The tape ────────────────────────────────────────────────
 *
 * ~/.config/synui/calc.history, newest first, one `expression<TAB>answer` line
 * each. A file for the emoji recents' reason: a calculator that forgets the
 * figure you worked out ten minutes ago is one you have to redo the work in.
 *
 * The ANSWER IS STORED, not recomputed on load. Re-deriving it would be tidier
 * right up to the first line containing `ans`, which resolves against whatever
 * the previous answer is TODAY — so a transcript that re-ran itself would show
 * numbers that were never on screen. This is a record of what happened.
 */

static bool calc_history_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "calc.history");
}

/*
 * Copy `src` into `dst` keeping only what the entry box itself accepts.
 *
 * Load-bearing on the way in from disk: calc.history is a plain file that can
 * be edited by hand or corrupted, and syn_show_text() puts its entire cairo
 * context into a permanent error state when handed invalid UTF-8 — one bad byte
 * in the tape would blank every row drawn after it, panel chrome included. The
 * expression grammar is ASCII in its entirety, so "printable ASCII only" is
 * both the strictest filter available and exactly the right one.
 */
static void calc_ascii_copy(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c >= 0x20 && c <= 0x7e) dst[o++] = (char)c;
    }
    dst[o] = '\0';
}

static void calc_history_load(syn_server_t *s)
{
    s->calc.hist_count = 0;

    char path[256];
    if (!calc_history_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no file yet is the normal first-run state */

    char line[CALC_ENTRY_MAX + CALC_RESULT_MAX + 8];
    while (s->calc.hist_count < CALC_HISTORY_MAX && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';

        char *tab = strchr(line, '\t');
        if (!tab) continue;    /* not a record this panel wrote */
        *tab = '\0';

        syn_calc_entry_t *h = &s->calc.hist[s->calc.hist_count];
        calc_ascii_copy(h->expr,   sizeof(h->expr),   line);
        calc_ascii_copy(h->result, sizeof(h->result), tab + 1);
        if (h->expr[0] && h->result[0]) s->calc.hist_count++;
    }
    fclose(f);
}

static void calc_history_save(syn_server_t *s)
{
    char path[256];
    if (!calc_history_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: calc: cannot write '%s'", path);
        return;
    }
    for (int i = 0; i < s->calc.hist_count; i++)
        fprintf(f, "%s\t%s\n", s->calc.hist[i].expr, s->calc.hist[i].result);
    fclose(f);
}

/* Newest first, and the same expression never appears twice: working the same
 * sum out again is how you CHECK it, and a tape that answered by filling up
 * with five identical lines would push the thing you wanted off the end. */
static void calc_history_push(syn_server_t *s, const char *expr, const char *result)
{
    syn_calc_panel_t *c = &s->calc;

    /* Copied before anything shifts, for emoji_recent_push()'s reason: on the
     * recall path `expr` can point into hist[], which the rotation below is
     * about to overwrite. */
    syn_calc_entry_t keep;
    snprintf(keep.expr,   sizeof(keep.expr),   "%s", expr);
    snprintf(keep.result, sizeof(keep.result), "%s", result);

    int existing = -1;
    for (int i = 0; i < c->hist_count; i++)
        if (strcmp(c->hist[i].expr, keep.expr) == 0) { existing = i; break; }

    int from = existing >= 0 ? existing : c->hist_count;
    if (from >= CALC_HISTORY_MAX) from = CALC_HISTORY_MAX - 1;

    for (int i = from; i > 0; i--)
        c->hist[i] = c->hist[i - 1];

    c->hist[0] = keep;
    if (existing < 0 && c->hist_count < CALC_HISTORY_MAX) c->hist_count++;

    calc_history_save(s);
}

/*
 * The tape line drawn at row `r`, 0 being the TOP of the tape.
 *
 * hist[] is newest first and the tape reads oldest-to-newest downward, so the
 * newest line sits against the expression box you are typing in — a paper tape,
 * and the same direction as every terminal. The reversal lives here rather than
 * in render.c because the row arithmetic and the array order are one fact.
 *
 * Returns 0 for a row with nothing in it (a short history, drawn top-aligned
 * against the title rather than floating).
 */
int calc_tape_row(syn_server_t *s, int r, const char **expr, const char **result)
{
    if (expr)   *expr   = "";
    if (result) *result = "";
    if (r < 0 || r >= CALC_TAPE_ROWS) return 0;

    int shown = s->calc.hist_count < CALC_TAPE_ROWS ? s->calc.hist_count
                                                    : CALC_TAPE_ROWS;
    /* Fewer entries than rows: leave the top rows blank so the newest is still
     * the one nearest the expression box. */
    int blank = CALC_TAPE_ROWS - shown;
    if (r < blank) return 0;

    int i = s->calc.scroll + (CALC_TAPE_ROWS - 1 - r);
    if (i < 0 || i >= s->calc.hist_count) return 0;

    if (expr)   *expr   = s->calc.hist[i].expr;
    if (result) *result = s->calc.hist[i].result;
    return 1;
}

/* ── Editing ─────────────────────────────────────────────────
 *
 * The status line carries two very different things — "missing )" and
 * "copied 42" — and render.c puts one of them in amber. Setting the text and
 * the flag together is the only way they cannot disagree. */

static void calc_status_clear(syn_server_t *s)
{
    s->calc.status[0]  = '\0';
    s->calc.status_err = 0;
}

static void calc_status(syn_server_t *s, int is_error, const char *msg)
{
    snprintf(s->calc.status, sizeof(s->calc.status), "%s", msg);
    s->calc.status_err = is_error;
}

/* Append-only with a Backspace, as the command bar's input is. No caret: this
 * is one short line of ASCII, Backspace reaches any of it, and a caret would
 * be a second cursor on screen for people to lose. */
static void calc_insert(syn_server_t *s, const char *text)
{
    syn_calc_panel_t *c = &s->calc;

    for (const char *t = text; *t; t++) {
        if (c->entry_len >= (int)sizeof(c->entry) - 1) break;
        c->entry[c->entry_len++] = *t;
    }
    c->entry[c->entry_len] = '\0';

    /* Typing leaves the history walk: the line is yours again, and Up from here
     * should start over from the newest rather than from wherever the recall
     * had got to. */
    c->recall = -1;
    calc_status_clear(s);
}

static void calc_clear(syn_server_t *s)
{
    s->calc.entry[0]  = '\0';
    s->calc.entry_len = 0;
    s->calc.recall    = -1;
    calc_status_clear(s);
}

static void calc_backspace(syn_server_t *s)
{
    if (s->calc.entry_len > 0) s->calc.entry[--s->calc.entry_len] = '\0';
    s->calc.recall    = -1;
    calc_status_clear(s);
}

/*
 * Evaluate, and on success clear the line.
 *
 * Clearing is what makes `ans` worth having: the answer is on screen, it is in
 * `ans`, it is on the tape, and the next thing you type is a fresh sum rather
 * than something appended to the last one. On FAILURE the line is kept — see
 * the header.
 */
static void calc_evaluate(syn_server_t *s)
{
    syn_calc_panel_t *c = &s->calc;
    if (c->entry_len == 0) return;

    double v;
    const char *err = NULL;
    if (!calc_eval(c->entry, c->has_ans ? c->ans : 0.0, &v, &err)) {
        calc_status(s, 1, err ? err : "cannot work that out");
        return;
    }

    calc_format(v, c->result, sizeof(c->result));
    c->ans     = v;
    c->has_ans = 1;

    calc_history_push(s, c->entry, c->result);

    c->entry[0]  = '\0';
    c->entry_len = 0;
    c->recall    = -1;
    c->scroll    = 0;      /* the newest line is the one you want to see */
    calc_status_clear(s);
}

/*
 * The answer to the clipboard.
 *
 * The whole reason to work something out on this desktop is usually to put it
 * somewhere else, and the alternative — reading twelve digits off the screen
 * and typing them back in — is exactly where a transcription error comes from.
 *
 * wl-copy only, no wtype. The emoji picker types as well because it closes
 * first and inserts into whatever had focus; this does not close, so there is
 * nothing to type into but the panel itself.
 *
 * QUOTED even though calc_format() only ever produces digits, '.', '-', 'e' and
 * '+': synui_spawn() runs /bin/sh -c, and "the formatter is safe" is a property
 * of the formatter that a later edit could quietly remove.
 */
static void calc_copy(syn_server_t *s)
{
    if (!s->calc.has_ans || !s->calc.result[0]) {
        calc_status(s, 1, "nothing to copy yet");
        return;
    }

    char q[sizeof(s->calc.result) * 4 + 8];
    size_t qi = 0;
    q[qi++] = '\'';
    for (size_t i = 0; s->calc.result[i] && qi + 8 < sizeof(q); i++) {
        if (s->calc.result[i] == '\'') {
            q[qi++] = '\''; q[qi++] = '\\'; q[qi++] = '\''; q[qi++] = '\'';
        } else {
            q[qi++] = s->calc.result[i];
        }
    }
    q[qi++] = '\'';
    q[qi]   = '\0';

    /* printf %s rather than echo: echo appends a newline, and a newline pasted
     * into a terminal or a chat box submits the line. Same as emoji.c. */
    char cmd[sizeof(q) + 32];
    snprintf(cmd, sizeof(cmd), "printf %%s %s | wl-copy", q);
    synui_spawn(cmd);

    char note[sizeof(s->calc.status)];
    snprintf(note, sizeof(note), "copied %s", s->calc.result);
    calc_status(s, 0, note);
}

/* Walk the tape into the expression box, newest first — a shell's history, and
 * for the same reason: the sum you want next is very often the last one with
 * one number changed. Down past the newest gives you the empty line back rather
 * than sticking, so there is always a way out of the walk. */
static void calc_recall(syn_server_t *s, int dir)
{
    syn_calc_panel_t *c = &s->calc;
    if (c->hist_count == 0) return;

    int idx = c->recall + dir;
    if (idx >= c->hist_count) idx = c->hist_count - 1;

    if (idx < 0) {
        c->recall    = -1;
        c->entry[0]  = '\0';
        c->entry_len = 0;
        calc_status_clear(s);
        return;
    }

    c->recall    = idx;
    c->entry_len = snprintf(c->entry, sizeof(c->entry), "%s", c->hist[idx].expr);
    if (c->entry_len >= (int)sizeof(c->entry)) c->entry_len = (int)sizeof(c->entry) - 1;
    calc_status_clear(s);
}

/* One keypad key, whether it was clicked or (for the action keys) reached from
 * the keyboard. The single place a button's meaning lives. */
static void calc_press(syn_server_t *s, int i)
{
    if (i < 0 || i >= calc_button_count()) return;

    switch (calc_buttons[i].kind) {
    case CALC_BTN_INSERT:
        calc_insert(s, calc_buttons[i].insert ? calc_buttons[i].insert
                                              : calc_buttons[i].label);
        break;
    case CALC_BTN_DELETE: calc_backspace(s); break;
    case CALC_BTN_CLEAR:  calc_clear(s);     break;
    case CALC_BTN_COPY:   calc_copy(s);      break;
    case CALC_BTN_EQUALS: calc_evaluate(s);  break;
    }
}

/* ── Show / hide ─────────────────────────────────────────── */

void calc_show(syn_server_t *s)
{
    calc_history_load(s);

    /* The expression starts empty, and `ans` does NOT: it survives a close, so
     * closing the panel to look something up and reopening it does not throw
     * away the figure you had. */
    s->calc.entry[0]  = '\0';
    s->calc.entry_len = 0;
    s->calc.recall    = -1;
    s->calc.scroll    = 0;
    s->calc.hover     = -1;
    calc_status_clear(s);

    s->calc.visible = 1;
    synui_render_calc(s);
}

void calc_hide(syn_server_t *s)
{
    s->calc.visible = 0;
    s->calc.hover   = -1;
    synui_render_calc(s);
    ctlpanel_child_closed(s, "calc");
}

void calc_toggle(syn_server_t *s)
{
    if (s->calc.visible) calc_hide(s);
    else                 calc_show(s);
}

/* ── Pointer ─────────────────────────────────────────────────
 * The panel pointer contract in synui.h. A grid, like the emoji picker, so the
 * geometry is hit_set_grid() and the index is a keypad cell. */

int calc_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->calc.visible) return 0;

    /* Hover IS the cursor. Over the chrome it clears rather than sticking on
     * the last key crossed, so nothing looks armed when the pointer has left
     * the keypad entirely. */
    int i = hit_index_at(&s->calc.hit, lx, ly);
    if (i >= calc_button_count()) i = -1;

    if (i != s->calc.hover) {
        s->calc.hover = i;
        synui_render_calc(s);
    }
    return 1;
}

int calc_click(syn_server_t *s, double lx, double ly, uint32_t button,
               uint32_t time_msec)
{
    (void)time_msec;
    if (!s->calc.visible) return 0;

    if (!hit_in_panel(&s->calc.hit, lx, ly)) {
        calc_hide(s);
        return 1;
    }
    if (button != BTN_LEFT) return 1;

    int i = hit_index_at(&s->calc.hit, lx, ly);
    if (i < 0 || i >= calc_button_count()) return 1;   /* chrome */

    /* Select the key that was landed on before pressing it: a tap or a warped
     * cursor arrives with no motion before it, and the contract says the click
     * must act on where it landed rather than on the old hover. */
    s->calc.hover = i;
    calc_press(s, i);

    synui_render_calc(s);
    return 1;
}

/*
 * The wheel scrolls the TAPE.
 *
 * Every other panel's wheel moves the selection, because in every other panel
 * the selection is the list. Here the list is the transcript and the selection
 * is a keypad key — a wheel that walked the keypad would arm buttons under the
 * pointer for no reason, and one that walked the HISTORY would retype the line
 * you are in the middle of on every notch, which is destructive in a way no
 * other panel's wheel is. So it scrolls the only thing here with more content
 * than room.
 */
int calc_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->calc.visible) return 0;
    if (delta == 0) return 1;

    int max = s->calc.hist_count - CALC_TAPE_ROWS;
    if (max < 0) max = 0;

    /* Wheel DOWN (delta > 0) walks back into older lines, which are further
     * along a newest-first array — the tape scrolls the way the paper would. */
    s->calc.scroll += delta > 0 ? 1 : -1;
    if (s->calc.scroll > max) s->calc.scroll = max;
    if (s->calc.scroll < 0)   s->calc.scroll = 0;

    synui_render_calc(s);
    return 1;
}

/* ── Keys ────────────────────────────────────────────────────
 *
 * Typing goes into the expression, so this panel has none of the single-letter
 * shortcuts the others do — no 'q' to close, no 'r' to reload, because 'r' is
 * the first letter of "round". Esc does the closing, as it does in the emoji
 * picker for exactly the same reason.
 */
int calc_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->calc.visible) return 0;

    syn_calc_panel_t *c = &s->calc;

    /*
     * Ctrl+C is the ONE modified combo this panel claims.
     *
     * It is the copy key on every desktop, synui's bind table does not use it,
     * and the alternative was putting "copy the answer" on a letter that the
     * expression box needs for typing. Swallowed rather than passed on: the
     * panel is modal, so the window underneath must not see it either.
     *
     * Everything else with Super or Ctrl held falls through to the bind table,
     * as in the emoji picker — those are how you leave, and a panel that ate
     * Super+C would trap you in it.
     */
    if (mods & WLR_MODIFIER_CTRL) {
        if (sym == XKB_KEY_c || sym == XKB_KEY_C) {
            calc_copy(s);
            synui_render_calc(s);
            return 1;
        }
        return 0;
    }
    if (mods & WLR_MODIFIER_LOGO) return 0;

    switch (sym) {
    case XKB_KEY_Escape:
        /* Clears a half-typed expression before it closes the panel: a line
         * that has gone wrong is the state you most want out of, and losing the
         * whole panel to get out of it is a step too many. */
        if (c->entry_len > 0) {
            calc_clear(s);
            synui_render_calc(s);
            return 1;
        }
        calc_hide(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_equal:      /* the key with '=' on it, unshifted */
    case XKB_KEY_KP_Equal:
        calc_evaluate(s);
        synui_render_calc(s);
        return 1;

    case XKB_KEY_BackSpace:
        calc_backspace(s);
        synui_render_calc(s);
        return 1;

    case XKB_KEY_Delete:
        calc_clear(s);
        synui_render_calc(s);
        return 1;

    case XKB_KEY_Up:
        calc_recall(s, +1);
        synui_render_calc(s);
        return 1;

    case XKB_KEY_Down:
        calc_recall(s, -1);
        synui_render_calc(s);
        return 1;

    case XKB_KEY_Page_Up:
        return calc_scroll(s, 0, 0, 1.0);

    case XKB_KEY_Page_Down:
        return calc_scroll(s, 0, 0, -1.0);

    default:
        break;
    }

    /*
     * Everything printable is expression text.
     *
     * The keypad's numeric keysyms are separate symbols from the main row's, so
     * they are folded here rather than left to fall through to the swallow at
     * the bottom — a numeric keypad is the one piece of hardware on the desk
     * specifically for this panel, and having it do nothing would be absurd.
     */
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9) {
        char d[2] = { (char)('0' + (sym - XKB_KEY_KP_0)), '\0' };
        calc_insert(s, d);
        synui_render_calc(s);
        return 1;
    }
    switch (sym) {
    case XKB_KEY_KP_Add:       calc_insert(s, "+"); synui_render_calc(s); return 1;
    case XKB_KEY_KP_Subtract:  calc_insert(s, "-"); synui_render_calc(s); return 1;
    case XKB_KEY_KP_Multiply:  calc_insert(s, "*"); synui_render_calc(s); return 1;
    case XKB_KEY_KP_Divide:    calc_insert(s, "/"); synui_render_calc(s); return 1;
    case XKB_KEY_KP_Decimal:
    case XKB_KEY_KP_Separator: calc_insert(s, "."); synui_render_calc(s); return 1;
    default: break;
    }

    if (sym >= 0x20 && sym <= 0x7e) {
        char ch[2] = { (char)sym, '\0' };
        calc_insert(s, ch);
        synui_render_calc(s);
        return 1;
    }

    /* Modal: swallow the rest rather than letting it reach the window under. */
    return 1;
}
