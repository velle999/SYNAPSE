/*
 * exec.c — Command execution for synsh
 *
 * Handles:
 *   - Shell pipeline execution (fork/exec with pipes)
 *   - AI suggestion display + confirmation
 *   - AI translation prompt construction
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <termios.h>

#include "synsh.h"
#include "exec.h"
#include "ipc.h"
#include "color.h"

/* ── Simple line tokenizer ────────────────────────────────── */
static int tokenize(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args - 1) {
        /* Skip whitespace */
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        /* Handle quotes */
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            argv[argc++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* ── Expand ~ in paths ────────────────────────────────────── */
static char *expand_tilde(const char *path, const char *home) {
    if (!path || path[0] != '~') return strdup(path);
    if (!home) return strdup(path);

    char *result;
    if (path[1] == '/' || path[1] == '\0') {
        asprintf(&result, "%s%s", home, path + 1);
    } else {
        result = strdup(path);
    }
    return result;
}

/* ── Run a single command (no pipeline) ──────────────────── */
static int run_cmd(synsh_state_t *s,
                   char *argv[], int argc,
                   int stdin_fd, int stdout_fd)
{
    (void)argc;

    /* Handle redirections in argv (crude but functional) */
    char *input_file  = NULL;
    char *output_file = NULL;
    int   append      = 0;
    int   background  = 0;
    char *filtered[SYNSH_MAX_ARGS];
    int   fargc = 0;

    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "<") == 0 && argv[i+1]) {
            input_file = argv[++i];
        } else if (strcmp(argv[i], ">>") == 0 && argv[i+1]) {
            output_file = argv[++i];
            append = 1;
        } else if (strcmp(argv[i], ">") == 0 && argv[i+1]) {
            output_file = argv[++i];
            append = 0;
        } else if (strcmp(argv[i], "&") == 0) {
            background = 1;
        } else {
            filtered[fargc++] = argv[i];
        }
    }
    filtered[fargc] = NULL;

    if (fargc == 0) return 0;

    /* Expand tilde in args */
    for (int i = 0; filtered[i]; i++) {
        if (filtered[i][0] == '~') {
            char *expanded = expand_tilde(filtered[i], s->home);
            filtered[i] = expanded;  /* leak: acceptable for shell lifetime */
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child */

        /* Setup stdin */
        if (stdin_fd != STDIN_FILENO) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }
        if (input_file) {
            int ifd = open(input_file, O_RDONLY);
            if (ifd < 0) { perror(input_file); exit(1); }
            dup2(ifd, STDIN_FILENO);
            close(ifd);
        }

        /* Setup stdout */
        if (stdout_fd != STDOUT_FILENO) {
            dup2(stdout_fd, STDOUT_FILENO);
            close(stdout_fd);
        }
        if (output_file) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int ofd = open(output_file, flags, 0644);
            if (ofd < 0) { perror(output_file); exit(1); }
            dup2(ofd, STDOUT_FILENO);
            close(ofd);
        }

        /* Reset signals */
        signal(SIGINT, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        execvp(filtered[0], filtered);
        /* execvp failed */
        fprintf(stderr, "synsh: %s: %s\n", filtered[0], strerror(errno));
        exit(127);
    }

    /* Parent */
    if (background) {
        printf("[%d] %s\n", pid, filtered[0]);
        return 0;
    }

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* ── Execute a pipeline string ────────────────────────────── */

/* ── Alias expansion ──────────────────────────────────────── */
/*
 * Expands the first word of 'line' if it matches an alias.
 * Writes expanded result into 'out' (max out_len bytes).
 * Returns 1 if expanded, 0 if not.
 */
static int alias_expand(synsh_state_t *s, const char *line, char *out, size_t out_len) {
    if (!s->alias_count) return 0;

    /* Find end of first word */
    const char *p = line;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t wlen = (size_t)(p - line);

    for (int i = 0; i < s->alias_count; i++) {
        if (strlen(s->alias_names[i]) == wlen &&
            strncmp(s->alias_names[i], line, wlen) == 0) {
            snprintf(out, out_len, "%s%s", s->alias_values[i], p);
            return 1;
        }
    }
    return 0;
}

