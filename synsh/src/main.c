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
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <getopt.h>
#include <termios.h>
#include <pwd.h>

#include "synsh.h"
#include "ipc.h"
#include "classify.h"
#include "intents.h"
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

/* --intent-check's "not an intent" answer. Not 1: an intent that matched and
 * then failed also exits nonzero, and a caller choosing between synsh and the
 * model must be able to tell "not mine" from "mine, and it went wrong". 70 is
 * outside both the shell's 1-2 and its 126-128 exec range. */
#define SYNSH_NO_INTENT 70

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS] [script]\n"
        "\n"
        "SynapseOS Natural Language Shell\n"
        "\n"
        "Options:\n"
        "  -c CMD         Execute CMD and exit\n"
        "  -i             Force interactive mode\n"
        "  --intent       Let -c answer plain-English intents, as the prompt does\n"
        "  --intent-check LINE\n"
        "                 Exit 0 if LINE is an intent synsh answers itself,\n"
        "                 %d if it is not. Runs nothing, prints nothing.\n"
        "  --toolinfo     Print the tools resolved from $PATH (for AI prompts)\n"
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
        prog, SYNSH_NO_INTENT
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
        synsh_history_add(s, line);

        /* The requests synsh answers exactly (intents.c), before classify.
         *
         * It has to be before: `play` is a real program — sox ships it, and
         * chibi pulls sox in — so classify sees a known command and routes
         * "play music" to the shell, where sox fails on a file called "music".
         * intents.c only claims whole lines that are nothing but the phrase, so
         * `play music.wav` still reaches sox untouched.
         *
         * Also before the synapd check: being told the time should not cost a
         * model round-trip, and should still work with the daemon down. */
        {
            int ic = 0;
            if (synsh_intent(s, line, &ic, false)) {
                s->last_exit = ic;
                free(line);
                continue;
            }
        }

        /* Classify and dispatch */
        input_class_t cls = classify_input(line);

        switch (cls) {
        case INPUT_BUILTIN:
        case INPUT_SHELL:
            /* Both go through the command-line layer: it splits on
             * ; && || and dispatches built-ins per segment, so a line
             * like `cd /etc && ls` works in either class. */
            s->last_exit = execute_command_line(s, line);
            break;

        case INPUT_AI: {
            /*
             * Natural language input.
             * Ask synapd to translate to a shell command,
             * optionally confirm, then execute.
             */

            /* The startup connect races synapd's boot — retry here so a
             * shell opened before the daemon was up heals on first use. */
            if (!s->synapd_connected && synapd_connect(s) == 0) {
                fprintf(stderr, "%ssynsh: connected to synapd — AI online\n%s",
                        COLOR_OK, COLOR_RESET);
            }
            if (!s->synapd_connected) {
                fprintf(stderr,
                    "%ssynsh: synapd not connected — running in shell-only mode\n%s",
                    COLOR_WARN, COLOR_RESET);
                s->last_exit = execute_command_line(s, line);
                break;
            }

            char cmd_buf[SYNSH_MAX_LINE]     = {0};
            char explain_buf[SYNSH_MAX_LINE] = {0};

            int r = ai_translate(s, line, cmd_buf, sizeof(cmd_buf),
                                 explain_buf, sizeof(explain_buf));
            if (r < 0) {
                fprintf(stderr, "%ssynsh: AI translation failed\n%s",
                        COLOR_ERR, COLOR_RESET);
                break;
            }

            /* Show and optionally confirm */
            s->last_exit = execute_ai_suggestion(s, cmd_buf, explain_buf);
            break;
        }

        case INPUT_HYBRID:
            /* Hybrid: try as shell, fall back to AI if it fails */
            s->last_exit = execute_command_line(s, line);
            if (s->last_exit != 0 && s->synapd_connected) {
                printf("%s  ↯ command failed, asking AI...\n%s",
                       COLOR_AI, COLOR_RESET);

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

        if (cls == INPUT_AI && s->synapd_connected) {
            char cmd_buf[SYNSH_MAX_LINE]     = {0};
            char explain_buf[SYNSH_MAX_LINE] = {0};
            if (ai_translate(s, line, cmd_buf, sizeof(cmd_buf),
                             explain_buf, sizeof(explain_buf)) == 0) {
                exit_code = execute_command_line(s, cmd_buf);
            }
        } else {
            exit_code = execute_command_line(s, line);
        }
    }

    fclose(f);
    return exit_code;
}

