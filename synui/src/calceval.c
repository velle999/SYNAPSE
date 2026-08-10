/*
 * calceval.c — the expression parser, and nothing else.
 *
 * Split out of calc.c so that evaluating "1440 * 0.8" needs no compositor. The
 * panel lives in calc.c and still calls straight into this; `syn-calc` is this
 * file, a main() and libm — no wlroots, no Wayland, no display — which is what
 * makes the calculator usable over SSH and inside a pipe.
 *
 * Everything here is pure: a string in, a double out, no allocation, no state
 * beyond the caller's `ans`. That is also why it is the half worth testing on
 * its own.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "calceval.h"

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
    /* Newlines too: the panel's entry line can never contain one, but syn-calc
     * reads an expression from stdin, where a wrapped or heredoc'd expression
     * is the ordinary case. */
    while (*ps->p == ' ' || *ps->p == '\t' ||
           *ps->p == '\n' || *ps->p == '\r') ps->p++;
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
