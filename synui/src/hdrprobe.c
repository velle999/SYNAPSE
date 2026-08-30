/*
 * hdrprobe.c — can this machine actually be driven in HDR10, and by whom?
 *
 * ── Why a probe exists before any HDR feature does ───────────────────────────
 *
 * synui composites through scenefx's fx_renderer, which declares
 * `features.output_color_transform = false` and ignores the field. So the
 * obvious route to HDR — hand the scene a colour transform — is closed, and
 * closed SILENTLY: nightlight.c documents discovering exactly that, with no
 * warmth, no error and nothing in the log.
 *
 * The route that is left is the one nightlight uses: put the transform on the
 * OUTPUT STATE, where the DRM backend programs it into CRTC hardware after
 * blending, and tell the display it is receiving PQ/BT.2020 with an image
 * description. Whether the hardware will take that is a question about a
 * particular GPU, a particular kernel and a particular monitor — not something
 * to be answered by reading anybody's documentation.
 *
 * ⛔ SO IT IS ASKED WITH wlr_output_test_state(), WHICH CHANGES NOTHING. A
 * probe that committed the state would put the display into HDR to find out
 * whether it could — and on hardware that half-supports it, that is a black
 * screen on somebody's only monitor. Every commit below is a test commit; the
 * atomic backend validates it against the kernel with TEST_ONLY and reports
 * back, and the screen never changes.
 *
 * ⚠ AND IT TESTS THE PIECES SEPARATELY, because they fail separately. Signalling
 * HDR10 (the image description) is a different capability from mapping SDR into
 * it (the colour transform), and a machine that takes the first and refuses the
 * second would show a washed-out desktop rather than an error. Knowing which
 * half works is the entire point of asking.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#define _GNU_SOURCE
#include "synui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <wlr/render/color.h>
#include <wlr/types/wlr_output.h>

/* sRGB primaries to BT.2020, row-major, D65 with no chromatic adaptation.
 *
 * ⚠ THE MATRIX IS PART OF THE QUESTION. Inverse-PQ alone changes the transfer
 * function and leaves the primaries where they were, so a desktop mapped with
 * the curve and not the matrix comes out over-saturated on a BT.2020 display —
 * correct brightness, wrong colours, and nothing reports it. If the backend
 * takes the curve but not the matrix, that is a finding and not a detail. */
static const float SRGB_TO_BT2020[9] = {
    0.6274f, 0.3293f, 0.0433f,
    0.0691f, 0.9195f, 0.0114f,
    0.0164f, 0.0880f, 0.8956f,
};

/* Named apart from synui.h's own yn(): this file answers a different
 * question and a shadowed helper is a merge conflict waiting to be wrong. */
static const char *hyn(bool v) { return v ? "yes" : "no"; }

/* Defined below; every probe here goes through it so that the one place a
 * state can be committed stays one place. */
static bool try_state(struct wlr_output *o, struct wlr_output_state *st);

/* ── the shape the hardware actually takes ──────────────────────────────────
 *
 * ⛔ THE CRTC IS NOT A GENERAL COLOUR ENGINE. It has a gamma LUT, and that is
 * one curve per channel — which is precisely `wlr_color_transform_init_lut_3x1d`
 * and precisely what nightlight already programs through this same path every
 * evening. Handed anything else (an inverse-EOTF object, a 3x3 matrix, a
 * pipeline of them) the DRM backend cannot express it in hardware, falls back
 * to the renderer, and the renderer is fx_renderer, which refuses — so the
 * whole commit fails validation.
 *
 * The consequence is the useful one: a transfer function is per-channel, so
 * inverse-PQ CAN be baked into a 1D LUT. Primaries cannot — a matrix mixes
 * channels and no per-channel curve does that. This function asks the only
 * question that is left: does the LUT shape carrying the PQ curve go in?
 */

