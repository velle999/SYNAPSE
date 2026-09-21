/*
 * cmdplan.h — what the command bar may do with a model's answer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stddef.h>

typedef enum {
    CMDPLAN_NONE,       /* not a command: show the answer as text */
    CMDPLAN_LAUNCH,     /* a bare installed application: start it as-is */
    CMDPLAN_CONFINED,   /* anything else: a shell fragment, run sandboxed */
} cmdplan_kind_t;

/* One installed application, as the app grid knows it. */
typedef struct {
    const char *exec;   /* Exec= with field codes stripped */
    int terminal;       /* Terminal=true */
} cmdplan_app_t;

/*
 * Decide what `response` asks for. Only an answer whose first non-blank text
 * is "CMD:" is a command at all. The command (for CONFINED) or program (for
 * LAUNCH) is written to `out`, one line, trimmed.
 */
cmdplan_kind_t cmdplan_decide(const char *response,
                              const cmdplan_app_t *apps, int n_apps,
                              char *out, size_t out_len);

/*
 * The syn-confine argv for a CONFINED command: the user's files read-only,
 * /tmp writable, the network allowed, the command under /bin/sh. `argv` needs
 * CMDPLAN_ARGV_MAX slots; returns the count, NULL-terminated.
 */
#define CMDPLAN_ARGV_MAX 12
int cmdplan_confine_argv(const char *cmd, const char *home,
                         const char **argv);
