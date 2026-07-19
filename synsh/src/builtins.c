/*
 * builtins.c — synsh built-in commands
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "synsh.h"
#include "intents.h"

/* ── cd ───────────────────────────────────────────────────── */
static int builtin_cd(synsh_state_t *s, int argc, char **argv) {
    const char *dir;
    if (argc < 2 || strcmp(argv[1], "~") == 0) {
        dir = getenv("HOME");
        if (!dir) dir = "/";
    } else if (strcmp(argv[1], "-") == 0) {
        dir = getenv("OLDPWD");
        if (!dir) { fprintf(stderr, "cd: OLDPWD not set\n"); return 1; }
        printf("%s\n", dir);
    } else {
        dir = argv[1];
    }

    if (chdir(dir) < 0) {
        fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }

    setenv("OLDPWD", s->cwd ? s->cwd : "/", 1);
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        free(s->cwd);
        s->cwd = strdup(buf);
        setenv("PWD", buf, 1);
    }
    return 0;
}

/* ── export ───────────────────────────────────────────────── */
static int builtin_export(synsh_state_t *s, int argc, char **argv) {
    (void)s;
    if (argc < 2) {
        /* Print all exported vars */
        extern char **environ;
        for (char **e = environ; *e; e++)
            printf("export %s\n", *e);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
        } else {
            /* Just mark for export (already exported if in environ) */
        }
    }
    return 0;
}

/* ── jobs ─────────────────────────────────────────────────── */
static int builtin_jobs(synsh_state_t *s, int argc, char **argv) {
    (void)argc; (void)argv;
    syn_job_t *j = s->jobs;
    if (!j) { printf("No background jobs.\n"); return 0; }
    while (j) {
        const char *state = j->state == JOB_RUNNING ? "Running"
                          : j->state == JOB_STOPPED ? "Stopped" : "Done";
        printf("[%d]  %s\t%s\n", j->id, state, j->command_str);
        j = j->next;
    }
    return 0;
}

