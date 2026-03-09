/*
 * synsh.h — SynapseOS Natural Language Shell
 *
 * Core types, constants, and interfaces for synsh.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* ── Version ─────────────────────────────────────────────── */
#define SYNSH_VERSION     "0.1.0-synapse"

/* ── Config paths ─────────────────────────────────────────── */
#define SYNSH_HISTORY_FILE   "/.synsh_history"    /* relative to $HOME */
#define SYNSH_RC_FILE        "/.synshrc"           /* relative to $HOME */
#define SYNSH_SYSTEM_RC      "/etc/synsh/synshrc"

/* ── Limits ───────────────────────────────────────────────── */
#define SYNSH_MAX_LINE       4096
#define SYNSH_MAX_ARGS       256
#define SYNSH_MAX_PIPELINE   16
#define SYNSH_HISTORY_MAX    10000
#define SYNSH_PROMPT_MAX     512

/* ── Input classification ─────────────────────────────────── */
/*
 * synsh classifies each input line before deciding how to handle it:
 *
 *   INPUT_SHELL   → standard POSIX shell command, run directly
 *   INPUT_AI      → natural language, route to synapd
 *   INPUT_HYBRID  → mixed: AI-generated command + user confirmation
 *   INPUT_BUILTIN → synsh built-in command (cd, exit, syn, etc.)
 */
typedef enum {
    INPUT_SHELL   = 0,
    INPUT_BUILTIN = 1,
    INPUT_AI      = 2,
    INPUT_HYBRID  = 3,
} input_class_t;

/* ── Command ──────────────────────────────────────────────── */
typedef struct {
    char   *argv[SYNSH_MAX_ARGS];
    int     argc;
    char   *input_redirect;    /* < file */
    char   *output_redirect;   /* > file */
    int     append_redirect;   /* >> file */
    int     background;        /* & */
} synsh_cmd_t;

/* ── Pipeline ─────────────────────────────────────────────── */
typedef struct {
    synsh_cmd_t  cmds[SYNSH_MAX_PIPELINE];
    int          n_cmds;
} synsh_pipeline_t;

/* ── Execution result ─────────────────────────────────────── */
typedef struct {
    int     exit_code;
    char   *ai_explanation;  /* non-NULL if AI was involved */
    int     ai_suggested;    /* AI generated this command */
} synsh_result_t;

/* ── Shell state ──────────────────────────────────────────── */
typedef struct synsh_state {
    /* Runtime */
    int         interactive;
    int         running;
    int         last_exit;
    pid_t       pid;

    /* IPC */
    int         synapd_fd;       /* socket fd to synapd */
    int         synapd_connected;
    uint32_t    request_counter;

    /* History */
    char      **history;
    int         history_count;
    int         history_pos;

    /* Environment */
    char       *cwd;
    char       *home;
    char       *user;

    /* Config */
    int         ai_confirm;      /* ask before running AI-generated commands */
    int         ai_explain;      /* print AI explanation after commands */
    int         color;           /* colored output */
    int         verbose;         /* verbose mode */
    char        prompt_fmt[256]; /* prompt format string */
} synsh_state_t;

/* ── Function declarations ────────────────────────────────── */

/* synapd IPC */
int  synapd_connect(synsh_state_t *s);
void synapd_disconnect(synsh_state_t *s);
int  synapd_query(synsh_state_t *s,
                  const char *prompt,
                  char *out_buf, size_t out_len);
int  synapd_status(synsh_state_t *s, char *out_buf, size_t out_len);

/* Input classification + AI translation */
input_class_t classify_input(const char *line);
int  ai_translate(synsh_state_t *s,
                  const char *natural_input,
                  char *cmd_buf, size_t cmd_len,
                  char *explain_buf, size_t explain_len);

/* Shell execution */
int  execute_pipeline(synsh_state_t *s, const char *line);
int  execute_builtin(synsh_state_t *s, synsh_cmd_t *cmd);
int  execute_ai_suggestion(synsh_state_t *s,
                            const char *suggested_cmd,
                            const char *explanation);

/* Prompt + readline */
void synsh_prompt(synsh_state_t *s, char *buf, size_t len);
char *synsh_readline(synsh_state_t *s);

/* History */
void history_add(synsh_state_t *s, const char *line);
void history_load(synsh_state_t *s);
void history_save(synsh_state_t *s);

/* Init / teardown */
int  synsh_init(synsh_state_t *s, int argc, char *argv[]);
void synsh_destroy(synsh_state_t *s);
void synsh_load_rc(synsh_state_t *s);
