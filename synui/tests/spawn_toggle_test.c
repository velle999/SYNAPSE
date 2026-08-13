/*
 * spawn_toggle_test.c — the key that opens a launcher has to close it
 *
 * `spawn` is fire-and-forget: the second press of the terminal bind gives you a
 * second terminal, which is right. A launcher is the other case, and rofi on a
 * plain spawn was the odd one out — every panel bind in synui toggles, that one
 * left you with the launcher up and Escape as the only way out.
 *
 * Driven with REAL children, because the two things worth checking are both
 * about a live process and neither can be seen by reading the code:
 *
 *   1. The second call kills instead of spawning again.
 *   2. A slot whose child has DIED spawns rather than killing. This is the
 *      dangerous one: pids are recycled, and a toggle that trusted a stale pid
 *      would send SIGTERM to whatever inherited it — a session that dies weeks
 *      later for no visible reason. The guard is the process start time from
 *      /proc/<pid>/stat, and only a real, really-dead child exercises it.
 *
 * Every child here is `sleep`, spawned by this test, in its own session. It
 * signals nothing it did not start.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "synui.h"

/* The real one resets every signal disposition in the child. Nothing here cares
 * what a `sleep` inherits, and the test must not need a compositor to link. */
void synui_child_reset_signals(void) { }

/* Wait for a pid to become un-signalable, or give up. Children of this process
 * are reaped below, so "gone" means gone rather than "zombie". */
static int wait_gone(pid_t pid, int ms)
{
    for (int i = 0; i < ms; i++) {
        if (kill(pid, 0) != 0 && errno == ESRCH) return 1;
        int st;
        (void)waitpid(-1, &st, WNOHANG);   /* stand in for reap_children() */
        usleep(1000);
    }
    return 0;
}

static void cleanup(void);

/* Kill anything still running BEFORE aborting. assert() does not run atexit
 * handlers, and a survivor holds the stdout the meson harness is waiting on —
 * so a failed assertion here would be reported as a 30-second timeout with no
 * message rather than as the line that failed. */
#define FAILF(...) do { printf("    " __VA_ARGS__); cleanup(); assert(0); } while (0)

static int alive(pid_t pid)
{
    return pid > 0 && kill(pid, 0) == 0;
}

/* ── 1. Open, then close on the same command ───────────────── */
static void test_second_call_closes(void)
{
    const char *cmd = "sleep 30";

    synui_spawn_toggle(cmd);
    pid_t pid = synui_spawn_toggle_pid(cmd);
    assert(pid > 0);
    assert(alive(pid));

    /* Same command again: no new child, and the first one goes away. */
    synui_spawn_toggle(cmd);
    assert(synui_spawn_toggle_pid(cmd) == 0);
    if (!wait_gone(pid, 2000))
        FAILF("the child survived the second press (pid %d)\n", (int)pid);

    /* And a third press opens it again — the toggle has to be a cycle, not a
     * one-shot that leaves the key dead. */
    synui_spawn_toggle(cmd);
    pid_t again = synui_spawn_toggle_pid(cmd);
    assert(again > 0 && again != pid);
    assert(alive(again));
    kill(-again, SIGTERM);
    wait_gone(again, 2000);

    printf("  second press closes ...... ok\n");
}

/* ── 2. Two commands do not toggle each other ──────────────── */
static void test_commands_are_independent(void)
{
    const char *a = "sleep 31";
    const char *b = "sleep 32";

    synui_spawn_toggle(a);
    synui_spawn_toggle(b);
    pid_t pa = synui_spawn_toggle_pid(a);
    pid_t pb = synui_spawn_toggle_pid(b);
    assert(pa > 0 && pb > 0 && pa != pb);

    synui_spawn_toggle(a);                       /* closes a only */
    assert(synui_spawn_toggle_pid(a) == 0);
    assert(synui_spawn_toggle_pid(b) == pb);
    assert(alive(pb));

    synui_spawn_toggle(b);
    wait_gone(pa, 2000);
    wait_gone(pb, 2000);

    printf("  commands independent ..... ok\n");
}

/* ── 3. A child that died on its own is not "up" ───────────── */
static void test_dead_child_reopens(void)
{
    const char *cmd = "sleep 0.05";

    synui_spawn_toggle(cmd);
    pid_t pid = synui_spawn_toggle_pid(cmd);
    assert(pid > 0);

    if (!wait_gone(pid, 3000))
        FAILF("the short-lived child never exited\n");

    /* The slot still names that pid. The toggle must read it as gone — if it
     * does not, this call sends SIGTERM to a process group it no longer owns
     * and, worse, reports the launcher as closed when it was never open. */
    assert(synui_spawn_toggle_pid(cmd) == 0);

    synui_spawn_toggle(cmd);
    pid_t fresh = synui_spawn_toggle_pid(cmd);
    if (fresh <= 0)
        FAILF("a dead slot closed instead of opening\n");
    assert(fresh != pid);
    wait_gone(fresh, 3000);

    printf("  dead child reopens ....... ok\n");
}

/* ── 4. The close reaches past `sh` ─────────────────────────
 *
 * `sh -c` execs a simple command directly, so the pid usually IS the program.
 * A command line sh cannot exec away (a subshell) is the case where the pid is
 * the shell and the program is its child — which is why the kill goes to the
 * process GROUP. Without that, closing rofi would leave rofi.
 */
static void test_close_reaches_the_group(void)
{
    const char *cmd = "sleep 33 & wait";   /* sh stays, sleep is its child */

    synui_spawn_toggle(cmd);
    pid_t sh = synui_spawn_toggle_pid(cmd);
    assert(sh > 0);
    usleep(200 * 1000);   /* let sh get as far as forking the sleep */

    synui_spawn_toggle(cmd);
    assert(wait_gone(sh, 2000));

    /* Nothing of that group may be left. pgrep is not available everywhere, so
     * ask the kernel: signal 0 to the process group is ESRCH when it is empty. */
    if (kill(-sh, 0) == 0)
        FAILF("the process group outlived the close\n");

    printf("  close reaches the group .. ok\n");
}

/* Every command this test ever opens. A child that outlives the test inherits
 * its stdout, and the meson harness waits for that pipe to close — so a missed
 * kill anywhere above does not fail the assertion it belongs to, it hangs the
 * suite for the full timeout with no output to say why. Belt and braces. */
static const char *const all_cmds[] = {
    "sleep 30", "sleep 31", "sleep 32", "sleep 0.05", "sleep 33 & wait",
};

static void cleanup(void)
{
    for (unsigned i = 0; i < sizeof(all_cmds) / sizeof(all_cmds[0]); i++) {
        pid_t pid = synui_spawn_toggle_pid(all_cmds[i]);
        if (pid > 0) {
            printf("    note: %s outlived its test, killing pid %d\n",
                   all_cmds[i], (int)pid);
            kill(-pid, SIGKILL);
        }
    }
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

int main(void)
{
    printf("spawn toggle test\n");

    test_second_call_closes();
    test_commands_are_independent();
    test_dead_child_reopens();
    test_close_reaches_the_group();

    cleanup();
    printf("all spawn toggle tests passed\n");
    return 0;
}
