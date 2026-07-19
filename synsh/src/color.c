/*
 * color.c — runtime ANSI palette
 *
 * The palette starts out enabled; main() calls synsh_color_init() once the
 * real setting is known. Disabling it swaps every pointer for "", so all the
 * ordinary printf("%s...") call sites silently stop emitting escapes — no
 * per-site `if (color)` guards, and nothing leaks into a pipe or a log file.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "color.h"

/* The real escape sequences. */
#define E_BRAND   "\033[38;5;51m"   /* electric cyan */
#define E_AI      "\033[38;5;81m"   /* sky blue */
#define E_CMD     "\033[38;5;222m"  /* warm gold */
#define E_OK      "\033[38;5;82m"   /* green */
#define E_WARN    "\033[38;5;214m"  /* amber */
#define E_ERR     "\033[38;5;196m"  /* red */
#define E_DIM     "\033[2m"
#define E_BOLD    "\033[1m"
#define E_USER    "\033[38;5;159m"  /* light cyan */
#define E_PATH    "\033[38;5;147m"  /* lavender */
#define E_PROMPT  "\033[38;5;255m"  /* near-white */
#define E_RESET   "\033[0m"

const char *COLOR_BRAND  = E_BRAND;
const char *COLOR_AI     = E_AI;
const char *COLOR_CMD    = E_CMD;
const char *COLOR_OK     = E_OK;
const char *COLOR_WARN   = E_WARN;
const char *COLOR_ERR    = E_ERR;
const char *COLOR_DIM    = E_DIM;
const char *COLOR_BOLD   = E_BOLD;
const char *COLOR_USER   = E_USER;
const char *COLOR_PATH   = E_PATH;
const char *COLOR_PROMPT = E_PROMPT;
const char *COLOR_RESET  = E_RESET;

const char *COL_RESET   = "\033[0m";
const char *COL_BOLD    = "\033[1m";
const char *COL_RED     = "\033[31m";
const char *COL_GREEN   = "\033[32m";
const char *COL_YELLOW  = "\033[33m";
const char *COL_BLUE    = "\033[34m";
const char *COL_CYAN    = "\033[36m";
const char *COL_BRED    = "\033[1;31m";
const char *COL_BGREEN  = "\033[1;32m";
const char *COL_BYELLOW = "\033[1;33m";
const char *COL_BBLUE   = "\033[1;34m";
const char *COL_BCYAN   = "\033[1;36m";

int synsh_color_supported(void) {
    /* https://no-color.org — any non-empty value means "no colour". */
    const char *nc = getenv("NO_COLOR");
    if (nc && *nc) return 0;

    const char *term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) return 0;

    return isatty(STDOUT_FILENO) ? 1 : 0;
}

void synsh_color_init(int enable) {
    if (enable) return;  /* pointers already hold the escape sequences */

    COLOR_BRAND = COLOR_AI = COLOR_CMD = COLOR_OK = COLOR_WARN =
        COLOR_ERR = COLOR_DIM = COLOR_BOLD = COLOR_USER = COLOR_PATH =
        COLOR_PROMPT = COLOR_RESET = "";

    COL_RESET = COL_BOLD = COL_RED = COL_GREEN = COL_YELLOW = COL_BLUE =
        COL_CYAN = COL_BRED = COL_BGREEN = COL_BYELLOW = COL_BBLUE =
        COL_BCYAN = "";
}
