/*
 * edid.h — what the monitor says about itself
 *
 * Deliberately free of wlroots/scenefx/synui types so the parser can be
 * linked and tested on its own (tests/edid_hdr_test.c).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#ifndef SYNUI_EDID_H
#define SYNUI_EDID_H

#include <stddef.h>

/*
 * The HDR half of an EDID: which electro-optical transfer functions the panel
 * implements, whether it claims a wide gamut, and how bright it wants content
 * to be. All-zero means "SDR panel", "no CTA-861 extension", or "no EDID" —
 * the display panel treats those the same way.
 *
 * This is what the *monitor* advertises. Whether the compositor does anything
 * with it is a separate question, and today the answer is no.
 */
typedef struct {
    int   pq;         /* SMPTE ST 2084 — i.e. HDR10 */
    int   hlg;        /* Hybrid Log-Gamma, Rec. BT.2100 */
    int   bt2020;     /* BT.2020 colorimetry advertised (RGB, YCC or cYCC) */
    float max_nits;   /* desired content max luminance, 0.0 if unset */
} syn_edid_hdr_t;

/*
 * Parse a raw EDID blob. `out` is zeroed first, so a monitor with no CTA
 * extension leaves it all-zero rather than untouched.
 *
 * Returns 1 if the blob parsed as an EDID (whether or not it mentions HDR),
 * 0 if it is too short or malformed.
 */
int edid_hdr_parse(const void *data, size_t len, syn_edid_hdr_t *out);

/*
 * Read a connector's EDID out of sysfs by its DRM name ("DP-3", "HDMI-A-1")
 * and parse it. `out` is zeroed first.
 *
 * Returns 1 on success, 0 when the connector has no EDID there — which is the
 * normal case on the headless/nested backends, not an error.
 */
int edid_hdr_probe_connector(const char *conn_name, syn_edid_hdr_t *out);

#endif /* SYNUI_EDID_H */
