/*
 * synsh — SynapseOS Natural Language Shell
 *
 * A shell where every line is either:
 *   - A standard POSIX command  → executed directly
 *   - Natural language          → sent to synapd, translated to a command
 *   - A synsh built-in          → handled internally
 *
 * The AI layer is transparent: synsh shows you what it's going to run
 * before running it, so you stay in control.
 *
 * Usage:
 *   synsh              — interactive shell
 *   synsh -c 'cmd'     — run single command
 *   synsh script.syn   — run script file
 *   synsh --no-ai      — disable AI translation (pure shell mode)
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
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <getopt.h>
#include <termios.h>
#include <pwd.h>

#include "synsh.h"
#include "ipc.h"
#include "classify.h"
#include "exec.h"
#include "readline_synsh.h"
#include "builtins.h"
#include "color.h"

/* ── Global state ─────────────────────────────────────────── */
static synsh_state_t g_state;

/* ── Signal handling ──────────────────────────────────────── */
static void sigint_handler(int sig) {
    (void)sig;
    /* In interactive mode, SIGINT cancels the current line.
     * The readline layer handles echoing a newline. */
    write(STDOUT_FILENO, "\n", 1);
}

static void sigchld_handler(int sig) {
    (void)sig;
    /* Reap background children */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

static void setup_signals(void) {
    struct sigaction sa = {0};
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    /* Ignore job-control signals we don't handle yet */
    sa.sa_handler = SIG_IGN;
    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
}

/* ── Usage ────────────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS] [script]\n"
        "\n"
        "SynapseOS Natural Language Shell\n"
        "\n"
        "Options:\n"
        "  -c CMD         Execute CMD and exit\n"
        "  -i             Force interactive mode\n"
        "  --no-ai        Disable AI translation (pure shell mode)\n"
        "  --no-confirm   Run AI-suggested commands without confirmation\n"
        "  --no-color     Disable colored output\n"
        "  -v, --verbose  Verbose mode\n"
        "  --version      Print version\n"
        "  -h, --help     This help\n"
        "\n"
        "AI Translation:\n"
        "  Lines beginning with '?' are always sent to AI.\n"
        "  Lines that look like natural language are auto-detected.\n"
        "  Use '!' prefix to force shell mode for a line.\n"
        "\n"
        "Built-in commands:\n"
        "  syn ask <question>   Ask the AI a question (no command execution)\n"
        "  syn status           Show synapd connection status\n"
        "  syn explain          Explain the last command run\n"
        "  syn history          Show AI query history\n"
        "  cd, exit, help, ...  Standard builtins\n",
        prog
    );
}

/* ── Main REPL ────────────────────────────────────────────── */
static int run_interactive(synsh_state_t *s) {
    char *line;

    while (s->running) {
        /* Read a line */
        line = synsh_readline(s);
        if (!line) {
            /* EOF (Ctrl+D) */
            if (s->interactive)
                printf("\nexit\n");
            break;
        }

        /* Skip empty lines */
        if (*line == '\0' || *line == '\n') {
            free(line);
            continue;
        }

        /* Add to history */
        history_add(s, line);

        /* Classify and dispatch */
        input_class_t cls = classify_input(line);

        switch (cls) {
        case INPUT_BUILTIN:
            /* Parse and run built-in */
            execute_builtin_line(s, line);
            break;

        case INPUT_SHELL:
            /* Standard shell command — run directly */
            s->last_exit = execute_pipeline(s, line);
            break;

        case INPUT_AI: {
            /*
             * Natural language input.
             * Ask synapd to translate to a shell command,
             * optionally confirm, then execute.
             */
            if (!s->synapd_connected) {
                fprintf(stderr, COLOR_WARN
                    "synsh: synapd not connected — running in shell-only mode\n"
                    COLOR_RESET);
                s->last_exit = execute_pipeline(s, line);
                break;
            }

            char cmd_buf[SYNSH_MAX_LINE]     = {0};
            char explain_buf[SYNSH_MAX_LINE] = {0};

            int r = ai_translate(s, line, cmd_buf, sizeof(cmd_buf),
                                 explain_buf, sizeof(explain_buf));
            if (r < 0) {
                fprintf(stderr, COLOR_ERR
                    "synsh: AI translation failed\n" COLOR_RESET);
                break;
            }

            /* Show and optionally confirm */
            s->last_exit = execute_ai_suggestion(s, cmd_buf, explain_buf);
            break;
        }

        case INPUT_HYBRID:
            /* Hybrid: try as shell, fall back to AI if it fails */
            s->last_exit = execute_pipeline(s, line);
            if (s->last_exit != 0 && s->synapd_connected) {
                if (s->color)
                    printf(COLOR_AI "  ↯ command failed, asking AI...\n" COLOR_RESET);
                else
                    printf("  ↯ command failed, asking AI...\n");

                char cmd_buf[SYNSH_MAX_LINE]     = {0};
                char explain_buf[SYNSH_MAX_LINE] = {0};
                int r = ai_translate(s, line, cmd_buf, sizeof(cmd_buf),
                                     explain_buf, sizeof(explain_buf));
                if (r == 0)
                    s->last_exit = execute_ai_suggestion(s, cmd_buf, explain_buf);
            }
            break;
        }

        free(line);
    }

    return s->last_exit;
}

/* ── Script execution ─────────────────────────────────────── */
static int run_script(synsh_state_t *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "synsh: %s: %s\n", path, strerror(errno));
        return 1;
    }

    char line[SYNSH_MAX_LINE];
    int exit_code = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        input_class_t cls = classify_input(line);

        if (cls == INPUT_BUILTIN) {
            execute_builtin_line(s, line);
        } else if (cls == INPUT_AI && s->synapd_connected) {
            char cmd_buf[SYNSH_MAX_LINE]     = {0};
            char explain_buf[SYNSH_MAX_LINE] = {0};
            if (ai_translate(s, line, cmd_buf, sizeof(cmd_buf),
                             explain_buf, sizeof(explain_buf)) == 0) {
                exit_code = execute_pipeline(s, cmd_buf);
            }
        } else {
            exit_code = execute_pipeline(s, line);
        }
    }

    fclose(f);
    return exit_code;
}

