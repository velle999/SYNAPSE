/*
 * gpuload_test.c — the GPU load probe, on a machine that may have no GPU.
 *
 * gpuload.c takes its sys directory as a parameter, and naming one means sysfs
 * ONLY — NVML is never consulted. That is what makes this hermetic: a build
 * machine with a real NVIDIA card must not be able to make these assertions
 * pass or fail on what that card happens to be doing.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gpuload.h"

static int failures = 0;

static void check(const char *what, int ok) {
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static char root[256];

static void mkdirp(const char *rel)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    /* ⛔ FROM path + 1, NOT PAST THE ROOT. Starting after the root's own slash
     * skips creating the root itself, and then every nested mkdir fails with
     * ENOENT while mkdirp reports nothing. */
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(path, 0755);
        *p = '/';
    }
    mkdir(path, 0755);
}

/* Write a card's counter. value NULL removes the file, leaving the directory —
 * the shape a driver without the counter actually has. */
static void put_card(const char *card, const char *value)
{
    char rel[256];
    snprintf(rel, sizeof(rel), "class/drm/%s/device", card);
    mkdirp(rel);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/gpu_busy_percent", root, rel);
    if (!value) { unlink(path); return; }

    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "%s\n", value);
    fclose(f);
}

int main(void)
{
    syn_gpuload_t g;

    printf("synapd GPU load probe\n\n");

    /* ── No /sys/class/drm at all ──────────────────────────────────────── */
    snprintf(root, sizeof(root), "gpuload_test.d/no-such-tree");
    syn_gpuload_read(root, &g);
    check("a tree that is not there reads as NOT MEASURED, never as idle",
          (g.available == 0 && g.busy_pct == 0));

    /* ⚠ AND THE REST RUNS TWICE. meson keeps the build directory between runs,
     * so a fixture left behind by the last run would make the first assertion
     * below depend on the previous invocation. Clear the counters first. */
    snprintf(root, sizeof(root), "gpuload_test.d");
    mkdirp("class/drm");
    put_card("card0", NULL);
    put_card("card1", NULL);

    /* ── A card with no counter (the NVIDIA shape) ─────────────────────── */
    syn_gpuload_read(root, &g);
    check("a card with no gpu_busy_percent is not measured",
          (g.available == 0));

    /* ── An ordinary reading ───────────────────────────────────────────── */
    put_card("card0", "73");
    syn_gpuload_read(root, &g);
    check("a card reporting 73% is read as 73% and available",
          (g.available == 1 && g.busy_pct == 73));

    /* ── Nonsense is clamped, not propagated ───────────────────────────── */
    put_card("card0", "4294967295");
    syn_gpuload_read(root, &g);
    check("a driver reporting past 100 is clamped to 100",
          (g.available == 1 && g.busy_pct == 100));

    /* ── Connectors are not devices ────────────────────────────────────── */
    /* card0-DP-1 lives right beside card0 in /sys/class/drm and has no device/
     * of its own. Matching it would read a file that is never there. */
    put_card("card0", "12");
    mkdirp("class/drm/card0-DP-1/device");
    syn_gpuload_read(root, &g);
    check("a connector directory is skipped, the card is still read",
          (g.available == 1 && g.busy_pct == 12));

    /* ── Which card, twice running ─────────────────────────────────────── */
    /*
     * ⚠ readdir does not promise an order, so "the first card" has to be a
     * decision rather than whatever the directory hands back. gpuload.c keeps
     * the LOWEST name, so adding a higher-numbered card must not move the
     * answer — otherwise the policy would follow a different card between two
     * polls with nothing having changed.
     */
    put_card("card1", "99");
    syn_gpuload_read(root, &g);
    check("adding card1 does not move the answer off card0",
          (g.available == 1 && g.busy_pct == 12));

    /* And it is genuinely reading card0 rather than ignoring everything. */
    put_card("card0", "44");
    syn_gpuload_read(root, &g);
    check("card0 changing IS seen (the reading is not stuck)",
          (g.available == 1 && g.busy_pct == 44));

    printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
