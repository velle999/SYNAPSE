/*
 * taskmgr_cpu_test.c — the CPU column is on the same scale as the CPU meter
 *
 * ⛔ THE BUG WAS NOT AN ERROR, IT WAS TWO SCALES UNDER ONE WORD. The panel's
 * meter has always been percent of the whole machine, 0-100. The per-process
 * column multiplied by ncpu(), which is top's convention — 100% means one core
 * saturated, so on a twelve-core desk the column can add up to 1200. Both
 * numbers were individually correct and they were printed six lines apart, so
 * the panel read
 *
 *     CPU  13%  (12 cores)
 *     synui                   82.1
 *
 * and looked simply broken. Reported 2026-08-26 as "percentages never seem
 * accurate". Measured against /proc at the time: synui really was using 72% of
 * one core and the machine really was 11.7% busy. Nothing was wrong except
 * that the reader had no way to know the two were different questions.
 *
 * ⚠ THIS IS EXACTLY THE BUG THAT A SCREENSHOT CANNOT SHOW YOU ON A ONE-CORE
 * MACHINE, and that a developer with a busy desktop reads straight past. The
 * whole test is therefore about the FACTOR OF ncpu, which is why every case
 * below names a core count.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <math.h>
#include <stdio.h>

#include "synui.h"

/* ── The compositor, stubbed ─────────────────────────────────
 *
 * taskmgr.c is linked whole so the arithmetic under test is the SHIPPED one
 * and not a copy of it in this file — a copy would go on passing after the
 * real line changed, which for a scale bug is the exact failure mode. What it
 * calls back into is the panel and the renderer, and neither is reachable from
 * a unit test. One line each, like the control-panel tests do it.
 */
void synui_render_taskmgr(syn_server_t *s) { (void)s; }
void ctlpanel_child_closed(syn_server_t *s, const char *a) { (void)s; (void)a; }
pid_t view_pid(syn_view_t *v) { (void)v; return 0; }
/* …and panel.c's own two, which it dispatches to by panel kind. */
void synui_render_calc(syn_server_t *s) { (void)s; }
void synui_render_ctlpanel(syn_server_t *s) { (void)s; }

static int fails;

static void near(const char *what, double got, double want)
{
    if (fabs(got - want) < 0.05) {
        printf("  ok    %-52s %.2f\n", what, got);
        return;
    }
    printf("  FAIL  %-52s got %.2f, wanted %.2f\n", what, got, want);
    fails++;
}

int main(void)
{
    /* One poll of one second at 100 Hz on a 12-core machine: every core
     * contributes 100 jiffies, so the window is 1200. */
    const unsigned long long HZ = 100, CORES = 12;
    const unsigned long long window = HZ * CORES;

    /* A process that used one whole core for the whole window. THIS IS THE
     * CASE THE BUG LIVED IN: the old code answered 100.0 here — correct as
     * "one core's worth", and impossible to reconcile with a meter that would
     * be reading 8% at the same moment. */
    near("one core of twelve is a twelfth of the machine",
         taskmgr_cpu_pct((long long)HZ, window), 100.0 / 12.0);

    /* And the same process on a two-core laptop is a quarter of it. The
     * printed number MOVES with the core count, which is the honest half of
     * this convention and the reason the row's colour is scaled by one core's
     * worth rather than by the number — see render.c. */
    near("one core of two is half the machine",
         taskmgr_cpu_pct((long long)HZ, HZ * 2), 50.0);

    /* Every core busy is the whole machine, and nothing can exceed it. That is
     * the invariant that makes the column comparable to the meter above it:
     * the rows sum to what the meter shows, near enough. */
    near("all twelve cores is 100%",
         taskmgr_cpu_pct((long long)window, window), 100.0);

    /* Half a core. */
    near("half a core of twelve",
         taskmgr_cpu_pct((long long)HZ / 2, window), 100.0 / 24.0);

    /* An idle process. */
    near("no jiffies is no percent", taskmgr_cpu_pct(0, window), 0.0);

    /* ⚠ DIVISION BY ZERO IS REACHABLE, not theoretical: two polls inside the
     * same jiffy give dtotal 0, and the panel repolls 150 ms after it opens on
     * purpose. It must be 0 and not a NaN — a NaN sorts unpredictably and
     * prints as "nan" or "-nan" depending on the libc. */
    near("a zero window is zero, not a NaN", taskmgr_cpu_pct(50, 0), 0.0);

    /* A process's counter cannot go backwards, but a pid can be REUSED between
     * polls: the baseline then belongs to a dead process with more jiffies
     * than the new one has, and the difference is negative. Clamped, or the
     * table shows a negative percentage and the sort puts it above everything.
     * (taskmgr.c also clamps dj before calling; both, deliberately — this one
     * is the guard that travels with the arithmetic.) */
    near("a reused pid does not print a negative", taskmgr_cpu_pct(-500, window),
         0.0);

    /* The whole point, stated as the invariant a reader checks by eye: add the
     * rows up and you get something a CPU meter could show. Twelve processes,
     * each pegging a core, is a machine at 100% — not at 1200%. */
    double sum = 0;
    for (int i = 0; i < 12; i++) sum += taskmgr_cpu_pct((long long)HZ, window);
    near("twelve pegged cores sum to a full machine", sum, 100.0);

    printf("%s\n", fails ? "taskmgr_cpu: FAILED" : "taskmgr_cpu: ok");
    return fails ? 1 : 0;
}
