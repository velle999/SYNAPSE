/*
 * isolation.c — Process isolation primitives for ENFORCE mode
 *
 * action_deny()/action_quarantine() must never act on a single PID in
 * isolation: a malicious process may have already forked, and a careless
 * SIGKILL can take down the whole system. This module provides the primitives
 * that make ENFORCE mode safe to actually turn on:
 *
 *   sg_is_protected()  — the hard guard. Refuses to ever touch PID 0/1,
 *                        synguard itself, our own process group, kernel
 *                        threads, or the core SynapseOS daemons. This is
 *                        the last line of defence and cannot be disabled.
 *
 *   sg_pid_identity_ok() — the stale-pid guard. A verdict is acted on up to
 *                        poll_interval_ms (plus any AI round-trip) after the
 *                        syscall that earned it, and a short-lived process can
 *                        exit and have its pid reissued inside that window.
 *                        Refuses when the pid is no longer the comm the event
 *                        recorded, so a bystander never inherits a death
 *                        sentence along with a pid. sg_is_protected() is no
 *                        help here — it only knows a few critical comms.
 *
 *   sg_kill_tree()     — terminate a process *and its descendants*, so a
 *                        process that forked before we reacted does not
 *                        survive through its children. Verifies the target
 *                        actually died.
 *
 *   sg_freeze_tree()   — quarantine via cgroup v2 freeze: SIGSTOP the
 *                        subtree, move it into a dedicated cgroup, and set
 *                        cgroup.freeze=1 so it stays frozen and cannot fork
 *                        its way out. Falls back to SIGSTOP alone if cgroup
 *                        v2 is unavailable.
 *
 * Everything here is best-effort userspace enforcement: we are not an LSM
 * and cannot win a TOCTOU race against the kernel. The goal is to make the
 * common case safe and the dangerous case impossible.
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
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "synguard.h"
#include "sg_log.h"

#define SG_CGROUP_ROOT     "/sys/fs/cgroup"
#define SG_MAX_TREE        4096   /* cap on descendants we will enumerate */

/* Core SynapseOS daemons we must never kill, even if an attacker spoofs
 * their comm — the pid checks below already cover *us*; this list protects
 * sibling services whose death would break the system's security posture. */
static const char *const sg_protected_comms[] = {
    "systemd", "init", "kthreadd",
    "synguard", "synapd", "synnet", "synsh",
    NULL,
};

/* comm as the kernel bounds it: TASK_COMM_LEN is a kernel-internal constant,
 * so take the width from the event struct the value actually arrives in
 * rather than hardcoding 16 in a second place. */
#define SG_COMM_LEN  ((int)sizeof(((sg_event_t *)0)->comm))

/* ── /proc helpers ────────────────────────────────────────── */

/* Defined below, used by sg_pid_identity_ok(). */
static int proc_alive(pid_t pid);

/* Parse the parent PID out of /proc/<pid>/stat. Returns -1 on failure.
 * The comm field can contain spaces and ')', so we scan to the final ')'
 * and parse the fixed fields after it. */
static pid_t proc_ppid(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[1024];
    char *got = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (!got) return -1;

    char *rp = strrchr(buf, ')');
    if (!rp) return -1;

    char state;
    int ppid = -1;
    if (sscanf(rp + 1, " %c %d", &state, &ppid) != 2)
        return -1;
    return ppid;
}

/* Read /proc/<pid>/comm into out (NUL-terminated, newline stripped). */
static int proc_comm(pid_t pid, char *out, size_t outlen)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);

    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, out, outlen - 1);
    close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    char *nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    return 0;
}