int execute_pipeline(synsh_state_t *s, const char *line) {
    if (!line || !*line) return 0;

    /* Make mutable copy */
    char *buf = strdup(line);
    if (!buf) return 1;

    /* Split on | */
    char *segments[SYNSH_MAX_PIPELINE];
    int   n_segs = 0;
    char *p = buf;

    while (*p && n_segs < SYNSH_MAX_PIPELINE) {
        segments[n_segs++] = p;
        /* Find next unquoted | */
        int in_q = 0; char q = 0;
        while (*p) {
            if (!in_q && (*p == '\'' || *p == '"')) { in_q = 1; q = *p; }
            else if (in_q && *p == q)                 in_q = 0;
            else if (!in_q && *p == '|') { *p++ = '\0'; break; }
            p++;
        }
    }

    int exit_code = 0;
    int prev_pipe_read = STDIN_FILENO;

    for (int i = 0; i < n_segs; i++) {
        /* Tokenize this segment */
        char *seg = strdup(segments[i]);
        char *argv[SYNSH_MAX_ARGS];
        int   argc = tokenize(seg, argv, SYNSH_MAX_ARGS);

        if (argc == 0) { free(seg); continue; }

        /* Last segment writes to stdout; others write to pipe */
        int pipe_fds[2] = {-1, -1};
        int stdout_fd = STDOUT_FILENO;

        if (i < n_segs - 1) {
            if (pipe(pipe_fds) < 0) { perror("pipe"); free(seg); break; }
            stdout_fd = pipe_fds[1];
        }

        exit_code = run_cmd(s, argv, argc, prev_pipe_read, stdout_fd);

        if (stdout_fd != STDOUT_FILENO) close(stdout_fd);
        if (prev_pipe_read != STDIN_FILENO) close(prev_pipe_read);
        if (pipe_fds[0] >= 0) prev_pipe_read = pipe_fds[0];

        free(seg);
    }

    if (prev_pipe_read != STDIN_FILENO) close(prev_pipe_read);
    free(buf);
    return exit_code;
}

/* ── AI translation ───────────────────────────────────────── */
/*
 * Ask synapd to translate natural language into a shell command.
 * We construct a specific system prompt so the model returns
 * a structured response we can parse.
 *
 * Expected response format from synapd:
 *   CMD: <shell command>
 *   WHY: <one-line explanation>
 *
 * We parse CMD and WHY from the response.
 */
