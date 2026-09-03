/*
 * exec.c — Command execution for synsh
 *
 * Handles:
 *   - Command lists (;  &&  ||  &) and pipelines
 *   - Redirection, including the fd forms (2>, 2>&1, &>)
 *   - Background jobs
 *   - AI suggestion display + confirmation
 *   - AI translation prompt construction
 *
 * ⚠ A PIPELINE'S STAGES ALL RUN AT ONCE, and that is not a refinement — it is
 * the difference between working and hanging. This used to fork one stage,
 * WAIT for it, and only then fork the next: so the first stage filled the
 * 64 KiB pipe buffer and blocked forever writing into a pipe whose reader had
 * not been created yet. `seq 1 200000 | wc -l` never returned, and neither did
 * `yes | head -1`, which is a reader that exits early. Both are ordinary
 * commands, and the shell simply stopped.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <termios.h>

#include "synsh.h"
#include "exec.h"
#include "expand.h"
#include "i18n.h"
#include "intents.h"
#include "ipc.h"
#include "color.h"

/* ── Redirections ─────────────────────────────────────────── */
/*
 * Parsed out of the operator tokens the splitter marked. The operator carries
 * the fd it applies to, which is why `2>` and `>` are one code path here
 * rather than two: they differ only in a number.
 */
typedef enum {
    R_IN, R_OUT, R_APPEND, R_DUP, R_CLOSE, R_OUT_BOTH, R_APPEND_BOTH
} redir_kind_t;

typedef struct {
    redir_kind_t kind;
    int          fd;       /* the descriptor being redirected */
    int          dupfd;    /* R_DUP: the descriptor to copy from */
    char        *target;   /* the file, for every kind but R_DUP and R_CLOSE */
} redir_t;

#define MAX_REDIRS 16

/*
 * Read one operator token. Returns 0 on success, -1 if it is not a
 * redirection we understand — which is reported, not ignored, because
 * swallowing an operator silently is how a shell writes to a file it was told
 * to write to a different one.
 */
static int parse_redir(const char *op, redir_t *r)
{
    const char *p = op;
    int fd = -1;

    memset(r, 0, sizeof(*r));

    if (strcmp(op, "&>") == 0)  { r->kind = R_OUT_BOTH;    r->fd = 1; return 0; }
    if (strcmp(op, "&>>") == 0) { r->kind = R_APPEND_BOTH; r->fd = 1; return 0; }

    if (isdigit((unsigned char)*p)) {
        fd = 0;
        while (isdigit((unsigned char)*p)) fd = fd * 10 + (*p++ - '0');
    }

    if (*p == '<') {
        p++;
        r->fd = (fd >= 0) ? fd : 0;
        if (*p == '&') {
            p++;
            if (*p == '-') { r->kind = R_CLOSE; return 0; }
            r->kind = R_DUP;
            r->dupfd = atoi(p);
            return 0;
        }
        r->kind = R_IN;
        return 0;
    }

    if (*p == '>') {
        p++;
        r->fd = (fd >= 0) ? fd : 1;
        if (*p == '>') { p++; r->kind = R_APPEND; return 0; }
        if (*p == '&') {
            p++;
            if (*p == '-') { r->kind = R_CLOSE; return 0; }
            r->kind = R_DUP;
            r->dupfd = atoi(p);
            return 0;
        }
        r->kind = R_OUT;
        return 0;
    }

    return -1;
}

/* Apply the parsed redirections. Runs in the child, after the pipe fds are in
 * place, and in the order they were written — which is what makes
 * `>log 2>&1` send both streams to the file and `2>&1 >log` not. */
