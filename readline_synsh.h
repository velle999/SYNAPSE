#ifndef READLINE_SYNSH_H
#define READLINE_SYNSH_H
#include "synsh.h"
char *synsh_readline(synsh_state_t *s);
void  synsh_prompt(synsh_state_t *s, char *buf, size_t len);
void  synsh_history_add(synsh_state_t *s, const char *line);
void  synsh_history_load(synsh_state_t *s);
void  synsh_history_save(synsh_state_t *s);
#endif
