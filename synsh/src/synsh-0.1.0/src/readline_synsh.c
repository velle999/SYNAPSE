/*
 * readline_synsh.c — Line input, prompt rendering, history
 *
 * Wraps GNU readline if available, falls back to a simple
 * fgets-based implementation that still supports history
 * navigation and basic line editing via termios.
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
#include <termios.h>
#include <sys/ioctl.h>
#include <pwd.h>

#include "synsh.h"
#include "readline_synsh.h"
#include "color.h"

/* ── Prompt rendering ─────────────────────────────────────── */
/*
 * Default prompt: [user@synapse cwd]⚡ or [user@synapse cwd]$
 * The ⚡ shows when AI is connected; $ when in shell-only mode.
 */
void synsh_prompt(synsh_state_t *s, char *buf, size_t len) {
    char *cwd = s->cwd ? s->cwd : "?";
    const char *user = s->user ? s->user : "user";

    /* Shorten home directory to ~ */
    char short_cwd[256] = {0};
    if (s->home && strncmp(cwd, s->home, strlen(s->home)) == 0) {
        snprintf(short_cwd, sizeof(short_cwd), "~%s", cwd + strlen(s->home));
    } else {
        strncpy(short_cwd, cwd, sizeof(short_cwd) - 1);
    }

    /* Truncate long paths to last 2 components */
    char *last_slash = strrchr(short_cwd, '/');
    if (last_slash && last_slash > short_cwd + 1) {
        char *prev = last_slash - 1;
        while (prev > short_cwd && *prev != '/') prev--;
        if (prev > short_cwd && strlen(short_cwd) > 30) {
            memmove(short_cwd + 3, prev, strlen(prev) + 1);
            short_cwd[0] = '.'; short_cwd[1] = '.'; short_cwd[2] = '.';
        }
    }

    const char *sigil = s->synapd_connected ? "⚡" : "$";

    if (s->color) {
        snprintf(buf, len,
            COLOR_BRAND "[" COLOR_RESET
            COLOR_USER "%s" COLOR_RESET
            COLOR_DIM "@synapse " COLOR_RESET
            COLOR_PATH "%s" COLOR_RESET
            COLOR_BRAND "]" COLOR_RESET
            "%s " COLOR_RESET,
            user, short_cwd, sigil
        );
    } else {
        snprintf(buf, len, "[%s@synapse %s]%s ", user, short_cwd, sigil);
    }
}

/* ── History management ───────────────────────────────────── */
void synsh_history_add(synsh_state_t *s, const char *line) {
    if (!line || !*line) return;

    /* Don't add duplicates of the last entry */
    if (s->history_count > 0 &&
        strcmp(s->history[s->history_count - 1], line) == 0)
        return;

    if (s->history_count >= SYNSH_HISTORY_MAX) {
        /* Rotate: drop oldest */
        free(s->history[0]);
        memmove(s->history, s->history + 1,
                (s->history_count - 1) * sizeof(char *));
        s->history_count--;
    }

    s->history[s->history_count++] = strdup(line);
    s->history_pos = s->history_count;
}

void synsh_history_load(synsh_state_t *s) {
    if (!s->home) return;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", s->home, SYNSH_HISTORY_FILE);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[SYNSH_MAX_LINE];
    while (fgets(line, sizeof(line), f) && s->history_count < SYNSH_HISTORY_MAX) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (*line) {
            s->history[s->history_count++] = strdup(line);
        }
    }
    fclose(f);
    s->history_pos = s->history_count;
}

void synsh_history_save(synsh_state_t *s) {
    if (!s->home || s->history_count == 0) return;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", s->home, SYNSH_HISTORY_FILE);

    FILE *f = fopen(path, "w");
    if (!f) return;

    /* Save last 1000 entries */
    int start = s->history_count > 1000 ? s->history_count - 1000 : 0;
    for (int i = start; i < s->history_count; i++) {
        fprintf(f, "%s\n", s->history[i]);
    }
    fclose(f);
}

