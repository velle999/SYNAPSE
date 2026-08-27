/*
 * barscan_format_test.c — barscan reads the right BYTE for each channel.
 *
 * A wrong channel index here does not fail. It reports the wrong colour, and
 * every consumer of the scan then inks itself confidently against it — the bar,
 * the start menu, the mixer, the OSD. An unhandled format is quieter still:
 * leaf_lum_run() declines, the cell stays -1, and the consumer falls back to
 * the wallpaper, which is a perfectly good-looking answer to the wrong
 * question. Measured 2026-08-26, a 2560x1440 DP output declined EVERY read
 * with 0x34324742 (BGR888) and nobody noticed, because the fallback looked
 * fine.
 *
 * So this asserts the mapping against drm_fourcc.h's own bit layout, built up
 * from the definition rather than copied from the switch it is checking:
 *
 *   "[23:0] B:G:R little endian" means B occupies bits 23:16. Little endian
 *   puts the LOW byte first, so bits 7:0 (R) are byte 0 and bits 23:16 (B) are
 *   byte 2 — the reverse of the order the format's NAME reads in.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <drm_fourcc.h>

#include "synui.h"

int barscan_pixel_layout(uint32_t fmt, int *bpp,
                         int *ri, int *gi, int *bi, int *ai);

/* ── The compositor half ───────────────────────────────────
 * contrast.c is linked for real (the colour maths is the point of the file it
 * serves); only the scan's two outside sources are stubbed. Nothing below is
 * reached — the layout table is pure — but the linker wants them. */
const double *wallpaper_lum_grid(const syn_output_t *o) { (void)o; return 0; }
double wallpaper_strip_lum(const syn_output_t *o) { (void)o; return -1.0; }
void wallpaper_backdrop_republish(syn_server_t *s) { (void)s; }
int game_owns_output(syn_server_t *s, syn_output_t *o) { (void)s; (void)o; return 0; }

static int fails;

/* Build a pixel the way the FORMAT says to, then ask barscan to read it back.
 * `shifts` are the bit positions drm_fourcc documents for R, G and B. */
static void layout(const char *name, uint32_t fmt, int want_bpp,
                   int r_shift, int g_shift, int b_shift, int want_ai)
{
    int bpp, ri, gi, bi, ai;
    if (!barscan_pixel_layout(fmt, &bpp, &ri, &gi, &bi, &ai)) {
        printf("  FAIL  %s — declined, expected to be handled\n", name);
        fails++;
        return;
    }

    /* Three channels that cannot be confused with one another. */
    const unsigned R = 0x11, G = 0x22, B = 0x33;
    uint32_t packed = (R << r_shift) | (G << g_shift) | (B << b_shift);

    unsigned char px[4] = { 0 };
    for (int i = 0; i < want_bpp; i++)          /* little endian: low byte first */
        px[i] = (unsigned char)((packed >> (8 * i)) & 0xff);

    int bad = 0;
    if (bpp != want_bpp)  { printf("  FAIL  %s — bpp %d, expected %d\n", name, bpp, want_bpp); bad = 1; }
    if (ai != want_ai)    { printf("  FAIL  %s — ai %d, expected %d\n",  name, ai,  want_ai);  bad = 1; }
    if (!bad && (px[ri] != R || px[gi] != G || px[bi] != B)) {
        printf("  FAIL  %s — read R=%02x G=%02x B=%02x, expected %02x/%02x/%02x"
               "  (bytes %02x %02x %02x %02x, idx r=%d g=%d b=%d)\n",
               name, px[ri], px[gi], px[bi], R, G, B,
               px[0], px[1], px[2], px[3], ri, gi, bi);
        bad = 1;
    }
    if (bad) { fails++; return; }
    printf("  ok    %s  bpp=%d  r=byte%d g=byte%d b=byte%d ai=%d\n",
           name, bpp, ri, gi, bi, ai);
}

static void declined(const char *name, uint32_t fmt)
{
    int bpp, ri, gi, bi, ai;
    if (barscan_pixel_layout(fmt, &bpp, &ri, &gi, &bi, &ai)) {
        printf("  FAIL  %s — handled, expected to decline\n", name);
        fails++;
        return;
    }
    printf("  ok    %s declines\n", name);
}

int main(void)
{
    printf("barscan_format_test\n\n THE 32-BIT ORDERS\n");
    /* [31:0] x:R:G:B  ->  R at bit 16, G at 8, B at 0 */
    layout("XRGB8888", DRM_FORMAT_XRGB8888, 4, 16, 8, 0, -1);
    layout("ARGB8888", DRM_FORMAT_ARGB8888, 4, 16, 8, 0,  3);
    /* [31:0] x:B:G:R  ->  B at bit 16, G at 8, R at 0 */
    layout("XBGR8888", DRM_FORMAT_XBGR8888, 4,  0, 8, 16, -1);
    layout("ABGR8888", DRM_FORMAT_ABGR8888, 4,  0, 8, 16,  3);

    printf("\n THE 24-BIT ORDERS  (the ones DP-3 actually asked for)\n");
    /* [23:0] B:G:R  ->  B at bit 16, G at 8, R at 0 */
    layout("BGR888", DRM_FORMAT_BGR888, 3,  0, 8, 16, -1);
    /* [23:0] R:G:B  ->  R at bit 16, G at 8, B at 0 */
    layout("RGB888", DRM_FORMAT_RGB888, 3, 16, 8,  0, -1);

    printf("\n WHAT STILL DECLINES\n");
    declined("RGB565",      DRM_FORMAT_RGB565);
    declined("XRGB2101010", DRM_FORMAT_XRGB2101010);
    declined("NV12",        DRM_FORMAT_NV12);

    printf("\n%s (%d failed)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