int ai_translate(synsh_state_t *s,
                  const char *natural_input,
                  char *cmd_buf, size_t cmd_len,
                  char *explain_buf, size_t explain_len)
{
    if (!s->synapd_connected) return -1;

    /*
     * Build translation prompt. We inject the current working directory
     * and the system name so the AI can make context-aware suggestions.
     */
    char prompt[SYNSH_MAX_LINE * 2];
    snprintf(prompt, sizeof(prompt),
        "[TRANSLATE TO SHELL]\n"
        "OS: SynapseOS (Arch Linux base)\n"
        "CWD: %s\n"
        "User: %s\n"
        "Request: %s\n"
        "\n"
        "Reply in EXACTLY this format (two lines only):\n"
        "CMD: <single shell command or pipeline>\n"
        "WHY: <one-sentence explanation>\n"
        "\n"
        "Rules:\n"
        "- CMD must be a valid bash command\n"
        "- If the request is ambiguous, pick the safest interpretation\n"
        "- Never include rm -rf without explicit confirmation request\n"
        "- If no shell command makes sense, write CMD: # not possible\n",
        s->cwd   ? s->cwd   : "/",
        s->user  ? s->user  : "user",
        natural_input
    );

    char response[SYNSH_MAX_LINE * 4] = {0};
    int r = synapd_query(s, prompt, response, sizeof(response));
    if (r < 0) return -1;

    /* Parse CMD: and WHY: */
    char *cmd_line = strstr(response, "CMD:");
    char *why_line = strstr(response, "WHY:");

    if (!cmd_line) return -1;

    cmd_line += 4;
    while (*cmd_line == ' ') cmd_line++;

    /* Copy up to newline */
    char *nl = strchr(cmd_line, '\n');
    size_t clen = nl ? (size_t)(nl - cmd_line) : strlen(cmd_line);
    if (clen >= cmd_len) clen = cmd_len - 1;
    strncpy(cmd_buf, cmd_line, clen);
    cmd_buf[clen] = '\0';

    /* Trim trailing whitespace */
    for (int i = (int)clen - 1; i >= 0 && (cmd_buf[i] == ' ' || cmd_buf[i] == '\r'); i--)
        cmd_buf[i] = '\0';

    if (why_line && explain_buf && explain_len > 0) {
        why_line += 4;
        while (*why_line == ' ') why_line++;
        nl = strchr(why_line, '\n');
        size_t wlen = nl ? (size_t)(nl - why_line) : strlen(why_line);
        if (wlen >= explain_len) wlen = explain_len - 1;
        strncpy(explain_buf, why_line, wlen);
        explain_buf[wlen] = '\0';
        for (int i = (int)wlen - 1; i >= 0 && (explain_buf[i] == ' ' || explain_buf[i] == '\r'); i--)
            explain_buf[i] = '\0';
    }

    return 0;
}

/* ── Execute AI suggestion with confirmation ──────────────── */
int execute_ai_suggestion(synsh_state_t *s,
                            const char *suggested_cmd,
                            const char *explanation)
{
    if (!suggested_cmd || !*suggested_cmd) return 1;

    /* Don't run "# not possible" */
    if (suggested_cmd[0] == '#') {
        if (s->color)
            printf(COLOR_WARN "  Synapse: %s\n" COLOR_RESET,
                   explanation ? explanation : "No shell equivalent found.");
        else
            printf("  Synapse: %s\n",
                   explanation ? explanation : "No shell equivalent found.");
        return 1;
    }

    /* Display the suggested command */
    if (s->color) {
        printf(COLOR_AI "  ⚡ " COLOR_RESET
               COLOR_CMD "%s" COLOR_RESET "\n",
               suggested_cmd);
        if (explanation && *explanation)
            printf(COLOR_DIM "     %s\n" COLOR_RESET, explanation);
    } else {
        printf("  > %s\n", suggested_cmd);
        if (explanation && *explanation)
            printf("    %s\n", explanation);
    }

    /* Confirmation step (if enabled) */
    if (s->ai_confirm) {
        if (s->color)
            printf(COLOR_PROMPT "  Run? [Y/n/e] " COLOR_RESET);
        else
            printf("  Run? [Y/n/e] ");
        fflush(stdout);

        char ans[8] = {0};
        if (!fgets(ans, sizeof(ans), stdin)) return 1;

        char ch = ans[0];

        if (ch == 'n' || ch == 'N') {
            printf("  Cancelled.\n");
            return 0;
        }
        if (ch == 'e' || ch == 'E') {
            /* Edit: print command for user to edit in readline */
            printf("  Edit in shell: %s\n", suggested_cmd);
            /* In a full implementation we'd push this into readline's buffer */
            return 0;
        }
        /* Y or Enter = proceed */
    }

    /* Execute */
    int exit_code = execute_pipeline(s, suggested_cmd);

    if (s->ai_explain && exit_code == 0 && s->color)
        printf(COLOR_DIM "  ✓ exit %d\n" COLOR_RESET, exit_code);
    else if (exit_code != 0 && s->color)
        printf(COLOR_ERR "  ✗ exit %d\n" COLOR_RESET, exit_code);

    return exit_code;
}
