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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

/* Write one output's LUT. temp <= 0 means identity (night light off). */
static void nightlight_apply_output(syn_output_t *o, int temp)
{
    struct wlr_output *wlr_out = o->wlr_output;
    size_t size = wlr_output_get_gamma_size(wlr_out);
    if (size == 0) {
        /* Backend has no gamma (headless/pixman, and some virtual outputs).
         * Not an error — there is simply nothing to warm. */
        return;
    }

    uint16_t *lut = calloc(3 * size, sizeof(uint16_t));
    if (!lut) return;

    uint16_t *r = lut, *g = lut + size, *b = lut + 2 * size;

    double rgb[3] = { 1.0, 1.0, 1.0 };
    if (temp > 0) temp_to_rgb(temp, rgb);

    for (size_t i = 0; i < size; i++) {
        /* The ramp is linear and then scaled per channel: identity is
         * val = i/(size-1), and a warm white is that with green and blue pulled
         * down. Scaling the *ramp* rather than applying a curve keeps the
         * response linear, which is what makes this look like a colour shift
         * instead of a contrast change. */
        double val = (double)i / (double)(size - 1);
        r[i] = (uint16_t)(val * rgb[0] * UINT16_MAX);
        g[i] = (uint16_t)(val * rgb[1] * UINT16_MAX);
        b[i] = (uint16_t)(val * rgb[2] * UINT16_MAX);
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_gamma_lut(&state, size, r, g, b);
    if (!wlr_output_commit_state(wlr_out, &state))
        wlr_log(WLR_INFO, "synui: nightlight: gamma commit failed for %s",
                wlr_out->name);
    wlr_output_state_finish(&state);
    free(lut);
}

void nightlight_apply(syn_server_t *s)
{
    int temp = s->config.night_light ? s->config.night_light_temp : 0;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        nightlight_apply_output(o, temp);

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
    nightlight_apply_output(o, s->config.night_light_temp);
}
