/*
 * isolation.c — Process isolation primitives for ENFORCE mode
 *
 * action_deny()/action_quarantine() must never act on a single PID in
 * isolation: a malicious process may have already forked, and a careless
 * SIGKILL can take down the whole system. This module provides the three
 * primitives that make ENFORCE mode safe to actually turn on:
 *
 *   sg_is_protected()  — the hard guard. Refuses to ever touch PID 0/1,
 *                        synguard itself, our own process group, kernel
 *                        threads, or the core SynapseOS daemons. This is
 *                        the last line of defence and cannot be disabled.
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

/* ── /proc helpers ────────────────────────────────────────── */

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

/* A kernel thread has no userspace executable, so readlink(/proc/pid/exe)
 * fails with ENOENT (we run as root, so EACCES is not in play). */
static int proc_is_kthread(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    char link[16];
    return (readlink(path, link, sizeof(link)) < 0 && errno == ENOENT);
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
    if (proc_is_kthread(pid)) {
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
int sg_kill_tree(synguard_state_t *s, pid_t target, const char *reason)
{
    char why[160];
    if (sg_is_protected(target, why, sizeof(why))) {
        sg_log(LOG_ERR, "⛔ REFUSED deny: %s (reason was: %s)", why, reason);
        s->stats.protected_skips++;
        return -1;
    }

    /* Freeze the target first so it cannot spawn new children while we
     * enumerate and tear down its subtree. */
    kill(target, SIGSTOP);

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
int sg_freeze_tree(synguard_state_t *s, pid_t target)
{
    char why[160];
    if (sg_is_protected(target, why, sizeof(why))) {
        sg_log(LOG_ERR, "⛔ REFUSED quarantine: %s", why);
        s->stats.protected_skips++;
        return -1;
    }

    /* SIGSTOP everything first so the subtree is stable and cannot fork
     * while we move it into the cgroup. */
    kill(target, SIGSTOP);

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
