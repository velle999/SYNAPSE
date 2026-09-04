/*
 * sysload.c — see sysload.h.
 *
 * Dependency-free on purpose, like pressure.c: no llama, no daemon state, one
 * parameter in and one struct out, so sysload_test drives every branch off
 * fixture files rather than off whatever the build machine happens to be doing.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sysload.h"

/* kB as /proc/meminfo prints it → MiB. */
static size_t kb_to_mib(unsigned long long kb) { return (size_t)(kb / 1024ULL); }

static void read_meminfo(const char *dir, syn_sysload_t *out)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/meminfo", dir);
    FILE *f = fopen(path, "re");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long v;
        /*
         * ⚠ MATCHED WITH THE COLON. "MemAvailable" and "MemTotal" are distinct
         * keys, but a prefix match on "Mem" would take whichever came first —
         * and the first is MemTotal, which would report a machine with 32 GB
         * of RAM as having 32 GB available at all times.
         */
        if (sscanf(line, "MemTotal: %llu kB", &v) == 1)
            out->mem_total_mib = kb_to_mib(v);
        else if (sscanf(line, "MemAvailable: %llu kB", &v) == 1)
            out->mem_avail_mib = kb_to_mib(v);
    }
    fclose(f);
}

/*
 * `some avg10=... avg60=12.34 ...` → 12.
 *
 * ⚠ avg60, NOT avg10. The decision this feeds is whether to unload a multi-GB
 * model, which costs tens of seconds to undo — so the question is whether the
 * machine is BUSY, not whether it just was for a moment. A ten-second window
 * releases on one heavy compile of one file; a minute is the shape of the
 * thing actually worth stepping aside for, and the policy's 120 s dwell is
 * sized for the same reason.
 *
 * ⚠ THE `some` LINE, NOT `full`. "some" is time in which AT LEAST ONE task was
 * stalled on the resource, which is the question here — is anything waiting on
 * memory or a core. "full" means EVERY task was stalled at once, which on a
 * desktop is a machine already in trouble, and for CPU is documented as
 * undefined at the system level (it reads 0.00 always). Keying on it would be
 * a policy that never fires.
 *
 * ⚠ AND NO FLOATING POINT IN THE RESULT. The whole percent is all the policy
 * uses, and parsing "12.34" as two integers avoids a locale that would read
 * the decimal point as a thousands separator — the same trap syn-cal hit.
 */
static int read_psi_some(const char *dir, const char *what, unsigned *pct)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/pressure/%s", dir, what);
    FILE *f = fopen(path, "re");
    if (!f) return 0;

    int got = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        unsigned a10 = 0, f10 = 0, whole = 0, frac = 0;
        if (strncmp(line, "some ", 5) != 0) continue;
        if (sscanf(line, "some avg10=%u.%u avg60=%u.%u",
                   &a10, &f10, &whole, &frac) >= 3) {
            *pct = whole > 100 ? 100 : whole;
            got  = 1;
        }
        break;
    }
    fclose(f);
    return got;
}

void syn_sysload_read(const char *proc_dir, syn_sysload_t *out)
{
    const char *dir = proc_dir ? proc_dir : "/proc";
    memset(out, 0, sizeof(*out));

    read_meminfo(dir, out);

    /*
     * ⛔ BOTH OR NEITHER. A kernel with CONFIG_PSI has both files; one of them
     * missing means something stranger is going on than a missing config
     * option, and half a signal is worse than none — the policy would read the
     * absent half as a quiet resource and act on the other alone.
     */
    unsigned mem = 0, cpu = 0;
    if (read_psi_some(dir, "memory", &mem) && read_psi_some(dir, "cpu", &cpu)) {
        out->psi_mem_pct   = mem;
        out->psi_cpu_pct   = cpu;
        out->psi_available = 1;
    }
}
