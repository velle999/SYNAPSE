/*
 * nightlight.c — warm the screen at night, natively.
 *
 * output_mgmt.c has carried a comment since it was written saying gamma control
 * exists so "night-light tools (wlsunset, gammastep) set per-output gamma
 * ramps" — and SynapseOS shipped neither, so nothing ever set one. The
 * mechanism was there and unused.
 *
 * synui owns the outputs, so it can write the ramps itself: no second process,
 * no client, no protocol round trip. wlr_gamma_control_manager_v1 stays exported
 * (see output_mgmt.c), so anyone who prefers wlsunset can still install it and
 * take over — last writer wins, which is the honest behaviour for a hardware LUT
 * with two writers, and there is only ever one in practice.
 *
 * wlroots 0.20 dropped wlr_output_state_set_gamma_lut() in favour of
 * struct wlr_color_transform.
 *
 * ⚠ THE LUT DIMENSION IS NOT OURS TO PICK. This file used to say it was — one
 * process-wide 1024-entry transform for every output, on the reasoning that a
 * colour transform is resampled by whatever consumes it and 1024 is past the
 * point where banding shows. It is not resampled. wlroots hands the LUT to the
 * kernel at exactly the length we built it:
 *
 *   atomic   create_gamma_lut_blob() writes `dim` struct drm_color_lut entries
 *            into the GAMMA_LUT blob, and the driver's atomic_check compares
 *            that against GAMMA_LUT_SIZE — i915's check_lut_size() wants an
 *            exact match — so a wrong length fails the TEST commit.
 *   legacy   drmModeCrtcSetGamma(), which the atomic path also falls back to
 *            when the CRTC has no GAMMA_LUT property ("older Intel GPUs that
 *            support gamma but not degamma", per wlroots' own comment). The
 *            kernel's drm_mode_gamma_set_ioctl rejects any size that is not
 *            crtc->gamma_size, full stop.
 *
 * So 1024 was never a free choice, it was a guess — and it happened to be
 * right on the box it was written on. NVIDIA reports GAMMA_LUT_SIZE=1024, so
 * every desktop CRTC took it. An Intel laptop panel reports 256 or 257 and an
 * AMD one 4096: the test fails, the frame is committed without the transform,
 * and the screen never warms. Night light "works on the desktop and does
 * nothing on the laptop" is this and only this.
 *
 * The size therefore comes from wlr_output_get_gamma_size(), per output, and
 * the transforms are cached BY DIMENSION — a laptop with an external monitor
 * on a second GPU legitimately needs two.
 *
 * WHERE THE TRANSFORM GOES MATTERS, and the obvious answer is the wrong one.
 * wlr_scene_output_state_options.color_transform is what a stock-wlroots
 * compositor hands the scene — but the scene only forwards it to the renderer
 * as wlr_buffer_pass_options.color_transform, and scenefx's fx_renderer
 * declares features.output_color_transform = false and ignores the field.
 * Given to the scene, night light is dropped in silence: no warmth, no error,
 * nothing in the log. It has to go on the OUTPUT STATE
 * (wlr_output_state_set_color_transform), where the DRM backend programs it
 * into the CRTC gamma LUT after blending — the same hardware the pre-0.20
 * gamma call drove. See output_frame() in synui_main.c and effects.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/render/color.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Tanner Helland's blackbody approximation, which is what redshift/wlsunset use
 * too. Accurate enough between ~1000K and ~40000K, and this is a thing judged by
 * eye at 3 in the morning, not a colourimeter. Returns 0..1 per channel. */
static void temp_to_rgb(int kelvin, double out[3])
{
    double t = kelvin / 100.0;
    double r, g, b;

    if (t <= 66) {
        r = 255;
        g = 99.4708025861 * log(t) - 161.1195681661;
    } else {
        r = 329.698727446 * pow(t - 60, -0.1332047592);
        g = 288.1221695283 * pow(t - 60, -0.0755148492);
    }

    if (t >= 66)      b = 255;
    else if (t <= 19) b = 0;
    else              b = 138.5177312231 * log(t - 10) - 305.0447927307;

    out[0] = r < 0 ? 0 : (r > 255 ? 1.0 : r / 255.0);
    out[1] = g < 0 ? 0 : (g > 255 ? 1.0 : g / 255.0);
    out[2] = b < 0 ? 0 : (b > 255 ? 1.0 : b / 255.0);
}