/* ── Entry point ──────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int force_interactive = 0;
    int no_ai = 0;
    int no_color = 0;
    int cmd_intents = 0;
    char *intent_check = NULL;
    char *cmd_string = NULL;
    char *script_path = NULL;

    static struct option long_opts[] = {
        {"no-ai",       no_argument,       0, 0},
        {"no-confirm",  no_argument,       0, 0},
        {"no-color",    no_argument,       0, 0},
        {"intent",      no_argument,       0, 0},
        {"intent-check", required_argument, 0, 0},
        {"toolinfo",    no_argument,       0, 0},
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
                no_color = 1;  /* applied after the rc load, so it wins */
            else if (strcmp(long_opts[longidx].name, "intent") == 0)
                cmd_intents = 1;
            else if (strcmp(long_opts[longidx].name, "intent-check") == 0)
                intent_check = optarg;
            else if (strcmp(long_opts[longidx].name, "toolinfo") == 0) {
                /* Before init: this resolves $PATH and nothing else, and the
                 * caller (synui's command bar, building its prompt) is on an
                 * event loop. Same contract as --intent-check. */
                puts(synsh_intent_toolinfo());
                return 0;
            }
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

    /*
     * --intent-check: answer and get out, before anything with a cost.
     *
     * Deliberately above synapd_connect(), the rc load and history: the caller
     * is synui's command bar, which runs this synchronously on its event loop
     * to choose between synsh and the model, and a compositor that blocks is a
     * frozen desktop. Matching needs the normalise-and-compare table and $PATH,
     * nothing else — so it must not pay for a socket to a daemon it will never
     * speak to. (synapd_connect() to a dead daemon is the exact stall this
     * would have introduced.)
     */
    if (intent_check) {
        int ignored = 0;
        return synsh_intent(&g_state, intent_check, &ignored, true)
               ? 0 : SYNSH_NO_INTENT;
    }

    /* Determine mode */
    g_state.interactive = force_interactive ||
                          (cmd_string == NULL && script_path == NULL && isatty(STDIN_FILENO));

    setup_signals();

    /* Load config */
    synsh_load_rc(&g_state);

    /* Colour precedence, lowest to highest: auto-detect (tty/NO_COLOR/TERM),
     * then the rc file, then the command line. --no-color is applied last so
     * that it always wins — the rc files ship `set color on`, which used to
     * silently override it. Once resolved, hand the answer to the palette;
     * from here on nothing needs to check g_state.color to print safely. */
    if (no_color) g_state.color = 0;
    synsh_color_init(g_state.color);

    /* Connect to synapd */
    if (!no_ai) {
        if (synapd_connect(&g_state) == 0) {
            if (g_state.verbose)
                printf("%ssynsh: connected to synapd\n%s", COLOR_OK, COLOR_RESET);
        } else {
            fprintf(stderr,
                "%ssynsh: warning — synapd not available, AI features disabled\n%s",
                COLOR_WARN, COLOR_RESET);
        }
    }

    /* Load history */
    if (g_state.interactive)
        synsh_history_load(&g_state);

    /* Print banner in interactive mode */
    if (g_state.interactive) {
        /* The box interior is BANNER_W columns wide; every row pads to it.
         * The border rows are drawn rather than spelled out so they cannot
         * drift out of step with the text rows again. */
        enum { BANNER_W = 37 };
        char rule[BANNER_W * 3 + 1];  /* ─ is 3 bytes in UTF-8 */
        char *p = rule;
        for (int i = 0; i < BANNER_W; i++) p = stpcpy(p, "─");

        printf("%s"
            "  ╭%s╮\n"
            "  │  SynapseOS  ·  synsh %s%*s│\n"
            "  │  Where the kernel thinks%*s│\n"
            "  ╰%s╯\n"
            "%s\n",
            COLOR_BRAND,
            rule,
            SYNSH_VERSION, (int)(BANNER_W - 22 - strlen(SYNSH_VERSION)), "",
            (int)(BANNER_W - 25), "",
            rule,
            COLOR_RESET
        );
        if (g_state.synapd_connected)
            printf("%s  ⚡ AI online%s — type naturally or use shell commands\n\n",
                   COLOR_AI, COLOR_RESET);
        else
            printf("%s  ⚠  AI offline%s — shell-only mode\n\n",
                   COLOR_WARN, COLOR_RESET);
    }

    int exit_code = 0;

    if (cmd_string) {
        /* -c mode: run single command string.
         *
         * Intents only when asked for. -c is the interface scripts use, and
         * there they mean shell: the tables claim bare words like `date`, `df`
         * and `uptime`, so answering them here would quietly turn `synsh -c
         * date` from the date(1) everyone expects into prose. Interactive input
         * is a person talking and gets them unconditionally; -c is a program
         * talking and has to say --intent to mean it. synui's command bar does. */
        int ic = 0;
        if (cmd_intents && synsh_intent(&g_state, cmd_string, &ic, false))
            exit_code = ic;
        else
            exit_code = execute_command_line(&g_state, cmd_string);
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
            exit_code = execute_command_line(&g_state, line);
        }
    }

    /* Cleanup */
    if (g_state.interactive)
        synsh_history_save(&g_state);

    synapd_disconnect(&g_state);
    synsh_destroy(&g_state);

    return exit_code;
}
