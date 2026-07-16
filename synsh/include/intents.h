#ifndef INTENTS_H
#define INTENTS_H
#include "synsh.h"

/* Try to answer a natural-language line directly.
 *
 * Returns 1 if the line was handled (and sets *exit_code), 0 if synsh should
 * fall through to the AI translator as before. */
int synsh_intent(synsh_state_t *s, const char *line, int *exit_code);

/* Human-readable list of what synsh can do without the model — `help` prints
 * this, and ai_translate() feeds it to synapd so the model stops inventing
 * programs this machine does not have. */
void synsh_intent_help(synsh_state_t *s);

/* The tools actually present on this box, resolved once from $PATH, formatted
 * for the translation prompt. Returns a pointer to a static buffer. */
const char *synsh_intent_toolinfo(void);

#endif
