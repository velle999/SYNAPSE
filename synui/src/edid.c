/*
 * edid.c — read the connector's EDID and answer "is this an HDR monitor?"
 *
 * The display panel used to answer that from a framebuffer-format test: if
 * the backend accepted a 10-bit buffer, the monitor was called "HDR-ready".
 * That is not the same question. Every 10-bit-capable GPU plane passes it, so
 * a genuine HDR10 display and the plain SDR panel beside it produced the same
 * answer, and the panel appeared not to know what was plugged into it.
 *
 * The real answer is in the monitor's EDID: the CTA-861 HDR static metadata
 * block lists the transfer functions the panel implements (PQ = HDR10, HLG),
 * and the colorimetry block says whether it covers BT.2020. wlroots parses
 * EDIDs with libdisplay-info but exposes only make/model/serial, so we read
 * the kernel's copy from sysfs and parse it with the same library.
 *
 * Reporting this is not the same as doing it: synui composites 8-bit sRGB
 * through scenefx/GLES2 and never touches the connector's HDR_OUTPUT_METADATA
 * property. See dispcfg.c and the syn_output_t hdr_* fields.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libdisplay-info/cta.h>
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>

#include "edid.h"

/* One base block plus the 255 extensions EDID 1.4 allows. Nothing on a desk
 * ships more than a couple, but the read is bounded by the buffer either way. */
#define EDID_MAX_BYTES (256 * 128)

int edid_hdr_parse(const void *data, size_t len, syn_edid_hdr_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!data || len < 128) return 0;

    struct di_info *info = di_info_parse_edid(data, len);
    if (!info) return 0;

    const struct di_edid *edid = di_info_get_edid(info);
    if (!edid) {
        di_info_destroy(info);
        return 0;
    }

    /* The HDR blocks live in the CTA-861 extension, never in the base block,
     * so a monitor with no extensions is answered by the zeroing above. */
    const struct di_edid_ext *const *exts = di_edid_get_extensions(edid);
    for (; exts && *exts; exts++) {
        const struct di_edid_cta *cta = di_edid_ext_get_cta(*exts);
        if (!cta) continue;   /* DisplayID or another extension type */

        const struct di_cta_data_block *const *blocks =
            di_edid_cta_get_data_blocks(cta);
        for (; blocks && *blocks; blocks++) {
            const struct di_cta_hdr_static_metadata_block *hdr =
                di_cta_data_block_get_hdr_static_metadata(*blocks);
            if (hdr) {
                if (hdr->eotfs) {
                    /* traditional_hdr is left out on purpose: it is a
                     * luminance-range hint with no defined transfer function,
                     * and calling a monitor "HDR" on it alone overstates. */
                    out->pq  = hdr->eotfs->pq  ? 1 : 0;
                    out->hlg = hdr->eotfs->hlg ? 1 : 0;
                }
                out->max_nits = hdr->desired_content_max_luminance;
                continue;
            }

            const struct di_cta_colorimetry_block *cm =
                di_cta_data_block_get_colorimetry(*blocks);
            if (cm && (cm->bt2020_rgb || cm->bt2020_ycc || cm->bt2020_cycc))
                out->bt2020 = 1;
        }
    }

    di_info_destroy(info);
    return 1;
}

/*
 * Find the connector's EDID in sysfs: /sys/class/drm/<card>-<connector>/edid.
 *
 * The card index is not derivable from the wlroots connector name ("DP-3"),
 * and hardcoding card0 is wrong on this machine (the NVIDIA GPU is card1), so
 * scan the directory and match everything after the first '-'.
 */
int edid_hdr_probe_connector(const char *conn_name, syn_edid_hdr_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!conn_name || !*conn_name) return 0;

    DIR *dir = opendir("/sys/class/drm");
    if (!dir) return 0;

    static uint8_t raw[EDID_MAX_BYTES];
    size_t got = 0;
    struct dirent *de;
    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "card", 4) != 0) continue;
        const char *dash = strchr(de->d_name + 4, '-');
        if (!dash || strcmp(dash + 1, conn_name) != 0) continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/class/drm/%s/edid", de->d_name);
        FILE *f = fopen(path, "rbe");
        if (f) {
            got = fread(raw, 1, sizeof(raw), f);
            fclose(f);
        }
        break;
    }
    closedir(dir);

    /* A connector with nothing plugged in has a zero-length edid attribute;
     * that is the common case for a disconnected DP port, not a failure. */
    if (got < 128) return 0;
    return edid_hdr_parse(raw, got, out);
}
