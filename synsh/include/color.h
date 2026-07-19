/*
 * color.h — ANSI terminal colors for synsh
 *
 * These are runtime pointers, not string literals: synsh_color_init()
 * points them either at real escape sequences or at "" (empty strings).
 * That way a single switch disables colour everywhere, and callers never
 * have to guard their own output. Because they are not literals they
 * cannot be concatenated at compile time — pass them as %s arguments.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

/* SynapseOS brand colors — cyan/electric theme (256-color) */
extern const char *COLOR_BRAND;   /* electric cyan */
extern const char *COLOR_AI;      /* sky blue — AI output */
extern const char *COLOR_CMD;     /* warm gold — commands */
extern const char *COLOR_OK;      /* green */
extern const char *COLOR_WARN;    /* amber */
extern const char *COLOR_ERR;     /* red */
extern const char *COLOR_DIM;     /* dimmed */
extern const char *COLOR_BOLD;
extern const char *COLOR_USER;    /* light cyan */
extern const char *COLOR_PATH;    /* lavender */
extern const char *COLOR_PROMPT;  /* near-white */
extern const char *COLOR_RESET;

/* Basic 8-color palette — used by the builtins (help, version, status) */
extern const char *COL_RESET;
extern const char *COL_BOLD;
extern const char *COL_RED;
extern const char *COL_GREEN;
extern const char *COL_YELLOW;
extern const char *COL_BLUE;
extern const char *COL_CYAN;
extern const char *COL_BRED;
extern const char *COL_BGREEN;
extern const char *COL_BYELLOW;
extern const char *COL_BBLUE;
extern const char *COL_BCYAN;

/*
 * Point the palette at real escapes (enable != 0) or at "" (enable == 0).
 * Call once, after the colour setting has been resolved from the
 * tty check, the rc file and the command line.
 */
void synsh_color_init(int enable);

/*
 * Decide whether colour should be on for this stdout, honouring the
 * NO_COLOR convention (https://no-color.org) and TERM=dumb.
 * This is the auto-detected default; the rc file and --no-color
 * are layered on top of it.
 */
int synsh_color_supported(void);
