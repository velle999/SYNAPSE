/*
 * isolation_test.c — stale-pid identity guard
 *
 * Covers the check that stands between a DENY verdict and the wrong process.
 *
 * synguard reads events out of the kmod ring up to poll_interval_ms after the
 * syscall happened, then may spend an AI round-trip classifying them. A
 * short-lived process can exit inside that window and the kernel can hand its
 * pid to something else. Acting on the bare pid would then SIGSTOP and SIGKILL
 * a bystander AND ITS ENTIRE DESCENDANT SUBTREE. sg_is_protected() does not
 * help here: it only knows a handful of critical comms, so any ordinary
 * process is fair game.
 *
 * The two primitives under test:
 *
 *   sg_proc_starttime()  — field 22 of /proc/<pid>/stat, the kernel's own
 *                          tiebreaker for a recycled pid.
 *   sg_pid_identity_ok() — "is this pid still the process the event blamed?"
 *
 * Nothing here kills or stops anything: the guard is a pure predicate over
 * /proc, so it can be driven honestly against this test's own pid and against
 * a real forked child that the test reaps itself. That is deliberate — a test
 * for a kill path must not be able to kill the machine it runs on.
 *
 * The recycled-pid case cannot be staged directly (a test cannot make the
 * kernel reissue a chosen pid), so it is covered by its two halves: a live pid
 * whose comm no longer matches must be refused, and a dead pid must be
 * refused. Together those are what a recycled pid looks like from here.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "synguard.h"

static int failures;

static void ok(const char *name, int cond)
{
    printf("  %s - %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* This process's own comm, which is what /proc/self/comm reports. */
static void self_comm(char *out, size_t outlen)
{
    int fd = open("/proc/self/comm", O_RDONLY);
    if (fd < 0) { out[0] = '\0'; return; }
    ssize_t n = read(fd, out, outlen - 1);
    close(fd);
    if (n <= 0) { out[0] = '\0'; return; }
    out[n] = '\0';
    char *nl = strchr(out, '\n');
    if (nl) *nl = '\0';
}

/* A pid that is certainly not in use: fork a child, reap it, reuse its pid.
 * The kernel allocates pids sequentially and will not come back around to this
 * one for another ~pid_max forks, so within this test it stays dead. */
static pid_t reaped_pid(void)
{
    pid_t p = fork();
    if (p == 0) _exit(0);
    if (p < 0) return -1;
    int st;
    waitpid(p, &st, 0);
    return p;
}

int main(void)
{
    char comm[64];
    self_comm(comm, sizeof(comm));
    printf("isolation: stale-pid identity guard (self comm=\"%s\")\n", comm);

    /* ── sg_proc_starttime ──────────────────────────────────── */
    unsigned long long st1 = 0, st2 = 0;
    ok("starttime of self is readable",
       sg_proc_starttime(getpid(), &st1) == 0);
    ok("starttime of self is nonzero", st1 != 0);

    /* Same clock domain, same process, so it must be byte-identical on a
     * re-read. This is the property the teardown re-check depends on. */
    ok("starttime of self is stable across reads",
       sg_proc_starttime(getpid(), &st2) == 0 && st1 == st2);

    ok("starttime of pid 1 is readable",
       sg_proc_starttime(1, &st1) == 0);

    /* A process that has been reaped has no /proc entry at all. */
    pid_t dead = reaped_pid();
    ok("fork+reap produced a pid", dead > 0);
    ok("starttime of a reaped pid fails",
       sg_proc_starttime(dead, &st1) != 0);
    ok("starttime of a negative pid fails",
       sg_proc_starttime(-1, &st1) != 0);

    /* ── sg_pid_identity_ok ─────────────────────────────────── */
    ok("self with correct comm is accepted",
       sg_pid_identity_ok(getpid(), comm) == 1);

    /* The core refusal: right pid, wrong program. This is what a recycled pid
     * looks like — the pid is alive, but it is not who the event blamed. */
    ok("self with a mismatched comm is REFUSED",
       sg_pid_identity_ok(getpid(), "definitely-not-me") == 0);

    /* comm is bounded at SG_COMM_LEN-1 chars by the kernel, and the event
     * carries that same truncated form, so a long expectation must not
     * accidentally compare equal to the truncated live value. */
    ok("self with a longer-but-prefixed comm is REFUSED",
       sg_pid_identity_ok(getpid(), "isolation_test_and_then_some") == 0);

    /* A dead pid is refused whatever comm is claimed for it. */
    ok("a reaped pid is REFUSED with a comm",
       sg_pid_identity_ok(dead, comm) == 0);
    ok("a reaped pid is REFUSED with no comm",
       sg_pid_identity_ok(dead, NULL) == 0);

    /* pid 0 and negatives must never be treated as valid targets: kill(0,...)
     * would signal the whole process group and kill(-1,...) everything the
     * uid can reach. */
    ok("pid 0 is REFUSED", sg_pid_identity_ok(0, NULL) == 0);
    ok("a negative pid is REFUSED", sg_pid_identity_ok(-1, NULL) == 0);

    /* NULL/empty comm degrades to a liveness check rather than blanket-passing
     * a dead pid — callers with no recorded comm still get the dead-pid guard. */
    ok("self with NULL comm passes the liveness check",
       sg_pid_identity_ok(getpid(), NULL) == 1);
    ok("self with empty comm passes the liveness check",
       sg_pid_identity_ok(getpid(), "") == 1);

    /* pid 1 is alive but is not us — a mismatched comm must be refused even
     * for a pid that certainly exists. (sg_is_protected() would also refuse
     * pid 1; this asserts the identity guard stands on its own.) */
    ok("pid 1 with our comm is REFUSED",
       sg_pid_identity_ok(1, comm) == 0);

    /* ── A real live child ──────────────────────────────────── */
    /* A forked child shares our comm until it execs, so it must be accepted
     * under that comm and refused under any other — the guard keys on identity
     * the event recorded, not on "is some process here". */
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        pid_t kid = fork();
        if (kid == 0) {
            close(pipefd[1]);
            char c;
            read(pipefd[0], &c, 1);   /* block until the parent is done */
            _exit(0);
        }
        close(pipefd[0]);
        if (kid > 0) {
            ok("a live child is accepted under its comm",
               sg_pid_identity_ok(kid, comm) == 1);
            ok("a live child is REFUSED under a foreign comm",
               sg_pid_identity_ok(kid, "some-other-proc") == 0);

            unsigned long long kst = 0;
            ok("a live child has a readable starttime",
               sg_proc_starttime(kid, &kst) == 0 && kst != 0);

            close(pipefd[1]);         /* release it */
            int st;
            waitpid(kid, &st, 0);

            /* Now that it is reaped, the same pid must be refused — the
             * before/after pair on one pid is the whole point. */
            ok("that same pid is REFUSED once reaped",
               sg_pid_identity_ok(kid, comm) == 0);
        }
    }

    printf(failures ? "isolation: FAILED (%d)\n" : "isolation: all passed\n",
           failures);
    return failures ? 1 : 0;
}
