/* curve.c — the point curve.
 *
 * Interpolated with a monotone cubic (Fritsch-Carlson), not a natural spline.
 * A natural spline through hand-placed points OVERSHOOTS: pull one point up
 * and the curve dips below its neighbour on the way there, which shows up in
 * a picture as a dark ring around a highlight and reads as a bug in the app,
 * not as the curve the user drew. Fritsch-Carlson limits the tangents so the
 * result can never be non-monotone between two rising points.
 *
 * Evaluation is through a 1024-entry table, because the curve is applied per
 * channel per pixel and a 24-megapixel export is 72 million evaluations.
 * Inputs outside [0,1] are extrapolated along the end slope rather than
 * clamped: scene values above 1 survive white balance and exposure, and
 * flattening them here would blow every recoverable highlight to a flat patch.
 */
#include "synstudio.h"

#include <math.h>
#include <string.h>

void ss_curve_reset(ss_curve *c)
{
    memset(c, 0, sizeof(*c));
    c->n = 2;
    c->x[0] = 0.0f; c->y[0] = 0.0f;
    c->x[1] = 1.0f; c->y[1] = 1.0f;
    c->identity = 1;
    c->built = 0;
}

int ss_curve_add(ss_curve *c, float x, float y)
{
    int i, j;

    if (c->n >= SS_CURVE_MAX_PTS) return -1;
    x = ss_clampf(x, 0.0f, 1.0f);

    /* Replace a point at the same x rather than creating a vertical segment,
     * which would make the secant infinite and the tangent limiter divide by
     * zero. */
    for (i = 0; i < c->n; i++) {
        if (fabsf(c->x[i] - x) < 1e-4f) { c->y[i] = y; c->built = 0; goto done; }
    }
    for (i = 0; i < c->n && c->x[i] < x; i++) ;
    for (j = c->n; j > i; j--) { c->x[j] = c->x[j-1]; c->y[j] = c->y[j-1]; }
    c->x[i] = x; c->y[i] = y;
    c->n++;
    c->built = 0;
done:
    c->identity = 0;
    return 0;
}

void ss_curve_build(ss_curve *c)
{
    float dlt[SS_CURVE_MAX_PTS], m[SS_CURVE_MAX_PTS];
    int i, n = c->n, seg;

    if (c->built) return;
    if (n < 2) { ss_curve_reset(c); }
    n = c->n;

    for (i = 0; i < n - 1; i++) {
        float dx = c->x[i+1] - c->x[i];
        dlt[i] = (dx > 1e-6f) ? (c->y[i+1] - c->y[i]) / dx : 0.0f;
    }

    m[0] = dlt[0];
    m[n-1] = dlt[n-2];
    for (i = 1; i < n - 1; i++) m[i] = 0.5f * (dlt[i-1] + dlt[i]);

    /* The limiter. A flat secant pins both its tangents to zero, otherwise
     * keep (alpha,beta) inside the circle of radius 3 — Fritsch and Carlson's
     * sufficient condition for monotonicity. */
    for (i = 0; i < n - 1; i++) {
        if (fabsf(dlt[i]) < 1e-9f) { m[i] = 0.0f; m[i+1] = 0.0f; continue; }
        {
            float a = m[i] / dlt[i], b = m[i+1] / dlt[i], s = a * a + b * b;
            if (s > 9.0f) {
                float tau = 3.0f / sqrtf(s);
                m[i]   = tau * a * dlt[i];
                m[i+1] = tau * b * dlt[i];
            }
        }
    }

    seg = 0;
    for (i = 0; i < SS_CURVE_LUT; i++) {
        float t = (float)i / (SS_CURVE_LUT - 1), h, u, u2, u3;
        while (seg < n - 2 && t > c->x[seg+1]) seg++;
        h = c->x[seg+1] - c->x[seg];
        if (h < 1e-6f) { c->lut[i] = c->y[seg]; continue; }
        u = (t - c->x[seg]) / h;
        u2 = u * u; u3 = u2 * u;
        c->lut[i] = (2*u3 - 3*u2 + 1) * c->y[seg]
                  + (u3 - 2*u2 + u)   * h * m[seg]
                  + (-2*u3 + 3*u2)    * c->y[seg+1]
                  + (u3 - u2)         * h * m[seg+1];
    }
    c->built = 1;
}

float ss_curve_eval(const ss_curve *c, float x)
{
    ss_curve *nc = (ss_curve *)c;   /* the table is a cache, not a mutation */
    float f, s;
    int i;

    if (c->identity) return x;
    if (!c->built) ss_curve_build(nc);

    /* Extrapolate along the end slope. See the file comment. */
    if (x <= 0.0f) {
        s = (c->lut[1] - c->lut[0]) * (SS_CURVE_LUT - 1);
        return c->lut[0] + x * s;
    }
    if (x >= 1.0f) {
        s = (c->lut[SS_CURVE_LUT-1] - c->lut[SS_CURVE_LUT-2]) * (SS_CURVE_LUT - 1);
        return c->lut[SS_CURVE_LUT-1] + (x - 1.0f) * s;
    }

    f = x * (SS_CURVE_LUT - 1);
    i = (int)f;
    f -= i;
    if (i >= SS_CURVE_LUT - 1) return c->lut[SS_CURVE_LUT-1];
    return c->lut[i] * (1.0f - f) + c->lut[i+1] * f;
}