/* SMPTE ST 2084 inverse EOTF: linear [0,1] (1.0 = 10000 cd/m²) → PQ code. */
static double pq_encode(double linear)
{
    static const double m1 = 2610.0 / 16384.0;
    static const double m2 = 2523.0 / 4096.0 * 128.0;
    static const double c1 = 3424.0 / 4096.0;
    static const double c2 = 2413.0 / 4096.0 * 32.0;
    static const double c3 = 2392.0 / 4096.0 * 32.0;

    if (linear < 0.0) linear = 0.0;
    if (linear > 1.0) linear = 1.0;
    double y = pow(linear, m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

/* sRGB EOTF: encoded [0,1] → linear [0,1]. */
static double srgb_to_linear(double v)
{
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

/* Does the CRTC take an SDR→PQ curve as its gamma LUT?
 *
 * ⚠ SDR WHITE AT 203 cd/m², which is BT.2408's reference level and what every
 * other compositor maps a desktop to. The number matters to how bright the
 * result looks, not to whether the commit is accepted — but inventing a
 * different one would make this probe answer a question no HDR mode would ask.
 */
static bool probe_pq_lut(struct wlr_output *o)
{
    size_t dim = wlr_output_get_gamma_size(o);
    if (dim == 0) return false;

    uint16_t *lut = calloc(3 * dim, sizeof *lut);
    if (!lut) return false;
    uint16_t *r = lut, *g = lut + dim, *b = lut + 2 * dim;

    for (size_t i = 0; i < dim; i++) {
        double enc = (double)i / (double)(dim - 1);
        double lin = srgb_to_linear(enc) * (203.0 / 10000.0);
        double pq = pq_encode(lin);
        uint16_t v = (uint16_t)(pq * UINT16_MAX);
        r[i] = g[i] = b[i] = v;
    }

    struct wlr_color_transform *tf = wlr_color_transform_init_lut_3x1d(dim, r, g, b);
    free(lut);
    if (!tf) return false;

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_color_transform(&st, tf);
    bool ok = try_state(o, &st);
    wlr_color_transform_unref(tf);
    return ok;
}

/* One test commit, reported. `st` is consumed. */
static bool try_state(struct wlr_output *o, struct wlr_output_state *st)
{
    bool ok = wlr_output_test_state(o, st);
    wlr_output_state_finish(st);
    return ok;
}

/* Does the backend accept an image description saying "this is HDR10"? */
static bool probe_image_description(struct wlr_output *o)
{
    struct wlr_output_state st;
    wlr_output_state_init(&st);

    struct wlr_output_image_description desc = {
        .primaries = WLR_COLOR_NAMED_PRIMARIES_BT2020,
        .transfer_function = WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ,
        /* Left unset deliberately: mastering luminance and MaxCLL are optional,
         * and a probe that supplied invented values would be asking a different
         * question from the one a real HDR mode would ask. */
    };
    if (!wlr_output_state_set_image_description(&st, &desc)) {
        wlr_output_state_finish(&st);
        return false;
    }
    return try_state(o, &st);
}

/* …and a colour transform on the output state, which is where fx_renderer is
 * bypassed. Reported for each shape separately — see the header. */
static void probe_transforms(struct wlr_output *o, bool *curve, bool *matrix,
                             bool *pipeline)
{
    *curve = *matrix = *pipeline = false;

    struct wlr_color_transform *tf =
        wlr_color_transform_init_linear_to_inverse_eotf(WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ);
    if (tf) {
        struct wlr_output_state st;
        wlr_output_state_init(&st);
        wlr_output_state_set_color_transform(&st, tf);
        *curve = try_state(o, &st);
    }

    struct wlr_color_transform *mx = wlr_color_transform_init_matrix(SRGB_TO_BT2020);
    if (mx) {
        struct wlr_output_state st;
        wlr_output_state_init(&st);
        wlr_output_state_set_color_transform(&st, mx);
        *matrix = try_state(o, &st);
    }

    /* The real shape an HDR mode would need: primaries, then the curve. */
    if (tf && mx) {
        struct wlr_color_transform *chain[2] = { mx, tf };
        struct wlr_color_transform *pipe = wlr_color_transform_init_pipeline(chain, 2);
        if (pipe) {
            struct wlr_output_state st;
            wlr_output_state_init(&st);
            wlr_output_state_set_color_transform(&st, pipe);
            *pipeline = try_state(o, &st);
            wlr_color_transform_unref(pipe);
        }
    }

    if (tf) wlr_color_transform_unref(tf);
    if (mx) wlr_color_transform_unref(mx);
}

void hdrprobe_report(syn_server_t *s, void (*emit)(void *ctx, const char *line),
                     void *ctx)
{
    char line[512];
    syn_output_t *o;

    wl_list_for_each(o, &s->outputs, link) {
        struct wlr_output *w = o->wlr_output;

        /* What the BACKEND says it can carry. In 0.20 this replaces reading the
         * EDID by hand — see dispcfg.c, which still does the EDID read for the
         * "what is this panel" answer the display panel shows. */
        bool tf_pq = (w->supported_transfer_functions &
                      WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ) != 0;
        bool prim_2020 = (w->supported_primaries &
                          WLR_COLOR_NAMED_PRIMARIES_BT2020) != 0;

        bool desc_ok = probe_image_description(w);
        bool curve = false, matrix = false, pipeline = false;
        probe_transforms(w, &curve, &matrix, &pipeline);
        bool pq_lut = probe_pq_lut(w);

        snprintf(line, sizeof line,
                 "%s\tpq=%s\tbt2020=%s\timage_description=%s\t"
                 "tf_curve=%s\tmatrix=%s\tpipeline=%s\tpq_lut=%s",
                 w->name, hyn(tf_pq), hyn(prim_2020), hyn(desc_ok),
                 hyn(curve), hyn(matrix), hyn(pipeline), hyn(pq_lut));
        emit(ctx, line);
    }
}
