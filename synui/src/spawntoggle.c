/*
 * spawntoggle.c — spawning something the same key can put away again.
 *
 * Split out of input.c because it is the one piece of spawning that has to
 * remember anything, and because a table of live child processes is testable on
 * its own: tests/spawn_toggle_test.c drives it with real children, which is the
 * only way to check "the second press closes it" that does not just restate the
 * code.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "synui.h"

/* One fork, one session, one `sh -c`. The session is what makes the pid also a
 * process-GROUP id, which is what lets the toggle below close a command that
 * did not end up being the process sh exec'd. */
pid_t synui_spawn_pid(const char *cmd)
{
    /* ⚠ SAY SO. This returning -1 in silence is how a keybind comes to do
     * nothing at all: the caller built an empty command, nothing forked,
     * nothing was logged, and `synctl dispatch` answered {"ok":true} over the
     * top of it. An empty command here is always a bug in the caller — there is
     * no legitimate "spawn nothing" — so it belongs in the journal. */
    if (!cmd || !*cmd) {
        wlr_log(WLR_ERROR, "synui: spawn refused an empty command — "
                           "whatever built it produced nothing to run");
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        synui_child_reset_signals();
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(1);
    }
    /* Set the group from BOTH sides, which is the standard fix for a race that
     * is otherwise invisible until the machine is busy: between fork() and the
     * child reaching setsid(), the child is still in synui's process group, so
     * a kill(-pid) aimed at it in that window finds no such group, kills
     * nothing, and returns ESRCH. The toggle would then report the launcher
     * closed while it was still on screen. Whichever side gets there first
     * wins and both agree on the value; EACCES here just means the child has
     * already exec'd, which is to say it has already done it itself. */
    if (pid > 0) setpgid(pid, pid);
    return pid;
}

/* ── spawn_toggle: a launcher its own key can put away ────────
 *
 * `spawn` is fire-and-forget, which is right for a terminal — the second press
 * of the terminal bind is meant to give you a second terminal. It is wrong for
 * a launcher: rofi bound to a tap opened a SECOND rofi behind the first, and
 * the only way out was Escape. The built-in start menu has always toggled (the
 * tap runs `menu toggle`), so a tap pointed at rofi should behave the same way.
 *
 * The child gets its own process group (both sides set it — see above), so one
 * kill(-pid) takes down `sh -c` and whatever it ran, in the case where sh did
 * not exec the command directly.
 *
 * Liveness is (pid, start time) and not pid alone. SIGCHLD carries
 * reap_children() (synui_main.c), so a dead child is reaped within moments and
 * its pid is free to be handed to something else — at which point a bare
 * kill(-pid) is a SIGTERM to an unrelated process group, which is a session
 * that dies for no visible reason weeks later. /proc/<pid>/stat field 22 is
 * fixed at process creation and survives the exec, so it tells the two apart.
 */
#define SPAWN_TOGGLE_MAX 8

struct spawn_toggle_slot {
    char           cmd[SYN_BIND_ARG_LEN];
    pid_t          pid;
    unsigned long  started;   /* /proc/<pid>/stat field 22 */
};
static struct spawn_toggle_slot g_toggles[SPAWN_TOGGLE_MAX];

/* 0 when the process is gone, or is no longer the one we started. */
static unsigned long proc_start_time(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return 0;
    buf[n] = '\0';

    /* Field 2 is the executable name in parentheses and may contain both spaces
     * and parentheses, so the fields are counted from the LAST ')' — the one
     * piece of parsing /proc/stat actually requires. */
    char *p = strrchr(buf, ')');
    if (!p) return 0;
    p++;

    unsigned long start = 0;
    int field = 2;   /* p now sits just after field 2 */
    for (char *tok = strtok(p, " "); tok; tok = strtok(NULL, " ")) {
        if (++field == 22) { start = strtoul(tok, NULL, 10); break; }
    }
    return start;
}

static int spawn_toggle_live(const struct spawn_toggle_slot *sl)
{
    return sl->pid > 0 && sl->started != 0 &&
           proc_start_time(sl->pid) == sl->started;
}

void synui_spawn_toggle(const char *cmd)
{
    if (!cmd || !*cmd) return;

    struct spawn_toggle_slot *slot = NULL, *free_slot = NULL;
    for (int i = 0; i < SPAWN_TOGGLE_MAX; i++) {
        if (strcmp(g_toggles[i].cmd, cmd) == 0) { slot = &g_toggles[i]; break; }
        if (!free_slot && !spawn_toggle_live(&g_toggles[i]))
            free_slot = &g_toggles[i];
    }

    if (slot && spawn_toggle_live(slot)) {
        /* SIGTERM, never SIGKILL: a launcher asked to go away should get to put
         * its own window down and let the compositor animate the unmap.
         *
         * The group first, so a command sh did not exec away goes down whole.
         * ESRCH means there is no group under that id — the pid is real (it was
         * just checked) but is not a group leader, which can only happen if
         * something moved it. Signal the process itself rather than deciding
         * the launcher is closed when nothing was signalled. */
        if (kill(-slot->pid, SIGTERM) != 0 && errno == ESRCH)
            kill(slot->pid, SIGTERM);
        slot->pid = 0;
        slot->started = 0;
        return;
    }

    if (!slot) slot = free_slot;
    if (!slot) {
        /* Eight live toggles at once means the table is not the mechanism the
         * user is reaching for. Launch it anyway — refusing to open a launcher
         * because a table is full is a worse answer than one that will not
         * toggle. */
        synui_spawn_pid(cmd);
        return;
    }

    pid_t pid = synui_spawn_pid(cmd);
    if (pid <= 0) return;
    snprintf(slot->cmd, sizeof(slot->cmd), "%s", cmd);
    slot->pid     = pid;
    slot->started = proc_start_time(pid);
}

/* The live pid for a command, or 0. Exists so the test can watch a real child
 * appear and go; a caller wanting "is this launcher up" has it too. */
pid_t synui_spawn_toggle_pid(const char *cmd)
{
    if (!cmd) return 0;
    for (int i = 0; i < SPAWN_TOGGLE_MAX; i++)
        if (strcmp(g_toggles[i].cmd, cmd) == 0 && spawn_toggle_live(&g_toggles[i]))
            return g_toggles[i].pid;
    return 0;
}