/* The ramp, as a colour transform. NULL means identity — night light off.
 *
 * NULL is a value to be COMMITTED, not a reason to skip the call: an output
 * state that leaves the colour transform unset leaves the CRTC LUT holding
 * whatever the last frame put there. Both commit paths must hand NULL to
 * wlr_output_state_set_color_transform() the same as any other transform, or
 * night light turns on and cannot be turned off. effects.c skipped the call on
 * NULL and that is exactly what it did.
 *
 * Used only when the backend cannot say what it wants — a nested or headless
 * output, or a DRM connector that has no CRTC yet. Never used to override a
 * size the hardware DID report; see the header.
 */
#define NIGHTLIGHT_LUT_DIM_FALLBACK 1024

/* Cached, and the cache is not only about the arithmetic.
 *
 * Both commit paths ask for the transform on EVERY frame they take, not only
 * when something changed, and wlroots elides a redundant colour-transform
 * commit by comparing the POINTER (output_compare_state_fields():
 * output->color_transform == state->color_transform). Returning a freshly
 * built transform each time would therefore re-upload the GAMMA_LUT blob to
 * the kernel at the refresh rate while looking identical on screen.
 *
 * One entry per distinct LUT size in use. Two is already generous: it takes a
 * hybrid laptop with the panel on one GPU and an external screen on the other
 * to need a second, and nothing sane needs a third. A miss on a full cache
 * evicts slot 0 rather than failing — a rebuild is some arithmetic and a
 * kilobyte, and a wrong-sized LUT is a screen that does not warm. */
#define NIGHTLIGHT_CACHE 4

static struct {
    size_t dim;
    struct wlr_color_transform *tf;
} g_nightlight[NIGHTLIGHT_CACHE];
static int g_nightlight_temp = -1;   /* what the whole cache was built for */

static void nightlight_flush(void)
{
    for (int i = 0; i < NIGHTLIGHT_CACHE; i++) {
        if (g_nightlight[i].tf)
            wlr_color_transform_unref(g_nightlight[i].tf);
        g_nightlight[i].tf = NULL;
        g_nightlight[i].dim = 0;
    }
}

/* Build (or find) the transform for one temperature at one LUT size.
 * NULL is a legitimate answer: temp <= 0 is identity, night light off. */
static struct wlr_color_transform *nightlight_build(int temp, size_t dim)
{
    if (temp != g_nightlight_temp) {
        nightlight_flush();
        g_nightlight_temp = temp;
    }
    if (temp <= 0) return NULL;     /* identity: no transform at all */
    if (dim < 2) return NULL;       /* a 0- or 1-entry ramp is not a ramp */

    int slot = -1;
    for (int i = 0; i < NIGHTLIGHT_CACHE; i++) {
        if (g_nightlight[i].tf && g_nightlight[i].dim == dim)
            return g_nightlight[i].tf;
        if (!g_nightlight[i].tf && slot < 0) slot = i;
    }
    if (slot < 0) {                 /* full: evict, see the comment above */
        wlr_color_transform_unref(g_nightlight[0].tf);
        g_nightlight[0].tf = NULL;
        g_nightlight[0].dim = 0;
        slot = 0;
    }

    uint16_t *lut = calloc(3 * dim, sizeof(uint16_t));
    if (!lut) return NULL;
    uint16_t *r = lut, *g = lut + dim, *b = lut + 2 * dim;

    double rgb[3] = { 1.0, 1.0, 1.0 };
    temp_to_rgb(temp, rgb);

