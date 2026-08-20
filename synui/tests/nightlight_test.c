/*
 * nightlight_test.c — the LUT is the size the HARDWARE asked for
 *
 * THE BUG THIS PINS. nightlight.c built one process-wide 1024-entry colour
 * transform and gave it to every output, on the stated reasoning that a colour
 * transform is resampled by whatever consumes it, so the dimension was ours to
 * pick. It is not resampled. wlroots hands the LUT to the kernel at exactly the
 * length it was built:
 *
 *   atomic   create_gamma_lut_blob() writes `dim` drm_color_lut entries and the
 *            driver's atomic_check compares that against GAMMA_LUT_SIZE —
 *            i915's check_lut_size() demands an exact match.
 *   legacy   drmModeCrtcSetGamma(), which the atomic path falls back to when
 *            the CRTC has no GAMMA_LUT property. drm_mode_gamma_set_ioctl
 *            rejects any size that is not crtc->gamma_size.
 *
 * 1024 was a guess that happened to be right on the machine it was written on:
 * the development desktop's NVIDIA CRTCs report GAMMA_LUT_SIZE=1024, measured.
 * An Intel laptop panel reports 256 or 257 and an AMD one 4096 — the test
 * commit is refused, the frame goes out without the transform, and the screen
 * never warms. "Night light works on the desktop and does nothing on the
 * laptop" was that, and nothing else.
 *
 * wlr_output_get_gamma_size() is stubbed here rather than mocked around: it is
 * the one input that decides the whole thing, and the DRM backend is not
 * reachable from a test — a headless output has no gamma at all, so it would
 * exercise only the fallback, which is the branch that was never wrong.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/render/color.h>
#include <wlr/types/wlr_output.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* ── the stubbed world ─────────────────────────────────────────────────────
 *
 * The executable's definition wins over the shared library's for both of
 * these, which is what lets the test drive the one input that matters. */
static size_t stub_gamma_size;
size_t wlr_output_get_gamma_size(struct wlr_output *output)
{
    (void)output;
    return stub_gamma_size;
}

/* nightlight_apply() schedules a frame on every output; there are none here. */
static int scheduled;
void wlr_output_schedule_frame(struct wlr_output *output)
{
    (void)output;
    scheduled++;
}

/* Two distinct non-NULL addresses. Nothing dereferences them — the stub above
 * is the only thing that ever looks at an output — but they have to differ so
 * "these are two different screens" is a real statement. */
static struct wlr_output panel, external;

int main(void)
{
    syn_server_t s = {0};
    wl_list_init(&s.outputs);
    s.config.night_light = true;
    s.config.night_light_temp = 3400;

    /* ── the length follows the hardware ─────────────────────────────── */
    /*
     * Every one of these is a real reported GAMMA_LUT_SIZE: 256 on Intel
     * gen ≤ 8, 257 on the gen 9/10 split-gamma path, 1024 on the NVIDIA
     * desktop this was written on, 4096 on amdgpu.
     */
    const size_t sizes[] = { 256, 257, 1024, 4096 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        stub_gamma_size = sizes[i];
        CHECK(nightlight_lut_dim(&panel) == sizes[i],
              "gamma size %zu produced a LUT of %zu", sizes[i],
              nightlight_lut_dim(&panel));
    }

    /* 0 is "this backend has no gamma" — a nested or headless output, or a DRM
     * connector with no CRTC yet. The fallback keeps those behaving as they
     * did; it must NEVER stand in for a size the backend actually reported,
     * which is the whole bug. */
    stub_gamma_size = 0;
    CHECK(nightlight_lut_dim(&panel) == 1024,
          "no reported gamma size did not fall back to 1024, got %zu",
          nightlight_lut_dim(&panel));

    /* ── the transform tracks the temperature ────────────────────────── */
    stub_gamma_size = 256;
    struct wlr_color_transform *warm = nightlight_color_transform(&s, &panel);
    CHECK(warm != NULL, "night light on produced no transform");

    /* Warm means blue comes down and red does not. Checked through the public
     * eval rather than by reaching into the LUT, which is opaque. */
    if (warm) {
        float out[3], in[3] = { 1.0f, 1.0f, 1.0f };
        wlr_color_transform_eval(warm, out, in);
        CHECK(out[2] < out[0], "3400K did not pull blue below red (r=%f b=%f)",
              (double)out[0], (double)out[2]);
        CHECK(out[0] > 0.9f, "3400K dimmed red to %f — this is a colour shift, "
              "not a brightness one", (double)out[0]);
    }

    /* Off is identity, and identity is NULL — a transform that is committed as
     * NULL is how the screen gets its colour back. */
    s.config.night_light = false;
    CHECK(nightlight_color_transform(&s, &panel) == NULL,
          "night light off still produced a transform");
    CHECK(nightlight_effective_temp(&s) == 0,
          "night light off did not read as identity");
    s.config.night_light = true;

    /* ── cached, or this rebuilds at the refresh rate ─────────────────── */
    /*
     * Both commit paths call nightlight_color_transform() on every frame they
     * take, not only when something changed. Rebuilding a LUT there would be
     * an allocation and a few thousand pow() calls per frame per output.
     */
    stub_gamma_size = 256;
    struct wlr_color_transform *a = nightlight_color_transform(&s, &panel);
    struct wlr_color_transform *b = nightlight_color_transform(&s, &panel);
    CHECK(a == b, "the same output and temperature rebuilt the transform");

    /* ── two sizes live at once ───────────────────────────────────────── */
    /*
     * A hybrid laptop drives its panel from one GPU and the external screen
     * from the other, and the two CRTCs need not agree. A single cached
     * transform would be right for one screen and refused by the other — which
     * is the original bug wearing a second coat.
     */
    stub_gamma_size = 4096;
    struct wlr_color_transform *big = nightlight_color_transform(&s, &external);
    CHECK(big != NULL && big != a,
          "a second LUT size did not get its own transform");
    stub_gamma_size = 256;
    CHECK(nightlight_color_transform(&s, &panel) == a,
          "asking for the second size evicted the first");

    /* ── a temperature change invalidates everything ──────────────────── */
    /*
     * Asserted on what the transform DOES, not on its address. A temperature
     * change frees the cache and builds again, and malloc is entitled to hand
     * back the block it was just given — so `new != old` is a coin toss dressed
     * up as a test. 2700K has to be warmer than 3400K, and that is checkable.
     */
    float o27[3], o34[3], white[3] = { 1.0f, 1.0f, 1.0f };
    s.config.night_light_temp = 2700;
    stub_gamma_size = 256;
    struct wlr_color_transform *cooler = nightlight_color_transform(&s, &panel);
    CHECK(cooler != NULL, "2700K produced no transform");

    s.config.night_light_temp = 3400;
    struct wlr_color_transform *t34 = nightlight_color_transform(&s, &panel);
    CHECK(t34 != NULL, "3400K produced no transform");

    if (cooler && t34) {
        /* ⚠ Both evals happen AFTER both builds. Evaluating `cooler` before
         * asking for 3400K would read a transform the second call has already
         * freed — the flush is what makes the cache correct, and it is also
         * what makes a borrowed pointer stale the moment the temperature
         * moves. Which is exactly the contract the header states. */
        wlr_color_transform_eval(t34, o34, white);
        s.config.night_light_temp = 2700;
        cooler = nightlight_color_transform(&s, &panel);
        wlr_color_transform_eval(cooler, o27, white);
        CHECK(o27[2] < o34[2],
              "2700K was not warmer than 3400K (b=%f vs %f)",
              (double)o27[2], (double)o34[2]);
    }

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("nightlight: all checks passed\n");
    return 0;
}
