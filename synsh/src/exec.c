/*
 * exec.c — Command execution for synsh
 *
 * Handles:
 *   - Shell pipeline execution (fork/exec with pipes)
 *   - AI suggestion display + confirmation
 *   - AI translation prompt construction
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
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
#include "intents.h"
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

        /* A built-in used as a pipeline stage (`syn status | grep foo`) runs
         * here, in the child, with the redirections above already in place.
         * Its side effects don't escape — same as a real shell, where a
         * built-in in a pipeline runs in a subshell. A built-in on its own
         * never reaches this path; run_segment() runs it in-process so that
         * `cd` can actually change synsh's directory. */
        if (synsh_is_builtin(filtered[0]))
            exit(synsh_builtin(s, fargc, filtered));

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
 * Aliases were parsed out of synshrc into s->alias_* and then never used —
 * the expander was written but never called, so every alias the shipped
 * synshrc defines was dead ('ll' was "not found", and '..' tried to *execute*
 * the directory).
 *
 * Expansion happens at each command position — the start of the segment and
 * just after an unquoted '|' — like a real shell, so `foo | ll` expands too.
 * A quoted first word is left alone, and an alias is never expanded twice in
 * the same position, so the usual `alias ls='ls --color'` self-reference
 * terminates instead of looping.
 */
static int alias_lookup(synsh_state_t *s, const char *name) {
    for (int i = 0; i < s->alias_count; i++)
        if (strcmp(s->alias_names[i], name) == 0) return i;
    return -1;
}

/* Expand the command word at the head of 'cmd' (in place, bounded). */
static void alias_expand_word(synsh_state_t *s, char *cmd, size_t cap) {
    const char *seen[8];
    int nseen = 0;

    for (int iter = 0; iter < 8; iter++) {
        /* First word of the current expansion. */
        char word[128];
        size_t wl = 0;
        const char *p = cmd;
        while (*p == ' ' || *p == '\t') p++;
        while (*p && *p != ' ' && *p != '\t' && wl < sizeof(word) - 1)
            word[wl++] = *p++;
        word[wl] = '\0';
        if (!wl) return;

        int idx = alias_lookup(s, word);
        if (idx < 0) return;

        for (int k = 0; k < nseen; k++)
            if (strcmp(seen[k], word) == 0) return;  /* already used here */
        seen[nseen++] = s->alias_names[idx];

        /* value + whatever followed the word */
        char next[SYNSH_MAX_LINE];
        snprintf(next, sizeof(next), "%s%s", s->alias_values[idx], p);
        snprintf(cmd, cap, "%s", next);

        if (nseen >= (int)(sizeof(seen) / sizeof(seen[0]))) return;
    }
}

