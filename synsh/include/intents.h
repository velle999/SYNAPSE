#ifndef INTENTS_H
#define INTENTS_H
#include "synsh.h"

/* Try to answer a natural-language line directly.
 *
 * Returns 1 if the line is ours, 0 if synsh should fall through to the AI
 * translator as before.
 *
 * check_only asks the question without answering it: nothing runs, *exit_code
 * is untouched, and the return value alone says whether this file would claim
 * the line. `synsh --intent-check` exposes it so synui's command bar can pick
 * between synsh and synapd using this table rather than a copy of it. */
int synsh_intent(synsh_state_t *s, const char *line, int *exit_code,
                 bool check_only);

/* Human-readable list of what synsh can do without the model — `help` prints
 * this, and ai_translate() feeds it to synapd so the model stops inventing
 * programs this machine does not have. */
void synsh_intent_help(synsh_state_t *s);

/* The tools actually present on this box, resolved once from $PATH, formatted
 * for the translation prompt. Returns a pointer to a static buffer. */
const char *synsh_intent_toolinfo(void);

#endif
