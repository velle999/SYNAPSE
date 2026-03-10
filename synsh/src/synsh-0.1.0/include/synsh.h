#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#define SYNSH_VERSION       "0.1.0-synapse"
#define SYNSH_HISTORY_FILE  "/.synsh_history"
#define SYNSH_RC_FILE       "/.synshrc"
#define SYNSH_SYSTEM_RC     "/etc/synsh/synshrc"
#define SYNSH_MAX_LINE      4096
#define SYNSH_MAX_ARGS      256
#define SYNSH_MAX_PIPELINE  16
#define MAX_HISTORY         10000
#define SYNSH_HISTORY_MAX   10000
#define SYNSH_PROMPT_MAX    512

#define SYNAPD_SOCKET_PATH  "/run/synapd/synapd.sock"
#define SYN_SOCKET_PATH     SYNAPD_SOCKET_PATH
#define SYN_MAGIC           0x53594E41u
#define SYN_PROTO_VER       1
#define SYN_MSG_QUERY       0x01
#define SYN_MSG_RESPONSE    0x80
#define SYN_MSG_CONTEXT_GET 0x03
#define SYN_MSG_STATUS      0x06
#define SYN_MAX_PAYLOAD     4096

#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_BLUE    "\033[34m"
#define COL_CYAN    "\033[36m"
#define COL_BRED    "\033[1;31m"
#define COL_BGREEN  "\033[1;32m"
#define COL_BYELLOW "\033[1;33m"
#define COL_BBLUE   "\033[1;34m"
#define COL_BCYAN   "\033[1;36m"

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  msg_type;
    uint16_t flags;
    uint32_t request_id;
    uint32_t payload_len;
    uint32_t client_pid;
} syn_msg_header_t;

typedef enum { INPUT_SHELL=0, INPUT_BUILTIN=1, INPUT_AI=2, INPUT_HYBRID=3 } input_class_t;
typedef enum { JOB_RUNNING=0, JOB_STOPPED, JOB_DONE } syn_job_state_t;

typedef struct syn_job {
    int              id;
    pid_t            pid;
    syn_job_state_t  state;
    char            *command_str;
    struct syn_job  *next;
} syn_job_t;

typedef struct {
    char  *argv[SYNSH_MAX_ARGS];
    int    argc;
    char  *input_redirect;
    char  *output_redirect;
    int    append_redirect;
    int    background;
} synsh_cmd_t;

typedef struct {
    synsh_cmd_t cmds[SYNSH_MAX_PIPELINE];
    int         n_cmds;
} synsh_pipeline_t;

typedef struct {
    int   exit_code;
    char *ai_explanation;
    int   ai_suggested;
} synsh_result_t;

typedef struct synsh_state {
    int         interactive;
    int         running;
    int         last_exit;
    int         last_exit_code;
    pid_t       pid;
    int         synapd_fd;
    int         synapd_connected;
    int         synapd_online;
    uint32_t    request_counter;
    uint32_t    req_id_counter;
    char      **history;
    int         history_count;
    int         history_pos;
    char       *history_file;
    char       *cwd;
    char       *home;
    char       *user;
    int         ai_confirm;
    int         ai_explain;
    int         ai_enabled;
    int         explain_mode;
    int         safe_mode;
    int         color;
    int         verbose;
    char        prompt_fmt[256];
    unsigned long commands_run;
    unsigned long nl_queries;
    unsigned long ai_assists;
    syn_job_t  *jobs;
    int         next_job_id;
} synsh_state_t;

/* Function declarations */
int   synapd_connect(synsh_state_t *s);
void  synapd_disconnect(synsh_state_t *s);
int   synapd_query(synsh_state_t *s, const char *prompt, char *out_buf, size_t out_len);
int   synapd_status(synsh_state_t *s, char *out_buf, size_t out_len);
input_class_t classify_input(const char *line);
int   ai_translate(synsh_state_t *s, const char *input, char *cmd_buf, size_t cmd_len, char *explain_buf, size_t explain_len);
int   execute_pipeline(synsh_state_t *s, const char *line);
int   execute_ai_suggestion(synsh_state_t *s, const char *cmd, const char *explanation);
void  synsh_prompt(synsh_state_t *s, char *buf, size_t len);
char *synsh_readline(synsh_state_t *s);
int   synsh_builtin(synsh_state_t *s, int argc, char **argv);
void  synsh_history_add(synsh_state_t *s, const char *line);
void  synsh_history_load(synsh_state_t *s);
void  synsh_history_save(synsh_state_t *s);
void  synsh_ai_disconnect(synsh_state_t *s);
int   synsh_init(synsh_state_t *s, int argc, char *argv[]);
void  synsh_destroy(synsh_state_t *s);
void  synsh_load_rc(synsh_state_t *s);
int   is_builtin(const char *cmd);

static inline int execute_builtin_line(synsh_state_t *s, const char *line) {
    char buf[SYNSH_MAX_LINE];
    char *av[SYNSH_MAX_ARGS];
    int ac = 0;
    strncpy(buf, line, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    char *tok = strtok(buf, " \t");
    while (tok && ac < SYNSH_MAX_ARGS-1) { av[ac++] = tok; tok = strtok(NULL, " \t"); }
    av[ac] = NULL;
    return synsh_builtin(s, ac, av);
}

/* history aliases — must come after readline includes */
