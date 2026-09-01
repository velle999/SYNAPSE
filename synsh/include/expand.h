/*
 * expand.h — turning a line of text into the words a command actually gets.
 *
 * synsh used to hand execve() whatever fell out of a whitespace split, which
 * meant it was not doing four things every shell is expected to do, and was
 * doing them so quietly that each one looked like a different bug:
 *
 *   `echo $HOME`        printed the five characters "$HOME"
 *   `ls *.c`            asked for a file literally called "*.c"
 *   `echo hi >f`        printed "hi >f" — a redirection only worked with a
 *                       space in front of it
 *   `pacman -Rns $(pacman -Qtdq)`
 *                       passed pacman the word "$(pacman" — which is synsh's
 *                       OWN remove-orphans intent, so that intent had never
 *                       worked once
 *
 * This layer does the expansions, in the order a POSIX shell does them:
 * tilde, then parameter/command substitution, then field splitting of the
 * UNQUOTED parts of what came back, then pathname expansion.
 *
 * ⚠ WHAT IS DELIBERATELY NOT HERE. No arithmetic `$(( ))`, no parameter
 * defaults `${x:-y}`, no arrays, no process substitution, no `for`/`while`.
 * synsh is a natural-language front end that must also be a competent
 * everyday shell — it is not trying to be bash, and every one of those has a
 * plain `bash -c` an arm's length away. What IS here is what somebody hits on
 * their first afternoon.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNSH_EXPAND_H
#define SYNSH_EXPAND_H

#include "synsh.h"

/*
 * A split-and-expanded command, ready for execve.
 *
 * `op[i]` marks word i as an UNQUOTED redirection operator — ">", ">>", "<",
 * "2>", "2>&1", "&>", "&". The flag exists because the word itself cannot be
 * trusted to say: `echo ">"` is an argument and `echo > x` is a redirection,
 * and by the time quotes have been removed the two look identical. Losing that
 * distinction is how a shell writes a file it was only asked to print.
 */
typedef struct {
    char **v;    /* NULL-terminated argv */
    int   *op;   /* per-word: 1 = unquoted redirection operator */
    int    n;
    int    cap;
} synsh_words_t;

/* Split and expand one command (no pipes, no ; && ||: the callers above have
 * already cut those out). Returns 0, or -1 on a syntax error it has already
 * reported. An empty line yields n == 0, which is not an error. */
int  synsh_words_split(synsh_state_t *s, const char *line, synsh_words_t *out);
void synsh_words_free(synsh_words_t *w);

/* A redirection target is expanded but NOT field-split and NOT globbed, and
 * the splitter does that itself: the word after a redirection operator comes
 * back whole. It is done there rather than by a second pass here because a
 * second pass no longer knows what was quoted, and would turn a file honestly
 * called `a$b` into `a`. */

#endif /* SYNSH_EXPAND_H */
