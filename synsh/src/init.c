/*
 * init.c — synsh initialization and RC file loading
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
#include <pwd.h>
#include <sys/stat.h>

#include "synsh.h"
#include "color.h"
#include "exec.h"

/* ── Init ─────────────────────────────────────────────────── */
int synsh_init(synsh_state_t *s, int argc, char *argv[]) {
    (void)argc; (void)argv;

    memset(s, 0, sizeof(*s));

    s->running    = 1;
    s->alias_count = 0;
    memset(s->alias_names,  0, sizeof(s->alias_names));
    memset(s->alias_values, 0, sizeof(s->alias_values));
    s->ai_confirm = 1;   /* ask before running AI commands by default */
    s->ai_explain = 1;
    s->color      = isatty(STDOUT_FILENO) ? 1 : 0;
    s->pid        = getpid();
    s->synapd_fd  = -1;

    /* Get user info */
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        s->home = strdup(pw->pw_dir);
        s->user = strdup(pw->pw_name);
    } else {
        s->home = strdup(getenv("HOME") ? getenv("HOME") : "/tmp");
        s->user = strdup(getenv("USER") ? getenv("USER") : "user");
    }

    /* Current directory */
    char buf[4096];
    if (getcwd(buf, sizeof(buf)))
        s->cwd = strdup(buf);
    else
        s->cwd = strdup("/");

    /* Allocate history */
    s->history = calloc(SYNSH_HISTORY_MAX, sizeof(char *));
    if (!s->history) return -1;

    /* Default prompt format */
    snprintf(s->prompt_fmt, sizeof(s->prompt_fmt), "[%%u@synapse %%d]%%s ");

    return 0;
}

/* ── RC file loading ──────────────────────────────────────── */
/*
 * synshrc format:
 *   # comment
 *   set ai_confirm off      → disable confirmation for AI commands
 *   set ai_explain on
 *   set color on|off
 *   alias ll='ls -la'
 *   export PATH=$PATH:/usr/local/sbin
 *   <any shell command>
 */
void synsh_load_rc(synsh_state_t *s) {
    /* Try user RC first, then system RC */
    const char *rc_paths[3] = {NULL, SYNSH_SYSTEM_RC, NULL};

    char user_rc[512] = {0};
    if (s->home) {
        snprintf(user_rc, sizeof(user_rc), "%s%s", s->home, SYNSH_RC_FILE);
        rc_paths[0] = user_rc;
    }

    for (int pi = 0; rc_paths[pi]; pi++) {
        FILE *f = fopen(rc_paths[pi], "r");
        if (!f) continue;

        char line[SYNSH_MAX_LINE];
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            if (line[0] == '#' || line[0] == '\0') continue;

            /* Handle 'set' directives */
            if (strncmp(line, "set ", 4) == 0) {
                char *key = line + 4;
                char *val = strchr(key, ' ');
                if (val) {
                    *val++ = '\0';
                    if (strcmp(key, "ai_confirm") == 0)
                        s->ai_confirm = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                    else if (strcmp(key, "ai_explain") == 0)
                        s->ai_explain = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                    else if (strcmp(key, "color") == 0)
                        s->color = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                    else if (strcmp(key, "verbose") == 0)
                        s->verbose = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                }
                continue;
            }

            /* Handle 'alias' directives */
            if (strncmp(line, "alias ", 6) == 0) {
                execute_builtin_line(s, line);
                continue;
            }
            /* Handle 'export' directives */
            if (strncmp(line, "export ", 7) == 0) {
                execute_builtin_line(s, line);
                continue;
            }
            /* Execute other lines as shell commands */
            execute_pipeline(s, line);
        }

        fclose(f);
    }
}

/* ── Reload config ────────────────────────────────────────── */
void synsh_reload_rc(synsh_state_t *s) {
    synsh_load_rc(s);
}

/* ── Destroy ──────────────────────────────────────────────── */
void synsh_destroy(synsh_state_t *s) {
    for (int i = 0; i < s->history_count; i++)
        free(s->history[i]);
    free(s->history);
    free(s->home);
    free(s->user);
    free(s->cwd);
    for (int i = 0; i < s->alias_count; i++) {
        free(s->alias_names[i]);
        free(s->alias_values[i]);
    }
    memset(s, 0, sizeof(*s));
}
