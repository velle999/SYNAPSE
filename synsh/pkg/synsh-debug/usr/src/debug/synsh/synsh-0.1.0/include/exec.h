#ifndef EXEC_H
#define EXEC_H
#include "synsh.h"
int execute_pipeline(synsh_state_t *s, const char *line);
int ai_translate(synsh_state_t *s, const char *query, char *out_cmd, size_t cmd_len, char *out_why, size_t why_len);
int execute_ai_suggestion(synsh_state_t *s, const char *cmd, const char *why);
#endif
