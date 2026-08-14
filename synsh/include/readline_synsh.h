#ifndef READLINE_SYNSH_H
#define READLINE_SYNSH_H
#include "synsh.h"
char *synsh_readline(synsh_state_t *s);
void  synsh_prompt(synsh_state_t *s, char *buf, size_t len);
void  synsh_history_add(synsh_state_t *s, const char *line);
void  synsh_history_load(synsh_state_t *s);
void  synsh_history_save(synsh_state_t *s);

/* OSC 133 semantic marks. Interactive, on a tty, only — see the definitions.
 * These are the STANDARD's marks, so the same shell gets prompt navigation in
 * kitty, WezTerm and iTerm2, not only in syntty. */
void synsh_mark_output_start(synsh_state_t *s);
void synsh_mark_command_done(synsh_state_t *s, int status);

#endif