/* Returns a malloc'd copy of 'line' with aliases expanded, or NULL. */
static char *alias_expand_line(synsh_state_t *s, const char *line) {
    if (!s->alias_count) return NULL;

    char *out = malloc(SYNSH_MAX_LINE);
    if (!out) return NULL;
    size_t olen = 0;

    const char *p = line;
    int at_cmd = 1;

    while (*p && olen < SYNSH_MAX_LINE - 1) {
        if (at_cmd) {
            while ((*p == ' ' || *p == '\t') && olen < SYNSH_MAX_LINE - 1)
                out[olen++] = *p++;

            /* A quoted command word is never an alias. */
            if (*p && *p != '\'' && *p != '"' && *p != '|') {
                char word[128];
                size_t wl = 0;
                const char *w = p;
                while (*w && *w != ' ' && *w != '\t' && *w != '|' &&
                       wl < sizeof(word) - 1)
                    word[wl++] = *w++;
                word[wl] = '\0';

                if (wl && alias_lookup(s, word) >= 0) {
                    char cmd[SYNSH_MAX_LINE];
                    snprintf(cmd, sizeof(cmd), "%s", word);
                    alias_expand_word(s, cmd, sizeof(cmd));
                    for (const char *e = cmd; *e && olen < SYNSH_MAX_LINE - 1; e++)
                        out[olen++] = *e;
                    p = w;  /* the original word is consumed */
                }
            }
            at_cmd = 0;
            continue;
        }

        /* Copy the rest of this command verbatim, minding quotes; an
         * unquoted '|' opens a new command position. */
        if (*p == '\'' || *p == '"') {
            char q = *p;
            out[olen++] = *p++;
            while (*p && *p != q && olen < SYNSH_MAX_LINE - 1) out[olen++] = *p++;
            if (*p && olen < SYNSH_MAX_LINE - 1) out[olen++] = *p++;
        } else if (*p == '|') {
            out[olen++] = *p++;
            at_cmd = 1;
        } else {
            out[olen++] = *p++;
        }
    }

    out[olen] = '\0';
    return out;
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

/* ── Command lists:  ;  &&  ||  ───────────────────────────── */
/*
 * execute_pipeline() understands a single pipeline and nothing else, and the
 * built-ins used to be reachable only from the REPL's classifier. So
 * `synsh -c 'cd /etc'` forked and exec'd a binary called "cd" (there isn't
 * one), and `a && b` was handed to execve as literal argv.
 *
 * This layer sits above the pipeline: it splits a line on the top-level
 * separators, short-circuits the way a POSIX shell does, and routes each
 * segment to a built-in or a pipeline. Everything that runs a command line
 * — the REPL, -c, scripts, stdin — goes through here, so `cd` behaves the
 * same everywhere.
 */
typedef enum { SEP_END, SEP_SEQ, SEP_AND, SEP_OR } sep_t;

/* Does this segment contain an unquoted '|'? */
static int has_pipe(const char *seg) {
    int in_q = 0;
    char q = 0;
    for (const char *p = seg; *p; p++) {
        if (!in_q && (*p == '\'' || *p == '"')) { in_q = 1; q = *p; }
        else if (in_q && *p == q)               { in_q = 0; }
        else if (!in_q && *p == '|')            return 1;
    }
    return 0;
}

/*
 * Run a built-in in-process, applying any redirections to synsh's own fds.
 * Built-ins don't fork, so there is no child to dup2 — we save the real fds,
 * swap them, run, then put them back. Without this, `cd x > log` passed ">"
 * and "log" to the built-in as plain arguments.
 */
/*
 * Pull the redirections out of a command line at the *string* level, leaving
 * the command itself for execute_builtin_line() to tokenize. Doing it this way
 * (rather than filtering an argv) matters: the built-in arg splitter strips
 * quotes mid-token, which is what makes `alias ll='ls -la'` parse — exec.c's
 * tokenize() only honours a quote at the start of a word and would mangle it.
 */
static int split_redirs(const char *line,
                        char *clean, size_t clean_cap,
                        char *in_f,  size_t in_cap,
                        char *out_f, size_t out_cap, int *append)
{
    size_t cl = 0;
    int in_q = 0;
    char q = 0;

    *append = 0;
    in_f[0] = out_f[0] = '\0';

    for (const char *p = line; *p; ) {
        if (!in_q && (*p == '\'' || *p == '"')) {
            in_q = 1; q = *p;
            if (cl < clean_cap - 1) clean[cl++] = *p;
            p++;
            continue;
        }
        if (in_q && *p == q) {
            in_q = 0;
            if (cl < clean_cap - 1) clean[cl++] = *p;
            p++;
            continue;
        }
        if (!in_q && (*p == '>' || *p == '<')) {
            int is_out = (*p == '>');
            int app = 0;
            p++;
            if (is_out && *p == '>') { app = 1; p++; }
            while (*p == ' ' || *p == '\t') p++;

            char fbuf[512];
            size_t fl = 0;
            if (*p == '\'' || *p == '"') {
                char fq = *p++;
                while (*p && *p != fq && fl < sizeof(fbuf) - 1) fbuf[fl++] = *p++;
                if (*p) p++;
            } else {
                while (*p && *p != ' ' && *p != '\t' && fl < sizeof(fbuf) - 1)
                    fbuf[fl++] = *p++;
            }
            fbuf[fl] = '\0';
            if (!fl) return -1;  /* redirection with no target */

            if (is_out) { snprintf(out_f, out_cap, "%s", fbuf); *append = app; }
            else        { snprintf(in_f,  in_cap,  "%s", fbuf); }
            continue;
        }
        if (cl < clean_cap - 1) clean[cl++] = *p;
        p++;
    }

    clean[cl] = '\0';
    return 0;
}

static int run_builtin_redirected(synsh_state_t *s, const char *line) {
    char clean[SYNSH_MAX_LINE];
    char in_f[512], out_f[512];
    int append = 0;

    if (split_redirs(line, clean, sizeof(clean),
                     in_f, sizeof(in_f), out_f, sizeof(out_f), &append) < 0) {
        fprintf(stderr, "synsh: syntax error near redirection\n");
        return 1;
    }

    /* No redirection: the common case, nothing to save or restore. */
    if (!*in_f && !*out_f)
        return execute_builtin_line(s, clean);

    int saved_in = -1, saved_out = -1;

    if (*in_f) {
        char *path = expand_tilde(in_f, s->home);
        int fd = open(path, O_RDONLY);
        free(path);
        if (fd < 0) {
            fprintf(stderr, "synsh: %s: %s\n", in_f, strerror(errno));
            return 1;
        }
        saved_in = dup(STDIN_FILENO);
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (*out_f) {
        char *path = expand_tilde(out_f, s->home);
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(path, flags, 0644);
        free(path);
        if (fd < 0) {
            fprintf(stderr, "synsh: %s: %s\n", out_f, strerror(errno));
            if (saved_in >= 0) { dup2(saved_in, STDIN_FILENO); close(saved_in); }
            return 1;
        }
        saved_out = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    int rc = execute_builtin_line(s, clean);

    /* Flush before restoring, or the built-in's buffered output lands on
     * whatever fd we restore rather than in the file. */
    fflush(stdout);
    if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
    if (saved_in  >= 0) { dup2(saved_in,  STDIN_FILENO);  close(saved_in);  }

    return rc;
}

/* One segment: a built-in, or a pipeline. */
static int run_segment(synsh_state_t *s, char *seg) {
    /* Aliases first — an alias may expand *to* a built-in (`..` → `cd ..`),
     * so this has to happen before we decide what this segment is. */
    char *expanded = alias_expand_line(s, seg);
    char *cmd = expanded ? expanded : seg;

    /* First word decides — built-ins never appear on $PATH. A built-in that
     * is part of a pipeline goes down the pipeline path instead, where it
     * runs in the forked child (see run_cmd). */
    char first[64];
    size_t i = 0;
    const char *r = cmd;
    while (*r == ' ' || *r == '\t') r++;
    while (*r && *r != ' ' && *r != '\t' && *r != '<' && *r != '>' &&
           i < sizeof(first) - 1)
        first[i++] = *r++;
    first[i] = '\0';

    int rc;
    if (synsh_is_builtin(first) && !has_pipe(cmd))
        rc = run_builtin_redirected(s, cmd);
    else
        rc = execute_pipeline(s, cmd);

    free(expanded);
    return rc;
}

int execute_command_line(synsh_state_t *s, const char *line) {
    if (!line || !*line) return 0;

    char *buf = strdup(line);
    if (!buf) return 1;

    int exit_code = 0;
    char *p = buf;
    sep_t pending = SEP_SEQ;  /* the first segment always runs */

    while (1) {
        char *seg = p;

        /* Scan to the next separator that isn't inside quotes. A lone '|'
         * (pipe) and a lone '&' (background) are left in the segment for
         * execute_pipeline()/run_cmd() to deal with — only the doubled
         * forms are list separators. */
        int in_q = 0;
        char q = 0;
        sep_t sep = SEP_END;
        while (*p) {
            if (!in_q && (*p == '\'' || *p == '"')) { in_q = 1; q = *p; }
            else if (in_q && *p == q)               { in_q = 0; }
            else if (!in_q) {
                if (p[0] == '&' && p[1] == '&') { sep = SEP_AND; break; }
                if (p[0] == '|' && p[1] == '|') { sep = SEP_OR;  break; }
                if (p[0] == ';')                { sep = SEP_SEQ; break; }
            }
            p++;
        }

        if (sep != SEP_END) {
            int seplen = (sep == SEP_SEQ) ? 1 : 2;
            *p = '\0';
            p += seplen;
        }

        /* Short-circuit against the previous segment's status. Skipping a
         * segment leaves exit_code alone, so `false && a || b` still sees
         * the failure at the '||' and runs b. */
        int run = (pending == SEP_SEQ) ||
                  (pending == SEP_AND && exit_code == 0) ||
                  (pending == SEP_OR  && exit_code != 0);

        char *t = seg;
        while (*t == ' ' || *t == '\t') t++;
        if (*t && run)
            exit_code = run_segment(s, t);

        if (sep == SEP_END) break;
        pending = sep;
    }

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
    /* Tell it what this machine actually is. Without the Installed: line it
     * suggests whatever is commonest on the internet — apt-get, systemctl
     * start on a unit that doesn't exist, rhythmbox on a box with no audio
     * player — and every one of those looks plausible until you run it.
     * synsh_intent_toolinfo() is resolved from $PATH, so it cannot drift from
     * reality the way a hardcoded list would. */
    char prompt[SYNSH_MAX_LINE * 2];
    snprintf(prompt, sizeof(prompt),
        "[TRANSLATE TO SHELL]\n"
        "OS: SynapseOS — Arch Linux. NOT Debian/Ubuntu/Fedora.\n"
        "Installed: %s\n"
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
        "- Arch syntax ONLY. Packages: pacman -S / -Rns / -Syu / -Ss / -Q.\n"
        "  Never apt, apt-get, dnf, yum, zypper, snap or flatpak.\n"
        "- Upgrading is `sudo pacman -Syu`, never a bare -Sy: syncing the\n"
        "  databases without upgrading is a partial upgrade and breaks Arch.\n"
        "- Services are systemctl; logs are journalctl. No service(8), no /etc/init.d.\n"
        "- Only name a program listed in Installed above. If the request needs one\n"
        "  that is not there, CMD must be the pacman -S that installs it.\n"
        "- If the request is ambiguous, pick the safest interpretation\n"
        "- Never include rm -rf without explicit confirmation request\n"
        "- Use sudo for privileged commands, NEVER su — the root account is locked\n"
        "- If no shell command makes sense, write CMD: # not possible\n",
        synsh_intent_toolinfo(),
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

    /* Models habitually wrap the command in quotes or backticks
     * ('df -h | awk ...'). Left in place, the whole pipeline parses as
     * one giant argv[0] and exec fails with 127 — strip matched pairs. */
    size_t blen = strlen(cmd_buf);
    while (blen >= 2 &&
           (cmd_buf[0] == '\'' || cmd_buf[0] == '"' || cmd_buf[0] == '`') &&
           cmd_buf[blen-1] == cmd_buf[0]) {
        memmove(cmd_buf, cmd_buf + 1, blen - 2);
        cmd_buf[blen-2] = '\0';
        blen -= 2;
    }
    /* ...and sometimes prefix a shell prompt marker */
    if (cmd_buf[0] == '$' && cmd_buf[1] == ' ')
        memmove(cmd_buf, cmd_buf + 2, strlen(cmd_buf + 2) + 1);

    /* Root is locked on installed systems, so a suggested `su` can never
     * authenticate — it prompts for a password that does not exist. The
     * prompt rule forbids it, but models still reach for it; rewrite the
     * common forms to sudo. */
    const char *su_rest = NULL;
    if      (strncmp(cmd_buf, "su -c ", 6) == 0)       su_rest = cmd_buf + 6;
    else if (strncmp(cmd_buf, "su - -c ", 8) == 0)     su_rest = cmd_buf + 8;
    else if (strncmp(cmd_buf, "su root -c ", 11) == 0) su_rest = cmd_buf + 11;
    if (su_rest) {
        char *rest = strdup(su_rest);
        if (rest && strlen(rest) + sizeof("sudo sh -c ") <= cmd_len)
            snprintf(cmd_buf, cmd_len, "sudo sh -c %s", rest);
        free(rest);
    } else if (strcmp(cmd_buf, "su") == 0     || strcmp(cmd_buf, "su -") == 0 ||
               strcmp(cmd_buf, "su root") == 0 || strcmp(cmd_buf, "su - root") == 0) {
        snprintf(cmd_buf, cmd_len, "sudo -i");
    }

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
        printf("%s  Synapse: %s\n%s", COLOR_WARN,
               explanation ? explanation : "No shell equivalent found.",
               COLOR_RESET);
        return 1;
    }

    /* Display the suggested command */
    printf("%s  ⚡ %s%s%s%s\n", COLOR_AI, COLOR_RESET,
           COLOR_CMD, suggested_cmd, COLOR_RESET);
    if (explanation && *explanation)
        printf("%s     %s\n%s", COLOR_DIM, explanation, COLOR_RESET);

    /* Confirmation step (if enabled) */
    if (s->ai_confirm) {
        printf("%s  Run? [Y/n/e] %s", COLOR_PROMPT, COLOR_RESET);
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

    /* Execute. Goes through the command-line layer: the model happily
     * emits things like `mkdir -p x && cd x`. */
    int exit_code = execute_command_line(s, suggested_cmd);

    /* The exit line is reported whether or not colour is on — it used to be
     * gated on s->color, so --no-color silently swallowed the status. */
    if (s->ai_explain && exit_code == 0)
        printf("%s  ✓ exit %d\n%s", COLOR_DIM, exit_code, COLOR_RESET);
    else if (exit_code != 0)
        printf("%s  ✗ exit %d\n%s", COLOR_ERR, exit_code, COLOR_RESET);

    return exit_code;
}