/* Read field 22 of /proc/<pid>/stat — the process start time, in clock ticks
 * since boot. Same "scan to the final ')'" trick as proc_ppid(): comm is
 * field 2 and may contain spaces and ')', so the fields after it can only be
 * counted from the right-hand end of comm. starttime is the 20th field after
 * that closing paren (state, ppid, pgrp, session, tty_nr, tpgid, flags,
 * minflt, cminflt, majflt, cmajflt, utime, stime, cutime, cstime, priority,
 * nice, num_threads, itrealvalue, starttime).
 *
 * Deliberately NOT compared against sg_event_t.timestamp_ns. That timestamp
 * comes from the kmod's ktime_get_raw_ns() (CLOCK_MONOTONIC_RAW, which stops
 * during suspend), while starttime is derived from the task's boot-time clock,
 * which keeps counting across a suspend. The two drift apart by the machine's
 * total suspend time, so "did this process start after the event?" is not a
 * question these two clocks can answer together. Every comparison here is
 * starttime against starttime — one clock, one domain. */
int sg_proc_starttime(pid_t pid, unsigned long long *out)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[1024];
    char *got = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (!got) return -1;

    char *rp = strrchr(buf, ')');
    if (!rp) return -1;

    unsigned long long st = 0;
    if (sscanf(rp + 1,
               " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d"
               " %*d %*d %*d %*d %llu", &st) != 1)
        return -1;

    if (out) *out = st;
    return 0;
}

/* Is `pid` still the process the event blamed?
 *
 * The window this closes: an event is drained from the kmod ring up to
 * poll_interval_ms (100ms) after the syscall, then classified — possibly
 * behind an AI round-trip. A short-lived process can exit in that time and
 * the kernel can hand its pid to something else. Acting on the bare pid would
 * then SIGSTOP and SIGKILL an unrelated process AND ITS WHOLE SUBTREE.
 * sg_is_protected() is not a backstop for this: it only knows a handful of
 * critical comms, so any ordinary process is fair game.
 *
 * comm is the check because it is what the event carries. Both sides are
 * TASK_COMM_LEN-bounded (15 chars + NUL) and come from the same kernel field,
 * so this compares like with like — no truncation mismatch. */
int sg_pid_identity_ok(pid_t pid, const char *expect_comm)
{
    if (pid <= 0) return 0;
    if (!proc_alive(pid)) return 0;
    if (!expect_comm || !*expect_comm) return 1;

    char now[64];
    if (proc_comm(pid, now, sizeof(now)) < 0)
        return 0;

    return strncmp(now, expect_comm, SG_COMM_LEN - 1) == 0;
}

/* PF_KTHREAD, from the kernel's include/linux/sched.h. Published to userspace
 * as field 9 (flags) of /proc/pid/stat. */
#define SG_PF_KTHREAD 0x00200000u

/* Is `pid` a kernel thread?  1 = yes, 0 = no, -1 = the pid is gone.
 *
 * readlink(/proc/pid/exe) is NOT a usable test, though it looks like one: it
 * fails with ENOENT for a kernel thread, but equally for a pid that has
 * already exited (there is no /proc/pid at all) and for a zombie (the exe
 * link is dropped at exit while the directory lingers until the parent
 * reaps). Every dead process therefore answered "kernel thread".
 *
 * That is not cosmetic. sg_is_protected() runs BEFORE the stale-pid guard, so
 * a target that died between the verdict and the action was refused as "is a
 * kernel thread" and charged to protected_skips — which is a statement about
 * the kernel's own tasks, not about a race — while stale_pid_skips, the
 * counter that exists to measure exactly this race, stayed at zero. Observed
 * 2026-07-29: five refusals naming a dead `claude` as a kernel thread.
 *
 * The task's own PF_KTHREAD flag tells all three cases apart, and reading it
 * from /proc/pid/stat answers existence in the same open(). */
static int proc_is_kthread(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[1024];
    char *got = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (!got) return -1;

    /* comm is parenthesised and may itself contain ')', so parse after the
     * LAST one. Fields from there: state ppid pgrp session tty_nr tpgid flags */
    char *rp = strrchr(buf, ')');
    if (!rp) return -1;

    unsigned int flags = 0;
    if (sscanf(rp + 1, " %*c %*d %*d %*d %*d %*d %u", &flags) != 1)
        return -1;

    return (flags & SG_PF_KTHREAD) ? 1 : 0;
}