/* ── Entry point ──────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int force_interactive = 0;
    int no_ai = 0;
    char *cmd_string = NULL;
    char *script_path = NULL;

    static struct option long_opts[] = {
        {"no-ai",      no_argument,       0, 0},
        {"no-confirm", no_argument,       0, 0},
        {"no-color",   no_argument,       0, 0},
        {"verbose",    no_argument,       0, 'v'},
        {"version",    no_argument,       0, 'V'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, longidx = 0;
    while ((opt = getopt_long(argc, argv, "c:ivVh", long_opts, &longidx)) != -1) {
        switch (opt) {
        case 0:
            if (strcmp(long_opts[longidx].name, "no-ai") == 0)
                no_ai = 1;
            else if (strcmp(long_opts[longidx].name, "no-confirm") == 0)
                g_state.ai_confirm = 0;
            else if (strcmp(long_opts[longidx].name, "no-color") == 0)
                g_state.color = 0;
            break;
        case 'c': cmd_string = optarg; break;
        case 'i': force_interactive = 1; break;
        case 'v': g_state.verbose = 1; break;
        case 'V':
            printf("synsh %s (SynapseOS natural language shell)\n", SYNSH_VERSION);
            return 0;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (optind < argc)
        script_path = argv[optind];

    /* Initialize shell state */
    if (synsh_init(&g_state, argc, argv) < 0) {
        fprintf(stderr, "synsh: initialization failed\n");
        return 1;
    }

    if (no_ai) g_state.synapd_connected = 0;

    /* Determine mode */
    g_state.interactive = force_interactive ||
                          (cmd_string == NULL && script_path == NULL && isatty(STDIN_FILENO));

    setup_signals();

    /* Load config */
    synsh_load_rc(&g_state);

    /* Connect to synapd */
    if (!no_ai) {
        if (synapd_connect(&g_state) == 0) {
            if (g_state.verbose)
                printf(COLOR_OK "synsh: connected to synapd\n" COLOR_RESET);
        } else {
            fprintf(stderr, COLOR_WARN
                "synsh: warning — synapd not available, AI features disabled\n"
                COLOR_RESET);
        }
    }

    /* Load history */
    if (g_state.interactive)
        history_load(&g_state);

    /* Print banner in interactive mode */
    if (g_state.interactive) {
        printf(COLOR_BRAND
            "  ╭─────────────────────────────────────╮\n"
            "  │  SynapseOS  ·  synsh %s%*s│\n"
            "  │  Where the kernel thinks             │\n"
            "  ╰─────────────────────────────────────╯\n"
            COLOR_RESET "\n",
            SYNSH_VERSION,
            (int)(14 - strlen(SYNSH_VERSION)), ""
        );
        if (g_state.synapd_connected)
            printf(COLOR_AI "  ⚡ AI online" COLOR_RESET " — type naturally or use shell commands\n\n");
        else
            printf(COLOR_WARN "  ⚠  AI offline" COLOR_RESET " — shell-only mode\n\n");
    }

    int exit_code = 0;

    if (cmd_string) {
        /* -c mode: run single command string */
        exit_code = execute_pipeline(&g_state, cmd_string);
    } else if (script_path) {
        /* Script mode */
        exit_code = run_script(&g_state, script_path);
    } else if (g_state.interactive) {
        /* Interactive REPL */
        exit_code = run_interactive(&g_state);
    } else {
        /* stdin pipeline mode */
        char line[SYNSH_MAX_LINE];
        while (fgets(line, sizeof(line), stdin)) {
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            if (line[0] == '#' || line[0] == '\0') continue;
            exit_code = execute_pipeline(&g_state, line);
        }
    }

    /* Cleanup */
    if (g_state.interactive)
        history_save(&g_state);

    synapd_disconnect(&g_state);
    synsh_destroy(&g_state);

    return exit_code;
}