static int apply_redirs(const redir_t *rs, int n)
{
    for (int i = 0; i < n; i++) {
        const redir_t *r = &rs[i];
        int fd;

        switch (r->kind) {
        case R_IN:
            fd = open(r->target, O_RDONLY);
            if (fd < 0) { fprintf(stderr, "synsh: %s: %s\n", r->target, strerror(errno)); return -1; }
            dup2(fd, r->fd);
            if (fd != r->fd) close(fd);
            break;

        case R_OUT:
        case R_APPEND:
        case R_OUT_BOTH:
        case R_APPEND_BOTH: {
            int append = (r->kind == R_APPEND || r->kind == R_APPEND_BOTH);
            fd = open(r->target, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
            if (fd < 0) { fprintf(stderr, "synsh: %s: %s\n", r->target, strerror(errno)); return -1; }
            dup2(fd, r->fd);
            if (r->kind == R_OUT_BOTH || r->kind == R_APPEND_BOTH)
                dup2(fd, STDERR_FILENO);
            if (fd != r->fd) close(fd);
            break;
        }

        case R_DUP:
            if (dup2(r->dupfd, r->fd) < 0) {
                fprintf(stderr, "synsh: %d: %s\n", r->dupfd, strerror(errno));
                return -1;
            }
            break;

        case R_CLOSE:
            close(r->fd);
            break;
        }
    }
    return 0;
}

static void free_redirs(redir_t *rs, int n)
{
    for (int i = 0; i < n; i++) free(rs[i].target);
}

/*
 * Split an expanded word list into the command's argv and its redirections.
 * Returns 0, or -1 on a syntax error already reported.
 */
static int take_redirs(synsh_state_t *s, synsh_words_t *w,
                       char **argv, int max_argv, int *argc_out,
                       redir_t *rs, int *nred_out, int *background)
{
    int argc = 0, nred = 0;
    *background = 0;

    for (int i = 0; i < w->n; i++) {
        if (!w->op[i]) {
            if (argc >= max_argv - 1) {
                fprintf(stderr, "synsh: %s\n", T(M_TOO_MANY_ARGS));
                free_redirs(rs, nred);
                return -1;
            }
            argv[argc++] = w->v[i];
            continue;
        }

        if (strcmp(w->v[i], "&") == 0) { *background = 1; continue; }

        if (nred >= MAX_REDIRS) {
            fprintf(stderr, "%s\n", T(M_TOO_MANY_REDIR));
            free_redirs(rs, nred);
            return -1;
        }

        redir_t r;
        if (parse_redir(w->v[i], &r) < 0) {
            fprintf(stderr, "synsh: %s: %s\n", w->v[i], T(M_SYNTAX_REDIR));
            free_redirs(rs, nred);
            return -1;
        }

        if (r.kind != R_DUP && r.kind != R_CLOSE) {
            /* The target is the next word, and it must not itself be an
             * operator: `> > x` is a syntax error, not a file called ">". */
            if (i + 1 >= w->n || w->op[i + 1]) {
                fprintf(stderr, "synsh: %s\n", T(M_SYNTAX_REDIR));
                free_redirs(rs, nred);
                return -1;
            }
            r.target = strdup(w->v[++i]);
            if (!r.target) { free_redirs(rs, nred); return -1; }
        }
        rs[nred++] = r;
    }

    (void)s;
    argv[argc] = NULL;
    *argc_out = argc;
    *nred_out = nred;
    return 0;
}
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


/* ── Jobs ─────────────────────────────────────────────────── */
/*
 * The job list was declared in synsh_state_t from the beginning and never
 * written to, so `jobs` answered "No background jobs." however many were
 * running — and there could not be any, because `&` was not a separator
 * either: `sleep 5 & echo done` ran `sleep 5 echo done` and complained about
 * an invalid time interval.
 *
 * ⚠ NO SIGCHLD HANDLER ANY MORE, and that is the fix for a second bug. The
 * old one called waitpid(-1) on every child, foreground ones included — so a
 * foreground command whose SIGCHLD arrived while the shell sat in waitpid()
 * had already been reaped by the time waitpid returned, which returned ECHILD
 * and left `status` UNINITIALISED. The exit code of that command was then
 * whatever was on the stack. Background children are reaped here instead, at
 * the prompt, which is both correct and where a shell is expected to announce
 * that a job has finished.
 */
static void job_add(synsh_state_t *s, pid_t pid, const char *cmd)
{
    syn_job_t *j = calloc(1, sizeof(*j));
    if (!j) return;
    j->id          = ++s->next_job_id;
    j->pid         = pid;
    j->state       = JOB_RUNNING;
    j->command_str = strdup(cmd ? cmd : "");
    j->next        = s->jobs;
    s->jobs        = j;
    printf("[%d] %d\n", j->id, (int)pid);
}

void synsh_reap_jobs(synsh_state_t *s)
{
    syn_job_t **link = &s->jobs;
    while (*link) {
        syn_job_t *j = *link;
        int st;
        pid_t r = waitpid(j->pid, &st, WNOHANG);
        if (r == j->pid || (r < 0 && errno == ECHILD)) {
            if (s->interactive)
                printf("[%d]  Done\t%s\n", j->id, j->command_str);
            *link = j->next;
            free(j->command_str);
            free(j);
            continue;
        }
        link = &j->next;
    }
}

void synsh_jobs_free(synsh_state_t *s)
{
    syn_job_t *j = s->jobs;
    while (j) {
        syn_job_t *n = j->next;
        free(j->command_str);
        free(j);
        j = n;
    }
    s->jobs = NULL;
}

/* ── Variable assignments ─────────────────────────────────── */
/*
 * `NAME=value`, on its own or in front of a command.
 *
 * synsh had `export` and nothing else, so `EDITOR=vi` looked for a program
 * called "EDITOR=vi" and `MAKEFLAGS=-j8 make` looked for one called
 * "MAKEFLAGS=-j8" — a shell that could not do the most ordinary thing anybody
 * does with an environment variable, and said "command not found" about it,
 * which reads as the variable being the problem rather than the syntax.
 *
 * Assignments in front of a command apply to THAT COMMAND ONLY, set in the
 * child after the fork. On their own they apply to the shell. That is the
 * POSIX split and it is the one people rely on: `LANG=C ls` must not leave the
 * shell in C afterwards.
 */
static bool is_assignment(const char *w)
{
    if (!w || (!isalpha((unsigned char)*w) && *w != '_')) return false;
    const char *p = w + 1;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    return *p == '=';
}

static void apply_assignment(const char *w)
{
    const char *eq = strchr(w, '=');
    if (!eq) return;
    char name[256];
    size_t n = (size_t)(eq - w);
    if (n >= sizeof(name)) return;
    memcpy(name, w, n);
    name[n] = '\0';
    setenv(name, eq + 1, 1);
}

/* Is this whole command nothing but assignments? Then they are the shell's. */
static bool words_all_assignments(const synsh_words_t *w)
{
    if (w->n == 0) return false;
    for (int i = 0; i < w->n; i++)
        if (w->op[i] || !is_assignment(w->v[i])) return false;
    return true;
}

/* ── One stage of a pipeline ──────────────────────────────── */
/*
 * Forks and returns immediately with the child's pid — it does NOT wait. The
 * waiting is done by the caller, after every stage exists, which is the whole
 * point (see the note at the top of this file).
 *
 * `close_in_child` is the read end of the pipe feeding the NEXT stage: the
 * child must not keep it open, or the stage after it never sees EOF.
 */
static pid_t run_stage(synsh_state_t *s, synsh_words_t *w,
                       int stdin_fd, int stdout_fd, int close_in_child,
                       int *background)
{
    char    *argv[SYNSH_MAX_ARGS];
    redir_t  rs[MAX_REDIRS];
    int      argc = 0, nred = 0;

    if (take_redirs(s, w, argv, SYNSH_MAX_ARGS, &argc, rs, &nred, background) < 0)
        return -1;

    /* Assignments in front of the command word: set in the child only. */
    int nassign = 0;
    while (nassign < argc && is_assignment(argv[nassign])) nassign++;

    if (argc == 0) {
        /* A stage that is nothing but redirections still creates its files —
         * `> log` truncates log, as it does in every shell. */
        if (nred) {
            pid_t p = fork();
            if (p == 0)
                _exit(apply_redirs(rs, nred) < 0 ? 1 : 0);
            free_redirs(rs, nred);
            return p;
        }
        free_redirs(rs, nred);
        return -2;   /* genuinely empty: not an error, nothing to run */
    }

    /* The targets are already expanded — the splitter did it, once, at the only
     * point where it still knew what was quoted (see finish_word_as). Expanding
     * again here is not a safety net, it is a second expansion: a file called
     * `a$b`, quoted at the prompt, would become `a`. */

    pid_t pid = fork();
    if (pid < 0) {
        perror("synsh: fork");
        free_redirs(rs, nred);
        return -1;
    }

    if (pid == 0) {
        if (stdin_fd != STDIN_FILENO) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }
        if (stdout_fd != STDOUT_FILENO) {
            dup2(stdout_fd, STDOUT_FILENO);
            close(stdout_fd);
        }
        if (close_in_child >= 0) close(close_in_child);

        if (apply_redirs(rs, nred) < 0) _exit(1);

        for (int i = 0; i < nassign; i++) apply_assignment(argv[i]);

        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);

        /* A built-in used as a pipeline stage (`syn status | grep foo`) runs
         * here, in the child, with the redirections already in place. Its side
         * effects don't escape — same as a real shell, where a built-in in a
         * pipeline runs in a subshell. A built-in on its own never reaches
         * this path; run_segment() runs it in-process so `cd` can actually
         * change synsh's directory. */
        if (synsh_is_builtin(argv[nassign]))
            _exit(synsh_builtin(s, argc - nassign, argv + nassign));

        execvp(argv[nassign], argv + nassign);
        fprintf(stderr, "synsh: %s: %s\n", argv[nassign], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    free_redirs(rs, nred);
    return pid;
}

/* ── Execute a pipeline string ────────────────────────────── */
/*
 * Scanning for the separator has to skip what is inside quotes AND inside a
 * command substitution: `echo $(a | b)` is one command with a pipeline in its
 * argument, and a scanner that only knew about quotes cut it in half.
 */
static const char *scan_to_pipe(const char *p)
{
    int depth = 0;
    char q = 0;

    for (; *p; p++) {
        if (q) { if (*p == q) q = 0; continue; }
        if (*p == '\'' || *p == '"' || *p == '`') { q = *p; continue; }
        if (*p == '\\' && p[1]) { p++; continue; }
        if (*p == '$' && p[1] == '(') { depth++; p++; continue; }
        if (*p == '(' && depth) { depth++; continue; }
        if (*p == ')' && depth) { depth--; continue; }
        if (!depth && *p == '|' && p[1] != '|') return p;
    }
    return NULL;
}

int execute_pipeline(synsh_state_t *s, const char *line)
{
    return execute_pipeline_bg(s, line, 0);
}

int execute_pipeline_bg(synsh_state_t *s, const char *line, int background)
{
    if (!line || !*line) return 0;

    char *buf = strdup(line);
    if (!buf) return 1;

    /* Cut into stages on unquoted, unnested '|'. */
    char *segments[SYNSH_MAX_PIPELINE];
    int   n_segs = 0;
    char *p = buf;
    while (n_segs < SYNSH_MAX_PIPELINE) {
        segments[n_segs++] = p;
        const char *cut = scan_to_pipe(p);
        if (!cut) break;
        char *c = (char *)cut;
        *c = '\0';
        p = c + 1;
    }

    pid_t pids[SYNSH_MAX_PIPELINE];
    int   npids = 0;
    int   exit_code = 0;
    int   prev_read = STDIN_FILENO;
    int   failed = 0;
    int   bg = background;

    for (int i = 0; i < n_segs; i++) {
        synsh_words_t w;
        if (synsh_words_split(s, segments[i], &w) < 0) { failed = 1; break; }
        if (w.n == 0) { synsh_words_free(&w); continue; }

        /* Nothing but assignments, and nothing else on the line: they belong to
         * this shell, so they must not be set inside a child that then exits.
         * A pipeline stage is a different matter — `X=1 | cat` is a subshell in
         * every shell there is — hence the single-stage test. */
        if (n_segs == 1 && !bg && words_all_assignments(&w)) {
            for (int k = 0; k < w.n; k++) apply_assignment(w.v[k]);
            synsh_words_free(&w);
            free(buf);
            return 0;
        }

        int fds[2] = { -1, -1 };
        int out_fd = STDOUT_FILENO;
        if (i < n_segs - 1) {
            if (pipe(fds) < 0) { perror("synsh: pipe"); synsh_words_free(&w); failed = 1; break; }
            out_fd = fds[1];
        }

        int stage_bg = 0;
        pid_t pid = run_stage(s, &w, prev_read, out_fd,
                              fds[0] >= 0 ? fds[0] : -1, &stage_bg);
        if (stage_bg) bg = 1;
        synsh_words_free(&w);

        /* The parent keeps neither end it handed on: the write end must close
         * here or the reader never sees EOF, and the previous read end has
         * been inherited by the child that needed it. */
        if (out_fd != STDOUT_FILENO) close(out_fd);
        if (prev_read != STDIN_FILENO) close(prev_read);
        prev_read = (fds[0] >= 0) ? fds[0] : STDIN_FILENO;

        if (pid == -1) { failed = 1; break; }
        if (pid >= 0) pids[npids++] = pid;
    }

    if (prev_read != STDIN_FILENO) close(prev_read);
    free(buf);

    if (failed && npids == 0) return 1;

    if (bg) {
        /* Only the last stage is followed; the rest are held open by the pipe
         * and finish when it does. Reaped at the next prompt. */
        if (npids) job_add(s, pids[npids - 1], line);
        for (int i = 0; i < npids - 1; i++) job_add(s, pids[i], line);
        return 0;
    }

    /* Wait for every stage, but report the LAST one's status — POSIX, and what
     * `false | true` returning 0 depends on. */
    for (int i = 0; i < npids; i++) {
        int st = 0;
        while (waitpid(pids[i], &st, 0) < 0 && errno == EINTR)
            ;
        if (i == npids - 1)
            exit_code = WIFEXITED(st) ? WEXITSTATUS(st)
                      : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 1;
    }

    return failed ? 1 : exit_code;
}

/* ── Command lists:  ;  &&  ||  &  ────────────────────────── */
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
typedef enum { SEP_END, SEP_SEQ, SEP_AND, SEP_OR, SEP_BG } sep_t;

/* Does this segment contain an unquoted, unnested '|'? */
static int has_pipe(const char *seg) { return scan_to_pipe(seg) != NULL; }

/*
 * Run a built-in in-process, applying any redirections to synsh's own fds.
 * Built-ins don't fork, so there is no child to dup2 — we save the real fds,
 * swap them, run, then put them back. Without this, `cd x > log` passed ">"
 * and "log" to the built-in as plain arguments.
 */
int execute_builtin_line(synsh_state_t *s, const char *line)
{
    synsh_words_t w;
    if (synsh_words_split(s, line, &w) < 0) return 1;
    if (w.n == 0) { synsh_words_free(&w); return 0; }

    char    *argv[SYNSH_MAX_ARGS];
    redir_t  rs[MAX_REDIRS];
    int      argc = 0, nred = 0, background = 0;

    if (take_redirs(s, &w, argv, SYNSH_MAX_ARGS, &argc, rs, &nred, &background) < 0) {
        synsh_words_free(&w);
        return 1;
    }

    int rc;
    if (!nred) {
        rc = argc ? synsh_builtin(s, argc, argv) : 0;
    } else {
        /* Save, swap, run, restore. Only the descriptors actually touched are
         * saved: dup()ing all three on every built-in call is three syscalls
         * for the common case of none. */
        int saved[3] = { -1, -1, -1 };
        for (int i = 0; i < nred; i++)
            if (rs[i].fd >= 0 && rs[i].fd < 3 && saved[rs[i].fd] < 0)
                saved[rs[i].fd] = dup(rs[i].fd);

        rc = (apply_redirs(rs, nred) < 0) ? 1
           : (argc ? synsh_builtin(s, argc, argv) : 0);

        /* Flush before restoring, or the built-in's buffered output lands on
         * whatever fd we restore rather than in the file. */
        fflush(stdout);
        fflush(stderr);
        for (int fd = 0; fd < 3; fd++)
            if (saved[fd] >= 0) { dup2(saved[fd], fd); close(saved[fd]); }
    }

    free_redirs(rs, nred);
    synsh_words_free(&w);
    return rc;
}

/* One segment: a built-in, or a pipeline. */
static int run_segment(synsh_state_t *s, char *seg, int background)
{
    /* Aliases first — an alias may expand *to* a built-in (`..` → `cd ..`),
     * so this has to happen before we decide what this segment is. */
    char *expanded = alias_expand_line(s, seg);
    char *cmd = expanded ? expanded : seg;

    /* First word decides — built-ins never appear on $PATH. A built-in that is
     * part of a pipeline, or was sent to the background, goes down the
     * pipeline path instead, where it runs in a forked child. */
    char first[64];
    size_t i = 0;
    const char *r = cmd;
    while (*r == ' ' || *r == '\t') r++;
    while (*r && *r != ' ' && *r != '\t' && *r != '<' && *r != '>' &&
           i < sizeof(first) - 1)
        first[i++] = *r++;
    first[i] = '\0';

    int rc;
    if (synsh_is_builtin(first) && !has_pipe(cmd) && !background)
        rc = execute_builtin_line(s, cmd);
    else
        rc = execute_pipeline_bg(s, cmd, background);

    free(expanded);
    return rc;
}

int execute_command_line(synsh_state_t *s, const char *line)
{
    if (!line || !*line) return 0;

    char *buf = strdup(line);
    if (!buf) return 1;

    int exit_code = 0;
    char *p = buf;
    sep_t pending = SEP_SEQ;  /* the first segment always runs */

    while (1) {
        char *seg = p;

        /*
         * Scan to the next separator that is not inside quotes and not inside
         * a command substitution. A lone '|' (pipe) is left in the segment for
         * execute_pipeline(); a lone '&' IS a separator here, which is what
         * makes `sleep 5 & echo done` two commands instead of one command with
         * three arguments.
         */
        int depth = 0;
        char q = 0;
        sep_t sep = SEP_END;
        while (*p) {
            if (q) { if (*p == q) q = 0; p++; continue; }
            if (*p == '\'' || *p == '"' || *p == '`') { q = *p; p++; continue; }
            if (*p == '\\' && p[1]) { p += 2; continue; }
            if (*p == '$' && p[1] == '(') { depth++; p += 2; continue; }
            if (*p == '(' && depth) { depth++; p++; continue; }
            if (*p == ')' && depth) { depth--; p++; continue; }
            if (!depth) {
                if (p[0] == '&' && p[1] == '&') { sep = SEP_AND; break; }
                if (p[0] == '|' && p[1] == '|') { sep = SEP_OR;  break; }
                /* ⚠ AN '&' INSIDE A REDIRECTION IS NOT A SEPARATOR. `>&1`
                 * and `2>&1` both put one there, and treating it as
                 * backgrounding cut `ls >out 2>&1` into `ls >out 2>` and a
                 * command called `1` — which reported a syntax error and then
                 * created a file named 1. The character before it settles it:
                 * an operator is the only thing that can precede it. */
                if (p[0] == '&' && p[1] != '>' &&
                    !(p > buf && (p[-1] == '>' || p[-1] == '<')))
                                                { sep = SEP_BG;  break; }
                if (p[0] == ';')                { sep = SEP_SEQ; break; }
            }
            p++;
        }

        int seplen = 0;
        if (sep == SEP_AND || sep == SEP_OR) seplen = 2;
        else if (sep != SEP_END)             seplen = 1;
        if (sep != SEP_END) { *p = '\0'; p += seplen; }

        /* Short-circuit against the previous segment's status. Skipping a
         * segment leaves exit_code alone, so `false && a || b` still sees
         * the failure at the '||' and runs b. */
        int run = (pending == SEP_SEQ) || (pending == SEP_BG) ||
                  (pending == SEP_AND && exit_code == 0) ||
                  (pending == SEP_OR  && exit_code != 0);

        char *t = seg;
        while (*t == ' ' || *t == '\t') t++;
        if (*t && run) {
            int bg = (sep == SEP_BG);
            int rc = run_segment(s, t, bg);
            /* $? has to move with every command, not once per line: `false;
             * echo $?` reads it BETWEEN two segments, and the REPL's
             * assignment at the end of the line is far too late. */
            s->last_exit = bg ? 0 : rc;
            /* A backgrounded segment's status is 0 — the shell did not wait
             * for it, so it has none yet, and letting the previous command's
             * code leak into a following `&&` would be a lie. */
            exit_code = bg ? 0 : rc;
        }

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
    /* What the model is told about who is asking. It used to be the login
     * account name, unconditionally — which is how a fresh install whose account
     * was named after its owner ended up answering "hello" with "Hello <name>":
     * the account name was in every prompt, so the model addressed it. A shell
     * translator has no use for a name; what it can actually act on is whether
     * this session is root, which decides if the command needs sudo at all — and
     * that was never being sent. So send the privilege, not the identity. Set
     * `ai_user_name` in ~/.synshrc to be addressed by name again. */
    char whoami[128];
    if (s->ai_user_name[0])
        snprintf(whoami, sizeof(whoami), "User: %s (%s)\n", s->ai_user_name,
                 geteuid() == 0 ? "root" : "not root — use sudo for privileged commands");
    else
        snprintf(whoami, sizeof(whoami), "Privilege: %s\n",
                 geteuid() == 0 ? "root" : "not root — use sudo for privileged commands");

    /*
     * What language the EXPLANATION comes back in.
     *
     * The command itself is not translatable and must not be: `df -h` is `df
     * -h` in Polish. The WHY line is prose written for the person reading it,
     * and it used to come back in English on every machine — so a shell that
     * had just been asked a question in Polish answered it in a language its
     * user might not have. The instruction names the language in ENGLISH
     * ("Polish", not "Polski") because the prompt around it is English and a
     * 7B model acts on the English name far more reliably.
     *
     * Nothing is added at all when the language is English: an extra rule that
     * says "reply in English" to a model already replying in English is prompt
     * for the sake of it, and the budget is shared with the Installed: line.
     */
    char reply_lang[160] = "";
    if (synsh_lang() != LANG_EN)
        snprintf(reply_lang, sizeof(reply_lang),
                 "- Write the WHY line in %s. Leave CMD exactly as it must be typed.\n",
                 synsh_lang_english_name(synsh_lang()));

    char prompt[SYNSH_MAX_LINE * 2];
    snprintf(prompt, sizeof(prompt),
        "[TRANSLATE TO SHELL]\n"
        "OS: SynapseOS — Arch Linux. NOT Debian/Ubuntu/Fedora.\n"
        "Installed: %s\n"
        "CWD: %s\n"
        "%s"
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
        "- Privileged commands take sudo, NEVER su — the root account is locked.\n"
        "  Skip the sudo when Privilege above already says root.\n"
        "- If no shell command makes sense, write CMD: # not possible\n"
        "%s",
        synsh_intent_toolinfo(),
        s->cwd   ? s->cwd   : "/",
        whoami,
        natural_input,
        reply_lang
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
               (explanation && *explanation) ? explanation : T(M_NO_SHELL_EQUIV),
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
        printf("%s  %s [Y/n/e] %s", COLOR_PROMPT, T(M_RUN_CONFIRM), COLOR_RESET);
        fflush(stdout);

        char ans[8] = {0};
        if (!fgets(ans, sizeof(ans), stdin)) return 1;

        /* ⚠ ANYTHING PAST THE BUFFER IS STILL ON stdin, and this shell reads
         * its next command from stdin: type "no thanks" at this prompt and the
         * tail of it — " thanks" — became the next command line. Drain to the
         * newline before doing anything else.
         *
         * The KEYS stay Y/n/e in every language. They are the answer to a
         * yes/no question the user may be seeing for the first time in a
         * language the label was translated into, and a key that moves with the
         * locale cannot be documented, scripted, or remembered across two
         * machines. The label says what it means; the key stays put. */
        if (!strchr(ans, '\n')) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;
        }

        char ch = ans[0];

        if (ch == 'n' || ch == 'N') {
            printf("  %s\n", T(M_CANCELLED));
            return 0;
        }
        if (ch == 'e' || ch == 'E') {
            /* Edit: print command for user to edit in readline */
            printf("  %s %s\n", T(M_EDIT_IN_SHELL), suggested_cmd);
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
        printf("%s  ✓ %s %d\n%s", COLOR_DIM, T(M_EXIT), exit_code, COLOR_RESET);
    else if (exit_code != 0)
        printf("%s  ✗ %s %d\n%s", COLOR_ERR, T(M_EXIT), exit_code, COLOR_RESET);

    return exit_code;
}