/* A process counts as "alive" only if it exists and is not a zombie. A
 * zombie has already terminated and is merely awaiting reap by its parent,
 * so kill(pid, 0) still succeeds for it — read the state field to tell them
 * apart, otherwise kill-verification would falsely warn about a dead task. */
static int proc_alive(pid_t pid)
{
    if (kill(pid, 0) != 0 && errno != EPERM)
        return 0;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char buf[1024];
    char *got = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (!got) return 0;

    char *rp = strrchr(buf, ')');
    char state = 0;
    if (rp) sscanf(rp + 1, " %c", &state);
    return state != 'Z';
}

/* ── The hard guard ───────────────────────────────────────── */
int sg_is_protected(pid_t pid, char *why, size_t wlen)
{
#define DENY_REASON(...) do { if (why) snprintf(why, wlen, __VA_ARGS__); } while (0)

    if (pid <= 1) {
        DENY_REASON("pid=%d is the kernel/init (never killable)", pid);
        return 1;
    }
    if (pid == getpid()) {
        DENY_REASON("pid=%d is synguard itself", pid);
        return 1;
    }
    /* Anything in synguard's own process group (e.g. a helper we spawned). */
    if (getpgid(pid) == getpgrp()) {
        DENY_REASON("pid=%d shares synguard's process group", pid);
        return 1;
    }
    /* A pid that is simply gone is NOT protected. Saying so here would spend
     * the answer on the wrong guard: the caller would log "protected" and
     * increment protected_skips, when what actually happened is that the
     * target exited before we could act on it. Fall through and let
     * sg_pid_identity_ok() name it — that is the guard that understands
     * exits and pid reuse, and its counter is the one worth watching. */
    int kt = proc_is_kthread(pid);
    if (kt > 0) {
        DENY_REASON("pid=%d is a kernel thread", pid);
        return 1;
    }

    char comm[64];
    if (proc_comm(pid, comm, sizeof(comm)) == 0) {
        for (const char *const *p = sg_protected_comms; *p; p++) {
            if (strcmp(comm, *p) == 0) {
                DENY_REASON("pid=%d is protected daemon '%s'", pid, comm);
                return 1;
            }
        }
    }
    return 0;
#undef DENY_REASON
}

/* ── Descendant enumeration ───────────────────────────────── */
/*
 * Collect every descendant of `root` into out[] (not including root itself).
 * Returns the count. Best-effort: a single /proc snapshot, then a breadth
 * walk over (pid, ppid) pairs. Callers SIGSTOP the root first so the tree
 * cannot grow underneath us mid-walk.
 */
static int collect_descendants(pid_t root, pid_t *out, int max)
{
    static pid_t pids[SG_MAX_TREE];
    static pid_t ppids[SG_MAX_TREE];
    int n = 0;

    DIR *d = opendir("/proc");
    if (!d) return 0;

    struct dirent *de;
    while ((de = readdir(d)) && n < SG_MAX_TREE) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        pid_t pid = (pid_t)strtol(de->d_name, NULL, 10);
        if (pid <= 0) continue;
        pid_t pp = proc_ppid(pid);
        if (pp < 0) continue;
        pids[n]  = pid;
        ppids[n] = pp;
        n++;
    }
    closedir(d);

    int out_n = 0;
    /* Repeated sweeps: add any pid whose parent is root or already collected.
     * Bounded by tree depth; process trees are shallow so this terminates
     * quickly. */
    int changed = 1;
    while (changed && out_n < max) {
        changed = 0;
        for (int i = 0; i < n; i++) {
            if (pids[i] == root) continue;
            int is_child = (ppids[i] == root);
            for (int j = 0; !is_child && j < out_n; j++)
                if (ppids[i] == out[j]) is_child = 1;
            if (!is_child) continue;

            int already = 0;
            for (int j = 0; j < out_n; j++)
                if (out[j] == pids[i]) { already = 1; break; }
            if (already) continue;

            if (out_n < max) {
                out[out_n++] = pids[i];
                changed = 1;
            }
        }
    }
    return out_n;
}

