/*
 * cat_pace_test.c — the cat steps 60 times a second, whatever the screen.
 *
 * Drives cat_pace.c with the frame times a real output would deliver:
 *
 *   busy  — something else animates, so a frame arrives on every vblank and
 *           the cat must skip the ones it does not need (144 and 165 Hz) and
 *           take every one on a 60 or 30 Hz screen, jitter and all;
 *   idle  — nothing else moves, so frames come only from the cat's own timer
 *           (cat_pace_wait_ms), landing on the vblank after it fires; every
 *           one of those frames must take a step, or it was a wasted repaint;
 *   stall — two seconds with no frames (a lock screen, a blank): no burst of
 *           catch-up steps afterwards.
 *
 * Before pkgrel 633 the cat stepped on every frame: 1440 steps in 10 s at
 * 144 Hz, each one a full-output blur with glass on.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <stdio.h>

#include "cat_pace.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) { printf("  ok    "); } \
    else { printf("  FAIL  "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

/* Deterministic jitter in [-j, +j]. */
static double jitter(unsigned *seed, double j)
{
    *seed = *seed * 1103515245u + 12345u;
    return ((double)((*seed >> 8) & 0xffff) / 65535.0 * 2.0 - 1.0) * j;
}

struct busy { int frames, steps, max_gap_frames; };

static struct busy run_busy(double hz, double secs, double jit)
{
    struct busy r = { 0, 0, 0 };
    double due = 0.0, t0 = 100.0;
    unsigned seed = 7;
    int since = 0;
    for (int k = 0; k < (int)(hz * secs); k++) {
        double now = t0 + k / hz + jitter(&seed, jit);
        r.frames++;
        since++;
        if (cat_pace_due(&due, now)) {
            r.steps++;
            if (since > r.max_gap_frames) r.max_gap_frames = since;
            since = 0;
        }
    }
    return r;
}

/* Frames only from the timer: each lands on the first vblank at or after the
 * wake-up the previous step asked for. */
static void run_idle(double hz, double secs, int *frames, int *steps)
{
    double due = 0.0, now = 100.0, end = now + secs;
    *frames = *steps = 0;
    while (now < end) {
        (*frames)++;
        if (cat_pace_due(&due, now)) (*steps)++;
        double wake = now + cat_pace_wait_ms(due, now) / 1000.0;
        now = ceil(wake * hz - 1e-9) / hz;      /* next vblank */
    }
}

int main(void)
{
    printf("cat_pace: the cat steps %.0f times a second\n", CAT_PACE_FPS);

    struct busy b = run_busy(144.0, 10.0, 0.0002);
    CHECK(b.steps >= 590 && b.steps <= 610,
          "144 Hz, busy screen: %d steps in 10 s (was %d — every frame)", b.steps, b.frames);
    CHECK(b.max_gap_frames <= 3,
          "…never more than 3 vblanks between steps (%d)", b.max_gap_frames);

    b = run_busy(165.0, 10.0, 0.0002);
    CHECK(b.steps >= 590 && b.steps <= 610, "165 Hz, busy screen: %d steps in 10 s", b.steps);

    b = run_busy(60.0, 10.0, 0.0015);
    CHECK(b.steps == b.frames,
          "60 Hz with ±1.5 ms jitter: a step on every frame (%d of %d) — no skipped vblank",
          b.steps, b.frames);

    b = run_busy(30.0, 10.0, 0.0015);
    CHECK(b.steps == b.frames, "30 Hz: a step on every frame (%d of %d)", b.steps, b.frames);

    int f, s;
    run_idle(144.0, 10.0, &f, &s);
    CHECK(s >= 590 && s <= 610, "144 Hz, idle screen: %d steps in 10 s from the timer alone", s);
    CHECK(f == s, "…and every frame the timer asked for took a step (%d frames, %d steps)", f, s);

    run_idle(60.0, 10.0, &f, &s);
    CHECK(s >= 590 && s <= 601 && f == s, "60 Hz, idle screen: %d steps, %d frames", s, f);

    /* A stall: steps up to t=1, nothing for 2 s, then frames again. */
    double due = 0.0;
    int before = 0, after = 0;
    for (int k = 0; k < 144; k++)
        before += cat_pace_due(&due, 100.0 + k / 144.0);
    for (int k = 0; k < 144; k++)
        after += cat_pace_due(&due, 103.0 + k / 144.0);
    CHECK(after >= 59 && after <= 61,
          "after a 2 s stall: %d steps in the next second, no catch-up burst (%d before)",
          after, before);

    /* The wake-up is never 0 ms (a spin) and never late. */
    CHECK(cat_pace_wait_ms(100.0, 100.0) == 1, "an overdue step wakes in 1 ms, not 0");
    CHECK(cat_pace_wait_ms(100.0 + CAT_PACE_PERIOD, 100.0) <= 13,
          "a step one period away wakes %d ms ahead of it", cat_pace_wait_ms(100.0 + CAT_PACE_PERIOD, 100.0));

    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
