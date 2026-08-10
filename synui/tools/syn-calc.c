/*
 * syn-calc — the desktop calculator, from a terminal.
 *
 * Super+X puts the calculator on screen; this is the same parser with a main()
 * around it, for the times the answer is wanted in a pipe, in a script, or over
 * SSH where there is no screen to put a panel on. It links calceval.c and libm
 * and nothing else: no Wayland, no compositor, no running session.
 *
 *   syn-calc '1440 * 0.8'      → 1152
 *   echo '2^10' | syn-calc     → 1024
 *   syn-calc --funcs           → what it knows
 *
 * The expression is taken from the arguments JOINED WITH SPACES, so both
 * `syn-calc '2 + 2'` and `syn-calc 2 + 2` work — but a shell eats * and ( ),
 * so the quoted form is the one to teach. Reading stdin when there are no
 * arguments is what makes it a filter.
 *
 * Exit status: 0 with the answer on stdout, 1 with the reason on stderr. A
 * calculator that printed "0" for a syntax error would be worse than useless
 * in a script, so nothing is printed to stdout unless it is an answer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calceval.h"

#define EXPR_MAX 4096

static void usage(FILE *f)
{
	fputs(
"syn-calc — evaluate an expression, the same one Super+X does\n"
"\n"
"Usage: syn-calc <expression>\n"
"       syn-calc            read the expression from stdin\n"
"\n"
"Options\n"
"  --funcs        list the functions it knows\n"
"  -h, --help     this text\n"
"\n"
"Quote the expression: a shell would eat * and ( ) first.\n"
"  syn-calc '1440 * 0.8'\n"
"  syn-calc 'sqrt(2) * 100'\n", f);
}

int main(int argc, char **argv)
{
	char expr[EXPR_MAX];
	size_t len = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(stdout);
			return 0;
		}
		if (!strcmp(argv[i], "--funcs")) {
			printf("%s\n", calc_func_hint());
			return 0;
		}
	}

	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			int n = snprintf(expr + len, sizeof(expr) - len, "%s%s",
			                 len ? " " : "", argv[i]);
			if (n < 0 || (size_t)n >= sizeof(expr) - len) {
				fprintf(stderr, "syn-calc: expression too long\n");
				return 1;
			}
			len += (size_t)n;
		}
	} else {
		/* No expression and a terminal on stdin is somebody typing `syn-calc`
		 * to see what it does — answer that, rather than sitting silently
		 * waiting for input they have no reason to expect to be asked for. */
		if (isatty(STDIN_FILENO)) {
			usage(stdout);
			return 0;
		}

		/* A filter: `echo '2+2' | syn-calc`. Reading the WHOLE of stdin and
		 * evaluating it as one expression rather than line by line — a
		 * multi-line expression is legal and a per-line loop would answer a
		 * question nobody asked. */
		len = fread(expr, 1, sizeof(expr) - 1, stdin);
		expr[len] = '\0';

		/* `echo '2+2' | syn-calc` arrives with the newline echo added, and the
		 * parser is right to refuse a stray character — so the newline is
		 * trimmed here, where it is known to be punctuation rather than input. */
		while (len && (expr[len - 1] == '\n' || expr[len - 1] == '\r' ||
		               expr[len - 1] == ' '  || expr[len - 1] == '\t'))
			expr[--len] = '\0';

		if (len == 0) {
			usage(stderr);
			return 1;
		}
	}

	double v;
	const char *err = NULL;
	if (!calc_eval(expr, 0.0, &v, &err)) {
		fprintf(stderr, "syn-calc: %s\n", err ? err : "cannot evaluate that");
		return 1;
	}

	char out[64];
	calc_format(v, out, sizeof(out));
	printf("%s\n", out);
	return 0;
}
