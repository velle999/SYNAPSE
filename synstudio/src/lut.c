/* lut.c — the bridge from a photo grade to a video clip.
 *
 * A 3D LUT is a lattice of input colours with an output colour at each node;
 * everything between is trilinearly interpolated by whoever applies it. Since
 * the pointwise half of a develop stack is by definition a function of colour
 * alone, sampling it on a lattice reproduces it exactly at the nodes and to
 * within interpolation error everywhere else. That is the whole trick: the
 * colour maths lives once, in colour.c, and video gets it through
 * ffmpeg's lut3d filter instead of a second implementation that would drift.
 *
 * The LUT domain is DISPLAY-ENCODED, not linear, because that is what a .cube
 * means to every tool that reads one and what ffmpeg will be feeding it.
 * Each node is therefore linearised on the way in and re-encoded on the way
 * out, wrapping the same ss_pixel_pointwise the still path calls.
 *
 * ⚠ Two real limits, and they are properties of the FORMAT, not of this code:
 *
 *  - The domain is [0,1]. A grade that lifts exposure pushes values past 1
 *    and the LUT cannot express where they went, so they flatten at white.
 *    Big exposure moves belong on the clip as an eq/exposure filter, which is
 *    why timeline.c emits large exposure separately instead of baking it.
 *  - Interpolation between nodes is linear, so a steep curve loses a little
 *    of its knee. 33 is the size everything in the industry uses and is the
 *    default here; 65 costs 8x the file for a difference nobody has seen.
 */
#include "synstudio.h"

#include <stdio.h>

int ss_lut_write(const ss_develop *d, int size, FILE *fp, const char *title)
{
    int r, g, b;

    if (size < 2 || size > 129) return -1;

    if (title && *title) fprintf(fp, "TITLE \"%s\"\n", title);
    fprintf(fp, "LUT_3D_SIZE %d\n", size);
    fprintf(fp, "DOMAIN_MIN 0.0 0.0 0.0\n");
    fprintf(fp, "DOMAIN_MAX 1.0 1.0 1.0\n\n");

    /* .cube ordering: red varies FASTEST, then green, then blue. Getting this
     * backwards produces a LUT that loads without complaint and swaps the red
     * and blue channels of the picture. */
    for (b = 0; b < size; b++)
        for (g = 0; g < size; g++)
            for (r = 0; r < size; r++) {
                float in[3], out[3];
                in[0] = ss_srgb_to_linear((float)r / (size - 1));
                in[1] = ss_srgb_to_linear((float)g / (size - 1));
                in[2] = ss_srgb_to_linear((float)b / (size - 1));

                ss_pixel_pointwise(d, in, out);

                fprintf(fp, "%.6f %.6f %.6f\n",
                        ss_clampf(ss_linear_to_srgb(out[0]), 0.0f, 1.0f),
                        ss_clampf(ss_linear_to_srgb(out[1]), 0.0f, 1.0f),
                        ss_clampf(ss_linear_to_srgb(out[2]), 0.0f, 1.0f));
            }

    return ferror(fp) ? -1 : 0;
}
