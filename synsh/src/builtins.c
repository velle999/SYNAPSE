/*
 * builtins.c — synsh built-in commands
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "synsh.h"
#include "intents.h"
#include "i18n.h"

/* ── cd ───────────────────────────────────────────────────── */
static int builtin_cd(synsh_state_t *s, int argc, char **argv) {
    const char *dir;
    if (argc < 2 || strcmp(argv[1], "~") == 0) {
        dir = getenv("HOME");
        if (!dir) dir = "/";
    } else if (strcmp(argv[1], "-") == 0) {
        dir = getenv("OLDPWD");
        if (!dir) { fprintf(stderr, "%s\n", T(M_CD_NO_OLDPWD)); return 1; }
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
    if (!j) { printf("%s\n", T(M_NO_JOBS)); return 0; }
    while (j) {
        /* ⚠ Translated HERE, at the draw site. The state itself is the enum
         * beside it; this is only the word printed for it. */
        const char *state = j->state == JOB_RUNNING ? T(M_JOB_RUNNING)
                          : j->state == JOB_STOPPED ? T(M_JOB_STOPPED)
                                                    : T(M_JOB_DONE);
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
            "  %ssyn model%s        — show loaded model info\n"
            "  %ssyn lang [CODE]%s  — show or change the language synsh speaks\n",
            COL_BCYAN, COL_RESET, SYNSH_VERSION,
            COL_BGREEN, COL_RESET,
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
            s->synapd_online ? T(M_STATUS_ONLINE) : T(M_STATUS_OFFLINE),
            COL_RESET);
        printf("%s: %s\n", T(M_LABEL_SOCKET), SYN_SOCKET_PATH);
        printf("%s: %s\n", T(M_LABEL_AI),
               s->ai_enabled ? T(M_STATUS_ENABLED) : T(M_STATUS_DISABLED));
        printf("%s: %s\n", T(M_LABEL_EXPLAIN),
               s->explain_mode ? T(M_STATUS_ON) : T(M_STATUS_OFF));
        printf("%s: %s\n", T(M_LABEL_SAFE),
               s->safe_mode ? T(M_STATUS_ON) : T(M_STATUS_OFF));
        printf("%s %s (%s)\n", T(M_LANG_IS),
               synsh_lang_name(synsh_lang()), synsh_lang_code(synsh_lang()));
        return 0;
    }

    /*
     * `syn lang` — what synsh is speaking, and how to change it for this
     * session. The durable answer is `set language` in ~/.synshrc; this is the
     * one that does not need a file, which is what somebody who has just been
     * greeted in a language they do not read is looking for.
     */
    if (strcmp(argv[1], "lang") == 0) {
        if (argc < 3) {
            printf("%s %s (%s)\n", T(M_LANG_IS),
                   synsh_lang_name(synsh_lang()), synsh_lang_code(synsh_lang()));
            /* i18n-english: language TAGS, not words — these are what you type */
            printf("  en de fr es pt it nl pl ru ja zh ko hi ar\n");
            return 0;
        }
        if (synsh_lang_from_string(argv[2]) == LANG_COUNT) {
            fprintf(stderr, "syn: %s %s\n", T(M_LANG_UNKNOWN),
                    "en de fr es pt it nl pl ru ja zh ko hi ar");
            return 1;
        }
        synsh_i18n_init(argv[2]);
        printf("%s %s (%s)\n", T(M_LANG_IS),
               synsh_lang_name(synsh_lang()), synsh_lang_code(synsh_lang()));
        return 0;
    }

    if (strcmp(argv[1], "ai") == 0 && argc >= 3) {
        s->ai_enabled = strcmp(argv[2], "on") == 0;
        /* ⚠ THE SAME TWO WORDS `syn status` ALREADY TRANSLATES, three lines
         * up, and they were bare literals here. A word in a %s slot is a word
         * no catalog can reach. */
        printf("%s %s\n", T(M_SET_AI),
               s->ai_enabled ? T(M_STATUS_ENABLED) : T(M_STATUS_DISABLED));
        return 0;
    }

    if (strcmp(argv[1], "explain") == 0 && argc >= 3) {
        s->explain_mode = strcmp(argv[2], "on") == 0;
        printf("%s %s\n", T(M_SET_EXPLAIN),
               s->explain_mode ? T(M_STATUS_ON) : T(M_STATUS_OFF));
        return 0;
    }

    if (strcmp(argv[1], "safe") == 0 && argc >= 3) {
        s->safe_mode = strcmp(argv[2], "on") == 0;
        printf("%s %s\n", T(M_SET_SAFE),
               s->safe_mode ? T(M_STATUS_ON) : T(M_STATUS_OFF));
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0) {
        printf("%-28s %lu\n", T(M_STAT_COMMANDS), s->commands_run);
        printf("%-28s %lu\n", T(M_STAT_NL),       s->nl_queries);
        printf("%-28s %lu\n", T(M_STAT_ASSISTS),  s->ai_assists);
        return 0;
    }

    if (strcmp(argv[1], "context") == 0) {
        /* Request context dump from synapd */
        if (!s->synapd_online) {
            fprintf(stderr, "%s\n", T(M_SYNAPD_OFFLINE));
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

    fprintf(stderr, T(M_UNKNOWN_SUBCMD), argv[1]);
    return 1;
}

/* ── help ─────────────────────────────────────────────────── */
static int builtin_help(synsh_state_t *s, int argc, char **argv) {
    (void)s; (void)argc; (void)argv;
    /* ⚠ THE BUILT-IN LIST NAMED fg AND bg, WHICH DO NOT EXIST — they are not
     * in BUILTIN_TABLE and never were, so `fg` reported "command not found"
     * from a shell whose own help had just promised it. A help screen is a
     * claim about the program; this one now lists what is actually there. */
    printf(
        "\n%ssynsh — SynapseOS Natural Language Shell%s\n\n"
        "%s\n\n"
        "  %s$ ls -la%s                        — %s\n"
        "  %s$ show me disk usage%s             — %s\n"
        "  %s$ what's using port 8080?%s        — %s\n"
        "  %s$ wie viel speicher ist frei%s     — %s\n\n"
        "%s\n\n"
        "%s cd, alias, unalias, export, jobs, syn, help, exit\n"
        "%s      syn status | syn ai on/off | syn safe on/off | syn lang\n\n",
        COL_BCYAN, COL_RESET,
        T(M_HELP_HEADLINE),
        COL_BGREEN, COL_RESET, T(M_HELP_REGULAR),
        COL_BCYAN,  COL_RESET, T(M_HELP_NATURAL),
        COL_BCYAN,  COL_RESET, T(M_HELP_QUESTION),
        COL_BCYAN,  COL_RESET, T(M_HELP_NATURAL),
        T(M_HELP_PREFIX),
        T(M_HELP_BUILTINS),
        T(M_HELP_META)
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
                fprintf(stderr, T(M_ALIAS_NOT_FOUND), argv[a]);
                rc = 1;
            }
            continue;
        }

        *eq = '\0';
        const char *name  = argv[a];
        const char *value = eq + 1;
        if (!*name) {
            fputs(T(M_ALIAS_BAD_NAME), stderr);
            rc = 1;
            continue;
        }

        int i = alias_find(s, name);
        if (i >= 0) {
            /* Redefine in place */
            free(s->alias_values[i]);
            s->alias_values[i] = strdup(value);
        } else if (s->alias_count >= 128) {
            fputs(T(M_ALIAS_FULL), stderr);
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
        fputs(T(M_UNALIAS_USAGE), stderr);
        return 1;
    }

    int rc = 0;
    for (int a = 1; a < argc; a++) {
        int i = alias_find(s, argv[a]);
        if (i < 0) {
            fprintf(stderr, T(M_UNALIAS_NOT_FOUND), argv[a]);
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

