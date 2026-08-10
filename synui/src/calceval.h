/*
 * calceval.h — the calculator's expression parser, on its own.
 *
 * Declared apart from synui.h on purpose: this is the half with no compositor
 * in it, so `syn-calc` can include exactly this and link exactly calceval.c.
 * synui.h re-declares the same three functions for the panel's benefit, which
 * costs nothing and keeps the compositor's one header the compositor's one
 * header.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNUI_CALCEVAL_H
#define SYNUI_CALCEVAL_H

#include <stdbool.h>
#include <stddef.h>

/* Evaluate `expr`. `ans` is what the identifier "ans" resolves to — the last
 * answer, or 0 when there has not been one. On failure returns false and
 * points *err at a fixed string saying why; *out is untouched. */
bool calc_eval(const char *expr, double ans, double *out, const char **err);

/* The answer as the panel and the CLI both print it: no trailing zeros, and an
 * exponent only when the plain form would be unreadable. */
void calc_format(double v, char *buf, size_t n);

/* Every function name the parser knows, space-separated, for a help line. */
const char *calc_func_hint(void);

#endif /* SYNUI_CALCEVAL_H */