/* ── Terminate a process and its descendants ──────────────── */
/*
 * Returns 0 if the target was terminated (or already gone), -1 if the
 * target is protected and was left untouched. Descendants that are
 * individually protected are skipped but do not fail the whole operation.
 */
int sg_kill_tree(synguard_state_t *s, pid_t target, const char *expect_comm,
                 const char *reason)
{
    char why[160];
    if (sg_is_protected(target, why, sizeof(why))) {
        sg_log(LOG_ERR, "⛔ REFUSED deny: %s (reason was: %s)", why, reason);
        s->stats.protected_skips++;
        return -1;
    }

    /* The pid may no longer be the process that earned this verdict — see
     * sg_pid_identity_ok(). Refuse rather than kill a stranger's subtree. */
    if (!sg_pid_identity_ok(target, expect_comm)) {
        sg_log(LOG_WARNING,
               "⛔ REFUSED deny: pid=%u is no longer '%s' (exited, or pid "
               "reused) — reason was: %s",
               target, expect_comm ? expect_comm : "?", reason);
        s->stats.stale_pid_skips++;
        return -1;
    }

    /* Snapshot the start time so the kill can be aborted if the pid turns
     * over between here and the SIGKILL below. */
    unsigned long long st_before = 0;
    int have_st = (sg_proc_starttime(target, &st_before) == 0);

    /* Freeze the target first so it cannot spawn new children while we
     * enumerate and tear down its subtree. */
    kill(target, SIGSTOP);

    /* Re-verify AFTER the SIGSTOP, which is what actually closes the race: a
     * stopped process cannot exit, so from here the pid cannot turn over
     * underneath us. This confirms that the thing now frozen is the thing
     * checked above, and catches a turnover in the microseconds between.
     *
     * On mismatch, SIGCONT before bailing — we just suspended a bystander,
     * and leaving it stopped would be its own denial of service. */
    unsigned long long st_now = 0;
    if (have_st && (sg_proc_starttime(target, &st_now) != 0 ||
                    st_now != st_before)) {
        kill(target, SIGCONT);
        sg_log(LOG_WARNING,
               "⛔ REFUSED deny: pid=%u turned over during teardown "
               "(starttime %llu → %llu); resumed it — reason was: %s",
               target, st_before, st_now, reason);
        s->stats.stale_pid_skips++;
        return -1;
    }

    static pid_t tree[SG_MAX_TREE];
    int count = collect_descendants(target, tree, SG_MAX_TREE);

    int killed = 0, skipped = 0;
    for (int i = 0; i < count; i++) {
        if (sg_is_protected(tree[i], NULL, 0)) {
            skipped++;
            continue;
        }
        if (kill(tree[i], SIGKILL) == 0)
            killed++;
    }

    /* Kill the target last (after its children) and verify. */
    if (kill(target, SIGKILL) < 0 && errno == ESRCH) {
        sg_log(LOG_INFO, "deny: pid=%u already gone", target);
    } else {
        killed++;
    }

    /* Verify the target is actually dead. SIGKILL is asynchronous and a
     * task in uninterruptible sleep (D state) may linger; give the kernel
     * a brief window before warning. */
    int alive = 0;
    for (int i = 0; i < 20; i++) {
        if (!proc_alive(target)) break;
        struct timespec ts = { 0, 5 * 1000 * 1000 };  /* 5ms */
        nanosleep(&ts, NULL);
        alive = proc_alive(target);
    }
    if (alive)
        sg_log(LOG_WARNING,
               "deny: pid=%u still alive after SIGKILL (uninterruptible?)",
               target);

    sg_log(LOG_WARNING,
           "⚡ DENY: pid=%u tree torn down — killed=%d protected-skipped=%d — %s",
           target, killed, skipped, reason);
    return 0;
}

/* ── cgroup v2 freeze ─────────────────────────────────────── */
static int cgroup_v2_available(void)
{
    /* The unified hierarchy exposes cgroup.controllers at the root. */
    return access(SG_CGROUP_ROOT "/cgroup.controllers", F_OK) == 0;
}

static int cgroup_write(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t n = write(fd, val, strlen(val));
    close(fd);
    return n < 0 ? -1 : 0;
}

