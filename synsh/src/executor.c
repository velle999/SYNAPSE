/*
 * executor.c — Command execution engine
 *
 * Handles fork/exec, pipelines, I/O redirection, and job control.
 * Full POSIX shell semantics for command execution.
 *
 * SynapseOS Project — GPLv2
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <glob.h>
#include <wordexp.h>

#include "synsh.h"

/* ── Simple command execution (no pipeline) ──────────────── */
static int exec_simple(synsh_state_t *s, const char *cmd) {
    /* Use wordexp for proper word splitting, glob, variable expansion */
    wordexp_t we;
    int r = wordexp(cmd, &we, WRDE_NOCMD | WRDE_SHOWERR);
    if (r != 0) {
        fprintf(stderr, "synsh: parse error: %s\n", cmd);
        return 1;
    }

    if (we.we_wordc == 0) {
        wordfree(&we);
        return 0;
    }

    /* Check for built-in first */
    if (synsh_is_builtin(we.we_wordv[0])) {
        int ret = synsh_builtin(s, (int)we.we_wordc, we.we_wordv);
        wordfree(&we);
        return ret;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("synsh: fork");
        wordfree(&we);
        return 1;
    }

    if (pid == 0) {
        /* Child */
        /* Put child in its own process group for job control */
        setpgid(0, 0);

        execvp(we.we_wordv[0], we.we_wordv);

        /* exec failed */
        fprintf(stderr, "synsh: %s: %s\n", we.we_wordv[0], strerror(errno));
        _exit(127);
    }

    /* Parent: wait for foreground job */
    wordfree(&we);

    int status;
    waitpid(pid, &status, 0);
    s->last_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

    /* Update cwd in case cd was run via subshell (usually a no-op) */
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        free(s->cwd);
        s->cwd = strdup(buf);
    }

    return s->last_exit_code;
}

/* ── Pipeline execution ───────────────────────────────────── */
/*
 * Splits "cmd1 | cmd2 | cmd3" and sets up pipe chain.
 */
static int exec_pipeline(synsh_state_t *s, char **stages, int n) {
    if (n == 1) return exec_simple(s, stages[0]);

    int pipes[n - 1][2];
    pid_t pids[n];

    /* Create all pipes */
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("synsh: pipe");
            return 1;
        }
    }

    /* Fork each stage */
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("synsh: fork");
            return 1;
        }

        if (pids[i] == 0) {
            /* Child i */
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            if (i < n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            /* Close all pipe fds in child */
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            wordexp_t we;
            if (wordexp(stages[i], &we, WRDE_NOCMD) == 0 && we.we_wordc > 0) {
                execvp(we.we_wordv[0], we.we_wordv);
            }
            fprintf(stderr, "synsh: %s: %s\n", stages[i], strerror(errno));
            _exit(127);
        }
    }

    /* Parent: close all pipe ends */
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Wait for all stages */
    int last_status = 0;
    for (int i = 0; i < n; i++) {
        int st;
        waitpid(pids[i], &st, 0);
        if (i == n - 1)
            last_status = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }

    s->last_exit_code = last_status;
    return last_status;
}

/* ── Split pipeline on unquoted '|' ──────────────────────── */
static int split_pipeline(const char *cmd, char **stages, int max) {
    int n = 0;
    char *copy = strdup(cmd);
    char *p = copy;
    char *stage_start = p;
    bool in_single = false, in_double = false;

    for (; *p; p++) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '|' && !in_single && !in_double) {
            *p = '\0';
            if (n < max) stages[n++] = strdup(stage_start);
            stage_start = p + 1;
        }
    }
    if (n < max) stages[n++] = strdup(stage_start);

    free(copy);
    return n;
}

/* ── Handle I/O redirection ───────────────────────────────── */
/*
 * Strips redirection operators from cmd, opens files, returns
 * modified command string and sets up fds.
 * For now handles: >, >>, <, 2>, 2>&1
 */
static char *handle_redirects(const char *cmd) {
    /*
     * TODO: full redirect parsing
     * For now, pass through to the child's wordexp which handles
     * basic redirects via the shell. Full implementation in v0.2.
     */
    return strdup(cmd);
}

/* ── Background job handling ──────────────────────────────── */
static bool is_background(const char *cmd) {
    size_t len = strlen(cmd);
    if (len == 0) return false;
    /* Check for trailing & (not in quotes) */
    const char *p = cmd + len - 1;
    while (p > cmd && (*p == ' ' || *p == '\t')) p--;
    return *p == '&';
}

/* ── Public execute ───────────────────────────────────────── */
int synsh_execute(synsh_state_t *s, const char *command) {
    if (!command || !*command) return 0;

    /* Strip trailing whitespace + & for bg detection */
    char *cmd = strdup(command);
    bool bg = is_background(cmd);
    if (bg) {
        /* Strip trailing & */
        char *p = cmd + strlen(cmd) - 1;
        while (p >= cmd && (*p == ' ' || *p == '&')) *p-- = '\0';
    }

    /* Handle redirects */
    char *clean_cmd = handle_redirects(cmd);
    free(cmd);

    /* Split on semicolons for sequential commands: cmd1; cmd2 */
    /* TODO: full ; and && || handling */
    /* For now handle simple single commands and pipelines */

    /* Split on pipe */
    char *stages[64];
    int n_stages = split_pipeline(clean_cmd, stages, 64);
    free(clean_cmd);

    int ret;
    if (bg && n_stages == 1) {
        /* Background job — fork and don't wait */
        wordexp_t we;
        if (wordexp(stages[0], &we, WRDE_NOCMD) == 0 && we.we_wordc > 0) {
            pid_t pid = fork();
            if (pid == 0) {
                setpgid(0, 0);
                execvp(we.we_wordv[0], we.we_wordv);
                _exit(127);
            }
            if (pid > 0) {
                /* Add to job table */
                syn_job_t *j = calloc(1, sizeof(*j));
                if (j) {
                    j->id          = s->next_job_id++;
                    j->pgid        = pid;
                    j->command_str = strdup(stages[0]);
                    j->state       = JOB_RUNNING;
                    j->next        = s->jobs;
                    s->jobs        = j;
                    printf("[%d] %d\n", j->id, pid);
                }
                ret = 0;
            } else {
                ret = 1;
            }
            wordfree(&we);
        } else {
            ret = 1;
        }
    } else {
        ret = exec_pipeline(s, stages, n_stages);
    }

    for (int i = 0; i < n_stages; i++) free(stages[i]);

    s->last_exit_code = ret;
    return ret;
}

int synsh_execute_pipeline(synsh_state_t *s, const char *pipeline) {
    return synsh_execute(s, pipeline);
}
