/* Shot match — make this shot look like that one.
 *
 * The hard part of a matcher is not measuring two pictures, it is knowing
 * what to DO about the difference. Every control here has a transfer function
 * of its own: exposure is stops, contrast is a curve around a pivot, and
 * temperature is a chromatic adaptation whose effect on a pixel depends on
 * where that pixel already is. Solving any of them in closed form means
 * writing down a second model of what colour.c does — and a second model is
 * one that drifts, silently, the first time a control is improved.
 *
 * So nothing is solved analytically. Each control is FITTED: set it, render
 * the shot through the real engine, measure, and bisect. That costs a few
 * dozen renders of a thumbnail and it is correct BY CONSTRUCTION — if
 * colour.c changes what `contrast` means tomorrow, this still lands on the
 * right number, because it never knew what contrast meant in the first place.
 *
 * What it matches is brightness, contrast and white balance. Not a three-way
 * grade: there are no per-channel lift/gamma/gain controls in this program,
 * and inventing them to have something to solve for would be the tail wagging
 * the dog. Those three are what makes two shots of the same scene cut
 * together, which is the job.
 */
#include "synstudio.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/* The four numbers a match is made of, all in the DISPLAY encoding — the one
 * a person is looking at, and the one clipping happens in. */
typedef struct {
    double luma;                /* mean */
    double spread;              /* standard deviation of luma */
    /* ⚠ The axes are chosen to match the CONTROLS, not to be tidy. Warm and
     * cool is red against blue, which is exactly what a temperature does;
     * green and magenta is green against the other two, which is exactly what
     * a tint does. Measuring R/G and B/G instead gives two numbers that BOTH
     * controls move, and a coordinate descent over those chases its own tail
     * — the first version did, and pinned tint at its limit. */
    double rb;                  /* mean R over mean B — warm against cool */
    double gm;                  /* mean G over the mean of R and B */
} ss_look_stats;

static void stats_of(const ss_image *im, ss_look_stats *s)
{
    long i, n = (long)im->w * im->h;
    double sl = 0, sq = 0, sr = 0, sg = 0, sb = 0;

    memset(s, 0, sizeof(*s));
    if (n < 1) return;
    for (i = 0; i < n; i++) {
        const float *p = im->px + i * 4;
        /* ⚠ CLAMPED, the way the histogram clamps.
         *
         * What is being matched is a DISPLAYED picture, and a value above
         * white is white — there is nothing brighter to show. Left unclamped,
         * a shot pushed eight stops up reports a mean of 5.0 rather than 1.0,
         * so the fit believes brightness keeps climbing forever, reads the
         * bright end as further from the target than the black end, and pins
         * exposure at MINUS eight. It did exactly that. */
        double r = ss_clampf(ss_linear_to_srgb(p[0]), 0.0f, 1.0f);
        double g = ss_clampf(ss_linear_to_srgb(p[1]), 0.0f, 1.0f);
        double b = ss_clampf(ss_linear_to_srgb(p[2]), 0.0f, 1.0f);
        double l = ss_clampf(ss_linear_to_srgb(ss_luma(p[0], p[1], p[2])),
                             0.0f, 1.0f);
        sl += l; sq += l * l;
        sr += r; sg += g; sb += b;
    }
    s->luma = sl / n;
    s->spread = sqrt(sq / n - (sl / n) * (sl / n));
    /* Ratios rather than differences: a shot that is simply darker has the
     * same colour cast, and a matcher that read the cast off the raw means
     * would spend its white balance undoing an exposure difference. */
    s->rb = sb > 1e-6 ? sr / sb : 1.0;
    s->gm = (sr + sb) > 1e-6 ? sg / ((sr + sb) / 2.0) : 1.0;
}

/* One statistic of `src` seen through develop stack `d`. `work` is scratch
 * the caller owns, the same size as src, so a fit of twenty steps allocates
 * nothing. */
static double measure(const ss_image *src, ss_image *work,
                      const ss_develop *d, int which)
{
    ss_edit e;
    ss_look_stats s;

    memcpy(work->px, src->px, (size_t)src->w * src->h * 4 * sizeof(float));
    memset(&e, 0, sizeof e);
    e.dev = *d;
    e.nmasks = 0;
    ss_edit_apply(work, &e);
    stats_of(work, &s);
    switch (which) {
    case 1:  return s.spread;
    case 2:  return s.rb;
    case 3:  return s.gm;
    default: return s.luma;
    }
}

/* Bisect one control until its statistic hits the target.
 *
 * No assumption about which way the control moves the number: the two ends
 * are measured first, and if the target is not between them the control is
 * pinned to whichever end gets closest. That is the honest answer when a
 * shot simply cannot be pushed far enough — half a match beats a wrong one,
 * and the caller reports how close it got.
 */