/*
 * Quarantine: freeze the target subtree. Returns 0 if the target was
 * frozen (via cgroup or at least SIGSTOP), -1 if protected.
 */
int sg_freeze_tree(synguard_state_t *s, pid_t target, const char *expect_comm)
{
    char why[160];
    if (sg_is_protected(target, why, sizeof(why))) {
        sg_log(LOG_ERR, "⛔ REFUSED quarantine: %s", why);
        s->stats.protected_skips++;
        return -1;
    }

    /* Same stale-pid guard as sg_kill_tree(). Freezing the wrong process is
     * less final than killing it, but it is still a hang inflicted on an
     * innocent subtree, and the admin would be handed a forensic note about
     * a process that never did anything. */
    if (!sg_pid_identity_ok(target, expect_comm)) {
        sg_log(LOG_WARNING,
               "⛔ REFUSED quarantine: pid=%u is no longer '%s' "
               "(exited, or pid reused)",
               target, expect_comm ? expect_comm : "?");
        s->stats.stale_pid_skips++;
        return -1;
    }

    unsigned long long st_before = 0;
    int have_st = (sg_proc_starttime(target, &st_before) == 0);

    /* SIGSTOP everything first so the subtree is stable and cannot fork
     * while we move it into the cgroup. */
    kill(target, SIGSTOP);

    unsigned long long st_now = 0;
    if (have_st && (sg_proc_starttime(target, &st_now) != 0 ||
                    st_now != st_before)) {
        kill(target, SIGCONT);
        sg_log(LOG_WARNING,
               "⛔ REFUSED quarantine: pid=%u turned over during freeze "
               "(starttime %llu → %llu); resumed it",
               target, st_before, st_now);
        s->stats.stale_pid_skips++;
        return -1;
    }

    static pid_t tree[SG_MAX_TREE];
    int count = collect_descendants(target, tree, SG_MAX_TREE);
    for (int i = 0; i < count; i++)
        kill(tree[i], SIGSTOP);

    if (!cgroup_v2_available()) {
        sg_log(LOG_WARNING,
               "🔒 QUARANTINE: pid=%u frozen via SIGSTOP "
               "(cgroup v2 unavailable, %d descendants stopped)",
               target, count);
        return 0;
    }

    /* Dedicated cgroup per quarantine event, directly under the root so it
     * has no internal-process constraint to satisfy. */
    char cg[256];
    snprintf(cg, sizeof(cg), SG_CGROUP_ROOT "/synguard.quarantine.%u", target);
    if (mkdir(cg, 0755) < 0 && errno != EEXIST) {
        sg_log(LOG_WARNING,
               "🔒 QUARANTINE: pid=%u SIGSTOP-frozen; cgroup mkdir failed: %s",
               target, strerror(errno));
        return 0;
    }

    char procs[300];
    snprintf(procs, sizeof(procs), "%s/cgroup.procs", cg);
    char pidbuf[16];

    /* Move the whole subtree in (children before parent doesn't matter; each
     * write moves exactly that pid). */
    snprintf(pidbuf, sizeof(pidbuf), "%u\n", target);
    cgroup_write(procs, pidbuf);
    int moved = 1;
    for (int i = 0; i < count; i++) {
        snprintf(pidbuf, sizeof(pidbuf), "%u\n", tree[i]);
        if (cgroup_write(procs, pidbuf) == 0)
            moved++;
    }

    /* Freeze the cgroup: robust against SIGCONT and against the processes
     * forking their way out — anything in the cgroup stays frozen. */
    char freeze[300];
    snprintf(freeze, sizeof(freeze), "%s/cgroup.freeze", cg);
    if (cgroup_write(freeze, "1\n") == 0)
        sg_log(LOG_WARNING,
               "🔒 QUARANTINE: pid=%u subtree frozen (%d procs in %s)",
               target, moved, cg);
    else
        sg_log(LOG_WARNING,
               "🔒 QUARANTINE: pid=%u moved to %s but freeze failed; "
               "SIGSTOP still in effect", target, cg);

    return 0;
}
