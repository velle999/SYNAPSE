#ifndef SYSLOAD_H
#define SYSLOAD_H
/*
 * sysload.h — what the machine is short of, read out of /proc.
 *
 * The other half of pressure.c's inputs. offload.c already reads free VRAM off
 * the card; this reads the two resources a layer count cannot help with, and
 * the two that shedding a layer actively SPENDS.
 *
 * ⚠ PSI, NOT FREE BYTES, WHEREVER THE KERNEL HAS IT. "Free memory" on Linux is
 * not a measure of shortage — the page cache keeps it small on a machine with
 * nothing whatever wrong, and MemFree in particular is meaningless. Pressure
 * Stall Information answers the question actually being asked: how much of the
 * last ten seconds did real tasks spend STALLED waiting for memory, or for a
 * core. A kernel built without CONFIG_PSI reports nothing here and the floors
 * carry the decision alone.
 *
 * ⚠ MemAvailable, not MemFree, for the fallback — it is the kernel's own
 * estimate of what a new allocation could actually get, reclaim included.
 *
 * ⛔ THE PROC DIRECTORY IS A PARAMETER, not an environment variable. It is how
 * the suite drives every branch here off fixture files without a kernel that
 * happens to be under pressure, and a parameter cannot be lost the way an
 * environment variable is when something escalates — the trap that had synpkg
 * editing the live /etc/pacman.conf.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>

typedef struct {
    size_t   mem_avail_mib;   /* MemAvailable; 0 if it could not be read */
    size_t   mem_total_mib;   /* MemTotal;     0 if it could not be read */

    /* `some avg60` as a whole percent, 0-100. Meaningful only when
     * psi_available is 1 — a kernel without PSI leaves them zero, which must
     * not be mistaken for "measured, and quiet".
     *
     * ⚠ THE MINUTE, NOT THE TEN SECONDS. What this decides is whether to
     * unload a multi-GB model that costs tens of seconds to bring back, so the
     * question is whether the machine is busy rather than whether it was busy
     * for a moment. avg10 releases on one heavy compile of one file. */
    unsigned psi_mem_pct;
    unsigned psi_cpu_pct;
    int      psi_available;
} syn_sysload_t;

/*
 * Fill `out` from <proc_dir>/meminfo and <proc_dir>/pressure/{memory,cpu}.
 * Never fails: anything unreadable is left zero, which every consumer must
 * treat as "not measured" rather than as "nothing is wrong".
 *
 * proc_dir may be NULL for "/proc".
 */
void syn_sysload_read(const char *proc_dir, syn_sysload_t *out);

#endif
