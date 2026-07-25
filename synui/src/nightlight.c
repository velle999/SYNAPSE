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
 * struct wlr_color_transform, which the scene combines with whatever a
 * gamma-control client has set and then either programs into the CRTC LUT or
 * folds into the render pass — the caller no longer chooses. So the warmth is
 * no longer committed per output at toggle time; it is one process-wide
 * transform handed to every scene commit, and a change takes effect by
 * scheduling a frame. One transform for every output is right here: the ramp
 * depends only on the temperature, and the LUT dimension is ours to pick now
 * that it is not the connector's hardware gamma size.
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

/* The ramp, as a colour transform. NULL means identity — night light off — and
 * every commit path treats NULL as "supply no transform", so the feature costs
 * nothing at all while it is off.
 *
 * 1024 entries: the transform is resampled by whatever consumes it (CRTC LUT or
 * shader), so this is a source resolution rather than a hardware size, and 1024
 * is past the point where banding is visible on a 10-bit link.
 */
#define NIGHTLIGHT_LUT_DIM 1024

static struct wlr_color_transform *g_nightlight_tf;
static int g_nightlight_temp;   /* what g_nightlight_tf was built for */

static void nightlight_rebuild(int temp)
{
    if (g_nightlight_tf && temp == g_nightlight_temp) return;

    if (g_nightlight_tf) {
        wlr_color_transform_unref(g_nightlight_tf);
        g_nightlight_tf = NULL;
    }
    g_nightlight_temp = temp;
    if (temp <= 0) return;      /* identity: leave the transform NULL */

    size_t dim = NIGHTLIGHT_LUT_DIM;
    uint16_t *lut = calloc(3 * dim, sizeof(uint16_t));
    if (!lut) return;
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

    g_nightlight_tf = wlr_color_transform_init_lut_3x1d(dim, r, g, b);
    free(lut);
    if (!g_nightlight_tf)
        wlr_log(WLR_INFO, "synui: nightlight: could not build the %dK transform",
                temp);
}

/*
 * What the commit paths ask for. Borrowed, not owned — the caller must not
 * unref it; it lives until the temperature changes.
 */
struct wlr_color_transform *nightlight_color_transform(syn_server_t *s)
{
    int temp = s->config.night_light ? s->config.night_light_temp : 0;
    nightlight_rebuild(temp);
    return g_nightlight_tf;
}

void nightlight_apply(syn_server_t *s)
{
    int temp = s->config.night_light ? s->config.night_light_temp : 0;
    nightlight_rebuild(temp);

    /* Nothing is committed here any more: the transform is picked up by the
     * next scene commit, so every output just needs a frame to be scheduled. */
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