    for (size_t i = 0; i < dim; i++) {
        /* The ramp is linear and then scaled per channel: identity is
         * val = i/(dim-1), and a warm white is that with green and blue pulled
         * down. Scaling the *ramp* rather than applying a curve keeps the
         * response linear, which is what makes this look like a colour shift
         * instead of a contrast change. */
        double val = (double)i / (double)(dim - 1);
        r[i] = (uint16_t)(val * rgb[0] * UINT16_MAX);
        g[i] = (uint16_t)(val * rgb[1] * UINT16_MAX);
        b[i] = (uint16_t)(val * rgb[2] * UINT16_MAX);
    }

    struct wlr_color_transform *tf = wlr_color_transform_init_lut_3x1d(dim, r, g, b);
    free(lut);
    if (!tf) {
        wlr_log(WLR_INFO, "synui: nightlight: could not build the %dK transform "
                "at %zu entries", temp, dim);
        return NULL;
    }

    g_nightlight[slot].dim = dim;
    g_nightlight[slot].tf  = tf;
    wlr_log(WLR_DEBUG, "synui: nightlight: built %dK at %zu entries", temp, dim);
    return tf;
}

/* What the hardware will accept, which is the only size worth building.
 *
 * 0 back from wlr_output_get_gamma_size() means "no gamma here" — a nested or
 * headless backend, or a DRM connector that has not been given a CRTC yet. The
 * fallback keeps those behaving exactly as before rather than turning night
 * light into a no-op on them, and it is deliberately NOT used when the backend
 * answered: a reported size is the whole point of asking. */
static size_t nightlight_dim_for(struct wlr_output *wo)
{
    size_t dim = wo ? wlr_output_get_gamma_size(wo) : 0;
    return dim ? dim : NIGHTLIGHT_LUT_DIM_FALLBACK;
}

int nightlight_effective_temp(syn_server_t *s)
{
    return s->config.night_light ? s->config.night_light_temp : 0;
}

/*
 * What the commit paths ask for, sized for the output they are about to commit.
 * Borrowed, not owned — the caller must not unref it; it lives until the
 * temperature changes or the cache evicts it.
 *
 * ⚠ Per OUTPUT, and it has to stay that way. The transform's length has to
 * match that connector's CRTC gamma size or the commit is refused, and a
 * process-wide one cannot be right for two GPUs at once.
 */
struct wlr_color_transform *nightlight_color_transform(syn_server_t *s,
                                                       struct wlr_output *wo)
{
    return nightlight_build(nightlight_effective_temp(s),
                            nightlight_dim_for(wo));
}

size_t nightlight_lut_dim(struct wlr_output *wo)
{
    return nightlight_dim_for(wo);
}

void nightlight_apply(syn_server_t *s)
{
    /* Nothing is built here either: the transform is per output now, and each
     * output's commit path asks for its own on the frame below. Building one
     * eagerly would mean guessing which size, which is the bug this file was
     * changed to remove.
     *
     * Nothing is committed here: output_frame() sees that this output's
     * committed temperature no longer matches and puts the new transform on
     * the state it is about to commit anyway. All that is needed is a frame —
     * and it must be scheduled even on a still screen, which is exactly the
     * case output_frame's damage guard has to let through. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);

    wlr_log(WLR_INFO, "synui: nightlight: %s (%dK)",
            s->config.night_light ? "on" : "off",
            s->config.night_light ? s->config.night_light_temp : 6500);
}

void nightlight_toggle(syn_server_t *s)
{
    s->config.night_light = !s->config.night_light;
    nightlight_apply(s);
}

/* A new output starts at identity, so it has to be told — otherwise plugging in
 * a second monitor with night light on leaves you with one warm screen and one
 * blue one, which looks exactly like a broken monitor. */
void nightlight_output_added(syn_server_t *s, syn_output_t *o)
{
    if (!s->config.night_light) return;
    /* The transform is process-wide, so there is nothing to apply — the new
     * output only needs a frame for its first commit to carry it. */
    wlr_output_schedule_frame(o->wlr_output);
}
