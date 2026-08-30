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
 * ⚠ AND IT REPORTS THE PIECES SEPARATELY, because they fail separately.
 * Signalling HDR10 (the image description) is a different capability from
 * mapping SDR into it (the colour transform), and that is different again from
 * getting back OUT to plain sRGB — a machine that takes the first and refuses
 * the second shows a washed-out desktop rather than an error, and one that
 * refuses the third has a mode it cannot leave. Knowing which half works is the
 * entire point of asking.
 *
 * ⚠ WHAT THE MODE ITSELF WOULD COMMIT IS ASKED BY hdr.c, not here. This file
 * carried its own copy of the ST 2084 curve to ask about it, which is two
 * implementations of one thing; the columns pq_lut/sdr/primaries/capable are
 * hdr_probe()'s answers now, re-asked live on every report.
 *
 * ⛔ AND IT REPORTS WHAT THE OUTPUT ADVERTISES, NOT ONLY WHAT IT ANSWERED.
 * sup_prim/sup_tf are wlr_output.supported_primaries and
 * supported_transfer_functions verbatim. They are here because pkgrel 548
 * shipped an HDR mode no machine could enter, and every derived column was
 * telling the truth about it: pq=yes, image_description=yes, pq_lut=yes,
 * sdr=no. The one fact that explained all of them — that wlroots advertises
 * BT.2020 and PQ and NOTHING else, so any sRGB description is refused before
 * the panel is asked — was the one fact not on the line. A capability report
 * that omits the inputs can only be read by whoever already knows the answer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#define _GNU_SOURCE
#include "synui.h"

#include <stdio.h>

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

/* ── the shape the hardware actually takes ──────────────────────────────────
 *
 * ⛔ THE CRTC IS NOT A GENERAL COLOUR ENGINE. It has a gamma LUT, and that is
 * one curve per channel — which is precisely `wlr_color_transform_init_lut_3x1d`
 * and precisely what nightlight already programs through this same path every
 * evening. Handed anything else (an inverse-EOTF object, a 3x3 matrix, a
 * pipeline of them) the DRM backend cannot express it in hardware, falls back
 * to the renderer, and the renderer is fx_renderer, which refuses — so the
 * whole commit fails validation. The three columns below are the measurement
 * that established that, kept because it is the reason the mode is shaped the
 * way it is and not a fact anyone should have to rediscover.
 *
 * The consequence is the useful one: a transfer function is per-channel, so
 * inverse-PQ CAN be baked into a 1D LUT. Primaries cannot — a matrix mixes
 * channels and no per-channel curve does that.
 *
 * ⚠ THE PQ CURVE, THE IMAGE DESCRIPTION AND THE WAY BACK TO sRGB ARE NOT ASKED
 * HERE ANY MORE. They are hdr.c's, and this file used to carry its own copy of
 * the ST 2084 arithmetic to ask about them — two implementations of the same
 * curve, which is two answers waiting to disagree. hdr_probe() asks the
 * connector for exactly what the mode will commit, and this reports what it
 * found.
 */

/* One test commit, reported. `st` is consumed. */
static bool try_state(struct wlr_output *o, struct wlr_output_state *st)
{
    bool ok = wlr_output_test_state(o, st);
    wlr_output_state_finish(st);
    return ok;
}

/* A colour transform on the output state, which is where fx_renderer is
 * bypassed. Reported for each shape separately — see the header: they fail
 * separately, and which one failed is the whole finding. */
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

        /* Asked live rather than reported from what was cached at output
         * creation: a mode change or a different cable moves these answers, and
         * a diagnostic that reports a stale yes is worse than no diagnostic. */
        hdr_probe(s, o);

        bool curve = false, matrix = false, pipeline = false;
        probe_transforms(w, &curve, &matrix, &pipeline);

        const char *prim = o->hdr_primaries == WLR_COLOR_NAMED_PRIMARIES_SRGB
                             ? "srgb"
                         : o->hdr_primaries == WLR_COLOR_NAMED_PRIMARIES_BT2020
                             ? "bt2020" : "none";

        snprintf(line, sizeof line,
                 "%s\tpq=%s\tbt2020=%s\timage_description=%s\t"
                 "tf_curve=%s\tmatrix=%s\tpipeline=%s\tpq_lut=%s\t"
                 "sdr=%s\tprimaries=%s\tsup_prim=0x%x\tsup_tf=0x%x\t"
                 "capable=%s\ton=%s",
                 w->name, hyn(tf_pq), hyn(prim_2020),
                 hyn(o->hdr_primaries != 0),
                 hyn(curve), hyn(matrix), hyn(pipeline), hyn(o->hdr_lut_ok),
                 hyn(o->hdr_sdr_ok), prim,
                 w->supported_primaries, w->supported_transfer_functions,
                 hyn(o->hdr_capable), hyn(o->hdr_on));
        emit(ctx, line);
    }
}