/* ── syn — meta-command ───────────────────────────────────── */
static int builtin_syn(synsh_state_t *s, int argc, char **argv) {
    if (argc < 2) {
        printf(
            "%ssynsh%s — SynapseOS shell %s\n\n"
            "  %ssyn status%s       — show synapd connection status\n"
            "  %ssyn ai on/off%s    — enable/disable AI assistance\n"
            "  %ssyn explain on/off%s — show AI explanations before exec\n"
            "  %ssyn safe on/off%s  — require confirmation for all AI commands\n"
            "  %ssyn stats%s        — show session statistics\n"
            "  %ssyn context%s      — show current AI context\n"
            "  %ssyn model%s        — show loaded model info\n",
            COL_BCYAN, COL_RESET, SYNSH_VERSION,
            COL_BGREEN, COL_RESET,
            COL_BGREEN, COL_RESET,
            COL_BGREEN, COL_RESET,
            COL_BGREEN, COL_RESET,
            COL_BGREEN, COL_RESET,
            COL_BGREEN, COL_RESET,
            COL_BGREEN, COL_RESET
        );
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        printf("synapd: %s%s%s\n",
            s->synapd_online ? COL_BGREEN : COL_YELLOW,
            s->synapd_online ? "online" : "offline",
            COL_RESET);
        printf("socket: %s\n", SYN_SOCKET_PATH);
        printf("ai: %s\n", s->ai_enabled ? "enabled" : "disabled");
        printf("explain: %s\n", s->explain_mode ? "on" : "off");
        printf("safe mode: %s\n", s->safe_mode ? "on" : "off");
        return 0;
    }

    if (strcmp(argv[1], "ai") == 0 && argc >= 3) {
        s->ai_enabled = strcmp(argv[2], "on") == 0;
        printf("AI assistance: %s\n", s->ai_enabled ? "enabled" : "disabled");
        return 0;
    }

    if (strcmp(argv[1], "explain") == 0 && argc >= 3) {
        s->explain_mode = strcmp(argv[2], "on") == 0;
        printf("Explain mode: %s\n", s->explain_mode ? "on" : "off");
        return 0;
    }

    if (strcmp(argv[1], "safe") == 0 && argc >= 3) {
        s->safe_mode = strcmp(argv[2], "on") == 0;
        printf("Safe mode: %s\n", s->safe_mode ? "on" : "off");
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0) {
        printf("Commands run:    %lu\n", s->commands_run);
        printf("NL queries:      %lu\n", s->nl_queries);
        printf("AI assists:      %lu\n", s->ai_assists);
        return 0;
    }

    if (strcmp(argv[1], "context") == 0) {
        /* Request context dump from synapd */
        if (!s->synapd_online) {
            fprintf(stderr, "syn: synapd offline\n");
            return 1;
        }
        syn_msg_header_t hdr = {
            .magic       = SYN_MAGIC,
            .version     = SYN_PROTO_VER,
            .msg_type    = SYN_MSG_CONTEXT_GET,
            .payload_len = 0,
            .request_id  = s->req_id_counter++,
            .client_pid  = (uint32_t)getpid(),
        };
        if (write(s->synapd_fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
            syn_msg_header_t resp;
            if (read(s->synapd_fd, &resp, sizeof(resp)) == sizeof(resp) && resp.payload_len) {
                char *buf = malloc(resp.payload_len + 1);
                if (buf) {
                    ssize_t r = read(s->synapd_fd, buf, resp.payload_len);
                    if (r > 0) { buf[r] = '\0'; printf("%s\n", buf); }
                    free(buf);
                }
            }
        }
        return 0;
    }

    fprintf(stderr, "syn: unknown subcommand '%s'. Try 'syn' for help.\n", argv[1]);
    return 1;
}

/* ── help ─────────────────────────────────────────────────── */
static int builtin_help(synsh_state_t *s, int argc, char **argv) {
    (void)s; (void)argc; (void)argv;
    printf(
        "\n%ssynsh — SynapseOS Natural Language Shell%s\n\n"
        "Type commands normally, or just speak naturally:\n\n"
        "  %s$ ls -la%s                        — regular command\n"
        "  %s$ show me disk usage%s             — natural language\n"
        "  %s$ what's using port 8080?%s        — question\n"
        "  %s$ find all logs older than 7 days%s — plain English\n\n"
        "Prefix with %s!%s to force command, %s?%s to force AI.\n\n"
        "Built-ins: cd, export, jobs, fg, bg, syn, help, exit\n"
        "Meta:      syn status | syn ai on/off | syn safe on/off\n\n",
        COL_BCYAN, COL_RESET,
        COL_BGREEN, COL_RESET,
        COL_BCYAN,  COL_RESET,
        COL_BCYAN,  COL_RESET,
        COL_BCYAN,  COL_RESET,
        COL_BOLD, COL_RESET,
        COL_BOLD, COL_RESET
    );
    /* What synsh answers itself, built from what is actually installed — so
     * this list tells you "no music player" instead of promising one. */
    synsh_intent_help(s);
    return 0;
}

/* ── alias / unalias ──────────────────────────────────────── */
static int alias_find(synsh_state_t *s, const char *name) {
    for (int i = 0; i < s->alias_count; i++)
        if (strcmp(s->alias_names[i], name) == 0) return i;
    return -1;
}

static void alias_print(synsh_state_t *s, int i) {
    printf("alias %s='%s'\n", s->alias_names[i], s->alias_values[i]);
}

static int builtin_alias(synsh_state_t *s, int argc, char **argv) {
    if (argc < 2) {
        for (int i = 0; i < s->alias_count; i++) alias_print(s, i);
        return 0;
    }

    int rc = 0;
    for (int a = 1; a < argc; a++) {
        char *eq = strchr(argv[a], '=');
        if (!eq) {
            /* No '=' → query an existing alias */
            int i = alias_find(s, argv[a]);
            if (i >= 0) {
                alias_print(s, i);
            } else {
                fprintf(stderr, "alias: %s: not found\n", argv[a]);
                rc = 1;
            }
            continue;
        }

        *eq = '\0';
        const char *name  = argv[a];
        const char *value = eq + 1;
        if (!*name) {
            fprintf(stderr, "alias: invalid alias name\n");
            rc = 1;
            continue;
        }

        int i = alias_find(s, name);
        if (i >= 0) {
            /* Redefine in place */
            free(s->alias_values[i]);
            s->alias_values[i] = strdup(value);
        } else if (s->alias_count >= 128) {
            fprintf(stderr, "alias: table full (max 128)\n");
            rc = 1;
        } else {
            s->alias_names[s->alias_count]  = strdup(name);
            s->alias_values[s->alias_count] = strdup(value);
            s->alias_count++;
        }
    }
    return rc;
}

static int builtin_unalias(synsh_state_t *s, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "unalias: usage: unalias name [name ...]\n");
        return 1;
    }

    int rc = 0;
    for (int a = 1; a < argc; a++) {
        int i = alias_find(s, argv[a]);
        if (i < 0) {
            fprintf(stderr, "unalias: %s: not found\n", argv[a]);
            rc = 1;
            continue;
        }
        free(s->alias_names[i]);
        free(s->alias_values[i]);
        /* Compact the table so lookups stay contiguous */
        for (int j = i; j < s->alias_count - 1; j++) {
            s->alias_names[j]  = s->alias_names[j + 1];
            s->alias_values[j] = s->alias_values[j + 1];
        }
        s->alias_count--;
        s->alias_names[s->alias_count]  = NULL;
        s->alias_values[s->alias_count] = NULL;
    }
    return rc;
}

/* ── Dispatch table ───────────────────────────────────────── */
typedef struct { const char *name; int (*fn)(synsh_state_t *, int, char **); } builtin_t;

static builtin_t BUILTIN_TABLE[] = {
    {"cd",     builtin_cd},
    {"alias",   builtin_alias},
    {"unalias", builtin_unalias},
    {"export", builtin_export},
    {"jobs",   builtin_jobs},
    {"syn",    builtin_syn},
    {"help",   builtin_help},
    {"exit",   NULL},
    {"quit",   NULL},
    {NULL,     NULL}
};

bool synsh_is_builtin(const char *cmd) {
    for (int i = 0; BUILTIN_TABLE[i].name; i++)
        if (strcmp(cmd, BUILTIN_TABLE[i].name) == 0) return true;
    return false;
}

int synsh_builtin(synsh_state_t *s, int argc, char **argv) {
    if (!argc || !argv[0]) return 1;

    if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        int code = argc > 1 ? atoi(argv[1]) : s->last_exit_code;
        synsh_history_save(s);
        synsh_ai_disconnect(s);
        exit(code);
    }

    for (int i = 0; BUILTIN_TABLE[i].name; i++) {
        if (strcmp(argv[0], BUILTIN_TABLE[i].name) == 0 && BUILTIN_TABLE[i].fn)
            return BUILTIN_TABLE[i].fn(s, argc, argv);
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════ */
/*  history.c — Semantic shell history                         */
/* ═══════════════════════════════════════════════════════════ */

