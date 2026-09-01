#ifndef EXEC_H
#define EXEC_H
#include "synsh.h"

/* One pipeline. Every stage is forked before any is waited on — see the note
 * at the top of exec.c; doing otherwise deadlocks on any pipe that fills. */
int execute_pipeline(synsh_state_t *s, const char *line);
/* The same, with the option of not waiting: the list layer passes background=1
 * when the segment was followed by '&'. */
int execute_pipeline_bg(synsh_state_t *s, const char *line, int background);

/* Runs a full command line: splits on ; && || & and dispatches built-ins.
 * This is the entry point every caller should use — execute_pipeline()
 * handles only one pipeline and never sees the built-ins. */
int execute_command_line(synsh_state_t *s, const char *line);

/* Run one line as a built-in: split it, apply any redirections to synsh's own
 * descriptors, and call synsh_builtin().
 *
 * ⚠ THIS USED TO BE A SECOND TOKENIZER, inline in synsh.h, and the two
 * disagreed: it stripped quotes mid-token (which is what made `alias
 * ll='ls -la'` parse) while exec.c's stripped them only at the start of a
 * word. Filtering a tokenize()d argv through the other one silently mangled
 * every quoted alias value. There is one splitter now — expand.c's — and it
 * strips quotes anywhere, so both callers get the same answer. */
int execute_builtin_line(synsh_state_t *s, const char *line);

/* Collect any background jobs that have finished, announcing them. Called at
 * the prompt: there is deliberately no SIGCHLD handler (see exec.c). */
void synsh_reap_jobs(synsh_state_t *s);
void synsh_jobs_free(synsh_state_t *s);

int ai_translate(synsh_state_t *s, const char *query, char *out_cmd, size_t cmd_len, char *out_why, size_t why_len);
int execute_ai_suggestion(synsh_state_t *s, const char *cmd, const char *why);
#endif
