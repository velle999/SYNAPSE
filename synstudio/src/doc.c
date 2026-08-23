/* doc.c — the sidecar.
 *
 * synstudio NEVER writes to the file you opened. Every decision lands in
 * "<original>.synstudio" beside it, and the original stays the bytes that came
 * off the camera. That is not caution for its own sake: it is what makes the
 * edit reversible for ever, makes two edits of one photograph possible, and
 * means a crash halfway through an adjustment costs nothing.
 *
 * The format is the same tab-separated text the rest of the suite uses, so it
 * greps, diffs and merges. An unknown key is skipped rather than rejected, so
 * a sidecar written by a later version still opens here minus whatever this
 * build has not learned yet.
 */
#include "synstudio.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void ss_edit_reset(ss_edit *e)
{
    memset(e, 0, sizeof(*e));
    ss_develop_reset(&e->dev);
    ss_thumb_reset(&e->thumb);
}

int ss_edit_apply(ss_image *im, const ss_edit *e)
{
    int i;

    ss_apply_pointwise(im, &e->dev);
    ss_apply_spatial(im, &e->dev);

    /* Masks run AFTER the global stack and BEFORE geometry. After the global
     * stack because a local adjustment is understood as a correction to the
     * picture you are looking at; before geometry because mask coordinates
     * are fractions of the uncropped frame, and cropping first would move
     * every mask. */
    for (i = 0; i < e->nmasks; i++)
        if (ss_apply_mask(im, &e->mask[i]) != 0) return -1;

    return ss_apply_geometry(im, &e->dev);
}

int ss_edit_write(const ss_edit *e, FILE *fp)
{
    int i;

    fprintf(fp, "# synstudio sidecar\n");
    if (ss_develop_write(&e->dev, fp) != 0) return -1;

    for (i = 0; i < e->nmasks; i++) {
        const ss_mask *m = &e->mask[i];
        fprintf(fp, "mask\t%s\n", m->type == SS_MASK_LINEAR ? "linear" : "radial");
        fprintf(fp, "mask.invert\t%d\n", m->invert);
        fprintf(fp, "mask.geom\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
                m->x0, m->y0, m->x1, m->y1, m->feather);
        if (ss_develop_write(&m->dev, fp) != 0) return -1;
        fprintf(fp, "endmask\n");
    }
    /* Last, and only when it is not the default, so every sidecar written
     * before thumbnails existed reads back byte for byte. */
    if (ss_thumb_write(&e->thumb, fp) != 0) return -1;
    return ferror(fp) ? -1 : 0;
}

int ss_edit_read(ss_edit *e, FILE *fp)
{
    char line[2048];
    ss_develop *target;

    ss_edit_reset(e);
    target = &e->dev;

    while (fgets(line, sizeof line, fp)) {
        char *nl = strchr(line, '\n'), *tab;
        if (nl) *nl = '\0';
        if (line[0] == '#' || !line[0]) continue;

        if (!strncmp(line, "mask\t", 5)) {
            if (e->nmasks >= SS_MAX_MASKS) { target = &e->dev; continue; }
            ss_mask_reset(&e->mask[e->nmasks],
                          strcmp(line + 5, "radial") ? SS_MASK_LINEAR : SS_MASK_RADIAL);
            target = &e->mask[e->nmasks].dev;
            e->nmasks++;
            continue;
        }
        if (!strcmp(line, "endmask")) { target = &e->dev; continue; }

        if (!strncmp(line, "mask.invert\t", 12)) {
            if (e->nmasks) e->mask[e->nmasks-1].invert = atoi(line + 12);
            continue;
        }
        if (!strncmp(line, "mask.geom\t", 10)) {
            if (e->nmasks) {
                ss_mask *m = &e->mask[e->nmasks-1];
                sscanf(line + 10, "%f %f %f %f %f",
                       &m->x0, &m->y0, &m->x1, &m->y1, &m->feather);
            }
            continue;
        }

        tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        /* The thumbnail layout, which is about the same photograph and so
         * lives in the same document. ⚠ Its own namespace, so a thumbnail
         * setting can never be mistaken for a develop key — and an unknown
         * `thumb.*` from a later version is skipped like any other unknown
         * key rather than reaching ss_develop_set and being refused. */
        if (!strncmp(line, "thumb.", 6)) {
            char un[512];
            const char *v = tab + 1;
            size_t o = 0, k;
            /* The words came through esc: a caption can contain anything. */
            for (k = 0; v[k] && o + 1 < sizeof un; k++) {
                if (v[k] == '\\' && v[k + 1]) {
                    k++;
                    un[o++] = v[k] == 'n' ? '\n' : v[k] == 't' ? '\t' : v[k];
                } else {
                    un[o++] = v[k];
                }
            }
            un[o] = '\0';
            ss_thumb_set(&e->thumb, line + 6, un);
            continue;
        }
        ss_develop_set(target, line, tab + 1);
    }
    return 0;
}

void ss_sidecar_path(const char *img, char *out, size_t n)
{
    snprintf(out, n, "%s.synstudio", img);
}

int ss_edit_load(ss_edit *e, const char *path)
{
    FILE *fp = fopen(path, "r");

    ss_edit_reset(e);
    /* A photograph with no sidecar is not an error, it is an unedited
     * photograph — which is the state of almost every file the first time it
     * is opened. */
    if (!fp) return (errno == ENOENT) ? 0 : -1;
    ss_edit_read(e, fp);
    fclose(fp);
    return 0;
}

/* Write to a temporary and rename. A sidecar half-written by a process that
 * died is worse than no sidecar: it loads, it is missing the last half of the
 * adjustments, and nothing says so. */
int ss_edit_save(const ss_edit *e, const char *path)
{
    char tmp[4200];
    FILE *fp;
    int rc;

    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    fp = fopen(tmp, "w");
    if (!fp) return -1;
    rc = ss_edit_write(e, fp);
    if (fflush(fp) != 0) rc = -1;
    if (rc == 0 && fsync(fileno(fp)) != 0) rc = -1;
    if (fclose(fp) != 0) rc = -1;
    if (rc != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ------------------------------------------------------------ histogram -- */

void ss_histogram_of(const ss_image *im, ss_histogram *h)
{
    long i, n = (long)im->w * im->h;

    memset(h, 0, sizeof(*h));
    for (i = 0; i < n; i++) {
        const float *p = im->px + i * 4;
        float v[4];
        int c, b;

        v[0] = ss_linear_to_srgb(p[0]);
        v[1] = ss_linear_to_srgb(p[1]);
        v[2] = ss_linear_to_srgb(p[2]);
        v[3] = ss_linear_to_srgb(ss_luma(p[0], p[1], p[2]));

        /* Counted BEFORE clamping, on the colour channels only: this is the
         * number the exposure warning needs, and it is only true if it is
         * measured where values can still be out of range. */
        for (c = 0; c < 3; c++) {
            if (v[c] <= 0.0f) { h->clip_lo++; break; }
        }
        for (c = 0; c < 3; c++) {
            if (v[c] >= 1.0f) { h->clip_hi++; break; }
        }

        for (c = 0; c < 4; c++) {
            b = (int)(ss_clampf(v[c], 0.0f, 1.0f) * 255.0f + 0.5f);
            switch (c) {
            case 0: h->r[b]++; break;
            case 1: h->g[b]++; break;
            case 2: h->b[b]++; break;
            default: h->l[b]++; break;
            }
        }
    }
}
