/*
 * sysload_test.c — reading what the machine is short of, off fixtures.
 *
 * ⛔ FIXTURES, NOT /proc. The branches that matter are a kernel with no PSI, a
 * meminfo without MemAvailable, and a machine actually stalling — none of which
 * a build machine can be asked to be. sysload.c takes its directory as a
 * parameter for exactly this, so every case here is a few files in a temp tree.
 *
 * The case this exists for is the prefix match: "MemTotal" and "MemAvailable"
 * both begin "Mem", and MemTotal comes FIRST — a loose match would report a
 * 32 GB machine as having 32 GB available at all times, which is a policy that
 * never fires and a bug nothing would ever print.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sysload.h"

static int failures = 0;

static void check(const char *what, int ok) {
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static char root[] = "/tmp/synapd-sysload.XXXXXX";

static void put(const char *rel, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    FILE *f = fopen(path, "we");
    if (!f) { perror(path); exit(2); }
    fputs(text, f);
    fclose(f);
}

static void rm(const char *rel) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    unlink(path);
}

static const char *MEMINFO =
    "MemTotal:       32731668 kB\n"
    "MemFree:          981234 kB\n"
    "MemAvailable:   16455168 kB\n"
    "Buffers:            8192 kB\n";

int main(void)
{
    syn_sysload_t s;
    char sub[512];

    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
    snprintf(sub, sizeof(sub), "%s/pressure", root);
    if (mkdir(sub, 0700) != 0) { perror(sub); return 2; }

    printf("synapd sysload\n\n");

    /* ── the ordinary machine ──────────────────────────────────────────── */
    put("meminfo", MEMINFO);
    put("pressure/memory", "some avg10=0.00 avg60=0.00 avg300=0.00 total=1\n"
                           "full avg10=0.00 avg60=0.00 avg300=0.00 total=1\n");
    put("pressure/cpu",    "some avg10=9.99 avg60=1.75 avg300=0.10 total=9\n"
                           "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n");
    syn_sysload_read(root, &s);

    check("MemTotal is read", s.mem_total_mib == 32731668 / 1024);
    /* ⛔ NOT MemTotal, and not MemFree either. */
    check("...and MemAvailable is the one used, not the first Mem line",
          s.mem_avail_mib == 16455168 / 1024);
    check("...which is not MemFree", s.mem_avail_mib != 981234 / 1024);
    check("PSI is found", s.psi_available == 1);
    check("a quiet machine reads zero memory stall", s.psi_mem_pct == 0);
    /* ⛔ avg60, NOT avg10 — the fixture's avg10 is 9.99 and would read as 9.
     * A ten-second window unloads the model over one heavy compile of one
     * file; the decision this feeds costs tens of seconds to undo. */
    check("...and the whole percent of the cpu one, from avg60", s.psi_cpu_pct == 1);
    check("...not from avg10", s.psi_cpu_pct != 9);

    /* ── a machine that is actually stalling ───────────────────────────── */
    put("pressure/memory", "some avg10=63.72 avg60=40.11 avg300=9.02 total=5\n"
                           "full avg10=31.00 avg60=20.00 avg300=4.00 total=2\n");
    syn_sysload_read(root, &s);
    check("a stalling machine reports it", s.psi_mem_pct == 40);

    /* ⛔ `some`, NEVER `full`. full/cpu is documented as undefined at the
     * system level and reads 0.00 for ever; keying on it is a policy that
     * never fires. This fixture has a full line that would give 31. */
    check("...from the `some` line, not `full`", s.psi_mem_pct != 20);

    /* ── a kernel with no PSI ──────────────────────────────────────────── */
    rm("pressure/memory");
    rm("pressure/cpu");
    syn_sysload_read(root, &s);
    check("no PSI at all says so", s.psi_available == 0);
    check("...and leaves the percentages zero", s.psi_mem_pct == 0 && s.psi_cpu_pct == 0);
    check("...while memory is still read", s.mem_avail_mib == 16455168 / 1024);

    /* ⛔ HALF A SIGNAL IS NOT A SIGNAL. One file present and one missing must
     * not be read as "the other resource is quiet". */
    put("pressure/memory", "some avg10=90.00 avg60=90.00 avg300=90.00 total=5\n");
    syn_sysload_read(root, &s);
    check("one pressure file without the other is not trusted",
          s.psi_available == 0);

    /* ── a meminfo that does not carry MemAvailable (very old kernels) ─── */
    rm("pressure/memory");
    put("meminfo", "MemTotal:       32731668 kB\nMemFree:  981234 kB\n");
    syn_sysload_read(root, &s);
    check("a meminfo with no MemAvailable leaves it zero, not guessed",
          s.mem_avail_mib == 0 && s.mem_total_mib == 32731668 / 1024);

    /* ── nothing readable at all ───────────────────────────────────────── */
    rm("meminfo");
    syn_sysload_read(root, &s);
    check("an unreadable /proc reports nothing measured",
          s.mem_total_mib == 0 && s.mem_avail_mib == 0 && s.psi_available == 0);

    /* ⚠ And a NULL directory is the real /proc, which must at least parse. */
    syn_sysload_read(NULL, &s);
    check("the real /proc gives a total", s.mem_total_mib > 0);

    rmdir(sub);
    rmdir(root);
    printf("\n%s\n", failures ? "FAILURES" : "all sysload tests passed");
    return failures ? 1 : 0;
}