/* ── Simple readline implementation ──────────────────────── */
/*
 * We use GNU readline if available (linked at build time).
 * If not, this fallback handles:
 *   - Line input with backspace
 *   - Ctrl+C to cancel line
 *   - Up/Down arrow for history (ANSI escape sequences)
 *   - Ctrl+D for EOF
 */

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>

char *synsh_readline(synsh_state_t *s) {
    char prompt[SYNSH_PROMPT_MAX];
    synsh_prompt(s, prompt, sizeof(prompt));

    char *line = readline(prompt);
    if (!line) return NULL;  /* EOF */
    if (*line == '\0') {
        free(line);
        return strdup("");
    }
    return line;
}

#else  /* fallback implementation */

static struct termios saved_termios;
static int termios_saved = 0;

static void raw_mode_enter(void) {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &saved_termios);
    termios_saved = 1;
    struct termios raw = saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void raw_mode_exit(void) {
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

char *synsh_readline(synsh_state_t *s) {
    char prompt[SYNSH_PROMPT_MAX];
    synsh_prompt(s, prompt, sizeof(prompt));

    /* Non-interactive: use fgets */
    if (!s->interactive || !isatty(STDIN_FILENO)) {
        static char buf[SYNSH_MAX_LINE];
        if (!fgets(buf, sizeof(buf), stdin)) return NULL;
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return strdup(buf);
    }

    /* Interactive: print prompt and read with raw mode */
    fputs(prompt, stdout);
    fflush(stdout);

    char buf[SYNSH_MAX_LINE];
    int pos = 0;
    int hist_pos = s->history_count;

    raw_mode_enter();

    while (1) {
        unsigned char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0) {
            raw_mode_exit();
            if (pos == 0) return NULL;  /* EOF */
            buf[pos] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            return strdup(buf);
        }

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }

        if (c == 3) {  /* Ctrl+C */
            write(STDOUT_FILENO, "\n", 1);
            raw_mode_exit();
            return strdup("");
        }

        if (c == 4) {  /* Ctrl+D */
            write(STDOUT_FILENO, "\n", 1);
            raw_mode_exit();
            if (pos > 0) {
                buf[pos] = '\0';
                return strdup(buf);
            }
            return NULL;  /* EOF */
        }

        if (c == 127 || c == 8) {  /* Backspace */
            if (pos > 0) {
                pos--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }

        if (c == 27) {  /* Escape sequence */
            unsigned char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            if (seq[0] == '[') {
                if (seq[1] == 'A') {  /* Up arrow */
                    if (hist_pos > 0) {
                        /* Clear current line */
                        while (pos > 0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            pos--;
                        }
                        hist_pos--;
                        const char *h = s->history[hist_pos];
                        size_t hlen = strlen(h);
                        if (hlen >= SYNSH_MAX_LINE) hlen = SYNSH_MAX_LINE - 1;
                        memcpy(buf, h, hlen);
                        pos = hlen;
                        write(STDOUT_FILENO, h, hlen);
                    }
                } else if (seq[1] == 'B') {  /* Down arrow */
                    while (pos > 0) {
                        write(STDOUT_FILENO, "\b \b", 3);
                        pos--;
                    }
                    if (hist_pos < s->history_count - 1) {
                        hist_pos++;
                        const char *h = s->history[hist_pos];
                        size_t hlen = strlen(h);
                        if (hlen >= SYNSH_MAX_LINE) hlen = SYNSH_MAX_LINE - 1;
                        memcpy(buf, h, hlen);
                        pos = hlen;
                        write(STDOUT_FILENO, h, hlen);
                    } else {
                        hist_pos = s->history_count;
                        pos = 0;
                    }
                }
            }
            continue;
        }

        if (c >= 32 && c < 127 && pos < SYNSH_MAX_LINE - 1) {
            buf[pos++] = c;
            write(STDOUT_FILENO, &c, 1);
        }
    }

    raw_mode_exit();
    buf[pos] = '\0';
    return strdup(buf);
}
#endif  /* HAVE_READLINE */