/* A control's OWN range, out of the develop table.
 *
 * ⚠ Hardcoding these is how the first version of this failed. `temp` is not
 * a -100..100 slider, it is KELVIN (2000..12000, neutral near 6500) — so
 * every `set` the fit made was refused as out of range, the fit returned
 * without touching it, and tint was left to do all of the colour work and
 * pinned at its limit. The table is the only thing that knows, and it already
 * knew. */
static int range_of(const char *key, double *lo, double *hi)
{
    ss_develop_info info;
    int i;
    /* ⚠ `== 0`, and it matters: ss_develop_describe returns ZERO on success
     * and -1 past the end, while ss_clip_describe two files away returns 1 on
     * success and 0 past the end. Written as a plain truth test this loop
     * stopped on the FIRST row, every lookup missed, every control was left
     * where it was, and the matcher reported a perfectly successful match
     * that had changed nothing. */
    for (i = 0; ss_develop_describe(i, &info) == 0; i++)
        if (!strcmp(info.key, key)) {
            /* ⚠ The UI range, not the accept range. `temp` ACCEPTS 0 to
             * 50000 — 0 meaning "as shot" and the top being a number no
             * photograph has ever been lit by — while the range a person can
             * actually set is 2000..12000 Kelvin. Bisecting the accept range
             * walks straight out of the sensible part of the control and
             * pins there. */
            *lo = info.ui_lo; *hi = info.ui_hi;
            return 0;
        }
    return -1;
}

static void fit_one(const ss_image *src, ss_image *work, ss_develop *d,
                    const char *key, int which, double target)
{
    double a, b, ga, gb, m, gm;
    char buf[64];
    int i;

    if (range_of(key, &a, &b) != 0) return;

    snprintf(buf, sizeof buf, "%.6f", a);
    if (ss_develop_set(d, key, buf) != 0) return;
    ga = measure(src, work, d, which) - target;

    snprintf(buf, sizeof buf, "%.6f", b);
    ss_develop_set(d, key, buf);
    gb = measure(src, work, d, which) - target;

    if (ga * gb > 0) {
        double end = fabs(ga) < fabs(gb) ? a : b;
        snprintf(buf, sizeof buf, "%.6f", end);
        ss_develop_set(d, key, buf);
        return;
    }

    for (i = 0; i < 22; i++) {
        m = (a + b) / 2.0;
        snprintf(buf, sizeof buf, "%.6f", m);
        ss_develop_set(d, key, buf);
        gm = measure(src, work, d, which) - target;
        if (ga * gm <= 0) { b = m; gb = gm; }
        else              { a = m; ga = gm; }
    }
    m = (a + b) / 2.0;
    snprintf(buf, sizeof buf, "%.6f", m);
    ss_develop_set(d, key, buf);
}

int ss_shot_match(const ss_image *ref, const ss_image *tgt, ss_develop *d,
                  ss_match_report *rep)
{
    ss_look_stats want, got;
    ss_image work;
    int pass;

    if (!ref || !tgt || !d) return -1;
    if (ss_image_init(&work, tgt->w, tgt->h) != 0) return -1;

    stats_of(ref, &want);

    /* Two passes, in this order, and the order is the point.
     *
     * Exposure moves every other measurement — a brighter picture has a wider
     * spread and its channel ratios move as the shot climbs off the toe of
     * the curve — so it is fitted first and then AGAIN after the rest, which
     * is what makes the second pass worth its renders. White balance before
     * contrast, because contrast around a pivot barely moves a ratio while a
     * cast very much moves a spread. */
    for (pass = 0; pass < 3; pass++) {
        /* Colour first, and it barely moves afterwards: both ratios are
         * brightness-invariant by construction, so neither the exposure nor
         * the contrast that follow can undo them.
         *
         * Then exposure and contrast, ALTERNATING — those two genuinely fight
         * each other. Contrast pivots around mid grey, so changing it moves
         * the mean; exposure moves the mean, which moves how much of the
         * picture sits on the flat parts of the curve, which moves the
         * spread. Three rounds settles both to well under a code value. */
        fit_one(tgt, &work, d, "temp",     2, want.rb);
        fit_one(tgt, &work, d, "tint",     3, want.gm);
        fit_one(tgt, &work, d, "exposure", 0, want.luma);
        fit_one(tgt, &work, d, "contrast", 1, want.spread);
        fit_one(tgt, &work, d, "exposure", 0, want.luma);
    }

    /* What it actually achieved, measured the same way, so the caller can
     * print the distance rather than a promise. */
    memcpy(work.px, tgt->px, (size_t)tgt->w * tgt->h * 4 * sizeof(float));
    {
        ss_edit e;
        memset(&e, 0, sizeof e);
        e.dev = *d;
        ss_edit_apply(&work, &e);
        stats_of(&work, &got);
    }
    if (rep) {
        rep->want_luma = want.luma;     rep->got_luma = got.luma;
        rep->want_spread = want.spread; rep->got_spread = got.spread;
        rep->want_rb = want.rb;         rep->got_rb = got.rb;
        rep->want_gm = want.gm;         rep->got_gm = got.gm;
    }
    ss_image_free(&work);
    return 0;
}
